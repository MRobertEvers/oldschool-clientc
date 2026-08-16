#!/usr/bin/env python3
"""Propose attack/defend/death sounds for the familiars 2009scape leaves silent.

Only six of the 78 admitted familiars carry a `combat_audio` array in
2009scape (see `docs/summoning_port/familiar_combat_audio_530.csv`); the other
72 have no combat sound anywhere in the source — not in the npc config, not in
a familiar class, and not in-band on any of their 171 combat sequences. This
tool does not invent sounds for them. It proposes *candidates with evidence*,
grades each one, and writes a ledger for review. Nothing here writes content.

The whole method rests on one fact about the target cache: **osrs239 names its
sound effects**, and the names are creature-shaped.
`OSRS-Content/osrs239-content/pack/4_soundeffects.pack` carries a name for all
12,010 records, and 279 stems have a complete `<stem>_attack` / `<stem>_hit` /
`<stem>_death` triple. `_hit` is the sound the creature makes when it is hit —
the defend slot.

That naming is not an assumption; it is checked against the six familiars whose
sounds ARE known, plus two unrelated control npcs:

    Spirit spider   537/539/538 -> insect_attack / insect_hit / insect_death
    Albino rat      703/705/704 -> anger_rat_{attack,hit,death}
    Arctic bear     498/500/499 -> jungle_horror_{attack2,hit,death}
    Spirit scorpion 3611/3612/3610 -> scorpion_{attack,hit,death}
    Angry unicorn   876/878/877 -> anger_unicorn_{attack,hit,death}
    Unicorn         369/371/370 -> cow_{attack,hit,death}

so a 2009scape `combat_audio` array is [attack, hit(defend), death], and a
creature's three ids are usually consecutive in (attack, death, hit) order.
That is the shape every candidate below has to satisfy.

## The layers

Each familiar is put through four independent identifications, strongest first.
A candidate triple is only emitted when a layer produces one; agreement between
layers is what turns a guess into a finding.

  L1 rig kin      Two npcs that share a `bas_type_id` share their whole base
                  animation set — stand, walk, run — and that is the closest
                  thing the rev-530 record has to "is the same creature". It is
                  the rig gate this tree already trusts over name matching
                  (docs/DEATH_ATK_DEF_ANIMS.md, and the 35% of LostCity anim
                  rows that fail it). Note that the familiars' own `anim_stand`
                  / `anim_walk` columns are -1 in a 530 dump — RS2 keeps those
                  in the bas type, not on the npc — so joining on frame
                  archives finds nothing and the bas type is the join that
                  works.

  L2 model kin    Same, one step weaker: npcs that share a model id with the
                  familiar. A familiar is very often a recolour of a live
                  creature, and a shared model is direct evidence of which one.

  L3 name kin     The familiar's creature word ("Spirit spider" -> spider)
                  matched against 2009scape npc names that carry combat_audio.
                  Weakest on its own; useful as corroboration and as the only
                  layer that can reach a creature the port shares no asset with.

  L4 sound-stem   The creature word matched directly against the 279 named
                  triples in the target cache. This is the only layer that can
                  answer for a familiar with no live counterpart at all, and it
                  is also the falsifier for L1-L3: a candidate whose three ids
                  do not form one complete stem triple is reported as
                  incoherent rather than accepted.

## Grades

  confirmed   two or more independent layers agree, and stem-coherent
  probable    one strong layer (L1/L2), stem-coherent
  weak        name or stem only
  incoherent  a candidate exists but its three ids are not one creature's
              triple — reported, never silently dropped
  none        nothing proposed

## How accurate is it? Measure, do not assume — `--validate`

Six familiars have a known answer, so run the inference against them with their
own record (and their combat pair) hidden and see what it says. As of
2026-08-12 that scores **3 correct, 3 wrong, 0 abstained** — a coin flip — and
the three wrong answers are the interesting part:

  Arctic bear       rig says grizzly/black bear 300/302/301; 2009scape says
                    498/500/499, which is `jungle_horror_*`. Graded `confirmed`.
  Spirit saratrice  rig says cockatrice 363/365/364; 2009scape says
                    703/705/704, which is `anger_rat_*` — the Albino rat's
                    exact triple, which looks like a copied row.
  Unicorn stallion  name kin reaches "black unicorn", which uses `cow_*`
                    369/371/370. Right family, wrong animal.

The first two identified the creature correctly and the *source* is the odd one;
the third is the inference being naive. No amount of extra layering separates
those cases, because the disagreement is about what rev 530 actually shipped and
neither side of it is observable from here.

**So the grade does not predict correctness — a `confirmed` row is wrong in this
sample.** Nothing in the output belongs in a `.npc` file on the strength of this
tool. What it is genuinely good for is narrowing 12,010 sound effects to three
named candidates that a person can listen to in a minute, which is the only step
that can actually settle one of these.

Usage:
    python3 tools/infer_familiar_combat_audio.py --scratch DIR [--out CSV]
    python3 tools/infer_familiar_combat_audio.py --scratch DIR --validate

`--scratch` needs `npc530.csv` (tools/dump_stats --rev 530) and an unpacked
rev-530 seq tree (`cachepack unpack --rev 530 --types seq`); the tool says how
to make them if they are absent.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CONTENT = REPO / "OSRS-Content" / "osrs239-content"
SOUND_PACK = CONTENT / "pack" / "4_soundeffects.pack"
ROSTER = REPO / "docs" / "summoning_port" / "roster_assets_530.csv"
SCAPE = Path.home() / "Documents" / "git_repos" / "2009scape"
NPC_CONFIGS = SCAPE / "Server" / "data" / "configs" / "npc_configs.json"

# The creature word in a familiar's name. Left side is matched against the
# familiar's `entry`; right side is the word every other layer searches on.
# Only stated where stripping "spirit_"/"giant_" and the like does not already
# produce it, or where the familiar is a made-up creature whose live
# counterpart has a different name.
CREATURE_WORD = {
    "spirit_tz_kih": "tz-kih",
    "karamthulhu": "karamthulhu",
    "giant_chinchompa": "chinchompa",
    "spirit_mosquito": "mosquito",
    "barker_toad": "toad",
    "bloated_leech": "leech",
    "compost_mound": "mound",
    "evil_turnip": "turnip",
    "forge_regent": "regent",
    "granite_crab": "crab",
    "granite_lobster": "lobster",
    "honey_badger": "badger",
    "obsidian_golem": "golem",
    "pack_yak": "yak",
    "praying_mantis": "mantis",
    "ravenous_locust": "locust",
    "stranger_plant": "plant",
    "thorny_snail": "snail",
    "unicorn_stallion": "unicorn",
    "war_tortoise": "tortoise",
    "abyssal_lukrer": "abyssal",
    "abyssal_lurker": "abyssal",
    "abyssal_parasite": "abyssal",
    "abyssal_titan": "abyssal",
    "desert_wyrm": "wyrm",
    "spirit_terrorbird": "terrorbird",
    "vampire_bat": "bat",
    "fruit_bat": "bat",
    "bull_ant": "ant",
    "albino_rat": "rat",
    "arctic_bear": "bear",
    "giant_ent": "ent",
}

# Familiars whose species is a Summoning invention with no live counterpart.
# Recorded so "no candidate" reads as a decision rather than a hole.
INVENTED = {
    "abyssal_titan", "fire_titan", "moss_titan", "ice_titan", "lava_titan",
    "swamp_titan", "geyser_titan", "iron_titan", "steel_titan",
    "bronze_minotaur", "iron_minotaur", "steel_minotaur", "mithril_minotaur",
    "adamant_minotaur", "rune_minotaur", "void_ravager", "void_spinner",
    "void_torcher", "void_shifter", "wolpertinger", "pyrelord", "forge_regent",
    "compost_mound", "talon_beast", "obsidian_golem", "karamthulhu",
}


def die(message: str) -> "NoReturn":  # type: ignore[name-defined]
    print(f"infer_familiar_combat_audio: {message}", file=sys.stderr)
    raise SystemExit(2)


def load_sound_names() -> dict[int, str]:
    if not SOUND_PACK.exists():
        die(f"{SOUND_PACK} is missing — unpack the osrs239 cache first")
    names: dict[int, str] = {}
    for line in SOUND_PACK.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        left, right = line.split("=", 1)
        names[int(left)] = right.strip()
    return names


def load_triples(names: dict[int, str]) -> dict[str, dict[str, int]]:
    """Group named sounds into `<stem>_{attack,hit,death}` triples.

    A trailing digit is dropped (`jungle_horror_attack2`), because a creature
    with two attack clips still has one attack slot; the lowest id wins so the
    choice is deterministic rather than dependent on pack order.
    """
    stems: dict[str, dict[str, int]] = defaultdict(dict)
    for sid, name in sorted(names.items()):
        match = re.match(r"^(.*?)_(attack|hit|death)\d?$", name)
        if not match:
            continue
        stem, slot = match.group(1), match.group(2)
        if slot not in stems[stem]:
            stems[stem][slot] = sid
    return {k: v for k, v in stems.items() if len(v) == 3}


def load_source_npcs() -> list[dict]:
    if not NPC_CONFIGS.exists():
        die(f"{NPC_CONFIGS} is missing — this needs a 2009scape checkout")
    return json.loads(NPC_CONFIGS.read_text(encoding="utf-8"))


def load_npc_dump(scratch: Path) -> dict[int, dict]:
    path = scratch / "npc530.csv"
    if not path.exists():
        die(
            f"{path} is missing. Make it with:\n"
            f"    make -C tools/dump_stats\n"
            f"    tools/dump_stats/dump_stats --rev 530 <2009scape>/Server/data/cache "
            f"--npc-csv {path}"
        )
    with path.open(encoding="utf-8") as handle:
        return {int(row["id"]): row for row in csv.DictReader(handle)}


def load_seq_archives(scratch: Path) -> dict[int, set[int]]:
    """seq id -> the frame archives it draws from.

    A dat2 sequence stores each frame as `(frame archive << 16) | frame`, so the
    high half names the archive — and two npcs animating out of the same archive
    are on the same rig. This avoids having to decode framemaps at all.
    """
    path = scratch / "src530" / "configs" / "all.seq"
    if not path.exists():
        die(
            f"{path} is missing. Make it with:\n"
            f"    3rd/rscache/tools/cachepack/cachepack unpack "
            f"--cache <2009scape>/Server/data/cache --rev 530 "
            f"--src {scratch / 'src530'} --types seq"
        )
    out: dict[int, set[int]] = {}
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        header = re.match(r"^\[seq_(\d+)\]", line)
        if header:
            current = int(header.group(1))
            out[current] = set()
            continue
        frame = re.match(r"^frame=(\d+),", line)
        if frame and current is not None:
            out[current].add(int(frame.group(1)) >> 16)
    return out


ANIM_COLUMNS = ("anim_stand", "anim_walk", "anim_run", "anim_crawl")


def npc_archives(row: dict, seq_archives: dict[int, set[int]]) -> set[int]:
    archives: set[int] = set()
    for column in ANIM_COLUMNS:
        raw = (row.get(column) or "").strip()
        if not raw or raw == "-1":
            continue
        archives |= seq_archives.get(int(raw), set())
    return archives


def npc_models(row: dict) -> set[int]:
    raw = (row.get("models") or "").strip()
    return {int(v) for v in raw.split() if v.lstrip("-").isdigit() and int(v) >= 0}


def audio_of(row: dict) -> tuple[int, int, int] | None:
    """The [attack, hit, death] triple, or None.

    A triple containing 0 is not a sound: `NPC.getAudio` explicitly refuses
    `audio.id != 0` because effect 0 is a real clip and cannot double as "no
    sound" — the same distinction that costs this tree a bug whenever it is
    forgotten (docs/WEAPON_FX.md 6.6). Several records carry all-zero arrays.
    """
    raw = row.get("combat_audio")
    if not raw:
        return None
    parts = [int(v) for v in str(raw).split(",")]
    if len(parts) != 3 or 0 in parts:
        return None
    return (parts[0], parts[1], parts[2])


def stem_of(triple: tuple[int, int, int], names: dict[int, str]) -> str | None:
    """The single stem all three ids share, or None if they do not agree.

    This is the falsifier. A proposal whose ids come from three different
    creatures is not a triple, however many kin voted for it.
    """
    stems = set()
    for sid in triple:
        match = re.match(r"^(.*?)_(attack|hit|death)\d?$", names.get(sid, ""))
        if not match:
            return None
        stems.add(match.group(1))
    return stems.pop() if len(stems) == 1 else None


class Evidence:
    """Every layer's opinion about one familiar, and the grade that follows."""

    def __init__(self, npc_id: int, word: str, invented: bool):
        self.npc_id = npc_id
        self.word = word
        self.invented = invented
        self.votes: dict[tuple[int, int, int], set[str]] = defaultdict(set)
        self.layers: set[str] = set()

    def add(self, layer: str, triple, why: str) -> None:
        if triple is None:
            return
        self.votes[triple].add(f"{layer} {why}")
        self.layers.add(layer)

    def verdict(self, names: dict[int, str]):
        if not self.votes:
            return None, set(), "none"
        best, why = max(self.votes.items(), key=lambda kv: (len(kv[1]), -sum(kv[0])))
        stem = stem_of(best, names)
        layers = {w.split()[0] for w in why}
        strong = bool(layers & {"L1", "L2"})
        if not stem:
            return best, why, "incoherent"
        if len(layers) >= 2:
            return best, why, "confirmed"
        return best, why, "probable" if strong else "weak"


