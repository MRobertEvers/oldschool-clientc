#!/usr/bin/env python3
"""Allocate ids for the server's own namespaces, LostCity's `pack.max++` rule.

A RuneScript config declares a record by *name* — `[coord_pair_table]`,
`[displaymessage_enum]` — and both the compiler and the runtime need a *number*
for it. For namespaces the cache owns (obj, npc, loc) the number is the cache's
and `pack/<ns>.pack` states it. For namespaces the **server** owns (enum,
struct, dbtable, dbrow, param, varn, vars, hunt) nothing states it, and LostCity
computes it in its packer as `max + 1`.

This is that packer. Without it, every server-authored config block is a name the
compiler cannot resolve, which is what porting the reference's content runs into
about four files in.

    tools/ss_allocate.py --tree OSRS-Content/osrs239-content [--check]

Three rules, and each one is a bug this tree has already paid for once:

**The base is layer 0's high-water mark, not a round number.** `configs/all.param.compack`
first allocated from 2000 because "surely nothing is up here"; cache.osrs239
defines params 0..2633 with no gaps, so nine names silently aliased real ones.
The base here is one past the largest id layer 0 holds, counting both
`pack/<ns>.pack` and the numeric `[<ns>_N]` blocks in `configs/all.<ns>`.

**An assignment, once made, is never changed.** Existing lines are read back and
preserved; only names that have no id yet get one. Sorted order is used for new
names so two machines allocate identically, but a *rename* in the content is a
new name and a stale line, not a renumber.

**Everything above the marker line is human.** This only ever rewrites below the
marker, so a hand-written header survives a regeneration — which is exactly what
`cachepack unpack` destroying `configs/all.param.compack`'s header taught
(docs/CONTENT_ARCHITECTURE.md §3.2).

**Output goes to the server's allocation ledger, `pack/<ns>.alloc`** — never to
`configs/all.<ns>.compack`, which is the cache's member index and the file
`cachepack unpack` regenerates. The two are layers of one namespace: every
reader (torirs_server_content.c, ssc_symbols.c, cachepack's cp_names.c) loads the
compack and then the ledger, and refuses a name bound in both. This tool used
to append below a marker *inside* the compack, which meant every server feature
edited a client-side file and `--gamevals` carried the server's dbrow names
into the client cache's own symbol table (gameval archive 9 — a table nothing
reads). The cache layer is still *read* here, for membership and for the
high-water mark; it is just never written.

Being in the ledger is also a routing statement: cachepack's entity gate treats
an alloc-claimed record as the server's without a `pack/<ns>.server` line — the
allocation is the membership. Restating every allocated name in a membership
roster was measured to drift within days (560 varps/enums/params/structs
allocated after the 2026-08-02 seeding, all cell-(c) errors).

**The floor comes from the register, not from a copy of it.** `declared_base`
reads `content.ini` and falls back to `src/content/content_register.c` — see
`register_bases`, and the paragraph on `varp` in that file for what the previous
arrangement, where the floor was read from a file that states none, cost.

A namespace whose ids are the *server's* — enum, struct, dbtable, dbrow — has no
gameval archive behind it, so its pack file is authored outright and cachepack
must not rewrite it. That is the register's `names = authored` row, and it is the
only thing keeping these allocations alive.

`--check` exits non-zero if anything would change, for use as a build gate.
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

MARKER = '// --- allocated below this line by tools/ss_allocate.py; do not hand-edit ---'

# The namespaces whose ids are the server's to choose, when the tree says nothing.
# `category` is deliberately absent: an obj's category is a *cache* field (config
# opcode 94), so those ids are read off the cache and cannot be allocated. See
# pack/category.pack.
#
# This is a **default**, not the answer — `server_namespaces()` below unions it
# with whatever `content.ini` declares. Two tables that had to agree by hand is
# the drift the register exists to remove, and this one had already drifted:
# `param` sat in this tuple while the register said `ids = cache`, so the
# allocator was allocating params the compiler then refused to resolve. The
# conclusion drawn at the time was "content cannot own this rule", which is
# exactly backwards — see docs/CONTENT_ARCHITECTURE.md §8.2(c).
SERVER_NAMESPACES = (
    'enum', 'struct', 'dbtable', 'dbrow', 'param', 'mesanim', 'inv', 'varn',
    # `varp` is here because the `ids` axis was retired, and it is the ONLY
    # namespace that axis ever contributed — measured against the tree, not
    # assumed. It is swept so content can declare the `%com_*` combat stat block
    # the reference computes in `[proc,player_combat_stat]`; before the promotion
    # no tool would hand those varps an id.
    'varp',
    # `vars` — world-shared variables — is swept because the cache names none of
    # them. There is no `vars` config group to layer over, so unlike every other
    # namespace here the alloc ledger IS the namespace: a `[block]` with no line
    # in `pack/vars.alloc` resolves to nothing at all, and `%name = 1` fails to
    # compile rather than writing somewhere wrong.
    'vars',
)


def server_namespaces(tree):
    """Every namespace this tool sweeps.

    One table, deliberately. This used to be a union of the tuple above with
    whatever `content.ini` declared `ids = server`, and that was the drift the
    register exists to remove rather than an instance of it: two authorities for
    one fact, agreeing by hand. It had already failed once — `param` sat in the
    tuple while the register said `ids = cache`, so the allocator handed out
    params the compiler then refused to resolve.

    The axis is gone because it decided nothing: ids are assigned by the pack
    files, and `npc` declared `ids = cache` while allocating from a base of
    20000. What a namespace needs stated is where a new id starts, which is
    `server_base` in the register, and whether this tool sweeps it, which is the
    tuple above. `tree` is kept in the signature so callers do not change.
    """
    del tree
    return tuple(sorted(SERVER_NAMESPACES))
    section = None
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            line = re.sub(r'[;#].*$', '', line).strip()
            m = re.match(r'\[namespace:([A-Za-z0-9_]+)\]$', line)
            if m:
                section = m.group(1)
                continue
            if section and '=' in line:
                key, value = (p.strip() for p in line.split('=', 1))
                if key == 'ids':
                    (found.add if value == 'server' else found.discard)(section)
    return tuple(sorted(found))


# The four namespaces whose names do NOT live beside a config archive.
#
# This mirrors `pack_kind_is_config()` in src/torirsserver/torirs_server_content.c and has to:
# the runtime reads `pack/<ns>.pack` for these and `configs/all.<ns>.compack` for
# everything else, so a file written to the other location is a file nothing reads.
#
# None of the four is `ids = server` today, so the disagreement was latent — but it
# was armed. Promoting `category` so the 20 LostCity-only npc categories can get an
# id (docs/LOSTCITY_PORT_TRIAGE.md §16.7) is a one-line change to content.ini, and
# the moment it landed this tool would have created `configs/all.category.compack`
# while `pack/category.pack` — the file both ToriRSServer and sscompile load — stayed
# untouched. Two authorities, silent disagreement; docs/CONTENT_ARCHITECTURE.md
# §8.2(c) for the third time.
NON_CONFIG_NAMESPACES = ('3_interfaces', 'component', 'stat', 'category')


# Config records are files of a config archive, so their index is a member index
# beside the archive — `configs/all.seq.compack` — not a pack in `pack/`, which
# holds one file per cache index naming that index's archives. Most namespaces this
# script allocates into are config types; see NON_CONFIG_NAMESPACES for the rest.
def pack_path(tree, ns):
    """The cache layer's symbol file — read for membership, never written."""
    if ns in NON_CONFIG_NAMESPACES:
        return os.path.join(tree, 'pack', f'{ns}.pack')
    return os.path.join(tree, 'configs', f'all.{ns}.compack')


