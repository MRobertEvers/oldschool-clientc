#include "plugin/porcelain/test/porcelain_testbed.h"
#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_lua.h"
#include "mem/torirs_mem.h"

#include <assert.h>
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
static int g_disabled_self;
static int g_config_dispatches;
static char g_disable_reason[192];

/* The refusals the runtime group provokes on purpose, counted. @see main. */
#define LUA_TEST_DELIBERATE_DISABLES 14

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

struct FakeInstance { char const* id; };

static char const* fake_plugin_id(struct ToriRS_Api* api)
{
    return ((struct FakeInstance*)api->instance)->id;
}
static char g_last_log[512];
static void fake_log(struct ToriRS_Api* api, char const* format, ...)
{
    va_list args;
    (void)api;
    va_start(args, format);
    vsnprintf(g_last_log, sizeof(g_last_log), format, args);
    va_end(args);
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
    static struct ToriRS_ClientApi client;
    static struct ToriRS_GameApi game;
    memset(&game, 0, sizeof(game));
    game.struct_size = sizeof(game);
    api.game = &game;
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
    static char const FRAMES_SOURCE[] =
        "return {id='frame-offer',frames={{id='old',title='Old',canvas='fixed',"
        "width=765,height=503}}}";
    static char const INVALID_ENUM_SOURCE[] =
        /* Deliberately collides with lua-v2-test in the 64-slot id table. */
        "return {id='invalid-enum-59',on_start=function(api) api.game.inventory_size(99) end}";
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
    static char const DEFAULT_OFF_SOURCE[] = "return {id='default-off'}";
    static char const SOURCE[] =
        "return { id='lua-v2-test', title='Lua V2 Test', version='2',"
        " config={{key='answer',type='int',default='42'}},"
        " on_start=function(api)"
        "  assert(api.log==nil and api.role==nil and api.window==nil and api.layout==nil)"
        "  assert(api.chrome==nil and api.entity==nil and api.object_create==nil)"
        "  assert(api.local_player==nil and api.image_load==nil and api.cfg_set==nil)"
        "  assert(pcall==nil and xpcall==nil and getmetatable==nil and type(setmetatable)=='function')"
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
    struct FakeInstance instance = { "lua-v2-test" };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_Graphics draw;
    struct ToriRS_PanelBuilder panel;
    int index;

    CHECK(PluginLua_AddScript(host, "empty", "", 0, 0u) < 0,
        "empty source is refused without a debug assertion");
    CHECK(PluginLua_AddScript(host, "legacy", LEGACY_SOURCE,
              (int)strlen(LEGACY_SOURCE), 0u) < 0,
        "legacy name field is not accepted as a V2 id");
    CHECK(PluginLua_AddScript(host, "frame-offer", FRAMES_SOURCE,
              (int)strlen(FRAMES_SOURCE), 0u) < 0,
        "a script that declares frame offers is refused: plugins provide no gameframe");
    index = PluginLua_AddScript(host, "lua-v2-test", SOURCE, (int)strlen(SOURCE), 0u);

    CHECK(index == 0, "runtime script registered through V2");
    CHECK(g_defs[0] && strcmp(g_defs[0]->id, "lua-v2-test") == 0, "Lua name is V2 id");
    CHECK(g_defs[0]->config && g_defs[0]->config->items, "V2 config schema retained");

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
    CHECK(g_logs == 2, "canonical and synchronously reentrant API calls reached V2 functions");
    CHECK(g_draws == 1 && g_headings == 1 && g_action_rows == 1,
        "scoped draw and native action-row builders reached V2 functions");
    CHECK(g_config_dispatches == 1,
        "config.set synchronously dispatched on_config_changed once");
    CHECK(g_reload[0] != NULL, "source reload handler installed");
    g_reload[0](host, 0, g_reload_user[0]);
    g_defs[0]->callbacks.on_start(&api, NULL);
    CHECK(g_logs == 3, "reload rebuilt and restarted the VM");
    CHECK(PluginLua_AddScript(host, "invalid-enum-59", INVALID_ENUM_SOURCE,
              (int)strlen(INVALID_ENUM_SOURCE), 0u) == 1,
        "enum misuse probe registered");
    struct FakeInstance invalid_instance = { "invalid-enum-59" };
    struct ToriRS_Api invalid_api = fake_api(&invalid_instance);
    g_defs[1]->callbacks.on_start(&invalid_api, NULL);
    CHECK(g_disabled_self == 1 && g_reported_errors == 0 && g_enabled_calls == 0,
        "out-of-range integer enum becomes a caught V2 lifecycle fault");
    CHECK(strstr(g_disable_reason, "inventory must be between") != NULL,
        "integer enum fault names the invalid domain");

    CHECK(PluginLua_AddScript(host, "budget-after-reentry", BUDGET_SOURCE,
              (int)strlen(BUDGET_SOURCE), 0u) == 2,
        "nested-budget probe registered");
    struct FakeInstance budget_instance = { "budget-after-reentry" };
    struct ToriRS_Api budget_api = fake_api(&budget_instance);
    g_defs[2]->callbacks.on_start(&budget_api, NULL);
    CHECK(g_disabled_self == 2 &&
            strstr(g_disable_reason, "instruction budget exhausted") != NULL,
        "nested callback cannot disarm the outer instruction budget");

    int const logs_before_nested_fault = g_logs;
    int const disables_before_nested_fault = g_disabled_self;
    CHECK(PluginLua_AddScript(host, "nested-fault", NESTED_FAULT_SOURCE,
              (int)strlen(NESTED_FAULT_SOURCE), 0u) == 3,
        "nested-fault probe registered");
    struct FakeInstance nested_instance = { "nested-fault" };
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
              (int)strlen(DISABLE_SOURCE), 0u) == 4,
        "explicit-disable probe registered");
    struct FakeInstance disable_instance = { "explicit-disable" };
    struct ToriRS_Api disable_api = fake_api(&disable_instance);
    g_defs[4]->callbacks.on_start(&disable_api, NULL);
    CHECK(g_logs == logs_before_disable && strstr(g_disable_reason, "requested stop") != NULL,
        "disable_self unwinds Lua before native teardown and later API use");

    CHECK(PluginLua_AddScript(host, "bad-pixels", BAD_PIXELS_SOURCE,
              (int)strlen(BAD_PIXELS_SOURCE), 0u) == 5,
        "bad-pixels probe registered");
    struct FakeInstance pixels_instance = { "bad-pixels" };
    struct ToriRS_Api pixels_api = fake_api(&pixels_instance);
    g_defs[5]->callbacks.on_start(&pixels_api, NULL);
    CHECK(strstr(g_disable_reason, "pixel 2 is not an integer") != NULL,
        "malformed pixel input faults without retaining a host allocation");

    /* The manifest's enabled=0 is the script's declared default: the host
     * reads it off the def, so the settings file can record a player's "on".
     * A reload rebuilds the def from nothing and must keep it. */
    CHECK(PluginLua_AddScript(host, "default-off", DEFAULT_OFF_SOURCE,
              (int)strlen(DEFAULT_OFF_SOURCE),
              TORIRS_PLUGIN_DISABLED_BY_DEFAULT) == 6,
        "default-off script registered");
    CHECK(g_defs[6]->flags == TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
        "a default-off script declares TORIRS_PLUGIN_DISABLED_BY_DEFAULT");
    CHECK(g_defs[5]->flags == 0u, "an enabled=1 script declares no default-off flag");
    CHECK(g_reload[6] != NULL, "default-off reload handler installed");
    g_reload[6](host, 6, g_reload_user[6]);
    CHECK(g_defs[6]->flags == TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
        "a reload keeps the script's default-off flag");
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
    struct FakeInstance instance={"widget-actions"};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.watch=fake_lua_watch;api.widgets.actions=fake_lua_actions;api.widgets.invoke=fake_lua_invoke;
    int index=PluginLua_AddScript(host,"widget-actions",source,(int)strlen(source), 0u);
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
    struct FakeInstance instance={"widget-watch"};
    struct ToriRS_Api api=fake_api(&instance);
    api.widgets.watch=fake_lua_watch;
    int index=PluginLua_AddScript(host,"widget-watch",source,(int)strlen(source), 0u);
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
    struct FakeInstance instance={"widget-op2"};struct ToriRS_Api api=fake_api(&instance);
    struct ToriRS_KeyEvent key={0};
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.set_on_op=fake_lua_set_on_op;
    int index=PluginLua_AddScript(host,"widget-op2",source,(int)strlen(source), 0u);
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
    struct FakeInstance instance={"widget-op"};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.set_on_op=fake_lua_set_on_op;
    int index=PluginLua_AddScript(host,"widget-op",source,(int)strlen(source), 0u);
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
    struct FakeInstance instance={"widget-anchor"};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.set_anchor=fake_lua_set_anchor;
    int index=PluginLua_AddScript(host,"widget-anchor",source,(int)strlen(source), 0u);
    CHECK(index>=0,"anchor script registers");
    lua_anchor_sets=0;
    g_defs[index]->callbacks.on_start(&api,NULL);
    CHECK(lua_anchor_sets==2 && lua_anchor_relation==TORIRS_WIDGET_RELATION_NATIVE && lua_anchor_target.opaque[0]==0,
          "Lua forwards named relations and a nil target for native");

    /* Its OWN instance: the runtime finds the script by core.plugin_id(), so
     * a second script driven through the first one's api runs the FIRST one's
     * body and proves nothing about itself. */
    struct FakeInstance bad_instance={"widget-anchor-relation"};
    struct ToriRS_Api bad_api=fake_api(&bad_instance);
    bad_api.widgets.get_widget=fake_lua_get_widget;bad_api.widgets.set_anchor=fake_lua_set_anchor;
    int disables=g_disabled_self;
    int bad=PluginLua_AddScript(host,"widget-anchor-relation",bad_relation,(int)strlen(bad_relation), 0u);
    CHECK(bad>=0,"the bad-relation script registers");
    lua_anchor_sets=0;
    g_defs[bad]->callbacks.on_start(&bad_api,NULL);
    CHECK(g_disabled_self==disables+1 && strstr(g_disable_reason,"sideways")!=NULL,
          "an unnamed anchor relation is refused by name");
    CHECK(lua_anchor_sets==0,"and never reaches the native set_anchor");

    struct FakeInstance nil_instance={"widget-anchor-target"};
    struct ToriRS_Api nil_api=fake_api(&nil_instance);
    nil_api.widgets.get_widget=fake_lua_get_widget;nil_api.widgets.set_anchor=fake_lua_set_anchor;
    disables=g_disabled_self;
    int nil_index=PluginLua_AddScript(host,"widget-anchor-target",nil_target,(int)strlen(nil_target), 0u);
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
    struct FakeInstance instance={"widget-img"};struct ToriRS_Api api=fake_api(&instance);
    api.widgets.get_widget=fake_lua_get_widget;api.widgets.create_image=fake_lua_create_image;
    api.widgets.set_image=fake_lua_set_image;api.widgets.set_opacity=fake_lua_set_opacity;
    api.widgets.set_mask=fake_lua_set_mask;
    int index=PluginLua_AddScript(host,"widget-img",source,(int)strlen(source), 0u);
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
/* The engine's script reads, captured so the test lands them itself. */
#define FAKE_READS_MAX 8
static struct
{
    char path[256];
    int serial;
    void (*deliver)(void*, int, char const*, void*, int);
    void* user;
} g_reads[FAKE_READS_MAX];
static int g_read_count;

