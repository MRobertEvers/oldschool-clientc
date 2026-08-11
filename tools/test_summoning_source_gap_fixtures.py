#!/usr/bin/env python3
"""Regression gate for researched Summoning source-gap contracts.

The local 2009scape classes intentionally contain no logic for these rows.  A
fixture keeps the replacement work evidence-led and makes an accidental change
to the live registry's source-gap set visible immediately.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from summoning_special_registry import (
    SOURCE_GAP_FIXTURE,
    SOURCE_GAPS,
    SOURCE_INCOMPLETE,
    records,
)


REPO = Path(__file__).resolve().parents[1]
FIXTURES = REPO / "docs/summoning_port/SPECIAL_SOURCE_GAP_FIXTURES.json"


def main() -> int:
    try:
        data = json.loads(FIXTURES.read_text(encoding="utf-8"))
        assert data["schema"] == 1, "unsupported source-gap fixture schema"
        fixtures = data["fixtures"]
        by_type = {fixture["type"]: fixture for fixture in fixtures}
        assert len(by_type) == len(fixtures), "duplicate source-gap fixture type"
        assert set(by_type) == set(SOURCE_GAPS) | set(SOURCE_INCOMPLETE) | {33}, (
            "fixtures must cover every live source gap/incomplete row plus the Magpie correction"
        )

        live = {record.type: record for record in records()}
        for kind in SOURCE_GAPS:
            fixture = by_type[kind]
            record = live[kind]
            assert fixture["familiar"] == record.familiar, (
                f"fixture familiar drift for type {kind}"
            )
            assert record.provenance == SOURCE_GAP_FIXTURE, (
                f"registry source gap {kind} does not point at its fixture"
            )
            assert fixture["implementation_state"] == "research_ready", (
                f"source gap {kind} is not ready for a bounded implementation"
            )
            assert fixture["historical_source"].startswith("https://"), (
                f"source gap {kind} lacks a cited historical source"
            )
            assert "2009scape/" in fixture["local_source"], (
                f"source gap {kind} does not identify the deficient local source"
            )
            assert fixture["expected"].strip(), f"source gap {kind} has no behavior contract"
            assert fixture["requirements"], f"source gap {kind} has no unblock dependencies"
            assert fixture["open_details"], f"source gap {kind} must retain its unknowns"

        magpie = by_type[33]
        assert live[33].familiar == magpie["familiar"], "Magpie correction drifted"
        assert magpie["implementation_state"] == "correction_fixture", (
            "Magpie must remain explicitly identified as a reconstructed correction"
        )
        assert "capped_skill_boost" in magpie["requirements"], (
            "Magpie correction no longer specifies its boost primitive"
        )

        honey_badger = by_type[16]
        assert live[16].state == "source_incomplete", (
            "Honey badger must remain unavailable until its local source conflict is resolved"
        )
        assert honey_badger["implementation_state"] == "evidence_partial", (
            "Honey badger must retain its conflicting historical evidence"
        )
        assert "authoritative_2009_behavior_resolution" in honey_badger["requirements"], (
            "Honey badger fixture no longer requires resolving the source conflict"
        )
    except (AssertionError, KeyError, OSError, ValueError, TypeError) as exc:
        print(f"test_summoning_source_gap_fixtures: error: {exc}", file=sys.stderr)
        return 1

    print(
        "test_summoning_source_gap_fixtures: "
        f"{len(SOURCE_GAPS)} source gaps + {len(SOURCE_INCOMPLETE)} incomplete row + Magpie correction, 0 errors"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
