#!/usr/bin/env python3
"""Compare two compiled real-model pipelines in one hardware-counter process.

Run an A/A comparison first: the libraries have independent heaps, so this
control measures layout bias that ordinary within-process flag A/B avoids.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import statistics
import tempfile
from model_chain import Adb, REPO, PKG


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('corpus',type=Path)
    ap.add_argument('--a',type=Path,required=True)
    ap.add_argument('--b',type=Path,required=True)
    ap.add_argument('--binary',type=Path,default=REPO/'build/model-chain/android/model_chain_compare')
    ap.add_argument('--serial',default='T062809L3Z')
    ap.add_argument('--event',action='append',choices=('cpu-cycles','instructions','branch-misses','L1-dcache-load-misses'))
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    adb=Adb(args.serial)
    raw=gzip.open(args.corpus,'rb').read() if args.corpus.suffix=='.gz' else args.corpus.read_bytes()
    digest=hashlib.sha256(raw).hexdigest()
    corpus_remote=f'/data/local/tmp/model-chain-{digest}.bin'
    result=dict(corpus=str(args.corpus),corpus_sha256=digest,serial=args.serial,events={},
                method='ABBA x3; separate libraries and heaps; same PMU process; three warm windows per sample')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    adb.shell('am','force-stop',PKG)
    with tempfile.TemporaryDirectory(prefix='chain-compare-') as directory:
        if adb.shell('test','-s',corpus_remote,check=False).returncode:
            local=Path(directory)/'corpus.bin';local.write_bytes(raw)
            adb.call('push',str(local),corpus_remote)
    remote_libraries=[]
    for arm,path in [('a',args.a),('b',args.b)]:
        sha=hashlib.sha256(path.read_bytes()).hexdigest()
        remote=f'/data/local/tmp/model-chain-lib-{arm}-{sha}.so'
        adb.call('push',str(path),remote)
        remote_libraries.append(remote)
        result[arm]=dict(path=str(path),sha256=sha)
    binary='/data/local/tmp/model_chain_compare'
    result['binary_sha256']=hashlib.sha256(args.binary.read_bytes()).hexdigest()
    adb.call('push',str(args.binary),binary)
    for event in args.event or ['cpu-cycles','instructions']:
        completed=adb.shell(binary,corpus_remote,*remote_libraries,event,timeout=180)
        rows=[]
        for line in completed.stdout.splitlines():
            if line.startswith(('verified:','placement:')):
                result[line.split(':',1)[0]]=line
            if line.startswith('pmu,'):
                _,observed,mode,sample,calls,count,per_call,arm=line.split(',')
                if observed!=event or mode!='chain-compare': raise RuntimeError('unexpected PMU result')
                rows.append(dict(sample=int(sample),calls=int(calls),count=int(count),per_call=float(per_call),arm=int(arm.split('=')[1])))
        if len(rows)!=12 or {r['sample'] for r in rows}!=set(range(1,13)):
            raise RuntimeError('incomplete hardware-counter samples')
        arms={str(arm):statistics.median(r['per_call'] for r in rows if r['arm']==arm) for arm in (0,1)}
        percent=100*(arms['1']/arms['0']-1) if arms['0'] else None
        result['events'][event]=dict(arms=arms,percent=percent,samples=rows)
        args.out.write_text(json.dumps(result,indent=2)+'\n')
        print(f'{event}: A={arms["0"]:.3f}, B={arms["1"]:.3f}, change={percent}',flush=True)

if __name__=='__main__': main()
