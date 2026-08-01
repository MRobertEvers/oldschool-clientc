#!/usr/bin/env python3
"""
port_name_diff — the display-name and definition diff for the LostCity port.

    tools/port_name_diff.py --report          # the review artifact, TSV on stdout
    tools/port_name_diff.py --check           # the build bar
    tools/port_name_diff.py --write-signed    # re-baseline (a human decision)
    tools/port_name_diff.py --collisions      # what copying an id would land on

Why this exists (docs/LOSTCITY_PORT_TRIAGE.md §4.1, bar 6). Name resolution
answers "does something here have this spelling". It does not answer "is it the
same thing", and nothing in this repo asked the second question until now. The
worst case in the corpus passes every check that already exists:

    obj rock_sample1   id 671 in BOTH trees — the name resolves, the id agrees
                       LostCity: 'Rock sample 1', model_2393_obj
                       osrs239:  'Animal skull',  model 17290

A gate that only checks names cannot see that, because there is nothing to fail
to resolve. So this emits, for every name present in both trees, what each tree
says the record *is* — and a human signs the answer off. The signed baseline is
`<tree>/port/name_diff.signed`; `--check` fails when a row appears with no
signature, or when a signed row changes class. That is what makes "a human signs
it off" a build bar rather than a review habit.

What the verdict is *not* about: whether the two trees give the name the same id.
They usually do not — npc agrees on 0 of 1,073 — so a verdict firing on that
would fire on almost every row and tell a reviewer nothing. Id agreement is a
column (`id_same`), and the thing that makes copying an id dangerous is reported
separately by `--collisions`: for a name in both trees, what record does *this*
tree hold at LostCity's id? The answer is almost never "nothing".
"""

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")
DEFAULT_REF = os.path.expanduser("~/Documents/git_repos/LostCity_Server")

# The namespaces where both trees state a record and the record says what it is.
#
# `display` is the field a player reads; `shape` is the cheapest structural
# fingerprint the type has, and it is what carries the verdict for the two
# namespaces with no display name at all. A seq's frame count and a spotanim's
# model are exactly the cross-checks §9 step 3e uses by hand.
NAMESPACES = {
    "npc": {"display": "name", "shape": ["model1", "vislevel"]},
    "obj": {"display": "name", "shape": ["model", "cost"]},
    "loc": {"display": "name", "shape": ["model1", "width", "length"]},
    "seq": {"display": None, "shape": ["__framecount"]},
    "spotanim": {"display": None, "shape": ["model"]},
}

# LostCity's pack file for a namespace, when it is not spelled the same.
LC_PACK = {}

# `--collisions` asks a narrower question than the diff — "what does this tree
# hold at LostCity's id" — which needs only the two id tables, so it runs over
# every namespace both trees number rather than only the five that state a
# record. `varp` is here and not in NAMESPACES for exactly that reason, and it
# is the namespace the port's one proven swap lives in.
COLLISION_NAMESPACES = ["npc", "obj", "loc", "seq", "spotanim", "inv", "varp",
                        "varbit", "idk", "param", "enum", "struct", "dbtable",
                        "dbrow"]

SKIP_DIRS = ("_unpack", "_test")

TAG = re.compile(r"<[^>]*>")
WORD = re.compile(r"[A-Za-z0-9_]+")


# ---------------------------------------------------------------- parsing


def read_pack(path):
    """`id=name` lines -> ({name: id}, {id: name}). Later lines win, as the
    loaders do; a genuine duplicate is the symbol validators' problem, not
    this tool's."""
    by_name, by_id = {}, {}
    try:
        handle = open(path, "r", encoding="utf-8", errors="replace")
    except OSError:
        return by_name, by_id
    with handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("//"):
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
    """`[name]` sections of `key=value` lines. Repeated keys are kept as a list
    under the same key so `frame=` can be counted."""
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


def walk_authored(root, suffix):
    """Every authored `*.<suffix>` under `root`, merged. `_unpack` and `_test`
    are the reference's own decompiled dumps, not its authored tree — including
    them would compare this tree against a machine export of the cache LostCity
    was built from rather than against what LostCity states."""
    records = {}
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in sorted(files):
            if name.endswith(suffix):
                read_config(os.path.join(base, name), records)
    return records


def first(record, key):
    value = record.get(key)
    if isinstance(value, list):
        return value[0]
    return value


