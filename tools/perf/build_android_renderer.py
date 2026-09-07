#!/usr/bin/env python3
"""Build/install an ARMv7 renderer diagnostic without stale probe objects."""
import argparse
import json
from pathlib import Path
import subprocess
from model_chain import REPO
from renderer_provenance import source_state, sha256
PROBES={
    'normal':'',
    'overlay-retain-verify':'-DTORIRS_OVERLAY_RETAIN_VERIFY=1',
    'ui-emit-pmu':'-DTORIRS_UI_EMIT_PMU=1',
    'ui-retain-trace':'-DTORIRS_UI_RETAIN_TRACE=1',
    'shader':'-DTORIRS_SHADER_PROBE=1',
    'gpu-counters':'-DTORIRS_GPU_COUNTERS=1',
    'bake-verify':'-DTORIRS_BAKE_VERIFY=1',
    'bake':'-DTORIRS_BAKE_CHAIN_CAPTURE=1',
    'model-chain':'-DTORIRS_MODEL_CHAIN_CAPTURE=1',
    'placement':'-DTORIRS_PLACEMENT_CAPTURE=1',
    'animation':'-DTORIRS_ANIM_CHAIN_CAPTURE=1',
    'pose-verify':'-DTORIRS_POSE_VERIFY=1',
    'pipeline-pmu':'-DTORIRS_PIPELINE_PMU=1',
    'frame-times':'-DTORIRS_PIPELINE_PMU=1 -DTORIRS_FRAME_TIMES=1',
    'canvas-capture':'-DTORIRS_CANVAS_CAPTURE=1',
}
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--probe',choices=PROBES,default='normal')
    ap.add_argument('--install',action='store_true')
    ap.add_argument('--serial',default='T062809L3Z')
    ap.add_argument('--jobs',type=int,default=8)
    args=ap.parse_args()
    directory=REPO/'build/model-chain';directory.mkdir(parents=True,exist_ok=True)
    log=directory/('android-'+args.probe+'.log')
    # Make does not track arbitrary preprocessor-flag changes. These are every
    # translation unit containing the supported probes; all are rebuilt when
    # changing mode, including when returning to a normal APK.
    command=['make','-C',str(REPO/'src'),f'-j{args.jobs}','PLATFORM=android','ANDROID_ABI=armeabi-v7a','OPT=1',
             'TORIDRAW_PROBE_CFLAGS='+PROBES[args.probe],'-W',str(REPO/'3rd/toridraw/toridraw_unity.c'),
             '-W','platform/platform_renderer_gles2_core.c','-W','platform/platform_renderer_gles2_dualcore.c',
             '-W','main.c','-W','app.c','-W','ui/uitree.c','all']
    source = source_state()
    print(f'Building {args.probe}; log: {log}',flush=True)
    with log.open('w') as output:
        subprocess.run(command,cwd=REPO,stdout=output,stderr=subprocess.STDOUT,check=True)
        subprocess.run([str(REPO/'android/gradlew'),'-p',str(REPO/'android'),'-PtorirsAbi=armeabi-v7a','assembleDebug'],cwd=REPO,stdout=output,stderr=subprocess.STDOUT,check=True)
    if source != source_state():
        raise RuntimeError('source changed during build; rebuild before benchmarking')
    library = REPO/'android/src/main/jniLibs/armeabi-v7a/libtorirs.so'
    manifest = dict(probe=args.probe, source=source, command=command,
                    library_sha256=sha256(library),
                    apk_sha256=sha256(REPO/'android/build/outputs/apk/debug/ToriRS-debug.apk'))
    library.with_suffix('.build.json').write_text(json.dumps(manifest, indent=2)+'\n')
    if args.install:
        subprocess.run(['adb','-s',args.serial,'install','-r',str(REPO/'android/build/outputs/apk/debug/ToriRS-debug.apk')],check=True)
    print('Build complete'+('; installed on '+args.serial if args.install else ''),flush=True)
if __name__=='__main__':main()
