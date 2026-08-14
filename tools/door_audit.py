#!/usr/bin/env python3
"""Audit door/gate coverage in OSRS-Content and emit the work queue.

`server/scripts/doors/configs/doors.loc` names `tools/door_import.py` as its
generator, and that file is gone from this tree — there is no way left to
regenerate the 784 pairings it produced, or to see what it missed. This is
its replacement: read-only, and scoped one step wider than a re-import,
because the gap left behind is not just "doors not yet imported" but
"doors nothing has ever looked at."

It joins four sources that each know part of the truth and none know all of
it:

  configs/all.loc(.compack)              the cache's own records: ops, ids
  server/scripts/doors/configs/doors.loc the existing swing pairings
  server/scripts/**/*.rs2                named `[oplocN,<loc>]` bindings
  maps/*.jl2                              where a loc is actually placed

A loc can only need fixing if it is *placed*: 62k cache records name
"door" or "gate", most of them never appear on a map square. Placement
counts also rank the queue — a door at 34 map squares matters more than one
at 1.

    tools/door_audit.py --tree OSRS-Content/osrs239-content
    tools/door_audit.py --tree OSRS-Content/osrs239-content --write-queue docs/DOORS_GATES_QUEUE.md
    tools/door_audit.py --tree OSRS-Content/osrs239-content --suggest-pairs

`--suggest-pairs` proposes an `open` partner for each uncovered Open/Close
loc, the way the reference importer did — by name. It is a proposal, not an
edit: nothing here writes to `doors.loc`. `mock230_pack -v` is the actual
gate (a closed half needs an Open op, an opened half needs to exist in the
cache), the same check the reference importer's header describes and the
same one a hand-added pairing has to clear.
"""
import argparse
import collections
import glob
import os
import re
import sys

DOOR_WORDS = ("door", "gate", "portcullis")
DOOR_DISPLAY_NAMES = {
    "door", "gate", "gates", "large door", "double door", "doorway",
    "portcullis", "cell door", "prison gate", "tent door", "big door",
    "huge door", "sturdy door", "wooden door", "metal door", "locked door",
    "iron gate", "wooden gate",
}
# Loc records that say "door"/"gate" but are furniture, not an opening in a
# wall — excluded so the queue is doors, not every container with a hinge.
EXCLUDE_WORDS = (
    "chest", "drawer", "wardrobe", "coffin", "cupboard", "crate", "trapdoor",
    "trap_door", "cabinet", "bookcase", "casket", "sarcophagus", "locker",
)
INTERACTIVE_OPS = {
    "open", "close", "pass-through", "pick-lock", "picklock", "push", "pull",
    "unlock", "quick-open", "go-through", "enter", "quick-enter", "lock",
}

OPLOC_BINDING_RE = re.compile(r"^\[(oploc\d|aplloc\d|opheldloc)\s*,\s*([A-Za-z0-9_]+)\s*\]")
OP_FIELD_RE = re.compile(r"^op([1-5])=(.*)$")
JL2_LINE_RE = re.compile(r"^\d+ \d+ \d+: (\d+) (\d+) (\d+)")


def parse_blocks(path):
    blocks, cur = {}, None
    with open(path, encoding="utf8", errors="replace") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("//"):
                continue
            if s.startswith("[") and s.endswith("]"):
                cur = s[1:-1]
                blocks[cur] = []
            elif cur is not None:
                blocks[cur].append(s)
    return blocks