def infer(entry: dict, ctx: dict, hide: set[int]) -> Evidence:
    """Run every layer for one familiar.

    `hide` is what makes `--validate` honest: pass the familiar's own record and
    its combat pair and the layers cannot read the answer off the thing they are
    being asked to predict. A pair is (id, id+1) — 2009scape gives the fighting
    variant of a familiar the next id, with the same audio.
    """
    npc_id = int(entry["source_npc"])
    name = entry["entry"]
    word = CREATURE_WORD.get(name, name.replace("spirit_", "").replace("_", " "))
    ev = Evidence(npc_id, word, name in INVENTED)
    row = ctx["dump"].get(npc_id)
    by_id, names, triples = ctx["by_id"], ctx["names"], ctx["triples"]

    if row:
        bas = (row.get("bas_type_id") or "").strip()
        if bas and bas != "-1":
            for kin in ctx["kin_by_bas"].get(int(bas), []):
                if kin in hide:
                    continue
                ev.add("L1", audio_of(by_id[kin]), f"rig:{by_id[kin].get('name')}")
        for model in npc_models(row):
            for kin in ctx["kin_by_model"].get(model, []):
                if kin in hide:
                    continue
                ev.add("L2", audio_of(by_id[kin]), f"model:{by_id[kin].get('name')}")

    # A made-up species has no live counterpart, so a name that happens to
    # contain a common word ("talon BEAST" -> dark beast) is noise, not kin.
    if not ev.invented:
        for kin_id, kin_name, audio in ctx["named_with_audio"]:
            if kin_id in hide:
                continue
            if re.search(rf"\b{re.escape(word)}\b", kin_name):
                ev.add("L3", audio, f"name:{kin_name}")
        for stem, slots in sorted(triples.items()):
            if re.search(rf"(^|_){re.escape(word.replace(' ', '_'))}(_|$)", stem):
                ev.add("L4", (slots["attack"], slots["hit"], slots["death"]), f"stem:{stem}")
                break
    return ev


