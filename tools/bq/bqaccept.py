"""Acceptance checks for the queue's two structural claims.

    python tools/bq/bqaccept.py overlap [job_id ...]
    python tools/bq/bqaccept.py recovery

`overlap` proves serialization the only way it can be proved: from the box's
own clock. Each result carries box_start/box_end stamped by the box-side
runner, plus per-run start/end. If any two timed runs from different jobs
share a moment, the queue failed, and it says so.

The box wall clock reads 2005 -- it is monotonic within the box, which is all
a non-overlap proof needs, so the numbers are printed as offsets from the
earliest run in the set.

`recovery` reads the append-only runlog and prints every recover/requeue/drop
decision, which is what "log what was dropped" means in practice.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bqlib as L  # noqa: E402


def load_results(job_ids):
    out = []
    if job_ids:
        for j in job_ids:
            r = L.read_json(L.result_path(j))
            assert r, 'no result for job %s' % j
            out.append(r)
    else:
        for fn in sorted(os.listdir(L.DIR_DONE)):
            if fn.endswith('.result.json'):
                out.append(L.read_json(os.path.join(L.DIR_DONE, fn)))
    return [r for r in out if r]


def cmd_overlap(job_ids):
    results = load_results(job_ids)
    runs = []
    for r in results:
        for run in r.get('runs', []) or []:
            runs.append((run['start'], run['end'], r['job_id'],
                         r.get('label', ''), run['arm'], run['rep'],
                         run['ms_per_frame']))
    if not runs:
        print('no timed runs in the selected jobs')
        return 1
    runs.sort()
    t0 = runs[0][0]

    print('%-22s %-24s %-10s %8s %8s %9s' %
          ('job', 'label', 'arm', 'start', 'end', 'ms/frame'))
    print('-' * 88)
    for s, e, jid, lab, arm, rep, ms in runs:
        print('%-22s %-24s %-10s %8.1f %8.1f %9.3f'
              % (jid, lab[:24], arm[:10], s - t0, e - t0, ms))

    bad = []
    for i in range(1, len(runs)):
        prev, cur = runs[i - 1], runs[i]
        if cur[0] < prev[1]:
            bad.append((prev, cur))
    print('')
    if bad:
        print('FAIL: %d overlapping pair(s) -- two benchmarks shared the box'
              % len(bad))
        for prev, cur in bad:
            print('  %s/%s [%.1f,%.1f] overlaps %s/%s [%.1f,%.1f]'
                  % (prev[2], prev[4], prev[0] - t0, prev[1] - t0,
                     cur[2], cur[4], cur[0] - t0, cur[1] - t0))
        return 1

    gaps = [runs[i][0] - runs[i - 1][1] for i in range(1, len(runs))]
    print('PASS: %d timed runs across %d jobs, none overlapping.'
          % (len(runs), len(results)))
    if gaps:
        print('      inter-run gaps (box clock): min %.1f s, max %.1f s'
              % (min(gaps), max(gaps)))
    return 0


def cmd_recovery():
    if not os.path.exists(L.RUNLOG):
        print('no runlog at %s' % L.RUNLOG)
        return 1
    interesting = ('recover', 'requeue', 'drainer-start', 'drainer-stop')
    n = 0
    with open(L.RUNLOG, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                e = json.loads(line)
            except ValueError:
                continue
            ev = e.get('event')
            if ev in interesting:
                n += 1
                print('%-14s %s' % (ev, json.dumps(
                    {k: v for k, v in e.items() if k not in ('ts', 'event')},
                    sort_keys=True)))
            elif ev == 'finish' and e.get('status') != 'ok':
                n += 1
                print('%-14s %s  status=%s  %s'
                      % ('finish', e.get('job_id'), e.get('status'),
                         (e.get('error') or '')[:120]))
    if not n:
        print('runlog has no recovery events yet')
    return 0


if __name__ == '__main__':
    assert len(sys.argv) > 1, 'usage: bqaccept.py overlap|recovery [job_id ...]'
    if sys.argv[1] == 'overlap':
        sys.exit(cmd_overlap(sys.argv[2:]))
    elif sys.argv[1] == 'recovery':
        sys.exit(cmd_recovery())
    else:
        assert False, 'unknown command %s' % sys.argv[1]
