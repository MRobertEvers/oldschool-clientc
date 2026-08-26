"""Host-side driver for the d3d9 / d3d9-zbuffer / soft3d profile on the XP box.

    python drive.py submit  <job.json>
    python drive.py poll    <job_id>
    python drive.py fetch   <job_id> <outdir>

Non-timed traffic (uploads, /fs/get, script puts) is not serialized by
anything, but a TIMED run must have the box to itself -- this driver runs one
job at a time and the box-side runner kills strays before every run.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import box

ROOT = r'C:\dev\oldschool-clientc'
OUT = ROOT + r'\d3d9prof'
TMPL = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    'runner.py.tmpl')


def submit(job):
    text = open(TMPL, encoding='utf-8').read().replace('@@JOB@@',
                                                       json.dumps(job))
    name = 'd3d9prof_' + job['job_id'] + '.py'
    box.putscript(text, name)
    print(box.run(name))
    return job['job_id']


def poll(job_id, timeout=7200):
    t0 = time.time()
    last = None
    while time.time() - t0 < timeout:
        r = box.get(OUT + '\\' + job_id + '.result.json')
        if r:
            return json.loads(r.decode('utf-8', 'replace'))
        p = box.get(OUT + '\\' + job_id + '.progress.json')
        s = p.decode('utf-8', 'replace') if p else 'no progress yet'
        if s != last:
            print('%6.0fs %s' % (time.time() - t0, s))
            last = s
        time.sleep(10)
    raise SystemExit('timed out waiting for ' + job_id)


if __name__ == '__main__':
    verb = sys.argv[1]
    if verb == 'submit':
        job = json.load(open(sys.argv[2], encoding='utf-8'))
        print(submit(job))
    elif verb == 'poll':
        res = poll(sys.argv[2])
        print(json.dumps(res, indent=1)[:4000])
    elif verb == 'fetch':
        job_id, outdir = sys.argv[2], sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        res = json.loads(box.get(OUT + '\\' + job_id +
                                 '.result.json').decode('utf-8', 'replace'))
        with open(os.path.join(outdir, job_id + '.result.json'), 'w',
                  encoding='utf-8') as f:
            json.dump(res, f, indent=1, sort_keys=True)
        for run in res['runs']:
            for key in ('log', 'bmp'):
                n = run.get(key)
                if not n:
                    continue
                b = box.get(OUT + '\\' + n)
                if b is None:
                    print('missing on box: ' + n)
                    continue
                open(os.path.join(outdir, n), 'wb').write(b)
                print('%s (%d bytes)' % (n, len(b)))
            eip = run.get('env', {}).get('TORIDRAW_EIP_SAMPLE_FILE')
            if eip:
                b = box.get(eip)
                if b is None:
                    print('missing on box: ' + eip)
                else:
                    n = os.path.basename(eip)
                    open(os.path.join(outdir, n), 'wb').write(b)
                    print('%s (%d bytes)' % (n, len(b)))
    else:
        raise SystemExit(__doc__)
