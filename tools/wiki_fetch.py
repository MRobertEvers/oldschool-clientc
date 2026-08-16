#!/usr/bin/env python3
"""
wiki_fetch — pull OSRS Wiki monster pages for the npc_roster.csv roster.

    tools/wiki_npc_roster.py --write            # build the roster first
    tools/wiki_fetch.py --fetch                 # pull, cache to disk
    tools/wiki_fetch.py                          # report what's cached, no network

See docs/NPC_WIKI_STATS_PLAN.md §3. This does not crawl `Category:Monsters` —
that was the plan's original shape, but the roster (`npc_roster.csv`) already
names exactly which npcs matter, at 553 distinct display names for 1,700 ids,
so a targeted fetch by title is smaller and faster than a category walk plus
redirect chasing over pages nothing spawns. `action=query&titles=` accepts up
to 50 titles per request and resolves redirects itself when `redirects=1` is
set, so "Vardorvis" and "Duke Sucellus" and every other alias the roster names
come back as one batch of pages with the redirect table alongside them.

## What gets written

    OSRS-Content/osrs239-content/wiki/monsters/<title>.wikitext   raw wikitext
    OSRS-Content/osrs239-content/wiki/manifest.tsv                title, revid, redirected_from, fetch_date
    OSRS-Content/osrs239-content/wiki/disambig.tsv                roster name -> candidate titles fetched
                                                                    for a name whose own page disambiguates

One file per resolved wiki page (title, not per-npc — several ids share a
page). `manifest.tsv` is the record of *when* and *from what revision*; nothing
here re-fetches a page already in the manifest unless `--refetch` is passed, so
a normal run after the first is a no-op against the network and a diff against
the manifest is how a deliberate re-crawl gets reviewed.

Etiquette: one request per second, a descriptive User-Agent with a contact
address, no parallelism. ~553 titles / 50 per batch ≈ 12 requests — well under
a minute even serialized like this.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wiki_infobox  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
ROSTER = os.path.join(CONTENT, "wiki", "npc_roster.csv")
OUT_DIR = os.path.join(CONTENT, "wiki", "monsters")
MANIFEST = os.path.join(CONTENT, "wiki", "manifest.tsv")
DISAMBIG_TSV = os.path.join(CONTENT, "wiki", "disambig.tsv")

API = "https://oldschool.runescape.wiki/api.php"
UA = "3draster-npc-stats/1.0 (mrobertevers@gmail.com; docs/NPC_WIKI_STATS_PLAN.md)"
BATCH = 50

# The roster's `display_name` is the cache's own npc name, and that is not
# always the wiki's page title — mostly disambiguation the cache has no room
# for. Verified by hand against the wiki for every name this crawl reported
# as "not found" on its first run; extend this table rather than guessing at
# a fuzzy match, because a wrong title silently binds the wrong monster.
TITLE_OVERRIDE = {}


def safe_filename(title: str) -> str:
    return re.sub(r"[^A-Za-z0-9._ -]", "_", title).strip() + ".wikitext"


def api(params: dict) -> dict:
    url = API + "?" + urllib.parse.urlencode({**params, "format": "json"})
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=60) as response:
        return __import__("json").load(response)


def load_roster_names() -> list[str]:
    with open(ROSTER, newline="", encoding="utf-8") as f:
        names = {row["display_name"].strip() for row in csv.DictReader(f)}
    names.discard("")
    return sorted(TITLE_OVERRIDE.get(n, n) for n in names)


def load_manifest() -> dict[str, dict]:
    if not os.path.exists(MANIFEST):
        return {}
    with open(MANIFEST, newline="", encoding="utf-8") as f:
        return {row["title"]: row for row in csv.DictReader(f, delimiter="\t")}


def write_manifest(rows: dict[str, dict]) -> None:
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    fields = ["title", "revid", "requested_as", "fetch_date"]
    with open(MANIFEST, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, delimiter="\t")
        w.writeheader()
        for title in sorted(rows):
            w.writerow(rows[title])


def fetch_titles(wanted: list[str], manifest: dict[str, dict]) -> tuple[int, list[str]]:
    """Fetches `wanted` titles into `manifest` and `OUT_DIR` in place. Returns
    (pages written, titles nothing came back for)."""
    os.makedirs(OUT_DIR, exist_ok=True)
    not_found: list[str] = []
    fetched = 0

    for i in range(0, len(wanted), BATCH):
        batch = wanted[i : i + BATCH]
        data = api(
            {
                "action": "query",
                "prop": "revisions",
                "rvprop": "content|ids",
                "rvslots": "main",
                "redirects": 1,
                "titles": "|".join(batch),
            }
        )
        query = data.get("query", {})
        redirect_from = {r["to"]: r["from"] for r in query.get("redirects", [])}
        normalized_from = {n["to"]: n["from"] for n in query.get("normalized", [])}
        batch_set = set(batch)
        covered: set[str] = set()

        for page in query.get("pages", {}).values():
            if "missing" in page:
                continue
            title = page["title"]
            revs = page.get("revisions")
            if not revs:
                continue
            content = revs[0]["slots"]["main"]["*"]
            # Two roster names can normalize onto the same wiki title (e.g. a
            # cache name differing only in case) — prefer whichever input
            # spelling exactly matches the title MediaWiki already returned.
            if title in batch_set:
                requested_as = title
            else:
                requested_as = redirect_from.get(title, normalized_from.get(title, title))
            covered.add(title)
            covered.add(requested_as)
            with open(os.path.join(OUT_DIR, safe_filename(title)), "w", encoding="utf-8") as f:
                f.write(content)
            manifest[title] = {
                "title": title,
                "revid": str(revs[0]["revid"]),
                "requested_as": requested_as,
                "fetch_date": datetime.date.today().isoformat(),
            }
            fetched += 1

        for name in batch:
            if name not in covered:
                not_found.append(name)

        time.sleep(1)

    return fetched, not_found


def resolve_disambiguations(manifest: dict[str, dict]) -> None:
    """A roster name whose page carries no `Infobox Monster` is a
    disambiguation page (`Cave goblin`, `Warrior`, ...) — the wiki's own
    answer to "which npc" is a list of more specific titles. Fetch every
    candidate those pages link to, and record roster-name -> candidate-title
    in `disambig.tsv` so `gen_npc_stats.py` can pick the one candidate whose
    own `id`/`idN` list actually contains the npc id being resolved. That
    check happens there, not here, because it needs the npc id and this pass
    only has names.
    """
    names = load_roster_names()
    covered = set(manifest) | {row["requested_as"] for row in manifest.values()}
    disambig_pairs: list[tuple[str, str]] = []
    candidates: set[str] = set()

    for name in names:
        if name not in covered:
            continue
        title = manifest.get(name, {}).get("title") or name
        # `name` might be the requested_as of a redirected/normalized title;
        # find the actual cached title.
        cached_title = name if name in manifest else next(
            (row["title"] for row in manifest.values() if row["requested_as"] == name), name
        )
        path = os.path.join(OUT_DIR, safe_filename(cached_title))
        if not os.path.exists(path):
            continue
        wikitext = open(path, encoding="utf-8").read()
        if wiki_infobox.find_templates(wikitext):
            continue  # a real monster page, not a disambiguation page
        for candidate in wiki_infobox.disambiguation_candidates(wikitext):
            disambig_pairs.append((name, candidate))
            candidates.add(candidate)

    if not candidates:
        return

    print(f"{len(candidates)} disambiguation candidates across "
          f"{len({p[0] for p in disambig_pairs})} ambiguous names", file=sys.stderr)
    wanted = sorted(c for c in candidates if c not in manifest)
    if wanted:
        fetched, not_found = fetch_titles(wanted, manifest)
        print(f"fetched {fetched} disambiguation candidate pages", file=sys.stderr)
        if not_found:
            print(f"  {len(not_found)} candidate titles did not resolve: {not_found}", file=sys.stderr)

    os.makedirs(os.path.dirname(DISAMBIG_TSV), exist_ok=True)
    with open(DISAMBIG_TSV, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["roster_name", "candidate_title"])
        for name, candidate in disambig_pairs:
            w.writerow([name, candidate])
    print(f"wrote {len(disambig_pairs)} rows -> {DISAMBIG_TSV}", file=sys.stderr)


def fetch(refetch: bool) -> None:
    names = load_roster_names()
    manifest = load_manifest()
    wanted = names if refetch else [n for n in names if n not in manifest]
    print(f"{len(names)} distinct titles named by the roster; {len(wanted)} to fetch", file=sys.stderr)

    fetched, not_found = fetch_titles(wanted, manifest)
    write_manifest(manifest)
    print(f"fetched {fetched} pages -> {OUT_DIR}", file=sys.stderr)
    if not_found:
        print(f"no wiki page found for {len(not_found)} names:", file=sys.stderr)
        for n in not_found:
            print(f"  {n}", file=sys.stderr)

    resolve_disambiguations(manifest)
    write_manifest(manifest)


def report() -> None:
    names = load_roster_names()
    manifest = load_manifest()
    covered = set(manifest) | {row["requested_as"] for row in manifest.values()}
    have = sum(1 for n in names if n in covered)
    print(f"{have}/{len(names)} roster titles cached under {OUT_DIR}")
    missing = [n for n in names if n not in covered]
    if missing:
        print(f"{len(missing)} not yet fetched (run --fetch):")
        for n in missing[:40]:
            print(f"  {n}")
        if len(missing) > 40:
            print(f"  ... and {len(missing) - 40} more")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fetch", action="store_true", help="hit the network for anything not already cached")
    ap.add_argument("--refetch", action="store_true", help="re-fetch every roster title, ignoring the manifest")
    args = ap.parse_args()

    if args.fetch or args.refetch:
        fetch(refetch=args.refetch)
    else:
        report()


if __name__ == "__main__":
    main()
