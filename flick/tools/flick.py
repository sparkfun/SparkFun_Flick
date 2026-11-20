#!/usr/bin/env python3
"""flick - the Flick command-line tool (formerly bsk). Baseline is the firmware on the board.

  flick targets                       list the targets (one per product) in targets/
  flick new <dir>                     create a sketch project
  flick build [dir] [-t TARGET] [-o OUT]   compile + link + package -> <name>.flick (in Flick's cache; -o copies it)
  flick upload [dir] [-m]             ask the board which target it is, build for it, upload, run
  flick upload [dir] --host NAME      same, over WiFi (the running sketch called ArduinoOTA.begin())
  flick monitor                       serial console (Ctrl+C to quit)
  flick info                          ask Baseline for its version / ABI / chip / target / active slot
  flick safe                          reboot Baseline into safe mode (sketch not started)
  flick test                          bring-up test of the plugged-in board (selftest, rollback, watchdog)
  flick setup [-t TARGET]             download the compiler sketches need (once)
  flick lib search WORDS              search the Arduino Library Manager
  flick lib install NAME[@VERSION]    install a library into libraries/
  flick baseline flash --file F.bin   flash a downloaded Baseline over USB (esptool)
  flick baseline update [--host H]    update Baseline over USB or WiFi (--file F.bsu, or build it)
  flick baseline build -t TARGET      compile a Baseline blob -> dist/<target>/baseline-<target>.bin
  flick baseline flash -t TARGET      compile and flash Baseline (needs ESP-IDF; rarely needed)
  flick package                       the sketch kit for users -> dist/flick-kit-<ver>.zip

The build target is taken from -t, else `target = ...` in the sketch's
sketch.cfg, else thing_plus_esp32s3. `upload` always builds for the chip on the board.

Commands that talk to the board find it by its USB ID (any target's
usb_vids in target.json); pass -p PORT to choose one yourself.

The toolchain is found in this order: $FLICK_GCC, the one `flick setup` installed
(~/.flick/tools, or $FLICK_HOME/tools), the target's gcc on PATH,
then an Arduino-ESP32 install (%LOCALAPPDATA%/Arduino15 or ~/.arduino15).
arduino-cli: $FLICK_ARDUINO_CLI, PATH, then the copy bundled with Arduino IDE 2.
"""
import argparse
import base64
import bisect
import codecs
import glob
import gzip
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
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent      # flick/: the tool, SDK, boards, Baseline source
# The folder above flick/ is the user's (the repository and the kit look the
# same): flick.py, examples/, my_sketches/, libraries/. Secrets are in ~/.flick.
WORK = ROOT.parent
FLICK_CMD = "python flick.py"      # how messages tell the user to run flick
# The download page: Baseline for every board (a Flash button each), the examples, the kit.
# GitHub Pages of the repository (release.yml); flick/tools/make_site.py builds it.
WEB_PAGE = "https://sparkfun.github.io/SparkFun_Flick/"

# Flick was called Baseline Sketcher, and the tool bsk: its BSK_* environment
# variables still work.
for _k, _v in list(os.environ.items()):
    if _k.startswith("BSK_") and "FLICK_" + _k[4:] not in os.environ:
        os.environ["FLICK_" + _k[4:]] = _v
SDK = ROOT / "sdk"
TARGETS = ROOT / "targets"
CHIPS = ROOT / "chips"
DIST = ROOT / "dist"
FLICK_HOME = Path(os.environ.get("FLICK_HOME") or Path.home() / ".flick")
TOOLS_HOME = FLICK_HOME / "tools"
DEFAULT_TARGET = "thing_plus_esp32s3"
# Target names Baselines built before the per-product targets report as chip=.
LEGACY_TARGETS = {"esp32": "thing_plus_esp32", "esp32s3": "thing_plus_esp32s3"}

# Must match sdk/include/sketch_abi.h (the target addresses come from bl_target.h)
HEADER_SIZE = 0x100
MAGIC = 0x4B534C42
HDR_OFF_ABI_MINOR = 10
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
    """Numeric and string #defines from a header (bl_target.h), in order, following
    #include "..." of a relative path (a variant's bl_board.h includes its base's)."""
    out = {}
    text = path.read_text()
    for m in re.finditer(r'^\s*#\s*(?:include\s+"([^"]+)"|define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+|"[^"]*"))', text, re.M):
        if m.group(1):
            inc = (path.parent / m.group(1))
            if inc.exists() and "/" in m.group(1):
                out.update(header_defines(inc))
            continue
        v = m.group(3)
        out[m.group(2)] = v.strip('"') if v.startswith('"') else int(v, 0)
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
    defs.update(header_defines(d / "sdk" / "bl_board.h"))     # a board may set the sketch size (i.MX RT)
    t["ibus"], t["dbus"] = defs["BL_SKETCH_IBUS_BASE"], defs["BL_SKETCH_DBUS_BASE"]
    t["max_size"] = defs["BL_SKETCH_MAX_SIZE"]
    t["chip_id"] = defs["BL_TARGET_CHIP_ID"]
    t["board_name"] = defs.get("BL_BOARD_NAME", name)
    return t


def cmd_targets(args):
    for name in target_names():
        t = load_target(name)
        print(f"{name:20} {t['chip']:8} {t['description']}")


# ---------------------------------------------------------------- toolchain

def package_gcc(target):
    """The Arduino core's own compiler, the one Baseline is built with: e.g.
    arduino-pico's pqt-gcc (chip.json "gcc_package"), installed by the Arduino
    IDE or arduino-cli. Same libc and libstdc++. None if it isn't installed."""
    exe = target["gcc"] + (".exe" if os.name == "nt" else "")
    for r in arduino_data_dirs():
        hits = sorted(glob.glob(str(r / "packages" / target["gcc_package"] / "*" / "bin" / exe)),
                      key=lambda h: version_key(Path(h).parents[1].name))
        if hits:
            return hits[-1]
    return None


def find_gcc(target):
    env = os.environ.get("FLICK_GCC")
    if env:
        return env
    gcc_name = target["gcc"]
    exe = gcc_name + (".exe" if os.name == "nt" else "")
    if target.get("builder") == "teensy":
        return teensy_gcc(target, exe)
    if target.get("gcc_package"):
        found = package_gcc(target)
        if found:
            return found
        dl = target.get("gcc_download")
        hits = sorted(glob.glob(str(TOOLS_HOME / dl["release"] / "**" / "bin" / exe), recursive=True)) if dl else []
        if hits:
            return hits[0]          # flick setup
        sys.exit(f"error: {gcc_name} not found. Run: {FLICK_CMD} setup")
    hits = sorted(glob.glob(str(TOOLS_HOME / "*" / "*" / "bin" / exe)))     # flick setup
    if hits:
        return hits[-1]
    on_path = shutil.which(gcc_name)
    if on_path:
        return on_path
    roots = []
    if os.environ.get("LOCALAPPDATA"):
        roots.append(Path(os.environ["LOCALAPPDATA"]) / "Arduino15")
    roots.append(Path.home() / ".arduino15")
    roots.append(Path.home() / "Library" / "Arduino15")
    for r in roots:
        hits = sorted(glob.glob(str(r / "packages" / "esp32" / "tools" / "esp-*32" / "*" / "bin" / exe)))  # esp-x32, esp-rv32
        if hits:
            return hits[-1]
    sys.exit(f"error: {gcc_name} not found. Run: {FLICK_CMD} setup")


def teensy_gcc(target, exe, quiet=False):
    """Teensyduino's compiler: from the Arduino IDE's or arduino-cli's Boards
    Manager install, else the copy `flick setup` puts in ~/.flick/tools
    (chip.json "teensy_download", laid out as an Arduino package, so
    teensy_core_dir() finds the core and libraries next to it the same way)."""
    roots = [*arduino_data_dirs()]
    if os.environ.get("LOCALAPPDATA"):
        roots.append(Path(os.environ["LOCALAPPDATA"]) / "Arduino15")
    roots += [Path.home() / ".arduino15", Path.home() / "Library" / "Arduino15"]
    pattern = Path("tools") / "teensy-compile" / "*" / "arm" / "bin" / exe
    for base in [*(r / "packages" / "teensy" for r in roots), *TOOLS_HOME.glob("teensyduino-*")]:
        hits = sorted(glob.glob(str(base / pattern)), key=lambda h: version_key(Path(h).parents[2].name))
        if hits:
            return hits[-1]
    if quiet:
        return None
    sys.exit(f"error: {target['gcc']} (Teensyduino's) not found. Run: {FLICK_CMD} setup")


def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        sys.exit(r.returncode)
    return r


# ---------------------------------------------------------------- project

SKETCH_TEMPLATE = """\
// {name}.ino - a Flick sketch. baseline.h is included automatically.

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.printf("Hello from {name} on %s, Baseline %s\\n", BL_BOARD_NAME, bl_baseline_version());
}

void loop()
{
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
"""


# Secrets (WiFi passwords, API keys) live in ~/.flick/secrets.cfg, outside every
# sketch and repository folder, so they can't be committed by accident and every
# copy of Flick on the PC shares them. $FLICK_SECRETS points somewhere else.
SECRETS_TEMPLATE = """# Secrets for your sketches. Each KEY = value line becomes #define KEY "value"
# when flick builds, so passwords and keys stay out of your code.
# The WiFi examples use WIFI_SSID and WIFI_PASSWORD. Set them with:
#   python flick.py wifi "network name" "password"
"""
SECRET_KEY = re.compile(r"[A-Z_][A-Z0-9_]*")


def secrets_file():
    return Path(os.environ.get("FLICK_SECRETS") or FLICK_HOME / "secrets.cfg")


def read_secrets(f):
    """{KEY: value} from a secrets file."""
    out = {}
    if f.exists():
        for line in f.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = (x.strip() for x in line.split("=", 1))
                if SECRET_KEY.fullmatch(k):
                    out[k] = v
    return out


def secret_defines():
    """-DKEY="value" for each KEY = value in the secrets file.
    Keeps WiFi passwords and API keys out of sketches: #ifdef WIFI_SSID ... in code."""
    f = secrets_file()
    old = WORK / "secrets.cfg"          # where Flick 0.2 kept them
    if not f.exists() and old.exists() and "FLICK_SECRETS" not in os.environ:
        print(f"note: using {old}; run  {FLICK_CMD} secrets  to move it to {f}")
        f = old
    out = []
    for k, v in read_secrets(f).items():
        v = v.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'-D{k}="{v}"')
    return out


def write_secrets(f, values):
    """Set KEY = value lines in the secrets file, keeping its other lines."""
    lines = f.read_text(encoding="utf-8").splitlines() if f.exists() else SECRETS_TEMPLATE.splitlines()
    for k, v in values.items():
        if not SECRET_KEY.fullmatch(k):
            sys.exit(f"error: '{k}' is not a valid name (capital letters, digits and _)")
        if "\n" in v or "\r" in v:
            sys.exit(f"error: {k} can't contain a line break")
        new = f"{k} = {v}"
        for i, line in enumerate(lines):
            if not line.lstrip().startswith("#") and line.split("=", 1)[0].strip() == k:
                lines[i] = new
                break
        else:
            lines.append(new)
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text("\n".join(lines) + "\n", encoding="utf-8")


def cmd_secrets(args):
    """Show where the secrets file is and which names it sets (never the values);
    create it, or move one from the old place next to flick.py."""
    f = secrets_file()
    old = WORK / "secrets.cfg"
    if not f.exists() and old.exists() and "FLICK_SECRETS" not in os.environ:
        f.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(old), str(f))
        print(f"moved {old} to {f}")
    if args.set:
        k, _, v = args.set.partition("=")
        write_secrets(f, {k.strip(): v.strip()})
        print(f"saved {k.strip()} in {f}")
        return
    if not f.exists():
        write_secrets(f, {})
        print(f"created {f}")
    names = list(read_secrets(f))
    print(f"secrets file: {f}")
    print(f"sets: {', '.join(names)}" if names else f"sets nothing yet: {FLICK_CMD} wifi \"network\" \"password\"")


def cmd_wifi(args):
    """Save the WiFi network the examples and your sketches join (WIFI_SSID, WIFI_PASSWORD)."""
    f = secrets_file()
    write_secrets(f, {"WIFI_SSID": args.ssid, "WIFI_PASSWORD": args.password})
    print(f"saved the WiFi network name and password in {f}")
    print("rebuild and upload a sketch to use them")


def load_config(proj):
    """Name defaults to the folder name. An optional sketch.cfg may set
    `name = ...`, `stack = <bytes>`, `target = <target>` and
    `extensions = <folder>[, <folder>]` (Baseline extensions the sketch uses,
    relative to the sketch: their headers are on its include path)."""
    cfg = {"name": proj.name, "stack": "8192"}
    f = proj / "sketch.cfg"
    if f.exists():
        for line in f.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if "=" in line:
                k, v = (s.strip() for s in line.split("=", 1))
                cfg[k] = v
    # The image header holds 31 bytes of name (char name[32], sketch_abi.h): the
    # label `flick info` shows. A longer name is cut there; files keep the whole name.
    label = cfg["name"].encode()[:31].decode(errors="ignore")
    if label != cfg["name"]:
        print(f"note: the sketch name is longer than 31 bytes; the board shows it as {label}")
    cfg["label"] = label
    return cfg


def example_dirs(root=None):
    """Every sketch folder (X/X.ino) under examples/, in order: 01_basics/... first."""
    root = root or WORK / "examples"
    return sorted(p.parent for p in root.rglob("*.ino")
                  if p.stem == p.parent.name and "build" not in p.relative_to(root).parts)


