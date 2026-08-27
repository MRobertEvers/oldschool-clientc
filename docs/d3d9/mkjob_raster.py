"""Emit the raster A/B jobs: baseline vs presorted, on the rev-289 world.

    python docs/d3d9/mkjob_raster.py fps  <job_id> <out.json> [exe]
    python docs/d3d9/mkjob_raster.py eip  <job_id> <out.json> [exe]

Both arms are ONE binary. TORIDRAW_RASTER_BATCH=0 puts the old pipeline back --
no pre-sort store in the depth sort, and the per-face kernels running their own
six-way y-sort ladder -- so the two arms differ by an environment variable and
nothing else. No second build, and no code-layout difference to argue about.

WINDOW SIZE. Nothing here states one, and that is deliberate. The interface is
UITREE_LAYOUT_ROOT_W x _H = 765x503, and the window is created from the layout
root, so saying nothing gets the right size. Stating `--windowmode resizable
--window 1024x768` does NOT letterbox that canvas -- UITree_LayoutSetRootSize
takes the stated size when the mode is not fixed, so the client renders a
canvas 2.7x the area the interface occupies and clears and blits the difference
every frame. Earlier jobs in this line of work did exactly that, with
TORIRS_WIN32_FULLSCREEN=1 on top; their A/B deltas survive, because both arms
were wrong by the same amount, but their absolute per-frame costs do not.

ARM ORDER. Palindromic, and an even number of each kind. The box drifts over a
quarter of an hour; a palindrome means that drift lands on both arms equally
instead of on whichever ran last.

FRAME-BOUNDED, NOT TIME-BOUNDED, for the EIP job. The dump is written on the
client's own quit path, so a run killed by a timer produces no dump at all --
and a run that produced no dump looks exactly like one that produced an empty
one. TORIRS_MAX_FRAMES makes the client exit by itself, and it also makes the
two arms rasterise the SAME NUMBER OF FRAMES, which is what lets a per-frame
cost be compared between them at all.
"""
import json
import sys

# Manifest and nothing about the window. See the module docstring.
BASE = ['--manifest', 'build/manifests/rs289lc-xp.ini', '--uncapped']
OUT_DIR = r'C:\dev\oldschool-clientc\d3d9prof'


def _run(tag, exe, user, env, seconds):
    return {
        'tag': tag,
        'exe': exe,
        'run_seconds': seconds,
        'boot_cap_seconds': 130,
        'args': BASE[:2] + ['--user', user, '--pass', 'zuk'] + BASE[2:]
                + ['--soft3d'],
        'env': env,
    }


def fps_job(job_id, exe, seconds=40):
    """Eight arms, four of each, for the frame-time comparison."""
    order = ['off', 'on', 'on', 'off', 'off', 'on', 'on', 'off']
    runs = []
    for i, name in enumerate(order):
        env = {} if name == 'on' else {'TORIDRAW_RASTER_BATCH': '0'}
        runs.append(_run('K%d-%s' % (i, name), exe, 'rb%d' % i, env, seconds))
    return {'job_id': job_id, 'settle_seconds': 15, 'settle_inworld': 16,
            'ready_mb': 45, 'runs': runs}


def eip_job(job_id, exe, warmup=400, measure=1600):
    """Four arms, two of each, sampling EIP after the warmup frames."""
    order = ['off', 'on', 'on', 'off']
    runs = []
    for i, name in enumerate(order):
        tag = 'R%d-%s' % (i, name)
        env = {
            'TORIDRAW_EIP_SAMPLE': '1',
            'TORIDRAW_EIP_SAMPLE_WARMUP': str(warmup),
            'TORIDRAW_EIP_SAMPLE_FILE': OUT_DIR + ('\\%s.eip.txt' % tag),
            'TORIRS_MAX_FRAMES': str(warmup + measure),
        }
        if name == 'off':
            env['TORIDRAW_RASTER_BATCH'] = '0'
        runs.append(_run(tag, exe, 'ep%d' % i, env, 400))
    return {'job_id': job_id, 'settle_seconds': 15, 'settle_inworld': 16,
            'ready_mb': 45, 'runs': runs}


if __name__ == '__main__':
    kind, job_id, out = sys.argv[1], sys.argv[2], sys.argv[3]
    exe = sys.argv[4] if len(sys.argv) > 4 else 'torirs-raster.exe'
    if kind == 'fps':
        job = fps_job(job_id, exe)
    elif kind == 'eip':
        job = eip_job(job_id, exe)
    else:
        raise SystemExit('kind must be fps|eip')
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(job, f, indent=1)
    print(out, [r['tag'] for r in job['runs']])
