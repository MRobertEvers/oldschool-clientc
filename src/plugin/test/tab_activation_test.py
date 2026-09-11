#!/usr/bin/env python3
"""Compile the real native activation seam with real owned hook snapshots."""
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "src/plugin/torirs_plugin_bridge.u.c").read_text()
start = source.index("static int\napp_plugin_tab_activate(")
end = source.index("static int\napp_plugin_stat(", start)
interact = (root / "src/ui/uitree_interact.c").read_text()
resolver_start = interact.index("static struct UITreeRuntimeScriptHook const*\nresolve_click_hook(")
resolver_end = interact.index("/* Innermost vertically-scrollable", resolver_start)
resolver = interact[resolver_start:resolver_end]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
before = r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ui/uitree_hook.h"
#define RS_UI_SLOTS_TAB_MAX 14
#define APP_UI_LOGIC_CS2 2
struct UITreeComponent { int component_id,parent; struct UITreeRuntimeHooks hooks; };
struct UITree { uint32_t component_count; struct UITreeComponent components[3]; };
struct App { struct UITree* tree; int logic,given,node,host,runner,need_redraw,revconfig_refs; };
static int selections,dispatches,event_op,dispatched_component;
static int App_UiLogic(struct App* app) { return app->logic; }
static int RS_UISlots_TabGiven(struct App* app,int tab) { return app->given && tab==3; }
static int app_plugin_tab_select(void* user,int tab)
{ (void)user;assert(tab==3);selections++;return 1; }
static int RevConfigRefs_Get(int* refs,char const* kind,char const* name)
{ assert(strcmp(kind,"enum")==0);assert(strcmp(name,"sidebar_buttons")==0);return *refs; }
static int app_plugin_frame_root(struct App* app) { (void)app;return 99; }
static int app_plugin_frame_role_enum_id(struct App* app,int root,int* control)
{ (void)app;assert(root==99);*control=1000;return 77; }
static int app_plugin_frame_enum_value(struct App* app,int table,int key,int* value)
{ if(table==88 && key==3) { *value=555;return 1; } if(table==77 && key==555) { *value=app->node<0?-1:654321;return 1; } return 0; }
static int UITree_FindByComponentId(struct UITree* tree,int uid)
{ for(uint32_t i=0;i<tree->component_count;i++) if(tree->components[i].component_id==uid) return (int)i;return -1; }
static struct UITreeRuntimeHooks const* UITree_Hooks(struct UITreeComponent const* node) { return &node->hooks; }
static void RS_CS2_SetEventOp(int* host,int op,int ignored)
{ (void)host;(void)ignored;event_op=op; }
static struct UITreeRuntimeScriptHook* live_hook;
static void RS_CS2_DispatchHook(int* host,int* runner,int component,struct UITreeRuntimeScriptHook const* hook)
{
    (void)host;(void)runner;assert(event_op==1);assert(hook!=live_hook);
    assert(hook->script_id==914);assert(hook->argc==3);
    /* The native callback may replace its hook. Its copied arguments must
     * survive that replacement, exactly as they do during real CS2 dispatch. */
    UITree_HookClear(live_hook);
    assert(UITree_HookArg(hook,1)==1129);assert(UITree_HookArg(hook,2)==3);
    dispatched_component=component;dispatches++;
}
'''
after = r'''
int main(void)
{
    struct UITree tree={.component_count=3};struct App app={.tree=&tree,.logic=1,.given=1,.node=0,.revconfig_refs=88};
    assert(app_plugin_tab_activate(&app,3));assert(selections==1 && dispatches==0);
    assert(app_plugin_tab_activate(&app,3));assert(selections==2 && dispatches==0);
    app.logic=APP_UI_LOGIC_CS2;tree.components[0].component_id=123456;
    tree.components[0].parent=2;tree.components[1].parent=2;tree.components[1].component_id=654321;
    tree.components[2].parent=-1;tree.components[2].component_id=987654;
    live_hook=&tree.components[1].hooks.on_op;
    assert(!app_plugin_tab_activate(&app,3));assert(dispatches==0);
    int args[]={-2147483644,1129,3};
    UITree_HookSet(live_hook,914,args,3,0,NULL,0);
    app.given=0;assert(!app_plugin_tab_activate(&app,3));assert(live_hook->script_id==914);
    app.given=1;app.revconfig_refs=-1;assert(!app_plugin_tab_activate(&app,3));
    app.revconfig_refs=88;app.node=-1;assert(!app_plugin_tab_activate(&app,3));
    app.node=0;assert(!app_plugin_tab_activate(&app,-1));assert(!app_plugin_tab_activate(&app,14));
    assert(app_plugin_tab_activate(&app,3));
    assert(dispatches==1 && selections==2 && dispatched_component==654321 && app.need_redraw);
    assert(live_hook->script_id==0);
    /* Some native controls bind on_click rather than on_op. Use exactly the
     * normal dispatch resolver, including its parent and hook-kind policy. */
    live_hook=&tree.components[1].hooks.on_click;
    UITree_HookSet(live_hook,914,args,3,0,NULL,0);
    assert(app_plugin_tab_activate(&app,3));assert(dispatches==2 && dispatched_component==654321);
    live_hook=&tree.components[2].hooks.on_op;
    UITree_HookSet(live_hook,914,args,3,0,NULL,0);
    assert(app_plugin_tab_activate(&app,3));assert(dispatches==3 && dispatched_component==987654);
    puts("tab activation: declared sibling button, readiness, direct/inherited native hook, arguments, deep snapshot and legacy selection passed");
}
'''
test = out / "tab_activation_test.c"
test.write_text(before + resolver + source[start:end] + after)
binary = out / "tab_activation_test"
subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                "-I" + str(root / "src"), str(test), str(root / "src/ui/uitree_hook.c"),
                "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
