#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64.h"
#include "parameters.h"

static char errbuf[256];

static const char *dump_str(const char *val) {
    return (val != NULL) ? val : "(null)";
}

static void dump_window(const char *name, const window_t *window) {
    if (window == NULL) {
        printf("%s: (null)\n", name);
        return;
    }

    printf("%s: x=%f y=%f width=%f height=%f\n", name, window->x, window->y,
           window->width, window->height);
}

static void dump_sensor_mode(const char *name, const sensor_mode_t *mode) {
    if (mode == NULL) {
        printf("%s: (null)\n", name);
        return;
    }

    printf("%s: width=%u height=%u bit_depth=%u packed=%d\n", name, mode->width,
           mode->height, mode->bit_depth, mode->packed);
}

static void dump_parameters(const parameters_t *params) {
    static int count = 0;
    if (count++ % 10 != 0) {
        return;
    }
    printf("parameters_unserialize params:\n");
    printf("  log_level: %s\n", dump_str(params->log_level));
    printf("  camera_id: %u\n", params->camera_id);
    printf("  width: %u\n", params->width);
    printf("  height: %u\n", params->height);
    printf("  h_flip: %d\n", params->h_flip);
    printf("  v_flip: %d\n", params->v_flip);
    printf("  brightness: %f\n", params->brightness);
    printf("  contrast: %f\n", params->contrast);
    printf("  saturation: %f\n", params->saturation);
    printf("  sharpness: %f\n", params->sharpness);
    printf("  exposure: %s\n", dump_str(params->exposure));
    printf("  awb: %s\n", dump_str(params->awb));
    printf("  awb_gain_red: %f\n", params->awb_gain_red);
    printf("  awb_gain_blue: %f\n", params->awb_gain_blue);
    printf("  denoise: %s\n", dump_str(params->denoise));
    printf("  shutter: %u\n", params->shutter);
    printf("  metering: %s\n", dump_str(params->metering));
    printf("  gain: %f\n", params->gain);
    printf("  ev: %f\n", params->ev);
    printf("  ");
    dump_window("roi", params->roi);
    printf("  hdr: %d\n", params->hdr);
    printf("  tuning_file: %s\n", dump_str(params->tuning_file));
    printf("  ");
    dump_sensor_mode("mode", params->mode);
    printf("  fps: %f\n", params->fps);
    printf("  af_mode: %s\n", dump_str(params->af_mode));
    printf("  af_range: %s\n", dump_str(params->af_range));
    printf("  af_speed: %s\n", dump_str(params->af_speed));
    printf("  lens_position: %f\n", params->lens_position);
    printf("  ");
    dump_window("af_window", params->af_window);
    printf("  flicker_period: %u\n", params->flicker_period);
    printf("  text_overlay_enable: %d\n", params->text_overlay_enable);
    printf("  text_overlay: %s\n", dump_str(params->text_overlay));
    printf("  codec: %s\n", dump_str(params->codec));
    printf("  idr_period: %u\n", params->idr_period);
    printf("  bitrate: %u\n", params->bitrate);
    printf("  hardware_h264_profile: %s\n",
           dump_str(params->hardware_h264_profile));
    printf("  hardware_h264_level: %s\n",
           dump_str(params->hardware_h264_level));
    printf("  software_h264_profile: %s\n",
           dump_str(params->software_h264_profile));
    printf("  software_h264_level: %s\n",
           dump_str(params->software_h264_level));
    printf("  secondary_width: %u\n", params->secondary_width);
    printf("  secondary_height: %u\n", params->secondary_height);
    printf("  secondary_fps: %f\n", params->secondary_fps);
    printf("  secondary_mjpeg_quality: %u\n", params->secondary_mjpeg_quality);
    printf("  buffer_count: %u\n", params->buffer_count);
}

static void set_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(errbuf, 256, format, args);
}

const char *parameters_get_error() { return errbuf; }

