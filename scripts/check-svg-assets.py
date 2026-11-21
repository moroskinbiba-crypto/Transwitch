#!/usr/bin/env python3
"""Validate the small built-in SVG icon set before packaging it for Switch."""
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

EXPECTED = {
    "settings.svg", "gamepad.svg", "gallery.svg", "power.svg",
    "library.svg", "home.svg", "recent.svg", "user.svg", "sliders.svg",
}


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/icons")
    actual = {path.name for path in root.glob("*.svg")}
    missing = sorted(EXPECTED - actual)
    extra = sorted(actual - EXPECTED)
    if missing:
        print("missing SVG icons: " + ", ".join(missing), file=sys.stderr)
        return 1
    if extra:
        print("unexpected SVG icons: " + ", ".join(extra), file=sys.stderr)
        return 1

    for name in sorted(EXPECTED):
        path = root / name
        if not path.is_file() or path.stat().st_size <= 0 or path.stat().st_size > 128 * 1024:
            print(f"invalid SVG file size: {path}", file=sys.stderr)
            return 1
        try:
            element = ET.parse(path).getroot()
        except (ET.ParseError, OSError) as exc:
            print(f"cannot parse {path}: {exc}", file=sys.stderr)
            return 1
        if not element.tag.endswith("svg"):
            print(f"not an SVG root: {path}", file=sys.stderr)
            return 1
        if not element.attrib.get("viewBox"):
            print(f"SVG has no viewBox: {path}", file=sys.stderr)
            return 1
    print(f"[svg-assets] OK: {len(EXPECTED)} editable vector icons")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
