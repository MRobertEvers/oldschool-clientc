#!/usr/bin/env python3
"""Find world interactions the cache offers that no script implements.

See docs/WORLD_INTERACTIONS.md for the defect taxonomy, the false-positive
classes this deliberately filters, and which of the three disagreeing
authorities answers which question.

Two passes:

  --coverage  for every (symbol, opN, verb) the cache declares, is anything
              bound -- directly by symbol, or through the symbol's category?
  --worklist  the curated subset of --coverage that is real, actionable work,
              re-resolved against the tree every run so a row can never claim
              done while the binding is still absent.
  --dead      the inverse and the higher-signal half: a script binds opN on a
              target where NO member declares opN, so the trigger can never
              fire.  Ranked, because most are benign (see OUTAGE vs resume).

The category authority is pack/category.pack.  port/categories_loc.map is
porting triage and disagrees with it; resolving against the triage map reports
every category-bound feature as unbound.
"""
import argparse, collections, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get(
    "MOCK230_CONTENT_DIR",
    os.path.join(os.path.dirname(HERE), "OSRS-Content", "osrs239-content"))
CFG = os.path.join(ROOT, "configs")
SCRIPTS = os.path.join(ROOT, "server", "scripts")

# Trigger kind -> (config table, config op prefix).  An obj's opN is its GROUND
# option and its ifopN the inventory one, so opobj/opheld split here.
KIND2TBL = {"oploc": "loc", "opnpc": "npc", "opheld": "obj", "opobj": "obj"}
KIND2PFX = {"oploc": "op", "opnpc": "op", "opheld": "ifop", "opobj": "op"}

TRIG_NUM = re.compile(r"^\[(oploc|opnpc|opheld|opobj)(\d)\s*,\s*([A-Za-z0-9_]+)\s*\]")
TRIG_ANY = re.compile(r"^\[(oploc|opnpc|opheld|opobj)(\d|u|t)\s*,\s*([A-Za-z0-9_]+)\s*\]")


def parse_config(path, opkeys):
    """[symbol] blocks -> {symbol: {name, ops:{opkey:verb}, category}}."""
    out, cur = {}, None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("[") and line.endswith("]"):
                cur = line[1:-1]
                out[cur] = {"name": None, "ops": {}, "category": None}
                continue
            if cur is None or "=" not in line:
                continue
            k, _, v = line.partition("=")
            k = k.strip()
            if k == "name":
                out[cur]["name"] = v.strip()
            elif k == "category":
                try:
                    out[cur]["category"] = int(v.strip())
                except ValueError:
                    pass
            else:
                for p in opkeys:
                    m = re.fullmatch(re.escape(p) + r"(\d)", k)
                    if m:
                        out[cur]["ops"][p + m.group(1)] = v.strip()
    return out


def load_cache():
    return {
        "loc": parse_config(os.path.join(CFG, "all.loc"), ["op"]),
        "npc": parse_config(os.path.join(CFG, "all.npc"), ["op"]),
        "obj": parse_config(os.path.join(CFG, "all.obj"), ["op", "iop", "ifop"]),
    }


def load_categories():
    """Name -> id, from the COMPILE-TIME authority."""
    out = {}
    with open(os.path.join(ROOT, "pack", "category.pack"),
              encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("//") or "=" not in line:
                continue
            i, _, nm = line.partition("=")
            try:
                out[nm.strip()] = int(i)
            except ValueError:
                pass
    return out


def scan_scripts():
    """Every trigger head, plus every op re-issued via p_oploc/p_opnpc/...

    Returns (live, sites, reissue) where live maps (kind, target) -> {op chars}.
    """
    live = collections.defaultdict(set)
    sites = {}
    reissue = collections.Counter()
    for dirpath, _, files in os.walk(SCRIPTS):
        if os.sep + "build" in dirpath:
            continue
        for fn in sorted(files):
            if not fn.endswith(".rs2"):
                continue
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, ROOT)
            with open(p, encoding="utf-8", errors="replace") as f:
                for i, line in enumerate(f, 1):
                    s = line.strip()
                    m = TRIG_ANY.match(s)
                    if m:
                        live[(m.group(1), m.group(3))].add(m.group(2))
                        sites.setdefault((m.group(1) + m.group(2), m.group(3)),
                                         f"{rel}:{i}")
                    for mm in re.finditer(
                            r"p_(oploc|opnpc|opobj|opheld)\s*\(\s*(\d)\s*\)", s):
                        reissue[(mm.group(1), mm.group(2))] += 1
    return live, sites, reissue


