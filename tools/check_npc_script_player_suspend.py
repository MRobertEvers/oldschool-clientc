#!/usr/bin/env python3
"""Sweep the server script pack for npc scripts that reach a player suspend.

An npc-driven script ([ai_queue<n>], [ai_timer], [ai_spawn], [ai_despawn],
[ai_walktrigger], [ai_ap/opnpc<n>], [ai_ap/opplayer<n>]) never holds protected
access to a player in LostCity: ScriptRunner.init(script, npc) binds the npc
alone, and npc_findhero (Engine-TS NpcOps.ts NPC_FINDHERO) adds ActivePlayer but
NOT ProtectedActivePlayer. Every player suspend -- p_pausebutton (so ~mesbox,
~chatnpc, ~chatplayer, ~objbox, the ~p_choice menus), p_delay, p_arrivedelay,
p_countdialog, p_namedialog -- is checkedHandler(ProtectedActivePlayer), so the
reference throws "NPC script error" at it.

The embedded server matches that since seam21 (torirs_server_scripts.c
run_or_park: a player suspend from an ai_* entry trigger aborts, naming the
trigger). This sweep lists every such script statically so content can move
the dialogue to the player's queue (queue/weakqueue/strongqueue start a NEW,
player-context script -- the LostCity way) before it becomes a runtime abort.

Reach is followed through ~proc gosubs and @label jumps; queue(...) family
calls are NOT followed (they are the fix). Each hit prints the chain from the
npc script to the suspending command.

Exit status: 0 always unless --strict, then 1 when any hit is not exempted.

  python3 tools/check_npc_script_player_suspend.py [--strict] [--content DIR]
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content", "server", "scripts")

SUSPEND_COMMANDS = {
    "p_pausebutton",
    "p_delay",
    "p_arrivedelay",
    "p_countdialog",
    "p_countdialog_noprompt",
    "p_namedialog",
}

NPC_TRIGGER = re.compile(
    r"^ai_(queue\d+|timer|spawn|despawn|walktrigger|apnpc\d|opnpc\d|applayer\d|opplayer\d)$"
)

HEADER = re.compile(r"^\[([a-z0-9_]+),([^\]]+)\]")
GOSUB = re.compile(r"~([a-z0-9_]+)")
JUMP = re.compile(r"@([a-z0-9_]+)")
# A command is called with arguments `p_delay(0)` or bare `p_pausebutton;`.
COMMAND = re.compile(r"(?<![~@.$%^a-z0-9_])([a-z_][a-z0-9_]*)(?![a-z0-9_:])")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    out = []
    for line in text.split("\n"):
        # Remove // comments outside string literals (content strings rarely
        # contain //, and a false cut only hides a call, never invents one).
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


GRANTS = ("p_finduid", "p_findmutualfriend", "p_findvisibleplayer", "npc_findhero")


def edges(body):
    """(target, file, line, granted) for every gosub/jump/suspend in a body.

    `granted` is True once a p_finduid-family call has appeared on this or an
    earlier line of the same body: those are the calls that give a script
    protected access in the reference (Engine-TS PlayerOps.ts P_FINDUID), so a
    suspend after one is legal. Line order stands in for control flow, which
    is the usual `if (p_finduid(uid) = true) { @label; }` shape.
    """
    granted = False
    for path, lineno, line in body:
        if any(re.search(r"\b%s\b" % g, line) for g in GRANTS):
            granted = True
        for m in GOSUB.finditer(line):
            yield ("proc", m.group(1)), path, lineno, granted
        for m in JUMP.finditer(line):
            yield ("label", m.group(1)), path, lineno, granted
        for m in COMMAND.finditer(line):
            if m.group(1) in SUSPEND_COMMANDS:
                yield ("!", m.group(1)), path, lineno, granted


def find_chains(bodies, start):
    """{(suspend command, granted): shortest chain from `start` to it}.

    Walked over (script, granted) so an ungranted path to a suspend is found
    even when a granted path to the same proc was seen first.
    """
    from collections import deque

    found = {}
    seen = {(start, False)}
    queue = deque([(start, False, [])])
    while queue:
        key, granted_in, chain = queue.popleft()
        for target, path, lineno, granted_here in edges(bodies.get(key, [])):
            granted = granted_in or granted_here
            step = chain + [(key, target, path, lineno)]
            if target[0] == "!":
                found.setdefault((target[1], granted), step)
                continue
            if (target, granted) in seen or target not in bodies:
                continue
            seen.add((target, granted))
            queue.append((target, granted, step))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--content", default=DEFAULT_CONTENT)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--brief", action="store_true", help="print chains for p_pausebutton only")
    ap.add_argument("--only", default="", help="substring filter on [trigger,subject]")
    ap.add_argument("--all", action="store_true",
                    help="also list scripts whose every suspend follows a p_finduid")
    args = ap.parse_args()

    bodies = load(args.content)
    hits = []
    for key in sorted(bodies):
        kind, subject = key
        if not NPC_TRIGGER.match(kind):
            continue
        if args.only and args.only not in "[%s,%s]" % key:
            continue
        chains = find_chains(bodies, key)
        ungranted = {cmd: chain for (cmd, granted), chain in chains.items() if not granted}
        granted = sorted({cmd for (cmd, g) in chains if g})
        if ungranted or (granted and args.all):
            finds_hero = any("npc_findhero" in line for _, _, line in bodies[key])
            hits.append((key, ungranted, granted, finds_hero))

    npc_scripts = sum(1 for k in bodies if NPC_TRIGGER.match(k[0]))
    by_command = {}
    dialogue = 0
    aborting = 0
    for key, ungranted, granted, finds_hero in hits:
        head = "[%s,%s]" % key
        if ungranted:
            aborting += 1
        if "p_pausebutton" in ungranted:
            dialogue += 1
        notes = []
        if granted:
            notes.append("after p_finduid: " + ", ".join(granted))
        if finds_hero:
            notes.append("calls npc_findhero")
        print("%s reaches %s%s" % (head, ", ".join(sorted(ungranted)) or "-",
                                   ("  (" + "; ".join(notes) + ")") if notes else ""))
        for command in sorted(ungranted):
            by_command[command] = by_command.get(command, 0) + 1
            if args.brief and command != "p_pausebutton":
                continue
            print("  %s:" % command)
            for src, target, path, lineno in ungranted[command]:
                rel = os.path.relpath(path, ROOT)
                name = target[1] if target[0] == "!" else ("~" if target[0] == "proc" else "@") + target[1]
                print("    [%s,%s] -> %s  %s:%d" % (src[0], src[1], name, rel, lineno))
    print("check-npc-script-player-suspend: %d npc scripts, %d reach a player suspend with no "
          "player bound before it (%s); %d of them reach p_pausebutton (dialogue)"
          % (npc_scripts, aborting,
             " ".join("%s=%d" % kv for kv in sorted(by_command.items())) or "none", dialogue))
    return 1 if (args.strict and aborting) else 0


if __name__ == "__main__":
    sys.exit(main())
