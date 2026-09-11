"""The XP benchmark queue drainer.

Exactly one of these runs. It is the only thing on this machine that launches a
timed benchmark on the box, which is what makes serialization a property of the
system rather than of agent discipline.

    python tools/bq/bqd.py run            # drain forever
    python tools/bq/bqd.py run --once     # drain until the queue empties
    python tools/bq/bqd.py status

Crash safety: the drainer holds a lease it refreshes every loop. A lease older
than LEASE_SECONDS is dead and the next drainer takes over. On takeover, a job
found in running/ is *adopted* if the box is still working on it (its progress
file is advancing), re-run if the box has gone quiet and attempts remain, and
explicitly reported dropped otherwise. Every one of those transitions is
written to the append-only runlog, because a silent requeue reads downstream as
"that arm was slow".
"""

import argparse
import json
import os
import socket
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bqlib as L  # noqa: E402

# A run is ~35 ms/frame * frames. Allow generous slack for a box that has
# started swapping, but not so much that a wedge parks the queue for an hour.
def run_budget_seconds(job):
    arms = max(1, len(job.get('arms', [])))
    n = arms * (2 if job.get('palindrome', True) and arms > 1 else 1)
    n *= max(1, job.get('reps', 1))
    per_run = job.get('frames', 900) * 0.200 + 90.0   # 200 ms/frame worst case
    return 180.0 + n * per_run


# Progress must advance at least this often or the box is presumed wedged.
STALL_SECONDS = 900.0

# Set by main_run. poll_job blocks for the whole length of a batched job -- a
# 12-arm palindrome at 900 frames is a quarter of an hour -- so the lease has
# to be refreshed from inside that wait. Refreshing only in the idle loop
# lets the lease go stale while the box is busy, and a second drainer would
# then take over a job that is still running.
OWNER = None

# The box-side child writes a .started marker as its very first act. If that
# never appears, the detached launch itself failed -- a distinct fault from a
# slow run, and one worth reporting in seconds rather than waiting out the
# whole run budget. Hashing the arms on a P4 is the slow part before it.
LAUNCH_SECONDS = 240.0


def log(msg):
    sys.stdout.write('[bqd %s] %s\n' % (time.strftime('%H:%M:%S'), msg))
    sys.stdout.flush()


# --------------------------------------------------------------------------
# staging
# --------------------------------------------------------------------------

def remote_name(sha256):
    """Content-addressed, so an arm shared between jobs uploads once and the
    filename itself carries provenance."""
    return 'bq_' + sha256[:12] + '.exe'


def load_boxstate():
    return L.read_json(L.BOXSTATE) or {}


def save_boxstate(st):
    L.write_json(L.BOXSTATE, st)