def cmd_new(args):
    proj = Path(args.dir)
    if len(proj.parts) == 1:
        proj = WORK / "my_sketches" / proj        # a bare name: the place for your own sketches
    if proj.exists() and any(proj.iterdir()):
        sys.exit(f"error: {proj} exists and is not empty")
    proj.mkdir(parents=True, exist_ok=True)
    name = proj.name
    (proj / f"{name}.ino").write_text(SKETCH_TEMPLATE.replace("{name}", name))
    print(f"created {proj / (name + '.ino')}")


# ---------------------------------------------------------------- build

ARDUINO_BENCH = ROOT / "benchmarks" / "arduino"

# Seen by sketches and libraries, like an Arduino-ESP32 build. Libraries test
# these to pick their ESP32 code paths. ESP-IDF builds everything with
# _GNU_SOURCE, so the C library declares strcasestr, memmem, ... there too.
PLATFORM_DEFINES = ["-DARDUINO=10812", "-DARDUINO_ARCH_ESP32", "-DESP32", "-D_GNU_SOURCE"]

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
        if text.startswith("//", i) or (c == "#" and not text[text.rfind("\n", 0, i) + 1:i].strip(" \t")):
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


COND_RE = re.compile(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b")


def _conditionals(text):
    """For each line of text: the #if groups open there, outermost first, each
    as the directive lines seen so far in its group (["#ifdef X", "#else"])."""
    stack, per_line = [], []
    for line in text.split("\n"):
        per_line.append([list(g) for g in stack])
        m = COND_RE.match(line)
        if not m:
            continue
        kind = m.group(1)
        if kind in ("if", "ifdef", "ifndef"):
            stack.append([line.strip()])
        elif kind in ("elif", "else") and stack:
            stack[-1].append(line.strip())
        elif kind == "endif" and stack:
            stack.pop()
    return per_line


def merge_inos(inos, main):
    """A sketch's .ino files as one text, the way the Arduino IDE compiles them:
    the main file first, then the others in alphabetical order, each starting
    with a #line so messages name the original file. Returns the text and a
    function mapping a line of the text to (file, line in that file)."""
    parts, starts, line = [], [], 1
    for f in [main] + sorted((f for f in inos if f != main), key=lambda f: f.name):
        body = f.read_text(errors="ignore")
        if not body.endswith("\n"):
            body += "\n"
        parts.append(f'#line 1 "{f.as_posix()}"\n' + body)
        starts.append((line, f.as_posix()))          # the #line itself is on this line
        line += 1 + body.count("\n")

    def where(n):
        start, name = next(s for s in reversed(starts) if s[0] <= n)
        return name, n - start
    return "".join(parts), where


def ino_with_prototypes(src, text=None, where=None):
    """The .ino's text with prototypes added, or None if it needs none. A
    function defined inside #if/#ifdef gets its prototype inside the same
    conditions, so two versions of one function (#ifdef USE_SPI ... #else ...)
    declare only the one that is compiled, as with the Arduino IDE.
    text/where: several .ino files merged by merge_inos()."""
    if text is None:
        text = src.read_text(errors="ignore")
    conds = _conditionals(text)
    clean = _blank_comments_and_strings(text)
    # Top level only: blank out everything inside braces (keeping the braces).
    # Only one branch of an #if group is compiled, so each #elif/#else branch
    # starts at the brace depth its #if had, and after #endif the depth is the
    # first branch's (two branches that each open a function body, as in
    # "#ifdef X / void f(A a) { / #else / void f(B b) { / #endif", count once).
    flat, depth = [], 0
    groups = []             # per open #if group: [depth at #if, depth after the first branch]
    src_lines = text.split("\n")
    for ln, line in enumerate(clean.split("\n")):
        m = COND_RE.match(src_lines[ln]) if ln < len(src_lines) else None
        if m:
            kind = m.group(1)
            if kind in ("if", "ifdef", "ifndef"):
                groups.append([depth, None])
            elif kind in ("elif", "else") and groups:
                if groups[-1][1] is None:
                    groups[-1][1] = depth
                depth = groups[-1][0]
            elif kind == "endif" and groups:
                start, first = groups.pop()
                if first is not None:
                    depth = first
        for ch in line:
            if ch == "{":
                flat.append(ch if depth == 0 else " ")
                depth += 1
            elif ch == "}":
                depth = max(0, depth - 1)
                flat.append(ch if depth == 0 else " ")
            else:
                flat.append(ch if depth == 0 else " ")
        flat.append("\n")
    flat = "".join(flat)[:len(clean)]
    # Blanked function bodies leave long runs of whitespace, over which the
    # regex is slow (quadratic in the run's length). Search a copy where each run
    # is one character ("\n" if it held a newline), mapping offsets back with pos.
    short, pos = [], []
    for m in re.finditer(r"\s+|\S+", flat):
        s = m.group()
        if s[0].isspace():
            short.append("\n" if "\n" in s else " ")
            pos.append(m.start())
        else:
            short.append(s)
            pos.extend(range(m.start(), m.end()))
    short = "".join(short)
    newlines = [i for i, c in enumerate(text) if c == "\n"]
    protos, first = [], None
    for m in FUNC_DEF_RE.finditer(short):
        sig, name, args = m.group(1), m.group(2), m.group(3)
        head = sig[: sig.rfind(name)].strip()
        # An elaborated type in a parameter ("struct Settings *s") is fine; in front of
        # the name these words mean a type or something that can't be declared again.
        if (name in PROTO_KEYWORDS or not head or re.search(r"\b(class|struct|enum|union|namespace|template|operator)\b", head)
                or re.search(r"\b(class|namespace|template|operator)\b|\[", sig)):
            continue
        if first is None:
            first = pos[m.start(1)]     # prototypes go before the first function, even setup()
        if name in PROTO_DECLARED or "=" in args:
            continue                    # already declared / default arguments may not be repeated
        proto = " ".join(sig.split()) + ";"
        groups = conds[bisect.bisect_left(newlines, pos[m.start(2)])]
        if groups:
            proto = "\n".join([*(d for g in groups for d in g), proto, *["#endif"] * len(groups)])
        protos.append(proto)
    if not protos:
        return None
    line = text.count("\n", 0, first) + 1
    # Insert at the start of the line that holds the first definition.
    cut = text.rfind("\n", 0, first) + 1
    if where:
        name, line = where(line)
        head = ""                       # merged text starts with its own #line
    else:
        name = src.as_posix()
        head = f'#line 1 "{name}"\n'
    return (head + text[:cut] + "\n".join(protos) + "\n" +
            f'#line {line} "{name}"\n' + text[cut:])


# ---------------------------------------------------------------- Arduino libraries
# Sketches can use ordinary Arduino libraries (SparkFun Qwiic drivers, ArduinoJson,
# ...). Like the Arduino builder, flick follows #include lines: a header that is not
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
    cli = shutil.which("arduino-cli") or os.environ.get("FLICK_ARDUINO_CLI")
    if cli:
        cfgs.append(Path(cli).parent / "arduino-cli.yaml")
    for cfg in cfgs:
        if cfg.exists():
            for line in cfg.read_text(errors="ignore").splitlines():
                if line.strip().startswith("user:"):
                    dirs.append(Path(line.split(":", 1)[1].strip()))
    dirs.append(Path.home() / "Documents" / "Arduino")
    return dirs


def library_roots(proj, extra=()):
    roots = [proj / "libraries", WORK / "libraries"]       # the sketch's own, then flick lib install's
    roots += [Path(p) for p in os.environ.get("FLICK_LIBRARIES", "").split(os.pathsep) if p]
    roots += [d / "libraries" for d in arduino_user_dirs()]
    roots += list(extra)                                    # the platform's bundled libraries, last
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


def lang_flags(src):
    """Compiler flags for a source file's language, as the sketch build uses them."""
    if src.suffix == ".ino":
        # Like Arduino: C++, with the core header included automatically.
        return ["-x", "c++", *CXXFLAGS, "-include", "baseline.h"]
    return CXXFLAGS if src.suffix == ".cpp" else CFLAGS


def include_deps(gcc, args, files, cwd):
    """(headers found, headers missing) when the preprocessor reads `files`.
    -MG lists a missing header as written instead of stopping at it, and only
    the #includes the #if/#ifdef lines leave in are followed."""
    r = subprocess.run([gcc, *args, "-M", "-MG", *map(str, files)], cwd=cwd,
                       capture_output=True, text=True, errors="replace")
    text = r.stdout.replace("\\\r\n", " ").replace("\\\n", " ")
    found, missing = set(), set()
    for tok in re.split(r"(?<!\\)\s+", text):
        tok = tok.replace("\\ ", " ")
        if not tok or tok.endswith(":"):
            continue
        p = Path(tok)
        if p.is_absolute() and p.exists():
            found.add(str(p.resolve()))
        else:
            missing.add(tok.replace("\\", "/"))
    return found, missing


def resolve_libraries(proj, gcc, units, base_dirs, flags_for, after=(), extra_roots=(), cache=None):
    """The libraries the sketch needs, in discovery order. Like the Arduino builder,
    the preprocessor reads the sketch (`units`: (file, extra flags)) and then each
    library's sources: a header it can't find selects the library that provides it,
    whose include folder is added before reading again. #includes left out by
    #if/#ifdef select nothing. `cache` (a JSON file) skips all of this when no file
    that was read has changed."""
    roots = library_roots(proj, extra_roots)
    if not roots:
        return []
    key = json.dumps([str(gcc), [str(u) for u, _ in units], [str(d) for d in base_dirs], list(after),
                      [str(r) for r in roots], flags_for(Path("x.cpp")), flags_for(Path("x.c"))])
    if cache and cache.exists():
        try:
            c = json.loads(cache.read_text())
            if (c["key"] == key and all(os.path.getmtime(f) == t for f, t in c["deps"].items())
                    and not (c["unresolved"] and set(c["unresolved"]) & set(index_libraries(roots)))):
                return [Path(p) for p in c["libs"]]
        except (OSError, ValueError, KeyError):
            pass
    idx = index_libraries(roots)
    libs, deps, unresolved = [], set(), set()

    def lib_groups(lib):
        files = [f for f in lib_files(lib) if f.suffix in (".c", ".cpp")]
        return [([f for f in files if f.suffix == s], []) for s in (".cpp", ".c")
                if any(f.suffix == s for f in files)]

    queue = [([u], extra) for u, extra in units]
    while queue:
        files, extra = queue.pop(0)
        inc = [a for d in base_dirs + [d for lib in libs for d in lib_src_dirs(lib)] for a in ("-I", str(d))]
        found, missing = include_deps(gcc, [*flags_for(files[0]), *inc, *after, *extra], files, cache.parent if cache else None)
        deps |= found
        new = []
        for name in missing:
            lib = idx.get(name)
            if lib is None:
                unresolved.add(name)
            elif lib not in libs:
                libs.append(lib)
                new.append(lib)
        if new:
            queue.append((files, extra))            # read again: the new headers may pull in more
            for lib in new:
                queue += lib_groups(lib)
    if cache:
        # Generated files in the build folder change every build: their .ino sources are listed instead.
        deps = {f for f in deps if cache.parent.resolve() not in Path(f).parents}
        deps |= {str(p.resolve()) for p in proj.iterdir() if p.suffix in (".ino", ".c", ".cpp", ".h")}
        cache.write_text(json.dumps({"key": key, "libs": [str(l) for l in libs],
                                     "deps": {f: os.path.getmtime(f) for f in sorted(deps)},
                                     "unresolved": sorted(unresolved)}))
    return libs


def teensy_core_dir(gcc):
    """cores/teensy4 of the Teensyduino install whose compiler flick uses."""
    pkg = Path(gcc).resolve().parents[5]        # packages/teensy (gcc is in tools/teensy-compile/<v>/arm/bin)
    cores = sorted(pkg.glob("hardware/avr/*/cores/teensy4"), key=lambda p: version_key(p.parents[1].name))
    if not cores:
        sys.exit(f"error: Teensyduino's core not found next to {gcc}")
    return cores[-1]


# Names that belong to a feature or a platform: when the compiler says one is
# undeclared, flick says why in a line (the board doesn't have it).
FEATURE_NAMES = [
    (r"BLE\w*|BLESerial(Class)?", "Bluetooth LE", "BL_HAS_BLE"),
    (r"BluetoothSerial", "Bluetooth Classic", "BL_HAS_BT_CLASSIC"),
    (r"esp_now_\w+|ESP_NOW_\w+", "ESP-NOW", "BL_HAS_ESPNOW"),
    (r"WiFiClientSecure|NetworkClientSecure", "TLS (HTTPS)", "BL_HAS_TLS"),
    (r"HTTPClient|NetworkClient|NetworkUDP", "a network (WiFi, or Ethernet on a Teensy 4.1)", "BL_HAS_NET"),
    (r"Ethernet\w*", "Ethernet", "BL_HAS_ETHERNET"),
    (r"WiFi\w*|WebServer|DNSServer|MDNS|PubSubClient|Network\w*|ArduinoOTA|configT(z)?ime|WL_\w+",
     "WiFi", "BL_HAS_WIFI"),
    (r"touch\w+|T[0-9]+", "capacitive touch", "BL_HAS_TOUCH"),
    (r"dacWrite", "a DAC", "BL_HAS_DAC"),
    (r"rgbLedWrite|neopixelWrite|FastLED|CRGB\w*|CHSV", "WS2812 LED support", "BL_HAS_LED_STRIP"),
    (r"USB\w*", "USB device mode in this Baseline", "BL_HAS_USB_DEVICE"),
    (r"x(Task|Semaphore|Queue|Port)\w*|v(Task|Semaphore|Queue)\w*|ux(Task|Semaphore|Queue)\w*|pcTask\w*|"
     r"(Task|Semaphore|Queue)Handle_t|(U)?BaseType_t|TickType_t|portMAX_DELAY|portTICK_\w+|pdMS_TO_TICKS|pd(TRUE|FALSE|PASS|FAIL)",
     "FreeRTOS", "BL_HAS_RTOS"),
    (r"esp_\w+|ESP_\w+|ESP|EspClass|ledc\w+|ps_(m|c|re)alloc|psramFound|temperatureRead|analogReadMilliVolts|"
     r"analogSet(Pin)?Attenuation|timer(Begin|End|Start|Stop|Alarm|AttachInterrupt\w*)|"
     r"hw_timer_t|I2SClass|SD_MMC|getLocalTime|getCpuFrequencyMhz|log_[ewidv]|gpio_num_t|GPIO_NUM_\w+",
     "an ESP32 API", "BL_PLATFORM_ESP32"),
    (r"IntervalTimer|elapsedMicros|elapsedMillis|Audio\w+|extmem_\w+|analogWrite(Frequency|Resolution)|tempmonGetTemp|"
     r"BUILTIN_SDCARD|digital(Write|Read|Toggle)Fast", "a Teensy API", "BL_PLATFORM_TEENSY"),
]
UNDECLARED = re.compile(r"'([A-Za-z_]\w*)' (?:was not declared in this scope|does not name a type|has not been declared|"
                        r"was not declared|undeclared)")


def missing_feature_hints(output, target, sketch):
    """One line per missing feature: from an #error "This board has no ..." or an undeclared name."""
    board = f"{target['board_name']} ({target['name']})"
    # The chip's features, and the board's (an RP2350 board with a radio module sets BL_HAS_WIFI).
    have = set(header_defines(target["chip_dir"] / "sdk" / "bl_target.h"))
    have |= set(header_defines(target["dir"] / "sdk" / "bl_board.h"))
    hints = []
    for m in re.finditer(r'#error "(This (?:board|chip) has no [^"]*)"', output):
        hints.append(f"flick: {sketch} can't run on {board}: {m.group(1)}")
    for name in dict.fromkeys(UNDECLARED.findall(output)):
        for pattern, what, macro in FEATURE_NAMES:
            macros = (macro,) if isinstance(macro, str) else macro
            if re.fullmatch(pattern, name) and not any(m in have for m in macros):
                hints.append(f"flick: {name} is {what}; {board} doesn't have it.")
                break
    return list(dict.fromkeys(hints))


def sketch_build_dir(proj, target_name):
    """Where a sketch's build output goes: Flick's cache folder, not the sketch
    folder (which stays as the user wrote it). One folder per sketch path."""
    proj = Path(proj).resolve()
    tag = hashlib.sha256(str(proj).lower().encode()).hexdigest()[:8]
    return BUILD_ROOT / "sketches" / f"{proj.name}-{tag}" / target_name


def build(proj, target_name=None):
    proj = Path(proj).resolve()
    if proj == ARDUINO_BENCH or ARDUINO_BENCH in proj.parents:
        sys.exit(f"error: {proj.relative_to(WORK)} is a plain Arduino sketch, kept only for the build-time "
                 f"comparison (tools/benchmark.py). It does not run on Baseline.\n"
                 f"Its Flick version is in benchmarks/flick/{proj.name}/.")
    cfg = load_config(proj)
    inos = sorted(proj.glob("*.ino"))
    user = inos + sorted(proj.glob("*.c")) + sorted(proj.glob("*.cpp"))
    if not user:
        sys.exit(f"error: no .ino, .c or .cpp files in {proj}")
    if inos and not (proj / (proj.name + ".ino")).exists():
        print(f"warning: expected {proj.name}.ino (Arduino names the main file after its folder)")
    cxx = any(s.suffix in (".ino", ".cpp") for s in user)
    target = load_target(target_name or cfg.get("target") or DEFAULT_TARGET)
    out = sketch_build_dir(proj, target["name"])
    out.mkdir(parents=True, exist_ok=True)
    gcc = find_gcc(target)
    t0 = time.time()

    base_dirs = [SDK / "include", SDK / "include" / "compat", *target["sdk_dirs"], proj]
    # Baseline extensions the sketch uses (docs/EXTENSIONS.md): their interface headers
    for d in filter(None, (d.strip() for d in cfg.get("extensions", "").split(","))):
        ext = (proj / d).resolve()
        if not ext.is_dir():
            sys.exit(f"error: sketch.cfg extensions: no folder {ext}")
        base_dirs.append(ext)
    # Teensy: Teensyduino's bundled libraries too (FlexCAN_T4, Encoder, ...), after
    # the user's; headers the SDK provides (Wire.h, SD.h, Audio.h, ...) never come from them.
    extra = [teensy_core_dir(gcc).parents[1] / "libraries"] if target.get("builder") == "teensy" else []
    # Teensy: last on the path, after the SDK's own headers: Teensy's register definitions (imxrt.h).
    after = ["-idirafter", str(teensy_core_dir(gcc))] if target.get("builder") == "teensy" else []
    defs = [f'-DBL_SKETCH_NAME="{cfg["label"]}"',f"-DBL_SKETCH_STACK={int(cfg['stack'])}", *secret_defines()]
    flags_for = lambda src: [*lang_flags(src), *target["cflags"], *target.get("defines", PLATFORM_DEFINES)]

    # The sketch's translation units: (file to compile, its object, extra flags).
    units = []
    if len(inos) > 1:
        # Several .ino files are one translation unit, as in the Arduino IDE:
        # they share globals, types and functions without headers.
        main = proj / (proj.name + ".ino")
        text, where = merge_inos(inos, main if main in inos else inos[0])
        gen = out / (proj.name + ".ino.merged.cpp")
        gen.write_text(ino_with_prototypes(main, text, where) or text)
        units.append((gen, out / (proj.name + ".ino.o"), ["-include", "baseline.h", *defs]))
    for src in ([] if len(inos) > 1 else inos) + user[len(inos):] + [SDK / "src" / "sketch_crt.c"]:
        obj = out / (src.name + ".o")
        if src.suffix == ".ino":
            with_protos = ino_with_prototypes(src)
            if with_protos is not None:
                gen = out / (src.name + ".cpp")
                gen.write_text(with_protos)
                units.append((gen, obj, ["-include", "baseline.h", *defs]))
                continue
        units.append((src, obj, defs))

    libs = resolve_libraries(proj, gcc, [(u, d) for u, _, d in units], base_dirs, flags_for, after, extra,
                             cache=out / "libraries.json")
    inc = []
    for d in base_dirs + [d for lib in libs for d in lib_src_dirs(lib)]:
        inc += ["-I", str(d)]
    inc += after

    def compile_one(src, obj, extra_defs=(), quiet=False):
        obj.parent.mkdir(parents=True, exist_ok=True)
        r = subprocess.run([gcc, *flags_for(src), *inc,
                            *extra_defs, *(["-w"] if quiet else []), "-c", str(src), "-o", str(obj)],
                           capture_output=True, text=True, errors="replace")
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        if r.returncode != 0:
            for hint in missing_feature_hints(r.stdout + r.stderr, target, proj.name):
                print(hint, file=sys.stderr)
            sys.exit(r.returncode)

    objs = []
    for src, obj, d in units:
        compile_one(src, obj, d)
        objs.append(str(obj))
    # Libraries: compiled into build/<target>/libs/<library>/, and only again when a
    # file of that library (or the SDK/flick) changes. Their warnings are not shown.
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
    # every sketch: compile them once per target and reuse them (one cache per SDK folder).
    cache = BUILD_ROOT / "sdk" / f"{hashlib.sha256(str(SDK).lower().encode()).hexdigest()[:8]}" / target["name"]
    cache.mkdir(parents=True, exist_ok=True)
    newest_dep = max(p.stat().st_mtime for p in [*(SDK / "include").glob("*.h"),
                                                 *(f for d in target["sdk_dirs"] for f in d.glob("*")),
                                                 target["dir"] / "target.json",
                                                 target["chip_dir"] / "chip.json", Path(__file__)])
    shared = ["sketch_libc.c", "sketch_time.c", "sketch_ctype.c", "sketch_rtos.c", *target["sdk_sources"]] + (
        ["sketch_cxx.cpp", "sketch_arduino.cpp", "sketch_string.cpp", "sketch_espnow.cpp",
         "sketch_net.cpp", "sketch_io.cpp", "sketch_ble.cpp", "sketch_storage.cpp",
         "sketch_mqtt.cpp", "sketch_system.cpp", "sketch_fastled.cpp", "sketch_usb.cpp",
         "sketch_extras.cpp", "sketch_usbhost.cpp", "sketch_idf.cpp"] if cxx else [])
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
         *objs, "-Wl,--start-group", "-lstdc++", "-lm", "-lgcc", *target.get("libs", []), "-Wl,--end-group",
         "-o", str(elf)])

    image = make_image(elf.read_bytes(), target)
    flick = out / (cfg["name"] + ".flick")
    flick.write_bytes(image)
    sha = image[HDR_OFF_SHA256:HDR_OFF_SHA256 + 32].hex()
    abi = f"{struct.unpack_from('<H', image, 8)[0]}.{struct.unpack_from('<H', image, HDR_OFF_ABI_MINOR)[0]}"
    print(f"built {cfg['name']}.flick for {target['name']}: {len(image)} bytes, needs Baseline ABI {abi}, "
          f"sha256 {sha[:16]}..., {time.time() - t0:.2f}s")
    return flick


