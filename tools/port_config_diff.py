#!/usr/bin/env python3
"""
port_config_diff — the server-allocated config gate (triage §9 step 3c).

    tools/port_config_diff.py --report         # the review artifact, TSV on stdout
    tools/port_config_diff.py --check          # the build bar
    tools/port_config_diff.py --write-map      # (re)generate port/configs.map
    tools/port_config_diff.py --blockers       # what stands between here and the rest

The five namespaces this covers — `param`, `struct`, `enum`, `dbtable`, `dbrow` —
are the ones whose ids are *ours*: nothing in cache.osrs239 names a param, a
struct or an enum the server defines, so `tools/ss_allocate.py` hands each block
`max + 1` off layer 0's high-water mark and that number is permanent. Triage §4
group C: a 0 % name-resolution rate against the cache is the correct answer here,
which is exactly why the name-resolution gate (§14) sees nothing in this family.

What it *can* see, and what this states instead, is the other direction: a
reference block resolves only if every symbol **inside** it resolves. A `.dbrow`
naming a sound, a music track, an interface component or an npc this tree has no
spelling for is not a block that fails to load — three of those four used to be
read as the integer 0, because `mock230_db.c` treated an unrecognised column type
as a literal. So the bar has two halves and both are new:

  * `mock230_db.c` refuses a column type it cannot resolve, and
    `mock230_pack` loads the `.dbtable`/`.dbrow` half of the tree at all
    (it had exactly one caller, `mock230_boot.c`, so the whole family was
    validated only by starting the server);
  * this file records, for every one of the reference's blocks, what this tree
    does with it — and fails the build when the tree and that statement drift.

Dispositions:

  landed              copied in; the name is in this tree's compack
  present             already here before the port reached it
  duplicate-concept   this tree carries the same thing under another name
                      (LostCity `prayers` -> `prayer_table`, `stat_names` ->
                      `stat_name_enum`); porting it would be a second table
                      saying the same thing
  collision           the name is a cache record meaning something else. One
                      today: `music`. LostCity's has 4 columns starting `name`;
                      cache dbtable 44 has 15 starting `sortname`, and 195
                      LostCity dbrows say `table=music`
  blocked-name        a symbol inside it has no spelling in this tree
  blocked-type        it needs a column or param type nothing here resolves
  blocked-loader      `struct`: no `.struct` loader exists in src/ and
                      SS_OP_STRUCT_PARAM has no server case, so a struct here
                      would be 733 param references nothing reads
  defer-slice         nothing wrong with it; its subject has not been ported

The reference half degrades: without a LostCity checkout the "every reference
block has a row" bar is skipped with a note, and every bar that compares this
tree to the map still runs — which is the half that catches a deferred block
being copied in without its row changing.
"""

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")
DEFAULT_REF = os.path.expanduser("~/Documents/git_repos/LostCity_Server")

MAP_NAME = os.path.join("port", "configs.map")
NAMESPACES = ("param", "struct", "enum", "dbtable", "dbrow")

# A disposition asserts the name is in this tree, or that it is not. Anything
# else is a spelling error in the map.
IN_TREE = {"landed", "present", "cache-record", "collision"}
NOT_IN_TREE = {
    "duplicate-concept", "blocked-name", "blocked-type",
    "blocked-loader", "defer-slice",
}
# Two of the four in-tree dispositions say the name is here because the *cache*
# states it, not because anybody authored it — so the assertion flips: no config
# file under server/scripts may declare a block by that name.
#
# `cache-record` is the ordinary case and it is 13 params wide: LostCity's
# `stabattack` is its param 163 and this cache's is param 0, so the name resolves
# and the id must never be copied. `collision` is the dangerous one — the name
# resolves to a record that means something ELSE. `music` is dbtable 44 here with
# 15 columns starting `sortname`; LostCity's has 4 starting `name`, and 195 of
# its dbrows say `table=music`.
NOT_AUTHORED = {"cache-record", "collision"}
MUST_BE_AUTHORED = {"landed", "present"}
DISPOSITIONS = IN_TREE | NOT_IN_TREE | {"tree-only"}


