#!/usr/bin/env python3
"""Vulkax Atlas normalized HTTP/JSON gateway with deterministic replay mode."""

from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
import os
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse


def _distance_squared(position: list[float], latitude: float, longitude: float) -> float:
    return (position[0] - latitude) ** 2 + (position[1] - longitude) ** 2


class AtlasGateway:
    def __init__(self, replay_path: Path, content_root: Path | None = None) -> None:
        self.replay_path = replay_path.resolve()
        self.content_root = content_root.resolve() if content_root else None
        self.replay = json.loads(self.replay_path.read_text(encoding="utf-8"))
        if self.replay.get("format") != "Vulkax-Atlas-navigation-replay-1":
            raise ValueError("unsupported Atlas navigation replay format")

    def status(self) -> dict[str, Any]:
        return {
            "service": "Vulkax Atlas Gateway",
            "apiVersion": "v1",
            "mode": "deterministic-replay",
            "capabilities": [
                "search",
                "reverse",
                "route",
                "transit",
                "traffic",
                "range-content",
            ],
            "upstreams": {
                "pelias": bool(os.getenv("PELIAS_URL")),
                "valhalla": bool(os.getenv("VALHALLA_URL")),
                "otp": bool(os.getenv("OTP_URL")),
                "traffic": bool(os.getenv("TOMTOM_API_KEY")),
            },
        }

    def search(self, query: str, limit: int) -> list[dict[str, Any]]:
        normalized = query.casefold()
        matches = [
            item
            for item in self.replay.get("search", [])
            if not normalized
            or normalized in item.get("name", "").casefold()
            or normalized in item.get("subtitle", "").casefold()
        ]
        matches.sort(key=lambda item: item.get("confidence", 0.0), reverse=True)
        return matches[: max(1, min(limit, 50))]

    def reverse(self, latitude: float, longitude: float) -> dict[str, Any] | None:
        candidates = self.replay.get("search", [])
        if not candidates:
            return None
        return min(
            candidates,
            key=lambda item: _distance_squared(
                item["position"], latitude, longitude
            ),
        )

    def routes(self, mode: str, alternatives: int) -> list[dict[str, Any]]:
        routes = [
            route
            for route in self.replay.get("routes", [])
            if route.get("mode") == mode
        ]
        return routes[: max(1, min(alternatives + 1, 4))]

    def transit(self) -> list[dict[str, Any]]:
        return self.replay.get("transit", [])

    def traffic(self) -> dict[str, Any]:
        return {
            "observedAt": "recorded-fixture",
            "segments": self.replay.get("traffic", []),
        }

    def content(self, relative_path: str) -> tuple[Path, str]:
        if self.content_root is None:
            raise FileNotFoundError("content serving is disabled")
        candidate = (self.content_root / relative_path).resolve()
        if self.content_root not in candidate.parents and candidate != self.content_root:
            raise PermissionError("content path escapes configured root")
        if not candidate.is_file():
            raise FileNotFoundError(relative_path)
        digest = hashlib.sha256(candidate.read_bytes()).hexdigest()
        return candidate, f'"{digest}"'


