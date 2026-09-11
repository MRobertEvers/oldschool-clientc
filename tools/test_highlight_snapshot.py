#!/usr/bin/env python3
"""Exercise actual bridge snapshot code against the real native highlight store.

The fixture contains only TILE subjects, so it extracts that exact resolver
branch instead of constructing unrelated NPC/LOC/player pools. The iterator,
style builder and both append paths are the production functions. --source-ref
runs the same population against an earlier implementation as a negative control.
"""
from pathlib import Path
import argparse, hashlib, os, shlex, subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--out-dir',required=True)
parser.add_argument('--source-ref')
parser.add_argument('--cc',default=os.environ.get('CC','cc'))
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args()
repo=Path(__file__).resolve().parents[1]
relative='src/plugin/torirs_plugin_bridge.u.c'
source=subprocess.check_output(['git','show',args.source_ref+':'+relative],cwd=repo,text=True) if args.source_ref else (repo/relative).read_text()

def function(name):
    at=source.index(name+'(')
    start=source.rfind('\nstatic ',0,at)+1
    opening=source.index('{',at)
    depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'

start=source.index('    /* ---- tiles: the member IS the thing, no pool to walk. ---- */')
end=source.index('    /* ---- npcs:',start)
loop=source[start:end]
growing='app_plugin_highlight_append(' in source
prefix=r'''
#include "plugin/torirs_plugin_api.h"
#include "game/rs_highlight.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define APP_PLUGIN_HIGHLIGHTS_MAX 256
#define APP_PLUGIN_HIGHLIGHTS_INITIAL_CAPACITY 256
struct World {int _base_tile_x,_base_tile_z;};
struct App {
 struct World* world;
 struct {struct RS_HighlightState highlight;} host;
 struct ToriRS_HighlightItem* plugin_highlights;
 int plugin_highlight_count,plugin_highlight_capacity;
 struct ToriRS_HighlightItem* plugin_highlight_loc;
 int plugin_highlight_loc_count,plugin_highlight_loc_capacity;
};
static int failures,checks;
#define CHECK(c,m) do {++checks;if(!(c)){++failures;fprintf(stderr,"FAIL: %s\n",m);}}while(0)
'''
parts=[prefix,function('app_plugin_highlight_begin')]
if growing: parts.append(function('app_plugin_highlight_append'))
parts += [function('app_plugin_highlight_push'),function('app_plugin_highlight_loc_cache_push')]
parts += ['static void app_plugin_highlights_rebuild(struct App* app) {\nstruct RS_HighlightState const* hl=&app->host.highlight;\nstruct ToriRS_HighlightItem proto;app->plugin_highlight_count=0;\n'+loop+'}\n',
          'static void app_plugin_highlights_report(struct App* app) {(void)app;}\n',function('app_plugin_highlight_next')]
