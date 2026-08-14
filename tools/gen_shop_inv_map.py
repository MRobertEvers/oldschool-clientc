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

Six tiers, and the `source` column is which one a row came from — the first
four are `confidence=verified` (each is a fact, not a guess); the rest are
`confidence=proposed` and need a person:

  `lostcity`            LostCity's own content already binds this npc to this
                         inv, via `param=owned_shop,<inv>` in its `.npc`
                         configs. Hand-verified upstream, imported as-is.
  `skillcape-variant`   a multi-table page's 2nd/3rd table, derived from its
                         own already-verified base table's inv via the
                         cache's `{base}_skillcape[_trimmed]` naming
                         convention — only when that exact name exists.
  `exact-gameval-match` the owner npc's own gameval is spelled identically to
                         a cache inv name (`aldarin_general_store` runs
                         `aldarin_general_store`) — a fact about the cache's
                         own naming, not a guess.
  `owner-stem-match`    the owner's gameval, with a role suffix stripped
                         (`_shopkeeper`, `_owner`, `_1op`, ...), is identical
                         to the inv name with a shop suffix stripped
                         (`_shop`, `_store`, ...) — `warguild_armour_shopkeeper`
                         / `warguild_armour_shop` share the stem
                         `warguild_armour`. Weaker than exact-gameval-match
                         only in that two suffixes were peeled off instead of
                         zero; still an identity match, not a token count.
  `namematch`           the inv gameval and the shop's owner gameval or page
                         slug share enough tokens to be worth proposing. A
                         suggestion, nothing more — `confidence=proposed`.
  ``                    (empty) an inv nothing proposed a shop for, or a shop
                         nothing proposed an inv for. Both directions are
                         listed, because a shop with no inv needs an
                         allocation and an inv with no shop is either dead
                         content or a shop this crawl missed.

`confidence` is `verified` for the four identity-based tiers above and
`proposed` for `namematch`/unbound. The file is meant to be edited: a
reviewer flips a `proposed` row to `verified`, fixes the inv, or blanks it.
Re-running never overwrites a row
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
    # NOT a blanket "smithing_": smithing_bronze1..5/iron1..5/.../claws and
    # smithing_lamp_iron/steel are the smithing-minigame's own progression
    # menus, never a shop a player walks up to — but smithing_guild_buyer and
    # smithing_guild_ore_seller (both at the Blast Furnace) are real
    # storefronts the blanket prefix was hiding (docs/SHOPS_PLAN.md §8).
    "smithing_bronze", "smithing_iron", "smithing_steel", "smithing_mithril",
    "smithing_adamant", "smithing_rune", "smithing_lamp",
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


# Trailing words that name a *role* (or, for `_multi`, a presentation layer)
# on an npc gameval, not the shop it runs — stripped so
# `deepfin_dwarf_durrik_1op` and `deepfin_dwarf_durrik_shop` (the inv, via
# strip_shop_suffix) compare equal on their shared stem
# `deepfin_dwarf_durrik`. `_multi` is the multinpc-base marker
# (`anma_assistant_multi` is what actually spawns; `wiki_shop_owners.py`
# substitutes it in when the wiki's own stated npc id names a variant that
# never spawns — see its `_row` comment) and stacks with a role suffix
# (`ahoy_akharanu_multi`), so this strips in a loop rather than once.
OWNER_ROLE_SUFFIXES = (
    "_shopkeeper", "_assistant", "_owner", "_seller", "_trader", "_merchant",
    "_bartender", "_keeper", "_1op", "_2op", "_3op", "_helper", "_worker",
    "_npc", "_multi",
)
SHOP_ROLE_SUFFIXES = ("_shop", "_store", "_stall", "_market", "_shopkeeper")


def strip_role_suffix(gameval: str) -> str:
    changed = True
    while changed:
        changed = False
        for suffix in OWNER_ROLE_SUFFIXES:
            if gameval.endswith(suffix):
                gameval = gameval[: -len(suffix)]
                changed = True
                break
    return gameval


def strip_shop_suffix(inv_name: str) -> str:
    for suffix in SHOP_ROLE_SUFFIXES:
        if inv_name.endswith(suffix):
            return inv_name[: -len(suffix)]
    return inv_name


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


# A multi-table page's 2nd/3rd table is a skillcape-tier sub-stock (§7 of
# docs/SHOPS_PLAN.md — Ranging/Runecraft/Fletching/Magic/Thieving cape and its
# trimmed twin), and the cache follows one naming convention for every one of
# them: `{base_inv}_skillcape` / `{base_inv}_skillcape_trimmed`. This is a
# derivation from an already-verified base binding, not a guess — it is
# accepted only when the derived name actually exists in the cache's own
# namespace, so a base shop whose skillcape counterpart the cache never built
# (most of them: only 5 of the wiki's ~11 skillcape multi-tables have one)
# stays unbound rather than being pointed at a name that resolves to nothing.
SKILLCAPE_SECTION_RE = re.compile(r"^(.+?) cape(\(t\))?$", re.IGNORECASE)


