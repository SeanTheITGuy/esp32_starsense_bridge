#!/usr/bin/env bash
# Run from a normal PC terminal with internet and access to the ESP32 COM port.
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
toolchain_dir="$project_dir/.toolchains"
idf_dir="$toolchain_dir/esp-idf-v6.1"
export IDF_TOOLS_PATH="$toolchain_dir/espressif"

if (( $# > 1 )); then
    echo "Usage: bash tools/setup_hc05.sh [ESP32_COM_PORT]" >&2
    exit 2
fi
if (( $# == 1 )) && [[ ! -c "$1" ]]; then
    echo "Serial device is unavailable: $1" >&2
    exit 1
fi
for executable in git python3 cmake ninja; do
    command -v "$executable" >/dev/null || {
        echo "Missing prerequisite: $executable; see README Ubuntu prerequisites." >&2
        exit 1
    }
done
mkdir -p "$toolchain_dir"
if [[ ! -d "$idf_dir/.git" ]]; then
    git clone --branch v6.1 --depth 1 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git "$idf_dir"
fi
if [[ ! -f "$toolchain_dir/install-complete" ]]; then
    (cd "$idf_dir" && ./install.sh esp32s3)
    touch "$toolchain_dir/install-complete"
fi
# ESP-IDF's environment script may read unset shell variables.
set +u
source "$idf_dir/export.sh"
set -u
cd "$project_dir"

# Regenerate this dedicated bring-up configuration on each invocation so a
# previous enumeration build cannot select USB host in the HC-05 test image.
rm -f "$project_dir/sdkconfig.hc05" "$project_dir/sdkconfig.hc05.old"
idf.py -B build-hc05 -D IDF_TARGET=esp32s3 \
    -D SDKCONFIG="$project_dir/sdkconfig.hc05" \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.hc05.defaults" build

if (( $# == 1 )); then
    # Explicit chip selection prevents treating the handset as a flash target.
    idf.py -B build-hc05 -p "$1" flash monitor
else
    echo "Build complete. To build and flash, rerun with the ESP32 COM port:"
    echo "bash tools/setup_hc05.sh /dev/ttyACM0"
fi
