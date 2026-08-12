#!/usr/bin/env python3
"""Regression gate for the generated 78-row Summoning special registry."""

from __future__ import annotations

import sys

from summoning_special_registry import (
    IMPLEMENTED,
    RECONSTRUCTED_TYPES,
    SOURCE_GAPS,
    SOURCE_INCOMPLETE,
    records,
)


def main() -> int:
    try:
        rows = records()
        assert len(rows) == 78, "registry must contain exactly 78 familiar types"
        assert {row.type for row in rows} == set(range(1, 79)), "registry types are not contiguous"
        assert len({row.pouch for row in rows}) == 78, "registry pouch mapping is not one-to-one"
        assert all(row.cost > 0 and row.xp_tenths >= 0 for row in rows), "invalid cost or XP"
        assert {
            row.type for row in rows if row.state in {"implemented", "implemented_reconstructed"}
        } == IMPLEMENTED, "enabled rows drifted"
        assert {
            row.type for row in rows if row.state == "implemented_reconstructed"
        } == RECONSTRUCTED_TYPES, "reconstructed rows drifted"
        assert all(row.handler == "unavailable" for row in rows if row.type not in IMPLEMENTED), (
            "an unimplemented special has a callable handler"
        )
        assert all(row.asset_bundle == "unavailable" for row in rows if row.type in SOURCE_GAPS), (
            "an evidence-blocked special claims an admitted active asset bundle"
        )
        assert all(row.handler != "unavailable" for row in rows if row.type in IMPLEMENTED), (
            "an enabled special has no handler"
        )
        assert all(row.provenance != "needs_citation" for row in rows if row.state not in {"source_gap", "source_incomplete"}), (
            "specified behavior lacks source provenance"
        )
        assert SOURCE_INCOMPLETE == set() and SOURCE_GAPS == set(), (
            "a completed reconstruction remains unavailable"
        )
        assert RECONSTRUCTED_TYPES == {16, 50, 52, 56, 58, 64, 65}
        by_type = {row.type: row for row in rows}
        assert by_type[16].cost == 12, "Insane Ferocity must use the official 12-point cost"
        assert by_type[52].cost == 12 and by_type[52].xp_tenths == 50, (
            "Phoenix must use the archived official cost/XP"
        )
        assert by_type[65].xp_tenths == 43, "Swamp Plague must use official 4.3 XP"
    except (AssertionError, KeyError, OSError, ValueError, IndexError) as exc:
        print(f"test_summoning_special_registry: error: {exc}", file=sys.stderr)
        return 1
    print(
        "test_summoning_special_registry: "
        f"78 unique rows, {len(IMPLEMENTED)} enabled, "
        f"{len(RECONSTRUCTED_TYPES)} reconstructed, {len(SOURCE_GAPS)} fail closed, 0 errors"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
