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

    def test_central_london_dataset_glbs_reload(self):
        root = ROOT / "data/central_london"
        manifest_path = root / "generated/geobeacon.json"
        manifest = json.loads(manifest_path.read_text())
        report = json.loads((root / "generated/tile_report.json").read_text())
        self.assertEqual(manifest["datasetId"], "central-london")
        self.assertEqual(manifest["displayName"], "Central London")
        self.assertGreater(len(manifest["tiles"]), 150)
        self.assertGreater(report["featureCount"], 8000)
        self.assertEqual(report["invalidFeatures"], {})
        self.assertEqual(
            hashlib.sha256((root / "source.osm").read_bytes()).hexdigest(),
            manifest["sourceChecksum"],
        )
        for tile in manifest["tiles"]:
            for representation in tile["representations"]:
                path = manifest_path.parent / representation["uri"]
                vertices, indices = TILES.glb_counts(path)
                self.assertGreater(vertices, 0)
                self.assertGreater(indices, 0)
                self.assertEqual(path.stat().st_size, representation["bytes"])

    def test_central_tokyo_dataset_glbs_reload(self):
        root = ROOT / "data/central_tokyo"
        manifest_path = root / "generated/geobeacon.json"
        manifest = json.loads(manifest_path.read_text())
        report = json.loads((root / "generated/tile_report.json").read_text())
        self.assertEqual(manifest["datasetId"], "central-tokyo")
        self.assertEqual(manifest["displayName"], "Central Tokyo")
        self.assertGreater(len(manifest["tiles"]), 200)
        self.assertGreater(report["featureCount"], 5000)
        self.assertEqual(report["invalidFeatures"], {})
        self.assertEqual(
            hashlib.sha256((root / "source.osm").read_bytes()).hexdigest(),
            manifest["sourceChecksum"],
        )
        for tile in manifest["tiles"]:
            for representation in tile["representations"]:
                path = manifest_path.parent / representation["uri"]
                vertices, indices = TILES.glb_counts(path)
                self.assertGreater(vertices, 0)
                self.assertGreater(indices, 0)
                self.assertEqual(path.stat().st_size, representation["bytes"])

    def test_checked_city_registry(self):
        registry = json.loads((ROOT / "data/cities.json").read_text())
        self.assertEqual(registry["format"], "Vulkax-city-registry-1")
        self.assertEqual(
            [city["id"] for city in registry["cities"]],
            ["connaught-place", "central-london", "central-tokyo"],
        )
        for city in registry["cities"]:
            self.assertEqual(len(city["center"]), 3)
            manifest = json.loads(
                (ROOT / "data" / city["manifest"]).read_text()
            )
            navigation = json.loads(
                (ROOT / "data" / city["navigation"]).read_text()
            )
            self.assertEqual(manifest["datasetId"], city["id"])
            self.assertEqual(manifest["displayName"], city["displayName"])
            self.assertEqual(navigation["region"], city["id"])
            self.assertEqual(navigation["displayName"], city["displayName"])
            self.assertAlmostEqual(
                manifest["originWgs84"][0], city["center"][0], places=5
            )
            self.assertAlmostEqual(
                manifest["originWgs84"][1], city["center"][1], places=5
            )


if __name__ == "__main__":
    unittest.main()
