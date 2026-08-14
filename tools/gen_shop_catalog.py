#!/usr/bin/env python3
"""
gen_shop_catalog — turn the cached shop wikitext into two reviewable tables.

    tools/wiki_shop_fetch.py --fetch      # first; writes wiki/shops/*.wikitext
    tools/gen_shop_catalog.py --write     # then; writes the two CSVs below

## What it writes

    OSRS-Content/osrs239-content/wiki/shop_catalog.csv   one row per stock table
    OSRS-Content/osrs239-content/wiki/shop_stock.csv     one row per stocked obj

`shop_catalog.csv` is the *behaviour*: the three price multipliers, the owner,
the location, whether it is members-only, what currency it trades in. Every
row carries `page_title` / `revid` / `fetch_date` so a generated `.rs2` header
can cite the exact revision it was written from, and so a re-crawl that changes
a price is a diff on this file rather than a silent drift.

`shop_stock.csv` is the *stock*: `(shop_key, slot, obj, stock, restock)` — the
same four fields LostCity's `.inv` grammar spells `stockN=obj,count,rate`, in
the same units (see docs/SHOPS_PLAN.md §3 for the derivation).

## The joins, and where each is allowed to fail

**Item name -> obj id** is by display name against `dump_stats --obj-csv`,
after dropping noted and placeholder records (they duplicate every name they
alias). Three rules, in order, and every row records which one fired:

  `exact`            the StoreLine's `name=` is the cache display name.
  `displayname`      `name=` is a wiki page title and `displayname=` is the
                     in-game name. 84 lines.
  `strip-qualifier`  `name=` is a *disambiguated page title* — `Shantay pass
                     (item)`, `Skirt (blue)` — and dropping the parenthetical
                     matches. 396 lines, and **always provisional**: the cache
                     gives every colour of a garment the same display name
                     (`Skirt` is `dwarf_skirt1..4`), so `Skirt (blue)` and
                     `Skirt (lilac)` both land on the lowest id and one of them
                     is wrong. These are flagged `review=variant` and must not
                     reach a `.inv` unlooked-at.

1,579 display names resolve to more than one real record even on an exact
match. Most of those are the cache's own "second copy that can't be sold or
lost" pattern — a members quest/minigame duplicate of a common item, same
name, `tradeable=false` (`Bucket` is 1925 tradeable alongside 8986/9660,
neither tradeable; spot-checked across the 20 most-repeated ambiguous names).
`load_obj_index` sorts each name's candidates tradeable-first, so a tie where
the chosen id *is* tradeable resolves without a flag — the alternatives are
still recorded in `obj_alternatives` for the audit trail, just not blocking.
Only a tie where *no* candidate is tradeable keeps `review=ambiguous`:
genuinely two marketable items sharing a name, which the tradeable signal
cannot break. Nothing is silently picked either way: a flagged row is a row a
human has to look at before it becomes stock.

**Owner name -> npc id** is deliberately *not* guessed here. Half the shop
pages are npc pages and state `|id=` outright; the rest name their owner as a
wikilink to a page this pass has not fetched. Both cases are recorded raw
(`owner_raw`, `owner_page`, `page_npc_ids`) and the id join is
`wiki_shop_owners.py`'s job, because it needs a second crawl.

**Shop -> cache inv name** is not attempted at all. The osrs239 cache already
names 1,026 inventories, ~475 of them shops (`configs/all.inv.compack`), and
which one backs which wiki shop is a naming judgement, not a computation —
`generalshop1` is Lumbridge only because someone checked. That binding lives in
a hand-reviewed table; this tool only emits the two sides of it.
"""

from __future__ import annotations

import argparse
import csv
import collections
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
SHOPS_DIR = os.path.join(CONTENT, "wiki", "shops")
MANIFEST = os.path.join(CONTENT, "wiki", "shops_manifest.tsv")
CATALOG = os.path.join(CONTENT, "wiki", "shop_catalog.csv")
STOCK = os.path.join(CONTENT, "wiki", "shop_stock.csv")
OBJ_COMPACK = os.path.join(CONTENT, "configs", "all.obj.compack")
CACHE = os.path.join(REPO, "cache.osrs239")
DUMP_STATS = os.path.join(REPO, "tools", "dump_stats", "dump_stats")
DEFAULT_OBJ_CSV = os.path.join(REPO, "obj_stats.csv")