# An asset reference LostCity spells as a name around the id it was exported
# from: `model_2393_obj`, `anim_301`, `spot_spotanim_278`. This tree spells the
# same thing as the bare number. Reducing both to the number is what lets a
# structural compare mean anything across the two eras — without it every seq
# and spotanim row reads `shape-differs` and the column is noise.
ASSET_ID = re.compile(r"^[a-z][a-z_]*_(\d+)(?:_[a-z][a-z0-9_]*)?$")


def normalise_shape(value):
    if not value:
        return ""
    match = ASSET_ID.match(value)
    return match.group(1) if match else value


def shape_of(record, keys):
    out = []
    for key in keys:
        if key == "__framecount":
            # Two spellings of the same list: this tree repeats `frame=`, the
            # reference numbers them `frame1=`..`frameN=`.
            frames = record.get("frame")
            count = len(frames) if isinstance(frames, list) else (1 if frames else 0)
            count += sum(1 for k in record if re.fullmatch(r"frame\d+", k))
            out.append(str(count) if count else "")
        else:
            out.append(normalise_shape(first(record, key)))
    return ",".join(out)


# ---------------------------------------------------------------- verdicts


def normalise(text):
    """What two display names have to agree on to be the same name: the letters
    and digits. Colour tags, casing, spacing and punctuation are twenty years of
    re-styling and are not what this is looking for."""
    if not text:
        return ""
    return re.sub(r"[^a-z0-9]", "", TAG.sub("", text).lower())


def tokens(text):
    return {t for t in WORD.findall(TAG.sub("", (text or "").lower())) if len(t) > 2}


def verdict_for(has_display, lc_display, this_display, lc_shape, this_shape):
    """One of six classes. Ordered so the cheapest certainty wins, and so the
    class a reviewer must actually read (`different-thing`) is never reached by
    a row that merely got restyled."""
    if not has_display:
        # The namespace states no display name at all: the structural
        # fingerprint is the whole answer. `seq` and `spotanim` live here, and
        # it is the only signal 3e's frame-count and model-id cross-checks have.
        # Note this is *not* the same as a record that happens to be unnamed —
        # keeping the two apart is what stops 72 nameless objs reading as a
        # structural finding.
        if lc_shape == this_shape:
            return "identical"
        return "shape-differs"
    if not lc_display or not this_display:
        return "unnamed"
    if lc_display == this_display:
        return "identical"
    if normalise(lc_display) == normalise(this_display):
        return "formatting-only"
    left, right = tokens(lc_display), tokens(this_display)
    if left & right:
        return "plausible-sibling"
    ln, rn = normalise(lc_display), normalise(this_display)
    if ln and rn and (ln.startswith(rn) or rn.startswith(ln)):
        return "plausible-sibling"
    return "different-thing"


# ---------------------------------------------------------------- the rows


def build_rows(tree, ref, refs_index):
    rows = []
    for ns, spec in sorted(NAMESPACES.items()):
        this_names, _this_ids = read_pack(
            os.path.join(tree, "configs", "all.%s.compack" % ns))
        this_records = read_config(os.path.join(tree, "configs", "all.%s" % ns))
        lc_names, _lc_ids = read_pack(
            os.path.join(ref, "content", "pack", "%s.pack" % LC_PACK.get(ns, ns)))
        lc_records = walk_authored(os.path.join(ref, "content", "scripts"), "." + ns)

        for name in sorted(set(this_names) & set(lc_names)):
            this_rec = this_records.get(name, {})
            lc_rec = lc_records.get(name, {})
            if not lc_rec:
                # In LostCity's pack but stated nowhere in its authored tree:
                # a name reserved for content that lives in the cache it was
                # built from. Nothing to compare, so nothing to sign.
                continue
            key = spec["display"]
            lc_display = first(lc_rec, key) if key else None
            this_display = first(this_rec, key) if key else None
            lc_shape = shape_of(lc_rec, spec["shape"])
            this_shape = shape_of(this_rec, spec["shape"])
            rows.append({
                "namespace": ns,
                "name": name,
                "lc_id": lc_names[name],
                "this_id": this_names[name],
                "id_same": "yes" if lc_names[name] == this_names[name] else "no",
                "lc_display": lc_display or "",
                "this_display": this_display or "",
                "lc_shape": lc_shape,
                "this_shape": this_shape,
                "rs2_refs": refs_index.get(name, 0),
                "verdict": verdict_for(key is not None, lc_display, this_display,
                                       lc_shape, this_shape),
            })
    return rows


