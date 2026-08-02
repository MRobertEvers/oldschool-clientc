#!/usr/bin/env python3
"""
port_droptables_check — the bars the drop tables slice needs and nothing else holds.

    tools/port_droptables_check.py --report   # the review artifact, TSV on stdout
    tools/port_droptables_check.py --check    # the build bar (test-port)

Four things go wrong here, all of them silently, and none of them is caught by
`sscompile`, by `mock230_pack` or by the name-resolution gate.

1.  **A duplicate trigger binding.** Two files both declaring `[ai_queue3,goblin]`
    compile to two scripts, not one — measured, because the inherited claim was
    that the compiler collapses them (docs/LOSTCITY_PORT_TRIAGE.md §16.11).
    `SSVM_ProviderLoad` sorts by key, counts equal adjacent keys into
    `provider->duplicate_keys`, and **nothing in mock230 ever reads that field**.
    `find_by_key` is a `bsearch`, so which of the two answers is whichever the
    search lands on: walk-order dependent, and one whole table quietly does not
    exist. §13 bar 4 calls a second binding a conflict; until this file, nothing
    enforced it.

2.  **A category-keyed trigger naming a category that is not this cache's.**
    `[ai_queue3,_giantrat]` compiles and binds — to osrs239's 262, which is the
    *rat* category, so the giant rat's table lands on the plain rat standing
    beside it. `port/categories.map` records which names survived that test
    (`minted`) and which did not (`broader`, `split`, `orphan`); this bar is that
    file being obeyed rather than filed.

3.  **An exact binding shadowing a category binding.** `SSVM_ProviderGetByTrigger`
    is type -> category -> global, so an npc bound by name never reaches the
    category script. That is correct behaviour and a correct half-converted-file
    detector: if `[ai_queue3,_chicken]` and `[ai_queue3,chicken]` both exist, the
    chicken silently keeps the old table and only its siblings get the new one.

4.  **A bound table that forgets `npc_param(death_drop)`.** Binding
    `[ai_queue3,<npc>]` at all *suppresses* the engine's death-drop fallback
    (`mock230_world_npc_died`, MOCK230_FALLBACK_AI_QUEUE3). A table that does not
    re-state the drop takes the bones away from an npc that had them, and the
    only symptom is loot that looks slightly thin. Either state it, or waive it in
    the file with `// no-death-drop: <reason>` on its own line — a waiver is a
    sentence somebody wrote, which is the point.
"""
import argparse
import collections
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")

# The trigger families whose subject is an npc type (or an npc category).
NPC_TRIGGERS = (
    ["ai_queue%d" % i for i in range(1, 21)]
    + ["ai_timer", "ai_apnpc1", "ai_apnpc2", "ai_apnpc3", "ai_apnpc4", "ai_apnpc5"]
    + ["opnpc%d" % i for i in range(1, 6)]
    + ["apnpc%d" % i for i in range(1, 6)]
    + ["opnpct", "apnpct", "opnpcu", "apnpcu", "ainpc1", "ainpc2", "ainpc3", "ainpc4", "ainpc5"]
)

TRIGGER_RE = re.compile(r'^\[([a-z_0-9]+)\s*,\s*([^\],]+?)\s*\](.*)$')
DEATH_DROP_RE = re.compile(r'npc_param\s*\(\s*death_drop\s*\)')
WAIVER_RE = re.compile(r'^\s*//\s*no-death-drop:\s*\S')


def strip_comments(text):
    """Drop `//` tails, line by line.

    Line by line and not over the whole block, which is the bug this comment
    exists to stop coming back: splitting a multi-line body on the first `//`
    anywhere throws away everything after the first comment, and every table in
    this directory has a comment in its first three lines. It reported all 136
    bindings as missing their death drop, which reads exactly like the port
    being wrong rather than the checker.
    """
    return "\n".join(line.split("//")[0] for line in text.split("\n"))


