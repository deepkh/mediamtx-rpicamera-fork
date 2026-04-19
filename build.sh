#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

Build mediamtx-rpicamera on a Raspberry Pi running Debian 13 / Raspberry Pi OS Trixie.

Options:
  --external-libcamera  Build against the system libcamera instead of the bundled fallback.
  --skip-packages       Do not install apt packages.
  --clean               Remove previous build artifacts before configuring.
  -h, --help            Show this help.
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PREFIX_DIR="${SCRIPT_DIR}/prefix"

INSTALL_PACKAGES=1
USE_EXTERNAL_LIBCAMERA=0
CLEAN_BUILD=0

while (($# > 0)); do
    case "$1" in
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

main() {
    cd "${SCRIPT_DIR}"

    check_environment

    if ((INSTALL_PACKAGES)); then
        install_packages
    fi

    if ((CLEAN_BUILD)); then
        rm -rf "${BUILD_DIR}" "${PREFIX_DIR}"
    fi

    configure_build

    rm -rf "${PREFIX_DIR}"
    DESTDIR="${PREFIX_DIR}" ninja -C "${BUILD_DIR}" install

    if [[ "$(gcc -dumpmachine)" == "aarch64-linux-gnu" ]]; then
        echo "Build complete: ${BUILD_DIR}/mtxrpicam_64/mtxrpicam"
    else
        echo "Build complete: ${BUILD_DIR}/mtxrpicam_32/mtxrpicam"
    fi
}

main "$@"
