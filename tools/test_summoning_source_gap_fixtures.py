#!/usr/bin/env python3
"""Regression gate for the dated final-seven reconstruction ledger."""

from __future__ import annotations

import json
import sys
from pathlib import Path

from summoning_special_registry import (
    RECONSTRUCTED_TYPES,
    SOURCE_GAP_FIXTURE,
    SOURCE_GAPS,
    records,
)


REPO = Path(__file__).resolve().parents[1]
FIXTURES = REPO / "docs/summoning_port/SPECIAL_SOURCE_GAP_FIXTURES.json"
OFFICIAL = (
    "https://web.archive.org/web/20090821105439id_/"
    "http://www.runescape.com/kbase/guid/summoning_scrolls"
)
PERIOD_2009 = {
    16: ("2009-11-12", "https://runescape.wiki/w/Honey_badger?oldid=1903749"),
    50: ("2009-12-06", "https://runescape.wiki/w/Ravenous_locust?oldid=1990108"),
    52: ("2009-11-27", "https://runescape.wiki/w/Phoenix_(familiar)?oldid=1954006"),
    56: ("2009-08-22", "https://runescape.wiki/w/Forge_regent?oldid=1652524"),
    58: ("2009-08-21", "https://runescape.wiki/w/Giant_ent?oldid=1645837"),
    64: ("2009-12-21", "https://runescape.wiki/w/Lava_titan?oldid=2058590"),
    65: ("2009-12-22", "https://runescape.wiki/w/Swamp_titan?oldid=2062840"),
}
PERIOD_2011 = {
    16: ("2011-12-16T14:17:42Z", "https://runescape.wiki/w/Honey_badger?oldid=5058424"),
    50: ("2011-12-19T03:51:53Z", "https://runescape.wiki/w/Ravenous_locust?oldid=5065661"),
    52: ("2011-12-10T01:47:56Z", "https://runescape.wiki/w/Phoenix_(familiar)?oldid=5037635"),
    56: ("2011-12-10T01:40:08Z", "https://runescape.wiki/w/Forge_regent?oldid=5037487"),
    58: ("2011-12-13T14:50:44Z", "https://runescape.wiki/w/Giant_ent?oldid=5048958"),
    64: ("2011-12-31T14:41:43Z", "https://runescape.wiki/w/Lava_titan?oldid=5101862"),
    65: ("2011-12-18T11:33:01Z", "https://runescape.wiki/w/Swamp_titan?oldid=5064427"),
}


def has_evidence(fixture: dict, date: str, url: str, kind: str | None = None) -> bool:
    return any(
        row.get("date") == date
        and row.get("url") == url
        and (kind is None or row.get("kind") == kind)
        for row in fixture["direct_evidence"]
    )


def main() -> int:
    try:
        data = json.loads(FIXTURES.read_text(encoding="utf-8"))
        assert data["schema"] == 3, "unsupported reconstruction fixture schema"
        assert data["audit_date"] == "2026-08-11"
        assert "2011-12-31" in data["evidence_window"]
        baseline = data["official_baseline"]
        assert baseline == {
            "snapshot_at": "2009-08-21T10:54:39Z",
            "url": OFFICIAL,
            "evidence_strength": "direct_period_official",
            "scope": (
                "Target class, high-level effect, scroll cost, and XP. Numeric details "
                "are used only where the table states them."
            ),
        }
        policy = data["shared_reconstruction_policy"]
        assert "multiway Wilderness" in policy["player_targets"]
        assert "combat-level difference" in policy["player_targets"]
        assert "Phoenix" in policy["asset_policy"]

        fixtures = data["fixtures"]
        by_type = {fixture["type"]: fixture for fixture in fixtures}
        assert len(by_type) == len(fixtures), "duplicate reconstruction type"
        assert set(by_type) == set(RECONSTRUCTED_TYPES)
        assert not SOURCE_GAPS, "a completed reconstruction remains classified as a source gap"

        live = {record.type: record for record in records()}
        required_mechanics = {
            "target_rules", "effects_formula", "duration_timing", "caps",
            "pvp_restrictions", "assets",
        }
        for kind in RECONSTRUCTED_TYPES:
            fixture = by_type[kind]
            record = live[kind]
            assert fixture["familiar"] == record.familiar
            assert fixture["cost"] == record.cost
            assert fixture["xp_tenths"] == record.xp_tenths
            assert fixture["target_kind"] == record.target_kind
            assert fixture["implementation_state"] == "implemented_reconstructed"
            assert record.state == "implemented_reconstructed"
            assert record.handler != "unavailable"
            assert record.provenance == SOURCE_GAP_FIXTURE
            assert record.asset_bundle == "reconstructed_special_assets"
            assert set(fixture["mechanics"]) == required_mechanics
            assert fixture["direct_evidence"]
            assert fixture["reconstruction_decisions"]
            assert fixture["remaining_uncertainty"]
            assert has_evidence(fixture, "2009-08-21", OFFICIAL, "official_period_snapshot")
            assert has_evidence(fixture, *PERIOD_2009[kind], "contemporaneous_wiki_revision")
            assert has_evidence(fixture, *PERIOD_2011[kind], "period_wiki_revision")
            assert set(fixture["mechanics"]["assets"]) == {
                "confirmed_from_local_source",
                "confirmed_from_later_source",
                "reconstructed_fallback",
            }

        honey = by_type[16]
        assert "+15%/+15%/-15%/-5/-10%" in honey["later_corroboration"][0]["supports"]
        assert "50% second-hit chance" in honey["reconstruction_decisions"][2]

        famine = by_type[50]
        assert "first inventory slot" in famine["mechanics"]["effects_formula"]
        assert "bandages" in famine["mechanics"]["effects_formula"].lower()
        assert "impact 1348" in famine["later_corroboration"][0]["supports"]

        phoenix = by_type[52]
        assert "floor(50*(M-H)/M)" in phoenix["mechanics"]["effects_formula"]
        assert "maximum five" in phoenix["mechanics"]["caps"]
        assert "sequence 11092" in phoenix["mechanics"]["assets"]["reconstructed_fallback"][0]

        forge = by_type[56]
        assert "0..20" in forge["mechanics"]["effects_formula"]
        assert "Castle Wars" in forge["mechanics"]["pvp_restrictions"]

        ent = by_type[58]
        assert "0..17" in ent["mechanics"]["effects_formula"]
        assert "25%" in ent["mechanics"]["effects_formula"]
        assert any("runehq.com" in row.get("url", "") for row in ent["direct_evidence"])

        lava = by_type[64]
        assert "100" in lava["mechanics"]["effects_formula"]
        assert "ten percentage points" in lava["reconstruction_decisions"][0]

        swamp = by_type[65]
        assert "0..20" in swamp["mechanics"]["effects_formula"]
        assert "25% poison" in swamp["mechanics"]["effects_formula"]
        assert "severity 29" in swamp["mechanics"]["effects_formula"]
        assert any("runehq.com" in row.get("url", "") for row in swamp["direct_evidence"])

        corrections = {row["type"]: row for row in data["completed_corrections"]}
        assert corrections[33]["implementation_state"] == "correction_fixture"
    except (AssertionError, KeyError, OSError, ValueError, TypeError) as exc:
        print(f"test_summoning_source_gap_fixtures: error: {exc}", file=sys.stderr)
        return 1

    print(
        "test_summoning_source_gap_fixtures: "
        f"{len(RECONSTRUCTED_TYPES)} dated reconstruction rows, 0 source gaps, 0 errors"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
