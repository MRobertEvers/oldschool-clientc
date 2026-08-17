#!/usr/bin/env python3
"""Generate and validate the revision-239 classic God Wars combat ledger.

The four GWD map spawn files are the scope authority.  Runtime scripts remain
the behavior authority; this tool refuses to emit a row for an NPC which lacks
an exact player-attack or death trigger, and checks the NPC-war trigger for all
aligned ambient combatants.  The checked-in CSV is intentionally reviewable:
one row represents one distinct attack against players or NPCs.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content/osrs239-content"
SCRIPTS = CONTENT / "server/scripts"
GWD = SCRIPTS / "areas/area_godwars"
OUT = CONTENT / "wiki/godwars_combat_manifest.csv"
HEADER = ROOT / "src/net/mock/mock230_gwd_manifest.gen.h"
SPAWNS = [
    SCRIPTS / "areas/world/configs/m44_82.spawn",
    SCRIPTS / "areas/world/configs/m44_83.spawn",
    SCRIPTS / "areas/world/configs/m45_82.spawn",
    SCRIPTS / "areas/world/configs/m45_83.spawn",
]

FIELDS = [
    "npc_id", "gameval", "display_name", "wiki_version", "wiki_url",
    "wiki_revision", "spawn_count", "faction", "role", "attack_name",
    "style", "max_hit", "attack_speed", "range", "attack_seq",
    "defend_seq", "death_seq", "move_seq", "projectile",
    "start_spotanim", "impact_spotanim", "sound", "launch_tick",
    "impact_tick", "hitsplat_tick", "aoe_shape", "forced_move",
    "secondary_effect", "prayer_rule", "player_or_npc_target",
    "attack_handler", "drop_handler",
]


BOSS = {
    "godwars_bandos_avatar": ("Bandos", "boss", "General_Graardor", "15298421"),
    "godwars_sergeant_goblin1": ("Bandos", "bodyguard", "Sergeant_Strongstack", "15298419"),
    "godwars_sergeant_goblin2": ("Bandos", "bodyguard", "Sergeant_Steelwill", "15298417"),
    "godwars_sergeant_goblin3": ("Bandos", "bodyguard", "Sergeant_Grimspike", "15298415"),
    "godwars_armadyl_avatar": ("Armadyl", "boss", "Kree%27arra", "15298481"),
    "godwars_armadyl_bodyguard_kilisa": ("Armadyl", "bodyguard", "Flight_Kilisa", "15298482"),
    "godwars_armadyl_bodyguard_skree": ("Armadyl", "bodyguard", "Wingman_Skree", "15298484"),
    "godwars_armadyl_bodyguard_geerin": ("Armadyl", "bodyguard", "Flockleader_Geerin", "15298486"),
    "godwars_saradomin_avatar": ("Saradomin", "boss", "Commander_Zilyana", "15298455"),
    "godwars_saradomin_unicorn": ("Saradomin", "bodyguard", "Starlight", "15298454"),
    "godwars_saradomin_lion": ("Saradomin", "bodyguard", "Growler", "15298447"),
    "godwars_saradomin_centaur": ("Saradomin", "bodyguard", "Bree", "15298452"),
    "godwars_zamorak_avatar": ("Zamorak", "boss", "K%27ril_Tsutsaroth", "15298476"),
    "godwars_ancient_greater_demon": ("Zamorak", "bodyguard", "Tstanon_Karlak", "15298479"),
    "godwars_ancient_black_demon": ("Zamorak", "bodyguard", "Balfrug_Kreeyath", "15298477"),
    "godwars_ancient_lesser_demon": ("Zamorak", "bodyguard", "Zakl%27n_Gritch", "15298478"),
}

FAMILY = {
    "goblin": ("Bandos", "Goblin_(God_Wars_Dungeon)", "15290836"),
    "hobgoblin": ("Bandos", "Hobgoblin", "15277154"),
    "ogre": ("Bandos", "Ogre", ""),
    "jogre": ("Bandos", "Jogre", "15259543"),
    "cyclops": ("Bandos", "Cyclops_(God_Wars_Dungeon)", "15200369"),
    "ork": ("Bandos", "Ork", "15221265"),
    "saradomin_knight": ("Saradomin", "Knight_of_Saradomin", ""),
    "saradomin_priest": ("Saradomin", "Saradomin_priest", ""),
    "imp": ("Zamorak", "Imp", "15271036"),
    "werewolf": ("Zamorak", "Werewolf", "15199240"),
    "vampyre": ("Zamorak", "Feral_Vampyre", "15199173"),
    "hellhound": ("Zamorak", "Hellhound", ""),
    "bloodveld": ("Zamorak", "Bloodveld", "15290415"),
    "gorak": ("Zamorak", "Gorak", ""),
    "aviansie": ("Armadyl", "Aviansie", "15199476"),
    "spiritual_warrior": ("mixed", "Spiritual_warrior", "15275507"),
    "spiritual_ranger": ("mixed", "Spiritual_ranger", "15275525"),
    "spiritual_mage": ("mixed", "Spiritual_mage", "15293350"),
    "icefiend": ("Unaligned", "Icefiend", "15275445"),
    "pyrefiend": ("Unaligned", "Pyrefiend", "15283685"),
}

AVIAN_MAX = {
    "godwars_armadyl_male_armor01_blue": 8,
    "godwars_armadyl_male_armor01_green": 9,
    "godwars_armadyl_male_armor01_red": 10,
    "godwars_armadyl_male_armor02_blue": 9,
    "godwars_armadyl_male_armor02_green": 10,
    "godwars_armadyl_male_armor02_red": 11,
    "godwars_armadyl_male_armor03_green": 15,
    "godwars_armadyl_male_armor03_red": 16,
    "godwars_armadyl_female_armor01_blue": 10,
    "godwars_armadyl_female_armor01_green": 10,
    "godwars_armadyl_female_armor01_red": 11,
    "godwars_armadyl_female_armor02_blue": 12,
    "godwars_armadyl_female_armor02_green": 11,
    "godwars_armadyl_female_armor02_red": 11,
    "godwars_armadyl_female_armor03_blue": 15,
}

SPECIAL_ATTACKS = {
    "godwars_bandos_avatar": [
        ("melee", "crush", "60", "godwars_bandos_attack", "none", "none", "none", "godwars_bandos_avatar_punch", "single", "none", "none", "Protect from Melee blocks"),
        ("ranged_volley", "ranged", "15-35", "godwars_bandos_ranged", "none", "godwars_bandos_proj", "none", "godwars_bandos_avatar_attack", "room rectangle", "none", "independent roll per player", "Protect from Missiles blocks"),
    ],
    "godwars_armadyl_avatar": [
        ("ranged_wind", "ranged", "69", "godwars_armadyl_avatar_wind_attack", "none", "godwars_armadyl_bolt_attack_projanim", "none", "godwars_armadyl_ranged", "room rectangle", "one tile away", "accurate hit knocks back", "Protect from Missiles blocks"),
        ("magic_wind", "magic", "21", "godwars_armadyl_avatar_wind_attack", "godwars_armadyl_avatar_magic_attack_spotanim", "none", "none", "godwars_armadyl_magic_cast", "room rectangle", "none", "none", "Protect from Missiles blocks; rolls Magic accuracy against Ranged defence"),
        ("magical_melee", "magic", "25", "godwars_armadyl_avatar_claw_attack", "none", "none", "none", "godwars_armadyl_avatar_attack", "single", "none", "only while target is not attacking Kree", "Protect from Magic blocks"),
    ],
    "godwars_saradomin_avatar": [
        ("melee", "crush", "27", "godwars_saradomin_attack", "none", "none", "none", "godwars_saradomin_swordglow", "single", "none", "none", "Protect from Melee blocks"),
        ("magic", "magic", "10-20", "godwars_saradomin_magic_attack", "godwars_saradomin_magic_attack_spotanim", "none", "none", "godwars_saradomin_magic_castandfire", "single", "none", "minimum 10 on accurate hit", "Protect from Magic blocks"),
    ],
    "godwars_zamorak_avatar": [
        ("melee", "slash", "46", "godwars_zamorak_attack", "none", "none", "none", "godwars_zamorak_avatar_attack", "single", "none", "poison severity 16", "Protect from Melee blocks"),
        ("magic", "magic", "10-30", "godwars_zamorak_magic_attack", "godwars_zamorak_magic_attack_spotanim", "godwars_zamorak_magic_attack_proj", "none", "godwars_zamorak_fire", "single", "none", "none", "Protect from Magic blocks"),
        ("prayer_slam", "slash", "35-49", "godwars_zamorak_attack", "none", "none", "none", "godwars_zamorak_avatar_attack", "single", "none", "drains half current Prayer; poison", "penetrates Protect from Melee"),
    ],
}

BODYGUARD = {
    "godwars_sergeant_goblin1": ("crush", "15", "slice_surface_goblin_sergent_attack", "none", "none", "none"),
    "godwars_sergeant_goblin2": ("magic", "15", "godwars_sergeant_goblin2_sonic", "godwars_goblin2_sonic_attk_spot", "godwars_goblin2_sonic_attk_proj", "godwars_goblin2_sonic_impact"),
    "godwars_sergeant_goblin3": ("ranged", "21", "godwars_sergeant_goblin3_ranged", "godwars_sergeant_goblin3_ranged", "godwars_goblin3_handaxe_proj", "none"),
    "godwars_armadyl_bodyguard_skree": ("magic", "16", "godwars_armadyl_cannon_attack", "godwars_armadyl_bolt_attack_spotanim", "godwars_armadyl_bolt_attack_projanim", "godwars_armadyl_bolt_hit_spotanim"),
    "godwars_armadyl_bodyguard_geerin": ("ranged", "25", "godwars_armadyl_spear_attack", "none", "godwars_armadyl_spear_travel_spotanim", "none"),
    "godwars_armadyl_bodyguard_kilisa": ("slash", "15", "godwars_armadyl_sword_attack", "none", "none", "none"),
    "godwars_saradomin_unicorn": ("crush", "15", "unicorn_rework_attack", "none", "none", "none"),
    "godwars_saradomin_lion": ("magic", "16", "godwars_lion_magic_attack", "godwars_lion_magic_spot", "godwars_lion_magic_proj", "godwars_lion_magic_impact"),
    "godwars_saradomin_centaur": ("ranged", "16", "godwars_centaur_attack_ranged", "godwars_centaur_arrow_launch", "godwars_centaur_arrow_proj", "none"),
    "godwars_ancient_greater_demon": ("crush", "15", "demon_attack", "none", "none", "none"),
    "godwars_ancient_lesser_demon": ("ranged", "21", "godwars_zamorak_bdygrd_ranged", "godwars_zamarok_bdygrd_ranged_spot", "godwars_zamorak_bdygrd_ranged_proj", "none"),
    "godwars_ancient_black_demon": ("magic", "16", "demon_attack", "godwars_black_demon_fireball_spot", "godwars_black_demon_fireball_proj", "none"),
}


def family_of(gameval: str) -> str:
    if gameval in AVIAN_MAX:
        return "aviansie"
    tests = [
        ("spiritual_", lambda s: "spiritual_mage" if s.endswith("_mage") else "spiritual_ranger" if s.endswith("_ranger") else "spiritual_warrior"),
        ("goblin", lambda _: "goblin"), ("hobgoblin", lambda _: "hobgoblin"),
        ("ancient_ogre", lambda _: "ogre"), ("ancient_jogre", lambda _: "jogre"),
        ("cyclops", lambda _: "cyclops"), ("ancient_ork", lambda _: "ork"),
        ("saradomin_knight", lambda _: "saradomin_knight"),
        ("saradomin_wizard", lambda _: "saradomin_priest"),
        ("ancient_imp", lambda _: "imp"), ("werewolf", lambda _: "werewolf"),
        ("ancient_vampire", lambda _: "vampyre"), ("ancient_hellhound", lambda _: "hellhound"),
        ("bloodveld", lambda _: "bloodveld"), ("gorak", lambda _: "gorak"),
        ("icefiend", lambda _: "icefiend"), ("pyrefiend", lambda _: "pyrefiend"),
    ]
    for needle, answer in tests:
        if needle in gameval:
            return answer(gameval)
    raise ValueError(f"no family mapping for {gameval}")


def read_blocks(path: Path) -> dict[str, dict[str, str]]:
    blocks: dict[str, dict[str, str]] = defaultdict(dict)
    current = ""
    for raw in path.read_text(errors="replace").splitlines():
        m = re.fullmatch(r"\[([^]]+)]", raw.strip())
        if m:
            current = m.group(1)
            continue
        if not current or "=" not in raw or raw.lstrip().startswith("//"):
            continue
        key, value = raw.strip().split("=", 1)
        if key == "param" and "," in value:
            key, value = value.split(",", 1)
        blocks[current][key] = value
    return blocks


def roster() -> tuple[list[str], Counter[str]]:
    counts: Counter[str] = Counter()
    for path in SPAWNS:
        for raw in path.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("//") or line.startswith("["):
                continue
            parts = line.split()
            if len(parts) >= 4 and parts[0].startswith("godwars_"):
                counts[parts[0]] += 1
    return sorted(counts), counts


def exact_bindings(trigger: str, text: str) -> Counter[str]:
    return Counter(re.findall(rf"\[{re.escape(trigger)},(godwars_[A-Za-z0-9_]+)]", text))


def locate_binding(trigger: str, gameval: str, files: list[Path]) -> str:
    needle = f"[{trigger},{gameval}]"
    matches = [p for p in files if needle in p.read_text(errors="replace")]
    if len(matches) != 1:
        raise ValueError(f"{gameval}: expected one {trigger} binding, found {len(matches)}")
    return str(matches[0].relative_to(ROOT))


def attack_rows(gameval: str, params: dict[str, str], aligned_ambient: bool) -> list[dict[str, str]]:
    if gameval in SPECIAL_ATTACKS:
        specs = SPECIAL_ATTACKS[gameval]
    elif gameval in BODYGUARD:
        style, max_hit, seq, start, proj, impact = BODYGUARD[gameval]
        specs = [("primary", style, max_hit, seq, start, proj, impact,
                  params.get("attack_sound", "none"), "single", "none", "none",
                  f"Protect from {style.title()} blocks")]
    else:
        family = family_of(gameval)
        style_num = params.get("damagetype", "2")
        style = {"0": "stab", "1": "slash", "2": "crush", "3": "ranged", "4": "magic"}.get(style_num, style_num)
        max_hit = "formula"
        effect = "none"
        prayer = f"Protect from {('Melee' if style in {'stab','slash','crush'} else style.title())} blocks"
        if gameval in AVIAN_MAX:
            style, max_hit = "ranged", str(AVIAN_MAX[gameval])
            prayer = "Protect from Missiles blocks; ordinary player melee cannot target"
        elif gameval == "godwars_gorak":
            style, max_hit = "typeless crush", "14"
            effect, prayer = "accurate hit drains random non-HP stat 1-4", "100% protection-prayer penetration"
        elif "spiritual_" in gameval:
            if gameval.endswith("_ranger") or gameval == "godwars_spiritual_armadyl_warrior":
                style = "ranged"
            elif gameval.endswith("_mage"):
                style = "magic"
            known = {
                "godwars_spiritual_armadyl_warrior": 15, "godwars_spiritual_armadyl_ranger": 16,
                "godwars_spiritual_armadyl_mage": 17, "godwars_spiritual_bandos_ranger": 13,
                "godwars_spiritual_bandos_mage": 17, "godwars_spiritual_saradomin_ranger": 16,
                "godwars_spiritual_saradomin_mage": 20, "godwars_spiritual_zamorak_ranger": 15,
                "godwars_spiritual_zamorak_mage": 19,
            }
            max_hit = str(known.get(gameval, "formula"))
            prayer = f"Protect from {('Missiles' if style == 'ranged' else style.title())} blocks"
        elif gameval == "godwars_ancient_saradomin_wizard":
            style, max_hit, prayer = "magic", "20", "Protect from Magic blocks"
        specs = [("primary", style, max_hit, params.get("attack_anim", "missing"),
                  params.get("proj_launch", "none"), params.get("proj_travel", "none"),
                  params.get("proj_impact", "none"), params.get("attack_sound", "none"),
                  "single", "none", effect, prayer)]

    rows = []
    for name, style, max_hit, seq, start, proj, impact, sound, aoe, move, effect, prayer in specs:
        rows.append({
            "attack_name": name, "style": style, "max_hit": str(max_hit),
            "attack_seq": seq, "start_spotanim": start, "projectile": proj,
            "impact_spotanim": impact, "sound": sound,
            "aoe_shape": aoe, "forced_move": move, "secondary_effect": effect,
            "prayer_rule": prayer, "player_or_npc_target": "player",
        })
    if aligned_ambient:
        base = rows[0].copy()
        base.update({
            "attack_name": "dungeon_war", "player_or_npc_target": "npc",
            "max_hit": "1" if gameval.endswith("_mage") and "spiritual_" in gameval else base["max_hit"],
            "prayer_rule": "not applicable", "secondary_effect": "marks NPC final blow; no player loot/KC",
        })
        rows.append(base)
    return rows


def build_rows() -> list[dict[str, str]]:
    gamevals, spawn_counts = roster()
    if len(gamevals) != 69:
        raise ValueError(f"classic GWD roster drift: expected 69 gamevals, got {len(gamevals)}")

    roster_meta = {}
    with (CONTENT / "wiki/npc_roster.csv").open(newline="") as fh:
        for row in csv.DictReader(fh):
            roster_meta[row["gameval"]] = row

    params: dict[str, dict[str, str]] = defaultdict(dict)
    for path in [
        CONTENT / "configs/all.npc",
        SCRIPTS / "npc/configs/combat_stats.generated.npc",
        SCRIPTS / "npc/configs/npc_anims.generated.npc",
        GWD / "configs/godwars.npc",
    ]:
        for name, values in read_blocks(path).items():
            params[name].update(values)

    rs2_files = list(SCRIPTS.rglob("*.rs2"))
    rs2_text = "\n".join(p.read_text(errors="replace") for p in rs2_files)
    attack_counts = exact_bindings("ai_opplayer2", rs2_text)
    death_counts = exact_bindings("ai_queue3", rs2_text)
    npc_attack_counts = exact_bindings("ai_opnpc2", rs2_text)

    rows: list[dict[str, str]] = []
    for gameval in gamevals:
        if attack_counts[gameval] != 1:
            raise ValueError(f"{gameval}: expected one exact ai_opplayer2 binding, got {attack_counts[gameval]}")
        if death_counts[gameval] != 1:
            raise ValueError(f"{gameval}: expected one exact ai_queue3 binding, got {death_counts[gameval]}")
        meta = roster_meta.get(gameval)
        if not meta:
            raise ValueError(f"{gameval}: missing npc_roster.csv row")
        p = params[gameval]
        required = ["attackrate", "attack_anim", "defend_anim", "death_anim"]
        missing = [key for key in required if not p.get(key)]
        if missing:
            raise ValueError(f"{gameval}: missing visual/cadence params: {', '.join(missing)}")

        if gameval in BOSS:
            faction, role, title, revision = BOSS[gameval]
        else:
            family = family_of(gameval)
            default_faction, title, revision = FAMILY[family]
            if default_faction == "mixed":
                faction = next(x for x in ("Armadyl", "Bandos", "Saradomin", "Zamorak") if f"_{x.lower()}_" in gameval)
            else:
                faction = default_faction
            role = "neutral" if faction == "Unaligned" else "ambient"
        aligned_ambient = role == "ambient"
        if aligned_ambient and npc_attack_counts[gameval] != 1:
            raise ValueError(f"{gameval}: expected one exact ai_opnpc2 binding, got {npc_attack_counts[gameval]}")

        attack_handler = locate_binding("ai_opplayer2", gameval, rs2_files)
        drop_handler = locate_binding("ai_queue3", gameval, rs2_files)
        for attack in attack_rows(gameval, p, aligned_ambient):
            target_handler = attack_handler
            if attack["player_or_npc_target"] == "npc":
                target_handler = locate_binding("ai_opnpc2", gameval, rs2_files)
            row = {field: "" for field in FIELDS}
            row.update({
                "npc_id": meta["id"], "gameval": gameval,
                "display_name": meta["display_name"],
                "wiki_version": f"{title.replace('_', ' ')}; cache NPC id {meta['id']}",
                "wiki_url": f"https://oldschool.runescape.wiki/w/{title}",
                "wiki_revision": revision or "checked-in manifest",
                "spawn_count": str(spawn_counts[gameval]), "faction": faction,
                # Typed server params may spell this as `int,5`; the ledger
                # records the observable cadence, not the config type tag.
                "role": role, "attack_speed": p["attackrate"].rsplit(",", 1)[-1],
                "range": "10" if attack["style"] in {"ranged", "magic"} else "1",
                "defend_seq": p["defend_anim"], "death_seq": p["death_anim"],
                "move_seq": p.get("walkanim", "cache ready/walk"),
                "start_spotanim": p.get("proj_launch", "none"),
                "sound": p.get("attack_sound", "none"), "launch_tick": "0",
                "impact_tick": "projectile duration / 30" if attack["projectile"] != "none" else "0",
                "hitsplat_tick": "impact tick", "attack_handler": target_handler,
                "drop_handler": drop_handler,
            })
            row.update(attack)
            rows.append(row)
    return rows


def validate(rows: list[dict[str, str]]) -> None:
    errors = []
    keys = Counter((r["gameval"], r["attack_name"], r["player_or_npc_target"]) for r in rows)
    errors += [f"duplicate attack row: {key}" for key, count in keys.items() if count != 1]
    gamevals, _ = roster()
    covered = {r["gameval"] for r in rows}
    if covered != set(gamevals):
        errors.append(f"roster mismatch missing={sorted(set(gamevals)-covered)} extra={sorted(covered-set(gamevals))}")
    required = ["style", "max_hit", "attack_speed", "range", "attack_seq", "defend_seq", "death_seq", "move_seq", "prayer_rule", "wiki_url", "drop_handler"]
    for i, row in enumerate(rows, 2):
        missing = [field for field in required if not row[field] or row[field] == "missing"]
        if missing:
            errors.append(f"row {i} {row['gameval']}/{row['attack_name']}: blank {missing}")
    if errors:
        raise ValueError("\n".join(errors))


def render_header(rows: list[dict[str, str]]) -> str:
    """Emit the asset/timing half of the reviewed ledger for VM selftests."""
    lines = [
        "/* Generated by tools/generate_godwars_combat_manifest.py. */",
        "#ifndef MOCK230_GWD_MANIFEST_GEN_H",
        "#define MOCK230_GWD_MANIFEST_GEN_H",
        "",
        "struct Mock230GwdManifestAttack {",
        "    const char* gameval;",
        "    const char* attack_name;",
        "    const char* attack_seq;",
        "    const char* projectile;",
        "    const char* start_spotanim;",
        "    const char* impact_spotanim;",
        "    const char* sound;",
        "    int attack_speed;",
        "    int targets_npc;",
        "};",
        "",
        "static const struct Mock230GwdManifestAttack k_mock230_gwd_manifest[] = {",
    ]
    for row in rows:
        values = [
            row["gameval"], row["attack_name"], row["attack_seq"],
            row["projectile"], row["start_spotanim"],
            row["impact_spotanim"], row["sound"],
        ]
        quoted = ", ".join(json.dumps(value) for value in values)
        lines.append(
            f"    {{ {quoted}, {int(row['attack_speed'])}, "
            f"{int(row['player_or_npc_target'] == 'npc')} }},"
        )
    lines += [
        "};",
        "",
        "#define MOCK230_GWD_MANIFEST_COUNT \\",
        "    ((int)(sizeof(k_mock230_gwd_manifest) / sizeof(k_mock230_gwd_manifest[0])))",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="compare generated rows with the checked-in CSV")
    args = parser.parse_args()
    try:
        rows = build_rows()
        validate(rows)
        header = render_header(rows)
        if args.check:
            with OUT.open(newline="") as fh:
                existing = list(csv.DictReader(fh))
            if existing != rows:
                raise ValueError(f"{OUT.relative_to(ROOT)} is stale; regenerate it")
            if not HEADER.exists() or HEADER.read_text() != header:
                raise ValueError(f"{HEADER.relative_to(ROOT)} is stale; regenerate it")
        else:
            with OUT.open("w", newline="") as fh:
                writer = csv.DictWriter(fh, fieldnames=FIELDS, lineterminator="\n")
                writer.writeheader()
                writer.writerows(rows)
            HEADER.write_text(header)
        print(f"God Wars manifest OK: 69 NPCs, {len(rows)} distinct attack rows")
        return 0
    except (OSError, ValueError) as exc:
        print(f"godwars manifest: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
