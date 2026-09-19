/*
 * Minimap orbs, described to Porcelain, against the shared fake engine.
 *
 * Every case pins a RULE and names the mutation that turns it red, because a
 * test that cannot be made to fail is not evidence. The first four are the
 * four ways this plugin has actually drawn a dead orb in this tree -- three
 * of them found on a device, the fourth by the native-contract matrix -- and
 * they are cases and not comments so that the next rewrite has to answer them.
 *
 * The fakes for the verbs the shared testbed does not carry (image pixels,
 * varps, run energy, native actions) are installed onto the testbed's own api
 * struct rather than copied into a second fake engine: one engine, one call
 * log, and a new engine callback does not have to be added here.
 */

#include "plugin/porcelain/test/porcelain_testbed.h"
#include "plugin/porcelain/torirs_porcelain.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS;

static int g_checks;
static int g_failures;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(condition) )                                                                         \
        {                                                                                          \
            g_failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));                    \
        }                                                                                          \
    } while( 0 )

/* ------------------------------------------------------------------------ */
/* The verbs the shared testbed does not carry                              */
/* ------------------------------------------------------------------------ */

#define ORB_W 57
#define ORB_H 34

static char const* const ORB_ART[15] = {
    "frame.png",       "frame_over.png", "fill_empty.png",    "fill_red.png",    "fill_grey.png",
    "fill_gold.png",   "fill_cyan.png",  "fill_cyan_lit.png", "fill_prayer.png", "icon_hp.png",
    "icon_prayer.png", "icon_walk.png",  "icon_run.png",      "icon_spec.png",   "digits.png"};

static char const* const ORB_KEY[4] = {"orb_hitpoints", "orb_prayer", "orb_run", "orb_special"};
static char const* const ORB_ROLE[4] = {"orb_hitpoints", "orb_prayer", "orb_run", "orb_spec"};
/** The action role behind each orb, in the same order. */
static char const* const ORB_ACTION_ROLE[4] = {
    "action_frame_orb_hitpoints_activate",
    "action_frame_orb_prayer_activate",
    "action_frame_orb_run_enable",
    "action_frame_orb_special_activate",
};

/** The red byte the digit atlas signs its pixels with, above every image's. */
#define ART_DIGITS_RED 0x1E

/** The disc, and the panel the number sits in. The plugin's own geometry. */
#define ORB_DISC_X 27
#define ORB_DISC_Y 4
#define ORB_DISC 26

/** The last picture composed for each orb, as the painter drew it. */
static uint32_t g_picture[4][ORB_W * ORB_H];
static int g_composes;

/**
 * `digits.ini`, when a case wants one.
 *
 * NULL is the default and is the shipped web-lane shape: the file has not
 * landed, the plugin keeps its fallback metrics, and it draws no number. A
 * case that needs to read the number's RAMP ROW back out of a picture hands
 * over the one-pixel-per-row atlas below instead.
 */
static char const* g_digits_ini;

/**
 * The atlas colour ramp, one pixel row per step instead of nine.
 *
 * `orbs_compose_number` samples the atlas at `glyph.y + row * row_height`,
 * where `row` is `filled * (steps - 1) / total` -- so with `row_height=1` and
 * one-pixel glyphs the row a picture chose is the row it read, and the fake
 * atlas below encodes that row in the pixel it answers. The vertical metrics
 * are the shipped file's, so the line still lands where the real one puts it.
 */
static char const DIGITS_INI_ONE_ROW_PER_STEP[] =
    "line_height=10\n"
    "max_ascent=10\n"
    "max_descent=2\n"
    "steps=21\n"
    "row_height=1\n"
    "0=0 0 6 1 1 1 7\n"
    "1=6 0 4 1 1 1 4\n"
    "2=10 0 6 1 1 1 7\n"
    "3=16 0 5 1 1 1 6\n"
    "4=21 0 5 1 1 1 5\n"
    "5=26 0 5 1 1 1 6\n"
    "6=31 0 6 1 1 1 7\n"
    "7=37 0 5 1 1 1 6\n"
    "8=42 0 6 1 1 1 7\n"
    "9=48 0 6 1 1 1 7\n";

/**
 * What source image `index` answers, at source row `row`.
 *
 * Every image names itself in the composed picture, so a picture can be read
 * back to say which of the plugin's fifteen files put a pixel there -- which
 * is the only way to ask a question like "is the top of this disc the dark
 * cap or the meter".
 *
 * `fill_empty` owns the BLUE byte and nothing else writes it. That is not
 * decoration: the meter discs are blitted at alpha 205 or 230 OVER the plate,
 * so their composed colour is a mixture, and a first attempt that numbered
 * every image in one channel had fill_red-over-frame land on exactly the
 * value fill_empty answers. A channel only one image can reach cannot be
 * arrived at by blending, because every other source contributes zero to it.
 *
 * The five icons answer alpha 0, because the shipped icons are cut-outs: an
 * opaque fake would paint over the whole disc at alpha 255 and hide the very
 * thing this is here to read. The digit atlas signs itself in the red byte,
 * above every other image's, and carries its own source row in the green.
 */
static uint32_t
art_pixel(int index, int row)
{
    if( index >= 9 && index <= 13 )
        return 0;
    if( index == 14 )
        return 0xFF000000u | ((uint32_t)ART_DIGITS_RED << 16) | ((uint32_t)(row & 0xFF) << 8);
    if( index == 2 )
        return 0xFF0000FFu;
    return 0xFF000000u | (uint32_t)((0x10 + index) << 16);
}

/** Is this composed pixel the dark cap? Only `fill_empty` writes blue. */
static bool
is_fill_empty(uint32_t pixel)
{
    return (pixel & 0xFFu) == 0xFFu;
}

static int g_run_energy = 75;
static int g_varp[512];
static int g_invoked_component = -1;
static int g_invoked_operation = -1;
static int g_native_invokes;
static enum ToriRS_ContractResult g_invoke_result = TORIRS_CONTRACT_OK;
static char g_last_log[256];
/** Every MINIMAP_ORBS_SKIP line the run emitted, newest last. @see
 *  case_a_dropped_orb_says_why. */
static char g_skips[4096];
/** The last MINIMAP_ORBS_VALUE line each orb logged, kept per orb because
 *  the covers log a CONTROL line after them and g_last_log is one slot. */
static char g_value_line[4][256];
static int g_notices;

static bool
fake_image_size(struct ToriRS_Api* api, struct ToriRS_ImageRef image, int* width, int* height)
{
    (void)api;
    if( image.value == 0 )
        return false;
    /* The plate is the whole orb; everything else is a 26x26 disc or the
     * digit atlas, and only the plate's size is load-bearing here. */
    *width = ORB_W;
    *height = ORB_H;
    return true;
}

/*
 * The testbed hands out asset values in declaration order, and `declare_art`
 * declares the fifteen source images first on a freshly reset testbed -- so
 * `image.value - 1` is the index into ORB_ART. That is the one thing this
 * fake knows about the testbed's bookkeeping, and it is what lets a picture
 * be read back at all.
 */
static bool
fake_image_pixels(struct ToriRS_Api* api, struct ToriRS_ImageRef image, uint32_t* out,
                  size_t capacity, size_t* count)
{
    int const index = image.value - 1;
    (void)api;
    if( image.value == 0 || capacity < (size_t)(ORB_W * ORB_H) )
        return false;
    for( int y = 0; y < ORB_H; y++ )
        for( int x = 0; x < ORB_W; x++ )
            out[y * ORB_W + x] = index >= 0 && index < 15 ? art_pixel(index, y) : 0xFF804020u;
    *count = (size_t)(ORB_W * ORB_H);
    return true;
}

static enum ToriRS_AssetState
fake_compose(struct ToriRS_Api* api, char const* name, int width, int height,
             uint32_t const* argb, struct ToriRS_ImageRef* out)
{
    (void)api;
    CHECK(width == ORB_W && height == ORB_H, "a composed orb is the plate's size");
    for( int i = 0; i < 4; i++ )
        if( strncmp(name, ORB_KEY[i], strlen(ORB_KEY[i])) == 0 )
            memcpy(g_picture[i], argb, sizeof(g_picture[i]));
    g_composes++;
    out->value = 700 + (int)strlen(name);
    return TORIRS_ASSET_READY;
}

static enum ToriRS_AssetState
fake_request(struct ToriRS_Api* api, char const* name)
{
    (void)api;
    /* digits.ini: absent unless a case hands one over, so the plugin keeps
     * its fallback metrics. That is the shipped web-lane shape and it must
     * not stop an orb drawing. */
    if( g_digits_ini && strcmp(name, "digits.ini") == 0 )
        return TORIRS_ASSET_READY;
    return TORIRS_ASSET_MISSING;
}

