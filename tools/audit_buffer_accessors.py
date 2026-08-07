#!/usr/bin/env python3
"""Inventory every gX/pX-style buffer accessor definition in the tree.

Finds *definitions* (not calls): a C function whose name looks like a
RuneScape buffer accessor -- g1/p2/g4_alt1/gjstr/psmart/gbits/... -- in any
form (extern, static, inline, macro), and reports where it lives.
"""
import os, re, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

AREAS = [
    ("net",     "src/net"),
    ("rscache", "3rd/rscache"),
    ("rsprot",  "3rd/rsprot"),
    ("rsareabuf", "3rd/rsareabuf"),
    ("v0",      "v0"),
    ("trspk",   "3rd/trspk"),
    ("serverscript", "src/serverscript"),
    ("engine",  "src/engine"),
    ("cs2vm2",  "src/cs2vm2"),
]

# A buffer-accessor-looking identifier.
NAME = r"(?:[A-Za-z_][A-Za-z0-9_]*_)?[gp](?:\d+[a-z]*|bits|jstr\w*|str\w*|smart\w*|varint\w*|bool\w*|data\w*|type|combined_id\w*|midivarlen\w*)(?:_?[Aa]lt\d)?"

# function definition: `rettype name(args)` at line start-ish, then `{`
DEF_RE = re.compile(
    r"^[ \t]*(?:static\s+|inline\s+|extern\s+|RSCACHE_API\s+|__attribute__\(\([^)]*\)\)\s*)*"
    r"(?:const\s+)?[A-Za-z_][A-Za-z0-9_ \*]*?[\s\*]+"
    r"(" + NAME + r")\s*\([^;]*?\)\s*(?:\{|$)", re.M)

MACRO_RE = re.compile(r"^[ \t]*#\s*define\s+(" + NAME + r")\s*\(", re.M)

def scan(path):
    try:
        txt = open(path, errors="replace").read()
    except OSError:
        return []
    out = []
    for m in DEF_RE.finditer(txt):
        line = txt[:m.start()].count("\n") + 1
        out.append((m.group(1), line, "fn", "static" in m.group(0)))
    for m in MACRO_RE.finditer(txt):
        line = txt[:m.start()].count("\n") + 1
        out.append((m.group(1), line, "macro", False))
    return out

by_area = collections.OrderedDict()
for area, rel in AREAS:
    base = os.path.join(ROOT, rel)
    if not os.path.isdir(base):
        continue
    hits = []
    for dirpath, dirnames, files in os.walk(base):
        dirnames[:] = [d for d in dirnames if d not in ("build", ".git", "node_modules")]
        for f in sorted(files):
            if not f.endswith((".c", ".h")):
                continue
            p = os.path.join(dirpath, f)
            for name, line, kind, is_static in scan(p):
                hits.append((os.path.relpath(p, ROOT), line, name, kind, is_static))
    by_area[area] = hits

total = 0
for area, hits in by_area.items():
    names = sorted({h[2] for h in hits})
    files = sorted({h[0] for h in hits})
    statics = sorted({h[2] for h in hits if h[4]})
    print(f"\n=== {area}: {len(hits)} definitions, {len(names)} distinct names, {len(files)} files ===")
    total += len(hits)
    for f in files:
        fh = [h for h in hits if h[0] == f]
        st = sum(1 for h in fh if h[4])
        print(f"   {f:<62} {len(fh):>3} defs ({st} static)")
    if statics:
        print(f"   STATIC/local names: {' '.join(statics)}")
print(f"\nTOTAL definitions: {total}")
