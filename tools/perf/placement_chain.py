#!/usr/bin/env python3
"""Capture actual retained-placement graphs/calls; replay with hardware counters."""
import argparse,gzip,hashlib,json,re,statistics,sys,tempfile,time
from pathlib import Path
from model_chain import Adb,DATA,PKG,REPO

def capture(args,adb):
    sys.path.insert(0,str(REPO/'tools'))
    from launcher.profiles import Manifest
    from launcher.bench import load_suite
    scene=next(s for s in load_suite(Manifest.load(str(REPO/'manifests/manifest_osrs239_bench.ini'))).scenes if s.name==args.scene)
    originals={};remote=f'{DATA}/placement-chain.bin'
    with tempfile.TemporaryDirectory(prefix='placement-capture-') as directory:
        tmp=Path(directory)
        for name in ('env.txt','extra_args.txt'):
            old=adb.shell('cat',f'{DATA}/{name}',check=False);originals[name]=old.stdout if old.returncode==0 else None
        try:
            adb.shell('am','force-stop',PKG);adb.shell('input','keyevent','82')
            settings=dict(TORIRS_PLUGINS='0',TORIRS_PERF='0',TORIRS_GLES2_DUALCORE='1',
                TORIRS_GLES2_DUALCORE_DEBUG='0',TORIRS_WORLD_MAP=scene.world_map_env(),
                TORIRS_WEDGE_CAM=scene.wedge_cam_env(),TORIRS_PLACEMENT_CAPTURE=remote,
                TORIRS_PLACEMENT_CAPTURE_FRAME=str(args.frame))
            adb.put_text(f'{DATA}/env.txt',''.join(f'{k}={v}\n' for k,v in settings.items()),tmp)
            adb.put_text(f'{DATA}/extra_args.txt','--gles2-dualcore\n--offline\n',tmp)
            adb.shell('rm','-f',remote)
            adb.shell('run-as',PKG,'am','start','--user','0','-n',f'{PKG}/.ClientActivity','--es',f'{PKG}.MANIFEST',f'{DATA}/manifests/manifest_osrs239_bench.ini')
            deadline=time.monotonic()+120;pid=None
            while True:
                if time.monotonic()>deadline:raise RuntimeError('placement capture did not finish')
                if pid is None:
                    pid=next((line.split()[1] for line in adb.shell('ps').stdout.splitlines() if line.split() and line.split()[-1]==PKG),None)
                lines=adb.call('logcat','-d','-s','torirs').stdout.splitlines()
                done=next((line for line in lines if pid and re.search(r'\(\s*'+pid+r'\)',line) and 'placement capture complete:' in line),None)
                if done:break
                time.sleep(1)
            local=tmp/'capture.bin';adb.call('pull',remote,str(local));raw=local.read_bytes()
            args.out.parent.mkdir(parents=True,exist_ok=True)
            with args.out.open('wb') as file:
                with gzip.GzipFile(fileobj=file,mode='wb',mtime=0) as stream:stream.write(raw)
            meta=dict(scene=args.scene,frame=args.frame,serial=args.serial,sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),completion=done)
            args.out.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta),flush=True)
        finally:
            adb.shell('am','force-stop',PKG,check=False)
            for name,content in originals.items():
                if content is None:adb.shell('rm','-f',f'{DATA}/{name}',check=False)
                else:adb.put_text(f'{DATA}/{name}',content,tmp)

def run(args,adb):
    raw=gzip.open(args.corpus,'rb').read();digest=hashlib.sha256(raw).hexdigest()
    remote=f'/data/local/tmp/placement-{digest}.bin'
    adb.shell('am','force-stop',PKG)
    if adb.shell('test','-s',remote,check=False).returncode:
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'placement.bin';p.write_bytes(raw);adb.call('push',str(p),remote)
    binary=REPO/'build/model-chain/android/placement_chain_replay'
    adb.call('push',str(binary),'/data/local/tmp/placement_chain_replay')
    result=dict(corpus=str(args.corpus),sha256=digest,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),events={})
    for event in args.event or ['cpu-cycles','instructions']:
        completed=adb.shell('/data/local/tmp/placement_chain_replay',remote,event,timeout=180)
        rows=[]
        for line in completed.stdout.splitlines():
            if line.startswith(('graph:','verified:')):result[line.split(':')[0]]=line
            if line.startswith('pmu,'):
                _,observed,mode,sample,queries,count,per_call,arm=line.split(',')
                if observed!=event or mode!='placement':raise RuntimeError('unexpected counter row')
                rows.append(dict(sample=int(sample),queries=int(queries),count=int(count),per_call=float(per_call),arm=int(arm.split('=')[1])))
        if event!='verify' and len(rows)!=12:raise RuntimeError('incomplete hardware counter rows')
        if rows:
            med={str(a):statistics.median(r['per_call'] for r in rows if r['arm']==a) for a in (0,1)}
            result['events'][event]=dict(arms=med,samples=rows)
            print(f'{event}: A={med["0"]:.3f}, B={med["1"]:.3f}',flush=True)
        else:print(completed.stdout,flush=True)
        args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2)+'\n')

def main():
    ap=argparse.ArgumentParser(description=__doc__);sub=ap.add_subparsers(dest='command',required=True)
    cap=sub.add_parser('capture');cap.add_argument('--scene',default='varrock-square-ground');cap.add_argument('--frame',type=int,default=120);cap.add_argument('--out',type=Path,required=True)
    replay=sub.add_parser('run');replay.add_argument('corpus',type=Path);replay.add_argument('--event',action='append',choices=('verify','cpu-cycles','instructions','branch-misses','L1-dcache-load-misses'));replay.add_argument('--out',type=Path,required=True)
    for p in (cap,replay):p.add_argument('--serial',default='T062809L3Z')
    args=ap.parse_args();(capture if args.command=='capture' else run)(args,Adb(args.serial))
if __name__=='__main__':main()
