"""Hardware-counter ABBA for real overlay-position/layout/UI-emission work."""
import argparse,hashlib,json,re,statistics,sys,tempfile,time
from pathlib import Path
from model_chain import Adb,DATA,PKG,REPO
from renderer_provenance import device_conditions

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--serial',default='T062809L3Z')
 p.add_argument('--manifest',required=True)
 p.add_argument('--args-file',required=True,type=Path)
 p.add_argument('--event',choices=['cpu-cycles','instructions','L1-dcache-load-misses'],default='cpu-cycles')
 p.add_argument('--out',required=True,type=Path)
 a=p.parse_args();a.out.parent.mkdir(parents=True,exist_ok=True)
 adb=Adb(a.serial);originals={};rows=[]
 before=device_conditions(adb)
 with tempfile.TemporaryDirectory() as directory:
  tmp=Path(directory)
  for name in ['env.txt','extra_args.txt']:
   r=adb.shell('cat',f'{DATA}/{name}',check=False);originals[name]=r.stdout if r.returncode==0 else None
  try:
   adb.shell('am','force-stop',PKG);adb.shell('input','keyevent','82')
   settings={'TORIRS_PLUGINS':'0','TORIRS_PERF':'0','TORIRS_GLES2_DUALCORE':'1','TORIRS_GLES2_DUALCORE_DEBUG':'0','TORIRS_UI_EMIT_PMU':a.event,'TORIRS_UI_CANVAS_COMPACT':'1','TORIRS_GLES2_ACTOR_DIRECT':'1'}
   adb.put_text(f'{DATA}/env.txt',''.join(f'{k}={v}\n' for k,v in settings.items()),tmp)
   launch=a.args_file.read_text()+'\n--uncapped\n';adb.put_text(f'{DATA}/extra_args.txt',launch,tmp)
   adb.shell('run-as',PKG,'am','start','--user','0','-n',f'{PKG}/.ClientActivity','--es',f'{PKG}.MANIFEST',a.manifest)
   deadline=time.monotonic()+360;pid=None
   while time.monotonic()<deadline:
    if pid is None:pid=next((l.split()[1] for l in adb.shell('ps').stdout.splitlines() if l.split() and l.split()[-1]==PKG),None)
    lines=[l for l in adb.call('logcat','-d','-s','torirs').stdout.splitlines() if pid and re.search(r'\(\s*'+pid+r'\)',l)]
    if any('ui-emit-pmu FAILED' in l for l in lines):raise RuntimeError('hardware counter capture failed')
    if any('ui-emit-pmu complete' in l for l in lines):break
    time.sleep(1)
   else:raise RuntimeError('hardware windows incomplete')
   records=[l for l in lines if 'ui-emit-pmu,' in l]
   for line in records:
    m=re.search(r'ui-emit-pmu,([^,]+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)',line)
    if not m or m[1]!=a.event:raise RuntimeError('invalid row')
    rows.append(dict(zip(['sample','arm','frames','events','reused','commands'],map(int,m.groups()[1:]))))
   if len(rows)!=12 or any(r['sample']!=i or r['arm']!=int(i%4 in [1,2]) or r['frames']!=180 for i,r in enumerate(rows)):raise RuntimeError('invalid ABBA protocol')
   summary={label:statistics.mean(r['events']/r['frames'] for r in rows if r['arm']==arm) for arm,label in [(0,'before_events_per_frame'),(1,'after_events_per_frame')]}
   summary['change_percent']=100*(summary['after_events_per_frame']/summary['before_events_per_frame']-1)
   result={'serial':a.serial,'event':a.event,'scope':'Real main-thread projected overlay positioning, layout, host-input publication and command emission. Excludes draw submission, worker and GPU. Hardware events only, no time conversion.','protocol':'600 warmup frames; 12 ABBA windows x 180 frames; six settling frames per window; pinned nonmultiplexed userspace event; no detailed software profiling.','settings':settings,'rows':rows,'summary':summary,'device_before':before,'device_after':device_conditions(adb),'build':json.loads((REPO/'android/src/main/jniLibs/armeabi-v7a/libtorirs.build.json').read_text()),'launch_args_sha256':hashlib.sha256(launch.encode()).hexdigest()}
   a.out.write_text(json.dumps(result,indent=2)+'\n');a.out.with_suffix('.log').write_text('\n'.join(records)+'\n')
   adb.shell('screencap','-p',f'{DATA}/p3-pmu.png');adb.call('pull',f'{DATA}/p3-pmu.png',str(a.out.with_suffix('.png')))
   print(json.dumps(summary),flush=True)
  finally:
   adb.shell('am','force-stop',PKG,check=False);adb.shell('rm','-f',f'{DATA}/p3-pmu.png',check=False)
   for name,content in originals.items():
    if content is None:adb.shell('rm','-f',f'{DATA}/{name}',check=False)
    else:adb.put_text(f'{DATA}/{name}',content,tmp)
if __name__=='__main__':main()
