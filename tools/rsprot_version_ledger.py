#!/usr/bin/env python3
"""RSProt packet **layout-version** ledger generator.

The unit of versioning for our C packet codecs is the *layout*, not the
revision.  RSProt vendors one Kotlin module per revision (protocol/osrs-221 ..
protocol/osrs-239); most encoder/decoder classes are byte-identical across long
runs of revisions, and RSProt's own ``V2``/``V1``/``Old`` class-name suffixes do
NOT track layout changes (``IfOpenTopEncoder`` keeps its name across many
distinct layouts).  So we derive the truth mechanically: hash the body of each
``encode``/``decode`` function and give every distinct body its own version
number.

============================================================================
THE ONE INVARIANT: VERSION NUMBERS ARE APPEND-ONLY.
============================================================================
Once a (class, direction, content-hash) triple has been assigned a version
number and written to the ledger, that number is frozen forever.  C tables,
generated codecs, wire tests and hand-written call sites key off these numbers;
silently renumbering them would repoint working code at the wrong layout with
no compile error and no test failure.  Therefore:

  * an existing (class, direction, hash) -> version assignment is *reused*
    verbatim, never recomputed;
  * only hashes that are not already in the ledger get fresh numbers, allocated
    from ``max(existing) + 1`` upward in ascending-revision order;
  * any condition that would change an existing assignment -- a renumbered row,
    a duplicated version, a gap in the 1..N sequence, a version that is out of
    ascending-revision order, a hash that would have to move -- makes the run
    ABORT with a loud, specific error.  The tool never "repairs" the ledger by
    renumbering.

If a class is removed from the vendored tree, its rows are *kept* (marked
``RETIRED`` in the REVS column) so that its numbers stay reserved and a later
re-vendor of the same layout gets its original number back.

Legitimate exception: vendoring a revision *older* than everything already in
the ledger (a backport) can introduce a layout whose first-seen revision sorts
before layouts that already hold lower version numbers.  That breaks the
ascending-revision ordering check but not append-only-ness, so it requires the
explicit ``--allow-backport`` flag.  It is not the default, because the same
symptom is what a hand-edited ledger looks like.

----------------------------------------------------------------------------
Method
----------------------------------------------------------------------------
For every ``*Encoder.kt`` / ``*Decoder.kt`` under each vendored revision:
  1. strip comments (comments cannot change the wire layout, and a reworded
     comment must not mint a new version);
  2. extract the *function bodies* declared in the file, in source order --
     block bodies and expression bodies, nested functions included -- and
     nothing else: no package line, no imports, no class header, no ``prot``
     property, no annotations, no companion constants;
  3. normalise: revision-specific tokens (``osrs239``, ``osrs-239``, ``Osrs239``)
     collapse to ``osrsREV``, then all whitespace runs collapse to one space;
  4. SHA-256 the result and keep the first 12 hex digits as the layout hash.

Step 2 deliberately takes *every* function in the codec file, not just the one
named ``encode``/``decode``.  A codec file exists to serialise one packet, so
every function in it is part of that packet's layout, and restricting the hash
to the entry point silently loses real changes.  Measured on this tree:
``NpcHitEncoder.encode`` is a two-line dispatcher into private same-file
``pHits``/``pHeadBars`` helpers, so an entry-point-only hash reports **1**
layout across revisions 221-236 when there are in fact **16**.  Same failure for
``PlayerHitEncoder`` (3 vs 18) and ``PlayerAppearanceEncoder`` (10 vs 16).  The
extended-info encoders also name their entry point ``precompute``, not
``encode``, and would hash to nothing at all under a strict name match.

Known limitation, stated rather than hidden: the hash covers one file.  A layout
change that lives entirely in a *cross-file* shared helper (``LoginBlockDecoder``,
the ``PlayerInfo`` bit-packing, ``JagByteBuf`` itself) does not move the hash of
the class that calls it.  Those helpers need versioning of their own; this
ledger does not claim to cover them.

Direction is RSProt-relative (RSProt is a server library):
    ``out`` = ``*Encoder.kt`` = server -> client
    ``in``  = ``*Decoder.kt`` = client -> server

Usage:
    python3 tools/rsprot_version_ledger.py                 # regenerate in place
    python3 tools/rsprot_version_ledger.py --check-only    # verify, write nothing
    python3 tools/rsprot_version_ledger.py --rsprot DIR --out FILE
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
from collections import defaultdict

DEFAULT_RSPROT = os.path.expanduser("~/Documents/git_repos/rsprot")
DEFAULT_OUT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "3rd", "rsprot", "gen", "packet_versions.txt",
)

HASH_LEN = 12
RETIRED = "RETIRED"
COLUMNS = ("CLASS", "DIR", "MODULE", "VER", "HASH", "REVS")


class LedgerError(Exception):
    """Raised for any condition that would violate the append-only invariant."""


# ---------------------------------------------------------------------------
# Kotlin source scanning
# ---------------------------------------------------------------------------

def mask_source(src: str) -> tuple[str, str]:
    """Return (clean, masked).

    ``clean`` is the source with comments replaced by equal-length spaces.
    ``masked`` is ``clean`` with the *contents* of string/char literals also
    replaced by spaces, so that braces and parens inside literals cannot
    confuse the structural scan.  Both strings have identical length and index
    space, so structure is found on ``masked`` and text is sliced from
    ``clean``.
    """
    n = len(src)
    clean = list(src)
    masked = list(src)
    i = 0

    def blank(a: int, b: int, both: bool) -> None:
        for k in range(a, b):
            ch = src[k]
            repl = ch if ch == "\n" else " "
            masked[k] = repl
            if both:
                clean[k] = repl

    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            blank(i, j, True)
            i = j
        elif c == "/" and nxt == "*":
            depth = 1  # Kotlin block comments nest
            j = i + 2
            while j < n and depth:
                if src[j] == "/" and j + 1 < n and src[j + 1] == "*":
                    depth += 1
                    j += 2
                elif src[j] == "*" and j + 1 < n and src[j + 1] == "/":
                    depth -= 1
                    j += 2
                else:
                    j += 1
            blank(i, j, True)
            i = j
        elif src.startswith('"""', i):
            j = src.find('"""', i + 3)
            j = n if j < 0 else j + 3
            blank(i, j, False)
            i = j
        elif c == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == '"':
                    j += 1
                    break
                if src[j] == "\n":
                    break
                j += 1
            blank(i, j, False)
            i = j
        elif c == "'":
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == "'":
                    j += 1
                    break
                if src[j] == "\n":
                    break
                j += 1
            blank(i, j, False)
            i = j
        else:
            i += 1
    return "".join(clean), "".join(masked)


