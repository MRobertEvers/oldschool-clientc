#!/usr/bin/env python3
"""Capture real live pose calls and replay them with hardware counters only."""
import argparse,gzip,hashlib,json,re,statistics,tempfile,time
from pathlib import Path
from model_chain import Adb,DATA,PKG,REPO

def capture(args,adb):
    originals={};remote=f'{DATA}/animation-chain.bin'
    with tempfile.TemporaryDirectory(prefix='animation-capture-') as directory:
        tmp=Path(directory)
        for name in ('env.txt','extra_args.txt'):
            old=adb.shell('cat',f'{DATA}/{name}',check=False);originals[name]=old.stdout if old.returncode==0 else None
        try:
            adb.shell('am','force-stop',PKG);adb.shell('input','keyevent','82')
            settings=dict(TORIRS_PLUGINS='0',TORIRS_PERF='0',TORIRS_GLES2_DUALCORE='0',
                TORIRS_GLES2_POSE_REUSE='0',TORIRS_ANIM_CHAIN_CAPTURE=remote,TORIRS_ANIM_CHAIN_FIRST_PASS=str(args.first),
                TORIRS_ANIM_CHAIN_PASSES=str(args.passes),TORIDRAW_ANIM_SKIP_SAME='0')
            for item in args.env or []:
                name,sep,value=item.partition('=')
                if not sep or not re.fullmatch(r'TORIRS_[A-Z0-9_]+',name) or '\n' in value: raise ValueError('invalid capture environment')
                settings[name]=value
            adb.put_text(f'{DATA}/env.txt',''.join(f'{k}={v}\n' for k,v in settings.items()),tmp)
            arguments=args.args_file.read_text().replace('--gles2-dualcore','--gles2')
            adb.put_text(f'{DATA}/extra_args.txt',arguments,tmp);adb.shell('rm','-f',remote)
            adb.shell('run-as',PKG,'am','start','--user','0','-n',f'{PKG}/.ClientActivity','--es',f'{PKG}.MANIFEST',args.manifest)
            deadline=time.monotonic()+180;pid=None
            while True:
                if time.monotonic()>deadline:raise RuntimeError('real animation capture incomplete')
                if pid is None:pid=next((l.split()[1] for l in adb.shell('ps').stdout.splitlines() if l.split() and l.split()[-1]==PKG),None)
                log=adb.call('logcat','-d','-s','torirs').stdout.splitlines()
                done=next((l for l in log if pid and re.search(r'\(\s*'+pid+r'\)',l) and 'animation capture complete:' in l),None)
                if done:break
                time.sleep(1)
            local=tmp/'animation.bin';adb.call('pull',remote,str(local));raw=local.read_bytes()
            args.out.parent.mkdir(parents=True,exist_ok=True)
            with args.out.open('wb') as f:
                with gzip.GzipFile(fileobj=f,mode='wb',mtime=0) as z:z.write(raw)
            meta=dict(serial=args.serial,manifest=args.manifest,first_pass=args.first,passes=args.passes,sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),completion=done)
            args.out.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta),flush=True)
        finally:
            adb.shell('am','force-stop',PKG,check=False)
            for name,text in originals.items():
                if text is None:adb.shell('rm','-f',f'{DATA}/{name}',check=False)
                else:adb.put_text(f'{DATA}/{name}',text,tmp)

def run(args,adb):
    raw=gzip.open(args.corpus,'rb').read();sha=hashlib.sha256(raw).hexdigest();remote=f'/data/local/tmp/anim-{sha}.bin'
    adb.shell('am','force-stop',PKG)
    if adb.shell('test','-s',remote,check=False).returncode:
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'anim.bin';p.write_bytes(raw);adb.call('push',str(p),remote)
    binary=REPO/'build/model-chain/android/anim_chain_replay';adb.call('push',str(binary),'/data/local/tmp/anim_chain_replay')
    result=dict(corpus=str(args.corpus),sha256=sha,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),events={})
    for event in args.event or ['cpu-cycles','instructions']:
        got=adb.shell('/data/local/tmp/anim_chain_replay',remote,event,timeout=180,check=False);rows=[]
        if got.returncode:
            args.out.parent.mkdir(parents=True,exist_ok=True);args.out.with_suffix('.log').write_text(got.stdout+got.stderr)
            raise RuntimeError('animation replay failed; see '+str(args.out.with_suffix('.log')))
        for line in got.stdout.splitlines():
            if line.startswith('verified:'):result['workload']=line
            if line.startswith('pmu,'):
                _,observed,mode,sample,calls,count,per_call,arm=line.split(',')
                if observed!=event or mode!='animation':raise RuntimeError('unexpected counter sample')
                rows.append(dict(sample=int(sample),calls=int(calls),count=int(count),per_call=float(per_call),arm=int(arm.split('=')[1])))
        if event!='verify' and len(rows)!=12:raise RuntimeError('missing hardware-counter windows')
        if rows:
            med={str(a):statistics.median(r['per_call'] for r in rows if r['arm']==a) for a in (0,1)}
            result['events'][event]=dict(arms=med,samples=rows);print(f'{event}: A={med["0"]:.3f}, B={med["1"]:.3f}',flush=True)
        else:print(got.stdout,flush=True)
        args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2)+'\n')
def main():
    ap=argparse.ArgumentParser(description=__doc__);sub=ap.add_subparsers(dest='command',required=True)
    cap=sub.add_parser('capture');cap.add_argument('--manifest',required=True);cap.add_argument('--args-file',type=Path,required=True);cap.add_argument('--first',type=int,default=120);cap.add_argument('--passes',type=int,default=16);cap.add_argument('--out',type=Path,required=True);cap.add_argument('--env',action='append')
    rep=sub.add_parser('run');rep.add_argument('corpus',type=Path);rep.add_argument('--event',action='append',choices=('verify','cpu-cycles','instructions','branch-misses','L1-dcache-load-misses'));rep.add_argument('--out',type=Path,required=True)
    for p in (cap,rep):p.add_argument('--serial',default='T062809L3Z')
    args=ap.parse_args();(capture if args.command=='capture' else run)(args,Adb(args.serial))
if __name__=='__main__':main()
