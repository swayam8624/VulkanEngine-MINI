#!/usr/bin/env python3

import json
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "services" / "atlas_gateway"))

from server import AtlasGateway, ThreadingHTTPServer, make_handler


class AtlasGatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        content = Path(cls.temporary.name)
        (content / "tile.bin").write_bytes(bytes(range(32)))
        gateway = AtlasGateway(
            ROOT / "data" / "atlas" / "navigation_replay.json", content
        )
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(gateway))
        cls.thread = threading.Thread(
            target=cls.server.serve_forever, daemon=True
        )
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)
        cls.temporary.cleanup()

    def json_request(self, path: str, payload=None):
        body = None if payload is None else json.dumps(payload).encode()
        request = Request(
            self.base + path,
            data=body,
            headers={"Content-Type": "application/json"},
        )
        with urlopen(request, timeout=2) as response:
            return response.status, json.load(response)

    def test_contract_endpoints(self):
        status, payload = self.json_request("/v1/status")
        self.assertEqual(status, 200)
        self.assertIn("route", payload["capabilities"])

        _, payload = self.json_request("/v1/search?q=gate")
        self.assertEqual(payload["results"][0]["name"], "India Gate")

        _, payload = self.json_request("/v1/reverse?lat=28.613&lon=77.23")
        self.assertEqual(payload["result"]["name"], "India Gate")

        _, payload = self.json_request(
            "/v1/route", {"mode": "driving", "alternatives": 0}
        )
        self.assertEqual(payload["routes"][0]["id"], "route-delhi-1")

        _, payload = self.json_request("/v1/transit", {})
        self.assertTrue(payload["itineraries"][0]["realtimeTransit"])

        _, payload = self.json_request("/v1/traffic")
        self.assertEqual(payload["segments"][0]["currentSpeedKph"], 18.0)

    def test_range_and_etag_content(self):
        request = Request(
            self.base + "/v1/content/tile.bin", headers={"Range": "bytes=4-9"}
        )
        with urlopen(request, timeout=2) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(response.read(), bytes(range(4, 10)))
            etag = response.headers["ETag"]

        request = Request(
            self.base + "/v1/content/tile.bin",
            headers={"If-None-Match": etag},
        )
        with self.assertRaises(HTTPError) as context:
            urlopen(request, timeout=2)
        self.assertEqual(context.exception.code, 304)
        context.exception.close()


if __name__ == "__main__":
    unittest.main()