# Every function declared in a codec file contributes to the layout -- see the
# module docstring for why an `encode`/`decode`-only match loses real changes.
FUN_RE = re.compile(r"\bfun\s+(?:<[^>]*>\s*)?(\w+)\s*\(")
EMPTY_HASH = hashlib.sha256(b"").hexdigest()[:HASH_LEN]
CONTINUATION_RE = re.compile(r"^(\.|\?:|\?\.|\+|\*|/|&&|\|\||==|!=|>=|<=|=|,|\)|]|else\b|,)")

OPENERS = "([{"
CLOSERS = ")]}"


def _match_paren(masked: str, open_idx: int) -> int:
    """Index just past the ``)`` matching the ``(`` at ``open_idx``."""
    depth = 0
    i = open_idx
    n = len(masked)
    while i < n:
        c = masked[i]
        if c in OPENERS:
            depth += 1
        elif c in CLOSERS:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise LedgerError("unbalanced parentheses in source")


def _match_brace(masked: str, open_idx: int) -> int:
    """Index of the ``}`` matching the ``{`` at ``open_idx``."""
    depth = 0
    i = open_idx
    n = len(masked)
    while i < n:
        c = masked[i]
        if c in OPENERS:
            depth += 1
        elif c in CLOSERS:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise LedgerError("unbalanced braces in source")