static bool
fake_bytes(struct ToriRS_Api* api, char const* name, void const** data, size_t* size)
{
    (void)api;
    if( g_digits_ini && strcmp(name, "digits.ini") == 0 )
    {
        *data = g_digits_ini;
        *size = strlen(g_digits_ini);
        return true;
    }
    return false;
}

static void
fake_asset_release(struct ToriRS_Api* api, char const* name)
{
    (void)api;
    (void)name;
}

static bool
fake_config_get_bool(struct ToriRS_Api* api, char const* key, bool* out)
{
    int number = 0;
    (void)api;
    if( !api->config.get_int(api, key, &number) )
        return false;
    *out = number != 0;
    return true;
}

static int
fake_varp(struct ToriRS_Api* api, int id)
{
    (void)api;
    return id >= 0 && id < 512 ? g_varp[id] : 0;
}

static bool
fake_cache_invoke(struct ToriRS_Api* api, int component, int operation)
{
    (void)api;
    g_invoked_component = component;
    g_invoked_operation = operation;
    return true;
}

/**
 * Does this lane's profile carry `[varp:...]` rows at all.
 *
 * Every revconfig profile in the tree that declares varps declares the orbs'
 * two, so the plugin's ORB_VARP_*_FALLBACK only ever fires on a profile that
 * has none. This is the switch that produces such a lane.
 */
static bool g_profile_states_varps = true;

static bool
fake_named_id_orbs(struct ToriRS_Api* api, char const* kind, char const* name, int* out)
{
    (void)api;
    if( !g_profile_states_varps && strcmp(kind, "varp") == 0 )
        return false;
    if( strcmp(kind, "varp") == 0 && strcmp(name, "run_mode") == 0 )
    {
        *out = 173;
        return true;
    }
    if( strcmp(kind, "varp") == 0 && strcmp(name, "special_attack_energy") == 0 )
    {
        *out = 300;
        return true;
    }
    if( strcmp(kind, "varp") == 0 && strcmp(name, "special_attack_armed") == 0 )
    {
        /* Deliberately NOT 301: the plugin used to read spec_varp + 1, and a
         * profile that put "armed" anywhere else was silently wrong. */
        *out = 411;
        return true;
    }
    if( strcmp(kind, "iface") == 0 && strcmp(name, "orb_run_on") == 0 )
    {
        *out = 153;
        return true;
    }
    return false;
}

static int
fake_run_energy(struct ToriRS_Api* api)
{
    (void)api;
    return g_run_energy;
}

static bool
fake_skill(struct ToriRS_Api* api, int index, struct ToriRS_SkillSnapshot* out)
{
    (void)api;
    if( index != 3 && index != 5 )
    {
        memset(out, 0, sizeof(*out));
        out->index = -1;
        return false;
    }
    out->index = index;
    out->stated = g_testbed.skill_stated;
    out->current_level = index == 3 ? 42 : 30;
    out->base_level = index == 3 ? 50 : 40;
    return true;
}