def make_handler(gateway: AtlasGateway) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "VulkaxAtlasGateway/1.0"

        def log_message(self, format_string: str, *args: object) -> None:
            if os.getenv("ATLAS_GATEWAY_QUIET") != "1":
                super().log_message(format_string, *args)

        def _json(self, status: HTTPStatus, payload: Any) -> None:
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _body(self) -> dict[str, Any]:
            try:
                size = int(self.headers.get("Content-Length", "0"))
            except ValueError as error:
                raise ValueError("invalid Content-Length") from error
            if size > 1024 * 1024:
                raise ValueError("request body exceeds 1 MiB")
            return json.loads(self.rfile.read(size) or b"{}")

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            query = parse_qs(parsed.query)
            try:
                if parsed.path == "/v1/status":
                    self._json(HTTPStatus.OK, gateway.status())
                elif parsed.path == "/v1/search":
                    self._json(
                        HTTPStatus.OK,
                        {
                            "results": gateway.search(
                                query.get("q", [""])[0],
                                int(query.get("limit", ["10"])[0]),
                            )
                        },
                    )
                elif parsed.path == "/v1/reverse":
                    result = gateway.reverse(
                        float(query["lat"][0]), float(query["lon"][0])
                    )
                    self._json(
                        HTTPStatus.OK if result else HTTPStatus.NOT_FOUND,
                        {"result": result},
                    )
                elif parsed.path == "/v1/traffic":
                    self._json(HTTPStatus.OK, gateway.traffic())
                elif parsed.path.startswith("/v1/content/"):
                    self._serve_content(parsed.path.removeprefix("/v1/content/"))
                else:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "unknown endpoint"})
            except (KeyError, ValueError) as error:
                self._json(HTTPStatus.BAD_REQUEST, {"error": str(error)})
            except PermissionError as error:
                self._json(HTTPStatus.FORBIDDEN, {"error": str(error)})
            except FileNotFoundError as error:
                self._json(HTTPStatus.NOT_FOUND, {"error": str(error)})

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            try:
                payload = self._body()
                if parsed.path == "/v1/route":
                    self._json(
                        HTTPStatus.OK,
                        {
                            "routes": gateway.routes(
                                payload.get("mode", "driving"),
                                int(payload.get("alternatives", 2)),
                            )
                        },
                    )
                elif parsed.path == "/v1/transit":
                    self._json(
                        HTTPStatus.OK, {"itineraries": gateway.transit()}
                    )
                else:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "unknown endpoint"})
            except (json.JSONDecodeError, ValueError) as error:
                self._json(HTTPStatus.BAD_REQUEST, {"error": str(error)})

        def _serve_content(self, relative_path: str) -> None:
            path, etag = gateway.content(relative_path)
            if self.headers.get("If-None-Match") == etag:
                self.send_response(HTTPStatus.NOT_MODIFIED)
                self.send_header("ETag", etag)
                self.end_headers()
                return
            size = path.stat().st_size
            start, end = 0, size - 1
            status = HTTPStatus.OK
            range_header = self.headers.get("Range")
            if range_header:
                if not range_header.startswith("bytes=") or "," in range_header:
                    self._json(
                        HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE,
                        {"error": "only one byte range is supported"},
                    )
                    return
                first, last = range_header[6:].split("-", 1)
                start = int(first) if first else 0
                end = int(last) if last else size - 1
                if start < 0 or end < start or start >= size:
                    self._json(
                        HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE,
                        {"error": "range outside content"},
                    )
                    return
                end = min(end, size - 1)
                status = HTTPStatus.PARTIAL_CONTENT
            length = end - start + 1
            self.send_response(status)
            self.send_header(
                "Content-Type",
                mimetypes.guess_type(path.name)[0] or "application/octet-stream",
            )
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("ETag", etag)
            self.send_header("Content-Length", str(length))
            if status == HTTPStatus.PARTIAL_CONTENT:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers()
            with path.open("rb") as stream:
                stream.seek(start)
                self.wfile.write(stream.read(length))

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--listen", default=os.getenv("ATLAS_GATEWAY_LISTEN", "127.0.0.1")
    )
    parser.add_argument(
        "--port", type=int, default=int(os.getenv("ATLAS_GATEWAY_PORT", "8080"))
    )
    parser.add_argument(
        "--replay",
        type=Path,
        default=Path(
            os.getenv(
                "ATLAS_REPLAY",
                "data/atlas/navigation_replay.json",
            )
        ),
    )
    parser.add_argument(
        "--content-root",
        type=Path,
        default=Path(os.environ["ATLAS_CONTENT_ROOT"])
        if os.getenv("ATLAS_CONTENT_ROOT")
        else None,
    )
    arguments = parser.parse_args()
    gateway = AtlasGateway(arguments.replay, arguments.content_root)
    server = ThreadingHTTPServer(
        (arguments.listen, arguments.port), make_handler(gateway)
    )
    print(
        f"Vulkax Atlas Gateway listening on "
        f"http://{arguments.listen}:{arguments.port}"
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