bool
PluginHost_ScriptRead(
    struct ToriRS_PluginHost* host,
    char const* path,
    int serial,
    void (*deliver)(void* deliver_user, int serial, char const* path, void* data, int size),
    void* deliver_user)
{
    (void)host;
    assert(g_read_count < FAKE_READS_MAX);
    snprintf(g_reads[g_read_count].path, sizeof(g_reads[0].path), "%s", path);
    g_reads[g_read_count].serial = serial;
    g_reads[g_read_count].deliver = deliver;
    g_reads[g_read_count].user = deliver_user;
    g_read_count++;
    return true;
}

static void
land_read(int i, char const* text)
{
    void* data = NULL;
    int size = 0;
    if( text )
    {
        size = (int)strlen(text);
        data = ToriMem_Alloc(ToriMem_Net(), (size_t)size);
        memcpy(data, text, (size_t)size);
    }
    g_reads[i].deliver(g_reads[i].user, g_reads[i].serial, g_reads[i].path, data, size);
}

/* api.sandbox: setmetatable without finalizers, a protected call that the
 * budget passes through, and modules read on demand from the script's own
 * directory. */
static void test_sandbox(struct ToriRS_PluginHost* host)
{
    char const source[] =
        "return {id='sandbox',"
        "on_start=function(api)"
        "  local Base={}; Base.__index=Base; function Base.hi(s) return s.v end"
        "  local o=setmetatable({v=5},Base); assert(o:hi()==5)"
        "  assert(setmetatable(o,nil)==o and o.hi==nil)"
        "  local ok,err=api.sandbox.protect(setmetatable,{},{__gc=function() end})"
        "  assert(not ok and tostring(err):find('__gc'),'__gc refused')"
        "  local locked=setmetatable({},{__metatable='no'})"
        "  ok=api.sandbox.protect(setmetatable,locked,{}); assert(not ok,'protected metatable')"
        "  local r1,r2,r3=api.sandbox.protect(function(a,b) return a+b,'x' end,2,3)"
        "  assert(r1==true and r2==5 and r3=='x')"
        "  ok,err=api.sandbox.protect(function() error('boom',0) end)"
        "  assert(ok==false and err=='boom')"
        "  ok,err=api.sandbox.protect(function() error({code=7}) end)"
        "  assert(ok==false and err.code==7)"
        "  assert(api.sandbox.module_request('mods.alpha')=='pending')"
        "  assert(api.sandbox.module_request('mods.alpha')=='pending')"
        "  assert(api.sandbox.module_request('mods.gone')=='pending')"
        "  assert(api.sandbox.module_request('mods.broken')=='pending')"
        "  ok,err=api.sandbox.protect(api.sandbox.module_request,'../etc')"
        "  assert(not ok and tostring(err):find('not a module name'))"
        "  ok,err=api.sandbox.protect(api.sandbox.module_load,'mods.alpha')"
        "  assert(not ok and tostring(err):find('pending'))"
        "  api.core.log('sandbox started')"
        "end,"
        "on_config_changed=function(api)"
        "  if PHASE==1 then"
        "    assert(api.sandbox.module_request('mods.alpha')=='ready','the re-read landed')"
        "    local m2=api.sandbox.module_load('mods.alpha'); assert(m2.v==8 and SEEN==2,'an unloaded module runs again')"
        "    assert(m2~=FIRST,'a fresh result')"
        "    api.core.log('module reloaded'); return"
        "  end"
        "  assert(api.sandbox.module_request('mods.alpha')=='ready')"
        "  assert(api.sandbox.module_request('mods.gone')=='missing')"
        "  local m=api.sandbox.module_load('mods.alpha'); assert(m.v==7 and SEEN==1)"
        "  assert(api.sandbox.module_load('mods.alpha')==m and SEEN==1,'a module runs once')"
        "  local ok,err=api.sandbox.protect(api.sandbox.module_load,'mods.broken')"
        "  assert(not ok and tostring(err):find('broken.lua'),'syntax error names the file')"
        "  assert(api.sandbox.module_request('mods.broken')=='invalid')"
        "  FIRST=m"
        "  assert(api.sandbox.module_unload('mods.alpha')==true)"
        "  ok,err=api.sandbox.protect(api.sandbox.module_unload,'mods.alpha')"
        "  assert(not ok and tostring(err):find('only a loaded module'),'a second unload raises')"
        "  ok,err=api.sandbox.protect(api.sandbox.module_unload,'mods.gone')"
        "  assert(not ok,'a missing module does not unload')"
        "  ok,err=api.sandbox.protect(api.sandbox.module_load,'mods.alpha')"
        "  assert(not ok and tostring(err):find('none'),'an unloaded module is not loadable until read again')"
        "  assert(api.sandbox.module_request('mods.alpha')=='pending','unload forgets it: read again')"
        "  PHASE=1"
        "  api.core.log('modules loaded')"
        "end,"
        "on_key=function(api)"
        "  api.sandbox.protect(function() while true do end end)"
        "  api.core.log('must not run: the budget passed through protect')"
        "end}";
    struct FakeInstance instance = { "sandbox" };
    struct ToriRS_Api api = fake_api(&instance);
    struct PluginLuaScriptOptions options = { "plugins/sandbox", 0, 0 };
    int logs, disabled;
    g_read_count = 0;
    int index = PluginLua_AddScriptEx(host, "sandbox", source, (int)strlen(source), 0u, &options);
    CHECK(index >= 0, "sandbox test registers");
    if( index < 0 ) return;
    logs = g_logs;
    g_defs[index]->callbacks.on_start(&api, NULL);
    CHECK(g_logs == logs + 1, "setmetatable, protect and module requests behave in on_start");
    CHECK(g_read_count == 3, "each module is read once however often it is requested");
    CHECK(strcmp(g_reads[0].path, "plugins/sandbox/mods/alpha.lua") == 0,
        "a dotted module name resolves under the script's own directory");
    land_read(0, "SEEN=(SEEN or 0)+1 return {v=7}");
    land_read(1, NULL);
    land_read(2, "return {");
    logs = g_logs;
    g_defs[index]->callbacks.on_config_changed(&api, NULL, "x");
    CHECK(g_logs == logs + 1, "landed modules load once, a missing one says so, a broken one is invalid");
    CHECK(g_read_count == 4, "an unloaded module is read again, in its own slot");
    CHECK(strcmp(g_reads[3].path, "plugins/sandbox/mods/alpha.lua") == 0, "the re-read is the same file");
    land_read(3, "SEEN=(SEEN or 0)+1 return {v=8}");
    logs = g_logs;
    g_defs[index]->callbacks.on_config_changed(&api, NULL, "x");
    CHECK(g_logs == logs + 1, "module_unload: the next request reads it again and module_load runs it again");
    disabled = g_disabled_self;
    struct ToriRS_KeyEvent key = { 0 };
    logs = g_logs;
    g_defs[index]->callbacks.on_key(&api, NULL, &key);
    CHECK(g_logs == logs && g_disabled_self == disabled + 1 && strstr(g_disable_reason, "instruction budget"),
        "an exhausted budget is not caught by sandbox.protect");
    g_defs[index]->callbacks.on_stop(&api, NULL);
}

