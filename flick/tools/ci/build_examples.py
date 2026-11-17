#!/usr/bin/env python3
"""Build every example (and test sketch) for the given targets (CI check of the SDK and flick).

  python flick/tools/ci/build_examples.py TARGET [TARGET ...]
  python flick/tools/ci/build_examples.py --out DIR [TARGET ...]    also keep the images, for the web page

An example that stops on purpose with `#error "This chip has no ..."` or
`#error "This board has no ..."` counts as "not for this board", not a failure.
Exits 1 if any other build fails.

With --out (default targets: all), DIR gets what the web page's example runner
(tools/site/runner.js) needs, for the examples only:
  DIR/<group>/<name>/<target>.flick    the image for each board it builds for
  DIR/<group>/<name>/<name>.ino        its source, shown next to the Run button
  DIR/examples.json                    the list: path, group, name, title, about, wifi, targets

CI builds the boards of each chip in parallel and joins the parts:
  python flick/tools/ci/build_examples.py --groups                  the boards per chip, as JSON
  python flick/tools/ci/build_examples.py --merge DIR PART [PART ...]
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]       # flick/
sys.path.insert(0, str(ROOT / "tools"))
import flick  # noqa: E402  (sketch_build_dir: where flick build puts the images)
EXAMPLES = ROOT.parent / "examples"
NOT_FOR_BOARD = re.compile(r'#error "This (chip|board) has no')


def sketches(with_tests=True):
    """The examples (examples/<group>/<name>), plus the test sketches flick test uploads."""
    dirs = [p.parent for p in EXAMPLES.rglob("*.ino")]
    if with_tests:
        dirs += [p.parent for p in (ROOT / "tools" / "tests").glob("*/*.ino")]
    return sorted(d for d in dirs if (d / f"{d.name}.ino").exists() and "build" not in d.parts)


def describe(ex):
    """Title (the first comment line, after 'name.ino - ') and the comment lines below it."""
    lines = (ex / f"{ex.name}.ino").read_text(encoding="utf-8").splitlines()
    head = []
    for line in lines:
        if not line.startswith("//"):
            break
        head.append(line[2:].strip())
    title = head[0].split(" - ", 1)[-1] if head else ex.name
    about = " ".join(head[1:]).strip()
    return title, about


def readme_order():
    """Sketch names in the order examples/README.md lists them (its tables' first column)."""
    names = re.findall(r"^\| `(\w+)` \|", (EXAMPLES / "README.md").read_text(encoding="utf-8"), re.M)
    return {n: i for i, n in enumerate(names)}


def all_targets():
    """Every target CI can build (a chip with "ci": false needs a toolchain CI doesn't have)."""
    def ci(tj):
        chip = json.loads(tj.read_text())["chip"]
        return json.loads((ROOT / "chips" / chip / "chip.json").read_text()).get("ci", True)
    return sorted(p.parent.name for p in (ROOT / "targets").glob("*/target.json") if ci(p))


def groups():
    """The CI targets grouped by chip: [{"chip": c, "targets": "t1 t2 ..."}]."""
    by_chip = {}
    for t in all_targets():
        chip = json.loads((ROOT / "targets" / t / "target.json").read_text())["chip"]
        by_chip.setdefault(chip, []).append(t)
    return [{"chip": c, "targets": " ".join(ts)} for c, ts in sorted(by_chip.items())]


def merge(out, parts):
    """Join --out folders built for different boards into one: the images side
    by side, and one examples.json whose targets lists are the union."""
    out.mkdir(parents=True, exist_ok=True)
    listing = {}
    for part in parts:
        for f in part.rglob("*"):
            if f.is_file() and f.name != "examples.json":
                (out / f.relative_to(part)).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(f, out / f.relative_to(part))
        for ex in json.loads((part / "examples.json").read_text())["examples"]:
            have = listing.setdefault(ex["path"], dict(ex, targets=[]))
            have["targets"] = sorted(set(have["targets"]) | set(ex["targets"]))
    order = readme_order()
    examples = sorted(listing.values(), key=lambda e: (e["group"], order.get(e["name"], 999), e["name"]))
    (out / "examples.json").write_text(json.dumps({"examples": examples}, indent=1) + "\n")
    print(f"{out}: {len(examples)} examples from {len(parts)} parts")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", help="keep the example images, sources and examples.json here")
    ap.add_argument("--groups", action="store_true", help="print the CI targets grouped by chip (JSON) and stop")
    ap.add_argument("--merge", nargs="+", metavar="DIR", help="DIR PART [PART ...]: join parts built with --out")
    ap.add_argument("targets", nargs="*")
    args = ap.parse_args()
    if args.groups:
        print(json.dumps(groups()))
        return
    if args.merge:
        if len(args.merge) < 2:
            ap.error("--merge needs DIR and at least one PART")
        merge(Path(args.merge[0]), [Path(p) for p in args.merge[1:]])
        return
    targets = args.targets or (all_targets() if args.out else None)
    if not targets:
        ap.error("give at least one target (or --out for all of them)")
    out = Path(args.out) if args.out else None
    env = dict(os.environ)
    if out:
        # Images for the web page are public: never build in secrets.cfg (WiFi passwords).
        env["FLICK_SECRETS"] = str(out / "no-secrets.cfg")
    found = {}          # example dir -> targets it built for
    failed = []
    for target in targets:
        built, skipped = 0, []
        for ex in sketches(with_tests=not out):
            r = subprocess.run([sys.executable, str(ROOT / "tools" / "flick.py"), "build", str(ex), "-t", target],
                               capture_output=True, text=True, env=env)
            if r.returncode == 0:
                built += 1
                found.setdefault(ex, []).append(target)
                if out:
                    rel = ex.relative_to(EXAMPLES)
                    (out / rel).mkdir(parents=True, exist_ok=True)
                    shutil.copy2(flick.sketch_build_dir(ex, target) / f"{ex.name}.flick", out / rel / f"{target}.flick")
            elif NOT_FOR_BOARD.search(r.stdout + r.stderr):
                skipped.append(ex.name)
                found.setdefault(ex, [])
            else:
                failed.append(f"{target}/{ex.name}")
                print(f"::error::{ex.name} failed to build for {target}")
                print((r.stdout + r.stderr)[-3000:])
        print(f"{target}: {built} built" + (f", not for this board: {', '.join(skipped)}" if skipped else ""))
    if out:
        listing = []
        order = readme_order()
        for ex, built_for in sorted(found.items(), key=lambda kv: (kv[0].relative_to(EXAMPLES).parts[0],
                                                                    order.get(kv[0].name, 999), kv[0].name)):
            rel = ex.relative_to(EXAMPLES)
            src = (ex / f"{ex.name}.ino").read_text(encoding="utf-8")
            (out / rel).mkdir(parents=True, exist_ok=True)
            shutil.copy2(ex / f"{ex.name}.ino", out / rel / f"{ex.name}.ino")
            title, about = describe(ex)
            listing.append({"path": rel.as_posix(), "group": rel.parts[0], "name": ex.name,
                            "title": title, "about": about,
                            "wifi": "WIFI_SSID" in src,        # needs the user's network: build it from the kit
                            "targets": built_for})
        (out / "examples.json").write_text(json.dumps({"examples": listing}, indent=1) + "\n")
        print(f"{out}: {len(listing)} examples")
    if failed:
        print(f"{len(failed)} failed: {', '.join(failed)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