def elf_section(elf, name):
    """The contents of a 32-bit ELF's section `name`, or b"" if it has none."""
    e_shoff, = struct.unpack_from("<I", elf, 32)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", elf, 46)
    if not e_shoff or e_shstrndx >= e_shnum:
        return b""
    shdr = lambda i: struct.unpack_from("<10I", elf, e_shoff + i * e_shentsize)
    strtab = shdr(e_shstrndx)
    want = name.encode()
    for i in range(e_shnum):
        sh_name, sh_type, _flags, _addr, sh_offset, sh_size = shdr(i)[:6]
        at = strtab[4] + sh_name
        if elf[at:at + len(want) + 1] == want + b"\0":
            return b"" if sh_type == 8 else elf[sh_offset:sh_offset + sh_size]     # SHT_NOBITS: no contents
    return b""


def elf_symbol(elf, name):
    """(value, size) of a 32-bit ELF's symbol `name` (local ones too), or None."""
    e_shoff, = struct.unpack_from("<I", elf, 32)
    e_shentsize, e_shnum = struct.unpack_from("<HH", elf, 46)
    shdr = lambda i: struct.unpack_from("<10I", elf, e_shoff + i * e_shentsize)
    want = name.encode() + b"\0"
    for i in range(e_shnum):
        h = shdr(i)
        if h[1] != 2:       # SHT_SYMTAB
            continue
        strtab_off = shdr(h[6])[4]
        for at in range(h[4], h[4] + h[5], 16):
            st_name, st_value, st_size = struct.unpack_from("<III", elf, at)
            if elf[strtab_off + st_name:strtab_off + st_name + len(want)] == want:
                return st_value, st_size
    return None


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
    # The oldest Baseline it runs on: the newest table entry the linked code calls.
    need = elf_section(elf, ".bl_need")
    struct.pack_into("<H", image, HDR_OFF_ABI_MINOR, max(need, default=0))
    build_id = hashlib.sha256(bytes(image) + str(time.time()).encode()).digest()[:16]
    image[HDR_OFF_BUILD_ID:HDR_OFF_BUILD_ID + 16] = build_id
    image[HDR_OFF_SHA256:HDR_OFF_SHA256 + 32] = bytes(32)
    image[HDR_OFF_SHA256:HDR_OFF_SHA256 + 32] = hashlib.sha256(image).digest()
    return bytes(image)


def cmd_build(args):
    flick = build(args.dir, args.target)
    if args.out:
        dest = Path(args.out)
        if dest.is_dir() or args.out.endswith(("/", "\\")):
            dest = dest / flick.name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(flick, dest)
        print(f"copied to {dest}")
    else:
        print(f"  {flick}")


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


WAKE_WAIT_S = 60       # how long upload/safe wait for a board that is asleep


def catch_waking_board(port=None):
    """A board that doesn't answer on USB (asleep: a sketch in a deep-sleep loop
    shows up for a moment after each wake): wait for it, and put it in safe mode
    the moment it answers, before its sketch sleeps again. Returns the port once
    the board is back in safe mode, or None if it never answered."""
    serial = import_serial()
    vids = board_vids()
    print(f"No answer from a board{f' on {port}' if port else ''}: waiting up to {WAKE_WAIT_S} s "
          "(a board in deep sleep answers when it wakes; or plug one in)...", flush=True)
    # SAFE's own reply: a late answer to an earlier request (INFO sent while the
    # board slept) can arrive first and must not count.
    ok = f"{PROTO_PREFIX} OK rebooting into safe mode".encode()
    deadline = time.time() + WAKE_WAIT_S
    dev = None
    while time.time() < deadline and not dev:
        found = [p.device for p in serial.tools.list_ports.comports()
                 if (p.device == port if port else p.vid in vids)]
        if not found:
            time.sleep(0.01)
            continue
        try:
            with open_port(found[0], 115200) as s:
                got = b""
                for _ in range(30):     # resend until the loader answers (it starts with Baseline)
                    s.write(f"{PROTO_PREFIX} SAFE\n".encode())
                    got += s.read(256)
                    if ok in got:
                        dev = found[0]
                        break
        except Exception:
            pass                        # asleep again, or not open yet: wait for the next wake
        if not dev:
            time.sleep(0.05)
    if not dev:
        return None
    print(f"Caught the board on {dev}: it restarts in safe mode (no sketch runs).")
    time.sleep(0.5)
    deadline = time.time() + 15
    while time.time() < deadline:     # it restarts: wait for its port again
        if any(p.device == dev for p in serial.tools.list_ports.comports()):
            time.sleep(0.3)
            return dev
        time.sleep(0.05)
    return None