/* The Quest Helper drawing verbs on the Lua draw object. The fakes answer
 * fixed values so the case pins the BINDING: argument order, the flat points
 * array, float truncation, the optional border, and nil for "no answer". */
static int g_qh_poly_calls;
static int g_qh_poly_count, g_qh_poly_x[4], g_qh_poly_y[4];
static uint32_t g_qh_poly_fill, g_qh_poly_outline;
static int g_qh_poly_alpha, g_qh_poly_width;
static enum ToriRS_Result fake_qh_polygon(struct ToriRS_Graphics* draw, int const* xs, int const* ys,
    int count, uint32_t fill_rgb, int fill_alpha, uint32_t outline_rgb, int outline_width)
{
    (void)draw;
    g_qh_poly_calls++;
    g_qh_poly_count = count;
    for( int i = 0; i < count && i < 4; i++ ) { g_qh_poly_x[i] = xs[i]; g_qh_poly_y[i] = ys[i]; }
    g_qh_poly_fill = fill_rgb; g_qh_poly_alpha = fill_alpha;
    g_qh_poly_outline = outline_rgb; g_qh_poly_width = outline_width;
    return g_qh_poly_calls >= 3 ? TORIRS_RESULT_BUDGET : TORIRS_RESULT_OK;
}
static int fake_qh_text_width(struct ToriRS_Graphics* draw, char const* text) { (void)draw; return (int)strlen(text) * 6; }
static int fake_qh_text_height(struct ToriRS_Graphics* draw) { (void)draw; return 12; }
static bool fake_qh_project(struct ToriRS_Graphics* draw, int fine_x, int fine_z, int height, int* out_x, int* out_y)
{ (void)draw; (void)height; *out_x = fine_x / 128; *out_y = fine_z / 128; return true; }
static bool fake_qh_minimap_project(struct ToriRS_Graphics* draw, int fine_x, int fine_z, int* out_x, int* out_y)
{ (void)draw; if( fine_x < 0 ) return false; *out_x = fine_x / 32; *out_y = fine_z / 32; return true; }
static bool fake_qh_minimap_bounds(struct ToriRS_Graphics* draw, struct ToriRS_MinimapBounds* out)
{ (void)draw; out->box = (struct ToriRS_Rect){ 550, 4, 152, 152 }; out->centre_x = 626; out->centre_y = 80; out->radius = 80; return true; }
static bool fake_qh_worldmap_project(struct ToriRS_Graphics* draw, int tile_x, int tile_z, int level, int* out_x, int* out_y)
{ (void)draw; (void)level; (void)tile_x; (void)tile_z; (void)out_x; (void)out_y; return false; }
static bool fake_qh_worldmap_bounds(struct ToriRS_Graphics* draw, struct ToriRS_Rect* out)
{ (void)draw; (void)out; return false; }
static void test_draw_verbs(struct ToriRS_PluginHost* host)
{
    char const source[] =
        "return {id='draw-verbs',"
        "on_draw_canvas=function(api,draw)"
        "  local ok,why=draw.polygon({1,2, 10,2.9, 10,20},0xff0000,60,0xffffff,2)"
        "  assert(ok==true,'polygon answers ok')"
        "  ok,why=draw.polygon({-1.5,4, 8,4},0x00ff00,0)"
        "  assert(ok==true,'a border-less polygon')"
        "  ok,why=draw.polygon({0,0, 5,0, 5,5},0x0000ff,30)"
        "  assert(ok==false and why=='budget','the refusal comes back named')"
        "  assert(not api.sandbox.protect(draw.polygon,{1,2,3},0,0),'odd points refused')"
        "  assert(not api.sandbox.protect(draw.polygon,{},0,0),'no points refused')"
        "  assert(not api.sandbox.protect(draw.polygon,{1,'x'},0,0),'a non-number refused')"
        "  assert(draw.text_width('abc')==18 and draw.text_height()==12)"
        "  local x,y=draw.project(256,384); assert(x==2 and y==3)"
        "  x,y=draw.minimap_project(64,32); assert(x==2 and y==1)"
        "  assert(draw.minimap_project(-5,0)==nil)"
        "  local b=draw.minimap_bounds()"
        "  assert(b.x==550 and b.y==4 and b.width==152 and b.height==152 and b.cx==626 and b.cy==80 and b.radius==80)"
        "  assert(draw.worldmap_project(3200,3200,0)==nil and draw.worldmap_bounds()==nil)"
        "  api.core.log('draw verbs')"
        "end}";
    struct FakeInstance instance = { "draw-verbs" };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_Graphics draw;
    int logs, disabled;
    int index = PluginLua_AddScript(host, "draw-verbs", source, (int)strlen(source), 0u);
    CHECK(index >= 0, "draw verbs test registers");
    if( index < 0 ) return;
    g_defs[index]->callbacks.on_start(&api, NULL);
    memset(&draw, 0, sizeof(draw));
    draw.struct_size = sizeof(draw);
    draw.polygon = fake_qh_polygon;
    draw.text_width = fake_qh_text_width;
    draw.text_height = fake_qh_text_height;
    draw.project = fake_qh_project;
    draw.minimap_project = fake_qh_minimap_project;
    draw.minimap_bounds = fake_qh_minimap_bounds;
    draw.worldmap_project = fake_qh_worldmap_project;
    draw.worldmap_bounds = fake_qh_worldmap_bounds;
    logs = g_logs;
    disabled = g_disabled_self;
    g_qh_poly_calls = 0;
    g_defs[index]->callbacks.on_draw_canvas(&api, NULL, &draw);
    if( g_disabled_self != disabled ) fprintf(stderr, "draw-verbs: %s\n", g_disable_reason);
    CHECK(g_logs == logs + 1 && g_disabled_self == disabled, "every draw verb answered as declared");
    CHECK(g_qh_poly_calls == 3, "the refused polygons never reached the builder");
    g_defs[index]->callbacks.on_stop(&api, NULL);
}
static void test_draw_verbs_arguments(struct ToriRS_PluginHost* host)
{
    char const source[] =
        "return {id='draw-args',"
        "on_draw_canvas=function(api,draw)"
        "  draw.polygon({1,2, 10,2.9, 10,20},0xff0000,60,0xffffff,2)"
        "end}";
    struct FakeInstance instance = { "draw-args" };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_Graphics draw;
    int index = PluginLua_AddScript(host, "draw-args", source, (int)strlen(source), 0u);
    CHECK(index >= 0, "draw argument test registers");
    if( index < 0 ) return;
    g_defs[index]->callbacks.on_start(&api, NULL);
    memset(&draw, 0, sizeof(draw));
    draw.struct_size = sizeof(draw);
    draw.polygon = fake_qh_polygon;
    g_qh_poly_calls = 0;
    g_defs[index]->callbacks.on_draw_canvas(&api, NULL, &draw);
    CHECK(g_qh_poly_calls == 1 && g_qh_poly_count == 3 &&
          g_qh_poly_x[0] == 1 && g_qh_poly_y[0] == 2 && g_qh_poly_x[1] == 10 && g_qh_poly_y[1] == 2 &&
          g_qh_poly_x[2] == 10 && g_qh_poly_y[2] == 20,
          "the flat points array arrives as vertices, floats truncated");
    CHECK(g_qh_poly_fill == 0xff0000u && g_qh_poly_alpha == 60 &&
          g_qh_poly_outline == 0xffffffu && g_qh_poly_width == 2,
          "wash and border arguments arrive in order");
    g_defs[index]->callbacks.on_stop(&api, NULL);
}

static void test_menu_module(struct ToriRS_PluginHost* host)
{
    char const source[]="return {id='menu-module',"
        "on_menu_build=function(api,event) assert(api.ui==nil);"
        "assert(api.menu.add('Tag Guard',85)) end,"
        "on_frame_start=function(api,event) api.menu.add('Tag Guard',85) end}";
    struct FakeInstance instance={"menu-module"};
    struct ToriRS_Api api=fake_api(&instance);
    api.menu.add=fake_menu_entry;
    int index=PluginLua_AddScript(host,"menu-module",source,(int)strlen(source), 0u);
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
        {"script_runner.lua","script-runner"},
        {"visual_metronome.lua","visual-metronome"},
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
            CHECK(PluginLua_AddScript(host, FILES[i].id, source, size, 0u) >= 0,
                "bundled Lua source loads as a V2 definition");
            free(source);
        }
    }
    CHECK(g_registered == 19, "all nineteen bundled scripts registered");
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
    {"show_renderer", 1, NULL},
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
/* The renderer drawing, and nothing else: the plugin asks for no other
 * display setting. */
