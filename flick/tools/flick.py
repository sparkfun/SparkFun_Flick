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
    if gcc_name.startswith("arm-"):
        # Teensyduino's compiler (Arduino IDE or arduino-cli Boards Manager)
        for r in [*roots, *arduino_data_dirs()]:
            hits = sorted(glob.glob(str(r / "packages" / "teensy" / "tools" / "teensy-compile" / "*" / "arm" / "bin" / exe)))
            if hits:
                return hits[-1]
        sys.exit(f"error: {gcc_name} not found. Install Teensyduino (Arduino IDE Boards Manager: Teensy).")
    sys.exit(f"error: {gcc_name} not found. Run: {FLICK_CMD} setup")


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
    `name = ...`, `stack = <bytes>` and `target = <target>`."""
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


def ino_with_prototypes(src):
    """The .ino's text with prototypes added, or None if it needs none. A
    function defined inside #if/#ifdef gets its prototype inside the same
    conditions, so two versions of one function (#ifdef USE_SPI ... #else ...)
    declare only the one that is compiled, as with the Arduino IDE."""
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
        proto = " ".join(sig.split()) + ";"
        groups = conds[text.count("\n", 0, m.start(2))]
        if groups:
            proto = "\n".join([*(d for g in groups for d in g), proto, *["#endif"] * len(groups)])
        protos.append(proto)
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


def library_roots(proj):
    roots = [proj / "libraries", WORK / "libraries"]       # the sketch's own, then flick lib install's
    roots += [Path(p) for p in os.environ.get("FLICK_LIBRARIES", "").split(os.pathsep) if p]
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
    (r"WiFi\w*|HTTPClient|WebServer|DNSServer|MDNS|PubSubClient|Network\w*|ArduinoOTA|configT(z)?ime|WL_\w+",
     "WiFi", "BL_HAS_WIFI"),
    (r"touch\w+|T[0-9]+", "capacitive touch", "BL_HAS_TOUCH"),
    (r"dacWrite", "a DAC", "BL_HAS_DAC"),
    (r"USB\w*", "USB device mode in this Baseline", "BL_HAS_USB_DEVICE"),
    (r"esp_\w+|ESP_\w+|ESP|EspClass|ledc\w+|ps_(m|c|re)alloc|psramFound|temperatureRead|analogReadMilliVolts|"
     r"analogSet(Pin)?Attenuation|timer(Begin|End|Start|Stop|Alarm|AttachInterrupt\w*)|"
     r"hw_timer_t|I2SClass|SD_MMC|getLocalTime|getCpuFrequencyMhz|log_[ewidv]|gpio_num_t|GPIO_NUM_\w+",
     "an ESP32 API", "BL_PLATFORM_ESP32"),
    (r"rgbLedWrite|neopixelWrite|FastLED|CRGB|CHSV", "a WS2812 API (ESP32 and RP2040/RP2350 boards)",
     ("BL_PLATFORM_ESP32", "BL_PLATFORM_RP2")),
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
    libs = resolve_libraries(proj, user, base_dirs)
    inc = []
    for d in base_dirs + [d for lib in libs for d in lib_src_dirs(lib)]:
        inc += ["-I", str(d)]
    if target.get("builder") == "teensy":
        # Last on the path, after the SDK's own headers: Teensy's register definitions (imxrt.h).
        inc += ["-idirafter", str(teensy_core_dir(gcc))]
    defs = [f'-DBL_SKETCH_NAME="{cfg["label"]}"',f"-DBL_SKETCH_STACK={int(cfg['stack'])}", *secret_defines()]

    def compile_one(src, obj, extra_defs=(), quiet=False):
        if src.suffix == ".ino":
            # Like Arduino: C++, with the core header included automatically.
            lang = ["-x", "c++", *CXXFLAGS, "-include", "baseline.h"]
        elif src.suffix == ".cpp":
            lang = CXXFLAGS
        else:
            lang = CFLAGS
        obj.parent.mkdir(parents=True, exist_ok=True)
        r = subprocess.run([gcc, *lang, *target["cflags"], *target.get("defines", PLATFORM_DEFINES), *inc,
                            *extra_defs, *(["-w"] if quiet else []), "-c", str(src), "-o", str(obj)],
                           capture_output=True, text=True, errors="replace")
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        if r.returncode != 0:
            for hint in missing_feature_hints(r.stdout + r.stderr, target, proj.name):
                print(hint, file=sys.stderr)
            sys.exit(r.returncode)

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
    shared = ["sketch_libc.c", "sketch_time.c", "sketch_ctype.c", *target["sdk_sources"]] + (
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
         *objs, "-Wl,--start-group", "-lstdc++", "-lm", "-lgcc", *target.get("libs", []), "-Wl,--end-group",
         "-o", str(elf)])

    image = make_image(elf.read_bytes(), target)
    flick = out / (cfg["name"] + ".flick")
    flick.write_bytes(image)
    sha = image[HDR_OFF_SHA256:HDR_OFF_SHA256 + 32].hex()
    print(f"built {cfg['name']}.flick for {target['name']}: {len(image)} bytes, sha256 {sha[:16]}..., "
          f"{time.time() - t0:.2f}s")
    return flick


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


def open_port(port, baud):
    serial = import_serial()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.1
    # Keep DTR/RTS released so opening the port does not reset the board
    # (CH340/CP210x wire them to EN/IO0; USB-Serial-JTAG resets on them too).
    # A TinyUSB serial port is the exception: Arduino's USBCDC discards output
    # until the host raises DTR, and DTR alone never resets the chip there.
    # arduino-pico's USB serial (RP2040/RP2350) does the same; it reboots into
    # the boot ROM only on 1200 baud, which flick never uses.
    s.dtr = is_tinyusb_port(port) or is_rp2_port(port)
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


def teensy_bootloader_port():
    """The Teensy's port as Teensyduino's uploader names it (usb:...), from arduino-cli."""
    out = subprocess.run([find_arduino_cli(), "board", "list", "--format", "json"],
                         capture_output=True, text=True).stdout
    ports = [d["port"]["address"] for d in json.loads(out or "{}").get("detected_ports", [])
             if d.get("port", {}).get("protocol") == "teensy"]
    if len(ports) != 1:
        sys.exit(f"error: expected one Teensy on USB, found {len(ports)}")
    return ports[0]


def build_baseline_teensy(target, flash):
    """Baseline for the i.MX RT1062 (Teensy 4.x): an Arduino sketch built with
    Teensyduino through arduino-cli. Staged from chips/<chip>/baseline plus the
    generated API table; the image is a .hex for Teensy's bootloader (HalfKay)."""
    gen = ROOT / "baseline" / "src" / "gen"
    wanted = {Path(f.name): f for f in (ROOT / target["baseline_source"]).glob("*") if f.is_file()}
    for name in ("api_table.c", "bl_impl.h", "bl_impl_weak.inc"):
        wanted[Path(name)] = gen / name
    sketch = sync_files(BUILD_ROOT / target["name"] / "baseline", wanted)
    build_path = baseline_build_path(target["name"])
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
           "--build-property", f"build.flags.defs=-D__IMXRT1062__ -DTEENSYDUINO=160 {inc}",
           "--build-property", f'build.flags.ld=-Wl,--gc-sections,--relax,--no-warn-rwx-segments "-T{ld}"',
           "--warnings", "default", str(sketch)]
    run(cmd)
    hexfile = build_path / "baseline.ino.hex"
    out = DIST / target["name"] / f"baseline-{target['name']}.hex"
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(hexfile, out)
    print(f"Baseline image: {out.relative_to(WORK)} (Intel hex for Teensy's bootloader)")
    if flash:
        run([find_arduino_cli(), "upload", "--fqbn", target["fqbn"], "--input-dir", str(build_path),
             "-p", teensy_bootloader_port(), "-l", "teensy", str(sketch)])


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
    if target.get("ca_bundle"):
        wanted[Path("bl_ca_bundle.h")] = ca_bundle_header(ROOT / target["ca_bundle"], BUILD_ROOT / "rp2-ca")
    build_path = baseline_build_path(target["name"])
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
    inputs = sorted(wanted.items()) + sorted(
        (f.relative_to(d), f) for d in (SDK / "include", *target["sdk_dirs"]) for f in d.rglob("*") if f.is_file())
    for rel, src in inputs:
        h.update(str(rel).replace("\\", "/").encode() + b"\0" + src.read_bytes())
    h.update("\0".join([target["fqbn"], *props]).encode())
    h.update((target["chip_dir"] / "chip.json").read_bytes())
    id_header = BUILD_ROOT / target["name"] / "bl_build_id.h"
    text = f'#define BL_BUILD_ID "{h.hexdigest()[:16]}"\n'
    if not id_header.exists() or id_header.read_text() != text:
        id_header.parent.mkdir(parents=True, exist_ok=True)
        id_header.write_text(text)
    wanted[Path("bl_build_id.h")] = id_header
    sketch = sync_files(BUILD_ROOT / target["name"] / "baseline", wanted)
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
    out = DIST / target["name"] / f"baseline-{target['name']}.uf2"
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(uf2, out)
    print(f"Baseline image: {out.relative_to(WORK)} (UF2: drop it on the board's USB drive in BOOTSEL mode)")
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

    # One blob at 0x0: bootloader + partition table + Baseline.
    b = proj / "build"
    args = json.loads((b / "flasher_args.json").read_text())
    out = DIST / target["name"] / f"baseline-{target['name']}.bin"
    out.parent.mkdir(parents=True, exist_ok=True)
    parts = []
    for off, f in sorted(args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        parts += [off, str(b / f)]
    run([str(py), "-m", "esptool", "--chip", target["idf_target"], "merge_bin", "-o", str(out)] + parts, env=env)
    print(f"Baseline blob: {out.relative_to(WORK)} ({out.stat().st_size:,} bytes; flash at 0x0)")
    bsu = make_bsu(target, b / args["app"]["file"], DIST / target["name"] / f"baseline-{target['name']}.bsu")
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


def unpack(url, dest):
    """Download an archive (kept in ~/.flick/downloads until unpacked) and unpack it into dest."""
    archive = FLICK_HOME / "downloads" / url.rsplit("/", 1)[1]
    if not archive.exists():
        print(f"downloading {archive.name}")
        download(url, archive)
    print(f"unpacking into {dest}")
    if archive.name.endswith(".zip"):
        import zipfile
        with zipfile.ZipFile(archive) as z:
            z.extractall(dest)
    else:
        import tarfile
        with tarfile.open(archive) as t:
            # "tar" filter: keep the toolchain's symlinks, refuse paths outside dest
            t.extractall(dest, **({"filter": "tar"} if hasattr(tarfile, "tar_filter") else {}))
    archive.unlink()


def cmd_setup(args):
    """Download the compiler for the targets' chips (Espressif's Xtensa or RISC-V GCC for the
    ESP32 family; arduino-pico's for the RP2040/RP2350). Without -t: for the plugged-in
    board, else for every chip. Installs pyserial if missing."""
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
# header, then the app image compressed with raw deflate.
BSU_MAGIC = 0x31555342
BSU_HEADER = struct.Struct("<IBBHII32s32s32s16s")


def make_bsu(target, app_bin, out):
    raw = Path(app_bin).read_bytes()
    z = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    comp = z.compress(raw) + z.flush()
    version = header_defines(ROOT / "baseline" / "src" / "bl_main.h")["BASELINE_VERSION"]
    hdr = BSU_HEADER.pack(BSU_MAGIC, 1, 0, target["chip_id"], len(raw), len(comp),
                          hashlib.sha256(raw).digest(), hashlib.sha256(comp).digest(),
                          target["name"].encode().ljust(32, b"\0"), version.encode()[:16].ljust(16, b"\0"))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(hdr + comp)
    return out


def parse_bsu(pkg):
    """The .bsu header as a dict, after checking it and the compressed data's SHA-256."""
    if len(pkg) < BSU_HEADER.size:
        sys.exit("error: not a Baseline update (.bsu): too short")
    magic, ver, _, chip, raw_size, comp_size, raw_sha, comp_sha, target, version = BSU_HEADER.unpack_from(pkg)
    if magic != BSU_MAGIC or ver != 1:
        sys.exit("error: not a Baseline update (.bsu)")
    comp = pkg[BSU_HEADER.size:]
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
        build_baseline_idf(target, esp32_core_for(target))
        bsu = DIST / target["name"] / f"baseline-{target['name']}.bsu"
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
    if kind == "bsu" and getattr(args, "host", None):
        # The update erased the sketch, so nothing will bring WiFi up again.
        print("the bootloader installs the update now (about 30 s); the sketch was erased, so the board is "
              "not on WiFi any more: upload a sketch over USB (or use ota_pull_baseline with a sketch URL)")
        return
    print("waiting for the board to restart" + (" (the bootloader installs the update)" if kind == "bsu" else ""))
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
    if args.action == "update":
        return cmd_baseline_update(args, target)
    print(f"target {target['name']}: {target['description']}")
    if target.get("builder") == "teensy":
        return build_baseline_teensy(target, args.action == "flash")
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
