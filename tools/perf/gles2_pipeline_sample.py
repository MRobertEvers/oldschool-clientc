#!/usr/bin/env python3
"""Sample an installed normal GLES2 app with hardware events only, after 300 rendered frames."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from model_chain import Adb, DATA, PKG, REPO


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--serial', default='T062809L3Z')
    ap.add_argument('--scene', default='varrock-square-ground')
    ap.add_argument('--event', choices=('cpu-cycles','instructions','branch-misses','L1-dcache-load-misses'), default='cpu-cycles')
    ap.add_argument('--period', type=int, default=150000)
    ap.add_argument('--duration', type=int, default=15)
    ap.add_argument('--motion', action='store_true')
    ap.add_argument('--manifest', help='existing device manifest path for a live session')
    ap.add_argument('--args-file',type=Path,help='private local file containing live client arguments')
    ap.add_argument('--warmup',type=int,default=300)
    ap.add_argument('--callgraph', choices=('none','dwarf','fp'), default='none')
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if bool(args.manifest)!=bool(args.args_file): ap.error('--manifest and --args-file must be supplied together')
    if args.warmup<1: ap.error('warmup must be positive')
    if args.period < 1 or not 1 <= args.duration <= 60:
        ap.error('positive period and duration 1..60 required')
    sys.path.insert(0,str(REPO/'tools'))
    from launcher.profiles import Manifest
    from launcher.bench import load_suite
    scene = None if args.manifest else next(s for s in load_suite(Manifest.load(str(REPO/'manifests/manifest_osrs239_bench.ini'))).scenes if s.name==args.scene)
    adb = Adb(args.serial)
    args.out.mkdir(parents=True,exist_ok=True)
    info = dict(scene=args.scene,event=args.event,period=args.period,motion=args.motion,serial=args.serial,
                live=bool(args.manifest),warmup=args.warmup,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip())
    so = REPO/'android/src/main/jniLibs/armeabi-v7a/libtorirs.so'
    info['local_library_sha256'] = hashlib.sha256(so.read_bytes()).hexdigest()
    originals = {}
    with tempfile.TemporaryDirectory(prefix='pipeline-sample-') as directory:
        tmp = Path(directory)
        for name in ('env.txt','extra_args.txt'):
            old = adb.shell('cat',f'{DATA}/{name}',check=False)
            originals[name] = old.stdout if old.returncode==0 else None
        try:
            adb.shell('am','force-stop',PKG)
            adb.shell('input','keyevent','82')
            warmfile = f'{DATA}/pipeline-sample-warm.bmp'
            adb.shell('rm','-f',warmfile)
            settings = dict(TORIRS_PLUGINS='0',TORIRS_PERF='0',TORIRS_GLES2_DUALCORE='1',
                TORIRS_GLES2_DUALCORE_DEBUG='0',TORIRS_GLES2_READBACK=warmfile,
                TORIRS_GLES2_READBACK_FRAME=str(args.warmup))
            if scene:
                settings['TORIRS_WORLD_MAP']=scene.world_map_env()
                settings['TORIRS_WEDGE_CAM']=scene.wedge_cam_env()
            if args.motion and scene and scene.wedge_cam_path_env():
                settings['TORIRS_WEDGE_CAM_PATH'] = scene.wedge_cam_path_env()
            adb.put_text(f'{DATA}/env.txt',''.join(f'{k}={v}\n' for k,v in settings.items()),tmp)
            adb.put_text(f'{DATA}/extra_args.txt',args.args_file.read_text() if args.args_file else '--gles2-dualcore\n--offline\n',tmp)
            adb.shell('run-as',PKG,'am','start','--user','0','-n',f'{PKG}/.ClientActivity',
                '--es',f'{PKG}.MANIFEST',args.manifest or f'{DATA}/manifests/manifest_osrs239_bench.ini')
            deadline = time.monotonic()+120
            pid=None
            while True:
                if time.monotonic()>deadline: raise RuntimeError('rendered-frame warmup did not complete')
                if pid is None:
                    pid=next((line.split()[1] for line in adb.shell('ps').stdout.splitlines() if line.split() and line.split()[-1]==PKG),None)
                if pid and adb.shell('test','-s',warmfile,check=False).returncode==0:
                    # Normal builds compile narration out. Check the completed
                    # BMP payload instead of waiting for a debug-only log line.
                    warm_local=tmp/'warm.bmp'
                    adb.call('pull',warmfile,str(warm_local))
                    warm=warm_local.read_bytes()
                    if len(warm)>=54 and warm[:2]==b'BM' and struct.unpack_from('<I',warm,2)[0]==len(warm): break
                time.sleep(1)
            maps = adb.shell('run-as',PKG,'cat',f'/proc/{pid}/maps').stdout
            (args.out/'maps.txt').write_text(maps)
            (args.out/'threads.txt').write_text(adb.shell('ps','-t').stdout)
            libpath = next(line.split()[-1] for line in maps.splitlines() if 'libtorirs.so' in line)
            installed = tmp/'installed.so'
            adb.shell('run-as',PKG,'cp',libpath,f'{DATA}/pipeline-sample.so')
            adb.call('pull',f'{DATA}/pipeline-sample.so',str(installed))
            adb.shell('rm','-f',f'{DATA}/pipeline-sample.so')
            info['installed_library_sha256'] = hashlib.sha256(installed.read_bytes()).hexdigest()
            if info['installed_library_sha256'] != info['local_library_sha256']:
                raise RuntimeError('installed library differs from local symbols; install the matching APK')
            sym = args.out/'symfs'/libpath.lstrip('/')
            sym.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(so,sym)
            command = ['run-as',PKG,'./simpleperf','record','-e',args.event+':u','-c',str(args.period),
                '-p',pid,'--duration',str(args.duration),'-o',f'{DATA}/pipeline-sample.data','--no-dump-kernel-symbols']
            if args.callgraph != 'none':
                command += ['--call-graph', 'dwarf,2048' if args.callgraph=='dwarf' else 'fp']
            print('Recording '+args.event+' hardware samples for pid '+pid,flush=True)
            completed = adb.shell(*command,timeout=args.duration+60,check=False)
            record_log=completed.stdout+completed.stderr
            (args.out/'record.log').write_text(record_log)
            print(completed.stdout+completed.stderr,flush=True)
            if completed.returncode: raise RuntimeError("hardware sampling failed; see record.log")
            counts=re.search(r'Samples recorded: ([0-9,]+)\. Samples lost: ([0-9,]+)',record_log)
            if not counts or int(counts[1].replace(',','')) == 0 or int(counts[2].replace(',','')):
                raise RuntimeError('hardware recording missing samples or reports loss')
            info['samples']=int(counts[1].replace(',',''))
            info['lost']=0
            adb.call('pull',f'{DATA}/pipeline-sample.data',str(args.out/'perf.data'))
            adb.shell('screencap','-p',f'{DATA}/pipeline-sample.png')
            adb.call('pull',f'{DATA}/pipeline-sample.png',str(args.out/'screen.png'))
            info['library_map_path'] = libpath
            info['record_command'] = command
            (args.out/'metadata.json').write_text(json.dumps(info,indent=2)+'\n')
        finally:
            adb.shell('am','force-stop',PKG,check=False)
            for name,content in originals.items():
                if content is None: adb.shell('rm','-f',f'{DATA}/{name}',check=False)
                else: adb.put_text(f'{DATA}/{name}',content,tmp)

if __name__=='__main__':
    main()