# ------------------------------------------------------------------ #
# Reading both trees                                                  #
# ------------------------------------------------------------------ #

def load_pack(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if "=" not in line or line.startswith("//"):
                continue
            ident, name = line.split("=", 1)
            if not ident.strip().lstrip("-").isdigit():
                continue
            out.setdefault(re.sub(r"\s*//.*$", "", name).strip(), int(ident.strip()))
    return out


def blocks(path):
    """[(name, [(key, value)])] for one config file."""
    out, cur = [], None
    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = re.sub(r"//.*$", "", raw).strip()
            if not line:
                continue
            m = re.fullmatch(r"\[([A-Za-z0-9_.+-]+)\]", line)
            if m:
                cur = (m.group(1), [])
                out.append(cur)
                continue
            if cur is not None and "=" in line:
                key, value = line.split("=", 1)
                cur[1].append((key.strip(), value.strip()))
    return out


def walk(root, suffix):
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in ("_unpack", "_test", "build")]
        for f in sorted(files):
            if f.endswith("." + suffix):
                yield os.path.join(base, f)


class Tree:
    """The destination tree's symbol tables and the types it can resolve."""

    def __init__(self, tree):
        self.tree = tree
        self.packs = {}
        for ns in ("obj", "npc", "loc", "seq", "spotanim", "inv", "param",
                   "struct", "enum", "dbtable", "dbrow", "varp", "varbit"):
            self.packs[ns] = load_pack(
                os.path.join(tree, "configs", "all.%s.compack" % ns))
            # The namespace's second layer: the server's allocation ledger.
            # A ported name whose id lives there is just as landed.
            self.packs[ns].update(
                load_pack(os.path.join(tree, "pack", "%s.alloc" % ns)))
        for ns in ("stat", "category"):
            self.packs[ns] = load_pack(os.path.join(tree, "pack", "%s.pack" % ns))
        self.packs["interface"] = load_pack(
            os.path.join(tree, "pack", "3_interfaces.pack"))
        self.packs["namedobj"] = self.packs["obj"]
        # `synth` and `midi` have pack files and not one real name in either:
        # 12,010 lines of `synth_<id>` and 881 of `song_<id>`. A filler name is
        # not a name, so they resolve nothing — which is the finding, not a bug
        # in this reader. Told apart from an ordinary miss in `resolves`: a
        # namespace with no names at all is a *type* this tree cannot yet carry,
        # not a spelling somebody has to look up.
        self.packs["synth"] = {}
        self.packs["midi"] = {}
        self.unnamed = {"synth", "midi"}

        self.components = set()
        idir = os.path.join(tree, "interfaces")
        if os.path.isdir(idir):
            for f in os.listdir(idir):
                if not f.endswith(".compack"):
                    continue
                iface = f[: -len(".compack")]
                for name in load_pack(os.path.join(idir, f)):
                    self.components.add(iface + ":" + name)

        self.constants = set()
        for base, _, files in os.walk(os.path.join(tree, "server", "scripts")):
            for f in files:
                if not f.endswith(".constant"):
                    continue
                with open(os.path.join(base, f), encoding="utf-8",
                          errors="replace") as handle:
                    for line in handle:
                        m = re.match(r"\s*\^([A-Za-z0-9_]+)\s*=", line)
                        if m:
                            self.constants.add(m.group(1))

    LITERAL = {"int", "string", "boolean", "coord", "autoint", "graphic"}

    def resolves(self, kind, token):
        """(ok, why). `why` names the namespace and token when it does not."""
        token = token.strip()
        if token in ("null", ""):
            return True, ""
        if token.startswith("^"):
            return (token[1:] in self.constants), "constant %s" % token
        if kind in self.LITERAL:
            return True, ""
        if kind == "component":
            return (token in self.components), "component %s" % token
        if token.lstrip("-").isdigit():
            return True, ""
        if kind in self.unnamed:
            return False, "type %s (this tree names none)" % kind
        pack = self.packs.get(kind)
        if pack is None:
            return False, "type %s" % kind
        return (token in pack), "%s %s" % (kind, token)


