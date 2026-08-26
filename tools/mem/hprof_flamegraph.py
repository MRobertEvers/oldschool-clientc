"""Convert an hprof CPU-samples report to folded stacks, then to a flamegraph SVG.

Folded format is one line per unique stack, root first, frames joined by ';',
followed by a sample count -- the interchange format flamegraph.pl consumes. It
is produced here rather than shelling out to a perl script so the graph can be
regenerated from a committed report with nothing installed.

Optionally drops the AWT idle pump and the title-screen/boot stacks, so the
picture is the in-world frame rather than the JVM's whole lifetime.
"""
import html
import re
import sys

IDLE_LEAF = "sun.awt.windows.WToolkit.eventLoop"
BOOT_MARKERS = (
    "jagex2.client.Client.titleScreenLoop", "jagex2.client.Client.titleScreenDraw",
    "jagex2.client.Client.renderFlames", "jagex2.client.Client.updateFlames",
    "jagex2.client.Client.drawFlames", "jagex2.client.Client.generateFlameCoolingMap",
    "jagex2.client.Client.maininit", "jagex2.client.Client.login",
    "jagex2.client.ClientBuild", "java.lang.ClassLoader",
)


def parse(path):
    traces, samples = {}, {}
    cur = mode = None
    for line in open(path, "r", errors="replace"):
        s = line.rstrip("\n")
        m = re.match(r"^TRACE (\d+):", s)
        if m:
            cur = int(m.group(1))
            traces[cur] = []
            mode = "trace"
            continue
        if s.startswith("CPU SAMPLES BEGIN"):
            mode = "samples"
            continue
        if s.startswith("CPU SAMPLES END"):
            mode = None
            continue
        if mode == "trace" and (s.startswith("\t") or s.startswith("    ")):
            fn = s.strip().split("(")[0]
            if fn:
                traces[cur].append(fn)
            continue
        if mode == "samples":
            p = s.split()
            if len(p) >= 6 and p[0].isdigit():
                samples[int(p[4])] = samples.get(int(p[4]), 0) + int(p[3])
    return traces, samples


def folded(path, drop_idle=True, drop_boot=True):
    traces, samples = parse(path)
    out = {}
    for tid, c in samples.items():
        fr = traces.get(tid) or []
        if not fr:
            continue
        if drop_idle and fr[0] == IDLE_LEAF:
            continue
        if drop_boot and any(any(f.startswith(b) for b in BOOT_MARKERS) for f in fr):
            continue
        stack = ";".join(reversed(fr))  # hprof lists leaf first
        out[stack] = out.get(stack, 0) + c
    return out


# ---- flamegraph -----------------------------------------------------------

PALETTE = [
    ("sun.java2d", "#c94f4f"), ("sun.awt", "#c9784f"), ("java.awt", "#c9784f"),
    ("jagex2.graphics", "#d6a13a"), ("jagex2.dash3d", "#5f9e5f"),
    ("jagex2.io", "#4f8fc9"), ("java.net", "#7a6fc9"), ("java.io", "#4f8fc9"),
    ("jagex2.client", "#8a8f98"), ("jagex2", "#6f7680"), ("java.lang", "#9c6fa8"),
]


def colour(fn):
    for pre, c in PALETTE:
        if fn.startswith(pre):
            return c
    return "#909090"


def build_tree(fold):
    root = {"name": "all", "value": 0, "children": {}}
    for stack, c in fold.items():
        node = root
        root["value"] += c
        for fn in stack.split(";"):
            node = node["children"].setdefault(fn, {"name": fn, "value": 0, "children": {}})
            node["value"] += c
    return root


def layout(node, depth, x, total, width, rows):
    if node["value"] <= 0:
        return
    w = width * node["value"] / total
    if w >= 0.12:  # skip slivers that cannot carry a label or a pixel
        rows.append((depth, x, w, node["name"], node["value"]))
    cx = x
    for ch in sorted(node["children"].values(), key=lambda n: -n["value"]):
        layout(ch, depth + 1, cx, total, width, rows)
        cx += width * ch["value"] / total


def svg(fold, title, subtitle):
    root = build_tree(fold)
    total = root["value"]
    rows = []
    for ch in sorted(root["children"].values(), key=lambda n: -n["value"]):
        pass
    layout(root, 0, 0.0, total, 1180.0, rows)
    depth_max = max(r[0] for r in rows) if rows else 1
    row_h = 17
    top = 56
    height = top + (depth_max + 1) * row_h + 16

    parts = []
    parts.append(
        '<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="%d" '
        'viewBox="0 0 1200 %d" font-family="Verdana,DejaVu Sans,sans-serif">' % (height, height))
    parts.append('<style>'
                 'rect{stroke:#1116;stroke-width:.4}'
                 'text{pointer-events:none}'
                 'g:hover rect{stroke:#000;stroke-width:1}'
                 '</style>')
    parts.append('<rect x="0" y="0" width="1200" height="%d" fill="#f7f7f7"/>' % height)
    parts.append('<text x="600" y="24" text-anchor="middle" font-size="15" '
                 'font-weight="bold" fill="#1a1a1a">%s</text>' % html.escape(title))
    parts.append('<text x="600" y="42" text-anchor="middle" font-size="11" '
                 'fill="#555">%s</text>' % html.escape(subtitle))

    for depth, x, w, name, val in rows:
        y = top + depth * row_h
        pct = 100.0 * val / total
        short = name.split(".")[-1]
        label = ""
        if w > 34:
            budget = int(w / 6.2)
            label = short if len(short) <= budget else short[:max(0, budget - 1)] + "..."
        parts.append('<g><title>%s\n%d samples, %.2f%% of shown work</title>'
                     '<rect x="%.2f" y="%d" width="%.2f" height="%d" fill="%s" rx="1"/>'
                     % (html.escape(name), val, pct, 10 + x, y, max(w, 0.4), row_h - 1, colour(name)))
        if label:
            parts.append('<text x="%.2f" y="%d" font-size="10" fill="#111">%s</text>'
                         % (10 + x + 3, y + row_h - 6, html.escape(label)))
        parts.append('</g>')
    parts.append('</svg>')
    return "\n".join(parts)


if __name__ == "__main__":
    src, out, title, subtitle = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    fold = folded(src)
    open(out + ".folded", "w", encoding="utf-8").write(
        "\n".join("%s %d" % (k, v) for k, v in sorted(fold.items(), key=lambda kv: -kv[1])) + "\n")
    open(out, "w", encoding="utf-8").write(svg(fold, title, subtitle))
    print("%s  (%d stacks, %d samples)" % (out, len(fold), sum(fold.values())))