def _expression_body_end(masked: str, start: int) -> int:
    """End index of an ``= expr`` function body beginning at ``start``."""
    i = start
    n = len(masked)
    depth = 0
    while i < n:
        c = masked[i]
        if c in OPENERS:
            depth += 1
        elif c in CLOSERS:
            if depth == 0:
                return i  # closing brace of the enclosing class/object
            depth -= 1
        elif c == "\n" and depth == 0:
            j = i + 1
            while j < n and masked[j] in " \t\r\n":
                j += 1
            if j >= n:
                return i
            if CONTINUATION_RE.match(masked[j:j + 8]):
                i += 1
                continue
            return i
        i += 1
    return n


def extract_codec_bodies(src: str) -> tuple[list[str], int]:
    """Return (bodies, n_expression_bodied) for every function in the file.

    Functions declared without a body (interface members such as
    ``OnDemandExtendedInfoEncoder.encode``) contribute nothing, so a pure
    interface -- and a ``NoOpMessageEncoder`` subclass, which declares no
    function at all -- yields an empty body list.  That is the correct answer
    for a zero-payload packet, and it hashes to a stable constant.
    """
    clean, masked = mask_source(src)
    bodies: list[str] = []
    n_expr = 0
    for m in FUN_RE.finditer(masked):
        after_params = _match_paren(masked, m.end() - 1)
        i = after_params
        n = len(masked)
        while i < n and masked[i] not in "{=":
            if masked[i] in ")}":  # ran off the end of the declaration
                break
            i += 1
        if i >= n or masked[i] not in "{=":
            continue
        if masked[i] == "{":
            end = _match_brace(masked, i)
            bodies.append(clean[i + 1:end])
        else:
            end = _expression_body_end(masked, i + 1)
            bodies.append(clean[i + 1:end])
            n_expr += 1
    return bodies, n_expr


REV_TOKEN_RE = re.compile(r"(?i)(osrs)[-_]?\d{3}")
WS_RE = re.compile(r"\s+")


def normalise(bodies: list[str]) -> str:
    text = "\n;;\n".join(bodies)
    text = REV_TOKEN_RE.sub(lambda m: m.group(1) + "REV", text)
    text = WS_RE.sub(" ", text).strip()
    return text


def layout_hash(norm: str) -> str:
    return hashlib.sha256(norm.encode("utf-8")).hexdigest()[:HASH_LEN]


# ---------------------------------------------------------------------------
# Scanning the vendored tree
# ---------------------------------------------------------------------------

REV_DIR_RE = re.compile(r"^osrs-(\d+)$")


def discover_revisions(rsprot_root: str) -> list[int]:
    proto = os.path.join(rsprot_root, "protocol")
    if not os.path.isdir(proto):
        raise LedgerError(f"no protocol/ directory under {rsprot_root!r}")
    revs = []
    for name in os.listdir(proto):
        m = REV_DIR_RE.match(name)
        if m and os.path.isdir(os.path.join(proto, name)):
            revs.append(int(m.group(1)))
    if not revs:
        raise LedgerError(f"no protocol/osrs-<rev> modules under {rsprot_root!r}")
    return sorted(revs)


MODULE_RE = re.compile(r"^osrs-\d+-(\w+)$")


