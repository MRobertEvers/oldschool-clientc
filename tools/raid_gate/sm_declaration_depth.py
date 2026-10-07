#!/usr/bin/env python3
"""HOW MUCH OF A MACHINE'S BEHAVIOUR IS ACTUALLY DECLARED?

QD.raid.sm_coverage answers "is every declared state reachable".  That is not
the question that matters.  A machine can declare three states, enter all
three every run, certify as fully live -- and keep every branch it had, as
`if` guards inside two handler bodies.

The owner, 2026-10-07, on the Dawnbringer machine: "You should be using state
machines or hierarchical state machines.  Why did you go back to boolean
soup?"  It declared absent / held / done and carried the whole behaviour --
wield, arm, fire, five-tick spacing, hide, spend, drop, the round-robin turn --
inside two handlers.  Coverage certified it, because three states are all easy
to enter.

So this measures the other axis: BRANCHES INSIDE THE BODIES A DECLARATION
REACHES, against the TRANSITIONS IT NAMES.

    states    declared state names
    branches  `if` / `elseif` in every handler, enter and exit body the
              declaration reaches, following names to plan-level functions,
              file locals and factories
    edges     distinct transition targets those bodies name, plus `broken`
              targets, declared `children`, and dynamic targets counted once
    ratio     branches / edges -- higher means more behaviour that the
              declaration does not express

HEURISTIC, and the limits are real: a text scan, not a Lua parser.  It cannot
see a branch behind an `and`/`or` chain, a table dispatch, or a handler stored
somewhere it cannot resolve, and it counts a branch that merely guards a log
line the same as one that chooses behaviour.  So the number is a FLOOR on
undeclared behaviour, never a ceiling, and a LOW ratio is much weaker evidence
than a high one.  Read it to indict, not to acquit.
"""
import re, glob, os

ROOT = '/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid'
WORD = re.compile(r'\b(function|if|for|while|do|repeat|end|until)\b')

def clean_of(text):
    """comments and string bodies blanked, LENGTH PRESERVED so an offset into
    the original is still valid here (getting this wrong silently read as
    '0 branches' for every delegated body)"""
    def blank(m):
        return ''.join('\n' if ch == '\n' else ' ' for ch in m.group(0))
    out = re.sub(r'--\[\[.*?\]\]', blank, text, flags=re.S)
    out = re.sub(r'--[^\n]*', blank, out)
    out = re.sub(r'"(?:[^"\\\n]|\\.)*"', blank, out)
    out = re.sub(r"'(?:[^'\\\n]|\\.)*'", blank, out)
    assert len(out) == len(text)
    return out

def block_end(clean, start, depth=1):
    """offset just past the `end` that closes the construct open at `start`"""
    j = start
    while j < len(clean):
        m = WORD.search(clean, j)
        if m is None:
            return len(clean)
        w = m.group(1)
        if w in ('function', 'if', 'for', 'while', 'repeat'):
            depth += 1
        elif w in ('end', 'until'):
            depth -= 1
            if depth == 0:
                return m.start()
        j = m.end()
    return len(clean)

def brace_span(text, start):
    i = text.index('{', start)
    d, j = 0, i
    while j < len(text):
        if text[j] == '{': d += 1
        elif text[j] == '}':
            d -= 1
            if d == 0: return i, j + 1
        j += 1
    raise ValueError('unbalanced')

def branches(body, clean_body):
    return len(re.findall(r'\bif\b', clean_body)) + len(re.findall(r'\belseif\b', clean_body))

def targets(body):
    out = set()
    for m in re.finditer(r'return\s+[^\n]*?,\s*"([A-Za-z_]\w*)"', body): out.add(m.group(1))
    for m in re.finditer(r'\bbroken\s*=\s*"([A-Za-z_]\w*)"', body): out.add(m.group(1))
    # A helper that takes its target as an ARGUMENT and returns it -- e.g.
    # `_verzik_sword_fire(c, ev, "ARMED")` called from two states with two
    # different literals -- is two edges, not the one dynamic target a naive
    # read sees.  Credit the literals at the call site (owner_verzik,
    # 2026-10-07: "if the tool credits that as one edge rather than two, I am
    # under-credited by one").
    for m in re.finditer(r'QD\.raid\._\w+\s*\([^()\n]*?"([A-Z][A-Z_0-9]*)"', body):
        out.add(m.group(1))
    return out

