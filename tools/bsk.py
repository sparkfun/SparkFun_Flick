#!/usr/bin/env python3
"""bsk - Baseline Sketcher command-line tool.

  bsk targets                       list the targets (chips/boards) in targets/
  bsk new <dir>                     create a sketch project
  bsk build [dir] [-t TARGET]       compile + link + package -> <dir>/build/<target>/<name>.bsk
  bsk upload [dir] [-m]             ask the board which target it is, build for it, upload, run
  bsk upload [dir] --host NAME      same, over WiFi (the running sketch called ArduinoOTA.begin())
  bsk monitor                       serial console (Ctrl+C to quit)
  bsk info                          ask Baseline for its version / ABI / chip / active slot
  bsk safe                          reboot Baseline into safe mode (sketch not started)
  bsk baseline build -t TARGET      compile a Baseline blob -> dist/<target>/baseline-<target>.bin
  bsk baseline flash -t TARGET      compile and flash Baseline (rarely needed)

The build target is taken from -t, else `target = ...` in the sketch's
sketch.cfg, else esp32s3. `upload` always builds for the chip on the board.

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
DIST = ROOT / "dist"
DEFAULT_TARGET = "esp32s3"

# Must match sdk/include/sketch_abi.h (the target addresses come from bl_target.h)
HEADER_SIZE = 0x100
MAGIC = 0x4B534C42
HDR_OFF_IMAGE_SIZE = 12
HDR_OFF_BUILD_ID = 96
HDR_OFF_SHA256 = HEADER_SIZE - 32

COMMON_FLAGS = [
    "-mlongcalls", "-Os", "-g3",
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
    """targets/<name>/target.json plus the addresses from its bl_target.h."""
    d = TARGETS / name
    if not (d / "target.json").exists():
        sys.exit(f"error: unknown target '{name}'. Targets: {', '.join(target_names())}")
    t = json.loads((d / "target.json").read_text())
    t["name"] = name
    t["dir"] = d
    defs = header_defines(d / "sdk" / "bl_target.h")
    t["ibus"], t["dbus"] = defs["BL_SKETCH_IBUS_BASE"], defs["BL_SKETCH_DBUS_BASE"]
    t["max_size"] = defs["BL_SKETCH_MAX_SIZE"]
    return t


def cmd_targets(args):
    for name in target_names():
        t = load_target(name)
        print(f"{name:10} {t['description']}")


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
        hits = sorted(glob.glob(str(r / "packages" / "esp32" / "tools" / "esp-x32" / "*" / "bin" / exe)))
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

    tsdk = target["dir"] / "sdk"
    inc = ["-I", str(SDK / "include"), "-I", str(SDK / "include" / "compat"), "-I", str(tsdk), "-I", str(proj)]
    defs = [f'-DBL_SKETCH_NAME="{cfg["name"]}"', f"-DBL_SKETCH_STACK={int(cfg['stack'])}"]

    def compile_one(src, obj, extra_defs=()):
        if src.suffix == ".ino":
            # Like Arduino: C++, with the core header included automatically.
            lang = ["-x", "c++", *CXXFLAGS, "-include", "baseline.h"]
        elif src.suffix == ".cpp":
            lang = CXXFLAGS
        else:
            lang = CFLAGS
        run([gcc, *lang, *target["cflags"], *inc, *extra_defs, "-c", str(src), "-o", str(obj)])

    objs = []
    for src in user + [SDK / "src" / "sketch_crt.c"]:     # crt carries the sketch's name/stack
        obj = out / (src.name + ".o")
        compile_one(src, obj, defs)
        objs.append(str(obj))
    # The C library shim, C++ runtime and Arduino-style objects are the same for
    # every sketch: compile them once per target into sdk/build/<target> and reuse them.
    cache = SDK / "build" / target["name"]
    cache.mkdir(parents=True, exist_ok=True)
    newest_dep = max(p.stat().st_mtime for p in [*(SDK / "include").glob("*.h"), *tsdk.glob("*"),
                                                 target["dir"] / "target.json", Path(__file__)])
    shared = ["sketch_libc.c", "sketch_time.c", *target["sdk_sources"]] + (
        ["sketch_cxx.cpp", "sketch_arduino.cpp", "sketch_string.cpp", "sketch_espnow.cpp",
         "sketch_net.cpp", "sketch_io.cpp", "sketch_ble.cpp", "sketch_storage.cpp",
         "sketch_mqtt.cpp", "sketch_system.cpp", "sketch_fastled.cpp", "sketch_usb.cpp"] if cxx else [])
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
    run([gcc, "-mlongcalls", "-nostdlib", "-nostartfiles",
         "-T", str(ld), "-T", str(tsdk / "rom.ld"),
         "-Wl,--gc-sections", "-Wl,--no-undefined", "-Wl,-Map," + str(out / (cfg["name"] + ".map")),
         *objs, "-lgcc", "-o", str(elf)])

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
    with open_port(find_port(args.port), args.baud) as s:
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
        chip = info_fields(info).get("chip", DEFAULT_TARGET)   # the board decides the target
        if args.target and args.target != chip:
            sys.exit(f"error: the board runs the {chip} Baseline, not {args.target}")
        image = build(args.dir, chip).read_bytes()
        digest = hashlib.sha256(image).hexdigest()
        t0 = time.time()
        link.request(f"BEGIN {len(image)} {digest}", timeout=15)   # erases the slot
        for off in range(0, len(image), CHUNK):
            data = base64.b64encode(image[off:off + CHUNK]).decode()
            link.request(f"DATA {off} {data}")
            print(f"\r  {min(off + CHUNK, len(image))}/{len(image)} bytes", end="", flush=True)
        print()
        print("installed:", link.request("END", timeout=10))
        print(f"upload took {time.time() - t0:.2f}s")
        if args.monitor:
            monitor(s, port, args.baud, link, follow=not args.port)


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
    monitor(open_port(port, args.baud), port, args.baud, follow=not args.port)


# ---------------------------------------------------------------- baseline firmware

# Thousands of intermediate files: keep them out of synced folders (Dropbox locks them).
BUILD_ROOT = Path(os.environ.get("LOCALAPPDATA") or tempfile.gettempdir()) / "BaselineSketcher"


def baseline_build_path(target_name):
    return BUILD_ROOT / target_name / "build"


def stage_baseline(target):
    """Arduino compiles one sketch folder, so assemble one per target: the
    shared Baseline source plus the target's own files (partitions.csv,
    bl_port.h, bl_port_mmu.c), target files winning. Copies keep their
    timestamps, so arduino-cli recompiles only what changed."""
    dst = BUILD_ROOT / target["name"] / "baseline"
    wanted = {}
    for root in (ROOT / target["baseline_source"], target["dir"] / "baseline"):
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
    tdef = target["dir"] / "idf" / "sdkconfig.defaults"
    if tdef.exists():
        ours += "\n" + tdef.read_text()
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
    inc = ";".join(str(p).replace("\\", "/") for p in (SDK / "include", target["dir"] / "sdk"))
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
    inc = f'"-I{SDK / "include"}" "-I{target["dir"] / "sdk"}"'
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
        p.add_argument("-b", "--baud", type=int, default=115200)
        p.set_defaults(fn=fn)

    p = sub.add_parser("baseline", help="build or flash a Baseline blob")
    p.add_argument("action", choices=["build", "flash"])
    p.add_argument("-t", "--target", help=targets_help + "; default " + DEFAULT_TARGET)
    p.add_argument("-p", "--port")
    p.set_defaults(fn=cmd_baseline)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
