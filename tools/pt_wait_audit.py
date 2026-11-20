#!/usr/bin/env python3
"""List every PT_WAIT_UNTIL / PT_WAIT_WHILE / PT_RESTART in a client task.

Those minipt macros suspend with PT_WAITING, and the client's task queues
accept only PT_YIELDED, PT_EXITED and PT_ENDED: ToriRS_TaskQueue_RunTask
(src/asyncio.h) asserts on anything else, and an NDEBUG build answers ERROR.
A task that waits on a condition says how it waits instead: TASK_AWAIT_STATE
(re-tested once a pump) or an await on a task or an asset.

src/torirsserver is skipped: its coroutines are stepped by the server's own
driver, not by a client task queue. Run from src/makefile as check-pt-wait;
the last line must read `total 0` (comments are stripped first).
"""
import os
import re
import sys

root = sys.argv[1] if len(sys.argv) > 1 else 'src'
skip = (os.path.join(root, 'torirsserver'), os.path.join(root, '3rd'))
hits = []
for d, _, files in os.walk(root):
    if d.startswith(skip):
        continue
    for f in files:
        if not f.endswith(('.c', '.h')):
            continue
        p = os.path.join(d, f)
        s = open(p, encoding='utf-8', errors='replace').read()
        # Blank comments out, keeping their newlines so line numbers hold.
        s = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), s, flags=re.S)
        s = re.sub(r'//[^\n]*', '', s)
        for m in re.finditer(r'\bPT_(WAIT_UNTIL|WAIT_WHILE|RESTART)\s*\(', s):
            hits.append((p, s[:m.start()].count('\n') + 1, m.group(1)))
for h in hits:
    print('%s:%d PT_%s' % h)
print('total', len(hits))
