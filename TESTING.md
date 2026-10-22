# Testing

What has been tested on real hardware, what passed, and what is still open. Update this file after every hardware test.

Legend: ✅ passed · ❌ failed · ⚠️ passed with a note · 👀 needs a look by eye · ⬜ not tested yet · — does not apply to this board

## How to test a board

```
python tools/bsk.py baseline flash -t <target>     # once per Baseline change
python tools/bsk.py test                          # automatic checks, one PASS/FAIL table (~2 min)
python tools/bsk.py upload examples/led_check     # LEDs, by eye: red, green, blue, white, off (1 s each)
```

- `bsk test` runs `examples/selftest` (memory, PSRAM, Strings, libm, time, Preferences, LittleFS, microSD, Ticker, the board's I2C devices, WiFi scan, BLE scan, WiFi connect + `WiFi.onEvent` + NTP + HTTP, then a 2 s deep sleep and the checks after waking), then a crash in `setup()` (`tools/tests/crash_test`, must roll back and be recorded in `INFO`), then a `loop()` that never returns (`tools/tests/hang_test`, the watchdog must catch it and roll back).
- WiFi checks need `secrets.cfg` (copy `secrets.cfg.example`); without it they are skipped.
- Before testing a new board, check it with `esptool flash-id`: the chip revision (classic ESP32: 3.0 or later) and the flash size against `flash_size` in its `target.json`.

## Boards

| Target | Board | Chip (rev), flash, PSRAM | Last tested | `bsk test` | LEDs by eye | Status |
|---|---|---|---|---|---|---|
| `thing_plus_esp32c6` | [Thing Plus - ESP32-C6](https://www.sparkfun.com/sparkfun-thing-plus-esp32-c6.html) | ESP32-C6 (0.0), 16 MB, none | 2026-10-22 | ✅ 31 passed, 0 failed, 1 skipped | ✅ RGB | **Fully tested** |
| `iot_redboard_esp32` | [IoT RedBoard - ESP32](https://www.sparkfun.com/sparkfun-iot-redboard-esp32-development-board.html) | ESP32-D0WD-V3 (3.1), 16 MB, none | 2026-10-22 | ✅ 30 passed, 0 failed, 2 skipped (3 runs) | ✅ status (GPIO 18) + RGB | **Fully tested** (at 921600 baud; 2 Mbaud not yet) |
| `thing_plus_esp32c5` | [Thing Plus - ESP32-C5](https://www.sparkfun.com/sparkfun-thing-plus-esp32-c5.html) | ESP32-C5 (1.0), 8 MB, 8 MB | 2026-10-22 | ✅ 36 passed, 0 failed, 0 skipped | 👀 RGB | Tested; LED pending; see issue 4 |
| `thing_plus_esp32s3` | [Thing Plus - ESP32-S3](https://www.sparkfun.com/sparkfun-thing-plus-esp32-s3.html) | ESP32-S3, 4 MB, 2 MB | 2026-10-21 | ⬜ (earlier selftest 21/21, before `bsk test` existed) | ⬜ | Needs `bsk test` on ABI 1.6 |
| `thing_plus_esp32` | [Thing Plus - ESP32 WROOM (USB-C)](https://www.sparkfun.com/sparkfun-thing-plus-esp32-wroom-usb-c.html) ("Thing Plus C") | ESP32-D0WD-V3 (3.0), 16 MB, none | 2026-10-21 | ⬜ (earlier checks with the Arduino-built Baseline) | ⬜ | Needs `bsk test` on ABI 1.6 |
| `rtk_postcard` | [RTK Postcard](https://www.sparkfun.com/sparkfun-rtk-postcard.html) | ESP32-PICO-V3-02 (3.1), 8 MB, 2 MB | 2026-10-21 (chip only) | ⬜ (the chip passed 21/21 on the IDF Baseline) | ⬜ | Board in use; needs `bsk test` |
| `datalogger_iot` | [DataLogger IoT](https://www.sparkfun.com/sparkfun-datalogger-iot.html) / [DataLogger IoT - 9DoF](https://www.sparkfun.com/sparkfun-datalogger-iot-9dof.html) | ESP32-WROOM-32E, 16 MB, none | — | ⬜ | ⬜ | Hardware on order |
| `pro_micro_esp32c3` | [Pro Micro - ESP32-C3](https://www.sparkfun.com/sparkfun-pro-micro-esp32-c3.html) | ESP32-C3, 4 MB, none | — | ⬜ | ⬜ | Hardware on order (first C3) |
| `qwiic_pocket_esp32c6` | [Qwiic Pocket - ESP32-C6](https://www.sparkfun.com/sparkfun-qwiic-pocket-development-board-esp32-c6.html) | ESP32-C6, 4 MB, none | — | ⬜ | ⬜ | Hardware on order (same chip as the tested Thing Plus C6) |

## Features by board

Results on the current Baseline (ABI 1.6, ESP-IDF build) unless noted.

| Feature | Thing Plus C6 | Thing Plus C5 | IoT RedBoard | Thing Plus S3 | Thing Plus C | Postcard |
|---|---|---|---|---|---|---|
| Baseline boots, `INFO` | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ |
| Sketch upload | ✅ (USB) | ✅ (USB) | ✅ 921600 baud, 0.09 s for `blink` (2 Mbaud ⬜) | ✅ (USB) | ⬜ | ⬜ |
| Memory: `malloc` internal / PSRAM | ✅ / — | ✅ / ✅ 8 MB | ✅ / — | ✅ / ✅ (2 MB) | ⬜ | ✅ (chip) / ✅ 2 MB |
| Strings, `F()`, libm, time | ✅ | ✅ | ✅ | ✅ (before `F()`/libm) | ⬜ | ✅ (chip) |
| Chip temperature | ✅ | ✅ | — (no sensor) | ⬜ | — | — |
| Preferences (NVS) | ✅ | ✅ | ✅ | ✅ | ⬜ | ✅ (chip) |
| LittleFS | ✅ 12.4 MB | ✅ | ✅ | ✅ 1.2 MB | ⬜ | ✅ 3.9 MB (chip) |
| microSD | ✅ SPI (CS 18) | ✅ SPI (CS 25) | ✅ SPI (CS 5) | ⬜ SDMMC | ⬜ | — |
| Ticker / timers | ✅ | ✅ | ✅ | ✅ | ⬜ | ✅ (chip) |
| On-board I2C device (MAX17048 0x36) | ✅ | ✅ | ✅ | ⬜ | ⬜ | — |
| Qwiic devices with SparkFun libraries | ✅ BMI270 + 1.3" OLED | ✅ BMI270 + 1.3" OLED | ⬜ (bus works) | ⬜ | ⬜ | ⬜ |
| Status LED | — (RGB only) | — (RGB only) | ✅ | ⬜ | ⬜ | ⬜ |
| RGB LED | ✅ | 👀 | ✅ | ⬜ | ⬜ | — |
| WiFi scan | ✅ | ✅ | ✅ | ✅ | ⬜ | ✅ (chip) |
| WiFi connect, `WiFi.onEvent`, NTP, HTTP | ✅ | ✅ | ✅ | ⬜ | ⬜ | ⬜ |
| BLE scan | ✅ | ✅ | ⚠️ see issue 1 | ✅ | ⬜ | ✅ (chip) |
| Deep sleep timer wake, RTC memory, NVS kept | ✅ | ✅ | ✅ | ⬜ | ⬜ | ⬜ |
| Crash rollback + crash in `INFO` | ✅ | ✅ | ✅ | ⬜ (rollback ✅ earlier) | ⬜ | ⬜ |
| Watchdog rollback | ✅ | ✅ | ✅ | ⬜ | ⬜ | ⬜ |
| USB device mode (keyboard etc.) | — | — | — | ✅ round trip | — | — |
| Bluetooth Classic SPP | — | — | ⬜ | — | ⬜ | ⬜ |

## Library compile check

The first example of each library, unchanged, built as a Baseline sketch (`bsk build`). Run on 2026-10-21; repeat after SDK changes.

| Target | Result |
|---|---|
| `thing_plus_esp32s3` | ✅ 28 / 28 |
| `thing_plus_esp32c6` | ✅ 28 / 28 |
| `thing_plus_esp32c5` | ✅ 28 / 28 (2026-10-22) |
| `rtk_postcard` (classic ESP32) | ⚠️ 27 / 28: the TMP102 example uses `A3`, which the Postcard has no pin for |

Libraries: ArduinoJson; SparkFun BME280, BMP581, BMI270, ICM-20948, ISM330DHCX, MMC5983MA, TMP102, TMP117, SHTC3, AHT20, SCD30, SCD4x, VEML7700, VCNL4040, VL53L1X, NAU7802, Qwiic Button, Qwiic Relay, Qwiic Joystick, Qwiic Twist, Qwiic Ultrasonic, Qwiic Alphanumeric, Qwiic OLED, I2C Mux, External EEPROM, u-blox GNSS v3, LG290P GNSS.

On hardware so far: BMI270 and Qwiic OLED (1.3") on the Thing Plus C6 and the Thing Plus C5.

Every example in `examples/` builds for the S3, the classic ESP32 and the C6 (`touch_button` stops with a clear message on the C6, which has no touch sensor).

## Not tested on hardware yet

- `Wire1` (second I2C bus), `Serial1.setRxBufferSize`, `WiFiMulti`, `dacWrite`
- ESP-NOW between two boards, MQTT, WiFi upload (`bsk upload --host`), `BLESerial` with a phone, BLE server notify/write, WebServer, TCP/UDP examples
- Safe mode by holding BOOT at reset
- Bluetooth Classic SPP pairing with a phone
- I2S (needs an amplifier), servos, LED strips longer than one LED
- The RISC-V C3 (no hardware yet)

## Issues found by testing

1. **BLE on the IoT RedBoard after WiFi (2026-10-22).** On the first `bsk test` run, the BLE scan after a full WiFi session (connect, NTP, HTTP) crashed in setup three times in a row: `BLEDevice::init()` had failed (no PSRAM: little internal RAM left) and Baseline used the scan object anyway. **Fixed:** every BLE call now checks that the stack started and returns `BL_ERR_NO_MEM` otherwise, logging the largest free internal block. Three later runs passed without the failure; watch for `BLE could not start` in the log on boards without PSRAM.
2. **Flash sizes in Arduino's board files are wrong for two boards:** the Thing Plus C6 and the IoT RedBoard both have 16 MB (Arduino says 4 MB). Their targets now use 16 MB. Always check with `esptool flash-id`.
3. **The IoT RedBoard's chip is rev 3.1**, not the ESP32-D0WDQ6 the hookup guide names, so Baseline's rev 3.0 requirement is met.
4. **Thing Plus C5: boot loop after the first flash (2026-10-22, recovered; watch for it).** The first time Baseline was flashed, the board reset every ~0.3 s with `rst:0x7 (TG0_WDT_HPSYS)` before printing anything, and esptool could no longer reset it into download mode. BOOT + RESET, then a plain RESET, cleared it, and the same Baseline then booted normally. Every later esptool flash leaves one watchdog reset (`INFO reset=7`) before a clean boot, and software restarts (`esp_restart`, after uploads) are clean. Best guess: the firmware the board shipped with left the flash or PSRAM interface in a state only a full reset clears. If a board loops after flashing, press RESET. The C5 also logs `MSPI Timing: Failed to allocate dummy cacheline for PSRAM memory barrier!` at start-up (an ESP-IDF message, PSRAM is initialized by Arduino after the heap); PSRAM tests pass regardless.
5. **Classic ESP32 console now 2 Mbaud** (was 921600). Builds; not yet run on hardware.
