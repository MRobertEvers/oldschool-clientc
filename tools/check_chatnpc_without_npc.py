#!/usr/bin/env python3
"""Sweep the server script pack for npc dialogue reached with no npc bound.

`~chatnpc`/`~chatnpc_anim` (interface_chat/scripts/chat.rs2) read the ACTIVE
npc -- `npc_type` for the head, `npc_name` for the label, `npc_coord` and
`npc_facesquare` to turn both ends -- and every one of those aborts the script
("npc_coord with no active npc") when nothing is bound. The page has already
mounted and armed its resume button by then, so the player sees one page and
the conversation closes on its first continue: Mountain Daughter's Shining
Pool (a bare [oploc1]) and Pest Control's Squire (a player [queue]) both died
this way (seam23; docs/QUEST_AUTHORING.md trap 22).

This lists every script whose entry trigger binds no npc -- anything that is
not [opnpc*]/[apnpc*]/[ai_*] ([oploc*], [opheld*], [opobj*], [queue],
[softtimer], [timer], [if_button], [debugproc], ...) -- that reaches
~chatnpc_anim (and so ~chatnpc) through ~proc gosubs and @label jumps with no
npc_find*/npc_add* on an earlier line of the path. A queue(...) family call is
NOT followed: a queued script starts with no npc (the Squire's shape is a hit
at the [queue] itself). Line order stands in for control flow, which matches
the usual `if (npc_find(...) = true) { ~chatnpc(...); }` shape.

The fix for a hit is one of:
  - npc_find(coord, <speaker>, <radius>, 0) first, when the speaker is in the
    world (rd_table_says, trapped_drezel.rs2 [oploc2,pip_prisondoor]);
  - ~chatnpc_specific_anim("<Name>", <npc record>, anim, text), which needs no
    active npc (LostCity chat.rs2:393-401 [proc,chatnpc_specific]);
  - ~mesbox, when the game shows the line with no speaker (Asleif's spirit).

Exit status: 0 always unless --strict, then 1 when any hit is not listed in
KNOWN_OPEN (the hits seam23 found in files it did not own). --all-strict fails
on any hit. [debugproc] entries are skipped unless --debugproc.

  python3 tools/check_chatnpc_without_npc.py [--strict|--all-strict] [--brief]
"""

import argparse
import os
import re
import sys
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content", "server", "scripts")

# The procs that read the active npc. ~chatnpc gosubs ~chatnpc_anim, so it is
# reached through the walk; naming both keeps the chain short in the report.
TARGET_PROCS = {"chatnpc", "chatnpc_anim"}

# Known open hits, found by the seam23 sweep, each in a file another worker
# owned during that pass. --strict does not fail on these; it fails on any NEW
# hit, and names an entry here that no longer hits so it can be deleted.
KNOWN_OPEN = {
    "[oploc1,ogre_bedman_loc]": "Zogre Flesh Eaters: Sithik in his bed is a loc; zogre_finish.rs2 [proc,zfe_sithik_man_talk]",
    "[oplocu,ogre_bedman_loc]": "Zogre Flesh Eaters: zogre_finish.rs2 item-on-Sithik branch",
    "[oploc1,ogre_bedogre_loc]": "Zogre Flesh Eaters: zogre_finish.rs2 [proc,zfe_sithik_ogre_talk]",
    "[oploc1,sithiks_cupboard]": "Zogre Flesh Eaters: Sithik talks while you search his cupboard (zogre_finish.rs2)",
    "[oploc1,sithiks_drawers]": "Zogre Flesh Eaters: Sithik talks while you search his drawers (zogre_finish.rs2)",
    "[oploc1,sithiks_wardrobe]": "Zogre Flesh Eaters: Sithik talks while you search his wardrobe (zogre_finish.rs2)",
    "[oploc1,pog_spirit_tree_multi]": "Path of Glouphrie: pog_longramble.rs2:90 spirit tree line",
    "[oploc1,viking_warrior_ladder_down]": "Fremennik Trials: viking_thorvald.rs2:44 Thorvald from the ladder",
    "[oploc1,tob_male_orator]": "Theatre of Blood: tob.rs2:34 orator loc",
    "[oploc3,fossil_mermaid_driftnets]": "Drift net fishing: drift_net_fishing.rs2:273 Annette from the nets",
}

NPC_BOUND_TRIGGER = re.compile(r"^(opnpc[0-9ut]|apnpc[0-9ut]|ai_[a-z0-9_]+)$")
NOT_AN_ENTRY = {"proc", "label"}

HEADER = re.compile(r"^\[([a-z0-9_]+),([^\]]+)\]")
GOSUB = re.compile(r"~([a-z0-9_]+)")
JUMP = re.compile(r"@([a-z0-9_]+)")
BINDS = re.compile(r"(?<![.a-z0-9_])npc_(find[a-z_]*|adds?)\b")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    out = []
    for line in text.split("\n"):
        in_str = False
        cut = len(line)
        for i, ch in enumerate(line):
            if ch == '"':
                in_str = not in_str
            elif not in_str and line.startswith("//", i):
                cut = i
                break
        out.append(line[:cut])
    return "\n".join(out)


def strip_strings(line):
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)


