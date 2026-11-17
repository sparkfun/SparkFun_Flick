# Board comparison

How the boards Flick supports compare: how much faster Flick gets a change onto each of them, how long things take, how much flash each one has left, and what each one can't do.

## Edit to running: Arduino IDE versus Flick

The time from saving a change to the sketch running on the board: compile, then upload. The same sketch, [`oled_dashboard`](../benchmarks/oled_dashboard/oled_dashboard.ino) (a live dashboard on SparkFun's 1.3" Qwiic OLED, with SparkFun's Qwiic OLED library), built unchanged both ways, on one board of each group.

<!-- benchmark table: tools/board_benchmark.py -->
| Boards | Arduino IDE: compile + upload | Flick: compile + upload | Edit to running |
|---|---|---|---|
| **ESP32 (classic), USB-serial chip**: IoT RedBoard - ESP32, Thing Plus C, DataLogger IoT, RTK Postcard | 7.1 s + 7.1 s = **14.1 s** | 0.6 s + 0.4 s = **1.0 s** | **14x faster** |
| **ESP32 with native USB**: Thing Plus S3, C5 and C6, Qwiic Pocket C6, Pro Micro C3, IoT RedBoard - ESP32-P4 | 5.1 s + 6.4 s = **11.5 s** | 0.6 s + 0.3 s = **0.9 s** | **13x faster** |
| **Teensy 4.x**: Teensy 4.0, Teensy 4.1, MicroMod Teensy | 5.3 s + 6.4 s = **11.7 s** | 0.6 s + 0.4 s = **1.0 s** | **12x faster** |

- ESP32 (classic), USB-serial chip: measured on the SparkFun DataLogger IoT (2026-11-17); the upload is 307,088 bytes with Arduino (.bin image), 13,264 with Flick (.flick).
- ESP32 with native USB: measured on the SparkFun IoT RedBoard - ESP32-P4 (PoE) (2026-11-17); the upload is 357,488 bytes with Arduino (.bin image), 14,464 with Flick (.flick).
- Teensy 4.x: measured on the SparkFun MicroMod Teensy (2026-11-17); the upload is 184,397 bytes with Arduino (.hex image), 11,480 with Flick (.flick).
<!-- end of benchmark table -->

How it's measured (`python flick/tools/board_benchmark.py`, results in [board_benchmark.json](board_benchmark.json)):

- **Arduino IDE** is arduino-cli with the board's usual Arduino board setting and the same Arduino-ESP32 core Baseline is built with (Teensyduino for the Teensys). Its compile is the everyday rebuild after an edit, with the core already compiled and cached; the very first build of a sketch takes much longer (48-140 s on the Thing Plus S3, [BUILD_RESULTS.md](BUILD_RESULTS.md)). Its upload writes the whole application image with esptool or Teensy Loader.
- **Flick**: `flick build` after the edit for the compile, and `flick upload` after the edit for edit to running (it asks the board which one it is, builds, uploads, and the board restarts into the sketch); the upload is the difference. The sketch is all that's sent: Baseline stays on the board.
- Medians of 3 runs (2 for Arduino uploads). Measured on one Windows 11 PC.

The rest of this page is about the boards themselves. Numbers in it come from Baseline images built on 2026-10-22 and 23 (versions 0.1.x to 0.2.0, ABI 1.7) unless a row says otherwise; sizes shift a little with each Baseline version. **Measured** numbers were timed on real boards. **Estimated** numbers are worked out from a measured rate and are labeled as such.

## At a glance

