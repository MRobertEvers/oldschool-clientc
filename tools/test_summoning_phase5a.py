#!/usr/bin/env python3
"""Permanent Phase-5a admission-boundary checks for the Summoning port.

The Phase-5a roster files are review candidates, not a grant to import the
full rev-530 Summoning cache closure.  This test keeps that distinction
mechanically enforceable: it validates the 78 familiar/pouch source pairs,
the four deferred Sacred Clay pairs, the single safe source synth, and the
fact that both the import ledger and feature-on staging reject breadth work
that has not been admitted by ``roster_boundary_530.json``.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import io
import json
import subprocess
import sys
import tempfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from types import ModuleType


REPO = Path(__file__).resolve().parents[1]
POUCHES = REPO / "docs/summoning_port/pouches_530.json"
ROSTER_CSV = REPO / "docs/summoning_port/roster_assets_530.csv"
ROSTER_MANIFEST = REPO / "docs/summoning_port/roster_assets_530.ini"
REVIEW_ONLY_DIR = REPO / "docs/summoning_port/review_only"
REVIEW_ONLY_ROSTER_CSV = REVIEW_ONLY_DIR / "roster_assets_530.csv"
REVIEW_ONLY_ROSTER_MANIFEST = REVIEW_ONLY_DIR / "roster_assets_530.ini"
REVIEW_ONLY_SHA256 = {
    REVIEW_ONLY_ROSTER_CSV: "6a64fcc542d4cad7f67d77799d408b3eff888b8d4e041d1048088770717c9491",
    REVIEW_ONLY_ROSTER_MANIFEST: "94773790c8a1f7d90cfafaeccbafea2a96997eebc69ba2a99ce46760c78c6e5a",
}
BOUNDARY = REPO / "docs/summoning_port/roster_boundary_530.json"
TREE = REPO / "OSRS-Content/osrs239-content"
LEDGER_TOOL = REPO / "tools/port_summoning_ids.py"
ROSTER_TOOL = REPO / "tools/summoning_roster_assets.py"
STAGER = REPO / "tools/stage_summoning_overlay.py"
LEDGER_REL = Path("port/summoning_530.map")


def load_module(name: str, path: Path) -> ModuleType:
    """Import a local tool without requiring tools/ to be a package."""
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    # Dataclasses in port_summoning_ids.py resolve their defining module while
    # decorating; register it before executing the file just like normal
    # import machinery does.
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def familiar_name(pouch_name: str) -> str:
    return pouch_name.lower().removesuffix("_pouch").replace("_pouch_", "_")


def parse_ini(path: Path) -> dict[str, list[tuple[str, str]]]:
    """Parse the small cachepack manifest format while retaining duplicates."""
    sections: dict[str, list[tuple[str, str]]] = {}
    current: str | None = None
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        text = raw.strip()
        if not text or text.startswith("#"):
            continue
        if text.startswith("[") and text.endswith("]"):
            current = text[1:-1]
            if current in sections:
                raise ValueError(f"{path}:{line_no}: duplicate section {current!r}")
            sections[current] = []
            continue
        if current is None or "=" not in text:
            raise ValueError(f"{path}:{line_no}: expected section entry")
        key, value = text.split("=", 1)
        sections[current].append((key.strip(), value.strip()))
    return sections


def section_map(entries: list[tuple[str, str]], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for key, value in entries:
        if key in result:
            raise ValueError(f"duplicate {label} entry {key!r}")
        result[key] = value
    return result


def run_ledger_check(module: ModuleType, manifest: Path, boundary: Path, tree: Path) -> tuple[int, str]:
    output = io.StringIO()
    with redirect_stdout(output), redirect_stderr(output):
        result = module.check(manifest, boundary, tree)
    return result, output.getvalue()


def file_contains(path: Path, needle: bytes) -> bool:
    """Search staged binary or text data without loading an asset into memory."""
    tail = b""
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            data = tail + chunk
            if needle in data:
                return True
            tail = data[-(len(needle) - 1):]
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=TREE)
    parser.add_argument("--pouches", type=Path, default=POUCHES)
    parser.add_argument("--roster-csv", type=Path, default=ROSTER_CSV)
    parser.add_argument("--roster-manifest", type=Path, default=ROSTER_MANIFEST)
    parser.add_argument("--boundary", type=Path, default=BOUNDARY)
    args = parser.parse_args()
    args.tree = args.tree.resolve()
    args.pouches = args.pouches.resolve()
    args.roster_csv = args.roster_csv.resolve()
    args.roster_manifest = args.roster_manifest.resolve()
    args.boundary = args.boundary.resolve()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    def expect_rejected(label: str, row: str, diagnostic: str, ledger_module: ModuleType,
                        manifest_path: Path, boundary_path: Path, ledger_text: str) -> None:
        with tempfile.TemporaryDirectory(prefix=f"summoning_phase5a_{label}_") as temporary:
            mutation_tree = Path(temporary) / "content"
            mutation_ledger = mutation_tree / LEDGER_REL
            mutation_ledger.parent.mkdir(parents=True)
            mutation_ledger.write_text(ledger_text + row, encoding="utf-8")
            result, output = run_ledger_check(
                ledger_module, manifest_path, boundary_path, mutation_tree
            )
            expect(result != 0, f"{label} ledger mutation was accepted")
            expect(diagnostic in output, f"{label} rejection lacked {diagnostic!r}: {output}")

    def expect_stage_rejected(label: str, stage_module: ModuleType, tree: Path, lane: Path,
                              boundary_path: Path, diagnostic: str) -> None:
        output = io.StringIO()
        try:
            with redirect_stdout(output), redirect_stderr(output):
                stage_module.audit_roster_admission(tree, lane, boundary_path)
        except ValueError as exc:
            message = str(exc)
            expect(diagnostic in message, f"{label} stage rejection lacked {diagnostic!r}: {message}")
        else:
            expect(False, f"{label} stage mutation was accepted")

    for path in (
        args.tree,
        args.pouches,
        args.roster_csv,
        args.roster_manifest,
        args.boundary,
        *REVIEW_ONLY_SHA256,
    ):
        expect(path.exists(), f"required input is missing: {path}")
    if errors:
        for error in errors:
            print(f"test_summoning_phase5a: error: {error}", file=sys.stderr)
        print(f"test_summoning_phase5a: {checked} checks, {len(errors)} errors")
        return 1

    for path, expected_hash in REVIEW_ONLY_SHA256.items():
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        expect(
            actual_hash == expected_hash,
            f"preserved review-only roster changed: {path} ({actual_hash})",
        )

    generated = subprocess.run(
        [sys.executable, str(ROSTER_TOOL), "--check"],
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    expect(generated.returncode == 0, f"roster generator --check failed:\n{generated.stdout}")
    expect(
        "78 familiars, 0 pet stages (Phase 7 deferred)" in generated.stdout,
        "roster generator did not report the 78-familiar/no-pet boundary",
    )

    pouches = json.loads(args.pouches.read_text(encoding="utf-8"))
    active = [entry for entry in pouches if int(entry["slot"]) >= 0]
    deferred = [entry for entry in pouches if int(entry["slot"]) == -1]
    expect(len(pouches) == 82, f"expected 82 pouch source records, got {len(pouches)}")
    expect(len(active) == 78, f"expected 78 active familiar/pouch pairs, got {len(active)}")
    expect(len(deferred) == 4, f"expected four deferred Sacred Clay pairs, got {len(deferred)}")
    expect(
        all(str(entry["name"]).startswith("SACRED_CLAY_POUCH_") for entry in deferred),
        "the deferred source records are not exactly the Sacred Clay pouches",
    )

    csv_rows = list(csv.DictReader(args.roster_csv.read_text(encoding="utf-8").splitlines()))
    csv_pairs = {(int(row["source_npc"]), int(row["source_obj"])) for row in csv_rows}
    source_pairs = {(int(entry["npc"]), int(entry["pouch"])) for entry in active}
    expect(len(csv_rows) == 78, f"roster CSV has {len(csv_rows)} rows instead of 78")
    expect(all(row["entity_kind"] == "familiar" and row["stage"] == "familiar" for row in csv_rows),
           "roster CSV contains a non-familiar or pet-stage candidate")
    expect(csv_pairs == source_pairs, "roster CSV source familiar/pouch pairs diverge from pouches_530.json")
    expect(len(csv_pairs) == len(csv_rows), "roster CSV duplicates a familiar/pouch source pair")

    candidate_text = args.roster_manifest.read_text(encoding="utf-8")
    try:
        sections = parse_ini(args.roster_manifest)
        imports = section_map(sections.get("import:scape2009", []), "import")
        npc_exports = section_map(sections.get("export:npc", []), "NPC export")
        obj_exports = section_map(sections.get("export:obj", []), "object export")
        synth_exports = section_map(sections.get("export:synth", []), "synth export")
    except ValueError as exc:
        errors.append(f"candidate manifest is malformed: {exc}")
        sections = {}
        imports = {}
        npc_exports = {}
        obj_exports = {}
        synth_exports = {}
    expected_npcs = {str(entry["npc"]) for entry in active} - {"6829"}
    expected_objs = {str(entry["pouch"]) for entry in active} - {"12047"}
    export_sections = {name for name in sections if name.startswith("export:")}
    expect(imports.get("npc_sounds") == "no", "candidate manifest must explicitly set npc_sounds=no")
    expect("npc_sounds=yes" not in candidate_text,
           "candidate manifest enables NPC sound closure")
    expect(imports.get("prefix") == "summoning_roster_530", "candidate manifest lost its reserved cohort prefix")
    expect(export_sections == {"export:npc", "export:obj", "export:seq", "export:synth"},
           f"candidate manifest has unexpected export sections: {sorted(export_sections)}")
    expect(set(npc_exports) == expected_npcs and len(npc_exports) == 77,
           "candidate manifest NPC exports are not the 77 non-proof familiar roots")
    expect(set(obj_exports) == expected_objs and len(obj_exports) == 77,
           "candidate manifest object exports are not the 77 non-proof pouch roots")
    expect(synth_exports == {"188": "familiar_sound_188"},
           f"candidate manifest has an unsafe synth export: {synth_exports}")
    expect(
        all("pet" not in value for value in (*npc_exports.values(), *obj_exports.values())),
        "candidate manifest exports a pet-named entity",
    )

    roster_module = load_module("summoning_phase5a_roster", ROSTER_TOOL)
    ledger_module = load_module("summoning_phase5a_ledger", LEDGER_TOOL)
    stage_module = load_module("summoning_phase5a_stager", STAGER)
    try:
        roster_module.validate_candidate_manifest(candidate_text)
    except ValueError as exc:
        expect(False, f"candidate manifest validator rejected generated output: {exc}")
    else:
        expect(True, "candidate manifest validator accepted generated output")
    npc_sounds_mutation = candidate_text.replace("npc_sounds=no", "npc_sounds=yes", 1)
    expect(npc_sounds_mutation != candidate_text, "candidate manifest lacks npc_sounds=no to mutate")
    try:
        roster_module.validate_candidate_manifest(npc_sounds_mutation)
    except ValueError as exc:
        expect(
            "npc_sounds=no" in str(exc),
            f"npc_sounds=yes mutation had the wrong rejection: {exc}",
        )
    else:
        expect(False, "npc_sounds=yes candidate mutation was accepted")

    result, output = run_ledger_check(ledger_module, args.pouches, args.boundary, args.tree)
    expect(result == 0, f"current ledger check failed:\n{output}")

    roots = ledger_module.expected_roots(ledger_module.load_manifest(args.pouches))
    boundary = ledger_module.load_boundary(args.boundary, roots)
    rows, parse_errors = ledger_module.parse_ledger(args.tree / LEDGER_REL)
    expect(not parse_errors, f"current ledger cannot be parsed: {parse_errors}")
    by_source = {(row.kind, row.src_id): row for row in rows}
    expect(len(roots) == 164, f"pouch ledger must contain 164 roots, got {len(roots)}")
    expect(all(key in by_source for key in roots), "current ledger is missing a familiar/pouch root")

    deferred_roots = [root for root in roots.values() if root.slot == boundary.deferred_slot]
    expect(len(deferred_roots) == 8, f"Sacred Clay must occupy eight ledger roots, got {len(deferred_roots)}")
    for root in deferred_roots:
        row = by_source.get((root.kind, root.src_id))
        expect(
            row is not None and (row.dst_id, row.dst_name, row.disposition, row.signoff)
            == ("-", "-", "deferred", "unreviewed"),
            f"deferred Sacred Clay root {root.kind} {root.src_id} is not deferred/unreviewed",
        )

    review_cohorts = boundary.review_only_cohorts
    expect(len(review_cohorts) == 1 and review_cohorts[0].prefix == "summoning_roster_530",
           "boundary must preserve exactly the known roster experiment as review-only")
    review = review_cohorts[0] if review_cohorts else None
    review_rows = [
        row for row in rows
        if review is not None and ledger_module.destination_review_cohort(row.dst_name, boundary) == review
    ]
    expected_review_counts = dict(review.expected_ledger_rows) if review is not None else {}
    actual_review_counts: dict[str, int] = {}
    for row in review_rows:
        actual_review_counts[row.kind] = actual_review_counts.get(row.kind, 0) + 1
    expect(actual_review_counts == expected_review_counts,
           f"preserved review-only roster counts changed: {actual_review_counts}")
    expect(
        sum(row.kind == "npc" and row.src_name.startswith("pet_") for row in review_rows)
        == (review.expected_pet_npc_rows if review is not None else -1),
        "preserved review-only roster pet count changed",
    )

    synth_rows = [row for row in rows if row.kind == "synth"]
    expected_legacy_synths = review.legacy_synth_sources if review is not None else frozenset()
    expect({row.src_id for row in synth_rows} == expected_legacy_synths,
           f"ledger synth sources changed: {[row.src_id for row in synth_rows]}")
    safe_synth = by_source.get(("synth", 188))
    expect(
        safe_synth is not None and (safe_synth.dst_id, safe_synth.dst_name,
                                    safe_synth.disposition, safe_synth.signoff)
        == ("20004", "summoning_roster_530_synth_188", "minted", "unreviewed"),
        "safe synth 188 is not confined to the preserved review-only cohort",
    )
    expect(
        not any(ledger_module.destination_is_reserved(row.dst_name, boundary) for row in rows),
        "current ledger contains an unadmitted generated roster destination",
    )

    review_prefix = review.prefix if review is not None else "summoning_roster_530"
    source_roots = [args.tree / "ported/scape2009_summoning"]
    source_roots.extend(args.tree / root / "ported/scape2009_summoning" for root in stage_module.ASSET_ROOTS)
    source_candidate_files = [
        path
        for root in source_roots if root.exists()
        for path in root.rglob("*")
        if path.is_file() and review_prefix in path.name
    ]
    pack_root = args.tree / "ported/scape2009_summoning/pack"
    source_pack_references = sum(
        path.read_text(encoding="latin-1").count(review_prefix)
        for path in pack_root.iterdir() if path.is_file()
    )
    expect(
        len(source_candidate_files) == (review.expected_source_files if review is not None else -1),
        f"preserved review-only source file count changed: {len(source_candidate_files)}",
    )
    expect(
        source_pack_references == (review.expected_pack_references if review is not None else -1),
        f"preserved review-only pack-reference count changed: {source_pack_references}",
    )

    ledger_text = (args.tree / LEDGER_REL).read_text(encoding="utf-8")
    if not ledger_text.endswith("\n"):
        ledger_text += "\n"
    mutation_rows = (
        (
            "pet",
            "npc\t990001\tphase5a_pet\t-\t-\tpending\tunreviewed\n",
            "outside the Phase-5a familiar/pouch boundary",
        ),
        (
            "scroll",
            "obj\t990002\tphase5a_scroll\t-\t-\tpending\tunreviewed\n",
            "outside the Phase-5a familiar/pouch boundary",
        ),
        (
            "tertiary",
            "obj\t990003\tphase5a_tertiary\t-\t-\tpending\tunreviewed\n",
            "outside the Phase-5a familiar/pouch boundary",
        ),
        (
            "potion",
            "obj\t990004\tphase5a_potion\t-\t-\tpending\tunreviewed\n",
            "outside the Phase-5a familiar/pouch boundary",
        ),
        (
            "unsafe_synth",
            "synth\t4161\tsynth_4161\t-\t-\tpending\tunreviewed\n",
            "not the permitted source synth 188",
        ),
        (
            "unadmitted_prefix",
            "model\t990006\tmodel_990006\t199006\tsummoning_cohort_probe\tminted\tunreviewed\n",
            "belongs to an unadmitted roster cohort",
        ),
        (
            "review_only_growth",
            "model\t990007\tmodel_990007\t199007\tsummoning_roster_530_probe\tminted\tunreviewed\n",
            "row counts changed",
        ),
    )
    for label, row, diagnostic in mutation_rows:
        expect_rejected(label, row, diagnostic, ledger_module, args.pouches, args.boundary, ledger_text)

    # The staging check must remain useful even when it is called separately
    # from a full cache build.  These fixtures deliberately contain no cache
    # dependencies, so they isolate admission behavior and stay fast.
    with tempfile.TemporaryDirectory(prefix="summoning_phase5a_stage_") as temporary:
        stage_tree = Path(temporary) / "content"
        lane = stage_tree / "ported/scape2009_summoning"
        lane.mkdir(parents=True)
        (lane / "PROVENANCE.md").write_text("Phase 5a fixture\n", encoding="utf-8")
        baseline_output = io.StringIO()
        with redirect_stdout(baseline_output), redirect_stderr(baseline_output):
            baseline_checks = stage_module.audit_roster_admission(stage_tree, lane, args.boundary)
        expect(baseline_checks > 0, "stage admission baseline executed zero checks")

        cohort = lane / "configs/unadmitted.npc"
        cohort.parent.mkdir(parents=True)
        cohort.write_text("[summoning_cohort_probe_pet]\n", encoding="utf-8")
        expect_stage_rejected(
            "unadmitted pet cohort", stage_module, stage_tree, lane, args.boundary,
            "unadmitted generated cohort",
        )
        cohort.unlink()

        cohort.write_text("[summoning_roster_530_pet_probe]\n", encoding="utf-8")
        try:
            with redirect_stdout(baseline_output), redirect_stderr(baseline_output):
                held_checks = stage_module.audit_roster_admission(stage_tree, lane, args.boundary)
        except ValueError as exc:
            expect(False, f"preserved review-only cohort was rejected: {exc}")
        else:
            expect(held_checks > 0, "preserved review-only cohort executed zero checks")
        cohort.unlink()

        unsafe_pack = lane / "pack/4_soundeffects.pack"
        unsafe_pack.parent.mkdir(parents=True)
        unsafe_pack.write_text("9900=summoning_safe_synth_4161_probe\n", encoding="utf-8")
        expect_stage_rejected(
            "unsafe synth", stage_module, stage_tree, lane, args.boundary,
            "unsafe synth source 4161",
        )

    # A clean admission audit is not enough on its own: this exercises the
    # actual second-pass staging walk and ensures no dormant candidate token is
    # copied into the feature-on cache input through a pack/config/asset path.
    with tempfile.TemporaryDirectory(prefix="summoning_phase5a_real_stage_") as temporary:
        staged = Path(temporary) / "stage"
        stage_output = io.StringIO()
        try:
            with redirect_stdout(stage_output), redirect_stderr(stage_output):
                stage_result = stage_module.stage(args.tree, staged, args.boundary)
        except (OSError, ValueError) as exc:
            expect(False, f"real-tree feature-on staging failed: {exc}")
        else:
            staged_files = [path for path in staged.rglob("*") if path.is_file()]
            marker = b"summoning_roster_530"
            marker_hits = [
                path.relative_to(staged).as_posix()
                for path in staged_files
                if marker in path.relative_to(staged).as_posix().encode("utf-8")
                or file_contains(path, marker)
            ]
            expect(stage_result == 0 and bool(staged_files), "real-tree stage produced zero files")
            expect(
                not marker_hits,
                "real-tree stage contains reserved roster candidate data: " + ", ".join(marker_hits[:10]),
            )
            expect(
                not (staged / "pack/4_soundeffects.pack").exists(),
                "review-only-only sound pack leaked into the staged tree",
            )
            post_source_files = [
                path
                for root in source_roots if root.exists()
                for path in root.rglob("*")
                if path.is_file() and review_prefix in path.name
            ]
            post_pack_references = sum(
                path.read_text(encoding="latin-1").count(review_prefix)
                for path in pack_root.iterdir() if path.is_file()
            )
            expect(
                len(post_source_files) == len(source_candidate_files),
                "feature-on staging modified preserved review-only source files",
            )
            expect(
                post_pack_references == source_pack_references,
                "feature-on staging modified preserved review-only pack entries",
            )

    server_sources = args.tree / "server/scripts"
    server_review_hits = [
        path.relative_to(server_sources).as_posix()
        for path in server_sources.rglob("*")
        if path.is_file() and review_prefix.encode("utf-8") in path.read_bytes()
    ]
    expect(
        not server_review_hits,
        "server source references a review-only roster cohort: " + ", ".join(server_review_hits[:10]),
    )

    for error in errors:
        print(f"test_summoning_phase5a: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase5a: {checked} checks, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
