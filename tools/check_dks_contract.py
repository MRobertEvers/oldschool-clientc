#!/usr/bin/env python3
"""Hold the Dagannoth Kings' config to the pinned wiki infoboxes.

The encounter is four authored npc params and nothing else, and those cannot be
asserted from a script: `nc_param(<type>, ...)` reads the CACHE's param table
while an authored `param=` row lands in the server's own def, which only
`npc_param` sees — and that needs a live npc, which the C-driven selftest has no
scene to provide. Every param read back 0 there whether the config was right or
wrong, so the check lives here instead.

Same shape as tools/check_charter_contract.py: read the content config, read the
pinned source, compare, exit non-zero on a disagreement.

  python3 tools/check_dks_contract.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(
    ROOT, "OSRS-Content", "osrs239-content", "server", "scripts", "minigames",
    "minigame_dagannothkings", "configs", "dks.npc",
)
SOURCES = os.path.join(ROOT, "docs", "bosses", "dagannoth_kings", "sources")

# The cache record -> its wiki page and the attack style that page states.
# `damagetype`'s values are combat_damagetypes.constant's: stab 0, slash 1,
# crush 2, ranged 3, magic 4 — NOT a melee/ranged/magic triple.
STYLE_IDS = {"stab": 0, "slash": 1, "crush": 2, "ranged": 3, "magic": 4}
KINGS = {
    "dagcave_melee_boss": "Dagannoth_Rex",
    "dagcave_magic_boss": "Dagannoth_Prime",
    "dagcave_ranged_boss": "Dagannoth_Supreme",
}


def config_blocks():
    text = open(CONFIG, encoding="utf-8").read()
    out = {}
    for block in re.split(r"\n(?=\[)", text):
        head = re.match(r"\[([^\]]+)\]", block)
        if not head:
            continue
        params = dict(re.findall(r"^param=([A-Za-z_]+),(\S+)$", block, re.M))
        fields = dict(re.findall(r"^([a-z_]+)=(\S+)$", block, re.M))
        out[head.group(1)] = (params, fields)
    return out


def wiki_field(page, key):
    text = open(os.path.join(SOURCES, page + ".wiki"), encoding="utf-8").read()
    match = re.search(r"^\|\s*%s\s*=\s*(.+?)\s*$" % re.escape(key), text, re.M)
    return match.group(1) if match else None


def main():
    blocks = config_blocks()
    problems = []
    for record, page in KINGS.items():
        if record not in blocks:
            problems.append("%s: no block in dks.npc" % record)
            continue
        params, fields = blocks[record]

        style = wiki_field(page, "attack style") or ""
        style = re.sub(r"\[\[|\]\]", "", style).split("|")[0].strip().lower()
        want = STYLE_IDS.get(style)
        got = params.get("damagetype", "")
        got_id = STYLE_IDS.get(got.lstrip("^").replace("_style", ""))
        if want is None:
            problems.append("%s: wiki attack style %r is not a damagetype" % (record, style))
        elif got_id != want:
            problems.append(
                "%s: wiki says attack style %s (damagetype %d), config says %s"
                % (record, style, want, got or "nothing")
            )

        speed = wiki_field(page, "attack speed")
        if speed and params.get("attackrate", "").lstrip("^") != "dks_attackrate":
            problems.append("%s: attackrate should be ^dks_attackrate" % record)

        respawn = wiki_field(page, "respawn")
        if respawn and fields.get("respawnrate") != respawn:
            problems.append(
                "%s: wiki respawn %s, config respawnrate %s"
                % (record, respawn, fields.get("respawnrate"))
            )

    if problems:
        print("check_dks_contract: %d problem(s)" % len(problems), file=sys.stderr)
        for line in problems:
            print("  " + line, file=sys.stderr)
        return 1
    print("check_dks_contract: 3 king(s) agree with the pinned wiki")
    return 0


if __name__ == "__main__":
    sys.exit(main())
