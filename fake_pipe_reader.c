#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped = 0;

static void on_signal(int sig) {
    (void)sig;
    stopped = 1;
}

static bool write_full(int fd, const void *buf, size_t size) {
    const uint8_t *ptr = (const uint8_t *)buf;

    while (size > 0) {
        ssize_t n = write(fd, ptr, size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        ptr += n;
        size -= n;
    }

    return true;
}

static bool read_full(int fd, void *buf, size_t size) {
    uint8_t *ptr = (uint8_t *)buf;

    while (size > 0) {
        ssize_t n = read(fd, ptr, size);
        if (n < 0) {
            if (errno == EINTR && !stopped) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        ptr += n;
        size -= n;
    }

    return true;
}

static void chdir_to_executable_dir(const char *argv0) {
    char *path = realpath(argv0, NULL);
    if (path == NULL) {
        path = realpath("/proc/self/exe", NULL);
    }
    if (path == NULL) {
        return;
    }

    char *slash = strrchr(path, '/');
    if (slash != NULL) {
        *slash = 0;
        chdir(path);
    }

    free(path);
}

static const char *default_mtxrpicam_path(void) {
    if (access("./mtxrpicam", X_OK) == 0) {
        return "./mtxrpicam";
    }
    return "./build/mtxrpicam";
}

static const char *default_tuning_file(void) {
    const char *env = getenv("MTXRPICAM_TUNING_FILE");
    if (env != NULL && env[0] != 0) {
        return env;
    }

    if (access("./share/libcamera/ipa/rpi/pisp/imx708_wide.json", R_OK) == 0) {
        return "./share/libcamera/ipa/rpi/pisp/imx708_wide.json";
    }

    return "/usr/share/libcamera/ipa/rpi/pisp/imx708_wide.json";
}

static void prepend_ld_library_path(const char *path) {
    const char *old_path = getenv("LD_LIBRARY_PATH");
    char new_path[4096];

    if (old_path != NULL && old_path[0] != 0) {
        snprintf(new_path, sizeof(new_path), "%s:%s", path, old_path);
    } else {
        snprintf(new_path, sizeof(new_path), "%s", path);
    }

    setenv("LD_LIBRARY_PATH", new_path, 1);
}

static void set_libpisp_config_file(void) {
    static const char *candidates[] = {
        "./build/subprojects/libpisp/src/libpisp/backend/"
        "backend_default_config.json",
        "./subprojects/libpisp/src/libpisp/backend/backend_default_config.json",
    };
    char *cwd = getcwd(NULL, 0);

    if (cwd == NULL) {
        return;
    }

    for (unsigned int i = 0; i < sizeof(candidates) / sizeof(candidates[0]);
         i++) {
        if (access(candidates[i], R_OK) != 0) {
            continue;
        }

        char *path = malloc(strlen(cwd) + 1 + strlen(candidates[i]) + 1);
        if (path == NULL) {
            free(cwd);
            return;
        }

        sprintf(path, "%s/%s", cwd, candidates[i]);
        setenv("LIBPISP_BE_CONFIG_FILE", path, 1);
        free(path);
        break;
    }

    free(cwd);
}

static char *base64_encode(const char *src) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t src_len = strlen(src);
    if (src_len == 0) {
        char *out = malloc(5);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, "AA==", 5);
        return out;
    }

    size_t out_len = 4 * ((src_len + 2) / 3);
    char *out = malloc(out_len + 1);
    if (out == NULL) {
        return NULL;
    }

    size_t i = 0;
    size_t j = 0;
    while (i < src_len) {
        uint32_t a = (uint8_t)src[i++];
        uint32_t b = i < src_len ? (uint8_t)src[i++] : 0;
        uint32_t c = i < src_len ? (uint8_t)src[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = table[(triple >> 18) & 0x3f];
        out[j++] = table[(triple >> 12) & 0x3f];
        out[j++] = table[(triple >> 6) & 0x3f];
        out[j++] = table[triple & 0x3f];
    }

    size_t padding = (3 - (src_len % 3)) % 3;
    for (size_t n = 0; n < padding; n++) {
        out[out_len - 1 - n] = '=';
    }

    out[out_len] = 0;
    return out;
}

static bool append_raw(char **buf, size_t *len, size_t *cap, const char *text) {
    size_t text_len = strlen(text);
    if (*len + text_len + 1 > *cap) {
        size_t new_cap = *cap == 0 ? 1024 : *cap;
        while (*len + text_len + 1 > new_cap) {
            new_cap *= 2;
        }

        char *new_buf = realloc(*buf, new_cap);
        if (new_buf == NULL) {
            return false;
        }
        *buf = new_buf;
        *cap = new_cap;
    }

    memcpy(*buf + *len, text, text_len);
    *len += text_len;
    (*buf)[*len] = 0;
    return true;
}

static bool append_param(char **buf, size_t *len, size_t *cap, const char *key,
                         const char *value) {
    return append_raw(buf, len, cap, key) && append_raw(buf, len, cap, ":") &&
           append_raw(buf, len, cap, value) && append_raw(buf, len, cap, " ");
}

static bool append_b64_param(char **buf, size_t *len, size_t *cap,
                             const char *key, const char *value) {
    char *encoded = base64_encode(value);
    if (encoded == NULL) {
        return false;
    }

    bool ok = append_param(buf, len, cap, key, encoded);
    free(encoded);
    return ok;
}

static char *build_parameters(void) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    const char *tuning_file = default_tuning_file();

    bool ok =
        append_b64_param(&buf, &len, &cap, "LogLevel", "info") &&
        append_param(&buf, &len, &cap, "CameraID", "0") &&
        append_param(&buf, &len, &cap, "Width", "2304") &&
        append_param(&buf, &len, &cap, "Height", "1296") &&
        append_param(&buf, &len, &cap, "HFlip", "1") &&
        append_param(&buf, &len, &cap, "VFlip", "0") &&
        append_param(&buf, &len, &cap, "Brightness", "0.0") &&
        append_param(&buf, &len, &cap, "Contrast", "1.0") &&
        append_param(&buf, &len, &cap, "Saturation", "1.0") &&
        append_param(&buf, &len, &cap, "Sharpness", "1.0") &&
        append_b64_param(&buf, &len, &cap, "Exposure", "short") &&
        append_b64_param(&buf, &len, &cap, "AWB", "auto") &&
        append_param(&buf, &len, &cap, "AWBGainRed", "0.0") &&
        append_param(&buf, &len, &cap, "AWBGainBlue", "0.0") &&
        append_b64_param(&buf, &len, &cap, "Denoise", "cdn_hq") &&
        append_param(&buf, &len, &cap, "Shutter", "0") &&
        append_b64_param(&buf, &len, &cap, "Metering", "centre") &&
        append_param(&buf, &len, &cap, "Gain", "0.0") &&
        append_param(&buf, &len, &cap, "EV", "0.0") &&
        append_b64_param(&buf, &len, &cap, "ROI", "") &&
        append_param(&buf, &len, &cap, "HDR", "0") &&
        append_b64_param(&buf, &len, &cap, "TuningFile", tuning_file) &&
        append_b64_param(&buf, &len, &cap, "Mode", "") &&
        append_param(&buf, &len, &cap, "FPS", "60.0") &&
        append_b64_param(&buf, &len, &cap, "AfMode", "manual") &&
        append_b64_param(&buf, &len, &cap, "AfRange", "normal") &&
        append_b64_param(&buf, &len, &cap, "AfSpeed", "normal") &&
        append_param(&buf, &len, &cap, "LensPosition", "0.0") &&
        append_b64_param(&buf, &len, &cap, "AfWindow", "") &&
        append_param(&buf, &len, &cap, "FlickerPeriod", "0") &&
        append_param(&buf, &len, &cap, "TextOverlayEnable", "0") &&
        append_b64_param(&buf, &len, &cap, "TextOverlay",
                         "%Y-%m-%d %H:%M:%S - MediaMTX") &&
        append_b64_param(&buf, &len, &cap, "Codec", "auto") &&
        append_param(&buf, &len, &cap, "IDRPeriod", "60") &&
        append_param(&buf, &len, &cap, "Bitrate", "10000000") &&
        append_b64_param(&buf, &len, &cap, "HardwareH264Profile", "main") &&
        append_b64_param(&buf, &len, &cap, "HardwareH264Level", "4.1") &&
        append_b64_param(&buf, &len, &cap, "SoftwareH264Profile", "baseline") &&
        append_b64_param(&buf, &len, &cap, "SoftwareH264Level", "4.1") &&
        append_param(&buf, &len, &cap, "SecondaryWidth", "0") &&
        append_param(&buf, &len, &cap, "SecondaryHeight", "0") &&
        append_param(&buf, &len, &cap, "SecondaryFPS", "0.0") &&
        append_param(&buf, &len, &cap, "SecondaryMJPEGQuality", "0");

    if (!ok) {
        free(buf);
        return NULL;
    }

    if (len > 0 && buf[len - 1] == ' ') {
        buf[len - 1] = 0;
    }
    return buf;
}

