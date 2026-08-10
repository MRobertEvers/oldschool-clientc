#!/usr/bin/env python3
"""Build the Phase-5a RS530 Summoning familiar/pouch candidate manifest.

The cache alone answers *which sequences can pose an NPC*: its NPC definition
points at a BAS (base-animation set), and the BAS supplies ready/walk sequences.
The combat roles are deliberately not guessed from that compatible set.  They
come from 2009scape's ``npc_configs.json``, which is what its server actually
uses for melee, defence, and death animations.

Run ``tools/entity_viewer/ev_catalog --rev rs530 ...`` once first.  This script
then joins that catalog with the 82-entry pouch table, the source combat config,
and the 82-entry pouch inventory.  It writes two generated artifacts:

* a CSV that tells reviewers exactly why each animation is selected; and
* a cachepack import manifest for the assets/configs (not gameplay behaviour).

The Phase-5a manifest is deliberately a candidate, not cache admission.  It
contains active familiar/pouch pairs only, disables NPC sound closures, and
allows the one documented safe byte-copy synth (188).  The full direct-familiar
sound table remains auditable separately because cross-revision sound IDs must
not be inferred from their numbers.
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
# These were minted by the pre-existing Spirit-wolf proof.  The broad roster
# manifest deliberately leaves them out so it can live in a separate config
# prefix without emitting duplicate definitions for the same destination ID.
PREEXISTING_EXPORTS = {("npc", 6829), ("obj", 12047)}
SAFE_SYNTH_IDS = {188}


@dataclass(frozen=True)
class Entity:
    kind: str
    entry: str
    stage: str
    npc_id: int
    obj_id: int | None


def load_pouch_entities(path: Path) -> list[Entity]:
    records = json.loads(path.read_text(encoding="utf-8"))
    entities: list[Entity] = []
    for record in records:
        # The four Sacred Clay pouches have slot -1 and are not familiar
        # pouches; importing them would pull unrelated minigames in. Phoenix
        # is a normal slot-50 familiar and intentionally remains in scope.
        if record["slot"] < 0:
            continue
        name = record["name"].lower()
        if name.endswith("_pouch"):
            name = name[: -len("_pouch")]
        entities.append(Entity("familiar", name, "familiar", record["npc"], record["pouch"]))
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
    return entity.entry


def add_unique(target: OrderedDict[int, str], source_id: int, name: str) -> None:
    if source_id >= 0:
        target.setdefault(source_id, name)


def direct_familiar_sound_ids(familiar_source: Path) -> list[int]:
    pattern = re.compile(r"Sounds\.[A-Z][A-Z0-9_]*_(\d+)")
    return sorted({int(match.group(1)) for path in familiar_source.rglob("*.java")
                   for match in pattern.finditer(path.read_text(encoding="utf-8"))})


def safe_direct_familiar_sound_ids(familiar_source: Path) -> list[int]:
    """Return only the explicitly reviewed source synths for this boundary."""
    found = direct_familiar_sound_ids(familiar_source)
    missing = SAFE_SYNTH_IDS - set(found)
    if missing:
        raise ValueError(f"safe synths are absent from familiar source: {sorted(missing)}")
    return [sound_id for sound_id in found if sound_id in SAFE_SYNTH_IDS]


def build_manifest(
    entities: list[Entity], combat: dict[int, dict[str, str]], direct_sounds: list[int]
) -> str:
    npcs: OrderedDict[int, str] = OrderedDict()
    objs: OrderedDict[int, str] = OrderedDict()
    seqs: OrderedDict[int, str] = OrderedDict()
    for entity in entities:
        name = entity_name(entity)
        if ("npc", entity.npc_id) not in PREEXISTING_EXPORTS:
            add_unique(npcs, entity.npc_id, name)
        if entity.obj_id is not None and ("obj", entity.obj_id) not in PREEXISTING_EXPORTS:
            add_unique(objs, entity.obj_id, f"{name}_pouch")
        config = combat.get(entity.npc_id, {})
        for role, field in (
            ("attack", "melee_animation"),
            ("magic", "magic_animation"),
            ("range", "range_animation"),
            ("defend", "defence_animation"),
            ("death", "death_animation"),
        ):
            value = animation_id(config, field)
            if value:
                add_unique(seqs, int(value), f"{name}_{role}")

    lines = [
        "# Generated by tools/summoning_roster_assets.py. Do not hand-edit.",
        "# Phase 5a candidate only: it is not an admitted feature-on cohort.",
        "# Active familiar/pouch pairs only; pets, scrolls, tertiary ingredients and potions are deferred.",
        "# NPC sound closure is deliberately disabled; source synth 188 is the only safe byte-copy candidate.",
        "[import:scape2009]",
        "from_rev=rs530",
        "from_cache=../../../2009scape/Server/data/cache",
        "to_rev=osrs239",
        "to_tree=../../OSRS-Content/osrs239-content",
        "lane=ported/scape2009_summoning",
        "ledger=port/summoning_530.map",
        "# A dedicated prefix prevents this candidate from truncating the proof's summoning.* configs.",
        "prefix=summoning_roster_530",
        "npc_sounds=no",
        "",
        "[export:npc]",
    ]
    lines.extend(f"{source_id}={name}" for source_id, name in npcs.items())
    lines.extend(["", "[export:obj]"])
    lines.extend(f"{source_id}={name}" for source_id, name in objs.items())
    lines.extend(["", "# Combat-role sequences come from npc_configs.json; BAS movement sequences are pulled by NPC closure.", "[export:seq]"])
    lines.extend(f"{source_id}={name}" for source_id, name in seqs.items())
    lines.extend(["", "# Explicit source familiar calls (see familiar_sound_refs_530.csv).", "[export:synth]"])
    lines.extend(f"{sound_id}=familiar_sound_{sound_id}" for sound_id in direct_sounds)
    lines.append("")
    return "\n".join(lines)


def validate_candidate_manifest(manifest_text: str) -> None:
    """Assert the non-negotiable Phase-5a candidate boundary.

    This protects the generated INI itself as well as the source data that fed
    it.  In particular, changing ``npc_sounds`` to ``yes`` would make
    cachepack close over several source audio records that Phase 5a has not
    reviewed.  The candidate stays deliberately separate from admission into
    the feature-on staging tree.
    """
    section = ""
    settings: dict[str, str] = {}
    exports: dict[str, list[tuple[int, str]]] = {}
    for line_number, raw in enumerate(manifest_text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if "=" not in line:
            raise ValueError(f"candidate manifest line {line_number} is not key=value: {raw!r}")
        key, value = (part.strip() for part in line.split("=", 1))
        if section == "import:scape2009":
            if key in settings:
                raise ValueError(f"candidate manifest repeats setting {key!r}")
            settings[key] = value
        elif section.startswith("export:"):
            try:
                source_id = int(key)
            except ValueError as exc:
                raise ValueError(
                    f"candidate manifest line {line_number} has a non-integer source id {key!r}"
                ) from exc
            exports.setdefault(section.removeprefix("export:"), []).append((source_id, value))
        else:
            raise ValueError(f"candidate manifest line {line_number} is outside a supported section")

    if settings.get("npc_sounds") != "no":
        raise ValueError("Phase 5a candidate must set npc_sounds=no")
    if settings.get("prefix") != "summoning_roster_530":
        raise ValueError("Phase 5a candidate must use the reserved summoning_roster_530 prefix")
    if set(exports) != {"npc", "obj", "seq", "synth"}:
        raise ValueError("Phase 5a candidate must contain npc/obj/seq/synth export sections only")
    for kind, rows in exports.items():
        source_ids = [source_id for source_id, _ in rows]
        if len(source_ids) != len(set(source_ids)):
            raise ValueError(f"candidate manifest duplicates {kind} source ids")
        if any(not name or name == "-" for _, name in rows):
            raise ValueError(f"candidate manifest has an unnamed {kind} export")
        if any(re.search(r"(?:^|_)pet(?:_|$)", name) for _, name in rows):
            raise ValueError("Phase 5a candidate must not export pet content")
    synth_sources = {source_id for source_id, _ in exports["synth"]}
    if synth_sources != SAFE_SYNTH_IDS:
        raise ValueError(
            f"Phase 5a candidate synth exports must be {sorted(SAFE_SYNTH_IDS)}, got {sorted(synth_sources)}"
        )


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
                "asset_status": "catalogued",
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
    parser.add_argument("--familiar-source", type=Path, default=FAMILIAR_SOURCE)
    parser.add_argument("--npc-config", type=Path, default=SOURCE / "Server/data/configs/npc_configs.json")
    parser.add_argument("--catalog", type=Path, default=REPO / "out/rs530_summoning_anims/npc_catalog.csv")
    parser.add_argument("--csv", type=Path, default=REPO / "docs/summoning_port/roster_assets_530.csv")
    parser.add_argument("--manifest", type=Path, default=REPO / "docs/summoning_port/roster_assets_530.ini")
    parser.add_argument("--sound-csv", type=Path, default=REPO / "docs/summoning_port/familiar_sound_refs_530.csv")
    parser.add_argument("--check", action="store_true", help="fail when generated outputs differ")
    args = parser.parse_args()

    entities = load_pouch_entities(args.pouches)
    catalog = load_catalog(args.catalog)
    combat = load_combat(args.npc_config)
    missing = sorted({entity.npc_id for entity in entities if entity.npc_id not in catalog})
    if missing:
        print(f"summoning_roster_assets: {len(missing)} source NPCs missing from catalog: {missing}", file=sys.stderr)
        return 1

    # Render into temporary sibling names so --check can be a strict, useful gate.
    csv_text_path = args.csv.with_suffix(args.csv.suffix + ".new")
    manifest_text = build_manifest(entities, combat, safe_direct_familiar_sound_ids(args.familiar_source))
    validate_candidate_manifest(manifest_text)
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
    combat_count = sum(
        bool(animation_id(combat.get(entity.npc_id, {}), field))
        for entity in entities
        for field in ("melee_animation", "defence_animation", "death_animation")
    )
    print(
        f"summoning_roster_assets: {familiar_count} familiars, 0 pet stages (Phase 7 deferred), "
        f"{len({entity.npc_id for entity in entities})} unique NPCs, {combat_count} combat-role links; "
        f"{source_sound_text.count(chr(10)) - 1} source sound references"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