bool parameters_unserialize(const uint8_t *buf, size_t buf_size,
                            parameters_t **params) {
    *params = malloc(sizeof(parameters_t));
    memset(*params, 0, sizeof(parameters_t));

    char *copy = malloc(buf_size + 1);
    memcpy(copy, buf, buf_size);
    copy[buf_size] = 0x00;
    char *ptr = copy;
    char *entry;

    while ((entry = strsep(&ptr, " ")) != NULL) {
        char *key = strsep(&entry, ":");
        char *val = strsep(&entry, ":");

        if (strcmp(key, "LogLevel") == 0) {
            (*params)->log_level = base64_decode(val);
        } else if (strcmp(key, "CameraID") == 0) {
            (*params)->camera_id = atoi(val);
        } else if (strcmp(key, "Width") == 0) {
            (*params)->width = atoi(val);
        } else if (strcmp(key, "Height") == 0) {
            (*params)->height = atoi(val);
        } else if (strcmp(key, "HFlip") == 0) {
            (*params)->h_flip = (strcmp(val, "1") == 0);
        } else if (strcmp(key, "VFlip") == 0) {
            (*params)->v_flip = (strcmp(val, "1") == 0);
        } else if (strcmp(key, "Brightness") == 0) {
            (*params)->brightness = atof(val);
        } else if (strcmp(key, "Contrast") == 0) {
            (*params)->contrast = atof(val);
        } else if (strcmp(key, "Saturation") == 0) {
            (*params)->saturation = atof(val);
        } else if (strcmp(key, "Sharpness") == 0) {
            (*params)->sharpness = atof(val);
        } else if (strcmp(key, "Exposure") == 0) {
            (*params)->exposure = base64_decode(val);
        } else if (strcmp(key, "AWB") == 0) {
            (*params)->awb = base64_decode(val);
        } else if (strcmp(key, "AWBGainRed") == 0) {
            (*params)->awb_gain_red = atof(val);
        } else if (strcmp(key, "AWBGainBlue") == 0) {
            (*params)->awb_gain_blue = atof(val);
        } else if (strcmp(key, "Denoise") == 0) {
            (*params)->denoise = base64_decode(val);
        } else if (strcmp(key, "Shutter") == 0) {
            (*params)->shutter = atoi(val);
        } else if (strcmp(key, "Metering") == 0) {
            (*params)->metering = base64_decode(val);
        } else if (strcmp(key, "Gain") == 0) {
            (*params)->gain = atof(val);
        } else if (strcmp(key, "EV") == 0) {
            (*params)->ev = atof(val);
        } else if (strcmp(key, "ROI") == 0) {
            char *decoded_val = base64_decode(val);
            if (strlen(decoded_val) != 0) {
                (*params)->roi = malloc(sizeof(window_t));
                bool ok = window_load(decoded_val, (*params)->roi);
                if (!ok) {
                    set_error("invalid ROI");
                    free(decoded_val);
                    goto failed;
                }
            }
            free(decoded_val);
        } else if (strcmp(key, "HDR") == 0) {
            (*params)->hdr = (strcmp(val, "1") == 0);
        } else if (strcmp(key, "TuningFile") == 0) {
            (*params)->tuning_file = base64_decode(val);
        } else if (strcmp(key, "Mode") == 0) {
            char *decoded_val = base64_decode(val);
            if (strlen(decoded_val) != 0) {
                (*params)->mode = malloc(sizeof(sensor_mode_t));
                bool ok = sensor_mode_load(decoded_val, (*params)->mode);
                if (!ok) {
                    set_error("invalid sensor mode");
                    free(decoded_val);
                    goto failed;
                }
            }
            free(decoded_val);
        } else if (strcmp(key, "FPS") == 0) {
            (*params)->fps = atof(val);
        } else if (strcmp(key, "AfMode") == 0) {
            (*params)->af_mode = base64_decode(val);
        } else if (strcmp(key, "AfRange") == 0) {
            (*params)->af_range = base64_decode(val);
        } else if (strcmp(key, "AfSpeed") == 0) {
            (*params)->af_speed = base64_decode(val);
        } else if (strcmp(key, "LensPosition") == 0) {
            (*params)->lens_position = atof(val);
        } else if (strcmp(key, "AfWindow") == 0) {
            char *decoded_val = base64_decode(val);
            if (strlen(decoded_val) != 0) {
                (*params)->af_window = malloc(sizeof(window_t));
                bool ok = window_load(decoded_val, (*params)->af_window);
                if (!ok) {
                    set_error("invalid AfWindow");
                    free(decoded_val);
                    goto failed;
                }
            }
            free(decoded_val);
        } else if (strcmp(key, "FlickerPeriod") == 0) {
            (*params)->flicker_period = atoi(val);
        } else if (strcmp(key, "TextOverlayEnable") == 0) {
            (*params)->text_overlay_enable = (strcmp(val, "1") == 0);
        } else if (strcmp(key, "TextOverlay") == 0) {
            (*params)->text_overlay = base64_decode(val);
        } else if (strcmp(key, "Codec") == 0) {
            (*params)->codec = base64_decode(val);
        } else if (strcmp(key, "IDRPeriod") == 0) {
            (*params)->idr_period = atoi(val);
        } else if (strcmp(key, "Bitrate") == 0) {
            (*params)->bitrate = atoi(val);
        } else if (strcmp(key, "HardwareH264Profile") == 0) {
            (*params)->hardware_h264_profile = base64_decode(val);
        } else if (strcmp(key, "HardwareH264Level") == 0) {
            (*params)->hardware_h264_level = base64_decode(val);
        } else if (strcmp(key, "SoftwareH264Profile") == 0) {
            (*params)->software_h264_profile = base64_decode(val);
        } else if (strcmp(key, "SoftwareH264Level") == 0) {
            (*params)->software_h264_level = base64_decode(val);
        } else if (strcmp(key, "SecondaryWidth") == 0) {
            (*params)->secondary_width = atoi(val);
        } else if (strcmp(key, "SecondaryHeight") == 0) {
            (*params)->secondary_height = atoi(val);
        } else if (strcmp(key, "SecondaryFPS") == 0) {
            (*params)->secondary_fps = atof(val);
        } else if (strcmp(key, "SecondaryMJPEGQuality") == 0) {
            (*params)->secondary_mjpeg_quality = atoi(val);
        }
    }

    free(copy);

    (*params)->buffer_count = 3;
#if 1
    dump_parameters(*params);
#endif
    return true;

failed:
    free(copy);
    parameters_destroy(*params);

    return false;
}

void parameters_destroy(parameters_t *params) {
    if (params->exposure != NULL) {
        free(params->exposure);
    }
    if (params->awb != NULL) {
        free(params->awb);
    }
    if (params->denoise != NULL) {
        free(params->denoise);
    }
    if (params->metering != NULL) {
        free(params->metering);
    }
    if (params->roi != NULL) {
        free(params->roi);
    }
    if (params->tuning_file != NULL) {
        free(params->tuning_file);
    }
    if (params->mode != NULL) {
        free(params->mode);
    }
    if (params->af_mode != NULL) {
        free(params->af_mode);
    }
    if (params->af_range != NULL) {
        free(params->af_range);
    }
    if (params->af_speed != NULL) {
        free(params->af_speed);
    }
    if (params->af_window != NULL) {
        free(params->af_window);
    }
    if (params->text_overlay != NULL) {
        free(params->text_overlay);
    }
    if (params->codec != NULL) {
        free(params->codec);
    }
    free(params);
}
