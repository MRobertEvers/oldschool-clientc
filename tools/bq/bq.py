"""Client for the XP benchmark queue. This is what an agent calls.

An agent enqueues a job and blocks on its result. It never needs to know that
any other agent exists, and it never launches a benchmark itself -- the drainer
is the only thing that touches the box for a timed run, which is what makes
"one benchmark at a time" a property of the system.

Batching is the point. A 4-arm palindrome at 900 frames is ~4.5 minutes of
exclusive box time, so twelve 2-arm jobs cost ~54 minutes while one 12-arm job
costs ~13. Build variants in parallel (cheap, local CPU); benchmark them in one
job.

    # one job, three arms, blocking
    python tools/bq/bq.py submitwait --label gouraud-v1 \
        --arm base=/path/torirs-base.exe \
        --arm v1=/path/torirs-v1.exe \
        --arm v2=/path/torirs-v2.exe \
        --frames 900 --reps 2

    python tools/bq/bq.py submit ... --json      # prints job_id, returns now
    python tools/bq/bq.py wait <job_id>
    python tools/bq/bq.py ledger                 # what has already been measured

An arm may carry `--arm name=exe:KEY=VAL,KEY2=VAL2` to set extra env for that
arm only (used for runtime switches; a compile-time variant is a separate exe).
"""

import argparse
import json
import os
import sys
import time
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bqlib as L  # noqa: E402


def parse_arm(spec):
    assert '=' in spec, 'arm spec must be name=/path/to.exe[:K=V,K=V]'
    name, rest = spec.split('=', 1)
    env = {}
    # split off a trailing :K=V,K=V that is not part of a drive letter
    if ':' in rest[2:]:
        head, tail = rest[:2], rest[2:]
        path_tail, envtxt = tail.split(':', 1)
        rest = head + path_tail
        for kv in envtxt.split(','):
            if kv:
                k, v = kv.split('=', 1)
                env[k] = v
    return {'name': name, 'exe': os.path.abspath(rest), 'env': env}


