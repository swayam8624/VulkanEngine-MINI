#!/usr/bin/env python3
"""Generate BEACON paper figures from raw benchmark CSV outputs.

The script intentionally uses only Python's standard library so figures can be
recreated on a fresh clone without installing plotting packages.
"""

from __future__ import annotations

import csv
import json
import math
import statistics
import sys
from pathlib import Path


def read_rows(case_dir: Path) -> list[dict[str, str]]:
    frame_path = case_dir / "frames.csv"
    if not frame_path.exists():
        return []
    with frame_path.open(newline="") as f:
        return list(csv.DictReader(f))


def number(row: dict[str, str], field: str, default: float = 0.0) -> float:
    try:
        return float(row.get(field, default) or default)
    except ValueError:
        return default


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    idx = (len(values) - 1) * p
    lo = int(idx)
    hi = min(lo + 1, len(values) - 1)
    t = idx - lo
    return values[lo] * (1.0 - t) + values[hi] * t


def summarize_case(case_dir: Path) -> dict[str, float | str]:
    rows = read_rows(case_dir)
    summary_path = case_dir / "summary.json"
    technique = case_dir.name
    if summary_path.exists():
      try:
          technique = json.loads(summary_path.read_text()).get("technique", technique)
      except json.JSONDecodeError:
          pass

    modeled_cost = [number(r, "totalModeledCostMs") for r in rows]
    gpu_object = [number(r, "gpuObjectPassMs") for r in rows]
    cpu = [number(r, "cpuFrameMs") for r in rows]
    gpu_total = [
        number(r, "gpuClusterBuildMs") + number(r, "gpuLightingPassMs")
        for r in rows
    ]
    timing_values = modeled_cost if any(modeled_cost) else gpu_total if any(gpu_total) else cpu
    active_clusters = [number(r, "activeClusters") for r in rows]
    light_bytes = [
        number(r, "lightListBytes") or number(r, "lightIndexCapacity") * 4.0
        for r in rows
    ]
    error = [number(r, "modeledScalarBoundDifference") for r in rows]
    pruned = [number(r, "prunedLightSamples") for r in rows]
    evaluated = [number(r, "evaluatedLightSamples") for r in rows]
    offscreen_mse = [number(r, "offscreenMse") for r in rows if number(r, "offscreenMse", -1.0) >= 0.0]
    offscreen_psnr = [number(r, "offscreenPsnr") for r in rows if number(r, "offscreenPsnr", -1.0) >= 0.0]
    offscreen_ssim = [number(r, "offscreenSsim") for r in rows if number(r, "offscreenSsim", -1.0) >= 0.0]
    semantic_utility = [number(r, "geoSemanticUtility") for r in rows]
    resident_bytes = [number(r, "geoResidentBytes") for r in rows]
    budget_violations = [number(r, "geoBudgetViolation") for r in rows]
    group = rows[0].get("measurementGroup", "unknown") if rows else "unknown"

    return {
        "case": case_dir.name,
        "technique": str(technique),
        "measurement_group": group,
        "frames": len(rows),
        "cpu_p50": percentile(cpu, 0.50),
        "time_p50": percentile(timing_values, 0.50),
        "time_p95": percentile(timing_values, 0.95),
        "clusters_avg": statistics.fmean(active_clusters) if active_clusters else 0.0,
        "light_list_kb": (statistics.fmean(light_bytes) / 1024.0) if light_bytes else 0.0,
        "modeled_bound_delta": statistics.fmean(error) if error else 0.0,
        "prune_ratio": (sum(pruned) / max(1.0, sum(pruned) + sum(evaluated))) if (pruned or evaluated) else 0.0,
        "offscreen_mse": statistics.fmean(offscreen_mse) if offscreen_mse else -1.0,
        "offscreen_psnr": statistics.fmean(offscreen_psnr) if offscreen_psnr else -1.0,
        "offscreen_ssim": statistics.fmean(offscreen_ssim) if offscreen_ssim else -1.0,
        "semantic_utility_per_ms": (
            statistics.fmean(semantic_utility) / max(percentile(cpu, 0.50), 1e-6)
            if semantic_utility else 0.0
        ),
        "resident_mib": statistics.fmean(resident_bytes) / (1024.0 * 1024.0) if resident_bytes else 0.0,
        "budget_violation_ratio": statistics.fmean(budget_violations) if budget_violations else 0.0,
    }


