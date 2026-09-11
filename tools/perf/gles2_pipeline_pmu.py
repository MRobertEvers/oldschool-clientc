#!/usr/bin/env python3
"""Same-launch ABBA renderer hardware counters; restores device launch settings."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys
import tempfile
import time
from model_chain import Adb, DATA, PKG, REPO


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default='T062809L3Z')
    parser.add_argument('--scene', default='grand-exchange')
    parser.add_argument('--event', choices=('cpu-cycles', 'instructions', 'branch-misses', 'L1-dcache-load-misses'), default='cpu-cycles')
    parser.add_argument('--target', choices=('direct', 'acquire', 'both', 'feed', 'all', 'compact', 'placement', 'pose', 'bake', 'complete'), default='both')
    parser.add_argument('--frames', type=int, default=90)
    parser.add_argument('--warmup', type=int, default=300)
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--manifest', help='device live manifest')
    parser.add_argument('--args-file', type=Path, help='private local client arguments')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.frames < 1 or args.warmup < 1:
        parser.error('frames and warmup must be positive')
    sys.path.insert(0, str(REPO / 'tools'))
    from launcher.profiles import Manifest
    from launcher.bench import load_suite
    suite = load_suite(Manifest.load(str(REPO / 'manifests/manifest_osrs239_bench.ini')))
    scene = None if args.manifest else next(scene for scene in suite.scenes if scene.name == args.scene)
    adb = Adb(args.serial)
    rows, originals = [], {}
    result = dict(serial=args.serial, scene=args.scene, event=args.event, target=args.target,
                  warmup=args.warmup, frames=args.frames, live=bool(args.manifest), manifest=args.manifest, rows=rows)
    if args.manifest: result['scene']=None
    apk = REPO / 'android/build/outputs/apk/debug/ToriRS-debug.apk'
    if apk.exists():
        result['local_apk_sha256'] = hashlib.sha256(apk.read_bytes()).hexdigest()
    result['scope'] = 'Draw RenderFrame plus worker model pass; per-thread userspace hardware counters; ABBA x3 with six settling frames; both arms aligned.'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pipeline-pmu-') as directory:
        tmp = Path(directory)
        for name in ('env.txt', 'extra_args.txt'):
            old = adb.shell('cat', f'{DATA}/{name}', check=False)
            originals[name] = old.stdout if old.returncode == 0 else None
        try:
            adb.shell('am', 'force-stop', PKG)
            settings = dict(TORIRS_PLUGINS='0', TORIRS_PERF='0', TORIRS_GLES2_DUALCORE='1',
                            TORIRS_GLES2_DUALCORE_DEBUG='0', TORIRS_PIPELINE_PMU=args.event,
                            TORIRS_PIPELINE_AB=args.target, TORIRS_PIPELINE_WARMUP=str(args.warmup),
                            TORIRS_PIPELINE_FRAMES=str(args.frames))
            if scene:
                settings['TORIRS_WORLD_MAP']=scene.world_map_env()
                settings['TORIRS_WEDGE_CAM']=scene.wedge_cam_env()
            # Static camera for identical work in adjacent arms. No camera path.
            adb.put_text(f'{DATA}/env.txt', ''.join(f'{k}={v}\n' for k, v in settings.items()), tmp)
            adb.put_text(f'{DATA}/extra_args.txt', args.args_file.read_text() if args.args_file else '--gles2-dualcore\n--offline\n', tmp)
            adb.shell('run-as', PKG, 'am', 'start', '--user', '0', '-n', f'{PKG}/.ClientActivity',
                      '--es', f'{PKG}.MANIFEST', args.manifest or f'{DATA}/manifests/manifest_osrs239_bench.ini')
            deadline, pid = time.monotonic()+args.timeout, None
            while len(rows) < 12:
                if time.monotonic() > deadline:
                    raise RuntimeError('hardware counter windows incomplete; no fallback')
                if pid is None:
                    for line in adb.shell('ps').stdout.splitlines():
                        fields = line.split()
                        if fields and fields[-1] == PKG:
                            pid = fields[1]
                for line in adb.call('logcat', '-d', '-s', 'torirs').stdout.splitlines():
                    if not pid or not re.search(r'\(\s*'+pid+r'\)', line):
                        continue
                    if 'pipeline-pmu:' in line:
                        raise RuntimeError(line)
                    match = re.search(r'pipeline,([^,]+),([^,]+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)', line)
                    if not match or any(row['sample'] == int(match[3]) for row in rows):
                        continue
                    if match[1] != args.event or match[2] != args.target or int(match[5]) != args.frames:
                        raise RuntimeError('unexpected measurement window')
                    row = dict(zip(('sample','arm','frames','draw','worker','models','worker_models','faces'), map(int, match.groups()[2:])))
                    rows.append(row)
                    print(json.dumps(row), flush=True)
                args.out.write_text(json.dumps(result, indent=2)+'\n')
                time.sleep(1)
            summary = {}
            for key in ('draw', 'worker', 'total'):
                values = {arm: [(r['draw']+r['worker'] if key == 'total' else r[key])/r['frames'] for r in rows if r['arm'] == arm] for arm in (0,1)}
                medians = {arm: statistics.median(values[arm]) for arm in (0,1)}
                summary[key] = dict(baseline=medians[0], candidate=medians[1], percent=100*(medians[1]/medians[0]-1),
                                    mad={arm: statistics.median(abs(v-medians[arm]) for v in values[arm]) for arm in (0,1)})
            result['summary'] = summary
            args.out.write_text(json.dumps(result, indent=2)+'\n')
            print(json.dumps(summary, indent=2), flush=True)
            remote_image = '/sdcard/pipeline-pmu.png'
            adb.shell('screencap', '-p', remote_image)
            adb.call('pull', remote_image, str(args.out.with_suffix('.png')))
            adb.shell('rm', '-f', remote_image)
        finally:
            adb.shell('am', 'force-stop', PKG, check=False)
            for name, content in originals.items():
                if content is None:
                    adb.shell('rm', '-f', f'{DATA}/{name}', check=False)
                else:
                    adb.put_text(f'{DATA}/{name}', content, tmp)

if __name__ == '__main__':
    main()