CATALOG_FIELDS = [
    "shop_key",
    "page_title",
    "revid",
    "fetch_date",
    "table_index",
    "section",
    "shop_name",
    "is_shop_page",
    "duplicate_of",
    "owner_raw",
    "owner_page",
    "page_npc_ids",
    "location",
    "members",
    "special",
    "currency",
    "sellmultiplier",
    "buymultiplier",
    "delta",
    "lines",
    "lines_resolved",
    "lines_needing_review",
]

STOCK_FIELDS = [
    "shop_key",
    "slot",
    "wiki_item",
    "obj_id",
    "obj_gameval",
    "stock",
    "restock",
    "buy_override",
    "sell_override",
    "match_rule",
    "review",
    "obj_alternatives",
    "unresolved",
]


# ------------------------------------------------------------------
# Wikitext templates
# ------------------------------------------------------------------
#
# Brace-depth scanning, not a regex over the page: a StoreLine value routinely
# nests another template (`{{GEP|Coins}}`, `{{plink|...}}`) and a naive split on
# `|` slices those in half. wiki_infobox.py makes the same argument for
# Infobox Monster and this is the same parser shape for three other templates.


def find_templates(text: str, name: str) -> list[str]:
    """Bodies of every `{{name|...}}` in `text`, sans the name and outer braces."""
    out: list[str] = []
    pattern = re.compile(r"\{\{\s*" + re.escape(name) + r"\s*(?=[|}])", re.IGNORECASE)
    for match in pattern.finditer(text):
        depth = 0
        i = match.start()
        n = len(text)
        while i < n - 1:
            if text[i : i + 2] == "{{":
                depth += 1
                i += 2
            elif text[i : i + 2] == "}}":
                depth -= 1
                i += 2
                if depth == 0:
                    break
            else:
                i += 1
        out.append(text[match.end() : i - 2])
    return out


def split_params(body: str) -> dict[str, str]:
    """`|k=v|k=v` -> dict, ignoring `|` inside nested templates or links."""
    params: dict[str, str] = {}
    depth = 0
    field = ""
    for i, ch in enumerate(body):
        two = body[i : i + 2]
        if two in ("{{", "[["):
            depth += 1
        elif two in ("}}", "]]"):
            depth -= 1
        if ch == "|" and depth <= 0:
            if "=" in field:
                k, v = field.split("=", 1)
                params[k.strip().lower()] = v.strip()
            field = ""
        else:
            field += ch
    if "=" in field:
        k, v = field.split("=", 1)
        params[k.strip().lower()] = v.strip()
    return params


def strip_markup(value: str) -> str:
    """`[[Shop keeper (Lumbridge)|Shop keeper]]` -> `Shop keeper`."""
    value = re.sub(r"\[\[([^|\]]*)\|([^\]]*)\]\]", r"\2", value)
    value = re.sub(r"\[\[([^\]]*)\]\]", r"\1", value)
    value = re.sub(r"\{\{[^}]*\}\}", "", value)
    value = re.sub(r"<[^>]+>", "", value)
    return re.sub(r"\s+", " ", value).strip()


def resolve_obj(
    wiki_name: str,
    display_name: str,
    by_name: dict[str, list[int]],
) -> tuple[list[int], str]:
    """(candidate obj ids, which rule matched). Empty list means unresolved."""
    if not wiki_name:
        return [], ""
    ids = by_name.get(wiki_name.lower())
    if ids:
        return ids, "exact"
    if display_name:
        ids = by_name.get(display_name.lower())
        if ids:
            return ids, "displayname"
    stripped = re.match(r"^(.*?) \((.+)\)$", wiki_name)
    if stripped:
        ids = by_name.get(stripped.group(1).lower())
        if ids:
            return ids, "strip-qualifier"
    return [], ""


def link_targets(value: str) -> list[str]:
    return [m.group(1).strip() for m in re.finditer(r"\[\[([^|\]]+)", value)]


def slug(text: str) -> str:
    text = text.lower().replace("'", "")
    text = re.sub(r"[^a-z0-9]+", "_", text)
    return text.strip("_")


# ------------------------------------------------------------------
# The obj join
# ------------------------------------------------------------------


def build_obj_csv(dest: str) -> None:
    if not os.path.exists(DUMP_STATS):
        subprocess.run(["make", "-C", os.path.dirname(DUMP_STATS)], check=True)
    subprocess.run(
        [DUMP_STATS, "--rev", "osrs239", CACHE, "--obj-only", "--obj-csv", dest], check=True
    )


