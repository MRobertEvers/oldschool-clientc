#!/usr/bin/env python3
"""Exercise presenter geometry and host input dispatch before plugin painting."""
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "src/plugin/torirs_plugin_panel.u.c").read_text()
start = source.index("static struct ToriRS_PanelWidget const*\napp_plugin_panel_model_for_row(")
end = source.index("/**\n * Native editors/toggles", start)
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
before = r'''
#define main unused_host_test_main
#include "plugin/test/torirs_plugin_host_test.c"
#undef main
#include "ui/uitree_debug_overlay.h"
#include "ui/torirs_chrome_exec_kind.h"
#define APP_PLUGIN_ROW_PANEL_WIDGET 1
struct AppPluginPanelRow { int kind,plugin,widget,widget_kind; uint32_t widget_serial; char widget_id[64]; };
struct TestRailLayout { int visible,width,custom_width; uint32_t selection_generation,page_generation; };
struct App { struct ToriRS_PluginHost* plugins; struct ToriRSChrome plugin_ui; uint32_t plugin_panel_built_generation; uint64_t plugin_panel_intent_sequence; int plugin_exec_kind,plugin_rail_has_layout; struct TestRailLayout plugin_rail_layout; struct { uint32_t selection_generation; } plugin_shell; };
static struct ToriRS_PanelActionEvent received;
static int actions,draws;
static void receive_action(struct ToriRS_Api* api,void* state,struct ToriRS_PanelActionEvent const* event) { (void)api;(void)state;received=*event;actions++; }
static void receive_draw(struct ToriRS_Api* api,void* state,char const* id,struct ToriRS_Graphics* draw) { (void)api;(void)state;(void)id;(void)draw;draws++; }
'''
after = r'''
int main(void) {
 static struct App app;
 struct ToriRS_PluginEngine engine=fake_engine();
 struct ToriRS_PluginDef def=PIN_PROBE;
 def.callbacks.on_ui_action=receive_action;def.callbacks.on_ui_draw=receive_draw;
 app.plugins=PluginHost_New(&engine);int p=PluginHost_Register(app.plugins,&def);PluginHost_Start(app.plugins);
 assert(PluginHost_PanelSelect(app.plugins,p));
 app.plugin_panel_built_generation=PluginHost_PanelSelectionGeneration(app.plugins);
 struct ToriRS_PanelWidget const* model=PluginHost_PanelWidgetAt(app.plugins,app.plugin_panel_built_generation,0);
 ToriRSChrome_Init(&app.plugin_ui);app.plugin_exec_kind=TORIRS_CHROME_EXEC_BUFFER;
 int panel=ToriRSChrome_PanelAdd(&app.plugin_ui,TORIRS_CHROME_PANEL_WINDOW,0,0,320,"Region");
 ToriRSChrome_PanelSetFixedHeight(&app.plugin_ui,panel,140);ToriRSChrome_PanelSetScrollable(&app.plugin_ui,panel,1);
 int custom=ToriRSChrome_Custom(&app.plugin_ui,panel,"",200);
 struct AppPluginPanelRow row={.kind=APP_PLUGIN_ROW_PANEL_WIDGET,.plugin=p,.widget=custom,.widget_kind=TORIRS_PANEL_WIDGET_CUSTOM,.widget_serial=model->serial};
 snprintf(row.widget_id,sizeof(row.widget_id),"%s",model->id);
 int first_width=0;
 for(int pass=0;pass<2;pass++) {
  int panel_width=pass?300:320;
  ToriRSChrome_PanelSetFixedWidth(&app.plugin_ui,panel,panel_width);ToriRSChrome_Build(&app.plugin_ui);
  struct ToriRSChromeRect region;assert(ToriRSChrome_CustomRegion(&app.plugin_ui,custom,&region,NULL));
  assert(region.w<panel_width);assert(region.w>0);
  assert(PluginHost_PanelLayout(app.plugins,app.plugin_panel_built_generation,panel_width,140,1000,TORIRS_PANEL_SIZE_MEDIUM,true,true));
  assert(ToriRSChrome_CustomActivate(&app.plugin_ui,custom,region.w-80,20));
  assert(ToriRSChrome_TakeActivated(&app.plugin_ui)==custom);
  int x,y;uint32_t generation,serial;
  assert(ToriRSChrome_ActivationWasCustom(&app.plugin_ui,&x,&y,&generation,&serial));
  assert(app_plugin_panel_dispatch_row(&app,&row,TORIRS_PANEL_ACTION_ACTIVATE,-1,"",x,y));
  assert(received.region_width==region.w);assert(received.region_height==region.h);assert(draws==0);
  if(pass) assert(received.region_width!=first_width);else first_width=received.region_width;
 }
 int surface=0;assert(PluginHost_PanelDraw(app.plugins,app.plugin_panel_built_generation,model->serial,&surface,0,0,received.region_width,received.region_height));
 assert(draws==1);int width=received.region_width;
 assert(app_plugin_panel_dispatch_row(&app,&row,TORIRS_PANEL_ACTION_ACTIVATE,-1,"",20,20));assert(received.region_width==width);
 app.plugin_exec_kind=TORIRS_CHROME_EXEC_BROWSER;app.plugin_rail_has_layout=1;app.plugin_shell.selection_generation=7;
 app.plugin_rail_layout=(struct TestRailLayout){.visible=1,.width=320,.custom_width=278,.selection_generation=7,.page_generation=app.plugin_panel_built_generation};
 assert(app_plugin_panel_dispatch_row(&app,&row,TORIRS_PANEL_ACTION_ACTIVATE,-1,"",197,20));assert(received.region_width==278);
 int count=actions;app.plugin_rail_layout.page_generation++;
 assert(!app_plugin_panel_dispatch_row(&app,&row,TORIRS_PANEL_ACTION_ACTIVATE,-1,"",197,20));assert(actions==count);
 PluginHost_Free(app.plugins);
 puts("panel region: real retained activation after layout, before first draw and after resize; painted control and stale presenter fence passed");
}
'''
test_source = out / "plugin_panel_region_test.c"
test_source.write_text(before + source[start:end] + after)
includes = ["src", "src/cs2vm2", "src/serverscript", ".", "3rd/rscache/include", "3rd/rscache/src", "3rd/toridraw", "3rd/trspk", "3rd/bmp", "3rd/hmap", "3rd/rsareabuf", "3rd/miniz", "3rd/xteas", "3rd/stb", "3rd/rsprot/src", "3rd/rsprot/include", "3rd/rsprot"]
implementation = ["src/plugin/torirs_plugin_host.c", "src/plugin/torirs_plugin_frame.c", "src/ui/uitree_minimenu.c", "src/revconfig/revconfig.c", "src/ui/torirs_chrome_inkwell.c", "src/ui/uitree_debug_overlay.c"]
binary = out / "plugin_panel_region_test"
command = shlex.split(os.environ.get("CC", "cc")) + ["-std=c11", "-UNDEBUG", "-Werror=incompatible-pointer-types"]
command += [f"-I{root / p}" for p in includes] + [str(test_source)] + [str(root / p) for p in implementation] + ["-lm", "-o", str(binary)]
subprocess.run(command, check=True)
subprocess.run([str(binary)], check=True)
