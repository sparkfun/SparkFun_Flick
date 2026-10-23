#!/usr/bin/env bash
# Build Baseline for the given targets on Linux, inside Espressif's ESP-IDF
# image (espressif/idf:v5.5.5). Used by .github/workflows/*.yml; to run it
# locally:
#   docker run --rm -v "$PWD:/work" -w /work -v flick-arduino15:/root/.arduino15 \
#       espressif/idf:v5.5.5 tools/ci/build_baseline.sh thing_plus_esp32c6
# Output: dist/<target>/baseline-<target>.bin and .bsu
set -euo pipefail

ARDUINO_ESP32_VERSION="${ARDUINO_ESP32_VERSION:-3.3.11}"
ESP32_INDEX="https://espressif.github.io/arduino-esp32/package_esp32_index.json"

# Arduino-ESP32 as an ESP-IDF component: flick takes the core and its
# precompiled libraries from ~/.arduino15 (cached between CI runs).
if [ ! -d "$HOME/.arduino15/packages/esp32/hardware/esp32/$ARDUINO_ESP32_VERSION" ]; then
    if ! command -v arduino-cli >/dev/null; then
        curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=/usr/local/bin sh
    fi
    arduino-cli core update-index --additional-urls "$ESP32_INDEX"
    arduino-cli core install "esp32:esp32@$ARDUINO_ESP32_VERSION" --additional-urls "$ESP32_INDEX"
fi

for target in "$@"; do
    echo "::group::Baseline for $target"
    python3 tools/flick.py baseline build -t "$target"
    echo "::endgroup::"
done