def bar_svg(rows: list[dict[str, float | str]], field: str, title: str, output: Path) -> None:
    width, height = 920, 420
    margin_left, margin_bottom, margin_top = 80, 80, 48
    plot_width = width - margin_left - 32
    plot_height = height - margin_top - margin_bottom
    values = [float(r[field]) for r in rows]
    max_value = max(values) if values else 1.0
    max_value = max(max_value, 1e-9)
    bar_width = plot_width / max(1, len(rows))

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#101418"/>',
        f'<text x="{margin_left}" y="30" fill="#f4f7fb" font-family="Arial" font-size="20">{title}</text>',
        f'<line x1="{margin_left}" y1="{height - margin_bottom}" x2="{width - 24}" y2="{height - margin_bottom}" stroke="#6b7280"/>',
        f'<line x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" y2="{height - margin_bottom}" stroke="#6b7280"/>',
    ]

    for i, row in enumerate(rows):
        value = float(row[field])
        h = (value / max_value) * plot_height
        x = margin_left + i * bar_width + 6
        y = height - margin_bottom - h
        parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{max(4, bar_width - 12):.1f}" height="{h:.1f}" fill="#4cc9f0"/>')
        parts.append(
            f'<text transform="translate({x + max(4, bar_width - 12) / 2:.1f},{height - 54}) rotate(-35)" '
            f'fill="#d1d5db" font-family="Arial" font-size="11" text-anchor="end">{row["case"]}</text>'
        )
        parts.append(f'<text x="{x:.1f}" y="{max(44, y - 6):.1f}" fill="#e5e7eb" font-family="Arial" font-size="11">{value:.3g}</text>')

    parts.append("</svg>\n")
    output.write_text("\n".join(parts))


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: generate_beacon_figures.py <results-dir> <figures-dir>", file=sys.stderr)
        return 2
    results_dir = Path(sys.argv[1])
    figures_dir = Path(sys.argv[2])
    figures_dir.mkdir(parents=True, exist_ok=True)

    if (results_dir / "frames.csv").exists():
        case_dirs = [results_dir]
    elif results_dir.is_dir():
        case_dirs = [p.parent for p in sorted(results_dir.rglob("frames.csv"))]
    else:
        case_dirs = [results_dir]
    summaries = [summarize_case(p) for p in case_dirs]
    summaries = [s for s in summaries if s["frames"]]
    if not summaries:
        print(f"no benchmark frames found under {results_dir}", file=sys.stderr)
        return 1

    groups: dict[str, list[dict[str, float | str]]] = {}
    for summary in summaries:
        groups.setdefault(str(summary["measurement_group"]), []).append(summary)

    for group, group_rows in groups.items():
        safe_group = group.replace("/", "_")
        bar_svg(group_rows, "time_p50", f"{group} p50 Time or Modeled Cost (ms)", figures_dir / f"{safe_group}_p50.svg")
        bar_svg(group_rows, "time_p95", f"{group} p95 Time or Modeled Cost (ms)", figures_dir / f"{safe_group}_p95.svg")
        bar_svg(group_rows, "modeled_bound_delta", f"{group} Modeled Scalar Bound Difference", figures_dir / f"{safe_group}_bound_delta.svg")
        bar_svg(group_rows, "prune_ratio", f"{group} Pruned Light-Sample Ratio", figures_dir / f"{safe_group}_prune_ratio.svg")
        captured_rows = [row for row in group_rows if float(row["offscreen_mse"]) >= 0.0]
        if captured_rows:
            bar_svg(captured_rows, "offscreen_mse", f"{group} Rendered Offscreen MSE", figures_dir / f"{safe_group}_offscreen_mse.svg")
        if any(float(row["semantic_utility_per_ms"]) for row in group_rows):
            bar_svg(
                group_rows,
                "semantic_utility_per_ms",
                f"{group} Semantic Utility per CPU Millisecond",
                figures_dir / f"{safe_group}_semantic_utility.svg",
            )
            bar_svg(
                group_rows,
                "resident_mib",
                f"{group} Mean Resident Memory (MiB)",
                figures_dir / f"{safe_group}_resident_memory.svg",
            )

    with (figures_dir / "summary.md").open("w") as out:
        out.write("| Case | Group | Technique | Frames | Time p50 | Time p95 | Modeled bound delta | Prune ratio | Light-list KB | Offscreen MSE | Offscreen PSNR | Offscreen SSIM |\n")
        out.write("| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n")
        for s in summaries:
            out.write(
                f"| {s['case']} | {s['measurement_group']} | {s['technique']} | {int(s['frames'])} | "
                f"{s['time_p50']:.4f} | {s['time_p95']:.4f} | {s['modeled_bound_delta']:.4f} | "
                f"{s['prune_ratio']:.4f} | {s['light_list_kb']:.2f} | {s['offscreen_mse']:.8f} | "
                f"{s['offscreen_psnr']:.4f} | {s['offscreen_ssim']:.6f} |\n"
            )
    print(f"wrote figures to {figures_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
