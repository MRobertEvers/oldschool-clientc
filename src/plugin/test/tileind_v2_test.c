/*
 * The C twin of the tile indicator, against a fake engine.
 *
 * Two things are pinned here, and the pair is what every ledger row for this
 * plugin is written in.
 *
 * WHAT it draws: three markers, in one order, with all six arguments of each
 * call, and the states in which one of them must not be there. The ORDER is
 * recorded and not just the count, because "hover off costs no pick" is
 * otherwise invisible -- the picture is identical either way and only the
 * sequence says which happened.
 *
 * WHAT IT COSTS: one declaration at on_start and, from then on, not one call
 * into the Porcelain layer. There is no describe, no fence, no commit and no
 * setter, and the counters are read to say so rather than believed.
 */

#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

#include <stdio.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_TILEIND;

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
    /** The border's THICKNESS. This plugin has no setting for it, so what is
     *  pinned is that it asks for the width every tile border was drawn at
     *  before the thickness was a parameter -- the marker's look is unchanged
     *  by the parameter existing. */
    int outline_width;
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
    struct ToriRS_PlayerSnapshot player;
    struct DrawCall calls[3];
    int call_count;
    /* The api FUNCTIONS this frame asked for, in the order it asked. A count
     * cannot tell "the switch was read before the pick" from "the pick was
     * made and thrown away". */
    char order[8][16];
    int order_count;
};

static void
fake_note(
    struct Fake* fake,
    char const* name)
{
    if( fake->order_count < (int)(sizeof(fake->order) / sizeof(fake->order[0])) )
        snprintf(fake->order[fake->order_count++], sizeof(fake->order[0]), "%s", name);
}

static bool
fake_asked(
    struct Fake const* fake,
    char const* name)
{
    for( int i = 0; i < fake->order_count; i++ )
        if( strcmp(fake->order[i], name) == 0 )
            return true;
    return false;
}

static struct Fake*
fake_api(struct ToriRS_Api* api)
{
    return api->instance;
}

static bool
fake_config_bool(
    struct ToriRS_Api* api,
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
    struct ToriRS_Api* api,
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
    struct ToriRS_Api* api,
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
    struct ToriRS_Api* api,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct Fake* fake = fake_api(api);

    fake_note(fake, "hover_tile");
    if( !fake->have_hover )
        return false;
    *out_x = fake->hover_x;
    *out_z = fake->hover_z;
    *out_level = fake->hover_level;
    return true;
}

static bool
fake_local_player(
    struct ToriRS_Api* api,
    struct ToriRS_PlayerSnapshot* out)
{
    struct Fake* fake = fake_api(api);

    fake_note(fake, "local_player");
    if( !fake->have_player )
        return false;
    *out = fake->player;
    return true;
}

static enum ToriRS_Result
fake_world_tile(
    struct ToriRS_Graphics* draw,
    int tile_x,
    int tile_z,
    int level,
    uint32_t fill_rgb,
    uint32_t outline_rgb,
    int outline_width,
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
        .outline_width = outline_width,
        .alpha = alpha,
    };
    return TORIRS_RESULT_OK;
}

static struct ToriRS_Api
make_api(struct Fake* fake)
{
    struct ToriRS_Api api = {
        .struct_size = sizeof(api),
        .major_version = TORIRS_PLUGIN_API_MAJOR,
        .minor_version = TORIRS_PLUGIN_API_MINOR,
        .instance = fake,
        .config = {
            .struct_size = sizeof(struct ToriRS_ConfigApi),
            .get_bool = fake_config_bool,
            .get_int = fake_config_int,
            .get_color = fake_config_color,
        },
        .world = {
            .struct_size = sizeof(struct ToriRS_WorldApi),
            .local_player = fake_local_player,
        },
        .input = {
            .struct_size = sizeof(struct ToriRS_InputApi),
            .hover_tile = fake_hover_tile,
        },
    };
    return api;
}