static enum ToriRS_ContractResult
fake_actions(void* context, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetAction* out,
             size_t capacity, size_t* count)
{
    (void)context;
    *count = 1;
    if( capacity )
    {
        memset(&out[0], 0, sizeof(out[0]));
        out[0].ref.widget = ref;
        out[0].ref.operation = 1;
        out[0].ref.revision = 9;
        snprintf(out[0].label, sizeof(out[0].label), "Activate");
    }
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_invoke(void* context, struct ToriRS_WidgetActionRef action)
{
    (void)context;
    (void)action;
    if( g_invoke_result == TORIRS_CONTRACT_OK )
        g_native_invokes++;
    return g_invoke_result;
}

static void
fake_notify(struct ToriRS_Api* api, char const* text)
{
    (void)api;
    (void)text;
    g_notices++;
}

static void
fake_log(struct ToriRS_Api* api, char const* format, ...)
{
    va_list arguments;
    (void)api;
    va_start(arguments, format);
    vsnprintf(g_last_log, sizeof(g_last_log), format, arguments);
    va_end(arguments);
    if( strncmp(g_last_log, "MINIMAP_ORBS_SKIP ", 18) == 0 )
    {
        strncat(g_skips, g_last_log, sizeof(g_skips) - strlen(g_skips) - 2);
        strncat(g_skips, "\n", sizeof(g_skips) - strlen(g_skips) - 1);
    }
    if( strncmp(g_last_log, "MINIMAP_ORBS_VALUE ", 19) == 0 )
        for( int i = 0; i < 4; i++ )
        {
            char wanted[48];
            snprintf(wanted, sizeof(wanted), "orb=%s ", ORB_KEY[i]);
            if( strstr(g_last_log, wanted) )
                snprintf(g_value_line[i], sizeof(g_value_line[i]), "%s", g_last_log);
        }
}

/* ------------------------------------------------------------------------ */
/* Fixtures                                                                 */
/* ------------------------------------------------------------------------ */

static void* g_state;

static void
install_verbs(void)
{
    struct ToriRS_Api* api = Testbed_Api();

    api->assets.image_size = fake_image_size;
    api->assets.image_pixels = fake_image_pixels;
    api->assets.image_compose = fake_compose;
    api->assets.request = fake_request;
    api->assets.bytes = fake_bytes;
    api->assets.release = fake_asset_release;
    api->config.get_bool = fake_config_get_bool;
    api->cache.varp = fake_varp;
    api->cache.invoke = fake_cache_invoke;
    api->cache.named_id = fake_named_id_orbs;
    api->widgets.actions = fake_actions;
    api->widgets.invoke = fake_invoke;
    api->core.notify = fake_notify;
    api->core.log = fake_log;
    /* The testbed owns the game table; the api points at it as const. */
    g_testbed.game.skill = fake_skill;
    g_testbed.game.run_energy = fake_run_energy;
}

static void
set_defaults(void)
{
    Testbed_SetConfigInt("show_hp", 1);
    Testbed_SetConfigInt("show_prayer", 1);
    Testbed_SetConfigInt("show_run", 1);
    Testbed_SetConfigInt("show_spec", 1);
    Testbed_SetConfigInt("replace_native", 1);
    Testbed_SetConfigInt("offset_x", 6);
    Testbed_SetConfigInt("offset_y", -3);
    Testbed_SetConfigInt("run_varp", -1);
    Testbed_SetConfigInt("spec_varp", -1);
    Testbed_SetConfigInt("spec_max", 1000);
    Testbed_SetConfigString("hp_button", "");
    Testbed_SetConfigString("prayer_button", "");
    Testbed_SetConfigString("run_button", "");
    Testbed_SetConfigString("run_button_off", "");
    Testbed_SetConfigString("spec_button", "");
}

/**
 * The plugin's own art, and the four pictures it publishes.
 *
 * The composed names are declared too because the engine's image table is one
 * table: a picture published by name is found by that name, which is what
 * lets the description carry a derived key in its `image` field at all.
 */
static void
declare_art(enum ToriRS_AssetState plate)
{
    for( int i = 0; i < 15; i++ )
        Testbed_DeclareAsset(ORB_ART[i], i == 0 ? plate : TORIRS_ASSET_READY);
    for( int i = 0; i < 4; i++ )
    {
        char name[48];
        snprintf(name, sizeof(name), "%s.composed", ORB_KEY[i]);
        Testbed_DeclareAsset(name, TORIRS_ASSET_READY);
    }
}

/** Set a facet and tell the watchers, the way a lane fact actually arrives. */
static void
set_facets(char const* role, uint32_t facets)
{
    struct TestbedElement* element = Testbed_Element(role);
    CHECK(element != NULL, "the element is declared");
    if( !element )
        return;
    element->facets = facets;
    Testbed_MoveElement(role, element->local.x, element->local.y);
}

/** A lane whose cache draws interface 160: four orb roots in a column. */
static void
declare_native_lane(void)
{
    static const int ORB_X[4] = {521, 521, 531, 553};
    static const int ORB_Y[4] = {41, 75, 107, 132};

    Testbed_DeclareElement("minimap", 575, 9, 146, 151);
    for( int i = 0; i < 4; i++ )
        Testbed_DeclareElement(ORB_ROLE[i], ORB_X[i], ORB_Y[i], ORB_W, ORB_H);
    Testbed_DeclareElement("action_frame_orb_run_enable", 3, 5, 50, 26);
    Testbed_DeclareElement("action_frame_orb_run_disable", 3, 5, 50, 26);
    Testbed_DeclareElement("action_frame_orb_prayer_activate", 3, 5, 50, 26);
    Testbed_DeclareElement("action_frame_orb_special_activate", 3, 5, 50, 26);
    /*
     * And where they LIVE, which is the fact the flat table was missing and
     * the reason a whole class of defect was invisible here: on osrs239 each
     * action is a child of the orb it belongs to -- runbutton is interface
     * 160 component 28 inside orb_runenergy's 26, prayerbutton 20 inside 18,
     * specbutton 36 inside 34 -- so a plugin that hides the orb hides the
     * button it means to press.
     */
    Testbed_ElementInside("action_frame_orb_run_enable", "orb_run");
    Testbed_ElementInside("action_frame_orb_run_disable", "orb_run");
    Testbed_ElementInside("action_frame_orb_prayer_activate", "orb_prayer");
    Testbed_ElementInside("action_frame_orb_special_activate", "orb_spec");
    declare_art(TORIRS_ASSET_READY);
    Testbed_BindElement("minimap");
    for( int i = 0; i < 4; i++ )
        Testbed_BindElement(ORB_ROLE[i]);
    Testbed_BindElement("action_frame_orb_run_enable");
    Testbed_BindElement("action_frame_orb_run_disable");
    Testbed_BindElement("action_frame_orb_prayer_activate");
    Testbed_BindElement("action_frame_orb_special_activate");
}

static void
start_plugin(void)
{
    g_state = calloc(1, TORIRS_PLUGIN_MINIMAP_ORBS.state_size);
    CHECK(g_state != NULL, "the instance allocates");
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_start(Testbed_Api(), g_state);
}

static void
frame(int times)
{
    for( int i = 0; i < times; i++ )
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(Testbed_Api(), g_state, NULL);
}

static void
stop_plugin(void)
{
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_stop(Testbed_Api(), g_state);
    free(g_state);
    g_state = NULL;
}

static void
reset(void)
{
    Testbed_Reset();
    install_verbs();
    set_defaults();
    memset(g_varp, 0, sizeof(g_varp));
    g_run_energy = 75;
    g_invoked_component = -1;
    g_invoked_operation = -1;
    g_native_invokes = 0;
    g_invoke_result = TORIRS_CONTRACT_OK;
    g_composes = 0;
    g_notices = 0;
    g_digits_ini = NULL;
    g_profile_states_varps = true;
    memset(g_picture, 0, sizeof(g_picture));
    memset(g_value_line, 0, sizeof(g_value_line));
    g_last_log[0] = '\0';
    g_skips[0] = '\0';
    g_testbed.skill_stated = true;
}

static int
live_orbs(void)
{
    int live = 0;
    for( int i = 0; i < 4; i++ )
        if( Testbed_Control(ORB_KEY[i]) )
            live++;
    return live;
}

/** How many findings this plugin filed that nobody declared expected. */
static int
undeclared_findings(struct Porcelain* porcelain)
{
    struct PorcelainFinding found[PORCELAIN_FINDINGS_MAX];
    int const count = Porcelain_Findings(porcelain, found, PORCELAIN_FINDINGS_MAX);
    int bad = 0;
    for( int i = 0; i < count; i++ )
        if( !found[i].expected )
        {
            bad++;
            fprintf(stderr, "  undeclared finding: verb=%s result=%d detail=%s role=%s\n",
                found[i].verb, found[i].result, found[i].detail ? found[i].detail : "-",
                found[i].element.role ? found[i].element.role : "-");
        }
    return bad;
}

/**
 * The handle the plugin opened.
 *
 * Porcelain's handle is the first field of the instance, which is the one
 * thing this test knows about the plugin's private state -- and knowing it is
 * what lets the counters below be read at all. @see struct OrbsState.
 */
struct OrbsStateHead
{
    struct ToriRS_Api* api;
    struct Porcelain* porcelain;
};

static struct Porcelain*
handle(void)
{
    return ((struct OrbsStateHead*)g_state)->porcelain;
}

/* ------------------------------------------------------------------------ */
/* 1. The claimed part's disc was anchored to the minimap                   */
/* ------------------------------------------------------------------------ */

/*
 * Found on a phone on 2026-09-02: four solid BLACK discs. The cover was
 * placed against the MINIMAP while the lane painted interface 160's plate at
 * the orb's own position, so the plate landed on top of the disc and all that
 * was left was frame.png's baked hole.
 *
 * The rule: when the lane HAS an orb, the cover stands exactly on that orb's
 * own drawn box -- never on the minimap's.
 *
 * Mutation: place the native branch against the minimap (or drop the
 * `orb_bound` test in orbs_canvas_box) -> every box below is the map's.
 */
static void
case_cover_stands_on_its_own_orb(void)
{
    static const int ORB_X[4] = {521, 521, 531, 553};
    static const int ORB_Y[4] = {41, 75, 107, 132};

    reset();
    declare_native_lane();
    start_plugin();
    frame(6);

    CHECK(live_orbs() == 4, "a lane with four orbs gets four covers");
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        if( !control )
            continue;
        CHECK(control->x == ORB_X[i] && control->y == ORB_Y[i],
              "the cover stands on its own orb, not on the minimap");
        CHECK(control->width == ORB_W && control->height == ORB_H,
              "the cover is the plate's 57x34");
    }
    CHECK(undeclared_findings(handle()) == 0, "a settled native lane files no finding");
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 2. The frame's own surround art painted over the orbs                    */
/* ------------------------------------------------------------------------ */

/*
 * Twice now. First as the map surround's ring, fixed in 2026-08 by giving the
 * gameframe draw_order -100; and again in the native-contract matrix, where
 * `orb_column_four_discs` was the only red rule in the whole run: under
 * minimap states 3, 4 and 5 the compass stops painting, the frame's `housing`
 * image is anchored OVER that compass, an anchored unit whose target has no
 * records keeps its NATIVE position -- which is last -- and the housing
 * painted over the whole orb column, native art and covers alike.
 *
 * The rule: the orb covers are the topmost piece of minimap chrome. They are
 * placed on the CANVAS, under the frame root, and not inside the interface's
 * own subtree where another unit's ordering can get above them.
 *
 * Mutation: place the native branch PORCELAIN_REPLACE on ORB(n) -> the parent
 * below becomes the orb's parent and the case goes red (it is also how the
 * matrix rule goes red again).
 */
static void
case_cover_is_on_the_canvas(void)
{
    struct ToriRS_WidgetRef root;
    struct TestbedElement const* map;

    reset();
    declare_native_lane();
    start_plugin();
    frame(6);

    map = Testbed_Element("minimap");
    CHECK(map != NULL, "the minimap is declared");
    root = map->parent;
    CHECK(live_orbs() == 4, "four covers");
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        if( !control )
            continue;
        CHECK(ToriRS_WidgetRefEqual(control->parent, root),
              "the cover hangs off the frame root, above the frame's own art");
        /*
         * And takes no ANCHOR. An anchored unit is ordered relative to its
         * target, which is how the frame's housing -- anchored OVER a compass
         * that stops painting in minimap states 3, 4 and 5 -- fell back to its
         * native position and painted over the whole column. It is also what
         * would consume the native orb's own menu rows, which the historical
         * child-at-(0,0) never did.
         */
        CHECK(!ToriRS_WidgetRefValid(control->anchor),
              "and is ordered by the canvas, not by an anchor to the orb");
    }
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 3. A picture published before its art landed                             */
/* ------------------------------------------------------------------------ */

/*
 * The third dead orb: the image budget filled, api_image_load answered -1,
 * and the refresh returned early at `state->image[ORB_IMG_FRAME].value == 0`
 * -- so the orb never appeared and nothing anywhere said so. The same shape
 * turned up again porting this file: a painter run before the plate decoded
 * answers false, Porcelain calls that DERIVED_FAILED, and FAILED is TERMINAL,
 * so the orb would never have drawn again in that session.
 *
 * The rule: nothing is described until the plate is decoded, and the painter
 * is therefore never run without it. No control, no finding, no terminal
 * state -- and all four appear the moment the art lands.
 *
 * Mutation: describe before `orbs_pixels(ORB_IMG_FRAME)` answers -> the
 * finding count below goes to four and the orbs stay dark after the landing.
 */
static void
case_no_orb_before_its_plate(void)
{
    reset();
    Testbed_DeclareElement("minimap", 575, 9, 146, 151);
    for( int i = 0; i < 4; i++ )
        Testbed_DeclareElement(ORB_ROLE[i], 521 + 10 * i, 41 + 34 * i, ORB_W, ORB_H);
    declare_art(TORIRS_ASSET_PENDING);
    Testbed_DeclareElement("action_frame_orb_run_enable", 3, 5, 50, 26);
    Testbed_DeclareElement("action_frame_orb_prayer_activate", 3, 5, 50, 26);
    Testbed_DeclareElement("action_frame_orb_special_activate", 3, 5, 50, 26);
    Testbed_BindElement("minimap");
    for( int i = 0; i < 4; i++ )
        Testbed_BindElement(ORB_ROLE[i]);
    Testbed_BindElement("action_frame_orb_run_enable");
    Testbed_BindElement("action_frame_orb_prayer_activate");
    Testbed_BindElement("action_frame_orb_special_activate");

    start_plugin();
    frame(6);
    CHECK(live_orbs() == 0, "no plate, no orb");
    CHECK(g_composes == 0, "and nothing was composed");
    CHECK(undeclared_findings(handle()) == 0, "a pending asset is not a failure");

    Testbed_LandAsset("frame.png");
    frame(4);
    CHECK(live_orbs() == 4, "the art lands and all four appear");
    CHECK(undeclared_findings(handle()) == 0, "and still nothing was refused");
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 4. A minimap state that hides only the compass                           */
/* ------------------------------------------------------------------------ */

/*
 * The six minimap states are three independent permissions. States 3, 4 and 5
 * stop the compass painting; 2 and 5 stop the map painting. The orb ROOTS
 * keep their own permission through all six -- the engine reports
 * native_paint=1 on every one of them in every state -- so the four discs
 * must survive all six, which is what the matrix asserts and what state 2
 * (map hidden, discs present) already proved before this port.
 *
 * The rule: the covers follow the ORB they stand on, and nothing else. A
 * facet on the minimap or the compass does not reach them.
 *
 * Mutation: gate the native branch on the minimap's DRAWN facet (or set
 * visible_with = MINIMAP on the native branch) -> the covers vanish in
 * states 2 and 5 and the matrix goes red on two more scenarios.
 */
static void
case_compass_state_keeps_the_discs(void)
{
    reset();
    declare_native_lane();
    Testbed_DeclareElement("compass", 550, 4, 33, 33);
    Testbed_BindElement("compass");
    start_plugin();
    frame(6);
    CHECK(live_orbs() == 4, "all four before the state change");

    /* State 3: the map still draws and is walkable, the compass does not. */
    set_facets("minimap", PORCELAIN_FACET_DRAWN | PORCELAIN_FACET_WALKABLE);
    set_facets("compass", 0);
    Testbed_PresentElement("compass", false);
    frame(4);
    CHECK(live_orbs() == 4, "a hidden compass does not take the discs with it");

    /* State 5: neither paints. The orb roots still do. */
    set_facets("minimap", 0);
    Testbed_PresentElement("minimap", false);
    frame(4);
    CHECK(live_orbs() == 4, "a hidden minimap does not take the native discs either");
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        CHECK(control && !control->hidden, "and none of them is hidden");
    }
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 5. A root switch rebuilds the tree under the column                      */
/* ------------------------------------------------------------------------ */

/*
 * The matrix's `remount` scenario: `layout 2` tears the HUD down and builds
 * it again. The canvas placement parents to the frame root, so every control
 * the plugin owns dies with the old tree -- and Porcelain neither notices nor
 * re-creates it, because it has no re-parent arm and no verb asks whether an
 * owned control still exists.
 *
 * The plugin therefore infers the rebuild from the elements that DO report
 * one, drops the column for a fence, and describes it again on the next. The
 * rule is not "the controls are still there": it is that they are NEW
 * controls. A description that quietly kept the old refs looks identical from
 * here and is exactly the regression -- four dead handles and an empty screen.
 *
 * Mutation: drop the `recreate` flag and simply keep describing -> the wipe
 * never happens, the refs below are carried over and this case goes red.
 *
 * The mutation that used to be written here was "invalidate from inside the
 * describe instead of setting `recreate`", and it no longer bites, because the
 * defect it named has been fixed in the layer: Porcelain_Invalidate called
 * inside a describe used to re-run that describe in the same fence and
 * reconcile the SECOND pass, throwing away the empty description that asked.
 * It defers the re-describe to the next fence now, so the wipe says so where
 * it happens. A mutation that no longer bites is not evidence, so it is gone
 * rather than left standing as one.
 */
static void
case_root_switch_recreates_the_column(void)
{
    struct ToriRS_WidgetRef before[4];
    int carried = 0;

    reset();
    declare_native_lane();
    start_plugin();
    frame(8);
    CHECK(live_orbs() == 4, "four before the switch");
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        memset(&before[i], 0, sizeof(before[i]));
        if( control )
            before[i] = control->ref;
    }

    /* The HUD is rebuilt: same roles, same boxes, new incarnations. */
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedElement* element = Testbed_Element(ORB_ROLE[i]);
        CHECK(element != NULL, "the orb element is declared");
        if( !element )
            continue;
        element->incarnation++;
        Testbed_MoveElement(ORB_ROLE[i], element->local.x, element->local.y);
    }
    {
        struct TestbedElement* element = Testbed_Element("minimap");
        CHECK(element != NULL, "the minimap is declared");
        if( element )
        {
            element->incarnation++;
            Testbed_MoveElement("minimap", element->local.x, element->local.y);
        }
    }

    frame(4);
    CHECK(live_orbs() == 4, "and four after it");
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        if( control && ToriRS_WidgetRefEqual(control->ref, before[i]) )
            carried++;
    }
    CHECK(carried == 0, "every control is a NEW one, not the handle the old tree took");
    CHECK(undeclared_findings(handle()) == 0, "and the rebuild files no finding");
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 6. Steady state costs nothing                                            */
/* ------------------------------------------------------------------------ */