static bool write_packet(int fd, char kind, const char *payload) {
    uint32_t size = 1 + (uint32_t)strlen(payload);

    return write_full(fd, &size, sizeof(size)) &&
           write_full(fd, &kind, sizeof(kind)) &&
           write_full(fd, payload, strlen(payload));
}

static bool discard_packet_payload(int fd, uint32_t size) {
    uint8_t buf[64 * 1024];

    while (size > 0) {
        size_t chunk = size < sizeof(buf) ? size : sizeof(buf);
        if (!read_full(fd, buf, chunk)) {
            return false;
        }
        size -= (uint32_t)chunk;
    }

    return true;
}

static bool read_video_packet(int fd, char *kind, uint64_t *timestamp) {
    uint32_t size;

    if (!read_full(fd, &size, sizeof(size)) || size < 1) {
        return false;
    }

    if (!read_full(fd, kind, sizeof(*kind))) {
        return false;
    }
    size--;

    *timestamp = 0;
    if ((*kind == 'd' || *kind == 's') && size >= sizeof(*timestamp)) {
        if (!read_full(fd, timestamp, sizeof(*timestamp))) {
            return false;
        }
        size -= sizeof(*timestamp);
    }

    return discard_packet_payload(fd, size);
}

static void drain_video_pipe(int fd) {
    uint64_t frame_count = 0;
    double prev_frame_ts = 0.0;
    double diff = 0.0;
    int fps = 0;
    double diff_times[256] = {0};
    double diff_values[256] = {0};
    int fps_values[256] = {0};
    unsigned int diff_index = 0;
    unsigned int diff_count = 0;

    while (!stopped) {
        char kind;
        uint64_t packet_ts;

        if (!read_video_packet(fd, &kind, &packet_ts)) {
            break;
        }
        if (kind != 'd') {
            continue;
        }

        frame_count++;
        double now = (double)packet_ts / 1000000.0;
        if (prev_frame_ts > 0.0) {
            diff = now - prev_frame_ts;
            if (diff > 0.0) {
                fps = (int)((1.0 / diff) + 0.5);

                diff_times[diff_index] = now;
                diff_values[diff_index] = diff;
                fps_values[diff_index] = fps;
                diff_index = (diff_index + 1) %
                             (sizeof(diff_values) / sizeof(diff_values[0]));
                if (diff_count < sizeof(diff_values) / sizeof(diff_values[0])) {
                    diff_count++;
                }
            }
        }
        prev_frame_ts = now;

        if (frame_count % 10 == 0) {
            double avg_diff = 0.0;
            double avg_fps = 0.0;
            double min_diff = 0.0;
            double max_diff = 0.0;
            int min_fps = 0;
            int max_fps = 0;
            unsigned int avg_count = 0;

            for (unsigned int i = 0; i < diff_count; i++) {
                double sample_age = now - diff_times[i];

                if (sample_age >= 0.0 && sample_age <= 2.0 &&
                    diff_values[i] > 0.0 && fps_values[i] > 0) {
                    if (avg_count == 0 || diff_values[i] < min_diff) {
                        min_diff = diff_values[i];
                    }
                    if (avg_count == 0 || diff_values[i] > max_diff) {
                        max_diff = diff_values[i];
                    }
                    if (avg_count == 0 || fps_values[i] < min_fps) {
                        min_fps = fps_values[i];
                    }
                    if (avg_count == 0 || fps_values[i] > max_fps) {
                        max_fps = fps_values[i];
                    }
                    avg_diff += diff_values[i];
                    avg_fps += fps_values[i];
                    avg_count++;
                }
            }
            if (avg_count > 0) {
                avg_diff /= avg_count;
                avg_fps /= avg_count;
            }

            printf("Frame:%lu, Fps:%d AvgFps:%.1f/%d/%d ts:%.3f "
                   "Diff:%.3f AvgDiff:%.3f/%.3f/%.3f\n",
                   frame_count, fps, avg_fps, min_fps, max_fps, now, diff,
                   avg_diff, min_diff, max_diff);
            fflush(stdout);
        }
    }
}

