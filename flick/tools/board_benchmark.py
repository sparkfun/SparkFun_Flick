#!/usr/bin/env python3
"""Edit-to-running times, Arduino IDE versus Flick, for docs/BOARD_COMPARISON.md.

  python flick/tools/board_benchmark.py --board classic=COM77 --board native=COM25 --board teensy=COM11
  python flick/tools/board_benchmark.py --table-only        rewrite the table from the saved results

One sketch, benchmarks/oled_dashboard (SparkFun's Qwiic OLED library), built
unchanged by both. For each board group, on one board of that group:

  Flick     the board runs its Baseline. Compile: `flick build` after an edit
            (the sketch's .ino touched). Edit to running: `flick upload` after an
            edit (build, upload, the board restarts into the sketch); upload is
            that minus the compile. Medians of RUNS.
  Arduino   arduino-cli with the board's usual FQBN. Compile: after an edit, with
            the core already cached (the everyday rebuild, not the first build).
            Upload: `arduino-cli upload` (esptool / Teensy Loader). This replaces
            Baseline, so the script flashes Baseline back afterwards.

Results are kept in docs/board_benchmark.json (one entry per group, measured on
different days if need be); the table in BOARD_COMPARISON.md is rewritten from it.
Arduino-ESP32 should be the core Baseline is built with: --esp32-data points
arduino-cli at the Arduino data folder that has it (Arduino IDE 2:
%LOCALAPPDATA%\\Arduino15); Teensyduino comes from arduino-cli's own config.
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flick  # noqa: E402

ROOT = flick.ROOT
SKETCH = ROOT / "benchmarks" / "oled_dashboard"
RESULTS = ROOT / "docs" / "board_benchmark.json"
DOC = ROOT / "docs" / "BOARD_COMPARISON.md"
RUNS = 3
START, END = "<!-- benchmark table: tools/board_benchmark.py -->", "<!-- end of benchmark table -->"

GROUPS = {
    "classic": {
        "title": "ESP32 (classic), USB-serial chip",
        "boards": "IoT RedBoard - ESP32, Thing Plus C, DataLogger IoT, RTK Postcard",
        "core": "esp32",
    },
    "native": {
        "title": "ESP32 with native USB",
        "boards": "Thing Plus S3, C5 and C6, Qwiic Pocket C6, Pro Micro C3, IoT RedBoard - ESP32-P4",
        "core": "esp32",
    },
    "teensy": {
        "title": "Teensy 4.x",
        "boards": "Teensy 4.0, Teensy 4.1, MicroMod Teensy",
        "core": "teensy",
    },
    "rp2": {
        "title": "RP2040 / RP2350",
        "boards": "Thing Plus RP2350 and RP2040, Pro Micro RP2350 and RP2040",
        "core": "rp2",
    },
}
# The plain-Arduino board for each Flick target that can stand for its group.
FQBN = {
    "iot_redboard_esp32": "esp32:esp32:sparkfun_esp32_iot_redboard",
    "thing_plus_esp32": "esp32:esp32:esp32thing_plus_c",
    "datalogger_iot": "esp32:esp32:esp32",            # no board of its own: ESP32 Dev Module
    "rtk_postcard": "esp32:esp32:esp32",
    "iot_redboard_esp32p4_poe": "esp32:esp32:esp32p4:ChipVariant=postv3,USBMode=hwcdc,CDCOnBoot=cdc",
    "iot_redboard_esp32p4_wifi": "esp32:esp32:esp32p4:ChipVariant=postv3,USBMode=hwcdc,CDCOnBoot=cdc",
    "thing_plus_esp32c6": "esp32:esp32:esp32c6:CDCOnBoot=cdc",
    "thing_plus_esp32s3": "esp32:esp32:sparkfun_esp32s3_thing_plus:CDCOnBoot=cdc",
    "teensy_micromod": "teensy:avr:teensyMM",
    "teensy40": "teensy:avr:teensy40",
    "teensy41": "teensy:avr:teensy41",
    "thing_plus_rp2350": "rp2040:rp2040:sparkfun_thingplusrp2350",
    "pro_micro_rp2350": "rp2040:rp2040:sparkfun_promicrorp2350",
    "thing_plus_rp2040": "rp2040:rp2040:sparkfun_thingplusrp2040",
    "pro_micro_rp2040": "rp2040:rp2040:sparkfun_promicrorp2040",
}
PJRC_VID = 0x16C0


def timed(cmd, env=None):
    t0 = time.perf_counter()
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    dt = time.perf_counter() - t0
    if r.returncode != 0:
        sys.exit(f"command failed: {' '.join(map(str, cmd))}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
    return dt, r.stdout + r.stderr


def edit():
    """Touch the sketch, as saving it in an editor does."""
    os.utime(SKETCH / f"{SKETCH.name}.ino")
    time.sleep(1.1)         # a new modification time even on coarse file systems


def port_now(port, core):
    """A Teensy changes COM port when its USB type changes (Baseline: Serial +
    MIDI + Audio; an Arduino sketch: Serial), and an RP2 board can come back on
    another one after its boot ROM: find it again by vendor ID."""
    vids = {"teensy": (PJRC_VID,), "rp2": flick.RP2_VIDS}.get(core)
    if not vids:
        return port
    serial = flick.import_serial()
    for p in serial.tools.list_ports.comports():
        if p.vid in vids:
            return p.device
    return port


def flick_cmd(*args):
    return [sys.executable, str(ROOT.parent / "flick.py"), *args]


def info_target(port):
    _, out = timed(flick_cmd("info", "-p", port))
    m = re.search(r"target=(\S+)", out)
    if not m:
        sys.exit(f"no Baseline answers on {port}: flash it first")
    return m.group(1)


def bench_flick(target, port):
    timed(flick_cmd("build", str(SKETCH), "-t", target))          # warm: library and SDK objects cached
    compiles, totals = [], []
    for _ in range(RUNS):
        edit()
        compiles.append(timed(flick_cmd("build", str(SKETCH), "-t", target))[0])
    for _ in range(RUNS):
        edit()
        totals.append(timed(flick_cmd("upload", str(SKETCH), "-p", port))[0])
        time.sleep(2)       # the board restarts into the sketch
    size = (flick.sketch_build_dir(SKETCH, target) / f"{SKETCH.name}.flick").stat().st_size
    compile_s = statistics.median(compiles)
    total = statistics.median(totals)
    return {"compile": compile_s, "upload": max(total - compile_s, 0.0), "total": total, "bytes": size}


def bench_arduino(fqbn, port, env, core):
    with tempfile.TemporaryDirectory() as build:
        base = ["arduino-cli", "compile", "--fqbn", fqbn, "--build-path", build, str(SKETCH)]
        timed(base, env)                                            # warm: core and library cached
        compiles = []
        for _ in range(RUNS):
            edit()
            compiles.append(timed(base, env)[0])
        uploads = []
        for _ in range(2):
            p = port_now(port, core)
            uploads.append(timed(["arduino-cli", "upload", "--fqbn", fqbn, "--input-dir", build, "-p", p,
                                  str(SKETCH)], env)[0])
            time.sleep(3)
        # What the upload sends: the .uf2 on RP2 boards (they also get a .bin).
        kinds = (".uf2",) if core == "rp2" else (".bin", ".hex")
        image = next(f for k in kinds for f in Path(build).glob(f"{SKETCH.name}.ino{k}"))
        size = image.stat().st_size
    compile_s, upload = statistics.median(compiles), statistics.median(uploads)
    return {"compile": compile_s, "upload": upload, "total": compile_s + upload, "bytes": size,
            "image": image.suffix}


def restore_baseline(target, port, core):
    time.sleep(2)
    p = port_now(port, core)
    timed(flick_cmd("baseline", "flash", "-t", target, "-p", p))


def fmt(r):
    return f"{r['compile']:.1f} s + {r['upload']:.1f} s = **{r['total']:.1f} s**"


def table(results):
    rows = ["| Boards | Arduino IDE: compile + upload | Flick: compile + upload | Edit to running |",
            "|---|---|---|---|"]
    notes = []
    for key, g in GROUPS.items():
        r = results.get(key)
        if not r:
            rows.append(f"| **{g['title']}**: {g['boards']} | not measured yet | | |")
            continue
        faster = r["arduino"]["total"] / r["flick"]["total"]
        rows.append(f"| **{g['title']}**: {g['boards']} | {fmt(r['arduino'])} | {fmt(r['flick'])} | "
                    f"**{faster:.0f}x faster** |")
        notes.append(f"{g['title']}: measured on the {r['board']} ({r['date']}); the upload is "
                     f"{r['arduino']['bytes']:,} bytes with Arduino ({r['arduino']['image']} image), "
                     f"{r['flick']['bytes']:,} with Flick (.flick)")
    return "\n".join(rows) + ("\n\n" + "\n".join(f"- {n}." for n in notes) if notes else "")


def write_table(results):
    text = DOC.read_text(encoding="utf-8")
    block = f"{START}\n{table(results)}\n{END}"
    if START in text:
        text = re.sub(re.escape(START) + r".*?" + re.escape(END), lambda _: block, text, flags=re.S)
    else:
        sys.exit(f"{DOC.name} has no {START} marker")
    DOC.write_text(text, encoding="utf-8")
    print(f"wrote the table in {DOC.relative_to(ROOT.parent)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--board", action="append", default=[], metavar="GROUP=PORT",
                    help=f"a board of that group ({', '.join(GROUPS)}) on that port, running its Baseline")
    ap.add_argument("--esp32-data", help="Arduino data folder with the Arduino-ESP32 core to compare with")
    ap.add_argument("--table-only", action="store_true")
    args = ap.parse_args()
    results = json.loads(RESULTS.read_text()) if RESULTS.exists() else {}
    if not args.table_only:
        for spec in args.board:
            key, _, port = spec.partition("=")
            if key not in GROUPS or not port:
                sys.exit(f"--board {spec}: use GROUP=PORT with GROUP one of {', '.join(GROUPS)}")
            core = GROUPS[key]["core"]
            target = info_target(port)
            if target not in FQBN:
                sys.exit(f"{target}: no plain-Arduino board known for it (FQBN in {Path(__file__).name})")
            env = dict(os.environ)
            if GROUPS[key]["core"] == "esp32" and args.esp32_data:
                env["ARDUINO_DIRECTORIES_DATA"] = args.esp32_data
            name = flick.header_defines(flick.load_target(target)["dirs"][-1] / "sdk" / "bl_board.h") \
                .get("BL_BOARD_NAME", target).strip('"')
            print(f"{key}: {name} on {port}")
            print("  Flick ...")
            f = bench_flick(target, port)
            print(f"  Flick: {fmt(f)}")
            print("  Arduino (replaces Baseline; flashed back after) ...")
            try:
                a = bench_arduino(FQBN[target], port, env, core)
            finally:
                print("  flashing Baseline back ...")
                restore_baseline(target, port, core)
            print(f"  Arduino: {fmt(a)}")
            results[key] = {"board": name, "target": target, "date": date.today().isoformat(),
                            "flick": f, "arduino": a}
            RESULTS.write_text(json.dumps(results, indent=1) + "\n")
    write_table(results)


if __name__ == "__main__":
    main()
