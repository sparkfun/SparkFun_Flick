#!/usr/bin/env python3
"""bsk - Baseline Sketcher command-line tool.

  bsk targets                       list the targets (one per product) in targets/
  bsk new <dir>                     create a sketch project
  bsk build [dir] [-t TARGET]       compile + link + package -> <dir>/build/<target>/<name>.bsk
  bsk upload [dir] [-m]             ask the board which target it is, build for it, upload, run
  bsk upload [dir] --host NAME      same, over WiFi (the running sketch called ArduinoOTA.begin())
  bsk monitor                       serial console (Ctrl+C to quit)
  bsk info                          ask Baseline for its version / ABI / chip / target / active slot
  bsk safe                          reboot Baseline into safe mode (sketch not started)
  bsk test                          bring-up test of the plugged-in board (selftest, rollback, watchdog)
  bsk baseline build -t TARGET      compile a Baseline blob -> dist/<target>/baseline-<target>.bin
  bsk baseline flash -t TARGET      compile and flash Baseline (rarely needed)

The build target is taken from -t, else `target = ...` in the sketch's
sketch.cfg, else thing_plus_esp32s3. `upload` always builds for the chip on the board.

Commands that talk to the board find it by its USB ID (any target's
usb_vids in target.json); pass -p PORT to choose one yourself.

The toolchain is found in this order: $BSK_GCC, the target's gcc on PATH,
then an Arduino-ESP32 install (%LOCALAPPDATA%/Arduino15 or ~/.arduino15).
arduino-cli: $BSK_ARDUINO_CLI, PATH, then the copy bundled with Arduino IDE 2.
"""
import argparse
import base64
import codecs
import glob
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK = ROOT / "sdk"
TARGETS = ROOT / "targets"
CHIPS = ROOT / "chips"
DIST = ROOT / "dist"
DEFAULT_TARGET = "thing_plus_esp32s3"
# Target names Baselines built before the per-product targets report as chip=.
LEGACY_TARGETS = {"esp32": "thing_plus_esp32", "esp32s3": "thing_plus_esp32s3"}

# Must match sdk/include/sketch_abi.h (the target addresses come from bl_target.h)
HEADER_SIZE = 0x100
MAGIC = 0x4B534C42
HDR_OFF_IMAGE_SIZE = 12
HDR_OFF_BUILD_ID = 96
HDR_OFF_SHA256 = HEADER_SIZE - 32

COMMON_FLAGS = [
    "-Os", "-g3",
    "-ffunction-sections", "-fdata-sections", "-fno-common",
    "-fno-asynchronous-unwind-tables", "-fno-exceptions",
    "-fstrict-volatile-bitfields",
    "-Wall", "-Wextra", "-Wno-unused-parameter",
]
CFLAGS = COMMON_FLAGS + ["-std=gnu17", "-Werror=implicit-function-declaration"]
# .ino and .cpp: C++ like Arduino, minus exceptions/RTTI (no C++ runtime in the sketch)
CXXFLAGS = COMMON_FLAGS + ["-std=gnu++17", "-fno-rtti", "-fno-threadsafe-statics"]

PROTO_PREFIX = "@BSK"
CHUNK = 768          # raw bytes per DATA line (1024 chars of base64)


# ---------------------------------------------------------------- targets

def target_names():
    return sorted(p.name for p in TARGETS.iterdir() if (p / "target.json").exists())


def header_defines(path):
    """Numeric and string #defines from a header (bl_target.h)."""
    out = {}
    for m in re.finditer(r'^\s*#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+|"[^"]*")', path.read_text(), re.M):
        v = m.group(2)
        out[m.group(1)] = v.strip('"') if v.startswith('"') else int(v, 0)
    return out


def load_target(name):
    """A product target: chips/<chip>/chip.json overlaid with targets/<name>/target.json,
    plus the sketch addresses from the chip's bl_target.h.

    dirs lists the folders whose files make up the target, chip first: later
    ones win when both have a file (Baseline sources), and sdk_dirs puts the
    board's first on the include path."""
    name = LEGACY_TARGETS.get(name, name)
    d = TARGETS / name
    if not (d / "target.json").exists():
        sys.exit(f"error: unknown target '{name}'. Targets: {', '.join(target_names())}")
    board = json.loads((d / "target.json").read_text())
    chip_dir = CHIPS / board["chip"]
    t = json.loads((chip_dir / "chip.json").read_text())
    t.update(board)
    t["name"] = name
    t["dir"] = d
    t["chip_dir"] = chip_dir
    t["dirs"] = [chip_dir, d]
    t["sdk_dirs"] = [d / "sdk", chip_dir / "sdk"]
    defs = header_defines(chip_dir / "sdk" / "bl_target.h")
    t["ibus"], t["dbus"] = defs["BL_SKETCH_IBUS_BASE"], defs["BL_SKETCH_DBUS_BASE"]
    t["max_size"] = defs["BL_SKETCH_MAX_SIZE"]
    return t


def cmd_targets(args):
    for name in target_names():
        t = load_target(name)
        print(f"{name:20} {t['chip']:8} {t['description']}")


# ---------------------------------------------------------------- toolchain

def find_gcc(target):
    env = os.environ.get("BSK_GCC")
    if env:
        return env
    gcc_name = target["gcc"]
    on_path = shutil.which(gcc_name)
    if on_path:
        return on_path
    exe = gcc_name + (".exe" if os.name == "nt" else "")
    roots = []
    if os.environ.get("LOCALAPPDATA"):
        roots.append(Path(os.environ["LOCALAPPDATA"]) / "Arduino15")
    roots.append(Path.home() / ".arduino15")
    roots.append(Path.home() / "Library" / "Arduino15")
    for r in roots:
        hits = sorted(glob.glob(str(r / "packages" / "esp32" / "tools" / "esp-*32" / "*" / "bin" / exe)))  # esp-x32, esp-rv32
        if hits:
            return hits[-1]
    sys.exit(f"error: {gcc_name} not found. Set BSK_GCC or install ESP-IDF / Arduino-ESP32.")


def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        sys.exit(r.returncode)
    return r


# ---------------------------------------------------------------- project

SKETCH_TEMPLATE = """\
// {name}.ino - a Baseline Sketcher sketch. baseline.h is included automatically.

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.printf("Hello from {name} on Baseline %s (%s)\\n", bl_baseline_version(), ESP.getChipModel());
}

void loop()
{
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
"""


def secret_defines():
    """-DKEY="value" for each KEY = value in secrets.cfg (repo root, or $BSK_SECRETS).
    Keeps WiFi passwords and API keys out of sketches: #ifdef WIFI_SSID ... in code."""
    f = Path(os.environ.get("BSK_SECRETS") or ROOT / "secrets.cfg")
    out = []
    if f.exists():
        for line in f.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = (x.strip() for x in line.split("=", 1))
                if re.fullmatch(r"[A-Z_][A-Z0-9_]*", k):
                    v = v.replace("\\", "\\\\").replace('"', '\\"')
                    out.append(f'-D{k}="{v}"')
    return out


def load_config(proj):
    """Name defaults to the folder name. An optional sketch.cfg may set
    `name = ...`, `stack = <bytes>` and `target = <target>`."""
    cfg = {"name": proj.name, "stack": "8192"}
    f = proj / "sketch.cfg"
    if f.exists():
        for line in f.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if "=" in line:
                k, v = (s.strip() for s in line.split("=", 1))
                cfg[k] = v
    if len(cfg["name"].encode()) > 31:
        sys.exit("error: sketch name must be at most 31 bytes")
    return cfg


