#!/usr/bin/env python3
"""Cache keys for CI's Baseline builds (.github/workflows/baseline.yml).

Prints JSON: {"targets": [...], "esp": [...], "teensy": [...], "rp2": [...], "keys": {target: key},
"all": key}. "esp", "teensy" and "rp2" split the targets by builder. A target's
key hashes every file its Baseline build reads: the shared sources and tools,
plus its own targets/<target>/ folder. "all" hashes every target's key, so one
cache entry can hold every Baseline and a run where nothing changed restores
them in one job.
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]       # flick/
sys.path.insert(0, str(ROOT / "tools" / "ci"))
from build_examples import all_targets  # noqa: E402

VERSION = "v2"      # change to drop every cached Baseline
SHARED = ["baseline", "chips", "idf", "api", "sdk/include",
          "tools/flick.py", "tools/gen_api.py", "tools/gen_audio.py",
          "tools/ci/build_baseline.sh", "tools/ci/fetch_arduino_esp32.py"]


def digest(paths):
    h = hashlib.sha256()
    for top in paths:
        p = ROOT / top
        files = sorted(f for f in p.rglob("*") if f.is_file()) if p.is_dir() else [p]
        for f in files:
            if "__pycache__" in f.parts:
                continue
            h.update(f.relative_to(ROOT).as_posix().encode() + b"\0")
            h.update(f.read_bytes().replace(b"\r\n", b"\n") + b"\0")
    return h.hexdigest()


def main():
    shared = digest(SHARED)
    targets = all_targets()
    keys = {t: f"baseline-{VERSION}-{t}-{hashlib.sha256((shared + digest([f'targets/{t}'])).encode()).hexdigest()[:40]}"
            for t in targets}
    every = hashlib.sha256("\n".join(keys[t] for t in targets).encode()).hexdigest()[:40]
    def builder(t):
        chip = json.loads((ROOT / "targets" / t / "target.json").read_text())["chip"]
        return json.loads((ROOT / "chips" / chip / "chip.json").read_text()).get("builder")
    teensy = [t for t in targets if builder(t) == "teensy"]
    rp2 = [t for t in targets if builder(t) == "rp2"]
    esp = [t for t in targets if t not in teensy and t not in rp2]
    print(json.dumps({"targets": targets, "esp": esp, "teensy": teensy, "rp2": rp2, "keys": keys,
                      "all": f"baseline-{VERSION}-all-{every}"}))


if __name__ == "__main__":
    main()
