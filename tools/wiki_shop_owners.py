#!/usr/bin/env python3
"""
wiki_shop_owners — join every catalogued shop to the npc that opens it.

    tools/gen_shop_catalog.py --write     # first; writes wiki/shop_catalog.csv
    tools/wiki_shop_owners.py --fetch     # crawl the linked owner pages
    tools/wiki_shop_owners.py --write     # then; writes wiki/shop_owners.csv

A shop is only reachable if something in the world opens it, and in this engine
that is an `[opnpc<n>,<gameval>]` trigger. So the load-bearing column is not the
shop's name — it is the **npc gameval**, and getting there needs an npc *id*,
because npc display names are not unique (`Shop keeper` names 14 records).

Two sources for the id, and they cover different halves of the catalogue:

  * The shop page **is** the npc page (`Duradel`, `Thurgo`, `Dunstan`) and
    states `{{Infobox NPC|id=}}` outright — 644 of 1,229 tables. Already in
    `shop_catalog.csv:page_npc_ids`; no crawl needed.
  * The shop has its own page and names its owner as a wikilink — 435 distinct
    owner pages. Those are what `--fetch` pulls.

## What gets written

    OSRS-Content/osrs239-content/wiki/owners/<title>.wikitext   raw wikitext
    OSRS-Content/osrs239-content/wiki/shop_owners.csv           the join

One row per (shop, owner npc id): a shop with a keeper *and* an assistant is
two rows, because both need the trigger. `spawned` is the reachability column —
whether that gameval appears in a `*.spawn` file under server/scripts, the same
test `wiki_npc_roster.py` uses. A shop whose owner is not spawned can still be
authored; it just cannot be walked up to and tested yet, and it should not be
counted as done.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import glob
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
CATALOG = os.path.join(CONTENT, "wiki", "shop_catalog.csv")
OUT_DIR = os.path.join(CONTENT, "wiki", "owners")
MANIFEST = os.path.join(CONTENT, "wiki", "owners_manifest.tsv")
OWNERS_CSV = os.path.join(CONTENT, "wiki", "shop_owners.csv")
NPC_COMPACK = os.path.join(CONTENT, "configs", "all.npc.compack")
SPAWN_GLOB = os.path.join(CONTENT, "server", "scripts", "**", "*.spawn")

API = "https://oldschool.runescape.wiki/api.php"
UA = "3draster-shops/1.0 (mrobertevers@gmail.com; docs/SHOPS_PLAN.md)"
BATCH = 50

FIELDS = [
    "shop_key",
    "shop_name",
    "owner_name",
    "owner_page",
    "npc_id",
    "npc_gameval",
    "spawned",
    "source",
]


def safe_filename(title: str) -> str:
    return re.sub(r"[^A-Za-z0-9._ -]", "_", title).strip() + ".wikitext"


def api(params: dict) -> dict:
    url = API + "?" + urllib.parse.urlencode({**params, "format": "json"})
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def owner_pages() -> list[str]:
    pages: set[str] = set()
    with open(CATALOG, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            for title in row["owner_page"].split(" | "):
                title = title.strip()
                if title:
                    pages.add(title)
    return sorted(pages)


def load_manifest() -> dict[str, dict]:
    if not os.path.exists(MANIFEST):
        return {}
    with open(MANIFEST, newline="", encoding="utf-8") as f:
        return {row["title"]: row for row in csv.DictReader(f, delimiter="\t")}


def fetch(refetch: bool) -> None:
    manifest = load_manifest()
    wanted = owner_pages() if refetch else [p for p in owner_pages() if p not in manifest]
    print(f"{len(owner_pages())} owner pages linked; {len(wanted)} to fetch", file=sys.stderr)
    os.makedirs(OUT_DIR, exist_ok=True)
    today = datetime.date.today().isoformat()
    missing: list[str] = []

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
            with open(os.path.join(OUT_DIR, safe_filename(requested)), "w", encoding="utf-8") as f:
                f.write(rev["slots"]["main"]["*"])
            manifest[requested] = {
                "title": requested,
                "resolved_title": title,
                "revid": str(rev["revid"]),
                "fetch_date": today,
            }
        missing += [n for n in batch if n not in covered]
        time.sleep(1)

    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(
            f, fieldnames=["title", "resolved_title", "revid", "fetch_date"], delimiter="\t"
        )
        w.writeheader()
        for title in sorted(manifest):
            w.writerow(manifest[title])
    print(f"cached {len(manifest)} owner pages -> {OUT_DIR}", file=sys.stderr)
    if missing:
        print(f"{len(missing)} owner titles did not resolve: {missing[:20]}", file=sys.stderr)


# ------------------------------------------------------------------
# The join
# ------------------------------------------------------------------


def npc_ids_on_page(text: str) -> list[str]:
    """Every id an `Infobox NPC` on the page claims, in page order."""
    ids: list[str] = []
    for match in re.finditer(r"\|\s*(id\d*)\s*=\s*([^\n|}]*)", text):
        for part in match.group(2).split(","):
            part = part.strip()
            if part.isdigit():
                ids.append(part)
    return list(dict.fromkeys(ids))


def load_gamevals() -> dict[str, str]:
    out: dict[str, str] = {}
    with open(NPC_COMPACK, encoding="utf-8") as f:
        for line in f:
            if line.startswith("//") or "=" not in line:
                continue
            num, _, name = line.strip().partition("=")
            if num.isdigit():
                out[num] = name
    return out


def load_spawned() -> set[str]:
    spawned: set[str] = set()
    for path in glob.glob(SPAWN_GLOB, recursive=True):
        in_npc = False
        for line in open(path, encoding="utf-8"):
            stripped = line.strip()
            if stripped.startswith("===="):
                in_npc = "NPC" in stripped
                continue
            if in_npc and stripped and not stripped.startswith("//"):
                spawned.add(stripped.split()[0])
    return spawned


def build(write: bool) -> None:
    gamevals = load_gamevals()
    spawned = load_spawned()
    manifest = load_manifest()
    rows: list[dict] = []

    with open(CATALOG, newline="", encoding="utf-8") as f:
        catalog = list(csv.DictReader(f))

    for shop in catalog:
        seen: set[str] = set()

        # The npc page hosting its own store is the unambiguous case: the id is
        # stated on the very page the stock table lives on.
        for npc_id in shop["page_npc_ids"].split():
            if npc_id not in seen:
                seen.add(npc_id)
                rows.append(_row(shop, shop["page_title"], shop["page_title"], npc_id,
                                 gamevals, spawned, "shop-page"))

        for title in [t.strip() for t in shop["owner_page"].split(" | ") if t.strip()]:
            path = os.path.join(OUT_DIR, safe_filename(title))
            if not os.path.exists(path):
                rows.append(_row(shop, title, title, "", gamevals, spawned, "owner-link-unfetched"))
                continue
            ids = npc_ids_on_page(open(path, encoding="utf-8").read())
            if not ids:
                rows.append(_row(shop, title, title, "", gamevals, spawned, "owner-link-no-id"))
            for npc_id in ids:
                if npc_id not in seen:
                    seen.add(npc_id)
                    rows.append(_row(shop, title, title, npc_id, gamevals, spawned, "owner-link"))

    with_id = [r for r in rows if r["npc_id"]]
    with_gv = [r for r in with_id if r["npc_gameval"]]
    live = [r for r in with_gv if r["spawned"] == "yes"]
    shops_live = {r["shop_key"] for r in live}

    print(f"{len(rows)} shop/owner rows from {len(catalog)} tables")
    print(f"  {len(with_id)} carry an npc id, {len(with_gv)} of those resolve to a gameval")
    print(f"  {len(live)} of those gamevals are spawned in this world")
    print(f"  {len(shops_live)}/{len(catalog)} tables have at least one reachable owner")

    if not write:
        print("\n(dry run — pass --write to update the CSV)")
        return
    with open(OWNERS_CSV, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    print(f"\nwrote {OWNERS_CSV}")


def _row(shop, owner_name, owner_page, npc_id, gamevals, spawned, source) -> dict:
    gameval = gamevals.get(npc_id, "") if npc_id else ""
    return {
        "shop_key": shop["shop_key"],
        "shop_name": shop["shop_name"],
        "owner_name": owner_name,
        "owner_page": owner_page,
        "npc_id": npc_id,
        "npc_gameval": gameval,
        "spawned": "yes" if gameval and gameval in spawned else "no",
        "source": source,
    }


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--fetch", action="store_true", help="crawl linked owner pages not yet cached")
    ap.add_argument("--refetch", action="store_true", help="re-crawl every linked owner page")
    ap.add_argument("--write", action="store_true", help="update shop_owners.csv")
    args = ap.parse_args()

    if args.fetch or args.refetch:
        fetch(refetch=args.refetch)
    build(args.write)


if __name__ == "__main__":
    main()
