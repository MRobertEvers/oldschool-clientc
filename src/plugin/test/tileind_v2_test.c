#include "plugin/torirs_plugin_v2.h"

#include <stdio.h>
#include <string.h>

extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_TILEIND;

static int g_checks;
static int g_failures;

#define CHECK(condition, message)                                                               \
    do                                                                                          \
    {                                                                                           \
        g_checks++;                                                                             \
        if( !(condition) )                                                                      \
        {                                                                                       \
            g_failures++;                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));                \
        }                                                                                       \
    } while( 0 )

struct DrawCall
{
    int x;
    int z;
    int level;
    uint32_t fill;
    uint32_t outline;
    int alpha;
};

struct Fake
{
    bool show_hover;
    bool show_dest;
    bool have_hover;
    bool have_player;
    int hover_x;
    int hover_z;
    int hover_level;
    struct ToriRS_PluginPlayerSnap player;
    struct DrawCall calls[3];
    int call_count;
};

static struct Fake*
fake_api(struct ToriRS_ApiV2* api)
{
    return api->instance;
}

static bool
fake_config_bool(
    struct ToriRS_ApiV2* api,
    char const* key,
    bool* out)
{
    struct Fake* fake = fake_api(api);

    if( strcmp(key, "show_hover") == 0 )
        *out = fake->show_hover;
    else if( strcmp(key, "show_dest") == 0 )
        *out = fake->show_dest;
    else
        return false;
    return true;
}

static bool
fake_config_int(
    struct ToriRS_ApiV2* api,
    char const* key,
    int* out)
{
    (void)api;
    if( strcmp(key, "true_fill_alpha") == 0 )
        *out = 13;
    else if( strcmp(key, "dest_fill_alpha") == 0 )
        *out = 23;
    else if( strcmp(key, "hover_fill_alpha") == 0 )
        *out = 33;
    else
        return false;
    return true;
}

static bool
fake_config_color(
    struct ToriRS_ApiV2* api,
    char const* key,
    uint32_t* out)
{
    (void)api;
    if( strcmp(key, "true_color") == 0 )
        *out = 0x111111u;
    else if( strcmp(key, "true_fill_color") == 0 )
        *out = 0x121212u;
    else if( strcmp(key, "dest_color") == 0 )
        *out = 0x212121u;
    else if( strcmp(key, "dest_fill_color") == 0 )
        *out = 0x222222u;
    else if( strcmp(key, "hover_color") == 0 )
        *out = 0x313131u;
    else if( strcmp(key, "hover_fill_color") == 0 )
        *out = 0x323232u;
    else
        return false;
    return true;
}

static bool
fake_hover_tile(
    struct ToriRS_ApiV2* api,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct Fake* fake = fake_api(api);

    if( !fake->have_hover )
        return false;
    *out_x = fake->hover_x;
    *out_z = fake->hover_z;
    *out_level = fake->hover_level;
    return true;
}

static bool
fake_local_player(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PluginPlayerSnap* out)
{
    struct Fake* fake = fake_api(api);

    if( !fake->have_player )
        return false;
    *out = fake->player;
    return true;
}

static enum ToriRS_Result
fake_world_tile(
    struct ToriRS_DrawBuilder* draw,
    int tile_x,
    int tile_z,
    int level,
    uint32_t fill_rgb,
    uint32_t outline_rgb,
    int alpha)
{
    struct Fake* fake = draw->implementation;

    if( fake->call_count >= (int)(sizeof(fake->calls) / sizeof(fake->calls[0])) )
        return TORIRS_RESULT_BUDGET;
    fake->calls[fake->call_count++] = (struct DrawCall){
        .x = tile_x,
        .z = tile_z,
        .level = level,
        .fill = fill_rgb,
        .outline = outline_rgb,
        .alpha = alpha,
    };
    return TORIRS_RESULT_OK;
}

static struct ToriRS_ApiV2
make_api(struct Fake* fake)
{
    struct ToriRS_ApiV2 api = {
        .struct_size = sizeof(api),
        .major_version = TORIRS_PLUGIN_API_V2_MAJOR,
        .minor_version = TORIRS_PLUGIN_API_V2_MINOR,
        .instance = fake,
        .config = {
            .struct_size = sizeof(struct ToriRS_ConfigApiV2),
            .get_bool = fake_config_bool,
            .get_int = fake_config_int,
            .get_color = fake_config_color,
        },
        .world = {
            .struct_size = sizeof(struct ToriRS_WorldApiV2),
            .local_player = fake_local_player,
        },
        .input = {
            .struct_size = sizeof(struct ToriRS_InputApiV2),
            .hover_tile = fake_hover_tile,
        },
    };
    return api;
}

