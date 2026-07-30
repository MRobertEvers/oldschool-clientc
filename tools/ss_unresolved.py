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


def walk(root, suffixes):
    for base, _, files in os.walk(root):
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
    args = ap.parse_args()

    constants = args.constants or args.src
    known = load_packs(args.pack)
    declared = load_tree_symbols(args.src, constants)
    commands = command_names(args.opcodes)
    reference = load_packs([os.path.join(args.reference, 'pack')]) if args.reference else {}

    # A bare word, but not one preceded by $ ^ % @ ~ or a quote.
    word = re.compile(r'(?<![\w$^%@~"\'])([a-z][a-z0-9_]*)(?![\w"\'])')

    unresolved = defaultdict(lambda: defaultdict(int))
    for path in walk(args.src, ('.rs2',)):
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
