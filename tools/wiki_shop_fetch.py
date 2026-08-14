#!/usr/bin/env python3
"""
wiki_shop_fetch — pull every OSRS Wiki page that carries a shop stock table.

    tools/wiki_shop_fetch.py --fetch      # discover + pull, cache to disk
    tools/wiki_shop_fetch.py              # report what's cached, no network

Sibling of `wiki_fetch.py`, and deliberately the same shape: a discovery pass
that names the pages, a batched `action=query&titles=` fetch that writes raw
wikitext one file per page, and a manifest recording title/revid/fetch date so
a re-crawl is a reviewable diff rather than an invisible refresh.

## What "a shop" is, on the wiki

Two templates, and the union of their transclusions is the roster:

  * `Template:Infobox Shop` — a page whose *subject* is a shop. ~454 pages.
  * `Template:StoreTableHead` — a page carrying a stock table. ~1056 pages.
    Strictly larger, because plenty of shops are not their own page: an npc
    (`Duradel`, `Thurgo`), a minigame reward counter, or a quest hub hosts the
    table inline.

Neither alone is the answer. `Infobox Shop` without `StoreTableHead` is a shop
with no stock listed (6 pages); `StoreTableHead` without `Infobox Shop` is an
npc- or area-hosted store (608 pages). Both are shops this server has to open.

`Category:Shops` (506) is *not* used as the roster: it is hand-maintained, it
misses npc-hosted stores, and it includes disambiguation and overview pages
that carry no table.

## What gets written

    OSRS-Content/osrs239-content/wiki/shops/<title>.wikitext   raw wikitext
    OSRS-Content/osrs239-content/wiki/shops_manifest.tsv       title, revid,
                                                               infobox, table,
                                                               fetch_date

`infobox`/`table` are the two discovery flags, recorded so a later pass can
tell "shop page with no stock table" from "stock table on some other page"
without re-parsing every file.

Etiquette matches `wiki_fetch.py`: one request per second, descriptive
User-Agent with a contact address, no parallelism. ~1060 titles / 50 per batch
≈ 22 requests plus discovery.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
OUT_DIR = os.path.join(CONTENT, "wiki", "shops")
MANIFEST = os.path.join(CONTENT, "wiki", "shops_manifest.tsv")

API = "https://oldschool.runescape.wiki/api.php"
UA = "3draster-shops/1.0 (mrobertevers@gmail.com; docs/SHOPS_PLAN.md)"
BATCH = 50

INFOBOX_TEMPLATE = "Template:Infobox Shop"
TABLE_TEMPLATE = "Template:StoreTableHead"

FIELDS = ["title", "revid", "infobox", "table", "fetch_date"]


def safe_filename(title: str) -> str:
    return re.sub(r"[^A-Za-z0-9._ -]", "_", title).strip() + ".wikitext"


def api(params: dict) -> dict:
    url = API + "?" + urllib.parse.urlencode({**params, "format": "json"})
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def embedded_in(template: str) -> list[str]:
    """Every mainspace page transcluding `template`, following continuations."""
    out: list[str] = []
    cont: dict = {}
    while True:
        data = api(
            {
                "action": "query",
                "list": "embeddedin",
                "eititle": template,
                "einamespace": 0,
                "eilimit": "max",
                **cont,
            }
        )
        out += [row["title"] for row in data["query"]["embeddedin"]]
        if "continue" not in data:
            return out
        cont = data["continue"]
        time.sleep(1)


def load_manifest() -> dict[str, dict]:
    if not os.path.exists(MANIFEST):
        return {}
    with open(MANIFEST, newline="", encoding="utf-8") as f:
        return {row["title"]: row for row in csv.DictReader(f, delimiter="\t")}


def write_manifest(rows: dict[str, dict]) -> None:
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, delimiter="\t")
        w.writeheader()
        for title in sorted(rows):
            w.writerow(rows[title])


def fetch_titles(
    wanted: list[str],
    manifest: dict[str, dict],
    flags: dict[str, dict],
) -> tuple[int, list[str]]:
    """Fetches `wanted` into `manifest` and `OUT_DIR` in place. Returns
    (pages written, titles nothing came back for)."""
    os.makedirs(OUT_DIR, exist_ok=True)
    not_found: list[str] = []
    fetched = 0
    today = datetime.date.today().isoformat()

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
            if "missing" in page:
                continue
            title = page["title"]
            revs = page.get("revisions")
            if not revs:
                continue
            content = revs[0]["slots"]["main"]["*"]
            requested = redirect_from.get(title, normalized_from.get(title, title))
            covered.add(title)
            covered.add(requested)
            path = os.path.join(OUT_DIR, safe_filename(title))
            with open(path, "w", encoding="utf-8") as f:
                f.write(content)
            flag = flags.get(title) or flags.get(requested) or {}
            manifest[title] = {
                "title": title,
                "revid": str(revs[0]["revid"]),
                "infobox": "yes" if flag.get("infobox") else "no",
                "table": "yes" if flag.get("table") else "no",
                "fetch_date": today,
            }
            fetched += 1

        for name in batch:
            if name not in covered:
                not_found.append(name)

        time.sleep(1)

    return fetched, not_found


def discover() -> dict[str, dict]:
    """title -> {'infobox': bool, 'table': bool} for every candidate page."""
    infobox = embedded_in(INFOBOX_TEMPLATE)
    time.sleep(1)
    table = embedded_in(TABLE_TEMPLATE)
    print(
        f"{len(infobox)} pages transclude {INFOBOX_TEMPLATE}; "
        f"{len(table)} transclude {TABLE_TEMPLATE}",
        file=sys.stderr,
    )
    flags: dict[str, dict] = {}
    for t in infobox:
        flags.setdefault(t, {"infobox": False, "table": False})["infobox"] = True
    for t in table:
        flags.setdefault(t, {"infobox": False, "table": False})["table"] = True
    print(f"{len(flags)} distinct shop-bearing pages", file=sys.stderr)
    return flags


def fetch(refetch: bool) -> None:
    flags = discover()
    manifest = load_manifest()
    wanted = sorted(flags) if refetch else sorted(t for t in flags if t not in manifest)
    print(f"{len(wanted)} to fetch", file=sys.stderr)

    fetched, not_found = fetch_titles(wanted, manifest, flags)
    write_manifest(manifest)
    print(f"fetched {fetched} pages -> {OUT_DIR}", file=sys.stderr)
    if not_found:
        print(f"no page came back for {len(not_found)} titles:", file=sys.stderr)
        for n in not_found[:40]:
            print(f"  {n}", file=sys.stderr)


def report() -> None:
    manifest = load_manifest()
    if not manifest:
        print(f"nothing cached under {OUT_DIR} (run --fetch)")
        return
    have = sum(1 for t in manifest if os.path.exists(os.path.join(OUT_DIR, safe_filename(t))))
    infobox = sum(1 for r in manifest.values() if r["infobox"] == "yes")
    table = sum(1 for r in manifest.values() if r["table"] == "yes")
    dates = sorted({r["fetch_date"] for r in manifest.values()})
    print(f"{have}/{len(manifest)} manifest titles present under {OUT_DIR}")
    print(f"  {infobox} carry Infobox Shop, {table} carry a stock table")
    print(f"  fetch dates: {', '.join(dates)}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--fetch", action="store_true", help="hit the network for anything not cached")
    ap.add_argument("--refetch", action="store_true", help="re-fetch every discovered title")
    args = ap.parse_args()

    if args.fetch or args.refetch:
        fetch(refetch=args.refetch)
    else:
        report()


if __name__ == "__main__":
    main()