def cmd_new(args):
    proj = Path(args.dir)
    if proj.exists() and any(proj.iterdir()):
        sys.exit(f"error: {proj} exists and is not empty")
    proj.mkdir(parents=True, exist_ok=True)
    name = proj.name
    (proj / f"{name}.ino").write_text(SKETCH_TEMPLATE.replace("{name}", name))
    print(f"created {proj / (name + '.ino')}")


# ---------------------------------------------------------------- build

ARDUINO_BENCH = ROOT / "benchmarks" / "arduino"

# Seen by sketches and libraries, like an Arduino-ESP32 build. Libraries test
# these to pick their ESP32 code paths.
PLATFORM_DEFINES = ["-DARDUINO=10812", "-DARDUINO_ARCH_ESP32", "-DESP32"]

# ---------------------------------------------------------------- .ino prototypes
# Like the Arduino IDE, declare the functions a .ino defines, so they can be
# called before their definition. The declarations go just before the first
# function definition (after the sketch's own types), and #line keeps compiler
# messages pointing at the original file.

PROTO_KEYWORDS = {"if", "for", "while", "switch", "return", "else", "do", "sizeof", "catch"}
PROTO_DECLARED = {"setup", "loop", "teardown"}      # baseline.h declares these
FUNC_DEF_RE = re.compile(r"(?:^|[;}\n])\s*((?:[A-Za-z_][\w:<>,\*&\s]*?[\s\*&])?([A-Za-z_]\w*)\s*\(([^;{}()]*)\)\s*(?:const\s*)?)\{")


def _blank_comments_and_strings(text):
    """Comments, string and char literals, and preprocessor lines replaced by
    spaces (newlines kept), so offsets still match the original text."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i) or (c == "#" and (i == 0 or text[i - 1] == "\n" or text[:i].rstrip(" \t").endswith("\n"))):
            j = i
            while j < n and text[j] != "\n":
                if text[j] == "\\" and j + 1 < n and text[j + 1] == "\n":
                    j += 1          # continued line
                j += 1
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            for k in range(i, min(j + 1, n)):
                if out[k] != "\n":
                    out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def ino_with_prototypes(src):
    """The .ino's text with prototypes added, or None if it needs none."""
    text = src.read_text(errors="ignore")
    clean = _blank_comments_and_strings(text)
    # Top level only: blank out everything inside braces (keeping the braces).
    flat, depth = [], 0
    for ch in clean:
        if ch == "{":
            flat.append(ch if depth == 0 else " ")
            depth += 1
        elif ch == "}":
            depth = max(0, depth - 1)
            flat.append(ch if depth == 0 else " ")
        else:
            flat.append(ch if depth == 0 or ch == "\n" else " ")
    flat = "".join(flat)
    protos, first = [], None
    for m in FUNC_DEF_RE.finditer(flat):
        sig, name, args = m.group(1), m.group(2), m.group(3)
        head = sig[: sig.rfind(name)].strip()
        if name in PROTO_KEYWORDS or not head or re.search(r"\b(class|struct|enum|union|namespace|template|operator)\b|\[", sig):
            continue
        if first is None:
            first = m.start(1)          # prototypes go before the first function, even setup()
        if name in PROTO_DECLARED or "=" in args:
            continue                    # already declared / default arguments may not be repeated
        protos.append(" ".join(sig.split()) + ";")
    if not protos:
        return None
    line = text.count("\n", 0, first) + 1
    # Insert at the start of the line that holds the first definition.
    cut = text.rfind("\n", 0, first) + 1
    name = src.as_posix()
    return (f'#line 1 "{name}"\n' + text[:cut] + "\n".join(protos) + "\n" +
            f'#line {line} "{name}"\n' + text[cut:])


# ---------------------------------------------------------------- Arduino libraries
# Sketches can use ordinary Arduino libraries (SparkFun Qwiic drivers, ArduinoJson,
# ...). Like the Arduino builder, bsk follows #include lines: a header that is not
# in the sketch or the SDK selects the library that provides it, whose sources are
# compiled into the sketch, and its own #includes are followed in turn.

INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', re.M)
LIB_SKIP_DIRS = {"examples", "extras", "test", "tests", "docs", "doc", ".git", ".github", ".development"}


def arduino_user_dirs():
    """Sketchbook folders from the Arduino configs we know about, then the default."""
    dirs = []
    cfgs = [Path.home() / ".arduinoIDE" / "arduino-cli.yaml"]
    if os.environ.get("LOCALAPPDATA"):
        cfgs.append(Path(os.environ["LOCALAPPDATA"]) / "Arduino15" / "arduino-cli.yaml")
    cli = shutil.which("arduino-cli") or os.environ.get("BSK_ARDUINO_CLI")
    if cli:
        cfgs.append(Path(cli).parent / "arduino-cli.yaml")
    for cfg in cfgs:
        if cfg.exists():
            for line in cfg.read_text(errors="ignore").splitlines():
                if line.strip().startswith("user:"):
                    dirs.append(Path(line.split(":", 1)[1].strip()))
    dirs.append(Path.home() / "Documents" / "Arduino")
    return dirs


def library_roots(proj):
    roots = [proj / "libraries"]
    roots += [Path(p) for p in os.environ.get("BSK_LIBRARIES", "").split(os.pathsep) if p]
    roots += [d / "libraries" for d in arduino_user_dirs()]
    seen, out = set(), []
    for r in roots:
        if r.is_dir() and r.resolve() not in seen:
            seen.add(r.resolve())
            out.append(r)
    return out


def lib_src_dirs(lib):
    """Include folders of a library: src/ (1.5 format), else the root (+ utility/)."""
    if (lib / "src").is_dir():
        return [lib / "src"]
    return [lib] + ([lib / "utility"] if (lib / "utility").is_dir() else [])


def lib_files(lib):
    """Every source and header of a library, skipping examples, tests and docs."""
    if (lib / "src").is_dir():
        found = [f for f in (lib / "src").rglob("*")
                 if f.is_file() and not LIB_SKIP_DIRS.intersection(f.relative_to(lib).parts)]
    else:
        found = [f for f in lib.iterdir() if f.is_file()]
        if (lib / "utility").is_dir():
            found += [f for f in (lib / "utility").rglob("*") if f.is_file()]
    return [f for f in found if f.suffix in (".c", ".cpp", ".h", ".hpp")]


def index_libraries(roots):
    """header path (as #included) -> library folder; earlier roots win."""
    idx = {}
    for root in roots:
        for lib in sorted(p for p in root.iterdir() if p.is_dir()):
            for d in lib_src_dirs(lib):
                for h in d.rglob("*.h*"):
                    if h.suffix in (".h", ".hpp") and not LIB_SKIP_DIRS.intersection(h.relative_to(lib).parts):
                        idx.setdefault(h.relative_to(d).as_posix(), lib)
    return idx


def resolve_libraries(proj, sources, include_dirs):
    """The libraries the sketch needs, in discovery order."""
    roots = library_roots(proj)
    if not roots:
        return []
    idx = None
    libs, known = [], list(include_dirs)
    queue, seen = list(sources), set()
    while queue:
        f = queue.pop()
        if f in seen:
            continue
        seen.add(f)
        for name in INCLUDE_RE.findall(f.read_text(errors="ignore")):
            if (f.parent / name).exists() or any((d / name).exists() for d in known):
                continue
            if idx is None:
                idx = index_libraries(roots)
            lib = idx.get(name)
            if lib and lib not in libs:
                libs.append(lib)
                known += lib_src_dirs(lib)
                queue += lib_files(lib)
    return libs


