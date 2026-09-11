#!/usr/bin/env python3
"""Build MULTI_NPCS.md from the cache's live static multi-NPC roster.

The checklist is intentionally one row per *spawned shell*, rather than one
row per display-name variant or switch. That is the unit NPC_INFO sends and
the client resolves independently for each player.

Existing checked rows are preserved when the file is regenerated, so the
document can be used as a durable one-at-a-time audit rather than a snapshot.
"""

import argparse
import collections
import os
import re

import gen_multinpc_catalog as catalog


CHECKED_ROW = re.compile(r"^- \[x\] `([^`]+)` ")


def load_checked(path):
    checked = set()
    if not os.path.exists(path):
        return checked
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = CHECKED_ROW.match(line)
            if match:
                checked.add(match.group(1))
    return checked


def load_spawns(root):
    """NPC shell -> every `(x, z, level)` row under server/scripts."""
    out = collections.defaultdict(list)
    for dirpath, _dirnames, filenames in os.walk(root):
        for filename in filenames:
            if not filename.endswith(".spawn"):
                continue
            path = os.path.join(dirpath, filename)
            section = ""
            with open(path, encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    line = line.strip()
                    if line.startswith("===="):
                        section = line
                        continue
                    if not line or line.startswith("//") or "NPC" not in section:
                        continue
                    fields = line.split()
                    if len(fields) >= 4:
                        out[fields[0]].append(tuple(map(int, fields[1:4])))
    return out


def display_names(shell):
    names = sorted({display for _index, _target, display in shell["variants"] if display})
    return ", ".join(names) if names else "(hidden/unnamed variants only)"


def spawn_summary(spawns):
    unique = sorted(set(spawns))
    shown = ", ".join("(%d, %d, %d)" % coord for coord in unique[:3])
    if len(unique) > 3:
        shown += ", …"
    noun = "spawn" if len(spawns) == 1 else "spawns"
    return f"{len(spawns)} {noun}: {shown}"


def row(shell, spawns, refs, checked):
    base = shell["base"]
    default = catalog.default_variant(shell["variants"])
    default_text = f"`{default}` (visible)" if default else "hidden"
    mark = "x" if base in checked else " "
    text = (
        f"- [{mark}] `{base}` — {display_names(shell)}; "
        f"{shell['switch_type']} `%{shell['switch_name']}`; default {default_text}; "
        f"{spawn_summary(spawns)}; {len(refs)} content refs."
    )
    if base == "head_wizard":
        text += " Import alias fixed; canonical Sedridor spawn restored."
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--content", required=True)
    parser.add_argument("--out", default="MULTI_NPCS.md")
    args = parser.parse_args()

    scripts = os.path.join(args.content, "server", "scripts")
    blocks = catalog.load_blocks(os.path.join(args.content, "configs", "all.npc"))
    shells = {shell["base"]: shell for shell in catalog.collect_shells(blocks)}
    spawns = load_spawns(scripts)
    refs = catalog.scan_content_refs(scripts)
    checked = load_checked(args.out)
    # The first completed audit item predates the checklist itself.
    checked.add("head_wizard")

    spawned = [shells[name] for name in spawns if name in shells]
    visible = [s for s in spawned if catalog.default_variant(s["variants"]) is not None]
    hidden_wired = [
        s
        for s in spawned
        if catalog.default_variant(s["variants"]) is None and refs.get(s["switch_name"])
    ]
    hidden_unwired = [
        s
        for s in spawned
        if catalog.default_variant(s["variants"]) is None and not refs.get(s["switch_name"])
    ]

    sections = [
        (
            "Visible at the default value",
            "These must appear immediately after their wrapper config loads.",
            visible,
        ),
        (
            "Hidden by default, content-wired",
            "These must appear or change when their per-player quest/game var changes.",
            hidden_wired,
        ),
        (
            "Hidden by default, no content reference",
            "These require individual ownership review; some are dynamic activity NPCs, while "
            "others are genuine missing quest-state wiring.",
            hidden_unwired,
        ),
    ]

    lines = [
        "# Multi-NPC audit",
        "",
        "This is the one-at-a-time checklist for every multi-NPC shell in the live static "
        "world roster. The server sends the shell; each client must load it, resolve its "
        "varp/varbit against that player's state, retain hidden shells, and remorph them "
        "when that player's state changes.",
        "",
        f"- Spawned multi-NPC shells: **{len(spawned)}**",
        f"- Visible at the default value: **{len(visible)}**",
        f"- Hidden by default with content references: **{len(hidden_wired)}**",
        f"- Hidden by default without a content reference: **{len(hidden_unwired)}**",
        f"- Individually checked: **{sum(s['base'] in checked for s in spawned)} / {len(spawned)}**",
        "",
        "A checked row means that shell's spawn, transform selection, per-player isolation, "
        "hidden behavior, and live remorph path have been individually validated. It does "
        "not mean every quest using that NPC is complete.",
        "",
    ]
    for title, description, group in sections:
        lines.extend([f"## {title}", "", description, ""])
        for shell in sorted(group, key=lambda item: (item["base"] != "head_wizard", item["base"])):
            base = shell["base"]
            lines.append(row(shell, spawns[base], refs.get(shell["switch_name"], []), checked))
        lines.append("")

    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))
    print(f"wrote {args.out}: {len(spawned)} shells, {sum(s['base'] in checked for s in spawned)} checked")


if __name__ == "__main__":
    main()
