#!/usr/bin/env python3
"""Build the Flick download page (GitHub Pages).

  python flick/tools/make_site.py [--out site_build] [--kit dist/flick-kit-<ver>.zip] [--examples DIR]

Reads targets/*/target.json and the built images in dist/<target>/
(flick baseline build), and writes a static site:
  index.html                  boards, "Flash" buttons (ESP Web Tools, Chrome/Edge), downloads
  manifests/<target>.json     ESP Web Tools manifests
  firmware/<target>/          baseline-<target>.bin (USB/web flash at 0x0) and .bsu (over the air)
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
    ap.add_argument("--examples", help="built examples (tools/ci/build_examples.py --out; default: <dist>/examples)")
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

    flick_cfg = json.dumps({"abi": ".".join(abi), "boards": boards})
    kit_button = (f'<a class="btn" href="{kit_link}">Download the kit ({html.escape(kit.name)})</a>'
                  if kit_link else "")
    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Flick</title>
<script type="module" src="{ESP_WEB_TOOLS}"></script>
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
</style>
</head>
<body>
<header><div>
  <h1>Flick</h1>
  <p>Arduino sketches for SparkFun ESP32 boards that build in about a second and upload in under one, over USB or WiFi.
  The board runs Baseline, precompiled firmware with Arduino-ESP32, WiFi, BLE and storage inside; your sketch is only your code.</p>
</div></header>
<main>
  <h2><span class="n">1</span>Put Baseline on your board</h2>
  <p class="lead">Plug in the board and press <b>Flash</b> next to it (Chrome or Edge). You do this once per board,
  and again only for a newer Baseline.</p>
  <div class="table"><table>
    <thead><tr><th>Board</th><th>Chip</th><th>Flash</th><th>Install</th><th>Download</th></tr></thead>
    <tbody>
{chr(10).join(row_html(*r) for r in rows)}
    </tbody>
  </table></div>

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
    <div class="step"><b>Build and upload</b><p>Open <code>examples/01_basics/hello_flick/hello_flick.ino</code> and press <b>Ctrl+Shift+B</b>. Your own sketches go in <code>my_sketches/</code>.</p></div>
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
<script type="module" src="runner.js"></script>
</body>
</html>
"""
    (out / "index.html").write_text(page, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site: {out} ({len(rows)} boards, {n_examples} examples, Baseline {version}, ABI {'.'.join(abi)})")


if __name__ == "__main__":
    main()
