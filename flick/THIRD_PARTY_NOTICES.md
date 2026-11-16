# Third-party notices

Flick's own code (this repository: `tools/`, `sdk/`, `baseline/`, `chips/`, `targets/`, `examples/`, docs) is released into the public domain under the Unlicense: see `LICENSE`.

The **Baseline firmware images** (`baseline-<target>.bin` and `.bsu`) are built from that code together with the projects below. Those parts remain under their own licenses, which apply to the images.

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

The sketch compiler that `flick setup` downloads, Espressif's GCC (crosstool-NG), is GPL-3.0 with the GCC Runtime Library Exception: sketches built with it are not affected.

Arduino libraries installed with `flick lib install` keep their own licenses.