suffix=r'''
int main(void) {
 struct World world={3136,3392};struct App app={.world=&world};
 /* Provision enough space even for the baseline's fixed-index append. The
    old256 policy still truncates; this avoids turning its known policy bug
    into an artificial null-pointer failure in the small scaffold. */
 app.plugin_highlight_capacity=512;
 app.plugin_highlights=calloc(512,sizeof(*app.plugin_highlights));assert(app.plugin_highlights);
 app.plugin_highlight_loc_capacity=512;
 app.plugin_highlight_loc=calloc(512,sizeof(*app.plugin_highlight_loc));assert(app.plugin_highlight_loc);
 RS_HighlightReset(&app.host.highlight);
 RS_HighlightSetup(&app.host.highlight,RS_HIGHLIGHT_TILE,6,0xabcdef,0,70,90);
 for(int i=0;i<512;++i) {
  int coord=RS_HIGHLIGHT_COORD(0,3200+i%32,3400+i/32);
  CHECK(RS_HighlightOn(&app.host.highlight,RS_HIGHLIGHT_TILE,6,0,coord,1),"native store accepts every one of512 distinct tiles");
 }
 CHECK(app.host.highlight.member_count[RS_HIGHLIGHT_TILE]==512,"all512 subjects exist before querying the bridge");
 int cursor=-1,seen=0;struct ToriRS_HighlightItem item;
 while((cursor=app_plugin_highlight_next(&app,cursor,&item))>=0) {
  CHECK(item.kind==TORIRS_HIGHLIGHT_TILE && item.element_id==-1,"native TILE identity reaches the iterator");
  CHECK(item.tile_x==3200+seen%32 && item.tile_z==3400+seen/32,"query preserves every coordinate in order");
  CHECK(item.rgb==0xabcdef && item.opacity==70 && item.outline_width==0 && item.flags==91,"native style and member flags survive the snapshot");
  ++seen;
 }
 CHECK(seen==512,"query must not silently truncate512 native tiles at256");
 printf("native TILE store512 -> bridge query%d\n",seen);
 struct ToriRS_HighlightItem const first=app.plugin_highlights[0];
 struct ToriRS_HighlightItem* alias=app_plugin_highlight_push(&app,&app.plugin_highlights[0]);
 CHECK(alias && !memcmp(alias,&first,sizeof(first)),"appending from the same vector preserves its value across a grow");
 struct ToriRS_HighlightItem loc={.kind=TORIRS_HIGHLIGHT_LOC,.size_x=2,.size_z=3};
 for(int i=0;i<700;++i) {loc.element_id=i;app_plugin_highlight_loc_cache_push(&app,&loc);}
 CHECK(app.plugin_highlight_loc_count==700,"LOC cache completeness is independent of the512 draw budget");
 if(app.plugin_highlight_loc_count==700) CHECK(app.plugin_highlight_loc[699].element_id==699,"last retained LOC survives cache growth");
 int capacity=app.plugin_highlight_loc_capacity;
 app.plugin_highlight_loc_count=0;
 app_plugin_highlight_loc_cache_push(&app,&loc);
 CHECK(app.plugin_highlight_loc_count==1 && app.plugin_highlight_loc_capacity==capacity,"a smaller later cache reuses its storage without stale entries");
 RS_HighlightClear(&app.host.highlight,RS_HIGHLIGHT_TILE,6);
 CHECK(app_plugin_highlight_next(&app,-1,&item)==-1,"clearing native subjects publishes an empty next snapshot");
#ifdef GROWING_SNAPSHOT
 struct ToriRS_HighlightItem sentinel;memset(&sentinel,0x5a,sizeof(sentinel));item=sentinel;
 CHECK(app_plugin_highlight_next(&app,-2,&item)==-1 && !memcmp(&item,&sentinel,sizeof(item)),"negative invalid cursor cannot index before the snapshot");
 CHECK(app_plugin_highlight_next(&app,INT_MAX,&item)==-1 && !memcmp(&item,&sentinel,sizeof(item)),"large cursor cannot overflow into the snapshot");
#endif
 free(app.plugin_highlights);free(app.plugin_highlight_loc);
 printf("highlight snapshot: %d checks, %d failures\n",checks,failures);
 return failures?1:0;
}
'''
parts.append(suffix)
out=Path(args.out_dir).resolve();out.mkdir(parents=True,exist_ok=True)
label='baseline' if args.source_ref else 'current'
cfile=out/f'highlight_snapshot_{label}.c';binary=cfile.with_suffix('')
cfile.write_text((''.join(parts)))
command=shlex.split(args.cc)+['-std=c11','-O1','-g','-Wall','-Wextra','-I'+str(repo/'src'),str(cfile),str(repo/'src/game/rs_highlight.c'),'-o',str(binary)]
if growing: command.append('-DGROWING_SNAPSHOT=1')
if args.sanitize: command.extend(['-fsanitize=address,undefined','-fno-omit-frame-pointer'])
subprocess.run(command,check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True)
receipt=f'Bridge source SHA256 {hashlib.sha256(source.encode()).hexdigest()}\n'+result.stdout+result.stderr+f'Exit {result.returncode}\n'
(out/f'highlight_snapshot_{label}.receipt').write_text(receipt)
print(receipt,end='')
raise SystemExit(result.returncode)