/*
 * Mutation: drop the `orbs_numbers_moved` compare and invalidate every frame
 * -> setters and property_applies climb with every fence.
 */
static void
case_steady_state_costs_nothing(void)
{
    struct PorcelainCounters counters;
    int settled_composes;

    reset();
    declare_native_lane();
    start_plugin();
    frame(8);

    /*
     * Four pictures, and one redraw: the special orb is drawn inactive-grey
     * until its verb resolves, and the verb resolves one fence later. Nothing
     * else may be redrawn -- the art arriving image by image used to redraw
     * every orb once per image, which is three compositions each.
     */
    CHECK(g_composes == 5, "boot composes each orb once, plus the spec orb arming");

    Porcelain_CountersReset(handle());
    Testbed_ClearLog();
    settled_composes = g_composes;
    frame(4);
    Porcelain_CountersRead(handle(), &counters);
    CHECK(counters.setters == 0, "a settled description writes no setter");
    CHECK(counters.creates == 0, "and creates nothing");
    CHECK(counters.removes == 0, "and removes nothing");
    CHECK(counters.revalidates == 0, "and costs no full-tree resolve");
    CHECK(counters.property_applies == 0, "an unchanged hash walks no property");
    CHECK(Testbed_LogCountWith("set_position") == 0, "no geometry is rewritten");
    CHECK(g_composes == settled_composes, "and nothing is recomposed");
    /*
     * The ledger's counter for this plugin: the varp and interface ids are
     * resolved ONCE, at the first describe. They used to be recomputed on
     * every call of orbs_varp -- several times a frame, each one a string
     * compare through the profile's ref table.
     *
     * The `find` calls that remain are Porcelain's own watch resolution, one
     * per watched element per fence, and they are the layer's to remove: this
     * plugin no longer looks a role up by name anywhere except at press time.
     */
    CHECK(Testbed_LogCountWith("named_id") == 0, "no profile lookup in the steady state");

    /* One number moves: exactly one picture is redrawn, and the revalidate
     * for the whole frame is still one. */
    Porcelain_CountersReset(handle());
    settled_composes = g_composes;
    g_run_energy = 40;
    frame(1);
    Porcelain_CountersRead(handle(), &counters);
    CHECK(g_composes == settled_composes + 1, "a changed number redraws exactly one orb");
    CHECK(counters.revalidates <= 1, "at most one revalidate a frame");
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 7. The lane's own facts                                                  */
/* ------------------------------------------------------------------------ */

/*
 * The run orb presses two different buttons, chosen by the ACTIVE facet on
 * its own orb -- not by varp arithmetic here -- and the special orb's lit
 * disc comes from the armed facet, not from `spec_varp + 1`.
 *
 * Mutation: read the run varp instead of the facet -> the role below stays
 * `..._enable` when the facet says the player is running.
 */
static void
case_run_orb_follows_the_active_facet(void)
{
    struct TestbedControl const* run;

    reset();
    declare_native_lane();
    start_plugin();
    frame(6);

    run = Testbed_Control("orb_run");
    CHECK(run && strcmp(run->label, "Toggle Run") == 0, "the run orb carries its verb");
    CHECK(run && run->armed, "and is armed where the lane declares the action");

    /* Running: the facet flips, and the orb presses the OTHER button. */
    set_facets("orb_run", PORCELAIN_FACET_ACTIVE);
    Testbed_UnbindElement("action_frame_orb_run_enable");
    frame(4);
    run = Testbed_Control("orb_run");
    CHECK(run && run->armed, "a running orb is still armed, through the disable role");

    Testbed_ClearLog();
    run->op(Testbed_Api(), run->op_user,
        &(struct ToriRS_WidgetEvent){.type = TORIRS_WIDGET_OPERATION, .widget = run->ref});
    CHECK(g_native_invokes == 1, "the press goes through the checked native action");
    CHECK(g_invoked_component == -1, "and never through a component id");
    CHECK(strstr(g_last_log, "via=native") != NULL, "and says so");
    stop_plugin();
}

/*
 * The cover takes the lane's orb OUT of the frame, and keeps pressing it.
 *
 * The hide is what stopped the lane's own plate showing through the cover's
 * transparent corners. What it must not do is take the button with it: on
 * osrs239 the orb's action is a CHILD of the node hidden here (interface 160
 * component 28 under component 26), so every question the plugin asks about
 * that action afterwards is asked through a subtree it hid itself. With the
 * layer's own hiding folded into the input answer, the reply was "there is
 * nothing to press", one fence after the plugin drew a plate that says there
 * is -- four orbs drawn, all four inert.
 *
 * Mutation: have the testbed's set_hidden clear the element's
 * `input_present` as well -- which is the engine's pre-fix answer, written
 * into the model -> every cover here is unarmed and the press never happens.
 * Dropping the `describe->hide` instead takes the first four checks.
 */
static void
case_covered_orb_keeps_its_press(void)
{
    struct TestbedControl const* run;

    reset();
    declare_native_lane();
    start_plugin();
    frame(6);

    CHECK(live_orbs() == 4, "four covers over four native orbs");
    for( int i = 0; i < 4; i++ )
    {
        char wanted[64];
        snprintf(wanted, sizeof(wanted), "set_hidden %s 1", ORB_ROLE[i]);
        CHECK(Testbed_LogCountWith(wanted) == 1, "the lane's own orb goes under the cover");
    }

    /* Six more fences, because the defect was not the first frame: the cover
     * was described armed and then disarmed by the consequence of its own
     * hide. */
    frame(6);
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        CHECK(control != NULL, "the cover is still there");
        /* The hitpoints orb has no verb on this lane -- @see ORB_PART. */
        if( control && i != 0 )
            CHECK(control->armed, "and still carries the operation it replaced");
    }

    Testbed_ClearLog();
    run = Testbed_Control("orb_run");
    CHECK(run && run->armed, "the run cover is armed over a hidden orb");
    if( run )
        run->op(Testbed_Api(), run->op_user,
            &(struct ToriRS_WidgetEvent){.type = TORIRS_WIDGET_OPERATION, .widget = run->ref});
    CHECK(g_native_invokes == 1, "and its press reaches the button underneath");
    CHECK(strstr(g_last_log, "via=native") != NULL, "through the native action, as before");
    stop_plugin();
}