static bool counters_display_get(struct ToriRS_Api* api, int setting, int* value, int* min, int* max)
{
    (void)api;
    assert(value);
    assert(min);
    assert(max);
    if( setting != TORIRS_DISPLAY_RENDERER_ACTIVE )
        return false;
    *value = TORIRS_RENDERER_OPENGL3;
    *min = 0;
    *max = TORIRS_RENDERER_COUNT - 1;
    return true;
}

static void
test_real_porcelain_pump(struct ToriRS_PluginHost* host)
{
    static struct ToriRS_Api api;
    static struct ToriRS_ClientApi client;
    struct FakeInstance instance = {"performance-counters" };
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
    client.display_get = counters_display_get;
    client.disable_self = fake_disable_self;
    api.client = &client;
    api.config.has = counters_config_has;
    api.config.get_bool = counters_config_get_bool;
    api.config.get_int = counters_config_get_int;
    api.config.get_color = counters_config_get_color;

    snprintf(source, capacity, "local product=(function()\n%.*s\nend)()\n%.*s",
        product_size, product, tail_size, tail);
    index = PluginLua_AddScript(host, "performance-counters", source, (int)strlen(source), 0u);
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
        CHECK(Testbed_LiveControls() == 5,
            "open() installed the pump: five rows are live with no fence anywhere "
            "in the plugin");
        CHECK(Testbed_Control("performance_frame") != NULL,
            "and they are the plugin's own keys");
        CHECK(Testbed_Control("performance_renderer") != NULL &&
                strcmp(Testbed_Control("performance_renderer")->text, "Renderer: OpenGL") == 0,
            "the renderer row names the renderer the host reports as drawing");
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
    struct FakeInstance instance = {"idle-probe" };
    int index;
    int disables_before;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");

    api = *Testbed_Api();
    api.instance = &instance;
    api.core.plugin_id = fake_plugin_id;
    api.core.log = counters_log;

    index = PluginLua_AddScript(host, "idle-probe", IDLE_PROBE, (int)strlen(IDLE_PROBE), 0u);
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
    struct FakeInstance instance = {"within-probe" };
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

    index = PluginLua_AddScript(host, "within-probe", WITHIN_PROBE, (int)strlen(WITHIN_PROBE), 0u);
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
    int index=PluginLua_AddScript(host,id,source,size, 0u);
    CHECK(index>=0,"product behavior registers");
    if( index>=0 )
    {
        struct FakeInstance instance={id};
        struct ToriRS_Api api=fake_api(&instance);
        int logs=g_logs,disables=g_disabled_self;
        g_defs[index]->callbacks.on_start(&api,NULL);
        if( g_disabled_self!=disables ) fprintf(stderr,"%s: %s\n",id,g_disable_reason);
        CHECK(g_disabled_self==disables && g_logs==logs+1,"product behavior passes");
        g_defs[index]->callbacks.on_stop(&api,NULL);
    }
    free(source);free(product);free(test);
}

/* The Quest Helper port's cache, game and script verbs, as Lua sees them:
 * the table shapes the bindings build out of the C answers, and the
 * arguments they hand down. The host's own checks are torirs_plugin_host_test's. */
