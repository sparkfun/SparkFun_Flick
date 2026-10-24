#!/usr/bin/env python3
"""flick: build and upload Flick sketches. Run  python flick.py --help

The tool itself is flick/tools/flick.py; this file only starts it, so the
commands are short:  python flick.py upload examples/01_basics/01_hello_flick -m
"""
import runpy
import sys
from pathlib import Path

tool = Path(__file__).resolve().parent / "flick" / "tools" / "flick.py"
sys.argv[0] = str(tool)
runpy.run_path(str(tool), run_name="__main__")
