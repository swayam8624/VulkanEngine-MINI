#!/usr/bin/env python3

import hashlib
import importlib.util
import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("build_geobeacon_tiles", ROOT / "tools/build_geobeacon_tiles.py")
TILES = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TILES
SPEC.loader.exec_module(TILES)


class GeoBeaconToolsTests(unittest.TestCase):
    def test_local_conversion(self):
        x, y = TILES.local_xy(28.6365, 77.2245, 28.63175, 77.21575)
        self.assertAlmostEqual(x, 854.3, delta=2.0)
        self.assertAlmostEqual(y, 528.8, delta=2.0)

    def test_height_precedence(self):
        self.assertAlmostEqual(TILES.building_height({"height": "18 m", "building:levels": "10"}), 18.0)
        self.assertAlmostEqual(TILES.building_height({"building:levels": "5"}), 16.0)

    def test_triangulation_orientation(self):
        points, triangles = TILES.triangulate([(0, 0), (0, 1), (1, 1), (1, 0), (0, 0)])
        self.assertEqual(len(points), 4)
        self.assertEqual(len(triangles), 2)

    def test_checked_dataset_glbs_reload(self):
        manifest_path = ROOT / "data/connaught_place/generated/geobeacon.json"
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(manifest["attribution"], "© OpenStreetMap contributors")
        self.assertEqual(len(manifest["tiles"]), 151)
        for tile in manifest["tiles"]:
            for representation in tile["representations"]:
                path = manifest_path.parent / representation["uri"]
                vertices, indices = TILES.glb_counts(path)
                self.assertGreater(vertices, 0)
                self.assertGreater(indices, 0)
                self.assertEqual(path.stat().st_size, representation["bytes"])

    def test_source_checksum(self):
        source = ROOT / "data/connaught_place/source.osm"
        manifest = json.loads((ROOT / "data/connaught_place/generated/geobeacon.json").read_text())
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), manifest["sourceChecksum"])


if __name__ == "__main__":
    unittest.main()
