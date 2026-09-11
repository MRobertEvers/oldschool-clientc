#!/usr/bin/env python3
"""Run the actual generated-settings Save against the real host validator."""
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "src/plugin/torirs_plugin_panel.u.c").read_text()
value_start = source.index("static char const*\napp_plugin_panel_config_value(")
value_end = source.index("/* A canonical snapshot", value_start)
save_start = source.index("static void\napp_plugin_panel_save(")
save_end = source.index("/** Throw away this tab", save_start)
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
before = r'''
/* Reuse the host suite's engine fixture; no config parser is mocked here. */
#define main unused_host_suite_main
#include "plugin/test/torirs_plugin_host_test.c"
#undef main
#define APP_PLUGIN_ROW_CONFIG 1
struct FakeSaveUi { char text[2][512]; int checked[2]; };
struct AppPluginPanelRow { int kind,plugin,cfg_index,widget; };
struct App { struct ToriRS_PluginHost* plugins; struct FakeSaveUi plugin_ui; int plugin_panel_row_count; struct AppPluginPanelRow plugin_panel_rows[2]; };
static int ToriRSChrome_Checked(struct FakeSaveUi* ui,int w) { return ui->checked[w]; }
static char const* ToriRSChrome_Text(struct FakeSaveUi* ui,int w) { return ui->text[w]; }
static char const* app_plugin_dropdown_value(struct App* app,int w) { return app->plugin_ui.text[w]; }
static int saved_hotkey,save_starts;
static void save_start(struct ToriRS_Api* api,void* state) { save_starts++;pin_start(api,state); }
static void save_tick(struct ToriRS_Api* api,void* state,struct ToriRS_TickEvent const* event) {
 pin_logic(api,state,event);assert(api->config.get_int(api,"hotkey",&saved_hotkey));
}
'''
after = r'''
int main(void) {
 struct ToriRS_PluginEngine engine=fake_engine();
 struct App app={.plugins=PluginHost_New(&engine),.plugin_panel_row_count=2};
 struct ToriRS_PluginDef def=PIN_PROBE;
 def.callbacks.on_start=save_start;def.callbacks.on_logic_tick=save_tick;
 int p=PluginHost_Register(app.plugins,&def);PluginHost_Start(app.plugins);
 for(int i=0;i<2;i++) { app.plugin_panel_rows[i].kind=APP_PLUGIN_ROW_CONFIG;app.plugin_panel_rows[i].plugin=p;app.plugin_panel_rows[i].widget=i; }
 for(int i=0;i<PluginHost_ConfigCount(app.plugins,p);i++) {
  char const* key=PluginHost_ConfigItem(app.plugins,p,i)->key;
  if(!strcmp(key,"rows")) app.plugin_panel_rows[0].cfg_index=i;
  if(!strcmp(key,"hotkey")) app.plugin_panel_rows[1].cfg_index=i;
 }
 strcpy(app.plugin_ui.text[0],"12");
 char const* expressions[]={"1 << 4","0x20"};int expected[]={16,32};
 for(int i=0;i<2;i++) {
  strcpy(app.plugin_ui.text[1],expressions[i]);app_plugin_panel_save(&app,p);PluginHost_LogicTick(app.plugins,1);
  assert(saved_hotkey==expected[i]);assert(!strcmp(PluginHost_ConfigGet(app.plugins,p,"hotkey"),expressions[i]));
  assert(!strcmp(app.plugin_ui.text[1],expressions[i]));
 }
 char const* invalid[]={"112","12 rows"};
 for(int i=0;i<2;i++) {
  strcpy(app.plugin_ui.text[0],"33");strcpy(app.plugin_ui.text[1],invalid[i]);
  PluginHost_ConfigClearDirty(app.plugins);int starts=save_starts;
  app_plugin_panel_save(&app,p);
  assert(!strcmp(PluginHost_ConfigGet(app.plugins,p,"rows"),"12"));
  assert(!strcmp(PluginHost_ConfigGet(app.plugins,p,"hotkey"),"0x20"));
  assert(!PluginHost_ConfigDirty(app.plugins));assert(save_starts==starts);
  assert(!strcmp(app.plugin_ui.text[1],invalid[i]));
 }
 PluginHost_Free(app.plugins);
 puts("settings Save: expressions evaluate to 16/32 unchanged; invalid range/text refuses all writes and reload");
}
'''
test_source = out / "plugin_settings_save_test.c"
test_source.write_text(before + source[value_start:value_end] + source[save_start:save_end] + after)
includes = ["src", "src/cs2vm2", "src/serverscript", ".", "3rd/rscache/include", "3rd/rscache/src", "3rd/toridraw", "3rd/trspk", "3rd/bmp", "3rd/hmap", "3rd/rsareabuf", "3rd/miniz", "3rd/xteas", "3rd/stb", "3rd/rsprot/src", "3rd/rsprot/include", "3rd/rsprot"]
implementation = ["src/plugin/torirs_plugin_host.c", "src/plugin/torirs_plugin_frame.c", "src/ui/uitree_minimenu.c", "src/revconfig/revconfig.c", "src/ui/torirs_chrome_inkwell.c"]
binary = out / "plugin_settings_save_test"
command = shlex.split(os.environ.get("CC", "cc")) + ["-std=c11", "-UNDEBUG", "-Werror=incompatible-pointer-types"]
command += [f"-I{root / p}" for p in includes] + [str(test_source)] + [str(root / p) for p in implementation] + ["-lm", "-o", str(binary)]
subprocess.run(command, check=True)
subprocess.run([str(binary)], check=True)