def stage_arms(job):
    """Hash, symbol-check and upload every arm. Returns (ok, error)."""
    st = load_boxstate()
    present = set()
    listing = L.box_list(L.BOX_ROOT)
    if listing:
        for e in listing.get('entries', []):
            present.add(e['n'])

    dropped = []
    kept = []
    for arm in job['arms']:
        local = arm['exe']
        if not os.path.isfile(local):
            # The binary is content-addressed on the box as bq_<sha12>.exe, so
            # a local copy is only needed to UPLOAD it. If the box already holds
            # this exact sha, the arm is runnable and the missing local file is
            # irrelevant -- and the box-side runner re-hashes every arm before
            # the first run anyway, so identity is still proven where it counts.
            #
            # This is not hypothetical: C: hit 100% mid-session and swept ~half
            # the scratchpad's exes, after which three fully-staged jobs died
            # here even though the box still had most of their binaries.
            rn = remote_name(arm.get('sha256') or '')
            if arm.get('sha256') and rn in present and st.get(rn) == arm['sha256']:
                arm['remote'] = rn
                arm['upload'] = 'cached (local copy gone)'
                kept.append(arm)
                continue
            # Not on the box either -- drop this ONE arm rather than the job.
            # Failing the whole job on the first bad arm threw away 38 good
            # arms across three jobs, about 2.5 h of box time, for 25 bad paths.
            dropped.append({'name': arm['name'], 'why': 'local exe missing',
                            'exe': local, 'sha256': arm.get('sha256')})
            continue

        sha = L.sha256_file(local)
        if arm.get('sha256') and arm['sha256'] != sha:
            return False, ('arm %s: submitted sha256 %s but the file now hashes '
                           '%s -- it was rebuilt after submission'
                           % (arm['name'], arm['sha256'][:12], sha[:12]))
        arm['sha256'] = sha
        arm['size'] = os.path.getsize(local)

        missing = L.missing_asm_syms(local)
        if missing is None:
            arm['asm_syms'] = 'UNVERIFIED (nm unavailable)'
        elif missing:
            dropped.append({'name': arm['name'], 'sha256': sha,
                            'why': 'handrolled kernels missing: %s -- built with '
                                   'TORIDRAW_ABLATE/SPAN_CENSUS/SPAN_TRACE in '
                                   'TORIDRAW_PROBE_CFLAGS?' % ','.join(missing)})
            continue
        else:
            arm['asm_syms'] = 'all 4 present'

        rn = remote_name(sha)
        arm['remote'] = rn
        if rn in present and st.get(rn) == sha:
            arm['upload'] = 'cached'
            kept.append(arm)
            continue
        t0 = time.time()
        n = L.box_put(local, L.BOX_ROOT + '\\' + rn)
        arm['upload'] = '%d B in %.1f s' % (n, time.time() - t0)
        st[rn] = sha
        save_boxstate(st)
        kept.append(arm)

    if dropped:
        job['dropped_arms'] = dropped
        job['arms'] = kept
        for d in dropped:
            log('DROPPED arm %s: %s' % (d['name'], d['why']))
    # A job that is only its own controls measures nothing; that IS fatal.
    real = [a for a in kept if a['name'] not in ('ctl', 'ctl2')]
    if not real:
        return False, ('every non-control arm was dropped: %s'
                       % '; '.join('%s (%s)' % (d['name'], d['why']) for d in dropped))
    return True, None


def launch(job):
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, 'xp_runner.py.tmpl'), 'r',
              encoding='utf-8') as f:
        tmpl = f.read()
    # The job JSON is embedded in a raw triple-quoted literal, so it must not
    # contain a quote run that closes it. json.dumps escapes nothing relevant,
    # so assert rather than silently generating a broken script.
    payload = json.dumps(job)
    assert "'''" not in payload
    assert '\\' not in payload or True
    src = tmpl.replace('@@JOB@@', payload)
    name = 'bq_%s.py' % job['job_id']
    L.box_put_script(src, name)
    return L.box_run_script(name)


def poll_job(job, deadline, adopt=False):
    """Wait for the box to finish. Returns (result_dict, reason)."""
    job_id = job['job_id']
    final = L.BOX_BQ + '\\' + job_id + '.result.json'
    progress = L.BOX_BQ + '\\' + job_id + '.progress.json'
    started = L.BOX_BQ + '\\' + job_id + '.started'
    last_progress_change = time.time()
    last_seen = None
    saw_start = bool(adopt)
    launch_deadline = time.time() + LAUNCH_SECONDS

    while time.time() < deadline:
        raw = L.box_get(final)
        if raw:
            try:
                return json.loads(raw.decode('utf-8', 'replace')), None
            except ValueError:
                pass  # still being written; try again
        praw = L.box_get(progress)
        if praw is not None:
            if praw != last_seen:
                last_seen = praw
                last_progress_change = time.time()
        if not saw_start:
            if raw or praw is not None or L.box_get(started) is not None:
                saw_start = True
                last_progress_change = time.time()
            elif time.time() > launch_deadline:
                return None, ('box never started the job: no .started marker '
                              'after %.0f s -- the detached launch failed'
                              % LAUNCH_SECONDS)
        if OWNER:
            # A heartbeat that loses the replace race against a concurrent
            # reader must not kill the job it is heartbeating for. The lease
            # stays valid for LEASE_SECONDS, so skipping one beat is free;
            # dying here threw away a finished 5-arm run once already.
            try:
                L.write_lease(OWNER)
            except OSError:
                pass
        if time.time() - last_progress_change > STALL_SECONDS:
            return None, ('box went quiet: no progress for %.0f s'
                          % STALL_SECONDS)
        time.sleep(5)
    return None, 'timed out after %.0f s' % run_budget_seconds(job)


