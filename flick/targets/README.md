# Targets and chips

A **target** is one product: one chip on one board, for example the SparkFun RTK Postcard. Each target gets its own Baseline blob, and sketches built for it see that board's pins (`LED_BUILTIN`, `SDA`, ...).

Everything a target shares with the other boards built on the same chip lives in a **chip** folder. A new board on a supported chip only needs a target folder with two small files.

```
api/                     ONE API for every target (baseline_api.txt, abi.lock)
baseline/                Baseline source shared by every chip (Arduino-ESP32 + ESP-IDF)
sdk/                     sketch SDK shared by every target (headers, C/C++ runtime, linker script)
idf/                     the ESP-IDF project template, shared sdkconfig changes, prebuilt components
examples/                sketches; the same source builds for any target
chips/<chip>/            one per chip: esp32, esp32s3, esp32c3, esp32c5, esp32c6, esp32p4, imxrt1062, rp2040, rp2350
  chip.json              IDF target, sketch compiler and flags, USB IDs
  sdk/bl_target.h        chip ID, fixed sketch addresses (ABI); includes the target's bl_board.h
  sdk/rom.ld             ROM functions sketches may call directly
  baseline/src/          bl_port.h (console, pin rules), bl_port_mmu.c (maps the sketch slot), ...
  idf/sdkconfig.defaults this chip's sdkconfig changes
  partitions-<N>MB.csv   flash layout for each flash size
targets/<target>/        one per product
  target.json            "chip", "description", "flash_size" (picks partitions-<N>MB.csv); may
                         override anything in chip.json
  sdk/bl_board.h         target name, board name, pins seen by sketches, Baseline defaults
                         (Serial1/Serial2 pins, microSD wiring)
  idf/sdkconfig.defaults (optional) this board's sdkconfig changes, e.g. CONFIG_ARDUINO_VARIANT
  baseline/              (optional) files that replace or add to the Baseline source
dist/<target>/baseline-<target>.bin    the built blob (flash at 0x0)
```

## Targets

