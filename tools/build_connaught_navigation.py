#!/usr/bin/env python3
"""Build the deterministic offline Connaught Place POI and routing graph."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET

DRIVING_EXCLUDED = {
    "bridleway",
    "construction",
    "corridor",
    "cycleway",
    "elevator",
    "footway",
    "path",
    "pedestrian",
    "platform",
    "proposed",
    "raceway",
    "steps",
}
WALKING_EXCLUDED = {"construction", "motorway", "motorway_link", "proposed", "raceway"}
CYCLING_EXCLUDED = {
    "construction",
    "corridor",
    "elevator",
    "motorway",
    "motorway_link",
    "proposed",
    "raceway",
    "steps",
}


def haversine(left: tuple[float, float], right: tuple[float, float]) -> float:
    radius = 6_371_008.8
    lat0, lon0 = map(math.radians, left)
    lat1, lon1 = map(math.radians, right)
    dlat = lat1 - lat0
    dlon = lon1 - lon0
    value = (
        math.sin(dlat * 0.5) ** 2
        + math.cos(lat0) * math.cos(lat1) * math.sin(dlon * 0.5) ** 2
    )
    return radius * 2.0 * math.atan2(math.sqrt(value), math.sqrt(max(0.0, 1.0 - value)))


def tags_for(element: ET.Element) -> dict[str, str]:
    return {
        tag.attrib["k"]: tag.attrib["v"]
        for tag in element.findall("tag")
        if "k" in tag.attrib and "v" in tag.attrib
    }


def category(tags: dict[str, str]) -> str:
    for key in ("amenity", "shop", "tourism", "historic", "railway", "highway", "building"):
        value = tags.get(key)
        if value:
            return f"{key}:{value}"
    return "place"


def mode_flags(highway: str, tags: dict[str, str]) -> int:
    flags = 0
    if highway not in DRIVING_EXCLUDED and tags.get("motor_vehicle") != "no":
        flags |= 1
    if highway not in WALKING_EXCLUDED and tags.get("foot") != "no":
        flags |= 2
    if highway not in CYCLING_EXCLUDED and tags.get("bicycle") != "no":
        flags |= 4
    return flags


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source_bytes = args.source.read_bytes()
    root = ET.fromstring(source_bytes)
    nodes: dict[str, tuple[float, float, dict[str, str]]] = {}
    ways: list[tuple[str, list[str], dict[str, str]]] = []
    places: list[dict[str, object]] = []

    for element in root:
        if element.tag == "node":
            node_id = element.attrib["id"]
            node_tags = tags_for(element)
            lat_lon = (float(element.attrib["lat"]), float(element.attrib["lon"]))
            nodes[node_id] = (*lat_lon, node_tags)
            if node_tags.get("name"):
                places.append(
                    {
                        "id": f"osm:n{node_id}",
                        "name": node_tags["name"],
                        "subtitle": "Connaught Place, New Delhi",
                        "category": category(node_tags),
                        "position": [lat_lon[0], lat_lon[1], 0.0],
                    }
                )
        elif element.tag == "way":
            way_id = element.attrib["id"]
            refs = [node.attrib["ref"] for node in element.findall("nd")]
            way_tags = tags_for(element)
            ways.append((way_id, refs, way_tags))
            valid = [nodes[ref] for ref in refs if ref in nodes]
            if way_tags.get("name") and valid:
                places.append(
                    {
                        "id": f"osm:w{way_id}",
                        "name": way_tags["name"],
                        "subtitle": "Connaught Place, New Delhi",
                        "category": category(way_tags),
                        "position": [
                            sum(item[0] for item in valid) / len(valid),
                            sum(item[1] for item in valid) / len(valid),
                            0.0,
                        ],
                    }
                )

    graph_node_ids: set[str] = set()
    raw_edges: list[tuple[str, str, float, int]] = []
    for _, refs, way_tags in ways:
        highway = way_tags.get("highway")
        if not highway:
            continue
        flags = mode_flags(highway, way_tags)
        if not flags:
            continue
        oneway = way_tags.get("oneway", "").lower()
        pairs = list(zip(refs, refs[1:]))
        if oneway == "-1":
            pairs = [(right, left) for left, right in pairs]
        for left, right in pairs:
            if left not in nodes or right not in nodes or left == right:
                continue
            graph_node_ids.update((left, right))
            distance = haversine(nodes[left][:2], nodes[right][:2])
            if distance < 0.01:
                continue
            raw_edges.append((left, right, distance, flags))
            if oneway not in {"yes", "1", "true", "-1"}:
                raw_edges.append((right, left, distance, flags))

    ordered_ids = sorted(graph_node_ids, key=int)
    node_index = {node_id: index for index, node_id in enumerate(ordered_ids)}
    graph_nodes = [
        [nodes[node_id][0], nodes[node_id][1]]
        for node_id in ordered_ids
    ]
    graph_edges = sorted(
        [
            [node_index[left], node_index[right], round(distance, 3), flags]
            for left, right, distance, flags in raw_edges
        ],
        key=lambda edge: (edge[0], edge[1], edge[3]),
    )

    deduplicated: dict[tuple[str, int, int], dict[str, object]] = {}
    for place in places:
        position = place["position"]
        key = (
            str(place["name"]).casefold(),
            round(float(position[0]) * 100_000),
            round(float(position[1]) * 100_000),
        )
        deduplicated.setdefault(key, place)
    ordered_places = sorted(
        deduplicated.values(),
        key=lambda place: (str(place["name"]).casefold(), str(place["id"])),
    )

    output = {
        "format": "Vulkax-local-navigation-1",
        "region": "connaught-place",
        "source": str(args.source.name),
        "sourceSha256": hashlib.sha256(source_bytes).hexdigest(),
        "modes": {"driving": 1, "walking": 2, "cycling": 4},
        "nodes": graph_nodes,
        "edges": graph_edges,
        "places": ordered_places,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(output, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    print(
        f"Wrote {len(graph_nodes)} road nodes, {len(graph_edges)} directed edges, "
        f"and {len(ordered_places)} searchable places to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