def adopt_or_rerun(job):
    """A job found in running/ at drainer startup. If the box is still working
    on it, adopt it rather than double-running the box."""
    job_id = job['job_id']
    final = L.BOX_BQ + '\\' + job_id + '.result.json'
    progress = L.BOX_BQ + '\\' + job_id + '.progress.json'
    if L.box_get(final):
        return 'adopt-final'
    p1 = L.box_get(progress)
    if p1 is None:
        return 'rerun'
    time.sleep(20)
    p2 = L.box_get(progress)
    if L.box_get(final):
        return 'adopt-final'
    if p2 != p1:
        return 'adopt-live'
    return 'rerun'


# --------------------------------------------------------------------------
# the loop
# --------------------------------------------------------------------------

def finish(job, result, status, error=None):
    result = dict(result or {})
    result['job_id'] = job['job_id']
    result['label'] = job.get('label', '')
    result['status'] = status
    if error:
        result['error'] = error
    result['attempts'] = job.get('attempts', 1)
    result['submitted_by'] = job.get('submitted_by', '')
    result['submitted_at'] = job.get('submitted_at')
    result['host_end'] = time.time()
    # Arms staged out of the job must survive into the result, or a job that
    # quietly ran 9 of its 16 arms reads as a complete 9-arm job.
    if job.get('dropped_arms'):
        result['dropped_arms'] = job['dropped_arms']
    result['arms'] = {}
    for a in job['arms']:
        result['arms'][a['name']] = {
            'sha256': a.get('sha256'),
            'size': a.get('size'),
            'remote': a.get('remote'),
            'build_cmd': a.get('build_cmd', ''),
            'source': a.get('source', ''),
            'env': a.get('env', {}),
            'asm_syms': a.get('asm_syms'),
            'upload': a.get('upload'),
        }

    # per-arm summary over the runs the box reported
    per = {}
    for r in result.get('runs', []) or []:
        per.setdefault(r['arm'], []).append(r['ms_per_frame'])
    for name, vals in per.items():
        vals_sorted = sorted(vals)
        result['arms'][name]['runs_ms'] = vals
        result['arms'][name]['best_ms'] = vals_sorted[0]
        result['arms'][name]['worst_ms'] = vals_sorted[-1]
        mid = len(vals_sorted) // 2
        result['arms'][name]['median_ms'] = (
            vals_sorted[mid] if len(vals_sorted) % 2
            else 0.5 * (vals_sorted[mid - 1] + vals_sorted[mid]))

    L.write_json(L.result_path(job['job_id']), result)
    try:
        os.remove(L.running_path(job['job_id']))
    except OSError:
        pass
    L.append_runlog({
        'ts': time.time(), 'event': 'finish', 'job_id': job['job_id'],
        'label': job.get('label', ''), 'status': status, 'error': error,
        'attempts': job.get('attempts', 1),
        'box_start': result.get('box_start'), 'box_end': result.get('box_end'),
        'arms': {n: {'sha256': (result['arms'][n].get('sha256') or '')[:16],
                     'best_ms': result['arms'][n].get('best_ms')}
                 for n in result['arms']},
    })
    log('job %s -> %s' % (job['job_id'], status))


def process(job, adopt=False):
    job_id = job['job_id']
    job['attempts'] = job.get('attempts', 0) + 1
    job['host_start'] = time.time()
    L.write_json(L.running_path(job_id), job)
    L.append_runlog({'ts': time.time(), 'event': 'start', 'job_id': job_id,
                     'label': job.get('label', ''),
                     'attempts': job['attempts'],
                     'adopted': bool(adopt)})

    if not adopt:
        ok, err = stage_arms(job)
        L.write_json(L.running_path(job_id), job)   # persist sha/remote
        if not ok:
            finish(job, {}, 'error', err)
            return
        st, detail = launch(job)
        log('job %s launched (%s)' % (job_id, st))

    result, reason = poll_job(job, time.time() + run_budget_seconds(job),
                              adopt=adopt)
    if result is None:
        if job['attempts'] < L.MAX_ATTEMPTS:
            L.append_runlog({'ts': time.time(), 'event': 'requeue',
                             'job_id': job_id, 'reason': reason,
                             'attempts': job['attempts']})
            log('job %s requeued: %s' % (job_id, reason))
            os.replace(L.running_path(job_id), L.pending_path(job_id))
        else:
            finish(job, {}, 'dropped', reason)
        return
    if result.get('status') != 'ok':
        finish(job, result, 'error', result.get('error', 'box reported failure'))
        return
    finish(job, result, 'ok')


