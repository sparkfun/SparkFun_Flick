#!/usr/bin/env python3
"""Build the Flick download page (GitHub Pages).

  python tools/make_site.py [--out site_build] [--kit dist/flick-kit-<ver>.zip]

Reads targets/*/target.json and the built images in dist/<target>/
(flick baseline build), and writes a static site:
  index.html                  boards, "Flash" buttons (ESP Web Tools, Chrome/Edge), downloads
  manifests/<target>.json     ESP Web Tools manifests
  firmware/<target>/          baseline-<target>.bin (USB/web flash at 0x0) and .bsu (over the air)
  downloads/                  the sketch kit zip

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
               "esp32c6": "ESP32-C6", "esp32p4": "ESP32-P4"}
ESP_WEB_TOOLS = "https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-button.js?module"


def board_name(target_dir):
    m = re.search(r'#define\s+BL_BOARD_NAME\s+"([^"]+)"', (target_dir / "sdk" / "bl_board.h").read_text())
    return m.group(1) if m else target_dir.name


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="site_build")
    ap.add_argument("--dist", default=str(ROOT / "dist"))
    ap.add_argument("--kit", help="the sketch kit zip (default: newest dist/flick-kit-*.zip)")
    gh = os.environ.get("GITHUB_REPOSITORY")      # set by GitHub Actions: follows a renamed repo
    ap.add_argument("--repo", default=f"https://github.com/{gh}" if gh else "https://github.com/sparkfun/SparkFun_Flick")
    args = ap.parse_args()
    out, dist = Path(args.out), Path(args.dist)
    version = re.search(r'BASELINE_VERSION\s+"([^"]+)"', (ROOT / "baseline/src/bl_main.h").read_text()).group(1)
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
        if not bin_.exists():
            print(f"skipping {name}: {bin_} not built", file=sys.stderr)
            continue
        fw = out / "firmware" / name
        fw.mkdir(parents=True)
        shutil.copy2(bin_, fw / bin_.name)
        if bsu.exists():
            shutil.copy2(bsu, fw / bsu.name)
        board = board_name(tdir)
        manifest = {
            "name": f"Baseline for {board}",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [{"chipFamily": CHIP_FAMILY[t["chip"]],
                        "parts": [{"path": f"../firmware/{name}/{bin_.name}", "offset": 0}]}],
        }
        (out / "manifests" / f"{name}.json").write_text(json.dumps(manifest, indent=2) + "\n")
        flash_mb = int(t["flash_size"].rstrip("MB"))
        rows.append((board, name, t, flash_mb, bin_, bsu))

    kit = Path(args.kit) if args.kit else max(dist.glob("flick-kit-*.zip"), default=None,
                                              key=lambda p: p.stat().st_mtime)
    kit_link = ""
    if kit and kit.exists():
        (out / "downloads").mkdir()
        shutil.copy2(kit, out / "downloads" / kit.name)
        kit_link = f'downloads/{kit.name}'

    def row_html(board, name, t, flash_mb, bin_, bsu):
        ota = "keeps the sketch" if flash_mb >= 8 else "erases the sketch"
        bsu_link = (f' · <a href="firmware/{name}/{bsu.name}" title="Baseline update over WiFi">.bsu</a>'
                    if bsu.exists() else "")
        return f"""      <tr>
        <td>{f'<a href="{html.escape(t["product_url"])}">{html.escape(board)}</a>' if t.get("product_url") else html.escape(board)}<div class="target">{name}</div></td>
        <td>{CHIP_FAMILY[t['chip']]}</td>
        <td>{flash_mb} MB<div class="target">OTA {ota}</div></td>
        <td>
          <esp-web-install-button manifest="manifests/{name}.json">
            <button slot="activate">Flash</button>
            <span slot="unsupported">Use Chrome or Edge</span>
            <span slot="not-allowed">Needs https</span>
          </esp-web-install-button>
        </td>
        <td><a href="firmware/{name}/{bin_.name}">.bin</a>{bsu_link}</td>
      </tr>"""

    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Flick</title>
<script type="module" src="{ESP_WEB_TOOLS}"></script>
<style>
  :root {{ --red: #e0311d; --ink: #1d1d1f; --mute: #6b6b70; --line: #e4e4e7; --bg: #fafafa; }}
  * {{ box-sizing: border-box; }}
  body {{ margin: 0; font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; color: var(--ink); background: var(--bg); }}
  header {{ background: var(--ink); color: #fff; padding: 32px 16px; }}
  header div, main {{ max-width: 960px; margin: 0 auto; }}
  header h1 {{ margin: 0 0 6px; font-size: 28px; }}
  header p {{ margin: 0; color: #c9c9ce; max-width: 680px; }}
  main {{ padding: 24px 16px 48px; }}
  h2 {{ font-size: 20px; margin: 32px 0 12px; }}
  .steps {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 12px; }}
  .step {{ background: #fff; border: 1px solid var(--line); border-radius: 8px; padding: 16px; }}
  .step b {{ display: block; margin-bottom: 4px; }}
  .step p {{ margin: 0; color: var(--mute); font-size: 14px; }}
  code {{ font: 13px/1.4 ui-monospace, Consolas, monospace; background: #f0f0f2; padding: 1px 4px; border-radius: 4px; }}
  pre {{ background: #fff; border: 1px solid var(--line); border-radius: 8px; padding: 12px; overflow-x: auto; }}
  pre code {{ background: none; padding: 0; }}
  .table {{ overflow-x: auto; background: #fff; border: 1px solid var(--line); border-radius: 8px; }}
  table {{ width: 100%; border-collapse: collapse; font-size: 15px; }}
  th, td {{ text-align: left; padding: 10px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }}
  tr:last-child td {{ border-bottom: 0; }}
  th {{ font-size: 13px; color: var(--mute); font-weight: 600; }}
  .target {{ font-size: 12px; color: var(--mute); }}
  a {{ color: var(--red); }}
  button {{ background: var(--red); color: #fff; border: 0; border-radius: 6px; padding: 6px 14px; font: inherit; cursor: pointer; }}
  button:hover {{ filter: brightness(1.1); }}
  .btn {{ display: inline-block; background: var(--red); color: #fff; text-decoration: none; border-radius: 6px; padding: 8px 16px; }}
  footer {{ color: var(--mute); font-size: 13px; margin-top: 32px; }}
</style>
</head>
<body>
<header><div>
  <h1>Flick</h1>
  <p>Arduino sketches for SparkFun ESP32 boards that build in about a second and upload in under one, over USB or WiFi.
  The board runs Baseline, precompiled firmware with Arduino-ESP32, WiFi, BLE and storage inside; your sketch is only your code.</p>
</div></header>
<main>
  <h2>Get started</h2>
  <div class="steps">
    <div class="step"><b>1. Flash Baseline</b><p>Plug in your board and press <i>Flash</i> next to it below (Chrome or Edge). Once per board.</p></div>
    <div class="step"><b>2. Get the kit</b><p>Unzip it, then run <code>pip install -r requirements.txt</code> and <code>python tools/flick.py setup</code>.</p></div>
    <div class="step"><b>3. Write sketches</b><p>Ask your AI assistant in the kit folder, or run <code>python tools/flick.py upload examples/blink -m</code>.</p></div>
  </div>
  <p style="margin-top:16px">{f'<a class="btn" href="{kit_link}">Download the kit ({html.escape(kit.name)})</a>' if kit_link else ''}</p>

  <h2>Boards</h2>
  <div class="table"><table>
    <thead><tr><th>Board</th><th>Chip</th><th>Flash</th><th>Install</th><th>Download</th></tr></thead>
    <tbody>
{chr(10).join(row_html(*r) for r in rows)}
    </tbody>
  </table></div>
  <p class="target"><i>.bin</i>: the full image, flash at 0x0 (<code>python tools/flick.py baseline flash --file ...</code>).
  <i>.bsu</i>: a Baseline update over WiFi (<code>flick baseline update --file ...</code> or <code>BaselineUpdate.baselineFromUrl()</code>).
  Boards with 8 or 16 MB keep their sketch through an over-the-air update; 4 MB boards lose the sketch and LittleFS.</p>

  <h2>Why a kit, and not just the firmware?</h2>
  <p>Baseline on its own runs whatever sketch you upload, but sketches are built against the kit's headers
  (<code>sdk/</code>), which say exactly what Baseline provides. The kit also has <code>AGENTS.md</code>,
  which tells AI coding assistants how to build and upload and what Baseline can't do, plus the examples and
  <code>flick</code> (build, upload, serial monitor, libraries, updates).</p>

  <footer>Baseline {version} · ABI {'.'.join(abi)} · <a href="{args.repo}">source</a></footer>
</main>
</body>
</html>
"""
    (out / "index.html").write_text(page, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site: {out} ({len(rows)} boards, Baseline {version}, ABI {'.'.join(abi)})")


if __name__ == "__main__":
    main()
