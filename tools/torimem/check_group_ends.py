#!/usr/bin/env python3
"""check-group-ends: every place a lifetime group is reset or ended is listed.

A group's end is where "always freed" is proven, so each one is a reviewed
decision. This lists every call to ToriMem_GroupReset / GroupEnd / GroupDelete
/ FrameEnd / CheckedRewind / Shutdown outside src/mem/ and tests, keyed by
file and enclosing function, and compares the set with group_ends.txt:

  - a call site not in the manifest fails: add a row naming the group it ends
    and why that function is the one place it ends;
  - a manifest row with no call site left fails as stale.

  python3 tools/torimem/check_group_ends.py
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MANIFEST = os.path.join(ROOT, 'tools', 'torimem', 'group_ends.txt')
CALL = re.compile(r'\b(ToriMem_(?:GroupReset|GroupEnd|GroupDelete|FrameEnd|CheckedRewind|Shutdown))\s*\(')
FUNC = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s*\(', re.M)
EXEMPT = re.compile(r'(^src/mem/|/test/|/tests/|_test\.c$)')

def sites():
    files = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard',
                            'src/*.c', 'src/*.h', 'src/*.inc'],
                           cwd=ROOT, capture_output=True, text=True).stdout.split()
    found = set()
    for f in files:
        if EXEMPT.search(f):
            continue
        try:
            text = open(os.path.join(ROOT, f), errors='ignore').read()
        except OSError:
            continue
        if 'ToriMem_' not in text:
            continue
        # definitions start at column 0: "Name(" on its own line after the return type
        defs = [(m.start(), m.group(1)) for m in FUNC.finditer(text)]
        for m in CALL.finditer(text):
            line_start = text.rfind('\n', 0, m.start()) + 1
            if text[line_start:m.start()].lstrip().startswith(('*', '//', '/*')):
                continue
            fn = '?'
            for pos, name in defs:
                if pos < m.start():
                    fn = name
                else:
                    break
            found.add((f, fn, m.group(1)))
    return found

def main():
    have = sites()
    listed = set()
    if os.path.exists(MANIFEST):
        for line in open(MANIFEST):
            line = line.split('#', 1)[0].strip()
            if line:
                f, fn, call = line.split()[:3]
                listed.add((f, fn, call))
    new = sorted(have - listed)
    stale = sorted(listed - have)
    for f, fn, call in new:
        print('check-group-ends: unlisted %s in %s (%s) -- add it to tools/torimem/group_ends.txt '
              'with the group it ends' % (call, fn, f))
    for f, fn, call in stale:
        print('check-group-ends: stale manifest row: %s %s %s' % (f, fn, call))
    print('check-group-ends: %d end site(s) listed, %d unlisted, %d stale'
          % (len(have & listed), len(new), len(stale)))
    return 1 if new or stale else 0

sys.exit(main())