def submit(args):
    L.ensure_dirs()
    arms = [parse_arm(s) for s in args.arm]
    assert arms, 'a job needs at least one arm'
    names = [a['name'] for a in arms]
    assert len(set(names)) == len(names), 'arm names must be unique'
    for a in arms:
        assert os.path.isfile(a['exe']), 'no such exe: %s' % a['exe']
        a['sha256'] = L.sha256_file(a['exe'])
        a['build_cmd'] = args.build_cmd or ''
        a['source'] = args.source or ''

    if args.build_cmd_file:
        with open(args.build_cmd_file, 'r', encoding='utf-8') as f:
            cmds = json.load(f)
        for a in arms:
            if a['name'] in cmds:
                a['build_cmd'] = cmds[a['name']]

    job_id = time.strftime('%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6]
    job = {
        'job_id': job_id,
        'label': args.label,
        'arms': arms,
        'frames': args.frames,
        'reps': args.reps,
        'palindrome': not args.no_palindrome,
        'submitted_by': args.by or os.environ.get('BQ_AGENT', 'unknown'),
        'submitted_at': time.time(),
        'submitted_at_str': time.strftime('%Y-%m-%d %H:%M:%S'),
    }
    L.write_json(L.pending_path(job_id), job)
    L.append_runlog({'ts': time.time(), 'event': 'submit', 'job_id': job_id,
                     'label': args.label, 'by': job['submitted_by'],
                     'arms': {a['name']: a['sha256'][:16] for a in arms},
                     'frames': args.frames, 'reps': args.reps})
    return job_id


def wait(job_id, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        r = L.read_json(L.result_path(job_id))
        if r:
            return r
        time.sleep(3)
    return None


def render(r):
    out = []
    out.append('job %s  [%s]  status=%s  attempts=%s'
               % (r['job_id'], r.get('label', ''), r['status'],
                  r.get('attempts')))
    if r.get('error'):
        out.append('error: %s' % r['error'])
    out.append('frames=%s reps=%s order=%s'
               % (r.get('frames'), r.get('reps'), ','.join(r.get('order', []))))
    if r.get('box_start'):
        out.append('box window: %.3f -> %.3f  (%.1f s exclusive)'
                   % (r['box_start'], r.get('box_end', 0),
                      (r.get('box_end', 0) - r['box_start'])))
    out.append('')
    out.append('%-14s %-18s %9s %9s %9s  %s'
               % ('arm', 'sha256[:16]', 'best_ms', 'med_ms', 'worst_ms', 'runs'))
    out.append('-' * 96)
    for name in r.get('order', []) or sorted(r.get('arms', {})):
        if name not in r.get('arms', {}):
            continue
        a = r['arms'][name]
        if 'best_ms' not in a:
            continue
        if any(name == line.split()[0] for line in out[6:]):
            continue
        out.append('%-14s %-18s %9.3f %9.3f %9.3f  %s'
                   % (name, (a.get('sha256') or '?')[:16], a['best_ms'],
                      a['median_ms'], a['worst_ms'],
                      ' '.join('%.2f' % v for v in a['runs_ms'])))
    out.append('')
    for name in sorted(r.get('arms', {})):
        a = r['arms'][name]
        out.append('%-14s asm=%s  remote=%s  upload=%s'
                   % (name, a.get('asm_syms'), a.get('remote'), a.get('upload')))
        if a.get('build_cmd'):
            out.append('%-14s build: %s' % ('', a['build_cmd']))
        if a.get('env'):
            out.append('%-14s env: %s' % ('', a['env']))

    # The box is bimodal by ~7.5%; anything closer than that is not a result.
    vals = [(n, r['arms'][n]['best_ms']) for n in r.get('arms', {})
            if 'best_ms' in r['arms'][n]]
    if len(vals) > 1:
        vals.sort(key=lambda t: t[1])
        base = vals[0][1]
        out.append('')
        out.append('deltas vs fastest arm (%s, %.3f ms):' % (vals[0][0], base))
        for n, v in vals[1:]:
            pct = 100.0 * (v - base) / base
            flag = '' if abs(pct) >= 8.0 else '   <-- inside the box bimodality; not a result yet'
            out.append('  %-14s %+8.3f ms  %+6.2f%%%s' % (n, v - base, pct, flag))
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)

    def add_submit_args(p):
        p.add_argument('--label', required=True)
        p.add_argument('--arm', action='append', required=True,
                       help='name=/path/to.exe[:ENV=VAL,ENV2=VAL2]')
        p.add_argument('--frames', type=int, default=900)
        p.add_argument('--reps', type=int, default=2)
        p.add_argument('--no-palindrome', action='store_true')
        p.add_argument('--by', default=None)
        p.add_argument('--source', default=None,
                       help='git rev / worktree the arms were built from')
        p.add_argument('--build-cmd', default=None)
        p.add_argument('--build-cmd-file', default=None,
                       help='JSON {armname: build command}')

    p = sub.add_parser('submit')
    add_submit_args(p)
    p = sub.add_parser('submitwait')
    add_submit_args(p)
    p.add_argument('--timeout', type=float, default=7200)

    p = sub.add_parser('wait')
    p.add_argument('job_id')
    p.add_argument('--timeout', type=float, default=7200)

    p = sub.add_parser('show')
    p.add_argument('job_id')

    sub.add_parser('ledger')

    # Pulling a file back off the box is not a timed operation, so it does not
    # queue and does not wait for the drainer. Any agent may do it at any time,
    # including while someone else's benchmark is running.
    p = sub.add_parser('fetch')
    p.add_argument('remote', help=r'path on the box, e.g. C:\dev\...\eip.txt')
    p.add_argument('local')

    p = sub.add_parser('logs')
    p.add_argument('job_id')
    p.add_argument('--outdir', default=None,
                   help='write each run log here instead of printing names')

    a = ap.parse_args()

    if a.cmd == 'submit':
        print(submit(a))
        return 0
    if a.cmd == 'submitwait':
        job_id = submit(a)
        sys.stderr.write('submitted %s; waiting\n' % job_id)
        r = wait(job_id, a.timeout)
        if r is None:
            print('TIMEOUT waiting for %s' % job_id)
            return 2
        print(render(r))
        return 0 if r['status'] == 'ok' else 1
    if a.cmd == 'wait':
        r = wait(a.job_id, a.timeout)
        if r is None:
            print('TIMEOUT waiting for %s' % a.job_id)
            return 2
        print(render(r))
        return 0 if r['status'] == 'ok' else 1
    if a.cmd == 'show':
        r = L.read_json(L.result_path(a.job_id))
        if not r:
            print('no result for %s' % a.job_id)
            return 2
        print(render(r))
        return 0
    if a.cmd == 'fetch':
        raw = L.box_get(a.remote)
        if raw is None:
            print('not on the box: %s' % a.remote)
            return 2
        with open(a.local, 'wb') as f:
            f.write(raw)
        print('%s -> %s (%d B)' % (a.remote, a.local, len(raw)))
        return 0
    if a.cmd == 'logs':
        r = L.read_json(L.result_path(a.job_id))
        if not r:
            print('no result for %s' % a.job_id)
            return 2
        for run in r.get('runs', []) or []:
            # A profile run leaves an EIP dump beside its log; pull both, or
            # the profile is on the box and the analysis is here.
            names = [run['log']] + ([run['eip']] if run.get('eip') else [])
            for nm in names:
                remote = L.BOX_BQ + '\\' + nm
                if not a.outdir:
                    print('%-10s rep%d slot%d  %s' % (run['arm'], run['rep'],
                                                      run['slot'], remote))
                    continue
                raw = L.box_get(remote)
                if raw is None:
                    print('MISSING %s' % remote)
                    continue
                dest = os.path.join(a.outdir, nm)
                with open(dest, 'wb') as f:
                    f.write(raw)
                print('%s (%d B)' % (dest, len(raw)))
        return 0
    if a.cmd == 'ledger':
        L.ensure_dirs()
        rows = []
        for fn in sorted(os.listdir(L.DIR_DONE)):
            r = L.read_json(os.path.join(L.DIR_DONE, fn))
            if not r:
                continue
            for name in sorted(r.get('arms', {})):
                arm = r['arms'][name]
                if 'best_ms' not in arm:
                    continue
                rows.append((r['job_id'], r.get('label', ''), name,
                             (arm.get('sha256') or '?')[:12], arm['best_ms'],
                             r['status']))
        print('%-20s %-26s %-12s %-14s %9s %s'
              % ('job', 'label', 'arm', 'sha256[:12]', 'best_ms', 'status'))
        for row in rows:
            print('%-20s %-26s %-12s %-14s %9.3f %s' % row)
        return 0
    return 1


if __name__ == '__main__':
    sys.exit(main())
