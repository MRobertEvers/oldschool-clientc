#!/usr/bin/env python3
"""Structural gate for the Phase-5c familiar/pouch policy cohort."""
from __future__ import annotations

import json
import importlib.util
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CATALOG = REPO / "docs/summoning_port/corpus_cohort_530.json"
MANIFEST = REPO / "docs/summoning_port/corpus_cohort_530.ini"
LEDGER = REPO / "OSRS-Content/osrs239-content/port/summoning_corpus_530.map"
BOUNDARY = REPO / "docs/summoning_port/roster_boundary_530.json"

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module

def main():
    checks = 0
    errors = []
    def expect(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            errors.append(message)

    generator = load("summoning_corpus_generator", REPO / "tools/generate_summoning_corpus.py")
    try:
        generator.build(check=True)
        expect(True, "generator check")
    except Exception as exc:
        expect(False, f"generated policy is stale: {exc}")

    catalog = json.loads(CATALOG.read_text())
    profiles = catalog.get("profiles", [])
    expect(catalog.get("schema") == 1 and catalog.get("phase") == "5c", "catalog schema/phase changed")
    expect(catalog.get("source_policy") == "fixed_pair_allowlist", "catalog widened to a heuristic policy")
    expect(len(profiles) == 45, f"catalog has {len(profiles)} profiles, expected 45")
    expect([p.get("type") for p in profiles] == list(range(3, 48)), "stable familiar types are not exactly 3..47")
    expect(len({(p["source_npc"], p["source_pouch"]) for p in profiles}) == 45, "catalog contains duplicate pairs")
    expect(all(p["target"]["name"].startswith("summoning_cohort_corpus_") for p in profiles), "target names escaped cohort prefix")
    expect(all(p["target"]["pouch_name"].startswith("summoning_cohort_corpus_") for p in profiles), "pouch target names escaped cohort prefix")
    expect(all(p["lifetime_ticks"] > 0 for p in profiles), "catalog contains a non-positive lifetime")
    expect({p["lifetime_ticks"] for p in profiles} >= {9400, 15100}, "long-lived source exceptions were rounded away")
    expect(all(p["drain"] == {"model": "accumulator_mod_100", "interval_ticks": 100, "points": 1} for p in profiles), "drain oracle profile changed")
    expect(all(set(p["deferred_capabilities"]) >= {"special", "scroll", "combat", "audio"} for p in profiles), "deferred capability contract widened")

    manifest = MANIFEST.read_text()
    expect(manifest.count("[export:npc]") == 1 and manifest.count("[export:obj]") == 1, "manifest export sections changed")
    expect(manifest.count("=summoning_cohort_corpus_") == 90, "manifest does not export exactly 90 roots")
    expect("npc_sounds=no" in manifest and "summoning_530.map" not in manifest, "manifest opens an unsafe/audio or primary closure")

    ledger = load("summoning_corpus_ledger", REPO / "tools/port_summoning_ids.py")
    parsed, parse_errors = ledger.parse_ledger(LEDGER)
    expect(not parse_errors, f"corpus ledger does not parse: {parse_errors}")
    expect(len(parsed) == 90, f"corpus root ledger has {len(parsed)} rows, expected 90")
    expect(all(row.disposition == "minted" and row.signoff == "unreviewed" for row in parsed), "corpus rows are not minted/unreviewed")
    expect(all(row.dst_name.startswith("summoning_cohort_corpus_") for row in parsed), "corpus ledger name escaped prefix")
    expect(len({(row.kind, row.dst_id, row.dst_name) for row in parsed}) == 90, "corpus targets are not unique")
    expect({(row.kind, row.src_id) for row in parsed if row.kind in {"npc", "obj"}} ==
           {(kind, value) for p in profiles for kind, value in (("npc", p["source_npc"]), ("obj", p["source_pouch"]))},
           "corpus ledger root closure drifted from the catalog")

    boundary_module = load("summoning_boundary", REPO / "tools/port_summoning_ids.py")
    roots = boundary_module.expected_roots(boundary_module.load_manifest(REPO / "docs/summoning_port/pouches_530.json"))
    try:
        boundary = boundary_module.load_boundary(BOUNDARY, roots)
        expect("summoning_cohort_corpus" in boundary.admitted_cohorts, "boundary does not admit corpus cohort")
        corpus = next(c for c in boundary.cohort_ledgers if c.prefix == "summoning_cohort_corpus")
        expect(dict(corpus.expected_ledger_rows) == {"npc": 45, "obj": 45}, "boundary corpus closure widened/narrowed")
        expect(dict(corpus.destination_ranges)["npc"] == (26016, 26060), "corpus NPC reservation changed")
        expect(dict(corpus.destination_ranges)["obj"] == (46016, 46060), "corpus object reservation changed")
    except Exception as exc:
        expect(False, f"Phase-5c boundary rejected: {exc}")

    if errors:
        for error in errors:
            print(f"  - {error}")
        print(f"test_summoning_phase5c_corpus: {checks} checks, {len(errors)} errors")
        return 1
    print(f"test_summoning_phase5c_corpus: {checks} checks, 0 errors; measured asset closure pending source cache")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
