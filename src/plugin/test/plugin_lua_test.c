#include "plugin/porcelain/test/porcelain_testbed.h"
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
static char g_disable_reason[192];

/* The refusals the runtime group provokes on purpose, counted. @see main. */
#define LUA_TEST_DELIBERATE_DISABLES 15

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
    g_defs[g_registered] = def;
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
    if( strchr(value, '\n') || strchr(value, '\r') || strchr(key, '-') )
        return TORIRS_RESULT_INVALID;
    for( int i = 0; i < g_registered; i++ )
    {
        if( strcmp(g_defs[i]->id, id) != 0 ) continue;
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
static enum ToriRS_ContractResult fake_lua_actions(void* ctx,struct ToriRS_WidgetRef widget,
    struct ToriRS_WidgetAction* out,size_t capacity,size_t* count)
{
    (void)ctx;*count=2;
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
static int lua_op_number;
static enum ToriRS_ContractResult fake_lua_get_widget(void* ctx,int32_t id,struct ToriRS_WidgetRef* out)
{
    (void)ctx;CHECK(id==1,"Lua forwards the requested component id");
    *out=(struct ToriRS_WidgetRef){{7,8,9}};return TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult fake_lua_set_on_op(void* ctx,struct ToriRS_WidgetRef widget,int op,char const* label,
    ToriRS_WidgetListener listener,void* user)
{
    (void)ctx;++lua_op_sets;
    CHECK(widget.opaque[0]==7 && widget.opaque[1]==8 && widget.opaque[2]==9,"Lua forwards the checked control identity");
    CHECK((listener==NULL)==(label==NULL),"Lua removal sends no label and arming sends one");
    CHECK(op>=1 && op<=TORIRS_WIDGET_OP_SLOTS,"Lua forwards an operation number inside the contract's range");
    snprintf(lua_op_label,sizeof(lua_op_label),"%s",label ? label : "");
    lua_op_listener=listener;lua_op_user=user;lua_op_number=op;
    return TORIRS_CONTRACT_OK;
}
/*
 * A control with TWO rows, and clearing one of them.
 *
 * The motivating case for numbered ops: a cover over a component that offers
 * two rows has to offer both, and taking one away must leave the other armed.
 * The Lua layer keeps ONE registry reference per control, so the assertion
 * that matters is that clearing op 2 keeps op 1's callback alive -- released
 * too early, the remaining row dispatches into a freed reference.
 */
static void test_widget_second_op(struct ToriRS_PluginHost* host)
{
    static char const source[]=
        "local p={id='widget-op2'};local fired=0;function p.on_start(api) "
        " local control=assert(api.widgets.get(1));"
        " local function press(widget,event) fired=fired+1;api.core.log('op '..event.operation) end;"
        " assert(control:set_on_op(1,'Quick-prayers',press));"
        " assert(control:set_on_op(2,'Setup',press)) end;"
        "function p.on_key(api) local control=api.widgets.get(1);assert(control:set_on_op(2)) end;"
        "return p";
    struct FakeInstance instance={"widget-op2",""};struct ToriRS_Api api=fake_api(&instance);
    struct ToriRS_KeyEvent key={0};
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.set_on_op=fake_lua_set_on_op;
    int index=PluginLua_AddScript(host,"widget-op2",source,(int)strlen(source));
    CHECK(index>=0,"two-operation script registers");
    lua_op_sets=0;
    g_defs[index]->callbacks.on_start(&api,NULL);
    CHECK(lua_op_sets==2 && lua_op_number==2 && strcmp(lua_op_label,"Setup")==0,
          "each operation reaches the adapter under its own number");
    ToriRS_WidgetListener armed=lua_op_listener;void* armed_user=lua_op_user;
    int logs=g_logs;
    g_defs[index]->callbacks.on_key(&api,NULL,&key);
    CHECK(lua_op_sets==3 && lua_op_number==2 && lua_op_listener==NULL,
          "clearing one row sends that number with no listener");
    struct ToriRS_WidgetEvent first={.type=TORIRS_WIDGET_OPERATION,.widget={{7,8,9}},.operation=1,.native_revision=11,.role=""};
    armed(&api,armed_user,&first);
    CHECK(g_logs==logs+1,"the row that was left armed still runs its callback");
    g_defs[index]->callbacks.on_stop(&api,NULL);
}

static void test_widget_set_on_op(struct ToriRS_PluginHost* host)
{
    static char const source[]=
        "local p={id='widget-op'};local presses=0;function p.on_start(api) "
        " local control=assert(api.widgets.get(1));"
        " assert(control:set_on_op(1,'Press',function(widget,event) "
        "  presses=presses+1;"
        "  assert(type(widget)=='userdata' and event.kind=='operation' and event.operation==1 and event.native_revision==11);"
        "  api.core.log('pressed '..presses);"
        "  if presses==2 then assert(control:set_on_op(1)) end end)) end;"
        "function p.on_key(api) local control=api.widgets.get(1);control:set_on_op(1,'',function() end) end;"
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
    test_widget_second_op(host);
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
        " assert(control:set_anchor(nil,'native')) end;return p";
    /* The two REFUSALS, each as its own script, because the sandbox removes
     * pcall and always has: a refusal is a FAULT here, and a fault ends the
     * callback. Written as `assert(not pcall(...))` inside the script above,
     * both of these read as passes -- pcall was nil, the script was disabled
     * at the call, and the only CHECK left was one the two lines before it
     * had already satisfied. Neither claim was ever tested. */
    static char const bad_relation[]=
        "local p={id='widget-anchor-relation'};function p.on_start(api) "
        " local control=assert(api.widgets.get(1));local target=assert(api.widgets.get(1));"
        " control:set_anchor(target,'sideways') end;return p";
    static char const nil_target[]=
        "local p={id='widget-anchor-target'};function p.on_start(api) "
        " local control=assert(api.widgets.get(1));"
        " control:set_anchor(nil,'over') end;return p";
    struct FakeInstance instance={"widget-anchor",""};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.set_anchor=fake_lua_set_anchor;
    int index=PluginLua_AddScript(host,"widget-anchor",source,(int)strlen(source));
    CHECK(index>=0,"anchor script registers");
    lua_anchor_sets=0;
    g_defs[index]->callbacks.on_start(&api,NULL);
    CHECK(lua_anchor_sets==2 && lua_anchor_relation==TORIRS_WIDGET_RELATION_NATIVE && lua_anchor_target.opaque[0]==0,
          "Lua forwards named relations and a nil target for native");

    /* Its OWN instance: the runtime finds the script by core.plugin_id(), so
     * a second script driven through the first one's api runs the FIRST one's
     * body and proves nothing about itself. */
    struct FakeInstance bad_instance={"widget-anchor-relation",""};
    struct ToriRS_Api bad_api=fake_api(&bad_instance);
    bad_api.widgets.get_widget=fake_lua_get_widget;bad_api.widgets.set_anchor=fake_lua_set_anchor;
    int disables=g_disabled_self;
    int bad=PluginLua_AddScript(host,"widget-anchor-relation",bad_relation,(int)strlen(bad_relation));
    CHECK(bad>=0,"the bad-relation script registers");
    lua_anchor_sets=0;
    g_defs[bad]->callbacks.on_start(&bad_api,NULL);
    CHECK(g_disabled_self==disables+1 && strstr(g_disable_reason,"sideways")!=NULL,
          "an unnamed anchor relation is refused by name");
    CHECK(lua_anchor_sets==0,"and never reaches the native set_anchor");

    struct FakeInstance nil_instance={"widget-anchor-target",""};
    struct ToriRS_Api nil_api=fake_api(&nil_instance);
    nil_api.widgets.get_widget=fake_lua_get_widget;nil_api.widgets.set_anchor=fake_lua_set_anchor;
    disables=g_disabled_self;
    int nil_index=PluginLua_AddScript(host,"widget-anchor-target",nil_target,(int)strlen(nil_target));
    CHECK(nil_index>=0,"the nil-target script registers");
    lua_anchor_sets=0;
    g_defs[nil_index]->callbacks.on_start(&nil_api,NULL);
    CHECK(g_disabled_self==disables+1 && strstr(g_disable_reason,"torirs.widget")!=NULL,
          "only 'native' takes a nil target; every other relation needs a widget");
    CHECK(lua_anchor_sets==0,"and that one never reaches the native set_anchor either");
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
    char const source[]="return {id='menu-module',"
        "on_menu_build=function(api,event) assert(api.ui==nil);"
        "assert(api.menu.add('Tag Guard',85)) end,"
        "on_frame_start=function(api,event) api.menu.add('Tag Guard',85) end}";
    struct FakeInstance instance={"menu-module",""};
    struct ToriRS_Api api=fake_api(&instance);
    api.menu.add=fake_menu_entry;
    int index=PluginLua_AddScript(host,"menu-module",source,(int)strlen(source));
    CHECK(index>=0,"menu module test registers");
    if( index<0 ) return;
    g_defs[index]->callbacks.on_start(&api,NULL);
    struct ToriRS_MenuBuildEvent event={0};
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

/* --------------------------------------------------- the real porcelain lane */
/*
 * Every other product-behaviour case runs the plugin against a stand-in
 * reconciler written in its own test file. A stand-in can only pin what its
 * author believed, and two things about the port are not the author's to
 * believe: whether the runtime installs the pump at all, and what the layer
 * actually spends in the steady state. This case runs the SHIPPED
 * performance_display.lua against the real layer over the Porcelain testbed,
 * dispatches its frames through the registered callback table -- lua_cb_frame,
 * the pump itself -- and lets the Lua side read the handle's own counters.
 */
static int g_counter_logs;
static char g_counter_log[4][192];

static void
counters_log(struct ToriRS_Api* api, char const* format, ...)
{
    va_list arguments;
    (void)api;
    if( g_counter_logs >= (int)(sizeof(g_counter_log) / sizeof(g_counter_log[0])) )
        return;
    va_start(arguments, format);
    vsnprintf(g_counter_log[g_counter_logs], sizeof(g_counter_log[0]), format, arguments);
    va_end(arguments);
    g_counter_logs++;
}

struct CountersConfigRow
{
    char const* key;
    int number;
    char const* text;
};
static struct CountersConfigRow COUNTERS_CONFIG[] = {
    {"show_fps", 1, NULL},        {"show_frame_time", 1, NULL},
    {"show_effective_fps", 1, NULL}, {"show_memory", 1, NULL},
    /* Long enough that the refresh window never closes inside the run: a
     * re-latched FPS or memory figure is a string that MOVED, which is a
     * describe the steady-state case is not measuring. */
    {"refresh_ms", 60000, NULL},  {"x", 10, NULL}, {"y", 25, NULL},
    {"text_color", 0xffffff, NULL},
};

static struct CountersConfigRow*
counters_config_row(char const* key)
{
    for( size_t i = 0; i < sizeof(COUNTERS_CONFIG) / sizeof(COUNTERS_CONFIG[0]); i++ )
        if( strcmp(COUNTERS_CONFIG[i].key, key) == 0 )
            return &COUNTERS_CONFIG[i];
    return NULL;
}
static bool counters_config_has(struct ToriRS_Api* api, char const* key)
{ (void)api; return counters_config_row(key) != NULL; }
static bool counters_config_get_bool(struct ToriRS_Api* api, char const* key, bool* out)
{
    struct CountersConfigRow const* row = counters_config_row(key);
    (void)api;
    if( !row ) return false;
    *out = row->number != 0;
    return true;
}
static bool counters_config_get_int(struct ToriRS_Api* api, char const* key, int* out)
{
    struct CountersConfigRow const* row = counters_config_row(key);
    (void)api;
    if( !row ) return false;
    *out = row->number;
    return true;
}
static bool counters_config_get_color(struct ToriRS_Api* api, char const* key, uint32_t* out)
{
    struct CountersConfigRow const* row = counters_config_row(key);
    (void)api;
    if( !row ) return false;
    *out = (uint32_t)row->number;
    return true;
}
static uint64_t counters_work_us(struct ToriRS_Api* api)
{ (void)api; return 4000; }
static size_t counters_memory_bytes(struct ToriRS_Api* api)
{ (void)api; return (size_t)128u * 1024u * 1024u; }

static void
test_real_porcelain_pump(struct ToriRS_PluginHost* host)
{
    static struct ToriRS_Api api;
    static struct ToriRS_ClientApi client;
    struct FakeInstance instance = {"performance-counters", ""};
    int product_size = 0, tail_size = 0;
    char* product = read_file("../script/plugins/performance_display.lua", &product_size);
    char* tail = read_file("plugin/test/performance_display_counters.lua", &tail_size);
    char* source;
    size_t capacity;
    int index;
    int errors_before;
    int disables_before;

    CHECK(product && tail, "real porcelain sources readable");
    if( !product || !tail ) { free(product); free(tail); return; }
    capacity = (size_t)product_size + (size_t)tail_size + 64;
    source = malloc(capacity);
    CHECK(source != NULL, "real porcelain source allocation");
    if( !source ) { free(product); free(tail); return; }

    Testbed_Reset();
    /* The one element the four rows are placed inside. Parent-local 0,0 so a
     * row's resolved box is the offset the plugin asked for. */
    Testbed_DeclareElement("viewport", 0, 0, 512, 334);
    Testbed_BindElement("viewport");

    api = *Testbed_Api();
    api.instance = &instance;
    api.core.plugin_id = fake_plugin_id;
    api.core.log = counters_log;
    /* Neither of these is on the porcelain testbed, because no Porcelain case
     * needed them; the plugin reads both every frame. */
    api.core.frame_work_us = counters_work_us;
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.memory_bytes = counters_memory_bytes;
    client.disable_self = fake_disable_self;
    api.client = &client;
    api.config.has = counters_config_has;
    api.config.get_bool = counters_config_get_bool;
    api.config.get_int = counters_config_get_int;
    api.config.get_color = counters_config_get_color;

    snprintf(source, capacity, "local product=(function()\n%.*s\nend)()\n%.*s",
        product_size, product, tail_size, tail);
    index = PluginLua_AddScript(host, "performance-counters", source, (int)strlen(source));
    CHECK(index >= 0, "real porcelain script registers");
    if( index >= 0 )
    {
        struct ToriRS_FrameEvent event;
        errors_before = g_reported_errors;
        disables_before = g_disabled_self;
        g_counter_logs = 0;
        g_defs[index]->callbacks.on_start(&api, NULL);
        for( int i = 1; i <= 36; i++ )
        {
            memset(&event, 0, sizeof(event));
            event.now_ms = (uint64_t)i * 20u;
            event.drawn_frames = (uint32_t)i;
            g_defs[index]->callbacks.on_frame_start(&api, NULL, &event);
            /* Three server ticks between the counter reset and the reading.
             * Nothing in the registered table declares on_server_tick, so a
             * timer that fires can only have been forwarded by the pump. */
            if( i == 20 || i == 22 || i == 24 )
            {
                struct ToriRS_TickEvent tick;
                memset(&tick, 0, sizeof(tick));
                tick.cycle = (uint32_t)i;
                g_defs[index]->callbacks.on_server_tick(&api, NULL, &tick);
            }
        }
        for( int i = 0; i < g_counter_logs; i++ )
            printf("lua plugin test: %s\n", g_counter_log[i]);
        /* A Lua assertion inside a handler is a FAULT, and a fault routes to
         * client.disable_self rather than to the host error, so both have to
         * be read -- otherwise every assertion in the tail above is a line on
         * stderr and a green exit code. */
        if( g_disabled_self != disables_before )
            fprintf(stderr, "performance-counters: %s\n", g_disable_reason);
        CHECK(g_disabled_self == disables_before,
            "the shipped plugin runs against the real layer without faulting");
        CHECK(g_reported_errors == errors_before,
            "and without a host error");
        CHECK(g_counter_logs == 2, "and reached the steady-state reading");
        /* The pump, from the other side: four rows exist on the real engine
         * although nothing in the plugin or this file ever called fence. */
        CHECK(Testbed_LiveControls() == 4,
            "open() installed the pump: four rows are live with no fence anywhere "
            "in the plugin");
        CHECK(Testbed_Control("performance_frame") != NULL,
            "and they are the plugin's own keys");
        /* The third handler the pump installs. Nothing in the registered table
         * notes the config input any more -- performance_display recomposes
         * its strings and nothing else -- so a row that empties can only mean
         * the runtime noted it before calling the plugin's handler. */
        {
            struct ToriRS_FrameEvent later;
            struct TestbedControl const* fps;
            counters_config_row("show_fps")->number = 0;
            g_defs[index]->callbacks.on_config_changed(&api, NULL, "show_fps");
            memset(&later, 0, sizeof(later));
            later.now_ms = 1000;
            later.drawn_frames = 40;
            g_defs[index]->callbacks.on_frame_start(&api, NULL, &later);
            fps = Testbed_Control("performance_fps");
            CHECK(fps != NULL && fps->text[0] == '\0',
                "the pump notes the config input: a switched-off row empties although "
                "the plugin never noted it");
            counters_config_row("show_fps")->number = 1;
        }
        g_defs[index]->callbacks.on_stop(&api, NULL);
        CHECK(Testbed_LiveControls() == 0, "stopping drops them with the handle");
    }
    free(source); free(product); free(tail);
}

/*
 * What the pump costs a plugin that has nothing to reconcile.
 *
 * Two of the four ported Lua plugins are deliberately minimal: tile_indicator
 * opens the layer for its refusal channel and never speaks to it again, and
 * entity_highlighter has no description at all -- it used to guard its own
 * fence with "only when the reveal key armed, so a lane that answered ABSENT
 * pays nothing per frame". `open` now installs the pump for both, so that
 * claim stops being the plugin's to make and becomes the layer's to answer.
 *
 * This is the answer, read off the handle rather than argued: a plugin that
 * opens the layer, describes nothing, registers no timer, holds no asset and
 * arms no key edge is pumped for 200 frames and makes ZERO engine calls and
 * ZERO allocations. Nothing had to be opted out of.
 */
static char const IDLE_PROBE[] =
    "local p={id='idle-probe'}\n"
    "local opened=false\n"
    "function p.on_start(api) opened=api.porcelain.open() end\n"
    "function p.on_frame_start(api)\n"
    "  if not opened then return end\n"
    "  local c=api.porcelain.counters_read()\n"
    "  assert(c.engine_calls==0,'an idle pumped handle makes no engine call')\n"
    "  assert(c.allocations==0,'and allocates nothing')\n"
    "  assert(c.describe_runs==0,'and runs no describe: there is none')\n"
    "  assert(c.revalidates==0,'and costs no layout resolve')\n"
    "end\n"
    "return p\n";

static void
test_pump_costs_an_idle_plugin_nothing(struct ToriRS_PluginHost* host)
{
    static struct ToriRS_Api api;
    struct FakeInstance instance = {"idle-probe", ""};
    int index;
    int disables_before;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");

    api = *Testbed_Api();
    api.instance = &instance;
    api.core.plugin_id = fake_plugin_id;
    api.core.log = counters_log;

    index = PluginLua_AddScript(host, "idle-probe", IDLE_PROBE, (int)strlen(IDLE_PROBE));
    CHECK(index >= 0, "idle probe registers");
    if( index < 0 ) return;
    disables_before = g_disabled_self;
    g_defs[index]->callbacks.on_start(&api, NULL);
    for( int i = 1; i <= 200; i++ )
    {
        struct ToriRS_FrameEvent event;
        memset(&event, 0, sizeof(event));
        event.now_ms = (uint64_t)i * 20u;
        event.drawn_frames = (uint32_t)i;
        g_defs[index]->callbacks.on_frame_start(&api, NULL, &event);
    }
    if( g_disabled_self != disables_before )
        fprintf(stderr, "idle-probe: %s\n", g_disable_reason);
    CHECK(g_disabled_self == disables_before,
        "200 pumped frames cost a plugin with nothing to reconcile nothing at all");
    CHECK(Testbed_LogCount() == 0,
        "and reach the engine not once");
    g_defs[index]->callbacks.on_stop(&api, NULL);
}

/*
 * The Lua word "within" reaches PORCELAIN_WITHIN, and what that costs.
 *
 * The behaviour tests read `item.place.kind` straight off the Lua table, so
 * they pin the plugin's intent and NOT the runtime's translation of it. This
 * drives the real parser and the real layer and reads the engine log: WITHIN
 * is a child of the element with no anchor, INSIDE is a sibling over it with
 * one, and the two read their corner and offsets identically.
 */
static char const WITHIN_PROBE[] =
    "local p={id='within-probe'}\n"
    "function p.on_start(api)\n"
    "  assert(api.porcelain.open())\n"
    "  api.porcelain.describe(function(d)\n"
    "    d.piece({key='child',image='camera.png',w=20,h=20,"
    "      place={kind='within',on='viewport',corner='bottom_right',dx=4,dy=4}})\n"
    "    d.piece({key='sibling',image='camera.png',w=20,h=20,"
    "      place={kind='inside',on='viewport',corner='bottom_right',dx=4,dy=4}})\n"
    "  end)\n"
    "end\n"
    "return p\n";

static void
test_lua_within_placement(struct ToriRS_PluginHost* host)
{
    static struct ToriRS_Api api;
    struct FakeInstance instance = {"within-probe", ""};
    struct TestbedControl const* child;
    struct TestbedControl const* sibling;
    int index;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    Testbed_DeclareImage("camera.png", TORIRS_ASSET_READY, 20, 20);

    api = *Testbed_Api();
    api.instance = &instance;
    api.core.plugin_id = fake_plugin_id;
    api.core.log = counters_log;

    index = PluginLua_AddScript(host, "within-probe", WITHIN_PROBE, (int)strlen(WITHIN_PROBE));
    CHECK(index >= 0, "within probe registers");
    if( index < 0 ) return;
    g_defs[index]->callbacks.on_start(&api, NULL);
    for( int i = 1; i <= 3; i++ )
    {
        struct ToriRS_FrameEvent event;
        memset(&event, 0, sizeof(event));
        event.now_ms = (uint64_t)i * 20u;
        event.drawn_frames = (uint32_t)i;
        g_defs[index]->callbacks.on_frame_start(&api, NULL, &event);
    }
    child = Testbed_Control("child");
    sibling = Testbed_Control("sibling");
    CHECK(child && child->live, "a 'within' item is placed");
    CHECK(sibling && sibling->live, "and so is the 'inside' item beside it");
    if( !child || !sibling ) return;
    CHECK(ToriRS_WidgetRefEqual(child->parent, Testbed_Element("viewport")->ref),
        "'within' parents the item to the element itself");
    CHECK(Testbed_LogCountWith("set_anchor child") == 0,
        "and pays NO anchor for it");
    CHECK(Testbed_LogCountWith("set_anchor sibling") == 1,
        "while 'inside' is a sibling that pays one");
    CHECK(child->x == 512 - 20 - 4 && child->y == 334 - 20 - 4,
        "'within' reads the corner in the element's OWN coordinates");
    CHECK(sibling->x == 4 + 512 - 20 - 4 && sibling->y == 4 + 334 - 20 - 4,
        "and 'inside' reads the same corner in the element's PARENT's");
    g_defs[index]->callbacks.on_stop(&api, NULL);
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

int
main(void)
{
    struct ToriRS_PluginHost host = { 0 };
    reset_fake();
    test_runtime(&host);
    test_widget_watch(&host);
    test_widget_actions(&host);
    test_widget_set_on_op(&host);
    test_widget_set_anchor(&host);
    test_widget_images(&host);
    test_menu_module(&host);
    /*
     * A DISABLED PLUGIN AND A WORKING ONE MAKE THE SAME SILENCE.
     *
     * Every refusal above is provoked on purpose and each has a CHECK on the
     * reason it gave. Nothing reconciled the SET, though, so a script that
     * faulted for a reason nobody intended just printed a line and left the
     * suite green -- which is how `widget-anchor` spent its life calling a
     * pcall the sandbox has always removed, disabling itself at the third
     * statement, with the only CHECK after it already satisfied by the first
     * two. Two refusals were claimed and neither was ever tested.
     *
     * So the count is DECLARED. Adding a negative fixture means saying so
     * here; a disable nobody declared turns the suite red at the frame that
     * caused it.
     */
    CHECK(g_disabled_self == LUA_TEST_DELIBERATE_DISABLES,
        "every self-disable in the runtime group is one this suite provoked on purpose");
    int const disables_after_runtime = g_disabled_self;
    PluginLua_Shutdown();
    reset_fake();
    test_bundled_scripts(&host);
    test_product_behavior(&host,"../script/plugins/_roleprobe.lua",
        "plugin/test/roleprobe_behavior.lua","roleprobe-behavior");
    test_product_behavior(&host,"../script/plugins/screenshot.lua",
        "plugin/test/screenshot_behavior.lua","screenshot-behavior");
    test_product_behavior(&host,"../script/plugins/performance_display.lua",
        "plugin/test/performance_display_behavior.lua","performance-behavior");
    test_product_behavior(&host,"../script/plugins/tile_indicator.lua",
        "plugin/test/tile_indicator_behavior.lua","tile-behavior");
    test_product_behavior(&host,"../script/plugins/entity_highlighter.lua",
        "plugin/test/entity_highlighter_behavior.lua","entity-behavior");
    test_product_behavior(&host,"../script/plugins/ground_items.lua",
        "plugin/test/ground_items_behavior.lua","ground-behavior");
    test_product_behavior(&host,"../script/plugins/loot_beam.lua",
        "plugin/test/loot_beam_behavior.lua","loot-beam-behavior");
    test_product_behavior(&host,"../script/plugins/_beamprobe.lua",
        "plugin/test/overlay_probe_behavior.lua","overlay-probe-behavior-beam-probe");
    test_product_behavior(&host,"../script/plugins/_gicount.lua",
        "plugin/test/overlay_probe_behavior.lua","overlay-probe-behavior-gi-count");
    test_product_behavior(&host,"../script/plugins/_drawprobe.lua",
        "plugin/test/overlay_probe_behavior.lua","overlay-probe-behavior-drawprobe");
    test_real_porcelain_pump(&host);
    test_lua_within_placement(&host);
    test_pump_costs_an_idle_plugin_nothing(&host);
    /* The second group has NO negative fixture in it: every bundled script,
     * every ported product and every pump probe is meant to run to the end.
     * One of them switching itself off at on_start is the exact failure this
     * suite exists to catch, and it is invisible in a green run otherwise. */
    CHECK(g_disabled_self == disables_after_runtime,
        "no bundled script, product or pump probe disabled itself");
    PluginLua_Shutdown();
    if( g_failures )
    {
        fprintf(stderr, "lua plugin test: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("lua plugin test: runtime, reload, descriptors, builders, and 17 bundled scripts passed");
    return 0;
}