| Target | Chip | Product | Status |
|---|---|---|---|
| `thing_plus_esp32` | esp32 | SparkFun Thing Plus - ESP32 WROOM (USB-C), the "Thing Plus C" | tested |
| `rtk_postcard` | esp32 | SparkFun RTK Postcard (ESP32-PICO-MINI-02, LG290P GNSS) | chip tested; board pins from the docs |
| `iot_redboard_esp32` | esp32 | SparkFun IoT RedBoard - ESP32 | tested |
| `datalogger_iot` | esp32 | SparkFun DataLogger IoT (and 9DoF) | `flick test` passes on the base board (2026-10-22); 9DoF not run |
| `iot_redboard_esp32p4_poe` | esp32p4 | SparkFun IoT RedBoard - ESP32-P4, PoE version (no radio; Ethernet) | `flick test` passes (2026-10-23); Ethernet not yet supported |
| `iot_redboard_esp32p4_wifi` | esp32p4 | SparkFun IoT RedBoard - ESP32-P4, WiFi BLE version (ESP32-C5 co-processor over ESP-Hosted; `idf_dependencies` in its target.json) | `flick test` 38/0/0 (2026-11-17, WiFi and the while(1) stop included); no ESP-NOW |
| `thing_plus_esp32s3` | esp32s3 | SparkFun Thing Plus - ESP32-S3 | tested |
| `pro_micro_esp32c3` | esp32c3 | SparkFun Pro Micro - ESP32-C3 | not run on hardware |
| `qwiic_pocket_esp32c6` | esp32c6 | SparkFun Qwiic Pocket Development Board - ESP32-C6 | not run on hardware |
| `thing_plus_esp32c6` | esp32c6 | SparkFun Thing Plus - ESP32-C6 (16 MB flash) | tested: `flick test` 31/31 (with WiFi, microSD), RGB LED by eye |
| `thing_plus_esp32c5` | esp32c5 | SparkFun Thing Plus - ESP32-C5 | not run on hardware |
| `teensy40` | imxrt1062 | PJRC Teensy 4.0 (Baseline built with Teensyduino, `chips/imxrt1062/baseline`) | tested: `flick test` 24/0/5, sketch interrupts 7/7, Audio 8/8 (2026-10-24) |
| `teensy41` | imxrt1062 | PJRC Teensy 4.1 (8 MB flash, PSRAM pads, SDIO microSD, Ethernet with NativeEthernet) | `flick test` 26/0/4 (2026-11-17); Ethernet HTTP not yet run |
| `teensy40_hid`, `teensy41_hid`, `teensy_micromod_hid` | imxrt1062 | The same boards with USB type Serial + Keyboard + Mouse + Joystick instead of Serial + MIDI + Audio (`variant_of` in target.json; bl_board.h includes the base's) | MicroMod: enumerates, mouse moves the cursor (2026-11-17) |
| `teensy_micromod` | imxrt1062 | SparkFun MicroMod Teensy Processor (16 MB flash, no PSRAM; Teensyduino's `teensyMM`, model 0x26; Serial2 on 16/17) | `flick test` 25/0/5 (2026-11-17) |
| `pro_micro_rp2350` | rp2350 | SparkFun Pro Micro - RP2350 (16 MB flash, 8 MB PSRAM; Baseline built with arduino-pico, `chips/rp2350/baseline`) | tested: `flick test` 30/0/4, LED by eye (2026-11-17) |
| `thing_plus_rp2350` | rp2350 | SparkFun Thing Plus - RP2350 (RM2 radio: WiFi and BLE) | tested: `flick test` 37/0/1 with WiFi and BLE, microSD, fuel gauge, BMI270 + OLED, LEDs (2026-11-17) |
| `thing_plus_rp2040` | rp2040 | SparkFun Thing Plus - RP2040 (16 MB flash; same Baseline source as the RP2350) | tested: `flick test` 27/0/3 (microSD, fuel gauge), LEDs (2026-11-17) |
| `pro_micro_rp2040` | rp2040 | SparkFun Pro Micro - RP2040 (16 MB flash) | tested: `flick test` 25/0/5, BMI270 + OLED (2026-11-17) |

**Generic boards** (not run on hardware): `<chip>_generic_<N>mb` for any board on that chip with N MB of flash, with the pins of Arduino-ESP32's generic variant for the chip (`esp32`, `esp32s3`, `esp32c3`, `esp32c6`, `esp32c5`):

| Chip | Targets | Notes |
|---|---|---|
| esp32 | `esp32_generic_4mb`, `_8mb`, `_16mb` | PSRAM found at runtime; the IoT RedBoard's no-PSRAM memory settings |
| esp32s3 | `esp32s3_generic_4mb`, `_8mb`, `_16mb` | quad PSRAM (R2) or none |
| esp32s3 | `esp32s3_generic_8mb_octal`, `_16mb_octal` | octal PSRAM (N8R8, N16R8): `CONFIG_SPIRAM_MODE_OCT`, GPIO 33-37 refused |
| esp32c3 | `esp32c3_generic_4mb` | native USB boards only (the console is USB-Serial-JTAG) |
| esp32c6 | `esp32c6_generic_4mb`, `_8mb` | native USB port |
| esp32c5 | `esp32c5_generic_8mb` | native USB port |
| rp2350 | `rp2350_generic_pico2` | Raspberry Pi Pico 2 (arduino-pico's `rpipico2` pins), 4 MB layout: Baseline 1.75 MB, two 512 KB slots, LittleFS 1.25 MB |
| rp2350 | `rp2350_generic_pico2w` | Raspberry Pi Pico 2 W (`rpipico2w`): WiFi and BLE on its CYW43439, same 4 MB layout |

PSRAM needs no image of its own: Baseline looks for it at boot and runs without it. The flash size does (it picks the partition table), and so does the S3's PSRAM type (quad and octal are wired differently). A board with more flash than its image works, with the extra flash unused.

The classic ESP32 targets need chip revision 3.0 or later (`esptool flash-id` shows it).

## The rules

- **The API table is the same on every target.** A function a chip can't do still has its slot and returns `BL_ERR_UNSUPPORTED`. For example, Bluetooth Classic SPP on the S3. A sketch therefore compiles everywhere and fails gracefully at run time.
- **A sketch binary belongs to one target.** Its header carries the chip ID and Baseline refuses a sketch built for another chip; within a chip, the board's pins are compiled in. `flick upload` asks the board which target it runs (`INFO` reports `chip=` and `target=`) and builds for that target automatically.
- **Later files win.** `flick baseline` copies `baseline/`, then `chips/<chip>/baseline/`, then `targets/<t>/baseline/` (if any) into `%LOCALAPPDATA%\Flick\<t>\idf\main`, and builds that copy. The sketch include path has the target's `sdk/` before the chip's.
- **Board facts go in `bl_board.h`, never in Baseline code.** Baseline's default pins (`Wire.begin()`, `SPI.begin()`, `SD.begin()`, `Serial1.begin()`) come from it, not from the Arduino variant, because all classic-ESP32 boards share the generic `esp32` variant.

## Commands

```
python flick.py targets                                   # list targets
python flick.py baseline build -t rtk_postcard            # -> dist/rtk_postcard/baseline-rtk_postcard.bin
python flick.py baseline flash -t rtk_postcard            # build and flash it
python flick.py build examples/01_basics/01_hello_flick -t rtk_postcard   # build a sketch for a target
python flick.py upload examples/01_basics/01_hello_flick      # the board says which target it is
```

## Adding a target

**A new board on a supported chip**
1. Create `targets/<name>/target.json` with `"chip"`, `"description"` and `"flash_size"` (a size the chip has a `partitions-<N>MB.csv` for).
2. Create `targets/<name>/sdk/bl_board.h`: copy one from a board on the same chip and change the pins. Arduino-ESP32's `variants/<board>/pins_arduino.h` and the board's hookup guide are the sources. `BL_TARGET_NAME` must equal the folder name.
3. If the board has an Arduino variant of its own, set `CONFIG_ARDUINO_VARIANT` in `targets/<name>/idf/sdkconfig.defaults`.
4. `flick baseline build -t <name>`, then flash it and run a sketch.

**A new ESP32-family chip (H2, C61, P4, S2...)**
1. Copy the closest chip folder: `esp32s3` for Xtensa, `esp32c6` for RISC-V.
2. In `chip.json`, set `idf_target`, the sketch compiler (`xtensa-*-elf-gcc` or `riscv32-esp-elf-gcc`), its flags and the USB IDs. RISC-V chips use `-mno-relax -msmall-data-limit=0`, because the `gp` register belongs to Baseline.
3. In `sdk/bl_target.h`, choose the sketch windows and the RAM window. Read the chip's `soc/ext_mem_defs.h` and IDF's `esp_system/ld/<chip>/memory.ld.in`. Check the result with `nm` on the built Baseline ELF: `_text_end`, `_rodata_reserved_end` and `_heap_start` must stay below the windows.
4. Build `sdk/rom.ld` from the chip's `esp_rom/<chip>/ld/*.rom.libc*.ld` / `*.newlib*.ld`.
5. Write `baseline/src/bl_port_mmu.c` for the chip's MMU, and `baseline/src/bl_port.h` for its console and pin rules.
6. Add `partitions-<N>MB.csv` for the flash sizes its boards have.
7. If the chip has a single core, Baseline runs the sketch task on core 0 (`CONFIG_FREERTOS_UNICORE`).

**A non-ESP32 chip (nRF52, RP2040, ...)**

This needs a new Baseline source folder next to `baseline/` (for example `baseline-nrf52/`), selected with `"baseline_source"` in `chip.json`. Of the existing code, these parts carry over unchanged:
- the loader protocol
- the slot and safe-mode logic
- the generated API table
- the SDK

The MMU mapping does not. On a Cortex-M, flash is directly executable at a fixed address, so the chip needs another way to show the active slot at one address: the i.MX RT1062 has FlexSPI's address remap, the RP2350 the QMI's address translation (the last 4 MB of its flash window, ATRANS3), and the RP2040, which has neither, a window that is a flash region of its own, into which Baseline copies the active slot (`BL_TARGET_COPIED_WINDOW`).

**RP2040 and RP2350** (`chips/rp2040`, `chips/rp2350`): one Baseline source, `chips/rp2350/baseline`, built with arduino-pico (Earle Philhower's core) through arduino-cli (`"builder": "rp2"`). Install it once: `arduino-cli core install rp2040:rp2040 --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`. Sketches build with arduino-pico's own compiler (`"gcc_package"`), for the float ABI Baseline uses (soft on the RP2040, softfp on the RP2350). The target's `bl_board.h` gives the flash layout (Baseline, LittleFS, EEPROM sector, slots), which flick passes to arduino-pico as the build properties of its Flash Size menu; the heap ends at the chip's sketch RAM window (`build.ram_length`). The sketch's `setup()`/`loop()` run on the process stack (PSP), sized by the sketch header (PSPLIM catches an overflow on the RP2350). The image is a `.uf2`; `flick baseline flash` uses arduino-pico's uploader (1200 baud reboots the board into its boot ROM).

**ATmega328 and other 8-bit AVRs** are a poor fit. With 32 KB of flash and 2 KB of RAM, there is no room for a meaningful precompiled blob, and the 185-entry function table alone would use 370 bytes. Arduino's normal build is already fast on those chips.