def build_context(scratch: Path) -> dict:
    names = load_sound_names()
    source = load_source_npcs()
    dump = load_npc_dump(scratch)
    by_id = {int(r["id"]): r for r in source}
    kin_by_bas: dict[int, list[int]] = defaultdict(list)
    kin_by_model: dict[int, list[int]] = defaultdict(list)
    for npc_id, row in dump.items():
        if not audio_of(by_id.get(npc_id, {})):
            continue
        bas = (row.get("bas_type_id") or "").strip()
        if bas and bas != "-1":
            kin_by_bas[int(bas)].append(npc_id)
        for model in npc_models(row):
            kin_by_model[model].append(npc_id)
    return dict(
        names=names, triples=load_triples(names), dump=dump, by_id=by_id,
        kin_by_bas=kin_by_bas, kin_by_model=kin_by_model,
        named_with_audio=[(int(r["id"]), (r.get("name") or "").lower(), audio_of(r))
                          for r in source if audio_of(r)])


def validate(ctx: dict, roster: list[dict]) -> int:
    """Score the inference against the six familiars whose answer is known."""
    print("blind validation — the familiar's own record and its pair are hidden\n")
    print("%-20s %-18s %-18s %s" % ("familiar", "2009scape says", "inference says", "verdict"))
    correct = wrong = abstain = 0
    for entry in sorted(roster, key=lambda r: r["entry"]):
        npc_id = int(entry["source_npc"])
        truth = audio_of(ctx["by_id"].get(npc_id, {}))
        if not truth:
            continue
        ev = infer(entry, ctx, hide={npc_id, npc_id + 1, npc_id - 1})
        best, why, grade = ev.verdict(ctx["names"])
        if best is None:
            verdict, abstain = "abstained", abstain + 1
        elif best == truth:
            verdict, correct = f"MATCH ({grade})", correct + 1
        else:
            verdict, wrong = f"WRONG ({grade})", wrong + 1
        print("%-20s %-18s %-18s %s" % (
            entry["entry"], str(truth), str(best) if best else "-", verdict))
        if best and best != truth:
            print("      %s -> %s ; source triple is %s" % (
                sorted(why)[0],
                stem_of(best, ctx["names"]),
                stem_of(truth, ctx["names"])))
    print(f"\n{correct} correct, {wrong} wrong, {abstain} abstained "
          f"out of {correct + wrong + abstain} with a known answer")
    if wrong:
        print("A wrong answer at this rate means no row in the candidate ledger "
              "is portable on its own.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path,
                        default=REPO / "docs/summoning_port/familiar_combat_audio_candidates.csv")
    parser.add_argument("--scratch", type=Path, required=True,
                        help="directory holding npc530.csv and src530/")
    parser.add_argument("--validate", action="store_true",
                        help="score the inference against the six known familiars and exit")
    args = parser.parse_args()

    ctx = build_context(args.scratch)
    names = ctx["names"]
    roster = list(csv.DictReader(ROSTER.open(encoding="utf-8")))
    if args.validate:
        return validate(ctx, roster)

    out_rows = []
    for entry in sorted(roster, key=lambda r: r["entry"]):
        name = entry["entry"]
        npc_id = int(entry["source_npc"])
        own = audio_of(ctx["by_id"].get(npc_id, {}))
        if own:
            out_rows.append(dict(
                entry=name, source_npc=npc_id, source_name=entry["source_name"],
                grade="source", attack=own[0], defend=own[1], death=own[2],
                stem=stem_of(own, names) or "", layer="L0",
                evidence="2009scape combat_audio on the familiar's own record"))
            continue
        ev = infer(entry, ctx, hide={npc_id, npc_id + 1})
        best, why, grade = ev.verdict(names)
        if best is None:
            out_rows.append(dict(
                entry=name, source_npc=npc_id, source_name=entry["source_name"],
                grade="none", attack="", defend="", death="", stem="",
                layer="invented species" if ev.invented else "",
                evidence="no rig or model kin with combat audio, no name kin, no stem"))
            continue
        out_rows.append(dict(
            entry=name, source_npc=npc_id, source_name=entry["source_name"],
            grade=grade, attack=best[0], defend=best[1], death=best[2],
            stem=stem_of(best, names) or "MIXED",
            layer="+".join(sorted(ev.layers)),
            evidence="; ".join(sorted(why))[:400]))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=[
            "entry", "source_npc", "source_name", "grade",
            "attack", "defend", "death", "stem", "layer", "evidence"])
        writer.writeheader()
        writer.writerows(out_rows)

    tally: dict[str, int] = defaultdict(int)
    for row in out_rows:
        tally[row["grade"]] += 1
    print(f"infer_familiar_combat_audio: {len(out_rows)} familiars -> {args.out}")
    for grade in ("source", "confirmed", "probable", "weak", "incoherent", "none"):
        if tally[grade]:
            print(f"  {grade:<10} {tally[grade]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
