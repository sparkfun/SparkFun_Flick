#!/usr/bin/env python3
"""Build the Flick download page (GitHub Pages).

  python flick/tools/make_site.py [--out site_build] [--kit dist/flick-kit-<ver>.zip] [--examples DIR]

Reads targets/*/target.json and the built images in dist/<target>/
(flick baseline build), and writes a static site:
  index.html                  boards, "Flash" buttons (runner.js + esptool-js, Chrome/Edge), downloads
  manifests/<target>.json     what each Flash button writes (ESP Web Tools' manifest format)
  firmware/<target>/          baseline-<target>.bin (USB/web flash at 0x0) and .bsu (over the air);
                              .uf2 for RP2040/RP2350 boards, .hex for Teensy
  downloads/                  the sketch kit zip
  examples/, runner.js        the examples, ready to run from the page over Web Serial
                              (DIR from tools/ci/build_examples.py --out; default dist/examples)

Every target is listed, unless its target.json says "listed": false; the
board name links to its product_url when it has one. The release workflow (.github/workflows/release.yml) runs
this and publishes the result.
"""
import argparse
import html
import json
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHIP_FAMILY = {"esp32": "ESP32", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3", "esp32c5": "ESP32-C5",
               "esp32c6": "ESP32-C6", "esp32p4": "ESP32-P4", "imxrt1062": "Teensy 4.x", "rp2350": "RP2350",
               "rp2040": "RP2040"}
# Groups in the board menu, in this order (by chip family).
GROUPS = [("esp32", "ESP32 (classic)"), ("esp32s3", "ESP32-S3"), ("esp32c3", "ESP32-C3"), ("esp32c5", "ESP32-C5"),
          ("esp32c6", "ESP32-C6"), ("esp32p4", "ESP32-P4"), ("rp2350", "RP2350"), ("rp2040", "RP2040"),
          ("imxrt1062", "Teensy 4.x (PJRC)")]


APP_OFFSET = 0x10000            # every ESP partition table puts Baseline here
APP_DESC_MAGIC = (0xABCD5432).to_bytes(4, "little")


def build_id(image):
    """The Baseline build in a merged .bin: the first 16 hex digits of the ELF
    SHA-256 in its app description (esp_app_desc_t.app_elf_sha256, 144 bytes
    in), as Baseline reports it in INFO build=. None if it isn't there."""
    data = Path(image).read_bytes()
    if Path(image).suffix == ".uf2":
        # RP2040/RP2350: flick compiles "BLBUILD=<16 hex>" into Baseline (bl_rp2_loader.cpp).
        # The payloads (256 bytes each, after a 32-byte header) in address order, so the
        # string is found even where it crosses from one block into the next.
        blocks = sorted((int.from_bytes(data[i + 12:i + 16], "little"),
                         data[i + 32:i + 32 + int.from_bytes(data[i + 16:i + 20], "little")])
                        for i in range(0, len(data) - 511, 512))
        m = re.search(rb"BLBUILD=([0-9a-f]{16})\0", b"".join(d for _, d in blocks))
        return m.group(1).decode() if m else None
    at = data.find(APP_DESC_MAGIC, APP_OFFSET, APP_OFFSET + 0x1000)
    if at < 0:
        return None
    return data[at + 144:at + 152].hex()


def board_name(target_dir):
    m = re.search(r'#define\s+BL_BOARD_NAME\s+"([^"]+)"', (target_dir / "sdk" / "bl_board.h").read_text())
    return m.group(1) if m else target_dir.name


# Teensyduino's code_size per flash size: the flash Teensy's bootloader writes
# (teensy_loader_cli).
TEENSY_CODE_SIZE = {2: 2031616, 8: 8126464, 16: 16515072}