def dyn(body):
    return set(m.group(1) for m in
               re.finditer(r'return\s+[^\n,]*,\s*(ev\.go|ev\.to|go|name|back|state|r\.state)\b', body))

def table_targets(text, body):
    """A machine that drives its targets off a CONSTANT TABLE --
    `return nil, QD.RAID_MAIDEN_PHASE_STATES[ev.form]` -- names every value in
    that table, not one dynamic target.  Resolving it matters because the
    naive read collapses the table to 1 and then divides the branches by 1, so
    THE MOST CONNECTED GRAPH SCORES THE FEWEST EDGES and its ratio is inflated
    by up to the size of the table (reported by the Maiden port, 2026-10-07).
    That is the opposite of this tool's "under-claim rather than mis-indict"
    rule, so it is a bug and not a rounding."""
    out = set()
    for m in re.finditer(r'return\s+[^\n,]*,\s*([A-Za-z_][\w.]*)\s*\[', body):
        name = m.group(1)
        tm = re.search(r'\n(?:local\s+)?' + re.escape(name) + r'\s*=\s*\{', text)
        if tm is None: continue
        try: ta, tb = brace_span(text, tm.end() - 1)
        except ValueError: continue
        for vm in re.finditer(r'"([A-Za-z_]\w*)"', text[ta:tb]): out.add(vm.group(1))
    return out

def find_named(text, clean, name):
    """body of a handler reached by NAME: a plan function, a file local, or a
    `local x = function`"""
    for pat in (r'\nfunction\s+QD\.raid\.%s\s*\(',
                r'\n\s*local function\s+%s\s*\(',
                r'\n\s*local\s+%s\s*=\s*function\s*\('):
        m = re.search(pat % re.escape(name), text)
        if m:
            e = block_end(clean, m.end())
            return text[m.end():e], clean[m.end():e]
    return None, None

