#!/usr/bin/env python3
"""check-bare-alloc: the migration to lifetime groups only moves forward.

Counts bare malloc/calloc/realloc/strdup/strndup/free CALLS (outside comments
and strings) per file under src/ and the in-house libraries' host-facing code,
and compares them with the checked-in baseline (bare_alloc_baseline.txt):

  - a file whose count went UP fails: new code allocates through a group
    (src/mem/torirs_mem.h), never bare;
  - a file not in the baseline that has any bare call fails;
  - a file whose count went DOWN passes, and --update writes the new baseline,
    so the ratchet tightens as subsystems convert.

Exempt: src/mem/ (the allocator itself), tests, tools, and the libraries'
default-libc wrappers. Memory owned by SDL/stb/Lua/the browser keeps its own
free and is listed per file in the baseline like any other bare call.

  python3 tools/torimem/check_bare_alloc.py            # check
  python3 tools/torimem/check_bare_alloc.py --update   # rewrite the baseline
  python3 tools/torimem/check_bare_alloc.py --list src/ui/uitree.c ...   # each call

A call whose memory an outside API owns is marked `torimem-bare: <reason>` in a
comment on its line or the line above, and is not counted.
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE = os.path.join(ROOT, 'tools', 'torimem', 'bare_alloc_baseline.txt')
CALL = re.compile(r'(?<![A-Za-z0-9_.>])(malloc|calloc|realloc|strdup|strndup|free)\s*\(')
EXEMPT = re.compile(r'(^src/mem/|/test/|/tests/|/tools/|_test\.c$|^src/OSRS-Content/'
                    r'|_alloc\.c$|^src/platform/torirs_memtrace\.c$|^src/platform/torirs_memprof_win32\.c$)')

def strip(src):
    # drop comments and string/char literals, keep line structure
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith('//', i):
            j = src.find('\n', i); i = n if j < 0 else j; continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); j = n if j < 0 else j + 2
            out.append('\n' * src.count('\n', i, j)); i = j; continue
        c = src[i]
        if c in '"\'':
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            i = j + 1; out.append('""'); continue
        out.append(c); i += 1
    return ''.join(out)

# A call whose memory belongs to an outside API (SDL_GetBasePath, stbi_load,
# getline, Lua, a browser or JNI buffer) keeps its own free. It is marked, on
# its line or the line above, with the reason:
#     free(path); /* torimem-bare: SDL_GetPrefPath's, freed with SDL's free */
MARK = 'torimem-bare:'

def bare_calls(f):
    """[(line, text)] of the unmarked bare calls in `f`."""
    raw = open(os.path.join(ROOT, f), errors='ignore').read()
    raw_lines = raw.split('\n')
    marked = set()
    for i, line in enumerate(raw_lines):
        if MARK in line:
            marked.add(i)
            marked.add(i + 1)
    text = strip(raw)
    # a libc-default wrapper body (return malloc(size);) is the hook itself
    text = re.sub(r'_libc_(malloc|calloc|realloc|free)\([^)]*\)\s*\{[^}]*\}',
                  lambda m: '\n' * m.group(0).count('\n'), text)
    out = []
    for i, line in enumerate(text.split('\n')):
        if i in marked:
            continue
        for _ in CALL.findall(line):
            out.append((i + 1, raw_lines[i].strip() if i < len(raw_lines) else ''))
    return out

def count_all():
    files = subprocess.run(['git', 'ls-files', 'src/*.c', 'src/*.h', 'src/*.inc'],
                           cwd=ROOT, capture_output=True, text=True).stdout.split()
    counts = {}
    for f in files:
        if EXEMPT.search(f):
            continue
        try:
            n = len(bare_calls(f))
        except OSError:
            continue
        if n:
            counts[f] = n
    return counts

def load():
    base = {}
    if os.path.exists(BASELINE):
        for line in open(BASELINE):
            line = line.strip()
            if line and not line.startswith('#'):
                n, f = line.split(None, 1)
                base[f] = int(n)
    return base

def main():
    if '--list' in sys.argv:
        # --list FILE...: every unmarked bare call, as file:line: text
        for f in sys.argv[sys.argv.index('--list') + 1:]:
            for line, text in bare_calls(f):
                print('%s:%d: %s' % (f, line, text))
        return 0
    counts = count_all()
    if '--update' in sys.argv:
        with open(BASELINE, 'w') as out:
            out.write('# bare allocation calls per file; may only go down. '
                      'Regenerate with tools/torimem/check_bare_alloc.py --update\n')
            for f in sorted(counts):
                out.write('%d %s\n' % (counts[f], f))
        print('check-bare-alloc: baseline written: %d files, %d calls'
              % (len(counts), sum(counts.values())))
        return 0
    base = load()
    bad = [(f, base.get(f, 0), n) for f, n in sorted(counts.items()) if n > base.get(f, 0)]
    for f, was, now in bad:
        print('check-bare-alloc: %s: %d bare allocation call(s), baseline %d -- allocate '
              'through a lifetime group (src/mem/torirs_mem.h)' % (f, now, was))
    went_down = sum(1 for f, n in base.items() if counts.get(f, 0) < n)
    print('check-bare-alloc: %d files, %d bare calls (baseline %d)%s'
          % (len(counts), sum(counts.values()), sum(base.values()),
             '; %d file(s) went down -- run with --update to tighten' % went_down if went_down else ''))
    return 1 if bad else 0

sys.exit(main())
