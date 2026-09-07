#!/usr/bin/env python3
"""Whole-frame and EGL swap-cadence A/B; no cycle-to-time conversion."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import statistics
import sys
import tempfile
import time
from model_chain import Adb, DATA, PKG, REPO
from renderer_provenance import source_state, sha256, switch_matrix, device_conditions


def percentile(values, q):
    values = sorted(values)
    index = (len(values) - 1) * q
    lo = int(index)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (index - lo)


def summarize(rows):
    result = {}
    for field in ('work_us', 'cadence_us', 'swap_us'):
        arms = {}
        for arm, label in ((0, 'before'), (1, 'after')):
            values = [row[field] / 1000 for row in rows if row['arm'] == arm]
            arms[label] = dict(frames=len(values), mean_ms=statistics.mean(values),
                              median_ms=statistics.median(values), p95_ms=percentile(values, .95),
                              p99_ms=percentile(values, .99), max_ms=max(values),
                              over_20ms_percent=100 * sum(v > 20 for v in values) / len(values),
                              over_33_33ms_percent=100 * sum(v > 100 / 3 for v in values) / len(values))
        arms['median_change_percent'] = 100 * (arms['after']['median_ms'] / arms['before']['median_ms'] - 1) if arms['before']['median_ms'] else None
        result[field] = arms
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default='T062809L3Z')
    parser.add_argument('--scene', default='grand-exchange-ground')
    parser.add_argument('--manifest')
    parser.add_argument('--args-file', type=Path)
    parser.add_argument('--uncapped', action='store_true')
    parser.add_argument('--target',choices=('complete','sub10-aa','sub10-canvas','sub10-actor','sub10-ui','sub10-ui-aa','sub10-words','sub10-words-aa','sub10'),default='complete')
    parser.add_argument('--env',action='append',default=[],metavar='NAME=VALUE')
    parser.add_argument('--frames', type=int, default=180)
    parser.add_argument('--warmup', type=int, default=600)
    parser.add_argument('--timeout', type=int, default=360)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.frames <= 1024 or args.warmup < 1:
        parser.error('frames must be 1..1024; warmup must be positive')
    if bool(args.manifest) != bool(args.args_file):
        parser.error('--manifest and --args-file must be supplied together')
    # Measurement controls must describe what actually ran. Optional diagnostic
    # flags are recorded, but cannot override the selected timing protocol.
    overrides = {}
    for setting in args.env:
        if '=' not in setting or '\n' in setting or '\r' in setting:
            parser.error('--env requires one NAME=VALUE per argument')
        key, value = setting.split('=', 1)
        if not re.fullmatch(r'[A-Z][A-Z0-9_]*', key) or key.startswith(('TORIRS_PIPELINE_', 'TORIRS_FRAME_TIME', 'TORIRS_GLES2_READBACK')) or key in ('TORIRS_PERF', 'TORIRS_GLES2_DUALCORE', 'TORIRS_PLUGINS'):
            parser.error('cannot override benchmark control: ' + key)
        overrides[key] = value
    library = REPO / 'android/src/main/jniLibs/armeabi-v7a/libtorirs.so'
    build = json.loads(library.with_suffix('.build.json').read_text())
    if build['probe'] != 'frame-times' or build['library_sha256'] != sha256(library):
        raise RuntimeError('build matching frame-times diagnostic before running')
    if build['source'] != source_state():
        raise RuntimeError('source changed since diagnostic build; rebuild for reproducible results')
    adb = Adb(args.serial)
    originals = {}
    remote = f'{DATA}/krait-frame-times.csv'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='frame-times-') as directory:
        tmp = Path(directory)
        package = adb.shell('pm', 'path', PKG).stdout.strip()
        if not package.startswith('package:') or '\n' in package:
            raise RuntimeError('cannot identify installed APK')
        adb.call('pull', package[len('package:'):], str(tmp/'installed.apk'))
        if sha256(tmp/'installed.apk') != build['apk_sha256']:
            raise RuntimeError('installed APK differs from diagnostic build')
        conditions_before = device_conditions(adb)
        for name in ('env.txt', 'extra_args.txt'):
            old = adb.shell('cat', f'{DATA}/{name}', check=False)
            originals[name] = old.stdout if old.returncode == 0 else None
        try:
            adb.shell('am', 'force-stop', PKG)
            adb.shell('rm', '-f', remote)
            adb.shell('input', 'keyevent', '82')
            settings = dict(TORIRS_PLUGINS='0', TORIRS_PERF='0', TORIRS_GLES2_DUALCORE='1',
                            TORIRS_GLES2_DUALCORE_DEBUG='0', TORIRS_PIPELINE_PMU='frame-time',
                            TORIRS_PIPELINE_AB=args.target, TORIRS_PIPELINE_WARMUP=str(args.warmup),
                            TORIRS_PIPELINE_FRAMES=str(args.frames), TORIRS_FRAME_TIME_FILE=remote)
            settings.update(overrides)
            if not args.manifest:
                sys.path.insert(0, str(REPO / 'tools'))
                from launcher.profiles import Manifest
                from launcher.bench import load_suite
                scene = next(s for s in load_suite(Manifest.load(str(REPO / 'manifests/manifest_osrs239_bench.ini'))).scenes if s.name == args.scene)
                settings.update(TORIRS_WORLD_MAP=scene.world_map_env(), TORIRS_WEDGE_CAM=scene.wedge_cam_env())
            adb.put_text(f'{DATA}/env.txt', ''.join(f'{k}={v}\n' for k, v in settings.items()), tmp)
            launch_args = args.args_file.read_text() if args.args_file else '--gles2-dualcore\n--offline\n'
            if args.uncapped:
                launch_args += '\n--uncapped\n'
            adb.put_text(f'{DATA}/extra_args.txt', launch_args, tmp)
            adb.shell('run-as', PKG, 'am', 'start', '--user', '0', '-n', f'{PKG}/.ClientActivity',
                      '--es', f'{PKG}.MANIFEST', args.manifest or f'{DATA}/manifests/manifest_osrs239_bench.ini')
            deadline = time.monotonic() + args.timeout
            pid = None
            while True:
                if time.monotonic() > deadline:
                    raise RuntimeError('frame-time capture incomplete')
                if pid is None:
                    pid = next((line.split()[1] for line in adb.shell('ps').stdout.splitlines()
                                if line.split() and line.split()[-1] == PKG), None)
                lines = [line for line in adb.call('logcat', '-d', '-s', 'torirs').stdout.splitlines()
                         if pid and re.search(r'\(\s*' + pid + r'\)', line)]
                if any('pipeline-pmu:' in line for line in lines):
                    raise RuntimeError('frame-time probe failed: ' + next(line for line in lines if 'pipeline-pmu:' in line))
                if any(f'frame-times-complete,{12 * args.frames}' in line for line in lines):
                    break
                time.sleep(1)
            raw = adb.shell('cat', remote).stdout
            rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(io.StringIO(raw))]
            if len(rows) != 12 * args.frames:
                raise RuntimeError('wrong frame count')
            for sample in range(12):
                block = [row for row in rows if row['sample'] == sample]
                if len(block) != args.frames or any(row['arm'] != int(sample % 4 in (1, 2)) for row in block):
                    raise RuntimeError('invalid ABBA window')
            if any(row['work_us'] <= 0 or row['cadence_us'] <= 0 for row in rows):
                raise RuntimeError('invalid frame duration')
            if any(a['presented_us'] >= b['presented_us'] for a, b in zip(rows, rows[1:])):
                raise RuntimeError('nonmonotonic presentation timestamps')
            args.out.with_suffix('.csv').write_text(raw)
            result = dict(serial=args.serial, target=args.target, scene='live-lumbridge' if args.manifest else args.scene,
                          uncapped=args.uncapped, frames_per_window=args.frames, warmup=args.warmup,
                          scope='Whole main-loop work through EGL swap and audio, before artificial pacing; cadence is time between completed EGL swaps. GPU queue waits are retained. This is not display scanout latency.',
                          comparison=('Existing optimized renderer enabled in both arms; vary only the named sub10 mechanism.' if args.target.startswith('sub10') else 'All accepted runtime switches off/on in the same binary; common alignment/refactoring remains in both arms.'),
                          instrumentation='Buffered timestamps only; detailed profiler and hardware counter ioctls disabled; no forced glFinish.',
                          requested_egl_swap_interval=0, summary=summarize(rows),
                          abba_blocks=[summarize([row for row in rows if row['sample']//4 == block]) for block in range(3)],
                          switch_matrix=switch_matrix(args.target), settings=settings, build=build,
                          device_before=conditions_before, device_after=device_conditions(adb),
                          launch_args_sha256=hashlib.sha256(launch_args.encode()).hexdigest(),
                          manifest_sha256=hashlib.sha256(adb.shell('cat', args.manifest or f'{DATA}/manifests/manifest_osrs239_bench.ini').stdout.encode()).hexdigest(),
                          library_sha256=hashlib.sha256((REPO / 'android/src/main/jniLibs/armeabi-v7a/libtorirs.so').read_bytes()).hexdigest())
            args.out.write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result['summary'], indent=2), flush=True)
            adb.shell('screencap', '-p', f'{DATA}/krait-frame-times.png')
            adb.call('pull', f'{DATA}/krait-frame-times.png', str(args.out.with_suffix('.png')))
        finally:
            adb.shell('am', 'force-stop', PKG, check=False)
            adb.shell('rm', '-f', remote, f'{DATA}/krait-frame-times.png', check=False)
            for name, content in originals.items():
                if content is None:
                    adb.shell('rm', '-f', f'{DATA}/{name}', check=False)
                else:
                    adb.put_text(f'{DATA}/{name}', content, tmp)


if __name__ == '__main__':
    main()
