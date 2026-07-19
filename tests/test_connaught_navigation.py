#!/usr/bin/env python3
"""Verify the checked Connaught Place search/routing graph regenerates exactly."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    checked = root / "data/connaught_place/navigation.json"
    with tempfile.TemporaryDirectory() as directory:
        generated = Path(directory) / "navigation.json"
        subprocess.run(
            [
                sys.executable,
                str(root / "tools/build_connaught_navigation.py"),
                "--source",
                str(root / "data/connaught_place/source.osm"),
                "--output",
                str(generated),
            ],
            check=True,
        )
        if generated.read_bytes() != checked.read_bytes():
            raise AssertionError("Connaught Place navigation graph checksum drift")
    data = json.loads(checked.read_text(encoding="utf-8"))
    assert len(data["nodes"]) > 3000
    assert len(data["edges"]) > 6000
    assert len(data["places"]) > 200
    print("Connaught Place navigation graph is deterministic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