def find_port(port=None, wait=False):
    """The given port, or the single supported board that is plugged in. With
    wait, a board that isn't there (asleep) is waited for and put in safe mode."""
    serial = import_serial()
    if port:
        deadline = time.time() + 3      # a board restarting after an upload is back soon
        while not any(p.device == port for p in serial.tools.list_ports.comports()):
            if time.time() > deadline:
                if wait:
                    return catch_waking_board(port) or sys.exit(f"error: {port} didn't appear.")
                break
            time.sleep(0.05)
        return port
    vids = board_vids()
    found = [p for p in serial.tools.list_ports.comports() if p.vid in vids]
    if len(found) == 1:
        return found[0].device
    if not found and wait:
        return catch_waking_board() or sys.exit("error: no supported board appeared on USB.")
    if not found:
        sys.exit("error: no supported board found on USB. Plug in the board, or pass -p PORT.")
    names = ", ".join(f"{p.device} ({p.description})" for p in found)
    sys.exit(f"error: several boards found: {names}. Pass -p PORT to pick one.")


ESPRESSIF_VID = 0x303A
PJRC_VID = 0x16C0       # Teensy: native USB serial, like Espressif's
RP2_VIDS = (0x2E8A, 0x1B4F)     # arduino-pico's USB serial: Raspberry Pi's IDs, or SparkFun's on its boards
USB_SERIAL_JTAG_PID = 0x1001


def is_tinyusb_port(port):
    """An Espressif TinyUSB serial port (S3 USB device mode), not USB-Serial-JTAG."""
    serial = import_serial()
    for p in serial.tools.list_ports.comports():
        if p.device == port:
            return p.vid == ESPRESSIF_VID and p.pid != USB_SERIAL_JTAG_PID
    return False


def is_rp2_port(port):
    """arduino-pico's USB serial (Baseline on an RP2040 or RP2350)."""
    serial = import_serial()
    return any(p.device == port and p.vid in RP2_VIDS for p in serial.tools.list_ports.comports())


def is_teensy_port(port):
    serial = import_serial()
    return any(p.device == port and p.vid == PJRC_VID for p in serial.tools.list_ports.comports())


def open_port(port, baud):
    serial = import_serial()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.1
    # Keep DTR/RTS released so opening the port does not reset the board
    # (CH340/CP210x wire them to EN/IO0; USB-Serial-JTAG resets on them too).
    # A TinyUSB serial port is the exception: Arduino's USBCDC discards output
    # until the host raises DTR, and DTR alone never resets the chip there.
    # arduino-pico's USB serial (RP2040/RP2350) does the same; it reboots into
    # the boot ROM only on 1200 baud, which flick never uses. A Teensy sends
    # its first output about 1.5 s late while DTR is low; it reboots only on
    # 134 baud.
    s.dtr = is_tinyusb_port(port) or is_rp2_port(port) or is_teensy_port(port)
    s.rts = False
    s.open()
    return s


# Baud rates Baseline's console may use on a USB-serial chip, most likely first:
# 115200 (Baseline 0.3 and later, and the oldest ones), 921600 (0.1 to 0.2) and
# 2000000 (one build while 2 Mbaud was tried). Native USB ports ignore the rate.
CONSOLE_BAUDS = (115200, 921600, 2000000)
# For an upload, flick asks the console to run at UPLOAD_BAUD (@BSK BAUD, Baseline
# 0.3+). Baseline's reply says how many lines may be in flight (window=): the
# round trip through the USB-serial chip costs more than the line itself.
UPLOAD_BAUD = 921600
_baud_cache = {}


def is_native_usb_port(port):
    serial = import_serial()
    return any(p.device == port and p.vid in (ESPRESSIF_VID, PJRC_VID, *RP2_VIDS)
               for p in serial.tools.list_ports.comports())


