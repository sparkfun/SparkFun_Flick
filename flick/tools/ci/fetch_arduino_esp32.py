#!/usr/bin/env python3
"""Install just what a Baseline build needs from Arduino-ESP32 (CI):

  python3 flick/tools/ci/fetch_arduino_esp32.py VERSION TARGET [TARGET ...]

the core's sources and each target chip's precompiled libraries, into
~/.arduino15/packages/esp32/{hardware/esp32,tools/<chip>-libs}/VERSION, where
flick looks. arduino-cli would install the whole platform, compilers and
debuggers for every chip included, which overflows a GitHub runner's disk next
to the ESP-IDF image (ESP-IDF has its own compilers).
"""
import io
import json
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INDEX = "https://espressif.github.io/arduino-esp32/package_esp32_index.json"
BASE = Path.home() / ".arduino15" / "packages" / "esp32"


def fetch_zip(url, dest):
    """Unpack a zip's single top folder into dest."""
    print(f"downloading {url.rsplit('/', 1)[1]}")
    z = zipfile.ZipFile(io.BytesIO(urllib.request.urlopen(url, timeout=600).read()))
    tmp = dest.parent / (dest.name + ".unpack")
    shutil.rmtree(tmp, ignore_errors=True)
    z.extractall(tmp)
    (top,) = list(tmp.iterdir())
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(top), str(dest))
    shutil.rmtree(tmp, ignore_errors=True)


def main():
    version, targets = sys.argv[1], sys.argv[2:]
    chips = sorted({json.loads((ROOT / "chips" / json.loads((ROOT / "targets" / t / "target.json").read_text())["chip"]
                                / "chip.json").read_text())["idf_target"] for t in targets})
    pkg = json.load(urllib.request.urlopen(INDEX, timeout=120))["packages"][0]
    plat = next(p for p in pkg["platforms"] if p["version"] == version)
    deps = {d["name"]: d["version"] for d in plat["toolsDependencies"]}
    core = BASE / "hardware" / "esp32" / version
    if not core.exists():
        fetch_zip(plat["url"], core)
    for chip in chips:
        name = f"{chip}-libs"
        dest = BASE / "tools" / name / version
        if dest.exists():
            continue
        tool = next(t for t in pkg["tools"] if t["name"] == name and t["version"] == deps[name])
        system = next(s for s in tool["systems"] if "linux" in s["host"] and s["host"].startswith("x86_64"))
        fetch_zip(system["url"], dest)
    print(f"Arduino-ESP32 {version}: core + {', '.join(c + '-libs' for c in chips)} in {BASE}")


if __name__ == "__main__":
    main()
