#!/usr/bin/env python3

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    builder = Path(sys.argv[1]).resolve()
    regions = (
        "delhi-ncr",
        "greater-london",
        "tokyo-metro",
        "new-york-metro",
        "swiss-alps",
    )
    with tempfile.TemporaryDirectory() as temporary:
        output_root = Path(temporary)
        for region in regions:
            source = root / "config" / "atlas" / "regions" / f"{region}.json"
            checked = (
                root
                / "data"
                / "atlas"
                / "regions"
                / region
                / "atlas-dataset.json"
            )
            generated = output_root / region / "atlas-dataset.json"
            subprocess.run(
                [builder, "generate-manifest", source, generated],
                check=True,
            )
            subprocess.run([builder, "validate", generated], check=True)
            if generated.read_bytes() != checked.read_bytes():
                raise AssertionError(
                    f"{region} Atlas manifest is not byte-for-byte reproducible"
                )
            manifest = json.loads(generated.read_text(encoding="utf-8"))
            if manifest["formatVersion"] != 2 or len(manifest["layers"]) != 10:
                raise AssertionError(f"{region} manifest is incomplete")
            tileset = generated.with_name("tileset.json")
            tileset_json = json.loads(tileset.read_text(encoding="utf-8"))
            if len(tileset_json["root"]["children"]) != 6:
                raise AssertionError(f"{region} does not have six cube roots")
    print("Vulkax Atlas reference manifests are reproducible")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