class Reference:
    """Every block the reference declares in the five namespaces, classified."""

    def __init__(self, ref, tree):
        root = os.path.join(ref, "content", "scripts")
        self.ok = os.path.isdir(root)
        self.rows = []            # (ns, name, relpath, [reasons])
        if not self.ok:
            return
        raw = {ns: [(p, b) for p in walk(root, ns) for b in blocks(p)]
               for ns in NAMESPACES}
        inslice = {ns: set(n for _, (n, _) in raw[ns]) for ns in NAMESPACES}

        def resolves(kind, token):
            if kind in inslice and token.strip() in inslice[kind]:
                return True, ""
            return tree.resolves(kind, token)

        params = {}
        for path, (name, kv) in raw["param"]:
            d = dict(kv)
            params[name] = d.get("type", "int")
            bad = []
            if "default" in d:
                ok, why = resolves(d.get("type", "int"), d["default"])
                if not ok:
                    bad.append(why)
            self.rows.append(("param", name, os.path.relpath(path, root), bad))

        for path, (name, kv) in raw["struct"]:
            bad = []
            for key, value in kv:
                if key != "param":
                    continue
                pname, _, pvalue = value.partition(",")
                ptype = params.get(pname.strip())
                if ptype is None:
                    bad.append("param %s" % pname)
                    continue
                ok, why = resolves(ptype, pvalue)
                if not ok:
                    bad.append(why)
            self.rows.append(("struct", name, os.path.relpath(path, root), bad))

        for path, (name, kv) in raw["enum"]:
            d = dict(kv)
            itype, otype = d.get("inputtype", "int"), d.get("outputtype", "int")
            bad = []
            for key, value in kv:
                if key == "default":
                    ok, why = resolves(otype, value)
                    if not ok:
                        bad.append(why)
                elif key == "val":
                    left, _, right = value.partition(",")
                    for kind, token in ((itype, left), (otype, right)):
                        ok, why = resolves(kind, token)
                        if not ok:
                            bad.append(why)
            self.rows.append(("enum", name, os.path.relpath(path, root), bad))

        tables = {}
        for path, (name, kv) in raw["dbtable"]:
            cols, bad = [], []
            for key, value in kv:
                if key != "column":
                    continue
                parts = [p.strip() for p in value.split(",")]
                types = [p for p in parts[1:]
                         if p not in ("INDEXED", "REQUIRED", "LIST")]
                cols.append((parts[0], types))
                for t in types:
                    if t not in tree.LITERAL and t not in tree.packs \
                            and t != "component":
                        bad.append("type %s (column %s)" % (t, parts[0]))
            tables[name] = cols
            self.rows.append(("dbtable", name, os.path.relpath(path, root), bad))

        for path, (name, kv) in raw["dbrow"]:
            d = dict(kv)
            tname = d.get("table", "")
            bad = []
            cols = tables.get(tname)
            if cols is None:
                bad.append("table %s" % tname)
            else:
                cmap = dict(cols)
                for key, value in kv:
                    if key != "data":
                        continue
                    cname, _, rest = value.partition(",")
                    types = cmap.get(cname.strip())
                    if types is None:
                        bad.append("column %s" % cname)
                        continue
                    values = rest.split(",")
                    for i, t in enumerate(types):
                        if i >= len(values):
                            continue
                        if t not in tree.LITERAL and t not in tree.packs \
                                and t != "component":
                            bad.append("type %s (column %s)" % (t, cname))
                            continue
                        ok, why = resolves(t, values[i])
                        if not ok:
                            bad.append(why)
            self.rows.append(("dbrow", name, os.path.relpath(path, root), bad))


# ------------------------------------------------------------------ #
# The map                                                             #
# ------------------------------------------------------------------ #

def read_map(tree):
    path = os.path.join(tree, MAP_NAME)
    rows = {}
    if not os.path.exists(path):
        return None, path
    with open(path, encoding="utf-8", errors="replace") as handle:
        for n, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                print("%s:%d: expected <namespace> <name> <disposition> [reason]"
                      % (path, n), file=sys.stderr)
                return None, path
            rows[(parts[0], parts[1])] = (parts[2], parts[3] if len(parts) > 3 else "")
    return rows, path