def scan(rsprot_root: str, revs: list[int]):
    """-> (observations, modules, stats).

    observations: {(class, dir): {hash: sorted[rev]}}
    modules:      {(class, dir): module}
    """
    observations: dict[tuple[str, str], dict[str, list[int]]] = defaultdict(
        lambda: defaultdict(list))
    modules: dict[tuple[str, str], str] = {}
    stats = {"files": 0, "empty_bodies": 0, "expr_bodies": 0}
    for rev in revs:
        base = os.path.join(rsprot_root, "protocol", f"osrs-{rev}")
        seen_in_rev: dict[tuple[str, str], str] = {}
        for dirpath, _dirnames, filenames in os.walk(base):
            for fn in sorted(filenames):
                if fn.endswith("Encoder.kt"):
                    direction = "out"
                elif fn.endswith("Decoder.kt"):
                    direction = "in"
                else:
                    continue
                path = os.path.join(dirpath, fn)
                cls = fn[:-3]
                key = (cls, direction)
                rel = os.path.relpath(path, base)
                mod_m = MODULE_RE.match(rel.split(os.sep)[0])
                module = mod_m.group(1) if mod_m else "?"
                if key in seen_in_rev:
                    raise LedgerError(
                        f"duplicate class basename {cls!r} in osrs-{rev}: "
                        f"{seen_in_rev[key]} and {rel} -- the (class, direction) "
                        f"key is not unique, the ledger cannot be keyed on it")
                seen_in_rev[key] = rel
                prev_mod = modules.get(key)
                if prev_mod is not None and prev_mod != module:
                    raise LedgerError(
                        f"class {cls!r} moved module: {prev_mod} -> {module} "
                        f"at osrs-{rev}")
                modules[key] = module
                with open(path, "r", encoding="utf-8") as fh:
                    src = fh.read()
                bodies, n_expr = extract_codec_bodies(src)
                stats["files"] += 1
                stats["expr_bodies"] += n_expr
                if not bodies:
                    stats["empty_bodies"] += 1
                norm = normalise(bodies)
                observations[key][layout_hash(norm)].append(rev)
    return observations, modules, stats


def rev_ranges(revs: list[int]) -> str:
    out = []
    start = prev = revs[0]
    for r in revs[1:]:
        if r == prev + 1:
            prev = r
            continue
        out.append(f"{start}-{prev}" if start != prev else f"{start}")
        start = prev = r
    out.append(f"{start}-{prev}" if start != prev else f"{start}")
    return ",".join(out)


# ---------------------------------------------------------------------------
# Ledger I/O + the append-only guard
# ---------------------------------------------------------------------------

HASH_RE = re.compile(r"^[0-9a-f]{%d}$" % HASH_LEN)


