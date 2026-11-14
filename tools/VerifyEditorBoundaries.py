#!/usr/bin/env python3
"""Check the editor's Qt backend boundary without starting the GUI."""

from pathlib import Path
import re
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    errors = []
    qt_header = re.compile(r'^\s*#\s*include\s*[<"](?:Q[A-Za-z]|Qt\w*/)', re.MULTILINE)
    for product in ("Editor", "Engine"):
        for path in (root / product).rglob("*"):
            if path.suffix not in (".hpp", ".cpp", ".inl"):
                continue
            relative = path.relative_to(root)
            if "ThirdParty" in relative.parts or relative.parts[:2] == ("Editor", "LuxUI"):
                continue
            if qt_header.search(path.read_text(encoding="utf-8")):
                errors.append(f"Qt include outside LuxUI: {relative}")
    for path in (root / "Engine").rglob("CMakeLists.txt"):
        if "ThirdParty" in path.parts:
            continue
        text = path.read_text(encoding="utf-8")
        if "box2d::" in text or "Qt6::" in text or "LuxUI::" in text:
            errors.append(f"Forbidden engine dependency: {path.relative_to(root)}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("LuxUI Qt boundary and engine dependency checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
