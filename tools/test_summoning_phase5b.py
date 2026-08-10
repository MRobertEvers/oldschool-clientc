#!/usr/bin/env python3
"""Structural and staging acceptance for the admitted Phase-5b Dreadfowl.

Phase 5a deliberately preserved a broad, unaccepted ``summoning_roster_530``
experiment as evidence.  Phase 5b admits one much smaller projection alongside
it: the Dreadfowl familiar and pouch, their three models, two ready/walk
sequences, one animation archive, and one framemap.  This test proves that the
new projection has its own ledger and target namespace, that its imported
closure is exact, and that feature-on staging retains it while withholding the
preserved experiment.

Runtime interaction, lifecycle, and client-render assertions belong in the
subsequent real-client acceptance slice.  Keeping this gate structural makes
the cache input contract independently auditable before those paths run.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import io
import json
import sys
import tempfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from types import ModuleType


REPO = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO / "OSRS-Content/osrs239-content"
DEFAULT_BOUNDARY = REPO / "docs/summoning_port/roster_boundary_530.json"
DEFAULT_MANIFEST = REPO / "docs/summoning_port/dreadfowl_cohort_530.ini"
STAGER = REPO / "tools/stage_summoning_overlay.py"

LANE_REL = Path("ported/scape2009_summoning")
COHORT = "summoning_cohort_dreadfowl"
REVIEW = "summoning_roster_530"
COHORT_LEDGER_REL = Path("port/summoning_dreadfowl_530.map")
PRIMARY_LEDGER_REL = Path("port/summoning_530.map")

EXPECTED_IMPORT = {
    "from_rev": "rs530",
    "from_cache": "../../../2009scape/Server/data/cache",
    "to_rev": "osrs239",
    "to_tree": "../../OSRS-Content/osrs239-content",
    "lane": "ported/scape2009_summoning",
    "ledger": "port/summoning_dreadfowl_530.map",
    "prefix": COHORT,
    "npc_base": "26000",
    "obj_base": "46000",
    "model_base": "120000",
    "seq_base": "23000",
    "animset_base": "23000",
    "framemap_base": "10000",
    "npc_sounds": "no",
}

EXPECTED_LEDGER = {
    ("npc", 6825, "dreadfowl", 26000, f"{COHORT}_dreadfowl"),
    ("obj", 12043, "dreadfowl_pouch", 46000, f"{COHORT}_dreadfowl_pouch"),
    ("model", 30429, "model_30429", 120000, f"{COHORT}_model_30429"),
    ("model", 31147, "model_31147", 120001, f"{COHORT}_model_31147"),
    ("model", 30664, "model_30664", 120002, f"{COHORT}_model_30664"),
    ("seq", 5386, "seq_5386", 23000, f"{COHORT}_seq_5386"),
    ("seq", 7808, "seq_7808", 23001, f"{COHORT}_seq_7808"),
    ("frame_archive", 1399, "animset_1399", 23000, f"{COHORT}_animset_1399"),
    ("framemap", 1255, "framemap_1255", 10000, f"{COHORT}_framemap_1255"),
}

EXPECTED_BOUNDARY_COHORT = {
    "prefix": COHORT,
    "ledger": COHORT_LEDGER_REL.as_posix(),
    "expected_ledger_rows": {
        "npc": 1,
        "obj": 1,
        "model": 3,
        "seq": 2,
        "frame_archive": 1,
        "framemap": 1,
    },
    "expected_sources": {
        "npc": [6825],
        "obj": [12043],
        "model": [30429, 31147, 30664],
        "seq": [5386, 7808],
        "frame_archive": [1399],
        "framemap": [1255],
    },
    "destination_ranges": {
        "npc": [26000, 26015],
        "obj": [46000, 46015],
        "model": [120000, 120031],
        "seq": [23000, 23031],
        "frame_archive": [23000, 23031],
        "framemap": [10000, 10031],
    },
}

EXPECTED_PACK_LINES = {
    "npc.alloc": {f"26000={COHORT}_dreadfowl"},
    "npc.client": {f"{COHORT}_dreadfowl"},
    "obj.alloc": {f"46000={COHORT}_dreadfowl_pouch"},
    "obj.client": {f"{COHORT}_dreadfowl_pouch"},
    "seq.alloc": {f"23000={COHORT}_seq_5386", f"23001={COHORT}_seq_7808"},
    "seq.client": {f"{COHORT}_seq_5386", f"{COHORT}_seq_7808"},
    "7_models.pack": {
        f"120000=ported/scape2009_summoning/{COHORT}_model_30429",
        f"120001=ported/scape2009_summoning/{COHORT}_model_31147",
        f"120002=ported/scape2009_summoning/{COHORT}_model_30664",
    },
    "0_animations.pack": {f"23000=ported/scape2009_summoning/{COHORT}_animset_1399"},
    "1_skeletons.pack": {f"10000=ported/scape2009_summoning/{COHORT}_framemap_1255"},
}

SOURCE_COHORT_FILES = {
    LANE_REL / "configs/summoning_cohort_dreadfowl.loc",
    LANE_REL / "configs/summoning_cohort_dreadfowl.npc",
    LANE_REL / "configs/summoning_cohort_dreadfowl.obj",
    LANE_REL / "configs/summoning_cohort_dreadfowl.seq",
    LANE_REL / "configs/summoning_cohort_dreadfowl.spotanim",
    Path("models") / LANE_REL / "summoning_cohort_dreadfowl_model_30429.model",
    Path("models") / LANE_REL / "summoning_cohort_dreadfowl_model_30664.model",
    Path("models") / LANE_REL / "summoning_cohort_dreadfowl_model_31147.model",
    Path("animsets") / LANE_REL / "summoning_cohort_dreadfowl_animset_1399.anim",
    Path("animsets") / LANE_REL / "summoning_cohort_dreadfowl_animset_1399.memberpack",
    Path("framemaps") / LANE_REL / "summoning_cohort_dreadfowl_framemap_1255.base",
}

STAGED_COHORT_FILES = {
    Path("configs/summoning_cohort_dreadfowl.loc"),
    Path("configs/summoning_cohort_dreadfowl.npc"),
    Path("configs/summoning_cohort_dreadfowl.obj"),
    Path("configs/summoning_cohort_dreadfowl.seq"),
    Path("configs/summoning_cohort_dreadfowl.spotanim"),
    Path("models") / LANE_REL / "summoning_cohort_dreadfowl_model_30429.model",
    Path("models") / LANE_REL / "summoning_cohort_dreadfowl_model_30664.model",
    Path("models") / LANE_REL / "summoning_cohort_dreadfowl_model_31147.model",
    Path("animsets") / LANE_REL / "summoning_cohort_dreadfowl_animset_1399.anim",
    Path("animsets") / LANE_REL / "summoning_cohort_dreadfowl_animset_1399.memberpack",
    Path("framemaps") / LANE_REL / "summoning_cohort_dreadfowl_framemap_1255.base",
}

LEDGER_HEADER = ("kind", "src_id", "src_name", "dst_id", "dst_name", "disposition", "signoff")


def load_module(name: str, path: Path) -> ModuleType:
    """Import a local tool without treating tools/ as a Python package."""
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def parse_ini(path: Path) -> dict[str, list[tuple[str, str]]]:
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
    values: dict[str, str] = {}
    for key, value in entries:
        if key in values:
            raise ValueError(f"duplicate {label} key {key!r}")
        values[key] = value
    return values


def parse_ledger(path: Path) -> list[tuple[str, str, str, str, str, str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(
            csv.reader(
                (line for line in stream if line.strip() and not line.lstrip().startswith("#")),
                delimiter="\t",
            )
        )
    if not rows or tuple(rows[0]) != LEDGER_HEADER:
        raise ValueError(f"{path}: invalid seven-column ledger header")
    parsed: list[tuple[str, str, str, str, str, str, str]] = []
    for line_no, row in enumerate(rows[1:], 2):
        if not row:
            continue
        if len(row) != len(LEDGER_HEADER):
            raise ValueError(f"{path}:{line_no}: expected seven columns, got {len(row)}")
        parsed.append(tuple(row))
    return parsed


def config_records(path: Path) -> dict[str, list[str]]:
    records: dict[str, list[str]] = {}
    current: str | None = None
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        text = raw.strip()
        if not text:
            continue
        if text.startswith("[") and text.endswith("]"):
            current = text[1:-1]
            if current in records:
                raise ValueError(f"{path}:{line_no}: duplicate record {current!r}")
            records[current] = []
            continue
        if current is None or "=" not in text:
            raise ValueError(f"{path}:{line_no}: expected config record entry")
        records[current].append(text)
    return records


def properties(lines: list[str], path: Path, record: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in lines:
        key, value = line.split("=", 1)
        if key in values:
            raise ValueError(f"{path}: duplicate {record} property {key!r}")
        values[key] = value
    return values


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def file_contains(path: Path, needle: bytes) -> bool:
    """Search binary or text data without reading an entire asset at once."""
    tail = b""
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            data = tail + chunk
            if needle in data:
                return True
            tail = data[-(len(needle) - 1):]
    return False


def review_footprint(tree: Path, asset_roots: tuple[str, ...]) -> tuple[dict[str, int], dict[str, str], int]:
    """Snapshot the held experiment without hashing its hundreds of models."""
    roots = [tree / LANE_REL]
    roots.extend(tree / root / LANE_REL for root in asset_roots)
    candidates: dict[str, int] = {}
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and REVIEW in path.name:
                candidates[path.relative_to(tree).as_posix()] = path.stat().st_size

    pack_root = tree / LANE_REL / "pack"
    packs = sorted(path for path in pack_root.iterdir() if path.is_file())
    hashes = {path.relative_to(tree).as_posix(): digest(path) for path in packs}
    references = sum(path.read_text(encoding="latin-1").count(REVIEW) for path in packs)
    return candidates, hashes, references


def marker_hits(root: Path, marker: str, text_suffixes: set[str]) -> list[str]:
    hits: list[str] = []
    marker_bytes = marker.encode("utf-8")
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if marker in relative or (path.suffix in text_suffixes and file_contains(path, marker_bytes)):
            hits.append(relative)
    return hits


def cohort_pack_lines(pack_root: Path) -> dict[str, set[str]]:
    """Return every admitted-cohort row, including rows in unexpected packs."""
    result: dict[str, set[str]] = {}
    for path in sorted(pack_root.iterdir()):
        if not path.is_file():
            continue
        rows = {line for line in path.read_text(encoding="latin-1").splitlines() if COHORT in line}
        if rows:
            result[path.name] = rows
    return result


def cohort_named_files(tree: Path, config_root: Path) -> set[Path]:
    """Find all Dreadfowl-named config/assets, not merely expected locations."""
    files: set[Path] = set()
    roots = (
        config_root,
        tree / "models" / LANE_REL,
        tree / "animsets" / LANE_REL,
        tree / "framemaps" / LANE_REL,
    )
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and COHORT in path.name:
                files.add(path.relative_to(tree))
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--boundary", type=Path, default=DEFAULT_BOUNDARY)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()
    args.tree = args.tree.resolve()
    args.boundary = args.boundary.resolve()
    args.manifest = args.manifest.resolve()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    lane = args.tree / LANE_REL
    ledger = args.tree / COHORT_LEDGER_REL
    primary_ledger = args.tree / PRIMARY_LEDGER_REL
    required = (
        args.tree,
        args.boundary,
        args.manifest,
        STAGER,
        lane,
        lane / "configs",
        lane / "pack",
        ledger,
        primary_ledger,
        *(lane / "pack" / name for name in EXPECTED_PACK_LINES),
    )
    for path in required:
        expect(path.exists(), f"required input is missing: {path}")
    if errors:
        return finish(errors, checked)

    try:
        manifest = parse_ini(args.manifest)
        import_values = section_map(manifest.get("import:scape2009", []), "import")
        npc_exports = section_map(manifest.get("export:npc", []), "NPC export")
        obj_exports = section_map(manifest.get("export:obj", []), "object export")
    except ValueError as exc:
        errors.append(f"Dreadfowl import manifest is malformed: {exc}")
    else:
        expect(
            set(manifest) == {"import:scape2009", "export:npc", "export:obj"},
            f"Dreadfowl manifest has unexpected sections: {sorted(manifest)}",
        )
        expect(import_values == EXPECTED_IMPORT,
               f"Dreadfowl import contract changed: {import_values}")
        expect(npc_exports == {"6825": "dreadfowl"},
               f"Dreadfowl manifest NPC roots changed: {npc_exports}")
        expect(obj_exports == {"12043": "dreadfowl_pouch"},
               f"Dreadfowl manifest object roots changed: {obj_exports}")

    try:
        boundary = json.loads(args.boundary.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"cannot read Phase-5b boundary: {exc}")
    else:
        expect(boundary.get("schema") == 1 and boundary.get("phase") == "5b",
               "boundary is not the schema-1 Phase-5b contract")
        expect(boundary.get("admitted_cohorts") == [COHORT],
               f"Phase-5b must admit only Dreadfowl: {boundary.get('admitted_cohorts')!r}")
        expect(boundary.get("cohort_ledgers") == [EXPECTED_BOUNDARY_COHORT],
               "boundary Dreadfowl cohort ledger contract changed")
        roots = boundary.get("admitted_root_sources")
        expect(
            isinstance(roots, dict) and 6825 in roots.get("npc", []) and 12043 in roots.get("obj", []),
            "boundary does not explicitly admit the Dreadfowl NPC/pouch roots",
        )
        review_only = boundary.get("review_only_cohorts")
        expect(
            isinstance(review_only, list)
            and [entry.get("prefix") for entry in review_only if isinstance(entry, dict)] == [REVIEW],
            "boundary no longer preserves the known review-only roster experiment",
        )

    try:
        cohort_rows = parse_ledger(ledger)
        primary_rows = parse_ledger(primary_ledger)
    except ValueError as exc:
        errors.append(f"Dreadfowl ledger cannot be parsed: {exc}")
        cohort_rows = []
        primary_rows = []
    else:
        normalized: set[tuple[str, int, str, int, str]] = set()
        disposition_ok = True
        names_ok = True
        duplicate_keys = False
        source_keys: set[tuple[str, int]] = set()
        for kind, src_id, src_name, dst_id, dst_name, disposition, signoff in cohort_rows:
            try:
                source_key = (kind, int(src_id))
                row = (kind, int(src_id), src_name, int(dst_id), dst_name)
            except ValueError:
                errors.append(f"Dreadfowl ledger has a non-integer source/destination id: {kind} {src_id}->{dst_id}")
                continue
            if source_key in source_keys:
                duplicate_keys = True
            source_keys.add(source_key)
            normalized.add(row)
            disposition_ok &= disposition == "minted" and signoff == "unreviewed"
            names_ok &= dst_name.startswith(f"{COHORT}_")
        expect(len(cohort_rows) == len(EXPECTED_LEDGER),
               f"Dreadfowl ledger has {len(cohort_rows)} rows instead of {len(EXPECTED_LEDGER)}")
        expect(normalized == EXPECTED_LEDGER,
               f"Dreadfowl ledger closure changed: {sorted(normalized)}")
        expect(not duplicate_keys, "Dreadfowl ledger duplicates a kind/source pair")
        expect(disposition_ok, "Dreadfowl ledger is not wholly minted/unreviewed")
        expect(names_ok, "Dreadfowl ledger escaped its separate cohort prefix")
        expect(not any(kind in {"synth", "sound", "song", "sample", "patch", "sprite"}
                       for kind, *_ in cohort_rows),
               "Dreadfowl ledger includes an out-of-scope audio/asset closure")

        primary_targets = {
            (kind, int(dst_id))
            for kind, _src_id, _src_name, dst_id, _dst_name, _disposition, _signoff in primary_rows
            if dst_id.isdigit()
        }
        cohort_targets = {
            (kind, int(dst_id))
            for kind, _src_id, _src_name, dst_id, _dst_name, _disposition, _signoff in cohort_rows
            if dst_id.isdigit()
        }
        expect(not (primary_targets & cohort_targets),
               "Dreadfowl reuses a target ID from the primary/review ledger")
        for kind, source_id in (("npc", 6825), ("obj", 12043)):
            evidence = [
                row for row in primary_rows
                if row[0] == kind and row[1] == str(source_id) and row[4].startswith(f"{REVIEW}_")
            ]
            expect(bool(evidence),
                   f"preserved review ledger no longer retains {kind} source {source_id} as evidence")

    required_files = [args.tree / relative for relative in SOURCE_COHORT_FILES]
    for path in required_files:
        expect(path.is_file(), f"Dreadfowl closure file is missing: {path}")
    if not errors:
        source_named_files = cohort_named_files(args.tree, lane / "configs")
        expect(source_named_files == SOURCE_COHORT_FILES,
               f"Dreadfowl named closure files changed: {sorted(map(str, source_named_files))}")
        source_config_tokens = {
            path.name
            for path in (lane / "configs").iterdir()
            if path.is_file() and file_contains(path, COHORT.encode("utf-8"))
        }
        expect(
            source_config_tokens == {
                "summoning_cohort_dreadfowl.npc",
                "summoning_cohort_dreadfowl.obj",
                "summoning_cohort_dreadfowl.seq",
            },
            f"Dreadfowl records escaped their exact config closure: {sorted(source_config_tokens)}",
        )

        npc_path = lane / "configs/summoning_cohort_dreadfowl.npc"
        obj_path = lane / "configs/summoning_cohort_dreadfowl.obj"
        seq_path = lane / "configs/summoning_cohort_dreadfowl.seq"
        try:
            npc_records = config_records(npc_path)
            obj_records = config_records(obj_path)
            seq_records = config_records(seq_path)
            npc = properties(npc_records[f"{COHORT}_dreadfowl"], npc_path, "Dreadfowl")
            pouch = properties(obj_records[f"{COHORT}_dreadfowl_pouch"], obj_path, "Dreadfowl pouch")
        except (KeyError, ValueError) as exc:
            errors.append(f"Dreadfowl config closure is malformed: {exc}")
        else:
            expect(set(npc_records) == {f"{COHORT}_dreadfowl"},
                   f"Dreadfowl NPC config contains extra records: {sorted(npc_records)}")
            expect(set(obj_records) == {f"{COHORT}_dreadfowl_pouch"},
                   f"Dreadfowl pouch config contains extra records: {sorted(obj_records)}")
            expect(set(seq_records) == {f"{COHORT}_seq_5386", f"{COHORT}_seq_7808"},
                   f"Dreadfowl sequence config contains extra records: {sorted(seq_records)}")
            expect(
                npc.get("model1") == "120000"
                and npc.get("head1") == "120001"
                and npc.get("readyanim") == f"{COHORT}_seq_5386"
                and npc.get("walkanim") == f"{COHORT}_seq_7808",
                "Dreadfowl NPC does not bind its exact body/head/ready/walk closure",
            )
            npc_ops = {key: value for key, value in npc.items() if key.startswith("op")}
            expect(npc_ops == {"op1": "Interact"},
                   f"Dreadfowl NPC exposes an unadmitted operation: {npc_ops}")
            pouch_ops = {key: value for key, value in pouch.items() if key.startswith("ifop")}
            expect(pouch.get("model") == "120002" and pouch_ops == {"ifop4": "Summon"},
                   f"Dreadfowl pouch does not expose exactly its real fourth held-item Summon op: {pouch_ops}")
            config_text = "\n".join((*npc_records[f"{COHORT}_dreadfowl"],
                                     *obj_records[f"{COHORT}_dreadfowl_pouch"],
                                     *(line for lines in seq_records.values() for line in lines))).lower()
            expect(not any(token in config_text for token in ("special", "scroll", "pet", "sound", "synth")),
                   "Dreadfowl config closure includes a deferred special/scroll/pet/audio path")
            for name, lines in seq_records.items():
                frames = [line.split("=", 1)[1] for line in lines if line.startswith("frame=")]
                expect(bool(frames), f"{name} has no frames")
                for frame in frames:
                    try:
                        frame_id = int(frame.split(",", 1)[0])
                    except ValueError:
                        errors.append(f"{name} has malformed frame {frame!r}")
                        continue
                    expect(frame_id >> 16 == 23000,
                           f"{name} frame {frame_id} does not use Dreadfowl animation archive 23000")

        expect((lane / "configs/summoning_cohort_dreadfowl.loc").stat().st_size == 0,
               "Dreadfowl import unexpectedly contains a loc closure")
        expect((lane / "configs/summoning_cohort_dreadfowl.spotanim").stat().st_size == 0,
               "Dreadfowl import unexpectedly contains a spotanim closure")
        for relative in SOURCE_COHORT_FILES - {
            LANE_REL / "configs/summoning_cohort_dreadfowl.loc",
            LANE_REL / "configs/summoning_cohort_dreadfowl.spotanim",
        }:
            expect((args.tree / relative).stat().st_size > 0,
                   f"Dreadfowl closure file is empty: {relative}")

        actual_pack_lines = cohort_pack_lines(lane / "pack")
        expect(actual_pack_lines == EXPECTED_PACK_LINES,
               f"Dreadfowl pack membership changed: {actual_pack_lines}")

    try:
        stage_module = load_module("summoning_phase5b_stager", STAGER)
        review_files_before, review_packs_before, review_refs_before = review_footprint(
            args.tree, stage_module.ASSET_ROOTS
        )
    except (OSError, RuntimeError, ValueError) as exc:
        errors.append(f"cannot prepare Phase-5b staging check: {exc}")
    else:
        expect(len(review_files_before) == 630,
               f"preserved review-only source file count changed: {len(review_files_before)}")
        expect(review_refs_before == 2175,
               f"preserved review-only pack-reference count changed: {review_refs_before}")
        with tempfile.TemporaryDirectory(prefix="summoning_phase5b_stage_") as temporary:
            staged = Path(temporary) / "stage"
            output = io.StringIO()
            try:
                with redirect_stdout(output), redirect_stderr(output):
                    stage_result = stage_module.stage(args.tree, staged, args.boundary)
                    admission = stage_module.load_roster_boundary(args.boundary)
                    exclusion_checks = stage_module.assert_review_only_absent(staged, admission)
            except (OSError, ValueError) as exc:
                errors.append(f"feature-on staging failed: {exc}")
            else:
                expect(stage_result == 0 and exclusion_checks > 0,
                       "feature-on staging did not complete the review-only exclusion audit")
                staged_files = [path for path in staged.rglob("*") if path.is_file()]
                expect(bool(staged_files), "feature-on staging produced zero files")
                expect(
                    all((staged / relative).is_file() for relative in STAGED_COHORT_FILES),
                    "feature-on stage lost a Dreadfowl config or asset closure file",
                )
                staged_named_files = cohort_named_files(staged, staged / "configs")
                expect(staged_named_files == STAGED_COHORT_FILES,
                       f"feature-on stage changed Dreadfowl named closure: {sorted(map(str, staged_named_files))}")
                staged_config_tokens = {
                    path.name
                    for path in (staged / "configs").iterdir()
                    if path.is_file() and file_contains(path, COHORT.encode("utf-8"))
                }
                expect(
                    staged_config_tokens == {
                        "summoning_cohort_dreadfowl.npc",
                        "summoning_cohort_dreadfowl.obj",
                        "summoning_cohort_dreadfowl.seq",
                    },
                    "feature-on stage admitted a Dreadfowl config record outside the exact closure",
                )
                staged_pack_lines = cohort_pack_lines(staged / "pack")
                expect(staged_pack_lines == EXPECTED_PACK_LINES,
                       f"feature-on stage changed Dreadfowl pack membership: {staged_pack_lines}")
                review_hits = marker_hits(staged, REVIEW, stage_module.ADMISSION_TEXT_SUFFIXES)
                dreadfowl_hits = marker_hits(staged, COHORT, stage_module.ADMISSION_TEXT_SUFFIXES)
                expect(not review_hits,
                       "feature-on stage leaked review-only roster data: " + ", ".join(review_hits[:10]))
                expect(bool(dreadfowl_hits), "feature-on stage lost every Dreadfowl marker")

        review_files_after, review_packs_after, review_refs_after = review_footprint(
            args.tree, stage_module.ASSET_ROOTS
        )
        expect(review_files_after == review_files_before,
               "feature-on staging modified the preserved review-only source footprint")
        expect(review_packs_after == review_packs_before,
               "feature-on staging modified a source pack containing review-only rows")
        expect(review_refs_after == review_refs_before,
               "feature-on staging modified preserved review-only pack references")

    return finish(errors, checked)


def finish(errors: list[str], checked: int) -> int:
    for error in errors:
        print(f"test_summoning_phase5b: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase5b: {checked} checks, {len(errors)} errors")
    return 1 if errors or checked == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
