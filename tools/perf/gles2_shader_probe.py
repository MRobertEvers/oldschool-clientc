#!/usr/bin/env python3
"""Collect existing KGSL shader hardware counters over rendered-frame windows."""
import argparse,json,re,sys,tempfile,time,hashlib
from pathlib import Path
from model_chain import Adb,DATA,PKG,REPO

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--serial',default='T062809L3Z');ap.add_argument('--scene',default='grand-exchange-ground');ap.add_argument('--out',type=Path,required=True);ap.add_argument('--aa',action='store_true');ap.add_argument('--motion',action='store_true');ap.add_argument('--parity-only',action='store_true');args=ap.parse_args()
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
            settings=dict(TORIRS_PLUGINS='0',TORIRS_PERF='0',TORIRS_GLES2_DUALCORE='1',TORIRS_GLES2_DUALCORE_DEBUG='0',TORIRS_GLES2_ACTOR_WORLD_CACHE='1',TORIRS_SHADER_PROBE='1',TORIRS_WORLD_MAP=scene.world_map_env(),TORIRS_WEDGE_CAM=scene.wedge_cam_env())
            if args.aa:settings['TORIRS_SHADER_AA']='1'
            if args.motion and scene.wedge_cam_path_env():settings['TORIRS_WEDGE_CAM_PATH']=scene.wedge_cam_path_env()
            if args.parity_only:settings['TORIRS_SHADER_PARITY_ONLY']='1'
            adb.put_text(f'{DATA}/env.txt',''.join(f'{k}={v}\n' for k,v in settings.items()),tmp);adb.put_text(f'{DATA}/extra_args.txt','--gles2-dualcore\n--offline\n',tmp)
            adb.shell('run-as',PKG,'am','start','--user','0','-n',f'{PKG}/.ClientActivity','--es',f'{PKG}.MANIFEST',f'{DATA}/manifests/manifest_osrs239_bench.ini')
            deadline=time.monotonic()+180;pid=None
            parity=None;parities=[];done=False
            while not done:
                if time.monotonic()>deadline:raise RuntimeError('GPU hardware windows incomplete')
                if pid is None:pid=next((l.split()[1] for l in adb.shell('ps').stdout.splitlines() if l.split() and l.split()[-1]==PKG),None)
                for line in adb.call('logcat','-d','-s','torirs').stdout.splitlines():
                    if not pid or not re.search(r'\(\s*'+pid+r'\)',line):continue
                    if 'shader probe:' in line:raise RuntimeError(line)
                    p=re.search(r'shader-parity,(\d+),(\d+),(\d+),(\d+),(\d+)',line)
                    if p:
                        parity=dict(width=int(p[1]),height=int(p[2]),different_pixels=int(p[3]),draw_items=int(p[4]),frame=int(p[5]))
                        if not any(x['frame']==parity['frame'] for x in parities):parities.append(parity);print(json.dumps(parity),flush=True)
                    if 'shader probe complete:' in line:done=True
                    m=re.search(r'shader-gpu,(\d+),(\d+),(\d+),(\d+),(\d+)',line)
                    if m and not any(r['sample']==int(m[1]) for r in rows):
                        row=dict(sample=int(m[1]),arm=int(m[2]),replays=int(m[3]),sp_alu_cycles=int(m[4]),sp_fragment_alu_instructions=int(m[5]));rows.append(row);print(json.dumps(row),flush=True)
                time.sleep(1)
            result=dict(scene=args.scene,serial=args.serial,aa=args.aa,parity=parity,parities=parities,motion=args.motion,scope='Real frozen painter draw-list replay; device-wide shader hardware counts. No time/occupancy/FPS inference.',rows=rows,library_sha256=hashlib.sha256((REPO/'android/src/main/jniLibs/armeabi-v7a/libtorirs.so').read_bytes()).hexdigest())
            if not parities or any(p['different_pixels'] for p in parities):raise RuntimeError('missing/failed pixel parity')
            if args.parity_only and len(parities)!=4:raise RuntimeError('missing moving-camera checkpoints')
            if not args.parity_only and len(rows)!=12:raise RuntimeError('missing hardware windows')
            args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2)+'\n')
        finally:
            adb.shell('am','force-stop',PKG,check=False)
            for name,value in originals.items():
                if value is None:adb.shell('rm','-f',f'{DATA}/{name}',check=False)
                else:adb.put_text(f'{DATA}/{name}',value,tmp)
if __name__=='__main__':main()