def members_by_category(cache):
    out = collections.defaultdict(lambda: collections.defaultdict(list))
    for tbl, data in cache.items():
        for sym, rec in data.items():
            if rec["category"] is not None:
                out[tbl][rec["category"]].append((sym, rec))
    return out


def coverage(cache, cats, live, out_json):
    cat_trigs = collections.defaultdict(set)
    for (kind, target), ops in live.items():
        if not target.startswith("_"):
            continue
        cid = cats.get(target[1:])
        if cid is not None:
            cat_trigs[cid] |= {kind + o for o in ops}

    rows = []
    for tbl, data in cache.items():
        for sym, rec in data.items():
            for opkey, verb in sorted(rec["ops"].items()):
                pfx, n = re.fullmatch(r"(op|iop|ifop)(\d)", opkey).groups()
                if tbl == "loc":
                    trig = "oploc" + n
                elif tbl == "npc":
                    trig = "opnpc" + n
                else:
                    trig = ("opobj" if pfx == "op" else "opheld") + n
                direct = n in live.get((trig[:-1], sym), set())
                via = rec["category"] is not None and \
                    trig in cat_trigs.get(rec["category"], set())
                rows.append({"table": tbl, "sym": sym, "name": rec["name"] or "",
                             "op": opkey, "verb": verb, "trig": trig,
                             "cat": rec["category"],
                             "bound": direct, "via_cat": via})
    nb = [r for r in rows if not r["bound"] and not r["via_cat"]]
    print(f"{len(rows)} (symbol,op) pairs; bound {len(rows)-len(nb)}, unbound {len(nb)}")
    print("\nunbound by verb (top 40) -- filter per docs/WORLD_INTERACTIONS.md §4:")
    for v, c in collections.Counter(r["verb"] for r in nb).most_common(40):
        print(f"  {c:6d}  {v}")
    if out_json:
        json.dump(rows, open(out_json, "w"), indent=1)
        print(f"\nwrote {out_json}")


def dead(cache, cats, live, sites, reissue, out_json):
    bycat = members_by_category(cache)
    findings = []
    for (kindop, target), where in sorted(sites.items()):
        m = re.fullmatch(r"(oploc|opnpc|opheld|opobj)(\d)", kindop)
        if not m:
            continue                       # 'u' / 't' have no cache op to match
        kind, n = m.groups()
        tbl, want = KIND2TBL[kind], KIND2PFX[kind] + n
        if target.startswith("_"):
            cid = cats.get(target[1:])
            if cid is None:
                continue
            mem = bycat[tbl].get(cid, [])
            if not mem or any(want in r["ops"] for _, r in mem):
                continue
            decl = collections.Counter()
            for _, r in mem:
                for k, v in r["ops"].items():
                    decl[f"{k}={v}"] += 1
            declared = [f"{d} x{c}" for d, c in decl.most_common(5)]
            label = f"category, {len(mem)} member(s)"
        else:
            rec = cache[tbl].get(target)
            if rec is None or not rec["ops"] or want in rec["ops"]:
                continue
            declared = [f"{k}={v}" for k, v in sorted(rec["ops"].items())]
            label = rec["name"] or ""
        siblings = sorted(live.get((kind, target), set()) - {n})
        # A live sibling of the same kind means the feature has a working door;
        # combined with a p_op* re-issue that is the deliberate resume slot.
        if siblings:
            klass = "resume" if reissue.get((kind, n)) else "partial"
        else:
            klass = "OUTAGE"
        findings.append({"where": where, "trig": kindop, "target": target,
                         "label": label, "declared": declared,
                         "siblings": siblings, "class": klass})

    counts = collections.Counter(f["class"] for f in findings)
    print(f"{len(findings)} binding(s) point at an op the cache never declares")
    for k in ("OUTAGE", "partial", "resume"):
        print(f"  {k:8s} {counts.get(k,0)}")
    print("\n=== OUTAGE — no live sibling, so this is the only door ===")
    for f in findings:
        if f["class"] != "OUTAGE":
            continue
        print(f'{f["where"]}\n   [{f["trig"]},{f["target"]}]  "{f["label"]}"'
              f'\n   cache declares: {", ".join(f["declared"])}')
    if out_json:
        json.dump(findings, open(out_json, "w"), indent=1)
        print(f"\nwrote {out_json}")