def console_baud(port, baud=None):
    """The baud rate to talk to Baseline on this port: the one given, or found
    by asking at each of CONSOLE_BAUDS until Baseline answers."""
    if baud:
        return baud
    if port in _baud_cache:
        return _baud_cache[port]
    if is_native_usb_port(port):
        return 115200
    for b in CONSOLE_BAUDS:
        try:
            with open_port(port, b) as s:
                s.reset_input_buffer()
                # The newline ends whatever an earlier probe at a wrong rate left
                # half-received, so the loader sees INFO at the start of a line.
                s.write(f"\n{PROTO_PREFIX} INFO\n".encode())
                deadline, buf = time.time() + 1.0, b""
                while time.time() < deadline:
                    buf += s.read(s.in_waiting or 1)
                    if f"{PROTO_PREFIX} OK".encode() in buf:
                        _baud_cache[port] = b
                        return b
        except OSError:
            break
    # Nothing answered (no Baseline yet, or it is restarting): assume the current
    # rate, and end the line the probes at other rates left half-received, or the
    # next command would arrive mid-line and go to the sketch.
    try:
        with open_port(port, CONSOLE_BAUDS[0]) as s:
            s.write(b"\n")
            s.flush()
    except OSError:
        pass
    return CONSOLE_BAUDS[0]


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
        conn = NetPort(args.host, args.password or os.environ.get("FLICK_OTA_PASSWORD"))
        return conn.port, conn
    port = find_port(args.port, wait=getattr(args, "wait", False))
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
                # A sketch that prints without a newline ("....") puts Baseline's
                # reply at the end of its own line: hand back the sketch's text
                # first, and the reply as a line of its own next.
                i = line.find(PROTO_PREFIX.encode() + b" ")
                if i > 0:
                    self.buf = line[i:] + b"\n" + self.buf
                    line = line[:i]
                return line.decode(errors="replace").rstrip("\r")
            self.buf += self.ser.read(self.ser.in_waiting or 1)
        return None

    def request_status(self, line, timeout=5.0):
        """(True, text) for OK, (False, text) for ERR, None if no reply."""
        self.send(line)
        return self.reply(timeout)

    def reply(self, timeout=5.0):
        """The next reply to a line already sent: (ok, text), or None."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = self.readline(deadline - time.time())
            if r is None:
                break
            if r.startswith(PROTO_PREFIX + " "):
                body = r[len(PROTO_PREFIX) + 1:]
                if body.startswith("OK"):
                    return True, body[2:].strip()
                if body.startswith("ERR"):
                    return False, body[3:].strip()
            elif self.echo and r:
                print(r)
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
            sys.exit(f"\nerror: no reply from {self.ser.port}. Is Baseline on the board?\n"
                     f"Put it on once from the Flick web page (Chrome or Edge): pick your board and press Flash.\n"
                     f"  {WEB_PAGE}\n"
                     f"Or download baseline-<board>.bin there and run: "
                     f"{FLICK_CMD} baseline flash --file baseline-<board>.bin")
        sys.exit(f"\nerror: Baseline stopped answering during {cmd} on {self.ser.port}.")


def cmd_info(args):
    _, s = open_link_port(args)
    with s:
        info = Link(s).request("INFO")
    print(info)
    for line in describe_sketch_addresses(info_fields(info)):
        print(line)


def describe_sketch_addresses(fields):
    """Source lines for the addresses in INFO's status (a while(1) stop) and crash
    (the sketch's pc), from the newest build of that sketch in Flick's cache."""
    sketch, target = fields.get("sketch", "-"), fields.get("target")
    if sketch == "-" or not target or not (TARGETS / target / "target.json").exists():
        return []
    elfs = sorted(BUILD_ROOT.glob(f"sketches/{sketch}-*/{target}/{sketch}.elf"), key=lambda p: p.stat().st_mtime)
    if not elfs:
        return []
    t = load_target(target)
    out = []
    for what, key, pattern in (("stopped", "status", r"while\(1\) at (0x[0-9a-f]+)"), ("crashed", "crash", r"pc=(0x[0-9a-f]+)")):
        m = re.search(pattern, fields.get(key, ""))
        if not m or not (t["ibus"] <= int(m.group(1), 16) < t["ibus"] + t["max_size"]):
            continue
        a2l = find_gcc(t)[:-len("gcc.exe" if os.name == "nt" else "gcc")] + "addr2line"
        r = subprocess.run([a2l, "-f", "-C", "-e", str(elfs[-1]), m.group(1)], capture_output=True, text=True)
        lines = r.stdout.splitlines()
        if r.returncode == 0 and len(lines) >= 2 and not lines[1].startswith("??"):
            loc = re.match(r"(.*):(\d+)", lines[1])     # path:line, maybe followed by (discriminator N)
            where = f"{Path(loc.group(1)).name}:{loc.group(2)}" if loc else lines[1]
            out.append(f"  {what} in {lines[0]}() at {where}")
    return out


def cmd_safe(args):
    port = find_port(args.port, wait=True)
    with open_port(port, console_baud(port, args.baud)) as s:
        Link(s).request("SAFE")
        print("rebooting into safe mode")


def info_fields(info):
    """'baseline=0.1.0 abi=1.5 chip=esp32 ...' -> dict"""
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', info))


def awake_info(s):
    """INFO's reply, or None if the board doesn't answer (or its port goes away)."""
    try:
        r = Link(s, echo=False).request_status("INFO", timeout=1.5)
    except Exception:
        return None
    return r[1] if r and r[0] else None


def cmd_upload(args):
    if args.host and args.monitor:
        sys.exit("error: --monitor needs the USB port; it cannot be combined with --host")
    args.wait = True        # a board asleep in a deep-sleep loop is caught when it wakes
    port, s = open_link_port(args)
    if not args.host and awake_info(s) is None:
        s.close()
        # A board that just restarted (an upload a moment ago) is back within a
        # few seconds; only one that stays silent is asleep.
        s = None
        deadline = time.time() + 3
        while time.time() < deadline and s is None:
            time.sleep(0.1)
            try:
                s = open_port(port, args.baud)
            except Exception:
                continue
            if awake_info(s) is None:
                s.close()
                s = None
        if s is None:   # asleep (or going to sleep): catch it when it wakes
            port = catch_waking_board(port) or sys.exit(f"error: no answer from a board on {port}.")
            s = open_port(port, args.baud)
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
        image = build(args.dir, board).read_bytes()
        serial = import_serial()
        try:
            send_image(link, image)
        except (serial.SerialException, OSError):
            if args.host:
                raise
            # The port went away under the upload: a sketch that sleeps (or
            # restarts) did so while the image was built. Catch the board when
            # it is back, in safe mode, and send the image again.
            print(f"{port} went away during the upload (the sketch sleeps or restarts).", flush=True)
            s.close()
            port = catch_waking_board(port) or sys.exit(f"error: no answer from a board on {port}.")
            s = open_port(port, args.baud)
            link = Link(s)
            send_image(link, image)
        if args.monitor:
            monitor(s, port, args.baud, link, follow=not args.port)


def fast_console(link):
    """Before an upload: a UART console (USB-serial chip) goes to UPLOAD_BAUD for its
    length. Returns (lines to keep in flight, the rate to go back to or None).
    Native USB has no rate to raise; over WiFi, TCP keeps many lines in flight."""
    ser = link.ser
    if isinstance(ser, NetPort):
        return 16, None
    if is_native_usb_port(ser.port):
        return 1, None
    old = ser.baudrate
    r = link.request_status(f"BAUD {UPLOAD_BAUD}", timeout=2)
    if not r or not r[0]:
        return 1, None          # a Baseline from before 0.3: one line at a time at its own rate
    window = int(info_fields(r[1]).get("window", "1"))
    if old != UPLOAD_BAUD:
        ser.flush()
        ser.baudrate = UPLOAD_BAUD
        ser.reset_input_buffer()
        link.buf = b""
    return max(1, window), (old if old != UPLOAD_BAUD else None)


def console_back(link, rate):
    """After an upload, the board restarts at its console rate: follow it."""
    if rate:
        link.ser.baudrate = rate
        link.buf = b""


def send_lines(link, cmd, data, window, progress=None):
    """<cmd> <offset> <base64> lines for all of data, with up to window lines in flight
    (Baseline answers them in order)."""
    pending = 0
    for off in range(0, len(data), CHUNK):
        link.send(f"{cmd} {off} {base64.b64encode(data[off:off + CHUNK]).decode()}")
        pending += 1
        while pending >= window or (pending and off + CHUNK >= len(data)):
            r = link.reply(10)
            if not r or not r[0]:
                sys.exit(f"\nerror from Baseline: {r[1] if r else 'no reply'} (during {cmd})")
            pending -= 1
        if progress:
            progress(min(off + CHUNK, len(data)))


def send_image(link, image, quiet=False):
    """Upload a .flick image over an open link; Baseline installs it and reboots."""
    digest = hashlib.sha256(image).hexdigest()
    t0 = time.time()
    window, back = fast_console(link)
    link.request(f"BEGIN {len(image)} {digest}", timeout=15)   # erases the slot
    send_lines(link, "DATA", image, window,
               None if quiet else lambda n: print(f"\r  {n}/{len(image)} bytes", end="", flush=True))
    if not quiet:
        print()
    installed = link.request("END", timeout=10)
    console_back(link, back)
    if not quiet:
        print("installed:", installed)
        print(f"upload took {time.time() - t0:.2f}s")
    return installed


# ---------------------------------------------------------------- flick test

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
    serial = import_serial()
    for attempt in range(3):
        try:
            with open_port(port, baud) as s:
                return send_image(Link(s, echo=False), image, quiet=True)
        except (serial.SerialException, OSError):
            # The running sketch restarted or went to sleep (the selftest does).
            print("  the board went away during the upload; trying again")
            time.sleep(4)
    sys.exit(f"error: could not upload {proj.name} to {port}")


def cmd_test(args):
    """Bring-up test: selftest (with a deep sleep), crash rollback, watchdog rollback, while(1) stop."""
    port = find_port(args.port)
    baud = console_baud(port, args.baud)
    fields = info_fields(board_request(port, baud, "INFO"))
    target = LEGACY_TARGETS.get(fields.get("target") or fields.get("chip"), fields.get("target") or fields.get("chip"))
    print(f"testing {target} on {port}: Baseline {fields.get('baseline')} ABI {fields.get('abi')}")
    board_request(port, baud, "CRASHCLEAR")
    rows = []           # (check, PASS/FAIL/SKIP/LOOK, detail)

    print("1/4 selftest (about a minute, including a 2 s deep sleep)...")
    for attempt in range(2):
        upload_dir(port, baud, TESTS / "selftest", target)
        text = capture_console(port, baud, "RESULT:", 180)
        # Drop anything the previous sketch printed while the upload ran (seen:
        # a selftest left running from an earlier test, its WiFi scan cut short).
        start = text.find("--- selftest")
        text = text[start:] if start >= 0 else text
        found =[m.groups() for m in re.finditer(r"^[ \t]*(PASS|FAIL|SKIP|LOOK)  (.*?)\s*$", text, re.M)]
        result = re.search(r"RESULT: (\d+) passed, (\d+) failed", text)
        # The selftest's own count covers both phases. Fewer PASS/FAIL lines
        # than that means the capture missed console output (seen once on a
        # CH340 right after an upload), so run the selftest again.
        seen = sum(1 for status, _ in found if status in ("PASS", "FAIL"))
        if not result or seen >= int(result.group(1)) + int(result.group(2)):
            break
        print(f"  saw {seen} of {int(result.group(1)) + int(result.group(2))} results on the console; running the selftest again")
    rows += [(name, status, "") for status, name in found]
    if not result:
        rows.append(("selftest finished", "FAIL", "no RESULT line within 3 minutes"))
        print(text[-2000:])

    print("2/4 crash rollback...")
    upload_dir(port, baud, TESTS / "crash_test", target)
    time.sleep(4)
    f = info_fields(board_request(port, baud, "INFO"))
    rows.append(("crash in setup() rolls back to selftest", "PASS" if f.get("sketch") == "selftest" else "FAIL",
                 f"running {f.get('sketch')}"))
    rows.append(("crash recorded (INFO crash=)", "PASS" if f.get("crash", '"-"') != '"-"' else "FAIL",
                 f.get("crash", "").strip('"')[:80]))

    print("3/4 watchdog rollback (about 15 s)...")
    board_request(port, baud, "CRASHCLEAR")
    upload_dir(port, baud, TESTS / "hang_test", target)
    time.sleep(12)
    f = info_fields(board_request(port, baud, "INFO"))
    rows.append(("hung loop() caught by the watchdog, rolls back", "PASS" if f.get("sketch") == "selftest" else "FAIL",
                 f"running {f.get('sketch')}, reset={f.get('reset')}"))
    board_request(port, baud, "CRASHCLEAR")

    # The classic `while (1);` is a stop, not a hang: parked, no crash, no rollback.
    name = "while(1) stops the sketch (no crash, no rollback)"
    if not load_target(target).get("stop_detect"):
        rows.append((name, "SKIP", "not in this chip's Baseline yet"))
    else:
        print("4/4 while(1) stop (about 15 s)...")
        upload_dir(port, baud, TESTS / "stop_test", target)
        time.sleep(12)      # past the 10 s trial
        f = info_fields(board_request(port, baud, "INFO"))
        ok = f.get("sketch") == "stop_test" and f.get("status", "").strip('"').startswith("stopped")             and f.get("crash", '"-"') == '"-"'
        rows.append((name, "PASS" if ok else "FAIL",
                     f"running {f.get('sketch')}, status {f.get('status')}, crash {f.get('crash', '')[:40]}"))

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
        time.sleep(0.05)


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
    # A native USB board that just restarted (after an upload) is gone from
    # USB for a moment: wait for its port instead of failing.
    serial = import_serial()
    deadline = time.time() + 5
    while True:
        try:
            baud = console_baud(port, args.baud)
            s = open_port(port, baud)
            break
        except serial.SerialException:
            if time.time() > deadline:
                sys.exit(f"error: can't open {port}: is the board plugged in, and no other program using it?")
            time.sleep(0.25)
    monitor(s, port, baud, follow=not args.port)


# ---------------------------------------------------------------- baseline firmware

# Thousands of intermediate files: keep them out of synced folders (Dropbox locks them).
BUILD_ROOT = Path(os.environ.get("LOCALAPPDATA") or tempfile.gettempdir()) / "Flick"


def baseline_build_path(target_name):
    return BUILD_ROOT / target_name / "build"


def with_extensions(target, dirs):
    """The target with Baseline extensions (flick baseline ... --ext DIR,
    docs/EXTENSIONS.md): folders of extra Baseline source a project keeps in
    its own repository. Its build and files are named <target>-<ext>, so the
    stock Baseline's are left alone; the board still reports the target's name."""
    exts = []
    for d in dirs or ():
        d = Path(d).resolve()
        if not d.is_dir():
            sys.exit(f"error: --ext {d}: no such folder")
        if not re.fullmatch(r"[a-z0-9_]+", d.name):
            sys.exit(f"error: --ext {d}: the folder name is the extension's name: lowercase letters, digits and _ only")
        if not any(f.suffix in (".c", ".cpp") for f in d.rglob("*")):
            sys.exit(f"error: --ext {d}: no .c or .cpp files")
        exts.append(d)
    target = dict(target, extensions=exts)
    target["build_name"] = "-".join([target["name"], *(d.name for d in exts)])
    return target


def build_name(target):
    return target.get("build_name", target["name"])


def extension_files(target, under):
    """{staged path: file} for each extension's sources, below `under`."""
    return {under / d.name / f.relative_to(d): f for d in target.get("extensions", ())
            for f in d.rglob("*") if f.is_file()}


def stage_baseline(target, with_ext=True):
    """Assemble one Baseline source folder per target: the shared Baseline
    source, then chips/<chip>/baseline (bl_port.h, bl_port_mmu.c, ...), then
    targets/<target>/baseline if it exists, later files winning; plus the
    chip's partition table for the target's flash size as partitions.csv, and
    (with_ext) the extensions' sources in src/ext/<name>/.
    Copies keep their timestamps, so the build recompiles only what changed."""
    dst = BUILD_ROOT / build_name(target) / "baseline"
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
    if with_ext:
        wanted.update(extension_files(target, Path("src") / "ext"))
    return sync_files(dst, wanted)


def sync_files(dst, wanted):
    """Make folder dst hold exactly wanted ({relative path: source file}).
    Copies keep their timestamps, so a build recompiles only what changed."""
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


def teensy_bootloader_port(serial_port=None):
    """The Teensy to flash, as Teensyduino's uploader names it (usb:...), from
    arduino-cli: the one behind serial_port (-p COMx) if given; else the only
    Teensy, or the only one waiting in a bootloader (a new board, or its button
    pressed: no serial port)."""
    out = subprocess.run([find_arduino_cli(), "board", "list", "--format", "json"],
                         capture_output=True, text=True).stdout
    found = [d["port"] for d in json.loads(out or "{}").get("detected_ports", [])
             if d.get("port", {}).get("protocol") == "teensy"]
    if serial_port:
        hits = [p for p in found if (p.get("label") or "").split(" ")[0] == serial_port]
        if hits:
            return hits[0]["address"]
    if len(found) == 1:
        return found[0]["address"]
    waiting = [p for p in found if not (p.get("label") or "").startswith("COM") and "/dev/" not in (p.get("label") or "")]
    if len(waiting) == 1:
        return waiting[0]["address"]
    names = ", ".join(f"{p['address']} ({p.get('label', '')})" for p in found)
    sys.exit(f"error: {len(found)} Teensys on USB ({names}): pick one with -p COMx, or unplug the others")


def id_bytes(path):
    """A file's bytes for a Baseline build ID (INFO build=), with CRLF read as
    LF: a Windows checkout and CI's Linux one give the same ID."""
    return path.read_bytes().replace(b"\r\n", b"\n")


def id_sorted(items):
    """(relative path, file) pairs in the order a build ID hashes them: by the
    path's text, case and all. Path objects sort without case on Windows only."""
    return sorted(items, key=lambda item: item[0].as_posix())


def build_baseline_teensy(target, flash, serial_port=None):
    """Baseline for the i.MX RT1062 (Teensy 4.x): an Arduino sketch built with
    Teensyduino through arduino-cli. Staged from chips/<chip>/baseline plus the
    generated API table; the image is a .hex for Teensy's bootloader (HalfKay)."""
    gen = ROOT / "baseline" / "src" / "gen"
    wanted = {Path(f.name): f for f in (ROOT / target["baseline_source"]).glob("*") if f.is_file()}
    for name in ("api_table.c", "bl_impl.h", "bl_impl_weak.inc"):
        wanted[Path(name)] = gen / name
    wanted[Path("bl_ext.c")] = ROOT / "baseline" / "src" / "bl_ext.c"
    wanted.update(extension_files(target, Path("src") / "ext"))
    # INFO build= (the download page skips flashing a board that already runs
    # this image): a hash of everything the build reads from Flick, as on RP2.
    h = hashlib.sha256()
    inputs = id_sorted(wanted.items()) + id_sorted(
        (f.relative_to(d), f) for d in (SDK / "include", *target["sdk_dirs"]) for f in d.rglob("*") if f.is_file())
    for rel, src in inputs:
        h.update(rel.as_posix().encode() + b"\0" + id_bytes(src))
    h.update(target["fqbn"].encode() + b"\0-DTEENSY_INIT_USB_DELAY_AFTER=0")
    h.update(id_bytes(target["chip_dir"] / "chip.json"))
    id_header = BUILD_ROOT / build_name(target) / "bl_build_id.h"
    text = f'#define BL_BUILD_ID "{h.hexdigest()[:16]}"\n'
    if not id_header.exists() or id_header.read_text() != text:
        id_header.parent.mkdir(parents=True, exist_ok=True)
        id_header.write_text(text)
    wanted[Path("bl_build_id.h")] = id_header
    sketch = sync_files(BUILD_ROOT / build_name(target) / "baseline", wanted)
    build_path = baseline_build_path(build_name(target))
    # Quoted: arduino-cli splits the expanded recipe on spaces (a user folder like "Jane Doe").
    # The staging folder too: arduino-cli does not copy .inc files into its build folder.
    inc = " ".join(f'"-I{p}"' for p in (SDK / "include", *target["sdk_dirs"], sketch))
    # The linker script gets the target's addresses (bl_target.h, bl_board.h) from the preprocessor.
    ld = build_path.parent / "imxrt1062_flick.ld"
    build_path.mkdir(parents=True, exist_ok=True)
    run([find_gcc(target), "-E", "-P", "-x", "c", "-DBL_LINKER_SCRIPT",
         *[a for d in (SDK / "include", *target["sdk_dirs"]) for a in ("-I", str(d))],
         str(sketch / "imxrt1062_flick.ld.in"), "-o", str(ld)])
    # An empty sketchbook: only Teensyduino's own libraries go into Baseline, never
    # whatever the user installed (another SdFat breaks Teensy's SD library).
    user_dir = BUILD_ROOT / "teensy-sketchbook"
    (user_dir / "libraries").mkdir(parents=True, exist_ok=True)
    os.environ["ARDUINO_DIRECTORIES_USER"] = str(user_dir)
    cmd = [find_arduino_cli(), "compile", "--fqbn", target["fqbn"], "--build-path", str(build_path),
           "--build-property", f"build.flags.defs=-D__IMXRT1062__ -DTEENSYDUINO=160 -DTEENSY_INIT_USB_DELAY_AFTER=0 {inc}",
           "--build-property", f'build.flags.ld=-Wl,--gc-sections,--relax,--no-warn-rwx-segments "-T{ld}"',
           "--warnings", "default", str(sketch)]
    run(cmd)
    hexfile = build_path / "baseline.ino.hex"
    out = DIST / build_name(target) / f"baseline-{build_name(target)}.hex"
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(hexfile, out)
    print(f"Baseline image: {out.relative_to(WORK)} (Intel hex for Teensy's bootloader)")
    if flash:
        run([find_arduino_cli(), "upload", "--fqbn", target["fqbn"], "--input-dir", str(build_path),
             "-p", teensy_bootloader_port(serial_port), "-l", "teensy", str(sketch)])


def ca_bundle_header(pem, out_dir):
    """bl_ca_bundle.h from a PEM bundle: the certificates as DER, each after its
    2-byte big-endian length, for Baseline's flash trust store. Rewritten only
    when the PEM changes, so arduino-cli's build cache holds."""
    ders = [base64.b64decode("".join(m.group(1).split()))
            for m in re.finditer(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", pem.read_text(), re.S)]
    blob = b"".join(len(d).to_bytes(2, "big") + d for d in ders)
    lines = [f"/* {len(ders)} CA certificates from {pem.name} (Mozilla's list), as DER. Made by flick. */",
             "#pragma once", "#include <stdint.h>", f"#define BL_CA_BUNDLE_COUNT {len(ders)}",
             "static const uint8_t bl_ca_bundle[] = {"]
    for i in range(0, len(blob), 24):
        lines.append("    " + ",".join(str(b) for b in blob[i:i + 24]) + ",")
    lines.append("};\n")
    text = "\n".join(lines)
    out = out_dir / "bl_ca_bundle.h"
    out_dir.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_text() != text:
        out.write_text(text)
    return out


def build_baseline_rp2(target, flash, port=None):
    """Baseline for the RP2350 / RP2040: an Arduino sketch built with arduino-pico
    through arduino-cli. Staged from chips/<chip>/baseline plus the generated API
    table. The target's bl_board.h decides the flash layout and the chip's
    bl_target.h where the heap ends (below the sketch RAM window); they reach
    arduino-pico's linker script as the build properties its Flash Size menu
    would set. The image is a .uf2 for the boot ROM's USB drive."""
    gen = ROOT / "baseline" / "src" / "gen"
    wanted = {Path(f.name): f for f in (ROOT / target["baseline_source"]).glob("*") if f.is_file()}
    for name in ("api_table.c", "bl_impl.h", "bl_impl_weak.inc"):
        wanted[Path(name)] = gen / name
    wanted[Path("bl_ext.c")] = ROOT / "baseline" / "src" / "bl_ext.c"
    wanted.update(extension_files(target, Path("src") / "ext"))
    if target.get("ca_bundle"):
        wanted[Path("bl_ca_bundle.h")] = ca_bundle_header(ROOT / target["ca_bundle"], BUILD_ROOT / "rp2-ca")
    build_path = baseline_build_path(build_name(target))
    board = header_defines(target["dir"] / "sdk" / "bl_board.h")
    chip = header_defines(target["chip_dir"] / "sdk" / "bl_target.h")
    ram_base = 0x20000000
    props = [
        f"build.flash_total={board['BL_BOARD_FLASH_SIZE']}",
        f"build.flash_length={board['BL_BOARD_BASELINE_MAX']}",
        f"upload.maximum_size={board['BL_BOARD_BASELINE_MAX']}",
        f"build.fs_start={board['BL_BOARD_FS_START']}",
        f"build.fs_end={board['BL_BOARD_FS_END']}",
        f"build.eeprom_start={board['BL_BOARD_EEPROM_START']}",
        # The heap ends where Baseline's kept 4 KB start, below the sketch RAM window.
        f"build.ram_length={(chip['BL_RP2_KEEP_BASE'] - ram_base) // 1024}k",
    ]
    # INFO build= (the download page skips flashing a board that already runs
    # this image): a hash of everything the build reads from Flick, so the same
    # sources give the same ID. chip.json names the core and compiler versions.
    h = hashlib.sha256()
    inputs = id_sorted(wanted.items()) + id_sorted(
        (f.relative_to(d), f) for d in (SDK / "include", *target["sdk_dirs"]) for f in d.rglob("*") if f.is_file())
    for rel, src in inputs:
        h.update(rel.as_posix().encode() + b"\0" + id_bytes(src))
    h.update("\0".join([target["fqbn"], *props]).encode())
    h.update(id_bytes(target["chip_dir"] / "chip.json"))
    id_header = BUILD_ROOT / build_name(target) / "bl_build_id.h"
    text = f'#define BL_BUILD_ID "{h.hexdigest()[:16]}"\n'
    if not id_header.exists() or id_header.read_text() != text:
        id_header.parent.mkdir(parents=True, exist_ok=True)
        id_header.write_text(text)
    wanted[Path("bl_build_id.h")] = id_header
    sketch = sync_files(BUILD_ROOT / build_name(target) / "baseline", wanted)
    # Quoted: arduino-cli splits the expanded recipe on spaces (a user folder like "Jane Doe").
    # The staging folder too: arduino-cli does not copy .inc files into its build folder.
    inc = " ".join(f'"-I{p}"' for p in (SDK / "include", *target["sdk_dirs"], sketch))
    # An empty sketchbook: only arduino-pico's own libraries go into Baseline, never
    # whatever the user installed (another SdFat or WiFi library would win).
    user_dir = BUILD_ROOT / "rp2-sketchbook"
    (user_dir / "libraries").mkdir(parents=True, exist_ok=True)
    os.environ["ARDUINO_DIRECTORIES_USER"] = str(user_dir)
    cmd = [find_arduino_cli(), "compile", "--fqbn", target["fqbn"], "--build-path", str(build_path),
           "--build-property", f"compiler.c.extra_flags={inc}",
           "--build-property", f"compiler.cpp.extra_flags={inc}"]
    for prop in props:
        cmd += ["--build-property", prop]
    run(cmd + ["--warnings", "default", str(sketch)])
    uf2 = build_path / "baseline.ino.uf2"
    out = DIST / build_name(target) / f"baseline-{build_name(target)}.uf2"
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(uf2, out)
    print(f"Baseline image: {out.relative_to(WORK)} (UF2: drop it on the board's USB drive in BOOTSEL mode)")
    version = header_defines(ROOT / target["baseline_source"] / "bl_rp2.h")["BASELINE_VERSION"]
    bsu = make_bsu(target, build_path / "baseline.ino.bin", DIST / build_name(target) / f"baseline-{build_name(target)}.bsu",
                   version, build_path / "baseline.ino.elf")
    print(f"Baseline update: {bsu.relative_to(WORK)} ({bsu.stat().st_size} bytes, for flick baseline update)")
    if flash:
        # arduino-pico's uploader opens the port at 1200 baud: its USB serial (Baseline's
        # console) then reboots into the boot ROM, and the .uf2 is copied to its drive.
        run([find_arduino_cli(), "upload", "--fqbn", target["fqbn"], "--input-dir", str(build_path),
             "-p", find_port(port), str(sketch)])


def find_arduino_cli():
    env = os.environ.get("FLICK_ARDUINO_CLI")
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
    sys.exit("error: arduino-cli not found. Set FLICK_ARDUINO_CLI or install Arduino IDE 2 / arduino-cli.")


def arduino_data_dirs():
    """Arduino data directories that may hold the esp32 core."""
    dirs = []
    if os.environ.get("ARDUINO_DIRECTORIES_DATA"):
        dirs.append(Path(os.environ["ARDUINO_DIRECTORIES_DATA"]))
    if os.environ.get("LOCALAPPDATA"):
        dirs.append(Path(os.environ["LOCALAPPDATA"]) / "Arduino15")
    dirs += [Path.home() / ".arduino15", Path.home() / "Library" / "Arduino15"]
    cfgs = [Path.home() / ".arduinoIDE" / "arduino-cli.yaml"]
    if os.environ.get("LOCALAPPDATA"):
        cfgs.append(Path(os.environ["LOCALAPPDATA"]) / "Arduino15" / "arduino-cli.yaml")
    for cfg in cfgs:
        if cfg.exists():
            for line in cfg.read_text().splitlines():
                if line.strip().startswith("data:"):
                    dirs.append(Path(line.split(":", 1)[1].strip()))
    return dirs


def esp32_cores():
    """(version tuple, data dir) of every installed esp32 Arduino core."""
    found = []
    for d in arduino_data_dirs():
        for v in glob.glob(str(d / "packages" / "esp32" / "hardware" / "esp32" / "*")):
            try:
                found.append((tuple(int(x) for x in Path(v).name.split(".")), d))
            except ValueError:
                continue
    return found


def newest_esp32_core():
    """(version tuple, data dir) of the newest installed esp32 Arduino core."""
    return max(esp32_cores(), key=lambda c: c[0], default=None)


def vstr(v):
    return ".".join(map(str, v))


def esp32_core_for(target):
    """The Arduino-ESP32 core to build target's Baseline with: the chip's
    "core_version" if it is installed, else the newest one, with a warning.
    A Baseline built with another core usually works; it just isn't what the
    release was built and tested with."""
    cores = esp32_cores()
    if not cores:
        sys.exit("error: no Arduino-ESP32 core found. Install it with the Arduino IDE Boards Manager.")
    want = target.get("core_version")
    exact = [c for c in cores if want and vstr(c[0]) == want]
    if exact:
        return exact[0]
    core = max(cores, key=lambda c: c[0])
    if want:
        print(f"warning: {target['chip']} Baselines are built with Arduino-ESP32 {want}, which is not "
              f"installed; building with {vstr(core[0])} instead (installed: "
              f"{', '.join(sorted({vstr(c[0]) for c in cores}))})")
    return core


# ---------------------------------------------------------------- Baseline via ESP-IDF
# Targets with "builder": "idf" build Baseline as an ESP-IDF project (idf/) with
# Arduino-ESP32 as a component, so Baseline can change the sdkconfig.

def idf_version_of(path):
    """'5.5.5' for an ESP-IDF checkout, or None if path isn't one."""
    f = Path(path) / "tools" / "cmake" / "version.cmake"
    if not f.exists():
        return None
    v = dict(re.findall(r"set\(IDF_VERSION_(MAJOR|MINOR|PATCH) (\d+)\)", f.read_text()))
    return f"{v.get('MAJOR')}.{v.get('MINOR')}.{v.get('PATCH')}"


def find_idf(version):
    """(IDF_PATH, IDF_TOOLS_PATH) of the ESP-IDF checkout to build with: version
    (e.g. "5.5.5") if it is installed, else the closest one (same major.minor
    first, then the newest), with a warning. $FLICK_IDF_PATH always wins."""
    tools = Path(os.environ.get("IDF_TOOLS_PATH") or
                 ("C:/Espressif" if os.name == "nt" else Path.home() / ".espressif"))
    forced = os.environ.get("FLICK_IDF_PATH")
    if forced:
        found = idf_version_of(forced)
        if not found:
            sys.exit(f"error: FLICK_IDF_PATH={forced} is not an ESP-IDF checkout")
        if found != version:
            print(f"warning: Baselines are built with ESP-IDF v{version}; FLICK_IDF_PATH is v{found}")
        return Path(forced), tools
    paths = [Path(os.environ["IDF_PATH"])] if os.environ.get("IDF_PATH") else []
    for root in (tools / "frameworks", Path.home() / "esp"):
        paths += sorted(root.glob("esp-idf-v*"), reverse=True)
    found = [(p, v) for p, v in ((p, idf_version_of(p)) for p in paths) if v]
    if not found:
        sys.exit(f"error: ESP-IDF not found. Install v{version} (for example in "
                 f"{tools / 'frameworks'}) or set FLICK_IDF_PATH.")
    key = lambda v: tuple(int(x) for x in v.split("."))
    same_minor = [pv for pv in found if key(pv[1])[:2] == key(version)[:2]]
    exact = [pv for pv in found if pv[1] == version]
    path, found_v = (exact or [max(same_minor or found, key=lambda pv: key(pv[1]))])[0]
    if found_v != version:
        print(f"warning: Baselines are built with ESP-IDF v{version}, which is not installed; "
              f"building with v{found_v} ({path}) instead")
    return path, tools


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


def staged_updates(target):
    """4 MB boards have one Baseline slot: Baseline updates are staged and the
    bootloader installs them (8/16 MB boards have two slots)."""
    return target["flash_size"] == "4MB"


def stage_idf(target, core_dir, libs_dir):
    """Assemble the IDF project for a target (layout: idf/CMakeLists.txt)."""
    proj = BUILD_ROOT / build_name(target) / "idf"
    idf_src = ROOT / "idf"
    proj.mkdir(parents=True, exist_ok=True)
    shutil.copy2(idf_src / "CMakeLists.txt", proj / "CMakeLists.txt")

    # main/: the same files the Arduino build stages, plus idf/main/.
    main = proj / "main"
    before = sorted(main.rglob("*")) if main.exists() else []
    sync_tree(stage_baseline(target, with_ext=False), main)
    (main / "baseline.ino").replace(main / "baseline_main.cpp")
    for f in (idf_src / "main").iterdir():
        shutil.copy2(f, main / f.name)
    if sorted(main.rglob("*")) != before:
        (main / "CMakeLists.txt").touch()     # new or removed sources: make CMake glob again

    # components/bl_ext_<name>: the extensions (docs/EXTENSIONS.md), with
    # idf/ext_component.cmake as their CMakeLists.txt unless they bring one.
    comps = proj / "components"
    for d in target.get("extensions", ()):
        comp = comps / f"bl_ext_{d.name}"
        sync_tree(d, comp)
        if not (d / "CMakeLists.txt").exists():
            shutil.copy2(idf_src / "ext_component.cmake", comp / "CMakeLists.txt")
    if comps.exists():
        for comp in comps.glob("bl_ext_*"):
            if comp.name[len("bl_ext_"):] not in {d.name for d in target.get("extensions", ())}:
                shutil.rmtree(comp)

    # bootloader_components/bsu_stage: on 4 MB boards (one Baseline slot) the
    # bootloader installs staged Baseline updates (baseline/src/bl_update.cpp).
    boot = proj / "bootloader_components"
    if staged_updates(target):
        sync_tree(idf_src / "bootloader_components", boot)
        for f in ("bsu_inflate.c", "bsu_inflate.h", "bsu_format.h"):
            shutil.copy2(ROOT / "baseline" / "src" / "bsu" / f, boot / "bsu_stage" / f)
    elif boot.exists():
        shutil.rmtree(boot)

    # components/arduino: the installed core (no examples), with a trimmed manifest.
    arduino = proj / "components" / "arduino"
    for part in ("cores", "libraries", "variants"):
        sync_tree(core_dir / part, arduino / part, skip={"examples"})
    for f in ("CMakeLists.txt", "Kconfig.projbuild"):
        shutil.copy2(core_dir / f, arduino / f)
    # A target can add components ("idf_dependencies": {"espressif/esp_hosted":
    # "^2.9.2"}), e.g. the WiFi co-processor driver of a P4 board.
    manifest = (idf_src / "arduino_component.yml").read_text()
    for comp, version in target.get("idf_dependencies", {}).items():
        manifest += f'  {comp}:\n    version: "{version}"\n    require: public\n'
    if not (arduino / "idf_component.yml").exists() or (arduino / "idf_component.yml").read_text() != manifest:
        (arduino / "idf_component.yml").write_text(manifest)

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
    out = [f"# Generated by flick from Arduino's sdkconfig: the options lib{name}.a was built with.",
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
              f"-DFLICK_DEFINES={';'.join(target.get('idf_defines', []) + (['BL_UPDATE_STAGED_BOOTLOADER=1'] if staged_updates(target) else []))}"]
    if not (proj / "sdkconfig").exists():
        shutil.rmtree(proj / "build", ignore_errors=True)   # starting over: set-target won't clean a half-made build
        run(idf_py + [f"-DIDF_TARGET={target['idf_target']}", f"-DFLICK_INCLUDE_DIRS={inc}", "set-target",
                      target["idf_target"]], env=env)
    run(idf_py + [f"-DFLICK_INCLUDE_DIRS={inc}", "build"], env=env)

    # Baseline's code and rodata are mapped from the bottom of the flash address
    # range; they must end below the sketch windows (bl_target.h).
    b = proj / "build"
    elf = (b / "baseline.elf").read_bytes()
    for sym, base, what in (("_instruction_reserved_end", target["ibus"], "code"),
                            ("_rodata_reserved_end", target["dbus"], "rodata")):
        end = elf_symbol(elf, sym)
        if end and end[0] > base:
            sys.exit(f"error: Baseline's {what} ends at 0x{end[0]:08X}, inside the sketch window at 0x{base:08X}")

    # One blob at 0x0: bootloader + partition table + Baseline.
    args = json.loads((b / "flasher_args.json").read_text())
    out = DIST / build_name(target) / f"baseline-{build_name(target)}.bin"
    out.parent.mkdir(parents=True, exist_ok=True)
    parts = []
    for off, f in sorted(args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        parts += [off, str(b / f)]
    run([str(py), "-m", "esptool", "--chip", target["idf_target"], "merge_bin", "-o", str(out)] + parts, env=env)
    print(f"Baseline blob: {out.relative_to(WORK)} ({out.stat().st_size:,} bytes; flash at 0x0)")
    bsu = make_bsu(target, b / args["app"]["file"], DIST / build_name(target) / f"baseline-{build_name(target)}.bsu")
    print(f"Baseline update: {bsu.relative_to(WORK)} ({bsu.stat().st_size:,} bytes; flick baseline update, "
          "or ota_pull_baseline)")
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


# ---------------------------------------------------------------- setup: the compiler

# The compiler sketches are built with: Espressif's GCC, the release ESP-IDF
# 5.5 and Arduino-ESP32 3.3 use (so sketches and Baseline match).
TOOLCHAIN_RELEASE = "esp-14.2.0_20260121"
TOOLCHAIN_URL = "https://github.com/espressif/crosstool-NG/releases/download/{rel}/{name}-14.2.0_20260121-{host}.{ext}"


def toolchain_host():
    import platform
    m = platform.machine().lower()
    arm = m in ("arm64", "aarch64")
    if sys.platform == "win32":
        return "x86_64-w64-mingw32", "zip"
    if sys.platform == "darwin":
        return ("aarch64-apple-darwin" if arm else "x86_64-apple-darwin"), "tar.xz"
    return ("aarch64-linux-gnu" if arm else "x86_64-linux-gnu"), "tar.xz"


def download(url, dest):
    import urllib.request
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "flick"}), timeout=60) as r:
        total = int(r.headers.get("Content-Length") or 0)
        got, shown = 0, -1
        with open(tmp, "wb") as f:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
                got += len(chunk)
                if total and got * 20 // total != shown:
                    shown = got * 20 // total
                    print(f"\r  {got >> 20} / {total >> 20} MB", end="", flush=True)
    print()
    tmp.replace(dest)


def board_target(port=None):
    """The target the plugged-in board's Baseline reports, or None (no board, or no Baseline)."""
    try:
        port = find_port(port)
        with open_port(port, console_baud(port, None)) as conn:
            r = Link(conn, echo=False).request_status("INFO", timeout=2)
        board = info_fields(r[1]).get("target") if r and r[0] else None
    except (SystemExit, OSError):
        return None
    return LEGACY_TARGETS.get(board, board)


def zstd_reader(path):
    """A .zst file as a stream: Python 3.14's compression.zstd, else the zstandard
    package (installed if missing)."""
    try:
        from compression import zstd
        return zstd.open(path, "rb")
    except ImportError:
        pass
    try:
        import zstandard
    except ImportError:
        print("installing zstandard (to unpack Teensyduino's .tar.zst archives)")
        run([sys.executable, "-m", "pip", "install", "zstandard"])
        import zstandard
    return zstandard.ZstdDecompressor().stream_reader(open(path, "rb"))


def unpack(url, dest, strip=None, sha256=None):
    """Download an archive (kept in ~/.flick/downloads until unpacked) and unpack it
    into dest. strip: only the members under this folder, without it."""
    archive = FLICK_HOME / "downloads" / url.rsplit("/", 1)[1]
    if not archive.exists():
        print(f"downloading {archive.name}")
        download(url, archive)
    if sha256:
        import hashlib
        h = hashlib.sha256()
        with open(archive, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        if h.hexdigest() != sha256.lower():
            archive.unlink()
            sys.exit(f"error: {archive.name} is damaged (SHA-256 differs): run setup again")
    print(f"unpacking into {dest}")
    if archive.name.endswith(".zip"):
        import zipfile
        with zipfile.ZipFile(archive) as z:
            z.extractall(dest)
    else:
        import tarfile
        # "tar" filter: keep the toolchain's symlinks, refuse paths outside dest
        safe = {"filter": "tar"} if hasattr(tarfile, "tar_filter") else {}
        import contextlib
        zst = archive.name.endswith(".zst")
        with (zstd_reader(archive) if zst else contextlib.nullcontext()) as stream, \
                (tarfile.open(fileobj=stream, mode="r|") if zst else tarfile.open(archive)) as t:
            if strip is None:
                t.extractall(dest, **safe)
            else:
                prefix = strip.rstrip("/") + "/"
                for m in t:
                    if m.name.startswith(prefix):
                        m.name = m.name[len(prefix):]
                        if m.islnk() and m.linkname.startswith(prefix):   # hard links name the full path
                            m.linkname = m.linkname[len(prefix):]
                        t.extract(m, dest, **safe)
    archive.unlink()


def cmd_setup(args):
    """Download the compiler for the targets' chips (Espressif's Xtensa or RISC-V GCC for the
    ESP32 family; arduino-pico's for the RP2040/RP2350; Teensyduino's compiler, core and
    libraries for the Teensy 4.x). Without -t: for the plugged-in board, else for every
    chip. Installs pyserial if missing."""
    try:
        import serial  # noqa: F401
    except ImportError:
        print("installing pyserial (to talk to the board)")
        run([sys.executable, "-m", "pip", "install", "pyserial>=3.5"])
    names = args.target
    if not names:
        board = board_target()
        if board in target_names():
            print(f"board: {board}")
            names = [board]
        else:
            print("no board with Baseline answered, so installing the compilers for every chip "
                  "(or plug in the board and run setup again)")
            names = target_names()
    targets = [load_target(t) for t in names]
    host, ext = toolchain_host()
    # Espressif's GCC (the ESP32 family)
    esp = sorted({"xtensa-esp-elf" if t["gcc"].startswith("xtensa") else "riscv32-esp-elf"
                  for t in targets if t["gcc"].startswith(("xtensa", "riscv32-esp"))})
    dest = TOOLS_HOME / TOOLCHAIN_RELEASE
    for name in esp:
        if (dest / name / "bin").is_dir():
            print(f"{name}: already installed in {dest / name}")
            continue
        unpack(TOOLCHAIN_URL.format(rel=TOOLCHAIN_RELEASE, name=name, host=host, ext=ext), dest)
    # An Arduino core's own GCC (chip.json "gcc_download"), unless that core is installed
    for t in {t["chip"]: t for t in targets if t.get("gcc_download")}.values():
        dl = t["gcc_download"]
        if package_gcc(t) or (TOOLS_HOME / dl["release"]).is_dir():
            print(f"{t['gcc']} for {t['chip']}: already installed")
            continue
        if host not in dl["files"]:
            sys.exit(f"error: no {t['gcc']} download for this computer ({host}). Install the Arduino core "
                     f"for {t['chip']} with the Arduino IDE instead.")
        unpack(dl["url"].format(file=dl["files"][host]), TOOLS_HOME / dl["release"])
    # Teensyduino's compiler, core and libraries (chip.json "teensy_download"), unless installed
    for t in {t["chip"]: t for t in targets if t.get("teensy_download")}.values():
        exe = t["gcc"] + (".exe" if os.name == "nt" else "")
        if teensy_gcc(t, exe, quiet=True):
            print(f"Teensyduino for {t['chip']}: already installed")
            continue
        dl = t["teensy_download"]
        if host not in dl["compiler"]["files"]:
            sys.exit(f"error: no Teensyduino download for this computer ({host}). Install Teensyduino "
                     "with the Arduino IDE's Boards Manager instead.")
        root = TOOLS_HOME / dl["root"]
        comp = dl["compiler"]
        f = comp["files"][host]
        unpack(f["url"], root / "tools" / "teensy-compile" / comp["version"], strip="tools", sha256=f["sha256"])
        for sub in root.glob("tools/teensy-compile/*/*"):
            if sub.name != "arm":       # the archive also holds an AVR compiler Teensy 4 doesn't use
                shutil.rmtree(sub, ignore_errors=True)
        plat = dl["platform"]
        unpack(plat["url"], root / "hardware" / "avr" / plat["version"], strip="avr", sha256=plat["sha256"])
    for t in targets:
        try:
            print(f"  {t['name']}: {find_gcc(t)}")
        except SystemExit as e:     # a compiler setup can't download (Teensyduino's)
            print(f"  {t['name']}: {e}")
    print(f"done: try  {FLICK_CMD} upload examples/01_basics/01_hello_flick -m")


# ---------------------------------------------------------------- libraries (Arduino Library Manager)

LIBRARY_INDEX = "https://downloads.arduino.cc/libraries/library_index.json.gz"


def library_index():
    """The Library Manager's index, cached for a day."""
    import gzip
    f = FLICK_HOME / "library_index.json.gz"
    if not f.exists() or time.time() - f.stat().st_mtime > 86400:
        print("downloading the Arduino library index")
        download(LIBRARY_INDEX, f)
    return json.loads(gzip.decompress(f.read_bytes()))["libraries"]


def version_key(v):
    return [int(x) if x.isdigit() else 0 for x in re.split(r"[.\-+]", v)]


def cmd_lib(args):
    libs = library_index()
    if args.action == "search":
        words = [w.lower() for w in args.names]
        latest = {}
        for lib in libs:
            text = " ".join((lib["name"], lib.get("sentence", ""), lib.get("author", ""))).lower()
            if all(w in text for w in words):
                if lib["name"] not in latest or version_key(lib["version"]) > version_key(latest[lib["name"]]["version"]):
                    latest[lib["name"]] = lib
        for lib in sorted(latest.values(), key=lambda l: l["name"].lower())[:40]:
            print(f'{lib["name"]} ({lib["version"]}): {lib.get("sentence", "")[:90]}')
        if not latest:
            print("nothing found")
        return
    import zipfile
    dest_root = WORK / "libraries"
    for spec in args.names:
        name, _, want = spec.partition("@")
        found = [l for l in libs if l["name"].lower() == name.lower() and (not want or l["version"] == want)]
        if not found:
            sys.exit(f"error: no library named '{name}'" + (f" with version {want}" if want else "") +
                     f". Try: {FLICK_CMD} lib search <words>")
        lib = max(found, key=lambda l: version_key(l["version"]))
        archive = FLICK_HOME / "downloads" / lib["archiveFileName"]
        print(f'installing {lib["name"]} {lib["version"]}')
        download(lib["url"], archive)
        folder = dest_root / re.sub(r"[^\w.-]", "_", lib["name"])
        if folder.exists():
            shutil.rmtree(folder)
        with zipfile.ZipFile(archive) as z:
            top = {n.split("/", 1)[0] for n in z.namelist()}
            z.extractall(dest_root / ".unpack")
        unpacked = dest_root / ".unpack" / (top.pop() if len(top) == 1 else "")
        shutil.move(str(unpacked), str(folder))
        shutil.rmtree(dest_root / ".unpack", ignore_errors=True)
        archive.unlink()
        print(f"  -> {folder.relative_to(WORK)}")
        deps = [d["name"] for d in lib.get("dependencies", [])]
        if deps:
            print(f"  it depends on: {', '.join(deps)} (install those too if a build asks for them)")


# ---------------------------------------------------------------- the sketch kit

# The kit is this repository without what only Baseline's developers need:
# the top level as it is, and the part of flick/ that builds and uploads sketches.
KIT_FILES = {
    "": ["README.md", "flick.py", "AGENTS.md", "CLAUDE.md", "LICENSE",
         "requirements.txt", ".vscode/tasks.json", "examples/**/*", "my_sketches/**/*"],
    "flick/": ["tools/flick.py", "tools/tests/**/*",
               "sdk/include/**/*", "sdk/src/**/*", "sdk/ld/**/*",
               "chips/*/chip.json", "chips/*/sdk/**/*",
               "targets/*/target.json", "targets/*/sdk/**/*",
               "docs/API.md", "docs/ARDUINO_COMPAT.md", "docs/OTA.md", "docs/BOARD_COMPARISON.md",
               "THIRD_PARTY_NOTICES.md"],
}
KIT_GITIGNORE = """# flick build output, downloaded libraries and secrets
build/
libraries/
secrets.cfg
__pycache__/
"""


def cmd_package(args):
    """The sketch kit: everything needed to write, build and upload sketches
    (no Baseline source, no ESP-IDF)."""
    import zipfile
    version = header_defines(ROOT / "baseline" / "src" / "bl_main.h")["BASELINE_VERSION"]
    abi = header_defines(SDK / "include" / "baseline_api.h")
    out = Path(args.out) if args.out else DIST / f"flick-kit-{version}.zip"
    out.parent.mkdir(parents=True, exist_ok=True)
    files = {}
    for dest, patterns in KIT_FILES.items():
        base = WORK / dest
        for pattern in patterns:
            for f in base.glob(pattern):
                rel = f.relative_to(base)
                if f.is_file() and not {"build", "__pycache__"}.intersection(rel.parts):
                    files[dest + rel.as_posix()] = f
    top = f"flick-kit-{version}"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for rel, f in sorted(files.items()):
            z.write(f, f"{top}/{rel}")
        z.writestr(f"{top}/.gitignore", KIT_GITIGNORE)
    print(f"kit: {out.relative_to(WORK) if out.is_relative_to(WORK) else out} ({len(files) + 1} files, "
          f"{out.stat().st_size // 1024} KB; Baseline {version}, ABI {abi.get('BL_ABI_MAJOR')}.{abi.get('BL_ABI_MINOR')})")


def flash_file(args):
    """Flash a downloaded Baseline image (baseline-<target>.bin, at 0x0) with esptool."""
    port = leave_usb_device_mode(find_port(args.port), args.port)
    image = Path(args.file)
    board = board_target(port)
    if board and f"baseline-{board}." not in image.name:
        sys.exit(f"error: the board runs the {board} Baseline; {image.name} looks like it is for another board "
                 "(flash the matching baseline-<target>.bin)")
    try:
        import esptool  # noqa: F401
    except ImportError:
        sys.exit(f"error: flashing needs esptool: pip install esptool (or flash it from the web page: {WEB_PAGE})")
    print(f"flashing {image.name} to {port}")
    run([sys.executable, "-m", "esptool", "-p", port, "-b", "921600", "write_flash", "0x0", str(image)])


# ---------------------------------------------------------------- Baseline updates over the air

# .bsu: the Baseline update package (baseline/src/bsu/bsu_format.h): a 128-byte
# header, then the app image compressed with raw deflate. On RP2040/RP2350 the
# compressed part is a gzip stream instead (header byte 5 = 1), the format
# arduino-pico's OTA stub inflates (chips/rp2350/baseline/bl_rp2_update.cpp),
# after a 16-byte extension: "RPX1", then the offset and size of the image's
# Bluetooth bond store (arduino-pico's __bluetooth_tlv, which BTstack rewrites
# at run time, so the installed image is checked without it), 0 and 0 if none.
BSU_MAGIC = 0x31555342
BSU_HEADER = struct.Struct("<IBBHII32s32s32s16s")
BSU_COMP_GZIP = 1
BSU_RP_EXT = struct.Struct("<4sIII")


def make_bsu(target, app_bin, out, version=None, elf=None):
    raw = Path(app_bin).read_bytes()
    ext = b""
    if target.get("builder") == "rp2":
        kind = BSU_COMP_GZIP
        comp = gzip.compress(raw, 9, mtime=0)
        tlv = elf_symbol(Path(elf).read_bytes(), "_ZL15__bluetooth_tlv") if elf else None
        tlv_off, tlv_size = (tlv[0] - 0x10000000, tlv[1]) if tlv else (0, 0)
        if tlv_size and raw[tlv_off:tlv_off + tlv_size].strip(b"\0"):
            sys.exit("error: the image's Bluetooth bond store is not all zeros")
        ext = BSU_RP_EXT.pack(b"RPX1", tlv_off, tlv_size, 0)
    else:
        kind = 0
        z = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
        comp = z.compress(raw) + z.flush()
    if version is None:
        version = header_defines(ROOT / "baseline" / "src" / "bl_main.h")["BASELINE_VERSION"]
    hdr = BSU_HEADER.pack(BSU_MAGIC, 1, kind, target["chip_id"], len(raw), len(comp),
                          hashlib.sha256(raw).digest(), hashlib.sha256(comp).digest(),
                          target["name"].encode().ljust(32, b"\0"), version.encode()[:16].ljust(16, b"\0"))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(hdr + ext + comp)
    return out


def parse_bsu(pkg):
    """The .bsu header as a dict, after checking it and the compressed data's SHA-256."""
    if len(pkg) < BSU_HEADER.size:
        sys.exit("error: not a Baseline update (.bsu): too short")
    magic, ver, kind, chip, raw_size, comp_size, raw_sha, comp_sha, target, version = BSU_HEADER.unpack_from(pkg)
    if magic != BSU_MAGIC or ver != 1:
        sys.exit("error: not a Baseline update (.bsu)")
    comp = pkg[BSU_HEADER.size + (BSU_RP_EXT.size if kind == BSU_COMP_GZIP else 0):]
    if len(comp) != comp_size or hashlib.sha256(comp).digest() != comp_sha:
        sys.exit("error: the .bsu file is damaged")
    return {"chip_id": chip, "raw_size": raw_size, "raw_sha": raw_sha, "comp": comp,
            "target": target.rstrip(b"\0").decode(), "version": version.rstrip(b"\0").decode()}


def reconnect(args, timeout=90):
    """Open the link again after the board restarts (USB port or WiFi host)."""
    deadline = time.time() + timeout
    time.sleep(3)
    while time.time() < deadline:
        try:
            if getattr(args, "host", None):
                conn = NetPort(args.host, args.password or os.environ.get("FLICK_OTA_PASSWORD"))
            else:
                port = find_port(args.port)
                _baud_cache.pop(port, None)
                conn = open_port(port, console_baud(port, None))
            link = Link(conn, echo=False)
            if link.request_status("INFO", timeout=3):
                return conn, link
            conn.close()
        except (SystemExit, OSError):
            pass
        time.sleep(2)
    sys.exit("error: the board did not come back after restarting")


def send_update(link, kind, data):
    """UBEGIN/UDATA/UEND. Returns None when stored, or the ERR text of UBEGIN."""
    r = link.request_status(f"UBEGIN {kind} {len(data)} {hashlib.sha256(data).hexdigest()}", timeout=60)
    if r is None:
        sys.exit("error: no reply to UBEGIN")
    if not r[0]:
        return r[1]
    t0 = time.time()
    window, back = fast_console(link)
    shown = [-1]

    def progress(n):
        pct = n * 100 // len(data)
        if pct // 10 != shown[0]:
            shown[0] = pct // 10
            print(f"\r  {pct}%", end="", flush=True)
    send_lines(link, "UDATA", data, window, progress)
    print()
    print(link.request("UEND", timeout=60))
    console_back(link, back)
    print(f"sent in {time.time() - t0:.1f}s")
    return None


def cmd_baseline_update(args, target):
    """Update Baseline over USB or WiFi (--host) without esptool."""
    if args.file:
        bsu = Path(args.file)
    else:
        print(f"target {target['name']}: {target['description']}")
        if target.get("builder") == "rp2":
            build_baseline_rp2(target, False)
        elif target.get("builder") == "idf":
            build_baseline_idf(target, esp32_core_for(target))
        else:
            sys.exit(f"error: {target['name']} updates Baseline over USB only (flick baseline flash)")
        bsu = DIST / build_name(target) / f"baseline-{build_name(target)}.bsu"
    pkg = bsu.read_bytes()
    h = parse_bsu(pkg)
    port, conn = open_link_port(args)
    link = Link(conn, echo=False)
    fields = info_fields(link.request("INFO"))
    board = fields.get("target") or fields.get("chip")
    if board != h["target"]:
        sys.exit(f"error: {bsu.name} is for {h['target']}; the board on {port} runs {board}")
    mode = fields.get("update", "none")
    print(f"{port}: Baseline {fields.get('baseline')} in {fields.get('bpart', '?')}, update mode {mode}; "
          f"sending Baseline {h['version']} ({bsu.name})")
    if mode == "dual":
        kind, data = "app", zlib.decompress(h["comp"], -15)
        if hashlib.sha256(data).digest() != h["raw_sha"]:
            sys.exit("error: the .bsu file is damaged")
    elif mode == "staged":
        kind, data = "bsu", pkg
        print("note: on this board the update erases the sketch and LittleFS")
    elif mode == "file":
        # RP2040/RP2350: stored in LittleFS, installed by arduino-pico's OTA stub at
        # the restart; the sketch, its settings and its files are kept.
        kind, data = "bsu", pkg
    else:
        sys.exit("error: this Baseline cannot be updated over the air (flash it once over USB: flick baseline flash)")
    for _ in range(2):
        err = send_update(link, kind, data)
        if err is None:
            break
        if "update mode" not in err:
            sys.exit(f"error from Baseline: {err}")
        print("the board restarts into update mode (the sketch stops); reconnecting...")
        conn.close()
        conn, link = reconnect(args)
    else:
        sys.exit("error: the board did not take the update")
    conn.close()
    if mode == "staged" and getattr(args, "host", None):
        # The update erased the sketch, so nothing will bring WiFi up again.
        print("the bootloader installs the update now (about 30 s); the sketch was erased, so the board is "
              "not on WiFi any more: upload a sketch over USB (or use ota_pull_baseline with a sketch URL)")
        return
    print("waiting for the board to restart" + (" (the bootloader installs the update)" if mode == "staged" else
                                                 " (the OTA stub installs the update)" if mode == "file" else ""))
    conn, link = reconnect(args, timeout=120)
    with conn:
        info = info_fields(link.request("INFO"))
    print(f"now: Baseline {info.get('baseline')} in {info.get('bpart', '?')}, sketch {info.get('sketch')} "
          f"({info.get('status')})")


def cmd_baseline(args):
    if args.action == "flash" and args.file:
        return flash_file(args)
    if args.action == "update" and args.file:
        return cmd_baseline_update(args, None)
    if args.action == "update" and not args.target:
        _, conn = open_link_port(args)      # the target the board runs
        with conn:
            args.target = info_fields(Link(conn, echo=False).request("INFO")).get("target")
    target = load_target(args.target or DEFAULT_TARGET)
    if args.ext:
        target = with_extensions(target, args.ext)
    if args.action == "update":
        return cmd_baseline_update(args, target)
    print(f"target {target['name']}: {target['description']}")
    if target.get("builder") == "teensy":
        return build_baseline_teensy(target, args.action == "flash", getattr(args, "port", None))
    if target.get("builder") == "rp2":
        return build_baseline_rp2(target, args.action == "flash", args.port)
    core = esp32_core_for(target)
    if target.get("builder") == "idf":
        proj, env, py, fargs = build_baseline_idf(target, core)
        if args.action == "flash":
            flash_baseline_idf(target, proj, env, py, fargs, leave_usb_device_mode(find_port(args.port), args.port))
        return
    print(f"using Arduino-ESP32 {'.'.join(map(str, core[0]))} from {core[1]}")
    os.environ["ARDUINO_DIRECTORIES_DATA"] = str(core[1])
    sketch = stage_baseline(target)
    build_path = baseline_build_path(build_name(target))
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
        out = DIST / build_name(target) / f"baseline-{build_name(target)}.bin"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(blob)
        print(f"Baseline blob: {out.relative_to(WORK)} ({len(blob):,} bytes; flash at 0x0)")


# ---------------------------------------------------------------- main

def main():
    # Board output can hold bytes the terminal cannot show (on the classic ESP32,
    # boot lines at 115200 reach a Baseline 0.1/0.2 console at 921600).
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    ap = argparse.ArgumentParser(prog="flick", description=__doc__,
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
    p.add_argument("-o", "--out", help="also copy the .flick image here (a file or a folder), "
                   "for example to put on a web server for BaselineUpdate")
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
            p.add_argument("--password", help="OTA password (or set FLICK_OTA_PASSWORD)")
        p.add_argument("-p", "--port", help="serial port (default: the plugged-in board)")
        p.add_argument("-b", "--baud", type=int, help="console baud rate (default: found automatically)")
        p.set_defaults(fn=fn)

    p = sub.add_parser("test", help="bring-up test: selftest, crash and watchdog rollback")
    p.add_argument("-p", "--port", help="serial port (default: the plugged-in board)")
    p.add_argument("-b", "--baud", type=int, help="console baud rate (default: found automatically)")
    p.set_defaults(fn=cmd_test)

    p = sub.add_parser("baseline", help="build, flash (USB, esptool) or update (USB or WiFi) Baseline")
    p.add_argument("action", choices=["build", "flash", "update"])
    p.add_argument("-t", "--target", help=targets_help + "; default " + DEFAULT_TARGET)
    p.add_argument("-p", "--port")
    p.add_argument("--host", help="update: over WiFi to NAME[.local][:port] (the sketch must call ArduinoOTA.begin())")
    p.add_argument("--password", help="update: OTA password (or set FLICK_OTA_PASSWORD)")
    p.add_argument("--file", help="flash: a downloaded baseline-<target>.bin; update: a .bsu file "
                                  "(default: build it for --target)")
    p.add_argument("-b", "--baud", type=int, help="console baud rate (default: found automatically)")
    p.add_argument("--ext", action="append", metavar="DIR",
                   help="build in a Baseline extension: a folder of extra Baseline source (repeatable; "
                        "docs/EXTENSIONS.md)")
    p.set_defaults(fn=cmd_baseline)

    p = sub.add_parser("setup", help="download the compiler sketches need (once)")
    p.add_argument("-t", "--target", action="append",
                   help="for this target (repeatable); default: the plugged-in board, else all")
    p.set_defaults(fn=cmd_setup)

    p = sub.add_parser("wifi", help="save the WiFi network sketches join (WIFI_SSID, WIFI_PASSWORD in ~/.flick/secrets.cfg)")
    p.add_argument("ssid", help="network name")
    p.add_argument("password", help="password (quote it if it has spaces)")
    p.set_defaults(fn=cmd_wifi)

    p = sub.add_parser("secrets", help="show or create the secrets file (~/.flick/secrets.cfg); --set KEY=value")
    p.add_argument("--set", metavar="KEY=value", help="add or change one secret")
    p.set_defaults(fn=cmd_secrets)

    p = sub.add_parser("lib", help="search or install Arduino libraries (Library Manager)")
    p.add_argument("action", choices=["search", "install"])
    p.add_argument("names", nargs="+", help="search: words; install: NAME or NAME@VERSION (quote names with spaces)")
    p.set_defaults(fn=cmd_lib)

    p = sub.add_parser("package", help="make the sketch kit zip for users")
    p.add_argument("-o", "--out", help="zip file (default dist/flick-kit-<version>.zip)")
    p.set_defaults(fn=cmd_package)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
