#!/usr/bin/env python3
"""Build every example for the given targets (CI check of the SDK and flick).

  python tools/ci/build_examples.py TARGET [TARGET ...]

An example that stops on purpose with `#error "This chip has no ..."` or
`#error "This board has no ..."` counts as "not for this board", not a failure.
Exits 1 if any other build fails.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NOT_FOR_BOARD = re.compile(r'#error "This (chip|board) has no')


def main():
    failed = []
    for target in sys.argv[1:]:
        built, skipped = 0, []
        for ex in sorted(p for p in (ROOT / "examples").iterdir() if (p / f"{p.name}.ino").exists()):
            r = subprocess.run([sys.executable, str(ROOT / "tools" / "flick.py"), "build", str(ex), "-t", target],
                               capture_output=True, text=True)
            if r.returncode == 0:
                built += 1
            elif NOT_FOR_BOARD.search(r.stdout + r.stderr):
                skipped.append(ex.name)
            else:
                failed.append(f"{target}/{ex.name}")
                print(f"::error::{ex.name} failed to build for {target}")
                print((r.stdout + r.stderr)[-3000:])
        print(f"{target}: {built} built" + (f", not for this board: {', '.join(skipped)}" if skipped else ""))
    if failed:
        print(f"{len(failed)} failed: {', '.join(failed)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
