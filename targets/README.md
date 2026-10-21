# Targets

Each folder here is one **target**: one chip on one board. Each target produces one Baseline blob. Everything else in the repository is shared by all targets.

```
api/                     ONE API for every target (baseline_api.txt, abi.lock)
baseline/                Baseline source shared by the ESP32 family (Arduino-ESP32)
sdk/                     sketch SDK shared by every target (headers, C/C++ runtime, linker script)
examples/                sketches; the same source builds for any target
targets/
  esp32s3/               SparkFun ESP32-S3 Thing Plus
  esp32/                 SparkFun Thing Plus C and other classic ESP32s, rev 3.0+ (built with ESP-IDF)
    target.json          builder ("idf", or an arduino-cli FQBN), sketch compiler and flags, USB IDs
    idf/sdkconfig.defaults   (idf builder) this target's sdkconfig changes
    sdk/bl_target.h      chip ID, fixed sketch addresses, board pins (LED_BUILTIN, SDA, ...)
    sdk/rom.ld           ROM functions sketches may call directly
    baseline/            files added to (or replacing) baseline/ for this target:
      partitions.csv       flash layout
      src/bl_port.h        console type, pin rules, default UART pins
      src/bl_port_mmu.c    maps the sketch slot at its fixed address
idf/                     (idf builder) the ESP-IDF project template, shared sdkconfig changes,
                         and the trimmed manifest for Arduino-ESP32 as a component
dist/<target>/baseline-<target>.bin    the built blob (flash at 0x0)
```

## The rules

- **The API table is the same on every target.** A function a chip can't do still has its slot and returns `BL_ERR_UNSUPPORTED`. For example, Bluetooth Classic SPP on the S3. A sketch therefore compiles everywhere and fails gracefully at run time.
- **A sketch binary belongs to one target.** The chip ID and fixed addresses are in its header, and Baseline refuses a sketch built for another chip. `bsk upload` asks the board what it is and builds for that target automatically.
- **Target files win.** `bsk baseline` copies `baseline/`, then `targets/<t>/baseline/` over it, into `%LOCALAPPDATA%\BaselineSketcher\<t>\baseline`, and compiles that copy.

## Commands

```
python tools/bsk.py targets                       # list targets
python tools/bsk.py baseline build -t esp32       # -> dist/esp32/baseline-esp32.bin
python tools/bsk.py baseline flash -t esp32       # compile and flash it
python tools/bsk.py build examples/blink -t esp32 # build a sketch for a target
python tools/bsk.py upload examples/blink         # the board says which target it is
```

## Adding a target

**Another ESP32-family chip (C3, C5, C6, H2, S2...)**
1. Copy the closest target folder.
2. In `target.json`, set the board FQBN, the compiler and its flags, and the USB IDs.
3. In `sdk/bl_target.h`, choose the sketch windows and the RAM window. Read the chip's `soc/ext_mem_defs.h` and `ld/memory.ld` in the Arduino-ESP32 libs. Check the result with `nm` on the built Baseline ELF: `_text_end`, `_rodata_reserved_end` and `_heap_start` must stay below the windows.
4. Copy `sdk/rom.ld` from the chip's `esp32xx.rom.libc-funcs.ld`.
5. Write `baseline/src/bl_port_mmu.c` for the chip's MMU.
6. Write `baseline/src/bl_port.h` for its pins and console.

The RISC-V chips (C3, C6, H2, C5) also need a RISC-V linker script, because there are no literal pools. They also need a RISC-V sketch compiler (`riscv32-esp-elf-gcc`) and `-march` flags in `target.json`.

**A non-ESP32 chip (nRF52, RP2040, ...)**

This needs a new Baseline source folder next to `baseline/` (for example `baseline-nrf52/`), selected with `"baseline_source"` in `target.json`. Of the existing code, these parts carry over unchanged:
- the loader protocol
- the slot and safe-mode logic
- the generated API table
- the SDK

The MMU mapping does not. On a Cortex-M, flash is directly executable at a fixed address, so the "mapping" step disappears: the sketch is linked at its slot's flash address.

**ATmega328 and other 8-bit AVRs** are a poor fit. With 32 KB of flash and 2 KB of RAM, there is no room for a meaningful precompiled blob, and the 185-entry function table alone would use 370 bytes. Arduino's normal build is already fast on those chips.