COLUMNS = ["namespace", "name", "lc_id", "this_id", "id_same", "lc_display",
           "this_display", "lc_shape", "this_shape", "rs2_refs", "verdict"]


def reference_index(ref):
    """How many times each name is a token in LostCity's authored `.rs2`. A row
    with no references is a name nothing calls, which is what tells a reviewer
    a `different-thing` verdict may cost nothing to defer."""
    counts = {}
    root = os.path.join(ref, "content", "scripts")
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if not name.endswith(".rs2"):
                continue
            try:
                with open(os.path.join(base, name), "r", encoding="utf-8",
                          errors="replace") as handle:
                    for token in WORD.findall(handle.read()):
                        counts[token] = counts.get(token, 0) + 1
            except OSError:
                pass
    return counts


# ---------------------------------------------------------------- signing

SIGNED_HEADER = """\
# Display-name and definition sign-off — docs/LOSTCITY_PORT_TRIAGE.md §4.1, bar 6.
#
# One line per name present in BOTH trees, as `namespace<TAB>name<TAB>verdict<TAB>signoff`.
# `tools/port_name_diff.py --check` fails when a row has no line here, or when a
# row's verdict is not the one signed. It does NOT fail on `unreviewed`: the
# baseline was signed wholesale so the bar could arm the day it was written
# rather than after a 652-row review, and what it catches from that moment is a
# row *changing class* — which is the event a review would have been looking for
# anyway.
#
# Signoff values: unreviewed | ok | wrong-record | deferred
# `wrong-record` is the outcome that matters: the name resolves and means
# something else. `rock_sample1` is the worked example.
"""


def authored_tokens(tree):
    """Every `\\w+` token this tree's own authored content uses.

    Only `server/scripts` — `configs/` is the machine export of the cache and
    mentions every name there is, so including it would make "is this name used
    here" always true."""
    names = set()
    root = os.path.join(tree, "server", "scripts")
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d != "build"]
        for name in files:
            try:
                with open(os.path.join(base, name), "r", encoding="utf-8",
                          errors="replace") as handle:
                    names.update(WORD.findall(handle.read()))
            except OSError:
                pass
    return names


def signed_path(tree):
    return os.path.join(tree, "port", "name_diff.signed")


def read_signed(path):
    signed = {}
    try:
        handle = open(path, "r", encoding="utf-8")
    except OSError:
        return None
    with handle:
        for line in handle:
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 3:
                continue
            signed[(parts[0], parts[1])] = (parts[2], parts[3] if len(parts) > 3 else "")
    return signed


def write_signed(path, rows, previous):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(SIGNED_HEADER)
        for row in rows:
            key = (row["namespace"], row["name"])
            # A signoff a human wrote survives a re-baseline; only the verdict
            # is recomputed. Losing an `ok` because the tool was re-run would
            # make re-running it expensive, and a bar nobody re-runs is stale.
            keep = previous.get(key, (None, "unreviewed"))[1] if previous else "unreviewed"
            handle.write("%s\t%s\t%s\t%s\n" % (row["namespace"], row["name"],
                                               row["verdict"], keep or "unreviewed"))


# ---------------------------------------------------------------- collisions


def collisions(tree, ref, limit):
    """What copying an id would land on.

    For every name in both trees whose ids disagree, the record *this* tree holds
    at LostCity's id. Triage §7 item 1: the answer is essentially never "nothing"
    — there is no fails-loudly case — and the rows worth reading are the ones
    where it is a *lexically similar* name, because those survive review."""
    out = []
    for ns in COLLISION_NAMESPACES:
        this_names, this_ids = read_pack(
            os.path.join(tree, "configs", "all.%s.compack" % ns))
        if not this_names:
            this_names, this_ids = read_pack(
                os.path.join(tree, "pack", "%s.pack" % ns))
        lc_names, _ = read_pack(
            os.path.join(ref, "content", "pack", "%s.pack" % LC_PACK.get(ns, ns)))
        for name in sorted(set(this_names) & set(lc_names)):
            lc_id = lc_names[name]
            if lc_id == this_names[name]:
                continue
            landed = this_ids.get(lc_id)
            similar = ""
            if landed:
                # Split on `_` rather than on word characters: every symbol here
                # is one `\w+` token, so a word-level comparison finds nothing
                # and `human_knife_slash` landing on `human_knife_chop` reads as
                # an ordinary collision. That is the class that survives review
                # — the reviewer sees a name that looks right.
                a = [p for p in name.split("_") if p]
                b = [p for p in landed.split("_") if p]
                shared = len(set(a) & set(b))
                prefix = len(os.path.commonprefix([name, landed]))
                if shared * 2 >= min(len(a), len(b)) and shared:
                    similar = "LEXICALLY-SIMILAR"
                elif prefix * 2 >= min(len(name), len(landed)):
                    similar = "LEXICALLY-SIMILAR"
            out.append((ns, name, lc_id, this_names[name], landed or "<nothing>", similar))
            if limit and len(out) >= limit:
                return out
    return out


