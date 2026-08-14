#!/usr/bin/env python3
"""
gen_shop_inv_map — seed the wiki-shop -> cache-inv binding worksheet.

    tools/wiki_shop_owners.py --write     # first; writes wiki/shop_owners.csv
    tools/gen_shop_inv_map.py --write     # then; writes wiki/shop_inv_map.tsv

Which cache inventory backs which wiki shop is the one join in this pipeline
that cannot be computed. `generalshop1` is Lumbridge and `generalshop3` is
Al-Kharid, and nothing in either name says so — Jagex's gameval names are
mnemonic, not descriptive, and the numbering follows no order the cache states.
So this tool does not decide; it *seeds a table for a human to decide in*, and
records where each proposal came from so the ones already checked by someone
else are not re-litigated.

Three tiers, and the `source` column is which one a row came from:

  `lostcity`   LostCity's own content already binds this npc to this inv, via
               `param=owned_shop,<inv>` in its `.npc` configs, and its `.inv`
               blocks cite the wiki page they were transcribed from. Those
               bindings are hand-verified upstream and are imported as-is —
               the *stock* is 2004-era and gets replaced by ours, but the
               npc -> inv identity does not change between eras.
  `namematch`  the inv gameval and the shop's owner gameval or page slug share
               enough tokens to be worth proposing. A suggestion, nothing more.
  ``          (empty) an inv nothing proposed a shop for, or a shop nothing
               proposed an inv for. Both directions are listed, because a shop
               with no inv needs an allocation and an inv with no shop is
               either dead content or a shop this crawl missed.

`confidence` is `verified` only for the LostCity tier. Everything else is
`proposed`, and the file is meant to be edited: a reviewer flips a row to
`verified`, fixes the inv, or blanks it. Re-running never overwrites a row
whose confidence is `verified` — see `--refresh` for the escape hatch.
"""

from __future__ import annotations

import argparse
import collections
import csv
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
CATALOG = os.path.join(CONTENT, "wiki", "shop_catalog.csv")
OWNERS = os.path.join(CONTENT, "wiki", "shop_owners.csv")
INV_COMPACK = os.path.join(CONTENT, "configs", "all.inv.compack")
INV_ALLOC = os.path.join(CONTENT, "pack", "inv.alloc")
OUT = os.path.join(CONTENT, "wiki", "shop_inv_map.tsv")

LOSTCITY = "/Users/matthewevers/Documents/git_repos/LostCity_Server/content"

FIELDS = [
    "shop_key",
    "shop_name",
    "cache_inv",
    "inv_id",
    "owner_gameval",
    "confidence",
    "source",
    "note",
]

# Names in the inv namespace that are containers but not shops. The namespace
# holds 1,026 records and only a fraction are storefronts; the rest are skill
# guides, smithing menus, minigame holding pens, GE offer slots, and the
# player's own containers. Excluding them is what makes the unclaimed list
# short enough to read.
NON_SHOP_PREFIXES = (
    "skill_guide_",
    "smithing_",
    "silvercast_",
    "crafting_make_",
    "hundred_",
    "100guide_",
    "trail_",
    "roguesden_puzzle",
    "boardgames_",
    "poh_furniture_menu",
    "reinitialisation_",
    "minecart_temp",
    "ge_offer_",
    "ge_collect_",
    "dream_",
    "eyeglo_",
    "bonds_",
    "bh_",
    "br_",
    "bas_",
    "dom_",
    "duelholding",
    "dueldisplay",
    "barbassault_egg",
    "cutscene_",
    "bot_busting",
)
NON_SHOP_SUFFIXES = ("_dummy", "_inv_in", "_inv_out", "_inv_side", "_holding", "_holdinginv")
NON_SHOP_EXACT = {
    "inv",
    "worn",
    "bank",
    "tradeoffer",
    "dueloffer",
    "duelwinnings",
    "dueltax",
    "duelarrows",
    "deathkeep",
    "gravestone",
    "macro_certer",
    "pickcatinv",
    "wielded_weapon_inv",
    "misc_resources_collected",
    "partyroom_dropinv",
    "partyroom_tempinv",
    "trawler_rewardinv",
    "rangingguild_trade_tickets",
    "collection_transmit",
    "bankpin_inv",
    "gauntlet_holding",
    "barbarian_knapsack",
    "forestry_kit",
    "canoe_axe",
    "butlers_bell_collection",
    "aldarin_donation_nest",
}

# One shop, one inv per account mode. `axeshop` and `axeshop_uim` are the same
# storefront; only the base name needs a binding, and listing the variants as
# unclaimed buries the shops that genuinely have none.
MODE_VARIANT_RE = re.compile(r"(_(gim|uim|im|leagues|deadman)$)|^deadman_|^aprilfoolshorseshop_")


def load_invs() -> dict[str, str]:
    """gameval name -> id, for every inv the cache or the tree names."""
    out: dict[str, str] = {}
    for path in (INV_COMPACK, INV_ALLOC):
        if not os.path.exists(path):
            continue
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("//") or "=" not in line:
                continue
            num, _, name = line.partition("=")
            if num.isdigit():
                out[name] = num
    return out


def inv_kind(name: str) -> str:
    """`shop`, `mode-variant`, or `non-shop`."""
    if name in NON_SHOP_EXACT or name.startswith(NON_SHOP_PREFIXES):
        return "non-shop"
    if name.endswith(NON_SHOP_SUFFIXES):
        return "non-shop"
    if MODE_VARIANT_RE.search(name):
        return "mode-variant"
    return "shop"