static int qhl_varc_set_id, qhl_varc_set_value, qhl_chat_type, qhl_run_ints, qhl_run_strings;
static char qhl_chat_text[64], qhl_run_string[64];
static int qhl_fake_varc_int(struct ToriRS_Api* api, int id) { (void)api; return id == 7 ? 1234 : -1; }
static bool qhl_fake_varc_str(struct ToriRS_Api* api, int id, char* out, size_t capacity, size_t* required)
{
    (void)api;
    if( id != 3 ) { *required = 0; return false; }
    *required = sizeof("hello world");
    if( capacity ) snprintf(out, capacity, "%s", "hello world");
    return true;
}
static enum ToriRS_Result qhl_fake_set_varc_int(struct ToriRS_Api* api, int id, int value)
{ (void)api; qhl_varc_set_id = id; qhl_varc_set_value = value; return TORIRS_RESULT_OK; }
static enum ToriRS_Result qhl_fake_set_varc_str(struct ToriRS_Api* api, int id, char const* value)
{ (void)api; (void)value; return id == 3 ? TORIRS_RESULT_OK : TORIRS_RESULT_INVALID; }
static enum ToriRS_AssetState qhl_fake_enum_def(struct ToriRS_Api* api, int id, struct ToriRS_EnumDef* out,
    struct ToriRS_EnumEntry* entries, int capacity)
{
    (void)api;
    if( id == 2 ) return TORIRS_ASSET_PENDING;
    memset(out, 0, sizeof(*out));
    out->id = id; out->key_type = 'i'; out->value_is_string = id == 9; out->value_type = id == 9 ? 's' : 'i';
    out->default_int = -1; snprintf(out->default_string, sizeof(out->default_string), "none");
    out->count = 3;
    for( int i = 0; i < 3 && i < capacity; i++ )
    {
        entries[i].key = 20 + i; entries[i].int_value = 300 + i;
        snprintf(entries[i].string_value, sizeof(entries[i].string_value), "s%d", i);
    }
    return TORIRS_ASSET_READY;
}
static void qhl_list(struct ToriRS_DefList* list, int base, int n)
{ list->count = n; for( int i = 0; i < n && i < list->capacity; i++ ) list->values[i] = base + i; }
static enum ToriRS_AssetState qhl_fake_npc_def(struct ToriRS_Api* api, int id, struct ToriRS_NpcDef* out)
{
    (void)api;
    if( id != 10 ) return TORIRS_ASSET_MISSING;
    out->id = 10; snprintf(out->name, sizeof(out->name), "Cook");
    snprintf(out->actions[0], sizeof(out->actions[0]), "Talk-to");
    out->size = 1; out->combat_level = 3; out->transform_varbit = -1; out->transform_varp = 29; out->impostor = 41;
    qhl_list(&out->transforms, 40, 300); qhl_list(&out->models, 100, 2);
    qhl_list(&out->recolor_from, 5, 1); qhl_list(&out->recolor_to, 6, 1);
    return TORIRS_ASSET_READY;
}
static enum ToriRS_AssetState qhl_fake_loc_def(struct ToriRS_Api* api, int id, struct ToriRS_LocDef* out)
{
    (void)api;
    if( id != 20 ) return TORIRS_ASSET_PENDING;
    out->id = 20; snprintf(out->name, sizeof(out->name), "Door");
    snprintf(out->actions[0], sizeof(out->actions[0]), "Open");
    out->size_x = 1; out->size_z = 2; out->impostor = -1; out->map_scene = -1; out->map_icon = 12;
    qhl_list(&out->transforms, 0, 0);
    return TORIRS_ASSET_READY;
}
static enum ToriRS_Result qhl_fake_chat_add(struct ToriRS_Api* api, int type, char const* sender, char const* text)
{
    (void)api; (void)sender;
    qhl_chat_type = type; snprintf(qhl_chat_text, sizeof(qhl_chat_text), "%s", text);
    return TORIRS_RESULT_OK;
}
static enum ToriRS_Result qhl_fake_arrow_tile(struct ToriRS_Api* api, int x, int z, int level)
{ (void)api; return x == 3222 && z == 3218 && level == 0 ? TORIRS_RESULT_OK : TORIRS_RESULT_INVALID; }
static bool qhl_fake_arrow(struct ToriRS_Api* api, struct ToriRS_HintArrowInfo* out)
{
    (void)api;
    memset(out, 0, sizeof(*out));
    out->kind = TORIRS_HINT_ARROW_TILE; out->tile_x = 3222; out->tile_z = 3218; out->npc_slot = -1; out->player_pid = -1;
    return true;
}
static enum ToriRS_Result qhl_fake_sound(struct ToriRS_Api* api, int id, int loops, int delay)
{ (void)api; return id == 2266 && loops == 1 && delay == 0 ? TORIRS_RESULT_OK : TORIRS_RESULT_INVALID; }
static int qhl_fake_tick(struct ToriRS_Api* api) { (void)api; return 777; }
static enum ToriRS_ContractResult qhl_fake_run(void* context, int32_t script_id, struct ToriRS_ScriptArg const* args,
    size_t count, struct ToriRS_ScriptResult* out)
{
    (void)context;
    memset(out, 0, sizeof(*out));
    if( script_id == 1 ) return TORIRS_CONTRACT_PENDING;
    qhl_run_ints = 0; qhl_run_strings = 0;
    for( size_t i = 0; i < count; i++ )
    {
        if( args[i].is_string ) { qhl_run_strings++; snprintf(qhl_run_string, sizeof(qhl_run_string), "%s", args[i].string_value); }
        else qhl_run_ints++;
    }
    out->int_count = 2; out->ints[0] = 2; out->ints[1] = args[0].int_value;
    out->string_count = 1; snprintf(out->strings[0], sizeof(out->strings[0]), "back");
    return TORIRS_CONTRACT_OK;
}
static int qhl_fake_container_size(struct ToriRS_Api* api, int container)
{ (void)api; return container == 659 ? 2 : 0; }
static bool qhl_fake_container_slot(struct ToriRS_Api* api, int container, int slot, int* id, int* count)
{
    (void)api;
    if( container != 659 || slot < 0 || slot >= 2 ) return false;
    *id = slot == 0 ? 2434 : -1; *count = slot == 0 ? 3 : 0;
    return true;
}
static int qhl_fake_wev_next(struct ToriRS_Api* api, int iter, struct ToriRS_WorldEntitySnapshot* out)
{
    (void)api;
    if( iter >= 4 ) return -1;
    memset(out, 0, sizeof(*out));
    out->id = 4; out->parent_view = 0; out->config_id = 9; out->fine_x = 409600; out->fine_z = 409664; out->angle = 512;
    out->level = 0; out->deck_level = 1; out->base_x = 6400; out->base_z = 6408; out->size_x = 16; out->size_z = 8;
    return 4;
}
static bool qhl_fake_wev_to_main(struct ToriRS_Api* api, int id, int x, int z, int* ox, int* oz)
{ (void)api; if( id != 4 ) return false; *ox = 409600 + z; *oz = 409664 - x; return true; }
static void test_quest_helper_verbs(struct ToriRS_PluginHost* host)
{
    char const source[] =
        "return {id='qh-verbs',"
        "on_start=function(api)"
        "  assert(api.cache.varc_int(7)==1234 and api.cache.varc_int(8)==-1)"
        "  assert(api.cache.varc_str(3)=='hello world' and api.cache.varc_str(4)==nil)"
        "  local ok,why=api.cache.set_varc_int(5,-9); assert(ok and why=='ok')"
        "  ok,why=api.cache.set_varc_str(4,'x'); assert(not ok and why=='invalid')"
        "  local e,s=api.cache.enum_def(1); assert(s=='ready' and e.key_type=='i' and e.value_type=='i')"
        "  assert(#e.keys==3 and e.keys[1]==20 and e.values[3]==302 and e.default==-1)"
        "  e=api.cache.enum_def(9); assert(e.value_is_string and e.values[2]=='s1' and e.default=='none')"
        "  e,s=api.cache.enum_def(2); assert(e==nil and s=='pending')"
        "  local n=api.cache.npc_def(10)"
        "  assert(n.name=='Cook' and n.actions[1]=='Talk-to' and n.actions[2]==false and #n.actions==5)"
        "  assert(#n.transforms==300 and n.transforms[300]==339,'a long list is not cut')"
        "  assert(n.impostor==41 and n.models[2]==101 and n.recolor_to[1]==6 and n.transform_varp==29)"
        "  n,s=api.cache.npc_def(11); assert(n==nil and s=='missing')"
        "  local l=api.cache.loc_def(20); assert(l.size_z==2 and l.map_icon==12 and #l.transforms==0)"
        "  l,s=api.cache.loc_def(21); assert(l==nil and s=='pending')"
        "  ok=api.game.chat_add(0,nil,'<col=ff0000>Quest</col>'); assert(ok)"
        "  assert(api.game.hint_arrow_tile(3222,3218))"
        "  local h=api.game.hint_arrow(); assert(h.kind=='tile' and h.tile_x==3222 and h.npc_slot==-1 and h.player_pid==-1)"
        "  assert(api.game.sound(2266))"
        "  assert(api.game.tick()==777)"
        "  assert(api.game.container_size(659)==2 and api.game.container_size(516)==0)"
        "  local cid,ccount=api.game.container_slot(659,0); assert(cid==2434 and ccount==3)"
        "  cid,ccount=api.game.container_slot(659,1); assert(cid==-1 and ccount==0)"
        "  assert(api.game.container_slot(659,2)==nil and api.game.container_slot(516,0)==nil)"
        "  local wc,w=api.game.world_entity_next(); assert(wc==4 and w.id==4 and w.config_id==9 and w.angle==512)"
        "  assert(w.base_x==6400 and w.base_z==6408 and w.size_x==16 and w.size_z==8 and w.deck_level==1 and w.parent_view==0)"
        "  assert(w.fine_x==409600 and w.fine_z==409664 and api.game.world_entity_next(wc)==nil)"
        "  local mx,mz=api.game.world_entity_to_main(4,64,128); assert(mx==409728 and mz==409600)"
        "  assert(api.game.world_entity_to_main(5,0,0)==nil)"
        "  local ints,strs=api.scripts.run(4029,6,'name',8)"
        "  assert(ints[1]==2 and ints[2]==6 and #ints==2 and strs[1]=='back')"
        "  local none,reason=api.scripts.run(1); assert(none==nil and reason=='pending')"
        "  ok=api.sandbox.protect(api.scripts.run,4029,{}); assert(not ok,'a table is not a script argument')"
        "  api.core.log('qh verbs ok')"
        "end}";
    struct FakeInstance instance = { "qh-verbs" };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_GameApi* game = (struct ToriRS_GameApi*)api.game;
    int logs;
    api.cache.struct_size = sizeof(api.cache);
    api.cache.varc_int = qhl_fake_varc_int; api.cache.varc_str = qhl_fake_varc_str;
    api.cache.set_varc_int = qhl_fake_set_varc_int; api.cache.set_varc_str = qhl_fake_set_varc_str;
    api.cache.enum_def = qhl_fake_enum_def; api.cache.npc_def = qhl_fake_npc_def; api.cache.loc_def = qhl_fake_loc_def;
    game->chat_add = qhl_fake_chat_add; game->hint_arrow_tile = qhl_fake_arrow_tile; game->hint_arrow = qhl_fake_arrow;
    game->sound = qhl_fake_sound; game->tick = qhl_fake_tick;
    game->container_size = qhl_fake_container_size; game->container_slot = qhl_fake_container_slot;
    game->world_entity_next = qhl_fake_wev_next; game->world_entity_to_main = qhl_fake_wev_to_main;
    api.scripts.run = qhl_fake_run;
    int index = PluginLua_AddScript(host, "qh-verbs", source, (int)strlen(source), 0u);
    CHECK(index >= 0, "quest helper verb test registers");
    if( index < 0 ) return;
    logs = g_logs;
    g_defs[index]->callbacks.on_start(&api, NULL);
    CHECK(g_logs == logs + 1, "cache, game and script verbs build the tables the contract states");
    CHECK(qhl_varc_set_id == 5 && qhl_varc_set_value == -9, "set_varc_int hands its id and value down");
    CHECK(qhl_chat_type == 0 && strcmp(qhl_chat_text, "<col=ff0000>Quest</col>") == 0,
        "chat_add keeps the line's markup");
    CHECK(qhl_run_ints == 2 && qhl_run_strings == 1 && strcmp(qhl_run_string, "name") == 0,
        "scripts.run passes integers as ints and strings as strings");
    g_defs[index]->callbacks.on_stop(&api, NULL);
}

