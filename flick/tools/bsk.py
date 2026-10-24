#!/usr/bin/env python3
"""bsk is now flick (tools/flick.py): this forwards to it for one release."""
import runpy
import sys
from pathlib import Path

print("note: bsk is now flick: python flick.py ...", file=sys.stderr)
sys.argv[0] = str(Path(__file__).with_name("flick.py"))
runpy.run_path(sys.argv[0], run_name="__main__")