| Board | Chip | Flash | PSRAM | USB | Baseline updates | Largest sketch | Sketch RAM | Files (LittleFS) |
|---|---|---|---|---|---|---|---|---|
| Thing Plus C6 | ESP32-C6, 1 core | 16 MB | none | native | dual (keeps the sketch) | 512 KB | 32 KB | 8.9 MB |
| Thing Plus C5 | ESP32-C5, 1 core | 8 MB | 8 MB | native | dual | 512 KB | 32 KB | 1.4 MB |
| Thing Plus S3 | ESP32-S3, 2 cores | 4 MB | 2 MB | native | staged (erases the sketch and files) | 256 KB | **64 KB** | 1.19 MB |
| Qwiic Pocket C6 | ESP32-C6, 1 core | 4 MB | none | native | staged | 256 KB | 32 KB | 1.19 MB |
| Pro Micro C3 | ESP32-C3, 1 core | 4 MB | none | native | staged | 256 KB | 32 KB | 1.19 MB |
| IoT RedBoard - ESP32 | ESP32, 2 cores | 16 MB | none | CH340 | dual | 512 KB | 32 KB | 8.9 MB |
| DataLogger IoT | ESP32, 2 cores | 16 MB | none | CH340 | dual | 512 KB | 32 KB | 8.9 MB |
| Thing Plus C (ESP32 WROOM) | ESP32, 2 cores | 16 MB | none | CH340 | dual | 512 KB | 32 KB | 8.9 MB |
| RTK Postcard | ESP32 (PICO), 2 cores | 8 MB | 2 MB | CH342 | dual | 512 KB | 32 KB | 1.4 MB |
| IoT RedBoard - ESP32-P4 (PoE) | ESP32-P4, 2 cores | 16 MB | 32 MB | native | dual | 512 KB | 32 KB | 8.9 MB |
| IoT RedBoard - ESP32-P4 (WiFi BLE) | ESP32-P4, 2 cores, + ESP32-C5 for WiFi 6 and BLE | 16 MB | 32 MB | native | dual | 512 KB | 32 KB | 8.9 MB |
| Teensy 4.0 | i.MX RT1062, 600 MHz | 2 MB | none | native (Teensy) | Teensy Loader only | 384 KB | **128 KB** | 576 KB |
| Teensy 4.1 | i.MX RT1062, 600 MHz | 8 MB | optional chips | native (Teensy) | Teensy Loader only | 1 MB | **128 KB** | 3.75 MB |
| MicroMod Teensy | i.MX RT1062, 600 MHz | 16 MB | none | native (Teensy) | Teensy Loader only | 1 MB | **128 KB** | 11.75 MB |

- **Largest sketch** is the size of one sketch slot. A typical sketch is 1–7 KB, so this limit only matters for sketches with big libraries or data.
- **Sketch RAM** is for the sketch's global and static variables. Larger buffers come from the heap with `malloc` or `new`, and on boards with PSRAM, `ps_malloc` puts them there.
- **Dual** boards keep two Baseline slots, so a new Baseline installs while the old one keeps running. **Staged** boards have room for only one Baseline: an update first restarts the board without its sketch, and erases the sketch and its files. See [OTA.md](OTA.md).

## Load times

### Building a sketch

Every board uses the same compiler setup, and a rebuild takes about **0.5 s**, the same on every chip:

| Chip | `01_hello_flick` | `02_wifi_http_get` | `01_ble_serial` | `06_mqtt_basic` |
|---|---|---|---|---|
| ESP32-S3 | 0.51 s | 0.52 s | 0.52 s | 0.52 s |
| ESP32 | 0.50 s | 0.52 s | 0.51 s | 0.52 s |
| ESP32-C6 | 0.50 s | 0.51 s | 0.50 s | 0.53 s |

These are medians of 3 `flick build` runs, Python's start-up included, measured on 2026-10-24. Earlier measurements on the Thing Plus S3 gave 0.44–0.57 s per example ([BUILD_RESULTS.md](https://github.com/sparkfun/SparkFun_Flick/blob/main/flick/docs/BUILD_RESULTS.md)). Before 0.3.0, build output went into the sketch folder, and a sketch in a cloud-synced folder could take about 4 s to build; output now goes to Flick's cache folder, so that no longer happens.

The first build after installing Flick, or after an SDK change, also compiles the SDK once per board, which adds a few seconds.

### Uploading a sketch

The upload speed depends on how the board connects over USB, not on the chip:

| Connection | Boards | Measured | Rate |
|---|---|---|---|
| Native USB | C6, C5, S3, C3, Pocket C6, P4 | 0.03 s for a small sketch (S3) | about 91 KB/s (2.1 MB in 23 s, Thing Plus C6) |
| CH340 USB-serial, 921600 baud during uploads | IoT RedBoard, DataLogger, Thing Plus C | 0.09 s for `01_hello_flick`, 1.3 KB; 0.43 s for the 15.8 KB selftest (IoT RedBoard) | about 60 KB/s (2.1 MB in 35 s, IoT RedBoard, Baseline 0.3.0; 17.5 KB/s with Baseline 0.2) |

