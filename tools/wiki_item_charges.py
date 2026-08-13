#!/usr/bin/env python3
"""
wiki_item_charges — pull OSRS Wiki item pages for the charged-items ledger.

    tools/charged_items_scan.py                  # build the ledger first
    tools/wiki_item_charges.py --fetch            # pull, cache to disk
    tools/wiki_item_charges.py                    # report what's cached, no network

See docs/ITEM_CHARGES_PLAN.md §4b. Same shape and etiquette as
tools/wiki_fetch.py (which does this for npc pages): batched
`action=query&titles=` (50/request, `redirects=1`), one request per second, a
descriptive User-Agent with a contact address, no parallelism, and a
manifest that makes a second run a network no-op unless `--refetch` is
passed.

## What gets written

    OSRS-Content/osrs239-content/wiki/items/<title>.wikitext   raw wikitext
    OSRS-Content/osrs239-content/wiki/items_manifest.tsv       title, revid, requested_as, fetch_date

Titles come from `charged_items.csv`'s own `family` column, restricted (by
default) to rows whose `status` is `implemented` or `charges_only` — the
families this pass actually needs wiki numbers for, not the full 200+ raw
scan. Pass `--all` to fetch every family regardless of status.
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

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
LEDGER = os.path.join(CONTENT, "wiki", "charged_items.csv")
OUT_DIR = os.path.join(CONTENT, "wiki", "items")
MANIFEST = os.path.join(CONTENT, "wiki", "items_manifest.tsv")

API = "https://oldschool.runescape.wiki/api.php"
UA = "3draster-item-charges/1.0 (mrobertevers@gmail.com; docs/ITEM_CHARGES_PLAN.md)"
BATCH = 50

# A ledger family name that is not the wiki's own page title — mostly the
# scan's own stemming losing a word the wiki keeps. Extend rather than guess:
# a wrong title silently binds the wrong item's numbers to a whole family.
TITLE_OVERRIDE = {
    "Toxic staff": "Toxic staff of the dead",
}


def safe_filename(title: str) -> str:
    return re.sub(r"[^A-Za-z0-9._ -]", "_", title).strip() + ".wikitext"


def api(params: dict) -> dict:
    url = API + "?" + urllib.parse.urlencode({**params, "format": "json"})
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=60) as response:
        import json

        return json.load(response)


def load_ledger_titles(want_all: bool) -> list[str]:
    with open(LEDGER, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    names = set()
    for row in rows:
        if not want_all and row["status"] not in ("implemented", "charges_only"):
            continue
        names.add(row["family"].strip())
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

        if i + BATCH < len(wanted):
            time.sleep(1)

    return fetched, not_found


def fetch(refetch: bool, want_all: bool) -> None:
    names = load_ledger_titles(want_all)
    manifest = load_manifest()
    wanted = names if refetch else [n for n in names if n not in manifest]
    print(f"{len(names)} distinct titles named by the ledger; {len(wanted)} to fetch", file=sys.stderr)
    if not wanted:
        print("nothing to fetch (pass --refetch to force)", file=sys.stderr)
        return

    fetched, not_found = fetch_titles(wanted, manifest)
    write_manifest(manifest)
    print(f"fetched {fetched} pages -> {OUT_DIR}", file=sys.stderr)
    if not_found:
        print(f"  {len(not_found)} titles did not resolve: {not_found}", file=sys.stderr)
        print("  add a TITLE_OVERRIDE entry, or fix the ledger family name", file=sys.stderr)


def report() -> None:
    names = load_ledger_titles(False)
    manifest = load_manifest()
    have = sum(1 for n in names if n in manifest)
    print(f"{have}/{len(names)} implemented/charges_only titles cached", file=sys.stderr)
    missing = [n for n in names if n not in manifest]
    if missing:
        print(f"missing: {missing}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetch", action="store_true", help="pull from the wiki")
    ap.add_argument("--refetch", action="store_true", help="re-pull every title, ignoring the manifest")
    ap.add_argument("--all", action="store_true", help="fetch every ledger family, not just implemented/charges_only")
    args = ap.parse_args()

    if args.fetch or args.refetch:
        try:
            fetch(args.refetch, args.all)
        except urllib.error.URLError as exc:
            print(f"network error: {exc}", file=sys.stderr)
            return 1
    else:
        report()
    return 0


if __name__ == "__main__":
    sys.exit(main())