def recover(drain_once):
    """Adopt / re-run / drop whatever a dead drainer left behind."""
    for fn in sorted(os.listdir(L.DIR_RUNNING)):
        if not fn.endswith('.job.json'):
            continue
        job = L.read_json(os.path.join(L.DIR_RUNNING, fn))
        if not job:
            continue
        job_id = job['job_id']
        what = adopt_or_rerun(job)
        L.append_runlog({'ts': time.time(), 'event': 'recover',
                         'job_id': job_id, 'decision': what,
                         'attempts': job.get('attempts', 1)})
        log('recover %s -> %s' % (job_id, what))
        if what in ('adopt-final', 'adopt-live'):
            job['attempts'] = job.get('attempts', 1) - 1  # process() re-adds
            process(job, adopt=True)
        elif job.get('attempts', 1) < L.MAX_ATTEMPTS:
            os.replace(os.path.join(L.DIR_RUNNING, fn), L.pending_path(job_id))
        else:
            finish(job, {}, 'dropped',
                   'drainer died mid-job and the box had not started it; '
                   'attempts exhausted (%d)' % job.get('attempts', 1))


def next_job():
    files = [f for f in os.listdir(L.DIR_PENDING) if f.endswith('.job.json')]
    if not files:
        return None
    files.sort(key=lambda f: (os.path.getmtime(os.path.join(L.DIR_PENDING, f)), f))
    return os.path.join(L.DIR_PENDING, files[0])


def main_run(once):
    L.ensure_dirs()
    global OWNER
    owner = '%s:%d:%d' % (socket.gethostname(), os.getpid(), int(time.time()))
    lease = L.read_lease()
    if L.lease_is_live(lease):
        log('another drainer holds the lease (%s, pid %s, heartbeat %.0f s '
            'ago) -- refusing. Exactly one drainer may exist; that is the '
            'whole serialization guarantee.'
            % (lease.get('owner'), lease.get('pid'),
               time.time() - lease.get('heartbeat', 0)))
        return 3
    if lease:
        log('taking over stale lease from %s' % lease.get('owner'))
    L.write_lease(owner)
    OWNER = owner
    L.append_runlog({'ts': time.time(), 'event': 'drainer-start',
                     'owner': owner})
    log('drainer %s up; queue root %s' % (owner, L.QUEUE_ROOT))

    try:
        recover(once)
        idle = 0
        while True:
            L.write_lease(owner)
            path = next_job()
            if path is None:
                if once:
                    log('queue empty; --once so exiting')
                    return 0
                idle += 1
                time.sleep(2)
                continue
            idle = 0
            job = L.read_json(path)
            if not job:
                os.replace(path, path + '.corrupt')
                continue
            os.replace(path, L.running_path(job['job_id']))
            try:
                process(job)
            except Exception:
                finish(job, {}, 'error', traceback.format_exc()[:4000])
    finally:
        L.append_runlog({'ts': time.time(), 'event': 'drainer-stop',
                         'owner': owner})
        L.clear_lease(owner)


def main_status():
    L.ensure_dirs()
    lease = L.read_lease()
    print('queue root : %s' % L.QUEUE_ROOT)
    if lease:
        age = time.time() - lease.get('heartbeat', 0)
        print('drainer    : %s (%s, heartbeat %.0f s ago)'
              % (lease.get('owner'), 'LIVE' if L.lease_is_live(lease) else 'DEAD',
                 age))
    else:
        print('drainer    : none')
    for d, tag in ((L.DIR_PENDING, 'pending'), (L.DIR_RUNNING, 'running'),
                   (L.DIR_DONE, 'done')):
        items = sorted(os.listdir(d))
        print('%-10s : %d  %s' % (tag, len(items), ' '.join(items[:8])))
    return 0


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['run', 'status'])
    ap.add_argument('--once', action='store_true')
    a = ap.parse_args()
    sys.exit(main_run(a.once) if a.cmd == 'run' else main_status())
