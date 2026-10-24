#!/usr/bin/env bash
# Build Baseline for the given targets on Linux, inside Espressif's ESP-IDF
# image (espressif/idf:v5.5.5). Used by .github/workflows/*.yml; to run it
# locally:
#   docker run --rm -v "$PWD:/work" -w /work -v flick-arduino15:/root/.arduino15 \
#       espressif/idf:v5.5.5 tools/ci/build_baseline.sh thing_plus_esp32c6
# Output: dist/<target>/baseline-<target>.bin and .bsu
set -euo pipefail

ARDUINO_ESP32_VERSION="${ARDUINO_ESP32_VERSION:-3.3.11}"

# Arduino-ESP32 as an ESP-IDF component: flick takes the core and the chip's
# precompiled libraries from ~/.arduino15 (cached between CI runs). Only those
# are installed: the whole platform does not fit a runner's disk next to ESP-IDF.
python3 flick/tools/ci/fetch_arduino_esp32.py "$ARDUINO_ESP32_VERSION" "$@"

for target in "$@"; do
    echo "::group::Baseline for $target"
    python3 flick/tools/flick.py baseline build -t "$target"
    echo "::endgroup::"
done
