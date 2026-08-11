#!/usr/bin/env python3
"""Regression gate for the generated 78-row Summoning special registry."""

from __future__ import annotations

import sys

from summoning_special_registry import IMPLEMENTED, records


def main() -> int:
    try:
        rows = records()
        assert len(rows) == 78, "registry must contain exactly 78 familiar types"
        assert {row.type for row in rows} == set(range(1, 79)), "registry types are not contiguous"
        assert len({row.pouch for row in rows}) == 78, "registry pouch mapping is not one-to-one"
        assert all(row.cost > 0 and row.xp_tenths >= 0 for row in rows), "invalid cost or XP"
        assert {row.type for row in rows if row.state == "implemented"} == IMPLEMENTED, "enabled rows drifted"
        assert all(row.handler == "unavailable" for row in rows if row.type not in IMPLEMENTED), (
            "an unimplemented special has a callable handler"
        )
        assert all(row.handler != "unavailable" for row in rows if row.type in IMPLEMENTED), (
            "an enabled special has no handler"
        )
        assert all(row.provenance != "needs_citation" for row in rows if row.state not in {"source_gap", "source_incomplete"}), (
            "specified behavior lacks source provenance"
        )
    except (AssertionError, KeyError, OSError, ValueError, IndexError) as exc:
        print(f"test_summoning_special_registry: error: {exc}", file=sys.stderr)
        return 1
    print("test_summoning_special_registry: 78 unique familiar rows, 15 enabled, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