def alloc_path(tree, ns):
    """The server's allocation ledger — the one file this tool writes."""
    return os.path.join(tree, 'pack', f'{ns}.alloc')


def read_pack(path):
    """(raw_lines, {name: id}) — the file verbatim, plus what it binds.

    The raw lines are kept because the write is an *append*, not a regeneration.
    An earlier version parsed the file into a header plus a mapping and rebuilt it
    from those two, which quietly rewrote every line it did not need to touch:
    `configs/all.dbtable.compack`'s per-table notes ("A list of (coord, coord) pairs, read
    by ~inzone_coord_pair_table…") detached from the tables they describe, and a
    trailing `// cache: x` note on a line this tool did not own disappeared.

    That is the same failure `cachepack unpack` committed against
    `configs/all.param.compack`, and it has the same fix on both sides: never rewrite a line
    you were not asked to change.
    """
    raw, mapping = [], {}
    if not os.path.exists(path):
        return raw, mapping
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            stripped = line.rstrip('\n')
            raw.append(stripped)
            if stripped.strip() == MARKER.strip() or '=' not in stripped:
                continue
            ident, name = stripped.split('=', 1)
            if ident.strip().lstrip('-').isdigit():
                # A trailing `// cache: x` note is documentation, not a name.
                mapping[re.sub(r'\s*//.*$', '', name).strip()] = int(ident.strip())
    return raw, mapping


