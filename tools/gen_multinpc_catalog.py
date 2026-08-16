#!/usr/bin/env python3
"""Catalog every `multinpc` shell in the npc cache: which varp/varbit picks its
live variant, whether that shell is actually spawned in the world (so is
invisible today, per [[world-spawn-roster]] / the gertrude+veos bug), and
whether any quest script already writes the switch.

    tools/gen_multinpc_catalog.py \
        --content OSRS-Content/osrs239-content \
        --out-dir tools/data

A `multinpc` record is a shell: no `model*=` of its own, just `multivarp=` or
`multivarbit=` naming the switch and `multinpc1..N=` pointing at the variants
that actually have a model. Nothing in the engine currently reads that switch
and substitutes the right variant before a spawn reaches NPC_INFO, so every
spawn of a shell id renders nothing — see the session that found this on
Gertrude/Veos. Fixing the engine is one piece; the other is that some of these
switches are never written by *any* quest script, so even a correct engine
would show the wrong (usually the "hidden") variant forever. This script tells
the two apart.

Output, under `--out-dir`:

  - `multinpc_shells.csv`   one row per shell record (2.4k+): base id, switch,
                            variant chain, whether it's in the world spawn
                            roster, how many places in `server/scripts/`
                            reference the switch by name.
  - `multinpc_switches.csv` one row per *unique* varp/varbit (~600): how many
                            shells key off it, whether any of them is spawned,
                            whether content ever writes it. Sorted worst-first:
                            spawned-and-never-written switches are the npcs
                            that are invisible in the game right now.

Content cannot declare a varbit (see [[content-cannot-declare-varbits]]) so
every switch name here already resolves in `configs/all.varp`/`all.varbit` --
this is purely an attribution pass, not a naming/allocation one.
"""

import argparse
import csv
import os
import re
import sys


def load_blocks(path):
    """`configs/all.npc` -- `[name]` blocks of `key=value`.

    First value wins for a repeated key, matching `multinpc1`, `multinpc2`,
    ... each being its own key (see tools/gen_spawns.py, same grammar).
    """
    out = {}
    current = None
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
                out[current] = {}
            elif current is not None and "=" in line and not line.startswith("//"):
                key, value = line.split("=", 1)
                out[current].setdefault(key, value)
    return out


MULTINPC_KEY = re.compile(r"^multinpc(\d+)$")


def collect_shells(blocks):
    """Every record that is a multinpc shell (has at least one multinpcN)."""
    shells = []
    for name, block in blocks.items():
        idx_keys = []
        for key in block:
            match = MULTINPC_KEY.match(key)
            if match:
                idx_keys.append((int(match.group(1)), key))
        if not idx_keys:
            continue
        idx_keys.sort()

        switch_type = None
        switch_name = None
        if "multivarp" in block:
            switch_type, switch_name = "varp", block["multivarp"]
        elif "multivarbit" in block:
            switch_type, switch_name = "varbit", block["multivarbit"]

        variants = []
        for idx, key in idx_keys:
            target = block[key]
            if target == "-1":
                variants.append((idx, None, None))
                continue
            tblock = blocks.get(target, {})
            variants.append((idx, target, tblock.get("name")))

        shells.append(
            {
                "base": name,
                "switch_type": switch_type or "",
                "switch_name": switch_name or "",
                "variants": variants,
            }
        )
    return shells


def load_spawn_names(spawn_dir):
    """Every npc name the world roster actually spawns (base ids included)."""
    names = set()
    if not os.path.isdir(spawn_dir):
        return names
    for entry in os.listdir(spawn_dir):
        if not entry.endswith(".spawn"):
            continue
        section = None
        with open(os.path.join(spawn_dir, entry), encoding="utf-8", errors="replace") as handle:
            for line in handle:
                line = line.strip()
                if line.startswith("===="):
                    section = line
                    continue
                if not line or line.startswith("//") or section is None:
                    continue
                if "NPC" in section:
                    names.add(line.split()[0])
    return names


NAME_REF = re.compile(r"%([a-zA-Z_][a-zA-Z0-9_]*)")


