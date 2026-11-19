# Writing sketches for Flick

This file is for AI coding assistants (Claude Code, Copilot, Cursor, Codex, ...) working in this folder. Read it before writing or changing a sketch.

## What this is

The board runs **Baseline**: precompiled firmware with Arduino-ESP32 3.3, WiFi, BLE, storage and the rest already inside. A **sketch** is small native code that calls into Baseline through a table of functions (the ABI). Sketches build in about a second and upload in under a second.

You write ordinary Arduino code (`setup()`, `loop()`, `Serial`, `WiFi`, `Wire`, ...) and build it with `flick.py`, not with the Arduino IDE.

## Commands

All commands run from this folder.

```
python flick.py upload my_sketches/NAME -m       build for the plugged-in board, upload, then show serial output
python flick.py build my_sketches/NAME -t TGT    build only (TGT: python flick.py targets)
python flick.py new NAME                         create my_sketches/NAME/NAME.ino
python flick.py monitor                          serial console
python flick.py info                             Baseline version, ABI, board (target), running sketch, last crash
python flick.py lib search WORDS                 search the Arduino Library Manager
python flick.py lib install "NAME"               install a library into libraries/
```

- `upload` asks the board which target it is and builds for it.
- After a change, run `upload` and read the serial output. That is the test.
- **Never** flash the board with the Arduino IDE, esptool, picotool or PlatformIO, or copy another `.uf2` onto an RP2040/RP2350 board's drive: that erases Baseline. If Baseline is missing, the user flashes it from the Flick web page (https://sparkfun.github.io/SparkFun_Flick/, Chrome or Edge).

## A sketch

