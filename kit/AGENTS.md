# Writing sketches for Flick

This file is for AI coding assistants (Claude Code, Copilot, Cursor, Codex, ...) working in this folder. Read it before writing or changing a sketch.

## What this is

The board runs **Baseline**: precompiled firmware with Arduino-ESP32 3.3, WiFi, BLE, storage and the rest already inside. A **sketch** is small native code that calls into Baseline through a table of functions (the ABI). Sketches build in about a second and upload in under a second.

You write ordinary Arduino code (`setup()`, `loop()`, `Serial`, `WiFi`, `Wire`, ...) and build it with `tools/flick.py`, not with the Arduino IDE.

## Commands

All commands run from this folder.

```
python tools/flick.py upload my_sketch -m      build for the plugged-in board, upload, then show serial output
python tools/flick.py build my_sketch -t TGT   build only (TGT: python tools/flick.py targets)
python tools/flick.py new my_sketch            create a sketch folder
python tools/flick.py monitor                  serial console
python tools/flick.py info                     Baseline version, ABI, board (target), running sketch, last crash
python tools/flick.py lib search WORDS         search the Arduino Library Manager
python tools/flick.py lib install "NAME"       install a library into libraries/
```

- `upload` asks the board which target it is and builds for it.
- After a change, run `upload` and read the serial output. That is the test.
- **Never** flash the board with the Arduino IDE, esptool or PlatformIO: that erases Baseline. If Baseline is missing, see README.md.

## A sketch

A sketch is a folder named like its main file: `my_sketch/my_sketch.ino`.
- **More sources:** `.cpp`, `.c` and `.h` files in the folder are built too. `.ino` files get prototypes added, as in Arduino.
- **Libraries:** a `libraries/` subfolder works like an Arduino sketchbook's.
- **`sketch.cfg` (optional):** `name = ...`, `stack = 8192` (bytes, for `setup()`/`loop()`) and `target = ...` (for `build`).
- **Includes:** `baseline.h` is included automatically. `#include <Arduino.h>`, `<WiFi.h>`, `<Wire.h>`, `<HTTPClient.h>`, `<Preferences.h>`, `<LittleFS.h>`, `<BLEDevice.h>`, `<esp_now.h>` and the like all work.
- **Secrets:** WiFi credentials and keys go in `secrets.cfg` (copy `secrets.cfg.example`; it is git-ignored). Each `KEY = value` becomes `#define KEY "value"`. Write `#ifdef WIFI_SSID` fallbacks, and never put credentials in the sketch.
- **Board pins:** `LED_BUILTIN`, `RGB_BUILTIN`, `SDA`, `SCL`, `A0`... and `BL_BOARD_*` / `BL_TARGET_NAME` come from `targets/<target>/sdk/bl_board.h`. Use those names rather than raw pin numbers.

## Rules: what Baseline does not do

1. **Only what Baseline provides.** Sketches cannot include ESP-IDF headers (`driver/gpio.h`, `esp_wifi.h`, FreeRTOS internals, ...) or call functions outside the SDK. The full list:
   - `docs/ARDUINO_COMPAT.md`: Arduino APIs, supported and not;
   - `docs/API.md`: the raw `bl_*` API;
   - `sdk/include/`: the headers.

   If a build fails with an undefined reference, the call is not in Baseline. Use a supported alternative and say so; do not try to add it.
2. **No C++ exceptions or RTTI** (`try`/`throw`, `dynamic_cast`, `typeid`).
3. **No `<ctype.h>` macros yet** (`isdigit`, `isalpha`...). Compare characters directly (`c >= '0' && c <= '9'`).
4. **Global and static data is limited to 32 KB** (64 KB on the ESP32-S3); the code to 512 KB (256 KB on 4 MB boards). Allocate big buffers with `malloc`/`new` in `setup()`. On boards with PSRAM, `ps_malloc` allocates there.
5. **`loop()` must return, and nothing may block for 5 s without a `delay()`:** the watchdog restarts the board. Calls such as WiFi connects and HTTP requests handle this themselves.
6. **Callbacks run on the sketch task** (interrupts, timers, WiFi/BLE events, `Ticker`): they arrive between `loop()` calls, not in interrupt context. Keep them short; don't busy-wait for them inside `loop()` without `delay()`.
7. **Library compatibility:** Arduino libraries that use only Arduino APIs (most I2C/SPI sensor and display libraries, ArduinoJson, ...) work. Libraries that call ESP-IDF directly or need exceptions do not.

## When things go wrong

- **Crashes roll back:** a new sketch that crashes on its first run is replaced by the previous one, and `python tools/flick.py info` shows `crash="..."` with the task and the program counter. Several crashes in a row put the board in safe mode (no sketch runs); upload a fixed sketch.
- **Missing library header:** `python tools/flick.py lib search <words>`, then `lib install "<exact name>"`.
- **Upload fails:** run `python tools/flick.py info`. If it doesn't answer, Baseline is not on the board (README.md).

## Over the air

- **Push from the PC:** a sketch that calls `ArduinoOTA.begin()` (see `examples/wifi_ota`) accepts uploads with `python tools/flick.py upload my_sketch --host baseline`.
- **Pull from a web server:** the board can download its own updates with `BaselineUpdate`.
- **Baseline too:** both work for Baseline as well. See `docs/OTA.md` and `examples/ota_pull_*`.

## Examples

`examples/` has a working sketch for most features: `blink`, `wifi_http_get`, `mqtt_basic`, `ble_server`, `sd_card`, `littlefs_files`, `fuel_gauge`, `deep_sleep`, `wifi_ota`, `selftest`, ... Start from the closest one.
