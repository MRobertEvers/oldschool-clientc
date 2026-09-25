#!/usr/bin/env python3
"""Collect existing KGSL shader hardware counters over rendered-frame windows."""
import argparse,json,re,sys,tempfile,time,hashlib
from pathlib import Path
from model_chain import Adb,DATA,PKG,REPO

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--serial',default='T062809L3Z');ap.add_argument('--scene',default='grand-exchange-ground');ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
    sys.path.insert(0,str(REPO/'tools'))
    from launcher.profiles import Manifest
    from launcher.bench import load_suite
    scene=next(s for s in load_suite(Manifest.load(str(REPO/'manifests/manifest_osrs239_bench.ini'))).scenes if s.name==args.scene)
    adb=Adb(args.serial);originals={};rows=[]
    with tempfile.TemporaryDirectory() as directory:
        tmp=Path(directory)
        for name in ('env.txt','extra_args.txt'):
            old=adb.shell('cat',f'{DATA}/{name}',check=False);originals[name]=old.stdout if old.returncode==0 else None
        try:
            adb.shell('am','force-stop',PKG);adb.shell('input','keyevent','82')
            settings=dict(TORIRS_PLUGINS='0',TORIRS_PERF='0',TORIRS_GLES2_DUALCORE='1',TORIRS_GLES2_DUALCORE_DEBUG='0',TORIRS_GLES2_ACTOR_WORLD_CACHE='1',TORIRS_GPU_COUNTERS='1',TORIRS_WORLD_MAP=scene.world_map_env(),TORIRS_WEDGE_CAM=scene.wedge_cam_env())
            adb.put_text(f'{DATA}/env.txt',''.join(f'{k}={v}\n' for k,v in settings.items()),tmp);adb.put_text(f'{DATA}/extra_args.txt','--gles2-dualcore\n--offline\n',tmp)
            adb.shell('run-as',PKG,'am','start','--user','0','-n',f'{PKG}/.ClientActivity','--es',f'{PKG}.MANIFEST',f'{DATA}/manifests/manifest_osrs239_bench.ini')
            deadline=time.monotonic()+180;pid=None
            while len(rows)<4:
                if time.monotonic()>deadline:raise RuntimeError('GPU hardware windows incomplete')
                if pid is None:pid=next((l.split()[1] for l in adb.shell('ps').stdout.splitlines() if l.split() and l.split()[-1]==PKG),None)
                for line in adb.call('logcat','-d','-s','torirs').stdout.splitlines():
                    if not pid or not re.search(r'\(\s*'+pid+r'\)',line):continue
                    if 'GPU counter' in line and any(x in line for x in ['failed','unavailable','failure']):raise RuntimeError(line)
                    m=re.search(r'gpu-counters,(\d+),(\d+),(\d+),(\d+)',line)
                    if m and not any(r['sample']==int(m[1]) for r in rows):
                        row=dict(sample=int(m[1]),frames=int(m[2]),sp_alu_cycles=int(m[3]),sp_fragment_alu_instructions=int(m[4]));rows.append(row);print(json.dumps(row),flush=True)
                time.sleep(1)
            result=dict(scene=args.scene,serial=args.serial,scope='Device-wide already-reserved SP hardware counters. glFinish at each 120-frame window boundary. No GPU time, occupancy or FPS inference.',rows=rows,library_sha256=hashlib.sha256((REPO/'android/src/main/jniLibs/armeabi-v7a/libtorirs.so').read_bytes()).hexdigest())
            args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2)+'\n')
        finally:
            adb.shell('am','force-stop',PKG,check=False)
            for name,value in originals.items():
                if value is None:adb.shell('rm','-f',f'{DATA}/{name}',check=False)
                else:adb.put_text(f'{DATA}/{name}',value,tmp)
if __name__=='__main__':main()
