#!/usr/bin/env python3
"""Apply the small GCC 15 compatibility patch required by this Tesla SDK header."""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch-libtesla-gcc15.py path/to/tesla.hpp")

header = Path(sys.argv[1])
if not header.is_file():
    raise SystemExit(f"libtesla header not found: {header}")

source = header.read_text(encoding="utf-8")
old = "this->m_scissoringStack.emplace(x, y, w, h);"
new = "this->m_scissoringStack.push({x, y, w, h});"

if old in source:
    if source.count(old) != 1:
        raise SystemExit("unexpected libtesla header: scissoring call occurs more than once")
    header.write_text(source.replace(old, new), encoding="utf-8")
    print("Applied GCC 15 aggregate-initialization compatibility patch.")
elif new in source:
    print("GCC 15 compatibility patch already present.")
else:
    raise SystemExit("unrecognized libtesla scissoring implementation; refusing a blind edit")
