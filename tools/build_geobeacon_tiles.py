#!/usr/bin/env python3
"""Convert an OSM XML neighborhood into deterministic GeoBEACON GLB/3D Tiles assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

EARTH_RADIUS_M = 6_378_137.0
SEMANTIC = {
    "ground": 0,
    "building": 1,
    "landmark": 2,
    "primary_road": 3,
    "intersection": 4,
    "secondary_road": 5,
    "pedestrian": 6,
    "vegetation": 7,
    "water": 8,
}
COLORS = {
    0: (0.34, 0.38, 0.34),
    1: (0.62, 0.66, 0.70),
    2: (0.88, 0.70, 0.28),
    3: (0.16, 0.19, 0.22),
    4: (0.92, 0.36, 0.22),
    5: (0.25, 0.27, 0.29),
    6: (0.52, 0.48, 0.40),
    7: (0.18, 0.48, 0.23),
    8: (0.12, 0.42, 0.70),
}


@dataclass
class Feature:
    osm_id: str
    semantic: int
    points: list[tuple[float, float]]
    tags: dict[str, str]
    holes: list[list[tuple[float, float]]] = field(default_factory=list)


@dataclass
class Mesh:
    vertices: list[tuple[float, ...]] = field(default_factory=list)
    indices: list[int] = field(default_factory=list)
    semantic_counts: Counter = field(default_factory=Counter)

    def vertex(self, p, normal, color, semantic):
        self.vertices.append((*p, *normal, *color, float(semantic)))
        return len(self.vertices) - 1


def local_xy(lat: float, lon: float, lat0: float, lon0: float) -> tuple[float, float]:
    lat_r = math.radians(lat)
    lat0_r = math.radians(lat0)
    x = math.radians(lon - lon0) * EARTH_RADIUS_M * math.cos(lat0_r)
    y = (lat_r - lat0_r) * EARTH_RADIUS_M
    return x, y


def polygon_area(points):
    return 0.5 * sum(
        points[i][0] * points[(i + 1) % len(points)][1]
        - points[(i + 1) % len(points)][0] * points[i][1]
        for i in range(len(points))
    )


def point_in_triangle(p, a, b, c):
    def cross(u, v, w):
        return (v[0] - u[0]) * (w[1] - u[1]) - (v[1] - u[1]) * (w[0] - u[0])

    d1, d2, d3 = cross(p, a, b), cross(p, b, c), cross(p, c, a)
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def triangulate(points):
    points = points[:-1] if len(points) > 2 and points[0] == points[-1] else points[:]
    if len(points) < 3:
        return []
    if polygon_area(points) < 0:
        points.reverse()
    remaining = list(range(len(points)))
    triangles = []
    guard = len(points) * len(points)
    while len(remaining) > 2 and guard:
        guard -= 1
        clipped = False
        for k, current in enumerate(remaining):
            prev = remaining[k - 1]
            nxt = remaining[(k + 1) % len(remaining)]
            a, b, c = points[prev], points[current], points[nxt]
            cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
            if cross <= 1e-8:
                continue
            if any(
                point_in_triangle(points[index], a, b, c)
                for index in remaining
                if index not in (prev, current, nxt)
            ):
                continue
            triangles.append((prev, current, nxt))
            del remaining[k]
            clipped = True
            break
        if not clipped:
            break
    return points, triangles


def parse_number(value: str | None, default: float) -> float:
    if not value:
        return default
    cleaned = value.lower().replace("meters", "").replace("metres", "").replace("m", "").strip()
    try:
        return float(cleaned.split(";")[0])
    except ValueError:
        return default


def building_height(tags):
    kind = tags.get("building", "")
    default = 18.0 if kind in {"commercial", "office", "retail"} else 12.0
    if "height" in tags:
        return max(2.0, parse_number(tags["height"], default))
    if "building:levels" in tags:
        return max(3.2, parse_number(tags["building:levels"], default / 3.2) * 3.2)
    return default


def road_width(tags):
    kind = tags.get("highway", "")
    defaults = {
        "motorway": 14.0,
        "trunk": 12.0,
        "primary": 10.0,
        "secondary": 8.0,
        "tertiary": 7.0,
        "residential": 5.5,
        "service": 4.0,
        "footway": 2.0,
        "pedestrian": 4.0,
        "path": 1.5,
    }
    if "width" in tags:
        return max(0.5, parse_number(tags["width"], defaults.get(kind, 4.0)))
    lanes = parse_number(tags.get("lanes"), 0.0)
    return max(defaults.get(kind, 4.0), lanes * 3.2)


def semantic_for(tags):
    if "building" in tags:
        if tags.get("historic") or tags.get("tourism") == "attraction" or tags.get("building") in {"monument", "government"}:
            return SEMANTIC["landmark"]
        return SEMANTIC["building"]
    highway = tags.get("highway")
    if highway:
        if highway in {"primary", "trunk", "motorway"}:
            return SEMANTIC["primary_road"]
        if highway in {"footway", "pedestrian", "path", "steps"}:
            return SEMANTIC["pedestrian"]
        return SEMANTIC["secondary_road"]
    if tags.get("natural") == "water" or tags.get("water"):
        return SEMANTIC["water"]
    if tags.get("leisure") == "park" or tags.get("landuse") in {"grass", "forest", "recreation_ground"}:
        return SEMANTIC["vegetation"]
    return SEMANTIC["ground"]


def read_osm(path: Path, origin):
    tree = ET.parse(path)
    root = tree.getroot()
    lat0, lon0 = origin
    nodes = {}
    node_degree = Counter()
    ways = {}
    for element in root:
        if element.tag == "node":
            nodes[element.attrib["id"]] = local_xy(
                float(element.attrib["lat"]), float(element.attrib["lon"]), lat0, lon0
            )
        elif element.tag == "way":
            refs = [child.attrib["ref"] for child in element if child.tag == "nd"]
            tags = {child.attrib["k"]: child.attrib["v"] for child in element if child.tag == "tag"}
            ways[element.attrib["id"]] = (refs, tags)
            if "highway" in tags:
                node_degree.update(refs)

    features = []
    consumed = set()
    invalid = Counter()
    for relation in root.findall("relation"):
        tags = {child.attrib["k"]: child.attrib["v"] for child in relation if child.tag == "tag"}
        if tags.get("type") != "multipolygon":
            continue
        members = [
            (child.attrib.get("ref"), child.attrib.get("role", "outer"))
            for child in relation
            if child.tag == "member" and child.attrib.get("type") == "way"
        ]
        rings = defaultdict(list)
        for ref, role in members:
            if ref in ways:
                rings[role].append(ways[ref][0])
                consumed.add(ref)

        def join(parts):
            output = []
            unused = [part[:] for part in parts if part]
            while unused:
                ring = unused.pop(0)
                changed = True
                while changed and ring[0] != ring[-1]:
                    changed = False
                    for i, part in enumerate(unused):
                        if ring[-1] == part[0]:
                            ring.extend(part[1:])
                        elif ring[-1] == part[-1]:
                            ring.extend(reversed(part[:-1]))
                        elif ring[0] == part[-1]:
                            ring = part[:-1] + ring
                        elif ring[0] == part[0]:
                            ring = list(reversed(part[1:])) + ring
                        else:
                            continue
                        del unused[i]
                        changed = True
                        break
                output.append(ring)
            return output

        outer_rings = join(rings["outer"])
        inner_rings = join(rings["inner"])
        for ring in outer_rings:
            points = [nodes[ref] for ref in ring if ref in nodes]
            if len(points) < 4 or points[0] != points[-1]:
                invalid["open_multipolygon"] += 1
                continue
            holes = [
                [nodes[ref] for ref in inner if ref in nodes]
                for inner in inner_rings
                if len(inner) >= 4 and inner[0] == inner[-1]
            ]
            features.append(Feature(f"r{relation.attrib['id']}", semantic_for(tags), points, tags, holes))

    for way_id, (refs, tags) in ways.items():
        if way_id in consumed:
            continue
        points = [nodes[ref] for ref in refs if ref in nodes]
        if len(points) < 2:
            invalid["missing_way_nodes"] += 1
            continue
        relevant = any(key in tags for key in ("building", "highway", "landuse", "natural", "leisure"))
        if not relevant:
            continue
        if "highway" not in tags and (len(points) < 4 or points[0] != points[-1]):
            invalid["open_polygon"] += 1
            continue
        features.append(Feature(f"w{way_id}", semantic_for(tags), points, tags))

    intersections = [nodes[node_id] for node_id, degree in node_degree.items() if degree >= 3 and node_id in nodes]
    return features, intersections, invalid


def add_building(mesh, feature, lod):
    triangulated = triangulate(feature.points)
    if not triangulated:
        return
    points, triangles = triangulated
    semantic = feature.semantic
    color = COLORS[semantic]
    min_height = parse_number(feature.tags.get("min_height"), 0.0)
    if "building:min_level" in feature.tags:
        min_height = max(min_height, parse_number(feature.tags["building:min_level"], 0.0) * 3.2)
    height = building_height(feature.tags)
    if lod == 0:
        min_x = min(p[0] for p in points)
        max_x = max(p[0] for p in points)
        min_y = min(p[1] for p in points)
        max_y = max(p[1] for p in points)
        points = [(min_x, min_y), (max_x, min_y), (max_x, max_y), (min_x, max_y)]
        triangles = [(0, 1, 2), (0, 2, 3)]
        height *= 0.9
    roof_y = -height
    base_y = -min_height
    roof_indices = [mesh.vertex((x, roof_y, y), (0, -1, 0), color, semantic) for x, y in points]
    for a, b, c in triangles:
        mesh.indices.extend((roof_indices[a], roof_indices[b], roof_indices[c]))
    if lod >= 1:
        for i, p0 in enumerate(points):
            p1 = points[(i + 1) % len(points)]
            dx, dz = p1[0] - p0[0], p1[1] - p0[1]
            length = math.hypot(dx, dz)
            if length < 1e-5:
                continue
            normal = (dz / length, 0.0, -dx / length)
            ids = [
                mesh.vertex((p0[0], base_y, p0[1]), normal, color, semantic),
                mesh.vertex((p1[0], base_y, p1[1]), normal, color, semantic),
                mesh.vertex((p1[0], roof_y, p1[1]), normal, color, semantic),
                mesh.vertex((p0[0], roof_y, p0[1]), normal, color, semantic),
            ]
            mesh.indices.extend((ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]))
    mesh.semantic_counts[semantic] += 1


def add_road(mesh, feature, lod):
    kind = feature.tags.get("highway", "")
    if lod == 0 and kind not in {"primary", "trunk", "motorway", "secondary"}:
        return
    if lod == 1 and kind in {"footway", "path", "steps"}:
        return
    semantic = feature.semantic
    color = COLORS[semantic]
    half_width = road_width(feature.tags) * 0.5
    layer = parse_number(feature.tags.get("layer"), 0.0)
    elevation = -(layer * 0.5 + (0.15 if feature.tags.get("bridge") == "yes" else 0.03))
    for p0, p1 in zip(feature.points, feature.points[1:]):
        dx, dz = p1[0] - p0[0], p1[1] - p0[1]
        length = math.hypot(dx, dz)
        if length < 1e-5:
            continue
        ox, oz = -dz / length * half_width, dx / length * half_width
        ids = [
            mesh.vertex((p0[0] + ox, elevation, p0[1] + oz), (0, -1, 0), color, semantic),
            mesh.vertex((p0[0] - ox, elevation, p0[1] - oz), (0, -1, 0), color, semantic),
            mesh.vertex((p1[0] - ox, elevation, p1[1] - oz), (0, -1, 0), color, semantic),
            mesh.vertex((p1[0] + ox, elevation, p1[1] + oz), (0, -1, 0), color, semantic),
        ]
        mesh.indices.extend((ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]))
    mesh.semantic_counts[semantic] += 1


def add_area(mesh, feature, lod):
    if lod == 0 and feature.semantic not in (SEMANTIC["water"], SEMANTIC["vegetation"]):
        return
    triangulated = triangulate(feature.points)
    if not triangulated:
        return
    points, triangles = triangulated
    semantic = feature.semantic
    color = COLORS[semantic]
    elevation = -0.01 if semantic != SEMANTIC["water"] else 0.0
    ids = [mesh.vertex((x, elevation, y), (0, -1, 0), color, semantic) for x, y in points]
    for a, b, c in triangles:
        mesh.indices.extend((ids[a], ids[b], ids[c]))
    mesh.semantic_counts[semantic] += 1


def add_tile_ground(mesh, bounds):
    min_x, min_z, max_x, max_z = bounds
    color = COLORS[SEMANTIC["ground"]]
    semantic = SEMANTIC["ground"]
    ids = [
        mesh.vertex((min_x, 0.02, min_z), (0, -1, 0), color, semantic),
        mesh.vertex((max_x, 0.02, min_z), (0, -1, 0), color, semantic),
        mesh.vertex((max_x, 0.02, max_z), (0, -1, 0), color, semantic),
        mesh.vertex((min_x, 0.02, max_z), (0, -1, 0), color, semantic),
    ]
    mesh.indices.extend((ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]))
    mesh.semantic_counts[semantic] += 1


def build_mesh(features, lod, bounds):
    mesh = Mesh()
    add_tile_ground(mesh, bounds)
    for feature in sorted(features, key=lambda value: value.osm_id):
        if "building" in feature.tags:
            add_building(mesh, feature, lod)
        elif "highway" in feature.tags:
            add_road(mesh, feature, lod)
        else:
            add_area(mesh, feature, lod)
    return mesh


def write_glb(path: Path, mesh: Mesh):
    vertex_blob = b"".join(struct.pack("<10f", *vertex) for vertex in mesh.vertices)
    index_offset = (len(vertex_blob) + 3) & ~3
    binary = vertex_blob + b"\x00" * (index_offset - len(vertex_blob))
    binary += b"".join(struct.pack("<I", index) for index in mesh.indices)
    binary += b"\x00" * ((4 - len(binary) % 4) % 4)
    if mesh.vertices:
        positions = [(v[0], v[1], v[2]) for v in mesh.vertices]
        minimum = [min(v[i] for v in positions) for i in range(3)]
        maximum = [max(v[i] for v in positions) for i in range(3)]
    else:
        minimum = maximum = [0.0, 0.0, 0.0]
    document = {
        "asset": {"version": "2.0", "generator": "GeoBEACON"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(vertex_blob), "byteStride": 40, "target": 34962},
            {
                "buffer": 0,
                "byteOffset": index_offset,
                "byteLength": len(mesh.indices) * 4,
                "target": 34963,
            },
        ],
        "accessors": [
            {
                "bufferView": 0,
                "byteOffset": 0,
                "componentType": 5126,
                "count": len(mesh.vertices),
                "type": "VEC3",
                "min": minimum,
                "max": maximum,
            },
            {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": len(mesh.vertices), "type": "VEC3"},
            {"bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": len(mesh.vertices), "type": "VEC3"},
            {"bufferView": 0, "byteOffset": 36, "componentType": 5126, "count": len(mesh.vertices), "type": "SCALAR"},
            {"bufferView": 1, "byteOffset": 0, "componentType": 5125, "count": len(mesh.indices), "type": "SCALAR"},
        ],
        "meshes": [
            {
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "COLOR_0": 2, "_SEMANTIC": 3},
                        "indices": 4,
                        "mode": 4,
                    }
                ],
                "extras": {"semanticCounts": dict(sorted(mesh.semantic_counts.items()))},
            }
        ],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    json_blob = json.dumps(document, separators=(",", ":"), sort_keys=True).encode()
    json_blob += b" " * ((4 - len(json_blob) % 4) % 4)
    total_length = 12 + 8 + len(json_blob) + 8 + len(binary)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        struct.pack("<4sII", b"glTF", 2, total_length)
        + struct.pack("<I4s", len(json_blob), b"JSON")
        + json_blob
        + struct.pack("<I4s", len(binary), b"BIN\x00")
        + binary
    )


def glb_counts(path):
    payload = path.read_bytes()
    if len(payload) < 20 or payload[:4] != b"glTF":
        raise ValueError(f"invalid GLB: {path}")
    json_length = struct.unpack_from("<I", payload, 12)[0]
    document = json.loads(payload[20 : 20 + json_length])
    return document["accessors"][0]["count"], document["accessors"][4]["count"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("data/connaught_place/source.osm"))
    parser.add_argument("--output", type=Path, default=Path("data/connaught_place/generated"))
    parser.add_argument("--bbox", nargs=4, type=float, default=(28.6270, 77.2070, 28.6365, 77.2245))
    parser.add_argument("--tile-size-m", type=float, default=128.0)
    args = parser.parse_args()
    south, west, north, east = args.bbox
    origin = ((south + north) * 0.5, (west + east) * 0.5)
    features, intersections, invalid = read_osm(args.source, origin)
    if not features:
        raise RuntimeError("OSM source produced no renderable features")
    tile_size = args.tile_size_m
    tiles = defaultdict(list)
    for feature in features:
        centroid = (
            sum(point[0] for point in feature.points) / len(feature.points),
            sum(point[1] for point in feature.points) / len(feature.points),
        )
        tile = (math.floor(centroid[0] / tile_size), math.floor(centroid[1] / tile_size))
        tiles[tile].append(feature)

    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    records = []
    report = {
        "source": str(args.source),
        "sourceSha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
        "originWgs84": [origin[0], origin[1], 0.0],
        "tileSizeMeters": tile_size,
        "featureCount": len(features),
        "intersectionCount": len(intersections),
        "invalidFeatures": dict(invalid),
        "semanticCounts": dict(Counter(feature.semantic for feature in features)),
        "tiles": [],
        "license": "ODbL-1.0",
        "attribution": "© OpenStreetMap contributors",
    }
    for tile_index, ((tx, ty), tile_features) in enumerate(sorted(tiles.items())):
        tile_id = ((tx & 0xFFFFFFFF) << 32) | (ty & 0xFFFFFFFF)
        bounds = [tx * tile_size, -120.0, ty * tile_size, (tx + 1) * tile_size, 10.0, (ty + 1) * tile_size]
        representations = []
        for lod in range(3):
            mesh = build_mesh(
                tile_features,
                lod,
                (tx * tile_size, ty * tile_size, (tx + 1) * tile_size, (ty + 1) * tile_size),
            )
            uri = f"tiles/{tile_id:016x}_lod{lod}.glb"
            path = output / uri
            write_glb(path, mesh)
            vertex_count, index_count = glb_counts(path)
            representations.append(
                {
                    "lod": lod,
                    "uri": uri,
                    "bytes": path.stat().st_size,
                    "vertexCount": vertex_count,
                    "indexCount": index_count,
                    "geometricError": [64.0, 16.0, 0.0][lod],
                }
            )
        importance = max(
            3.0 if feature.semantic == SEMANTIC["landmark"] else
            2.5 if feature.semantic == SEMANTIC["intersection"] else
            2.0 if feature.semantic == SEMANTIC["primary_road"] else 1.0
            for feature in tile_features
        )
        record = {
            "tileId": tile_id,
            "tileX": tx,
            "tileY": ty,
            "bounds": bounds,
            "semanticImportance": importance,
            "representations": representations,
        }
        records.append(record)
        report["tiles"].append(record)

    dataset = {
        "format": "GeoBEACON-runtime-1",
        "originWgs84": [origin[0], origin[1], 0.0],
        "boundsWgs84": [south, west, north, east],
        "tileSizeMeters": tile_size,
        "attribution": "© OpenStreetMap contributors",
        "copyrightUrl": "https://www.openstreetmap.org/copyright",
        "sourceChecksum": report["sourceSha256"],
        "tiles": records,
    }
    (output / "geobeacon.json").write_text(json.dumps(dataset, indent=2, sort_keys=True) + "\n")
    children = []
    for record in records:
        lo_x, lo_y, lo_z, hi_x, hi_y, hi_z = record["bounds"]
        cx, cy, cz = (lo_x + hi_x) / 2, (lo_y + hi_y) / 2, (lo_z + hi_z) / 2
        hx, hy, hz = (hi_x - lo_x) / 2, (hi_y - lo_y) / 2, (hi_z - lo_z) / 2
        lod2 = {
            "boundingVolume": {"box": [cx, cy, cz, hx, 0, 0, 0, hy, 0, 0, 0, hz]},
            "geometricError": 0,
            "refine": "REPLACE",
            "content": {"uri": record["representations"][2]["uri"]},
        }
        lod1 = {
            "boundingVolume": lod2["boundingVolume"],
            "geometricError": 16,
            "refine": "REPLACE",
            "content": {"uri": record["representations"][1]["uri"]},
            "children": [lod2],
        }
        children.append(
            {
                "boundingVolume": lod2["boundingVolume"],
                "geometricError": 64,
                "refine": "REPLACE",
                "content": {"uri": record["representations"][0]["uri"]},
                "children": [lod1],
                "extras": {"tileId": record["tileId"], "semanticImportance": record["semanticImportance"]},
            }
        )
    all_bounds = [record["bounds"] for record in records]
    lo_x = min(v[0] for v in all_bounds)
    lo_y = min(v[1] for v in all_bounds)
    lo_z = min(v[2] for v in all_bounds)
    hi_x = max(v[3] for v in all_bounds)
    hi_y = max(v[4] for v in all_bounds)
    hi_z = max(v[5] for v in all_bounds)
    root_box = [
        (lo_x + hi_x) / 2, (lo_y + hi_y) / 2, (lo_z + hi_z) / 2,
        (hi_x - lo_x) / 2, 0, 0, 0, (hi_y - lo_y) / 2, 0, 0, 0, (hi_z - lo_z) / 2,
    ]
    tileset = {
        "asset": {"version": "1.1", "tilesetVersion": "GeoBEACON-1"},
        "geometricError": 128,
        "root": {
            "boundingVolume": {"box": root_box},
            "geometricError": 128,
            "refine": "REPLACE",
            "children": children,
        },
        "extras": {
            "attribution": "© OpenStreetMap contributors",
            "sourceChecksum": report["sourceSha256"],
            "originWgs84": dataset["originWgs84"],
        },
    }
    (output / "tileset.json").write_text(json.dumps(tileset, indent=2, sort_keys=True) + "\n")
    (output / "tile_report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"generated {len(records)} tiles and {len(records) * 3} GLBs in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
