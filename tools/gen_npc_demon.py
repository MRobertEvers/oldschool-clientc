#!/usr/bin/env python3
"""
gen_npc_demon -- state every npc's demon attribute from its wiki infobox.

    tools/gen_npc_demon.py            # report only
    tools/gen_npc_demon.py --write    # rewrite the generated .npc file

Source: the cached `Infobox Monster` pages (OSRS-Content/osrs-content/wiki/
monsters), parsed by wiki_infobox, joined to this cache's npc ids by each
version block's own `id`/`idN` list -- never by name, as gen_npc_immunity.py.
One infobox row answers the question:

    |attributes = demon[, ...]    -> param=demon,yes

The cached pages trail the live wiki (none of the Fractured Archive's bosses is
in them), so the live wiki's own list of demons is read beside them: its
`infobox_monster` bucket where `attribute` is demon, kept as a snapshot in
wiki/demon_attribute.json (`--fetch` refreshes it). An id either source calls a
demon is one.

No cache param states the attribute, so before this only Dreadnoth's own config
did, and the scorching bow's special (which works on demons alone) refused
every tormented demon. Not a demon is the param's declared default
(npc_combat.param), so nothing is written for one.

Only npcs that already have a server record get a block (see gen_npc_immunity.py
for why), and the output is a `.generated.` file, so a hand-authored
`param=demon` wins over the line here.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import sys
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_npc_immunity as imm  # noqa: E402
import wiki_infobox as wi  # noqa: E402

OUT = os.path.join(imm.CONTENT, "server", "scripts", "npc", "configs", "npc_demon.generated.npc")
SNAPSHOT = os.path.join(imm.CONTENT, "wiki", "demon_attribute.json")
BUCKET_QUERY = ("bucket('infobox_monster').select('page_name','id')"
                ".where('attribute','demon').limit(5000).run()")


def fetch_snapshot() -> None:
    url = "https://oldschool.runescape.wiki/api.php?" + urllib.parse.urlencode(
        {"action": "bucket", "format": "json", "query": BUCKET_QUERY})
    req = urllib.request.Request(url, headers={"User-Agent": "torirs gen_npc_demon"})
    with urllib.request.urlopen(req, timeout=60) as r:
        rows = json.load(r)["bucket"]
    rows = sorted(({"page_name": row["page_name"], "id": sorted(row["id"], key=str)}
                   for row in rows), key=lambda row: (row["page_name"], row["id"]))
    with open(SNAPSHOT, "w", encoding="utf-8") as f:
        json.dump(rows, f, indent=1)
        f.write("\n")
    print("fetched %d rows -> %s" % (len(rows), SNAPSHOT))


def load_snapshot() -> dict[int, str]:
    """id -> page, from the live wiki's list."""
    if not os.path.exists(SNAPSHOT):
        return {}
    out: dict[int, str] = {}
    with open(SNAPSHOT, encoding="utf-8") as f:
        for row in json.load(f):
            for npc_id in row["id"]:
                if str(npc_id).isdigit():
                    out.setdefault(int(npc_id), row["page_name"])
    return out


def is_demon(fields: dict[str, str]) -> bool:
    raw = fields.get("attributes")
    if raw is None:
        return False
    words = [w.strip().lower() for w in wi.clean_text(raw).split(",")]
    return "demon" in words


def load_authored_records() -> set[str]:
    """Npcs a hand-authored .npc block describes (the Fractured Archive's
    bosses have no server_fields block): a record exists for them too."""
    names = set()
    scripts = os.path.join(imm.CONTENT, "server", "scripts")
    for path in glob.glob(os.path.join(scripts, "**", "*.npc"), recursive=True):
        if ".generated." in os.path.basename(path):
            continue
        with open(path, encoding="latin-1") as f:
            for line in f:
                m = imm.BLOCK_RE.match(line.strip())
                if m:
                    names.add(m.group(1))
    return names


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--fetch", action="store_true",
                        help="refresh wiki/demon_attribute.json from the live wiki first")
    args = parser.parse_args()
    if args.fetch:
        fetch_snapshot()

    names = imm.load_compack()
    records = imm.load_server_records() | load_authored_records()

    # id -> (demon, page) from the first page naming it.
    answers: dict[int, tuple[bool, str]] = {}
    conflicts: list[str] = []
    for path in sorted(glob.glob(os.path.join(wi.MONSTERS_DIR, "*.wikitext"))):
        title = os.path.basename(path)[: -len(".wikitext")]
        with open(path, encoding="utf-8") as f:
            versions = wi.parse_page(f.read())
        for version in versions:
            answer = (is_demon(version["fields"]), title)
            for npc_id in version["ids"]:
                if npc_id in answers and answers[npc_id][0] != answer[0]:
                    conflicts.append("npc %d: %r says %s, %r says %s" % (
                        npc_id, answers[npc_id][1], answers[npc_id][0], title, answer[0]))
                    continue
                answers.setdefault(npc_id, answer)
    live = load_snapshot()
    live_only = 0
    for npc_id, title in live.items():
        if not answers.get(npc_id, (False, ""))[0]:
            answers[npc_id] = (True, title)
            live_only += 1

    blocks: list[tuple[str, int, str]] = []
    no_record = 0
    for npc_id, (demon, title) in sorted(answers.items()):
        if not demon:
            continue
        name = names.get(npc_id)
        if name is None or name not in records:
            no_record += 1
            continue
        blocks.append((name, npc_id, title))

    print("wiki ids with an answer: %d" % len(answers))
    print("live wiki demons the cached pages miss: %d" % live_only)
    print("demons with a server record: %d" % len(blocks))
    print("demons with no server record (skipped): %d" % no_record)
    print("ids two pages disagree on (first page kept): %d" % len(conflicts))
    for line in conflicts[:20]:
        print("  " + line)

    if not args.write:
        return

    out = [
        "// Generated by tools/gen_npc_demon.py --write. Re-running rewrites this file.",
        "//",
        "// Every npc whose wiki infobox states the demon attribute (`|attributes =",
        "// demon`, the cached pages and the live wiki's list), joined to this cache's",
        "// npc ids by the wiki's own id lists. Read by the scorching bow's special (pvm_scorching_bow.rs2), which",
        "// works on demons alone. An npc absent here is not a demon, the param's",
        "// declared default (skill_combat/configs/npc_combat.param).",
        "//",
        "// A `.generated.` file: it merges below hand-authored blocks.",
        "//",
        "// %d npcs." % len(blocks),
        "",
    ]
    for name, npc_id, title in sorted(blocks):
        out.append("[%s]" % name)
        out.append("param=demon,yes // %s (npc %d)" % (title, npc_id))
        out.append("")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out).rstrip() + "\n")
    print("wrote %s" % OUT)


if __name__ == "__main__":
    main()
