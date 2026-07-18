#!/usr/bin/env python3
"""Run deterministic GeoBEACON Vulkan benchmark matrices with resumable case output."""

from __future__ import annotations

import argparse
import itertools
import json
import os
import subprocess
import sys
import time
from pathlib import Path

POLICIES = ["fixed-lod1", "distance-lod", "semantic-lod", "geo-beacon-exact", "geo-beacon-bounded"]
PATHS = ["outer-orbit", "street-drive", "intersection-dwell", "landmark-approach", "rapid-teleport"]
ICDS = {
    "moltenvk": "/usr/local/share/vulkan/icd.d/MoltenVK_icd.json",
    "kosmickrisp": "/usr/local/share/vulkan/icd.d/libkosmickrisp_icd.json",
}


def profile_matrix(profile: str):
    if profile == "smoke":
        return {
            "policies": POLICIES,
            "paths": ["outer-orbit", "rapid-teleport"],
            "lights": [100],
            "frame_budgets": [16.67],
            "memory": [256],
            "upload": [25],
            "resolutions": [(640, 360)],
            "cache": ["cold"],
            "repeats": 1,
            "warmup": 30,
            "frames": 60,
        }
    if profile == "core":
        return {
            "policies": POLICIES,
            "paths": PATHS,
            "lights": [100, 500, 2000],
            "frame_budgets": [16.67, 33.33],
            "memory": [256, 512],
            "upload": [25, 100],
            "resolutions": [(1280, 720), (1920, 1080)],
            "cache": ["cold", "warm"],
            "repeats": 3,
            "warmup": 300,
            "frames": 900,
        }
    return {
        "policies": POLICIES,
        "paths": PATHS,
        "lights": [100, 500, 2000],
        "frame_budgets": [16.67, 33.33],
        "memory": [256, 512, 1024],
        "upload": [25, 100, 0],
        "resolutions": [(1280, 720), (1920, 1080), (2560, 1440)],
        "cache": ["cold", "warm"],
        "repeats": 5,
        "warmup": 600,
        "frames": 1800,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("docs/results/geobeacon"))
    parser.add_argument("--profile", choices=("smoke", "core", "full"), default="full")
    parser.add_argument("--drivers", nargs="+", choices=tuple(ICDS), default=list(ICDS))
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    matrix = profile_matrix(args.profile)
    args.output.mkdir(parents=True, exist_ok=True)
    cases = list(itertools.product(
        args.drivers,
        matrix["policies"],
        matrix["paths"],
        matrix["lights"],
        matrix["frame_budgets"],
        matrix["memory"],
        matrix["upload"],
        matrix["resolutions"],
        matrix["cache"],
        range(matrix["repeats"]),
    ))
    failures = []
    started = time.time()
    for index, case in enumerate(cases, 1):
        driver, policy, path, lights, frame_budget, memory, upload, resolution, cache, repeat = case
        width, height = resolution
        name = (
            f"{driver}_{policy}_{path}_l{lights}_f{frame_budget:g}_m{memory}_"
            f"u{upload}_r{width}x{height}_{cache}_n{repeat}"
        )
        case_dir = args.output / name
        if (case_dir / "frames.csv").exists() and not args.force:
            print(f"[{index}/{len(cases)}] cached {name}", flush=True)
            continue
        case_dir.mkdir(parents=True, exist_ok=True)
        command = [
            str(args.binary),
            "--geo",
            "--geo-policy", policy,
            "--geo-camera-path", path,
            "--geo-cache-mode", cache,
            "--geo-budget-frame-ms", str(frame_budget),
            "--geo-budget-memory-mib", str(memory),
            "--geo-budget-upload-mibps", str(upload if upload else 100000),
            "--lights", str(lights),
            "--width", str(width),
            "--height", str(height),
            "--warmup-frames", str(matrix["warmup"]),
            "--frames", str(matrix["frames"]),
            "--capture-reference", "true" if policy in ("geo-beacon-exact", "geo-beacon-bounded") else "false",
            "--output", str(case_dir),
            "--quiet",
        ]
        env = os.environ.copy()
        env["VK_ICD_FILENAMES"] = ICDS[driver]
        print(f"[{index}/{len(cases)}] running {name}", flush=True)
        result = subprocess.run(command, cwd=Path(__file__).resolve().parents[1], env=env)
        if result.returncode:
            failures.append({"case": name, "returnCode": result.returncode})
    elapsed = time.time() - started
    index_data = {
        "profile": args.profile,
        "caseCount": len(cases),
        "elapsedSeconds": elapsed,
        "failures": failures,
        "matrix": matrix,
    }
    (args.output / "matrix_manifest.json").write_text(json.dumps(index_data, indent=2) + "\n")
    print(f"completed {len(cases) - len(failures)}/{len(cases)} cases in {elapsed:.1f}s")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
