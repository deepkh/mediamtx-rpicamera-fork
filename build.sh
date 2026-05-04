#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

Build mediamtx-rpicamera on a Raspberry Pi running Debian 13 / Raspberry Pi OS Trixie.

Options:
  --build-camera       Rebuild only repo source files with the existing Meson/Ninja setup.
  --build-fake-pipe-reader
                       Build only the fake PIPE_CONF_FD / PIPE_VIDEO_FD test harness.
  --install            Stage a portable runtime bundle under ./dst.
  --external-libcamera  Build against the system libcamera instead of the bundled fallback.
  --skip-packages       Do not install apt packages.
  --clean               Remove previous build artifacts before configuring.
  -h, --help            Show this help.
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PREFIX_DIR="${SCRIPT_DIR}/prefix"
DST_DIR="${SCRIPT_DIR}/dst"

INSTALL_PACKAGES=1
USE_EXTERNAL_LIBCAMERA=0
CLEAN_BUILD=0
BUILD_CAMERA_ONLY=0
BUILD_FAKE_PIPE_READER_ONLY=0
INSTALL_BUNDLE=0

while (($# > 0)); do
    case "$1" in
        --build-camera)
            BUILD_CAMERA_ONLY=1
            ;;
        --build-fake-pipe-reader)
            BUILD_FAKE_PIPE_READER_ONLY=1
            ;;
        --install)
            INSTALL_BUNDLE=1
            ;;
        --external-libcamera)
            USE_EXTERNAL_LIBCAMERA=1
            ;;
        --skip-packages)
            INSTALL_PACKAGES=0
            ;;
        --clean)
            CLEAN_BUILD=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

