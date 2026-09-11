#!/usr/bin/env python3
"""Exercise the actual generated-settings merge helpers against staged UI values."""
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "src/plugin/torirs_plugin_panel.u.c").read_text()
start = source.index("static void\napp_plugin_panel_load_row(")
end = source.index("/*\n * Rebuild the whole window", start)
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
before = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define TORIRS_PLUGIN_CONFIG_VALUE_MAX 192
#define APP_PLUGIN_ROW_CONFIG 1
#define TORIRS_CONFIG_BOOL 0
#define TORIRS_CONFIG_COLOR 2
#define TORIRS_CONFIG_ENUM 4
#define TORIRS_CHROME_W_DROPDOWN 3
struct ToriRS_ConfigItem { int type; char const* key; };
struct ToriRSChromeWidget { int kind,option_count; char const** options; };
struct FakeUi { char value[512]; int checked,loads,widget_count; struct ToriRSChromeWidget widgets[1]; };
struct AppPluginPanelRow { int kind,plugin,cfg_index,widget; char config_source[192],config_presented[192]; };
struct App { void* plugins; struct FakeUi plugin_ui; int plugin_panel_row_count; struct AppPluginPanelRow plugin_panel_rows[1]; char source[192]; int host_bool,bool_reads; };
static struct ToriRS_ConfigItem item={3,"setting"};
static struct ToriRS_ConfigItem const* PluginHost_ConfigItem(void* h,int p,int c) { (void)h;(void)p;(void)c;return &item; }
static int ToriRSChrome_Checked(struct FakeUi* ui,int w) { (void)w;return ui->checked; }
static char const* ToriRSChrome_Text(struct FakeUi* ui,int w) { (void)w;return ui->value; }
static char const* app_plugin_dropdown_value(struct App* app,int w) { (void)w;return app->plugin_ui.value; }
static char const* app_plugin_panel_value(struct App* app,int p,char const* k) { (void)p;(void)k;return app->source; }
/* Mock the authoritative typed host result, not its parser. The host suite
 * separately proves this accessor matches get_bool for spellings/fallbacks. */
static bool PluginHost_ConfigGetBool(void* h,int p,char const* k) { struct App* app=h;(void)p;(void)k;app->bool_reads++;return app->host_bool; }
static void ToriRSChrome_SetChecked(struct FakeUi* ui,int w,int v) { (void)w;ui->checked=v;ui->loads++; }
static void ToriRSChrome_SetText(struct FakeUi* ui,int w,char const* v) { (void)w;snprintf(ui->value,sizeof(ui->value),"%s",v);ui->loads++; }
static void ToriRSChrome_DropdownSetSelected(struct FakeUi* ui,int w,int n) { (void)ui;(void)w;(void)n;assert(!"unexpected dropdown widget"); }
static int app_plugin_choice_index(char const* const* v,int n,char const* x) { (void)v;(void)n;(void)x;return 0; }
static void ToriRSChrome_ColorPickSet(struct FakeUi* ui,int w,int c) { (void)ui;(void)w;(void)c;assert(!"unexpected color widget"); }
static int app_plugin_color_hsl(char const* t) { (void)t;return 0; }
'''
after = r'''
int main(void) {
 struct App app={.plugin_panel_row_count=1};struct AppPluginPanelRow* row=&app.plugin_panel_rows[0];row->kind=APP_PLUGIN_ROW_CONFIG;app.plugins=&app;
 strcpy(app.source,"old");strcpy(app.plugin_ui.value,"old");app_plugin_panel_config_remember(&app,row);
 app_plugin_panel_sync_config(&app);assert(app.plugin_ui.loads==0);
 strcpy(app.source,"external");app_plugin_panel_sync_config(&app);assert(app.plugin_ui.loads==1);assert(!strcmp(app.plugin_ui.value,"external"));
 strcpy(app.plugin_ui.value,"unsaved");strcpy(app.source,"second external");app_plugin_panel_sync_config(&app);assert(app.plugin_ui.loads==1);assert(!strcmp(app.plugin_ui.value,"unsaved"));
 app_plugin_panel_load_row(&app,row);app_plugin_panel_config_remember(&app,row);strcpy(app.source,"after revert");app_plugin_panel_sync_config(&app);assert(!strcmp(app.plugin_ui.value,"after revert"));
 memset(app.plugin_ui.value,'x',300);app.plugin_ui.value[300]=0;strcpy(app.source,"third external");app_plugin_panel_sync_config(&app);assert(strlen(app.plugin_ui.value)==300);
 item.type=TORIRS_CONFIG_BOOL;strcpy(app.source,"0");app.plugin_ui.checked=0;app_plugin_panel_config_remember(&app,row);strcpy(app.source,"true");app.host_bool=1;app_plugin_panel_sync_config(&app);assert(app.plugin_ui.checked==1);assert(app.bool_reads==1);assert(!strcmp(row->config_presented,"1"));
 /* A local edit is retained while an external spelling changes. Revert
  * still uses the same typed host read rather than interpreting raw text. */
 app.plugin_ui.checked=0;strcpy(app.source,"yes");app_plugin_panel_sync_config(&app);assert(app.plugin_ui.checked==0);assert(app.bool_reads==1);
 app_plugin_panel_load_row(&app,row);app_plugin_panel_config_remember(&app,row);assert(app.plugin_ui.checked==1);assert(app.bool_reads==2);
 strcpy(app.source,"false");app.host_bool=0;app_plugin_panel_sync_config(&app);assert(app.plugin_ui.checked==0);assert(app.bool_reads==3);
 item.type=TORIRS_CONFIG_ENUM;strcpy(app.source,"A");strcpy(app.plugin_ui.value,"A");app_plugin_panel_config_remember(&app,row);strcpy(app.source,"B");app_plugin_panel_sync_config(&app);assert(!strcmp(app.plugin_ui.value,"B"));
 puts("settings merge: actual row load, typed true/false, retained edits, revert, long input and enum passed");
}
'''
test_source = out / "plugin_settings_merge_test.c"
test_source.write_text(before + source[start:end] + after)
command = shlex.split(os.environ.get("CC", "cc")) + ["-std=c11", "-Wall", "-Wextra", "-UNDEBUG", str(test_source), "-o", str(out / "plugin_settings_merge_test")]
subprocess.run(command, check=True)
subprocess.run([str((out / "plugin_settings_merge_test").resolve())], check=True)