def build(proj, target_name=None):
    proj = Path(proj).resolve()
    if proj == ARDUINO_BENCH or ARDUINO_BENCH in proj.parents:
        sys.exit(f"error: {proj.relative_to(ROOT)} is a plain Arduino sketch, kept only for the build-time "
                 f"comparison (tools/benchmark.py). It does not run on Baseline.\n"
                 f"Open the Baseline version instead: examples/{proj.name}/{proj.name}.ino")
    cfg = load_config(proj)
    inos = sorted(proj.glob("*.ino"))
    user = inos + sorted(proj.glob("*.c")) + sorted(proj.glob("*.cpp"))
    if not user:
        sys.exit(f"error: no .ino, .c or .cpp files in {proj}")
    if inos and not (proj / (proj.name + ".ino")).exists():
        print(f"warning: expected {proj.name}.ino (Arduino names the main file after its folder)")
    cxx = any(s.suffix in (".ino", ".cpp") for s in user)
    target = load_target(target_name or cfg.get("target") or DEFAULT_TARGET)
    out = proj / "build" / target["name"]
    out.mkdir(parents=True, exist_ok=True)
    gcc = find_gcc(target)
    t0 = time.time()

    base_dirs = [SDK / "include", SDK / "include" / "compat", *target["sdk_dirs"], proj]
    libs = resolve_libraries(proj, user, base_dirs)
    inc = []
    for d in base_dirs + [d for lib in libs for d in lib_src_dirs(lib)]:
        inc += ["-I", str(d)]
    defs = [f'-DBL_SKETCH_NAME="{cfg["name"]}"', f"-DBL_SKETCH_STACK={int(cfg['stack'])}", *secret_defines()]

    def compile_one(src, obj, extra_defs=(), quiet=False):
        if src.suffix == ".ino":
            # Like Arduino: C++, with the core header included automatically.
            lang = ["-x", "c++", *CXXFLAGS, "-include", "baseline.h"]
        elif src.suffix == ".cpp":
            lang = CXXFLAGS
        else:
            lang = CFLAGS
        obj.parent.mkdir(parents=True, exist_ok=True)
        run([gcc, *lang, *target["cflags"], *PLATFORM_DEFINES, *inc, *extra_defs,
             *(["-w"] if quiet else []), "-c", str(src), "-o", str(obj)])

    objs = []
    for src in user + [SDK / "src" / "sketch_crt.c"]:     # crt carries the sketch's name/stack
        obj = out / (src.name + ".o")
        if src.suffix == ".ino":
            with_protos = ino_with_prototypes(src)
            if with_protos is not None:
                gen = out / (src.name + ".cpp")
                gen.write_text(with_protos)
                compile_one(gen, obj, ["-include", "baseline.h", *defs])
                objs.append(str(obj))
                continue
        compile_one(src, obj, defs)
        objs.append(str(obj))
    # Libraries: compiled into build/<target>/libs/<library>/, and only again when a
    # file of that library (or the SDK/bsk) changes. Their warnings are not shown.
    sdk_dep = max(p.stat().st_mtime for p in [*(SDK / "include").glob("*.h"), Path(__file__)])
    for lib in libs:
        files = lib_files(lib)
        lib_dep = max([sdk_dep] + [f.stat().st_mtime for f in files])
        for src in (f for f in files if f.suffix in (".c", ".cpp")):
            obj = out / "libs" / lib.name / (src.relative_to(lib).as_posix().replace("/", "__") + ".o")
            if not obj.exists() or obj.stat().st_mtime < lib_dep:
                compile_one(src, obj, quiet=True)
            objs.append(str(obj))
    if libs:
        print("libraries: " + ", ".join(lib.name for lib in libs))
    # The C library shim, C++ runtime and Arduino-style objects are the same for
    # every sketch: compile them once per target into sdk/build/<target> and reuse them.
    cache = SDK / "build" / target["name"]
    cache.mkdir(parents=True, exist_ok=True)
    newest_dep = max(p.stat().st_mtime for p in [*(SDK / "include").glob("*.h"),
                                                 *(f for d in target["sdk_dirs"] for f in d.glob("*")),
                                                 target["dir"] / "target.json",
                                                 target["chip_dir"] / "chip.json", Path(__file__)])
    shared = ["sketch_libc.c", "sketch_time.c", *target["sdk_sources"]] + (
        ["sketch_cxx.cpp", "sketch_arduino.cpp", "sketch_string.cpp", "sketch_espnow.cpp",
         "sketch_net.cpp", "sketch_io.cpp", "sketch_ble.cpp", "sketch_storage.cpp",
         "sketch_mqtt.cpp", "sketch_system.cpp", "sketch_fastled.cpp", "sketch_usb.cpp",
         "sketch_extras.cpp"] if cxx else [])
    for name in shared:
        src = SDK / "src" / name
        obj = cache / (name + ".o")
        if not obj.exists() or obj.stat().st_mtime < max(newest_dep, src.stat().st_mtime):
            compile_one(src, obj)
        objs.append(str(obj))

    ld = out / "sketch.ld"
    run([gcc, "-E", "-P", "-x", "c", "-DBL_LINKER_SCRIPT", *inc,
         str(ROOT / target["linker_script"]), "-o", str(ld)])

    elf = out / (cfg["name"] + ".elf")
    # -fno-rtti picks the toolchain's no-rtti multilib, matching how sketches compile.
    # libstdc++ supplies the out-of-line parts of std::string, std::map, ...; the
    # SDK supplies operator new/delete and the std::__throw_* hooks (no exceptions).
    run([gcc, *target.get("ldflags", []), "-fno-rtti", "-nostdlib", "-nostartfiles",
         "-T", str(ld), "-T", str(target["chip_dir"] / "sdk" / "rom.ld"),
         "-Wl,--gc-sections", "-Wl,--no-undefined", "-Wl,-Map," + str(out / (cfg["name"] + ".map")),
         *objs, "-Wl,--start-group", "-lstdc++", "-lm", "-lgcc", "-Wl,--end-group", "-o", str(elf)])

    image = make_image(elf.read_bytes(), target)
    bsk = out / (cfg["name"] + ".bsk")
    bsk.write_bytes(image)
    sha = image[HDR_OFF_SHA256:HDR_OFF_SHA256 + 32].hex()
    print(f"built {bsk.relative_to(proj.parent)}: {len(image)} bytes, sha256 {sha[:16]}..., "
          f"{time.time() - t0:.2f}s")
    return bsk


