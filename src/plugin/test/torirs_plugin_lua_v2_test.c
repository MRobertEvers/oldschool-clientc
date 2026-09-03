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

static struct ToriRS_PluginDefV2 const* g_defs[PLUGIN_LUA_TEST_MAX];
static void (*g_reload[PLUGIN_LUA_TEST_MAX])(struct ToriRS_PluginHost*, int, void*);
static void* g_reload_user[PLUGIN_LUA_TEST_MAX];
static int g_registered;
static int g_failures;
static int g_reported_errors;
static int g_logs;
static int g_enabled_calls;
static int g_ui_enabled;
static int g_ui_updates;
static int g_draws;
static int g_headings;
static int g_surfaces;
static int g_reasons;
static int g_disabled_self;
static int g_config_dispatches;
static char g_disable_reason[192];
static int g_log_disable_snapshot;

#define CHECK(condition, message)                                                       \
    do                                                                                  \
    {                                                                                   \
        if( !(condition) )                                                              \
        {                                                                               \
            fprintf(stderr, "lua v2 test: %s\n", (message));                          \
            g_failures++;                                                               \
        }                                                                               \
    } while( 0 )

int
PluginHost_RegisterV2(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PluginDefV2 const* def)
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

static char const* fake_plugin_id(struct ToriRS_ApiV2* api)
{
    return ((struct FakeInstance*)api->instance)->id;
}
static void fake_log(struct ToriRS_ApiV2* api, char const* format, ...)
{
    (void)api; (void)format;
    g_log_disable_snapshot = g_disabled_self;
    g_logs++;
}
static bool fake_config_get_int(struct ToriRS_ApiV2* api, char const* key, int* out)
{
    (void)api;
    if( strcmp(key, "answer") != 0 ) return false;
    *out = 42;
    return true;
}
static enum ToriRS_Result
fake_config_set(struct ToriRS_ApiV2* api, char const* key, char const* value)
{
    char const* id = ((struct FakeInstance*)api->instance)->id;
    (void)value;
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
static struct ToriRS_UiNodeRef fake_ui_ref(struct ToriRS_ApiV2* api, char const* name)
{
    (void)api;
    struct ToriRS_UiNodeRef ref = { strcmp(name, "frame.chat.button.report") == 0 ? 77u : 0u };
    return ref;
}
static enum ToriRS_Result fake_ui_update(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef node,
    uint32_t facets,
    struct ToriRS_UiNode const* value)
{
    (void)api;
    CHECK(node.value == 77, "ui.update receives stable ref");
    CHECK(facets == (TORIRS_UI_FACET_APPEARANCE | TORIRS_UI_FACET_ACTIONS),
        "ui.update receives parsed facets");
    CHECK(value && value->label && strcmp(value->label, "Camera") == 0,
        "ui.update receives node value");
    g_ui_updates++;
    return TORIRS_RESULT_OK;
}
static enum ToriRS_Result fake_ui_set_enabled(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef node,
    bool enabled)
{
    (void)api;
    CHECK(node.value == 77, "ui.set_enabled receives stable ref");
    g_ui_enabled += enabled ? 1 : -1;
    return TORIRS_RESULT_OK;
}
static void fake_frame_selection(struct ToriRS_ApiV2* api, struct ToriRS_FrameSelection* out)
{
    struct FakeInstance* instance = api->instance;
    snprintf(out->active_id, sizeof(out->active_id), "%s",
        instance->active_frame ? instance->active_frame : "");
}
static void fake_disable_self(struct ToriRS_ApiV2* api, char const* reason)
{
    (void)api;
    CHECK(reason && reason[0], "disable_self receives an actionable reload reason");
    snprintf(g_disable_reason, sizeof(g_disable_reason), "%s", reason ? reason : "");
    g_disabled_self++;
}
static void fake_draw_rect(
    struct ToriRS_DrawBuilder* draw,
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
static void fake_surface(
    struct ToriRS_FrameBuilder* frame,
    int surface,
    struct ToriRS_Rect rect)
{
    (void)frame;
    CHECK(surface == TORIRS_SURFACE_VIEWPORT, "frame surface name mapped");
    CHECK(rect.width == 512 && rect.height == 334, "frame surface rectangle forwarded");
    g_surfaces++;
}
static void fake_reason(struct ToriRS_FrameBuilder* frame, char const* text)
{
    (void)frame;
    CHECK(strcmp(text, "built by Lua") == 0, "frame reason forwarded");
    g_reasons++;
}

static struct ToriRS_ApiV2
fake_api(struct FakeInstance* instance)
{
    struct ToriRS_ApiV2 api;
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    api.major_version = TORIRS_PLUGIN_API_V2_MAJOR;
    api.minor_version = TORIRS_PLUGIN_API_V2_MINOR;
    api.instance = instance;
    api.core.struct_size = sizeof(api.core);
    api.core.log = fake_log;
    api.core.plugin_id = fake_plugin_id;
    api.config.struct_size = sizeof(api.config);
    api.config.get_int = fake_config_get_int;
    api.config.set = fake_config_set;
    api.ui.struct_size = sizeof(api.ui);
    api.ui.ref = fake_ui_ref;
    api.ui.update = fake_ui_update;
    api.ui.set_enabled = fake_ui_set_enabled;
    api.frame.struct_size = sizeof(api.frame);
    api.frame.selection = fake_frame_selection;
    static struct ToriRS_ClientApiV2 client;
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
    static char const BAD_MODE_SOURCE[] =
        "return {id='bad-mode',ui_contributions={{node='frame.viewport',"
        "mode='bogus',value={}}}}";
    static char const CYCLIC_FACETS_SOURCE[] =
        "local f={};f[1]=f;return {id='cyclic-facets',ui_contributions={{"
        "node='frame.viewport',facets=f,value={}}}}";
    static char const BAD_ACTION_SOURCE[] =
        "return {id='bad-action',ui_contributions={{node='frame.viewport',"
        "facets={'actions'},value={actions={{}}}}}}";
    static char const INVALID_ENUM_SOURCE[] =
        /* Deliberately collides with lua-v2-test in the 32-slot id table. */
        "return {id='invalid-enum-59',on_start=function(api) api.placement.area(99) end}";
    static char const BUDGET_SOURCE[] =
        "return {id='budget-after-reentry',config={{key='answer',type='int',default='42'}},"
        "on_config_changed=function() end,on_start=function(api)"
        " assert(api.config.set('answer',43));local n=0;"
        " for i=1,10000000 do n=n+i end;api.core.log(n) end}";
    static char const NESTED_FAULT_SOURCE[] =
        "return {id='nested-fault',config={{key='answer',type='int',default='42'}},"
        "on_config_changed=function() error('inner boom') end,"
        "on_start=function(api) assert(api.config.set('answer',43));"
        "api.core.log('outer remained safe') end}";
    static char const DISABLE_SOURCE[] =
        "return {id='explicit-disable',on_start=function(api)"
        "api.client.disable_self('requested stop');api.core.log('must not run') end}";
    static char const BAD_PIXELS_SOURCE[] =
        "return {id='bad-pixels',on_start=function(api)"
        "api.assets.image_compose('x',2,2,{1,'bad',3,4}) end}";
    static char const DRIFT_SOURCE[] =
        "return {id='lua-v2-test',title='Lua V2 Test',version='2',"
        "frames={{id='changed',title='Changed',canvas='fixed',width=765,height=503,"
        "build=function() return 'ready' end}}}";
    static char const SOURCE[] =
        "return { id='lua-v2-test', title='Lua V2 Test', version='2',"
        " config={{key='answer',type='int',default='42'}},"
        " ui_contributions={{node='frame.chat.button.report',"
        " mode='replace_or_provide',facets={'appearance','actions'},"
        " value={flags=3,label='Camera',action='capture',actions={'capture'}}}},"
        " frames={"
        "  {id='ready',title='Ready',canvas='fixed',width=765,height=503,"
        "   build=function(api,frame,ctx)"
        "    assert(ctx.offer_id=='ready' and ctx.canvas=='fixed')"
        "    frame.surface('viewport',{x=0,y=0,width=512,height=334})"
        "    frame.reason('built by Lua');return 'ready'"
        "   end,draw=function(api,draw) draw.rect(1,2,3,4,0xffffff,255) end},"
        "  {id='waiting',title='Waiting',canvas='window',min_width=640,min_height=480,"
        "   build=function() return 'pending' end},"
        "  {id='failure',title='Failure',canvas='fixed',width=765,height=503,"
        "   build=function() return 'error' end},"
        "  {id='invalid-surface',title='Invalid Surface',canvas='fixed',width=765,height=503,"
        "   build=function(api,frame)"
        "    frame.surface(99,{x=0,y=0,width=1,height=1});return 'ready' end}"
        " },"
        " on_start=function(api)"
        "  assert(api.log==nil and api.role==nil and api.window==nil and api.layout==nil)"
        "  assert(api.chrome==nil and api.entity==nil and api.object_create==nil)"
        "  assert(api.local_player==nil and api.image_load==nil and api.cfg_set==nil)"
        "  assert(pcall==nil and xpcall==nil and setmetatable==nil and getmetatable==nil)"
        "  assert(warn==nil)"
        "  assert(api.core.plugin_id()=='lua-v2-test' and api.config.answer==42)"
        "  local node=api.ui.ref('frame.chat.button.report')"
        "  assert(api.ui.set_enabled(node,true))"
        "  assert(api.ui.update(node,{'appearance','actions'},"
        "   {flags=3,label='Camera',action='capture',actions={'capture'}}))"
        "  api.core.log('started')"
        " end,"
        " on_config_changed=function(api,key)"
        "  assert(key=='answer' and api.core.plugin_id()=='lua-v2-test')"
        "  api.core.log('changed')"
        " end,"
        " on_draw_canvas=function(api,draw)"
        "  assert(api.config.set('answer',43));draw.rect(1,2,3,4,0xffffff,255)"
        " end,"
        " on_ui_build=function(api,panel,view) assert(view=='page');panel.heading('Native V2') end"
        "}";
    struct FakeInstance instance = { "lua-v2-test", "lua-v2-test/ready" };
    struct ToriRS_ApiV2 api = fake_api(&instance);
    struct ToriRS_DrawBuilder draw;
    struct ToriRS_PanelBuilder panel;
    struct ToriRS_FrameBuilder frame;
    struct ToriRS_FrameBuildContext context;
    int index;

    CHECK(PluginLua_AddScript(host, "empty", "", 0) < 0,
        "empty source is refused without a debug assertion");
    CHECK(PluginLua_AddScript(host, "legacy", LEGACY_SOURCE,
              (int)strlen(LEGACY_SOURCE)) < 0,
        "legacy name field is not accepted as a V2 id");
    CHECK(PluginLua_AddScript(host, "bad-mode", BAD_MODE_SOURCE,
              (int)strlen(BAD_MODE_SOURCE)) < 0,
        "malformed contribution mode is a protected load failure");
    CHECK(PluginLua_AddScript(host, "cyclic-facets", CYCLIC_FACETS_SOURCE,
              (int)strlen(CYCLIC_FACETS_SOURCE)) < 0,
        "cyclic facet tables cannot recurse in native code");
    CHECK(PluginLua_AddScript(host, "bad-action", BAD_ACTION_SOURCE,
              (int)strlen(BAD_ACTION_SOURCE)) < 0,
        "malformed contribution action is a protected load failure");
    index = PluginLua_AddScript(host, "lua-v2-test", SOURCE, (int)strlen(SOURCE));

    CHECK(index == 0, "runtime script registered through V2");
    CHECK(g_defs[0] && strcmp(g_defs[0]->id, "lua-v2-test") == 0, "Lua name is V2 id");
    CHECK(g_defs[0]->config && g_defs[0]->config->items, "V2 config schema retained");
    CHECK(g_defs[0]->ui_contributions != NULL, "V2 UI contribution retained");
    CHECK(g_defs[0]->frames != NULL, "V2 frame offers retained");
    if( g_defs[0] && g_defs[0]->ui_contributions )
    {
        struct ToriRS_UiContribution const* contribution = g_defs[0]->ui_contributions;
        CHECK(strcmp(contribution->node, "frame.chat.button.report") == 0,
            "canonical contribution name copied");
        CHECK(contribution->mode == TORIRS_UI_REPLACE_OR_PROVIDE,
            "contribution mode parsed");
        CHECK(contribution->facets ==
                (TORIRS_UI_FACET_APPEARANCE | TORIRS_UI_FACET_ACTIONS),
            "contribution facets parsed");
    }

    g_defs[0]->callbacks.on_start(&api, NULL);
    memset(&draw, 0, sizeof(draw));
    draw.struct_size = sizeof(draw);
    draw.rect = fake_draw_rect;
    g_defs[0]->callbacks.on_draw_canvas(&api, NULL, &draw);
    memset(&panel, 0, sizeof(panel));
    panel.struct_size = sizeof(panel);
    panel.heading = fake_heading;
    g_defs[0]->callbacks.on_ui_build(&api, NULL, &panel, TORIRS_PANEL_VIEW_PAGE);
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.surface = fake_surface;
    frame.reason = fake_reason;
    memset(&context, 0, sizeof(context));
    context.struct_size = sizeof(context);
    context.canvas = TORIRS_FRAME_CANVAS_FIXED;
    context.logical_canvas = (struct ToriRS_Rect){ 0, 0, 765, 503 };
    context.offer_id = "ready";
    CHECK(g_defs[0]->frames[0].build(&api, NULL, &frame, &context) == TORIRS_FRAME_READY,
        "Lua frame build returns READY");
    context.offer_id = "waiting";
    context.canvas = TORIRS_FRAME_CANVAS_WINDOW;
    CHECK(g_defs[0]->frames[1].build(&api, NULL, &frame, &context) == TORIRS_FRAME_PENDING,
        "Lua frame build returns PENDING");
    context.offer_id = "failure";
    context.canvas = TORIRS_FRAME_CANVAS_FIXED;
    CHECK(g_defs[0]->frames[2].build(&api, NULL, &frame, &context) == TORIRS_FRAME_ERROR,
        "Lua frame build returns ERROR");
    context.offer_id = "invalid-surface";
    CHECK(g_defs[0]->frames[3].build(&api, NULL, &frame, &context) == TORIRS_FRAME_ERROR,
        "invalid integer surface becomes a caught Lua frame error");
    CHECK(g_disabled_self == 1 && g_reported_errors == 0 && g_enabled_calls == 0,
        "invalid frame enum refuses the provider through the V2 lifecycle");
    g_defs[0]->frames[0].draw(&api, NULL, &draw);
    CHECK(g_logs == 2 && g_ui_enabled == 1 && g_ui_updates == 1,
        "canonical and synchronously reentrant API calls reached V2 functions");
    CHECK(g_draws == 2 && g_headings == 1, "scoped draw/panel builders reached V2 functions");
    CHECK(g_surfaces == 1 && g_reasons == 1, "scoped frame builder reached V2 functions");
    CHECK(g_config_dispatches == 1,
        "config.set synchronously dispatched on_config_changed once");
    CHECK(g_reload[0] != NULL, "source reload handler installed");
    g_reload[0](host, 0, g_reload_user[0]);
    g_defs[0]->callbacks.on_start(&api, NULL);
    CHECK(g_logs == 3 && g_ui_updates == 2, "reload rebuilt and restarted the VM");
    context.offer_id = "ready";
    CHECK(g_defs[0]->frames[0].build(&api, NULL, &frame, &context) == TORIRS_FRAME_READY,
        "reload rebuilt the frame-offer function");

    CHECK(PluginLua_TestReplaceSource(0, DRIFT_SOURCE, (int)strlen(DRIFT_SOURCE)),
        "test source replacement reached retained reload bytes");
    g_reload[0](host, 0, g_reload_user[0]);
    CHECK(strcmp(g_defs[0]->frames[0].id, "ready") == 0 &&
            strcmp(g_defs[0]->frames[3].id, "invalid-surface") == 0,
        "rejected reload restores catalogue-backed offer strings");
    g_defs[0]->callbacks.on_start(&api, NULL);
    CHECK(g_disabled_self == 2,
        "changed static frame descriptor is refused on restart");

    CHECK(PluginLua_AddScript(host, "invalid-enum-59", INVALID_ENUM_SOURCE,
              (int)strlen(INVALID_ENUM_SOURCE)) == 1,
        "enum misuse probe registered");
    struct FakeInstance invalid_instance = { "invalid-enum-59", NULL };
    struct ToriRS_ApiV2 invalid_api = fake_api(&invalid_instance);
    g_defs[1]->callbacks.on_start(&invalid_api, NULL);
    CHECK(g_disabled_self == 3 && g_reported_errors == 0 && g_enabled_calls == 0,
        "out-of-range integer enum becomes a caught V2 lifecycle fault");
    CHECK(strstr(g_disable_reason, "placement area must be between") != NULL,
        "integer enum fault names the invalid domain");

    CHECK(PluginLua_AddScript(host, "budget-after-reentry", BUDGET_SOURCE,
              (int)strlen(BUDGET_SOURCE)) == 2,
        "nested-budget probe registered");
    struct FakeInstance budget_instance = { "budget-after-reentry", NULL };
    struct ToriRS_ApiV2 budget_api = fake_api(&budget_instance);
    g_defs[2]->callbacks.on_start(&budget_api, NULL);
    CHECK(g_disabled_self == 4 &&
            strstr(g_disable_reason, "instruction budget exhausted") != NULL,
        "nested callback cannot disarm the outer instruction budget");

    int const logs_before_nested_fault = g_logs;
    int const disables_before_nested_fault = g_disabled_self;
    CHECK(PluginLua_AddScript(host, "nested-fault", NESTED_FAULT_SOURCE,
              (int)strlen(NESTED_FAULT_SOURCE)) == 3,
        "nested-fault probe registered");
    struct FakeInstance nested_instance = { "nested-fault", NULL };
    struct ToriRS_ApiV2 nested_api = fake_api(&nested_instance);
    g_defs[3]->callbacks.on_start(&nested_api, NULL);
    CHECK(g_logs == logs_before_nested_fault + 1 &&
            g_log_disable_snapshot == disables_before_nested_fault,
        "inner fault leaves the outer API live until it unwinds");
    CHECK(g_disabled_self == disables_before_nested_fault + 1 &&
            strstr(g_disable_reason, "inner boom") != NULL,
        "inner fault is disabled at the outer callback boundary");

    int const logs_before_disable = g_logs;
    CHECK(PluginLua_AddScript(host, "explicit-disable", DISABLE_SOURCE,
              (int)strlen(DISABLE_SOURCE)) == 4,
        "explicit-disable probe registered");
    struct FakeInstance disable_instance = { "explicit-disable", NULL };
    struct ToriRS_ApiV2 disable_api = fake_api(&disable_instance);
    g_defs[4]->callbacks.on_start(&disable_api, NULL);
    CHECK(g_logs == logs_before_disable && strstr(g_disable_reason, "requested stop") != NULL,
        "disable_self unwinds Lua before native teardown and later API use");

    CHECK(PluginLua_AddScript(host, "bad-pixels", BAD_PIXELS_SOURCE,
              (int)strlen(BAD_PIXELS_SOURCE)) == 5,
        "bad-pixels probe registered");
    struct FakeInstance pixels_instance = { "bad-pixels", NULL };
    struct ToriRS_ApiV2 pixels_api = fake_api(&pixels_instance);
    g_defs[5]->callbacks.on_start(&pixels_api, NULL);
    CHECK(strstr(g_disable_reason, "pixel 2 is not an integer") != NULL,
        "malformed pixel input faults without retaining a host allocation");
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
    CHECK(g_registered == 16, "all sixteen bundled scripts registered");
}

int
main(void)
{
    struct ToriRS_PluginHost host = { 0 };
    reset_fake();
    test_runtime(&host);
    PluginLua_Shutdown();
    reset_fake();
    test_bundled_scripts(&host);
    PluginLua_Shutdown();
    if( g_failures )
    {
        fprintf(stderr, "lua v2 test: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("lua v2 test: runtime, reload, descriptors, builders, and 16 bundled scripts passed");
    return 0;
}
