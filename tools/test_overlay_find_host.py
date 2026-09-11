#!/usr/bin/env python3
"""Exercise the actual hosted202/203 branches against primary/secondary nodes."""
from pathlib import Path
import argparse,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source-ref');a=p.parse_args()
root=Path(__file__).resolve().parents[1]
source=subprocess.check_output(['git','show',a.source_ref+':src/game/rs_cs2_host.c'],cwd=root,text=True) if a.source_ref else (root/'src/game/rs_cs2_host.c').read_text()
start=source.index('    /* ---- decorate ------------------------------------------------------ */')
start=source.index('    case CS2_OP_OVERLAY_FIND:',start)
end=source.index('    case CS2_OP_OVERLAY_CC_CREATE:',start)
prefix='\n#include <stdio.h>\n#include <stdint.h>\n#include <stdbool.h>\nenum {CS2_OP_OVERLAY_FIND=202,CS2_OP_OVERLAY_CC_FIND=203};\nstruct UITree {struct {int component_id;} components[2];int root_live,child_live;};\nstruct VM {int value,target[2];};\nstruct Host {int component;};\nstatic int rs_cs2_overlay_component_id(struct Host* host,int index) {return index==41?host->component:-1;}\nstatic int UITree_FindByComponentId(struct UITree* tree,int component) {return tree->root_live&&tree->components[0].component_id==component?0:-1;}\nstatic int UITree_FindChildBySubid(struct UITree* tree,int parent,int component,int sub) {(void)parent;(void)component;return tree->child_live&&sub==0?1:-1;}\nstatic void rs_cs2_set_cc_target(struct VM* vm,int dot,int component) {vm->target[dot]=component;}\nstatic int CS2VM2_PushInt(struct VM* vm,int value) {vm->value=value;return 0;}\nstatic int query(struct Host* host,struct UITree* tree,struct VM* vm,int opcode,int const* a,int dot_operand)\n{switch(opcode){\n'
suffix='\ndefault:return -1;}}\nint main(void){\n int failures=0;\n for(int dot=0;dot<=1;++dot){\n  struct Host host={123};struct UITree tree={.components={{123},{456}},.root_live=1};\n  struct VM vm={.target={999,999}};int args[2]={41,0};\n  query(&host,&tree,&vm,202,args,dot);\n  if(vm.value!=1||vm.target[dot]!=123||vm.target[1-dot]!=999){puts("FAIL202 empty root selection");++failures;}\n  query(&host,&tree,&vm,203,args,dot);\n  if(vm.value!=0||vm.target[dot]!=123){puts("FAIL203 absent child changed target");++failures;}\n  tree.child_live=1;query(&host,&tree,&vm,203,args,dot);\n  if(vm.value!=1||vm.target[dot]!=456||vm.target[1-dot]!=999){puts("FAIL203 child lookup selected root");++failures;}\n  args[1]=7;query(&host,&tree,&vm,203,args,dot);\n  if(vm.value!=0||vm.target[dot]!=456){puts("FAIL203 missing subid changed target");++failures;}\n  tree.root_live=0;args[1]=0;\n  for(int op=202;op<=203;++op){query(&host,&tree,&vm,op,args,dot);\n   if(vm.value!=0||vm.target[dot]!=456){puts("FAIL absent overlay target");++failures;}}\n }\n printf("native host202root203child: %d failures\\n",failures);return failures?1:0;\n}\n'
with tempfile.TemporaryDirectory(prefix='overlay-find-host-') as tmp:
 c=Path(tmp)/'host.c';exe=Path(tmp)/'host';c.write_text(prefix+source[start:end]+suffix)
 subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Wno-unused-function',str(c),'-o',str(exe)],check=True)
 raise SystemExit(subprocess.run([str(exe)]).returncode)