# ---------------------------------------------------------------- main


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", default=DEFAULT_TREE)
    parser.add_argument("--ref", default=DEFAULT_REF)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write-signed", action="store_true")
    parser.add_argument("--collisions", action="store_true")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    if not os.path.isdir(os.path.join(args.ref, "content")):
        # Not a failure. The reference is a second checkout that a build machine
        # is not required to have, and a bar that cannot run is not a bar that
        # failed — saying so is what keeps the difference visible.
        print("port_name_diff: no reference tree at %s — nothing to diff" % args.ref)
        return 0

    if args.collisions:
        rows = collisions(args.tree, args.ref, args.limit)
        similar = sum(1 for r in rows if r[5])
        print("namespace\tname\tlc_id\tthis_id\tthis_tree_holds_lc_id\tnote")
        for row in rows:
            print("\t".join(str(c) for c in row))
        print("# %d name(s) would land on a different record if the id were copied; "
              "%d of them on a lexically similar name" % (len(rows), similar),
              file=sys.stderr)
        return 0

    refs_index = reference_index(args.ref) if (args.report or args.write_signed) else {}
    rows = build_rows(args.tree, args.ref, refs_index)

    if args.report:
        print("\t".join(COLUMNS))
        for row in rows:
            print("\t".join(str(row[c]) for c in COLUMNS))
        tally = {}
        for row in rows:
            tally[row["verdict"]] = tally.get(row["verdict"], 0) + 1
        print("# %d row(s): %s" % (len(rows), ", ".join(
            "%s %d" % (k, v) for k, v in sorted(tally.items()))), file=sys.stderr)
        return 0

    path = signed_path(args.tree)
    previous = read_signed(path)

    if args.write_signed:
        write_signed(path, rows, previous)
        print("port_name_diff: signed %d row(s) into %s" % (len(rows), path))
        return 0

    if args.check:
        if previous is None:
            print("port_name_diff: no %s — run --write-signed once to baseline it" % path,
                  file=sys.stderr)
            return 1
        authored = authored_tokens(args.tree)
        problems = 0
        seen = set()
        for row in rows:
            key = (row["namespace"], row["name"])
            seen.add(key)
            if key not in previous:
                print("port_name_diff: %s `%s` is new and unsigned (%s: '%s' here, '%s' "
                      "there) — sign it in %s" %
                      (row["namespace"], row["name"], row["verdict"], row["this_display"],
                       row["lc_display"], os.path.relpath(path, REPO)), file=sys.stderr)
                problems += 1
            elif previous[key][0] != row["verdict"]:
                print("port_name_diff: %s `%s` changed class, %s -> %s — re-sign it" %
                      (row["namespace"], row["name"], previous[key][0], row["verdict"]),
                      file=sys.stderr)
                problems += 1
            elif previous[key][1] == "wrong-record":
                # A name a reviewer found to mean something else. Fatal only once
                # *this* tree's authored content names it — otherwise recording
                # the finding would break the build, and a bar that punishes
                # honesty gets signed `ok` instead.
                used = row["name"] in authored
                print("port_name_diff: %s `%s` is signed `wrong-record` (%s here vs %s "
                      "there)%s" %
                      (row["namespace"], row["name"], row["this_display"] or "<unnamed>",
                       row["lc_display"] or "<unnamed>",
                       " and this tree's content names it — map it, do not resolve it"
                       if used else " — not referenced here, so nothing depends on it yet"),
                      file=sys.stderr)
                if used:
                    problems += 1
        gone = sorted(k for k in previous if k not in seen)
        for key in gone:
            print("port_name_diff: %s `%s` is signed but no longer in both trees" % key,
                  file=sys.stderr)
        print("port_name_diff: %d row(s) checked, %d signed, %d problem(s)%s" %
              (len(rows), len(previous), problems,
               ", %d stale" % len(gone) if gone else ""))
        return 1 if problems else 0

    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
