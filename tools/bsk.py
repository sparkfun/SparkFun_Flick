#!/usr/bin/env python3
"""bsk - Baseline Sketcher command-line tool (ESP32-S3).

  bsk new <dir>                     create a sketch project
  bsk build [dir]                   compile + link + package -> <dir>/build/<name>.bsk
  bsk upload [dir] [-m]             build, then send to Baseline's loader and run it
  bsk upload [dir] --host NAME      same, over WiFi (the running sketch called ArduinoOTA.begin())
  bsk monitor                       serial console (Ctrl+C to quit)
  bsk info                          ask Baseline for its version / ABI / active slot
  bsk safe                          reboot Baseline into safe mode (sketch not started)
  bsk baseline build                compile the Baseline firmware (arduino-cli)
  bsk baseline flash                compile and flash Baseline (rarely needed)

Commands that talk to the board find the plugged-in ESP32-S3 by its USB ID;
pass -p PORT to choose one yourself.

The toolchain is found in this order: $BSK_GCC, xtensa-esp32s3-elf-gcc on PATH,
then an Arduino-ESP32 install (%LOCALAPPDATA%/Arduino15 or ~/.arduino15).
arduino-cli: $BSK_ARDUINO_CLI, PATH, then the copy bundled with Arduino IDE 2.
"""
import argparse
import base64
import codecs
import glob
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK = ROOT / "sdk"
GCC_NAME = "xtensa-esp32s3-elf-gcc"

# Must match sdk/include/sketch_abi.h
IBUS_BASE = 0x43800000
DBUS_BASE = 0x3D800000
MAX_SIZE = 0x80000
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


# ---------------------------------------------------------------- toolchain

def find_gcc():
    env = os.environ.get("BSK_GCC")
    if env:
        return env
    on_path = shutil.which(GCC_NAME)
    if on_path:
        return on_path
    exe = GCC_NAME + (".exe" if os.name == "nt" else "")
    roots = []
    if os.environ.get("LOCALAPPDATA"):
        roots.append(Path(os.environ["LOCALAPPDATA"]) / "Arduino15")
    roots.append(Path.home() / ".arduino15")
    roots.append(Path.home() / "Library" / "Arduino15")
    for r in roots:
        hits = sorted(glob.glob(str(r / "packages" / "esp32" / "tools" / "esp-x32" / "*" / "bin" / exe)))
        if hits:
            return hits[-1]
    sys.exit(f"error: {GCC_NAME} not found. Set BSK_GCC or install ESP-IDF / Arduino-ESP32.")


def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        sys.exit(r.returncode)
    return r


# ---------------------------------------------------------------- project

SKETCH_TEMPLATE = """\
// {name}.ino - a Baseline Sketcher sketch. baseline.h is included automatically.

const int LED_PIN = 0;  // blue STAT LED on the SparkFun ESP32-S3 Thing Plus

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    bl_printf("Hello from {name} on Baseline %s\\n", bl_baseline_version());
}

void loop()
{
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
    delay(500);
}
"""


def load_config(proj):
    """Name defaults to the folder name. An optional sketch.cfg may set
    `name = ...` and `stack = <bytes>`."""
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