def load(content_dir):
    """Return {(kind, name): [(file, line, text), ...]} of script bodies."""
    bodies = {}
    for dirpath, _, files in os.walk(content_dir):
        if os.sep + "build" in dirpath:
            continue
        for fname in files:
            if not fname.endswith(".rs2"):
                continue
            path = os.path.join(dirpath, fname)
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = strip_comments(fh.read())
            current = None
            for lineno, line in enumerate(text.split("\n"), 1):
                m = HEADER.match(line)
                if m:
                    current = (m.group(1), m.group(2).strip())
                    bodies.setdefault(current, [])
                    continue
                if current is not None:
                    bodies[current].append((path, lineno, strip_strings(line)))
    return bodies


def binders(bodies):
    """Scripts that bind an npc themselves or through a gosub/jump they make
    (`@arena_spawn_general;` npc_adds the General, then the caller talks)."""
    direct = {k for k, body in bodies.items() if any(BINDS.search(line) for _, _, line in body)}
    callees = {}
    for k, body in bodies.items():
        out = set()
        for _, _, line in body:
            out.update(("proc", m.group(1)) for m in GOSUB.finditer(line))
            out.update(("label", m.group(1)) for m in JUMP.finditer(line))
        callees[k] = out
    result = set(direct)
    changed = True
    while changed:
        changed = False
        for k, out in callees.items():
            if k not in result and k[0] in NOT_AN_ENTRY and out & result:
                result.add(k)
                changed = True
    return result


BINDERS = set()


def edges(body):
    """(target, file, line, bound) for every gosub/jump in a body; `bound` is
    True once an npc_find*/npc_add* -- or a call to a script that makes one --
    appeared on an earlier line (or on this one, before the call)."""
    bound = False
    for path, lineno, line in body:
        if BINDS.search(line):
            bound = True
        for m in GOSUB.finditer(line):
            yield ("proc", m.group(1)), path, lineno, bound
            if ("proc", m.group(1)) in BINDERS:
                bound = True
        for m in JUMP.finditer(line):
            yield ("label", m.group(1)), path, lineno, bound
            if ("label", m.group(1)) in BINDERS:
                bound = True


def unbound_chain(bodies, start):
    """Shortest chain from `start` to a TARGET_PROCS gosub with no npc bound,
    or None. Bound paths are not followed at all."""
    seen = {start}
    queue = deque([(start, [])])
    while queue:
        key, chain = queue.popleft()
        for target, path, lineno, bound in edges(bodies.get(key, [])):
            if bound:
                continue
            step = chain + [(key, target, path, lineno)]
            if target[0] == "proc" and target[1] in TARGET_PROCS:
                return step
            if target in seen or target not in bodies:
                continue
            seen.add(target)
            queue.append((target, step))
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--content", default=DEFAULT_CONTENT)
    ap.add_argument("--strict", action="store_true", help="exit 1 on any hit under quests/")
    ap.add_argument("--all-strict", action="store_true", help="exit 1 on any hit")
    ap.add_argument("--brief", action="store_true", help="one line per hit, no chain")
    ap.add_argument("--debugproc", action="store_true",
                    help="also sweep [debugproc,...] entries (test scaffolding: the bmp photo procs"
                         " open a page with no npc on purpose and are never run by a player)")
    ap.add_argument("--only", default="", help="substring filter on path or [trigger,subject]")
    args = ap.parse_args()

    bodies = load(args.content)
    BINDERS.update(binders(bodies))
    entries = [k for k in sorted(bodies) if k[0] not in NOT_AN_ENTRY and not NPC_BOUND_TRIGGER.match(k[0])
               and (args.debugproc or k[0] != "debugproc")]
    hits = []
    for key in entries:
        chain = unbound_chain(bodies, key)
        if chain is None:
            continue
        where = os.path.relpath(chain[-1][2], args.content)
        if args.only and args.only not in where and args.only not in "[%s,%s]" % key:
            continue
        hits.append((key, chain, where))

    quest_hits = 0
    new_hits = 0
    for key, chain, where in hits:
        head = "[%s,%s]" % key
        is_quest = where.startswith("quests" + os.sep)
        quest_hits += is_quest
        known = KNOWN_OPEN.get(head)
        if known is None:
            new_hits += 1
        last = chain[-1]
        print("%s -> ~%s  %s:%d%s%s" % (head, last[1][1], where, last[3],
                                     "  (quest)" if is_quest else "",
                                     "  [known open: %s]" % known if known else "  [NEW]"))
        if args.brief:
            continue
        for src, target, path, lineno in chain:
            rel = os.path.relpath(path, args.content)
            name = ("~" if target[0] == "proc" else "@") + target[1]
            print("    [%s,%s] -> %s  %s:%d" % (src[0], src[1], name, rel, lineno))
    hit_heads = {"[%s,%s]" % key for key, _, _ in hits}
    if not args.only:
        for head in sorted(set(KNOWN_OPEN) - hit_heads):
            print("stale KNOWN_OPEN entry (no longer a hit, delete it): %s" % head)
    print("check-chatnpc-without-npc: %d entry scripts with no npc bound, %d reach ~chatnpc/~chatnpc_anim "
          "with no npc_find*/npc_add* before it (%d under quests/, %d not in KNOWN_OPEN)"
          % (len(entries), len(hits), quest_hits, new_hits))
    if args.all_strict and hits:
        return 1
    if args.strict and new_hits:
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
