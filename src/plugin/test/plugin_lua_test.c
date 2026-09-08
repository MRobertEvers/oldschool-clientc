#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_lua.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLUGIN_LUA_TEST_MAX 32

struct ToriRS_PluginHost { int unused; };

static struct ToriRS_PluginDef const* g_defs[PLUGIN_LUA_TEST_MAX];
static struct ToriRS_PluginDef g_def_storage[PLUGIN_LUA_TEST_MAX];
static void (*g_reload[PLUGIN_LUA_TEST_MAX])(struct ToriRS_PluginHost*, int, void*);
static void* g_reload_user[PLUGIN_LUA_TEST_MAX];
static int g_registered;
static int g_failures;
static int g_reported_errors;
static int g_logs;
static int g_enabled_calls;
static int g_draws;
static int g_headings;
static int g_action_rows;
static int g_surfaces;
static int g_disabled_self;
static int g_config_dispatches;
static char g_config_value[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
static int g_stroke_calls;
static char g_disable_reason[192];

#define CHECK(condition, message)                                                       \
    do                                                                                  \
    {                                                                                   \
        if( !(condition) )                                                              \
        {                                                                               \
            fprintf(stderr, "lua plugin test: %s\n", (message));                          \
            g_failures++;                                                               \
        }                                                                               \
    } while( 0 )

int
PluginHost_Register(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PluginDef const* def)
{
    (void)host;
    CHECK(g_registered < PLUGIN_LUA_TEST_MAX, "registration table capacity");
    if( g_registered >= PLUGIN_LUA_TEST_MAX ) return -1;
    /* Registration copies callbacks in the real host. Keeping its caller's
     * pointer here would conceal a reload that failed to refresh that copy. */
    g_def_storage[g_registered] = *def;
    g_defs[g_registered] = &g_def_storage[g_registered];
    return g_registered++;
}

void
PluginHost_SetReloadHandler(
    struct ToriRS_PluginHost* host,
    int index,
    void (*handler)(struct ToriRS_PluginHost*, int, void*),
    void* user)
{
    (void)host;
    g_reload[index] = handler;
    g_reload_user[index] = user;
}

void
PluginHost_SetCallbacks(
    struct ToriRS_PluginHost* host,
    int index,
    struct ToriRS_PluginCallbacks const* callbacks)
{
    (void)host;
    g_def_storage[index].callbacks = *callbacks;
}

void PluginHost_SetError(struct ToriRS_PluginHost* host, int index, char const* text)
{
    (void)host; (void)index; (void)text;
    g_reported_errors++;
}
void PluginHost_SetEnabled(struct ToriRS_PluginHost* host, int index, bool enabled)
{
    (void)host; (void)index; (void)enabled;
    g_enabled_calls++;
}

struct FakeInstance { char const* id; char const* active_frame; };

static char const* fake_plugin_id(struct ToriRS_Api* api)
{
    return ((struct FakeInstance*)api->instance)->id;
}
static void fake_log(struct ToriRS_Api* api, char const* format, ...)
{
    (void)api; (void)format;
    g_logs++;
}
static bool fake_config_get_int(struct ToriRS_Api* api, char const* key, int* out)
{
    (void)api;
    if( strcmp(key, "answer") != 0 ) return false;
    *out = 42;
    return true;
}
static enum ToriRS_Result
fake_config_set(struct ToriRS_Api* api, char const* key, char const* value)
{
    char const* id = ((struct FakeInstance*)api->instance)->id;
    if( strlen(value) >= sizeof(g_config_value) ||
        strchr(value, '\n') || strchr(value, '\r') || strchr(key, '-') )
        return TORIRS_RESULT_INVALID;
    for( int i = 0; i < g_registered; i++ )
    {
        if( strcmp(g_defs[i]->id, id) != 0 ) continue;
        memcpy(g_config_value, value, strlen(value) + 1);
        if( g_defs[i]->callbacks.on_config_changed )
            g_defs[i]->callbacks.on_config_changed(api, NULL, key);
        g_config_dispatches++;
        return TORIRS_RESULT_OK;
    }
    return TORIRS_RESULT_NOT_FOUND;
}
static void fake_frame_selection(struct ToriRS_Api* api, struct ToriRS_FrameSelection* out)
{
    struct FakeInstance* instance = api->instance;
    snprintf(out->active_id, sizeof(out->active_id), "%s",
        instance->active_frame ? instance->active_frame : "");
}
static void fake_disable_self(struct ToriRS_Api* api, char const* reason)
{
    (void)api;
    CHECK(reason && reason[0], "disable_self receives an actionable reload reason");
    snprintf(g_disable_reason, sizeof(g_disable_reason), "%s", reason ? reason : "");
    g_disabled_self++;
}
static void fake_draw_rect(
    struct ToriRS_Graphics* draw,
    struct ToriRS_Rect rect,
    uint32_t rgb,
    int alpha)
{
    (void)draw; (void)rgb; (void)alpha;
    CHECK(rect.width == 3 && rect.height == 4, "draw builder forwards rectangle");
    g_draws++;
}
static void fake_heading(struct ToriRS_PanelBuilder* panel, char const* text)
{
    (void)panel;
    CHECK(strcmp(text, "Native V2") == 0, "panel builder forwards heading");
    g_headings++;
}
static void fake_action_row(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    char const* summary)
{
    (void)panel;
    CHECK(strcmp(id, "skill_8") == 0, "Lua action row forwards its stable id");
    CHECK(strcmp(label, "Woodcutting") == 0,
        "Lua action row forwards its primary label");
    CHECK(strcmp(summary, "XP/hr 12.3K") == 0,
        "Lua action row forwards its retained summary");
    g_action_rows++;
}
static bool fake_surface_native_size(struct ToriRS_Api* api, int surface, int* w, int* h)
{
    (void)api;
    CHECK(surface == TORIRS_SURFACE_CHAT, "frame surface name mapped");
    *w = 519;
    *h = 165;
    g_surfaces++;
    return true;
}

static struct ToriRS_Api
fake_api(struct FakeInstance* instance)
{
    struct ToriRS_Api api;
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    api.major_version = TORIRS_PLUGIN_API_MAJOR;
    api.minor_version = TORIRS_PLUGIN_API_MINOR;
    api.instance = instance;
    api.core.struct_size = sizeof(api.core);
    api.core.log = fake_log;
    api.core.plugin_id = fake_plugin_id;
    api.config.struct_size = sizeof(api.config);
    api.config.get_int = fake_config_get_int;
    api.config.set = fake_config_set;
    api.frame.struct_size = sizeof(api.frame);
    api.frame.selection = fake_frame_selection;
    api.frame.surface_native_size = fake_surface_native_size;
    static struct ToriRS_ClientApi client;
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.disable_self = fake_disable_self;
    api.client = &client;
    return api;
}

static char*
read_file(char const* path, int* size)
{
    FILE* file = fopen(path, "rb");
    char* bytes;
    long length;
    if( !file ) return NULL;
    if( fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0 )
    {
        fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length);
    if( !bytes ) abort();
    if( fread(bytes, 1, (size_t)length, file) != (size_t)length )
    {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (int)length;
    return bytes;
}

static void
reset_fake(void)
{
    memset(g_defs, 0, sizeof(g_defs));
    memset(g_def_storage, 0, sizeof(g_def_storage));
    memset(g_reload, 0, sizeof(g_reload));
    memset(g_reload_user, 0, sizeof(g_reload_user));
    g_registered = 0;
}

static void
test_runtime(struct ToriRS_PluginHost* host)
{
    static char const LEGACY_SOURCE[] = "return { name='legacy' }";
    static char const BUILDER_OFFER_SOURCE[] =
        "return {id='builder-offer',frames={{id='old',title='Old',canvas='fixed',"
        "width=765,height=503,build=function() return 'ready' end}}}";
    static char const DRAW_OFFER_SOURCE[] =
        "return {id='draw-offer',frames={{id='old',title='Old',canvas='fixed',"
        "width=765,height=503,draw=function() end}}}";
    static char const INVALID_ENUM_SOURCE[] =
        /* Deliberately collides with lua-v2-test in the 64-slot id table. */
        "return {id='invalid-enum-59',on_start=function(api) api.frame.surface_native_size(99) end}";
    static char const BUDGET_SOURCE[] =
        "return {id='budget-after-reentry',config={{key='answer',type='int',default='42'}},"
        "on_config_changed=function() local n=0;"
        " for i=1,10000000 do n=n+i end end,on_start=function(api)"
        " api.config.set('answer',43);api.core.log('must not run') end}";
    static char const NESTED_FAULT_SOURCE[] =
        "return {id='nested-fault',config={{key='answer',type='int',default='42'}},"
        "on_config_changed=function() error('inner boom') end,"
        "on_start=function(api) assert(api.config.set('answer',43));"
        "api.core.log('outer remained safe') end,"
        "on_key=function(api) assert(api.config.set('answer',43));return 'consume' end}";
    static char const DISABLE_SOURCE[] =
        "return {id='explicit-disable',on_start=function(api)"
        "api.client.disable_self('requested stop');api.core.log('must not run') end}";
    static char const BAD_PIXELS_SOURCE[] =
        "return {id='bad-pixels',on_start=function(api)"
        "api.assets.image_compose('x',2,2,{1,'bad',3,4}) end}";
    static char const DRIFT_SOURCE[] =
        "return {id='lua-v2-test',title='Lua V2 Test',version='2',"
        "frames={{id='changed',title='Changed',canvas='fixed',width=765,height=503}},"
        "on_gameframe=function() return 'ready' end}";
    static char const SOURCE[] =
        "return { id='lua-v2-test', title='Lua V2 Test', version='2',"
        " config={{key='answer',type='int',default='42'}},"
        " frames={"
        "  {id='ready',title='Ready',canvas='fixed',width=765,height=503},"
        "  {id='waiting',title='Waiting',canvas='window',min_width=640,min_height=480},"
        "  {id='failure',title='Failure',canvas='fixed',width=765,height=503},"
        "  {id='invalid-surface',title='Invalid Surface',canvas='fixed',width=765,height=503}"
        " },"
        " on_gameframe=function(api,ev)"
        "  if not ev.active then return end"
        "  if ev.offer_id=='ready' then"
        "   assert(ev.canvas=='fixed' and ev.width==765 and ev.height==503)"
        "   assert(ev.safe.x==0 and ev.safe.y==0 and ev.safe.width==765 and ev.safe.height==420 and ev.safe.h==420)"
        "   local w,h=api.frame.surface_native_size('chat');assert(w==519 and h==165)"
        "   return 'ready'"
        "  elseif ev.offer_id=='waiting' then return 'pending','still loading'"
        "  elseif ev.offer_id=='failure' then error('boom')"
        "  else api.frame.surface_native_size(99) end"
        " end,"
        " on_start=function(api)"
        "  assert(api.log==nil and api.role==nil and api.window==nil and api.layout==nil)"
        "  assert(api.chrome==nil and api.entity==nil and api.object_create==nil)"
        "  assert(api.local_player==nil and api.image_load==nil and api.cfg_set==nil)"
        "  assert(pcall==nil and xpcall==nil and setmetatable==nil and getmetatable==nil)"
        "  assert(warn==nil)"
        "  assert(api.core.plugin_id()=='lua-v2-test' and api.config.answer==42)"
        "  local ok,status=api.config.set('answer','bad\\nvalue')"
        "  assert(not ok and status=='invalid')"
        "  ok,status=api.config.set('bad-key','1')"
        "  assert(not ok and status=='invalid')"
        "  assert(api.ui==nil and api.placement==nil)"
        "  api.core.log('started')"
        " end,"
        " on_config_changed=function(api,key)"
        "  assert(key=='answer' and api.core.plugin_id()=='lua-v2-test')"
        "  api.core.log('changed')"
        " end,"
        " on_draw_canvas=function(api,draw)"
        "  assert(api.config.set('answer',43));draw.rect(1,2,3,4,0xffffff,255)"
        " end,"
        " on_ui_build=function(api,panel,view) assert(view=='page');"
        "  panel.heading('Native V2');panel.action_row('skill_8','Woodcutting','XP/hr 12.3K') end"
        "}";
    struct FakeInstance instance = { "lua-v2-test", "lua-v2-test/ready" };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_Graphics draw;
    struct ToriRS_PanelBuilder panel;
    struct ToriRS_GameframeEvent gameframe;
    char reason[TORIRS_FRAME_REASON_MAX];
    int index;

    CHECK(PluginLua_AddScript(host, "empty", "", 0) < 0,
        "empty source is refused without a debug assertion");
    CHECK(PluginLua_AddScript(host, "legacy", LEGACY_SOURCE,
              (int)strlen(LEGACY_SOURCE)) < 0,
        "legacy name field is not accepted as a V2 id");
    CHECK(PluginLua_AddScript(host, "builder-offer", BUILDER_OFFER_SOURCE,
              (int)strlen(BUILDER_OFFER_SOURCE)) < 0,
        "a frame offer with a declarative build function is refused");
    CHECK(PluginLua_AddScript(host, "draw-offer", DRAW_OFFER_SOURCE,
              (int)strlen(DRAW_OFFER_SOURCE)) < 0,
        "a frame offer with a declarative draw function is refused");
    index = PluginLua_AddScript(host, "lua-v2-test", SOURCE, (int)strlen(SOURCE));

    CHECK(index == 0, "runtime script registered through V2");
    CHECK(g_defs[0] && strcmp(g_defs[0]->id, "lua-v2-test") == 0, "Lua name is V2 id");
    CHECK(g_defs[0]->config && g_defs[0]->config->items, "V2 config schema retained");
    CHECK(g_defs[0]->frames != NULL, "V2 frame offers retained");
    CHECK(g_defs[0]->callbacks.on_gameframe != NULL, "V2 offers are served by on_gameframe");

    g_defs[0]->callbacks.on_start(&api, NULL);
    memset(&draw, 0, sizeof(draw));
    draw.struct_size = sizeof(draw);
    draw.rect = fake_draw_rect;
    g_defs[0]->callbacks.on_draw_canvas(&api, NULL, &draw);
    memset(&panel, 0, sizeof(panel));
    panel.struct_size = sizeof(panel);
    panel.heading = fake_heading;
    panel.action_row = fake_action_row;
    g_defs[0]->callbacks.on_ui_build(&api, NULL, &panel, TORIRS_PANEL_VIEW_PAGE);
    memset(&gameframe, 0, sizeof(gameframe));
    gameframe.active = true;
    gameframe.canvas = TORIRS_FRAME_CANVAS_FIXED;
    gameframe.width = 765;
    gameframe.height = 503;
    gameframe.reason = reason;
    gameframe.reason_capacity = sizeof(reason);
    gameframe.safe.width = 765;
    gameframe.safe.height = 420;
    gameframe.offer_id = "ready";
    CHECK(g_defs[0]->callbacks.on_gameframe(&api, NULL, &gameframe) == TORIRS_FRAME_READY,
        "Lua on_gameframe returns READY and reads the event's safe rect");
    gameframe.offer_id = "waiting";
    gameframe.canvas = TORIRS_FRAME_CANVAS_WINDOW;
    reason[0] = '\0';
    CHECK(g_defs[0]->callbacks.on_gameframe(&api, NULL, &gameframe) == TORIRS_FRAME_PENDING &&
              strcmp(reason, "still loading") == 0,
        "Lua on_gameframe returns PENDING with its reason");
    gameframe.offer_id = "failure";
    gameframe.canvas = TORIRS_FRAME_CANVAS_FIXED;
    CHECK(g_defs[0]->callbacks.on_gameframe(&api, NULL, &gameframe) == TORIRS_FRAME_ERROR,
        "a Lua error inside on_gameframe is ERROR");
    CHECK(g_disabled_self == 1, "the faulting provider is disabled through the V2 lifecycle");
    gameframe.offer_id = "invalid-surface";
    CHECK(g_defs[0]->callbacks.on_gameframe(&api, NULL, &gameframe) == TORIRS_FRAME_ERROR,
        "invalid integer surface becomes a caught Lua frame error");
    CHECK(g_disabled_self == 2 && g_reported_errors == 0 && g_enabled_calls == 0,
        "invalid frame enum refuses the provider through the V2 lifecycle");
    CHECK(g_logs == 2, "canonical and synchronously reentrant API calls reached V2 functions");
    CHECK(g_draws == 1 && g_headings == 1 && g_action_rows == 1,
        "scoped draw and native action-row builders reached V2 functions");
    CHECK(g_surfaces == 1, "frame.surface_native_size reached the V2 function");
    CHECK(g_config_dispatches == 1,
        "config.set synchronously dispatched on_config_changed once");
    CHECK(g_reload[0] != NULL, "source reload handler installed");
    g_reload[0](host, 0, g_reload_user[0]);
    g_defs[0]->callbacks.on_start(&api, NULL);
    CHECK(g_logs == 3, "reload rebuilt and restarted the VM");
    gameframe.offer_id = "ready";
    CHECK(g_defs[0]->callbacks.on_gameframe(&api, NULL, &gameframe) == TORIRS_FRAME_READY,
        "reload rebuilt the on_gameframe function");

    CHECK(PluginLua_TestReplaceSource(0, DRIFT_SOURCE, (int)strlen(DRIFT_SOURCE)),
        "test source replacement reached retained reload bytes");
    g_reload[0](host, 0, g_reload_user[0]);
    CHECK(strcmp(g_defs[0]->frames[0].id, "ready") == 0 &&
            strcmp(g_defs[0]->frames[3].id, "invalid-surface") == 0,
        "rejected reload restores catalogue-backed offer strings");
    g_defs[0]->callbacks.on_start(&api, NULL);
    CHECK(g_disabled_self == 3,
        "changed static frame descriptor is refused on restart");

    CHECK(PluginLua_AddScript(host, "invalid-enum-59", INVALID_ENUM_SOURCE,
              (int)strlen(INVALID_ENUM_SOURCE)) == 1,
        "enum misuse probe registered");
    struct FakeInstance invalid_instance = { "invalid-enum-59", NULL };
    struct ToriRS_Api invalid_api = fake_api(&invalid_instance);
    g_defs[1]->callbacks.on_start(&invalid_api, NULL);
    CHECK(g_disabled_self == 4 && g_reported_errors == 0 && g_enabled_calls == 0,
        "out-of-range integer enum becomes a caught V2 lifecycle fault");
    CHECK(strstr(g_disable_reason, "frame surface must be between") != NULL,
        "integer enum fault names the invalid domain");

    CHECK(PluginLua_AddScript(host, "budget-after-reentry", BUDGET_SOURCE,
              (int)strlen(BUDGET_SOURCE)) == 2,
        "nested-budget probe registered");
    struct FakeInstance budget_instance = { "budget-after-reentry", NULL };
    struct ToriRS_Api budget_api = fake_api(&budget_instance);
    g_defs[2]->callbacks.on_start(&budget_api, NULL);
    CHECK(g_disabled_self == 5 &&
            strstr(g_disable_reason, "instruction budget exhausted") != NULL,
        "nested callback cannot disarm the outer instruction budget");

    int const logs_before_nested_fault = g_logs;
    int const disables_before_nested_fault = g_disabled_self;
    CHECK(PluginLua_AddScript(host, "nested-fault", NESTED_FAULT_SOURCE,
              (int)strlen(NESTED_FAULT_SOURCE)) == 3,
        "nested-fault probe registered");
    struct FakeInstance nested_instance = { "nested-fault", NULL };
    struct ToriRS_Api nested_api = fake_api(&nested_instance);
    g_defs[3]->callbacks.on_start(&nested_api, NULL);
    CHECK(g_logs == logs_before_nested_fault + 1,
        "inner fault keeps native state alive until the outer Lua callback returns");
    CHECK(g_disabled_self == disables_before_nested_fault + 1 &&
            strstr(g_disable_reason, "inner boom") != NULL,
        "inner fault is disabled at the outer callback boundary");
    struct ToriRS_KeyEvent nested_key = { 0 };
    int const disables_before_key = g_disabled_self;
    CHECK(g_defs[3]->callbacks.on_key(&nested_api, NULL, &nested_key) ==
            TORIRS_CALLBACK_CONTINUE && g_disabled_self == disables_before_key + 1,
        "deferred nested fault cannot consume the triggering input");

    int const logs_before_disable = g_logs;
    CHECK(PluginLua_AddScript(host, "explicit-disable", DISABLE_SOURCE,
              (int)strlen(DISABLE_SOURCE)) == 4,
        "explicit-disable probe registered");
    struct FakeInstance disable_instance = { "explicit-disable", NULL };
    struct ToriRS_Api disable_api = fake_api(&disable_instance);
    g_defs[4]->callbacks.on_start(&disable_api, NULL);
    CHECK(g_logs == logs_before_disable && strstr(g_disable_reason, "requested stop") != NULL,
        "disable_self unwinds Lua before native teardown and later API use");

    CHECK(PluginLua_AddScript(host, "bad-pixels", BAD_PIXELS_SOURCE,
              (int)strlen(BAD_PIXELS_SOURCE)) == 5,
        "bad-pixels probe registered");
    struct FakeInstance pixels_instance = { "bad-pixels", NULL };
    struct ToriRS_Api pixels_api = fake_api(&pixels_instance);
    g_defs[5]->callbacks.on_start(&pixels_api, NULL);
    CHECK(strstr(g_disable_reason, "pixel 2 is not an integer") != NULL,
        "malformed pixel input faults without retaining a host allocation");
}

static ToriRS_WidgetListener lua_watch_listener;
static void* lua_watch_user;
static enum ToriRS_ContractResult fake_lua_watch(void* context, char const* role,
                                               ToriRS_WidgetListener listener, void* user)
{
    (void)context;
    CHECK(strcmp(role,"sidebar")==0,"Lua watch forwards its role");
    lua_watch_listener=listener;lua_watch_user=user;
    return TORIRS_CONTRACT_OK;
}
static int lua_action_calls;
static int lua_actions_mode;
static enum ToriRS_ContractResult fake_lua_actions(void* ctx,struct ToriRS_WidgetRef widget,
    struct ToriRS_WidgetAction* out,size_t capacity,size_t* count)
{
    (void)ctx;*count=2;
    if( lua_actions_mode == 1 ) { *count=0;return TORIRS_CONTRACT_NATIVE_BLOCKED; }
    if( lua_actions_mode == 2 && capacity >= 2 ) return TORIRS_CONTRACT_STALE_REFERENCE;
    if( lua_actions_mode == 3 ) { *count=0;return TORIRS_CONTRACT_OK; }
    if( capacity<2 ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
    out[0]=(struct ToriRS_WidgetAction){.ref={widget,1,3},.label="First"};
    out[1]=(struct ToriRS_WidgetAction){.ref={widget,2,UINT64_C(0xfedcba9876543210)},.label="Second"};
    return TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult fake_lua_invoke(void* ctx,struct ToriRS_WidgetActionRef action)
{
    (void)ctx;
    CHECK(action.operation==2 && action.revision==UINT64_C(0xfedcba9876543210) &&
        action.widget.opaque[0]==UINT64_C(0xfedcba9876543211),"Lua native action preserves full-width checked identity");
    ++lua_action_calls;return TORIRS_CONTRACT_STALE_REFERENCE;
}
static void test_widget_actions(struct ToriRS_PluginHost* host)
{
    static char const source[]=
        "local p={id='widget-actions'};function p.on_start(api) "
        "assert(api.widgets.watch('sidebar',function(widget,event) "
        "local actions=assert(widget:actions());assert(#actions==2 and actions[2].label=='Second');"
        "local ok,reason=api.widgets.invoke(actions[2].ref);assert(not ok and reason=='stale_reference');"
        "api.core.log('actions verified') end)) end;function p.on_key(api) api.widgets.invoke({}) end;return p";
    struct FakeInstance instance={"widget-actions",""};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.watch=fake_lua_watch;api.widgets.actions=fake_lua_actions;api.widgets.invoke=fake_lua_invoke;
    int index=PluginLua_AddScript(host,"widget-actions",source,(int)strlen(source));
    CHECK(index>=0,"native action script registers");
    g_defs[index]->callbacks.on_start(&api,NULL);
    struct ToriRS_WidgetEvent event={.type=TORIRS_WIDGET_BOUND,.widget={{UINT64_C(0xfedcba9876543211),2,3}},.role="sidebar"};
    int logs=g_logs;lua_watch_listener(&api,lua_watch_user,&event);
    CHECK(lua_action_calls==1 && g_logs==logs+1,"Lua actions retain native status");
    int disables=g_disabled_self;struct ToriRS_KeyEvent key={0};
    g_defs[index]->callbacks.on_key(&api,NULL,&key);
    CHECK(g_disabled_self==disables+1 && lua_action_calls==1 && strstr(g_disable_reason,"torirs.WidgetActionRef"),
          "Lua rejects forged action references before reaching native dispatch");
}
static int tab_select_calls,tab_activate_calls;
static bool fake_lua_tab_select(struct ToriRS_Api* api,int tab)
{ (void)api;tab_select_calls++;return tab==3; }
static bool fake_lua_tab_activate(struct ToriRS_Api* api,int tab)
{ (void)api;tab_activate_calls++;return tab==3; }
static void test_tab_activation(struct ToriRS_PluginHost* host)
{
    static char const source[] = "local p={id='tab-activation'};function p.on_key(api) "
        "assert(api.cache.tab_select(3));assert(api.cache.tab_activate(3));"
        "assert(not api.cache.tab_activate(-1));api.core.log('tab verbs verified') end;return p";
    struct FakeInstance instance={"tab-activation",""};
    struct ToriRS_Api api=fake_api(&instance);
    api.cache.tab_select=fake_lua_tab_select;api.cache.tab_activate=fake_lua_tab_activate;
    int index=PluginLua_AddScript(host,"tab-activation",source,(int)strlen(source));
    CHECK(index>=0,"tab activation binding registers");
    int const logs=g_logs,disabled=g_disabled_self;
    struct ToriRS_KeyEvent key={0};
    g_defs[index]->callbacks.on_key(&api,NULL,&key);
    CHECK(g_logs==logs+1 && g_disabled_self==disabled && tab_select_calls==1 && tab_activate_calls==2,
          "Lua keeps deterministic selection and native activation as distinct verbs");
}

static void test_widget_action_refusals(struct ToriRS_PluginHost* host)
{
    static char const source[] =
        "local p={id='widget-action-refusals'};local n=0;function p.on_start(api) "
        "assert(api.widgets.watch('sidebar',function(widget,event) n=n+1;"
        "local actions,reason=widget:actions();"
        "if n==1 then assert(actions==nil and reason=='native_blocked') "
        "elseif n==2 then assert(actions==nil and reason=='stale_reference') "
        "else assert(type(actions)=='table' and #actions==0 and reason==nil) end;"
        "api.core.log('action refusal checked') end)) end;return p";
    struct FakeInstance instance={"widget-action-refusals",""};
    struct ToriRS_Api api=fake_api(&instance);
    api.widgets.watch=fake_lua_watch;api.widgets.actions=fake_lua_actions;
    int index=PluginLua_AddScript(host,"widget-action-refusals",source,(int)strlen(source));
    CHECK(index>=0,"action refusal script registers");
    g_defs[index]->callbacks.on_start(&api,NULL);
    struct ToriRS_WidgetEvent event={.type=TORIRS_WIDGET_BOUND,.widget={{1,2,3}},.role="sidebar"};
    int const logs=g_logs,disables=g_disabled_self;
    for( lua_actions_mode=1;lua_actions_mode<=3;lua_actions_mode++ )
        lua_watch_listener(&api,lua_watch_user,&event);
    lua_actions_mode=0;
    CHECK(g_logs==logs+3 && g_disabled_self==disables,
        "Lua preserves first-query and fill-query refusal reasons while empty actions remain a successful table");
}

static void test_widget_watch(struct ToriRS_PluginHost* host)
{
    static char const source[] =
        "local p={id='widget-watch'};function p.on_start(api) "
        " assert(api.widgets.watch('sidebar',function(widget,event) "
        "  assert(type(widget)=='userdata' and event.kind=='bound' and event.role=='sidebar');"
        "  api.core.log('first');"
        "  assert(api.widgets.watch('sidebar',function(widget,event) api.core.log('replacement') end))"
        " end)) end; return p";
    struct FakeInstance instance={"widget-watch",""};
    struct ToriRS_Api api=fake_api(&instance);
    api.widgets.watch=fake_lua_watch;
    int index=PluginLua_AddScript(host,"widget-watch",source,(int)strlen(source));
    CHECK(index>=0,"watch script registers");
    g_defs[index]->callbacks.on_start(&api,NULL);
    CHECK(lua_watch_listener!=NULL,"Lua watch registers native listener");
    struct ToriRS_WidgetEvent event={.type=TORIRS_WIDGET_BOUND,.widget={{1,2,3}},.role="sidebar",.native_revision=5};
    int logs=g_logs;
    lua_watch_listener(&api,lua_watch_user,&event);
    lua_watch_listener(&api,lua_watch_user,&event);
    CHECK(g_logs==logs+2,"Lua binding callbacks have API scope and can replace themselves");
    g_defs[index]->callbacks.on_stop(&api,NULL);
    lua_watch_listener(&api,lua_watch_user,&event);
    CHECK(g_logs==logs+2,"Lua shutdown revokes retained watch closure state");
    g_defs[index]->callbacks.on_start(&api,NULL);
    lua_watch_listener(&api,lua_watch_user,&event);
    CHECK(g_logs==logs+3,"Lua watch starts fresh after enable");
}

static ToriRS_WidgetListener lua_op_listener;
static void* lua_op_user;
static char lua_op_label[64];
static int lua_op_sets;
static enum ToriRS_ContractResult fake_lua_get_widget(void* ctx,int32_t id,struct ToriRS_WidgetRef* out)
{
    (void)ctx;CHECK(id==1,"Lua forwards the requested component id");
    *out=(struct ToriRS_WidgetRef){{7,8,9}};return TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult fake_lua_set_on_op(void* ctx,struct ToriRS_WidgetRef widget,char const* label,
    ToriRS_WidgetListener listener,void* user)
{
    (void)ctx;++lua_op_sets;
    CHECK(widget.opaque[0]==7 && widget.opaque[1]==8 && widget.opaque[2]==9,"Lua forwards the checked control identity");
    CHECK((listener==NULL)==(label==NULL),"Lua removal sends no label and arming sends one");
    snprintf(lua_op_label,sizeof(lua_op_label),"%s",label ? label : "");
    lua_op_listener=listener;lua_op_user=user;
    return TORIRS_CONTRACT_OK;
}
static void test_widget_set_on_op(struct ToriRS_PluginHost* host)
{
    static char const source[]=
        "local p={id='widget-op'};local presses=0;function p.on_start(api) "
        " local control=assert(api.widgets.get(1));"
        " assert(control:set_on_op('Press',function(widget,event) "
        "  presses=presses+1;"
        "  assert(type(widget)=='userdata' and event.kind=='operation' and event.operation==1 and event.native_revision==11);"
        "  api.core.log('pressed '..presses);"
        "  if presses==2 then assert(control:set_on_op(nil)) end end)) end;"
        "function p.on_key(api) local control=api.widgets.get(1);control:set_on_op('',function() end) end;"
        "return p";
    struct FakeInstance instance={"widget-op",""};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.set_on_op=fake_lua_set_on_op;
    int index=PluginLua_AddScript(host,"widget-op",source,(int)strlen(source));
    CHECK(index>=0,"owned operation script registers");
    g_defs[index]->callbacks.on_start(&api,NULL);
    CHECK(lua_op_sets==1 && strcmp(lua_op_label,"Press")==0 && lua_op_listener!=NULL,"Lua arms the control through the C API");
    struct ToriRS_WidgetEvent event={.type=TORIRS_WIDGET_OPERATION,.widget={{7,8,9}},.operation=1,.native_revision=11,.role=""};
    int logs=g_logs;
    ToriRS_WidgetListener armed=lua_op_listener;void* armed_user=lua_op_user;
    armed(&api,armed_user,&event);
    CHECK(g_logs==logs+1,"an operation runs the Lua callback with widget and event");
    armed(&api,armed_user,&event);
    CHECK(g_logs==logs+2 && lua_op_sets==2 && lua_op_listener==NULL && !lua_op_label[0],
          "the Lua callback can remove its own operation, which reaches the adapter as a NULL listener");
    armed(&api,armed_user,&event);
    CHECK(g_logs==logs+2,"a released operation closure is inert even if a stale dispatch reaches it");
    g_defs[index]->callbacks.on_stop(&api,NULL);
    g_defs[index]->callbacks.on_start(&api,NULL);
    CHECK(lua_op_sets==3 && lua_op_listener!=NULL,"restart re-arms the control");
    armed=lua_op_listener;armed_user=lua_op_user;
    g_defs[index]->callbacks.on_stop(&api,NULL);
    armed(&api,armed_user,&event);
    CHECK(g_logs==logs+2,"Lua shutdown revokes retained operation closures");
    g_defs[index]->callbacks.on_start(&api,NULL);
    int disables=g_disabled_self;struct ToriRS_KeyEvent key={0};
    g_defs[index]->callbacks.on_key(&api,NULL,&key);
    CHECK(g_disabled_self==disables+1 && strstr(g_disable_reason,"invalid operation label"),
          "Lua rejects an empty operation label before reaching native dispatch");
}

static int lua_anchor_sets,lua_anchor_relation;
static struct ToriRS_WidgetRef lua_anchor_target;
static enum ToriRS_ContractResult fake_lua_set_anchor(void* ctx,struct ToriRS_WidgetRef widget,struct ToriRS_WidgetRef target,
    enum ToriRS_WidgetRelation relation)
{
    (void)ctx;++lua_anchor_sets;lua_anchor_relation=(int)relation;lua_anchor_target=target;
    CHECK(widget.opaque[0]==7 && widget.opaque[1]==8 && widget.opaque[2]==9,"Lua forwards the anchored widget identity");
    return TORIRS_CONTRACT_OK;
}
static void test_widget_set_anchor(struct ToriRS_PluginHost* host)
{
    static char const source[]=
        "local p={id='widget-anchor'};function p.on_start(api) "
        " local control=assert(api.widgets.get(1));local target=assert(api.widgets.get(1));"
        " assert(control:set_anchor(target,'behind'));"
        " assert(control:set_anchor(nil,'native'));"
        " assert(not pcall(function() control:set_anchor(target,'sideways') end));"
        " assert(not pcall(function() control:set_anchor(nil,'over') end)) end;return p";
    struct FakeInstance instance={"widget-anchor",""};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.set_anchor=fake_lua_set_anchor;
    int index=PluginLua_AddScript(host,"widget-anchor",source,(int)strlen(source));
    CHECK(index>=0,"anchor script registers");
    lua_anchor_sets=0;
    g_defs[index]->callbacks.on_start(&api,NULL);
    CHECK(lua_anchor_sets==2 && lua_anchor_relation==TORIRS_WIDGET_RELATION_NATIVE && lua_anchor_target.opaque[0]==0,
          "Lua forwards named relations and a nil target for native");
}
static int lua_img_slot,lua_img_w,lua_img_h,lua_img_opacity,lua_img_creates;
static enum ToriRS_ContractResult fake_lua_create_image(void* ctx,struct ToriRS_WidgetRef parent,char const* key,struct ToriRS_WidgetRef* out)
{
    (void)ctx;++lua_img_creates;
    CHECK(parent.opaque[0]==7 && strcmp(key,"camera")==0,"Lua forwards the parent and key of an owned image");
    *out=(struct ToriRS_WidgetRef){{7,40,1}};return TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult fake_lua_set_image(void* ctx,struct ToriRS_WidgetRef widget,struct ToriRS_ImageRef image,int w,int h)
{
    (void)ctx;CHECK(widget.opaque[1]==40,"Lua sets the image on the created control");
    lua_img_slot=image.value;lua_img_w=w;lua_img_h=h;return TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult fake_lua_set_opacity(void* ctx,struct ToriRS_WidgetRef widget,int opacity)
{
    (void)ctx;(void)widget;lua_img_opacity=opacity;return TORIRS_CONTRACT_OK;
}
static int lua_mask_sets,lua_mask_value;
static enum ToriRS_ContractResult fake_lua_set_mask(void* ctx,struct ToriRS_WidgetRef widget,struct ToriRS_ImageRef image)
{
    (void)ctx;(void)widget;++lua_mask_sets;lua_mask_value=image.value;return TORIRS_CONTRACT_OK;
}
static void test_widget_images(struct ToriRS_PluginHost* host)
{
    static char const source[]=
        "local p={id='widget-img'};function p.on_start(api) "
        " local parent=assert(api.widgets.get(1));local control=assert(parent:create_image('camera'));"
        " assert(control:set_image(12,20,10));assert(control:set_opacity(85));"
        " assert(parent:set_mask(12));assert(parent:set_mask(nil));api.core.log('image set') end;"
        "function p.on_key(api) local parent=api.widgets.get(1);parent:set_opacity(300) end;return p";
    struct FakeInstance instance={"widget-img",""};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.create_image=fake_lua_create_image;
    api.widgets.set_image=fake_lua_set_image;api.widgets.set_opacity=fake_lua_set_opacity;
    api.widgets.set_mask=fake_lua_set_mask;
    int index=PluginLua_AddScript(host,"widget-img",source,(int)strlen(source));
    CHECK(index>=0,"owned image script registers");
    int logs=g_logs;g_defs[index]->callbacks.on_start(&api,NULL);
    CHECK(g_logs==logs+1 && lua_img_creates==1 && lua_img_slot==12 && lua_img_w==20 && lua_img_h==10 && lua_img_opacity==85,
          "Lua owned image control forwards image token, size and opacity");
    CHECK(lua_mask_sets==2 && lua_mask_value==0,"Lua forwards a mask token and nil as the explicit unmask");
    int disables=g_disabled_self;struct ToriRS_KeyEvent key={0};
    g_defs[index]->callbacks.on_key(&api,NULL,&key);
    CHECK(g_disabled_self==disables+1 && strstr(g_disable_reason,"opacity"),"Lua rejects an out-of-range opacity before native dispatch");
}

static int menu_calls;
static struct ToriRS_MenuBuildEvent* expected_menu;
static bool fake_menu_entry(struct ToriRS_Api* api,struct ToriRS_MenuBuildEvent* menu,
    char const* text,uint32_t id)
{
    (void)api;
    CHECK(menu==expected_menu && strcmp(text,"Tag Guard")==0 && id==85,
        "menu module forwards current dispatch and intended action");
    menu_calls++;
    return true;
}
static void test_menu_module(struct ToriRS_PluginHost* host)
{
    CHECK(TORIRS_PLUGIN_MENU_ROWS_MAX>=25,"native menu snapshot exceeds the old16-row boundary");
    if( TORIRS_PLUGIN_MENU_ROWS_MAX<25 ) return;
    char const source[]="return {id='menu-module',"
        "on_menu_build=function(api,event) assert(api.ui==nil);"
        "assert(#event.rows==25 and event.rows[25].npc_slot==99, 'crowded menu retains its final native target');"
        "assert(api.menu.add('Tag Guard',85)) end,"
        "on_frame_start=function(api,event) api.menu.add('Tag Guard',85) end}";
    struct FakeInstance instance={"menu-module",""};
    struct ToriRS_Api api=fake_api(&instance);
    api.menu.add=fake_menu_entry;
    int index=PluginLua_AddScript(host,"menu-module",source,(int)strlen(source));
    CHECK(index>=0,"menu module test registers");
    if( index<0 ) return;
    g_defs[index]->callbacks.on_start(&api,NULL);
    struct ToriRS_MenuBuildEvent event={.row_count=25};
    for( int i=0; i<event.row_count; i++ )
    {
        event.rows[i].text="Examine Guard";
        event.rows[i].npc_slot=i==24 ? 99 : -1;
    }
    expected_menu=&event;menu_calls=0;
    g_defs[index]->callbacks.on_menu_build(&api,NULL,&event);
    CHECK(menu_calls==1,"menu addition reaches native API");
    struct ToriRS_FrameEvent frame={0};
    int disabled=g_disabled_self;
    g_defs[index]->callbacks.on_frame_start(&api,NULL,&frame);
    CHECK(g_disabled_self==disabled+1 && menu_calls==1 &&
        strstr(g_disable_reason,"menu.add is only valid in on_menu_build"),
        "menu addition after dispatch is rejected");
    g_defs[index]->callbacks.on_stop(&api,NULL);
}

static void
test_bundled_scripts(struct ToriRS_PluginHost* host)
{
    static struct { char const* file; char const* id; } const FILES[] = {
        {"_beamprobe.lua","beam-probe"}, {"_drawprobe.lua","drawprobe"},
        {"_gicount.lua","gi-count"}, {"_giprobe.lua","gi-probe"},
        {"_hoverprobe.lua","hover-probe"}, {"_hullprobe.lua","hull-probe"},
        {"_paneldemo.lua","paneldemo"}, {"_probe.lua","probe"},
        {"_roleprobe.lua","roleprobe"}, {"_windemo.lua","windemo"},
        {"_widgetprobe.lua","widgetprobe"},
        {"entity_highlighter.lua","entity-highlighter"},
        {"ground_items.lua","ground-items"}, {"loot_beam.lua","loot-beam"},
        {"performance_display.lua","performance-display"},
        {"screenshot.lua","screenshot"}, {"tile_indicator.lua","tile-indicator-lua"},
    };
    char path[512];

    for( size_t i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++ )
    {
        int size = 0;
        snprintf(path, sizeof(path), "../script/plugins/%s", FILES[i].file);
        char* source = read_file(path, &size);
        CHECK(source != NULL, "bundled Lua source readable");
        if( source )
        {
            CHECK(PluginLua_AddScript(host, FILES[i].id, source, size) >= 0,
                "bundled Lua source loads as a V2 definition");
            free(source);
        }
    }
    CHECK(g_registered == 17, "all seventeen bundled scripts registered");
}

static void test_product_behavior(struct ToriRS_PluginHost* host,
    char const* product_path, char const* test_path, char const* id)
{
    int product_size=0,test_size=0;
    char* product=read_file(product_path,&product_size);
    char* test=read_file(test_path,&test_size);
    CHECK(product && test,"product behavior sources readable");
    if( !product || !test ) { free(product);free(test);return; }
    size_t capacity=(size_t)product_size+(size_t)test_size+64;
    char* source=malloc(capacity);
    CHECK(source!=NULL,"product behavior source allocation");
    if( !source ) { free(product);free(test);return; }
    int size=snprintf(source,capacity,"local product=(function()\n%.*s\nend)()\n%.*s",
        product_size,product,test_size,test);
    int index=PluginLua_AddScript(host,id,source,size);
    CHECK(index>=0,"product behavior registers");
    if( index>=0 )
    {
        struct FakeInstance instance={id,""};
        struct ToriRS_Api api=fake_api(&instance);
        int logs=g_logs,disables=g_disabled_self;
        g_defs[index]->callbacks.on_start(&api,NULL);
        if( g_disabled_self!=disables ) fprintf(stderr,"%s: %s\n",id,g_disable_reason);
        CHECK(g_disabled_self==disables && g_logs==logs+1,"product behavior passes");
        g_defs[index]->callbacks.on_stop(&api,NULL);
    }
    free(source);free(product);free(test);
}

static void
test_config_boundary(struct ToriRS_PluginHost* host)
{
    static char const SOURCE[] =
        "return {id='config-boundary',on_key=function(api,event) "
        "local value, key = nil, 'tags' "
        "if event.key==0 then value=string.rep('n',191) "
        "elseif event.key==1 then value=string.rep('n',192) "
        "elseif event.key==2 then value=string.rep('n',1000) "
        "elseif event.key==3 then value='123'..string.char(0)..'456' "
        "elseif event.key==4 then key='tags'..string.char(0)..'other';value='123' "
        "elseif event.key==5 then value=true "
        "elseif event.key==6 then value=false "
        "elseif event.key==7 then value=42 "
        "else value='' end "
        "local ok,reason=api.config.set(key,value) "
        "if event.key>=1 and event.key<=4 then "
        "assert(not ok and reason=='invalid','lossy config write was accepted') "
        "else assert(ok) end return 'consume' end}";
    struct FakeInstance instance = { "config-boundary", NULL };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_KeyEvent event = { 0 };
    int index = PluginLua_AddScript(host, instance.id, SOURCE, (int)strlen(SOURCE));
    int writes = g_config_dispatches;
    int disables = g_disabled_self;
    char expected[TORIRS_PLUGIN_CONFIG_VALUE_MAX];

    CHECK(index >= 0, "config boundary probe registers");
    if( index < 0 ) return;
    memset(expected, 'n', sizeof(expected) - 1);
    expected[sizeof(expected) - 1] = '\0';
    CHECK(g_defs[index]->callbacks.on_key(&api, NULL, &event) == TORIRS_CALLBACK_CONSUME &&
            g_config_dispatches == writes + 1 && strcmp(g_config_value, expected) == 0,
        "maximum-length config value reaches the host without losing a byte");
    for( event.key = 1; event.key <= 4; event.key++ )
        CHECK(g_defs[index]->callbacks.on_key(&api, NULL, &event) == TORIRS_CALLBACK_CONSUME &&
                g_config_dispatches == writes + 1 && strcmp(g_config_value, expected) == 0,
            "oversized or embedded-NUL config input is refused without a prefix write");
    for( event.key = 5; event.key <= 8; event.key++ )
    {
        static char const* const values[] = { "1", "0", "42", "" };
        CHECK(g_defs[index]->callbacks.on_key(&api, NULL, &event) == TORIRS_CALLBACK_CONSUME &&
                strcmp(g_config_value, values[event.key - 5]) == 0,
            "boolean, numeric, and empty config values retain their supported spelling");
    }
    CHECK(g_disabled_self == disables && g_config_dispatches == writes + 5,
        "refused config data leaves the plugin running and only valid writes dispatch");
}

static void
test_callback_subscriptions(struct ToriRS_PluginHost* host)
{
    static char const SOURCE[] =
        "return {id='sparse-callbacks',on_key=function(api) "
        "api.core.log('key');return 'consume' end}";
    static char const REPLACEMENT[] =
        "return {id='sparse-callbacks',on_logic_tick=function(api) "
        "api.core.log('tick') end}";
    static char const MISSING_FRAME_HANDLER[] =
        "return {id='missing-frame-handler',frames={{id='frame',title='Frame',"
        "canvas='fixed',width=765,height=503}}}";
    struct FakeInstance instance = { "sparse-callbacks", NULL };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_KeyEvent key = { 0 };
    struct ToriRS_TickEvent tick = { 0 };
    int index = PluginLua_AddScript(host, instance.id, SOURCE, (int)strlen(SOURCE));
    int logs = g_logs;
    int disables = g_disabled_self;

    CHECK(index >= 0, "sparse callback probe registers");
    if( index < 0 ) return;
    struct ToriRS_PluginCallbacks const* callbacks = &g_defs[index]->callbacks;
    CHECK(callbacks->on_start && callbacks->on_stop,
        "adapter lifecycle hooks remain available without script lifecycle handlers");
    CHECK(callbacks->on_key && !callbacks->on_frame_start && !callbacks->on_logic_tick &&
            !callbacks->on_server_tick && !callbacks->on_npc_spawn && !callbacks->on_item_spawn &&
            !callbacks->on_draw_world && !callbacks->on_draw_canvas && !callbacks->on_menu_build &&
            !callbacks->on_menu_select && !callbacks->on_ui_build && !callbacks->on_gameframe,
        "a key-only Lua plugin does not subscribe the host to unrelated event production");
    callbacks->on_start(&api, NULL);
    CHECK(callbacks->on_key(&api, NULL, &key) == TORIRS_CALLBACK_CONSUME && g_logs == logs + 1,
        "a declared callback still dispatches and preserves its consume result");
    callbacks->on_stop(&api, NULL);
    CHECK(PluginLua_TestReplaceSource(index, REPLACEMENT, (int)strlen(REPLACEMENT)),
        "replacement source changes the actual callback set");
    g_reload[index](host, index, g_reload_user[index]);
    CHECK(!callbacks->on_key && callbacks->on_logic_tick && !callbacks->on_draw_world,
        "reload refreshes copied host subscriptions, adding and removing callbacks");
    callbacks->on_start(&api, NULL);
    if( callbacks->on_logic_tick ) callbacks->on_logic_tick(&api, NULL, &tick);
    CHECK(g_logs == logs + 2 && g_disabled_self == disables,
        "the newly subscribed callback reaches the rebuilt VM");
    CHECK(PluginLua_AddScript(host, "missing-frame-handler", MISSING_FRAME_HANDLER,
            (int)strlen(MISSING_FRAME_HANDLER)) < 0,
        "frame offers require a real gameframe handler before host registration");
}

static enum ToriRS_Result
fake_world_tile_stroke(struct ToriRS_Graphics* draw, int x, int z, int level,
    uint32_t fill, uint32_t outline, int alpha, int width)
{
    (void)draw;
    CHECK(x == 3200 && z == 3201 && level == 0 && fill == 0x112233 &&
            outline == 0x445566 && alpha == 40 && width == 0,
        "Lua stroke tile preserves fill, border colour, alpha, and a zero-width border");
    g_stroke_calls++;
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
fake_world_hull_stroke(struct ToriRS_Graphics* draw, int element, uint32_t rgb,
    int alpha, int shape, int width)
{
    (void)draw;
    CHECK(element == 42 && rgb == 0x778899 && alpha == 80 &&
            shape == TORIRS_HULL_MESH && width == 2,
        "Lua stroke hull forwards the named shape and literal canvas-pixel width");
    g_stroke_calls++;
    return TORIRS_RESULT_BUDGET;
}

static enum ToriRS_Result
fake_world_hull_styled(struct ToriRS_Graphics* draw,int element,uint32_t rgb,
                      int alpha,int shape,int width,uint32_t flags)
{
    (void)draw;
    CHECK(element==42 && rgb==0x778899 && alpha==80 && shape==TORIRS_HULL_MESH && width==2,
          "Lua silhouette preserves mesh geometry and style");
    CHECK(flags==0 || flags==TORIRS_WORLD_DRAW_ALWAYS_ON_TOP,
          "Lua silhouette carries only the requested visibility flag");
    g_stroke_calls++;
    return TORIRS_RESULT_BUDGET;
}

static enum ToriRS_Result
fake_world_tile_styled(struct ToriRS_Graphics* draw,int x,int z,int level,
    uint32_t fill,uint32_t outline,int alpha,int width,uint32_t flags)
{
    (void)draw;
    CHECK(x==3200 && z==3201 && level==0 && fill==0x112233 && outline==0x445566 &&
          alpha==40 && width==0,"Lua surface preserves independent fill, outline, alpha and width");
    CHECK(flags==0 || flags==TORIRS_WORLD_DRAW_ALWAYS_ON_TOP,"Lua surface forwards visibility policy");
    g_stroke_calls++;
    return TORIRS_RESULT_OK;
}

static void
test_graphics_stroke(struct ToriRS_PluginHost* host)
{
    static char const SOURCE[] =
        "local calls=0;return {id='graphics-stroke',on_draw_world=function(api,draw) "
        "calls=calls+1;local ok,reason "
        "if calls==1 then "
        "assert(draw.world_tile_stroke(3200,3201,0,0x112233,0x445566,40,0)) "
        "assert(draw.world_tile_styled(3200,3201,0,0x112233,0x445566,40,0,0)) "
        "assert(draw.world_tile_styled(3200,3201,0,0x112233,0x445566,40,0,16)) "
        "ok,reason=draw.world_tile_styled(1,2,0,1,nil,0,1,1) "
        "assert(not ok and reason=='invalid','unknown tile visibility flag is refused') "
        "ok,reason=draw.world_hull_stroke(42,0x778899,80,'mesh',2) "
        "assert(not ok and reason=='budget','stroke capacity refusal must reach Lua') "
        "ok,reason=draw.world_hull_styled(42,0x778899,80,'mesh',2,0) "
        "assert(not ok and reason=='budget','visible mesh refusal must reach Lua') "
        "ok,reason=draw.world_hull_styled(42,0x778899,80,'mesh',2,16) "
        "assert(not ok and reason=='budget','always-on-top mesh refusal must reach Lua') "
        "ok,reason=draw.world_hull_styled(42,1,0,'mesh',2,1) "
        "assert(not ok and reason=='invalid','unknown style flag is refused') "
        "ok,reason=draw.world_tile_stroke(1,2,0,1,nil,0,256) "
        "assert(not ok and reason=='invalid','invalid stroke width is refused') "
        "else "
        "ok,reason=draw.world_tile_stroke(1,2,0,1) "
        "assert(not ok and reason=='unsupported','short graphics prefix has no stroke tail') "
        "ok,reason=draw.world_hull_stroke(42,1) "
        "assert(not ok and reason=='unsupported','short graphics prefix has no hull stroke tail') "
        "ok,reason=draw.world_hull_styled(42,1) "
        "assert(not ok and reason=='unsupported','short prefix cannot read the style tail') "
        "ok,reason=draw.world_tile_styled(1,2,0,1) "
        "assert(not ok and reason=='unsupported','short prefix cannot read tile style tail') "
        "end end}";
    struct FakeInstance instance = { "graphics-stroke", NULL };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_Graphics draw = { 0 };
    int index = PluginLua_AddScript(host, instance.id, SOURCE, (int)strlen(SOURCE));
    int disables = g_disabled_self;

    CHECK(index >= 0, "graphics stroke probe registers");
    if( index < 0 ) return;
    draw.struct_size = sizeof(draw);
    draw.world_tile_stroke = fake_world_tile_stroke;
    draw.world_hull_stroke = fake_world_hull_stroke;
    draw.world_hull_styled = fake_world_hull_styled;
    draw.world_tile_styled = fake_world_tile_styled;
    g_defs[index]->callbacks.on_draw_world(&api, NULL, &draw);
    draw.struct_size = offsetof(struct ToriRS_Graphics, world_tile_stroke);
    g_defs[index]->callbacks.on_draw_world(&api, NULL, &draw);
    CHECK(g_disabled_self == disables && g_stroke_calls == 6,
        "Lua stroke bindings preserve results and never read an unavailable optional tail");
}

static int lua_minimap_calls;
static enum ToriRS_Result fake_minimap_tile(struct ToriRS_Graphics* draw,int x,int z,int level,
    uint32_t fill,uint32_t outline,int alpha,int width)
{
    (void)draw;
    CHECK(x==3200 && z==3210 && level==0 && fill==0x123456 && outline==0x654321 && alpha==50 && width==0,
        "Lua minimap verb preserves world coords, independent alpha and zero border");
    ++lua_minimap_calls;return TORIRS_RESULT_BUDGET;
}
static void test_minimap_graphics(struct ToriRS_PluginHost* host)
{
    char const* source="return {id='minimap-probe',on_draw_minimap=function(api,d) "
        "assert(d.rect==nil and d.world_tile==nil,'map scope only offers map primitives'); "
        "local ok,why=d.minimap_tile(3200,3210,0,0x123456,0x654321,50,0); "
        "assert(not ok and (why=='budget' or why=='unsupported')); "
        "local a,b=d.minimap_tile(3200,3210,0,0,0,50,256); assert(not a and b=='invalid') end}";
    int index=PluginLua_AddScript(host,"minimap-probe",source,(int)strlen(source));
    CHECK(index>=0,"Lua minimap callback registers");
    if( index<0 ) return;
    CHECK(g_defs[index]->callbacks.on_draw_minimap && !g_defs[index]->callbacks.on_draw_world,
        "minimap callback stays sparse and does not subscribe to world draws");
    struct FakeInstance instance={.id="minimap-probe"};
    struct ToriRS_Api api=fake_api(&instance);
    struct ToriRS_Graphics draw={.struct_size=sizeof(draw),.minimap_tile=fake_minimap_tile};
    int disabled=g_disabled_self;
    g_defs[index]->callbacks.on_draw_minimap(&api,NULL,&draw);
    draw.struct_size=offsetof(struct ToriRS_Graphics,minimap_tile);
    g_defs[index]->callbacks.on_draw_minimap(&api,NULL,&draw);
    CHECK(lua_minimap_calls==1 && g_disabled_self==disabled,
        "Lua map callback forwards budget and guards an older graphics prefix");
}

int
main(void)
{
    struct ToriRS_PluginHost host = { 0 };
    reset_fake();
    test_runtime(&host);
    test_config_boundary(&host);
    test_callback_subscriptions(&host);
    test_graphics_stroke(&host);
    test_minimap_graphics(&host);
    test_widget_watch(&host);
    test_widget_actions(&host);
    test_widget_action_refusals(&host);
    test_tab_activation(&host);
    test_widget_set_on_op(&host);
    test_widget_set_anchor(&host);
    test_widget_images(&host);
    test_menu_module(&host);
    PluginLua_Shutdown();
    reset_fake();
    test_bundled_scripts(&host);
    test_product_behavior(&host,"../script/plugins/_roleprobe.lua",
        "plugin/test/roleprobe_behavior.lua","roleprobe-behavior");
    test_product_behavior(&host,"../script/plugins/screenshot.lua",
        "plugin/test/screenshot_behavior.lua","screenshot-behavior");
    test_product_behavior(&host,"../script/plugins/loot_beam.lua",
        "plugin/test/loot_beam_behavior.lua","loot-beam-behavior");
    test_product_behavior(&host,"../script/plugins/performance_display.lua",
        "plugin/test/performance_display_behavior.lua","performance-behavior");
    test_product_behavior(&host,"../script/plugins/tile_indicator.lua",
        "plugin/test/tile_indicator_behavior.lua","tile-behavior");
    test_product_behavior(&host,"../script/plugins/entity_highlighter.lua",
        "plugin/test/entity_highlighter_behavior.lua","entity-behavior");
    test_product_behavior(&host,"../script/plugins/ground_items.lua",
        "plugin/test/ground_items_behavior.lua","ground-behavior");
    test_product_behavior(&host,"../script/plugins/_beamprobe.lua",
        "plugin/test/overlay_probe_behavior.lua","overlay-probe-behavior-beam-probe");
    test_product_behavior(&host,"../script/plugins/_gicount.lua",
        "plugin/test/overlay_probe_behavior.lua","overlay-probe-behavior-gi-count");
    test_product_behavior(&host,"../script/plugins/_drawprobe.lua",
        "plugin/test/overlay_probe_behavior.lua","overlay-probe-behavior-drawprobe");
    PluginLua_Shutdown();
    if( g_failures )
    {
        fprintf(stderr, "lua plugin test: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("lua plugin test: runtime, reload, descriptors, builders, and 17 bundled scripts passed");
    return 0;
}
