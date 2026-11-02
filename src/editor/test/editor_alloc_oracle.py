#!/usr/bin/env python3
"""What tools/ss_allocate.py would allocate, per namespace, for editor_alloc_test.

    editor_alloc_oracle.py <tools-dir> <content-tree> <out-dir> <probe-name> <ns>...

Imports ss_allocate itself, so the rules are its rules and not a restatement:
for each namespace it computes the script's `mark`, `floor`, `base` and the id
and spelling one fresh name would get, prints them one line per namespace,
and writes what `write_pack` would leave in `pack/<ns>.alloc` after appending
that name (`<out-dir>/<ns>.alloc`) and in an empty ledger
(`<out-dir>/<ns>.empty.alloc`). The tree is only read; the writes go to
<out-dir>.

Line format (space separated):
    ns floor base next name authority ledger_names other_names highest_other
"""

import os
import sys


def main():
    tools, tree, out_dir, probe = sys.argv[1:5]
    namespaces = sys.argv[5:]
    sys.path.insert(0, tools)
    import ss_allocate as sa  # noqa: E402 -- from the path given

    os.makedirs(out_dir, exist_ok=True)
    bases = sa.register_bases()
    for ns in namespaces:
        raw, mapping = sa.read_pack(sa.alloc_path(tree, ns))
        elsewhere, highest = sa.other_layers(tree, ns)
        mark = max(highest, max(mapping.values()) if mapping else -1) + 1
        floor = sa.declared_base(tree, ns, bases)
        base = max(mark, floor)
        authority = sa.id_authority(tree, ns)
        held = set(mapping.values()) | set(elsewhere.values())
        ident = base
        while ident in held:
            ident += 1
        prefix = sa.VAR_PREFIX.get(ns)
        name = f'{prefix}{ident}_{probe}' if prefix else probe
        sa.write_pack(os.path.join(out_dir, f'{ns}.alloc'), ns, raw, [(ident, name)])
        sa.write_pack(os.path.join(out_dir, f'{ns}.empty.alloc'), ns, [], [(ident, name)])
        print(ns, floor, base, ident, name, authority, len(mapping), len(elsewhere), highest)
    return 0


if __name__ == '__main__':
    sys.exit(main())
