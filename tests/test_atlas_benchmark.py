#!/usr/bin/env python3

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary)
        subprocess.run(
            [binary, "--frames", "240", "--output", output], check=True
        )
        summary = json.loads(
            (output / "summary.json").read_text(encoding="utf-8")
        )
        if summary["measurementClass"] != "analytical-scheduler-simulation":
            raise AssertionError("scheduler estimates must remain explicitly classified")
        policies = {
            item["policy"]: item for item in summary["policies"]
        }
        if policies["full-atlas"]["deadlineMisses"] >= policies["distance-only"][
            "deadlineMisses"
        ]:
            raise AssertionError(
                "route-aware stress fixture no longer distinguishes the controller"
            )
        if policies["route-only"]["deadlineMisses"] >= policies["distance-only"][
            "deadlineMisses"
        ]:
            raise AssertionError("route-only ablation lost its expected prefetch behavior")
        if not (output / "frames.csv").is_file():
            raise AssertionError("raw Atlas frame rows were not written")
    print("Vulkax Atlas research benchmark passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