int main(int argc, char **argv) {
    chdir_to_executable_dir(argv[0]);
    const char *mtxrpicam_path = argc >= 2 ? argv[1] : default_mtxrpicam_path();
    int conf_pipe[2];
    int video_pipe[2];

    if (pipe(conf_pipe) != 0 || pipe(video_pipe) != 0) {
        perror("pipe");
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        char conf_fd[32];
        char video_fd[32];

        close(conf_pipe[1]);
        close(video_pipe[0]);

        snprintf(conf_fd, sizeof(conf_fd), "%d", conf_pipe[0]);
        snprintf(video_fd, sizeof(video_fd), "%d", video_pipe[1]);
        setenv("PIPE_CONF_FD", conf_fd, 1);
        setenv("PIPE_VIDEO_FD", video_fd, 1);
        prepend_ld_library_path("./subprojects/libcamera/src/libcamera/base");
        prepend_ld_library_path("./subprojects/libcamera/src/libcamera");
        prepend_ld_library_path(
            "./build/subprojects/libcamera/src/libcamera/base");
        prepend_ld_library_path("./build/subprojects/libcamera/src/libcamera");
        set_libpisp_config_file();

        execl(mtxrpicam_path, mtxrpicam_path, NULL);
        perror("execl");
        _exit(127);
    }

    close(conf_pipe[0]);
    close(video_pipe[1]);

    char *params = build_parameters();
    if (params == NULL) {
        fprintf(stderr, "failed to build parameters\n");
        return 1;
    }

    if (!write_packet(conf_pipe[1], 'c', params)) {
        perror("write parameters");
        free(params);
        return 1;
    }
    free(params);

    drain_video_pipe(video_pipe[0]);

    write_packet(conf_pipe[1], 'e', "");
    close(conf_pipe[1]);
    close(video_pipe[0]);

    waitpid(pid, NULL, 0);
    return 0;
}
