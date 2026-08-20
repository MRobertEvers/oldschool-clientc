#!/usr/bin/env python3
"""Hold solo-boss combat configs to their pinned wiki infoboxes.

Why this is a script and not a selftest: an authored npc `param=` row lands in
the server's own def, which only `npc_param` sees, and that needs a live npc —
which the C-driven selftest has no scene to provide. Every param reads back 0
there whether the config is right or wrong. See
`minigame_dagannothkings/scripts/dks.rs2`'s verification note.

Checks, per boss:

  * `attackrate` equals the infobox's "attack speed";
  * `damagetype` is a style the infobox's "attack style" lists;
  * the cache's own hitpoints (stat4) equal the infobox's "hitpoints", which is
    a check on the CACHE rather than on the config — it catches a record picked
    by the wrong name, which is the single most likely mistake in a batch like
    this and the one hardest to see afterwards.

`damagetype` values are combat_damagetypes.constant's: stab 0, slash 1, crush 2,
ranged 3, magic 4.

  python3 tools/check_boss_contract.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content")
WIKI = os.path.join(ROOT, "docs", "bosses", "wiki")
CONFIGS = [
    os.path.join(CONTENT, "server", "scripts", "bosses", "boss_wilderness_singles",
                 "configs", "bosses.npc"),
]

STYLE_IDS = {"stab": 0, "slash": 1, "crush": 2, "ranged": 3, "magic": 4}
# A wiki style word that is not itself a damagetype, mapped to the one it
# implies. "Melee" with no sub-style stated is crush by this tree's default.
STYLE_ALIASES = {
    "melee": "crush",
    "magical ranged": "magic",
    "ranged magic": "magic",
    "dragonfire": None,   # its own mechanic, never a damagetype
}

# cache record -> wiki page
BOSSES = {
    "slayer_kraken_boss": "Kraken",
    "smoke_devil_boss": "Thermonuclear_smoke_devil",
    "scorpia": "Scorpia",
    "sarachnis": "Sarachnis",
    "hillgiant_boss": "Obor",
    "gb_mossgiant": "Bryophyta",
    "king_dragon": "King_Black_Dragon",
    "chaos_fanatic": "Chaos_Fanatic",
    "crazy_archaeologist": "Crazy_archaeologist",
    "fossil_crazy_archaeologist": "Deranged_archaeologist",
}


def read_configs():
    out = {}
    for path in CONFIGS:
        text = open(path, encoding="utf-8").read()
        for block in re.split(r"\n(?=\[)", text):
            head = re.match(r"\[([^\]]+)\]", block)
            if not head:
                continue
            out[head.group(1)] = dict(
                re.findall(r"^param=([A-Za-z_]+),(\S+)$", block, re.M)
            )
    return out


def wiki_field(page, key):
    text = open(os.path.join(WIKI, page + ".wiki"), encoding="utf-8").read()
    match = re.search(r"^\|\s*%s\s*=\s*(.+?)\s*$" % re.escape(key), text, re.M)
    return match.group(1) if match else None


def wiki_styles(page):
    """The damagetype ids the infobox's attack style permits."""
    raw = wiki_field(page, "attack style") or ""
    raw = re.sub(r"\{\{[^}]*\}\}", "", raw)
    raw = re.sub(r"\[\[([^\]|]*\|)?([^\]]*)\]\]", r"\2", raw)
    raw = re.sub(r"\(.*?\)", "", raw)
    out = []
    for word in re.split(r"[,/]| and ", raw):
        word = word.strip().lower()
        if not word:
            continue
        word = STYLE_ALIASES.get(word, word)
        if word is None:
            continue
        if word in STYLE_IDS:
            out.append(STYLE_IDS[word])
    return out


def cache_hitpoints(record):
    """stat4 on the cache record — this tree's hitpoints slot."""
    text = open(os.path.join(CONTENT, "configs", "all.npc"), encoding="utf-8",
                errors="replace").read()
    match = re.search(r"\n\[%s\]\n(.*?)(?=\n\[|\Z)" % re.escape(record), text, re.S)
    if not match:
        return None
    hp = re.search(r"^stat4=(\d+)", match.group(1), re.M)
    return int(hp.group(1)) if hp else None


def main():
    configs = read_configs()
    problems = []
    for record, page in sorted(BOSSES.items()):
        params = configs.get(record)
        if params is None:
            problems.append("%s: no block in any boss config" % record)
            continue

        speed = wiki_field(page, "attack speed")
        rate = params.get("attackrate")
        if speed and rate != speed.strip():
            problems.append("%s: wiki attack speed %s, config attackrate %s"
                            % (record, speed, rate))

        allowed = wiki_styles(page)
        got = params.get("damagetype", "").lstrip("^").replace("_style", "")
        got_id = STYLE_IDS.get(got)
        if got_id is None:
            problems.append("%s: damagetype %r is not a style" % (record, got))
        elif allowed and got_id not in allowed:
            problems.append(
                "%s: wiki attack style allows %s, config says %s (%d)"
                % (record, allowed, got, got_id)
            )

        want_hp = wiki_field(page, "hitpoints")
        have_hp = cache_hitpoints(record)
        if want_hp and have_hp is not None:
            want_hp = want_hp.replace(",", "").strip()
            if want_hp.isdigit() and int(want_hp) != have_hp:
                problems.append(
                    "%s: wiki hitpoints %s but cache record has %d — wrong record?"
                    % (record, want_hp, have_hp)
                )

    if problems:
        print("check_boss_contract: %d problem(s)" % len(problems), file=sys.stderr)
        for line in problems:
            print("  " + line, file=sys.stderr)
        return 1
    print("check_boss_contract: %d boss(es) agree with the pinned wiki" % len(BOSSES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