These are **estimates** for bigger sketches, worked out from the rates above. Each also needs a moment to erase the slot and restart:

| Sketch size | Native USB | CH340 |
|---|---|---|
| 5 KB (most examples) | under 0.1 s | about 0.2 s (measured: `06_mqtt_basic`, 5.3 KB, 0.19 s) |
| 64 KB | about 0.7 s | about 1.1 s |
| 256 KB (largest on 4 MB boards) | about 2.8 s | about 4.3 s |

Build and upload together, as `flick upload` runs them (asking the board, building, uploading), measured on the IoT RedBoard over its CH340:

| Sketch | Build + upload |
|---|---|
| `blink` (benchmark) | 0.66 s |
| `hello_world` (benchmark) | 0.66 s |
| `01_ble_serial` | 0.69 s |
| `02_wifi_http_get` | 0.71 s |

For comparison, a plain Arduino build and upload of a small sketch on the Thing Plus S3 takes about 20 s: 12–15 s to compile, then 7–9 s to upload a 275–980 KB image. Flick takes about 0.5 s for the same sketches ([the readme's table](../../README.md)).

### Putting Baseline on a board (once)

| Board | How | Measured |
|---|---|---|
| IoT RedBoard (CH340) | `flick baseline flash --file`, esptool at 921600 | 21 s to write the 2.16 MB image (1.39 MB compressed), plus a few seconds to connect |
| Native-USB boards | the web page's Flash button, or esptool | not measured yet |

### Updating Baseline over the air

From [OTA.md](OTA.md), all measured:

| Board | How | Time |
|---|---|---|
| IoT RedBoard (dual) | USB push, 2.1 MB | 35 s (Baseline 0.3.0; 120 s with 0.2) |
| IoT RedBoard (dual) | WiFi push | 45 s |
| Thing Plus C6 (dual) | USB push | 23 s |
| Thing Plus C6 (dual) | pull from a GitHub release (1.28 MB `.bsu`) | 17 s, with the sketch running the whole time |
| Pro Micro C3 (staged) | USB push (1.26 MB `.bsu`) | 14 s, then about 30 s for the bootloader to install it |
| Pro Micro C3 (staged) | WiFi push | 23 s, then about 30 s to install |
| Qwiic Pocket C6 (staged) | USB push (1.28 MB) | 15 s, then the install |
| Qwiic Pocket C6 (staged) | WiFi push | 30 s, then the install |

With Baseline 0.2, USB was the slowest way to update a CH340 board (WiFi was 2.7 times faster); with 0.3.0, USB (35 s) is quicker than WiFi (45 s).

## Flash use

Baseline's size is the downloaded image minus its first 64 KB, which hold the bootloader and the partition table.

| Board | Image (`.bin`) | Baseline | Baseline slot | Used | Room left | Update file (`.bsu`) |
|---|---|---|---|---|---|---|
| IoT RedBoard - ESP32-P4 | 1.28 MB | 1.22 MB | 3.00 MB | 41 % | 1,824 KB | |
| DataLogger IoT | 2.04 MB | 1.97 MB | 3.00 MB | 66 % | 1,051 KB | |
| IoT RedBoard - ESP32 | 2.06 MB | 2.00 MB | 3.00 MB | 67 % | 1,022 KB | 1.30 MB |
| Thing Plus C6 | 2.10 MB | 2.03 MB | 3.00 MB | 68 % | 988 KB | 1.22 MB |
| Thing Plus C | 2.10 MB | 2.04 MB | 3.00 MB | 68 % | 986 KB | |
| RTK Postcard | 2.05 MB | 1.99 MB | 2.75 MB | 72 % | 782 KB | 1.29 MB |
| Thing Plus C5 | 2.14 MB | 2.08 MB | 2.75 MB | 76 % | 689 KB | |
| Thing Plus S3 | 1.88 MB | 1.81 MB | 2.19 MB | **83 %** | 382 KB | 1.17 MB |
| Pro Micro C3 | 2.08 MB | 2.02 MB | 2.19 MB | **92 %** | **174 KB** | 1.20 MB |
| Qwiic Pocket C6 | 2.10 MB | 2.04 MB | 2.19 MB | **93 %** | **155 KB** | 1.22 MB |

- **The 4 MB boards are nearly full.** Each new Baseline feature has to fit in what's left: about 155 KB on the Pocket C6 and 174 KB on the Pro Micro C3. Their partition table can't grow Baseline's slot without shrinking the sketch slots or the file system.
- **The P4's Baseline is the smallest** because it has no radio, so no WiFi or Bluetooth stack is built in.

### The same sketch on each chip

Image sizes in bytes, from the web page's example build:

| Board | `01_hello_flick` | `02_wifi_http_get` | `01_ble_serial` | `06_mqtt_basic` | All examples |
|---|---|---|---|---|---|
| ESP32 boards (RedBoard, DataLogger, Thing Plus C, Postcard) | 1,320 | 2,812 | 1,696 | 5,308 | 88–91 KB |
| Thing Plus S3 | 1,320 | 2,812 | 1,696 | 5,484 | 92 KB |
| RISC-V boards (C3, C6, C5, P4) | 1,384–1,416 | 3,148 | 1,808 | 6,652 | 101–105 KB |

RISC-V sketches are 5–25 % larger, partly because they're built without linker relaxation (the `gp` register belongs to Baseline). On boards whose only LED is the RGB one, `01_hello_flick` is 32 bytes larger; that's the code that turns `digitalWrite(LED_BUILTIN)` into the RGB LED's white. Neither difference matters at these sizes.

## Downsides of each platform

### Classic ESP32: IoT RedBoard, DataLogger IoT, Thing Plus C, RTK Postcard

- **Slower uploads.** These boards reach the PC through a USB-serial chip: about 60 KB/s with Baseline 0.3.0 (921600 baud during uploads, several lines in flight), against about 91 KB/s on native USB. Small sketches don't notice; a Baseline update over USB takes 35 s (2 minutes with Baseline 0.2).
- **The console runs at 115200** (since 0.3.0), the rate the ROM and bootloader use and serial monitors expect, so boot messages are readable. Uploads switch to 921600 by themselves for their length. Baselines 0.1 to 0.2 ran the console at 921600, which garbled the boot messages.
- **Tight RAM without PSRAM** (RedBoard, DataLogger, Thing Plus C). To fit WiFi and Bluetooth together, Baseline turns BLE Mesh off and makes several system tasks' stacks smaller. Only 25 KB of the chip's fast instruction RAM is left for Baseline to grow into.
- **RGB LED and LED strips are bit-banged** rather than driven by the RMT hardware, a holdover from when IRAM was scarcer. It works, but it hasn't been checked on a long strip.
- **Shared pins:** the IoT RedBoard's STAT LED shares GPIO 18 with SPI's SCK. The Postcard's STAT LED is also its BOOT button (GPIO 0).
- **The DataLogger** powers its Qwiic port and microSD slot from a switched 3.3 V rail on GPIO 32, which Baseline turns on at boot.
- **Upsides:** these are the only boards with Bluetooth Classic (`BluetoothSerial`) and a DAC (`dacWrite`); they have touch pins; and the 16 MB boards have the most room for files (8.9 MB).

### ESP32-S3: Thing Plus S3

- **Only 4 MB of flash.** Baseline updates are staged and erase the sketch and its files; sketches are limited to 256 KB; there's 1.19 MB for files; and Baseline's slot is 83 % full.
- **USB device mode has a cost.** When a sketch makes the board a USB keyboard or MIDI device, crash output is no longer visible over USB, and esptool can't reset the board (`flick baseline flash` handles that for you).
- **GPIO 0 is both the STAT LED and the BOOT button.**
- **Not fully tested on the current ABI:** `flick test` hasn't run on it since ABI 1.6.
- **Upsides:** the only board with 64 KB of sketch RAM; USB device mode (keyboard, mouse, MIDI); touch pins; 2 MB PSRAM; two cores.

### ESP32-C3: Pro Micro C3

- **Its Baseline slot is 92 % full**, with 174 KB left for new Baseline features.
- **One core at 160 MHz, no PSRAM:** WiFi, Bluetooth and the sketch share one core and about 400 KB of RAM.
- **WiFi stops while flash is being erased.** On a single-core chip the radio can't run during an erase. Long erases once broke Baseline updates over WiFi on the single-core Pocket C6 ([TESTING.md](https://github.com/sparkfun/SparkFun_Flick/blob/main/flick/TESTING.md) issue 16); updates now erase a little at a time.
- **Things it doesn't have:** an RGB LED (its LED is a plain blue one), touch pins, Bluetooth Classic and a DAC. `05_rgb_led`, `06_led_strip` and `05_touch_button` don't build for it.
- **Staged Baseline updates**, as on every 4 MB board.

### ESP32-C6: Thing Plus C6 and Qwiic Pocket C6

- **One core at 160 MHz, no PSRAM**, and the same WiFi-during-erase limit as the C3.
- **The Thing Plus C6's only LED is the RGB one.** Since 0.3.0, `digitalWrite(LED_BUILTIN, …)` lights it white, but `analogWrite` dimming is a white level rather than PWM.
- **The Qwiic Pocket C6 is a 4 MB board.** Its Baseline slot is 93 % full (155 KB left), updates are staged, and it has no RGB LED.
- **No touch pins, no Bluetooth Classic, no DAC.**
- **Upsides:** WiFi 6. The Thing Plus C6 has 16 MB, with dual Baseline slots and 8.9 MB for files.

### ESP32-C5: Thing Plus C5

- **One core**, and the same WiFi-during-erase limit as the other single-core chips.
- **The newest chip here** (revision 1.0), so its ESP-IDF and Arduino support has had the least time in use. Its BOOT-button pin for safe mode (GPIO 28) hasn't been confirmed on the board yet.
- **8 MB of flash:** 1.4 MB for files, and a 2.75 MB Baseline slot that's 76 % full.
- **Its only LED is the RGB one**, as on the Thing Plus C6. No touch pins, Bluetooth Classic or DAC.
- **Upsides:** dual-band WiFi 6 (2.4 and 5 GHz), 8 MB of PSRAM, and dual Baseline slots.

### ESP32-P4: IoT RedBoard - ESP32-P4, PoE and WiFi BLE versions

- **The PoE version has no networking yet.** The P4 has no radio, so WiFi, BLE and ESP-NOW calls return errors, and Baseline doesn't support its Ethernet port (DP83825 PHY) yet.
- **The WiFi BLE version** gets WiFi 6 and BLE from an ESP32-C5 beside the P4, over ESP-Hosted. Everything WiFi and BLE works, but there's **no ESP-NOW** (ESP-Hosted doesn't carry it: `<esp_now.h>` stops a sketch at compile time), and GPIO 14-20 belong to the link with the C5.
- **Unreleased boards:** no product pages yet. The BOOT-button pin for safe mode (GPIO 35) hasn't been confirmed.
- **No touch support yet:** the chip has touch hardware, but Flick's touch calls don't cover it, so `05_touch_button` doesn't build for it. No DAC.
- **Upsides:** the most memory by far (32 MB of PSRAM), two fast RISC-V cores with a hardware FPU, 16 MB of flash with dual Baseline slots, and the smallest Baseline (41 % of its slot).

### Teensy 4.x: Teensy 4.0, Teensy 4.1, MicroMod Teensy

- **No WiFi or Bluetooth**, and no over-the-air updates: Baseline goes on with Teensy Loader (the web page offers the `.hex`; it can't flash a Teensy itself yet).
- **No ESP32 names:** `esp_*`, `ledc*`, `ESP.` and the like don't exist; sketches written for both use `#if defined(BL_PLATFORM_ESP32)`.
- **Not every Teensy library yet:** the Audio library is built in; USB host (USBHost_t36) and Ethernet (QNEthernet) aren't.
- **Upsides:** the fastest CPU here (Cortex-M7 at 600 MHz with a double-precision FPU), 128 KB of sketch RAM, sketch code may run real interrupt handlers (`attachInterrupt`, `IntervalTimer`), and the 16 MB MicroMod has the most room for files (11.75 MB).

## Not measured yet

- Flashing Baseline from the web page, on any board, and with esptool on native-USB boards.
- The time from the end of an upload until the sketch runs (the board restarts).
- Build times on the C3, C5 and P4 compilers (the C6's RISC-V compiler measured the same as the S3's and ESP32's).
- Uploads of large sketches; the table above is an estimate.
- Baseline WiFi pushes on the Thing Plus C6, and anything on the Thing Plus C5, S3 and Thing Plus C beyond `flick test` (see [TESTING.md](https://github.com/sparkfun/SparkFun_Flick/blob/main/flick/TESTING.md)).