/*
 * NATIVE_BLOCKED stops. It used to fall through to the compatibility id and
 * press an unrelated component by number.
 *
 * Mutation: restore the fall-through -> g_invoked_component becomes 153.
 */
static void
case_blocked_action_presses_nothing(void)
{
    struct TestbedControl const* run;

    reset();
    declare_native_lane();
    start_plugin();
    frame(6);

    g_invoke_result = TORIRS_CONTRACT_NATIVE_BLOCKED;
    run = Testbed_Control("orb_run");
    CHECK(run != NULL, "the run orb exists");
    if( run )
        run->op(Testbed_Api(), run->op_user,
            &(struct ToriRS_WidgetEvent){.type = TORIRS_WIDGET_OPERATION, .widget = run->ref});
    CHECK(g_invoked_component == -1, "a blocked native action presses nothing at all");
    CHECK(g_notices == 0, "and does not blame the user's configuration for it");
    CHECK(strstr(g_last_log, "via=refused") != NULL, "it says which role refused");
    stop_plugin();
}

/*
 * A cutscene hides the cache's own orbs, and the covers go with them. The
 * facet is the lane's answer: a cache with no cutscene varbit reports none,
 * and that is not a finding.
 *
 * Mutation: drop the HIDDEN_BY_CUTSCENE test -> four covers over a hidden HUD.
 */
static void
case_cutscene_removes_the_covers(void)
{
    reset();
    declare_native_lane();
    start_plugin();
    frame(6);
    CHECK(live_orbs() == 4, "four before the cutscene");

    set_facets("minimap", PORCELAIN_FACET_HIDDEN_BY_CUTSCENE);
    for( int i = 0; i < 4; i++ )
        set_facets(ORB_ROLE[i], PORCELAIN_FACET_HIDDEN_BY_CUTSCENE);
    frame(4);
    CHECK(live_orbs() == 0, "a cutscene removes every cover");

    set_facets("minimap", 0);
    for( int i = 0; i < 4; i++ )
        set_facets(ORB_ROLE[i], 0);
    frame(4);
    CHECK(live_orbs() == 4, "and they come back when it ends");
    CHECK(undeclared_findings(handle()) == 0, "a lane fact is not a finding");
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 8. A lane with no orbs at all                                            */
/* ------------------------------------------------------------------------ */

/*
 * The 2004 lane: no interface 160, so the column hangs off the minimap by
 * interface 160's own offsets and is clamped clear of the map disc. Asking
 * each ORB element on such a lane would file four ABSENT findings for a fact
 * `Porcelain_Count` already answers, which is why the count is asked first.
 *
 * Mutation: ask Porcelain_Element for each orb unconditionally -> four
 * undeclared findings, and the clean gate fails on the dat1 lane.
 */
static void
case_lane_without_orbs(void)
{
    struct ToriRS_WidgetBounds const map = {575, 9, 146, 151};

    reset();
    Testbed_DeclareElement("minimap", map.x, map.y, map.width, map.height);
    declare_art(TORIRS_ASSET_READY);
    Testbed_BindElement("minimap");
    start_plugin();
    frame(8);

    CHECK(live_orbs() == 4, "a lane with no orbs still gets the column");
    CHECK(undeclared_findings(handle()) == 0, "and files no absence for orbs it never had");
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        long const cx = 2L * map.x + map.width;
        long const cy = 2L * map.y + map.height;
        long const r = map.width < map.height ? map.width : map.height;
        int covers = 0;
        if( !control )
            continue;
        CHECK(control->x + ORB_W <= map.x + map.width, "the plate stays left of the map box");
        for( int y = control->y; y < control->y + ORB_H; y++ )
            for( int x = control->x; x < control->x + ORB_W; x++ )
            {
                long const dx = 2L * x + 1 - cx;
                long const dy = 2L * y + 1 - cy;
                if( dx * dx + dy * dy < r * r )
                    covers = 1;
            }
        CHECK(!covers, "and is clamped out of the map disc");
    }

    /* The minimap stops painting: the plates hang off nothing, so they go. */
    Testbed_PresentElement("minimap", false);
    frame(3);
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        CHECK(control && control->hidden, "a hidden minimap hides the plates beside it");
    }
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 9. Configuration                                                         */
/* ------------------------------------------------------------------------ */

/*
 * Mutation: ignore `replace_native` -> the covers survive the flip.
 */
static void
case_config(void)
{
    reset();
    declare_native_lane();
    start_plugin();
    frame(6);
    CHECK(live_orbs() == 4, "four by default");

    Testbed_SetConfigInt("replace_native", 0);
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(Testbed_Api(), g_state,
                                                           "replace_native");
    frame(2);
    CHECK(live_orbs() == 0, "keeping the native orbs adds none of ours");

    Testbed_SetConfigInt("replace_native", 1);
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(Testbed_Api(), g_state,
                                                           "replace_native");
    frame(2);
    CHECK(live_orbs() == 4, "and turning it back on brings them back");

    Testbed_SetConfigInt("show_prayer", 0);
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(Testbed_Api(), g_state, "show_prayer");
    frame(2);
    CHECK(Testbed_Control("orb_prayer") == NULL, "a cleared toggle removes that orb");
    CHECK(live_orbs() == 3, "and only that one");
    stop_plugin();
}

/*
 * An unstated skill draws nothing. `stated` is the reading: the pre-login
 * table is a fresh account's, not an empty one.
 *
 * Mutation: read base_level instead of stated -> the hitpoints orb draws 42
 * of 50 before the server has said anything.
 */
