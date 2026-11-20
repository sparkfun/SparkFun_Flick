# Baseline extensions

Some products need code that a general-purpose Baseline should not carry: a
Bluetooth profile only one product speaks, a licensed protocol, a driver for a
custom chip. A **Baseline extension** is that code, kept in the product's own
repository and built into a Baseline for that product. Flick itself stays the
same for everyone.

| | Stock Baseline | Baseline with an extension |
|---|---|---|
| Built by | Flick's releases (the download page) | the product: `flick baseline build --ext <folder>` |
| Extension code | not there: `bl_ext_find()` returns NULL | there: `bl_ext_find()` returns what it registered |
| Sketches | every sketch for the target | the same, plus the features the extension adds |

A board running a Baseline with an extension still reports its usual target, so
every other sketch for that target runs on it too. The download page's
**Flash** puts the stock Baseline back, which drops the extension.

## Writing one

An extension is a folder whose name is the extension's name (lowercase letters,
digits, `_`). It holds C/C++ sources and headers. Its code is Baseline code: it
runs outside the sketch, may call ESP-IDF (or the Teensy / arduino-pico core)
directly, and is not limited by the sketch rules in AGENTS.md.

It registers what sketches may use, usually a `const` struct of function
pointers, with `BL_EXTENSION(name, pointer)` from `baseline_extension.h`:

```cpp
// my_ext/my_ext_api.h: the interface, included by the extension and by sketches
typedef struct {
    uint32_t version;
    int (*begin)(const char *name);
    int (*read)(uint8_t *buf, size_t size);
} my_ext_api_t;

// my_ext/my_ext.cpp
#include "baseline_extension.h"
#include "my_ext_api.h"
static int my_begin(const char *name) { /* ESP-IDF calls */ return 0; }
static int my_read(uint8_t *buf, size_t size) { return 0; }
static const my_ext_api_t api = {1, my_begin, my_read};
BL_EXTENSION("my_product.my_ext", &api);
```

The sketch names the extension in its `sketch.cfg` (the path is relative to the
sketch folder), which puts the extension's headers on the sketch's include path:

```
extensions = ../my_ext
```

and looks it up at run time:

```cpp
#include "my_ext_api.h"
const my_ext_api_t *ext = (const my_ext_api_t *)bl_ext_find("my_product.my_ext");
if (!ext) {
    Serial.println("this Baseline has no my_ext");   // e.g. the stock Baseline
} else {
    ext->begin("hello");
}
```

Rules:

- **The interface is the product's own.** Flick knows only the name and the
  pointer. Keep the struct in one header that both sides include, and only add
  to its end, with a version field, so sketches built against an older layout
  keep working.
- **Sketch code runs on sketch tasks.** An extension's own tasks and callbacks
  (Bluetooth, WiFi, timers) should hand events to the sketch through a queue or
  a flag it polls, not call sketch functions from them, and never from an
  interrupt.
- **Names:** use `<product>.<feature>` so two extensions never collide.

## Building and installing

```
python flick.py baseline build  -t rtk_postcard --ext ../My_Product/baseline_ext/my_ext
python flick.py baseline update -t rtk_postcard --ext ../My_Product/baseline_ext/my_ext -p COM5
python flick.py baseline flash  -t rtk_postcard --ext ../My_Product/baseline_ext/my_ext -p COM5
```

`--ext` may be given more than once. The build is named `<target>-<ext>`: its
files are `flick/dist/<target>-<ext>/baseline-<target>-<ext>.bin` and `.bsu`,
next to the stock ones, which it leaves alone. The `.bsu` works with every way
of updating Baseline (USB, WiFi, `BaselineUpdate.baselineFromUrl`), so a
product can serve its own Baseline updates.

How each builder takes the folder:

- **ESP32 family (ESP-IDF):** a component of its own,
  `components/bl_ext_<name>`, linked whole so its `BL_EXTENSION`s are kept. Its
  `CMakeLists.txt` is `flick/idf/ext_component.cmake` unless the folder brings
  one: all of its sources, the Arduino-ESP32 component and the ESP-IDF
  components that one uses (Bluetooth, WiFi, mbedTLS, ...). An extension that
  needs another component brings its own `CMakeLists.txt` with `REQUIRES`.
- **Teensy and RP2 (arduino-cli):** the sources go into the Baseline sketch's
  `src/ext/<name>/`.

## Example

`flick/tools/tests/ext_demo` is a small extension and `flick/tools/tests/ext_test`
the sketch that checks it. On a stock Baseline the test reports the extension as
missing (SKIP); with `--ext flick/tools/tests/ext_demo` every check runs.
