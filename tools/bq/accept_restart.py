"""Acceptance test 2: kill the drainer mid-job, restart it, prove the queue
survives -- and that whatever happened to the interrupted job is stated out
loud rather than silently retried.

    python tools/bq/accept_restart.py --arm dense=C:\\path\\to\\torirs-dense.exe

Two scenarios, run back to back:

  early  the drainer is killed while it is still staging, before the box has
         started anything. Nothing was measured, so the job must be re-run.

  live   the drainer is killed while the box is mid-benchmark. Re-running
         would double the box's work and throw away a run that is going to
         finish anyway, so the restarted drainer must *adopt* the job.

Both are failures of the same event -- the drainer dying -- and the queue is
only correct if it tells them apart. This script asserts on the decision the
restarted drainer actually recorded in the runlog.

The drainer is killed with taskkill, i.e. no cleanup runs and the lease is
left behind exactly as a crash would leave it. The restart then has to take
over a stale lease, which is the path that matters.
"""

import argparse
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bqlib as L  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
BQD = os.path.join(HERE, 'bqd.py')
BQ = os.path.join(HERE, 'bq.py')


def say(msg):
    sys.stdout.write(msg + '\n')
    sys.stdout.flush()


def runlog_len():
    if not os.path.exists(L.RUNLOG):
        return 0
    with open(L.RUNLOG, 'r', encoding='utf-8') as f:
        return len(f.readlines())


def runlog_since(n):
    out = []
    with open(L.RUNLOG, 'r', encoding='utf-8') as f:
        for i, line in enumerate(f):
            if i < n or not line.strip():
                continue
            try:
                out.append(json.loads(line))
            except ValueError:
                pass
    return out


