#!/usr/bin/env python3
"""Generate dependency-free SVG Atlas research figures from summary.json."""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path


COLORS = (
    "#315b7d",
    "#4f7f6a",
    "#d07a34",
    "#8b5c9e",
    "#9b3f45",
    "#68717a",
    "#1f9d75",
)


def bar_chart(items, metric: str, title: str, output: Path) -> None:
    width, height = 980, 520
    margin_left, margin_right = 90, 30
    margin_top, margin_bottom = 70, 120
    plot_width = width - margin_left - margin_right
    plot_height = height - margin_top - margin_bottom
    maximum = max(float(item[metric]) for item in items) or 1.0
    slot = plot_width / len(items)
    bar_width = slot * 0.64
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#f7f8f6"/>',
        f'<text x="{width / 2}" y="36" text-anchor="middle" '
        'font-family="sans-serif" font-size="22" fill="#182229">'
        f"{html.escape(title)}</text>",
        f'<line x1="{margin_left}" y1="{margin_top + plot_height}" '
        f'x2="{width - margin_right}" y2="{margin_top + plot_height}" '
        'stroke="#46545d" stroke-width="1"/>',
    ]
    for index, item in enumerate(items):
        value = float(item[metric])
        height_value = value / maximum * plot_height
        x = margin_left + index * slot + (slot - bar_width) / 2
        y = margin_top + plot_height - height_value
        color = COLORS[index % len(COLORS)]
        label = item["policy"].replace("-", " ")
        parts.extend(
            (
                f'<rect x="{x:.2f}" y="{y:.2f}" width="{bar_width:.2f}" '
                f'height="{height_value:.2f}" fill="{color}"/>',
                f'<text x="{x + bar_width / 2:.2f}" y="{y - 8:.2f}" '
                'text-anchor="middle" font-family="sans-serif" font-size="13" '
                f'fill="#182229">{value:,.0f}</text>',
                f'<text x="{x + bar_width / 2:.2f}" y="{margin_top + plot_height + 20}" '
                'text-anchor="end" transform="rotate(-35 '
                f'{x + bar_width / 2:.2f} {margin_top + plot_height + 20})" '
                'font-family="sans-serif" font-size="13" fill="#26343d">'
                f"{html.escape(label)}</text>",
            )
        )
    parts.append("</svg>\n")
    output.write_text("\n".join(parts), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    summary = json.loads(
        (arguments.results / "summary.json").read_text(encoding="utf-8")
    )
    arguments.output.mkdir(parents=True, exist_ok=True)
    items = summary["policies"]
    bar_chart(
        items,
        "deadlineMisses",
        "Route-corridor deadline misses by controller policy",
        arguments.output / "atlas_deadline_misses.svg",
    )
    bar_chart(
        items,
        "wastedPrefetchBytes",
        "Wasted prefetch bytes by controller policy",
        arguments.output / "atlas_wasted_prefetch.svg",
    )
    print(f"wrote Atlas figures to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