static struct ToriRS_DrawBuilder
make_draw(struct Fake* fake)
{
    struct ToriRS_DrawBuilder draw = {
        .struct_size = sizeof(draw),
        .implementation = fake,
        .world_tile = fake_world_tile,
    };
    return draw;
}

static void
check_call(
    struct DrawCall const* call,
    int x,
    int z,
    int level,
    uint32_t fill,
    uint32_t outline,
    int alpha,
    char const* message)
{
    CHECK(call->x == x, message);
    CHECK(call->z == z, message);
    CHECK(call->level == level, message);
    CHECK(call->fill == fill, message);
    CHECK(call->outline == outline, message);
    CHECK(call->alpha == alpha, message);
}

int
main(void)
{
    struct Fake fake = {
        .show_hover = true,
        .show_dest = true,
        .have_hover = true,
        .have_player = true,
        .hover_x = 10,
        .hover_z = 11,
        .hover_level = 2,
        .player = {
            .true_x = 20,
            .true_z = 21,
            .dest_x = 30,
            .dest_z = 31,
            .level = 1,
        },
    };
    struct ToriRS_ApiV2 api = make_api(&fake);
    struct ToriRS_DrawBuilder draw = make_draw(&fake);
    struct ToriRS_PluginConfigItem const* config;
    int config_count = 0;

    CHECK(TORIRS_PLUGIN_TILEIND.struct_size == sizeof(TORIRS_PLUGIN_TILEIND),
        "definition carries its complete v2 size");
    CHECK(strcmp(TORIRS_PLUGIN_TILEIND.id, "tile-indicator-c") == 0,
        "saved plugin identity is unchanged");
    CHECK(strcmp(TORIRS_PLUGIN_TILEIND.title, "Tile Indicator (C)") == 0,
        "roster title is unchanged");
    CHECK(TORIRS_PLUGIN_TILEIND.state_size == 0,
        "stateless plugin requests no per-instance allocation");
    CHECK(TORIRS_PLUGIN_TILEIND.callbacks.on_draw_world != NULL,
        "world drawing is a declarative v2 callback");
    CHECK(TORIRS_PLUGIN_TILEIND.callbacks.on_start == NULL,
        "no startup subscription callback is required");

    config = TORIRS_PLUGIN_TILEIND.config->items;
    while( config[config_count].key )
        config_count++;
    CHECK(config_count == 11, "all existing config rows remain declared");
    CHECK(strcmp(config[0].key, "true_color") == 0,
        "existing config keys retain their order");
    CHECK(strcmp(config[10].key, "show_hover") == 0,
        "last existing config key remains present");

    TORIRS_PLUGIN_TILEIND.callbacks.on_draw_world(&api, NULL, &draw);
    CHECK(fake.call_count == 3, "hover, true, and destination tiles are drawn");
    check_call(&fake.calls[0], 10, 11, 2, 0x323232u, 0x313131u, 33,
        "hover marker keeps its tile, level, fill, outline, and alpha");
    check_call(&fake.calls[1], 20, 21, 1, 0x121212u, 0x111111u, 13,
        "true marker keeps its tile, level, fill, outline, and alpha");
    check_call(&fake.calls[2], 30, 31, 1, 0x222222u, 0x212121u, 23,
        "destination marker keeps its tile, level, fill, outline, and alpha");

    fake.call_count = 0;
    fake.have_player = false;
    TORIRS_PLUGIN_TILEIND.callbacks.on_draw_world(&api, NULL, &draw);
    CHECK(fake.call_count == 1, "hover remains available without a local player");

    fake.call_count = 0;
    fake.have_player = true;
    fake.show_hover = false;
    fake.show_dest = false;
    TORIRS_PLUGIN_TILEIND.callbacks.on_draw_world(&api, NULL, &draw);
    CHECK(fake.call_count == 1, "disabled optional markers leave only the true tile");

    fake.call_count = 0;
    fake.show_dest = true;
    fake.player.dest_x = fake.player.true_x;
    fake.player.dest_z = fake.player.true_z;
    TORIRS_PLUGIN_TILEIND.callbacks.on_draw_world(&api, NULL, &draw);
    CHECK(fake.call_count == 1, "destination marker disappears on arrival");

    printf("tileind v2: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
