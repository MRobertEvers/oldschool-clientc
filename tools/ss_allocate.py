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

**Output goes to the namespace's one symbol file**, which is
`configs/all.<ns>.compack` for a config type and `pack/<ns>.pack` for the four
that are not one — see `pack_path` and NON_CONFIG_NAMESPACES, which mirror
`pack_kind_is_config()` in src/net/mock/mock230_content.c because the runtime
reads only those locations. Earlier drafts wrote to `names/<ns>.pack`
(docs/CONTENT_ARCHITECTURE.md §4.4) and then to `server/pack/<ns>.pack`; both are
wrong now that a namespace has exactly one symbol file. The marker discipline
above is what makes sharing that file with a human safe in the other direction,
and the cache-side codec's comment-preserving merge is what makes it safe in this
one.

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
import os
import re
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
DEFAULT_SERVER_NAMESPACES = (
    'enum', 'struct', 'dbtable', 'dbrow', 'param', 'mesanim', 'inv',
)

# Kept for readers that import the old name.
SERVER_NAMESPACES = DEFAULT_SERVER_NAMESPACES


def server_namespaces(tree):
    """Every namespace this tool should sweep: the defaults, plus the register's.

    A union and not a replacement, because `content.ini` *overlays* the built-in
    defaults rather than restating them (see its own header): `enum`, `struct`,
    `dbtable`, `dbrow`, `mesanim` and `inv` appear nowhere in it and still have
    server-owned ids. Deriving the list from the file alone would silently stop
    allocating for five namespaces.

    What the union adds is the other direction: a namespace the tree *promotes*
    to `ids = server` is swept without anyone remembering to edit this file.
    `varp` is the case that motivated it — the register was changed so content
    could declare the `%com_*` combat stat block the reference computes in
    `[proc,player_combat_stat]`, and until this the promotion bought nothing
    because no tool would hand those varps an id.
    """
    found = set(DEFAULT_SERVER_NAMESPACES)
    path = os.path.join(tree, 'content.ini')
    if not os.path.exists(path):
        return tuple(sorted(found))
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
# This mirrors `pack_kind_is_config()` in src/net/mock/mock230_content.c and has to:
# the runtime reads `pack/<ns>.pack` for these and `configs/all.<ns>.compack` for
# everything else, so a file written to the other location is a file nothing reads.
#
# None of the four is `ids = server` today, so the disagreement was latent — but it
# was armed. Promoting `category` so the 20 LostCity-only npc categories can get an
# id (docs/LOSTCITY_PORT_TRIAGE.md §16.7) is a one-line change to content.ini, and
# the moment it landed this tool would have created `configs/all.category.compack`
# while `pack/category.pack` — the file both mock230 and sscompile load — stayed
# untouched. Two authorities, silent disagreement; docs/CONTENT_ARCHITECTURE.md
# §8.2(c) for the third time.
NON_CONFIG_NAMESPACES = ('3_interfaces', 'component', 'stat', 'category')


# Config records are files of a config archive, so their index is a member index
# beside the archive — `configs/all.seq.compack` — not a pack in `pack/`, which
# holds one file per cache index naming that index's archives. Most namespaces this
# script allocates into are config types; see NON_CONFIG_NAMESPACES for the rest.
def pack_path(tree, ns):
    if ns in NON_CONFIG_NAMESPACES:
        return os.path.join(tree, 'pack', f'{ns}.pack')
    return os.path.join(tree, 'configs', f'all.{ns}.compack')


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
    """
    known = {}
    highest = -1
    for layer in (pack_path(tree, ns),):
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
    r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*CONTENT_IDS_\w+\s*,\s*CONTENT_NAMES_\w+\s*,'
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
    and 8000 is past `MOCK230_VARP_COUNT` (6217), so the first varp allocated at
    the declared floor would have been silently dropped by
    `mock230_world_set_varp`'s bounds check — docs/CONTENT_ARCHITECTURE.md §8.3's
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
    a boot check (`validate_id_bases` in mock230_pack.c) that holds the floor above
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


def declared_blocks(scripts_root, ns):
    """Every `[name]` block in every `*.<ns>` config under the server tree."""
    names = []
    for base, _, files in os.walk(scripts_root):
        for f in sorted(files):
            if not f.endswith('.' + ns):
                continue
            with open(os.path.join(base, f), encoding='utf-8',
                      errors='replace') as handle:
                for line in handle:
                    m = re.match(r'\s*\[([A-Za-z0-9_+.\-]+)\]\s*$', line)
                    if not m:
                        continue
                    name = m.group(1)
                    # A numeric block is the cache's own record, not ours.
                    if re.fullmatch(re.escape(ns) + r'_\d+', name):
                        continue
                    if name not in names:
                        names.append(name)
    return names


DEFAULT_HEADER = (
    '// The `{ns}` namespace: ids this server allocated.',
    '//',
    '// Appended by tools/ss_allocate.py from the `[block]` names in',
    '// server/scripts/**/configs/*.{ns}, allocated from one past the largest id',
    '// anything else claims. See docs/CONTENT_PACK_PLAN.md §4.',
    '//',
    '// Prose anywhere in this file survives: the allocator only ever appends, and',
    '// never rewrites a line it did not add.',
)


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
    args = ap.parse_args()

    scripts_root = os.path.join(args.tree, 'server', 'scripts')
    if not os.path.isdir(scripts_root):
        print(f'ss_allocate: no server/scripts under {args.tree}',
              file=sys.stderr)
        return 2

    namespaces = args.namespace or server_namespaces(args.tree)
    bases = register_bases()
    dirty = False
    # A name that cannot be allocated is a failure in its own right, not a
    # pending change — so it fails the run with or without --check.
    failed = False
    for ns in namespaces:
        declared = declared_blocks(scripts_root, ns)
        if not declared:
            continue
        path = pack_path(args.tree, ns)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        raw, mapping = read_pack(path)
        elsewhere, highest = other_layers(args.tree, ns)
        mark = max(highest, max(mapping.values()) if mapping else -1) + 1
        # The register's floor. `max` and not `or`: a floor below the high-water
        # mark must not pull an allocation down onto an id something already holds.
        floor = declared_base(args.tree, ns, bases)
        base = max(mark, floor)

        authority = id_authority(args.tree, ns)
        # Unknown means unknown to *every* layer, not just to ours.
        unknown = [n for n in sorted(declared)
                   if n not in mapping and n not in elsewhere]

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
            for name in fresh:
                mapping[name] = base
                allocated.append((base, name))
                base += 1

        print(f'{ns:9} declared={len(declared):5} allocated={len(mapping):5} '
              f'base_was={base - len(fresh)} floor={floor}'
              + (f' NEW={len(fresh)}' if fresh else '')
              + (f' STALE={len(stale)}' if stale else ''))
        for name in fresh:
            print(f'            + {mapping[name]}={name}')
        for name in sorted(stale):
            print(f'            ? {mapping[name]}={name} '
                  f'(no longer declared; kept, ids are stable)')

        if fresh and not args.check:
            write_pack(path, ns, raw, allocated)

    if failed:
        print('ss_allocate: a cache-owned name has no id (see above)',
              file=sys.stderr)
        return 2
    if args.check and dirty:
        print('ss_allocate: server/pack/*.pack are stale; '
              'run tools/ss_allocate.py', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