def scan_content_refs(scripts_root):
    """switch name -> list of `relpath:lineno` for every `%name` reference
    under `server/scripts/` (quest scripts read/write varp+varbit the same
    way, so this doesn't need to know which kind a given switch is)."""
    refs = {}
    for dirpath, _dirnames, filenames in os.walk(scripts_root):
        for fname in filenames:
            if not fname.endswith(".rs2"):
                continue
            path = os.path.join(dirpath, fname)
            rel = os.path.relpath(path, scripts_root)
            with open(path, encoding="utf-8", errors="replace") as handle:
                for lineno, line in enumerate(handle, 1):
                    for match in NAME_REF.finditer(line):
                        refs.setdefault(match.group(1), []).append(f"{rel}:{lineno}")
    return refs


def variant_summary(variants):
    parts = []
    for idx, target, display in variants:
        if target is None:
            parts.append(f"{idx}=(hidden)")
        else:
            parts.append(f"{idx}={target}" + (f" ({display})" if display else ""))
    return "; ".join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--content", required=True, help="path to osrs239-content")
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    npc_path = os.path.join(args.content, "configs", "all.npc")
    spawn_dir = os.path.join(args.content, "server", "scripts", "areas", "world", "configs")
    scripts_root = os.path.join(args.content, "server", "scripts")

    blocks = load_blocks(npc_path)
    shells = collect_shells(blocks)
    spawn_names = load_spawn_names(spawn_dir)
    content_refs = scan_content_refs(scripts_root)

    os.makedirs(args.out_dir, exist_ok=True)

    shells_csv = os.path.join(args.out_dir, "multinpc_shells.csv")
    with open(shells_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "base",
                "switch_type",
                "switch_name",
                "is_spawned",
                "variant_count",
                "variants",
                "content_ref_count",
            ]
        )
        for shell in sorted(shells, key=lambda s: s["base"]):
            refs = content_refs.get(shell["switch_name"], [])
            writer.writerow(
                [
                    shell["base"],
                    shell["switch_type"],
                    shell["switch_name"],
                    "yes" if shell["base"] in spawn_names else "no",
                    len(shell["variants"]),
                    variant_summary(shell["variants"]),
                    len(refs),
                ]
            )

    # Group by unique switch.
    by_switch = {}
    for shell in shells:
        key = (shell["switch_type"], shell["switch_name"])
        entry = by_switch.setdefault(
            key,
            {"shells": [], "spawned_shells": [], "variant_count": 0},
        )
        entry["shells"].append(shell["base"])
        if shell["base"] in spawn_names:
            entry["spawned_shells"].append(shell["base"])
        entry["variant_count"] += len([v for v in shell["variants"] if v[1] is not None])

    switches_csv = os.path.join(args.out_dir, "multinpc_switches.csv")
    rows = []
    for (switch_type, switch_name), entry in by_switch.items():
        refs = content_refs.get(switch_name, [])
        is_spawned = bool(entry["spawned_shells"])
        has_content = bool(refs)
        # Worst first: spawned in the world right now, but no script ever
        # sets the switch -- these are invisible in-game today with zero
        # content coverage, per the gertrude/veos finding.
        priority = 0 if (is_spawned and not has_content) else (1 if is_spawned else 2)
        rows.append(
            (
                priority,
                switch_type,
                switch_name,
                len(entry["shells"]),
                entry["variant_count"],
                "yes" if is_spawned else "no",
                ", ".join(sorted(entry["spawned_shells"])),
                len(refs),
                "; ".join(refs[:3]),
            )
        )
    rows.sort(key=lambda r: (r[0], -r[3]))

    with open(switches_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "switch_type",
                "switch_name",
                "shell_count",
                "variant_count",
                "spawned_in_world",
                "spawned_shell_bases",
                "content_ref_count",
                "sample_content_refs",
            ]
        )
        for row in rows:
            writer.writerow(row[1:])

    gap_count = sum(1 for r in rows if r[0] == 0)
    spawned_count = sum(1 for r in rows if r[0] <= 1)
    print(f"shells: {len(shells)}  unique switches: {len(by_switch)}", file=sys.stderr)
    print(f"switches spawned in world: {spawned_count}", file=sys.stderr)
    print(f"switches spawned AND never written by content (gaps): {gap_count}", file=sys.stderr)
    print(f"wrote {shells_csv}", file=sys.stderr)
    print(f"wrote {switches_csv}", file=sys.stderr)


if __name__ == "__main__":
    main()