def ported_layers(tree, ns):
    """Every imported lane's symbol file for `ns`, in lane order.

    An imported lane mints ids of its own, outside the rank-0 cache's member
    compacks, and states them in `ported/<lane>/pack/<ns>.alloc` beside
    `ported/<lane>/configs/all.<ns>.compack`. Every other reader already layers
    them over the ordinary symbols — `torirs_server_content.c: load_ported_pack_symbols`,
    `cp_names.c: cp_names_load_ported_allocs`, and sscompile's `--pack` list — so
    this tool has to see them too, or it allocates a *second* id for a name a lane
    has already bound.

    Which is what it did: `prayer_curses_0` is varp 5705 in
    `ported/rs558_ancient_curses`, and this tool, seeing only the base tree,
    appended `6353=prayer_curses_0` to `pack/varp.alloc`. Both readers then
    refused the tree — 2 of the 15 content load errors that failed
    `ToriRSServer --selftest`'s "the content tree should load clean" — and
    `make torirsserver-servpack` would not pack at all.
    """
    root = os.path.join(tree, 'ported')
    if not os.path.isdir(root):
        return []
    layers = []
    for lane in sorted(os.listdir(root)):
        if lane.startswith('.') or not os.path.isdir(os.path.join(root, lane)):
            continue
        lane_tree = os.path.join(root, lane)
        layers.append(pack_path(lane_tree, ns))
        layers.append(alloc_path(lane_tree, ns))
    return layers


