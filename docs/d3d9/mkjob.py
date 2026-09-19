"""Emit a runner job.  Arms are ordered as a palindrome so the box's slow
drift over a job cannot be read as a difference between arms."""
import json
import sys

ARM_FLAGS = {
    'soft3d': ['--soft3d'],
    'd3d9': ['--d3d9'],
    'd3d9zb': ['--d3d9-zbuffer'],
}

BASE_ARGS = ['--manifest', 'build/manifests/osrs239-xp-js5.ini',
             '--uncapped',
             '--windowmode', 'resizable', '--window', '1024x768']


def timing_job(job_id, seconds=60):
    order = ['soft3d', 'd3d9', 'd3d9zb', 'd3d9zb', 'd3d9', 'soft3d']
    runs = []
    for i, name in enumerate(order):
        runs.append({
            'tag': 'T%d-%s' % (i, name),
            'exe': 'torirs-d3d9prof.exe',
            'run_seconds': seconds,
            'boot_cap_seconds': 180,
            'args': (BASE_ARGS[:2] + ['--user', 'prof%d' % (10 + i),
                                      '--pass', 'test'] + BASE_ARGS[2:]
                     + ARM_FLAGS[name]),
            'env': {'TORIRS_WIN32_FULLSCREEN': '1'},
        })
    return {'job_id': job_id, 'settle_seconds': 10, 'settle_inworld': 10,
            'ready_mb': 90, 'runs': runs}


def eip_job(job_id, warmup=400, measure=900):
    """Frame-count bounded, so the client exits through its own quit path --
    that is what writes the EIP dump and the exit BMP.  A hard kill writes
    neither, and a run that produced no dump looks exactly like a run that
    produced an empty one."""
    runs = []
    for i, name in enumerate(['soft3d', 'd3d9', 'd3d9zb']):
        tag = 'E%d-%s' % (i, name)
        runs.append({
            'tag': tag,
            'exe': 'torirs-d3d9prof.exe',
            'run_seconds': 900,
            'boot_cap_seconds': 180,
            'args': (BASE_ARGS[:2] + ['--user', 'prfe%d' % (20 + i),
                                      '--pass', 'test'] + BASE_ARGS[2:]
                     + ARM_FLAGS[name]),
            'env': {
                'TORIRS_WIN32_FULLSCREEN': '1',
                'TORIDRAW_EIP_SAMPLE': '1',
                'TORIDRAW_EIP_SAMPLE_WARMUP': str(warmup),
                'TORIDRAW_EIP_SAMPLE_FILE':
                    r'C:\dev\oldschool-clientc\d3d9prof\%s.eip.txt' % tag,
                'TORIRS_MAX_FRAMES': str(warmup + measure),
            },
        })
    return {'job_id': job_id, 'settle_seconds': 10, 'settle_inworld': 10,
            'ready_mb': 90, 'runs': runs}


if __name__ == '__main__':
    kind, job_id, out = sys.argv[1], sys.argv[2], sys.argv[3]
    if kind == 'timing':
        job = timing_job(job_id, int(sys.argv[4]) if len(sys.argv) > 4 else 60)
    elif kind == 'eip':
        job = eip_job(job_id,
                      int(sys.argv[4]) if len(sys.argv) > 4 else 400,
                      int(sys.argv[5]) if len(sys.argv) > 5 else 900)
    else:
        raise SystemExit('kind must be timing|eip')
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(job, f, indent=1)
    print(out, [r['tag'] for r in job['runs']])
