#!/usr/bin/env python3
"""
port_names_diff — the npc / loc / seq / spotanim name map for the LostCity port.

    tools/port_names_diff.py --report      # the review artifact, TSV on stdout
    tools/port_names_diff.py --check       # the build bar (runs inside test-port)
    tools/port_names_diff.py --propose     # what the evidence rules would say
    tools/port_names_diff.py --write-map   # regenerate, preserving human columns

docs/LOSTCITY_PORT_TRIAGE.md §9 step 3e. The reference's authored content names
npc, loc, seq and spotanim records that this tree has no spelling for. Every one
of those is a compile error the moment the script naming it is ported, which is
the *good* case — bar 1 says an unresolved name must fail loudly, and the list is
the work queue. This file is the other half: for the names where the reference
and this tree really do describe the same record under two spellings, what the
target is, and **what evidence says so**.

The rule the whole file is written against:

> A map row is a claim that two records are the SAME THING. Name resolution
> cannot make that claim — it answers "does something here have this spelling".
> So a row states its evidence as re-checkable facts, and `--check` re-derives
> every one of them from the two trees on every build.

Why that matters more here than anywhere else in the port: an unresolved name
fails loudly, and a wrong map does not fail at all. `obj rock_sample1` (§14) is
the shape of it one namespace over — the name resolves, the id agrees, and the
record is a different object. The same trap is live in this namespace:

    npc macro_bigfish   LostCity 'Big fish', no ops, readyanim fish_ready
                        osrs239's only 'Big Fish' is sotn_hazeel_troll_vis,
                        a Shadow-of-the-Storm troll on models 35833/35836

That row passes *every* automatic rule in this file — unique display name, same
vislevel, injective, no collision — and is wrong. It is carried as `deferred`
with the reason stated, and it is why `--propose` and `--check` are separate
verbs: proposing is mechanical, landing is not.

Evidence grammar. The `evidence` column is `fact;fact;...` optionally followed
by ` | free text`. A fact is re-checked by `--check`; free text is for a human.

    src=<id>@<rev>   this tree's record at <id> is the target (a port manifest
                     export, or a `<prefix>_<ns>_<id>` name the exporter minted)
    display=<text>   both records carry this display name
    vislevel=<n>     npc combat level (LostCity's `hide` is 0 here)
    size=<w>x<l>     loc footprint
    ops=<a>/<b>      the reference's op verbs, all present on the target
    frames=<n>       seq frame count, both sides
    model=<id>       spotanim model id, both sides

Dispositions. The first three carry a target and are the map; the rest are the
work queue and carry none.

    manifest   a port_lostcity export. The source id is stated in the manifest
               (or minted into the name), so the target is a LOOKUP, not a
               judgement — and the structural fact is the cross-check.
    renamed    a stated rename verified by a second field on every member of
               the set, with no counterexample in the whole namespace.
    resolved   display name plus at least one structural field agree, uniquely,
               injectively, and the target is not itself a reference name.
    proposed   one target and one stated disagreement. A human signs it.
    collision  two reference names land on one osrs239 record.
    family     the family is certain, the member is not.
    absent     no osrs239 record carries this display name at all.
    scenery    a generic loc ('Gate' x328, 'Ladder' x488) whose identity is
               fixed by where it stands, not by what it is called. Wants the
               coordinate resolver, not more name guessing.
    deferred   nothing defensible. The reason is the point of the row.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")
DEFAULT_REF = os.path.expanduser("~/Documents/git_repos/LostCity_Server")
MAP_REL = os.path.join("port", "names.map")

NAMESPACES = ["npc", "loc", "seq", "spotanim"]
SKIP_DIRS = ("_unpack", "_test")
UNPACK_REVS = ("225", "244", "245", "254")

WORD = re.compile(r"[A-Za-z0-9_]+")
TAG = re.compile(r"<[^>]*>")
# `model_2393_obj`, `anim_301`, `inferno_locmodel_33035` — LostCity spells an
# asset reference as a name built around the id it was exported from, and this
# tree spells the same thing as the bare number.
ASSET_ID = re.compile(r"^[a-z][a-z_]*_(\d+)(?:_[a-z][a-z0-9_]*)?$")

WITH_TARGET = ("manifest", "renamed", "resolved", "proposed", "collision")
WITHOUT_TARGET = ("family", "absent", "scenery", "deferred")
DISPOSITIONS = WITH_TARGET + WITHOUT_TARGET

COLUMNS = ["namespace", "name", "refs", "lc_id", "lc_layer",
           "disposition", "target", "evidence"]


# ---------------------------------------------------------------- parsing

def read_pack(path):
    by_name, by_id = {}, {}
    try:
        handle = open(path, "r", encoding="utf-8", errors="replace")
    except OSError:
        return by_name, by_id
    with handle:
        for line in handle:
            line = line.strip()
            if not line or line[0] in "#/":
                continue
            if "=" not in line:
                continue
            left, right = line.split("=", 1)
            right = right.split("//")[0].strip()
            if not right:
                continue
            try:
                ident = int(left.strip())
            except ValueError:
                continue
            by_name[right] = ident
            by_id[ident] = right
    return by_name, by_id


def read_config(path, into=None):
    """`[name]` sections of `key=value`. A repeated key becomes a list so
    `frame=` can be counted."""
    records = into if into is not None else {}
    try:
        handle = open(path, "r", encoding="utf-8", errors="replace")
    except OSError:
        return records
    current = None
    with handle:
        for raw in handle:
            line = raw.split("//")[0].strip()
            if not line:
                continue
            if line.startswith("[") and line.endswith("]"):
                current = records.setdefault(line[1:-1], {})
                continue
            if current is None or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key, value = key.strip(), value.strip()
            if key in current:
                if isinstance(current[key], list):
                    current[key].append(value)
                else:
                    current[key] = [current[key], value]
            else:
                current[key] = value
    return records


def walk_configs(root, suffix, records=None):
    records = {} if records is None else records
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in sorted(files):
            if name.endswith(suffix):
                read_config(os.path.join(base, name), records)
    return records


def first(record, key):
    value = record.get(key)
    return value[0] if isinstance(value, list) else value


def normalise(text):
    if not text:
        return ""
    return re.sub(r"[^a-z0-9]", "", TAG.sub("", text).lower())


# ---------------------------------------------------------------- the trees

class Trees(object):
    """Both sides, loaded once.

    The reference half deliberately reads `_unpack` as *evidence* — never as
    something to port. §11 is right that `_unpack` is LostCity's own decompiled
    review queue and is not content, but 163 of the names its authored scripts
    reference are stated nowhere else, and a row with no record on the reference
    side has nothing to check. `lc_layer` records which half a row's facts came
    from, so a reviewer can weigh them. (`tools/port_category_crawl.py` made the
    same call for the same reason.)
    """

    def __init__(self, tree, ref):
        self.tree, self.ref = tree, ref
        scripts = os.path.join(ref, "content", "scripts")
        self.here_name, self.here_id = {}, {}
        self.here_rec = {}
        self.lc_name = {}
        self.lc_rec, self.lc_layer = {}, {}
        for ns in NAMESPACES:
            self.here_name[ns], self.here_id[ns] = read_pack(
                os.path.join(tree, "configs", "all.%s.compack" % ns))
            # The server's allocation ledger — the namespace's second layer.
            alloc_name, alloc_id = read_pack(
                os.path.join(tree, "pack", "%s.alloc" % ns))
            self.here_name[ns].update(alloc_name)
            self.here_id[ns].update(alloc_id)
            self.here_rec[ns] = read_config(
                os.path.join(tree, "configs", "all.%s" % ns))
            # This tree's own authored overlays can name a record too.
            for base, dirs, files in os.walk(os.path.join(tree, "server", "scripts")):
                dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
                for name in files:
                    if name.endswith("." + ns):
                        for key in read_config(os.path.join(base, name)):
                            self.here_name[ns].setdefault(key, None)
            self.lc_name[ns], _ = read_pack(
                os.path.join(ref, "content", "pack", "%s.pack" % ns))
            authored = walk_configs(scripts, "." + ns)
            self.lc_rec[ns] = dict(authored)
            self.lc_layer[ns] = {k: "authored" for k in authored}
            for rev in UNPACK_REVS:
                path = os.path.join(scripts, "_unpack", rev, "all." + ns)
                for key, rec in read_config(path).items():
                    if key not in self.lc_rec[ns]:
                        self.lc_rec[ns][key] = rec
                        self.lc_layer[ns][key] = "_unpack" + rev

    # -- structural fingerprints, one per namespace ------------------------

    def display(self, ns, name, side):
        rec = (self.lc_rec if side == "lc" else self.here_rec)[ns].get(name, {})
        return first(rec, "name") or ""

    def vislevel(self, ns, name, side):
        rec = (self.lc_rec if side == "lc" else self.here_rec)[ns].get(name)
        if rec is None:
            return None
        value = first(rec, "vislevel")
        if value is None:
            return "0"          # both decoders default an absent vislevel to 0
        return "0" if value == "hide" else value

    def size(self, ns, name, side):
        rec = (self.lc_rec if side == "lc" else self.here_rec)[ns].get(name)
        if rec is None:
            return None
        return "%sx%s" % (first(rec, "width") or "1", first(rec, "length") or "1")

    def ops(self, ns, name, side):
        rec = (self.lc_rec if side == "lc" else self.here_rec)[ns].get(name, {})
        return {first(rec, k) for k in rec if k.startswith("op") and k[2:].isdigit()}

    def frames(self, name, side):
        """This tree repeats `frame=`; the reference numbers `frame1..frameN`."""
        rec = (self.lc_rec if side == "lc" else self.here_rec)["seq"].get(name)
        if rec is None:
            return None
        value = rec.get("frame")
        count = len(value) if isinstance(value, list) else (1 if value else 0)
        count += sum(1 for k in rec if re.fullmatch(r"frame\d+", k))
        return str(count)

    def model(self, ns, name, side):
        rec = (self.lc_rec if side == "lc" else self.here_rec)[ns].get(name)
        if rec is None:
            return None
        # spotanim: `model=`. loc: the reference says `model=`, this tree
        # `models=`. Neither tree uses `model1` for a loc, which is worth
        # knowing before trusting a loc `shape` column anywhere.
        value = first(rec, "model") or first(rec, "models") or ""
        match = ASSET_ID.match(value)
        return match.group(1) if match else value


# ---------------------------------------------------------------- the corpus

def manifest_entries(repo, ref):
    """Every `port_lostcity` export: (ns, lc_name, src_id, rev, source).

    Two spellings, because the tool grew a manifest format after the first port
    was run: `3rd/rscache/tools/port_lostcity/*.ini`, and — for Inferno — a
    shell block in the area's own README. `lc_manifest.h` says shell history is
    not a repeatable port; that is a filed gap, not this file's to close, so
    the block is parsed rather than trusted to be re-runnable.
    """
    out = []
    directory = os.path.join(repo, "3rd", "rscache", "tools", "port_lostcity")
    for name in sorted(os.listdir(directory)) if os.path.isdir(directory) else []:
        if not name.endswith(".ini"):
            continue
        rev = prefix = section = None
        for raw in open(os.path.join(directory, name), encoding="utf-8",
                        errors="replace"):
            line = raw.split(";")[0].split("#")[0].strip()
            if not line:
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
                continue
            if "=" not in line:
                continue
            key, value = [x.strip() for x in line.split("=", 1)]
            if section == "port:lostcity":
                if key == "rev":
                    rev = value
                elif key == "prefix":
                    prefix = value
            elif section and section.startswith("export:"):
                ns = section.split(":", 1)[1]
                if ns in NAMESPACES and key.isdigit():
                    out.append((ns, value or "%s_%s_%s" % (prefix, ns, key),
                                int(key), rev, name, prefix))
    for base, _dirs, files in os.walk(os.path.join(ref, "content", "scripts")):
        if "README.md" not in files:
            continue
        text = open(os.path.join(base, "README.md"), encoding="utf-8",
                    errors="replace").read()
        for block in re.findall(r"```sh(.*?)```", text, re.S):
            if "port_lostcity" not in block:
                continue
            rev = re.search(r"--rev\s+(\S+)", block)
            prefix = re.search(r"--prefix\s+(\S+)", block)
            rev = rev.group(1) if rev else None
            prefix = prefix.group(1) if prefix else None
            source = os.path.relpath(os.path.join(base, "README.md"), ref)
            for ns in NAMESPACES:
                for hit in re.finditer(r"--%s\s+(\d+)(?:=(\S+))?" % ns, block):
                    ident = int(hit.group(1))
                    out.append((ns, hit.group(2) or "%s_%s_%d" % (prefix, ns, ident),
                                ident, rev, source, prefix))
    return out


def unresolved(trees, repo):
    """Every npc/loc/seq/spotanim name the reference's authored tree references
    and this tree cannot provide.

    A `[name]` header in a `.<ns>` file is a *definition*, not a reference, and
    is discounted — otherwise every record the reference authors reads as a
    reference to itself and the corpus is meaningless. Everything else counts:
    a `.npc` naming a `seq` is a reference the port has to resolve just as much
    as an `.rs2` naming an `npc`, and restricting the walk to `.rs2` (which is
    what §13.1's 206 counts) misses that half.
    """
    refs = {ns: Counter() for ns in NAMESPACES}
    where = {ns: defaultdict(set) for ns in NAMESPACES}
    root = os.path.join(trees.ref, "content", "scripts")
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            path = os.path.join(base, name)
            ext = os.path.splitext(name)[1][1:]
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            headers = set()
            if ext in NAMESPACES:
                for line in text.splitlines():
                    line = line.split("//")[0].strip()
                    if line.startswith("[") and line.endswith("]"):
                        headers.add(line[1:-1])
            counts = Counter(WORD.findall(text))
            for token, count in counts.items():
                for ns in NAMESPACES:
                    if token not in trees.lc_name[ns]:
                        continue
                    seen = count - 1 if (ext == ns and token in headers) else count
                    if seen <= 0:
                        continue
                    refs[ns][token] += seen
                    where[ns][token].add(ext)
    rows = []
    manifest = {}
    prefixes = set()
    for ns, name, ident, rev, source, prefix in manifest_entries(repo, trees.ref):
        manifest[(ns, name)] = (ident, rev, source)
        if prefix:
            prefixes.add(prefix)
    embedded = re.compile(r"^(%s)_(npc|loc|seq|spotanim)_(\d+)(?:_.*)?$"
                          % "|".join(sorted(prefixes))) if prefixes else None
    prefix_rev = {}
    for ns, name, ident, rev, source, prefix in manifest_entries(repo, trees.ref):
        if prefix:
            prefix_rev[prefix] = (rev, source)
    for ns in NAMESPACES:
        for name in sorted(refs[ns]):
            if name in trees.here_name[ns]:
                continue
            src = None
            if (ns, name) in manifest:
                ident, rev, source = manifest[(ns, name)]
                src = (ident, rev, "manifest:" + os.path.basename(source))
            elif embedded:
                hit = embedded.match(name)
                if hit and hit.group(2) == ns:
                    rev, source = prefix_rev.get(hit.group(1), (None, ""))
                    src = (int(hit.group(3)), rev,
                           "name:%s (exporter-minted)" % hit.group(1))
            rows.append({
                "namespace": ns,
                "name": name,
                "refs": refs[ns][name],
                "lc_id": trees.lc_name[ns][name],
                "lc_layer": trees.lc_layer[ns].get(name, "pack-only"),
                "where": ",".join(sorted(where[ns][name])),
                "src": src,
            })
    return rows


# ---------------------------------------------------------------- proposals

# A rename the reference applied wholesale, stated once here rather than 26
# times in the map. `--propose` refuses to use a rule that has a counterexample
# anywhere in the namespace, which is the only thing that makes a rule stronger
# than the 26 individual guesses it replaces: `midget_` -> `gnome_` holds for
# 30 of the reference's 30 `midget_*` seqs with identical frame counts and no
# exception. A frame count alone proves nothing (345 of this tree's seqs have
# 13 frames); the rule plus the count is what carries it.
RENAMES = [("seq", "midget_", "gnome_")]


def rename_targets(trees):
    """{(ns, lc_name): (target, fact)} for every rename rule that holds
    everywhere in its namespace."""
    out = {}
    for ns, before, after in RENAMES:
        members = [n for n in trees.lc_name[ns] if n.startswith(before)]
        pairs, ok = [], True
        for name in members:
            target = after + name[len(before):]
            if target not in trees.here_name[ns]:
                ok = False
                break
            left = trees.frames(name, "lc") if ns == "seq" else None
            right = trees.frames(target, "here") if ns == "seq" else None
            if left is None or left != right:
                ok = False
                break
            pairs.append((name, target, left))
        if not ok:
            continue
        for name, target, count in pairs:
            out[(ns, name)] = (target, "frames=%s" % count,
                               "the reference's `%s*` set is this tree's `%s*`: "
                               "%d of %d members, frame counts identical, no "
                               "counterexample" % (before, after, len(pairs),
                                                   len(members)))
    return out


def propose(trees, rows):
    """What the evidence rules say, before a human reads them.

    Deliberately conservative, and every one of the four narrowing rules exists
    because a looser version produced a wrong answer on this corpus:

      injective          `gnome_tree_branch_1/2/3` all want `climbing_branch`
      not-itself-a-name  `observatory_professor2` -> `observatory_professor`,
                         which the reference *also* defines and which resolves
                         here already: a 2:1 collapse, not a rename
      no suffix rule     the longest-common-suffix heuristic maps `king_lathas`
                         to `ds2_meeting_king_lathas` (the DS2 cutscene copy,
                         category 1207) over `kinglathas_vis`, and
                         `crafting_guild_door` to `ranging_guild_door`
      ops are a subset   a modern record gains verbs; requiring set equality
                         dropped `loc_2114` -> `coal_truck` (which gained
                         `Investigate`) and kept generic coffins
    """
    display_index = {}
    for ns in ("npc", "loc"):
        index = defaultdict(list)
        for name, rec in trees.here_rec[ns].items():
            text = first(rec, "name")
            if text:
                index[normalise(text)].append(name)
        display_index[ns] = index

    out = {}
    renames = rename_targets(trees)
    for row in rows:
        ns, name = row["namespace"], row["name"]
        key = (ns, name)
        if row["src"]:
            # A port_lostcity export: the source id is stated, so the target is
            # read out of this tree rather than chosen. The structural fact is
            # the cross-check that the id still means the same record — which
            # is not free, because `td_*` was exported from cache.osrs230 and
            # this tree is osrs239.
            ident, rev, how = row["src"]
            target = trees.here_id[ns].get(ident)
            facts = ["src=%d@%s" % (ident, rev or "unstated")]
            if ns == "seq":
                facts.append("frames=%s" % trees.frames(name, "lc"))
            elif ns == "spotanim":
                facts.append("model=%s" % trees.model(ns, name, "lc"))
            elif ns == "npc":
                facts.append("display=%s" % trees.display(ns, name, "lc"))
                facts.append("vislevel=%s" % trees.vislevel(ns, name, "lc"))
            else:
                facts.append("size=%s" % trees.size(ns, name, "lc"))
                if trees.display(ns, name, "lc"):
                    facts.append("display=%s" % trees.display(ns, name, "lc"))
            if rev and rev != "osrs239":
                how += ("; exported from cache.%s, not this tree's cache — the "
                        "stated structural fact is what says the id still means "
                        "the same record" % rev)
            out[key] = {"cands": [target] if target else [], "facts": facts,
                        "note": how, "display_hits": 0, "verdict": "manifest"}
            if not target:
                out[key]["verdict"] = "deferred"
                out[key]["note"] = "%s, but no %s here holds id %d" % (how, ns, ident)
            continue
        if key in renames:
            target, fact, note = renames[key]
            out[key] = {"cands": [target], "facts": [fact], "note": note,
                        "display_hits": 0, "verdict": "renamed"}
            continue
        if ns not in ("npc", "loc"):
            # seq and spotanim state no display name, so the only signal is the
            # structural one — and on its own it is worthless. Say so with the
            # number rather than leaving the row blank: this is what "under-
            # promise" means for a namespace with nothing to compare.
            if ns == "seq":
                count = trees.frames(name, "lc")
                same = sum(1 for t in trees.here_name["seq"]
                           if trees.frames(t, "here") == count) if count else 0
                out[key] = {"cands": [], "facts": [], "display_hits": 0,
                            "verdict": "deferred",
                            "note": "a seq states no display name; %s frames "
                                    "matches %d seq(s) here, so the frame count "
                                    "cannot pick one" % (count, same)}
            else:
                model = trees.model(ns, name, "lc")
                same = sum(1 for t in trees.here_name["spotanim"]
                           if trees.model(ns, t, "here") == model) if model else 0
                out[key] = {"cands": [], "facts": [], "display_hits": 0,
                            "verdict": "deferred",
                            "note": "a spotanim states no display name; the "
                                    "reference's model %s matches %d spotanim(s) "
                                    "here" % (model or "(none)", same)}
            continue
        lc_display = trees.display(ns, name, "lc")
        if not lc_display:
            out[key] = {"cands": [], "facts": [], "display_hits": 0,
                        "verdict": "deferred",
                        "note": "the reference states no display name for this "
                                "record (%s), so there is nothing to match on"
                                % (trees.lc_layer[ns].get(name, "pack-only"))}
            continue
        cands = list(display_index[ns].get(normalise(lc_display), []))
        facts = ["display=%s" % lc_display]
        if ns == "npc":
            want = trees.vislevel(ns, name, "lc")
            cands = [c for c in cands if trees.vislevel(ns, c, "here") == want]
            facts.append("vislevel=%s" % want)
        else:
            want = trees.size(ns, name, "lc")
            cands = [c for c in cands if trees.size(ns, c, "here") == want]
            facts.append("size=%s" % want)
        lc_ops = {o for o in trees.ops(ns, name, "lc") if o}
        note = ""
        if lc_ops and cands:
            keep = [c for c in cands if lc_ops <= trees.ops(ns, c, "here")]
            if keep:
                cands = keep
                facts.append("ops=%s" % "/".join(sorted(lc_ops)))
            else:
                note = "the reference's ops (%s) are on no candidate" % \
                       "/".join(sorted(lc_ops))
        out[(ns, name)] = {"cands": cands, "facts": facts, "note": note,
                           "display_hits": len(display_index[ns].get(
                               normalise(lc_display), [])),
                           "display": lc_display}
    taken = Counter()
    for key, value in out.items():
        if len(value["cands"]) == 1:
            taken[(key[0], value["cands"][0])] += 1
    for key, value in out.items():
        ns = key[0]
        cands = value["cands"]
        if "verdict" in value:
            continue
        if not cands:
            if value["display_hits"] == 0:
                value["verdict"] = "absent"
                value["note"] = ("no %s record in this tree carries the display "
                                 "name '%s'" % (ns, value.get("display", "")))
            else:
                value["verdict"] = "family"
                value["note"] = ("%d %s record(s) here display '%s', but none "
                                 "at %s%s" % (value["display_hits"], ns,
                                              value.get("display", ""),
                                              value["facts"][1].split("=", 1)[1]
                                              if len(value["facts"]) > 1 else "?",
                                              ("; " + value["note"]) if value["note"]
                                              else ""))
        elif len(cands) > 1:
            # A loc's identity is fixed by where it stands, not by what it is
            # called: this tree holds 328 records displaying 'Gate', 488 'Ladder'
            # and 191 'Rocks'. Past this many same-named records, more name
            # evidence does not exist — the `.jm2` <-> map-square cross-reference
            # is what would resolve them, and this stage does not build it and
            # does not hand-pick a gate instead.
            value["verdict"] = ("scenery" if ns == "loc" and
                                value["display_hits"] >= 100 else "family")
            if value["verdict"] == "scenery":
                value["note"] = ("%d loc(s) here display '%s'; %d survive the "
                                 "size and op filters. A name cannot pick one"
                                 % (value["display_hits"], value.get("display", ""),
                                    len(cands)))
        elif taken[(ns, cands[0])] > 1:
            value["verdict"] = "collision"
        elif cands[0] in trees.lc_name[ns]:
            value["verdict"] = "collision"
            value["note"] = ("the target is itself a reference name, and it "
                             "resolves here — a 2:1 collapse, not a rename")
        elif value["note"]:
            value["verdict"] = "proposed"
        else:
            value["verdict"] = "resolved"
    return out


# ---------------------------------------------------------------- the map

def read_map(path):
    rows, order = {}, []
    if not os.path.exists(path):
        return rows, order, False
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            order.append(("comment", line))
            continue
        parts = line.split("\t")
        if len(parts) < len(COLUMNS):
            parts += [""] * (len(COLUMNS) - len(parts))
        row = dict(zip(COLUMNS, parts[:len(COLUMNS)]))
        rows[(row["namespace"], row["name"])] = row
        order.append(("row", (row["namespace"], row["name"])))
    return rows, order, True


def write_map(path, header, rows):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header)
        for row in rows:
            handle.write("\t".join(str(row[c]) for c in COLUMNS) + "\n")


# ---------------------------------------------------------------- checking

def parse_evidence(text):
    facts, note = text, ""
    if "|" in text:
        facts, note = text.split("|", 1)
    out = []
    for item in facts.split(";"):
        item = item.strip()
        if not item or "=" not in item:
            continue
        key, value = item.split("=", 1)
        out.append((key.strip(), value.strip()))
    return out, note.strip()


def check_fact(trees, ns, name, target, key, value, errors):
    def fail(msg):
        errors.append("%s %s: %s" % (ns, name, msg))

    if key == "src":
        ident = value.split("@")[0]
        try:
            ident = int(ident)
        except ValueError:
            return fail("`src=` is not an id: %s" % value)
        held = trees.here_id[ns].get(ident)
        if held != target:
            fail("the source id %d holds `%s` here, not the stated target `%s`"
                 % (ident, held, target))
    elif key == "display":
        lhs = normalise(trees.display(ns, name, "lc"))
        rhs = normalise(trees.display(ns, target, "here"))
        if not lhs or lhs != rhs or lhs != normalise(value):
            fail("display no longer agrees: reference '%s', here '%s', row '%s'"
                 % (trees.display(ns, name, "lc"),
                    trees.display(ns, target, "here"), value))
    elif key == "vislevel":
        if trees.vislevel(ns, name, "lc") != value or \
           trees.vislevel(ns, target, "here") != value:
            fail("vislevel no longer agrees: reference %s, here %s, row %s"
                 % (trees.vislevel(ns, name, "lc"),
                    trees.vislevel(ns, target, "here"), value))
    elif key == "size":
        if trees.size(ns, name, "lc") != value or \
           trees.size(ns, target, "here") != value:
            fail("size no longer agrees: reference %s, here %s, row %s"
                 % (trees.size(ns, name, "lc"),
                    trees.size(ns, target, "here"), value))
    elif key == "ops":
        want = {o for o in value.split("/") if o}
        if not want <= trees.ops(ns, target, "here"):
            fail("the target no longer offers %s (it offers %s)"
                 % ("/".join(sorted(want)),
                    "/".join(sorted(o for o in trees.ops(ns, target, "here") if o))))
    elif key == "frames":
        if trees.frames(name, "lc") != value or trees.frames(target, "here") != value:
            fail("frame count no longer agrees: reference %s, here %s, row %s"
                 % (trees.frames(name, "lc"), trees.frames(target, "here"), value))
    elif key == "model":
        if trees.model(ns, name, "lc") != value or \
           trees.model(ns, target, "here") != value:
            fail("model no longer agrees: reference %s, here %s, row %s"
                 % (trees.model(ns, name, "lc"),
                    trees.model(ns, target, "here"), value))
    else:
        fail("unknown evidence fact `%s`" % key)


def check(trees, rows, mapped, existed):
    errors = []
    if not existed:
        return ["port/names.map does not exist — there is no baseline to check "
                "against. Generate it with --write-map and commit it."]
    live = {(r["namespace"], r["name"]): r for r in rows}
    for key, row in sorted(live.items()):
        if key not in mapped:
            errors.append("%s %s: the reference references this name %d time(s) "
                          "and it has no row — an unclassified name" %
                          (key[0], key[1], row["refs"]))
    seen_targets = defaultdict(list)
    for key, row in sorted(mapped.items()):
        ns, name = key
        if ns not in NAMESPACES:
            errors.append("%s %s: unknown namespace" % (ns, name))
            continue
        disposition = row["disposition"]
        target = row["target"].strip()
        if disposition not in DISPOSITIONS:
            errors.append("%s %s: unknown disposition `%s`" % (ns, name, disposition))
            continue
        if key not in live:
            if name in trees.here_name[ns]:
                errors.append("%s %s: this tree now provides this name directly — "
                              "the row is stale and must be removed" % (ns, name))
            else:
                errors.append("%s %s: the reference no longer references this name "
                              "— the row is stale" % (ns, name))
            continue
        if disposition in WITH_TARGET:
            if not target:
                errors.append("%s %s: `%s` needs a target" % (ns, name, disposition))
                continue
            if target not in trees.here_name[ns]:
                errors.append("%s %s: the target `%s` is not a %s in this tree"
                              % (ns, name, target, ns))
                continue
            seen_targets[(ns, target)].append(name)
        else:
            if target:
                errors.append("%s %s: `%s` must not carry a target (`%s`)"
                              % (ns, name, disposition, target))
                continue
        facts, note = parse_evidence(row["evidence"])
        if disposition in ("manifest", "renamed", "resolved") and not facts:
            errors.append("%s %s: `%s` with no re-checkable evidence is a guess"
                          % (ns, name, disposition))
        if disposition in WITHOUT_TARGET and not (facts or note):
            errors.append("%s %s: `%s` needs a stated reason" % (ns, name, disposition))
        if target:
            for fkey, fvalue in facts:
                check_fact(trees, ns, name, target, fkey, fvalue, errors)
    for (ns, target), names in sorted(seen_targets.items()):
        if len(names) > 1:
            offenders = [n for n in names
                         if mapped[(ns, n)]["disposition"] != "collision"]
            if offenders:
                errors.append("%s: %d reference names map to `%s` (%s) — at most "
                              "one may be anything but `collision`"
                              % (ns, len(names), target, ", ".join(sorted(names))))
    return errors


# ---------------------------------------------------------------- main

HEADER = """\
# port/names.map — one row per npc / loc / seq / spotanim name the LostCity
# reference's authored content references and this tree cannot provide.
#
# LOSTCITY_PORT_TRIAGE.md §9 step 3e / §4 group B / bar 1. Generated columns are
# re-measured by `tools/port_names_diff.py --check` (which runs in
# `make -C src test-port`); `disposition`, `target` and `evidence` are HUMAN and
# are never regenerated. A referenced name with no row is an error.
#
# An unresolved name fails loudly the moment a script naming it is ported, and
# that is by design — this file is not here to make those go away. It is here
# because the opposite failure is silent: a row claiming two records are the
# same thing when they are not costs nothing at pack time and everything later.
# So every row carries facts `--check` re-derives from both trees.
#
# columns
#   namespace    npc / loc / seq / spotanim
#   name         the reference's spelling
#   refs         how often the reference's authored tree names it (informational,
#                not a bar — the reference checkout is live, see §14.5)
#   lc_id        the reference's own id. NEVER copy it (§7 item 1: 1,329 names
#                would land on a real, occupied, wrong record here)
#   lc_layer     where the reference states the record: `authored`, or the
#                `_unpack<rev>` dump it decompiled from its own cache. `_unpack`
#                is read for EVIDENCE only; §11 still says do not port it
#   disposition  the decision (below)
#   target       this tree's name, for the five dispositions that carry one
#   evidence     `fact;fact | free text`. Facts are re-checked:
#                src=<id>@<rev> display=<text> vislevel=<n> size=<w>x<l>
#                ops=<a>/<b> frames=<n> model=<id>
#
# dispositions
#   manifest   a port_lostcity export. The source id is stated in the manifest or
#              minted into the name, so the target is a LOOKUP; the structural
#              fact is the cross-check. All 113 cross-check clean.
#   renamed    a stated rename verified by a second field on every member, with
#              no counterexample anywhere in the namespace.
#   resolved   display name + a structural field agree, uniquely, injectively,
#              and the target is not itself a reference name.
#   proposed   one target, one stated disagreement. A human signs it.
#   collision  two reference names land on one osrs239 record.
#   family     the family is certain, the member is not.
#   absent     no osrs239 record carries this display name at all.
#   scenery    a generic loc whose identity is fixed by where it stands. Wants
#              the coordinate resolver, not more name guessing.
#   deferred   nothing defensible, or a proposal a human rejected. The reason is
#              the whole point of the row.
#
# namespace\tname\trefs\tlc_id\tlc_layer\tdisposition\ttarget\tevidence
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tree", default=DEFAULT_TREE)
    parser.add_argument("--reference", default=DEFAULT_REF)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--propose", action="store_true")
    parser.add_argument("--write-map", action="store_true")
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()

    # A bar that cannot run has not failed: without the reference checkout there
    # is no left-hand side to compare against, and reporting 524 stale rows would
    # be a lie about the tree rather than a fact about the checkout.
    if not os.path.isdir(os.path.join(args.reference, "content", "scripts")):
        print("port_names_diff: no reference tree at %s — skipped" % args.reference)
        return 0

    trees = Trees(args.tree, args.reference)
    rows = unresolved(trees, REPO)
    path = os.path.join(args.tree, MAP_REL)
    mapped, _order, existed = read_map(path)

    if args.propose:
        guesses = propose(trees, rows)
        for key in sorted(guesses):
            value = guesses[key]
            print("%-9s %-36s %-10s %s%s" % (
                key[0], key[1], value["verdict"],
                ",".join(value["cands"][:4]),
                (" | " + value["note"]) if value["note"] else ""))
        return 0

    if args.write_map:
        guesses = propose(trees, rows)
        out = []
        for row in rows:
            key = (row["namespace"], row["name"])
            old = mapped.get(key)
            if old:
                out.append({**row, "disposition": old["disposition"],
                            "target": old["target"], "evidence": old["evidence"]})
                continue
            guess = guesses.get(key)
            verdict = guess["verdict"] if guess else "deferred"
            target = ""
            evidence = ""
            if guess and verdict in WITH_TARGET and len(guess["cands"]) == 1:
                target = guess["cands"][0]
                evidence = ";".join(guess["facts"])
                if guess["note"]:
                    evidence += " | " + guess["note"]
            elif guess:
                evidence = "| candidates: %s%s" % (
                    ",".join(guess["cands"][:6]) or "none",
                    ("; " + guess["note"]) if guess["note"] else "")
            else:
                evidence = "| UNCLASSIFIED — a human must read this row"
            if verdict in WITH_TARGET and not target:
                verdict = "family"
            out.append({**row, "disposition": verdict, "target": target,
                        "evidence": evidence})
        write_map(path, HEADER, out)
        print("wrote %s (%d rows)" % (path, len(out)))
        return 0

    if args.report or args.summary:
        counts = Counter()
        print("\t".join(COLUMNS + ["where", "src"]))
        for row in rows:
            key = (row["namespace"], row["name"])
            entry = mapped.get(key, {})
            counts[(row["namespace"], entry.get("disposition", "MISSING"))] += 1
            if args.report:
                print("\t".join([row["namespace"], row["name"], str(row["refs"]),
                                 str(row["lc_id"]), row["lc_layer"],
                                 entry.get("disposition", "MISSING"),
                                 entry.get("target", ""),
                                 entry.get("evidence", ""), row["where"],
                                 "%s@%s via %s" % row["src"] if row["src"] else ""]))
        if args.summary:
            for key in sorted(counts):
                print("%-9s %-10s %d" % (key[0], key[1], counts[key]))
            print("total %d" % len(rows))
        return 0

    if args.check:
        errors = check(trees, rows, mapped, existed)
        for message in errors:
            print("port_names_diff: %s" % message)
        if errors:
            print("port_names_diff: %d error(s)" % len(errors))
            return 1
        counts = Counter(r["disposition"] for r in mapped.values())
        print("port_names_diff: %d reference name(s), %d row(s) — %s" % (
            len(rows), len(mapped),
            ", ".join("%s %d" % (k, counts[k]) for k in sorted(counts))))
        return 0

    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
