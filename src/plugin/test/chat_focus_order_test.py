#!/usr/bin/env python3
"""Exercise actual plugin focus + App pointer-focus order for both default paths."""
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
app = (root / "src/app.c").read_text()
bridge = (root / "src/plugin/torirs_plugin_bridge.u.c").read_text()
focus = app[app.index("static int\napp_chat_focus_tick("):app.index("/*\n * Rebuild the chat presentation")]
request = bridge[bridge.index("static void\napp_plugin_chat_focus("):bridge.index("static int\napp_plugin_if_click(")]
ownership = []
for marker in ["/* A retained control owns this press", "/* The world default list can contain"]:
    start = app.index(marker)
    ownership.append(app[start:app.index("struct UIMinimenu saved", start)])
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
before = r'''
#include <assert.h>
#include <stdio.h>
#define TORIRSM_LEFT 0
#define TORIRS_OSRSKEY_ESCAPE 13
#define TORIRS_OSRSKEY_ENTER 84
#define RS_MINIMENU_ACTION_PLUGIN_WIDGET 777
struct LibToriRS_Input { int down,key_event_count; struct { int mouse_x,mouse_y; } curr; struct { int key_typed; } key_events[4]; };
struct App { int* tree; int chat_input_active,need_redraw,locedit_visible,text_input_effective,chat_visible,iface_focus; struct { int social_input_open,dialog_input_open; } chat; };
struct UIMinimenu { struct { int action; } options[1]; };
static int app_chat_node_index(struct App* a) { return a->chat_visible?0:-1; }
static int UITree_InputFocusId(int* tree) { (void)tree;return -1; }
static int app_iface_text_input_focused(struct App* a) { return a->iface_focus; }
static int app_point_in_chat(struct App* a,int x,int y) { (void)a;return x>=100 && y>=100; }
static int LibToriRS_Input_IsMouseDown(struct LibToriRS_Input* in,int button) { assert(button==0);return in->down; }
'''
wrappers = ""
for i, code in enumerate(ownership):
    wrappers += "static int ownership%d(int action) { struct UIMinimenu scratch={0}; scratch.options[0].action=action; int default_idx=0,plugin_pointer_consumed=0;\n" % i + code + "return plugin_pointer_consumed;}\n"
after = r'''
int main(void)
{
    for(int route=0;route<2;route++) {
        int (*owns)(int)=route?ownership1:ownership0;
        struct App app={.chat_visible=1};struct LibToriRS_Input input={.down=1,.curr={62,26}};int submit;
        int consumed=owns(RS_MINIMENU_ACTION_PLUGIN_WIDGET);
        app_plugin_chat_focus(&app,1);
        assert(app.chat_input_active && app.text_input_effective==-1);
        app_chat_focus_tick(&app,&input,consumed,&submit);
        assert(app.chat_input_active && !submit);
        input.down=0;input.key_event_count=1;input.key_events[0].key_typed=-1;
        app_chat_focus_tick(&app,&input,0,&submit);
        assert(app.chat_input_active && !submit); /* typing still belongs to chat */
        input.down=1;input.key_event_count=0;
        app_chat_focus_tick(&app,&input,owns(1),&submit);
        assert(!app.chat_input_active); /* ordinary outside click still blurs */
        app_plugin_chat_focus(&app,1);app_plugin_chat_focus(&app,0);
        app_chat_focus_tick(&app,&input,consumed,&submit);assert(!app.chat_input_active);
        app.chat_visible=0;app_plugin_chat_focus(&app,1);assert(!app.chat_input_active);
    }
    puts("chat focus order: owned UI/world default presses retain callback focus; typing, outside blur, explicit off and missing-chat controls passed");
}
'''
test = out / "chat_focus_order_test.c"
test.write_text(before + request + focus + wrappers + after)
binary = out / "chat_focus_order_test"
subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                str(test), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
