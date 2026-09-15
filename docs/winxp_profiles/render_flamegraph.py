#!/usr/bin/env python3
"""Render the Very Sleepy folded stacks as a self-contained SVG flame graph."""

from __future__ import annotations

import hashlib
import html
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
# Defaults name the first capture. Every later capture is a different binary
# measured under different conditions, so pass its folded stacks explicitly
# rather than overwriting an SVG that documents a run you can no longer take.
#   render_flamegraph.py <stacks.folded> [out.svg]
INPUT = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "verysleepy_stacks.folded"
OUTPUT = (
    Path(sys.argv[2])
    if len(sys.argv) > 2
    else INPUT.with_name(INPUT.name.replace("_stacks.folded", "") + "-flamegraph.svg")
)
WIDTH = 1600
FRAME_HEIGHT = 18
LEFT = 10
RIGHT = 10
TOP = 56
BOTTOM = 30


class Node:
    def __init__(self, name: str):
        self.name = name
        self.value = 0
        self.children: dict[str, Node] = {}


def color(name: str) -> str:
    digest = hashlib.sha1(name.encode("utf-8", "replace")).digest()
    red = 210 + digest[0] % 42
    green = 70 + digest[1] % 105
    blue = 35 + digest[2] % 50
    return f"rgb({red},{green},{blue})"


def main() -> None:
    root = Node("all samples")
    max_depth = 0
    for line in INPUT.read_text(encoding="utf-8").splitlines():
        stack_text, count_text = line.rsplit(" ", 1)
        names = stack_text.split(";")
        count = int(count_text)
        root.value += count
        node = root
        for name in names:
            node = node.children.setdefault(name, Node(name))
            node.value += count
        max_depth = max(max_depth, len(names))

    graph_height = (max_depth + 1) * FRAME_HEIGHT
    height = TOP + graph_height + BOTTOM
    usable_width = WIDTH - LEFT - RIGHT
    rows: list[tuple[Node, int, float, float]] = []

    def visit(node: Node, depth: int, x: float, width: float) -> None:
        if depth >= 0:
            rows.append((node, depth, x, width))
        cursor = x
        children = sorted(node.children.values(), key=lambda item: (-item.value, item.name))
        for child in children:
            child_width = width * child.value / node.value if node.value else 0.0
            visit(child, depth + 1, cursor, child_width)
            cursor += child_width

    visit(root, -1, LEFT, usable_width)

    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" '
        f'viewBox="0 0 {WIDTH} {height}">',
        "<style>",
        "text { font-family: Consolas, 'Courier New', monospace; }",
        ".frame:hover { stroke: #111; stroke-width: 1.2; filter: brightness(1.08); }",
        ".label { pointer-events: none; fill: #111; font-size: 11px; }",
        "</style>",
        '<rect width="100%" height="100%" fill="#fafafa"/>',
        '<text x="10" y="22" font-size="17" font-weight="bold">'
        "Windows XP Soft3D — Very Sleepy 0.7, 60-second profile</text>",
        '<text x="10" y="42" font-size="12" fill="#555">'
        "Width is inclusive sampled time; hover a frame for exact time and percentage. "
        "Root is at the bottom.</text>",
    ]
    for node, depth, x, width in rows:
        if width < 0.02:
            continue
        y = TOP + graph_height - (depth + 1) * FRAME_HEIGHT
        seconds = node.value / 1_000_000.0
        percent = 100.0 * node.value / root.value if root.value else 0.0
        escaped_name = html.escape(node.name)
        out.append(
            f'<g><title>{escaped_name} — {seconds:.3f}s ({percent:.2f}%)</title>'
            f'<rect class="frame" x="{x:.3f}" y="{y}" width="{max(0.0, width - 0.25):.3f}" '
            f'height="{FRAME_HEIGHT - 1}" rx="1" fill="{color(node.name)}"/></g>'
        )
        max_chars = int((width - 5) / 6.6)
        if max_chars >= 3:
            label = node.name if len(node.name) <= max_chars else node.name[: max_chars - 2] + ".."
            out.append(
                f'<text class="label" x="{x + 3:.3f}" y="{y + 12}">{html.escape(label)}</text>'
            )
    out.append(
        f'<text x="10" y="{height - 9}" font-size="11" fill="#555">'
        f'Total weighted sample time: {root.value / 1_000_000.0:.3f}s</text>'
    )
    out.append("</svg>")
    OUTPUT.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(OUTPUT)


if __name__ == "__main__":
    main()
