#!/usr/bin/env python3
"""Audit that every deferred Summoning pet/audio record has a disposition."""
from __future__ import annotations
import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
LEDGER = REPO / "OSRS-Content/osrs239-content/port/summoning_530.map"
AUDIT = REPO / "docs/summoning_port/phase7_completion_audit.json"
TREE = REPO / "OSRS-Content/osrs239-content"
STAGER = REPO / "tools/stage_summoning_overlay.py"

def main() -> int:
    errors: list[str] = []; checks = 0
    def expect(ok: bool, message: str) -> None:
        nonlocal checks; checks += 1
        if not ok: errors.append(message)
    data = json.loads(AUDIT.read_text(encoding="utf-8"))
    expect(data.get("schema") == 1, "unsupported audit schema")
    rows = [line.split("\t") for line in LEDGER.read_text(encoding="utf-8").splitlines()[1:] if line]
    pets = {"npc": 0, "obj": 0}; synths: set[int] = set()
    for row in rows:
        if len(row) != 7: continue
        kind, source, source_name, _, dest_name, _, _ = row
        if kind == "synth": synths.add(int(source))
        if kind in pets and ("_pet_" in source_name or "_pet_" in dest_name): pets[kind] += 1
    declared = data["review_only_pet_rows"]
    for kind, count in pets.items(): expect(declared.get(kind) == count, f"pet {kind} count {count} lacks an exact disposition")
    sources = {int(key) for key in data["synth_sources"]}
    expect(synths <= sources, f"review synths missing dispositions: {sorted(synths-sources)}")
    expect(data["synth_sources"].get("188", "").startswith("admitted_"), "source synth 188 admission missing")
    expect(all(data["synth_sources"][str(source)].startswith("withheld_") for source in synths-{188}), "review synth must remain explicitly withheld")
    # The audit only counts if the feature-on staging boundary enforces it.
    with tempfile.TemporaryDirectory(prefix="summoning_phase7f_") as root:
        staged = Path(root) / "stage"
        result = subprocess.run([sys.executable, str(STAGER), "--tree", str(TREE), "--out", str(staged)],
                                cwd=REPO, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=False)
        expect(result.returncode == 0, "feature-on staging failed completion-audit enforcement")
        staged_text = "\n".join(path.read_text(encoding="latin-1", errors="ignore")
                                  for path in staged.rglob("*") if path.is_file()) if staged.exists() else ""
        for source in synths-{188}:
            expect(f"synth_{source}" not in staged_text, f"withheld synth {source} leaked to stage")
        expect("summoning_roster_530_pet_" not in staged_text, "review-only pet record leaked to stage")
    for error in errors: print(f"test_summoning_phase7f: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase7f: {checks} checks, {len(errors)} errors")
    return int(bool(errors))

if __name__ == "__main__": raise SystemExit(main())