HEADER = """\
# port/configs.map — what this tree does with every LostCity block in the five
# server-allocated config namespaces (param, struct, enum, dbtable, dbrow).
#
# Generated by tools/port_config_diff.py --write-map; checked by --check, which
# runs inside `make -C src test-content` via the test-port target. Columns:
#
#   namespace  name  disposition  reason
#
# `landed`/`present` assert the name IS in this tree's configs/all.<ns>.compack.
# Every other disposition asserts it is NOT — so copying a deferred block in
# without changing its row is a build failure, which is the whole point of the
# file. See tools/port_config_diff.py's docstring for the disposition list and
# docs/LOSTCITY_PORT_TRIAGE.md §17 for how each population was measured.
"""


def authored_here(tree):
    """{(ns, name)} for every block a config file under server/scripts declares."""
    out = {}
    scripts = os.path.join(tree, "server", "scripts")
    for ns in NAMESPACES:
        for f in walk(scripts, ns):
            for name, _ in blocks(f):
                if re.fullmatch(re.escape(ns) + r"_\d+", name):
                    continue
                out[(ns, name)] = os.path.relpath(f, tree)
    return out


def classify(tree_obj, reference, decided, authored):
    """(ns, name, disposition, reason) for every reference block."""
    out = []
    for ns, name, relpath, bad in reference.rows:
        forced = decided.get((ns, name))
        if forced:
            out.append((ns, name, forced[0], forced[1]))
            continue
        if name in tree_obj.packs[ns]:
            here = authored.get((ns, name))
            out.append((ns, name, "present" if here else "cache-record",
                        here or ("the cache states it; %s" % relpath)))
        elif ns == "struct":
            out.append((ns, name, "blocked-loader",
                        "no .struct loader; SS_OP_STRUCT_PARAM has no server case"))
        elif any(b.startswith("type ") for b in bad):
            out.append((ns, name, "blocked-type", "; ".join(sorted(set(bad))[:4])))
        elif bad:
            out.append((ns, name, "blocked-name", "; ".join(sorted(set(bad))[:4])))
        else:
            out.append((ns, name, "defer-slice", relpath))
    return out


