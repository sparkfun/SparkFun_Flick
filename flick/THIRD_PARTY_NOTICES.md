# Third-party notices

Flick's own code (this repository: `tools/`, `sdk/`, `baseline/`, `chips/`, `targets/`, `examples/`, docs) is released into the public domain under the Unlicense: see `LICENSE`.

The **Baseline firmware images** (`baseline-<target>.bin`, `.bsu`, `.hex` and `.uf2`) are built from that code together with the projects below. Those parts remain under their own licenses, which apply to the images.

| Project | License | Used for |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) 5.5 (Espressif) | Apache-2.0 | The framework Baseline is built on: drivers, WiFi/BLE stacks, bootloader |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) 3.3 (Espressif) | LGPL-2.1 (core); libraries vary | The Arduino core and libraries inside Baseline |
| FreeRTOS (in ESP-IDF) | MIT | Scheduler |
| lwIP (in ESP-IDF) | BSD-3-Clause | TCP/IP |
| Mbed TLS (in ESP-IDF) | Apache-2.0 | TLS, SHA-256 |
| NimBLE / Bluedroid (in ESP-IDF) | Apache-2.0 | Bluetooth |
| LittleFS (via ESP-IDF component) | BSD-3-Clause | The sketch file system |
| [esptool-js](https://github.com/espressif/esptool-js) (download page, loaded from a CDN) | Apache-2.0 | The browser "Flash" button |

The RP2040 and RP2350 images (`.uf2`):

| Project | License | Used for |
|---|---|---|
| [arduino-pico](https://github.com/earlephilhower/arduino-pico) 6.3 (Earle F. Philhower, III) | LGPL-2.1 (core); libraries vary | The Arduino core and libraries inside Baseline |
| [Pico SDK](https://github.com/raspberrypi/pico-sdk) (Raspberry Pi) | BSD-3-Clause | Drivers, flash, USB (TinyUSB, MIT) |
| lwIP (in the Pico SDK) | BSD-3-Clause | TCP/IP (boards with WiFi) |
| BearSSL (in arduino-pico) | MIT | TLS (boards with WiFi) |
| CYW43 driver (in the Pico SDK, George Robotics) | Its own license with Raspberry Pi's grant (`LICENSE.RP`): use with Raspberry Pi's chips only | The WiFi/Bluetooth radio (Thing Plus - RP2350) |
| [BTstack](https://github.com/bluekitchen/btstack) (BlueKitchen) | Its own license with Raspberry Pi's grant for products built on Pico W, Pico 2 W and RM2 (`pico_btstack/LICENSE.RP`) | Bluetooth (Thing Plus - RP2350, RM2 module) |
| LittleFS (in arduino-pico) | BSD-3-Clause | The sketch file system |
| Mozilla's CA certificates (`chips/rp2350/cacrt_all.pem`, via curl) | MPL-2.0 | The TLS trust store |

The sketch compilers that `flick setup` downloads, Espressif's GCC (crosstool-NG) and arduino-pico's (pico-quick-toolchain), are GPL-3.0 with the GCC Runtime Library Exception: sketches built with them are not affected. The page's RP2040/RP2350 Flash button uses no third-party code (WebUSB and the boot ROM's PICOBOOT interface).

Arduino libraries installed with `flick lib install` keep their own licenses.
