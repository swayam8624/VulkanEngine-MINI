#!/usr/bin/env python3
"""Fetch the canonical GeoBEACON OSM extract through the read-only Overpass API."""

from __future__ import annotations

import argparse
import hashlib
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_ENDPOINT = "https://overpass-api.de/api/interpreter"
DEFAULT_BBOX = (28.6270, 77.2070, 28.6365, 77.2245)
USER_AGENT = "GeoBEACON-Research/1.0 (https://github.com/swayam8624/VulkanEngine-MINI)"


def query_for(bbox: tuple[float, float, float, float]) -> str:
    south, west, north, east = bbox
    bounds = f"{south},{west},{north},{east}"
    return f"""[out:xml][timeout:120];
(
  way["building"]({bounds});
  relation["building"]({bounds});
  way["highway"]({bounds});
  relation["highway"]({bounds});
  way["landuse"]({bounds});
  relation["landuse"]({bounds});
  way["natural"="water"]({bounds});
  relation["natural"="water"]({bounds});
  way["leisure"="park"]({bounds});
  relation["leisure"="park"]({bounds});
  node["public_transport"]({bounds});
  way["public_transport"]({bounds});
  node["barrier"]({bounds});
  way["barrier"]({bounds});
);
(._;>;);
out body;"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    parser.add_argument(
        "--bbox",
        nargs=4,
        type=float,
        metavar=("SOUTH", "WEST", "NORTH", "EAST"),
        default=DEFAULT_BBOX,
    )
    parser.add_argument("--output", type=Path, default=Path("data/connaught_place/source.osm"))
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--max-bytes", type=int, default=100 * 1024 * 1024)
    args = parser.parse_args()

    output = args.output
    metadata_path = output.with_suffix(".source.json")
    if output.exists() and not args.force:
        print(f"using cached extract: {output}")
        return 0

    query = query_for(tuple(args.bbox))
    request = urllib.request.Request(
        args.endpoint,
        data=urllib.parse.urlencode({"data": query}).encode(),
        headers={"User-Agent": USER_AGENT, "Accept": "application/xml"},
        method="POST",
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    last_error: Exception | None = None
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                content_length = int(response.headers.get("Content-Length", 0))
                if content_length > args.max_bytes:
                    raise RuntimeError(f"response exceeds --max-bytes: {content_length}")
                payload = response.read(args.max_bytes + 1)
                if len(payload) > args.max_bytes:
                    raise RuntimeError("response exceeded --max-bytes while downloading")
            if not payload.startswith(b"<?xml"):
                raise RuntimeError("Overpass response is not OSM XML")
            output.write_bytes(payload)
            metadata = {
                "endpoint": args.endpoint,
                "query": query,
                "bbox": list(args.bbox),
                "fetchedUtc": datetime.now(timezone.utc).isoformat(),
                "sha256": hashlib.sha256(payload).hexdigest(),
                "bytes": len(payload),
                "userAgent": USER_AGENT,
                "license": "ODbL-1.0",
                "attribution": "© OpenStreetMap contributors",
                "copyrightUrl": "https://www.openstreetmap.org/copyright",
            }
            metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
            print(f"wrote {output} ({len(payload):,} bytes)")
            return 0
        except (urllib.error.URLError, TimeoutError, RuntimeError) as error:
            last_error = error
            if attempt == 3:
                break
            delay = 5 * (2**attempt)
            print(f"attempt {attempt + 1} failed: {error}; retrying in {delay}s")
            time.sleep(delay)
    raise RuntimeError(f"unable to fetch OSM extract: {last_error}")


if __name__ == "__main__":
    raise SystemExit(main())
