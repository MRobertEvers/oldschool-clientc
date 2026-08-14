#!/usr/bin/env python3
"""
wiki_item_ids — resolve flagged shop_stock.csv rows via the wiki's own
`Infobox Item|id=`, instead of guessing from a shared display name.

    tools/gen_shop_catalog.py --write     # first; writes wiki/shop_stock.csv
    tools/wiki_item_ids.py --fetch        # pull each flagged item's own page
    tools/wiki_item_ids.py --write        # then; patches shop_stock.csv in place

## Why this exists

`gen_shop_catalog.py`'s obj resolver matches by *display name*, and the
cache reuses one display name across recolours — `Skirt` is `dwarf_skirt1`
through `dwarf_skirt4`, so `Skirt (blue)` and `Skirt (lilac)` both had to
guess (docs/SHOPS_PLAN.md §2.1's `review=variant`). The guess turns out to be
unnecessary: the OSRS Wiki's own `Skirt (blue)` page states
`{{Infobox Item|...|id=5052}}` — the exact cache id, stated by the source
that would otherwise only be guessed from. `id2=`/`id3=`/... cover an item
with several versions (a base cape and its trimmed sibling, say); the first
one is normally the version the page's own name refers to, and this only
ever uses `id` (or `id1`) — never a numbered variant — because there is no
way from the shop table alone to know which numbered version applies.

## What gets written

    OSRS-Content/osrs239-content/wiki/items/<title>.wikitext   raw wikitext
    OSRS-Content/osrs239-content/wiki/items_manifest.tsv       title, revid, id, fetch_date
    OSRS-Content/osrs239-content/wiki/shop_stock.csv            patched in place

A patched row gets `match_rule=wiki-infobox-id`, `review=` cleared, and
`obj_id`/`obj_gameval` set to the wiki's stated id — but **only if that id is
a real record in this cache's own obj table** (`configs/all.obj.compack`).
The OSRS Wiki describes the live game; this cache is frozen at rev 239, and
an id the wiki states for content added after that revision does not exist
here. Refusing rather than writing a dangling id is the same rule
`gen_shop_catalog.py` already follows for everything else.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import os
import re
import sys
import time
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
STOCK = os.path.join(CONTENT, "wiki", "shop_stock.csv")
CATALOG = os.path.join(CONTENT, "wiki", "shop_catalog.csv")
OUT_DIR = os.path.join(CONTENT, "wiki", "items")
MANIFEST = os.path.join(CONTENT, "wiki", "items_manifest.tsv")
OBJ_COMPACK = os.path.join(CONTENT, "configs", "all.obj.compack")

API = "https://oldschool.runescape.wiki/api.php"
UA = "3draster-shops/1.0 (mrobertevers@gmail.com; docs/SHOPS_PLAN.md)"
BATCH = 50

INFOBOX_RE = re.compile(r"\{\{\s*Infobox Item\b", re.IGNORECASE)
ID_RE = re.compile(r"\|\s*id\s*=\s*(\d+)", re.IGNORECASE)
VERSION_RE = re.compile(r"\|\s*version(\d+)\s*=\s*(.*)")
VERSIONED_ID_RE = re.compile(r"\|\s*id(\d+)\s*=\s*(\d+)")

# A multi-version item (`version1=`/`version2=`/... alongside `id1=`/`id2=`)
# names the *state* each id represents, and that vocabulary is genuinely
# heterogeneous across the corpus (docs/SHOPS_PLAN.md §8: "Active"/"Inactive"
# for charge state, "Reward"/"During event" for event exclusives, "Style 1"/
# "Style 2" for pure cosmetics — none of it says which id, if any, a shop
# would stock). Only pairs unambiguous enough that *any* reader would agree
# which side is the sellable one are whitelisted; everything else stays
# flagged rather than guessed. Case-insensitive, first version wins the pair.
SAFE_VERSION_PAIRS = {
    ("inventory", "worn"): 1,   # a shop holds it, it doesn't wear it
    ("fixed", "broken"): 1,     # nothing is sold pre-broken
    ("normal", "broken"): 1,
    ("closed", "open"): 1,      # bought empty/closed
}


def extract_item_id(content: str) -> str:
    """The wiki's own answer for which cache id this page's item is — a bare
    `id=`, or (§8) a whitelisted `version1=`/`version2=` pair's `id1=`."""
    m = INFOBOX_RE.search(content)
    if not m:
        return ""
    idm = ID_RE.search(content, m.start())
    if idm:
        return idm.group(1)
    # re.findall has no positional-start form; slice to the infobox instead.
    tail = content[m.start():]
    versions = {int(n): v.strip().lower() for n, v in VERSION_RE.findall(tail)}
    ids = {int(n): v for n, v in VERSIONED_ID_RE.findall(tail)}
    if versions.get(1) and versions.get(2):
        pair = (versions[1], versions[2])
        if pair in SAFE_VERSION_PAIRS:
            want = SAFE_VERSION_PAIRS[pair]
            if want in ids:
                return ids[want]
    return ""


def safe_filename(title: str) -> str:
    return re.sub(r"[^A-Za-z0-9._ -]", "_", title).strip() + ".wikitext"


def api(params: dict) -> dict:
    url = API + "?" + urllib.parse.urlencode({**params, "format": "json"})
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=60) as response:
        return __import__("json").load(response)


def flagged_names() -> list[str]:
    with open(STOCK, newline="", encoding="utf-8") as f:
        names = {row["wiki_item"] for row in csv.DictReader(f) if row["review"] and row["wiki_item"]}
    return sorted(names)


def load_manifest() -> dict[str, dict]:
    if not os.path.exists(MANIFEST):
        return {}
    with open(MANIFEST, newline="", encoding="utf-8") as f:
        return {row["title"]: row for row in csv.DictReader(f, delimiter="\t")}


