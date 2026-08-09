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
    "interface",
}
NAMED_CONFIG_KINDS = {"npc", "obj", "loc", "seq", "spotanim", "interface"}
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


def expected_roots(manifest: list[dict[str, object]]) -> dict[tuple[str, int], str]:
    roots: dict[tuple[str, int], str] = {}
    for index, entry in enumerate(manifest, start=1):
        try:
            pouch_name = canonical_name(str(entry["name"]))
            pouch_id = int(entry["pouch"])
            npc_id = int(entry["npc"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError(f"manifest entry {index} has an invalid name/pouch/npc: {exc}") from exc
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
            roots[row_key] = src_name
    return roots


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
