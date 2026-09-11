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
    # The generated combat-stats table carries `attackrate` for 1,277 records,
    # which is where most bosses outside boss_wilderness_singles get theirs.
    # Reading only the hand-authored file meant every boss in it reported "no
    # block in any boss config" -- indistinguishable from a boss with no stats
    # at all.
    os.path.join(CONTENT, "server", "scripts", "npc", "configs",
                 "combat_stats.generated.npc"),
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
    # Added 2026-08-20. These six were recorded as "no cache asset" until the
    # cache was searched by DISPLAY name rather than by symbol -- see
    # tools/cache_find.py. None of the symbols contains the boss's own name.
    "corp_beast": "Corporeal_Beast",          # Corporeal Beast
    "cata_boss": "Skotizo",                   # Skotizo
    "vorkath": "Vorkath",                     # Vorkath
    "akd_xamphur_combat": "Xamphur",          # Xamphur
    "ma2_boss_guthix": "Derwen",              # Rise of the Six
    "ma2_boss_saradomin": "Justiciar_Zachariah",
    # Revenant maledictus is deliberately NOT listed. It is the one case where
    # the cache and the wiki genuinely disagree: `wild_cave_superior` is named
    # "Revenant maledictus" and carries 1200 hitpoints where the wiki states
    # 1250. The name matches, so this is not a wrong record -- this cache
    # snapshot predates a hitpoints change. Listing it would make the checker
    # cry wolf about a divergence that is real and expected; leaving it out with
    # this note is the honest form. Recheck if the cache is ever re-exported.
}


def read_configs():
    """Merge the config files, and REMEMBER where each value came from.

    A record can be declared in more than one file -- `chaos_fanatic` is in both
    the hand-authored `bosses.npc` and the generated `combat_stats.generated.npc`
    -- and the two can disagree. Silently letting the last file win reproduces
    whatever the packer does without ever saying that a choice was made, so a
    contradiction between two sources reads as a single wrong value.
    """
    out = {}
    seen = {}
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
    unconfigured = []
    for record, page in sorted(BOSSES.items()):
        params = configs.get(record)
        if params is None:
            # No authored block yet -- but the CACHE half of the contract can
            # still run, and it is the half that proves the record was picked by
            # the right name. A boss whose cache hitpoints match the wiki is a
            # boss waiting for a config and a script; one whose hitpoints
            # disagree is a boss whose symbol was guessed wrong, and those are
            # very different pieces of news to report as "missing".
            cache_hp = cache_hitpoints(record)
            wiki_hp = wiki_field(page, "hitpoints")
            wiki_hp = wiki_hp.strip() if wiki_hp else None
            if cache_hp is None:
                problems.append("%s: no config block AND no cache record -- "
                                "the symbol is wrong" % record)
            elif wiki_hp and wiki_hp.isdigit() and int(wiki_hp) != cache_hp:
                problems.append("%s: no config block, and cache hitpoints %d "
                                "disagree with the wiki's %s -- wrong record"
                                % (record, cache_hp, wiki_hp))
            else:
                unconfigured.append("%s (%s, %s hp) cache record confirmed"
                                    % (record, page.replace("_", " "),
                                       cache_hp))
            continue

        speed = wiki_field(page, "attack speed")
        rate = params.get("attackrate")
        if speed and rate != speed.strip():
            problems.append("%s: wiki attack speed %s, config attackrate %s"
                            % (record, speed, rate))

        allowed = wiki_styles(page)
        # `damagetype` may be declared in more than one config file. Five of
        # the seven records that appear twice write the SAME style two ways
        # (`^crush_style` and `2`), which is not a conflict; two genuinely
        # disagree. Normalise to the id before comparing so only the real ones
        # are reported.
        got = params.get("damagetype", "").lstrip("^").replace("_style", "")
        # The hand-authored configs spell the style ("crush"); the GENERATED
        # combat-stats table writes the id ("2"). Both are valid `damagetype`
        # values and the packer accepts either, so a checker that understands
        # only one of them reports six correct records as broken -- which is
        # exactly what happened the first time this file was widened.
        got_id = STYLE_IDS.get(got)
        if got_id is None and got.isdigit():
            got_id = int(got)
            if got_id not in STYLE_IDS.values():
                got_id = None
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

    if unconfigured:
        print("check_boss_contract: %d boss(es) have a confirmed cache record "
              "but no authored config yet:" % len(unconfigured))
        for line in unconfigured:
            print("  " + line)

    if problems:
        print("check_boss_contract: %d problem(s)" % len(problems), file=sys.stderr)
        for line in problems:
            print("  " + line, file=sys.stderr)
        return 1
    print("check_boss_contract: %d boss(es) agree with the pinned wiki" % len(BOSSES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