def fetch(refetch: bool) -> None:
    names = flagged_names()
    manifest = load_manifest()
    wanted = names if refetch else [n for n in names if n not in manifest]
    print(f"{len(names)} distinct flagged item names; {len(wanted)} to fetch", file=sys.stderr)
    os.makedirs(OUT_DIR, exist_ok=True)
    today = datetime.date.today().isoformat()
    not_found: list[str] = []

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
        covered: set[str] = set()

        for page in query.get("pages", {}).values():
            if "missing" in page or not page.get("revisions"):
                continue
            title = page["title"]
            rev = page["revisions"][0]
            requested = redirect_from.get(title, normalized_from.get(title, title))
            covered.add(title)
            covered.add(requested)
            content = rev["slots"]["main"]["*"]
            with open(os.path.join(OUT_DIR, safe_filename(requested)), "w", encoding="utf-8") as f:
                f.write(content)
            item_id = extract_item_id(content)
            manifest[requested] = {
                "title": requested,
                "revid": str(rev["revid"]),
                "id": item_id,
                "fetch_date": today,
            }
        not_found += [n for n in batch if n not in covered]
        time.sleep(1)

    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["title", "revid", "id", "fetch_date"], delimiter="\t")
        w.writeheader()
        for title in sorted(manifest):
            w.writerow(manifest[title])
    with_id = sum(1 for r in manifest.values() if r["id"])
    print(f"cached {len(manifest)} item pages ({with_id} carry an Infobox Item id) -> {OUT_DIR}",
          file=sys.stderr)
    if not_found:
        print(f"{len(not_found)} names had no wiki page at all: {not_found[:20]}", file=sys.stderr)


def load_obj_ids() -> set[int]:
    ids: set[int] = set()
    with open(OBJ_COMPACK, encoding="utf-8") as f:
        for line in f:
            if "=" in line and not line.startswith("//"):
                num, _, _ = line.strip().partition("=")
                if num.isdigit():
                    ids.add(int(num))
    return ids


def load_gamevals() -> dict[int, str]:
    out: dict[int, str] = {}
    with open(OBJ_COMPACK, encoding="utf-8") as f:
        for line in f:
            if "=" in line and not line.startswith("//"):
                num, _, name = line.strip().partition("=")
                if num.isdigit():
                    out[int(num)] = name
    return out


def apply(write: bool) -> None:
    manifest = load_manifest()
    cache_ids = load_obj_ids()
    gameval = load_gamevals()

    with open(STOCK, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames
        rows = list(reader)

    patched = 0
    not_in_cache = 0
    no_wiki_id = 0
    for row in rows:
        if not row["review"] or not row["wiki_item"]:
            continue
        info = manifest.get(row["wiki_item"])
        if not info or not info["id"]:
            no_wiki_id += 1
            continue
        item_id = int(info["id"])
        if item_id not in cache_ids:
            not_in_cache += 1
            continue
        row["obj_id"] = str(item_id)
        row["obj_gameval"] = gameval.get(item_id, "")
        row["match_rule"] = "wiki-infobox-id"
        row["review"] = ""
        row["obj_alternatives"] = ""
        row["unresolved"] = ""
        patched += 1

    print(f"{patched} stock lines resolved via the item's own Infobox Item id")
    print(f"  {no_wiki_id} flagged names have no wiki page or no stated id")
    print(f"  {not_in_cache} wiki ids do not exist in this cache (post-rev-239 content)")

    if not write:
        print("\n(dry run — pass --write to update shop_stock.csv)")
        return
    with open(STOCK, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)
    print(f"\nwrote {STOCK}")
    reconcile_catalog(rows)


def reconcile_catalog(stock_rows: list[dict]) -> None:
    """`shop_catalog.csv`'s `lines_resolved`/`lines_needing_review` are a
    cached summary of `shop_stock.csv`, computed once by `gen_shop_catalog.py`.
    Patching stock here without updating that summary would leave the
    catalogue lying about a shop this pass just cleared — re-running
    `gen_shop_catalog.py` itself is not the fix, because that would re-derive
    `shop_stock.csv` from scratch by name-matching again and silently discard
    every id this pass just resolved by wiki authority instead. So this
    recomputes just the two summary columns, in place, from the stock rows
    already on disk — no other catalogue field changes."""
    import collections

    resolved: dict[str, int] = collections.Counter()
    needs_review: dict[str, int] = collections.Counter()
    for row in stock_rows:
        key = row["shop_key"]
        if row["obj_id"]:
            resolved[key] += 1
        if row["review"]:
            needs_review[key] += 1

    with open(CATALOG, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames
        catalog_rows = list(reader)

    changed = 0
    for row in catalog_rows:
        key = row["shop_key"]
        new_resolved = str(resolved.get(key, 0))
        new_review = str(needs_review.get(key, 0))
        if row["lines_resolved"] != new_resolved or row["lines_needing_review"] != new_review:
            row["lines_resolved"] = new_resolved
            row["lines_needing_review"] = new_review
            changed += 1

    with open(CATALOG, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(catalog_rows)
    print(f"reconciled {changed} shop_catalog.csv row(s) against the patched stock -> {CATALOG}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--fetch", action="store_true", help="hit the network for flagged item pages")
    ap.add_argument("--refetch", action="store_true", help="re-fetch every flagged item page")
    ap.add_argument("--write", action="store_true", help="patch shop_stock.csv (default: report only)")
    args = ap.parse_args()

    if args.fetch or args.refetch:
        fetch(refetch=args.refetch)
    apply(args.write)


if __name__ == "__main__":
    main()
