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
class Boundary:
    deferred_slot: int
    safe_synth_sources: frozenset[int]
    admitted_synth_sources: frozenset[int]
    admitted_roots: frozenset[tuple[str, int]]
    vertical_support: frozenset[tuple[str, int]]
    admitted_cohorts: tuple[str, ...]
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


def load_boundary(path: Path, roots: dict[tuple[str, int], Root]) -> Boundary:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read boundary {path}: {exc}") from exc
    if not isinstance(data, dict) or data.get("schema") != 1 or data.get("phase") != "5a":
        raise ValueError(f"boundary {path} must be schema 1 for Phase 5a")

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
    vertical_support = source_pairs("vertical_support_sources")
    if any(kind in ROOT_KINDS and (kind, source_id) in roots for kind, source_id in vertical_support):
        raise ValueError("boundary vertical support must not duplicate a familiar/pouch root")

    admitted_cohorts = _prefixes(data.get("admitted_cohorts", ["summoning_placeholder"]), "admitted_cohorts") \
        if data.get("admitted_cohorts") else ()
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
        reserved_generated_prefixes,
    )


def destination_is_reserved(name: str, boundary: Boundary) -> bool:
    if name == "-":
        return False
    for prefix in boundary.reserved_generated_prefixes:
        if name == prefix or name.startswith(f"{prefix}_"):
            return not any(name == admitted or name.startswith(f"{admitted}_")
                           for admitted in boundary.admitted_cohorts)
    return False


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


def check(manifest_path: Path, tree: Path) -> int:
    ledger_path = tree / MAP_REL
    errors: list[str] = []
    try:
        roots = expected_roots(load_manifest(manifest_path))
    except ValueError as exc:
        print(f"port_summoning_ids: error: {exc}", file=sys.stderr)
        return 1

    rows, parse_errors = parse_ledger(ledger_path)
    errors.extend(parse_errors)
    by_source: dict[tuple[str, int], Row] = {}
    by_destination: dict[tuple[str, int], Row] = {}

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
            if row.dst_name == "summoning":
                errors.append(f"{where}: bare destination name 'summoning' is forbidden")
            if row.kind in NAMED_CONFIG_KINDS and not row.dst_name.startswith("summoning_"):
                errors.append(
                    f"{where}: {row.kind} destination names must start with 'summoning_'"
                )

    checked = 0
    for key, expected_name in roots.items():
        row = by_source.get(key)
        if row is None:
            errors.append(f"{ledger_path}: missing manifest root {key[0]} {key[1]} {expected_name}")
            continue
        checked += 1
        if row.src_name != expected_name:
            errors.append(
                f"{ledger_path}:{row.line}: source name {row.src_name!r}; "
                f"manifest says {expected_name!r}"
            )

    if checked == 0:
        errors.append("summoning ledger check executed zero required-row checks")

    for error in errors:
        print(f"port_summoning_ids: error: {error}", file=sys.stderr)
    print(
        f"port_summoning_ids: checked {checked} required rows, "
        f"{len(rows)} total rows, {len(errors)} errors"
    )
    return 1 if errors else 0


def seed(manifest_path: Path, tree: Path) -> int:
    ledger_path = tree / MAP_REL
    if ledger_path.exists():
        print(f"port_summoning_ids: refusing to overwrite existing {ledger_path}", file=sys.stderr)
        return 1
    try:
        roots = expected_roots(load_manifest(manifest_path))
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
    for (kind, src_id), src_name in sorted(roots.items(), key=lambda item: (item[0][0], item[0][1])):
        lines.append(f"{kind}\t{src_id}\t{src_name}\t-\t-\tpending\tunreviewed")
    ledger_path.parent.mkdir(parents=True, exist_ok=True)
    ledger_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"port_summoning_ids: seeded {len(roots)} rows in {ledger_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true", help="validate the existing ledger")
    action.add_argument("--seed", action="store_true", help="create the ledger only if absent")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    args = parser.parse_args()
    if args.seed:
        return seed(args.manifest.resolve(), args.tree.resolve())
    return check(args.manifest.resolve(), args.tree.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