def teensy_model(header):
    """BL_BOARD_TEENSY_MODEL from a bl_board.h, following a variant's #include
    of its base board's: the model Teensy's bootloader reports (HID usage)."""
    text = header.read_text()
    m = re.search(r"#define\s+BL_BOARD_TEENSY_MODEL\s+(0x[0-9A-Fa-f]+|\d+)", text)
    if m:
        return int(m.group(1), 0)
    inc = re.search(r'#include\s+"([^"]+/bl_board\.h)"', text)
    return teensy_model(header.parent / inc.group(1)) if inc else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="site_build")
    ap.add_argument("--dist", default=str(ROOT / "dist"))
    ap.add_argument("--kit", help="the sketch kit zip (default: newest dist/flick-kit-*.zip)")
    ap.add_argument("--examples", help="built examples (tools/ci/build_examples.py --out; default: <dist>/examples)")
    gh = os.environ.get("GITHUB_REPOSITORY")      # set by GitHub Actions: follows a renamed repo
    ap.add_argument("--repo", default=f"https://github.com/{gh}" if gh else "https://github.com/sparkfun/SparkFun_Flick")
    args = ap.parse_args()
    out, dist = Path(args.out), Path(args.dist)
    version = re.search(r'BASELINE_VERSION\s+"([^"]+)"', (ROOT / "baseline/src/bl_main.h").read_text()).group(1)
    # The Teensy and RP2 Baselines have their own copy of the version: the page names one for all.
    for other in ("chips/imxrt1062/baseline/bl_teensy.h", "chips/rp2350/baseline/bl_rp2.h"):
        v = re.search(r'BASELINE_VERSION\s+"([^"]+)"', (ROOT / other).read_text()).group(1)
        if v != version:
            sys.exit(f"make_site: BASELINE_VERSION is {v} in {other}, {version} in baseline/src/bl_main.h")
    abi = re.findall(r"#define BL_ABI_(?:MAJOR|MINOR) (\d+)", (ROOT / "sdk/include/baseline_api.h").read_text())

    if out.exists():
        shutil.rmtree(out)
    (out / "manifests").mkdir(parents=True)
    rows = []
    for tdir in sorted((ROOT / "targets").iterdir()):
        tj = tdir / "target.json"
        if not tj.exists():
            continue
        t = json.loads(tj.read_text())
        name = tdir.name
        if t.get("listed") is False:
            continue
        bin_ = dist / name / f"baseline-{name}.bin"
        bsu = dist / name / f"baseline-{name}.bsu"
        hex_ = dist / name / f"baseline-{name}.hex"
        uf2 = dist / name / f"baseline-{name}.uf2"
        board = board_name(tdir)
        flash_mb = int(t["flash_size"].rstrip("MB"))
        if t["chip"] not in CHIP_FAMILY:
            print(f"skipping {name}: no download page support for chip {t['chip']}", file=sys.stderr)
            continue
        if not bin_.exists():
            other = hex_ if hex_.exists() else uf2 if uf2.exists() else None
            if other:                           # Teensy (.hex, Teensy Loader) or RP2040/RP2350 (.uf2, flashRp2())
                fw = out / "firmware" / name
                fw.mkdir(parents=True)
                shutil.copy2(other, fw / other.name)
                rows.append((board, name, t, flash_mb, other, bsu))
            else:
                print(f"skipping {name}: {bin_} not built", file=sys.stderr)
            continue
        fw = out / "firmware" / name
        fw.mkdir(parents=True)
        shutil.copy2(bin_, fw / bin_.name)
        if bsu.exists():
            shutil.copy2(bsu, fw / bsu.name)
        # No full erase: the merged image already blanks NVS and otadata, and
        # keeping LittleFS keeps the user's files. Only the coredump partition
        # is blanked, as `flick baseline flash` does: a dump left by the
        # previous firmware can't be decoded against this Baseline.
        parts = [{"path": f"../firmware/{name}/{bin_.name}", "offset": 0}]
        table = ROOT / "chips" / t["chip"] / f"partitions-{flash_mb}MB.csv"
        for line in table.read_text().splitlines() if table.exists() else []:
            cells = [c.strip() for c in line.split(",")]
            if cells[0] == "coredump":
                size = int(cells[4], 0)
                blank = out / "firmware" / f"blank-{size // 1024}k.bin"
                if not blank.exists():
                    blank.write_bytes(b"\xff" * size)
                parts.append({"path": f"../firmware/{blank.name}", "offset": int(cells[3], 0)})
        manifest = {
            "name": f"Baseline for {board}",
            "version": version,
            "builds": [{"chipFamily": CHIP_FAMILY[t["chip"]], "parts": parts}],
        }
        (out / "manifests" / f"{name}.json").write_text(json.dumps(manifest, indent=2) + "\n")
        rows.append((board, name, t, flash_mb, bin_, bsu))

    examples = Path(args.examples) if args.examples else dist / "examples"
    n_examples = 0
    if (examples / "examples.json").exists():
        shutil.copytree(examples, out / "examples", ignore=shutil.ignore_patterns("no-secrets.cfg"))
        n_examples = len(json.loads((examples / "examples.json").read_text())["examples"])
    else:
        print(f"no examples in {examples}: the page has no example runner (tools/ci/build_examples.py --out)",
              file=sys.stderr)
    shutil.copy2(ROOT / "tools" / "site" / "runner.js", out / "runner.js")
    boards = {name: board for board, name, *_ in rows}
    # What the Flash buttons compare with INFO build= (a board already running
    # this Baseline is not flashed again).
    builds = {name: build_id(image) for _, name, _, _, image, _ in rows if image.suffix in (".bin", ".uf2")}

    kit = Path(args.kit) if args.kit else max(dist.glob("flick-kit-*.zip"), default=None,
                                              key=lambda p: p.stat().st_mtime)
    kit_link = ""
    if kit and kit.exists():
        (out / "downloads").mkdir()
        shutil.copy2(kit, out / "downloads" / kit.name)
        kit_link = f'downloads/{kit.name}'

    def sort_key(r):
        board, name, t, flash_mb, *_ = r
        return (bool(t.get("generic")), flash_mb if t.get("generic") else 0, name.endswith("_octal"), board.lower())

    def board_html(board, name, t, flash_mb, image, bsu):
        fam = CHIP_FAMILY[t["chip"]]
        title = (f'<a href="{html.escape(t["product_url"])}">{html.escape(board)}</a>'
                 if t.get("product_url") else html.escape(board))
        notes = []
        if t.get("generic"):
            notes.append(f"For any {fam} board with {flash_mb} MB of flash, such as {html.escape(t.get('examples', ''))}. "
                         "Pins follow Arduino-ESP32's generic variant. Not yet tested on hardware.")
            if t["chip"] != "esp32":
                notes.append("Plug into the chip's own USB port: on boards with two ports, the one labeled USB, not UART.")
            if name.endswith("_octal"):
                notes.append("For modules with octal PSRAM (the R8 in N8R8 or N16R8). GPIO 33-37 belong to the PSRAM.")
            elif t["chip"] == "esp32s3":
                notes.append("For modules with quad PSRAM (R2) or none. With octal PSRAM (R8), pick the octal entry.")
        if image.suffix == ".hex":
            code_size = TEENSY_CODE_SIZE[flash_mb]
            install = (f'<button class="teensy-flash" data-target="{name}" data-code-size="{code_size}">Flash</button>'
                       '<div class="flash-status"></div>'
                       '<p class="note-small">Chrome or Edge, with the Teensy Loader app closed (it takes the '
                       'bootloader for itself). Or open the .hex in '
                       '<a href="https://www.pjrc.com/teensy/loader.html">Teensy Loader</a>.</p>')
        elif image.suffix == ".uf2":
            drive = "RP2350" if t["chip"] == "rp2350" else "RPI-RP2"
            install = ('<p class="note-small">The first time, hold the BOOTSEL button while you plug the board in: '
                       f'it shows a drive named {drive}. After that, Flash restarts the board into that mode by itself.</p>'
                       f'<button class="flash rp2" data-target="{name}" data-chip="{fam}" data-flash-mb="{flash_mb}">Flash</button>'
                       '<div class="flash-status"></div>')
        else:
            install = (f'<button class="flash" data-target="{name}" data-chip="{fam}" data-flash-mb="{flash_mb}">Flash</button>'
                       '<div class="flash-status"></div>')
        links = [f'<a href="firmware/{name}/{image.name}">{image.suffix}</a>']
        if bsu.exists():
            links.append(f'<a href="firmware/{name}/{bsu.name}" title="Baseline update over WiFi">.bsu</a>')
        note_html = "".join(f"<p class=\"note-small\">{n}</p>" for n in notes)
        return f"""      <div class="board" data-target="{name}" hidden>
        <div class="board-title">{title}</div>
        <div class="facts">{fam} · {flash_mb} MB flash · download {' · '.join(links)}</div>
        {note_html}
        {install}
      </div>"""

    options, panels = [], []
    for chip, label in GROUPS:
        group = sorted((r for r in rows if r[2]["chip"] == chip), key=sort_key)
        if not group:
            continue
        options.append(f'    <optgroup label="{html.escape(label)}">')
        for r in group:
            options.append(f'      <option value="{r[1]}">{html.escape(r[0])}</option>')
            panels.append(board_html(*r))
        options.append("    </optgroup>")

    # Auto-detect, two ways (runner.js, flashBoard()): "SparkFun" matches the
    # chip and flash size to a SparkFun board (asking when several match, or
    # taking the target a Baseline already on the board reports); "Generic"
    # flashes the largest generic image that fits (pickGeneric()).
    generic = [{"target": name, "name": board, "chip": CHIP_FAMILY[t["chip"]], "flash_mb": flash_mb,
                "octal": name.endswith("_octal")}
               for board, name, t, flash_mb, image, _ in rows if t.get("generic") and image.suffix == ".bin"]
    sparkfun = [{"target": name, "name": board, "chip": CHIP_FAMILY[t["chip"]], "flash_mb": flash_mb}
                for board, name, t, flash_mb, image, _ in rows if not t.get("generic") and image.suffix == ".bin"]
    auto_text = ("Not sure which ESP module you've got? Auto-detect will check and load the correct Baseline for your "
                 "board. If you've got a SparkFun board, auto-detect will attempt to select the right one and display "
                 "the supported examples.")
    # RP2040/RP2350 (flashRp2()): the target a Baseline on the board reports,
    # else the chip from the boot ROM, asking when several boards have it.
    rp2 = [{"target": name, "name": board, "chip": CHIP_FAMILY[t["chip"]], "flash_mb": flash_mb}
           for board, name, t, flash_mb, image, _ in sorted(rows, key=sort_key) if image.suffix == ".uf2"]
    rp2_text = ("Not sure which RP2040 or RP2350 board you've got? Auto-detect asks the board, or reads the chip "
                "from its boot ROM and asks you when two SparkFun boards share it. The first time, hold the BOOTSEL "
                "button while you plug the board in.")
    # Teensy 4.x (flashTeensy()): the target a Baseline on the board reports,
    # else the model its bootloader reports (HID usage), which names the board.
    teensy = [{"target": name, "name": board, "model": teensy_model(ROOT / "targets" / name / "sdk" / "bl_board.h"),
               "code_size": TEENSY_CODE_SIZE[flash_mb], "hid": name.endswith("_hid")}
              for board, name, t, flash_mb, image, _ in sorted(rows, key=sort_key) if image.suffix == ".hex"]
    teensy_text = ("Not sure which Teensy 4.x you've got? Auto-detect asks the board, or reads the model from its "
                   "bootloader. Chrome or Edge, with the Teensy Loader app closed.")
    auto_option = []
    for key, label, have, text, cls in (
            ("auto-sparkfun", "SparkFun ESP32 board - Auto-detect", sparkfun, auto_text, "flash"),
            ("auto-generic", "Generic ESP32 board - Auto-detect", generic, auto_text, "flash"),
            ("auto-rp2", "SparkFun RP2040 / RP2350 board - Auto-detect", rp2, rp2_text, "flash rp2"),
            ("auto-teensy", "Teensy 4.x board - Auto-detect", teensy, teensy_text, "teensy-flash")):
        if not have:
            continue
        chips = sorted({g.get("chip", "i.MX RT1062") for g in have},
                       key=lambda c: list(CHIP_FAMILY.values()).index(c) if c in CHIP_FAMILY.values() else 99)
        auto_option.append(f'    <option value="{key}">{html.escape(label)}</option>')
        panels.insert(len(auto_option) - 1, f"""      <div class="board" data-target="{key}" hidden>
        <div class="board-title">{html.escape(label)}</div>
        <div class="facts">{' · '.join(chips)}</div>
        <p class="note-small">{html.escape(text)}</p>
        <button class="{cls}" data-target="{key}" data-chip="" data-flash-mb="0" data-code-size="0">Flash</button>
        <div class="flash-status"></div>
      </div>""")
    none = ("Pick a board above. SparkFun boards come first in each group; for another board, pick the generic entry "
            "with its chip and flash size (printed on the module: N8R8 = 8 MB flash, 8 MB PSRAM)"
            + (", or one of the Auto-detect entries" if auto_option else "") + ". The Flash button checks both before it writes.")
    board_picker = "\n".join(["  <select id=\"board-pick\" aria-label=\"Board\">",
                              "    <option value=\"\">Choose your board...</option>", *auto_option, *options, "  </select>",
                              "  <div class=\"card board-card\" id=\"board-card\">", *panels,
                              f"      <p class=\"note-small\" id=\"board-none\">{none}</p>", "  </div>"])

    flick_cfg = json.dumps({"abi": ".".join(abi), "version": version, "boards": boards, "builds": builds, "generic": generic,
                            "sparkfun": sparkfun, "rp2": rp2, "teensy": teensy})
    kit_button = (f'<a class="btn" href="{kit_link}">Download the kit ({html.escape(kit.name)})</a>'
                  if kit_link else "")
    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Flick</title>