def tokens(name: str) -> set[str]:
    parts = re.split(r"[^a-z0-9]+", name.lower())
    # Trailing digits carry no meaning across namespaces: `generalshop1` and
    # `generalshopkeeper1` agreeing on "1" is a real signal, but `shop2` and
    # `store2` agreeing on it is not, so the digit rides with its word.
    return {p for p in parts if p and p not in {"shop", "store", "the", "of", "s"}}


def load_lostcity_bindings() -> dict[str, dict]:
    """npc gameval -> {'inv':..., 'title':...} from LostCity's .npc configs."""
    out: dict[str, dict] = {}
    for path in glob.glob(os.path.join(LOSTCITY, "**", "*.npc"), recursive=True):
        if "_unpack" in path:
            continue
        block = None
        for line in open(path, encoding="utf-8", errors="replace"):
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                block = line[1:-1]
            elif block and line.startswith("param=owned_shop,"):
                out.setdefault(block, {})["inv"] = line.split(",", 1)[1].strip()
            elif block and line.startswith("param=shop_title,"):
                out.setdefault(block, {})["title"] = line.split(",", 1)[1].strip()
    return {k: v for k, v in out.items() if "inv" in v}


def load_existing() -> dict[str, dict]:
    if not os.path.exists(OUT):
        return {}
    with open(OUT, newline="", encoding="utf-8") as f:
        return {row["shop_key"]: row for row in csv.DictReader(f, delimiter="\t")}


def build(write: bool, refresh: bool) -> None:
    invs = load_invs()
    kinds = {n: inv_kind(n) for n in invs}
    shop_invs = {n: i for n, i in invs.items() if kinds[n] == "shop"}
    lostcity = load_lostcity_bindings()
    existing = load_existing()

    catalog = {r["shop_key"]: r for r in csv.DictReader(open(CATALOG, newline="", encoding="utf-8"))}
    owners = list(csv.DictReader(open(OWNERS, newline="", encoding="utf-8")))
    by_shop: dict[str, list[dict]] = collections.defaultdict(list)
    for row in owners:
        if row["npc_gameval"]:
            by_shop[row["shop_key"]].append(row)

    rows: list[dict] = []
    claimed: set[str] = set()

    for shop_key, shop in sorted(catalog.items()):
        prior = existing.get(shop_key)
        if prior and prior.get("confidence") == "verified" and not refresh:
            rows.append(prior)
            claimed.add(prior["cache_inv"])
            continue

        owner_gamevals = [r["npc_gameval"] for r in by_shop.get(shop_key, [])]
        inv = ""
        source = ""
        note = ""

        for gameval in owner_gamevals:
            if gameval in lostcity:
                inv = lostcity[gameval]["inv"]
                source = "lostcity"
                note = lostcity[gameval].get("title", "")
                break

        if not inv:
            want = tokens(shop_key) | set().union(*(tokens(g) for g in owner_gamevals)) if owner_gamevals else tokens(shop_key)
            best, score = "", 0
            for name in shop_invs:
                overlap = len(tokens(name) & want)
                if overlap > score:
                    best, score = name, overlap
            if score >= 2:
                inv = best
                source = "namematch"
                note = f"{score} shared tokens"

        rows.append(
            {
                "shop_key": shop_key,
                "shop_name": shop["shop_name"],
                "cache_inv": inv,
                "inv_id": invs.get(inv, ""),
                "owner_gameval": " ".join(dict.fromkeys(owner_gamevals)),
                "confidence": "verified" if source == "lostcity" else ("proposed" if inv else ""),
                "source": source,
                "note": note,
            }
        )
        if inv:
            claimed.add(inv)

    unclaimed = sorted(set(shop_invs) - claimed)
    verified = sum(1 for r in rows if r["confidence"] == "verified")
    proposed = sum(1 for r in rows if r["confidence"] == "proposed")
    unbound = sum(1 for r in rows if not r["cache_inv"])
    tally = collections.Counter(kinds.values())

    print(f"{len(rows)} shops; {len(invs)} invs in the namespace "
          f"({tally['shop']} shop-shaped, {tally['mode-variant']} account-mode "
          f"variants, {tally['non-shop']} not shops)")
    print(f"  {verified} verified from LostCity's own owned_shop bindings")
    print(f"  {proposed} proposed by name match (review these)")
    print(f"  {unbound} shops with no inv proposed at all")
    print(f"  {len(unclaimed)} shop invs nothing claimed")

    if not write:
        print("\n(dry run — pass --write to update the TSV)")
        return

    with open(OUT, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, delimiter="\t")
        w.writeheader()
        w.writerows(rows)
        for name in unclaimed:
            w.writerow(
                {
                    "shop_key": "",
                    "shop_name": "",
                    "cache_inv": name,
                    "inv_id": shop_invs[name],
                    "owner_gameval": "",
                    "confidence": "",
                    "source": "unclaimed-inv",
                    "note": "cache names this inv; no crawled shop claimed it",
                }
            )
    print(f"\nwrote {OUT}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--write", action="store_true", help="update the TSV (default: report only)")
    ap.add_argument(
        "--refresh", action="store_true", help="re-propose even rows marked verified by hand"
    )
    args = ap.parse_args()
    build(args.write, args.refresh)


if __name__ == "__main__":
    main()
