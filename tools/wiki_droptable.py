#!/usr/bin/env python3
"""
wiki_droptable — parse `{{DropsLine}}` tables out of a cached OSRS Wiki
monster page and emit the RuneScript threshold-walk shape every hand-ported
table in server/scripts/drop_tables/scripts/ already uses.

    tools/wiki_droptable.py --report <gameval> [<gameval> ...]
    tools/wiki_droptable.py --batch <file-of-gamevals> --write

See docs/NPC_WIKI_DROPTABLES_PLAN.md §4. The id join is not re-derived here —
it is read straight out of npc_stats/<shard>/<gameval>.stats, the same ledger
tools/gen_npc_stats.py already wrote for combat stats (§1 of the plan: match
by id, never by name, and the id match already happened once).
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wiki_infobox as wi  # noqa: E402

NPC_STATS_DIR = os.path.join(CONTENT, "npc_stats")
MANIFEST = os.path.join(CONTENT, "wiki", "manifest.tsv")

HEADING_RE = re.compile(r"^(={2,6})\s*(.+?)\s*\1\s*$")
STATS_SOURCE_RE = re.compile(
    r'Source: OSRS Wiki [\'"]([^\'"]+)[\'"] \(version ids include ([0-9, ]+)\)'
)


# ---------------------------------------------------------------------------
# Step 1: find the (title, npc ids) this gameval already resolved to.
# ---------------------------------------------------------------------------

_DEATH_DROP_INDEX: dict[str, str] | None = None


def death_drop_index() -> dict[str, str]:
    """gameval -> its own `param=death_drop,<value>` (rank-0 configs/all.npc,
    then rank-1 overlays winning, matching the same load order every other
    npc field in this tree already follows). Some npcs are configured
    `death_drop,null` -- OldSchool giant spiders and this batch's
    `shadow_spider` drop nothing at all, not even bones (shared_droptables.rs2
    says so explicitly for the spiders). Restating `npc_param(death_drop)`
    unconditionally for one of those would emit `obj_add(..., null, ...)`,
    which no existing table has ever exercised because nothing currently
    binds a table to one of these npcs."""
    global _DEATH_DROP_INDEX
    if _DEATH_DROP_INDEX is not None:
        return _DEATH_DROP_INDEX
    index: dict[str, str] = {}
    block_re = re.compile(r"^\[([A-Za-z0-9_]+)\]$")
    dd_re = re.compile(r"^param=death_drop,(\S+)$")
    paths = [os.path.join(CONTENT, "configs", "all.npc")] + sorted(
        glob.glob(os.path.join(CONTENT, "server", "scripts", "**", "configs", "*.npc"), recursive=True)
    )
    for path in paths:
        cur = None
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                m = block_re.match(line)
                if m:
                    cur = m.group(1)
                    continue
                m = dd_re.match(line)
                if m and cur:
                    index[cur] = m.group(1)
    _DEATH_DROP_INDEX = index
    return index


def find_stats_file(gameval: str) -> str | None:
    shard = gameval[0] if gameval else "_"
    path = os.path.join(NPC_STATS_DIR, shard, f"{gameval}.stats")
    return path if os.path.isfile(path) else None


def resolved_title(gameval: str) -> tuple[str, list[int]] | None:
    path = find_stats_file(gameval)
    if not path:
        return None
    with open(path, encoding="utf-8", errors="replace") as f:
        head = f.read(600)
    m = STATS_SOURCE_RE.search(head)
    if not m:
        return None
    title = m.group(1)
    ids = [int(x) for x in re.split(r",\s*", m.group(2)) if x.strip()]
    return title, ids


def manifest_row(title: str) -> tuple[str, str] | None:
    """(revid, fetch_date) for a page title, from wiki/manifest.tsv."""
    if not os.path.isfile(MANIFEST):
        return None
    with open(MANIFEST, encoding="utf-8") as f:
        next(f, None)
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 4 and parts[0] == title:
                return parts[1], parts[3]
    return None


# ---------------------------------------------------------------------------
# Step 2: generic top-level template walk (reuses wiki_infobox's depth-aware
# helpers — DropsLine values can nest {{plink|...}} the same way infobox
# fields can).
# ---------------------------------------------------------------------------

def _iter_top_level_templates(wikitext: str):
    i = 0
    n = len(wikitext)
    while i < n:
        if wikitext[i:i + 2] == "{{":
            close = wi._find_matching_close(wikitext, i)
            inner = wikitext[i + 2:close - 2]
            parts = wi._split_top_level(inner, "|")
            name = parts[0].strip()
            params = {}
            for part in parts[1:]:
                if "=" in part:
                    k, v = part.split("=", 1)
                    params[k.strip().lower()] = v.strip()
            yield name, params, i, close
            i = close
        else:
            i += 1


def _headings(wikitext: str):
    """[(char_offset, level, title), ...] in document order."""
    out = []
    offset = 0
    for line in wikitext.split("\n"):
        m = HEADING_RE.match(line.strip())
        if m:
            out.append((offset, len(m.group(1)), m.group(2)))
        offset += len(line) + 1
    return out


def _heading_context(headings, pos):
    """(h2_title, h3_title) in effect at character offset `pos`."""
    h2 = h3 = None
    for off, level, title in headings:
        if off > pos:
            break
        if level == 2:
            h2, h3 = title, None
        elif level == 3:
            h3 = title
    return h2, h3


# ---------------------------------------------------------------------------
# Step 3: pull DropsLine entries into (dropversion, tertiary, [lines]) blocks.
# ---------------------------------------------------------------------------

def parse_drop_blocks(wikitext: str) -> list[dict]:
    headings = _headings(wikitext)
    blocks = []
    current = None
    for name, params, start, end in _iter_top_level_templates(wikitext):
        lname = name.strip().lower()
        if lname == "dropstablehead":
            h2, h3 = _heading_context(headings, start)
            dropversion = params.get("dropversion") or h2
            tertiary = bool(h3 and "tertiary" in h3.lower())
            current = {"dropversion": dropversion, "tertiary": tertiary, "lines": []}
            blocks.append(current)
        elif lname == "dropsline" and current is not None:
            current["lines"].append({
                "name": wi.clean_text(params.get("name", "")),
                "quantity": wi.clean_text(params.get("quantity", "1")) or "1",
                "rarity": wi.clean_text(params.get("rarity", "")),
            })
        elif lname == "dropstablebottom":
            current = None
    return blocks


def select_blocks(blocks: list[dict], target_dropversion: str | None) -> list[dict]:
    if target_dropversion:
        matched = [b for b in blocks if b["dropversion"] == target_dropversion]
        if matched:
            return matched
    # Single-version page (no dropversion field, or nothing matched): take
    # every block that isn't itself tagged with a *different* version's name
    # a heading walk already found. Conservative fallback: all blocks whose
    # dropversion is falsy or already equal to the only distinct value seen.
    versions_seen = {b["dropversion"] for b in blocks if b["dropversion"]}
    if len(versions_seen) <= 1:
        return blocks
    return []  # ambiguous — caller reports it, does not guess


# ---------------------------------------------------------------------------
# Step 4: rarity/quantity -> RuneScript.
# ---------------------------------------------------------------------------

FRACTION_RE = re.compile(r"^(\d+)\s*/\s*(\d+)$")
RANGE_RE = re.compile(r"^(\d+)\s*-\s*(\d+)$")
INT_RE = re.compile(r"^\d+$")


def quantity_expr(qty: str) -> str | None:
    qty = qty.strip()
    m = RANGE_RE.match(qty)
    if m:
        lo, hi = int(m.group(1)), int(m.group(2))
        if hi <= lo:
            return None
        return f"calc({lo} + random({hi - lo + 1}))"
    if INT_RE.match(qty):
        return qty
    return None  # comma lists, "?", etc. — not generated, flagged by caller


# What a corpse leaves behind: the bones families, the demonic ashes families,
# and `Urium remains`. Matched as whole words against the wiki's display name so
# `Bonesack` or `Ashes of the Fallen` cannot join by accident.
REMAINS_RE = re.compile(r"\b(bones|ashes|remains)\b", re.I)


def death_drop_choice(lines: list[dict]) -> dict | None:
    """The one `Always` remains line `param=death_drop` speaks for, or None.

    **This is the single decision `tools/gen_npc_stats.py` and this file must
    agree on**, which is why it lives here and both import it. The generator
    writes the chosen item as `param=death_drop,<gameval>`; `build_table` below
    drops that same line from the table it emits. Compute it twice, differently,
    and the npc either drops its remains twice or not at all — both of which
    this tree has actually shipped.

    First in wiki order wins. 11 roster npcs state two distinct remains items
    (Callisto and Venenatis drop Big bones *and* Bones); `death_drop` is one
    value, so it takes the first and the table keeps the rest as ordinary
    `Always` drops, which is exactly right.

    Quantity is not consulted. `death_drop` is always one item —
    `obj_add(npc_coord, npc_param(death_drop), 1, ...)` at all 80 restatement
    sites — and 40 roster npcs state `1;2` for their remains. Dropping one where
    the wiki says one-or-two is a rounding error; the alternative designs are
    worse (leaving it to the table double-drops it, since the guarded
    restatement still fires, and the npcs with no generated table at all would
    then drop nothing).
    """
    for line in lines:
        nm = (line.get("name") or "").strip()
        if not nm or not REMAINS_RE.search(nm):
            continue
        if line.get("rarity", "").strip().lower() != "always":
            continue
        gameval = resolve_obj_name(nm)
        if gameval is None:
            continue
        return {"name": nm, "gameval": gameval, "quantity": line.get("quantity", "1")}
    return None


def build_table(lines: list[dict], death_drop: dict | None = None) -> tuple[list[str], list[str]]:
    """Returns (runescript_lines, skipped_reasons). `Always` lines become
    unconditional obj_add; fraction lines become one cumulative threshold
    walk over the LCD of their denominators, in wiki order, matching every
    hand-ported table's existing shape.

    `death_drop` is `death_drop_choice(lines)` — the one line the npc's
    `param=death_drop` already states, skipped here so it is not dropped twice.
    """
    always: list[dict] = []
    fractions: list[dict] = []
    skipped: list[str] = []
    death_drop_taken = False

    for line in lines:
        nm = line["name"]
        # Plain `Bones` keeps its blanket filter, at every rarity, and that is
        # deliberate rather than left over. Several pages state bones twice: an
        # `Always` line *and* a fractional one belonging to a sub-table the
        # cumulative-rarity model here cannot represent (Bloodveld: `Vile ashes`
        # Always, then `Bones 10/128` and `Big bones 7/128`). Letting the
        # fractional plain-bones line through pushed five bloodvelds, the
        # ancient hellhound and the giant frog over their own denominator
        # (133/128), which fails the check below and drops those seven tables on
        # the floor entirely — a much larger regression than the line is worth.
        # Widening this is its own job, with the sub-table model, not this one's.
        if not nm or nm.lower() in ("nothing", "bones"):
            continue
        # The npc's remains, skipped exactly once and matched on the same
        # identity `gen_npc_stats.py` wrote into `param=death_drop`. This is the
        # variant half: `Big bones`, `Dragon bones`, `Vile ashes` and the rest
        # were never filtered here, so the table emitted them *and* the
        # restatement added the tree-wide default plain bones on top — 128 npcs
        # leaving two sets of remains, one of them the wrong species.
        if (not death_drop_taken and death_drop is not None
                and (line.get("rarity", "").strip().lower() == "always")
                and resolve_obj_name(nm) == death_drop["gameval"]):
            death_drop_taken = True
            continue
        gameval = resolve_obj_name(nm)
        if gameval is None:
            skipped.append(f"{nm}: no obj in configs/all.obj carries this exact name= (item table drift, or the wiki's display text needs cleanup)")
            continue
        qty_expr = quantity_expr(line["quantity"])
        rarity = line["rarity"].strip()
        if qty_expr is None:
            skipped.append(f"{nm}: unhandled quantity '{line['quantity']}'")
            continue
        if rarity.lower() == "always":
            always.append({"gameval": gameval, "qty": qty_expr})
            continue
        m = FRACTION_RE.match(rarity)
        if not m:
            skipped.append(f"{nm}: unhandled rarity '{rarity}'")
            continue
        num, den = int(m.group(1)), int(m.group(2))
        fractions.append({"gameval": gameval, "qty": qty_expr, "num": num, "den": den})

    # Guarded, not unconditional, and now meaning what it says: an npc's
    # `param=death_drop` is `death_drop_choice`'s item, written by
    # `tools/gen_npc_stats.py` from this same page, or `null` when the page
    # states no remains at all. So the restatement drops the *right* remains for
    # the npc that died, and `null` genuinely means "leaves nothing" rather than
    # "nobody filled this in".
    #
    # One shared label still covers several gamevals, which is still fine and is
    # now the point: `npc_param` resolves per-npc at runtime, so the six TzHaar-
    # Xil share a label and each answers for itself.
    #
    # See docs/NPC_WIKI_DROPTABLES_PLAN.md §7 and §9.
    out = [
        "if (npc_param(death_drop) ! null) {",
        "    obj_add(npc_coord, npc_param(death_drop), 1, ^lootdrop_duration);",
        "}",
    ]
    for a in always:
        out.append(f'obj_add(npc_coord, {a["gameval"]}, {a["qty"]}, ^lootdrop_duration);')

    if fractions:
        from math import gcd
        lcm = 1
        for f in fractions:
            lcm = lcm * f["den"] // gcd(lcm, f["den"])
        scale = {id(f): lcm // f["den"] for f in fractions}
        total = sum(f["num"] * scale[id(f)] for f in fractions)
        if total > lcm:
            skipped.append(f"cumulative rolls ({total}) exceed table denominator ({lcm}) after LCD scaling — rarities may be from different sub-tables, review by hand")
        else:
            out.append(f"def_int $dropint = random({lcm});")
            running = 0
            for i, f in enumerate(fractions):
                running += f["num"] * scale[id(f)]
                kw = "if" if i == 0 else "} else if"
                out.append(f'{kw} ($dropint < {running}) {{')
                out.append(f'    obj_add(npc_coord, {f["gameval"]}, {f["qty"]}, ^lootdrop_duration);')
            out.append("}")

    return out, skipped


def slugify(name: str) -> str:
    return re.sub(r"[^a-z0-9_]", "", name.strip().lower().replace(" ", "_").replace("-", "_").replace("'", ""))


ALL_OBJ = os.path.join(CONTENT, "configs", "all.obj")
_OBJ_NAME_INDEX: dict[str, list[str]] | None = None
_SUSPECT_GAMEVAL_RE = re.compile(
    r"(^fake_|^dummy|^test|^unused|^old_|^deleted|^roguetrader_|^100guide_|_dum$|^guide_|^tutorial_|_placeholder$)",
    re.IGNORECASE,
)


def obj_name_index() -> dict[str, list[str]]:
    """lowercased obj `name=` -> [gameval, ...], in file order. Multiple
    gamevals routinely share a display name (event/quest/joke reskins of the
    same item) -- resolve_obj_name() picks among them."""
    global _OBJ_NAME_INDEX
    if _OBJ_NAME_INDEX is not None:
        return _OBJ_NAME_INDEX
    index: dict[str, list[str]] = {}
    cur = None
    block_re = re.compile(r"^\[([A-Za-z0-9_]+)\]$")
    name_re = re.compile(r"^name=(.+)$")
    with open(ALL_OBJ, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = block_re.match(line)
            if m:
                cur = m.group(1)
                continue
            m = name_re.match(line)
            if m and cur:
                index.setdefault(m.group(1).strip().lower(), []).append(cur)
    _OBJ_NAME_INDEX = index
    return index


def resolve_obj_name(display_name: str) -> str | None:
    """Display name (as it appears in a DropsLine) -> the gameval this table
    should reference. Exact match on the obj's own `name=` field, never a
    slug guess -- 'Water rune' is gameval `waterrune`, not `water_rune`, and
    naive slugification silently generated invalid item references for every
    multi-word or irregularly-named item until this was caught by hand-
    checking a sample against configs/all.obj."""
    candidates = obj_name_index().get(display_name.strip().lower())
    if not candidates:
        return None
    clean = [g for g in candidates if not _SUSPECT_GAMEVAL_RE.search(g)]
    pool = clean or candidates
    return min(pool, key=len)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

_EXISTING_BINDINGS: set[str] | None = None


def existing_bindings() -> set[str]:
    """Every gameval already bound by an [ai_queue3,<gameval>] anywhere in the
    whole content tree -- NOT just drop_tables/scripts/. Quest, minigame and
    boss scripts routinely declare their own ai_queue3 death-drop handler
    outside that directory (e.g. quest_legends/scripts/ranalph_devere.rs2),
    and port_droptables_check.py's duplicate-trigger bar scans the whole tree
    for exactly this reason. A first version of this scan only checked
    drop_tables/scripts/ and produced 51 real duplicate-trigger bindings on
    its first batch run -- checked fresh from every .rs2 file, not from the
    CSV ledger, which also only tracked drop_tables/scripts/ and so
    undercounts true coverage the same way."""
    global _EXISTING_BINDINGS
    if _EXISTING_BINDINGS is not None:
        return _EXISTING_BINDINGS
    seen = set()
    binding_re = re.compile(r"^\[ai_queue3,([A-Za-z0-9_]+)\]")
    for path in glob.glob(os.path.join(CONTENT, "server", "scripts", "**", "*.rs2"), recursive=True):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = binding_re.match(line.strip())
                if m:
                    seen.add(m.group(1))
    _EXISTING_BINDINGS = seen
    return seen


def report_one(gameval: str) -> dict:
    if gameval in existing_bindings():
        return {"gameval": gameval, "ok": False, "reason": "already exact-bound in drop_tables/scripts/ -- ledger is stale, skipping"}
    res = resolved_title(gameval)
    if res is None:
        return {"gameval": gameval, "ok": False, "reason": "no confirmed wiki join in npc_stats"}
    title, ids = res
    try:
        wikitext = wi.load_page(title)
    except FileNotFoundError:
        return {"gameval": gameval, "ok": False, "reason": f"no cached wikitext for '{title}'"}

    versions = wi.parse_page(wikitext)
    stats_id = None
    for p in find_stats_id_candidates(gameval, ids):
        stats_id = p
        break
    match = None
    for v in versions:
        if stats_id in v["ids"]:
            match = v
            break
    if match is None and len(ids) == 1 and versions:
        for v in versions:
            if ids[0] in v["ids"]:
                match = v
                break
    if match is None:
        return {"gameval": gameval, "ok": False, "reason": f"id {ids} not in any version block on '{title}'"}

    target_dropversion = match["fields"].get("dropversion")
    blocks = parse_drop_blocks(wikitext)
    main_blocks = select_blocks([b for b in blocks if not b["tertiary"]], target_dropversion)
    tertiary_blocks = [b for b in blocks if b["tertiary"]]

    if not main_blocks:
        return {"gameval": gameval, "ok": False, "reason": f"no main drop block resolved for dropversion={target_dropversion!r} on '{title}' ({len(blocks)} blocks total)"}

    lines = [ln for b in main_blocks for ln in b["lines"]]
    rs2, skipped = build_table(lines, death_drop_choice(lines))
    if len(rs2) <= 3 and not skipped:
        # Nothing beyond the death_drop restatement -- a binding that states
        # nothing is worse than no binding at all (the project's own stated rule
        # for `duck`, shared_droptables.rs2). Not an error; just nothing to add.
        #
        # Still 3, and still correct now that the restatement carries the npc's
        # own remains: an npc whose whole table is `Bones|Always` is answered
        # entirely by `param=death_drop,bones`, which `[ai_queue3,_]` restates
        # for it without any binding here.
        return {"gameval": gameval, "ok": False, "reason": f"no drops beyond remains/default on '{title}' -- no binding needed"}
    manifest = manifest_row(title)
    return {
        "gameval": gameval, "ok": True, "title": title, "ids": ids,
        "dropversion": target_dropversion, "rs2": rs2, "skipped": skipped,
        "tertiary_present": bool(tertiary_blocks),
        "revid": manifest[0] if manifest else "?",
        "fetch_date": manifest[1] if manifest else "?",
    }


def find_stats_id_candidates(gameval: str, ids: list[int]):
    # npc_stats ids are already the resolved candidate set; try them in order.
    for i in ids:
        yield i


DROP_TABLES_DIR = os.path.join(CONTENT, "server", "scripts", "drop_tables", "scripts")


def title_slug(title: str) -> str:
    s = slugify(title)
    return s or "unnamed"


def partition_by_table(results: list[dict]) -> list[dict]:
    """Split one page's npcs into groups that genuinely share a drop table.

    A wiki page is one *monster*, not one *drop table*: `Goblin` carries `Drop
    table 1` and `Drop table 2`, `Zombie` carries `Level 13` and `Level 24`,
    `Hellhound` carries Regular / God Wars Dungeon / Wilderness Slayer Cave.
    `report_one` already resolves each npc to its own `dropversion` and builds
    that version's table; this is only about not throwing that away again when
    the file is written.

    It used to be thrown away: `write_group` emitted `results[0]["rs2"]` under a
    single label and pointed every `[ai_queue3]` on the page at it, so 48 npcs
    across 11 groups were served another version's loot — the level-13 giant
    frog got the level-99 one's table, and the God Wars hellhound got the
    surface one's smouldering stone.

    Partitioned on the generated table rather than on the `dropversion` string,
    so two versions that happen to roll the same drops still share one label
    (which is correct, and keeps the file short) while any real difference gets
    its own. Insertion-ordered, so output is stable across runs.
    """
    groups: dict[tuple, dict] = {}
    for r in results:
        key = tuple(r["rs2"])
        g = groups.get(key)
        if g is None:
            groups[key] = {"rs2": r["rs2"], "versions": [], "results": []}
            g = groups[key]
        g["results"].append(r)
        if r["dropversion"] and r["dropversion"] not in g["versions"]:
            g["versions"].append(r["dropversion"])
    return list(groups.values())


def write_group(title: str, results: list[dict]) -> str | None:
    slug = title_slug(title)
    path = os.path.join(DROP_TABLES_DIR, f"wiki_{slug}.rs2")
    if os.path.exists(path):
        return None  # do not clobber a prior run or a hand-authored collision
    groups = partition_by_table(results)
    r0 = results[0]
    lines = [
        f"// {r0['title']} death drops. Sourced from the OSRS Wiki (not a LostCity",
        "// port -- this npc postdates 2004scape's data, or wasn't ported. See",
        "// docs/NPC_WIKI_DROPTABLES_PLAN.md for the pipeline and citation rule.",
        f"// Source: OSRS Wiki '{r0['title']}', https://oldschool.runescape.wiki/w/"
        + r0['title'].replace(' ', '_') + f", revision {r0['revid']}, scraped {r0['fetch_date']}.",
    ]
    if any(r["tertiary_present"] for r in results):
        lines.append(f"// A Tertiary drop table exists on this page and is intentionally omitted --")
        lines.append(f"// no varp-check machinery exists in this table family yet (plan section 6).")
    if len(groups) > 1:
        lines.append("//")
        lines.append(f"// This page states {len(groups)} distinct drop tables and each npc below is")
        lines.append("// bound to its own. They are NOT interchangeable -- see plan section 9.")

    # Label names: the page's own `wiki_<slug>_drop` when the page has a single
    # table, which is the overwhelming majority and keeps those files byte-stable
    # across this change. Only a genuinely multi-table page gets the version
    # suffix, and only then does any existing file's text move.
    used: set[str] = set()
    for i, g in enumerate(groups):
        if len(groups) == 1:
            name = f"wiki_{slug}_drop"
        else:
            vslug = slugify(g["versions"][0]) if g["versions"] else ""
            name = f"wiki_{slug}_{vslug}_drop" if vslug else f"wiki_{slug}_{i + 1}_drop"
            # Two versions can slugify to the same thing, and a duplicate
            # `[label,...]` is a compile error rather than a silent mismerge --
            # but only because it is disambiguated here.
            if name in used:
                name = f"wiki_{slug}_{vslug}_{i + 1}_drop"
        used.add(name)
        g["label"] = name

    lines.append("")
    for g in groups:
        for r in g["results"]:
            lines.append(f"[ai_queue3,{r['gameval']}]")
            lines.append(f"@{g['label']};")
            lines.append("")
    for g in groups:
        if len(groups) > 1:
            versions = ", ".join(g["versions"]) if g["versions"] else "(unversioned)"
            covered = ", ".join(r["gameval"] for r in g["results"])
            lines.append(f"// dropversion: {versions}")
            lines.append(f"// covers: {covered}")
        lines.append(f"[label,{g['label']}]")
        lines.append("if (npc_findhero = ^false) {")
        lines.append("    return;")
        lines.append("}")
        lines.extend(g["rs2"])
        lines.append("")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("gamevals", nargs="*")
    ap.add_argument("--batch", help="file with one gameval per line")
    ap.add_argument("--write", action="store_true", help="write .rs2 files for clean (0-skip) results")
    args = ap.parse_args()

    gamevals = list(args.gamevals)
    if args.batch:
        with open(args.batch) as f:
            gamevals += [ln.strip() for ln in f if ln.strip() and not ln.startswith("#")]

    ok = 0
    clean_by_title: dict[str, list[dict]] = {}
    for gv in gamevals:
        r = report_one(gv)
        if r["ok"]:
            ok += 1
            print(f"OK   {gv:30s} '{r['title']}' dropversion={r['dropversion']!r} "
                  f"{len(r['rs2'])} rs2-lines skipped={len(r['skipped'])} tertiary={r['tertiary_present']}")
            for s in r["skipped"]:
                print(f"       skip: {s}")
            if not r["skipped"]:
                clean_by_title.setdefault(r["title"], []).append(r)
        else:
            print(f"FAIL {gv:30s} {r['reason']}")
    print(f"\n{ok}/{len(gamevals)} resolved cleanly, {sum(len(v) for v in clean_by_title.values())} clean (0-skip)")

    if args.write:
        written = 0
        for title, results in clean_by_title.items():
            path = write_group(title, results)
            if path:
                written += 1
                print(f"wrote {os.path.relpath(path, REPO)} ({len(results)} gameval(s))")
        print(f"\n{written} files written, covering {sum(len(v) for v in clean_by_title.values())} npcs")


if __name__ == "__main__":
    main()