def load_pack(path):
    """`id=name` lines -> {name: id}. Comments and blanks skipped."""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("//") or "=" not in line:
                continue
            ident, name = line.split("=", 1)
            try:
                out[name] = int(ident)
            except ValueError:
                continue
    return out


def load_npc_categories(path):
    """{npc name: category id} out of configs/all.npc."""
    out, cur = {}, None
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip()
            if line.startswith("[") and line.endswith("]"):
                cur = line[1:-1]
            elif cur and line.startswith("category="):
                try:
                    out[cur] = int(line.split("=", 1)[1])
                except ValueError:
                    pass
    return out


def load_categories_map(path):
    """{name: (disposition, id)} out of port/categories.map."""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 3:
                out[parts[0]] = (parts[1], parts[2])
    return out


class Block:
    __slots__ = ("kind", "subject", "tail", "path", "line", "body")

    def __init__(self, kind, subject, tail, path, line):
        self.kind, self.subject, self.tail = kind, subject, tail
        self.path, self.line = path, line
        self.body = []


def parse_tree(scripts_dir):
    """Every `[kind,subject]` block in the tree, with its body lines."""
    blocks = []
    for root, _dirs, files in os.walk(scripts_dir):
        if os.path.basename(root) == "build":
            continue
        for fn in sorted(files):
            if not fn.endswith(".rs2"):
                continue
            path = os.path.join(root, fn)
            cur = None
            with open(path, encoding="utf-8", errors="replace") as fh:
                for n, raw in enumerate(fh, 1):
                    m = TRIGGER_RE.match(raw.rstrip("\n"))
                    if m:
                        cur = Block(m.group(1), m.group(2), m.group(3), path, n)
                        blocks.append(cur)
                    elif cur is not None:
                        cur.body.append(raw.rstrip("\n"))
    return blocks


def resolve_body(block, labels, seen=None):
    """A block's effective body, following `@label;` and `gosub(proc)` hops.

    `gosub` was added when the `[ai_queue3,_]` wildcard landed: the reference
    writes that rung as `gosub(npc_default_death)` and states the death drop in
    the proc, so a checker that followed only `@label` looked straight past it
    and reported the wildcard as dropping nothing. `labels` carries both kinds
    now — see its construction in `run`.
    """
    seen = seen or set()
    text = block.tail + "\n" + "\n".join(block.body)
    for m in re.finditer(r'@([a-z_0-9]+)\s*;|gosub\(\s*([a-z_0-9]+)\s*\)', text):
        name = m.group(1) or m.group(2)
        if name in labels and name not in seen:
            seen.add(name)
            text += "\n" + resolve_body(labels[name], labels, seen)
    return text


