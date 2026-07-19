#!/usr/bin/env python3
"""Regenerate checked city tiles and reject any byte-level drift."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare_trees(checked: Path, generated: Path) -> None:
    checked_files = sorted(
        path.relative_to(checked) for path in checked.rglob("*") if path.is_file()
    )
    generated_files = sorted(
        path.relative_to(generated) for path in generated.rglob("*") if path.is_file()
    )
    if checked_files != generated_files:
        raise AssertionError(
            f"generated file set drift for {checked.parent.name}: "
            f"checked={len(checked_files)} generated={len(generated_files)}"
        )
    for relative in checked_files:
        if digest(checked / relative) != digest(generated / relative):
            raise AssertionError(
                f"generated city content drift: {checked.parent.name}/{relative}"
            )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    cities = [
        (
            "connaught_place",
            ["28.6270", "77.2070", "28.6365", "77.2245"],
            "connaught-place",
            "Connaught Place",
        ),
        (
            "central_london",
            ["51.5030", "-0.1395", "51.5160", "-0.1160"],
            "central-london",
            "Central London",
        ),
        (
            "central_tokyo",
            ["35.6740", "139.7520", "35.6865", "139.7750"],
            "central-tokyo",
            "Central Tokyo",
        ),
    ]
    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        for folder, bbox, dataset_id, display_name in cities:
            generated = temporary / folder
            subprocess.run(
                [
                    sys.executable,
                    "tools/build_geobeacon_tiles.py",
                    "--source",
                    f"data/{folder}/source.osm",
                    "--output",
                    str(generated),
                    "--bbox",
                    *bbox,
                    "--tile-size-m",
                    "128",
                    "--dataset-id",
                    dataset_id,
                    "--display-name",
                    display_name,
                ],
                cwd=root,
                check=True,
            )
            compare_trees(root / "data" / folder / "generated", generated)
    print("Checked city tile databases regenerate byte-for-byte")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