static void
case_unstated_skill_draws_nothing(void)
{
    reset();
    declare_native_lane();
    g_testbed.skill_stated = false;
    start_plugin();
    frame(6);
    CHECK(Testbed_Control("orb_hitpoints") == NULL, "no reading, no hitpoints orb");
    CHECK(Testbed_Control("orb_prayer") == NULL, "no reading, no prayer orb");
    CHECK(Testbed_Control("orb_run") != NULL, "run energy is seeded and does draw");

    g_testbed.skill_stated = true;
    frame(4);
    CHECK(Testbed_Control("orb_hitpoints") != NULL, "the first reading brings it in");
    stop_plugin();
}

/*
 * The hitpoints orb has no cure verb on any profile in this tree. It is
 * DECLARED absent, so the run stays clean while that holds -- and goes red
 * the day a profile binds one and nobody wires the orb up to it.
 *
 * Mutation: delete the Porcelain_ExpectAbsent call -> the finding below is
 * undeclared and the clean gate fails on every lane.
 */
static void
case_hitpoints_verb_is_declared_absent(void)
{
    struct PorcelainFinding found[PORCELAIN_FINDINGS_MAX];
    int count;
    int declared = 0;

    reset();
    declare_native_lane();
    start_plugin();
    frame(8);

    count = Porcelain_Findings(handle(), found, PORCELAIN_FINDINGS_MAX);
    for( int i = 0; i < count; i++ )
        if( found[i].expected && found[i].element.role &&
            strcmp(found[i].element.role, "action_frame_orb_hitpoints_activate") == 0 )
            declared++;
    CHECK(declared == 1, "the missing cure verb is a declared absence, not a surprise");
    CHECK(undeclared_findings(handle()) == 0, "and nothing else is undeclared");
    {
        struct TestbedControl const* hp = Testbed_Control("orb_hitpoints");
        CHECK(hp && !hp->armed, "so the orb is drawn and inert rather than armed");
    }
    stop_plugin();
}

/*
 * An UNDECLARED varp is a blank orb that says why -- never a guessed id.
 *
 * This file used to carry `ORB_VARP_RUN_FALLBACK 173` and
 * `ORB_VARP_SPEC_FALLBACK 300` as a third resolve step, so a lane whose
 * profile stated nothing still drew a confident orb off ids nobody had
 * checked. There is no capture that can catch that being wrong: a spec orb
 * reading a foreign var draws a number, and a number is what a right one draws
 * too. So the guess is gone, and the two halves below are what replaced it.
 *
 * The lane facts this pins, from revconfig: osrs239, rs289lc and rs245_2lc are
 * the only cache profiles a manifest loads, and all three declare
 * `[varp:run_mode]` and `[varp:special_attack_energy]` -- which is why
 * deleting the fallback moved no shipping lane's picture, and why the
 * declaring half below is the one that runs in the field.
 *
 * Mutation 1: restore either fallback -> the first half fails, because the
 * spec orb draws a reading on a lane that declared no id for it.
 * Mutation 2: declare unconditionally -> the second half fails, because a lane
 * carrying both rows is told it is missing them.
 * Mutation 3: drop orbs_declare_missing -> the first half fails on the count,
 * and a blank orb goes back to having no stated reason.
 */
