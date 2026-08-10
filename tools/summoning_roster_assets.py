#!/usr/bin/env python3
"""Build an auditable RS530 Summoning familiar/pet asset manifest.

The cache alone answers *which sequences can pose an NPC*: its NPC definition
points at a BAS (base-animation set), and the BAS supplies ready/walk sequences.
The combat roles are deliberately not guessed from that compatible set.  They
come from 2009scape's ``npc_configs.json``, which is what its server actually
uses for melee, defence, and death animations.

Run ``tools/entity_viewer/ev_catalog --rev rs530 ...`` once first.  This script
then joins that catalog with the 82-entry pouch table, the source combat config,
and ``Pets.java``.  It writes two generated artifacts:

* a CSV that tells reviewers exactly why each animation is selected; and
* a cachepack import manifest for the assets/configs (not gameplay behaviour).

The manifest intentionally lists audio for review but imports no synths.  The
Summoning plan establishes that only summon sound 188 is safe to byte-copy;
the remaining source audio is cross-revision content that needs an explicit
semantic decision rather than an automatic import.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO.parent / "2009scape"
FAMILIAR_SOURCE = SOURCE / "Server/src/main/content/global/skill/summoning/familiar"
# Pets.java contains the CAT_6 overgrown item id, but that id is absent from
# the 530 object table distributed with this source cache.  Keep the NPC stage
# in the roster; omit only the unavailable inventory definition.
UNAVAILABLE_SOURCE_OBJECTS = {15092}


@dataclass(frozen=True)
class Entity:
    kind: str
    entry: str
    stage: str
    npc_id: int
    obj_id: int | None


def split_java_args(text: str) -> list[str]:
    """Split a Java constructor argument list without mistaking commas in calls."""
    parts: list[str] = []
    start = 0
    depth = 0
    for index, char in enumerate(text):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == "," and depth == 0:
            parts.append(text[start:index].strip())
            start = index + 1
    parts.append(text[start:].strip())
    return parts


def extract_java_enum_calls(path: Path) -> list[tuple[str, list[str]]]:
    """Extract enum constructor calls from Pets.java before its fields begin."""
    text = path.read_text(encoding="utf-8")
    begin = text.index("public enum Pets")
    end = text.index("private static final Map", begin)
    body = text[begin:end]
    found: list[tuple[str, list[str]]] = []
    cursor = 0
    while True:
        match = re.search(r"\b([A-Z][A-Z0-9_]*)\s*\(", body[cursor:])
        if not match:
            break
        name = match.group(1)
        open_at = cursor + match.end() - 1
        depth = 0
        close_at = open_at
        while close_at < len(body):
            char = body[close_at]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    break
            close_at += 1
        if close_at >= len(body):
            raise ValueError(f"unterminated Pets enum value {name}")
        found.append((name, split_java_args(body[open_at + 1 : close_at])))
        cursor = close_at + 1
    return found


def as_int(value: str, context: str) -> int:
    try:
        return int(value.strip())
    except ValueError as exc:
        raise ValueError(f"{context}: expected integer, got {value!r}") from exc


def load_pouch_entities(path: Path) -> list[Entity]:
    records = json.loads(path.read_text(encoding="utf-8"))
    entities: list[Entity] = []
    for record in records:
        # The four Sacred Clay pouches and Phoenix have slot -1 and are not
        # familiar pouches; importing them would pull unrelated minigames in.
        if record["slot"] < 0:
            continue
        name = record["name"].lower()
        if name.endswith("_pouch"):
            name = name[: -len("_pouch")]
        entities.append(Entity("familiar", name, "familiar", record["npc"], record["pouch"]))
    return entities


def load_pet_entities(path: Path) -> list[Entity]:
    entities: list[Entity] = []
    for name, args in extract_java_enum_calls(path):
        if len(args) < 8:
            raise ValueError(f"Pets.{name}: expected at least eight constructor arguments")
        items = [as_int(args[index], f"Pets.{name}") for index in range(3)]
        npcs = [as_int(args[index], f"Pets.{name}") for index in range(3, 6)]
        for stage, npc_id, obj_id in zip(("baby", "grown", "overgrown"), npcs, items):
            if npc_id >= 0:
                entities.append(Entity("pet", name.lower(), stage, npc_id, obj_id if obj_id >= 0 else None))
    return entities


def load_catalog(path: Path) -> dict[int, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as file:
        return {int(row["npc_id"]): row for row in csv.DictReader(file)}


def load_combat(path: Path) -> dict[int, dict[str, str]]:
    records = json.loads(path.read_text(encoding="utf-8"))
    return {int(record["id"]): record for record in records}


def animation_id(config: dict[str, str], key: str) -> str:
    value = config.get(key)
    # 2009scape's config parser treats absent fields as absent.  Its literal
    # zeroes are fallback placeholders, not a selected familiar animation.
    return "" if value in (None, "", "0", 0) else str(value)


def entity_name(entity: Entity) -> str:
    if entity.kind == "familiar":
        return entity.entry
    return f"pet_{entity.entry}_{entity.stage}"


def add_unique(target: OrderedDict[int, str], source_id: int, name: str) -> None:
    if source_id >= 0:
        target.setdefault(source_id, name)


def load_minted_sources(ledger: Path) -> set[tuple[str, int]]:
    """Return source IDs that already have a concrete destination in the lane."""
    minted: set[tuple[str, int]] = set()
    for line in ledger.read_text(encoding="utf-8").splitlines():
        columns = line.split("\t")
        if len(columns) >= 5 and columns[0] != "kind" and columns[3] != "-":
            try:
                minted.add((columns[0], int(columns[1])))
            except ValueError:
                pass
    return minted


def build_manifest(
    entities: list[Entity], combat: dict[int, dict[str, str]], minted: set[tuple[str, int]]
) -> str:
    npcs: OrderedDict[int, str] = OrderedDict()
    objs: OrderedDict[int, str] = OrderedDict()
    seqs: OrderedDict[int, str] = OrderedDict()
    for entity in entities:
        name = entity_name(entity)
        if ("npc", entity.npc_id) not in minted:
            add_unique(npcs, entity.npc_id, name)
        if (entity.obj_id is not None and entity.obj_id not in UNAVAILABLE_SOURCE_OBJECTS
                and ("obj", entity.obj_id) not in minted):
            suffix = "pouch" if entity.kind == "familiar" else "item"
            add_unique(objs, entity.obj_id, f"{name}_{suffix}")
        config = combat.get(entity.npc_id, {})
        for role, field in (
            ("attack", "melee_animation"),
            ("magic", "magic_animation"),
            ("range", "range_animation"),
            ("defend", "defence_animation"),
            ("death", "death_animation"),
        ):
            value = animation_id(config, field)
            if value and ("seq", int(value)) not in minted:
                add_unique(seqs, int(value), f"{name}_{role}")

    lines = [
        "# Generated by tools/summoning_roster_assets.py. Do not hand-edit.",
        "# This imports client assets/configs only; gameplay and pet lifecycle remain authored content.",
        "[import:scape2009]",
        "from_rev=rs530",
        "from_cache=../../../2009scape/Server/data/cache",
        "to_rev=osrs239",
        "to_tree=../../OSRS-Content/osrs239-content",
        "lane=ported/scape2009_summoning",
        "ledger=port/summoning_530.map",
        "# A dedicated prefix prevents this batch from truncating the proof's summoning.* configs.",
        "prefix=summoning_roster_530",
        "",
        "[export:npc]",
    ]
    lines.extend(f"{source_id}={name}" for source_id, name in npcs.items())
    lines.extend(["", "[export:obj]"])
    lines.extend(f"{source_id}={name}" for source_id, name in objs.items())
    lines.extend(["", "# Combat-role sequences come from npc_configs.json; BAS movement sequences are pulled by NPC closure.", "[export:seq]"])
    lines.extend(f"{source_id}={name}" for source_id, name in seqs.items())
    lines.append("")
    return "\n".join(lines)


def write_csv(path: Path, entities: list[Entity], catalog: dict[int, dict[str, str]], combat: dict[int, dict[str, str]]) -> None:
    fields = [
        "entity_kind", "entry", "stage", "source_npc", "source_obj", "source_name",
        "base_seqs", "framemaps", "rig_candidate_count", "attack_seq", "magic_seq",
        "range_seq", "defend_seq", "death_seq", "combat_audio", "asset_status",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        for entity in entities:
            cat = catalog.get(entity.npc_id, {})
            config = combat.get(entity.npc_id, {})
            writer.writerow({
                "entity_kind": entity.kind,
                "entry": entity.entry,
                "stage": entity.stage,
                "source_npc": entity.npc_id,
                "source_obj": "" if entity.obj_id is None else entity.obj_id,
                "source_name": cat.get("npc_name", config.get("name", "")),
                "base_seqs": cat.get("seed_seqs", ""),
                "framemaps": cat.get("framemaps", ""),
                "rig_candidate_count": cat.get("rig_match_seqs", ""),
                "attack_seq": animation_id(config, "melee_animation"),
                "magic_seq": animation_id(config, "magic_animation"),
                "range_seq": animation_id(config, "range_animation"),
                "defend_seq": animation_id(config, "defence_animation"),
                "death_seq": animation_id(config, "death_animation"),
                "combat_audio": config.get("combat_audio", "") or "",
                "asset_status": (
                    "source_obj_missing" if entity.obj_id in UNAVAILABLE_SOURCE_OBJECTS else "catalogued"
                ),
            })


def sound_csv_text(familiar_source: Path) -> str:
    """Audit source sound calls without treating source synth IDs as portable."""
    pattern = re.compile(r"Sounds\.([A-Z][A-Z0-9_]*_(\d+))")
    rows: list[dict[str, str]] = []
    for source in sorted(familiar_source.rglob("*.java")):
        for line_number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
            for match in pattern.finditer(line):
                sound_id = int(match.group(2))
                rows.append({
                    "source_file": str(source.relative_to(familiar_source)),
                    "line": str(line_number),
                    "sound_symbol": match.group(1),
                    "source_sound_id": str(sound_id),
                    "port_disposition": "eligible_byte_copy" if sound_id == 188 else "review_required",
                })
    output = ["source_file,line,sound_symbol,source_sound_id,port_disposition"]
    output.extend(
        ",".join(row[field] for field in (
            "source_file", "line", "sound_symbol", "source_sound_id", "port_disposition",
        ))
        for row in rows
    )
    return "\n".join(output) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pouches", type=Path, default=REPO / "docs/summoning_port/pouches_530.json")
    parser.add_argument("--pets", type=Path, default=SOURCE / "Server/src/main/content/global/skill/summoning/pet/Pets.java")
    parser.add_argument("--familiar-source", type=Path, default=FAMILIAR_SOURCE)
    parser.add_argument("--npc-config", type=Path, default=SOURCE / "Server/data/configs/npc_configs.json")
    parser.add_argument("--catalog", type=Path, default=REPO / "out/rs530_summoning_anims/npc_catalog.csv")
    parser.add_argument("--csv", type=Path, default=REPO / "docs/summoning_port/roster_assets_530.csv")
    parser.add_argument("--manifest", type=Path, default=REPO / "docs/summoning_port/roster_assets_530.ini")
    parser.add_argument("--ledger", type=Path, default=REPO / "OSRS-Content/osrs239-content/port/summoning_530.map")
    parser.add_argument("--sound-csv", type=Path, default=REPO / "docs/summoning_port/familiar_sound_refs_530.csv")
    parser.add_argument("--check", action="store_true", help="fail when generated outputs differ")
    args = parser.parse_args()

    entities = load_pouch_entities(args.pouches) + load_pet_entities(args.pets)
    catalog = load_catalog(args.catalog)
    combat = load_combat(args.npc_config)
    minted = load_minted_sources(args.ledger)
    missing = sorted({entity.npc_id for entity in entities if entity.npc_id not in catalog})
    if missing:
        print(f"summoning_roster_assets: {len(missing)} source NPCs missing from catalog: {missing}", file=sys.stderr)
        return 1

    # Render into temporary sibling names so --check can be a strict, useful gate.
    csv_text_path = args.csv.with_suffix(args.csv.suffix + ".new")
    manifest_text = build_manifest(entities, combat, minted)
    source_sound_text = sound_csv_text(args.familiar_source)
    write_csv(csv_text_path, entities, catalog, combat)
    csv_text = csv_text_path.read_text(encoding="utf-8")
    csv_text_path.unlink()
    changed = (
        not args.csv.exists() or args.csv.read_text(encoding="utf-8") != csv_text or
        not args.manifest.exists() or args.manifest.read_text(encoding="utf-8") != manifest_text or
        not args.sound_csv.exists() or args.sound_csv.read_text(encoding="utf-8") != source_sound_text
    )
    if args.check:
        if changed:
            print("summoning_roster_assets: generated roster is stale; rerun without --check", file=sys.stderr)
            return 1
    else:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.sound_csv.parent.mkdir(parents=True, exist_ok=True)
        args.csv.write_text(csv_text, encoding="utf-8")
        args.manifest.write_text(manifest_text, encoding="utf-8")
        args.sound_csv.write_text(source_sound_text, encoding="utf-8")

    familiar_count = sum(entity.kind == "familiar" for entity in entities)
    pet_count = len(entities) - familiar_count
    combat_count = sum(
        bool(animation_id(combat.get(entity.npc_id, {}), field))
        for entity in entities
        for field in ("melee_animation", "defence_animation", "death_animation")
    )
    print(
        f"summoning_roster_assets: {familiar_count} familiars, {pet_count} pet stages, "
        f"{len({entity.npc_id for entity in entities})} unique NPCs, {combat_count} combat-role links; "
        f"{source_sound_text.count(chr(10)) - 1} source sound references"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