/* Quest Helper port: the widget reads/edits and the engine-news handlers. */
static char qhlw_text[64];
static uint32_t qhlw_rgb;
static int qhlw_obj, qhlw_qty, qhlw_sprite;
static enum ToriRS_ContractResult qhlw_get(void* ctx, int32_t id, struct ToriRS_WidgetRef* out)
{ (void)ctx; *out=(struct ToriRS_WidgetRef){{77,(uint64_t)id,1}}; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult qhlw_detail(void* ctx, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetDetail* d)
{
    (void)ctx;
    CHECK(d->struct_size==sizeof(*d),"Lua states the detail struct size");
    d->id=(int32_t)ref.opaque[1]; d->index=-1; d->type=4; d->item_id=-1; d->sprite_id=-1;
    d->text_color=0xff9040; d->hidden=false; d->self_hidden=true; d->original_x=7; d->parent_id=0x00e70000;
    snprintf(d->text,sizeof(d->text),"%s","Select an option");
    snprintf(d->actions[0],sizeof(d->actions[0]),"%s","Continue");
    d->action_count=2;
    return TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult qhlw_dynamic(void* ctx, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetRef* out, size_t cap, size_t* count)
{
    (void)ctx;(void)ref;
    *count=3;
    for( size_t i=0;i<3 && i<cap;i++ ) out[i]=(struct ToriRS_WidgetRef){{77,100+i,1}};
    return cap<3 ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult qhlw_none(void* ctx, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetRef* out, size_t cap, size_t* count)
{ (void)ctx;(void)ref;(void)out;(void)cap; *count=0; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult qhlw_stale(void* ctx, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetRef* out, size_t cap, size_t* count)
{ (void)ctx;(void)ref;(void)out;(void)cap;(void)count; return TORIRS_CONTRACT_STALE_REFERENCE; }
static enum ToriRS_ContractResult qhlw_native_text(void* ctx, struct ToriRS_WidgetRef ref, char const* text)
{ (void)ctx;(void)ref; snprintf(qhlw_text,sizeof(qhlw_text),"%s",text); return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult qhlw_native_rgb(void* ctx, struct ToriRS_WidgetRef ref, uint32_t rgb)
{ (void)ctx;(void)ref; qhlw_rgb=rgb; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult qhlw_create_item(void* ctx, struct ToriRS_WidgetRef parent, char const* key, struct ToriRS_WidgetRef* out)
{ (void)ctx;(void)parent; CHECK(strcmp(key,"qh-tab-1")==0,"create_item hands its key down"); *out=(struct ToriRS_WidgetRef){{77,300,1}}; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult qhlw_set_item(void* ctx, struct ToriRS_WidgetRef ref, int32_t obj, int32_t qty)
{ (void)ctx;(void)ref; qhlw_obj=obj; qhlw_qty=qty; return TORIRS_CONTRACT_PENDING; }
static enum ToriRS_ContractResult qhlw_set_sprite(void* ctx, struct ToriRS_WidgetRef ref, int32_t sprite)
{ (void)ctx;(void)ref; qhlw_sprite=sprite; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult qhlw_listener(void* ctx, struct ToriRS_WidgetRef ref, char const* name,
    struct ToriRS_WidgetListenerArg* out, size_t cap, size_t* count)
{
    (void)ctx;(void)ref;
    if( strcmp(name,"bogus")==0 ) return TORIRS_CONTRACT_INVALID_ARGUMENT;
    if( strcmp(name,"onLoad")==0 ) return TORIRS_CONTRACT_UNAVAILABLE;
    *count=3;
    CHECK(cap>=3,"Lua offers room for a whole listener");
    out[0]=(struct ToriRS_WidgetListenerArg){.int_value=277};
    out[1]=(struct ToriRS_WidgetListenerArg){.int_value=-2147483645};
    out[2]=(struct ToriRS_WidgetListenerArg){.is_string=true,.string_value="Search"};
    return TORIRS_CONTRACT_OK;
}
static void test_quest_helper_widgets_events(struct ToriRS_PluginHost* host)
{
    char const source[] =
        "local seen={};return {id='qh-widgets',"
        "on_start=function(api)"
        "  local w=assert(api.widgets.get(15138817))"
        "  local d=assert(w:detail())"
        "  assert(d.id==15138817 and d.index==-1 and d.type==4 and d.text=='Select an option')"
        "  assert(d.text_color==0xff9040 and d.hidden==false and d.self_hidden==true and d.original_x==7)"
        "  assert(d.parent_id==0x00e70000 and #d.actions==2 and d.actions[1]=='Continue' and d.actions[2]=='')"
        "  local kids=assert(w:dynamic_children()); assert(#kids==3 and type(kids[3])=='userdata')"
        "  local st=assert(w:static_children()); assert(#st==0)"
        "  assert(w:nested_children()==nil,'a gone widget answers nil')"
        "  assert(w:set_native_text('<col=00ff00>Yes</col>'))"
        "  assert(w:set_native_text_color('#00ff00'))"
        "  local item=assert(w:create_item('qh-tab-1'))"
        "  local ok,why=item:set_item(995,25); assert(not ok and why=='pending')"
        "  ok,why=item:set_item(-1); assert(ok==false and why=='pending')"
        "  assert(w:set_sprite(1453))"
        "  local l=assert(w:listener('onInvTransmit')); assert(#l==3 and l[1]==277 and l[2]==-2147483645 and l[3]=='Search')"
        "  assert(w:listener('onLoad')==nil)"
        "  ok=api.sandbox.protect(w.listener,w,'bogus'); assert(not ok,'an unknown listener name raises')"
        "  api.core.log('qh widgets ok')"
        "end,"
        "on_var_changed=function(api,ev) seen[#seen+1]='v'..ev.varp..'='..ev.value..'<'..ev.old end,"
        "on_interface=function(api,ev) seen[#seen+1]=(ev.open and '+' or '-')..ev.group end,"
        "on_inventory_changed=function(api,ev) seen[#seen+1]='i'..ev.inv end,"
        "on_menu_build=function(api,ev)"
        "  local r=ev.rows[1]"
        "  assert(r.option=='Talk-to' and r.target=='<col=ffff00>Hans' and r.type==9 and r.identifier==12)"
        "  assert(r.param0==0 and r.param1==0 and r.text=='Talk-to @yel@Hans')"
        "  local q=ev.rows[2]; assert(q.option=='Walk here' and q.target=='' and q.type==23 and q.param0==51)"
        "  api.core.log(table.concat(seen,' '))"
        "end}";
    char const plain[] = "return {id='qh-plain', on_start=function(api) end}";
    struct FakeInstance instance = { "qh-widgets" };
    struct ToriRS_Api api = fake_api(&instance);
    api.widgets.get_widget=qhlw_get; api.widgets.detail=qhlw_detail;
    api.widgets.dynamic_children=qhlw_dynamic; api.widgets.static_children=qhlw_none; api.widgets.nested_children=qhlw_stale;
    api.widgets.set_native_text=qhlw_native_text; api.widgets.set_native_text_color=qhlw_native_rgb;
    api.widgets.create_item=qhlw_create_item; api.widgets.set_item=qhlw_set_item; api.widgets.set_sprite=qhlw_set_sprite;
    api.widgets.listener=qhlw_listener;
    int index = PluginLua_AddScript(host, "qh-widgets", source, (int)strlen(source), 0u);
    int plain_index = PluginLua_AddScript(host, "qh-plain", plain, (int)strlen(plain), 0u);
    CHECK(index >= 0 && plain_index >= 0, "quest helper widget test registers");
    if( index < 0 || plain_index < 0 ) return;
    CHECK(g_defs[index]->callbacks.on_var_changed && g_defs[index]->callbacks.on_interface &&
          g_defs[index]->callbacks.on_inventory_changed, "a script with the engine-news handlers carries their callbacks");
    CHECK(!g_defs[plain_index]->callbacks.on_var_changed && !g_defs[plain_index]->callbacks.on_interface &&
          !g_defs[plain_index]->callbacks.on_inventory_changed,
          "a script without them carries none, so the host never pays for it");
    int logs = g_logs;
    g_defs[index]->callbacks.on_start(&api, NULL);
    CHECK(g_logs == logs + 1, "widget detail, child lists, native edits, items, sprites and listeners");
    CHECK(strcmp(qhlw_text,"<col=00ff00>Yes</col>")==0 && qhlw_rgb==0x00ff00 && qhlw_sprite==1453 &&
          qhlw_obj==-1 && qhlw_qty==1, "the edits hand their arguments down (quantity defaults to 1)");
    struct ToriRS_VarChangedEvent var = { 101, 7, 5 };
    struct ToriRS_InterfaceEvent iface = { 231, true };
    struct ToriRS_InventoryChangedEvent inv = { 93 };
    g_defs[index]->callbacks.on_var_changed(&api, NULL, &var);
    g_defs[index]->callbacks.on_interface(&api, NULL, &iface);
    g_defs[index]->callbacks.on_inventory_changed(&api, NULL, &inv);
    struct ToriRS_MenuBuildEvent menu = {0};
    menu.row_count = 2;
    menu.rows[0] = (struct ToriRS_MenuRow){.text="Talk-to @yel@Hans",.npc_slot=12,.player_pid=-1,.target_id=3077,
        .component_id=-1,.slot=-1,.identifier=12,.param0=0,.param1=0,.type=9};
    menu.rows[1] = (struct ToriRS_MenuRow){.text="Walk here",.npc_slot=-1,.player_pid=-1,.target_id=-1,
        .component_id=-1,.slot=-1,.identifier=0,.param0=51,.param1=50,.type=23};
    logs = g_logs;
    int const disables = g_disabled_self;
    g_defs[index]->callbacks.on_menu_build(&api, NULL, &menu);
    CHECK(g_logs == logs + 1 && g_disabled_self == disables,
          "menu rows carry RuneLite's option/target/identifier/params/type, and the three events reached Lua");
    g_defs[index]->callbacks.on_stop(&api, NULL);
    g_defs[plain_index]->callbacks.on_stop(&api, NULL);
}

/* Quest Helper fix round: cache.sprite_image, panel.show, the actor fields
 * of the npc/player snapshots, on_loc_changed and on_map_anim. */
static int qhlf_shows;
static enum ToriRS_AssetState qhlf_sprite(struct ToriRS_Api* api, int id, int frame, struct ToriRS_ImageRef* out)
{
    (void)api;
    if( id == 5 && frame == 2 ) { out->value = 91; return TORIRS_ASSET_READY; }
    out->value = 0;
    return id == 6 ? TORIRS_ASSET_PENDING : TORIRS_ASSET_MISSING;
}
static void qhlf_show(struct ToriRS_Api* api) { (void)api; ++qhlf_shows; }
static bool qhlf_local_player(struct ToriRS_Api* api, struct ToriRS_PlayerSnapshot* out)
{
    (void)api;
    memset(out, 0, sizeof(*out));
    out->server_pid = 3; out->orientation = 512; out->anim_id = 808; out->interacting = 17; out->gender = 1;
    snprintf(out->name, sizeof(out->name), "%s", "Tester");
    return true;
}
static bool qhlf_npc_by_slot(struct ToriRS_Api* api, int slot, struct ToriRS_NpcSnapshot* out)
{
    (void)api;
    memset(out, 0, sizeof(*out));
    out->server_slot = slot; out->orientation = 1024; out->anim_id = -1; out->interacting = 32768 + 3;
    return true;
}
static void test_quest_helper_fix_seams(struct ToriRS_PluginHost* host)
{
    char const source[] =
        "local seen={};return {id='qh-fix',"
        "on_start=function(api)"
        "  local r,s=api.cache.sprite_image(5,2); assert(r==91 and s=='ready')"
        "  r,s=api.cache.sprite_image(6); assert(r==nil and s=='pending')"
        "  r,s=api.cache.sprite_image(7,0); assert(r==nil and s=='missing')"
        "  api.panel.show()"
        "  local p=assert(api.world.local_player())"
        "  assert(p.orientation==512 and p.anim_id==808 and p.interacting==17 and p.gender==1)"
        "  local n=assert(api.world.npc_by_slot(17))"
        "  assert(n.orientation==1024 and n.anim_id==-1 and n.interacting==32771)"
        "  api.core.log('qh fix ok')"
        "end,"
        "on_loc_changed=function(api,ev)"
        "  seen[#seen+1]='L'..ev.tile_x..','..ev.tile_z..':'..ev.old_loc_id..'>'..ev.loc_id..'/'..ev.shape..'/'..ev.angle..'/'..ev.old_shape"
        " end,"
        "on_item_search=function(api,ev)"
        "  if ev.query=='quest-helper' and ev.tradeable_only then return {952,995,'x',1925} end"
        " end,"
        "on_map_anim=function(api,ev)"
        "  seen[#seen+1]='G'..ev.spotanim..'@'..ev.tile_x..','..ev.tile_z..'+'..ev.height..'d'..ev.delay..'c'..ev.cycle"
        "  api.core.log(table.concat(seen,' '))"
        "end}";
    char const plain[] = "return {id='qh-fix-plain', on_start=function(api) end}";
    struct FakeInstance instance = { "qh-fix" };
    struct ToriRS_Api api = fake_api(&instance);
    api.cache.struct_size = sizeof(api.cache);
    api.cache.sprite_image = qhlf_sprite;
    api.panel.struct_size = sizeof(api.panel);
    api.panel.show = qhlf_show;
    api.world.struct_size = sizeof(api.world);
    api.world.local_player = qhlf_local_player;
    api.world.npc_by_slot = qhlf_npc_by_slot;
    int index = PluginLua_AddScript(host, "qh-fix", source, (int)strlen(source), 0u);
    int plain_index = PluginLua_AddScript(host, "qh-fix-plain", plain, (int)strlen(plain), 0u);
    CHECK(index >= 0 && plain_index >= 0, "quest helper fix test registers");
    if( index < 0 || plain_index < 0 ) return;
    CHECK(g_defs[index]->callbacks.on_loc_changed && g_defs[index]->callbacks.on_map_anim,
          "a script with on_loc_changed/on_map_anim carries their callbacks");
    CHECK(!g_defs[plain_index]->callbacks.on_loc_changed && !g_defs[plain_index]->callbacks.on_map_anim,
          "a script without them carries none");
    qhlf_shows = 0;
    int logs = g_logs;
    g_defs[index]->callbacks.on_start(&api, NULL);
    CHECK(g_logs == logs + 1, "sprite_image answers, panel.show, and the actor fields of both snapshots");
    CHECK(qhlf_shows == 1, "panel.show reaches the host");
    struct ToriRS_LocChangedEvent loc = { 3226, 3240, 0, 0, 1, 1531, 1530, 0, 0 };
    struct ToriRS_MapAnimEvent anim = { 3227, 3241, 1, 86, 40, 5, 900 };
    logs = g_logs;
    g_defs[index]->callbacks.on_loc_changed(&api, NULL, &loc);
    g_defs[index]->callbacks.on_map_anim(&api, NULL, &anim);
    CHECK(g_logs == logs + 1 && strstr(g_last_log, "L3226,3240:1530>1531/0/1/0 G86@3227,3241+40d5c900") != NULL,
          "loc change and map graphic events carry every field");
    {
        struct ToriRS_ItemSearchEvent search;
        memset(&search, 0, sizeof(search));
        snprintf(search.query, sizeof(search.query), "%s", "spade");
        search.tradeable_only = true;
        g_defs[index]->callbacks.on_item_search(&api, NULL, &search);
        CHECK(!search.consumed, "a handler answering nil leaves the GE search to the client");
        snprintf(search.query, sizeof(search.query), "%s", "quest-helper");
        g_defs[index]->callbacks.on_item_search(&api, NULL, &search);
        CHECK(search.consumed && search.count == 3 && search.ids[0] == 952 && search.ids[2] == 1925,
              "a returned id list answers the GE search (non-integers skipped)");
    }
    g_defs[index]->callbacks.on_stop(&api, NULL);
    g_defs[plain_index]->callbacks.on_stop(&api, NULL);
}

/* Quest Helper client-APIs round: menu.set_order / set_text / shown,
 * cache.set_varbit / seq_def, client.open_url / worldmap_jump, on_command,
 * and the manifest's memory_mb= through sandbox.memory_limit. The fakes
 * record what the binding handed down, so the case pins the BINDING: 1-based
 * rows become payload indices, tags and the deprioritized flag pass through,
 * the args are split at spaces, "consume" comes back as CONSUME. */
static struct ToriRS_MenuRowRef qhc_refs[8];
static int qhc_ref_count, qhc_text_row;
static char qhc_text[128], qhc_url[128];
static int qhc_varbit_id, qhc_varbit_value, qhc_jump[3];
static enum ToriRS_Result qhc_menu_set_order(struct ToriRS_Api* api, struct ToriRS_MenuBuildEvent* menu,
    struct ToriRS_MenuRowRef const* rows, int count)
{
    (void)api; (void)menu;
    if( count == 3 )
    {
        qhc_ref_count = count;
        for( int i = 0; i < count; i++ ) qhc_refs[i] = rows[i];
    }
    return count > 0 && rows[0].row >= TORIRS_PLUGIN_MENU_ROWS_MAX ? TORIRS_RESULT_INVALID : TORIRS_RESULT_OK;
}
static enum ToriRS_Result qhc_menu_set_text(struct ToriRS_Api* api, struct ToriRS_MenuBuildEvent* menu, int row, char const* text)
{ (void)api; (void)menu; qhc_text_row = row; snprintf(qhc_text, sizeof(qhc_text), "%s", text); return TORIRS_RESULT_OK; }
static bool qhc_menu_shown(struct ToriRS_Api* api, struct ToriRS_Rect* out)
{ (void)api; *out = (struct ToriRS_Rect){ 10, 20, 30, 40 }; return true; }
static enum ToriRS_Result qhc_set_varbit(struct ToriRS_Api* api, int id, int value)
{ (void)api; qhc_varbit_id = id; qhc_varbit_value = value; return id == 4070 ? TORIRS_RESULT_OK : TORIRS_RESULT_INVALID; }
static enum ToriRS_AssetState qhc_seq_def(struct ToriRS_Api* api, int id, struct ToriRS_SeqDef* out)
{
    static int const lengths[3] = { 3, 4, 5 };
    (void)api;
    if( id != 866 ) return id == 867 ? TORIRS_ASSET_PENDING : TORIRS_ASSET_MISSING;
    out->id = 866; out->frame_count = 3; out->duration = 12; out->frame_step = -1; out->skeletal = false;
    out->frame_lengths.count = 3;
    for( int i = 0; i < 3 && i < out->frame_lengths.capacity; i++ ) out->frame_lengths.values[i] = lengths[i];
    return TORIRS_ASSET_READY;
}
static enum ToriRS_Result qhc_open_url(struct ToriRS_Api* api, char const* url)
{ (void)api; snprintf(qhc_url, sizeof(qhc_url), "%s", url); return TORIRS_RESULT_UNSUPPORTED; }
static enum ToriRS_Result qhc_worldmap_jump(struct ToriRS_Api* api, int x, int z, int level)
{ (void)api; qhc_jump[0] = x; qhc_jump[1] = z; qhc_jump[2] = level; return TORIRS_RESULT_OK; }
static void test_quest_helper_client_apis(struct ToriRS_PluginHost* host)
{
    char const source[] =
        "return {id='qh-client',"
        "on_start=function(api)"
        "  local ok,why=api.cache.set_varbit(4070,0); assert(ok and why=='ok')"
        "  ok,why=api.cache.set_varbit(1,2); assert(not ok and why=='invalid')"
        "  local d,st=api.cache.seq_def(866); assert(st=='ready' and d.frame_count==3 and d.duration==12)"
        "  assert(#d.frame_lengths==3 and d.frame_lengths[3]==5 and d.frame_step==-1 and d.skeletal==false)"
        "  d,st=api.cache.seq_def(867); assert(d==nil and st=='pending')"
        "  d,st=api.cache.seq_def(1); assert(d==nil and st=='missing')"
        "  ok,why=api.client.open_url('https://discord.gg/XCfwNnz6RB'); assert(not ok and why=='unsupported')"
        "  ok,why=api.client.worldmap_jump(3222,3218); assert(ok)"
        "  local b=api.menu.shown(); assert(b.x==10 and b.y==20 and b.width==30 and b.height==40)"
        "  assert(api.sandbox.memory_limit()==16*1024*1024)"
        "  api.core.log('qh client start')"
        "end,"
        "on_menu_build=function(api,ev)"
        "  assert(api.menu.set_order({{row=1},{row=2,deprioritized=true},{tag=41}}))"
        "  local ok,why=api.menu.set_order({{row=0}}); assert(not ok and why=='invalid')"
        "  assert(not api.sandbox.protect(api.menu.set_order,{{}}),'an entry names a row or a tag')"
        "  assert(api.menu.set_text(2,'Talk-to @yel@Veos'))"
        "end,"
        "on_command=function(api,ev)"
        "  api.core.log('cmd '..ev.name..' #'..#ev.args..' '..table.concat(ev.args,'|')..' ['..ev.text..']')"
        "  if ev.name=='questhelperdebug' then return 'consume' end "
        "end}";
    char const big[] =
        "return {id='qh-mem',on_start=function(api)"
        "  assert(api.sandbox.memory_limit()==24*1024*1024,'memory_mb= sets the cap')"
        "  BIG={} for i=1,18 do BIG[i]=string.rep(string.char(64+i),1024*1024) end"
        "  api.core.log('qh mem ok '..#BIG)"
        "end}";
    char const small[] =
        "return {id='qh-mem16',on_start=function(api)"
        "  local t={} for i=1,18 do t[i]=string.rep(string.char(64+i),1024*1024) end"
        "  api.core.log('qh mem16 must not get here')"
        "end}";
    char const huge[] = "return {id='qh-mem-huge',on_start=function(api) api.core.log('cap '..api.sandbox.memory_limit()) end}";
    struct FakeInstance instance = { "qh-client" };
    struct ToriRS_Api api = fake_api(&instance);
    struct ToriRS_ClientApi client = *api.client;
    struct PluginLuaScriptOptions mem_options = { NULL, 0, 24 };
    struct PluginLuaScriptOptions huge_options = { NULL, 0, 200 };
    int logs;
    api.menu.struct_size = sizeof(api.menu);
    api.menu.set_order = qhc_menu_set_order;
    api.menu.set_text = qhc_menu_set_text;
    api.menu.shown = qhc_menu_shown;
    api.cache.struct_size = sizeof(api.cache);
    api.cache.set_varbit = qhc_set_varbit;
    api.cache.seq_def = qhc_seq_def;
    client.open_url = qhc_open_url;
    client.worldmap_jump = qhc_worldmap_jump;
    api.client = &client;
    int index = PluginLua_AddScript(host, "qh-client", source, (int)strlen(source), 0u);
    CHECK(index >= 0, "client-API test registers");
    if( index < 0 ) return;
    CHECK(g_defs[index]->callbacks.on_command != NULL, "a script with on_command carries the callback");
    logs = g_logs;
    g_defs[index]->callbacks.on_start(&api, NULL);
    CHECK(g_logs == logs + 1, "set_varbit, seq_def, open_url, worldmap_jump, menu.shown and memory_limit answer");
    CHECK(qhc_varbit_id == 1 && qhc_varbit_value == 2, "set_varbit hands its id and value down");
    CHECK(strcmp(qhc_url, "https://discord.gg/XCfwNnz6RB") == 0, "open_url hands the url down");
    CHECK(qhc_jump[0] == 3222 && qhc_jump[1] == 3218 && qhc_jump[2] == 0, "worldmap_jump's level defaults to 0");
    {
        struct ToriRS_MenuBuildEvent ev = { 0 };
        int disabled = g_disabled_self;
        ev.row_count = 3;
        g_defs[index]->callbacks.on_menu_build(&api, NULL, &ev);
        CHECK(g_disabled_self == disabled, "the menu edits ran");
        CHECK(qhc_text_row == 1 && strcmp(qhc_text, "Talk-to @yel@Veos") == 0, "set_text's row is 1-based in Lua");
    }
    CHECK(qhc_ref_count == 3 && qhc_refs[0].row == 0 && !qhc_refs[0].deprioritized && qhc_refs[1].row == 1 &&
              qhc_refs[1].deprioritized && qhc_refs[2].row == -1 && qhc_refs[2].tag == 41,
          "set_order's rows are 0-based payload indices, tags pass with row -1, deprioritized passes");
    {
        struct ToriRS_CommandEvent cmd = { "qh-inv  a b", "qh-inv", " a b" };
        logs = g_logs;
        CHECK(g_defs[index]->callbacks.on_command(&api, NULL, &cmd) == TORIRS_CALLBACK_CONTINUE,
              "a command the handler leaves alone continues");
        CHECK(g_logs == logs + 1 && strstr(g_last_log, "cmd qh-inv #2 a|b [qh-inv  a b]") != NULL,
              "on_command splits the args at spaces");
        struct ToriRS_CommandEvent dbg = { "questhelperdebug", "questhelperdebug", "" };
        CHECK(g_defs[index]->callbacks.on_command(&api, NULL, &dbg) == TORIRS_CALLBACK_CONSUME,
              "'consume' keeps a command from the server");
    }
    g_defs[index]->callbacks.on_stop(&api, NULL);

    {
        /* The runtime finds a script by the api's plugin id: one each. */
        struct FakeInstance mi = { "qh-mem" }, di = { "qh-mem16" }, ti = { "qh-mem-huge" };
        struct ToriRS_Api mapi = fake_api(&mi), dapi = fake_api(&di), tapi = fake_api(&ti);
        int mem = PluginLua_AddScriptEx(host, "qh-mem", big, (int)strlen(big), 0u, &mem_options);
        int def = PluginLua_AddScript(host, "qh-mem16", small, (int)strlen(small), 0u);
        int top = PluginLua_AddScriptEx(host, "qh-mem-huge", huge, (int)strlen(huge), 0u, &huge_options);
        CHECK(mem >= 0 && def >= 0 && top >= 0, "memory_mb scripts register");
        if( mem < 0 || def < 0 || top < 0 ) return;
        logs = g_logs;
        g_defs[mem]->callbacks.on_start(&mapi, NULL);
        CHECK(g_logs == logs + 1 && strstr(g_last_log, "qh mem ok") != NULL, "memory_mb=24 holds an 18 MB value");
        {
            /* A memory error is never caught by sandbox.protect: the script
             * is disabled (the one deliberate disable this case adds). */
            int const disabled = g_disabled_self;
            logs = g_logs;
            g_defs[def]->callbacks.on_start(&dapi, NULL);
            CHECK(g_logs == logs && g_disabled_self == disabled + 1 && strstr(g_disable_reason, "not enough memory"),
                  "the default 16 MB cap refuses 18 MB");
        }
        logs = g_logs;
        g_defs[top]->callbacks.on_start(&tapi, NULL);
        CHECK(g_logs == logs + 1 && strstr(g_last_log, "cap 67108864") != NULL, "memory_mb past 64 is clamped to 64");
        g_defs[mem]->callbacks.on_stop(&mapi, NULL);
        g_defs[def]->callbacks.on_stop(&dapi, NULL);
        g_defs[top]->callbacks.on_stop(&tapi, NULL);
    }
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
    test_sandbox(&host);
    test_quest_helper_verbs(&host);
    test_quest_helper_widgets_events(&host);
    test_quest_helper_fix_seams(&host);
    test_quest_helper_client_apis(&host);
    test_draw_verbs(&host);
    test_draw_verbs_arguments(&host);
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
    test_product_behavior(&host,"../script/plugins/npc_indicators.lua",
        "plugin/test/npc_indicators_behavior.lua","npc-indicators-behavior");
    test_product_behavior(&host,"../script/plugins/visual_metronome.lua",
        "plugin/test/visual_metronome_behavior.lua","visual-metronome-behavior");
    /* (olm_cycle.lua is loaded only here: the script table is full) */
    test_product_behavior(&host,"../script/plugins/olm_cycle.lua",
        "plugin/test/olm_cycle_behavior.lua","olm-cycle-behavior");
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
    /* The script table holds PLUGIN_LUA_MAX_SCRIPTS (32), and the bundled
     * scripts and products above fill it; the pump probes start an empty one.
     * The disable count is not reset_fake's, so the check below still spans
     * both halves. */
    PluginLua_Shutdown();
    reset_fake();
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
    puts("lua plugin test: runtime, reload, descriptors, builders, and 19 bundled scripts passed");
    return 0;
}