def other_layers(tree, ns):
    """({name: id}, highest_id) for every layer this tool does not own.

    Both halves matter and for different reasons:

    - **membership**, so a name layer 0 already provides is not allocated a
      second id. The cache-side tooling folded `names/` into `pack/` while this
      was being written, and a version that only checked its own file promptly
      offered to allocate fresh ids for `stabattack` and the eleven other
      equipment bonuses the cache itself defines.
    - **the high-water mark**, which has to be the highest id *anyone* claims.
      Reading only the current location would allocate on top of ids a previous
      layout had already given out.

    "Anyone" includes the imported lanes — see `ported_layers`. Their bands sit
    below the base ledger's mark in every server-owned namespace today, so
    counting them costs no id; what it buys is that a lane band which grows past
    the mark cannot be allocated over.
    """
    known = {}
    highest = -1
    for layer in [pack_path(tree, ns)] + ported_layers(tree, ns):
        if not os.path.exists(layer):
            continue
        with open(layer, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith('//') or '=' not in line:
                    continue
                ident, name = line.split('=', 1)
                ident = ident.strip()
                if not ident.lstrip('-').isdigit():
                    continue
                # A trailing `// cache: x` comment is documentation, not a name.
                known.setdefault(re.sub(r'\s*//.*$', '', name).strip(), int(ident))
                highest = max(highest, int(ident))

    # cachepack writes unnamed records as `[<ns>_<id>]`, and those ids are just
    # as real as a named one — enum has no configs/all.enum.compack at all, so this is the
    # only evidence of how far the cache's enum group reaches.
    config = os.path.join(tree, 'configs', f'all.{ns}')
    if os.path.exists(config):
        with open(config, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                m = re.match(r'\s*\[' + re.escape(ns) + r'_(\d+)\]\s*$', line)
                if m:
                    highest = max(highest, int(m.group(1)))
    return known, highest


REGISTER_ROW = re.compile(
    r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*CONTENT_NAMES_\w+\s*,'
    r'\s*-?\d+\s*,\s*-?\d+\s*,\s*(-?\d+)\s*,\s*-?\d+\s*\}')


def register_bases():
    """{namespace: server_base} read out of src/content/content_register.c.

    The register's built-in defaults are a C table, and this tool needs the same
    numbers. There are exactly two ways to have them: restate them here, or read
    the file. Restating is the drift the register exists to remove — and
    `declared_base`'s own docstring said so while the function went on returning 0
    for every namespace, because `content.ini` states no `base =` key at all.

    So the C table was the authority in name and the high-water mark was the
    authority in fact, and nothing compared them. What that cost, measured:
    `struct` reads 8000 in the register and the allocator would have handed out
    6500; `varp` read 8000 while nineteen server varps already sat at 5705..5723,
    and 8000 is past `TORIRSSERVER_VARP_COUNT` (6217), so the first varp allocated at
    the declared floor would have been silently dropped by
    `ToriRSServer_WorldSetVarp`'s bounds check — docs/CONTENT_ARCHITECTURE.md §8.3's
    named failure mode, re-armed.

    Read, not restated. The path is derived from this file's own location because
    the register belongs to the engine repo, not to a content tree.
    """
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        '..', 'src', 'content', 'content_register.c')
    bases = {}
    if not os.path.exists(path):
        return bases
    with open(path, encoding='utf-8', errors='replace') as handle:
        for m in REGISTER_ROW.finditer(handle.read()):
            bases[m.group(1)] = int(m.group(2))
    return bases


def declared_base(tree, ns, bases=None):
    """The floor for a namespace: `content.ini`'s `base =`, else the register's.

    The floor and the high-water mark are both needed and neither replaces the
    other. The mark stops an allocation landing on an id something already holds;
    the floor is where *ours* start, so an id in a log reads as ours at a glance.
    The allocator takes `max(floor, mark + 1)`, which is also what
    `lc_pack_alloc_from` does on the C side — two implementations of one rule, and
    a boot check (`validate_id_bases` in torirs_server_pack.c) that holds the floor above
    whatever the cache actually reaches.

    `content.ini` *overlays* the register, exactly as it does for `ids` and
    `names`, so a tree that wants a different floor says so and a tree that says
    nothing gets the register's.
    """
    path = os.path.join(tree, 'content.ini')
    if os.path.exists(path):
        section = None
        with open(path, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                line = re.sub(r'[;#].*$', '', line).strip()
                m = re.match(r'\[namespace:([A-Za-z0-9_]+)\]$', line)
                if m:
                    section = m.group(1)
                    continue
                if section == ns and '=' in line:
                    key, value = (p.strip() for p in line.split('=', 1))
                    if key == 'base':
                        return 0 if value == 'none' else int(value)
    if bases is None:
        bases = register_bases()
    return bases.get(ns, 0)


def id_authority(tree, ns):
    """The `ids =` axis for a namespace, from content.ini. Defaults to server.

    This decides whether a layer-1 line with no config block behind it is worth
    reporting. For a namespace the *server* owns, such a line is a stale
    allocation — the config was renamed or deleted. For one the *cache* owns,
    it is the normal case: `configs/all.param.compack` names cache params 0-14 and 434-437
    by hand, and no `.param` in this tree declares them because the cache does.
    Reporting those as stale every run trains the reader to ignore the column.
    """
    path = os.path.join(tree, 'content.ini')
    if not os.path.exists(path):
        return 'server'
    section, authority = None, {}
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            line = re.sub(r'[;#].*$', '', line).strip()
            m = re.match(r'\[namespace:([A-Za-z0-9_]+)\]$', line)
            if m:
                section = m.group(1)
                continue
            if section and '=' in line:
                key, value = (p.strip() for p in line.split('=', 1))
                if key == 'ids':
                    authority[section] = value
    return authority.get(ns, 'server')


# --- the --stamp fast path -------------------------------------------------
#
# Every file this tool reads goes through `open` below and every directory it
# walks through `_files_by_extension`, so a stamp of their sizes and mtimes is
# the complete statement of its inputs. When none moved, nothing new can need
# an id and the run is a stat of a few thousand paths instead of a read of
# every config file — the difference between 0.75 s and 0.05 s on each build.
_READ_PATHS = set()
_WALKED_DIRS = set()
_builtin_open = open


def open(file, mode='r', *args, **kwargs):  # noqa: A001 — records what is read
    if not any(c in mode for c in 'wax+'):
        _READ_PATHS.add(os.path.abspath(file))
    return _builtin_open(file, mode, *args, **kwargs)


# A directory is stamped by what it holds that this tool could read -- its
# subdirectories and its files with a namespace or ledger extension -- not by
# its mtime: packs, logs and this stamp are written into server/scripts/build
# and screenshots into selftest/, and an mtime test re-ran the whole sweep after
# every one of them.
_RELEVANT_EXTENSIONS = None


def _relevant(name):
    global _RELEVANT_EXTENSIONS
    if _RELEVANT_EXTENSIONS is None:
        _RELEVANT_EXTENSIONS = set(SERVER_NAMESPACES) | {
            'alloc', 'pack', 'compack', 'ini', 'constant', 'server', 'client'}
    dot = name.rfind('.')
    return name.startswith('all.') or (dot > 0 and name[dot + 1:] in _RELEVANT_EXTENSIONS)


def _stat_line(kind, path):
    if kind == 'D':
        try:
            with os.scandir(path) as entries:
                names = sorted(e.name + '/' if e.is_dir() else e.name for e in entries
                               if not e.name.startswith('.') and
                               (e.is_dir() or _relevant(e.name)))
        except OSError:
            return f'D\t-\t-\t{path}'
        digest = hashlib.sha1('\n'.join(names).encode('utf-8')).hexdigest()
        return f'D\t0\t{digest}\t{path}'
    try:
        st = os.stat(path)
    except OSError:
        return f'{kind}\t-\t-\t{path}'
    return f'{kind}\t{st.st_size}\t{st.st_mtime_ns}\t{path}'


def _stamp_lines():
    lines = [_stat_line('F', os.path.abspath(__file__))]
    dirs = set(_WALKED_DIRS) | {os.path.dirname(p) for p in _READ_PATHS}
    lines += [_stat_line('D', d) for d in sorted(dirs)]
    lines += [_stat_line('F', p) for p in sorted(_READ_PATHS)]
    return lines


def _stamp_current(stamp):
    try:
        with _builtin_open(stamp, encoding='utf-8') as handle:
            recorded = handle.read().splitlines()
    except OSError:
        return False
    if not recorded:
        return False
    for line in recorded:
        kind, _, _, path = line.split('\t', 3)
        if _stat_line(kind, path) != line:
            return False
    return True


_FILES_BY_EXT = {}
_BLOCK_RE = re.compile(r'\s*\[([A-Za-z0-9_+.\-]+)\]\s*$')


def _files_by_extension(source_roots):
    """{extension: [path]} for every file under the roots, walked once.

    Each namespace used to walk the whole tree for its own extension, and
    server/scripts holds ~54,000 files (most of them selftest screenshots): a
    dozen walks were most of this tool's 1.3 s, which every pack build pays.
    The per-directory `sorted(files)` order is kept.
    """
    key = tuple(source_roots)
    if key not in _FILES_BY_EXT:
        found = {}
        for source_root in source_roots:
            if not os.path.isdir(source_root):
                continue
            for base, _, files in os.walk(source_root):
                _WALKED_DIRS.add(os.path.abspath(base))
                for f in sorted(files):
                    dot = f.rfind('.')
                    if dot > 0:
                        found.setdefault(f[dot + 1:], []).append(os.path.join(base, f))
        _FILES_BY_EXT[key] = found
    return _FILES_BY_EXT[key]


def declared_blocks(source_roots, ns):
    """Every allocated `[name]` block in the server tree and marked client lanes."""
    names = {}
    numeric = re.compile(re.escape(ns) + r'_\d+')
    for path in _files_by_extension(source_roots).get(ns, []):
        with open(path, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                if '[' not in line:
                    continue
                m = _BLOCK_RE.match(line)
                if not m:
                    continue
                name = m.group(1)
                # A numeric block is the cache's own record, not ours.
                if numeric.fullmatch(name):
                    continue
                names.setdefault(name, None)
    return list(names)


DEFAULT_HEADER = (
    '// The `{ns}` namespace: ids this server allocated.',
    '//',
    '// The server\'s allocation ledger — one layer of the namespace whose other',
    '// layer is configs/all.{ns}.compack, the cache\'s member index. Every reader',
    '// loads both and refuses a name bound in each. Being listed here also routes',
    '// the record server-side in `cachepack pack`: the allocation is the',
    '// membership, so no pack/{ns}.server line is needed.',
    '//',
    '// Appended by tools/ss_allocate.py from the `[block]` names in',
    '// server/scripts/**/configs/*.{ns}, allocated from one past the largest id',
    '// anything else claims. See docs/CONTENT_PACK_PLAN.md §4.',
    '//',
    '// Prose anywhere in this file survives: the allocator only ever appends, and',
    '// never rewrites a line it did not add.',
)


# Namespaces whose names carry their kind and id; see
# OSRS-Content/tools/var_prefix_names.py.
VAR_PREFIX = {'varp': 'varp', 'varbit': 'varb', 'varc': 'varc'}


def write_pack(path, ns, raw, fresh):
    """Append `fresh` — [(id, name)] — leaving every existing line alone.

    New ids are always above the high-water mark, so appending them after the
    marker keeps the file ascending without having to move anything.
    """
    lines = list(raw)
    if not lines:
        lines = [line.format(ns=ns) for line in DEFAULT_HEADER]
    if not any(line.strip() == MARKER.strip() for line in lines):
        if lines and lines[-1].strip():
            lines.append('')
        lines.append(MARKER)
    for ident, name in sorted(fresh):
        lines.append(f'{ident}={name}')
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write('\n'.join(lines) + '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tree', required=True)
    ap.add_argument('--check', action='store_true',
                    help='report what would change and exit non-zero')
    ap.add_argument('--namespace', action='append', default=[])
    ap.add_argument('--stamp',
                    help='skip the run when nothing it read last time has moved; '
                         'record what it read here after a clean run')
    ap.add_argument('--verbose', action='store_true',
                    help='list the ids no block declares any more')
    args = ap.parse_args()

    if args.stamp and not args.check and _stamp_current(args.stamp):
        print('ss_allocate: up to date (no input moved since the last run)')
        return 0

    scripts_root = os.path.join(args.tree, 'server', 'scripts')
    if not os.path.isdir(scripts_root):
        print(f'ss_allocate: no server/scripts under {args.tree}',
              file=sys.stderr)
        return 2

    namespaces = args.namespace or server_namespaces(args.tree)
    source_roots = [scripts_root]
    ported_root = os.path.join(args.tree, 'ported')
    if os.path.isdir(ported_root):
        source_roots.extend(
            os.path.join(ported_root, lane, 'configs')
            for lane in sorted(os.listdir(ported_root))
        )
    bases = register_bases()
    dirty = False
    respell = False
    # A name that cannot be allocated is a failure in its own right, not a
    # pending change — so it fails the run with or without --check.
    failed = False
    for ns in namespaces:
        declared = declared_blocks(source_roots, ns)
        if not declared:
            continue
        path = alloc_path(args.tree, ns)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        raw, mapping = read_pack(path)
        elsewhere, highest = other_layers(args.tree, ns)
        mark = max(highest, max(mapping.values()) if mapping else -1) + 1
        # The register's floor. `max` and not `or`: a floor below the high-water
        # mark must not pull an allocation down onto an id something already holds.
        floor = declared_base(args.tree, ns, bases)
        base = max(mark, floor)

        authority = id_authority(args.tree, ns)
        # A var's name carries its kind and id (`varp5725_mock_zone_clock`), so
        # a block still spelled `[mock_zone_clock]` is the same var: known when
        # any layer holds the prefixed spelling.
        prefix = VAR_PREFIX.get(ns)
        known = set(mapping) | set(elsewhere)
        if prefix:
            known |= {m.group(1) for m in map(
                re.compile(rf'^{prefix}\d+_(.+)$').match, known) if m}
        # Unknown means unknown to *every* layer, not just to ours.
        unknown = [n for n in sorted(declared) if n not in known]

        # ------------------------------------------------------------------
        # Only a `ids = server` namespace may be allocated into.
        # ------------------------------------------------------------------
        #
        # For a namespace the *cache* owns — `param`, `category` — a config block
        # with no id is a **missing name**, not an id to invent. Inventing one is
        # actively destructive: `param` 0..11 are the twelve equipment bonuses the
        # cache itself defines, so allocating `stabattack` a fresh id above the
        # high-water mark silently detaches every weapon in the game from its
        # attack bonus. This tool did exactly that once, the moment the authored
        # `configs/all.param.compack` went missing, and the output looked like a normal
        # run — 22 tidy new ids.
        #
        # So: report and fail. A cache-owned name that does not resolve wants
        # someone to find the number in the cache, which is not something a
        # counter can do.
        if authority != 'server':
            if unknown:
                print(f'{ns:9} declared={len(declared):5} '
                      f'ERROR: {len(unknown)} name(s) have no id, and `ids = '
                      f'{authority}` means they cannot be allocated one:')
                for name in unknown:
                    print(f'            ! {name}')
                print(f'            read the id off the cache and add it to '
                      f'configs/all.{ns}.compack')
                failed = True
            continue

        fresh = unknown
        stale = [n for n in mapping
                 if n not in declared and n not in elsewhere]
        allocated = []
        if fresh:
            dirty = True
            held = {ident: name for name, ident in list(mapping.items()) +
                    list(elsewhere.items())}
            prefixed_re = re.compile(rf'^{prefix}(\d+)_(.+)$') if prefix else None
            base_of = {}
            if prefix:
                for name, ident in list(mapping.items()) + list(elsewhere.items()):
                    m = prefixed_re.match(name)
                    base_of[m.group(2) if m and int(m.group(1)) == ident else name] = name
            for name in fresh:
                # A block that already spells a `varp<N>_` prefix.
                #
                # This used to prefix the whole name again — `[varp6883_x]`
                # became `varp7344_varp6883_x` in the ledger and in the tree,
                # and a hand fix back to `[varp7344_x]` matched nothing, so the
                # next build allocated `varp7346_varp7344_x`: a fresh id and a
                # deeper prefix on every build. var_prefix_names.py can only
                # respell a block whose *base* name the ledger holds, so no
                # silent respelling of a prefixed block converges. The rules:
                # the id is free -> adopt it as written; anything else -> stop
                # and say which spelling to use.
                m = prefixed_re.match(name) if prefix else None
                if m:
                    ident, rest = int(m.group(1)), m.group(2)
                    if rest in base_of:
                        print(f'{ns:9} ERROR: [{name}] is `{rest}` with a guessed id; '
                              f'the ledger already spells it [{base_of[rest]}]')
                        failed = True
                        continue
                    if ident in held or ident < floor:
                        owner = held.get(ident)
                        print(f'{ns:9} ERROR: [{name}] states id {ident}, which '
                              + (f'pack/{ns}.alloc gives to {owner}' if owner
                                 else f'is below the server floor {floor}')
                              + f'; spell the block [{rest}] and this tool numbers it')
                        failed = True
                        continue
                    mapping[name] = ident
                    held[ident] = name
                    allocated.append((ident, name))
                    continue
                while base in held:
                    base += 1
                if prefix:
                    name = f'{prefix}{base}_{name}'
                mapping[name] = base
                held[base] = name
                allocated.append((base, name))
                base += 1
            fresh = [name for _, name in allocated]

        print(f'{ns:9} declared={len(declared):5} allocated={len(mapping):5} '
              f'base_was={max(mark, floor)} floor={floor}'
              + (f' NEW={len(fresh)}' if fresh else '')
              + (f' STALE={len(stale)} (kept; --verbose lists them)' if stale else ''))
        for name in fresh:
            print(f'            + {mapping[name]}={name}')
        # Stale ids are kept on purpose (ids are stable), so they are not news
        # on every build: the count is on the line above, the list on request.
        if args.verbose:
            for name in sorted(stale):
                print(f'            ? {mapping[name]}={name} '
                      f'(no longer declared; kept, ids are stable)')

        if fresh and not args.check:
            write_pack(path, ns, raw, allocated)
            if prefix:
                respell = True

    if respell:
        # The new ids are in the ledger; now the `[name]` blocks and every
        # `%name` that declared them take the prefixed spelling.
        tool = os.path.join(args.tree, os.pardir, 'tools', 'var_prefix_names.py')
        subprocess.run([sys.executable, tool, '--write'], check=True)
    if failed:
        print('ss_allocate: a declared name cannot be given an id (see the ERROR '
              'lines above)', file=sys.stderr)
        return 2
    if args.check and dirty:
        print('ss_allocate: pack/<ns>.alloc is stale; '
              'run tools/ss_allocate.py', file=sys.stderr)
        return 1
    # Recorded after the writes and the respelling, so the stamp describes the
    # tree as this run left it. A respelling run rewrites sources it did not
    # read; the next run sees their directories move and does the full pass.
    if args.stamp and not args.check:
        os.makedirs(os.path.dirname(os.path.abspath(args.stamp)), exist_ok=True)
        temporary = args.stamp + '.tmp'
        with _builtin_open(temporary, 'w', encoding='utf-8') as handle:
            handle.write('\n'.join(_stamp_lines()) + '\n')
        os.replace(temporary, args.stamp)
    return 0


if __name__ == '__main__':
    sys.exit(main())
