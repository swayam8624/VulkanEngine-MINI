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
    configurations = [
        {
            "directory": "connaught_place",
            "region": "connaught-place",
            "display": "Connaught Place",
            "subtitle": "Connaught Place, New Delhi",
            "minimums": (3000, 6000, 200),
        },
        {
            "directory": "central_london",
            "region": "central-london",
            "display": "Central London",
            "subtitle": "Central London, United Kingdom",
            "minimums": (14000, 30000, 5000),
        },
        {
            "directory": "central_tokyo",
            "region": "central-tokyo",
            "display": "Central Tokyo",
            "subtitle": "Central Tokyo, Japan",
            "minimums": (10000, 22000, 3000),
        },
    ]
    with tempfile.TemporaryDirectory() as directory:
        for configuration in configurations:
            dataset = root / "data" / configuration["directory"]
            checked = dataset / "navigation.json"
            generated = Path(directory) / f"{configuration['region']}.json"
            subprocess.run(
                [
                    sys.executable,
                    str(root / "tools/build_connaught_navigation.py"),
                    "--source",
                    str(dataset / "source.osm"),
                    "--output",
                    str(generated),
                    "--region",
                    configuration["region"],
                    "--display-name",
                    configuration["display"],
                    "--subtitle",
                    configuration["subtitle"],
                ],
                check=True,
            )
            if generated.read_bytes() != checked.read_bytes():
                raise AssertionError(
                    f"{configuration['display']} navigation graph checksum drift"
                )
            data = json.loads(checked.read_text(encoding="utf-8"))
            minimum_nodes, minimum_edges, minimum_places = configuration["minimums"]
            assert len(data["nodes"]) > minimum_nodes
            assert len(data["edges"]) > minimum_edges
            assert len(data["places"]) > minimum_places
            assert data["displayName"] == configuration["display"]
    print("Checked city navigation graphs are deterministic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
