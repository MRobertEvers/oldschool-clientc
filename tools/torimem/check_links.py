#!/usr/bin/env python3
"""check-torimem-links: every rule that links code reaching ToriMem links the core.

A file reaches ToriMem directly (ToriMem_*) or through the task machinery's
static inlines in asyncio.h / task_runner.h (task_free ends the task's group).
Test rules spell their sources out, so a rule that compiles such a file without
$(TORIMEM_SRCS) fails to link, but only when someone next builds that test.
This dry-runs every make target and names those rules. Run from src/."""
import os, re, subprocess, sys, functools
NEEDS = re.compile(r'\bToriMem_|\btask_free\(|ToriRS_TaskQueue_|TaskRunner_[A-Z]|TASK_AWAIT|PT_TASK_|ToriRS_TaskMem\(')
srcs = subprocess.run(['git', 'ls-files', '*.c', '*.h'], capture_output=True, text=True).stdout.split()
by_base = {}
for s in srcs:
    if s.endswith('.c'):
        by_base.setdefault(os.path.basename(s)[:-2], []).append(s)
@functools.lru_cache(None)
def needs(path):
    try:
        return bool(NEEDS.search(open(path, errors='ignore').read()))
    except OSError:
        return False
targets = sorted(set(re.findall(r'^([a-zA-Z0-9_.-]+):', open('makefile').read(), re.M)))
objbase = sys.argv[1] if len(sys.argv) > 1 else 'build_torimem'
missing = 0
for t in targets:
    if t.startswith('.'):
        continue
    r = subprocess.run(['make', '-n', t, 'PLATFORM_OBJ_BASE=' + objbase], capture_output=True, text=True)
    if r.returncode != 0:
        continue
    text = re.sub(r'\\\n', ' ', r.stdout)
    for cmd in text.split('\n'):
        if ' -o ' not in cmd or ' -c ' in cmd or 'torirs_mem' in cmd:
            continue
        hit = None
        for tok in cmd.split():
            if tok.endswith('.c'):
                p = tok
                for pre in (os.getcwd() + '/', 'src/'):
                    if p.startswith(pre):
                        p = p[len(pre):]
                if needs(p):
                    hit = tok; break
            elif tok.endswith('.o'):
                for p in by_base.get(os.path.basename(tok)[:-2], []):
                    if needs(p):
                        hit = tok; break
                if hit:
                    break
        if hit:
            print('check-torimem-links: %s links %s without $(TORIMEM_SRCS)' % (t, hit))
            missing += 1
            break
print('check-torimem-links: %d rule(s) missing the core' % missing)
sys.exit(1 if missing else 0)