def check(tree, ref, verbose):
    tree_obj = Tree(tree)
    rows, path = read_map(tree)
    problems = 0
    if rows is None:
        print("port_config_diff: no %s — run --write-map" % path, file=sys.stderr)
        return 1

    for (ns, name), (disp, reason) in sorted(rows.items()):
        if ns not in NAMESPACES:
            print("%s: unknown namespace `%s` on `%s`" % (path, ns, name),
                  file=sys.stderr)
            problems += 1
            continue
        if disp not in DISPOSITIONS:
            print("%s: unknown disposition `%s` on %s `%s`" % (path, disp, ns, name),
                  file=sys.stderr)
            problems += 1
            continue
        here = name in tree_obj.packs[ns]
        if disp in IN_TREE and not here:
            print("%s `%s` is %s but has no id in configs/all.%s.compack — it was "
                  "removed, or never landed" % (ns, name, disp, ns), file=sys.stderr)
            problems += 1
        if disp in NOT_IN_TREE and here:
            print("%s `%s` is %s (%s) but IS in configs/all.%s.compack — a deferred "
                  "block was landed without its row being changed"
                  % (ns, name, disp, reason, ns), file=sys.stderr)
            problems += 1

    # Every block this tree declares has to be accounted for, so a name that
    # exists here and in neither the reference nor the map cannot hide. This is
    # also where `collision` is enforced: `music` is dbtable 44 in this cache
    # with 15 columns starting `sortname`, and LostCity's is 4 starting `name`,
    # so an authored `[music]` block would take the cache's id and detach the
    # client's music player from its own table. `present` gets the same rule for
    # a different reason — those are records the cache states, and re-declaring
    # one is a rank-1 override of a record nobody meant to override.
    scripts = os.path.join(tree, "server", "scripts")
    declared = 0
    authored = set(k for k, (d, _) in rows.items() if d in MUST_BE_AUTHORED)
    for ns in NAMESPACES:
        for f in walk(scripts, ns):
            for name, _ in blocks(f):
                if re.fullmatch(re.escape(ns) + r"_\d+", name):
                    continue
                declared += 1
                if (ns, name) not in rows:
                    print("%s `%s` (%s) is declared here and has no row in %s"
                          % (ns, name, os.path.relpath(f, tree), MAP_NAME),
                          file=sys.stderr)
                    problems += 1
                    continue
                disp, reason = rows[(ns, name)]
                authored.discard((ns, name))
                if disp in NOT_AUTHORED:
                    print("%s `%s` is %s (%s) and %s declares a block for it — that "
                          "name already belongs to a record this tree did not author"
                          % (ns, name, disp, reason, os.path.relpath(f, tree)),
                          file=sys.stderr)
                    problems += 1
    for ns, name in sorted(authored):
        print("%s `%s` is %s in %s but no config file here declares it"
              % (ns, name, rows[(ns, name)][0], MAP_NAME), file=sys.stderr)
        problems += 1

    reference = Reference(ref, tree_obj)
    if not reference.ok:
        print("port_config_diff: no reference at %s — the "
              "'every reference block has a row' bar is skipped" % ref)
    else:
        missing = [(ns, name) for ns, name, _, _ in reference.rows
                   if (ns, name) not in rows]
        for ns, name in missing:
            print("%s `%s` is in the reference and has no row in %s — classify it"
                  % (ns, name, MAP_NAME), file=sys.stderr)
        problems += len(missing)

    if problems:
        print("port_config_diff: %d problem(s)" % problems, file=sys.stderr)
        return 1
    counts = {}
    for _, (disp, _) in rows.items():
        counts[disp] = counts.get(disp, 0) + 1
    if verbose:
        print("port_config_diff: %d row(s), %d block(s) declared here — %s"
              % (len(rows), declared,
                 ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", default=DEFAULT_TREE)
    parser.add_argument("--ref", default=DEFAULT_REF)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write-map", action="store_true")
    parser.add_argument("--blockers", action="store_true")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    if args.check:
        return check(args.tree, args.ref, args.verbose)

    tree_obj = Tree(args.tree)
    reference = Reference(args.ref, tree_obj)
    if not reference.ok:
        print("port_config_diff: no reference at %s" % args.ref, file=sys.stderr)
        return 2

    decided, _ = read_map(args.tree)
    decided = {k: v for k, v in (decided or {}).items()
               if v[0] in ("landed", "duplicate-concept", "collision")}
    rows = classify(tree_obj, reference, decided, authored_here(args.tree))

    if args.blockers:
        counts = {}
        for ns, name, disp, reason in rows:
            if not disp.startswith("blocked"):
                continue
            for part in reason.split(";"):
                key = part.strip().split(" ")[0]
                counts[(disp, key)] = counts.get((disp, key), 0) + 1
        for key, n in sorted(counts.items(), key=lambda kv: -kv[1]):
            print("%-14s %-12s %d" % (key[0], key[1], n))
        return 0

    if args.write_map:
        path = os.path.join(args.tree, MAP_NAME)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        seen = set()
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(HEADER)
            for ns, name, disp, reason in sorted(rows):
                handle.write("%s\t%s\t%s\t%s\n" % (ns, name, disp, reason))
                seen.add((ns, name))
            scripts = os.path.join(args.tree, "server", "scripts")
            for ns in NAMESPACES:
                for f in walk(scripts, ns):
                    for name, _ in blocks(f):
                        if re.fullmatch(re.escape(ns) + r"_\d+", name):
                            continue
                        if (ns, name) in seen:
                            continue
                        seen.add((ns, name))
                        handle.write("%s\t%s\ttree-only\t%s\n"
                                     % (ns, name, os.path.relpath(f, args.tree)))
        print("wrote %s" % path)
        return 0

    for ns, name, disp, reason in sorted(rows):
        print("%s\t%s\t%s\t%s" % (ns, name, disp, reason))
    return 0


if __name__ == "__main__":
    sys.exit(main())