run_as_root() {
    if [[ "${EUID}" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

check_environment() {
    local arch
    arch="$(dpkg --print-architecture)"
    if [[ "${arch}" != "arm64" && "${arch}" != "armhf" ]]; then
        echo "Warning: expected a Raspberry Pi architecture, found '${arch}'." >&2
    fi

    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        if [[ "${ID:-}" != "debian" && "${ID_LIKE:-}" != *"debian"* && "${NAME:-}" != *"Raspberry Pi OS"* ]]; then
            echo "Warning: this script was written for Debian-based systems, found '${PRETTY_NAME:-unknown}'." >&2
        fi
        if [[ "${VERSION_ID:-}" != "13" && "${VERSION_CODENAME:-}" != "trixie" ]]; then
            echo "Warning: upstream documents Raspberry Pi OS Trixie / Debian 13 support; found '${PRETTY_NAME:-unknown}'." >&2
        fi
    fi

    if [[ -r /proc/device-tree/model ]]; then
        local model
        model="$(tr -d '\0' </proc/device-tree/model)"
        if [[ "${model}" != *"Raspberry Pi 5"* ]]; then
            echo "Warning: expected Raspberry Pi 5, found '${model}'." >&2
        fi
    else
        echo "Warning: could not verify Raspberry Pi hardware model." >&2
    fi
}

install_packages() {
    local packages=(
        g++
        xxd
        wget
        git
        cmake
        meson
        ninja-build
        pkg-config
        python3-jinja2
        python3-yaml
        python3-ply
        ca-certificates
    )

    if ((USE_EXTERNAL_LIBCAMERA)); then
        packages+=(libcamera-dev)
    fi

    run_as_root apt-get update
    run_as_root apt-get install -y "${packages[@]}"
}

configure_build() {
    local meson_args=()

    if ((USE_EXTERNAL_LIBCAMERA)); then
        meson_args+=(--wrap-mode=default)
    fi

    if [[ -d "${BUILD_DIR}" ]]; then
        meson setup --wipe "${meson_args[@]}" "${BUILD_DIR}"
    else
        meson setup "${meson_args[@]}" "${BUILD_DIR}"
    fi
}

print_output_path() {
    if [[ "$(gcc -dumpmachine)" == "aarch64-linux-gnu" ]]; then
        echo "Build complete: ${BUILD_DIR}/mtxrpicam_64/mtxrpicam"
    else
        echo "Build complete: ${BUILD_DIR}/mtxrpicam_32/mtxrpicam"
    fi
}

build_camera() {
    if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
        echo "Existing build directory not found: ${BUILD_DIR}" >&2
        echo "Run ./build.sh once before using --build-camera." >&2
        exit 1
    fi

    rm -rf "${PREFIX_DIR}"
    DESTDIR="${PREFIX_DIR}" ninja -C "${BUILD_DIR}" mtxrpicam install
    print_output_path
}

build_fake_pipe_reader() {
    if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
        echo "Existing build directory not found: ${BUILD_DIR}" >&2
        echo "Run ./build.sh once before using --build-fake-pipe-reader." >&2
        exit 1
    fi

    meson setup --reconfigure "${BUILD_DIR}"
    ninja -C "${BUILD_DIR}" fake_pipe_reader
    echo "Build complete: ${BUILD_DIR}/fake_pipe_reader"
}

copy_required() {
    local src="$1"
    local dst="$2"

    if [[ ! -e "${src}" ]]; then
        echo "Required file not found: ${src}" >&2
        exit 1
    fi

    mkdir -p "${dst}"
    cp -a "${src}" "${dst}/"
}

install_bundle() {
    if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
        configure_build
    else
        meson setup --reconfigure "${BUILD_DIR}"
    fi

    ninja -C "${BUILD_DIR}" mtxrpicam fake_pipe_reader

    rm -rf "${DST_DIR}"
    mkdir -p "${DST_DIR}"

    copy_required "${BUILD_DIR}/mtxrpicam" "${DST_DIR}"
    copy_required "${BUILD_DIR}/fake_pipe_reader" "${DST_DIR}"

    mkdir -p \
        "${DST_DIR}/build/subprojects/libcamera/src/libcamera" \
        "${DST_DIR}/build/subprojects/libcamera/src/libcamera/base" \
        "${DST_DIR}/build/subprojects/libcamera/src/ipa/rpi/pisp" \
        "${DST_DIR}/build/subprojects/libcamera/src/ipa/rpi/vc4" \
        "${DST_DIR}/build/subprojects/libcamera/source/src/ipa/rpi" \
        "${DST_DIR}/build/subprojects/libcamera/source/src/libcamera/pipeline/rpi" \
        "${DST_DIR}/build/subprojects/libpisp/src/libpisp/backend" \
        "${DST_DIR}/share/libcamera/ipa/rpi"

    cp -a "${BUILD_DIR}/subprojects/libcamera/src/libcamera"/libcamera.so* \
        "${DST_DIR}/build/subprojects/libcamera/src/libcamera/"
    cp -a "${BUILD_DIR}/subprojects/libcamera/src/libcamera/base"/libcamera-base.so* \
        "${DST_DIR}/build/subprojects/libcamera/src/libcamera/base/"
    cp -a "${BUILD_DIR}/subprojects/libcamera/src/ipa/rpi/pisp"/ipa_rpi_pisp.so* \
        "${DST_DIR}/build/subprojects/libcamera/src/ipa/rpi/pisp/"
    cp -a "${BUILD_DIR}/subprojects/libcamera/src/ipa/rpi/vc4"/ipa_rpi_vc4.so* \
        "${DST_DIR}/build/subprojects/libcamera/src/ipa/rpi/vc4/"
    cp -a "${BUILD_DIR}/subprojects/libpisp/src/libpisp/backend/backend_default_config.json" \
        "${DST_DIR}/build/subprojects/libpisp/src/libpisp/backend/"

    cp -a "${SCRIPT_DIR}/subprojects/libcamera/src/ipa/rpi/pisp/data" \
        "${DST_DIR}/build/subprojects/libcamera/source/src/ipa/rpi/pisp"
    cp -a "${SCRIPT_DIR}/subprojects/libcamera/src/ipa/rpi/vc4/data" \
        "${DST_DIR}/build/subprojects/libcamera/source/src/ipa/rpi/vc4"
    cp -a "${SCRIPT_DIR}/subprojects/libcamera/src/libcamera/pipeline/rpi/pisp/data" \
        "${DST_DIR}/build/subprojects/libcamera/source/src/libcamera/pipeline/rpi/pisp"
    cp -a "${SCRIPT_DIR}/subprojects/libcamera/src/libcamera/pipeline/rpi/vc4/data" \
        "${DST_DIR}/build/subprojects/libcamera/source/src/libcamera/pipeline/rpi/vc4"

    cp -a "${SCRIPT_DIR}/subprojects/libcamera/src/ipa/rpi/pisp/data" \
        "${DST_DIR}/share/libcamera/ipa/rpi/pisp"
    cp -a "${SCRIPT_DIR}/subprojects/libcamera/src/ipa/rpi/vc4/data" \
        "${DST_DIR}/share/libcamera/ipa/rpi/vc4"

    cat >"${DST_DIR}/README_RUN.txt" <<'EOF'
Run from this directory:

  ./fake_pipe_reader

fake_pipe_reader sets PIPE_CONF_FD, PIPE_VIDEO_FD, LD_LIBRARY_PATH and
LIBPISP_BE_CONFIG_FILE before launching ./mtxrpicam.
EOF

    echo "Install bundle staged: ${DST_DIR}"

    local answer
    while true; do
        read -r -p "Create mtxrpicam.tar.gz from dst/? [y/n] " answer
        case "${answer}" in
            y|Y)
                tar -C "${SCRIPT_DIR}" -czf "${SCRIPT_DIR}/mtxrpicam.tar.gz" dst
                echo "Archive created: ${SCRIPT_DIR}/mtxrpicam.tar.gz"
                break
                ;;
            n|N)
                echo "Archive skipped."
                break
                ;;
            *)
                echo "Please answer y or n."
                ;;
        esac
    done
}

main() {
    cd "${SCRIPT_DIR}"

    check_environment

    if ((BUILD_CAMERA_ONLY)); then
        build_camera
        exit 0
    fi

    if ((BUILD_FAKE_PIPE_READER_ONLY)); then
        build_fake_pipe_reader
        exit 0
    fi

    if ((INSTALL_BUNDLE)); then
        install_bundle
        exit 0
    fi

    if ((INSTALL_PACKAGES)); then
        install_packages
    fi

    if ((CLEAN_BUILD)); then
        rm -rf "${BUILD_DIR}" "${PREFIX_DIR}"
    fi

    configure_build

    rm -rf "${PREFIX_DIR}"
    DESTDIR="${PREFIX_DIR}" ninja -C "${BUILD_DIR}" install
    print_output_path
}

main "$@"