def build(proj):
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
    out = proj / "build"
    out.mkdir(exist_ok=True)
    gcc = find_gcc()
    t0 = time.time()

    inc = ["-I", str(SDK / "include"), "-I", str(SDK / "include" / "compat"), "-I", str(proj)]
    defs = [f'-DBL_SKETCH_NAME="{cfg["name"]}"', f"-DBL_SKETCH_STACK={int(cfg['stack'])}"]

    def compile_one(src, obj, extra_defs=()):
        if src.suffix == ".ino":
            # Like Arduino: C++, with the core header included automatically.
            lang = ["-x", "c++", *CXXFLAGS, "-include", "baseline.h"]
        elif src.suffix == ".cpp":
            lang = CXXFLAGS
        else:
            lang = CFLAGS
        run([gcc, *lang, *inc, *extra_defs, "-c", str(src), "-o", str(obj)])

    objs = []
    for src in user + [SDK / "src" / "sketch_crt.c"]:     # crt carries the sketch's name/stack
        obj = out / (src.name + ".o")
        compile_one(src, obj, defs)
        objs.append(str(obj))
    # The C library shim, C++ runtime and Arduino-style objects are the same for
    # every sketch: compile them once into sdk/build and reuse them.
    cache = SDK / "build"
    cache.mkdir(exist_ok=True)
    newest_dep = max(p.stat().st_mtime for p in [*(SDK / "include").glob("*.h"), Path(__file__)])
    shared = ["sketch_libc.c"] + (["sketch_cxx.cpp", "sketch_arduino.cpp", "sketch_string.cpp", "sketch_espnow.cpp",
                                  "sketch_net.cpp", "sketch_io.cpp", "sketch_ble.cpp", "sketch_storage.cpp",
                                  "sketch_mqtt.cpp"] if cxx else [])
    for name in shared:
        src = SDK / "src" / name
        obj = cache / (name + ".o")
        if not obj.exists() or obj.stat().st_mtime < max(newest_dep, src.stat().st_mtime):
            compile_one(src, obj)
        objs.append(str(obj))

    ld = out / "sketch.ld"
    run([gcc, "-E", "-P", "-x", "c", "-DBL_LINKER_SCRIPT", *inc,
         str(SDK / "ld" / "sketch_esp32s3.ld.in"), "-o", str(ld)])

    elf = out / (cfg["name"] + ".elf")
    run([gcc, "-mlongcalls", "-nostdlib", "-nostartfiles",
         "-T", str(ld), "-T", str(SDK / "ld" / "rom_esp32s3.ld"),
         "-Wl,--gc-sections", "-Wl,--no-undefined", "-Wl,-Map," + str(out / (cfg["name"] + ".map")),
         *objs, "-lgcc", "-o", str(elf)])

    image = make_image(elf.read_bytes())
    bsk = out / (cfg["name"] + ".bsk")
    bsk.write_bytes(image)
    sha = image[HDR_OFF_SHA256:HDR_OFF_SHA256 + 32].hex()
    print(f"built {bsk.relative_to(proj.parent)}: {len(image)} bytes, sha256 {sha[:16]}..., "
          f"{time.time() - t0:.2f}s")
    return bsk


def make_image(elf):
    """Lay the ELF's loadable segments out by their load address in the sketch
    window, then patch image_size, build_id and sha256 into the header."""
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
    build(args.dir)


# ---------------------------------------------------------------- serial

ESPRESSIF_VID = 0x303A      # ESP32-S3 native USB (USB-Serial-JTAG)


def import_serial():
    try:
        import serial
        import serial.tools.list_ports  # noqa: F401
    except ImportError:
        sys.exit("error: pyserial is required: pip install pyserial")
    return serial


def find_port(port=None):
    """The given port, or the single Espressif USB device that is plugged in."""
    if port:
        return port
    serial = import_serial()
    found = [p for p in serial.tools.list_ports.comports() if p.vid == ESPRESSIF_VID]
    if len(found) == 1:
        return found[0].device
    if not found:
        sys.exit("error: no ESP32-S3 found on USB. Plug in the board, or pass -p PORT.")
    names = ", ".join(f"{p.device} ({p.description})" for p in found)
    sys.exit(f"error: several ESP32 boards found: {names}. Pass -p PORT to pick one.")


def open_port(port, baud):
    serial = import_serial()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.1
    # Keep DTR/RTS released so opening the port does not reset the board.
    s.dtr = False
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


def cmd_upload(args):
    bsk = build(args.dir)
    image = bsk.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if args.host and args.monitor:
        sys.exit("error: --monitor needs the USB port; it cannot be combined with --host")
    port, s = open_link_port(args)
    with s:
        link = Link(s)
        t0 = time.time()
        print(f"Baseline on {port}:", link.request("INFO"))
        link.request(f"BEGIN {len(image)} {digest}", timeout=15)   # erases the slot
        for off in range(0, len(image), CHUNK):
            data = base64.b64encode(image[off:off + CHUNK]).decode()
            link.request(f"DATA {off} {data}")
            print(f"\r  {min(off + CHUNK, len(image))}/{len(image)} bytes", end="", flush=True)
        print()
        print("installed:", link.request("END", timeout=10))
        print(f"upload took {time.time() - t0:.2f}s")
        if args.monitor:
            monitor(s, port, args.baud, link)