def load_ledger(path: str):
    """-> {(class, dir): {hash: (version, module, revs_field)}}."""
    existing: dict[tuple[str, str], dict[str, tuple[int, str, str]]] = defaultdict(dict)
    if not os.path.exists(path):
        return existing
    with open(path, "r", encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if tuple(parts) == COLUMNS:  # the column-header row
                continue
            if len(parts) != len(COLUMNS):
                raise LedgerError(
                    f"{path}:{lineno}: expected {len(COLUMNS)} tab-separated "
                    f"columns {COLUMNS}, got {len(parts)}: {line!r}")
            cls, direction, module, ver_s, hsh, revs = parts
            if direction not in ("in", "out"):
                raise LedgerError(
                    f"{path}:{lineno}: bad direction {direction!r} (want in/out)")
            if not HASH_RE.match(hsh):
                raise LedgerError(
                    f"{path}:{lineno}: bad hash {hsh!r} "
                    f"(want {HASH_LEN} lowercase hex digits)")
            try:
                ver = int(ver_s)
            except ValueError:
                raise LedgerError(
                    f"{path}:{lineno}: version {ver_s!r} is not an integer") from None
            if ver < 1:
                raise LedgerError(f"{path}:{lineno}: version {ver} must be >= 1")
            key = (cls, direction)
            if hsh in existing[key]:
                raise LedgerError(
                    f"{path}:{lineno}: hash {hsh} listed twice for {cls} [{direction}]")
            existing[key][hsh] = (ver, module, revs)
    return existing


def check_ledger_integrity(existing, path: str) -> None:
    """A valid append-only ledger numbers each (class, dir) exactly 1..N."""
    for (cls, direction), rows in sorted(existing.items()):
        versions = sorted(v for v, _m, _r in rows.values())
        expected = list(range(1, len(versions) + 1))
        if versions != expected:
            dupes = {v for v in versions if versions.count(v) > 1}
            detail = f"duplicate version(s) {sorted(dupes)}" if dupes else \
                f"expected {expected}, found {versions}"
            raise LedgerError(
                f"APPEND-ONLY VIOLATION in {path}: {cls} [{direction}] does not "
                f"number its layouts 1..{len(versions)} -- {detail}.\n"
                f"  Version numbers are append-only and dense: every layout ever "
                f"recorded for a class keeps its number, and the Nth layout is "
                f"version N. A gap, a duplicate or an out-of-range number means "
                f"the ledger was hand-edited or written by a different tool.\n"
                f"  Rows for {cls} [{direction}]:\n" +
                "".join(f"    ver={v:<4} hash={h} revs={r}\n"
                        for h, (v, _m, r) in sorted(rows.items(), key=lambda kv: kv[1][0])) +
                f"  Fix the ledger (git checkout {path}) rather than renumbering; "
                f"this tool will not renumber an existing assignment.")


def assign_versions(observations, existing, path: str, allow_backport: bool):
    """Allocate version numbers, preserving every existing assignment."""
    assignments: dict[tuple[str, str], list[tuple[int, str, list[int]]]] = {}
    reused = 0
    fresh = 0
    for key in sorted(set(observations) | set(existing)):
        obs = observations.get(key, {})
        prior = existing.get(key, {})
        # first-seen revision decides allocation order
        ordered = sorted(obs.items(), key=lambda kv: (kv[1][0], kv[0]))
        taken = {v for v, _m, _r in prior.values()}
        rows: list[tuple[int, str, list[int]]] = []
        next_free = (max(taken) + 1) if taken else 1
        for hsh, revs in ordered:
            if hsh in prior:
                ver = prior[hsh][0]
                reused += 1
            else:
                ver = next_free
                next_free += 1
                if ver in taken:  # cannot happen; assert the invariant anyway
                    raise LedgerError(
                        f"APPEND-ONLY VIOLATION: new layout {hsh} for "
                        f"{key[0]} [{key[1]}] would take version {ver}, which is "
                        f"already assigned to another layout")
                taken.add(ver)
                fresh += 1
            rows.append((ver, hsh, revs))
        # retired layouts: keep their numbers reserved
        for hsh, (ver, _mod, _revs) in prior.items():
            if hsh not in obs:
                rows.append((ver, hsh, []))
        rows.sort(key=lambda r: r[0])
        # the invariant, restated as an executable check
        for hsh, (ver, _m, _r) in prior.items():
            got = next(v for v, h, _rv in rows if h == hsh)
            if got != ver:
                raise LedgerError(
                    f"APPEND-ONLY VIOLATION: {key[0]} [{key[1]}] layout {hsh} was "
                    f"version {ver} in {path}, this run computed {got}")
        if not allow_backport:
            first_seen = {h: (r[0] if r else None) for _v, h, r in rows}
            live = [(v, h) for v, h, r in rows if r]
            for (v1, h1), (v2, h2) in zip(live, live[1:]):
                if first_seen[h1] is not None and first_seen[h2] is not None \
                        and first_seen[h1] > first_seen[h2]:
                    raise LedgerError(
                        f"APPEND-ONLY VIOLATION in {path}: {key[0]} [{key[1]}] "
                        f"version {v1} (hash {h1}, first seen at osrs-{first_seen[h1]}) "
                        f"sorts before version {v2} (hash {h2}, first seen at "
                        f"osrs-{first_seen[h2]}).\n"
                        f"  Versions must ascend with the revision that first "
                        f"introduced the layout. Two version numbers were swapped, "
                        f"or a revision older than the whole ledger was vendored.\n"
                        f"  If this really is a backport, re-run with "
                        f"--allow-backport. Otherwise restore the ledger "
                        f"(git checkout {path}).")
        assignments[key] = rows
    return assignments, reused, fresh


HEADER = """\
# 3rd/rsprot/gen/packet_versions.txt -- RSProt packet LAYOUT-VERSION ledger
#
# GENERATED FILE. Do not hand-edit.
#   generator : tools/rsprot_version_ledger.py
#   regenerate: python3 tools/rsprot_version_ledger.py
#   source    : {source}
#   revisions : {revlist}
#
# VERSION NUMBERS ARE APPEND-ONLY.
#   A (CLASS, DIR, HASH) triple keeps its VER forever. New layouts get the next
#   free number; existing numbers are never reassigned, renumbered or reused.
#   The generator aborts rather than change one, so C tables and generated
#   codecs can hard-code these numbers. Each class numbers its layouts 1..N in
#   ascending order of the revision that first introduced them.
#
# HASH is the first {hashlen} hex digits of the SHA-256 of the normalised bodies of
# every function declared in the codec's .kt file, in source order: comments
# stripped, revision tokens folded to "osrsREV", whitespace collapsed. Nothing
# outside a function body contributes (no package, imports, class header, prot
# property or companion constants). Private same-file helpers ARE included --
# NpcHitEncoder.encode is a two-line dispatcher into pHits()/pHeadBars(), and an
# entry-point-only hash would report 1 layout for 221-236 instead of 16.
# A layout change living entirely in a CROSS-FILE helper (LoginBlockDecoder,
# PlayerInfo bit-packing, JagByteBuf) does NOT move this hash.
# HASH {emptyhash} is the empty body: a zero-payload packet
# (NoOpMessageEncoder) or a body-less interface declaration.
#
# DIR is RSProt-relative (RSProt is a server library):
#   out = *Encoder.kt = server -> client
#   in  = *Decoder.kt = client -> server
#
# REVS lists the revisions whose module contains that exact layout, as ranges
# (e.g. "221-230,233"). {retired} means the layout is no longer present in any
# vendored revision; its number stays reserved.
#
# Columns are tab-separated:
#   {cols}
#
# SUMMARY
#   classes (class,dir pairs)  : {n_classes}
#     out (encoders)           : {n_out}
#     in  (decoders)           : {n_in}
#   distinct layouts           : {n_layouts}
#     out                      : {n_layouts_out}
#     in                       : {n_layouts_in}
#   layouts per class          : min {min_l}, max {max_l}, mean {mean_l:.2f}
#   classes with 1 layout      : {n_stable}   (never change across their revisions)
#   classes changing every rev : {n_churn}
#   classes whose every layout : {n_contig}
#     is one contiguous run       (their C table row is a single range)
#   contiguous revision runs   : {n_runs}
#     total, i.e. what a naive per-revision-run scheme would need. Deduplicating
#     recurring layouts by hash saves {n_saved} codec bodies. Recurrence is real:
#     IfOpenTopEncoder cycles p2/p2Alt1/p2Alt2/p2Alt3 over 13 runs but only 4
#     distinct layouts, and revs 223-224 and 227-230 are byte-identical files.
"""


def render(assignments, modules, revs, source, existing_modules) -> str:
    n_classes = len(assignments)
    n_out = sum(1 for (_c, d) in assignments if d == "out")
    n_in = n_classes - n_out
    n_layouts = sum(len(v) for v in assignments.values())
    n_layouts_out = sum(len(v) for (_c, d), v in assignments.items() if d == "out")
    n_layouts_in = n_layouts - n_layouts_out
    counts = [len(v) for v in assignments.values()] or [0]
    n_stable = sum(1 for c in counts if c == 1)
    n_rev = len(revs)
    n_contig = 0
    n_runs = 0
    n_churn = 0
    for rows in assignments.values():
        by_rev = {}
        for _ver, hsh, rlist in rows:
            for r in rlist:
                by_rev[r] = hsh
        seq = sorted(by_rev)
        runs = 0
        for i, r in enumerate(seq):
            if i == 0 or r != seq[i - 1] + 1 or by_rev[r] != by_rev[seq[i - 1]]:
                runs += 1
        n_runs += runs
        if runs == n_rev and len(seq) == n_rev:
            n_churn += 1
        if all("," not in rev_ranges(r) for _ver, _h, r in rows if r):
            n_contig += 1
    head = HEADER.format(
        source=source,
        revlist=f"osrs-{revs[0]} .. osrs-{revs[-1]} ({n_rev} vendored)",
        hashlen=HASH_LEN,
        retired=RETIRED,
        emptyhash=EMPTY_HASH,
        cols="  ".join(COLUMNS),
        n_classes=n_classes, n_out=n_out, n_in=n_in,
        n_layouts=n_layouts, n_layouts_out=n_layouts_out, n_layouts_in=n_layouts_in,
        min_l=min(counts), max_l=max(counts), mean_l=sum(counts) / len(counts),
        n_stable=n_stable, n_churn=n_churn, n_contig=n_contig,
        n_runs=n_runs, n_saved=n_runs - n_layouts,
    )
    lines = [head, "\t".join(COLUMNS) + "\n"]
    for (cls, direction), rows in sorted(assignments.items()):
        module = modules.get((cls, direction)) or existing_modules.get(
            (cls, direction), "?")
        for ver, hsh, rlist in rows:
            revs_field = rev_ranges(rlist) if rlist else RETIRED
            lines.append("\t".join(
                (cls, direction, module, str(ver), hsh, revs_field)) + "\n")
    return "".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rsprot", default=DEFAULT_RSPROT,
                    help="path to the RSProt checkout (default: %(default)s)")
    ap.add_argument("--out", default=DEFAULT_OUT,
                    help="ledger path (default: %(default)s)")
    ap.add_argument("--check-only", action="store_true",
                    help="verify the ledger is current; write nothing")
    ap.add_argument("--allow-backport", action="store_true",
                    help="permit a new layout whose first revision precedes "
                         "existing, higher-numbered layouts")
    args = ap.parse_args(argv)

    try:
        revs = discover_revisions(args.rsprot)
        observations, modules, stats = scan(args.rsprot, revs)
        existing = load_ledger(args.out)
        check_ledger_integrity(existing, args.out)
        existing_modules = {k: next(iter(v.values()))[1]
                            for k, v in existing.items() if v}
        assignments, reused, fresh = assign_versions(
            observations, existing, args.out, args.allow_backport)
        text = render(assignments, modules, revs, args.rsprot, existing_modules)
    except LedgerError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    old = None
    if os.path.exists(args.out):
        with open(args.out, "r", encoding="utf-8") as fh:
            old = fh.read()
    changed = old != text

    n_layouts = sum(len(v) for v in assignments.values())
    print(f"scanned {stats['files']} codec files across "
          f"{len(revs)} revisions (osrs-{revs[0]}..osrs-{revs[-1]})")
    print(f"  {len(assignments)} (class, direction) pairs, "
          f"{n_layouts} distinct layouts")
    print(f"  versions: {reused} reused from ledger, {fresh} newly assigned")
    if stats["empty_bodies"]:
        print(f"  note: {stats['empty_bodies']} file(s) had no encode/decode body")

    if args.check_only:
        print("ledger is UP TO DATE" if not changed else
              f"ledger is STALE: {args.out} differs from a fresh generation")
        return 0 if not changed else 1

    if not changed:
        print(f"no-op: {args.out} already byte-identical")
        return 0
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
