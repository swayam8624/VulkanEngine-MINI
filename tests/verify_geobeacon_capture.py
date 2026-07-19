#!/usr/bin/env python3
"""Reject smoke captures that do not contain a resident GeoBEACON city scene."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    with path.open("rb") as stream:
        if stream.readline().strip() != b"P6":
            raise AssertionError("capture is not binary PPM")
        dimensions = stream.readline().split()
        while dimensions and dimensions[0].startswith(b"#"):
            dimensions = stream.readline().split()
        width, height = map(int, dimensions)
        if stream.readline().strip() != b"255":
            raise AssertionError("capture has unsupported color range")
        pixels = stream.read()
    if len(pixels) != width * height * 3:
        raise AssertionError("capture byte count is invalid")
    return width, height, pixels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result", type=Path)
    args = parser.parse_args()

    with (args.result / "frames.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise AssertionError("GeoBEACON smoke produced no measured frame")
    row = rows[-1]
    if int(row["geoVisibleTiles"]) <= 0 or int(row["geoResidentTiles"]) <= 0:
        raise AssertionError("default scene has no visible/resident GeoBEACON tiles")
    if int(row["atlasSelectedTiles"]) != 0:
        raise AssertionError("GeoBEACON smoke accidentally entered globe mode")

    width, height, pixels = read_ppm(args.result / "test.ppm")
    luminance = [
        (pixels[index] + pixels[index + 1] + pixels[index + 2]) / 3.0
        for index in range(0, len(pixels), 3)
    ]
    non_black = sum(value > 4.0 for value in luminance) / len(luminance)
    mean = sum(luminance) / len(luminance)
    variance = sum((value - mean) ** 2 for value in luminance) / len(luminance)
    if non_black < 0.08:
        raise AssertionError(f"capture is mostly blank: non-black={non_black:.3f}")
    if variance < 20.0:
        raise AssertionError(f"capture lacks scene variation: variance={variance:.3f}")
    print(
        f"Verified {width}x{height} GeoBEACON capture: "
        f"visible={row['geoVisibleTiles']} resident={row['geoResidentTiles']} "
        f"nonBlack={non_black:.3f} variance={variance:.1f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
