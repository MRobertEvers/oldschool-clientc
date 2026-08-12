#!/usr/bin/env python3
"""Build the familiar Interact-dialogue corpus from RuneScape Wiki transcripts.

2009scape has a familiar conversation for exactly one of the 78 admitted
familiars (spirit graahk); its own author flags the placeholder line the other
77 fall back on as "likely inauthentic". The wiki's `Category:Familiar dialogue`
transcripts are the citable source for the rest, and the repo's porting policy
allows one "when the local 2009scape method is absent" — which it is.

This is a ONE-OFF fetch. It writes `docs/summoning_port/familiar_dialogue.json`,
which is checked in and is what the generator and the tests read; nothing in the
build reaches the network. Re-run with --fetch only to refresh the corpus, and
review the diff before committing it.

The parse is deliberately narrow. It takes the unconditional `Conversation N`
sections and nothing else:

  * conditional conversations ("If the player has any type of bone in their
    inventory") are recorded with their condition text but held out of the
    random pool — the state they key on is not modelled here;
  * `Overhead dialogue` is the familiar's ambient chat, not the Interact result,
    and is recorded separately for whoever implements that;
  * anything that does not parse into speaker/text lines is reported rather than
    guessed at.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SCRIPT_LANE = REPO / "OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning"
CORPUS = REPO / "docs/summoning_port/familiar_dialogue.json"
API = "https://runescape.wiki/api.php"
UA = "3draster-summoning-port/1.0 (one-off familiar dialogue transcript pull)"

# The in-game name is abbreviated or ambiguous; each of these is the wiki's own
# page for the same 2008/2009 familiar record.
PAGE_OVERRIDE = {
    "Vampire bat": "Vampyre bat",
    "Evil turnip": "Evil turnip (familiar)",
    "Phoenix": "Phoenix (familiar)",
    "Sp. cockatrice": "Spirit cockatrice",
    "Sp. guthatrice": "Spirit guthatrice",
    "Sp. saratrice": "Spirit saratrice",
    "Sp. zamatrice": "Spirit zamatrice",
    "Sp. pengatrice": "Spirit pengatrice",
    "Sp. coraxatrice": "Spirit coraxatrice",
    "Sp. vulatrice": "Spirit vulatrice",
}


# --------------------------------------------------------------------------
# The registry is the authority on which 78 familiars exist and what they are
# called; the corpus is keyed by its type ids so the two cannot drift.
# --------------------------------------------------------------------------


def registry() -> tuple[dict[int, str], dict[int, int]]:
    source = (SCRIPT_LANE / "scripts/summoning_registry.rs2").read_text(encoding="utf-8")
    constants = (SCRIPT_LANE / "configs/summoning.constant").read_text(encoding="utf-8")
    consts = {m.group(1): m.group(2) for m in re.finditer(r"^\^(\S+) = (\S+)$", constants, re.M)}

    def value(token: str) -> str:
        token = token.strip()
        return consts.get(token[1:], token) if token.startswith("^") else token

    def table(proc: str, cast):
        body = source.split(f"[proc,{proc}]", 1)[1].split("\n[proc,", 1)[0]
        out = {}
        for m in re.finditer(r'if \(\$type = ([^)]+)\) return\("?([^")]+)"?\);', body):
            out[int(value(m.group(1)))] = cast(m.group(2))
        tail = re.findall(r'^return\("?([^")]+)"?\);', body, re.M)
        default = cast(tail[-1]) if tail else None
        for type_id in range(1, 79):
            out.setdefault(type_id, default)
        return out

    names = table("summoning_familiar_name", lambda s: s)
    levels = table("summoning_familiar_level", lambda s: int(value(s)))
    return names, levels


# --------------------------------------------------------------------------
# Fetch
# --------------------------------------------------------------------------


def api(params: dict) -> dict:
    url = API + "?" + urllib.parse.urlencode({**params, "format": "json"})
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def fetch(names: dict[int, str]) -> tuple[dict[int, str], dict[str, str]]:
    members = api(
        {
            "action": "query",
            "list": "categorymembers",
            "cmtitle": "Category:Familiar dialogue",
            "cmlimit": 500,
        }
    )["query"]["categorymembers"]
    titles = {m["title"] for m in members}
    lower = {t.lower(): t for t in titles}

    pages: dict[int, str] = {}
    missing: list[str] = []
    for type_id, name in names.items():
        wanted = "Transcript:" + PAGE_OVERRIDE.get(name, name)
        found = lower.get(wanted.lower())
        if found:
            pages[type_id] = found
        else:
            missing.append(f"{type_id} {name} -> {wanted}")
    if missing:
        raise SystemExit("no transcript page for: " + "; ".join(missing))

    raw: dict[str, str] = {}
    unique = sorted(set(pages.values()))
    for i in range(0, len(unique), 40):
        batch = unique[i : i + 40]
        data = api(
            {
                "action": "query",
                "prop": "revisions",
                "rvprop": "content",
                "rvslots": "main",
                "titles": "|".join(batch),
            }
        )
        for page in data["query"]["pages"].values():
            if "revisions" in page:
                raw[page["title"]] = page["revisions"][0]["slots"]["main"]["*"]
        time.sleep(1)
    absent = [t for t in unique if t not in raw]
    if absent:
        raise SystemExit("no revision content for: " + ", ".join(absent))
    return pages, raw


# --------------------------------------------------------------------------
# Wikitext -> lines
# --------------------------------------------------------------------------

HEADING = re.compile(r"^(={2,6})\s*(.+?)\s*\1\s*$", re.M)
BULLET = re.compile(r"^\*+\s*'''(?P<who>[^']+?):?'''\s*(?P<text>.*)$")
# A section that IS the dialogue rather than a container for numbered ones.
PLAIN_DIALOGUE = re.compile(r"^(Dialogues?|When spoken to)$", re.I)
# Conditions that are about conversation ORDER, not about world state. These
# stay in the random pool; the state ones cannot, because nothing here models
# "has a mirror shield equipped".
ORDERING_CONDITION = re.compile(r"^(always first|after conversation \d+|randomly after )", re.I)
# The one state condition this lane can actually answer: the beast-of-burden
# inventory is a real container here (`summoning_bob`, 12 slots). Spirit
# terrorbird's three conversations partition it (<=10 / 11 / 12), so with these
# understood it always has something to say; without them it has nothing.
BOB_ITEMS = re.compile(r"^With .+ having (\d+)( or less)? items in inventory\.?$", re.I)

# Every other condition the wiki records needs a concrete obj/category token
# from THIS cache, and most of the wiki's items either don't exist in this
# content pack under the expected name or need a mechanic (area, familiar-
# carried state) this lane has nothing to check against. Each is verified
# against `configs/all.obj` and `pack/category.pack` before being listed here —
# see docs/summoning_port/FAMILIAR_INTERACT.md for the full per-condition audit.
# Anything not in this table stays in `conditional`, unimplemented, with the
# reason recorded in that doc rather than guessed at here.
#
# guard shapes:
#   {"kind": "cat", "container": "inv"|"worn", "category": <category.pack name>,
#    "op": ">=", "count": N}
#   {"kind": "sum", "terms": [(container, obj), ...], "op": ">=", "count": N}
#   {"kind": "hp_missing"}          -- stat(hitpoints) < stat_base(hitpoints)
#   {"kind": "run_energy_zero"}     -- runenergy = 0
# The 38 real (non-cert, non-placeholder) obj records this cache carries with
# `category=6` — enumerated rather than passed as `inv_totalcat(inv, bones)`,
# because `bones` is BOTH a category name (category.pack: `6=bones`) AND a real
# obj's own name, and inv_totalcat's symbol resolution took the obj (confirmed
# live: `inv_totalcat(inv, bones)` measured 0 with 5 [bones] in the inventory,
# `inv_total(inv, bones)` measured 5 at the same time). category.pack's own
# header warns about exactly this collision shape for `arrows_dragon` vs
# `dragon_arrows`; `firemaking_logs` and `weapon_pickaxe` below do not collide
# with any obj name and were confirmed to resolve correctly the same way.
BONES = (
    "bones", "bones_burnt", "bat_bones", "big_bones", "babydragon_bones",
    "dragon_bones", "wolf_bones", "tbwt_beast_bones", "tbwt_jogre_bones",
    "tbwt_burnt_jogre_bones", "mm_small_ninja_monkey_bones",
    "mm_medium_ninja_monkey_bones", "mm_normal_gorilla_monkey_bones",
    "mm_bearded_gorilla_monkey_bones", "mm_normal_monkey_bones",
    "mm_small_zombie_monkey_bones", "mm_large_zombie_monkey_bones",
    "mm_skeleton_bones", "zogre_bones", "zogre_ancestral_bones_fayg",
    "zogre_ancestral_bones_raurg", "zogre_ancestral_bones_ourg",
    "dagannoth_king_bones", "wyvern_bones", "dorgesh_construction_bone",
    "dorgesh_construction_bone_curved", "lava_dragon_bones",
    "dragon_bones_superior", "wyrm_bones", "drake_bones", "hydra_bones",
    "shade_bleached_bones", "babywyrm_bones", "giant_bones", "alan_bones",
    "strykewyrm_bones", "frost_dragon_bones", "bull_bones",
)

CURATED_GUARDS: dict[tuple[int, str], dict] = {
    (1, "If the player has any type of bone in their inventory"): {
        "kind": "sum", "terms": [("inv", name) for name in BONES], "op": ">=", "count": 1,
    },
    (5, "If the player is wearing a snelm"): {
        "kind": "sum",
        "terms": [
            ("worn", name)
            for name in (
                "snelm_round_swamp", "snelm_round_red+black", "snelm_round_yellow",
                "snelm_round_blue", "snelm_round_orange", "snelm_point_swamp",
                "snelm_point_red+black", "snelm_point_yellow", "snelm_point_blue",
            )
        ],
        "op": ">=", "count": 1,
    },
    (8, "when wielding pickaxe"): {
        "kind": "cat", "container": "worn", "category": "weapon_pickaxe", "op": ">=", "count": 1,
    },
    (10, "With at least one dose of prayer potion in the inventory"): {
        "kind": "sum",
        "terms": [
            ("inv", name)
            for name in (
                "1doseprayerrestore", "2doseprayerrestore",
                "3doseprayerrestore", "4doseprayerrestore",
            )
        ],
        "op": ">=", "count": 1,
    },
    (15, "when missing life points"): {"kind": "hp_missing"},
    (17, "If the player has logs in their inventory"): {
        "kind": "cat", "container": "inv", "category": "firemaking_logs", "op": ">=", "count": 1,
    },
    (22, "When the player has 0% running energy"): {"kind": "run_energy_zero"},
    (37, "If the player has at least two raw sharks in their inventory"): {
        "kind": "sum", "terms": [("inv", "raw_shark")], "op": ">=", "count": 2,
    },
    (38, "If the player has a Ball of wool in their inventory"): {
        "kind": "sum", "terms": [("inv", "ball_of_wool")], "op": ">=", "count": 1,
    },
    (44, "while wearing the ring of charos (a)"): {
        "kind": "sum", "terms": [("worn", "ring_of_charos_unlocked")], "op": ">=", "count": 1,
    },
    (46, "while holding a swamp toad in inventory"): {
        "kind": "sum", "terms": [("inv", "swamp_toad")], "op": ">=", "count": 1,
    },
    (49, "With 5 or more papayas in inventory"): {
        "kind": "sum", "terms": [("inv", "papaya")], "op": ">=", "count": 5,
    },
    (55, "with Butterfly net equipped or in inventory"): {
        "kind": "sum",
        "terms": [("worn", "hunting_butterfly_net"), ("inv", "hunting_butterfly_net")],
        "op": ">=", "count": 1,
    },
    (65, "with any type of swamp tar or paste in inventory"): {
        "kind": "sum",
        "terms": [("inv", "swamp_tar"), ("inv", "rawswamppaste"), ("inv", "swamppaste")],
        "op": ">=", "count": 1,
    },
    (72, "when the player does not have maximum life points"): {"kind": "hp_missing"},
}


def clean(text: str) -> str:
    """Wikitext to the plain sentence the dialogue box shows."""
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    text = re.sub(r"\{\{RSChat\|(.*?)\}\}", r"\1", text, flags=re.S)
    text = re.sub(r"\{\{RSFont\|[^|}]*\|(.*?)\}\}", r"\1", text, flags=re.S)
    text = re.sub(r"\{\{Sic\|(.*?)(?:\|[^}]*)?\}\}", r"\1", text, flags=re.S | re.I)
    text = re.sub(r"\{\{[^{}]*\}\}", "", text)
    # A line break inside one spoken line is a layout break in the wiki's box,
    # not a pause: this era's chat body is one wrapping component, so it joins
    # with a space (interface_chat/scripts/chat.rs2).
    text = re.sub(r"<br\s*/?>", " ", text, flags=re.I)
    text = re.sub(r"\[\[[^\]|]*\|([^\]]*)\]\]", r"\1", text)
    text = re.sub(r"\[\[([^\]]*)\]\]", r"\1", text)
    text = re.sub(r"'''?", "", text)
    text = re.sub(r"</?[a-zA-Z][^>]*>", "", text)
    text = text.replace("&nbsp;", " ").replace("&amp;", "&")
    text = text.replace("&quot;", "'").replace('"', "'")
    text = text.replace("&lt;", "(").replace("&gt;", ")")
    text = text.replace("<", "(").replace(">", ")")
    return re.sub(r"\s+", " ", text).strip()


def parse(page: str, source: str, familiar: str) -> dict:
    """Split one transcript into conversations, conditionals and overhead lines."""
    marks = [(m.start(), m.end(), len(m.group(1)), m.group(2)) for m in HEADING.finditer(source)]
    sections = []
    for index, (_start, end, depth, title) in enumerate(marks):
        stop = marks[index + 1][0] if index + 1 < len(marks) else len(source)
        sections.append((depth, title, source[end:stop]))

    conversations: list[dict] = []
    conditional: list[dict] = []
    overhead: list[str] = []
    below_gate: str | None = None
    unparsed: list[str] = []
    ancestry: dict[int, str] = {}
    # Pages with no numbered conversations carry the whole thing in one section.
    # Collected separately and used only as a fallback, because a page that HAS
    # numbered conversations may also carry a rewritten modern `==Dialogue==`
    # (fire titan) and the numbered set under "Dialogue options prior to
    # 6 December 2011" is the period-correct one for a 2009 port.
    plain_sections: list[dict] = []

    for depth, title, body in sections:
        ancestry[depth] = title
        for deeper in [d for d in ancestry if d > depth]:
            ancestry.pop(deeper)
        heading = clean(title)
        context = " / ".join(ancestry[d] for d in sorted(ancestry))

        lines = []
        for raw_line in body.splitlines():
            match = BULLET.match(raw_line.strip())
            if not match:
                continue
            text = clean(match.group("text"))
            if not text:
                continue
            who = "player" if match.group("who").strip().lower() == "player" else "npc"
            lines.append({"who": who, "text": text})

        if "overhead" in context.lower():
            overhead.extend(line["text"] for line in lines)
            continue
        if re.match(r"^Below level \d+ Summoning$", heading, re.I):
            if lines:
                below_gate = lines[0]["text"]
            continue
        conversation = re.match(r"^Conversation\s+\d+\s*(?:\((?P<cond>.*)\))?\s*$", heading, re.I)
        if not conversation:
            if lines and PLAIN_DIALOGUE.match(heading):
                # `{{Trandom}}` marks the bullets below it as alternatives, one
                # of which is picked — so each is its own one-line conversation.
                # Without it the bullets are one conversation read in order.
                if re.search(r"\{\{Trandom\}\}", body, re.I):
                    plain_entries = [{"lines": [line]} for line in lines]
                else:
                    plain_entries = [{"lines": lines}]
                plain_sections.extend(plain_entries)
            elif lines and "Conversation" not in context:
                unparsed.append(f"{page}: unclaimed section {heading!r} with {len(lines)} line(s)")
            continue
        if not lines:
            unparsed.append(f"{page}: {heading!r} has no dialogue lines")
            continue
        entry = {"lines": lines}
        condition = conversation.group("cond")
        bob = BOB_ITEMS.match(clean(condition)) if condition else None
        if bob:
            entry["condition"] = clean(condition)
            entry["guard"] = {
                "kind": "bob_items",
                "op": "<=" if bob.group(2) else "==",
                "count": int(bob.group(1)),
            }
            conversations.append(entry)
        elif condition and not ORDERING_CONDITION.match(clean(condition)):
            entry["condition"] = clean(condition)
            conditional.append(entry)
        else:
            if condition:
                # Kept, but recorded: the source unlocks these in sequence and
                # this pool picks at random.
                entry["ordering"] = clean(condition)
            conversations.append(entry)

    if not conversations:
        conversations = plain_sections

    return {
        "name": familiar,
        "page": page,
        "url": "https://runescape.wiki/w/" + page.replace(" ", "_"),
        "below_gate": below_gate,
        "conversations": conversations,
        "conditional": conditional,
        "overhead": overhead,
        "unparsed": unparsed,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="re-pull from the wiki")
    parser.add_argument("--cache", type=Path, help="directory holding a previous raw pull")
    args = parser.parse_args()

    names, levels = registry()

    if args.fetch or not args.cache:
        pages, raw = fetch(names)
        if args.cache:
            args.cache.mkdir(parents=True, exist_ok=True)
            (args.cache / "pages.json").write_text(json.dumps({str(k): v for k, v in pages.items()}))
            (args.cache / "raw.json").write_text(json.dumps(raw))
    else:
        pages = {int(k): v for k, v in json.loads((args.cache / "pages.json").read_text()).items()}
        raw = json.loads((args.cache / "raw.json").read_text())

    familiars = {}
    problems: list[str] = []
    guarded_count = 0
    for type_id in sorted(names):
        page = pages[type_id]
        entry = parse(page, raw[page], names[type_id])
        entry["level"] = levels[type_id]
        entry["chat_level"] = levels[type_id] + 10
        problems.extend(entry.pop("unparsed"))

        # Conditions this lane can actually check move out of `conditional`
        # into `conversations`, in front of the random pool — the wiki's own
        # shape: the special line when the condition holds, the ordinary pool
        # otherwise.
        remaining = []
        for conditional_entry in entry["conditional"]:
            guard = CURATED_GUARDS.get((type_id, conditional_entry["condition"]))
            if guard is None:
                remaining.append(conditional_entry)
                continue
            guarded_count += 1
            entry["conversations"].insert(
                0, {**conditional_entry, "guard": guard}
            )
        entry["conditional"] = remaining

        if not entry["conversations"]:
            problems.append(f"{page}: no unconditional conversation")
        familiars[str(type_id)] = entry

    # Every curated guard must have actually matched a parsed condition, so a
    # future wiki edit that reflows the condition text is a loud failure here
    # rather than a guard that silently stops applying.
    consumed = {
        (int(t), c["condition"])
        for t, f in familiars.items()
        for c in f["conversations"]
        if "guard" in c and "condition" in c
    }
    stale = [f"{t} {c!r}" for (t, c) in CURATED_GUARDS if (t, c) not in consumed]
    if stale:
        raise SystemExit(
            "CURATED_GUARDS entries did not match any parsed condition (wiki "
            "text changed?): " + "; ".join(stale)
        )
    print(f"build_familiar_dialogue_corpus: {guarded_count} curated guard(s) applied")

    corpus = {
        "_meta": {
            "source": "RuneScape Wiki, Category:Familiar dialogue",
            "licence": "CC BY-NC-SA 3.0 — attribute the wiki when reusing these transcripts",
            "generator": "tools/build_familiar_dialogue_corpus.py",
            "why": (
                "2009scape authors a familiar conversation for spirit graahk only and "
                "marks its fallback line as likely inauthentic; the porting policy "
                "permits a cited external reference when the local method is absent."
            ),
            "chat_level_rule": (
                "Summoning level + 10, agreed by three independent sources: "
                "SpiritGraahkDialogue.kt gates a level-57 familiar at 67, "
                "FamiliarDialoguePlugin's commented-out pet gate reads "
                "getSummoningLevel() + 10, and Transcript:Spirit wolf splits a "
                "level-1 familiar at 11."
            ),
            "below_gate_rule": (
                "Below the gate the familiar makes its noise without the "
                "translation. Transcript:Spirit wolf is the only page that records "
                "the split and that is what it shows: 'Whurf?' below level 11, "
                "'Whurf? (What are you doing?)' at 11+."
            ),
            "held_out": (
                "Conditional conversations keep their condition text but stay out "
                "of the random pool unless CURATED_GUARDS gives them a real "
                "ServerScript check against this cache's own objs/categories; "
                "overhead lines are ambient chat, not Interact."
            ),
        },
        "familiars": familiars,
    }
    CORPUS.write_text(json.dumps(corpus, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")

    total = sum(len(f["conversations"]) for f in familiars.values())
    held = sum(len(f["conditional"]) for f in familiars.values())
    print(
        f"build_familiar_dialogue_corpus: {len(familiars)} familiars, {total} conversations, "
        f"{held} conditional held out, {len(problems)} problem(s) -> {CORPUS}"
    )
    for problem in problems:
        print("  " + problem, file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
