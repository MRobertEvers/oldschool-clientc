#!/usr/bin/env python3
"""detail_lengths.py — every Porcelain declaration string that will not fit.

PORCELAIN_DETAIL_MAX is 96 and Porcelain_CopyString asserts on a longer one.
That assert only exists in a debug build, so a plugin whose reason runs long
aborts at start under OPT=0 and silently truncates under OPT=1 — which is every
build the gate and the matrix run, and is why nothing in the tree had caught
one. This lists them without running anything.

Reads the Lua plugins (where the string is a `..`-joined literal, so it has to
be reassembled) and the C plugins (where it is adjacent string literals, which
the compiler joins for the same reason).
"""
import pathlib
import re
import sys

MAX = 96
ROOT = pathlib.Path(__file__).resolve().parents[3]
CALLS = ("expect_unsupported", "expect_absent", "Porcelain_ExpectUnsupported",
         "Porcelain_ExpectAbsent", "Porcelain_Require", "Porcelain_Notify")

# A run of "..." literals, optionally joined by Lua's `..` or by nothing at all
# (C's adjacent-literal concatenation), across newlines.
PIECES = re.compile(r'"((?:[^"\\]|\\.)*)"(?:\s*(?:\.\.)?\s*)')


def strings_in_call(text, start):
    """The literals of one call, from its open paren to its matching close."""
    depth, i = 0, start
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = text[start:i]
    out, pos = [], 0
    while True:
        m = PIECES.search(body, pos)
        if not m:
            return out
        joined, pos = m.group(1), m.end()
        while True:
            n = PIECES.match(body, pos)
            if not n:
                break
            joined += n.group(1)
            pos = n.end()
        out.append(joined)


def main():
    bad = 0
    files = sorted(ROOT.glob("script/plugins/*.lua")) + \
        sorted(ROOT.glob("src/plugin/plugins/*.c"))
    for path in files:
        text = path.read_text(errors="replace")
        for call in CALLS:
            for m in re.finditer(re.escape(call) + r"\s*\(", text):
                for s in strings_in_call(text, m.end() - 1):
                    if len(s) >= MAX:
                        line = text.count("\n", 0, m.start()) + 1
                        print(f"{path.relative_to(ROOT)}:{line}: {call}: "
                              f"{len(s)} chars (max {MAX - 1})")
                        print(f"    {s[:70]}...")
                        bad += 1
    print(f"\n{bad} over-long declaration string(s)")
    return 1 if bad else 0


sys.exit(main())