def skillcape_variant(base_inv: str, section: str, shop_invs: dict[str, str]) -> tuple[str, str]:
    """(inv name, note) for a skillcape sub-table, or ("", "") if this cache
    has no such variant of `base_inv`."""
    m = SKILLCAPE_SECTION_RE.match(section.strip())
    if not m or not base_inv:
        return "", ""
    trimmed = bool(m.group(2))
    candidate = f"{base_inv}_skillcape_trimmed" if trimmed else f"{base_inv}_skillcape"
    if candidate in shop_invs:
        return candidate, f"skillcape variant of {base_inv}"
    return "", ""


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
    by_key: dict[str, dict] = {}
    claimed: set[str] = set()

    for shop_key, shop in sorted(catalog.items()):
        prior = existing.get(shop_key)
        # `manual-review` rows are a human's own finding (docs/SHOPS_PLAN.md
        # §8's general-store/stall passes) and outrank every mechanical rule
        # below by construction — `--refresh` re-derives everything *else*
        # specifically so a smarter mechanical rule can correct an old
        # mechanical guess, but it has no way to know a hand-verified row is
        # right, so it must never touch one. Losing 16 of these to a refresh
        # that didn't know they existed is what taught this the hard way.
        if prior and prior.get("confidence") == "verified" and (
            not refresh or prior.get("source") == "manual-review"
        ):
            rows.append(prior)
            by_key[shop_key] = prior
            claimed.add(prior["cache_inv"])
            continue

        owner_gamevals = [r["npc_gameval"] for r in by_shop.get(shop_key, [])]
        inv = ""
        source = ""
        note = ""

        # A sub-table of a multi-table shop shares its owner npc with every
        # other table on the same page — the owner-based lostcity lookup below
        # cannot tell "Ranging cape" apart from the base table, and matching on
        # it first is exactly the bug that put __2 and __3 on the same inv as
        # __1 (docs/SHOPS_PLAN.md §1.3's original phase-1 pass). So the
        # skillcape derivation goes first here, off the base table's *own*
        # already-decided binding, and only the base table (or a sub-table with
        # no skillcape counterpart in this cache) falls through to it.
        is_sub_table = "__" in shop_key and not shop_key.endswith("__1")
        if is_sub_table:
            base_key = shop_key.rsplit("__", 1)[0] + "__1"
            base_row = by_key.get(base_key)
            if base_row and base_row["confidence"] == "verified":
                inv, note = skillcape_variant(base_row["cache_inv"], shop.get("section", ""), shop_invs)
                if inv:
                    source = "skillcape-variant"
            # No owner-based lostcity fallback here on purpose: every table on
            # a multi-table page shares one owner npc, so that lookup cannot
            # distinguish "Ranging cape" from the base table and would bind
            # both to the same inv — the bug this whole branch exists to avoid.
        else:
            for gameval in owner_gamevals:
                if gameval in lostcity:
                    inv = lostcity[gameval]["inv"]
                    source = "lostcity"
                    note = lostcity[gameval].get("title", "")
                    break

        # An owner npc's own gameval, spelled identically to a cache inv name,
        # is not a token-overlap guess — it is the same string, and this
        # cache's newer (post-2004) content routinely names a shop's inv after
        # the one npc who runs it (`aldarin_general_store` the npc owns
        # `aldarin_general_store` the inv; `cam_torum_shop_blacksmith`,
        # `port_roberts_silver_trader`, ...). 104 of the 305 `proposed` rows
        # this replaced were exactly this shape (2026-08-13 audit) and had
        # been scored as weak 2-token matches only because `tokens()` strips
        # "shop"/"store" for the *token-overlap* heuristic below — a filter
        # that is right for that heuristic and wrong for exact identity, which
        # is why this is a separate check rather than a threshold on the same
        # score. Gated the same way the plain lostcity lookup above is (only
        # for a base table, never a sub-table): every table on a multi-table
        # page shares one owner npc, so this can't tell "Ranging cape" from
        # the base either, and would repeat the same wrong-duplicate bug.
        if not inv and not is_sub_table:
            for gameval in owner_gamevals:
                if gameval in shop_invs:
                    inv = gameval
                    source = "exact-gameval-match"
                    note = "owner npc's own gameval is the inv name"
                    break

        # A weaker cousin of exact-gameval-match: strip a role suffix off the
        # owner's gameval (`deepfin_dwarf_durrik_1op` -> `deepfin_dwarf_durrik`)
        # and a shop suffix off the inv name (`deepfin_dwarf_durrik_shop` ->
        # `deepfin_dwarf_durrik`), and accept only when what's left is
        # identical — not merely a prefix, which would also match e.g.
        # `hunting_shop` against both `hunting_shop_yanille` and
        # `hunting_shop_nardah`. Still gated off sub-tables for the same
        # shared-owner reason as above.
        if not inv and not is_sub_table:
            for gameval in owner_gamevals:
                stem = strip_role_suffix(gameval)
                for name in shop_invs:
                    if stem and stem == strip_shop_suffix(name):
                        inv = name
                        source = "owner-stem-match"
                        note = f"{gameval} / {name} share the stem '{stem}'"
                        break
                if inv:
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

        row = {
            "shop_key": shop_key,
            "shop_name": shop["shop_name"],
            "cache_inv": inv,
            "inv_id": invs.get(inv, ""),
            "owner_gameval": " ".join(dict.fromkeys(owner_gamevals)),
            "confidence": "verified" if source in ("lostcity", "skillcape-variant", "exact-gameval-match", "owner-stem-match") else ("proposed" if inv else ""),
            "source": source,
            "note": note,
        }
        rows.append(row)
        by_key[shop_key] = row
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
    skillcape = sum(1 for r in rows if r["source"] == "skillcape-variant")
    print(f"  {verified} verified ({skillcape} of those derived from a "
          f"skillcape-tier naming convention off an already-verified base)")
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
