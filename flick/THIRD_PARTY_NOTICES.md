# Third-party notices

Flick's own code (this repository: `tools/`, `sdk/`, `baseline/`, `chips/`, `targets/`, `examples/`, docs) is released into the public domain under the Unlicense: see `LICENSE`.

The **Baseline firmware images** (`baseline-<target>.bin`, `.bsu`, `.hex` and `.uf2`; ESP32 images first, then RP2040/RP2350 and Teensy) are built from that code together with the projects below. Those parts remain under their own licenses, which apply to the images.

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
| [arduino-pico](https://github.com/earlephilhower/arduino-pico) 6.3 (Earle F. Philhower, III) | LGPL-2.1 (core); libraries vary | The Arduino core and libraries inside Baseline, and the OTA stub that installs Baseline updates (with uzlib, zlib license) |
| [Pico SDK](https://github.com/raspberrypi/pico-sdk) (Raspberry Pi) | BSD-3-Clause | Drivers, flash, USB (TinyUSB, MIT) |
| lwIP (in the Pico SDK) | BSD-3-Clause | TCP/IP (boards with WiFi) |
| BearSSL (in arduino-pico) | MIT | TLS (boards with WiFi) |
| CYW43 driver (in the Pico SDK, George Robotics) | Its own license with Raspberry Pi's grant (`LICENSE.RP`): use with Raspberry Pi's chips only | The WiFi/Bluetooth radio (Thing Plus - RP2350) |
| [BTstack](https://github.com/bluekitchen/btstack) (BlueKitchen) | Its own license with Raspberry Pi's grant for products built on Pico W, Pico 2 W and RM2 (`pico_btstack/LICENSE.RP`) | Bluetooth (Thing Plus - RP2350, RM2 module) |
| LittleFS (in arduino-pico) | BSD-3-Clause | The sketch file system |
| Mozilla's CA certificates (`chips/rp2350/cacrt_all.pem`, via curl) | MPL-2.0 | The TLS trust store |

The Teensy 4.0, 4.1 and MicroMod Teensy images (`.hex`):

| Project | License | Used for |
|---|---|---|
| [Teensyduino](https://www.pjrc.com/teensy/td_download.html) 1.62 core, `teensy4` (PJRC.COM, LLC) | MIT, with PJRC's condition: a build system that lists target devices must list PJRC's similar devices too | The Arduino core: startup, USB serial/MIDI/audio/HID, DMAChannel, IntervalTimer |
| Audio library (Paul Stoffregen, PJRC) | MIT, with a development funding notice that must be kept | The Teensy Audio library inside Baseline |
| CMSIS-DSP (in the Teensy core, Arm) | Apache-2.0 | FFT and filters for the Audio library |
| USBHost_t36 (Paul Stoffregen, PJRC) | MIT | The USB host port |
| LittleFS wrapper, SD (Paul Stoffregen, PJRC) | MIT, with a development funding notice | The sketch file system, microSD |
| littlefs (in the LittleFS library, Arm) | BSD-3-Clause | The sketch file system |
| [SdFat](https://github.com/greiman/SdFat) (Bill Greiman) | MIT | microSD |
| SPI, Wire, EEPROM (Arduino, with PJRC changes) | LGPL-2.1 (SPI: GPL-2.0 or LGPL-2.1) | SPI and I2C buses, Baseline's slot settings |
| [NativeEthernet](https://github.com/vjmuzik/NativeEthernet) (Tino Hernandez) | MIT | Ethernet (Teensy 4.1) |
| [FNET](https://github.com/butok/FNET) (in NativeEthernet) | Apache-2.0 | TCP/IP (Teensy 4.1) |

The SparkFun Artemis (Apollo3) images (`.bin`, behind SparkFun's SVL bootloader):

| Project | License | Used for |
|---|---|---|
| [SparkFun Apollo3 core](https://github.com/sparkfun/Arduino_Apollo3) 2.2.1 (SparkFun Electronics) | MIT (SparkFun's code); ArduinoCore-API LGPL-2.1 | The Arduino core inside Baseline |
| [Mbed OS](https://github.com/ARMmbed/mbed-os) 6 (in the core, Arm) | Apache-2.0 | RTOS (RTX), drivers, the Cordio HCI driver and WSF |
| AmbiqSuite HAL (in Mbed OS, Ambiq Micro) | BSD-3-Clause | Flash, UART, the BLE controller interface |
| [ArduinoBLE](https://github.com/arduino-libraries/ArduinoBLE) 2.1.0 (Arduino SA) | LGPL-2.1 | The Bluetooth LE host (HCI, ATT/GATT, GAP), linked into Baseline |

ArduinoBLE is LGPL-2.1 and linked statically into the Artemis Baseline images. Its source is the unmodified 2.1.0 release (Baseline only replaces its GAP and ATT objects with subclasses, through the library's own `FAKE_GAP` / `FAKE_ATT` switches); Baseline's own source is in this repository, so an image can be rebuilt against a changed ArduinoBLE with `python flick.py baseline build -t <target>`, as the LGPL asks.

Artemis sketches are compiled with the Arm GNU toolchain (GCC 8-2018-q4-major) that SparkFun's Apollo3 core installs, or that `flick setup` downloads from SparkFun (GPL-3.0 with the GCC Runtime Library Exception: sketches are not affected). The page's Artemis Flash button uses no third-party code: it speaks the protocol of SparkFun's SVL bootloader, which stays on the board, over Web Serial.

Teensy sketches are compiled with the GCC that Teensyduino installs (GPL-3.0 with the GCC Runtime Library Exception). The page's Teensy Flash button uses no third-party code: it speaks PJRC's HalfKay bootloader protocol over WebHID, written from the protocol, not from Teensy Loader's source.

The sketch compilers that `flick setup` downloads, Espressif's GCC (crosstool-NG) and arduino-pico's (pico-quick-toolchain), are GPL-3.0 with the GCC Runtime Library Exception: sketches built with them are not affected. The page's RP2040/RP2350 Flash button uses no third-party code (WebUSB and the boot ROM's PICOBOOT interface).

Arduino libraries installed with `flick lib install` keep their own licenses.
