#!/usr/bin/env python3
"""Audit every varp/varbit a loc record transforms on, and emit the work queue.

A *multiloc* is a loc record that carries `multivarbit=` (or `multivarp=`) and a
`multiloc1..N` list: the client and the server both resolve it by reading that
variable for the player and indexing the list, so the record placed on the map
square is a base that renders as whichever child the variable selects. The rule
is `mock230_scene.c:mock230_scene_resolve_loc` / `VarPManager_ResolveTransform`
— **`multiloc1` is value 0** — and an index that is out of range or names -1
falls to the *last* entry, which is usually -1, meaning nothing is drawn.

That last clause is why this audit exists. A multiloc whose value-0 child is -1
is invisible until something sets the variable. If no server script ever writes
it, the loc is not "in its default state", it is *permanently absent from the
world*, and there is nothing at the map square to click that would tell you so.
1,634 placed bases are in exactly that position in this tree.

It joins four sources that each know part of the truth and none know all of it:

  configs/all.loc(.compack)     which locs transform, on what, into what
  configs/all.varbit(.compack)  the carrier varp and bit range behind each name
  maps/*.jl2                    where a base loc is actually placed
  server/scripts/**/*.rs2       who reads the variable, who writes it, and
                                which locs have an `[oplocN,...]` binding

Placement is what separates a real gap from cache noise: 62k loc records exist,
4,675 of them transform, and a record nothing places cannot be wrong on screen.
Op bindings are what rank the rest — a variable nothing writes whose loc the
content *already answers clicks for* is a live bug today (the script runs, the
loc never changes), while one with no binding is unwritten content, which is a
different and much larger job.

    tools/loc_var_audit.py --tree OSRS-Content/osrs239-content
    tools/loc_var_audit.py --tree OSRS-Content/osrs239-content --bucket op_bound_gap
    tools/loc_var_audit.py --tree OSRS-Content/osrs239-content --var pest_lander_difficulty
    tools/loc_var_audit.py --tree OSRS-Content/osrs239-content --write-queue docs/LOC_VARS_QUEUE.md
    tools/loc_var_audit.py --tree OSRS-Content/osrs239-content --json /tmp/loc_vars.json

Read-only. Nothing here writes to the content tree; `--write-queue` writes the
one doc it is given.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

# `plane x z: loc shape [angle]` -- the angle is OMITTED when it is 0, which is
# a third of this tree's placements. Same regex as tools/door_audit.py, and for
# the same reason: requiring the third field silently drops them.
JL2_LINE_RE = re.compile(r"^\d+ \d+ \d+: (\d+) (\d+)(?: (\d+))?")
# `[oploc1,loc_name]`, `[aplloc3,loc_name]`, ... — the server-side answer to a
# click on a loc. `opheldloc`/`oplocu` are the use-item-on-loc forms.
BINDING_RE = re.compile(
    r"^\[(oploc\d|aploc\d|aplloc\d|oplocu|aplocu|opheldloc|oplocc|aplocc)\s*,"
    r"\s*([A-Za-z0-9_:]+)\s*\]")
# A RuneScript variable reference. `%name` is the whole syntax for both varps
# and varbits — which of the two it resolves to is the compiler's business, and
# for this audit's purpose (does anything touch it at all) the distinction does
# not matter, because a name is either a varp or a varbit, never both:
# validate_name_layers() refuses to start a tree where one is.
VAR_RE = re.compile(r"%([a-zA-Z0-9_]+)")
VAR_WRITE_RE = re.compile(r"%([a-zA-Z0-9_]+)\s*=(?!=)")
# `varbit_1234` / `varp_1234` is what cachepack writes when the cache's gameval
# archive does not name an id — a placeholder, not a name.
PLACEHOLDER_RE = re.compile(r"^var(bit|p)_\d+$")
# `loc_change(<loc>, <dur>)` / `loc_add(<coord>, <loc>, ...)` — the *other* way
# to put a loc into a state, and the one the LostCity-sourced content in this
# tree mostly uses. It swaps the loc for everyone rather than transforming it
# per player, so a multiloc driven this way is implemented, not broken, and
# rewriting it onto the cache's varbit would trade working world-scoped state
# for per-player state. Both call shapes name the loc as an identifier.
LOC_CHANGE_RE = re.compile(r"loc_change\s*\(\s*([A-Za-z0-9_:]+)")
LOC_ADD_RE = re.compile(r"loc_add\s*\([^,]*,\s*([A-Za-z0-9_:]+)")


def parse_compack(path):
    """`id=name` lines -> {id: name}. Trailing `// cache: x` notes are dropped."""
    out = {}
    with open(path, encoding="utf8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or "=" not in line:
                continue
            i, name = line.split("=", 1)
            out[int(i)] = name.split("//")[0].strip()
    return out


def parse_blocks(path):
    """`[name]` + `key=value` config dump -> [(name, {key: [values]})], in file order.

    Values are lists because a loc block repeats `multiloc1..N` and `op1..5` as
    separate keys but `models=` as one comma list; keeping every occurrence is
    the only form that loses nothing.
    """
    blocks, cur = [], None
    with open(path, encoding="utf8", errors="replace") as f:
        for line in f:
            s = line.split("//")[0].strip()
            if not s:
                continue
            if s.startswith("[") and s.endswith("]"):
                cur = (s[1:-1], collections.defaultdict(list))
                blocks.append(cur)
            elif cur is not None and "=" in s:
                k, v = s.split("=", 1)
                cur[1][k.strip()].append(v.strip())
    return blocks


def load_placements(tree):
    placed = collections.Counter()
    for p in glob.glob(os.path.join(tree, "maps", "*.jl2")):
        square = os.path.basename(p)[1:-4]
        with open(p, encoding="utf8", errors="replace") as f:
            for line in f:
                m = JL2_LINE_RE.match(line)
                if m:
                    placed[int(m.group(1))] += 1
    return placed


def load_squares(tree):
    """loc id -> the map squares it is placed on, for locating a gap in-world."""
    squares = collections.defaultdict(set)
    for p in glob.glob(os.path.join(tree, "maps", "*.jl2")):
        square = os.path.basename(p)[1:-4]
        with open(p, encoding="utf8", errors="replace") as f:
            for line in f:
                m = JL2_LINE_RE.match(line)
                if m:
                    squares[int(m.group(1))].add(square)
    return squares


def load_varp_decls(tree):
    """varp name -> {transmit, scope, protect} from server/scripts/**/*.varp.

    A carrier the content has not declared is invisible to the client: the
    server's varp send (`mock230_world.c`, `if( !def || !def->transmit )`) drops
    it, so setting a varbit based on it changes the server's copy and nothing
    else. A multiloc reading that varbit never transforms, however correct the
    write was — which is why this is joined in rather than left to a reader.
    """
    decls = {}
    cur = None
    for p in glob.glob(os.path.join(tree, "server", "scripts", "**", "*.varp"),
                       recursive=True):
        rel = os.path.relpath(p, tree).replace("\\", "/")
        with open(p, encoding="utf8", errors="replace") as f:
            for line in f:
                s = line.split("//")[0].strip()
                if not s:
                    continue
                if s.startswith("[") and s.endswith("]"):
                    cur = s[1:-1]
                    decls[cur] = {"file": rel, "transmit": False, "scope": "temp"}
                elif cur and "=" in s:
                    k, v = s.split("=", 1)
                    if k.strip() == "transmit":
                        decls[cur]["transmit"] = v.strip() == "yes"
                    elif k.strip() == "scope":
                        decls[cur]["scope"] = v.strip()
    return decls


def load_scripts(tree):
    """(reads, writes, files-per-var, loc name -> binding files)"""
    reads, writes = collections.Counter(), collections.Counter()
    files = collections.defaultdict(set)
    bound = collections.defaultdict(set)
    swapped = collections.defaultdict(set)
    root = os.path.join(tree, "server", "scripts")
    for p in glob.glob(os.path.join(root, "**", "*.rs2"), recursive=True):
        rel = os.path.relpath(p, root).replace("\\", "/")
        with open(p, encoding="utf8", errors="replace") as f:
            for line in f:
                s = line.strip()
                m = BINDING_RE.match(s)
                if m:
                    bound[m.group(2)].add(rel)
                code = line.split("//")[0]
                for m in VAR_RE.finditer(code):
                    reads[m.group(1)] += 1
                    files[m.group(1)].add(rel)
                for m in VAR_WRITE_RE.finditer(code):
                    writes[m.group(1)] += 1
                for rx in (LOC_CHANGE_RE, LOC_ADD_RE):
                    for m in rx.finditer(code):
                        swapped[m.group(1)].add(rel)
    return reads, writes, files, bound, swapped


def build_rows(tree):
    cfg = os.path.join(tree, "configs")
    loc_id_of = {}
    for i, n in parse_compack(os.path.join(cfg, "all.loc.compack")).items():
        loc_id_of.setdefault(n, i)
    varbit_id_of = {n: i for i, n in parse_compack(os.path.join(cfg, "all.varbit.compack")).items()}
    varp_id_of = {n: i for i, n in parse_compack(os.path.join(cfg, "all.varp.compack")).items()}

    varbit_def = {}
    for name, fields in parse_blocks(os.path.join(cfg, "all.varbit")):
        varbit_def[name] = (
            fields["basevar"][0] if fields["basevar"] else None,
            int(fields["startbit"][0]) if fields["startbit"] else None,
            int(fields["endbit"][0]) if fields["endbit"] else None,
        )

    placed = load_placements(tree)
    squares = load_squares(tree)
    reads, writes, var_files, bound, swapped = load_scripts(tree)
    varp_decls = load_varp_decls(tree)

    refs = collections.defaultdict(list)
    for lname, fields in parse_blocks(os.path.join(cfg, "all.loc")):
        vb = fields.get("multivarbit")
        vp = fields.get("multivarp")
        if not vb and not vp:
            continue
        variants, i = [], 1
        while f"multiloc{i}" in fields:
            variants.append(fields[f"multiloc{i}"][0])
            i += 1
        lid = loc_id_of.get(lname, -1)
        rec = dict(
            loc=lname, loc_id=lid,
            display=fields["name"][0] if fields["name"] else "",
            variants=variants,
            placed=placed.get(lid, 0),
            squares=sorted(squares.get(lid, ())),
            # value 0 draws nothing: this base is absent from the world until
            # something sets the variable.
            hidden_at_zero=bool(variants) and variants[0] == "-1",
            bindings=sorted(bound.get(lname, ())),
            variant_bindings=sorted({f for v in variants for f in bound.get(v, ())}),
            swapped_by=sorted({f for v in [lname] + variants for f in swapped.get(v, ())}),
        )
        if vb:
            refs[("varbit", vb[0])].append(rec)
        if vp:
            refs[("varp", vp[0])].append(rec)

    rows = []
    for (kind, name), locs in refs.items():
        base, sb, eb = varbit_def.get(name, (None, None, None))
        rows.append(dict(
            kind=kind, name=name,
            id=(varbit_id_of if kind == "varbit" else varp_id_of).get(name, -1),
            placeholder_name=bool(PLACEHOLDER_RE.match(name)),
            basevar=base, startbit=sb, endbit=eb,
            states=None if sb is None else 1 << (eb - sb + 1),
            reads=reads.get(name, 0), writes=writes.get(name, 0),
            # A whole-varp write to the carrier sets every bit in it, so a
            # varbit nothing names by itself can still be fully implemented:
            # barrows' door layout is one `%barrows = ^barrows_tunnel_cfg3`,
            # declared `wholewrite=allow` in barrows.varp. Counting only
            # by-name writes reported all 16 doors as gaps.
            carrier_writes=writes.get(base, 0) if base else 0,
            # The carrier a varbit lives in, and whether the content declares it
            # transmit=yes. Undeclared or transmit=no means the client never
            # learns the value, so the multiloc cannot transform no matter who
            # writes it.
            carrier_declared=(base or name) in varp_decls,
            carrier_transmit=varp_decls.get(base or name, {}).get("transmit", False),
            carrier_decl_file=varp_decls.get(base or name, {}).get("file"),
            carrier_files=sorted(var_files.get(base, ())) if base else [],
            files=sorted(var_files.get(name, ())),
            loc_count=len(locs),
            placements=sum(l["placed"] for l in locs),
            hidden_bases=sum(1 for l in locs if l["hidden_at_zero"] and l["placed"]),
            op_bound=sorted({f for l in locs for f in l["bindings"] + l["variant_bindings"]}),
            swapped_by=sorted({f for l in locs for f in l["swapped_by"]}),
            locs=locs,
        ))
    rows.sort(key=lambda r: (-r["placements"], -r["loc_count"], r["name"]))
    return rows


def classify(row):
    """One bucket per row, most-actionable first.

    The order is the point: a variable can be several of these at once, and the
    bucket a row lands in is the *reason to look at it*, not a taxonomy.
    """
    if row["placements"] == 0:
        return "unplaced"          # cache record nothing puts in the world
    if row["writes"]:
        return "implemented"       # some script sets it by name
    if row["carrier_writes"]:
        return "carrier_written"   # set as part of a whole-varp write
    if row["swapped_by"]:
        return "loc_change_driven"  # state produced by swapping the loc instead
    if row["op_bound"]:
        return "op_bound_gap"      # content answers clicks; nothing sets the state
    if row["reads"]:
        return "read_only"         # read as a condition, never set here
    return "unwritten"             # no server content at all


BUCKET_ORDER = ["op_bound_gap", "read_only", "unwritten", "loc_change_driven",
                "carrier_written", "implemented", "unplaced"]
BUCKET_BLURB = {
    "op_bound_gap": "nothing writes the variable, but its locs already have server op bindings — live gaps",
    "read_only": "read as a condition somewhere, never written — the state has no producer",
    "unwritten": "no server content references the variable at all",
    "loc_change_driven": "state produced by loc_change/loc_add on the variants instead of the variable",
    "carrier_written": "set only as a side effect of a whole-varp write to its carrier",
    "implemented": "at least one script writes it by name",
    "unplaced": "no base loc is placed on any map square",
}


def owner(name):
    """Coarse owning system, from the gameval name's first token.

    Purely for grouping the queue. The cache's names are prefixed by quest or
    system (`sote_*`, `barrows_*`, `hunt_pitfall_*`), so the first underscore
    token is a good-enough owner for a work queue and wrong for nothing else.
    """
    return name.split("_")[0]


def cmd_summary(rows, args):
    buckets = collections.defaultdict(list)
    for r in rows:
        buckets[classify(r)].append(r)
    total_locs = sum(r["loc_count"] for r in rows)
    print(f"loc records that transform on a variable: {total_locs}")
    print(f"  distinct varbits: {sum(1 for r in rows if r['kind'] == 'varbit')}")
    print(f"  distinct varps:   {sum(1 for r in rows if r['kind'] == 'varp')}")
    ph = [r for r in rows if r["placeholder_name"]]
    print(f"  unnamed in the cache (varbit_<id>): {len(ph)}"
          + (" -> " + ", ".join(r["name"] for r in ph[:8]) if ph else ""))
    print()
    for b in BUCKET_ORDER:
        rs = buckets.get(b, [])
        print(f"  {b:<14} {len(rs):>5}   locs={sum(r['loc_count'] for r in rs):<6}"
              f" placements={sum(r['placements'] for r in rs):<7} {BUCKET_BLURB[b]}")
    print()
    gaps = buckets.get("op_bound_gap", [])
    print(f"top {min(args.top, len(gaps))} op-bound gaps by placement count:")
    for r in gaps[:args.top]:
        print(f"  {r['kind']:6} {r['id']:>6} {r['name']:<38} placed={r['placements']:<5}"
              f" locs={r['loc_count']:<4} hidden={r['hidden_bases']:<4} {r['op_bound'][0]}")


def cmd_bucket(rows, args):
    rs = [r for r in rows if classify(r) == args.bucket]
    print(f"{args.bucket}: {len(rs)} variables ({BUCKET_BLURB[args.bucket]})")
    for r in rs[:args.top]:
        print(f"\n  {r['kind']} {r['id']} {r['name']}  "
              f"[{r['basevar']} bits {r['startbit']}..{r['endbit']}, {r['states']} states]")
        print(f"    placed={r['placements']} locs={r['loc_count']} "
              f"hidden_at_zero={r['hidden_bases']} reads={r['reads']} writes={r['writes']}")
        for f in r["op_bound"][:6]:
            print(f"    bound by {f}")
        for l in sorted(r["locs"], key=lambda l: -l["placed"])[:4]:
            print(f"    loc {l['loc_id']:>6} {l['loc']} x{l['placed']} "
                  f"{'(hidden at 0)' if l['hidden_at_zero'] else ''} "
                  f"{','.join(l['squares'][:4])}")


def cmd_var(rows, args):
    hits = [r for r in rows if r["name"] == args.var or str(r["id"]) == args.var]
    if not hits:
        print(f"no loc transforms on {args.var!r}", file=sys.stderr)
        return 1
    for r in hits:
        print(json.dumps(r, indent=1))
    return 0


def cmd_check_carriers(rows):
    """Exit non-zero for any loc-driving varbit the content writes whose carrier
    the client never sees.

    This is the check the tree could not make from a comment. A varbit write is
    correct code, compiles, runs, and updates the server's copy; if the carrier
    varp is not declared `transmit=yes` in some `server/scripts/**/*.varp`, the
    value stops there and the multiloc never transforms. Nothing else in the
    build says so — sscompile only cares that the varbit resolves, and the
    runtime drops the packet silently in `mock230_world_mark_varp`.
    """
    bad = [r for r in rows
           if r["kind"] == "varbit" and (r["writes"] or r["carrier_writes"])
           and not r["carrier_transmit"]]
    if not bad:
        print("carriers: every written loc-transform varbit reaches the client")
        return 0
    carriers = collections.defaultdict(list)
    for r in bad:
        carriers[r["basevar"]].append(r)
    print(f"carriers: {len(bad)} written loc-transform varbit(s) in "
          f"{len(carriers)} carrier(s) the client never sees\n")
    for name, rs in sorted(carriers.items(), key=lambda kv: -sum(r["placements"] for r in kv[1])):
        state = "declared transmit=no" if rs[0]["carrier_declared"] else "undeclared"
        print(f"  [{name}] {state} — {len(rs)} varbit(s), "
              f"{sum(r['placements'] for r in rs)} placement(s)")
        for r in sorted(rs, key=lambda r: -r["placements"])[:4]:
            print(f"      {r['name']} (written by {', '.join(r['files'][:1]) or 'a carrier write'})")
    print("\nDeclare each in a server/scripts/**/*.varp with transmit=yes — see "
          "server/scripts/general/configs/loc_transform_carriers.varp.")
    return 1


def write_queue(rows, path):
    buckets = collections.defaultdict(list)
    for r in rows:
        buckets[classify(r)].append(r)
    out = []
    out.append("# Loc transform variables — work queue\n")
    out.append("Generated by `tools/loc_var_audit.py --write-queue`. Do not hand-edit;\n"
               "re-run it after landing content and the counts move on their own.\n")
    out.append(f"\n{sum(r['loc_count'] for r in rows)} loc records in `cache.osrs239` transform on a "
               f"variable: {sum(1 for r in rows if r['kind'] == 'varbit')} varbits and "
               f"{sum(1 for r in rows if r['kind'] == 'varp')} varps.\n")
    out.append("\n| bucket | vars | locs | placements | meaning |\n|---|---:|---:|---:|---|\n")
    for b in BUCKET_ORDER:
        rs = buckets.get(b, [])
        out.append(f"| `{b}` | {len(rs)} | {sum(r['loc_count'] for r in rs)} | "
                   f"{sum(r['placements'] for r in rs)} | {BUCKET_BLURB[b]} |\n")

    for b in ("op_bound_gap", "read_only"):
        rs = buckets.get(b, [])
        out.append(f"\n## `{b}` — {len(rs)} variables\n\n{BUCKET_BLURB[b]}.\n")
        by_owner = collections.defaultdict(list)
        for r in rs:
            by_owner[owner(r["name"])].append(r)
        for own, group in sorted(by_owner.items(),
                                 key=lambda kv: -sum(r["placements"] for r in kv[1])):
            out.append(f"\n### {own} — {len(group)} var(s), "
                       f"{sum(r['placements'] for r in group)} placements\n\n")
            out.append("| id | name | bits | states | locs | placed | hidden@0 | bound by |\n"
                       "|---:|---|---|---:|---:|---:|---:|---|\n")
            for r in sorted(group, key=lambda r: -r["placements"]):
                bits = (f"{r['basevar']} {r['startbit']}..{r['endbit']}"
                        if r["basevar"] else "(varp)")
                files = ", ".join(f"`{f}`" for f in r["op_bound"][:3]) or "—"
                out.append(f"| {r['id']} | `{r['name']}` | {bits} | {r['states'] or ''} | "
                           f"{r['loc_count']} | {r['placements']} | {r['hidden_bases']} | {files} |\n")
    with open(path, "w", encoding="utf8") as f:
        f.write("".join(out))
    print(f"wrote {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", default="OSRS-Content/osrs239-content")
    ap.add_argument("--bucket", choices=BUCKET_ORDER)
    ap.add_argument("--var", help="name or id of one variable, as JSON")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--json", help="write the whole join to this path")
    ap.add_argument("--write-queue", help="write the markdown work queue to this path")
    ap.add_argument("--check-carriers", action="store_true",
                    help="exit 1 if a written loc-transform varbit sits in a carrier "
                         "the content never declares transmit=yes")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(args.tree, "configs")):
        print(f"no content tree at {args.tree!r}", file=sys.stderr)
        return 2
    rows = build_rows(args.tree)
    for r in rows:
        r["bucket"] = classify(r)
    if args.json:
        with open(args.json, "w", encoding="utf8") as f:
            json.dump(rows, f, indent=1)
        print(f"wrote {args.json}")
    if args.write_queue:
        write_queue(rows, args.write_queue)
    if args.check_carriers:
        return cmd_check_carriers(rows)
    if args.var:
        return cmd_var(rows, args)
    if args.bucket:
        return cmd_bucket(rows, args)
    cmd_summary(rows, args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