def run(tree, report):
    scripts = os.path.join(tree, "server", "scripts")
    npc_pack = load_pack(os.path.join(tree, "configs", "all.npc.compack"))
    cat_pack = load_pack(os.path.join(tree, "pack", "category.pack"))
    npc_cat = load_npc_categories(os.path.join(tree, "configs", "all.npc"))
    cat_map = load_categories_map(os.path.join(tree, "port", "categories.map"))

    blocks = parse_tree(scripts)
    # Both kinds, because resolve_body follows both. A proc and a label cannot
    # share a name in this grammar, so one dict is safe.
    labels = {b.subject: b for b in blocks if b.kind in ("label", "proc")}
    problems = []
    rows = []

    # ---- bar 1: no duplicate trigger binding, tree-wide -------------------
    keyed = collections.defaultdict(list)
    for b in blocks:
        keyed[(b.kind, b.subject)].append(b)
    for (kind, subject), got in sorted(keyed.items()):
        if len(got) > 1:
            where = ", ".join("%s:%d" % (os.path.relpath(g.path, tree), g.line) for g in got)
            problems.append(
                "duplicate trigger [%s,%s] declared %d times (%s) — both compile, and "
                "SSVM_ProviderLoad's bsearch decides which one answers"
                % (kind, subject, len(got), where))

    # ---- the npc-family bindings -----------------------------------------
    npc_blocks = [b for b in blocks if b.kind in NPC_TRIGGERS]
    bound_cat_ids = {}
    for b in npc_blocks:
        if not b.subject.startswith("_"):
            continue
        # The bare `_` is the wildcard rung, not a category. It did not exist in
        # the tree until the ai_queue3 fallback was evicted, and `b.subject[1:]`
        # makes it the empty string, which no pack states.
        if b.subject == "_":
            continue
        name = b.subject[1:]
        where = "%s:%d" % (os.path.relpath(b.path, tree), b.line)

        # ---- bar 2: a category subject must be minted --------------------
        if name not in cat_pack:
            problems.append(
                "[%s,%s] at %s names a category pack/category.pack does not state"
                % (b.kind, b.subject, where))
            continue
        disposition = cat_map.get(name, ("(not in categories.map)", "-"))[0]
        if disposition != "minted":
            problems.append(
                "[%s,%s] at %s binds a category port/categories.map holds at '%s' — "
                "bind the reference's members by name instead, or change that row first"
                % (b.kind, b.subject, where, disposition))
            continue
        bound_cat_ids.setdefault(cat_pack[name], []).append((b.kind, name, where))
        rows.append(("category", b.kind, b.subject, str(cat_pack[name]), disposition, where))

    for b in npc_blocks:
        if b.subject.startswith("_"):
            continue
        where = "%s:%d" % (os.path.relpath(b.path, tree), b.line)
        if b.subject not in npc_pack:
            problems.append(
                "[%s,%s] at %s names an npc configs/all.npc.compack does not state"
                % (b.kind, b.subject, where))
            continue
        rows.append(("npc", b.kind, b.subject, str(npc_pack[b.subject]),
                     str(npc_cat.get(b.subject, "-")), where))

        # ---- bar 3: an exact binding shadowing a category binding --------
        cat = npc_cat.get(b.subject)
        for kind, name, cat_where in bound_cat_ids.get(cat, []):
            if kind != b.kind:
                continue
            problems.append(
                "[%s,%s] at %s shadows [%s,_%s] at %s (npc carries category %d) — "
                "the lookup is type -> category, so the named npc never reaches the "
                "category script"
                % (b.kind, b.subject, where, kind, name, cat_where, cat))

    # ---- bar 4: a bound ai_queue3 must state or waive the death drop -----
    waived = set()
    for path in {b.path for b in npc_blocks}:
        with open(path, encoding="utf-8", errors="replace") as fh:
            if any(WAIVER_RE.match(line) for line in fh):
                waived.add(path)
    for b in npc_blocks:
        if b.kind != "ai_queue3":
            continue
        if DEATH_DROP_RE.search(strip_comments(resolve_body(b, labels))):
            continue
        if b.path in waived:
            continue
        problems.append(
            "[%s,%s] at %s:%d never reaches npc_param(death_drop) — nothing else "
            "drops it now that the ai_queue3 engine fallback is gone, so this npc "
            "silently stops dropping what its config says it drops. State it, or "
            "waive it in this file with a `// no-death-drop: <reason>` line"
            % (b.kind, b.subject, os.path.relpath(b.path, tree), b.line))

    if report:
        print("\t".join(("kind", "trigger", "subject", "id", "category/disposition", "where")))
        for row in sorted(rows):
            print("\t".join(row))
    return problems, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", default=DEFAULT_TREE)
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    problems, rows = run(args.tree, args.report)

    if args.check or not args.report:
        for p in problems:
            print("port_droptables_check: %s" % p, file=sys.stderr)
        if problems:
            print("port_droptables_check: %d problem(s)" % len(problems), file=sys.stderr)
            return 1
        if args.verbose or not args.report:
            n_cat = sum(1 for r in rows if r[0] == "category")
            print("port_droptables_check: %d npc-family binding(s), %d of them "
                  "category-keyed, no duplicates, every bound ai_queue3 states its "
                  "death drop" % (len(rows), n_cat))
    return 0


if __name__ == "__main__":
    sys.exit(main())