def load_obj_index(
    obj_csv: str,
) -> tuple[dict[str, list[int]], dict[int, str], dict[int, bool]]:
    """(lowercased display name -> real obj ids [tradeable first, then by id],
    obj id -> gameval name, obj id -> tradeable).

    A name shared by several real records is almost always one marketable
    item plus a members-only quest/minigame duplicate the cache keeps so the
    original can't be sold or lost — `Bucket` is 1925 (tradeable) alongside
    8986/9660 (both `tradeable=false`), and the pattern repeats across the
    corpus (`Tinderbox`, `Chisel`, `Rope`, `Watering can`, ... — spot-checked
    across the 20 most-repeated ambiguous names in `wiki/shop_stock.csv`,
    docs/SHOPS_PLAN.md §2.1). Putting a tradeable id first means the resolver
    picks the item a shop can actually sell, not whichever id happens lowest —
    those usually agree, but not always (`watering can`'s tradeable id is not
    its lowest).
    """
    if not os.path.exists(obj_csv):
        build_obj_csv(obj_csv)
    by_name: dict[str, list[int]] = collections.defaultdict(list)
    tradeable: dict[int, bool] = {}
    with open(obj_csv, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            # A noted or placeholder record carries the same display name as the
            # thing it aliases; keeping them would make every stackable item
            # ambiguous with itself.
            if row["noted_template"] != "-1" or row["placeholder_template"] != "-1":
                continue
            name = row["name"].strip()
            if not name or name.lower() == "null":
                continue
            obj_id = int(row["id"])
            by_name[name.lower()].append(obj_id)
            tradeable[obj_id] = row["tradeable"].strip().lower() == "true"
    for ids in by_name.values():
        ids.sort(key=lambda i: (not tradeable.get(i, False), i))

    gameval: dict[int, str] = {}
    with open(OBJ_COMPACK, encoding="utf-8") as f:
        for line in f:
            if "=" in line and not line.startswith("//"):
                num, _, name = line.strip().partition("=")
                if num.isdigit():
                    gameval[int(num)] = name
    return by_name, gameval, tradeable


# ------------------------------------------------------------------
# The pass
# ------------------------------------------------------------------


def load_manifest() -> dict[str, dict]:
    with open(MANIFEST, newline="", encoding="utf-8") as f:
        return {row["title"]: row for row in csv.DictReader(f, delimiter="\t")}


def safe_filename(title: str) -> str:
    return re.sub(r"[^A-Za-z0-9._ -]", "_", title).strip() + ".wikitext"


def parse_page(
    title: str,
    meta: dict,
    text: str,
    by_name: dict[str, list[int]],
    gameval: dict[int, str],
    tradeable: dict[int, bool],
) -> tuple[list[dict], list[dict]]:
    infoboxes = [split_params(b) for b in find_templates(text, "Infobox Shop")]
    info = infoboxes[0] if infoboxes else {}

    # An npc page hosting a store states its own npc ids; a dedicated shop page
    # names its owner instead. Record whichever exists — the id join needs both.
    npc_ids: list[str] = []
    for npc in find_templates(text, "Infobox NPC"):
        p = split_params(npc)
        for key, value in p.items():
            if re.fullmatch(r"id\d*", key):
                npc_ids += [v.strip() for v in value.split(",") if v.strip().isdigit()]

    heads = find_templates(text, "StoreTableHead")
    # StoreLines belong to the head above them, so both are located by offset
    # rather than collected independently: a page with two tables (a members and
    # a free version, or two counters on one npc) must not merge their stock.
    head_spans = [m.start() for m in re.finditer(r"\{\{\s*StoreTableHead", text, re.IGNORECASE)]
    line_spans = [
        (m.start(), m) for m in re.finditer(r"\{\{\s*StoreLine\s*(?=[|}])", text, re.IGNORECASE)
    ]
    line_bodies = find_templates(text, "StoreLine")

    catalog_rows: list[dict] = []
    stock_rows: list[dict] = []
    base_key = slug(title)

    for index, head_body in enumerate(heads):
        head = split_params(head_body)
        start = head_spans[index]
        end = head_spans[index + 1] if index + 1 < len(head_spans) else len(text)
        mine = [body for (pos, _), body in zip(line_spans, line_bodies) if start < pos < end]

        shop_key = base_key if len(heads) == 1 else f"{base_key}__{index + 1}"
        resolved = 0
        needs_review = 0
        # A page's second and third tables are not second shops — they are the
        # same storefront's conditional stock, and the section heading above
        # each one is the condition. Aaron's Archery Appendages has "Ranging
        # cape" and "Ranging cape(t)" sub-tables, and the cache names them
        # `ranging_guild_armourshop_skillcape{,_trimmed}` — one inv per table.
        headings = re.findall(r"^=+ *(.+?) *=+\s*$", text[:start], re.MULTILINE)
        section = headings[-1] if headings and index > 0 else ""

        for slot, body in enumerate(mine, start=1):
            line = split_params(body)
            wiki_item = strip_markup(line.get("name", ""))
            display_name = strip_markup(line.get("displayname", ""))
            ids, rule = resolve_obj(wiki_item, display_name, by_name)

            # A stripped qualifier is never authoritative: the cache reuses one
            # display name across a garment's colours, so the pick is a guess
            # even when exactly one id carries the name.
            #
            # A same-name tie is not, on its own, still a guess: `by_name`
            # sorts tradeable ids first (load_obj_index), so when the chosen
            # id is tradeable the untradeable alternatives are the common
            # "quest/minigame duplicate that can't be sold" pattern
            # (docs/SHOPS_PLAN.md §2.1) and the pick stands without a human
            # look. Only when *no* candidate is tradeable — genuinely two
            # marketable items sharing one name — does the tie stay flagged.
            if rule == "strip-qualifier":
                review = "variant"
            elif len(ids) > 1 and not tradeable.get(ids[0], False):
                review = "ambiguous"
            else:
                review = ""

            stock_rows.append(
                {
                    "shop_key": shop_key,
                    "slot": slot,
                    "wiki_item": wiki_item,
                    "obj_id": ids[0] if ids else "",
                    "obj_gameval": gameval.get(ids[0], "") if ids else "",
                    "stock": line.get("stock", ""),
                    "restock": line.get("restock", ""),
                    "buy_override": line.get("buy", ""),
                    "sell_override": line.get("sell", ""),
                    "match_rule": rule,
                    "review": review,
                    "obj_alternatives": " ".join(str(i) for i in ids[1:]),
                    "unresolved": "" if ids else ("no-name" if not wiki_item else "no-obj-match"),
                }
            )
            if ids:
                resolved += 1
            if review:
                needs_review += 1

        owner_raw = info.get("owner", "")
        catalog_rows.append(
            {
                "shop_key": shop_key,
                "page_title": title,
                "revid": meta["revid"],
                "fetch_date": meta["fetch_date"],
                "table_index": index + 1,
                "section": strip_markup(section),
                "shop_name": strip_markup(info.get("name", "")) or title,
                "is_shop_page": "yes" if infoboxes else "no",
                "duplicate_of": "",
                "owner_raw": strip_markup(owner_raw),
                "owner_page": " | ".join(link_targets(owner_raw)),
                "page_npc_ids": " ".join(dict.fromkeys(npc_ids)),
                "location": strip_markup(info.get("location", "")),
                "members": strip_markup(info.get("members", "")),
                "special": strip_markup(info.get("special", "")),
                "currency": strip_markup(head.get("currency", "")),
                # Mirror image of the delta default below: a page with
                # `buymultiplier=`/`delta=` but no `sellmultiplier=` is a
                # buy-only reward shop (Initiate/Proselyte Temple Knight
                # Armoury — 2 confirmed, both quest-reward armour stalls) —
                # the wiki simply never states a sell rate because the shop's
                # own item set isn't meant to be sold back at a markup.
                # Defaulting sellmultiplier to match buymultiplier (buy and
                # sell at the identical rate) is the zero-arbitrage choice:
                # neither direction profits, so a wrong guess here costs
                # nothing either way rather than opening a money exploit.
                "sellmultiplier": head.get("sellmultiplier") or
                    (head.get("buymultiplier", "") if head.get("delta") else ""),
                "buymultiplier": head.get("buymultiplier", ""),
                # A pub/bar buyback shop (buymultiplier=0 — nothing is ever
                # purchasable) has nothing for haggle to move the price of, so
                # its own page routinely omits delta rather than stating a
                # meaningless 0. Both real multipliers present with delta
                # missing is that shape, not an unpriced shop — 13 confirmed
                # this way (docs/SHOPS_PLAN.md §8: Dancing Donkey Inn, Flying
                # Horse Inn, ...), all `buymultiplier=0`. Defaulting delta to
                # "0" here states the same "no movement" fact the page's
                # silence implies, for exactly this narrow shape; a page
                # missing sellmultiplier or buymultiplier too still falls
                # through to unpriced, same as ever.
                "delta": head.get("delta") or ("0" if head.get("buymultiplier") == "0" else ""),
                "lines": len(mine),
                "lines_resolved": resolved,
                "lines_needing_review": needs_review,
            }
        )

    return catalog_rows, stock_rows


def _locations_compatible(a: str, b: str) -> bool:
    """Same shop, catalogued twice, is (page + hosting-npc-page): the npc's own
    infobox usually states no `location=` at all, so one side empty is the
    common, correct case. Two different *stated* locations is not a coverage
    gap in this check — it is two different real shops that happen to use the
    same stock template (Jagex reuses templates constantly: every gem stall in
    the game sells the same four gems). 59 such pairs were being silently
    merged before this existed — `varrock_general_store` and
    `al_kharid_general_store` are obviously not one shop, and merging them
    made `gen_shop_scripts.py` skip generating Varrock's entirely."""
    return not a or not b or a == b


def mark_duplicates(catalog: list[dict], stock: list[dict]) -> None:
    """The roster unions two templates, so a shop with its own page *and* an
    owner npc page that repeats the table is catalogued twice. Same stock, same
    three multipliers means same shop *only when the location doesn't actively
    disagree* — see `_locations_compatible`. `duplicate_of` names the copy to
    author from, and the shop page wins over the npc page when both exist."""
    lines: dict[str, list[str]] = collections.defaultdict(list)
    for row in stock:
        lines[row["shop_key"]].append(str(row["obj_id"] or row["wiki_item"]))

    groups: dict[tuple, list[dict]] = collections.defaultdict(list)
    for row in catalog:
        if not int(row["lines"]):
            continue
        key = (
            tuple(sorted(lines[row["shop_key"]])),
            row["sellmultiplier"],
            row["buymultiplier"],
            row["delta"],
        )
        groups[key].append(row)

    for members in groups.values():
        if len(members) < 2:
            continue
        members.sort(key=lambda r: (r["is_shop_page"] != "yes", r["shop_key"]))
        canonical = members[0]
        for row in members[1:]:
            if _locations_compatible(canonical["location"], row["location"]):
                row["duplicate_of"] = canonical["shop_key"]
            # Incompatible: leave `row` as its own independent shop. It may
            # still end up sharing an inv with `canonical` for real (the gem
            # stalls do) — that is `gen_shop_inv_map.py`'s question to answer
            # with real evidence, not this pass's to assume from stock alone.


def run(write: bool, obj_csv: str) -> None:
    by_name, gameval, tradeable = load_obj_index(obj_csv)
    manifest = load_manifest()

    catalog: list[dict] = []
    stock: list[dict] = []
    no_table: list[str] = []

    for title, meta in sorted(manifest.items()):
        path = os.path.join(SHOPS_DIR, safe_filename(title))
        if not os.path.exists(path):
            continue
        text = open(path, encoding="utf-8").read()
        rows, lines = parse_page(title, meta, text, by_name, gameval, tradeable)
        if not rows:
            no_table.append(title)
            continue
        catalog += rows
        stock += lines

    mark_duplicates(catalog, stock)

    total = sum(int(r["lines"]) for r in catalog)
    resolved = sum(int(r["lines_resolved"]) for r in catalog)
    review = sum(int(r["lines_needing_review"]) for r in catalog)
    priced = sum(1 for r in catalog if r["sellmultiplier"] and r["buymultiplier"] and r["delta"])
    owned = sum(1 for r in catalog if r["owner_page"] or r["page_npc_ids"])
    rules = collections.Counter(r["match_rule"] for r in stock if r["match_rule"])
    clean = sum(1 for r in catalog if int(r["lines"]) and not int(r["lines_needing_review"]))

    dupes = sum(1 for r in catalog if r["duplicate_of"])
    print(f"{len(catalog)} stock tables across {len(manifest)} pages")
    print(f"  {dupes} are the same shop catalogued twice ({len(catalog) - dupes} distinct)")
    print(f"  {priced} carry all three price multipliers")
    print(f"  {owned} name an owner npc (link or id)")
    print(f"  {clean} tables resolve every line with no review flag")
    print(f"  {total} stock lines, {resolved} resolved to an obj id, {review} need review")
    print(f"  match rules: {dict(rules)}")
    print(f"  {len(no_table)} pages carry no StoreTableHead at all")

    if not write:
        print("\n(dry run — pass --write to update the CSVs)")
        return

    with open(CATALOG, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=CATALOG_FIELDS)
        w.writeheader()
        w.writerows(catalog)
    with open(STOCK, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=STOCK_FIELDS)
        w.writeheader()
        w.writerows(stock)
    print(f"\nwrote {CATALOG}\nwrote {STOCK}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--write", action="store_true", help="update the CSVs (default: report only)")
    ap.add_argument("--obj-csv", default=DEFAULT_OBJ_CSV, help="reuse a dump_stats --obj-csv output")
    args = ap.parse_args()
    run(args.write, args.obj_csv)


if __name__ == "__main__":
    main()