def start_drainer():
    """Start a drainer and refuse to proceed until it actually owns the lease.

    A drainer that finds a live lease exits immediately by design. Returning
    that corpse as if it were running turns every later wait in this test into
    a fifteen-minute timeout with no explanation.
    """
    p = subprocess.Popen([sys.executable, BQD, 'run'],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    t0 = time.time()
    while time.time() - t0 < 60:
        if p.poll() is not None:
            out = p.stdout.read().decode('utf-8', 'replace')
            assert False, ('drainer exited at once (rc=%s): %s'
                           % (p.returncode, out))
        lease = L.read_lease()
        if lease and lease.get('pid') == p.pid and L.lease_is_live(lease):
            return p
        time.sleep(1)
    assert False, 'drainer never took the lease'


def kill_drainer(p):
    """No cleanup, no lease release -- exactly what a crash leaves behind."""
    subprocess.call(['taskkill', '/F', '/T', '/PID', str(p.pid)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p.wait()


def wait_for(pred, timeout, what):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(2)
    assert False, 'timed out after %.0f s waiting for %s' % (timeout, what)


def submit(arm, label, frames, reps):
    cmd = [sys.executable, BQ, 'submit', '--label', label, '--by',
           'accept_restart', '--frames', str(frames), '--reps', str(reps),
           '--arm', arm]
    out = subprocess.check_output(cmd).decode('utf-8', 'replace')
    job_id = out.strip().split()[-1]
    say('  submitted %s [%s]' % (job_id, label))
    return job_id


def box_progress(job_id):
    return L.box_get(L.BOX_BQ + '\\' + job_id + '.progress.json')


def result_of(job_id):
    return L.read_json(L.result_path(job_id))


def scenario_early(arm, frames):
    say('')
    say('=== scenario "early": kill the drainer before the box starts ===')
    mark = runlog_len()
    d = start_drainer()
    job_id = submit(arm, 'accept-restart-early', frames, 1)

    # Kill as soon as the job leaves pending/ -- staging (hash, nm, upload)
    # takes seconds and the box has certainly not started a run yet.
    wait_for(lambda: os.path.exists(L.running_path(job_id)), 120,
             'the job to enter running/')
    say('  job is in running/; killing drainer pid %d' % d.pid)
    kill_drainer(d)
    assert box_progress(job_id) is None, \
        'box already started -- widen the race, this is the "live" case'
    say('  confirmed: box has no progress file, nothing was measured')

    say('  restarting drainer...')
    d2 = start_drainer()
    res = wait_for(lambda: result_of(job_id), 900, 'the job to complete')
    kill_drainer(d2)

    events = runlog_since(mark)
    rec = [e for e in events
           if e.get('event') == 'recover' and e.get('job_id') == job_id]
    assert rec, 'no recover event was logged for %s' % job_id
    say('  runlog decision: %s' % rec[0]['decision'])
    assert rec[0]['decision'] == 'rerun', \
        'expected rerun, got %s' % rec[0]['decision']
    assert res['status'] == 'ok', 'job did not complete: %s' % res.get('error')
    say('  PASS: re-run and completed, attempts=%d, %d timed run(s)'
        % (res['attempts'], len(res.get('runs', []))))
    return job_id


def scenario_live(arm, frames):
    say('')
    say('=== scenario "live": kill the drainer while the box is running ===')
    mark = runlog_len()
    d = start_drainer()
    job_id = submit(arm, 'accept-restart-live', frames, 4)

    wait_for(lambda: box_progress(job_id) is not None, 600,
             'the box to start the job')
    # Let a run actually get under way so adoption has something to adopt.
    time.sleep(20)
    say('  box is mid-benchmark; killing drainer pid %d' % d.pid)
    kill_drainer(d)
    # The killed drainer released nothing: its lease file is still sitting
    # there with a heartbeat a second or two old. What makes the restart
    # instant rather than a 90 s wait is that the lease names a pid that no
    # longer exists, so lease_is_live can tell a crash from a rival.
    stale = L.read_lease()
    assert stale, 'the dead drainer left no lease at all'
    assert time.time() - stale['heartbeat'] < L.LEASE_SECONDS, \
        'heartbeat already aged out; this is not the crash path'
    assert not L.lease_is_live(stale), \
        'a lease whose drainer is dead still reads as live -- the restart ' \
        'will refuse and the queue will park for %d s' % L.LEASE_SECONDS
    say('  the dead drainer left its lease behind (heartbeat %.0f s old) but '
        'its pid is gone, so the restart takes over at once'
        % (time.time() - stale['heartbeat']))

    say('  restarting drainer...')
    d2 = start_drainer()
    res = wait_for(lambda: result_of(job_id), 1800, 'the job to complete')
    kill_drainer(d2)

    events = runlog_since(mark)
    rec = [e for e in events
           if e.get('event') == 'recover' and e.get('job_id') == job_id]
    assert rec, 'no recover event was logged for %s' % job_id
    say('  runlog decision: %s' % rec[0]['decision'])
    assert rec[0]['decision'] in ('adopt-live', 'adopt-final'), \
        'expected adoption, got %s' % rec[0]['decision']
    assert res['status'] == 'ok', 'job did not complete: %s' % res.get('error')
    runs = res.get('runs', [])
    say('  PASS: adopted, attempts=%d, %d timed run(s) -- the box was never '
        'asked to repeat work' % (res['attempts'], len(runs)))
    return job_id


def scenario_keeps_moving(arm, frames, prior):
    """A queue that recovers but then stops is no better than one that wedges."""
    say('')
    say('=== the queue keeps moving after both restarts ===')
    d = start_drainer()
    job_id = submit(arm, 'accept-restart-after', frames, 1)
    res = wait_for(lambda: result_of(job_id), 900, 'the follow-up job')
    kill_drainer(d)
    assert res['status'] == 'ok', res.get('error')
    say('  PASS: a job submitted after the restarts ran normally (%.3f ms)'
        % list(res['arms'].values())[0]['best_ms'])
    return job_id


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--arm', required=True, help='name=/abs/path/to.exe')
    ap.add_argument('--frames', type=int, default=300,
                    help='short by design: this test is about control flow, '
                         'not about the number')
    a = ap.parse_args()

    L.ensure_dirs()
    assert not L.lease_is_live(L.read_lease()), (
        'another drainer holds the lease; stop it first -- this test starts '
        'and kills its own')

    ids = []
    ids.append(scenario_early(a.arm, a.frames))
    ids.append(scenario_live(a.arm, a.frames))
    ids.append(scenario_keeps_moving(a.arm, a.frames, ids))

    say('')
    say('=== recovery events recorded ===')
    subprocess.call([sys.executable, os.path.join(HERE, 'bqaccept.py'),
                     'recovery'])
    say('')
    say('ALL RESTART SCENARIOS PASSED')