def reopen(port, baud):
    """Wait for the port to come back (the board re-enumerates when it reboots)."""
    serial = import_serial()
    while True:
        try:
            return open_port(port, baud)
        except (serial.SerialException, OSError):
            time.sleep(0.3)


def monitor(s, port, baud, link=None):
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
                s = reopen(port, baud)
                print("--- reconnected ---", flush=True)
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
    monitor(open_port(port, args.baud), port, args.baud)


# ---------------------------------------------------------------- baseline firmware

BASELINE_DIR = ROOT / "baseline"
# Thousands of intermediate files: keep them out of synced folders (Dropbox locks them).
BASELINE_BUILD = Path(os.environ.get("LOCALAPPDATA") or tempfile.gettempdir()) / "BaselineSketcher" / "baseline-build"
BASELINE_FQBN = ("esp32:esp32:sparkfun_esp32s3_thing_plus:"
                 "USBMode=default,CDCOnBoot=cdc,PartitionScheme=custom,PSRAM=enabled")


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


MIN_CORE = (3, 3, 0)    # Baseline is verified against Arduino-ESP32 3.3.x (ESP-IDF 5.5)


def cmd_baseline(args):
    core = newest_esp32_core()
    if not core or core[0] < MIN_CORE:
        found = ".".join(map(str, core[0])) if core else "none"
        sys.exit(f"error: Baseline needs Arduino-ESP32 core >= 3.3.0 (found {found}). "
                 "Install it with the Arduino IDE Boards Manager.")
    print(f"using Arduino-ESP32 {'.'.join(map(str, core[0]))} from {core[1]}")
    os.environ["ARDUINO_DIRECTORIES_DATA"] = str(core[1])
    inc = f'-I{SDK / "include"}'
    cmd = [find_arduino_cli(), "compile", "--fqbn", BASELINE_FQBN,
           "--build-path", str(BASELINE_BUILD),
           "--build-property", f"compiler.c.extra_flags={inc}",
           "--build-property", f"compiler.cpp.extra_flags={inc}",
           "--warnings", "default"]
    if args.action == "flash":
        cmd += ["--upload", "-p", find_port(args.port)]
    cmd.append(str(BASELINE_DIR))
    run(cmd)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(prog="bsk", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("new", help="create a sketch project")
    p.add_argument("dir")
    p.set_defaults(fn=cmd_new)

    p = sub.add_parser("build", help="build a sketch")
    p.add_argument("dir", nargs="?", default=".")
    p.set_defaults(fn=cmd_build)

    for name, fn, hlp in (("upload", cmd_upload, "build and upload a sketch"),
                          ("monitor", cmd_monitor, "serial console"),
                          ("info", cmd_info, "show Baseline info"),
                          ("safe", cmd_safe, "reboot into safe mode")):
        p = sub.add_parser(name, help=hlp)
        if name == "upload":
            p.add_argument("dir", nargs="?", default=".")
            p.add_argument("-m", "--monitor", action="store_true", help="open the monitor after upload")
        if name in ("upload", "info"):
            p.add_argument("--host", help="upload over WiFi to NAME[.local][:port] (sketch must call ArduinoOTA.begin())")
            p.add_argument("--password", help="OTA password (or set BSK_OTA_PASSWORD)")
        p.add_argument("-p", "--port", help="serial port (default: the plugged-in ESP32-S3)")
        p.add_argument("-b", "--baud", type=int, default=115200)
        p.set_defaults(fn=fn)

    p = sub.add_parser("baseline", help="build or flash the Baseline firmware")
    p.add_argument("action", choices=["build", "flash"])
    p.add_argument("-p", "--port")
    p.set_defaults(fn=cmd_baseline)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