WORKLIST = os.path.join(HERE, "world_interaction_worklist.tsv")


def worklist(cache, cats, live, todo_only):
    """Re-resolve the curated TSV against the tree.  Status is derived, never
    stored -- a checked-off row that lost its binding shows up as TODO again."""
    cat_trigs = collections.defaultdict(set)
    for (kind, target), ops in live.items():
        if target.startswith("_"):
            cid = cats.get(target[1:])
            if cid is not None:
                cat_trigs[cid] |= {kind + o for o in ops}

    fams = collections.OrderedDict()
    with open(WORKLIST, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            fam, tbl, sym, opkey, verb, cat, name = line.rstrip("\n").split("\t")
            n = opkey[-1]
            trig = {"loc": "oploc", "npc": "opnpc"}[tbl] + n
            rec = cache[tbl].get(sym)
            cid = rec["category"] if rec else None
            done = n in live.get((trig[:-1], sym), set()) or \
                (cid is not None and trig in cat_trigs.get(cid, set()))
            fams.setdefault(fam, []).append((done, tbl, sym, opkey, verb, name))

    ndone = nrows = 0
    for fam, rs in fams.items():
        d = sum(1 for r in rs if r[0])
        ndone += d
        nrows += len(rs)
        if todo_only and d == len(rs):
            continue
        print(f"{'DONE' if d == len(rs) else 'TODO'}  {fam:12s} {d}/{len(rs)}")
        for done, tbl, sym, opkey, verb, name in rs:
            if todo_only and done:
                continue
            print(f'        {"x" if done else " "} {tbl} {sym}.{opkey}={verb}  "{name}"')
    print(f"\n{ndone}/{nrows} bound")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--coverage", action="store_true",
                    help="cache ops with no binding")
    ap.add_argument("--worklist", action="store_true",
                    help="curated actionable subset, status re-derived per run")
    ap.add_argument("--todo", action="store_true",
                    help="with --worklist, hide rows that are already bound")
    ap.add_argument("--dead", action="store_true",
                    help="bindings pointing at an op the cache never declares")
    ap.add_argument("--json", metavar="PATH", help="also dump findings as JSON")
    a = ap.parse_args()
    if not (a.coverage or a.dead or a.worklist):
        ap.error("pick --coverage, --worklist or --dead")
    if not os.path.isdir(CFG):
        sys.exit(f"content tree not found at {ROOT} (set MOCK230_CONTENT_DIR)")

    cache = load_cache()
    cats = load_categories()
    live, sites, reissue = scan_scripts()
    if a.coverage:
        coverage(cache, cats, live, a.json)
    if a.worklist:
        worklist(cache, cats, live, a.todo)
    if a.dead:
        dead(cache, cats, live, sites, reissue, a.json)


if __name__ == "__main__":
    main()