def load_name_ids(tree):
    name2id = {}
    with open(os.path.join(tree, "configs", "all.loc.compack"), encoding="utf8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if "=" in line:
                i, n = line.split("=", 1)
                name2id[n] = int(i)
    return name2id


def load_script_bindings(tree):
    bound = collections.defaultdict(set)
    for p in glob.glob(os.path.join(tree, "server", "scripts", "**", "*.rs2"), recursive=True):
        rel = os.path.relpath(p, tree).replace("\\", "/")
        with open(p, encoding="utf8", errors="replace") as f:
            for line in f:
                m = OPLOC_BINDING_RE.match(line.strip())
                if m:
                    bound[m.group(2)].add(rel)
    return bound


def load_placements(tree):
    placed = collections.Counter()
    shapes = collections.defaultdict(collections.Counter)
    for p in glob.glob(os.path.join(tree, "maps", "*.jl2")):
        with open(p, encoding="utf8", errors="replace") as f:
            for line in f:
                m = JL2_LINE_RE.match(line)
                if m:
                    lid, shape = int(m.group(1)), int(m.group(2))
                    placed[lid] += 1
                    shapes[lid][shape] += 1
    return placed, shapes


def loc_ops(fields):
    out = {}
    for s in fields:
        m = OP_FIELD_RE.match(s)
        if m:
            out["op" + m.group(1)] = m.group(2)
    return out


def loc_display(fields):
    for s in fields:
        if s.startswith("name="):
            return s[5:]
    return ""


def is_door_record(name, disp):
    lname, ldisp = name.lower(), disp.lower()
    if any(w in lname or w in ldisp for w in EXCLUDE_WORDS):
        return False
    if ldisp in DOOR_DISPLAY_NAMES:
        return True
    return any(w in lname for w in DOOR_WORDS)


def build_rows(tree):
    name2id = load_name_ids(tree)
    cache = parse_blocks(os.path.join(tree, "configs", "all.loc"))
    doors = parse_blocks(os.path.join(tree, "server", "scripts", "doors", "configs", "doors.loc"))
    # doubledoors.loc uses door_left_*/door_right_* categories instead of
    # door_closed/door_opened, but it is the same pairing shape (category +
    # next_loc_stage) and a leaf named here is exactly as covered.
    doubledoors_path = os.path.join(tree, "server", "scripts", "doors", "configs", "doubledoors.loc")
    if os.path.exists(doubledoors_path):
        doors.update(parse_blocks(doubledoors_path))
    bound = load_script_bindings(tree)
    placed, shapes = load_placements(tree)

    rows = []
    for name, fields in cache.items():
        disp = loc_display(fields)
        if not is_door_record(name, disp):
            continue
        ops = loc_ops(fields)
        opvals = {v.lower() for v in ops.values()}
        cat = nxt = None
        if name in doors:
            for s in doors[name]:
                if s.startswith("category="):
                    cat = s.split("=", 1)[1]
                if s.startswith("param=next_loc_stage,"):
                    nxt = s.split(",", 1)[1]
        lid = name2id.get(name, -1)
        rows.append(dict(
            name=name, id=lid, disp=disp, ops=ops,
            interactive=bool(opvals & INTERACTIVE_OPS),
            cat=cat, nxt=nxt,
            placed=placed.get(lid, 0), shapes=dict(shapes.get(lid, {})),
            scripts=sorted(bound.get(name, [])),
        ))
    return rows


def classify(rows):
    buckets = collections.defaultdict(list)
    for r in rows:
        if r["placed"] == 0:
            buckets["unplaced"].append(r)
        elif r["cat"]:
            buckets["paired"].append(r)
        elif r["scripts"]:
            buckets["script_bound"].append(r)
        elif not r["interactive"]:
            buckets["inert"].append(r)
        else:
            buckets["gap"].append(r)
    return buckets


def _normalize(name):
    return name.lower().replace("_", "")


def build_norm_index(cache_names):
    """normalized (underscore-stripped, lowercased) name -> original name(s).

    The cache is not consistent about *where* the underscore sits between a
    door's stem and the word "open" (`roguesden_door_to_pub_open` vs
    `elfdooropen_reverse` vs `xbowscastledoor_open` for `xbows_castle_door`),
    so comparing with underscores stripped turns three different insertion
    conventions into one lookup.
    """
    idx = collections.defaultdict(list)
    for n in cache_names:
        idx[_normalize(n)].append(n)
    return idx


def suggest_partner(name, cache_names, norm_index=None):
    if norm_index is None:
        norm_index = build_norm_index(cache_names)
    norm_a = _normalize(name)

    # Exact suffix/prefix and closed/shut substitution — cheap, checked first.
    # "c" (bare, no separator) is a third convention seen on _l/_r gate pairs
    # (fai_varrock_gate_l -> fai_varrock_gate_lc, ii_fencegate_l -> _lc):
    # same model, closed offers Open, the "c" name offers Close.
    for s in ("open", "_open", "opened", "_opened", "_o", "c"):
        if name + s in cache_names:
            return name + s
    for pat, repls in ((r"closed", ("open", "opened")), (r"shut", ("open",))):
        if pat in name:
            for repl in repls:
                cand = name.replace(pat, repl)
                if cand in cache_names:
                    return cand

    # Insert "open"/"opened" at every character position of the normalized
    # name and see if the result is itself a real cache record. Catches any
    # underscore convention in one pass, at the cost of being a guess: the
    # caller must still validate with mock230_pack before landing anything
    # this returns.
    candidates = []
    for word in ("open", "opened"):
        for k in range(len(norm_a) + 1):
            cand_norm = norm_a[:k] + word + norm_a[k:]
            for hit in norm_index.get(cand_norm, ()):
                if hit != name:
                    candidates.append(hit)
    if candidates:
        # Prefer the shortest hit (closest to a pure insertion, not a
        # coincidental longer name that happens to contain the substring).
        candidates.sort(key=len)
        return candidates[0]
    return None


def cmd_summary(rows, buckets):
    print(f"door/gate-named loc records: {len(rows)}")
    print(f"  placed on a map:     {sum(len(v) for k, v in buckets.items() if k != 'unplaced')}")
    print(f"  paired (doors.loc):  {len(buckets['paired'])}")
    print(f"  script-bound:        {len(buckets['script_bound'])}")
    print(f"  inert (no op):       {len(buckets['inert'])}")
    print(f"  GAP (needs work):    {len(buckets['gap'])}")
    gap = sorted(buckets["gap"], key=lambda r: -r["placed"])
    print()
    print("gap, by placement count:")
    for r in gap[:80]:
        print(f"  {r['id']:>6} {r['name']:<44} x{r['placed']:<5} {r['disp']:<18} {r['ops']}")


def cmd_suggest_pairs(rows, buckets, tree):
    cache_names = set(load_name_ids(tree).keys())
    norm_index = build_norm_index(cache_names)
    gap = [r for r in buckets["gap"] if r["ops"].get("op1", "") in ("Open", "Close")]
    found, missing = [], []
    for r in gap:
        cand = suggest_partner(r["name"], cache_names, norm_index)
        (found if cand else missing).append((r, cand))
    print(f"gap with Open/Close on op1: {len(gap)}")
    print(f"  partner found by naming convention: {len(found)}")
    print(f"  no partner found (needs manual lookup): {len(missing)}")
    print()
    print("-- proposed pairs (verify with mock230_pack -v before landing) --")
    for r, cand in sorted(found, key=lambda x: -x[0]["placed"]):
        print(f"  [{r['name']}] -> [{cand}]  x{r['placed']}")
    print()
    print("-- unresolved, needs manual identification --")
    for r, _ in sorted(missing, key=lambda x: -x[0]["placed"]):
        print(f"  {r['id']:>6} {r['name']:<44} x{r['placed']:<5} {r['disp']:<18} {r['ops']}")


# ----------------------------------------------------------------------------
# Two-leaf detection
# ----------------------------------------------------------------------------
#
# A door with two leaves has to be told so. `doors.rs2` swings the leaf that
# was clicked and nothing else, so a pair wired as two `door_closed` records is
# a door that opens *half way* — one leaf swings, the other stays across the
# doorway. That is invisible to every check above: both halves are "paired",
# both have an `Open`, and `mock230_pack -v` is happy, because every id in the
# pairing resolves.
#
# What separates the two is geometry, and the map squares state it. Two leaves
# of one door sit on ADJACENT tiles, on the same shape, at the same angle, and
# the offset between them is `~door_close(angle, shape)` — the same vector
# `doors/scripts/door_procs.rs2` computes. That is a fact about the placement,
# not about the name, which is why it can be measured: the leaf the offset
# points AWAY from is the `door_left_*`/`gate_main_*` half and the one it points
# AT is the `door_right_*`/`gate_outer_*` half. Checked against all eleven
# clusters this tree already wires by hand (6 in `doubledoors.loc`, 5 gates in
# `doors.loc`): the derived side matches the authored category 11 times out of
# 11, so the rule reproduces every decision a human already made here.
#
# Confidence comes from requiring EVERY placement of both ids to participate.
# A record that is sometimes half of a pair and sometimes a lone door is not a
# double door, it is a coincidence of two doors in one wall, and it is reported
# as partial rather than proposed.
#
# Which of the two systems a confirmed pair belongs to is the one remaining
# judgement, and it is a model-id test, not a name test:
#
#   * `gate_main_*`/`gate_outer_*` (general_use/scripts/gates.rs2) — the long
#     fence gate, whose two leaves swing to the SAME side and end up in a line
#     perpendicular to the fence.
#   * `door_left_*`/`door_right_*` (doors/scripts/doubledoors.rs2) — everything
#     else, whose leaves swing apart.
#
# Kronos' `Door.java` states the same split as a model test and it is the only
# place either tree writes the rule down:
#
#     if (name.contains("gate")) {
#         def.gateType = true;
#         if (def.modelIds[0] == 7371 ||
#             (def.modelIds[0] == 966 && def.modelIds[1] == 967 && def.modelIds[2] == 968))
#             def.longGate = true;
#
# LostCity agrees from the other direction: every one of its four
# `gate_main_closed` records is `model=fence_gateclosedl`, which is 966/967/968
# here — and `membergatel`/`membergater`, a metal gate, is `door_left_closed`
# in its `doubledoors.loc` rather than a gate. So "gate" in the name is not the
# test; the wooden-fence-gate model is.
LONG_GATE_MODEL_HEADS = ((7371,), (966, 967, 968))

# `~door_close(angle, shape)` for the two shapes it answers, as (dx, dz).
# Verbatim from doors/scripts/door_procs.rs2 — angles are ^loc_west=0,
# ^loc_north=1, ^loc_east=2, ^loc_south=3.
DOOR_CLOSE_STRAIGHT = {0: (0, 1), 1: (1, 0), 2: (0, -1), 3: (-1, 0)}
DOOR_CLOSE_DIAGONAL = {0: (1, 0), 1: (0, -1), 2: (-1, 0), 3: (0, 1)}
SHAPE_WALL_STRAIGHT = 0
SHAPE_WALL_DIAGONAL = 9


def door_close_delta(shape, angle):
    if shape == SHAPE_WALL_STRAIGHT:
        return DOOR_CLOSE_STRAIGHT.get(angle)
    if shape == SHAPE_WALL_DIAGONAL:
        return DOOR_CLOSE_DIAGONAL.get(angle)
    return None


def load_placement_tiles(tree):
    """(level, x, z) -> [(loc id, shape, angle)] and loc id -> [placements].

    Separate from `load_placements` because that one only counts, and the
    side of a leaf cannot be derived from a count.
    """
    tiles = collections.defaultdict(list)
    by_id = collections.defaultdict(list)
    for p in glob.glob(os.path.join(tree, "maps", "*.jl2")):
        base = os.path.basename(p)[1:-4]
        mx, mz = (int(v) for v in base.split("_"))
        with open(p, encoding="utf8", errors="replace") as f:
            for line in f:
                m = re.match(r"^(\d+) (\d+) (\d+): (\d+) (\d+) (\d+)", line)
                if not m:
                    continue
                level, lx, lz, lid, shape, angle = (int(v) for v in m.groups())
                x, z = mx * 64 + lx, mz * 64 + lz
                tiles[(level, x, z)].append((lid, shape, angle))
                by_id[lid].append((level, x, z, shape, angle))
    return tiles, by_id


def load_overlay_locs(tree):
    """loc name -> (source path, category, next_loc_stage) over every server .loc."""
    out = {}
    for p in sorted(glob.glob(os.path.join(tree, "server", "scripts", "**", "*.loc"), recursive=True)):
        rel = os.path.relpath(p, tree).replace("\\", "/")
        for name, fields in parse_blocks(p).items():
            if name in out:
                continue
            cat = nxt = None
            for s in fields:
                if s.startswith("category="):
                    cat = s.split("=", 1)[1]
                elif s.startswith("param=next_loc_stage,"):
                    nxt = s.split(",", 1)[1]
            out[name] = (rel, cat, nxt)
    return out


def loc_models(fields):
    """The model ids a loc record names, in shape order."""
    out = []
    for s in fields:
        m = re.match(r"^shape\d+=(\d+),(.*)$", s)
        if m:
            out.extend(int(v) for v in m.group(2).split(","))
    return out


def find_leaf_pairs(tree, cache, name2id, id2name):
    """Ordered (left name, right name, shape) -> observed placements.

    `left` is the leaf `~door_close` points away from, i.e. the one whose own
    coordinate plus the close vector lands on its partner.
    """
    tiles, by_id = load_placement_tiles(tree)
    swings = {
        n for n, fields in cache.items()
        if {v.lower() for v in loc_ops(fields).values()} & {"open", "close"}
    }
    pairs = collections.Counter()
    paired_placements = collections.Counter()
    for lid, placements in by_id.items():
        name = id2name.get(lid)
        if name not in swings:
            continue
        for level, x, z, shape, angle in placements:
            delta = door_close_delta(shape, angle)
            if delta is None:
                continue
            hit = False
            for dx, dz, forward in ((delta[0], delta[1], True), (-delta[0], -delta[1], False)):
                for lid2, shape2, angle2 in tiles.get((level, x + dx, z + dz), ()):
                    other = id2name.get(lid2)
                    if lid2 == lid or shape2 != shape or angle2 != angle or other not in swings:
                        continue
                    hit = True
                    if forward:
                        pairs[(name, other, shape)] += 1
            if hit:
                paired_placements[lid] += 1
    return pairs, paired_placements, by_id


def cmd_suggest_double_leaf(tree):
    name2id = load_name_ids(tree)
    id2name = {v: k for k, v in name2id.items()}
    cache = parse_blocks(os.path.join(tree, "configs", "all.loc"))
    overlay = load_overlay_locs(tree)
    pairs, paired_placements, by_id = find_leaf_pairs(tree, cache, name2id, id2name)

    def is_long_gate(name):
        models = loc_models(cache.get(name, []))
        return any(tuple(models[:len(head)]) == head for head in LONG_GATE_MODEL_HEADS)

    gates, doubles, partial, skipped = [], [], [], []
    for (left, right, shape), seen in sorted(pairs.items(), key=lambda kv: -kv[1]):
        lid, rid = name2id[left], name2id[right]
        full = (paired_placements[lid] == len(by_id[lid])
                and paired_placements[rid] == len(by_id[rid]))
        lsrc, lcat, lnext = overlay.get(left, (None, None, None))
        rsrc, rcat, rnext = overlay.get(right, (None, None, None))
        row = dict(left=left, right=right, shape=shape, seen=seen,
                   lcat=lcat, rcat=rcat, lnext=lnext, rnext=rnext,
                   lsrc=lsrc, rsrc=rsrc,
                   lplaced=len(by_id[lid]), rplaced=len(by_id[rid]),
                   lpaired=paired_placements[lid], rpaired=paired_placements[rid])
        if not full:
            partial.append(row)
        elif (lcat or "").startswith(("door_left", "door_right", "gate_")) or \
             (rcat or "").startswith(("door_left", "door_right", "gate_")):
            skipped.append((row, "already wired as a two-leaf door"))
        elif lcat is None or rcat is None or lnext is None or rnext is None:
            skipped.append((row, f"no single-door pairing to convert ({lcat}/{rcat})"))
        elif "reverse" in left or "reverse" in right:
            skipped.append((row, "reverse double door — that variant is not ported"))
        elif is_long_gate(left) and is_long_gate(right):
            gates.append(row)
        else:
            doubles.append(row)

    print(f"placed two-leaf clusters: {len(pairs)}")
    print(f"  long fence gates to rewire (gate_main/gate_outer): {len(gates)}")
    print(f"  double doors to rewire (door_left/door_right):     {len(doubles)}")
    print(f"  already wired or not convertible:                  {len(skipped)}")
    print(f"  partially paired — not proposed, look by hand:     {len(partial)}")
    print()
    for title, rows_ in (("long fence gates", gates), ("double doors", doubles)):
        print(f"-- {title} --")
        for r in rows_:
            print(f"  x{r['seen']:<3} {r['left']:<44} -> {r['lnext']}")
            print(f"        {r['right']:<44} -> {r['rnext']}   [{r['lsrc']}]")
        print()
    print("-- not proposed --")
    for r, why in skipped:
        print(f"  {r['left']:<42} + {r['right']:<42} :: {why}")
    for r in partial:
        print(f"  {r['left']:<42} + {r['right']:<42} :: partial "
              f"({r['lpaired']}/{r['lplaced']}, {r['rpaired']}/{r['rplaced']})")


QUEUE_HEADER = """# Doors & gates coverage queue

Generated by `tools/door_audit.py`, which replaces `tools/door_import.py`
(named in `doors.loc`'s header, missing from this tree since before this
queue existed — there was no way to regenerate its output or see what it
missed). Re-run the tool after any `doors.loc` or `server/scripts/**/*.rs2`
change; this file is derived, not authored.

A loc only appears here if it is **placed on a map** — the cache names
thousands of things "door" or "gate" that are furniture (excluded) or never
placed (excluded); neither can be clicked.

Coverage means one of four things now, in descending order of how OSRS-like
the result is:

1. **Paired in `doors.loc`/`doubledoors.loc`** — swings via the generic
   `_door_closed`/`_door_opened` handlers (`doors/scripts/doors.rs2`) or,
   for a confirmed two-leaf door, the `_door_left_*`/`_door_right_*`
   handlers (`doors/scripts/doubledoors.rs2`, ported from LostCity, which
   also swings the opposite leaf). This is what a real door does.
2. **Bound to a named script** (`[oplocN,<name>]` somewhere in
   `server/scripts`) — a quest, minigame, or skill-specific mechanism
   (`skill_thieving/scripts/doors/locked_door.rs2`, `general_use/scripts/
   gates.rs2`, and so on).
3. **Walk-through fallback** (`general_use/scripts/
   door_walkthrough_fallback.rs2`) — a door with an Open action but no
   discoverable opened variant anywhere in the cache under any naming
   convention this tool tried. The door cannot visually swing (there is
   nothing to swing *to*), so the player passes through instead of being
   stuck at a scenery loc with a dead click. Same fallback this tree
   already used for memberfencegate_l/r, thieving locked doors, and
   several quests before this pass existed.
4. **Eternal-lock fallback** (`general_use/scripts/
   door_locked_fallback.rs2`) — same situation as (3), but the loc's own
   name says "locked" and letting the player through for free would be
   wrong. Responds "It's locked." and nothing else; several of these carry
   a Pick-lock op that really wants `skill_thieving/configs/doors/
   locked_door.dbrow` data (level, tool, success chance) this pass does not
   have the source for.
5. **Correctly inert** (no Open/Close/Enter-style op) — decorative archway
   or scenery; not a gap.

**Out of scope for this pass:** `Enter`/`Pass-through`/`Go-through` passage
doorways ({enter_count} locs) — a teleport-through mechanic, not a swing;
tracked separately below rather than silently dropped.

"""


def write_queue(rows, buckets, path):
    gap = sorted(buckets["gap"], key=lambda r: -r["placed"])
    swing_gap = [r for r in gap if r["ops"].get("op1") in ("Open", "Close", "Push", "Pull", "Unlock", "Pick-lock", "Picklock")]
    enter_gap = [r for r in gap if r["ops"].get("op1") in ("Enter", "Pass-through", "Go-through")]
    other_gap = [r for r in gap if r not in swing_gap and r not in enter_gap]

    lines = [QUEUE_HEADER.format(enter_count=len(enter_gap))]
    lines.append("## Coverage snapshot\n")
    lines.append("| | count |")
    lines.append("|---|---|")
    lines.append(f"| door/gate-named cache records | {len(rows)} |")
    lines.append(f"| placed on a map | {sum(len(v) for k, v in buckets.items() if k != 'unplaced')} |")
    lines.append(f"| paired in doors.loc | {len(buckets['paired'])} |")
    lines.append(f"| bound to a named script | {len(buckets['script_bound'])} |")
    lines.append(f"| inert (no interactive op) | {len(buckets['inert'])} |")
    lines.append(f"| **gap: swing (Open/Close/etc.)** | **{len(swing_gap)}** |")
    lines.append(f"| gap: Enter/Pass-through (out of scope) | {len(enter_gap)} |")
    lines.append(f"| gap: other op | {len(other_gap)} |")
    lines.append("")

    lines.append("## Swing-door gap — in scope\n")
    lines.append("| id | loc | display | placements | ops | status |")
    lines.append("|---|---|---|---|---|---|")
    for r in swing_gap:
        opstr = ", ".join(f"{k}={v}" for k, v in sorted(r["ops"].items()))
        lines.append(f"| {r['id']} | `{r['name']}` | {r['disp']} | {r['placed']} | {opstr} | pending |")
    lines.append("")

    lines.append("## Enter/Pass-through doorways — out of scope, tracked only\n")
    lines.append("| id | loc | display | placements | ops |")
    lines.append("|---|---|---|---|---|")
    for r in enter_gap:
        opstr = ", ".join(f"{k}={v}" for k, v in sorted(r["ops"].items()))
        lines.append(f"| {r['id']} | `{r['name']}` | {r['disp']} | {r['placed']} | {opstr} |")
    lines.append("")

    if other_gap:
        lines.append("## Other op signature — needs triage\n")
        lines.append("| id | loc | display | placements | ops |")
        lines.append("|---|---|---|---|---|")
        for r in other_gap:
            opstr = ", ".join(f"{k}={v}" for k, v in sorted(r["ops"].items()))
            lines.append(f"| {r['id']} | `{r['name']}` | {r['disp']} | {r['placed']} | {opstr} |")
        lines.append("")

    with open(path, "w", encoding="utf8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {path}: {len(swing_gap)} swing gaps, {len(enter_gap)} out-of-scope, {len(other_gap)} other")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", required=True, help="osrs239-content tree")
    ap.add_argument("--write-queue", metavar="PATH", help="write the markdown queue doc")
    ap.add_argument("--suggest-pairs", action="store_true", help="propose open/close partners by naming convention")
    ap.add_argument("--suggest-double-leaf", action="store_true",
                    help="find doors placed as two leaves and wired as two single doors")
    args = ap.parse_args()

    if args.suggest_double_leaf:
        cmd_suggest_double_leaf(args.tree)
        return 0

    rows = build_rows(args.tree)
    buckets = classify(rows)

    if args.suggest_pairs:
        cmd_suggest_pairs(rows, buckets, args.tree)
    elif args.write_queue:
        write_queue(rows, buckets, args.write_queue)
    else:
        cmd_summary(rows, buckets)


if __name__ == "__main__":
    sys.exit(main())
