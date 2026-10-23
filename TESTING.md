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
- WiFi checks need `secrets.cfg` (copy `secrets.cfg.example`); without it they are skipped. On a chip without a radio (ESP32-P4) the WiFi and BLE checks are skipped.
- Before testing a new board, check it with `esptool flash-id`: the chip revision (classic ESP32: 3.0 or later) and the flash size against `flash_size` in its `target.json`.

## Boards

| Target | Board | Chip (rev), flash, PSRAM | Last tested | `bsk test` | LEDs by eye | Status |
|---|---|---|---|---|---|---|
| `thing_plus_esp32c6` | [Thing Plus - ESP32-C6](https://www.sparkfun.com/sparkfun-thing-plus-esp32-c6.html) | ESP32-C6 (0.0), 16 MB, none | 2026-10-22 | ✅ 31 passed, 0 failed, 1 skipped | ✅ RGB | **Fully tested** |
| `iot_redboard_esp32` | [IoT RedBoard - ESP32](https://www.sparkfun.com/sparkfun-iot-redboard-esp32-development-board.html) | ESP32-D0WD-V3 (3.1), 16 MB, none | 2026-10-22 | ✅ 29 passed, 0 failed, 3 skipped at 921600 (2026-10-23, 2 runs, after the RAM changes; microSD skipped, no card); also at 2 Mbaud before | ✅ status (GPIO 18) + RGB | **Fully tested**; see issues 1, 8 |
| `datalogger_iot` | [DataLogger IoT](https://www.sparkfun.com/sparkfun-datalogger-iot.html) / [DataLogger IoT - 9DoF](https://www.sparkfun.com/sparkfun-datalogger-iot-9dof.html) | ESP32-D0WD-V3 (3.1), 16 MB, none (base board tested) | 2026-10-22 | ✅ 30 passed, 0 failed, 2 skipped (2 runs) | ✅ status (GPIO 25) + RGB (26) | **Fully tested** (base board); see issues 9–11 |
| `iot_redboard_esp32p4_poe` | IoT RedBoard - ESP32-P4 (PoE) (unreleased; no product page yet) | ESP32-P4 (3.2), 16 MB, PSRAM in package; no radio, Ethernet | 2026-10-23 | ✅ 30 passed, 0 failed, 2 skipped (WiFi, BLE: no radio) | ✅ RGB (GPIO 12) | **Fully tested**; Ethernet not yet supported |
| `thing_plus_esp32c5` | [Thing Plus - ESP32-C5](https://www.sparkfun.com/sparkfun-thing-plus-esp32-c5.html) | ESP32-C5 (1.0), 8 MB, 8 MB | 2026-10-22 | ✅ 36 passed, 0 failed, 0 skipped | ✅ RGB | **Fully tested**; see issue 4 |
| `pro_micro_esp32c3` | [Pro Micro - ESP32-C3](https://www.sparkfun.com/sparkfun-pro-micro-esp32-c3.html) | ESP32-C3 (0.4), 4 MB embedded, none | 2026-10-22 | ✅ 29 passed, 0 failed, 3 skipped (2 runs) | ✅ blue (GPIO 10, plain LED) | **Fully tested**; see issues 6, 7 |
| `qwiic_pocket_esp32c6` | [Qwiic Pocket - ESP32-C6](https://www.sparkfun.com/sparkfun-qwiic-pocket-development-board-esp32-c6.html) | ESP32-C6FH4 (0.1), 4 MB embedded, none | 2026-10-22 | ✅ 30 passed, 0 failed, 3 skipped | ✅ blue (GPIO 23) | **Fully tested** |
| `thing_plus_esp32s3` | [Thing Plus - ESP32-S3](https://www.sparkfun.com/sparkfun-thing-plus-esp32-s3.html) | ESP32-S3, 4 MB, 2 MB | 2026-10-21 | ⬜ (earlier selftest 21/21, before `bsk test` existed) | ⬜ | Needs `bsk test` on ABI 1.6 |
| `thing_plus_esp32` | [Thing Plus - ESP32 WROOM (USB-C)](https://www.sparkfun.com/sparkfun-thing-plus-esp32-wroom-usb-c.html) ("Thing Plus C") | ESP32-D0WD-V3 (3.0), 16 MB, none | 2026-10-21 | ⬜ (earlier checks with the Arduino-built Baseline) | ⬜ | Needs `bsk test` on ABI 1.6 |
| `rtk_postcard` | [RTK Postcard](https://www.sparkfun.com/sparkfun-rtk-postcard.html) | ESP32-PICO-V3-02 (3.1), 8 MB, 2 MB | 2026-10-21 (chip only) | ⬜ (the chip passed 21/21 on the IDF Baseline) | ⬜ | Board in use; needs `bsk test` |

## Features by board

Results on the current Baseline (ABI 1.6, ESP-IDF build) unless noted.

| Feature | Thing Plus C6 | Thing Plus C5 | Pro Micro C3 | Qwiic Pocket C6 | IoT RedBoard | DataLogger IoT | IoT RedBoard P4 (PoE) | Thing Plus S3 | Thing Plus C | Postcard |
|---|---|---|---|---|---|---|---|---|---|---|
| Baseline boots, `INFO` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ |
| Sketch upload | ✅ (USB) | ✅ (USB) | ✅ (USB) | ✅ (USB) | ✅ 921600 (2 Mbaud tried, issue 5) | ✅ (CH340) | ✅ (USB) | ✅ (USB) | ⬜ | ⬜ |
| Memory: `malloc` internal / PSRAM | ✅ / — | ✅ / ✅ 8 MB | ✅ / — | ✅ / — | ✅ / — | ✅ / — | ✅ / ✅ | ✅ / ✅ (2 MB) | ⬜ | ✅ (chip) / ✅ 2 MB |
| Strings, `F()`, libm, time | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (before `F()`/libm) | ⬜ | ✅ (chip) |
| Chip temperature | ✅ | ✅ | ✅ | ✅ | — (no sensor) | — (no sensor) | ✅ | ⬜ | — | — |
| Preferences (NVS) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ⬜ | ✅ (chip) |
| LittleFS | ✅ 12.4 MB | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ 1.2 MB | ⬜ | ✅ 3.9 MB (chip) |
| microSD | ✅ SPI (CS 18) | ✅ SPI (CS 25) | — | — | ✅ SPI (CS 5) | ✅ SDMMC 4-bit, 7.6 GB (issue 9) | ✅ SDMMC 4-bit (on-chip LDO + power switch) | ⬜ SDMMC | ⬜ | — |
| Ticker / timers | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ⬜ | ✅ (chip) |
| On-board I2C device (MAX17048 0x36) | ✅ | ✅ | — | — (battery on ADC instead: ✅ 3215 mV) | ✅ | ✅ | ✅ | ⬜ | ⬜ | — |
| Qwiic devices with SparkFun libraries | ✅ BMI270 + 1.3" OLED | ✅ BMI270 + 1.3" OLED | ⬜ | ✅ BMI270 + 1.3" OLED | ⬜ (bus works) | ✅ BMI270 (OLED 👀) | ✅ BMI270 + 1.3" OLED | ⬜ | ⬜ | ⬜ |
| Status LED | — (RGB only) | — (RGB only) | ✅ blue | ✅ blue | ✅ | ✅ | — (RGB only) | ⬜ | ⬜ | ⬜ |
| RGB LED | ✅ | ✅ | — | — | ✅ | ✅ | ✅ | ⬜ | ⬜ | — |
| WiFi scan | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — (no radio) | ✅ | ⬜ | ✅ (chip) |
| WiFi connect, `WiFi.onEvent`, NTP, HTTP | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — (no radio; Ethernet ⬜) | ⬜ | ⬜ | ⬜ |
| BLE scan | ✅ | ✅ | ✅ (after fix, issue 7) | ✅ (with the issue 7 fix) | ✅ (after WiFi; issue 1) | ✅ (with microSD mounted; issue 10) | — (no radio) | ✅ | ⬜ | ✅ (chip) |
| Deep sleep timer wake, RTC memory, NVS kept | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | ⬜ |
| Crash rollback + crash in `INFO` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ⬜ (rollback ✅ earlier) | ⬜ | ⬜ |
| Watchdog rollback | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | ⬜ |
| USB device mode (keyboard etc.) | — | — | — | — | — | — | ⬜ | ✅ round trip | — | — |
| Bluetooth Classic SPP | — | — | — | — | ⬜ | ⬜ | — | — | ⬜ | ⬜ |

## Library compile check

The first example of each library, unchanged, built as a Baseline sketch (`bsk build`). Run on 2026-10-21; repeat after SDK changes.

| Target | Result |
|---|---|
| `thing_plus_esp32s3` | ✅ 28 / 28 |
| `thing_plus_esp32c6` | ✅ 28 / 28 |
| `thing_plus_esp32c5` | ✅ 28 / 28 (2026-10-22) |
| `pro_micro_esp32c3` | ✅ 28 / 28 (2026-10-22) |
| `qwiic_pocket_esp32c6` | ✅ 28 / 28 (2026-10-22) |
| `datalogger_iot` | ✅ 28 / 28 (2026-10-22) |
| `iot_redboard_esp32p4_poe` | ✅ 28 / 28 (2026-10-23) |
| `rtk_postcard` (classic ESP32) | ⚠️ 27 / 28: the TMP102 example uses `A3`, which the Postcard has no pin for |

Libraries: ArduinoJson; SparkFun BME280, BMP581, BMI270, ICM-20948, ISM330DHCX, MMC5983MA, TMP102, TMP117, SHTC3, AHT20, SCD30, SCD4x, VEML7700, VCNL4040, VL53L1X, NAU7802, Qwiic Button, Qwiic Relay, Qwiic Joystick, Qwiic Twist, Qwiic Ultrasonic, Qwiic Alphanumeric, Qwiic OLED, I2C Mux, External EEPROM, u-blox GNSS v3, LG290P GNSS.

On hardware so far: BMI270 and Qwiic OLED (1.3") on the Thing Plus C6, the Thing Plus C5, the Qwiic Pocket C6, the DataLogger IoT (BMI270) and the IoT RedBoard P4 (BMI270).

Every example in `examples/` builds for the S3, the classic ESP32 and the C6 (`touch_button` stops with a clear message on the C6, which has no touch sensor).

## Over-the-air updates (docs/OTA.md)

| Test | IoT RedBoard (dual, 16 MB) | Pro Micro C3 (staged, 4 MB) | Thing Plus C6 (dual, 16 MB) |
|---|---|---|---|
| Baseline push over USB (`bsk baseline update -p`) | ✅ 2.1 MB in 120 s; sketch kept, slot switched | ✅ 1.26 MB in 14 s; restarts into update mode first; sketch and LittleFS erased | ✅ 2.1 MB in 23 s (native USB) |
| Baseline push over WiFi (`--host`) | ✅ 45 s (16 lines in flight; 141 s one at a time) | ✅ 23 s; update mode reconnects to WiFi and reopens the OTA port | ⬜ |
| A different Baseline really installed | (same version; the slot switch shows it) | ✅ `0.1.0-otatest`, then `0.1.1-test` reported by `INFO` | ✅ `0.1.1-test` in `baseline_1` |
| Rollback: reset before the new Baseline confirms itself | ✅ 3 of 3 back to the old slot | (one slot: n/a) | ⬜ |
| Reset during the bootloader's install (power cut) | (n/a) | ✅ install starts again and finishes | (n/a) |
| Sketch pull from a GitHub release (`ota_pull_sketch`) | ⬜ | ✅ v1 installed v2, v2 up to date | ⬜ |
| Baseline pull from a GitHub release (`ota_pull_baseline`) | ⬜ (run on the C6 instead) | ✅ update mode download, bootloader install, sketch reinstalled from the release | ✅ inflated on the board into `baseline_1` while the sketch ran (17 s), restarted, sketch reinstalled, confirmed |
| `bsk test` with ABI 1.7 | ✅ 29/0/3 | ✅ 29/0/3 | ✅ 30/0/2 |

## Not tested on hardware yet

- `Wire1` (second I2C bus), `Serial1.setRxBufferSize`, `WiFiMulti`, `dacWrite`
- ESP-NOW between two boards, MQTT, WiFi upload (`bsk upload --host`), `BLESerial` with a phone, BLE server notify/write, WebServer, TCP/UDP examples
- Safe mode by holding BOOT at reset
- Staged updates on the Qwiic Pocket C6 and Thing Plus S3 (built; the C3 is tested)
- Bluetooth Classic SPP pairing with a phone
- I2S (needs an amplifier), servos, LED strips longer than one LED

## Issues found by testing

1. **BLE on the IoT RedBoard after WiFi (2026-10-22).** On the first `bsk test` run, the BLE scan after a full WiFi session (connect, NTP, HTTP) crashed in setup three times in a row: `BLEDevice::init()` had failed (no PSRAM: little internal RAM left) and Baseline used the scan object anyway. **Fixed:** every BLE call now checks that the stack started and returns `BL_ERR_NO_MEM` otherwise, logging the largest free internal block. Three later runs passed without the failure. **Then at 2 Mbaud (same RAM, different luck) it failed differently:** `BLEDevice::init()` returned, but Bluedroid's own task crashed cleaning up after its start-up allocation failed (`l2c_free` in `BTU_StartUp`), out of Baseline's reach; and when it did start, a scan with ~40 devices around aborted on `operator new`. Measured: 132 KB free at sketch start, 62 KB with WiFi up, 9 KB after Bluedroid. **Fixed (2026-10-22)** by giving internal RAM back on the classic ESP32: the first BLE use releases Bluetooth Classic's controller memory when SPP has not started (SPP then reports a failure until restart; start SPP first to use both); BLE Mesh is compiled out (all chips); and on the boards without PSRAM (IoT RedBoard, Thing Plus C, DataLogger IoT) WiFi TX buffers are dynamic, 6 static RX buffers, and smaller BTC/BTU/timer task stacks (`targets/<t>/idf/sdkconfig.defaults`). Now 80 KB free with WiFi up and 20 KB after Bluedroid; `bsk test` passes with the BLE scan after WiFi.
2. **Flash sizes in Arduino's board files are wrong for two boards:** the Thing Plus C6 and the IoT RedBoard both have 16 MB (Arduino says 4 MB). Their targets now use 16 MB. Always check with `esptool flash-id`.
3. **The IoT RedBoard's chip is rev 3.1**, not the ESP32-D0WDQ6 the hookup guide names, so Baseline's rev 3.0 requirement is met.
4. **Thing Plus C5: boot loop after the first flash (2026-10-22, recovered; watch for it).** The first time Baseline was flashed, the board reset every ~0.3 s with `rst:0x7 (TG0_WDT_HPSYS)` before printing anything, and esptool could no longer reset it into download mode. BOOT + RESET, then a plain RESET, cleared it, and the same Baseline then booted normally. Every later esptool flash leaves one watchdog reset (`INFO reset=7`) before a clean boot, and software restarts (`esp_restart`, after uploads) are clean. Best guess: the firmware the board shipped with left the flash or PSRAM interface in a state only a full reset clears. If a board loops after flashing, press RESET. The C5 also logs `MSPI Timing: Failed to allocate dummy cacheline for PSRAM memory barrier!` at start-up (an ESP-IDF message, PSRAM is initialized by Arduino after the heap); PSRAM tests pass regardless.
5. **Classic ESP32 console: tried 2 Mbaud, back to 921600 (2026-10-23).** 2 Mbaud worked on the IoT RedBoard's CH340, but uploads were no faster (bound by request/reply round trips: `selftest`, 15.8 KB, about 1 s either way), and about 3% of console replies lost a byte (2 of 60 `INFO` replies; 0 of 120 at 921600). That caused a failed `crash recorded` check once. `bsk` still finds a board left at 2 Mbaud. The bootloader's own messages were at 115200 all along (IDF ignores the console baud setting unless the console UART is "custom").
6. **Pro Micro C3: too little heap for WiFi + BLE (2026-10-22, fixed).** The C3 has no PSRAM, and the code it keeps in IRAM comes out of the same 400 KB as the heap: 161 KB free at boot. **Fixed** by moving FreeRTOS, heap and Bluetooth-controller code to flash (`chips/esp32c3/idf/sdkconfig.defaults`): 205 KB free at boot.
7. **BLE scans grew without bound on NimBLE (S3, C3, C5, C6) (2026-10-22, fixed).** Baseline asked the BLE library for duplicate reports (right for Bluedroid on the classic ESP32). On NimBLE the library then appends every report to the device's payload, so each device's copy grew by kilobytes during a scan; on the C3 the heap ran out and the library aborted (`std::terminate`). **Fixed:** duplicates only on Bluedroid. The S3, C5 and C6 passed their BLE scans before the fix; re-run `bsk test` on them to confirm.
8. **`bsk test` missed console output once (IoT RedBoard, 2026-10-22).** One run captured only the after-sleep checks: the console was not read during the first phase right after the upload (likely the CH340 port briefly unavailable after `bsk` closed it). **Fixed in `bsk`:** it compares the PASS/FAIL lines it saw with the selftest's own `RESULT` count and runs the selftest again if lines are missing. **Also (2026-10-23):** lines the previous sketch printed during the upload (a selftest left running, its WiFi scan cut short) were counted as results (`FAIL WiFi scan`); `bsk test` now ignores everything before the new selftest's `--- selftest` banner.
9. **DataLogger IoT: Qwiic and microSD are on a switched 3.3 V rail (2026-10-22, fixed).** With Baseline as first written, the Qwiic scan found only the on-board fuel gauge, and the card failed with `sdmmc_host_clock_update_command ... 0x107` (a stock Arduino sketch failed the same way). GPIO 32 enables the rail (high = on; `kDLBoardEn3v3_SW` in SparkFun's firmware, not in the hookup guide). **Fixed:** targets can define `BL_BOARD_EN_3V3_SW`, and Baseline drives it high at boot. Also from SparkFun's firmware: the RX/TX header is RX 16, TX 17 (the hookup guide lists them the other way round; not yet checked with a loopback). A3 (GPIO 39) added.
10. **No-PSRAM classic boards: microSD + WiFi + BLE ran out of RAM (2026-10-22, fixed).** Mounting the card reserved ~43 KB: FATFS sets aside a file object for each of the 8 files that may be open, and with the per-file cache each holds a 4 KB sector buffer. Bluedroid then failed to start (`assert failed: hci_hal_env_init`). **Fixed** on the RedBoard, Thing Plus C and DataLogger IoT by turning the per-file cache off (open files share the volume's buffer): the mount now costs ~10.6 KB. With microSD, WiFi and BLE all up, ~10 KB is left, so a sketch that also allocates large buffers can still run out; boards with PSRAM are not affected.
11. **An upload failed with `rejected: out of memory` (2026-10-22, fixed).** Checking an uploaded sketch's SHA-256 took a 4 KB buffer from the heap, which a running sketch can have used up. **Fixed:** a 512-byte buffer on the loader's stack.
12. **ESP32-P4: first chip without a radio (2026-10-23).** Arduino's WiFi and BLE libraries compile to nothing on the P4, so Baseline's WiFi, ESP-NOW and BLE files did not build. **Fixed:** `tools/gen_api.py` now also writes `baseline/src/gen/bl_impl_stubs.inc`: stubs grouped by prefix (`BL_STUB_WIFI`, `BL_STUB_BLE`, `BL_STUB_ESPNOW`), which a file includes when the chip lacks the hardware (`SOC_WIFI_SUPPORTED`, `SOC_BT_SUPPORTED`). Actions return `BL_ERR_UNSUPPORTED`; queries answer "nothing there" (0, or -1 for reads); `WiFi.status()` returns the new `WL_NO_SHIELD` (255, as in Arduino). `BLEDevice::init()` now returns `bool`, as in Arduino-ESP32 3.x. Arduino's P4 sdkconfig expects Espressif's dev board, with an ESP32-C6 for WiFi/BLE over SDIO (`esp_hosted`, slave reset on GPIO 54, which is A0 on this board); Baseline leaves that out and turns Bluetooth off for the P4.
13. **OTA port starved the idle task's watchdog (2026-10-23, fixed).** With `bsk` keeping 16 lines in flight over WiFi, the OTA task read one byte at a time (an lwIP call each) and handled line after line without yielding: the task watchdog reset the board. **Fixed:** `bl_ota.cpp` reads in 512-byte blocks and yields after every command.
14. **GitHub release downloads failed with `HTTP_CLIENT: Out of buffer` (2026-10-23, fixed).** The redirect to GitHub's file storage carries a long signed URL that did not fit `esp_http_client`'s default 512-byte buffers. **Fixed:** 4 KB receive and 2 KB transmit buffers in `HTTPClient` (`bl_impl_wifi.cpp`) and in the update downloads (`bl_update.cpp`).
15. **The bootloader logs to UART0, not USB.** On native-USB boards the staged install's own messages (`bsu_stage: installing ...`) are only on the TX pin; Baseline logs the outcome on the next start (`update: Baseline ... installed`).