A sketch is a folder named like its main file: `my_sketches/plant_monitor/plant_monitor.ino`.
- **More sources:** `.cpp`, `.c` and `.h` files in the folder are built too. `.ino` files get prototypes added, as in Arduino.
- **Libraries:** a `libraries/` subfolder works like an Arduino sketchbook's.
- **`sketch.cfg` (optional):** `name = ...`, `stack = 8192` (bytes, for `setup()`/`loop()`) and `target = ...` (for `build`).
- **Includes:** `baseline.h` is included automatically. `#include <Arduino.h>`, `<WiFi.h>`, `<Wire.h>`, `<HTTPClient.h>`, `<Preferences.h>`, `<LittleFS.h>`, `<BLEDevice.h>`, `<esp_now.h>` and the like all work.
- **What a board has decides what exists.** `bl_target.h` defines `BL_PLATFORM_ESP32`, `BL_PLATFORM_TEENSY`, `BL_PLATFORM_RP2` or `BL_PLATFORM_APOLLO3`, and `BL_HAS_WIFI`, `BL_HAS_NET` (sockets and HTTP: WiFi, or Ethernet), `BL_HAS_TLS`, `BL_HAS_ETHERNET`, `BL_HAS_ESPNOW`, `BL_HAS_BLE`, `BL_HAS_LED_STRIP`, `BL_HAS_USB_HOST`, `BL_HAS_USB_AUDIO`, `BL_HAS_BT_CLASSIC`, `BL_HAS_TOUCH`, `BL_HAS_DAC`, `BL_HAS_USB_DEVICE`, `BL_HAS_AUDIO` for the hardware it has. The APIs of a missing feature are not declared, so using them is a compile error (flick adds a line saying why). Arduino-ESP32 names (`esp_*`, `ESP.`, `ledc*`, `ps_malloc`, `temperatureRead`, `analogReadMilliVolts`, `SD_MMC`, ...) exist only on the ESP32 family. Never fake a feature with a stub; to support several boards, use `#if defined(BL_HAS_WIFI)` etc. An example that needs a feature starts with `#if !defined(BL_HAS_BLE)` / `#error "This board has no Bluetooth LE."`.
- **Teensy 4.x boards** (`teensy40`, `teensy41`, `teensy_micromod`, and `*_hid` variants that are a USB keyboard/mouse/joystick instead of USB audio): `#include <Audio.h>` gives the Teensy Audio library (code exported by PJRC's Audio System Design Tool compiles as it is); `IntervalTimer`, `elapsedMillis`, `Serial1`-`Serial8`, `analogWriteFrequency()`/`analogWriteResolution()`, `extmem_malloc()`, `tempmonGetTemp()` and `SD.begin(BUILTIN_SDCARD)` (4.1) work, and `<imxrt.h>` (Teensy's register definitions) may be included. There is no WiFi or Bluetooth. Examples: `examples/08_teensy`. `#include <USBHost_t36.h>` drives the USB host port (keyboards, mice, joysticks, MIDI, USB-serial adapters, USB drives); `#include <NativeEthernet.h>` gives Ethernet on the Teensy 4.1 (EthernetClient/Server/UDP and HTTPClient, plain http:// only: no TLS); `#include <Snooze.h>` sleeps (deepSleep restarts the sketch on wake, like an ESP32); FastLED-style LED strips work on any pin; and Teensyduino's own libraries that drive the chip directly (FlexCAN_T4 for CAN, Encoder, ...) are found and compile into the sketch.
- **RP2040 / RP2350 boards** (`pro_micro_rp2040`, `thing_plus_rp2040`, `pro_micro_rp2350`, `thing_plus_rp2350`, and the Raspberry Pi Pico 2 / 2 W as `rp2350_generic_pico2` / `rp2350_generic_pico2w`): the same classic Arduino names as on the ESP32, plus `rgbLedWrite()` and FastLED. `Serial1`/`Serial2`, `Wire`/`Wire1` and `SPI` take any pins the chip allows (Baseline picks the controller from the pins). WiFi and BLE only on the Thing Plus - RP2350 and the Pico 2 W (`BL_HAS_WIFI`, `BL_HAS_BLE`). No ESP-NOW, Bluetooth Classic, touch, DAC or USB device mode, and no Arduino-ESP32 names (`esp_*`, `ESP.`, `ledc*`, `ps_malloc`); on the RP2350 boards `malloc()` of more than 4 KB uses the PSRAM by itself. A sketch for these boards must not call arduino-pico's own APIs (`rp2040.*`, `PIO`, `<hardware/...>`): they aren't in Baseline.
- **Artemis boards** (`redboard_artemis_atp`: SparkFun's Apollo3 Blue module): pin numbers are the pad numbers printed on the board; `A0`-`A7` are the ADC pads 16, 29, 11, 31, 32, 33, 34, 35. `Serial1` (UART1), `Wire`/`Wire1` and `SPI` take any pads the chip allows (one IOM block is either an I2C or an SPI bus); there is no `Serial2` (UART0 is the console). Sketch code is limited to 128 KB and LittleFS has 112 KB. Deep sleep restarts only the sketch: Baseline takes back its memory, files, buses and timers. Bluetooth LE works (`BL_HAS_BLE`: BLESerial, BLEDevice server, scan and client). No WiFi; no Arduino-ESP32 names (`esp_*`, `ESP.`, `ledc*`, `ps_malloc`), and no Mbed or Ambiq calls (`mbed::`, `am_hal_*`): they aren't in Baseline. Baseline sits behind SparkFun's SVL bootloader, which the web page and `flick baseline flash` use; an upload from the Arduino IDE replaces Baseline.
- **Secrets:** WiFi credentials and keys live in `~/.flick/secrets.cfg`, outside this folder. The user sets the WiFi network once with `python flick.py wifi "network" "password"` (or the VS Code task **Set WiFi network**); other keys with `python flick.py secrets --set KEY=value`. Each `KEY = value` becomes `#define KEY "value"`. Write `#ifdef WIFI_SSID` fallbacks, never put credentials in the sketch, and never print or read out the secrets file.
- **Board pins:** `LED_BUILTIN`, `RGB_BUILTIN`, `SDA`, `SCL`, `A0`... and `BL_BOARD_*` / `BL_TARGET_NAME` come from `flick/targets/<target>/sdk/bl_board.h`. Use those names rather than raw pin numbers.

## Rules: what Baseline does not do

1. **Only what Baseline provides.** Sketches cannot include ESP-IDF headers (`driver/gpio.h`, `esp_wifi.h`, FreeRTOS internals, ...) or call functions outside the SDK. The full list:
   - `flick/docs/ARDUINO_COMPAT.md`: Arduino APIs, supported and not;
   - `flick/docs/API.md`: the raw `bl_*` API;
   - `flick/sdk/include/`: the headers.

   If a build fails with an undefined reference, the call is not in Baseline. Use a supported alternative and say so; do not try to add it. (On Teensy 4.x, `<imxrt.h>` register access is allowed.)
2. **No C++ exceptions or RTTI** (`try`/`throw`, `dynamic_cast`, `typeid`).
3. **Global and static data is limited to 32 KB** (64 KB on the ESP32-S3, RP2350 and Artemis, 128 KB on Teensy 4.x); the code to 512 KB (256 KB on 4 MB boards, 128 KB on Artemis). Allocate big buffers with `malloc`/`new` in `setup()`. On boards with PSRAM, `ps_malloc` allocates there.
4. **`loop()` must return, and nothing may block for 5 s without a `delay()`:** the watchdog restarts the board. Calls such as WiFi connects and HTTP requests handle this themselves. **Exception:** an empty `while (1);` (or `for (;;);`) is the way to stop a sketch, as in plain Arduino: Baseline parks it (`flick info` shows `status="stopped"` and the source line), uploads keep working, and it is not a crash. Use it after a fatal error, e.g. `if (!sensor.begin()) { Serial.println("sensor not found"); while (1); }`. Only an *empty* loop counts; a loop that does work without `delay()` is still a hang.
5. **Callbacks run on the sketch task** (interrupts, timers, WiFi/BLE events, `Ticker`): they arrive between `loop()` calls, not in interrupt context. Keep them short; don't busy-wait for them inside `loop()` without `delay()`. **Except on Teensy 4.x:** `attachInterrupt()` and `IntervalTimer` handlers run in interrupt context, as on a plain Teensy; keep them short, make shared variables `volatile`, and use `noInterrupts()`/`interrupts()` around reads of several of them. `Ticker` still runs on the sketch task there.
6. **Library compatibility:** Arduino libraries that use only Arduino APIs (most I2C/SPI sensor and display libraries, ArduinoJson, ...) work. Libraries that call ESP-IDF directly or need exceptions do not.

## When things go wrong

- **Crashes roll back:** a new sketch that crashes on its first run is replaced by the previous one, and `python flick.py info` shows `crash="..."` with the task and the program counter. Several crashes in a row put the board in safe mode (no sketch runs); upload a fixed sketch.
- **Missing library header:** `python flick.py lib search <words>`, then `lib install "<exact name>"`.
- **Upload fails:** run `python flick.py info`. If it doesn't answer, Baseline is not on the board: send the user to https://sparkfun.github.io/SparkFun_Flick/.

## Over the air

- **Push from the PC:** a sketch that calls `ArduinoOTA.begin()` (see `examples/07_updates/01_wifi_ota`) accepts uploads with `python flick.py upload my_sketches/NAME --host baseline`.
- **Pull from a web server:** the board can download its own updates with `BaselineUpdate`.
- **Baseline too:** both work for Baseline as well. See `flick/docs/OTA.md` and `examples/07_updates/`.

## Where things go

- **The user's sketches go in `my_sketches/`**, one folder each. Create new ones there, never inside `examples/` or `flick/`.
- **`examples/`** holds working sketches in groups: `01_basics` (hello_flick, fade, button, analog, RGB LED, timers), `02_hardware` (I2C, fuel gauge, UART, SPI, touch, LED strip, servo, I2S), `03_wifi` (scan, HTTPS, NTP, web server, captive portal, MQTT, TCP, UDP, ESP-NOW), `04_bluetooth`, `05_storage` (LittleFS, Preferences, microSD), `06_power_and_usb`, `07_updates` (over the air). Start from the closest one: copy it into `my_sketches/` and rename the folder and its `.ino` to match.
- **`flick/`** is the tool, the SDK and the board definitions. Read it, don't change it.
- **`libraries/`** is where `lib install` puts Arduino libraries.
