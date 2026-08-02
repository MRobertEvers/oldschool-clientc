#!/usr/bin/env python3
"""Report every symbol a RuneScript tree references and cannot resolve.

`sscompile` stops at the first unresolved name, which makes porting the
reference server's 113,000 lines of content a one-error-at-a-time crawl. This
walks the same sources and reports *all* of them at once, grouped so the output
is a work queue rather than a list: a name that appears 40 times in one
namespace is a namespace to import, and a name that appears once is probably
content nobody needs yet.

    tools/ss_unresolved.py --src DIR [--pack DIR]... [--constants DIR]
                           [--reference DIR] [--only SUBDIR]...

`--reference` points at a LostCity content tree; when given, each unresolved
name is annotated with the namespace the reference's own packs put it in, which
is what tells `bronze_axe` (an obj, and a real gap) apart from `woodcutting_axe`
(a category, so the gap is the category import).

This is deliberately a *reporter* and not part of the build. It over-reports by
design — it cannot see argument position, so it treats every bare word as a
candidate — and the whitelist below is the accumulated set of words that are
grammar rather than symbols. Under-reporting would be the harmful direction: a
missed name is a compile error later, whereas a false positive costs one line of
reading.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

# Language words, type names and literals. These are grammar, not symbols, and
# the compiler never looks them up in the symbol table.
GRAMMAR = {
    'if', 'else', 'while', 'return', 'switch', 'case', 'default', 'calc',
    'def_int', 'def_string', 'def_coord', 'def_boolean', 'def_namedobj',
    'def_obj', 'def_loc', 'def_npc', 'def_component', 'def_interface',
    'def_inv', 'def_enum', 'def_struct', 'def_param', 'def_stat', 'def_seq',
    'def_spotanim', 'def_synth', 'def_dbrow', 'def_dbtable', 'def_category',
    'def_npc_uid', 'def_player_uid', 'def_npc_stat', 'def_fontmetrics',
    'def_mesanim', 'def_hunt', 'def_varp', 'def_char', 'def_maparea',
    'def_movespeed', 'def_idkit', 'def_locshape', 'def_bas', 'def_overlay',
    'def_underlay', 'def_midi', 'def_model',
    'true', 'false', 'null',
    # type names, as they appear as arguments to enum() / db ops / def_ forms
    'int', 'string', 'coord', 'boolean', 'namedobj', 'obj', 'loc', 'npc',
    'component', 'interface', 'inv', 'enum', 'struct', 'param', 'stat', 'seq',
    'spotanim', 'synth', 'dbrow', 'dbtable', 'category', 'npc_uid',
    'player_uid', 'npc_stat', 'fontmetrics', 'mesanim', 'hunt', 'char',
    'maparea', 'movespeed', 'idkit', 'locshape', 'bas', 'overlay', 'underlay',
    'midi', 'model', 'varp', 'graphic',
}

# Trigger names are grammar too: `[opnpc1,hans]` names a trigger, not a symbol.
TRIGGERS = re.compile(
    r'^(proc|label|command|debugproc|queue|softtimer|timer|walktrigger|login|'
    r'logout|tutorial|advancestat|changestat|if_button\d*|if_close|inv_button\d*|'
    r'inv_buttond|ai_\w+|op\w+|ap\w+|mapzone\w*|zone\w*)$')


def namespace_for(filename):
    """The namespace a pack filename belongs to, or None.

    Two levels of index live in two directories and two spellings.
    `pack/7_models.pack` names the archives of a cache index; the leading number is
    part of the name. `configs/all.npc.compack` names the records inside one
    archive, and the type is between the two dots. Reading only `.pack` skipped
    every config type, which is most of them.
    """
    if filename.endswith('.compack'):
        stem = filename[:-len('.compack')]
        return stem[len('all.'):] if stem.startswith('all.') else None
    if filename.endswith('.pack'):
        return filename[:-len('.pack')]
    return None


def load_packs(dirs):
    """name -> set of namespaces, from every index file in every directory."""
    names = defaultdict(set)
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            ns = namespace_for(f)
            if ns is None:
                continue
            path = os.path.join(d, f)
            with open(path, encoding='utf-8', errors='replace') as handle:
                for line in handle:
                    line = line.strip()
                    if not line or line.startswith('//') or '=' not in line:
                        continue
                    ident, name = line.split('=', 1)
                    if ident.strip().lstrip('-').isdigit():
                        names[name.strip()].add(ns)
    return names


# `_unpack` and `_test` are the reference's own decompiled dumps of the cache it
# was built from, not its authored tree. Walking them compares this tree against
# a machine export rather than against what LostCity states, and it multiplies
# the name surface by ten.
SKIP_DIRS = ('_unpack', '_test', 'build')


def walk(root, suffixes):
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in sorted(files):
            if f.endswith(suffixes):
                yield os.path.join(base, f)


def load_tree_symbols(src, constants):
    """Everything the tree itself declares: constants, config blocks, scripts."""
    declared = defaultdict(set)

    # `^name = value`
    for path in walk(constants, ('.constant',)):
        with open(path, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                m = re.match(r'\s*\^([A-Za-z0-9_]+)\s*=', line)
                if m:
                    declared[m.group(1)].add('constant')

    # `[block]` in a config declares that block's name in its file's namespace
    for path in walk(constants, ('.enum', '.struct', '.dbtable', '.dbrow',
                                 '.param', '.npc', '.obj', '.loc', '.inv',
                                 '.varp', '.varbit', '.varn', '.vars', '.seq',
                                 '.spotanim', '.hunt', '.prayer', '.mesanim',
                                 '.category')):
        ns = path.rsplit('.', 1)[1]
        with open(path, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                m = re.match(r'\s*\[([A-Za-z0-9_+.\-]+)\]\s*$', line)
                if m:
                    declared[m.group(1)].add(ns)

    # `[proc,name]` / `[label,name]` and every other script header
    for path in walk(src, ('.rs2',)):
        with open(path, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                m = re.match(r'\s*\[[a-z0-9_]+\s*,\s*([^\]]+)\]', line)
                if m:
                    declared[m.group(1).strip()].add('script')
    return declared


# ------------------------------------------------------------------ configs
#
# `.rs2` is not the whole authored tree, and a name in a config has to resolve
# exactly as hard as a name in a script: a `.npc` that says
# `param=death_anim,goblin_death` is naming a seq, and until 2026-08-01 a
# misspelling there loaded at 0 errors and the npc simply died without an
# animation. Counting only `.rs2` references undercounts the port's name surface
# by more than half.

# Config suffixes whose values name other records.
CONFIG_SUFFIXES = ('.npc', '.obj', '.loc', '.inv', '.seq', '.spotanim', '.enum',
                   '.struct', '.param', '.dbtable', '.dbrow', '.varp', '.varbit',
                   '.hunt', '.mesanim', '.spawn', '.category')

# Keys whose value is prose, a number, or a wire enum the packs never name.
# Everything not listed is treated as possibly symbolic, because the harmful
# direction is missing a name, not reading one extra line.
FREE_TEXT_KEYS = {
    'name', 'desc', 'debugname',
    'op1', 'op2', 'op3', 'op4', 'op5',
    'iop1', 'iop2', 'iop3', 'iop4', 'iop5',
    'moverestrict', 'huntmode', 'defaultmode', 'vislevel', 'minimap',
    'wearpos', 'wearpos2', 'wearpos3', 'category',
    'table', 'column', 'type', 'default', 'defaultstr',
    # `.varp` / `.varbit` grammar: `scope=perm`, `transmit=no`, `protect=no`.
    #
    # `wholewrite=allow` is the carrier-write exemption (`fields/varp.ini`
    # §wholewrite). It was declared in the field register and honoured by
    # sscompile (`check_carrier_write`) and the runtime long before any content
    # used it — `fields/varp.ini` says so in as many words: "Nothing in this tree
    # declares it, which is the point". So the first content to declare it found
    # this gate reading `allow` as a namespace reference and failing the whole
    # tree. A key the register knows and this list does not is a hole in the
    # gate, not a fact about the content.
    'scope', 'transmit', 'protect', 'clientcode', 'wholewrite',
}

# Config-grammar words that are values rather than names.
CONFIG_GRAMMAR = {'yes', 'no', 'true', 'false', 'null', 'hide', 'none'}

NUMERIC = re.compile(r'^-?\d+$')
COORD = re.compile(r'^\d+(_\d+){4}$')
# Uppercase is allowed even though every pack name is lower-case: a typo is not
# obliged to keep the convention, and `goblin_death_TYPO` slipping through a
# lower-case-only pattern is the exact hole this bar exists to close.
SYMBOL = re.compile(r'^[A-Za-z][A-Za-z0-9_]*$')


# RuneScript types whose values are literals rather than names. Everything else
# is a namespace, so a value of that type has to resolve.
LITERAL_TYPES = {'int', 'string', 'coord', 'boolean', 'long', 'char'}


def read_blocks(path):
    """`[name]` sections as (name, [(key, value), ...]), order preserved."""
    blocks = []
    current = None
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            line = re.sub(r'//.*$', '', line).strip()
            if not line:
                continue
            if line.startswith('[') and line.endswith(']'):
                current = (line[1:-1], [])
                blocks.append(current)
                continue
            if current is None or '=' not in line:
                continue
            key, value = line.split('=', 1)
            current[1].append((key.strip(), value.strip()))
    return blocks


def dbtable_columns(root):
    """table name -> [(column, type)], from every `.dbtable` under `root`.

    A `.dbrow`'s `data=` lines are typed by its table, and without that type the
    checker cannot tell `data=name,Thick Skin` (a string) from
    `data=headicon,prayer_smite` (a name). Reading the declaration is what makes
    this bar exact instead of a heuristic that has to be whitelisted."""
    tables = {}
    for path in walk(root, ('.dbtable',)):
        for name, lines in read_blocks(path):
            columns = []
            for key, value in lines:
                if key != 'column':
                    continue
                parts = [p.strip() for p in value.split(',')]
                columns.append((parts[0], parts[1] if len(parts) > 1 else 'int'))
            tables[name] = columns
    return tables


def config_references(root):
    """(name, relative path) for every value in an authored config that names a
    record. Multi-field values are split on commas: `param=death_drop,bones`
    names a param and an obj, and both have to exist."""
    tables = dbtable_columns(root)
    for path in walk(root, CONFIG_SUFFIXES):
        rel = os.path.relpath(path, root)
        for _block, lines in read_blocks(path):
            declared = dict(lines)
            table = tables.get(declared.get('table', ''), [])
            column_type = dict(table)
            for key, value in lines:
                stem = re.sub(r'\d+$', '', key) or key
                if stem in FREE_TEXT_KEYS:
                    continue
                fields = [f.strip() for f in value.split(',')]

                if key == 'val':
                    # `.enum`: the block states the type of both halves.
                    types = [declared.get('inputtype', 'int'),
                             declared.get('outputtype', 'int')]
                    fields = [f for f, t in zip(fields, types)
                              if t not in LITERAL_TYPES]
                elif key == 'data':
                    # `.dbrow`: the first field names a column of the row's own
                    # table — declared by `column=`, not a pack symbol — and the
                    # rest are typed by that column.
                    kind = column_type.get(fields[0], 'int') if fields else 'int'
                    fields = fields[1:] if kind not in LITERAL_TYPES else []

                for field in fields:
                    if (not field or field.startswith('^') or field.startswith('"')
                            or field in CONFIG_GRAMMAR or NUMERIC.match(field)
                            or COORD.match(field) or not SYMBOL.match(field)):
                        continue
                    yield field, rel


# ------------------------------------------------------------- bare ids
#
# Triage §13 bar 5: no content file carries a bare id. It is the bar that makes
# bars 1 and 3 mean anything — an id has nothing to resolve, so it cannot fail
# to resolve, so a gate that only checks names cannot see it. Spawns were the
# last exception (§10.2) and `sos_pest_giantspider1` is what that cost: a
# level-50 Pest Control spider standing in Lumbridge Swamp, spelled 2477.
#
# Three places an id can still be written, and all three are checkable:
#   - a section header that is a number rather than a name
#   - a value of a field the register declares `ref = <namespace>` for
#   - the first column of a `.spawn` line


def ref_fields(tree):
    """(type, field) -> namespace, from `fields/<type>.ini`'s `ref =` rows. The
    register is the authority on which fields are symbolic; a list here would be
    a second copy that stops agreeing."""
    refs = {}
    fields_dir = os.path.join(tree, 'fields')
    if not os.path.isdir(fields_dir):
        return refs
    for filename in sorted(os.listdir(fields_dir)):
        if not filename.endswith('.ini'):
            continue
        section = None
        with open(os.path.join(fields_dir, filename), encoding='utf-8',
                  errors='replace') as handle:
            for line in handle:
                line = re.sub(r'[;#].*$', '', line).strip()
                if line.startswith('[') and line.endswith(']'):
                    section = line[1:-1]
                    continue
                if section and line.startswith('ref') and '=' in line:
                    refs[section] = line.split('=', 1)[1].strip()
    return refs


def bare_ids(root, tree):
    """(what, relative path) for every bare id in an authored content file."""
    refs = ref_fields(tree)

    for path in walk(root, CONFIG_SUFFIXES):
        rel = os.path.relpath(path, root)
        kind = path.rsplit('.', 1)[1]
        for block, lines in read_blocks(path):
            if NUMERIC.match(block):
                yield "section [%s] is an id, not a name" % block, rel
            for key, value in lines:
                parts = [f.strip() for f in value.split(',')]
                ns = refs.get('%s.%s' % (kind, key))
                if ns:
                    for field in parts:
                        # -1 is `null` spelled as a number and means "none"; it
                        # names no record, so it is not a bare id.
                        if NUMERIC.match(field) and field != '-1':
                            yield ("[%s] %s = %s is an id in `%s`, not a name"
                                   % (block, key, field, ns), rel)
                # `param=<name>,<value>`: the register keys the ref on the param,
                # not on the word `param`. Checked whether or not `<type>.param`
                # itself is a declared ref field, because it is not one.
                if key == 'param' and len(parts) > 1:
                    pns = refs.get('%s.%s' % (kind, parts[0]))
                    if pns and NUMERIC.match(parts[1]) and parts[1] != '-1':
                        yield ("[%s] param %s = %s is an id in `%s`, not a name"
                               % (block, parts[0], parts[1], pns), rel)

    for path in walk(root, ('.spawn',)):
        rel = os.path.relpath(path, root)
        with open(path, encoding='utf-8', errors='replace') as handle:
            for number, line in enumerate(handle, 1):
                line = re.sub(r'//.*$', '', line).strip()
                if not line or line.startswith('='):
                    continue
                first = line.split()[0]
                if NUMERIC.match(first):
                    yield "line %d spawns id %s, not a name" % (number, first), rel


def command_names(meta_header):
    """Every host command, from the generated opcode table."""
    if not os.path.exists(meta_header):
        return set()
    text = open(meta_header, encoding='utf-8', errors='replace').read()
    return {n.lower() for n in re.findall(r'SS_OP_([A-Z0-9_]+)', text)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True)
    ap.add_argument('--pack', action='append', default=[])
    ap.add_argument('--constants')
    ap.add_argument('--reference',
                    help='a LostCity content tree, to name each gap')
    ap.add_argument('--opcodes',
                    default='src/serverscript/ss_opcode.h')
    ap.add_argument('--only', action='append', default=[],
                    help='restrict to these subdirectories of --src')
    ap.add_argument('--scripts-only', action='store_true',
                    help='the historical behaviour: .rs2 references only')
    ap.add_argument('--check', action='store_true',
                    help='exit non-zero on any unresolved name or bare id '
                         '(the build bar)')
    ap.add_argument('--tree', help='content tree root (default two levels above '
                                   '--src); read for fields/<type>.ini')
    args = ap.parse_args()

    constants = args.constants or args.src
    known = load_packs(args.pack)
    declared = load_tree_symbols(args.src, constants)
    commands = command_names(args.opcodes)
    reference = load_packs([os.path.join(args.reference, 'pack')]) if args.reference else {}

    # A bare word, but not one preceded by $ ^ % @ ~ or a quote.
    word = re.compile(r'(?<![\w$^%@~"\'])([a-z][a-z0-9_]*)(?![\w"\'])')

    unresolved = defaultdict(lambda: defaultdict(int))
    # `--check` reads configs only, and that is a statement about where the
    # *exact* checkers are rather than a convenience.
    #
    # The script half already has one: `sscompile` refuses an unresolved name
    # outright, and it compiles this tree's 319 scripts. So every one of the 74
    # names this reporter flags in `.rs2` is provably a false positive of its own
    # heuristic — it cannot see argument position — and a bar built on them could
    # only ever be red or whitelisted. The config half had no such checker for
    # every field until 2026-08-01 (a `.npc`'s `param=death_anim,<seq>` resolved
    # to -1 in silence), which is exactly why it is the half worth a bar.
    scan_scripts = not args.check
    for path in (walk(args.src, ('.rs2',)) if scan_scripts else ()):
        rel = os.path.relpath(path, args.src)
        if args.only and not any(rel.startswith(p) for p in args.only):
            continue
        with open(path, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                line = re.sub(r'//.*$', '', line)
                line = re.sub(r'"[^"]*"', '""', line)
                head = re.match(r'\s*\[([a-z0-9_]+)\s*,', line)
                if head:
                    continue  # a trigger header names a subject, checked below
                for name in word.findall(line):
                    if (name in GRAMMAR or name in commands or name in known
                            or name in declared or TRIGGERS.match(name)):
                        continue
                    unresolved[name][rel] += 1

    # The other half of the authored tree. Skipped for `--only`, which selects
    # script subdirectories.
    if not args.scripts_only:
        for name, rel in config_references(constants):
            if args.only and not any(rel.startswith(p) for p in args.only):
                continue
            if (name in GRAMMAR or name in CONFIG_GRAMMAR or name in commands
                    or name in known or name in declared):
                continue
            unresolved[name][rel] += 1

    if args.check:
        # The build bar. Nothing over-reports here that is not also a real
        # question: a name in an authored config that no pack and no block in
        # this tree declares is either a typo or a namespace that has not been
        # imported, and triage §13 bar 1 says both are errors.
        problems = 0
        for name in sorted(unresolved):
            files = unresolved[name]
            ns = ','.join(sorted(reference.get(name, {'?'})))
            where = sorted(files)[0]
            print(f'ss_unresolved: `{name}` does not resolve '
                  f'({sum(files.values())} use(s), first in {where}; '
                  f'the reference calls it {ns})', file=sys.stderr)
            problems += 1
        tree = args.tree or os.path.normpath(os.path.join(args.src, '..', '..'))
        for what, where in bare_ids(constants, tree):
            print(f'ss_unresolved: {where}: {what} — triage §13 bar 5',
                  file=sys.stderr)
            problems += 1
        if problems:
            print(f'ss_unresolved: {problems} problem(s)', file=sys.stderr)
            return 1
        print(f'ss_unresolved: every name in {args.src} resolves, '
              f'and no content file carries a bare id')
        return 0

    if not unresolved:
        print('nothing unresolved')
        return 0

    # Group by what the reference says the name is, because that is the axis the
    # fix follows: one missing namespace, not fifty missing names.
    by_ns = defaultdict(list)
    for name, files in unresolved.items():
        total = sum(files.values())
        ns = ','.join(sorted(reference.get(name, {'?'})))
        by_ns[ns].append((total, name, len(files)))

    print(f'{len(unresolved)} unresolved name(s) in {args.src}\n')
    for ns in sorted(by_ns, key=lambda k: -sum(t for t, _, _ in by_ns[k])):
        rows = sorted(by_ns[ns], reverse=True)
        print(f'--- reference namespace: {ns}  '
              f'({len(rows)} names, {sum(t for t, _, _ in rows)} uses)')
        for total, name, nfiles in rows:
            print(f'  {total:5}  {name:44} in {nfiles} file(s)')
        print()
    return 1


if __name__ == '__main__':
    sys.exit(main())