rows = []
for path in sorted(glob.glob(os.path.join(ROOT, 'script/plugins/quest_driver/raid_play_tob_*.lua'))):
    room = os.path.basename(path)[len('raid_play_tob_'):-4]
    text = open(path, errors='replace').read()
    clean = clean_of(text)
    for dm in re.finditer(r'QD\.raid\.sm_declare\(\s*("?[^,]+?"?)\s*,', text):
        mid = dm.group(1).strip().strip('"')
        try: a, b = brace_span(text, dm.end())
        except ValueError: continue
        decl = text[a:b]
        sm = re.search(r'states\s*=\s*', decl)
        if sm is None: continue
        tail = decl[sm.end():].lstrip()
        if tail.startswith('{'):
            sa, sb = brace_span(decl, sm.end())
            sbody, soff = decl[sa:sb], a + sa
        else:
            nm = re.match(r'([A-Za-z_]\w*)', tail)
            if nm is None: continue
            ref = nm.group(1)
            rm = (re.search(r'\n(?:local\s+)?%s\s*=\s*\{' % re.escape(ref), text)
                  or re.search(r'\nlocal function\s+%s\s*\(' % re.escape(ref), text))
            if rm is None: continue
            try: ta, tb = brace_span(text, rm.start())
            except ValueError: continue
            sbody, soff = text[ta:tb], ta
        # state names at depth 1 of the states table
        names, depth = [], 0
        for line in sbody[1:-1].split('\n'):
            if depth == 0:
                mm = re.match(r'\s*([A-Za-z_]\w*)\s*=', line)
                if mm: names.append(mm.group(1))
            depth += line.count('{') - line.count('}')
        # every `<key> = <value>` in the declaration, line by line
        nbr, edges, dynset, delegated = 0, set(), set(), set()
        for mm in re.finditer(r'([A-Za-z_]\w*)\s*=\s*', sbody):
            rhs = sbody[mm.end():]
            if rhs.startswith('function'):
                off = soff + mm.end()
                e = block_end(clean, off + len('function'))
                body, cbody = text[off:e], clean[off:e]
                nbr += branches(body, cbody)
                edges |= targets(body) | table_targets(text, body); dynset |= dyn(body)
                # an inline handler that merely forwards -- `function(c) return
                # QD.raid._verzik_phase_p3(c) end` -- carries none of the
                # behaviour itself, so follow it or a 1,002-line phase body
                # reads as zero.  Library helpers (_play_*) are NOT followed:
                # they are the executor's, not this machine's.
                for fm in re.finditer(r'QD\.raid\.(_\w+)\s*\(', body):
                    if not fm.group(1).startswith('_play_'): delegated.add(fm.group(1))
            else:
                im = re.match(r'(QD\.raid\.)?([A-Za-z_]\w*)\s*(\(|,|$|\n)', rhs)
                if im and im.group(2) not in ('true', 'false', 'nil') \
                        and not im.group(2).startswith('_play_'):
                    delegated.add(im.group(2))
        # a state whose `on` is a shared table
        for mm in re.finditer(r'on\s*=\s*(QD\.[A-Z]\w*)', sbody):
            tm = re.search(r'\n%s\s*=\s*' % re.escape(mm.group(1)), text)
            if tm:
                ta, tb = brace_span(text, tm.end())
                for hm in re.finditer(r'QD\.raid\.(_\w+)\s*\(', text[ta:tb]): delegated.add(hm.group(1))
                for hm in re.finditer(r'=\s*function', text[ta:tb]):
                    off = ta + hm.end() - len('function')
                    e = block_end(clean, off + len('function'))
                    nbr += branches(text[off:e], clean[off:e])
                    edges |= targets(text[off:e]) | table_targets(text, text[off:e]); dynset |= dyn(text[off:e])
        # TRANSITIVE.  A handler can be two hops from its branches: the table
        # entry is a bare file-local (`leave_window = duty_leave`), that local
        # merely calls QD.raid._bloat_leave(c, ev), and the branches live
        # there.  Following one hop counts the wrapper as a branch-free body
        # and stops, which FLATTERS any room that names its handlers through
        # wrappers -- Bloat read 0.1 where the honest count is 0.6 (reported by
        # the Bloat port, 2026-10-07, against its own headline).
        seen, work = set(), list(delegated)
        while work:
            name = work.pop()
            if name in seen: continue
            seen.add(name)
            body, cbody = find_named(text, clean, name)
            if body is None: continue
            nbr += branches(body, cbody)
            edges |= targets(body) | table_targets(text, body); dynset |= dyn(body)
            for fm in re.finditer(r'QD\.raid\.(_\w+)\s*\(', body):
                if not fm.group(1).startswith('_play_') and fm.group(1) not in seen:
                    work.append(fm.group(1))
            for fm in re.finditer(r'(?<![\w.])([a-z_]\w*)\s*\(c\s*[,)]', body):
                if fm.group(1) not in seen: work.append(fm.group(1))
        for mm in re.finditer(r'children\s*=\s*\{([^}]*)\}', sbody):
            for cm in re.finditer(r'"(\w+)"', mm.group(1)): edges.add('child:' + cm.group(1))
        # An UNRESOLVED dynamic target could be any declared state, so count it
        # as the state count rather than as 1.  Counting it as 1 inflates the
        # ratio, which mis-indicts; counting it as the state count under-claims,
        # which is the bias this tool is supposed to have.
        nedge = len(edges)
        if dynset and len(edges) < len(names):
            nedge = max(nedge, len(names))
        ratio = (nbr / nedge) if nedge else float(nbr)
        rows.append((ratio, room, mid, len(names), nbr, nedge, bool(dynset)))

rows.sort(reverse=True)
print("%-9s %-24s %6s %9s %6s %8s" % ("room", "machine", "states", "branches", "edges", "ratio"))
for ratio, room, mid, ns, nbr, nedge, hd in rows:
    print("%-9s %-24s %6d %9d %6d %8.1f%s" % (room, mid, ns, nbr, nedge, ratio, "  dyn" if hd else ""))