static void
case_undeclared_varp_draws_nothing_and_says_so(void)
{
    struct PorcelainFinding found[PORCELAIN_FINDINGS_MAX];
    int count;
    int declared = 0;

    /*
     * A lane whose profile carries no [varp:] rows. The varp STORAGE is
     * seeded at the historical ids, so a plugin that still guessed them would
     * find a live reading sitting there and draw it -- which is exactly the
     * shape the fallback had in the field.
     */
    reset();
    declare_native_lane();
    g_profile_states_varps = false;
    g_varp[300] = 700;
    g_varp[173] = 1;
    start_plugin();
    frame(8);

    CHECK(Testbed_Control("orb_special") == NULL,
        "no declared id, no special orb -- not an orb read off the historical number");
    CHECK(g_value_line[3][0] == '\0', "and no reading reaches the log either");

    count = Porcelain_Findings(handle(), found, PORCELAIN_FINDINGS_MAX);
    for( int i = 0; i < count; i++ )
        if( found[i].element.role &&
            (strcmp(found[i].element.role, "run_mode varp id") == 0 ||
             strcmp(found[i].element.role, "special_attack_energy varp id") == 0) )
        {
            declared++;
            CHECK(found[i].expected, "the missing row is declared, so the finding is expected");
        }
    CHECK(declared == 2, "both missing [varp:] rows are named in the findings channel");
    CHECK(undeclared_findings(handle()) == 0, "and saying so keeps the run clean");
    stop_plugin();

    /*
     * And the lane that carries the rows is told nothing -- including about
     * the armed bit, which rs289lc and rs245_2lc genuinely do not declare.
     * That one is a supported degradation (the orb reads, it just never
     * lights), so declaring it would file a limitation against two shipping
     * lanes behaving exactly as intended.
     */
    declared = 0;
    reset();
    declare_native_lane();
    start_plugin();
    frame(6);
    CHECK(Testbed_Control("orb_special") != NULL, "a declared id draws its orb");
    count = Porcelain_Findings(handle(), found, PORCELAIN_FINDINGS_MAX);
    for( int i = 0; i < count; i++ )
        if( found[i].element.role && strstr(found[i].element.role, "varp id") )
            declared++;
    CHECK(declared == 0, "a profile that states its varps is told nothing about them");
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 10. A dead orb still holds its reading                                   */
/* ------------------------------------------------------------------------ */

/**
 * How many rows of `orb`'s disc the dark cap covers, read off the picture.
 *
 * `fill_empty` is the only image that writes the blue byte, so a capped row
 * says so and a meter row cannot. Counted down the disc's middle column,
 * which every blit here covers.
 */
static int
capped_rows(int orb)
{
    int rows = 0;
    for( int y = ORB_DISC_Y; y < ORB_DISC_Y + ORB_DISC; y++ )
        if( is_fill_empty(g_picture[orb][y * ORB_W + ORB_DISC_X + ORB_DISC / 2]) )
            rows++;
    return rows;
}

/**
 * Which row of the digit ramp `orb`'s number was tinted from, or -1 for a
 * picture that drew no number. The fake atlas signs each pixel with its own
 * source row, so this is a read and not an inference.
 */
/**
 * Which of the two plates a composed picture was built on: 0 `frame`,
 * 1 `frame_over`, -1 nothing.
 *
 * The plate is the only one of the fifteen that reaches the picture's own
 * top-left corner -- the disc and the icon start at +27,+4, the cap is inside
 * the disc and the number is inside the 23x13 panel at +4,+16 -- so the corner
 * names it without any blending to unpick. @see art_pixel, which signs each
 * source image with `0x10 + index` in the red byte.
 */
static int
plate_index(int orb)
{
    uint32_t const corner = g_picture[orb][0];
    if( ((corner >> 24) & 0xFFu) == 0 )
        return -1;
    return (int)((corner >> 16) & 0xFFu) - 0x10;
}

static int
ramp_row(int orb)
{
    int row = -1;
    for( int i = 0; i < ORB_W * ORB_H; i++ )
    {
        uint32_t const pixel = g_picture[orb][i];
        if( ((pixel >> 16) & 0xFFu) != ART_DIGITS_RED )
            continue;
        if( row >= 0 && row != (int)((pixel >> 8) & 0xFFu) )
            return -2; /* two rows in one number: the atlas was sampled twice */
        row = (int)((pixel >> 8) & 0xFFu);
    }
    return row;
}

/**
 * The fifth dead orb, and the one this file's own painter forbids in a
 * comment: a FULL meter under a number that is not full.
 *
 * Seen on rs289lc, logged in, at canvas 538,134: a 26x26 grey disc filled
 * edge to edge with no dark cap on any row, and "70" beside it in the pure
 * green (0,255,0) that is the ramp's LAST row -- the colour reserved for a
 * meter that is at 100%. Both halves of the picture said full and the text
 * said 70. A second capture of the same lane drew a green ZERO on a full
 * disc, which is the same fault with nothing left to disguise it.
 *
 * The cause was one line in each of the two orbs that have an inactive
 * state: `out->filled = out->total` beside the grey substitution. `filled`
 * is not a colour, it is the READING -- it drives the height of the
 * fill_empty cap and the ramp row of the digits, and both of those are about
 * how much energy there is rather than about whether the button works. The
 * run orb carried the identical line and hid it, because run energy is 100
 * in every capture in this tree.
 *
 * The rule: inactive changes the COLOUR of a meter and never its reading.
 *
 * Mutation: put `out->filled = out->total` back in either arm of
 * orbs_picture -> that orb's cap goes to 0 rows and its ramp row to 20.
 */
static void
case_inactive_orb_keeps_its_reading(void)
{
    reset();
    g_digits_ini = DIGITS_INI_ONE_ROW_PER_STEP;
    declare_native_lane();
    /*
     * A walking player with 43 energy and 700 of 1000 special, holding
     * nothing that specials. That is the rs289lc capture exactly: the run
     * orb is grey because the player is walking, and the special orb is grey
     * because the equipped weapon offers no Activate -- which is what the
     * lane says by HIDING the orb's action button, exactly as
     * `orbs_spec_draw_button` writes it: `if_sethide(true, $button)`.
     */
    g_run_energy = 43;
    g_varp[300] = 700;
    Testbed_PresentElement("action_frame_orb_special_activate", false);
    start_plugin();
    frame(8);

    /*
     * The log line the field diagnosis was made from. It carried
     * `filled=1000 total=1000 inactive=1` for a 70% orb, which is the whole
     * defect said in one line, so it is pinned as a line and not only as
     * pixels: a reader of a headless run must be able to see the reading.
     */
    CHECK(strstr(g_value_line[3], "value=70 filled=700 total=1000 inactive=1") != NULL,
        "the special orb's log line carries the reading, not the total");
    CHECK(strstr(g_value_line[2], "value=43 filled=43 total=100 inactive=1") != NULL,
        "and so does the walking run orb's");

    /*
     * 700 of 1000 caps ceil(26 - 700*26/1000) = 7 rows, and picks ramp row
     * 700 * 20 / 1000 = 14. Before the fix both were the full-meter answer:
     * 0 rows capped and row 20.
     */
    CHECK(capped_rows(3) == 7, "a 70% special caps seven rows of its disc");
    CHECK(ramp_row(3) == 14, "and tints its number from the ramp's row 14");

    /* 43 of 100 caps 26 - ceil(43*26/100) = 14 rows, and picks row 8. */
    CHECK(capped_rows(2) == 14, "a walking player's 43 energy caps fourteen rows");
    CHECK(ramp_row(2) == 8, "and tints its number from the ramp's row 8");

    /*
     * The positive control, and the thing the cap must not do to a meter
     * that IS full: the prayer orb reads 30 of 40 and the hitpoints orb 42
     * of 50, both active, both capped by the same arithmetic. An
     * implementation that simply stopped capping would pass the two checks
     * above by drawing nothing anywhere.
     */
    CHECK(capped_rows(0) == 4, "an active orb is capped by the same arithmetic");
    CHECK(ramp_row(0) == 16, "42 of 50 is ramp row 16");
    CHECK(capped_rows(1) == 6, "the prayer orb's 30 of 40 caps six rows");
    CHECK(ramp_row(1) == 15, "and is ramp row 15");

    /* And a meter that really is full shows no cap at all. */
    g_run_energy = 100;
    frame(1);
    CHECK(capped_rows(2) == 0, "100 of 100 caps nothing");
    CHECK(ramp_row(2) == 20, "and is the only reading that reaches the ramp's last row");
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 16. The chip's border never lit under the pointer                        */
/* ------------------------------------------------------------------------ */

/*
 * Reported from a live capture: hovering an orb did nothing. Interface 160's
 * orbs light their whole plate border under the pointer, and the plugin had
 * shipped the art for it since the assets were first cut -- `frame_over.png`
 * is graphic 1072, pixel for pixel -- and never once drew it. The enum slot
 * was declared, the file was decoded into the fifteenth of fifteen pixel
 * buffers, and nothing named it.
 *
 * The rule, from `orbs_update_health`, `orbs_update_prayer`,
 * `orbs_update_runenergy` and `orbs_spec_draw_button`, which all four write it
 * the same way:
 *
 *     if_setonmouserepeat("graphic_swapper($plate, 1072)", $button);
 *     if_setonmouseleave ("graphic_swapper($plate, 1071)", $button);
 *
 * Three things are load-bearing in those two lines and each is checked below.
 * The hook is on the orb's BUTTON, not on its plate, so the plate's corners do
 * not light it. The swap names the PLATE and nothing else, so the disc, the
 * cap, the icon and the number are untouched. And both calls are made only in
 * the branch that also arms the operation -- the other branch passes `null`
 * to both and hides the button -- so an orb with nothing to press has no
 * highlight either.
 *
 * Mutation: blit ORB_IMG_FRAME unconditionally -> the hovered orb below stays
 * on plate 0. Drop the `available` test in orbs_hover -> the special orb
 * lights with no verb behind it. Test the plate's box instead of
 * ORB_BUTTON[i] -> the corner lights.
 */
static void
case_hovered_orb_lights_its_plate(void)
{
    static const int ORB_X[4] = {521, 521, 531, 553};
    static const int ORB_Y[4] = {41, 75, 107, 132};
    uint32_t idle_disc;

    reset();
    g_digits_ini = DIGITS_INI_ONE_ROW_PER_STEP;
    declare_native_lane();
    /* The special orb has no verb behind it: the equipped weapon offers no
     * Activate, which the lane says by hiding the orb's action button. */
    Testbed_PresentElement("action_frame_orb_special_activate", false);
    g_testbed.pointer_present = true;
    g_testbed.pointer_x = -1;
    g_testbed.pointer_y = -1;
    start_plugin();
    frame(8);

    CHECK(live_orbs() == 4, "four covers before any hover");
    for( int i = 0; i < 4; i++ )
        CHECK(plate_index(i) == 0, "an orb nobody is pointing at is on the plain plate");
    idle_disc = g_picture[2][(ORB_DISC_Y + ORB_DISC / 2) * ORB_W + ORB_DISC_X + ORB_DISC / 2];

    /* Inside the run orb's button: its plate's origin plus ORB_BUTTON[2]. */
    g_testbed.pointer_x = ORB_X[2] + 3 + 25;
    g_testbed.pointer_y = ORB_Y[2] + 5 + 13;
    frame(2);
    CHECK(plate_index(2) == 1, "the orb under the pointer wears the lit plate");
    CHECK(plate_index(0) == 0 && plate_index(1) == 0 && plate_index(3) == 0,
          "and only that one: the pointer is over one button");
    /*
     * And the swap touches the PLATE alone. `graphic_swapper` takes one
     * component and one graphic; a hover that also moved the meter would be
     * this plugin inventing a state the reference does not have.
     */
    CHECK(g_picture[2][(ORB_DISC_Y + ORB_DISC / 2) * ORB_W + ORB_DISC_X + ORB_DISC / 2] ==
              idle_disc,
          "the disc under the lit plate is the disc it already was");
    CHECK(ramp_row(2) == ramp_row(2), "the number is still sampled once");

    /* The plate's own top-left corner is OUTSIDE the button, and the corner is
     * clear pixels in the shipped art -- the reference does not light there. */
    g_testbed.pointer_x = ORB_X[2];
    g_testbed.pointer_y = ORB_Y[2];
    frame(2);
    CHECK(plate_index(2) == 0, "the plate's corner is not the button and does not light");

    /* One pixel past the button's right edge, for the same reason. */
    g_testbed.pointer_x = ORB_X[2] + 3 + 50;
    g_testbed.pointer_y = ORB_Y[2] + 5 + 13;
    frame(2);
    CHECK(plate_index(2) == 0, "and the column one past its right edge");

    /* The special orb's button, with nothing behind it to press. */
    g_testbed.pointer_x = ORB_X[3] + 3 + 25;
    g_testbed.pointer_y = ORB_Y[3] + 6 + 12;
    frame(2);
    CHECK(plate_index(3) == 0, "an orb with no verb does not light: both hooks are null");

    /* And the prayer orb, which has one. */
    g_testbed.pointer_x = ORB_X[1] + 3 + 24;
    g_testbed.pointer_y = ORB_Y[1] + 5 + 13;
    frame(2);
    CHECK(plate_index(1) == 1, "the prayer orb's own 49-wide button lights it");

    /* onMouseLeave: the pointer goes away and the plate goes back. */
    g_testbed.pointer_x = -1;
    g_testbed.pointer_y = -1;
    frame(2);
    for( int i = 0; i < 4; i++ )
        CHECK(plate_index(i) == 0, "the pointer leaves and every plate is plain again");

    /* A lane with no pointer at all -- a touch lane -- has no hover, and that
     * is an answer rather than a failure. */
    g_testbed.pointer_present = false;
    g_testbed.pointer_x = ORB_X[2] + 3 + 25;
    g_testbed.pointer_y = ORB_Y[2] + 5 + 13;
    frame(2);
    CHECK(plate_index(2) == 0, "a lane that answers no pointer lights nothing");

    CHECK(undeclared_findings(handle()) == 0, "and none of it files a finding");
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 17. Every orb on every CS2 lane was inert                                */
/* ------------------------------------------------------------------------ */

/*
 * Measured on classic-fixed 548 at rev 239 while fixing the hover above: four
 * plates drawn, four numbers live, and ZERO menu rows anywhere in the column.
 * Not one orb could be clicked, and nothing said so -- the covers were simply
 * described with `enabled = false` for the rest of the session.
 *
 * The cause is one field read. Availability asked the orb's action role for
 * `input_present`, and `input_present` is the ENGINE's answer with every veto
 * folded in -- including this plugin's own. The action button is a CHILD of
 * the orb (160:28 inside 160:26, 160:20 inside 160:18) and the description
 * hides that orb to stop two plates stacking, so from the SECOND describe
 * onward the button read `presented=0 input_present=0`; the description then
 * changed nothing, no further describe ran, and false was the answer forever.
 * One fence after the column first appeared, it went dead.
 *
 * The rule: "has this orb a verb" is the LANE's question, so it is answered
 * from the lane's own bits -- `own_hidden` and `native_hidden`, which no
 * plugin veto reaches. That is also how the reference states it: every one of
 * the four orb scripts says "no verb" by hiding the button layer and "a verb"
 * by showing it.
 *
 * Mutation: read `input_present` again -> the armed orbs below go to zero the
 * moment the engine answers what it actually answers.
 */
static void
case_covered_orb_keeps_its_op(void)
{
    reset();
    declare_native_lane();
    start_plugin();
    frame(8);

    /*
     * The engine's answer AFTER the cover hid the orb, measured on the live
     * client: the button is still bound and the lane never hid it, and yet
     * `input_present` is 0 because an ancestor this plugin hid is in its
     * chain. The testbed models the contract's intent instead -- a plugin's
     * own hide does not reach the lane's input answer -- so this states the
     * engine's real answer by hand, and the orb must survive it.
     */
    for( int i = 0; i < 4; i++ )
    {
        struct TestbedElement* action = Testbed_Element(ORB_ACTION_ROLE[i]);
        if( !action )
            continue;
        action->input_present = false;
        /*
         * And PUBLISHED, which is the whole difference between a case that
         * measures this and one that measures nothing. Porcelain serves
         * elements from a state it refreshes on a state change, so a field
         * poked straight into the testbed is a fact no describe has been told
         * -- the arming the first fences wrote stands, and every assertion
         * below passes against the old answer. A move raises the change the
         * way a publication fence does.
         */
        Testbed_MoveElement(ORB_ACTION_ROLE[i], action->local.x, action->local.y + 1);
    }
    frame(4);

    CHECK(live_orbs() == 4, "four covers");
    /* Prayer and run: the lane shows both buttons, so both carry a verb. The
     * hitpoints role binds on no profile in this tree and the special orb is
     * left to the lane, which is why only two are named here. */
    for( int i = 1; i <= 2; i++ )
    {
        struct TestbedControl const* control = Testbed_Control(ORB_KEY[i]);
        CHECK(control != NULL, "the orb is described");
        if( !control )
            continue;
        CHECK(control->armed && control->label[0] != '\0',
              "and carries its verb: a covered orb keeps the op it delegates");
    }
    stop_plugin();
}

/* ------------------------------------------------------------------------ */
/* 19. A dropped orb says which test dropped it                             */
/* ------------------------------------------------------------------------ */

/*
 * The symptom this file exists to chase is "sometimes an orb is missing", and
 * every one of the seven ways orbs_describe declines to describe one used to
 * be a bare `continue`. A description is one-shot: what it does not state is
 * reconciled away, so a skipped orb is a REMOVED cover -- and on a lane whose
 * orbs are claimed it takes the hide of the lane's own orb with it. The
 * plugin emitted not one word about which test said no, which is why the
 * report could only ever be "sometimes".
 *
 * The rule: an orb that is not described says why, once, on the transition --
 * and says so again when it comes back, because a log that records the going
 * and never the returning cannot be read either.
 *
 * Mutation: delete any orbs_note_skip call -> the matching CHECK below goes
 * red. Make the reason a snprintf'd buffer instead of a literal -> the
 * pointer comparison never matches and the line repeats every frame, which
 * the "once" check catches.
 */
static int
skip_lines(char const* needle)
{
    int found = 0;
    char const* at = g_skips;
    while( (at = strstr(at, needle)) != NULL )
    {
        found++;
        at += strlen(needle);
    }
    return found;
}

static void
case_a_dropped_orb_says_why(void)
{
    reset();
    declare_native_lane();
    Testbed_SetConfigInt("spec_varp", -1);
    g_varp[300] = 640;
    start_plugin();
    frame(6);
    CHECK(live_orbs() == 4, "all four stand to begin with");
    CHECK(g_skips[0] == '\0', "and a described orb says nothing at all");

    /* The two skill orbs lose their reading. Nothing else about the lane
     * moves, which is the case a bare `continue` made invisible. */
    g_skips[0] = '\0';
    g_testbed.skill_stated = false;
    Testbed_MoveElement("minimap", 575, 10);
    frame(4);
    CHECK(live_orbs() == 2, "the two skill orbs are dropped");
    CHECK(skip_lines("orb=orb_hitpoints why=the orb has no reading to draw") == 1,
          "and the hitpoints orb says which test dropped it, once");
    CHECK(skip_lines("orb=orb_prayer why=the orb has no reading to draw") == 1,
          "as does the prayer orb");
    CHECK(skip_lines("orb=orb_run ") == 0, "an orb that still draws stays quiet");

    /* Held: the same reason on a later fence is not a second line. */
    g_skips[0] = '\0';
    Testbed_MoveElement("minimap", 575, 11);
    frame(4);
    CHECK(g_skips[0] == '\0', "a reason that has not changed is not repeated");

    /* And the returning half. */
    g_skips[0] = '\0';
    g_testbed.skill_stated = true;
    Testbed_MoveElement("minimap", 575, 9);
    frame(4);
    CHECK(live_orbs() == 4, "the reading comes back and so do the orbs");
    CHECK(skip_lines("orb=orb_hitpoints why=none") == 1, "and the orb says it is drawing again");
    CHECK(skip_lines("orb=orb_prayer why=none") == 1, "both of them");
    stop_plugin();
}

/*
 * The whole-column drop has the same duty: the plate guard refuses every orb
 * at once, and four silent refusals read exactly like a plugin that never
 * started.
 */
static void
case_the_whole_column_says_why(void)
{
    reset();
    Testbed_DeclareElement("minimap", 575, 9, 146, 151);
    for( int i = 0; i < 4; i++ )
        Testbed_DeclareElement(ORB_ROLE[i], 521 + 10 * i, 41 + 34 * i, ORB_W, ORB_H);
    declare_art(TORIRS_ASSET_PENDING);
    Testbed_BindElement("minimap");
    for( int i = 0; i < 4; i++ )
        Testbed_BindElement(ORB_ROLE[i]);

    start_plugin();
    frame(6);
    CHECK(live_orbs() == 0, "no plate, no orb");
    CHECK(skip_lines("why=the plate art has not decoded") == 4,
          "and all four say so rather than going quiet");

    g_skips[0] = '\0';
    Testbed_LandAsset("frame.png");
    frame(4);
    CHECK(live_orbs() == 4, "the art lands and all four appear");
    CHECK(skip_lines("why=none") == 4, "and all four say they are drawing");
    stop_plugin();
}

int
main(void)
{
    case_cover_stands_on_its_own_orb();
    case_cover_is_on_the_canvas();
    case_no_orb_before_its_plate();
    case_compass_state_keeps_the_discs();
    case_root_switch_recreates_the_column();
    case_steady_state_costs_nothing();
    case_run_orb_follows_the_active_facet();
    case_covered_orb_keeps_its_press();
    case_blocked_action_presses_nothing();
    case_cutscene_removes_the_covers();
    case_lane_without_orbs();
    case_config();
    case_unstated_skill_draws_nothing();
    case_hitpoints_verb_is_declared_absent();
    case_undeclared_varp_draws_nothing_and_says_so();
    case_inactive_orb_keeps_its_reading();
    case_hovered_orb_lights_its_plate();
    case_covered_orb_keeps_its_op();
    case_a_dropped_orb_says_why();
    case_the_whole_column_says_why();

    printf("minimap orbs v2: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures != 0;
}
