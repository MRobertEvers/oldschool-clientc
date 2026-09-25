#!/usr/bin/env python3
"""List every whole-UI-tree loop that is neither metered nor exempted.

A `for` from 0 to a UITree's `component_count` walks every node -- ~7,000 on
an OSRS239 frame. Walked once when something changed, that is fine; walked
per question on a frame where nothing changed, it is the failure that ran the
Stone Drawer at 68 ms a frame on the phone (2026-09-21): a lookup whose miss
was never remembered, asked fourteen times per widget per frame.

So every such loop must say what it is, on one of the three lines above it:

    UITREE_SCAN_METER(tree);                 the frame loop's scan meter counts
                                             it, and a steady frame that keeps
                                             walking fails loudly
    /* tree-walk-exempt: <reason> */         boot, teardown, a debug dump --
                                             code the frame loop never runs

Run from src/makefile as check-tree-walks; the last line must read `total 0`.
`--all` lists every whole-tree loop it recognised, marked or not.
"""
import os
import re
import sys

args = [a for a in sys.argv[1:] if a != '--all']
list_all = '--all' in sys.argv[1:]
root = args[0] if args else 'src'

# The container before `component_count` names a UITree: `tree`, `app->tree`,
# `app.tree`, `ctx->tree`, `t`, `source`. Packs and manifests (`pack->`, `m->`)
# are cache data, not the tree.
TREE_CONTAINER = r'(?:\b(?:tree|t|source)|(?:->|\.)tree)'
LOOP = re.compile(
    r'\bfor\s*\([^;]*=\s*0\s*;[^;]*<\s*(?:\([^)]*\)\s*)?[\w\.\->]*?' + TREE_CONTAINER +
    r'\s*(?:->|\.)component_count\b')
MARK = re.compile(r'UITREE_SCAN_METER\s*\(|tree-walk-exempt:')

hits = []
for directory, _, files in os.walk(root):
    if os.sep + 'test' in directory + os.sep or '/test/' in directory + '/':
        continue
    for name in files:
        if not name.endswith(('.c', '.h')):
            continue
        path = os.path.join(directory, name)
        lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
        for number, line in enumerate(lines):
            code = re.sub(r'//.*', '', re.sub(r'/\*.*?\*/', '', line))
            if not LOOP.search(code):
                continue
            above = [l for l in lines[max(0, number - 3):number]]
            if list_all:
                print('%s:%d: %s' % (path, number + 1,
                      'marked' if any(MARK.search(l) for l in above) else 'UNMARKED'))
            if any(MARK.search(l) for l in above):
                continue
            hits.append((path, number + 1, line.strip()))

for path, number, text in hits:
    print('%s:%d: unmetered whole-tree walk: %s' % (path, number, text))
print('total', len(hits))