<style>
  :root {{ --red: #e0311d; --ink: #1d1d1f; --mute: #6b6b70; --line: #e4e4e7; --bg: #fafafa; --card: #fff;
          --code: #f0f0f2; --good: #1a7f37; --bad: #c62828; --term: #16161a; --term-ink: #e6e6e6; }}
  @media (prefers-color-scheme: dark) {{
    :root {{ --ink: #ececf0; --mute: #a0a0a8; --line: #33333a; --bg: #121214; --card: #1c1c20; --code: #26262c;
            --good: #4cc26a; --bad: #ff6b6b; --red: #ff5a45; }}
  }}
  * {{ box-sizing: border-box; }}
  body {{ margin: 0; font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; color: var(--ink); background: var(--bg); }}
  header {{ background: #1d1d1f; color: #fff; padding: 32px 16px; }}
  header div, main {{ max-width: 960px; margin: 0 auto; }}
  header h1 {{ margin: 0 0 6px; font-size: 28px; }}
  header p {{ margin: 0; color: #c9c9ce; max-width: 680px; }}
  main {{ padding: 24px 16px 48px; }}
  h2 {{ font-size: 20px; margin: 36px 0 8px; }}
  h2 .n {{ display: inline-block; width: 28px; height: 28px; line-height: 28px; text-align: center; border-radius: 50%;
          background: var(--red); color: #fff; font-size: 15px; margin-right: 8px; }}
  h3 {{ font-size: 13px; color: var(--mute); text-transform: uppercase; letter-spacing: .04em; margin: 14px 0 4px; }}
  .lead {{ color: var(--mute); margin: 0 0 12px; max-width: 720px; }}
  code {{ font: 13px/1.4 ui-monospace, Consolas, monospace; background: var(--code); padding: 1px 4px; border-radius: 4px; }}
  pre {{ margin: 0; font: 13px/1.45 ui-monospace, Consolas, monospace; }}
  .table, .card {{ background: var(--card); border: 1px solid var(--line); border-radius: 8px; }}
  .table {{ overflow-x: auto; }}
  table {{ width: 100%; border-collapse: collapse; font-size: 15px; }}
  th, td {{ text-align: left; padding: 10px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }}
  tr:last-child td {{ border-bottom: 0; }}
  th {{ font-size: 13px; color: var(--mute); font-weight: 600; }}
  .target, .note-small {{ font-size: 12px; color: var(--mute); }}
  a {{ color: var(--red); }}
  button {{ background: var(--red); color: #fff; border: 0; border-radius: 6px; padding: 6px 14px; font: inherit; cursor: pointer; }}
  button:hover {{ filter: brightness(1.1); }}
  button:disabled {{ background: var(--line); color: var(--mute); cursor: default; filter: none; }}
  .btn {{ display: inline-block; background: var(--red); color: #fff; text-decoration: none; border-radius: 6px; padding: 8px 16px; }}
  .bar {{ display: flex; gap: 12px; align-items: center; flex-wrap: wrap; margin-bottom: 12px; }}
  .status {{ color: var(--mute); }}
  .status.good {{ color: var(--good); }}
  .status.bad {{ color: var(--bad); }}
  #board-pick {{ font: inherit; padding: 8px 10px; border: 1px solid var(--line); border-radius: 6px; background: var(--card);
                color: var(--ink); width: 100%; max-width: 480px; margin-bottom: 12px; }}
  .board-card {{ padding: 16px; max-width: 720px; }}
  .board-title {{ font-weight: 600; font-size: 17px; }}
  .facts {{ color: var(--mute); font-size: 14px; margin: 2px 0 8px; }}
  .board .note-small {{ margin: 0 0 8px; }}
  .board button.flash, .board button.teensy-flash {{ margin-top: 4px; }}
  .flash-status {{ font-size: 12px; color: var(--mute); margin-top: 4px; max-width: 420px; }}
  .flash-found {{ font-size: 13px; color: var(--ink); margin-top: 6px; max-width: 420px; font-weight: 600; }}
  .flash-status button.choice {{ font-size: 13px; padding: 4px 10px; margin: 6px 6px 0 0; }}
  .flash-status.good {{ color: var(--good); }}
  .flash-status.bad {{ color: var(--bad); }}
  .runner {{ display: grid; grid-template-columns: minmax(0, 5fr) minmax(0, 6fr); gap: 12px; }}
  @media (max-width: 760px) {{ .runner {{ grid-template-columns: minmax(0, 1fr); }} }}
  #examples {{ padding: 4px 12px 12px; max-height: 640px; overflow-y: auto; }}
  #examples ul {{ list-style: none; margin: 0; padding: 0; }}
  #examples li {{ padding: 5px 4px; border-radius: 6px; font-size: 14px; }}
  #examples li.selected {{ background: var(--code); }}
  #examples li button {{ padding: 2px 10px; font-size: 13px; }}
  #examples .about {{ color: var(--mute); }}
  .pane {{ display: flex; flex-direction: column; gap: 12px; min-width: 0; }}
  .pane .head {{ font-size: 13px; color: var(--mute); padding: 8px 12px; border-bottom: 1px solid var(--line);
                display: flex; justify-content: space-between; align-items: center; gap: 8px; }}
  #source {{ padding: 12px; max-height: 300px; overflow: auto; }}
  #console {{ background: var(--term); color: var(--term-ink); padding: 12px; height: 280px; overflow: auto;
             font: 13px/1.45 ui-monospace, Consolas, monospace; white-space: pre-wrap; word-break: break-all; }}
  #console .note {{ color: #8ab4ff; }}
  #console .bad {{ color: #ff8a80; }}
  #send-form {{ display: flex; gap: 8px; padding: 8px; border-top: 1px solid var(--line); }}
  #send {{ flex: 1; min-width: 0; font: 13px ui-monospace, Consolas, monospace; padding: 6px 8px; border: 1px solid var(--line);
          border-radius: 6px; background: var(--card); color: var(--ink); }}
  .head button {{ padding: 2px 10px; font-size: 13px; background: var(--code); color: var(--ink); }}
  .steps {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 12px; }}
  .step {{ background: var(--card); border: 1px solid var(--line); border-radius: 8px; padding: 16px; }}
  .step b {{ display: block; margin-bottom: 4px; }}
  .step p {{ margin: 0; color: var(--mute); font-size: 14px; }}
  .step code {{ word-break: break-all; }}
  footer {{ color: var(--mute); font-size: 13px; margin-top: 40px; }}
  #browser-warn {{ max-width: 440px; width: calc(100% - 32px); border: 1px solid var(--line); border-radius: 10px;
                  background: var(--card); color: var(--ink); padding: 20px 22px; }}
  #browser-warn::backdrop {{ background: rgba(0, 0, 0, .55); }}
  #browser-warn h2 {{ margin: 0 0 8px; }}
  #browser-warn p {{ margin: 0 0 12px; color: var(--mute); }}
  #browser-warn .bar {{ margin: 16px 0 0; justify-content: flex-end; }}
  #browser-close {{ background: var(--code); color: var(--ink); }}
</style>
</head>
<body>
<header><div>
  <h1>Flick</h1>
  <p>Flick shortens compiling and loading an Arduino sketch to mere seconds. A big blob of code called Baseline
  is preloaded onto the board. After that, all that is compiled and loaded is your personal sketch.</p>
</div></header>
<main>
  <h2><span class="n">1</span>Put Baseline on your board</h2>
  <p class="lead">Pick your board, plug it in and press <b>Flash</b> (Chrome or Edge). You do this once per board,
  and again only for a newer Baseline.</p>
{board_picker}

  <h2><span class="n">2</span>Try the examples</h2>
  <p class="lead">Press <b>Connect</b>, pick the board, then <b>Run</b> an example: it is on the board a second later,
  and its output shows below. Start with <b>hello_flick</b>. Nothing to install.</p>
  <div class="bar">
    <button id="connect">Connect</button>
    <span id="board-status" class="status">Not connected.</span>
  </div>
  <div class="runner">
    <div class="card" id="examples">Loading the examples...</div>
    <div class="pane">
      <div class="card">
        <div class="head"><span id="source-name">Source</span></div>
        <pre id="source"></pre>
      </div>
      <div class="card">
        <div class="head"><span>Serial output</span><button id="clear">Clear</button></div>
        <div id="console"></div>
        <form id="send-form"><input id="send" placeholder="Type to send to the sketch's Serial" autocomplete="off"><button>Send</button></form>
      </div>
    </div>
  </div>

  <h2><span class="n">3</span>Write your own</h2>
  <p class="lead">The kit is everything you need to change the examples and write your own sketches in VS Code,
  with one key to build and upload. It is small: the examples, the <code>flick</code> tool and the board files.</p>
  <div class="steps">
    <div class="step"><b>Get the kit</b><p>Unzip it and open the folder in VS Code. You also need Python 3.</p></div>
    <div class="step"><b>Install the compiler</b><p>Terminal &gt; Run Task &gt; <i>Setup: install the compiler (once)</i>, with the board plugged in.</p></div>
    <div class="step"><b>Build and upload</b><p>Open <code>examples/01_basics/01_hello_flick/01_hello_flick.ino</code> and press <b>Ctrl+Shift+B</b>. Your own sketches go in <code>my_sketches/</code>.</p></div>
  </div>
  <p style="margin-top:16px">{kit_button}</p>
  <p class="note-small">The kit also has <code>AGENTS.md</code>, which tells AI coding assistants (Claude Code, Copilot, Cursor, ...)
  how to build and upload, and what Baseline cannot do: open the folder in one and ask for the sketch you want.</p>

  <p class="note-small" style="margin-top:32px"><i>.bin</i>: the full image, flash at 0x0 (<code>python flick.py baseline flash --file ...</code>).
  <i>.bsu</i>: a Baseline update over WiFi (<code>python flick.py baseline update --file ...</code> or <code>BaselineUpdate.baselineFromUrl()</code>).
  Boards with 8 or 16 MB keep their sketch through an over-the-air update; 4 MB boards lose the sketch and LittleFS.</p>

  <footer>Baseline {version} · ABI {'.'.join(abi)} · <a href="{args.repo}">source</a></footer>
</main>
<script>window.FLICK = {flick_cfg};</script>
<dialog id="browser-warn" aria-labelledby="browser-title">
  <h2 id="browser-title">Please use Chrome or Edge</h2>
  <p id="browser-why">Flashing Baseline and running examples talk to the board over USB (Web Serial),
  which is supported only in Google Chrome and Microsoft Edge on a computer.</p>
  <p>Open this page in Chrome or Edge to continue.</p>
  <div class="bar"><button id="browser-copy" type="button">Copy page link</button>
  <button id="browser-close" type="button">Continue anyway</button></div>
</dialog>
<script type="module" src="runner.js"></script>
</body>
</html>
"""
    (out / "index.html").write_text(page, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site: {out} ({len(rows)} boards, {n_examples} examples, Baseline {version}, ABI {'.'.join(abi)})")


if __name__ == "__main__":
    main()
