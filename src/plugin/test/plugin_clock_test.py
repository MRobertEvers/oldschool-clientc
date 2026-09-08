#!/usr/bin/env python3
"""Exercise the shipped clock handoff/getter with deliberately different clocks."""
from pathlib import Path
import os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[3]
app=(root/'src/app.c').read_text();bridge=(root/'src/plugin/torirs_plugin_bridge.u.c').read_text()
def body(source,name):
 m=re.search(r'\b'+re.escape(name)+r'\s*\([^;]*?\)\s*\{',source,re.S)
 assert m,name
 start=m.end()-1;depth=1;at=start+1
 while depth:
  depth+=(source[at]=='{')-(source[at]=='}');at+=1
 return source[start:at]
setter=body(app,'App_SetPluginFrameTime');getter=body(bridge,'app_plugin_frame_ms')
run=app[app.index('\nApp_RunOnce('):]
setter_call=re.search(r'\bApp_SetPluginFrameTime\(app,[^;]+;',run).group()
frame_call=re.search(r'\bPluginHost_FrameStart\(app->plugins,[^;]+;',run).group()
program='''#include <stdint.h>
#include <stdio.h>
#include <assert.h>
struct ToriRS_PluginHost { int unused; };
struct App { uint64_t last_frame_ms,plugin_frame_ms,frames_rendered; struct ToriRS_PluginHost* plugins; };
struct Input { struct { uint64_t time; } curr; };
static uint64_t observed_time,observed_draws,observed_core;
static struct App* observed_app;
static void App_SetPluginFrameTime(struct App* app,uint64_t frame_ms) SETTER
static uint64_t app_plugin_frame_ms(void* user) GETTER
static void PluginHost_FrameStart(struct ToriRS_PluginHost* host,uint64_t now,uint64_t draws)
{ (void)host;observed_time=now;observed_draws=draws;observed_core=app_plugin_frame_ms(observed_app); }
static int handoff(uint64_t real_or_recorded,uint64_t logic)
{
 struct App state={.last_frame_ms=77,.frames_rendered=15},*app=&state;
 struct Input values={.curr={.time=real_or_recorded}},*input=&values;
 uint64_t now_ms=logic;
 observed_app=app;
 SETTER_CALL
 FRAME_CALL
 if(observed_time!=real_or_recorded || observed_core!=real_or_recorded || observed_draws!=15 || state.last_frame_ms!=77)
 { fprintf(stderr,"clock mismatch input=%llu logic=%llu event=%llu core=%llu\\n",(unsigned long long)real_or_recorded,(unsigned long long)logic,(unsigned long long)observed_time,(unsigned long long)observed_core);return 1; }
 return 0;
}
int main(void)
{
 int failed=handoff(13443,13160); /* observed live GameShell drift */
 failed|=handoff(1000,40000);    /* recorded input must not become wall/logic time */
 failed|=handoff(1000,40020);    /* deterministic repeated timestamp */
 failed|=handoff(250,40040);     /* replay seek/reset retains the recorded value */
 failed|=handoff(0,20);          /* synthetic time zero is a valid timestamp */
 if(!failed)puts("plugin clock: live drift, replay, repeated/reset/zero time and shared asset deadline clock passed");
 return failed;
}
'''.replace('SETTER_CALL',setter_call).replace('FRAME_CALL',frame_call).replace('SETTER',setter).replace('GETTER',getter)
with tempfile.TemporaryDirectory(prefix='torirs-plugin-clock-') as scratch:
 p=Path(scratch);(p/'test.c').write_text(program)
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
