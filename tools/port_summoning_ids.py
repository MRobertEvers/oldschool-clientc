#!/usr/bin/env python3
"""Check the rev-530 -> osrs239 Summoning id ledger.

The 82-entry pouch manifest is the source inventory for the first ledger rung:
one pouch obj and one familiar npc per entry.  Later asset-import slices may
append closure rows (models, frame archives, sequences, and so on), but may not
remove or duplicate these 164 roots.

Human columns (destination, disposition, signoff) are never regenerated.
``--seed`` only creates a missing ledger; ``--check`` is the permanent build
bar used by ``make -C src test-port``.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = REPO_ROOT / "docs" / "summoning_port" / "pouches_530.json"
DEFAULT_BOUNDARY = REPO_ROOT / "docs" / "summoning_port" / "roster_boundary_530.json"
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"
MAP_REL = Path("port") / "summoning_530.map"

HEADER = ("kind", "src_id", "src_name", "dst_id", "dst_name", "disposition", "signoff")
ALLOWED_KINDS = {
    "npc",
    "obj",
    "loc",
    "seq",
    "spotanim",
    "model",
    "framemap",
    "frame_archive",
    "sprite",
    "texture",
    "sound",
    "synth",
    "interface",
}
NAMED_CONFIG_KINDS = {"npc", "obj", "loc", "seq", "spotanim", "interface"}
ROOT_KINDS = {"npc", "obj"}
GENERATED_COHORT_NAME = re.compile(r"^summoning_(?:roster|cohort)_[a-z0-9_]+$")
ALLOWED_DISPOSITIONS = {
    "pending",
    "minted",
    "mapped",
    "present",
    "dropped",
    "deferred",
    "hand-mapped",
}


@dataclass(frozen=True)
class Row:
    kind: str
    src_id: int
    src_name: str
    dst_id: str
    dst_name: str
    disposition: str
    signoff: str
    line: int


@dataclass(frozen=True)
class Root:
    kind: str
    src_id: int
    src_name: str
    slot: int


@dataclass(frozen=True)
class ReviewOnlyCohort:
    prefix: str
    expected_ledger_rows: tuple[tuple[str, int], ...]
    expected_pet_npc_rows: int
    expected_source_files: int
    expected_pack_references: int
    source_fingerprint_sha256: str
    legacy_synth_sources: frozenset[int]


@dataclass(frozen=True)
class CohortLedger:
    """A separately owned, explicitly admitted Phase-5b import ledger.

    The broad ``summoning_roster_530`` ledger is preserved as evidence only.
    An admitted cohort must therefore use a separate map file and declare its
    complete source closure and destination reservation up front.
    """

    prefix: str
    ledger_rel: Path
    expected_ledger_rows: tuple[tuple[str, int], ...]
    expected_sources: frozenset[tuple[str, int]]
    destination_ranges: tuple[tuple[str, tuple[int, int]], ...]


@dataclass(frozen=True)
class Boundary:
    deferred_slot: int
    safe_synth_sources: frozenset[int]
    admitted_synth_sources: frozenset[int]
    admitted_roots: frozenset[tuple[str, int]]
    vertical_support: frozenset[tuple[str, int]]
    admitted_cohorts: tuple[str, ...]
    cohort_ledgers: tuple[CohortLedger, ...]
    review_only_cohorts: tuple[ReviewOnlyCohort, ...]
    reserved_generated_prefixes: tuple[str, ...]


def canonical_name(value: str) -> str:
    return value.strip().lower()


def load_manifest(path: Path) -> list[dict[str, object]]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read manifest {path}: {exc}") from exc
    if not isinstance(data, list) or not data:
        raise ValueError(f"manifest {path} must be a non-empty JSON array")
    return data


def expected_roots(manifest: list[dict[str, object]]) -> dict[tuple[str, int], Root]:
    roots: dict[tuple[str, int], Root] = {}
    for index, entry in enumerate(manifest, start=1):
        try:
            pouch_name = canonical_name(str(entry["name"]))
            pouch_id = int(entry["pouch"])
            npc_id = int(entry["npc"])
            slot = int(entry["slot"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError(f"manifest entry {index} has an invalid name/pouch/npc/slot: {exc}") from exc
        # The extracted source table is irregular: three constants omit the
        # _POUCH suffix, while SACRED_CLAY_POUCH_1..4 put it before a variant
        # number.  Preserve the obj source spelling and only strip the marker
        # for the paired familiar label.
        familiar_name = pouch_name.removesuffix("_pouch").replace("_pouch_", "_")
        for key, name in (("obj", pouch_id), ("npc", npc_id)):
            row_key = (key, name)
            src_name = pouch_name if key == "obj" else familiar_name
            if row_key in roots:
                raise ValueError(f"manifest duplicates {key} source id {name}")
            roots[row_key] = Root(key, name, src_name, slot)
    return roots


def _id_set(value: object, label: str) -> frozenset[int]:
    if not isinstance(value, list):
        raise ValueError(f"boundary {label} must be an array")
    try:
        result = frozenset(int(item) for item in value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"boundary {label} must contain integer ids") from exc
    if len(result) != len(value) or any(item < 0 for item in result):
        raise ValueError(f"boundary {label} must contain distinct non-negative ids")
    return result


def _prefixes(value: object, label: str) -> tuple[str, ...]:
    if not isinstance(value, list) or not value:
        raise ValueError(f"boundary {label} must be a non-empty array")
    if not all(isinstance(item, str) and item.startswith("summoning_") for item in value):
        raise ValueError(f"boundary {label} entries must start with 'summoning_'")
    if len(set(value)) != len(value):
        raise ValueError(f"boundary {label} contains duplicates")
    return tuple(value)


def _review_only_cohorts(value: object) -> tuple[ReviewOnlyCohort, ...]:
    """Parse preserved generated work that must not enter the feature cache.

    Review-only data is intentionally retained because it is useful porting
    evidence from another agent.  Its exact ledger footprint is part of the
    boundary, which prevents that preservation exception from turning into an
    open-ended way to add pets or unsafe synths.
    """
    if not isinstance(value, list) or not value:
        raise ValueError("boundary review_only_cohorts must be a non-empty array")
    cohorts: list[ReviewOnlyCohort] = []
    prefixes: list[str] = []
    for index, raw in enumerate(value, start=1):
        label = f"review_only_cohorts[{index}]"
        if not isinstance(raw, dict):
            raise ValueError(f"boundary {label} must be an object")
        prefix = raw.get("prefix")
        if not isinstance(prefix, str) or not prefix.startswith("summoning_"):
            raise ValueError(f"boundary {label}.prefix must start with 'summoning_'")
        if raw.get("status") != "preserved_experiment":
            raise ValueError(f"boundary {label}.status must be preserved_experiment")
        counts = raw.get("expected_ledger_rows")
        if not isinstance(counts, dict) or not counts:
            raise ValueError(f"boundary {label}.expected_ledger_rows must be a non-empty object")
        normalized_counts: list[tuple[str, int]] = []
        for kind, count in counts.items():
            if kind not in ALLOWED_KINDS:
                raise ValueError(f"boundary {label} has unsupported row kind {kind!r}")
            try:
                count = int(count)
            except (TypeError, ValueError) as exc:
                raise ValueError(f"boundary {label}.{kind} must be an integer") from exc
            if count <= 0:
                raise ValueError(f"boundary {label}.{kind} must be positive")
            normalized_counts.append((kind, count))
        try:
            pet_count = int(raw["expected_pet_npc_rows"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError(f"boundary {label}.expected_pet_npc_rows must be an integer") from exc
        if pet_count < 0 or pet_count > dict(normalized_counts).get("npc", 0):
            raise ValueError(f"boundary {label}.expected_pet_npc_rows is outside its NPC count")
        try:
            source_files = int(raw["expected_source_files"])
            pack_references = int(raw["expected_pack_references"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError(
                f"boundary {label}.expected_source_files/expected_pack_references must be integers"
            ) from exc
        if source_files <= 0 or pack_references <= 0:
            raise ValueError(f"boundary {label} source-file and pack-reference counts must be positive")
        source_fingerprint = raw.get("source_fingerprint_sha256")
        if (not isinstance(source_fingerprint, str) or
                re.fullmatch(r"[0-9a-f]{64}", source_fingerprint) is None):
            raise ValueError(f"boundary {label}.source_fingerprint_sha256 must be a lowercase SHA-256")
        synths = _id_set(raw.get("legacy_synth_sources"), f"{label}.legacy_synth_sources")
        if synths != {188, 4161, 4265, 4372, 5753, 5776, 5777, 5792}:
            raise ValueError(f"boundary {label}.legacy_synth_sources does not match the preserved experiment")
        if dict(normalized_counts).get("synth") != len(synths):
            raise ValueError(f"boundary {label} synth count disagrees with legacy_synth_sources")
        prefixes.append(prefix)
        cohorts.append(ReviewOnlyCohort(
            prefix,
            tuple(sorted(normalized_counts)),
            pet_count,
            source_files,
            pack_references,
            source_fingerprint,
            synths,
        ))
    if len(set(prefixes)) != len(prefixes):
        raise ValueError("boundary review_only_cohorts contains duplicate prefixes")
    return tuple(cohorts)


def _root_pair_name(root: Root) -> str:
    """Return the shared familiar label for either half of a pouch pair."""
    if root.kind == "npc":
        return root.src_name
    return root.src_name.removesuffix("_pouch").replace("_pouch_", "_")


def _cohort_ledger_path(value: object, label: str) -> Path:
    if not isinstance(value, str) or not value:
        raise ValueError(f"boundary {label}.ledger must be a non-empty relative path")
    if "\\" in value:
        raise ValueError(f"boundary {label}.ledger must use a repository-relative POSIX path")
    path = Path(value)
    if (path.is_absolute() or path.suffix != ".map" or len(path.parts) < 2 or
            path.parts[0] != "port" or any(part in {"", ".", ".."} for part in path.parts) or
            path.as_posix() != value):
        raise ValueError(f"boundary {label}.ledger must be a safe port/*.map relative path")
    return path


def _cohort_ledgers(
    value: object,
    phase: str,
    admitted_cohorts: tuple[str, ...],
    admitted_roots: frozenset[tuple[str, int]],
    roots: dict[tuple[str, int], Root],
) -> tuple[CohortLedger, ...]:
    """Parse the closed Phase-5b cohort-ledger contract.

    Source IDs may intentionally duplicate rows in the preserved review-only
    experiment.  Destination IDs/names may not: every admitted cohort owns a
    new, disjoint range in a separate ledger.
    """
    if phase == "5a":
        if value not in (None, []):
            raise ValueError("Phase 5a must not declare dedicated cohort ledgers")
        if admitted_cohorts:
            raise ValueError("Phase 5a must not admit a generated cohort")
        return ()

    if not isinstance(value, list) or not value:
        raise ValueError("Phase 5b must declare at least one dedicated cohort ledger")
    if not admitted_cohorts:
        raise ValueError("Phase 5b cohort ledgers require admitted_cohorts")

    cohorts: list[CohortLedger] = []
    prefixes: list[str] = []
    ledger_paths: list[Path] = []
    all_pair_roots: dict[str, frozenset[tuple[str, int]]] = {}
    for key, root in roots.items():
        all_pair_roots.setdefault(_root_pair_name(root), frozenset())
    # Rebuild the values rather than mutating frozensets above; this keeps the
    # resulting pair contract easy to read and avoids a second source of truth.
    all_pair_roots = {
        pair_name: frozenset(
            key for key, root in roots.items() if _root_pair_name(root) == pair_name
        )
        for pair_name in all_pair_roots
    }

    for index, raw in enumerate(value, start=1):
        label = f"cohort_ledgers[{index}]"
        if not isinstance(raw, dict):
            raise ValueError(f"boundary {label} must be an object")
        prefix = raw.get("prefix")
        if not isinstance(prefix, str) or not prefix.startswith("summoning_cohort_"):
            raise ValueError(f"boundary {label}.prefix must start with 'summoning_cohort_'")
        if prefix not in admitted_cohorts:
            raise ValueError(f"boundary {label}.prefix is not in admitted_cohorts")
        ledger_rel = _cohort_ledger_path(raw.get("ledger"), label)

        raw_counts = raw.get("expected_ledger_rows")
        if not isinstance(raw_counts, dict) or not raw_counts:
            raise ValueError(f"boundary {label}.expected_ledger_rows must be a non-empty object")
        counts: dict[str, int] = {}
        for kind, raw_count in raw_counts.items():
            if kind not in ALLOWED_KINDS:
                raise ValueError(f"boundary {label} has unsupported row kind {kind!r}")
            try:
                count = int(raw_count)
            except (TypeError, ValueError) as exc:
                raise ValueError(f"boundary {label}.{kind} must be an integer") from exc
            if count <= 0:
                raise ValueError(f"boundary {label}.{kind} must be positive")
            counts[kind] = count

        raw_sources = raw.get("expected_sources")
        if not isinstance(raw_sources, dict) or set(raw_sources) != set(counts):
            raise ValueError(
                f"boundary {label}.expected_sources must name exactly the expected row kinds"
            )
        sources: set[tuple[str, int]] = set()
        for kind, count in counts.items():
            source_ids = _id_set(raw_sources[kind], f"{label}.expected_sources.{kind}")
            if len(source_ids) != count:
                raise ValueError(
                    f"boundary {label}.expected_sources.{kind} has {len(source_ids)} ids; expected {count}"
                )
            sources.update((kind, source_id) for source_id in source_ids)

        raw_ranges = raw.get("destination_ranges")
        if not isinstance(raw_ranges, dict) or set(raw_ranges) != set(counts):
            raise ValueError(
                f"boundary {label}.destination_ranges must name exactly the expected row kinds"
            )
        ranges: dict[str, tuple[int, int]] = {}
        for kind, count in counts.items():
            raw_range = raw_ranges[kind]
            if not isinstance(raw_range, list) or len(raw_range) != 2:
                raise ValueError(f"boundary {label}.destination_ranges.{kind} must be a [first, last] pair")
            try:
                lower, upper = (int(raw_range[0]), int(raw_range[1]))
            except (TypeError, ValueError) as exc:
                raise ValueError(
                    f"boundary {label}.destination_ranges.{kind} must contain integer ids"
                ) from exc
            if lower < 0 or upper < lower:
                raise ValueError(f"boundary {label}.destination_ranges.{kind} is invalid")
            if upper - lower + 1 < count:
                raise ValueError(
                    f"boundary {label}.destination_ranges.{kind} cannot hold {count} target ids"
                )
            ranges[kind] = (lower, upper)

        cohort_roots = frozenset(source for source in sources if source[0] in ROOT_KINDS)
        if not cohort_roots:
            raise ValueError(f"boundary {label} must include a familiar/pouch root pair")
        if not cohort_roots <= admitted_roots:
            raise ValueError(f"boundary {label} claims a root that is not admitted")
        for source in cohort_roots:
            root = roots.get(source)
            if root is None:
                raise ValueError(f"boundary {label} claims a root absent from the pouch manifest")
            pair_roots = all_pair_roots[_root_pair_name(root)]
            if not pair_roots <= cohort_roots:
                raise ValueError(
                    f"boundary {label} must include both familiar/pouch roots for {_root_pair_name(root)!r}"
                )

        prefixes.append(prefix)
        ledger_paths.append(ledger_rel)
        cohorts.append(CohortLedger(
            prefix,
            ledger_rel,
            tuple(sorted(counts.items())),
            frozenset(sources),
            tuple(sorted(ranges.items())),
        ))

    if len(set(prefixes)) != len(prefixes):
        raise ValueError("boundary cohort_ledgers contains duplicate prefixes")
    if len(set(ledger_paths)) != len(ledger_paths):
        raise ValueError("boundary cohort_ledgers contains duplicate ledger paths")
    if set(prefixes) != set(admitted_cohorts):
        raise ValueError("boundary admitted_cohorts must each have exactly one dedicated ledger")

    # Ranges are reservations, not merely hints.  Two cohorts may use the
    # same numerical IDs only when their cache kinds differ.
    for left_index, left in enumerate(cohorts):
        for right in cohorts[left_index + 1:]:
            for kind, (left_lower, left_upper) in left.destination_ranges:
                right_range = dict(right.destination_ranges).get(kind)
                if right_range is None:
                    continue
                right_lower, right_upper = right_range
                if max(left_lower, right_lower) <= min(left_upper, right_upper):
                    raise ValueError(
                        f"boundary cohort destination ranges overlap for {kind}: "
                        f"{left.prefix!r} and {right.prefix!r}"
                    )
    return tuple(cohorts)


def load_boundary(path: Path, roots: dict[tuple[str, int], Root]) -> Boundary:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read boundary {path}: {exc}") from exc
    if (not isinstance(data, dict) or data.get("schema") != 1 or
            data.get("phase") not in {"5a", "5b"}):
        raise ValueError(f"boundary {path} must be schema 1 for Phase 5a or 5b")
    phase = str(data["phase"])

    inventory = data.get("source_inventory")
    if not isinstance(inventory, dict):
        raise ValueError("boundary source_inventory must be an object")
    try:
        pouch_records = int(inventory["pouch_records"])
        active_pairs = int(inventory["active_familiar_pairs"])
        deferred_pairs = int(inventory["deferred_sacred_clay_pairs"])
        deferred_slot = int(inventory["deferred_slot"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError("boundary source_inventory has invalid counts") from exc

    root_records = len(roots) // 2
    active_records = len({root.src_id for root in roots.values() if root.kind == "npc" and root.slot >= 0})
    deferred_records = len({root.src_id for root in roots.values() if root.kind == "npc" and root.slot == deferred_slot})
    if (pouch_records, active_pairs, deferred_pairs) != (root_records, active_records, deferred_records):
        raise ValueError(
            "boundary source_inventory disagrees with pouch manifest: "
            f"expected {(root_records, active_records, deferred_records)}, got "
            f"{(pouch_records, active_pairs, deferred_pairs)}"
        )
    if data.get("candidate_entity_kinds") != ["familiar", "pouch"]:
        raise ValueError("boundary candidate_entity_kinds must be exactly familiar/pouch")
    deferred = data.get("deferred_entity_kinds")
    if not isinstance(deferred, dict) or set(deferred) != {"pet", "scroll", "tertiary", "potion"}:
        raise ValueError("boundary must explicitly defer pet, scroll, tertiary and potion")

    safe_synth_sources = _id_set(data.get("safe_synth_sources"), "safe_synth_sources")
    if safe_synth_sources != {188}:
        raise ValueError("Phase 5a permits exactly the documented safe synth source 188")
    admitted_synth_sources = _id_set(data.get("admitted_synth_sources"), "admitted_synth_sources")
    if not admitted_synth_sources <= safe_synth_sources:
        raise ValueError("boundary admits an unsafe synth source")

    def source_pairs(key: str) -> frozenset[tuple[str, int]]:
        raw = data.get(key)
        if not isinstance(raw, dict):
            raise ValueError(f"boundary {key} must be an object")
        pairs: set[tuple[str, int]] = set()
        for kind, ids in raw.items():
            if kind not in ALLOWED_KINDS:
                raise ValueError(f"boundary {key} has unsupported kind {kind!r}")
            pairs.update((kind, source_id) for source_id in _id_set(ids, f"{key}.{kind}"))
        return frozenset(pairs)

    admitted_roots = source_pairs("admitted_root_sources")
    if not admitted_roots <= frozenset(roots):
        raise ValueError("boundary admits a root that is absent from the pouch manifest")
    pairs: dict[str, set[tuple[str, int]]] = {}
    for key, root in roots.items():
        pair_name = root.src_name if root.kind == "npc" else (
            root.src_name.removesuffix("_pouch").replace("_pouch_", "_")
        )
        pairs.setdefault(pair_name, set()).add(key)
    for pair_name, pair_roots in pairs.items():
        if len(pair_roots) != 2:
            raise ValueError(f"pouch manifest has incomplete familiar/pouch pair {pair_name!r}")
        admitted_count = len(pair_roots & admitted_roots)
        if admitted_count not in {0, 2}:
            raise ValueError(f"boundary admits only one half of familiar/pouch pair {pair_name!r}")
    vertical_support = source_pairs("vertical_support_sources")
    if any(kind in ROOT_KINDS and (kind, source_id) in roots for kind, source_id in vertical_support):
        raise ValueError("boundary vertical support must not duplicate a familiar/pouch root")

    raw_cohorts = data.get("admitted_cohorts")
    if not isinstance(raw_cohorts, list):
        raise ValueError("boundary admitted_cohorts must be an array")
    admitted_cohorts = _prefixes(raw_cohorts, "admitted_cohorts") if raw_cohorts else ()
    cohort_ledgers = _cohort_ledgers(
        data.get("cohort_ledgers"), phase, admitted_cohorts, admitted_roots, roots
    )
    review_only_cohorts = _review_only_cohorts(data.get("review_only_cohorts"))
    if set(admitted_cohorts) & {cohort.prefix for cohort in review_only_cohorts}:
        raise ValueError("boundary cannot admit and review-hold the same cohort")
    reserved_generated_prefixes = _prefixes(
        data.get("reserved_generated_prefixes"), "reserved_generated_prefixes"
    )
    return Boundary(
        deferred_slot,
        safe_synth_sources,
        admitted_synth_sources,
        admitted_roots,
        vertical_support,
        admitted_cohorts,
        cohort_ledgers,
        review_only_cohorts,
        reserved_generated_prefixes,
    )


def _matches_prefix(name: str, prefix: str) -> bool:
    return name == prefix or name.startswith(f"{prefix}_")


def destination_review_cohort(name: str, boundary: Boundary) -> ReviewOnlyCohort | None:
    if name == "-":
        return None
    return next(
        (cohort for cohort in boundary.review_only_cohorts if _matches_prefix(name, cohort.prefix)),
        None,
    )


def destination_is_reserved(name: str, boundary: Boundary) -> bool:
    """Whether a generated destination has neither admission nor a review hold."""
    if name == "-":
        return False
    is_generated = GENERATED_COHORT_NAME.match(name) is not None or any(
        _matches_prefix(name, prefix)
        for prefix in boundary.reserved_generated_prefixes
    )
    return is_generated and destination_review_cohort(name, boundary) is None and not any(
        _matches_prefix(name, admitted) for admitted in boundary.admitted_cohorts
    )


def parse_ledger(path: Path) -> tuple[list[Row], list[str]]:
    errors: list[str] = []
    rows: list[Row] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        return [], [f"cannot read ledger {path}: {exc}"]

    saw_header = False
    for line_no, raw in enumerate(lines, start=1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        fields = raw.split("\t")
        if not saw_header and tuple(fields) == HEADER:
            saw_header = True
            continue
        if len(fields) != len(HEADER):
            errors.append(
                f"{path}:{line_no}: expected {len(HEADER)} tab-separated fields, got {len(fields)}"
            )
            continue
        kind, src_id_text, src_name, dst_id, dst_name, disposition, signoff = fields
        try:
            src_id = int(src_id_text)
        except ValueError:
            errors.append(f"{path}:{line_no}: invalid source id {src_id_text!r}")
            continue
        rows.append(Row(kind, src_id, src_name, dst_id, dst_name, disposition, signoff, line_no))
    if not saw_header:
        errors.append(f"{path}: missing exact tab-separated header: {' '.join(HEADER)}")
    return rows, errors


COHORT_GENERATED_SOURCE_PREFIXES = {
    "model": "model",
    "seq": "seq",
    "frame_archive": "animset",
    "framemap": "framemap",
}


def expected_cohort_source_name(
    kind: str, source_id: int, roots: dict[tuple[str, int], Root]
) -> str | None:
    """Return the importer-owned source label when it is deterministic."""
    root = roots.get((kind, source_id))
    if root is not None:
        return root.src_name
    generated_prefix = COHORT_GENERATED_SOURCE_PREFIXES.get(kind)
    if generated_prefix is None:
        return None
    return f"{generated_prefix}_{source_id}"


def check_cohort_ledger(
    cohort: CohortLedger,
    tree: Path,
    roots: dict[tuple[str, int], Root],
    primary_destinations: dict[tuple[str, int], Row],
    primary_destination_names: dict[tuple[str, str], Row],
    cohort_destinations: dict[tuple[str, int], tuple[CohortLedger, Row]],
    cohort_destination_names: dict[tuple[str, str], tuple[CohortLedger, Row]],
) -> tuple[int, int, list[str]]:
    """Validate one separately-owned Phase-5b closure ledger.

    A source ID may appear in the review-only ledger as historical evidence,
    but destinations and their reserved ranges must be wholly new.  This is
    deliberately stricter than the primary ledger: all cohort rows are known
    imports, so pending, omitted, or extra closure rows are boundary failures.
    """
    ledger_path = tree / cohort.ledger_rel
    rows, errors = parse_ledger(ledger_path)
    expected_counts = dict(cohort.expected_ledger_rows)
    expected_ranges = dict(cohort.destination_ranges)
    actual_counts: dict[str, int] = {}
    actual_sources: set[tuple[str, int]] = set()
    by_source: dict[tuple[str, int], Row] = {}
    by_destination: dict[tuple[str, int], Row] = {}
    by_destination_name: dict[tuple[str, str], Row] = {}

    # A destination reservation must be entirely disjoint from the primary
    # ledger, rather than merely avoiding the one target currently minted.
    for kind, (lower, upper) in cohort.destination_ranges:
        for (primary_kind, destination_id), primary_row in primary_destinations.items():
            if primary_kind == kind and lower <= destination_id <= upper:
                errors.append(
                    f"{ledger_path}: reserved {kind} destination range {lower}..{upper} "
                    f"overlaps primary target {destination_id} at {MAP_REL}:{primary_row.line}"
                )

    for row in rows:
        where = f"{ledger_path}:{row.line}"
        actual_counts[row.kind] = actual_counts.get(row.kind, 0) + 1
        source_key = (row.kind, row.src_id)
        actual_sources.add(source_key)

        if row.kind not in ALLOWED_KINDS:
            errors.append(f"{where}: unknown kind {row.kind!r}")
        if row.src_id < 0:
            errors.append(f"{where}: source id must be non-negative")
        if not row.src_name or row.src_name == "-":
            errors.append(f"{where}: source name must be stated")
        expected_source_name = expected_cohort_source_name(row.kind, row.src_id, roots)
        if expected_source_name is not None and row.src_name != expected_source_name:
            errors.append(
                f"{where}: source name must be exactly {expected_source_name!r}"
            )
        if source_key not in cohort.expected_sources:
            errors.append(
                f"{where}: source {row.kind} {row.src_id} is outside the exact "
                f"{cohort.prefix!r} closure"
            )
        previous = by_source.get(source_key)
        if previous is not None:
            errors.append(
                f"{where}: duplicate source {row.kind} {row.src_id} (first at line {previous.line})"
            )
        else:
            by_source[source_key] = row

        if (row.disposition, row.signoff) != ("minted", "unreviewed"):
            errors.append(
                f"{where}: admitted cohort rows must remain minted/unreviewed"
            )
        if (row.dst_id == "-") != (row.dst_name == "-"):
            errors.append(f"{where}: destination id and name must both be '-' or both be stated")
            continue
        if row.dst_id == "-":
            errors.append(f"{where}: admitted cohort row has no destination")
            continue
        try:
            destination_id = int(row.dst_id)
        except ValueError:
            errors.append(f"{where}: invalid destination id {row.dst_id!r}")
            continue
        if destination_id < 0:
            errors.append(f"{where}: destination id must be non-negative")
            continue

        expected_name = f"{cohort.prefix}_{expected_source_name or row.src_name}"
        if row.dst_name != expected_name:
            errors.append(
                f"{where}: destination name must be exactly {expected_name!r}"
            )
        destination_range = expected_ranges.get(row.kind)
        if destination_range is None:
            errors.append(
                f"{where}: kind {row.kind!r} has no admitted destination range"
            )
        else:
            lower, upper = destination_range
            if not lower <= destination_id <= upper:
                errors.append(
                    f"{where}: destination {row.kind} {destination_id} is outside "
                    f"its admitted range {lower}..{upper}"
                )

        destination_key = (row.kind, destination_id)
        previous_destination = by_destination.get(destination_key)
        if previous_destination is not None:
            errors.append(
                f"{where}: duplicate destination {row.kind} {destination_id} "
                f"(first at line {previous_destination.line})"
            )
        else:
            by_destination[destination_key] = row
        previous_primary = primary_destinations.get(destination_key)
        if previous_primary is not None:
            errors.append(
                f"{where}: destination {row.kind} {destination_id} collides with primary ledger "
                f"target at {MAP_REL}:{previous_primary.line}"
            )
        previous_cohort = cohort_destinations.get(destination_key)
        if previous_cohort is not None:
            previous_cohort_spec, previous_row = previous_cohort
            errors.append(
                f"{where}: destination {row.kind} {destination_id} collides with "
                f"{previous_cohort_spec.prefix!r} at {tree / previous_cohort_spec.ledger_rel}:{previous_row.line}"
            )
        else:
            cohort_destinations[destination_key] = (cohort, row)

        name_key = (row.kind, row.dst_name)
        previous_name = by_destination_name.get(name_key)
        if previous_name is not None:
            errors.append(
                f"{where}: duplicate destination name {row.kind} {row.dst_name!r} "
                f"(first at line {previous_name.line})"
            )
        else:
            by_destination_name[name_key] = row
        previous_primary_name = primary_destination_names.get(name_key)
        if previous_primary_name is not None:
            errors.append(
                f"{where}: destination name {row.dst_name!r} collides with primary ledger "
                f"target at {MAP_REL}:{previous_primary_name.line}"
            )
        previous_cohort_name = cohort_destination_names.get(name_key)
        if previous_cohort_name is not None:
            previous_cohort_spec, previous_row = previous_cohort_name
            errors.append(
                f"{where}: destination name {row.dst_name!r} collides with "
                f"{previous_cohort_spec.prefix!r} at {tree / previous_cohort_spec.ledger_rel}:{previous_row.line}"
            )
        else:
            cohort_destination_names[name_key] = (cohort, row)

    if actual_counts != expected_counts:
        errors.append(
            f"{ledger_path}: exact row counts changed: expected {expected_counts}, got {actual_counts}"
        )
    if actual_sources != cohort.expected_sources:
        errors.append(
            f"{ledger_path}: exact source closure changed: expected "
            f"{sorted(cohort.expected_sources)}, got {sorted(actual_sources)}"
        )
    return sum(expected_counts.values()), len(rows), errors


def check(manifest_path: Path, boundary_path: Path, tree: Path) -> int:
    ledger_path = tree / MAP_REL
    errors: list[str] = []
    try:
        roots = expected_roots(load_manifest(manifest_path))
        boundary = load_boundary(boundary_path, roots)
    except ValueError as exc:
        print(f"port_summoning_ids: error: {exc}", file=sys.stderr)
        return 1

    rows, parse_errors = parse_ledger(ledger_path)
    errors.extend(parse_errors)
    by_source: dict[tuple[str, int], Row] = {}
    by_destination: dict[tuple[str, int], Row] = {}
    by_destination_name: dict[tuple[str, str], Row] = {}
    review_rows: dict[str, list[Row]] = {
        cohort.prefix: [] for cohort in boundary.review_only_cohorts
    }

    for row in rows:
        where = f"{ledger_path}:{row.line}"
        if row.kind not in ALLOWED_KINDS:
            errors.append(f"{where}: unknown kind {row.kind!r}")
        if row.src_id < 0:
            errors.append(f"{where}: source id must be non-negative")
        if not row.src_name or row.src_name == "-":
            errors.append(f"{where}: source name must be stated")
        if row.disposition not in ALLOWED_DISPOSITIONS:
            errors.append(f"{where}: unknown disposition {row.disposition!r}")
        if not row.signoff:
            errors.append(f"{where}: signoff must be stated")
        source_key = (row.kind, row.src_id)
        review_cohort = destination_review_cohort(row.dst_name, boundary)
        if review_cohort is not None:
            review_rows[review_cohort.prefix].append(row)
            if row.disposition != "minted" or row.signoff != "unreviewed":
                errors.append(
                    f"{where}: preserved experiment {review_cohort.prefix!r} must remain minted/unreviewed"
                )
        if (row.kind in ROOT_KINDS and source_key not in roots and
                source_key not in boundary.vertical_support and review_cohort is None):
            errors.append(
                f"{where}: {row.kind} {row.src_id} is outside the Phase-5a familiar/pouch boundary"
            )
        if (row.kind == "synth" and row.src_id not in boundary.safe_synth_sources and
                review_cohort is None):
            errors.append(f"{where}: synth {row.src_id} is not the permitted source synth 188")
        if destination_is_reserved(row.dst_name, boundary):
            errors.append(f"{where}: destination {row.dst_name!r} belongs to an unadmitted roster cohort")
        admitted_cohort = next(
            (cohort for cohort in boundary.cohort_ledgers if _matches_prefix(row.dst_name, cohort.prefix)),
            None,
        )
        if admitted_cohort is not None:
            errors.append(
                f"{where}: destination {row.dst_name!r} belongs in dedicated cohort ledger "
                f"{admitted_cohort.ledger_rel}"
            )
        previous = by_source.get(source_key)
        if previous is not None:
            errors.append(
                f"{where}: duplicate source {row.kind} {row.src_id} (first at line {previous.line})"
            )
        else:
            by_source[source_key] = row

        if (row.dst_id == "-") != (row.dst_name == "-"):
            errors.append(f"{where}: destination id and name must both be '-' or both be stated")
        if row.dst_id != "-":
            try:
                dst_id = int(row.dst_id)
            except ValueError:
                errors.append(f"{where}: invalid destination id {row.dst_id!r}")
            else:
                if dst_id < 0:
                    errors.append(f"{where}: destination id must be non-negative")
                destination_key = (row.kind, dst_id)
                previous_dst = by_destination.get(destination_key)
                if previous_dst is not None:
                    errors.append(
                        f"{where}: duplicate destination {row.kind} {dst_id} "
                        f"(first at line {previous_dst.line})"
                    )
                else:
                    by_destination[destination_key] = row
                destination_name_key = (row.kind, row.dst_name)
                previous_name = by_destination_name.get(destination_name_key)
                if previous_name is not None:
                    errors.append(
                        f"{where}: duplicate destination name {row.kind} {row.dst_name!r} "
                        f"(first at line {previous_name.line})"
                    )
                else:
                    by_destination_name[destination_name_key] = row
            if row.dst_name == "summoning":
                errors.append(f"{where}: bare destination name 'summoning' is forbidden")
            if row.kind in NAMED_CONFIG_KINDS and not row.dst_name.startswith("summoning_"):
                errors.append(
                    f"{where}: {row.kind} destination names must start with 'summoning_'"
                )

    checked = 0
    for key, root in roots.items():
        row = by_source.get(key)
        if row is None:
            errors.append(f"{ledger_path}: missing manifest root {key[0]} {key[1]} {root.src_name}")
            continue
        checked += 1
        if row.src_name != root.src_name:
            errors.append(
                f"{ledger_path}:{row.line}: source name {row.src_name!r}; "
                f"manifest says {root.src_name!r}"
            )
        admitted = key in boundary.admitted_roots
        deferred = root.slot == boundary.deferred_slot
        review_cohort = destination_review_cohort(row.dst_name, boundary)
        if admitted:
            if row.dst_id == "-":
                errors.append(f"{ledger_path}:{row.line}: admitted root {key[0]} {key[1]} has no destination")
        elif review_cohort is not None:
            if deferred:
                errors.append(
                    f"{ledger_path}:{row.line}: deferred Sacred Clay root {key[0]} {key[1]} entered review cohort"
                )
        else:
            expected_disposition = "deferred" if deferred else "pending"
            if row.dst_id != "-" or row.dst_name != "-":
                errors.append(
                    f"{ledger_path}:{row.line}: unadmitted root {key[0]} {key[1]} must not claim a destination"
                )
            if row.disposition != expected_disposition or row.signoff != "unreviewed":
                errors.append(
                    f"{ledger_path}:{row.line}: unadmitted root {key[0]} {key[1]} must be "
                    f"{expected_disposition}/unreviewed"
                )

    for source_id in boundary.safe_synth_sources:
        row = by_source.get(("synth", source_id))
        if row is None:
            errors.append(f"{ledger_path}: missing safe synth candidate {source_id}")
            continue
        checked += 1
        review_cohort = destination_review_cohort(row.dst_name, boundary)
        if source_id not in boundary.admitted_synth_sources and review_cohort is None:
            if (row.dst_id != "-" or row.dst_name != "-" or row.disposition != "pending" or
                    row.signoff != "unreviewed"):
                errors.append(
                    f"{ledger_path}:{row.line}: unadmitted safe synth {source_id} must remain pending/unreviewed without a destination"
                )

    for cohort in boundary.review_only_cohorts:
        cohort_rows = review_rows[cohort.prefix]
        actual_counts: dict[str, int] = {}
        for row in cohort_rows:
            actual_counts[row.kind] = actual_counts.get(row.kind, 0) + 1
        expected_counts = dict(cohort.expected_ledger_rows)
        if actual_counts != expected_counts:
            errors.append(
                f"{ledger_path}: preserved experiment {cohort.prefix!r} row counts changed: "
                f"expected {expected_counts}, got {actual_counts}"
            )
        pet_count = sum(row.kind == "npc" and row.src_name.startswith("pet_") for row in cohort_rows)
        if pet_count != cohort.expected_pet_npc_rows:
            errors.append(
                f"{ledger_path}: preserved experiment {cohort.prefix!r} pet NPC count changed: "
                f"expected {cohort.expected_pet_npc_rows}, got {pet_count}"
            )
        synth_sources = {row.src_id for row in cohort_rows if row.kind == "synth"}
        if synth_sources != cohort.legacy_synth_sources:
            errors.append(
                f"{ledger_path}: preserved experiment {cohort.prefix!r} synth sources changed: "
                f"expected {sorted(cohort.legacy_synth_sources)}, got {sorted(synth_sources)}"
            )
        checked += sum(expected_counts.values()) + 2

    cohort_destinations: dict[tuple[str, int], tuple[CohortLedger, Row]] = {}
    cohort_destination_names: dict[tuple[str, str], tuple[CohortLedger, Row]] = {}
    total_rows = len(rows)
    for cohort in boundary.cohort_ledgers:
        cohort_checked, cohort_rows, cohort_errors = check_cohort_ledger(
            cohort,
            tree,
            roots,
            by_destination,
            by_destination_name,
            cohort_destinations,
            cohort_destination_names,
        )
        checked += cohort_checked
        total_rows += cohort_rows
        errors.extend(cohort_errors)

    if checked == 0:
        errors.append("summoning ledger check executed zero required-row checks")

    for error in errors:
        print(f"port_summoning_ids: error: {error}", file=sys.stderr)
    print(
        f"port_summoning_ids: checked {checked} required rows, "
        f"{total_rows} total ledger rows, {len(errors)} errors"
    )
    return 1 if errors else 0


def seed(manifest_path: Path, boundary_path: Path, tree: Path) -> int:
    ledger_path = tree / MAP_REL
    if ledger_path.exists():
        print(f"port_summoning_ids: refusing to overwrite existing {ledger_path}", file=sys.stderr)
        return 1
    try:
        roots = expected_roots(load_manifest(manifest_path))
        boundary = load_boundary(boundary_path, roots)
    except ValueError as exc:
        print(f"port_summoning_ids: error: {exc}", file=sys.stderr)
        return 1

    lines = [
        "# port/summoning_530.map — rev-530 ids translated into the osrs239 Summoning lane.",
        "#",
        "# Generated source columns are checked against docs/summoning_port/pouches_530.json.",
        "# Destination, disposition and signoff columns are human decisions and are never regenerated.",
        "# A '-' destination is deliberately unresolved; source ids must never be copied as targets.",
        "# Imported named records use the summoning_* prefix to prevent cross-namespace trigger capture.",
        "\t".join(HEADER),
    ]
    for (kind, src_id), root in sorted(roots.items(), key=lambda item: (item[0][0], item[0][1])):
        disposition = "deferred" if root.slot == boundary.deferred_slot else "pending"
        lines.append(f"{kind}\t{src_id}\t{root.src_name}\t-\t-\t{disposition}\tunreviewed")
    for source_id in sorted(boundary.safe_synth_sources):
        lines.append(f"synth\t{source_id}\tsynth_{source_id}\t-\t-\tpending\tunreviewed")
    ledger_path.parent.mkdir(parents=True, exist_ok=True)
    ledger_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"port_summoning_ids: seeded {len(roots)} roots and {len(boundary.safe_synth_sources)} synth candidates in {ledger_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true", help="validate the existing ledger")
    action.add_argument("--seed", action="store_true", help="create the ledger only if absent")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--boundary", type=Path, default=DEFAULT_BOUNDARY)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    args = parser.parse_args()
    if args.seed:
        return seed(args.manifest.resolve(), args.boundary.resolve(), args.tree.resolve())
    return check(args.manifest.resolve(), args.boundary.resolve(), args.tree.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