def make_image(elf, target):
    """Lay the ELF's loadable segments out by their load address in the sketch
    window, then patch image_size, build_id and sha256 into the header."""
    IBUS_BASE, DBUS_BASE, MAX_SIZE = target["ibus"], target["dbus"], target["max_size"]
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        sys.exit("error: expected a 32-bit little-endian ELF")
    e_phoff, = struct.unpack_from("<I", elf, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", elf, 42)
    image = bytearray()
    for i in range(e_phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz, _memsz, _flags, _align = \
            struct.unpack_from("<8I", elf, e_phoff + i * e_phentsize)
        if p_type != 1 or p_filesz == 0:      # PT_LOAD with file contents
            continue
        if IBUS_BASE <= p_paddr < IBUS_BASE + MAX_SIZE:
            off = p_paddr - IBUS_BASE
        elif DBUS_BASE <= p_paddr < DBUS_BASE + MAX_SIZE:
            off = p_paddr - DBUS_BASE
        else:
            sys.exit(f"error: loadable segment at 0x{p_paddr:08x} is outside the sketch window")
        end = off + p_filesz
        if len(image) < end:
            image.extend(b"\xff" * (end - len(image)))
        image[off:end] = elf[p_offset:p_offset + p_filesz]
    while len(image) % 4:
        image.append(0xFF)
    if len(image) < HEADER_SIZE or struct.unpack_from("<I", image, 0)[0] != MAGIC:
        sys.exit("error: sketch header missing (was sketch_crt.c linked?)")
    if len(image) > MAX_SIZE:
        sys.exit(f"error: image is {len(image)} bytes; a sketch slot holds {MAX_SIZE}")
    struct.pack_into("<I", image, HDR_OFF_IMAGE_SIZE, len(image))
    build_id = hashlib.sha256(bytes(image) + str(time.time()).encode()).digest()[:16]
    image[HDR_OFF_BUILD_ID:HDR_OFF_BUILD_ID + 16] = build_id
    image[HDR_OFF_SHA256:HDR_OFF_SHA256 + 32] = bytes(32)
    image[HDR_OFF_SHA256:HDR_OFF_SHA256 + 32] = hashlib.sha256(image).digest()
    return bytes(image)


def cmd_build(args):
    build(args.dir, args.target)


# ---------------------------------------------------------------- serial

def board_vids():
    """USB vendor IDs of every target's board (target.json usb_vids)."""
    return {int(v, 16) for name in target_names() for v in load_target(name)["usb_vids"]}


def import_serial():
    try:
        import serial
        import serial.tools.list_ports  # noqa: F401
    except ImportError:
        sys.exit("error: pyserial is required: pip install pyserial")
    return serial


def find_port(port=None):
    """The given port, or the single supported board that is plugged in."""
    if port:
        return port
    serial = import_serial()
    vids = board_vids()
    found = [p for p in serial.tools.list_ports.comports() if p.vid in vids]
    if len(found) == 1:
        return found[0].device
    if not found:
        sys.exit("error: no supported board found on USB. Plug in the board, or pass -p PORT.")
    names = ", ".join(f"{p.device} ({p.description})" for p in found)
    sys.exit(f"error: several boards found: {names}. Pass -p PORT to pick one.")


ESPRESSIF_VID = 0x303A
USB_SERIAL_JTAG_PID = 0x1001


def is_tinyusb_port(port):
    """An Espressif TinyUSB serial port (S3 USB device mode), not USB-Serial-JTAG."""
    serial = import_serial()
    for p in serial.tools.list_ports.comports():
        if p.device == port:
            return p.vid == ESPRESSIF_VID and p.pid != USB_SERIAL_JTAG_PID
    return False


def open_port(port, baud):
    serial = import_serial()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.1
    # Keep DTR/RTS released so opening the port does not reset the board
    # (CH340/CP210x wire them to EN/IO0; USB-Serial-JTAG resets on them too).
    # A TinyUSB serial port is the exception: Arduino's USBCDC discards output
    # until the host raises DTR, and DTR alone never resets the chip there.
    s.dtr = is_tinyusb_port(port)
    s.rts = False
    s.open()
    return s


# Baud rates Baseline's console may use on a USB-serial chip, fastest first:
# 921600 (classic ESP32 Baselines from ABI 1.6) and 115200 (older ones). Native
# USB ports ignore the baud rate.
CONSOLE_BAUDS = (921600, 115200)
_baud_cache = {}


def is_native_usb_port(port):
    serial = import_serial()
    return any(p.device == port and p.vid == ESPRESSIF_VID for p in serial.tools.list_ports.comports())


def console_baud(port, baud=None):
    """The baud rate to talk to Baseline on this port: the one given, or found
    by asking at each of CONSOLE_BAUDS until Baseline answers."""
    if baud:
        return baud
    if port in _baud_cache:
        return _baud_cache[port]
    if is_native_usb_port(port):
        return CONSOLE_BAUDS[-1]
    for b in CONSOLE_BAUDS:
        try:
            with open_port(port, b) as s:
                s.reset_input_buffer()
                s.write(f"{PROTO_PREFIX} INFO\n".encode())
                deadline, buf = time.time() + 1.0, b""
                while time.time() < deadline:
                    buf += s.read(s.in_waiting or 1)
                    if f"{PROTO_PREFIX} OK".encode() in buf:
                        _baud_cache[port] = b
                        return b
        except OSError:
            break
    return CONSOLE_BAUDS[0]     # nothing answered (no Baseline yet, or it is busy): assume the current one


OTA_PORT = 3232


class NetPort:
    """A TCP connection to Baseline's OTA listener, with the small part of the
    pyserial interface that Link uses."""

    def __init__(self, host, password=None):
        import socket
        name, _, port = host.partition(":")
        port = int(port) if port else OTA_PORT
        candidates = [name] if "." in name else [name + ".local", name]
        self.port = f"{name}:{port}"
        self.in_waiting = 0
        last = None
        for cand in candidates:
            try:
                self.sock = socket.create_connection((cand, port), timeout=5)
                break
            except OSError as e:
                last = e
        else:
            sys.exit(f"error: cannot reach {self.port} ({last}). Is the sketch running ArduinoOTA.begin() "
                     "on the same network?")
        self.sock.settimeout(0.1)
        if password:
            Link(self).request(f"AUTH {password}")

    def write(self, data):
        self.sock.sendall(data)

    def read(self, n):
        import socket
        try:
            return self.sock.recv(max(n, 4096))
        except socket.timeout:
            return b""

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def open_link_port(args):
    """(port name, connection): WiFi when --host is given, else USB serial."""
    if getattr(args, "host", None):
        conn = NetPort(args.host, args.password or os.environ.get("BSK_OTA_PASSWORD"))
        return conn.port, conn
    port = find_port(args.port)
    args.baud = console_baud(port, args.baud)
    return port, open_port(port, args.baud)


class Link:
    """Line protocol with Baseline's loader. Lines that don't start with
    @BSK are console output and are echoed."""

    def __init__(self, ser, echo=True):
        self.ser, self.echo, self.buf = ser, echo, b""

    def send(self, line):
        self.ser.write((PROTO_PREFIX + " " + line + "\n").encode())

    def readline(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                return line.decode(errors="replace").rstrip("\r")
            self.buf += self.ser.read(self.ser.in_waiting or 1)
        return None

    def request(self, line, timeout=5.0):
        self.send(line)
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = self.readline(deadline - time.time())
            if r is None:
                break
            if r.startswith(PROTO_PREFIX + " "):
                body = r[len(PROTO_PREFIX) + 1:]
                if body.startswith("OK"):
                    return body[2:].strip()
                if body.startswith("ERR"):
                    sys.exit(f"\nerror from Baseline: {body[3:].strip()}")
            elif self.echo and r:
                print(r)
        cmd = line.split()[0]
        if cmd == "INFO":
            sys.exit(f"\nerror: no reply from {self.ser.port}. Is Baseline installed? "
                     "Flash it once with: python tools/bsk.py baseline flash")
        sys.exit(f"\nerror: Baseline stopped answering during {cmd} on {self.ser.port}.")


def cmd_info(args):
    _, s = open_link_port(args)
    with s:
        print(Link(s).request("INFO"))


def cmd_safe(args):
    port = find_port(args.port)
    with open_port(port, console_baud(port, args.baud)) as s:
        Link(s).request("SAFE")
        print("rebooting into safe mode")


def info_fields(info):
    """'baseline=0.1.0 abi=1.5 chip=esp32 ...' -> dict"""
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', info))


def cmd_upload(args):
    if args.host and args.monitor:
        sys.exit("error: --monitor needs the USB port; it cannot be combined with --host")
    port, s = open_link_port(args)
    with s:
        link = Link(s)
        info = link.request("INFO")
        print(f"Baseline on {port}:", info)
        # The board decides the target. Baselines from before the per-product
        # targets report only chip=, which LEGACY_TARGETS maps.
        fields = info_fields(info)
        board = fields.get("target") or fields.get("chip") or DEFAULT_TARGET
        board = LEGACY_TARGETS.get(board, board)
        if args.target and args.target != board:
            sys.exit(f"error: the board runs the {board} Baseline, not {args.target}")
        send_image(link, build(args.dir, board).read_bytes())
        if args.monitor:
            monitor(s, port, args.baud, link, follow=not args.port)


def send_image(link, image, quiet=False):
    """Upload a .bsk image over an open link; Baseline installs it and reboots."""
    digest = hashlib.sha256(image).hexdigest()
    t0 = time.time()
    link.request(f"BEGIN {len(image)} {digest}", timeout=15)   # erases the slot
    for off in range(0, len(image), CHUNK):
        data = base64.b64encode(image[off:off + CHUNK]).decode()
        link.request(f"DATA {off} {data}")
        if not quiet:
            print(f"\r  {min(off + CHUNK, len(image))}/{len(image)} bytes", end="", flush=True)
    if not quiet:
        print()
    installed = link.request("END", timeout=10)
    if not quiet:
        print("installed:", installed)
        print(f"upload took {time.time() - t0:.2f}s")
    return installed


# ---------------------------------------------------------------- bsk test

TESTS = ROOT / "tools" / "tests"


def board_request(port, baud, line, attempts=40):
    """Send one loader command, waiting for the board to (re)appear."""
    serial = import_serial()
    for _ in range(attempts):
        try:
            with open_port(port, baud) as s:
                link = Link(s, echo=False)
                try:
                    return link.request(line, timeout=3)
                except SystemExit:
                    pass
        except (serial.SerialException, OSError):
            pass
        time.sleep(0.5)
    sys.exit(f"error: no reply from {port} to {line}")


def capture_console(port, baud, until, timeout):
    """Console text until a line containing `until`, following the port across reboots."""
    serial = import_serial()
    out, deadline, s = b"", time.time() + timeout, None
    while time.time() < deadline:
        try:
            if s is None:
                s = open_port(port, baud)
            out += s.read(s.in_waiting or 1)
            if until.encode() in out and out.endswith(b"\n"):
                break
        except (serial.SerialException, OSError):
            if s:
                s.close()
            s = None
            time.sleep(0.3)
    if s:
        s.close()
    return out.decode(errors="replace")


def upload_dir(port, baud, proj, target):
    image = build(proj, target).read_bytes()
    with open_port(port, baud) as s:
        return send_image(Link(s, echo=False), image, quiet=True)


def cmd_test(args):
    """Bring-up test: selftest (with a deep sleep), crash rollback, watchdog rollback."""
    port = find_port(args.port)
    baud = console_baud(port, args.baud)
    fields = info_fields(board_request(port, baud, "INFO"))
    target = LEGACY_TARGETS.get(fields.get("target") or fields.get("chip"), fields.get("target") or fields.get("chip"))
    print(f"testing {target} on {port}: Baseline {fields.get('baseline')} ABI {fields.get('abi')}")
    board_request(port, baud, "CRASHCLEAR")
    rows = []           # (check, PASS/FAIL/SKIP/LOOK, detail)

    print("1/3 selftest (about a minute, including a 2 s deep sleep)...")
    upload_dir(port, baud, ROOT / "examples" / "selftest", target)
    text = capture_console(port, baud, "RESULT:", 180)
    for line in text.splitlines():
        m = re.match(r"^(PASS|FAIL|SKIP|LOOK)  (.*)$", line.strip())
        if m:
            rows.append((m.group(2), m.group(1), ""))
    result = re.search(r"RESULT: (\d+) passed, (\d+) failed", text)
    if not result:
        rows.append(("selftest finished", "FAIL", "no RESULT line within 3 minutes"))
        print(text[-2000:])

    print("2/3 crash rollback...")
    upload_dir(port, baud, TESTS / "crash_test", target)
    time.sleep(4)
    f = info_fields(board_request(port, baud, "INFO"))
    rows.append(("crash in setup() rolls back to selftest", "PASS" if f.get("sketch") == "selftest" else "FAIL",
                 f"running {f.get('sketch')}"))
    rows.append(("crash recorded (INFO crash=)", "PASS" if f.get("crash", '"-"') != '"-"' else "FAIL",
                 f.get("crash", "").strip('"')[:80]))

    print("3/3 watchdog rollback (about 15 s)...")
    board_request(port, baud, "CRASHCLEAR")
    upload_dir(port, baud, TESTS / "hang_test", target)
    time.sleep(12)
    f = info_fields(board_request(port, baud, "INFO"))
    rows.append(("hung loop() caught by the watchdog, rolls back", "PASS" if f.get("sketch") == "selftest" else "FAIL",
                 f"running {f.get('sketch')}, reset={f.get('reset')}"))
    board_request(port, baud, "CRASHCLEAR")

    width = max(len(r[0]) for r in rows)
    print()
    for name, status, detail in rows:
        print(f"  {status:4}  {name:{width}}  {detail}")
    failed = sum(1 for r in rows if r[1] == "FAIL")
    passed = sum(1 for r in rows if r[1] == "PASS")
    print(f"\n{target}: {passed} passed, {failed} failed, {sum(1 for r in rows if r[1] == 'SKIP')} skipped"
          + ("; check the LOOK items by eye" if any(r[1] == "LOOK" for r in rows) else ""))
    sys.exit(1 if failed else 0)


def board_ports():
    serial = import_serial()
    vids = board_vids()
    return [p.device for p in serial.tools.list_ports.comports() if p.vid in vids]


def reopen(port, baud, follow=False):
    """Wait for the board to come back after a reboot. With follow, accept any
    board port: USB device mode on the S3 changes the COM port number."""
    serial = import_serial()
    while True:
        candidates = [port] + ([p for p in board_ports() if p != port] if follow else [])
        for p in candidates:
            try:
                return p, open_port(p, baud)
            except (serial.SerialException, OSError):
                pass
        time.sleep(0.3)


def monitor(s, port, baud, link=None, follow=True):
    serial = import_serial()
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    print(f"--- monitor on {port} (Ctrl+C to quit) ---", flush=True)
    if link and link.buf:
        sys.stdout.write(decoder.decode(link.buf))
    try:
        while True:
            try:
                data = s.read(s.in_waiting or 1)
            except (serial.SerialException, OSError):
                s.close()
                print("\n--- board disconnected, waiting for it ---", flush=True)
                port, s = reopen(port, baud, follow)
                print(f"--- reconnected on {port} ---", flush=True)
                continue
            if data:
                sys.stdout.write(decoder.decode(data))
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n--- exit ---")
    finally:
        s.close()


def cmd_monitor(args):
    port = find_port(args.port)
    baud = console_baud(port, args.baud)
    monitor(open_port(port, baud), port, baud, follow=not args.port)


# ---------------------------------------------------------------- baseline firmware

# Thousands of intermediate files: keep them out of synced folders (Dropbox locks them).
BUILD_ROOT = Path(os.environ.get("LOCALAPPDATA") or tempfile.gettempdir()) / "BaselineSketcher"


def baseline_build_path(target_name):
    return BUILD_ROOT / target_name / "build"


def stage_baseline(target):
    """Assemble one Baseline source folder per target: the shared Baseline
    source, then chips/<chip>/baseline (bl_port.h, bl_port_mmu.c, ...), then
    targets/<target>/baseline if it exists, later files winning; plus the
    chip's partition table for the target's flash size as partitions.csv.
    Copies keep their timestamps, so the build recompiles only what changed."""
    dst = BUILD_ROOT / target["name"] / "baseline"
    table = target["chip_dir"] / f"partitions-{target['flash_size']}.csv"
    if not table.exists():
        sys.exit(f"error: no partition table for {target['flash_size']} of flash: {table.relative_to(ROOT)}")
    wanted = {Path("partitions.csv"): table}
    for root in (ROOT / target["baseline_source"], *(d / "baseline" for d in target["dirs"])):
        if not root.exists():
            continue
        for f in root.rglob("*"):
            if f.is_file():
                wanted[f.relative_to(root)] = f
    if dst.exists():
        for f in list(dst.rglob("*")):
            if f.is_file() and f.relative_to(dst) not in wanted:
                f.unlink()
    for rel, src in wanted.items():
        out = dst / rel
        if not out.exists() or out.stat().st_mtime != src.stat().st_mtime or out.stat().st_size != src.stat().st_size:
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, out)
    return dst


def find_arduino_cli():
    env = os.environ.get("BSK_ARDUINO_CLI")
    if env:
        return env
    on_path = shutil.which("arduino-cli")
    if on_path:
        return on_path
    exe = "arduino-cli" + (".exe" if os.name == "nt" else "")
    bundled = Path("resources") / "app" / "lib" / "backend" / "resources" / exe
    candidates = []
    if os.environ.get("LOCALAPPDATA"):
        candidates.append(Path(os.environ["LOCALAPPDATA"]) / "Programs" / "Arduino IDE" / bundled)
    candidates.append(Path("C:/Program Files/Arduino IDE") / bundled)
    for c in candidates:
        if c.exists():
            return str(c)
    sys.exit("error: arduino-cli not found. Set BSK_ARDUINO_CLI or install Arduino IDE 2 / arduino-cli.")


def arduino_data_dirs():
    """Arduino data directories that may hold the esp32 core."""
    dirs = []
    if os.environ.get("ARDUINO_DIRECTORIES_DATA"):
        dirs.append(Path(os.environ["ARDUINO_DIRECTORIES_DATA"]))
    if os.environ.get("LOCALAPPDATA"):
        dirs.append(Path(os.environ["LOCALAPPDATA"]) / "Arduino15")
    dirs += [Path.home() / ".arduino15", Path.home() / "Library" / "Arduino15"]
    for cfg in (Path.home() / ".arduinoIDE" / "arduino-cli.yaml",):
        if cfg.exists():
            for line in cfg.read_text().splitlines():
                if line.strip().startswith("data:"):
                    dirs.append(Path(line.split(":", 1)[1].strip()))
    return dirs


def newest_esp32_core():
    """(version tuple, data dir) of the newest installed esp32 Arduino core."""
    best = None
    for d in arduino_data_dirs():
        for v in glob.glob(str(d / "packages" / "esp32" / "hardware" / "esp32" / "*")):
            try:
                ver = tuple(int(x) for x in Path(v).name.split("."))
            except ValueError:
                continue
            if best is None or ver > best[0]:
                best = (ver, d)
    return best


# ---------------------------------------------------------------- Baseline via ESP-IDF
# Targets with "builder": "idf" build Baseline as an ESP-IDF project (idf/) with
# Arduino-ESP32 as a component, so Baseline can change the sdkconfig.

def find_idf(version):
    """(IDF_PATH, IDF_TOOLS_PATH) of an ESP-IDF checkout matching version (e.g. "5.5")."""
    tools = Path(os.environ.get("IDF_TOOLS_PATH") or
                 ("C:/Espressif" if os.name == "nt" else Path.home() / ".espressif"))
    candidates = []
    if os.environ.get("BSK_IDF_PATH"):
        candidates.append(Path(os.environ["BSK_IDF_PATH"]))
    if os.environ.get("IDF_PATH"):
        candidates.append(Path(os.environ["IDF_PATH"]))
    for root in (tools / "frameworks", Path.home() / "esp"):
        candidates += sorted(root.glob(f"esp-idf-v{version}*"), reverse=True)
    for c in candidates:
        ver = c / "tools" / "cmake" / "version.cmake"
        if ver.exists():
            v = dict(re.findall(r"set\(IDF_VERSION_(MAJOR|MINOR|PATCH) (\d+)\)", ver.read_text()))
            if f"{v.get('MAJOR')}.{v.get('MINOR')}" == version:
                return c, tools
    sys.exit(f"error: ESP-IDF v{version} not found. Install it (for example in "
             f"{tools / 'frameworks'}) or set BSK_IDF_PATH.")


def idf_env(idf_path, tools_path):
    """The environment `export.ps1/.sh` would set up, from idf_tools.py export."""
    env = dict(os.environ, IDF_PATH=str(idf_path), IDF_TOOLS_PATH=str(tools_path))
    env.pop("MSYSTEM", None)   # IDF refuses to run under MSYS/Git Bash, but works fine from it with this unset
    version = ".".join(re.findall(r"set\(IDF_VERSION_(?:MAJOR|MINOR) (\d+)\)",
                                  (idf_path / "tools" / "cmake" / "version.cmake").read_text()))
    venvs = sorted(Path(tools_path, "python_env").glob(f"idf{version}_py*_env"))
    if not venvs:
        sys.exit(f"error: no Python environment for ESP-IDF {version} in {tools_path}\\python_env. "
                 f"Run {idf_path / 'install.bat'}.")
    env["IDF_PYTHON_ENV_PATH"] = str(venvs[-1])
    py = venvs[-1] / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    out = subprocess.run([str(py), str(idf_path / "tools" / "idf_tools.py"), "export", "--format", "key-value"],
                         env=env, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("error: idf_tools.py export failed:\n" + out.stderr)
    for line in out.stdout.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            env[k] = v.replace("%PATH%", env.get("PATH", "")).replace("$PATH", env.get("PATH", ""))
    return env, py


def sync_tree(src, dst, skip=()):
    """Copy src to dst, only files whose size or time changed; remove extras."""
    wanted = {}
    for f in src.rglob("*"):
        rel = f.relative_to(src)
        if f.is_file() and not any(part in skip for part in rel.parts):
            wanted[rel] = f
    if dst.exists():
        for f in list(dst.rglob("*")):
            if f.is_file() and f.relative_to(dst) not in wanted:
                f.unlink()
    for rel, f in wanted.items():
        out = dst / rel
        st = f.stat()
        if not out.exists() or out.stat().st_mtime != st.st_mtime or out.stat().st_size != st.st_size:
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, out)


def stage_idf(target, core_dir, libs_dir):
    """Assemble the IDF project for a target (layout: idf/CMakeLists.txt)."""
    proj = BUILD_ROOT / target["name"] / "idf"
    idf_src = ROOT / "idf"
    proj.mkdir(parents=True, exist_ok=True)
    shutil.copy2(idf_src / "CMakeLists.txt", proj / "CMakeLists.txt")

    # main/: the same files the Arduino build stages, plus idf/main/.
    main = proj / "main"
    before = sorted(main.rglob("*")) if main.exists() else []
    sync_tree(stage_baseline(target), main)
    (main / "baseline.ino").replace(main / "baseline_main.cpp")
    for f in (idf_src / "main").iterdir():
        shutil.copy2(f, main / f.name)
    if sorted(main.rglob("*")) != before:
        (main / "CMakeLists.txt").touch()     # new or removed sources: make CMake glob again

    # components/arduino: the installed core (no examples), with a trimmed manifest.
    arduino = proj / "components" / "arduino"
    for part in ("cores", "libraries", "variants"):
        sync_tree(core_dir / part, arduino / part, skip={"examples"})
    for f in ("CMakeLists.txt", "Kconfig.projbuild"):
        shutil.copy2(core_dir / f, arduino / f)
    shutil.copy2(idf_src / "arduino_component.yml", arduino / "idf_component.yml")

    # Prebuilt components (idf/prebuilt/<name>): headers and library from
    # Arduino's precompiled libs, Kconfig symbols from its sdkconfig.
    for name in target.get("idf_prebuilt_components", []):
        comp = proj / "components" / name
        sync_tree(idf_src / "prebuilt" / name, comp)
        sync_tree(libs_dir / "include" / name, comp / "include")
        (comp / "lib").mkdir(exist_ok=True)
        shutil.copy2(libs_dir / "lib" / f"lib{name}.a", comp / "lib" / f"lib{name}.a")
        kconfig = prebuilt_kconfig(name, (libs_dir / "sdkconfig").read_text())
        if not (comp / "Kconfig").exists() or (comp / "Kconfig").read_text() != kconfig:
            (comp / "Kconfig").write_text(kconfig)

    # sdkconfig: Arduino's, then ours. A stale sdkconfig would win over changed
    # defaults, so start over whenever they change.
    base = (libs_dir / "sdkconfig").read_text()
    ours = (idf_src / "sdkconfig.defaults").read_text()
    for d in target["dirs"]:                    # chip, then product target
        f = d / "idf" / "sdkconfig.defaults"
        if f.exists():
            ours += f"\n# ---- {f.relative_to(ROOT).as_posix()}\n" + f.read_text()
    ours += f"\n# ---- target.json flash_size\nCONFIG_ESPTOOLPY_FLASHSIZE_{target['flash_size']}=y\n"
    for name, text in (("sdkconfig.defaults.arduino", base), ("sdkconfig.defaults", ours)):
        f = proj / name
        if not f.exists() or f.read_text() != text:
            f.write_text(text)
            for stale in (proj / "sdkconfig", proj / "build" / "CMakeCache.txt"):
                if stale.exists():
                    stale.unlink()
    return proj


# Kconfig prefix of each prebuilt component's options in Arduino's sdkconfig.
PREBUILT_KCONFIG = {"arduino_tinyusb": "TINYUSB"}


def prebuilt_kconfig(name, sdkconfig):
    """Declare a prebuilt component's CONFIG_<prefix>_* options, with the values
    its library was built with, so they reach sdkconfig.h."""
    prefix = PREBUILT_KCONFIG[name]
    out = [f"# Generated by bsk from Arduino's sdkconfig: the options lib{name}.a was built with.",
           f'menu "{name} (prebuilt)"']
    for line in sdkconfig.splitlines():
        m = re.match(rf"^CONFIG_({prefix}_\w+)=(.*)$", line)
        n = re.match(rf"^# CONFIG_({prefix}_\w+) is not set$", line)
        if m:
            sym, val = m.groups()
            if val in ("y", "n"):
                kind, default = "bool", val
            elif val.startswith('"'):
                kind, default = "string", val
            elif re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)", val):
                kind, default = ("hex" if val.startswith("0x") else "int"), val
            else:
                continue
        elif n:
            sym, kind, default = n.group(1), "bool", "n"
        else:
            continue
        out += [f"config {sym}", f'    {kind} "{sym}"', f"    default {default}", ""]
    out.append("endmenu")
    return "\n".join(out) + "\n"


def build_baseline_idf(target, core):
    core_dir = core[1] / "packages" / "esp32" / "hardware" / "esp32" / ".".join(map(str, core[0]))
    libs_dir = core[1] / "packages" / "esp32" / "tools" / f"{target['idf_target']}-libs" / ".".join(map(str, core[0]))
    idf_path, tools_path = find_idf(target["idf_version"])
    env, py = idf_env(idf_path, tools_path)
    print(f"using ESP-IDF {idf_path.name} ({idf_path}) with Arduino-ESP32 {'.'.join(map(str, core[0]))} as a component")
    proj = stage_idf(target, core_dir, libs_dir)
    inc = ";".join(str(p).replace("\\", "/") for p in (SDK / "include", *target["sdk_dirs"]))
    idf_py = [str(py), str(idf_path / "tools" / "idf.py"), "-C", str(proj), "-B", str(proj / "build"),
              f"-DBSK_DEFINES={';'.join(target.get('idf_defines', []))}"]
    if not (proj / "sdkconfig").exists():
        shutil.rmtree(proj / "build", ignore_errors=True)   # starting over: set-target won't clean a half-made build
        run(idf_py + [f"-DIDF_TARGET={target['idf_target']}", f"-DBSK_INCLUDE_DIRS={inc}", "set-target",
                      target["idf_target"]], env=env)
    run(idf_py + [f"-DBSK_INCLUDE_DIRS={inc}", "build"], env=env)

    # One blob at 0x0: bootloader + partition table + Baseline.
    b = proj / "build"
    args = json.loads((b / "flasher_args.json").read_text())
    out = DIST / target["name"] / f"baseline-{target['name']}.bin"
    out.parent.mkdir(parents=True, exist_ok=True)
    parts = []
    for off, f in sorted(args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        parts += [off, str(b / f)]
    run([str(py), "-m", "esptool", "--chip", target["idf_target"], "merge_bin", "-o", str(out)] + parts, env=env)
    print(f"Baseline blob: {out.relative_to(ROOT)} ({out.stat().st_size:,} bytes; flash at 0x0)")
    return proj, env, py, args


def partitions_for_flash(csv_text, flash_size):
    """The partition table with its last two partitions (userfs, coredump) moved
    so they end at flash_size: boards with less flash than the table was laid
    out for get a smaller LittleFS partition. Everything before them is unchanged."""
    rows = []
    for line in csv_text.splitlines():
        cells = [c.strip() for c in line.split(",")]
        rows.append(cells if line.strip() and not line.lstrip().startswith("#") else line)
    entries = [r for r in rows if isinstance(r, list)]
    fs, core = entries[-2], entries[-1]
    if (fs[0], core[0]) != ("userfs", "coredump"):
        sys.exit("error: partitions.csv must end with userfs, coredump")
    core_size = int(core[4], 0)
    core_off = flash_size - core_size
    fs_off = int(fs[3], 0)
    if core_off - fs_off < 0x10000:
        sys.exit(f"error: {flash_size // 0x100000} MB of flash is too small for this target's partition table")
    fs[4], core[3] = hex(core_off - fs_off), hex(core_off)
    return "\n".join(", ".join(r) if isinstance(r, list) else r for r in rows) + "\n"


# esptool commands are spelled the v4 way (merge_bin, write_flash): IDF 5.5
# ships esptool v4, and v5 still accepts those names.
def flash_baseline_idf(target, proj, env, py, args, port):
    """Flash bootloader, partition table and Baseline; the partition table is
    fitted to the flash the board actually has."""
    esptool = [str(py), "-m", "esptool", "--chip", target["idf_target"], "-p", port, "-b", "921600"]
    r = subprocess.run(esptool + ["flash_id"], env=env, capture_output=True, text=True)
    m = re.search(r"Detected flash size:\s*(\d+)MB", r.stdout)
    if not m:
        sys.exit("error: could not read the flash size:\n" + r.stdout + r.stderr)
    flash_mb = int(m.group(1))
    b = proj / "build"
    files = dict(args["flash_files"])
    pt_off = args["partition-table"]["offset"]
    built_mb = int(args["flash_settings"]["flash_size"].rstrip("MB"))
    csv = (proj / "main" / "partitions.csv").read_text()
    if flash_mb != built_mb:
        csv = partitions_for_flash(csv, flash_mb * 0x100000)
        fitted = b / f"partitions-{flash_mb}MB.csv"
        fitted.write_text(csv)
        run([str(py), str(Path(env["IDF_PATH"]) / "components" / "partition_table" / "gen_esp32part.py"),
             "--flash-size", f"{flash_mb}MB", str(fitted), str(b / f"partitions-{flash_mb}MB.bin")], env=env)
        files[pt_off] = f"partitions-{flash_mb}MB.bin"
        print(f"board has {flash_mb} MB of flash: userfs and coredump moved to fit")
    # Erase the coredump partition: a dump left by the previous Baseline (or
    # whatever the flash held there before) cannot be decoded against this one.
    for line in csv.splitlines():
        cells = [c.strip() for c in line.split(",")]
        if cells[0] == "coredump":
            run(esptool + ["erase_region", cells[3], cells[4]], env=env)
    cmd = esptool + ["write_flash", "--flash_size", f"{flash_mb}MB"]
    for off, f in sorted(files.items(), key=lambda kv: int(kv[0], 16)):
        cmd += [off, str(b / f)]
    run(cmd, env=env)


def leave_usb_device_mode(port, fixed_port):
    """In USB device mode (S3) the USB port is a TinyUSB serial port that esptool
    cannot reset into the bootloader. Ask Baseline to switch it off first.
    Returns the port to flash through (it may change)."""
    try:
        with open_port(port, 115200) as s:
            link = Link(s, echo=False)
            info = info_fields(link.request("INFO", timeout=2))
            if info.get("usb", "0") in ("0", ""):
                return port
            print("Baseline is in USB device mode: switching it off before flashing")
            link.request("USBOFF")
    except SystemExit:
        return port     # no Baseline answering (blank board, or another firmware): flash anyway
    time.sleep(2)
    if fixed_port:
        return fixed_port
    new_port, s = reopen(port, 115200, follow=True)
    s.close()
    return new_port


def cmd_baseline(args):
    target = load_target(args.target or DEFAULT_TARGET)
    min_core = tuple(int(x) for x in target["min_core"].split("."))
    core = newest_esp32_core()
    if not core or core[0] < min_core:
        found = ".".join(map(str, core[0])) if core else "none"
        sys.exit(f"error: Baseline needs Arduino-ESP32 core >= {target['min_core']} (found {found}). "
                 "Install it with the Arduino IDE Boards Manager.")
    print(f"target {target['name']}: {target['description']}")
    if target.get("builder") == "idf":
        proj, env, py, fargs = build_baseline_idf(target, core)
        if args.action == "flash":
            flash_baseline_idf(target, proj, env, py, fargs, leave_usb_device_mode(find_port(args.port), args.port))
        return
    print(f"using Arduino-ESP32 {'.'.join(map(str, core[0]))} from {core[1]}")
    os.environ["ARDUINO_DIRECTORIES_DATA"] = str(core[1])
    sketch = stage_baseline(target)
    build_path = baseline_build_path(target["name"])
    # Quoted: arduino-cli splits the expanded recipe on spaces (e.g. "C:\Users\Jane Doe").
    inc = " ".join(f'"-I{p}"' for p in (SDK / "include", *target["sdk_dirs"]))
    cmd = [find_arduino_cli(), "compile", "--fqbn", target["fqbn"],
           "--build-path", str(build_path),
           "--build-property", f"compiler.c.extra_flags={inc}",
           "--build-property", f"compiler.cpp.extra_flags={inc}",
           "--warnings", "default"]
    for prop in target.get("build_properties", []):
        cmd += ["--build-property", prop]
    if args.action == "flash":
        cmd += ["--upload", "-p", leave_usb_device_mode(find_port(args.port), args.port)]
    cmd.append(str(sketch))
    run(cmd)
    # The finished blob: bootloader + partition table + Baseline in one image,
    # written at flash offset 0 (esptool write_flash 0x0 <file>). Arduino pads
    # it with 0xFF to the full flash size; the padding is dropped.
    merged = build_path / "baseline.ino.merged.bin"
    if merged.exists():
        blob = merged.read_bytes().rstrip(b"\xff")
        blob += b"\xff" * (-len(blob) % 4)
        out = DIST / target["name"] / f"baseline-{target['name']}.bin"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(blob)
        print(f"Baseline blob: {out.relative_to(ROOT)} ({len(blob):,} bytes; flash at 0x0)")


# ---------------------------------------------------------------- main

def main():
    # Board output can hold bytes the terminal cannot show (the classic ESP32's
    # ROM boot lines arrive at 115200 while the console runs at 921600).
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    ap = argparse.ArgumentParser(prog="bsk", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    targets_help = f"target ({', '.join(target_names())})"

    p = sub.add_parser("targets", help="list the targets")
    p.set_defaults(fn=cmd_targets)

    p = sub.add_parser("new", help="create a sketch project")
    p.add_argument("dir")
    p.set_defaults(fn=cmd_new)

    p = sub.add_parser("build", help="build a sketch")
    p.add_argument("dir", nargs="?", default=".")
    p.add_argument("-t", "--target", help=targets_help + "; default: sketch.cfg, else " + DEFAULT_TARGET)
    p.set_defaults(fn=cmd_build)

    for name, fn, hlp in (("upload", cmd_upload, "build and upload a sketch"),
                          ("monitor", cmd_monitor, "serial console"),
                          ("info", cmd_info, "show Baseline info"),
                          ("safe", cmd_safe, "reboot into safe mode")):
        p = sub.add_parser(name, help=hlp)
        if name == "upload":
            p.add_argument("dir", nargs="?", default=".")
            p.add_argument("-m", "--monitor", action="store_true", help="open the monitor after upload")
            p.add_argument("-t", "--target", help="optional check: fail unless the board runs this target")
        if name in ("upload", "info"):
            p.add_argument("--host", help="upload over WiFi to NAME[.local][:port] (sketch must call ArduinoOTA.begin())")
            p.add_argument("--password", help="OTA password (or set BSK_OTA_PASSWORD)")
        p.add_argument("-p", "--port", help="serial port (default: the plugged-in board)")
        p.add_argument("-b", "--baud", type=int, help="console baud rate (default: found automatically)")
        p.set_defaults(fn=fn)

    p = sub.add_parser("test", help="bring-up test: selftest, crash and watchdog rollback")
    p.add_argument("-p", "--port", help="serial port (default: the plugged-in board)")
    p.add_argument("-b", "--baud", type=int, help="console baud rate (default: found automatically)")
    p.set_defaults(fn=cmd_test)

    p = sub.add_parser("baseline", help="build or flash a Baseline blob")
    p.add_argument("action", choices=["build", "flash"])
    p.add_argument("-t", "--target", help=targets_help + "; default " + DEFAULT_TARGET)
    p.add_argument("-p", "--port")
    p.set_defaults(fn=cmd_baseline)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