static struct ToriRS_Graphics
make_draw(struct Fake* fake)
{
    struct ToriRS_Graphics draw = {
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
    CHECK(call->outline_width == TORIRS_TILE_OUTLINE_WIDTH_DEFAULT, message);
    CHECK(call->alpha == alpha, message);
}

/*
 * The instance the host would allocate, and the handle inside it.
 *
 * Porcelain's handle is the only field of the instance, which is the one
 * thing this test knows about the plugin's private state -- and knowing it is
 * what lets the findings and the counters below be read at all. The same
 * assumption, for the same reason, as minimap_orbs_v2_test.c's OrbsStateHead.
 * @see struct TileindState.
 */
struct TileindStateHead
{
    struct Porcelain* porcelain;
};

static unsigned char g_state[64];

static struct Porcelain*
handle(void)
{
    return ((struct TileindStateHead*)g_state)->porcelain;
}

static void
frame(
    struct Fake* fake,
    struct ToriRS_Api* api,
    struct ToriRS_Graphics* draw)
{
    fake->call_count = 0;
    fake->order_count = 0;
    TORIRS_PLUGIN_TILEIND.callbacks.on_draw_world(api, g_state, draw);
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
    struct ToriRS_Api api = make_api(&fake);
    struct ToriRS_Graphics draw = make_draw(&fake);
    struct ToriRS_ConfigItem const* config;
    int config_count = 0;
    struct PorcelainFinding findings[PORCELAIN_FINDINGS_MAX];
    struct PorcelainCounters counters;
    int finding_count;

    CHECK(TORIRS_PLUGIN_TILEIND.struct_size == sizeof(TORIRS_PLUGIN_TILEIND),
        "definition carries its complete v2 size");
    CHECK(strcmp(TORIRS_PLUGIN_TILEIND.id, "tile-indicator-c") == 0,
        "saved plugin identity is unchanged");
    CHECK(strcmp(TORIRS_PLUGIN_TILEIND.title, "Tile Indicator (C)") == 0,
        "roster title is unchanged");
    /*
     * The plugin carries state now. It used to request none, and a test that
     * still pinned `state_size == 0` would be pinning "this plugin cannot
     * hold a Porcelain handle" -- which is the port. What the size is for is
     * one pointer, and nothing is retained behind it.
     */
    CHECK(TORIRS_PLUGIN_TILEIND.state_size == sizeof(struct TileindStateHead),
        "the instance is the layer handle and nothing else");
    CHECK(TORIRS_PLUGIN_TILEIND.state_size <= sizeof(g_state),
        "the fake instance is big enough for the real one");
    CHECK(TORIRS_PLUGIN_TILEIND.callbacks.on_draw_world != NULL,
        "world drawing is a declarative v2 callback");
    CHECK(TORIRS_PLUGIN_TILEIND.callbacks.on_start != NULL,
        "on_start exists to declare the one refusal this plugin cannot see");
    CHECK(TORIRS_PLUGIN_TILEIND.callbacks.on_stop != NULL,
        "and on_stop gives the handle back, so a re-enable does not leak one");

    config = TORIRS_PLUGIN_TILEIND.config->items;
    while( config[config_count].key )
        config_count++;
    CHECK(config_count == 11, "all existing config rows remain declared");
    CHECK(strcmp(config[0].key, "true_color") == 0,
        "existing config keys retain their order");
    CHECK(strcmp(config[10].key, "show_hover") == 0,
        "last existing config key remains present");

    /*
     * Start declares the one refusal this plugin cannot see, BY NAME, so it
     * is an expected finding in every capture instead of a sentence in a
     * commit message. It is the whole of what opening the layer bought.
     */
    TORIRS_PLUGIN_TILEIND.callbacks.on_start(&api, g_state);
    CHECK(handle() != NULL, "on_start opens a layer handle and keeps it");
    finding_count = Porcelain_Findings(handle(), findings, PORCELAIN_FINDINGS_MAX);
    CHECK(finding_count == 1, "on_start files exactly one finding");
    if( finding_count == 1 )
    {
        CHECK(strcmp(findings[0].verb, "unsupported") == 0,
            "and its verb says the feature is unsupported, not refused");
        CHECK(findings[0].result == PORCELAIN_FINDING_UNSUPPORTED,
            "with the UNSUPPORTED result the clean gate reads");
        CHECK(findings[0].detail && strcmp(findings[0].detail, "draw_refusal_readout") == 0,
            "the unreadable draw result is declared, not left unsaid");
        CHECK(findings[0].element.role
                && strcmp(findings[0].element.role, "draw_refusal_readout") == 0,
            "named identically to the Lua twin's declaration, because the twins "
            "must be comparable");
        CHECK(findings[0].expected,
            "and it is EXPECTED, so a declared gap does not fail the clean gate");
        CHECK(findings[0].count == 1, "declared once, not once a frame");
    }
    Porcelain_CountersRead(handle(), &counters);
    CHECK(counters.engine_calls == 0, "a declaration costs no engine call");
    CHECK(counters.allocations == 0, "and no allocation");

    frame(&fake, &api, &draw);
    CHECK(fake.call_count == 3, "hover, true, and destination tiles are drawn");
    check_call(&fake.calls[0], 10, 11, 2, 0x323232u, 0x313131u, 33,
        "hover marker keeps its tile, level, fill, outline, and alpha");
    check_call(&fake.calls[1], 20, 21, 1, 0x121212u, 0x111111u, 13,
        "true marker keeps its tile, level, fill, outline, and alpha");
    check_call(&fake.calls[2], 30, 31, 1, 0x222222u, 0x212121u, 23,
        "destination marker keeps its tile, level, fill, outline, and alpha");
    /*
     * Said twice, from two sides, and in this order on purpose. The Lua twin's
     * committed negative control swaps the two halves of on_draw_world and
     * names the ORDER line as the one that must catch it; the C twin could not
     * catch it at all while it only counted calls.
     */
    CHECK(fake.order_count >= 1 && strcmp(fake.order[0], "hover_tile") == 0,
        "the hover pick is the first thing the frame asks for");

    fake.have_player = false;
    frame(&fake, &api, &draw);
    CHECK(fake.call_count == 1, "hover remains available without a local player");
    CHECK(fake.calls[0].level == 2,
        "and it is still drawn at the level the PICK landed on");

    fake.have_player = true;
    fake.show_hover = false;
    fake.show_dest = false;
    frame(&fake, &api, &draw);
    CHECK(fake.call_count == 1, "disabled optional markers leave only the true tile");
    CHECK(!fake_asked(&fake, "hover_tile"),
        "and the switch is read BEFORE the pick, so hover off costs no pick");

    fake.show_hover = true;
    fake.show_dest = true;
    fake.player.dest_x = fake.player.true_x;
    fake.player.dest_z = fake.player.true_z;
    frame(&fake, &api, &draw);
    CHECK(fake.call_count == 2, "destination marker disappears on arrival");

    /*
     * Aboard a vessel the api answers deck tiles as STAGING-ABSOLUTE addresses
     * and world_tile draws them through the hull's live transform. This
     * plugin's whole contribution to that is to NOT touch the numbers -- no
     * clamp, no region fold, no level of its own -- and until now the staging
     * branch had nothing pinning it on the C side at all.
     */
    fake.player.true_x = 6080;
    fake.player.true_z = 6152;
    fake.player.level = 1;
    fake.player.dest_x = 6083;
    fake.player.dest_z = 6152;
    frame(&fake, &api, &draw);
    CHECK(fake.call_count == 3, "a deck tile draws the same three markers");
    CHECK(fake.calls[1].x == 6080 && fake.calls[1].z == 6152 && fake.calls[1].level == 1,
        "a staging-absolute deck tile is passed through untouched");
    CHECK(fake.calls[2].x == 6083 && fake.calls[2].z == 6152 && fake.calls[2].level == 1,
        "and so is the deck destination");

    /*
     * Steady state. Sixty frames of the same picture, and every counter the
     * layer keeps stays where it was: no describe, no fence, no commit, no
     * setter, no revalidate, no allocation. This is the acceptance rule
     * "steady state costs nothing", read rather than believed -- and the
     * declaration is still one finding, not sixty.
     */
    Porcelain_CountersReset(handle());
    for( int i = 0; i < 60; i++ )
        frame(&fake, &api, &draw);
    CHECK(fake.call_count == 3, "the sixtieth frame draws what the first one did");
    Porcelain_CountersRead(handle(), &counters);
    CHECK(counters.engine_calls == 0, "sixty frames make no engine call through the layer");
    CHECK(counters.describe_runs == 0, "this overlay describes nothing");
    CHECK(counters.setters == 0, "it owns no control to move");
    CHECK(counters.revalidates == 0, "and asks for no layout");
    CHECK(counters.allocations == 0, "and allocates nothing");
    CHECK(Porcelain_Findings(handle(), findings, PORCELAIN_FINDINGS_MAX) == 1,
        "the declaration is made once, not once a frame");

    /*
     * A disable through the settings panel, then a re-enable. The handle table
     * is a fixed array, so a stop that did not give its slot back would run
     * the layer out of handles after thirty-two toggles.
     */
    TORIRS_PLUGIN_TILEIND.callbacks.on_stop(&api, g_state);
    CHECK(handle() == NULL, "on_stop gives the handle back");
    TORIRS_PLUGIN_TILEIND.callbacks.on_start(&api, g_state);
    CHECK(handle() != NULL, "and a re-enable opens a fresh one");
    CHECK(Porcelain_Findings(handle(), findings, PORCELAIN_FINDINGS_MAX) == 1,
        "which declares the same gap again, on its own clean channel");
    frame(&fake, &api, &draw);
    CHECK(fake.call_count == 3, "and the markers draw after a restart");
    TORIRS_PLUGIN_TILEIND.callbacks.on_stop(&api, g_state);

    printf("tileind v2: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
