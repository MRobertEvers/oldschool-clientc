#!/usr/bin/env python3
"""Every packet kind the executor handles has a row in the packet kinds.

The packet pump latches and flushes on one table (src/net/rev/revpacket.c,
k_packet_kinds). A packet the executor learns to handle with no row would
assert at run time in a debug build and latch nothing in an OPT one; this
fails first.

Reads the executor's units -- the model (game/rs_server_state.c), the
tree-only writes (game/rs_gameproto_exec.c), the real client's presentation
(app/app_gameproto_exec.c) and the packet task (game/task_gameproto_exec.c)
-- for every `case PKT_NAME_X:` and every
`packet_type == PKT_NAME_X`, and the table for its KIND(PKT_NAME_X, ...) rows.

    python3 tools/packet_kinds_check.py --root src
"""
import argparse
import os
import re
import sys

EXECUTOR = (
    "game/rs_server_state.c",
    "game/rs_gameproto_exec.c",
    "app/app_gameproto_exec.c",
    "game/task_gameproto_exec.c",
)
TABLE = "net/rev/revpacket.c"
NAMES = "net/rev/pktnames.h"


def handled_names(text):
    found = set(re.findall(r"\bcase\s+(PKT_NAME_[A-Z0-9_]+)\s*:", text))
    found |= set(re.findall(r"packet_type\s*==\s*(PKT_NAME_[A-Z0-9_]+)", text))
    return found


def enum_names(text):
    body = text[text.index("enum GameProtoPktName"):]
    body = body[: body.index("PKT_NAME_COUNT")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return set(re.findall(r"\b(PKT_NAME_[A-Z0-9_]+)", body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="src")
    args = ap.parse_args()

    def read(rel):
        with open(os.path.join(args.root, rel), encoding="utf-8") as f:
            return f.read()

    rows = set(re.findall(r"\bKIND\((PKT_NAME_[A-Z0-9_]+)", read(TABLE)))
    failures = 0
    handled = set()
    for unit in EXECUTOR:
        names = handled_names(read(unit))
        handled |= names
        for name in sorted(names - rows):
            print(f"FAIL {unit}: handles {name}, which has no row in {TABLE}")
            failures += 1
    for name in sorted(enum_names(read(NAMES)) - rows):
        print(f"FAIL {NAMES}: {name} has no row in {TABLE}")
        failures += 1
    print(
        f"packet_kinds_check: {len(handled)} handled names, {len(rows)} rows, "
        f"{failures} failure(s)"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
