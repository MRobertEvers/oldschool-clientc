#include "plugin/torirs_plugin_v2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_MINIMAP_ORBS;

#define CHECK(x) do { if( !(x) ) { fprintf(stderr, "minimap v2: %s\n", #x); exit(1); } } while( 0 )

static struct ToriRS_UiNode nodes[8];
static struct ToriRS_Rect map_rect = { 600, 20, 146, 151 };
static struct ToriRS_Rect housing_rect = { 575, 15, 172, 156 };
static bool housing_available = true;
static int next_image = 1;
static int released;
static int invoked_component = -1;
static int invoked_operation = -1;
static int images_drawn;
static int clipped_images;
static int text_drawn;

static bool cfg_bool(struct ToriRS_ApiV2* api, char const* key, bool* out)
{
    (void)api;
    *out = strncmp(key, "show_", 5) == 0;
    return true;
}

static bool cfg_int(struct ToriRS_ApiV2* api, char const* key, int* out)
{
    (void)api;
    if( strcmp(key, "offset_x") == 0 ) *out = 6;
    else if( strcmp(key, "offset_y") == 0 ) *out = -3;
    else if( strcmp(key, "run_varp") == 0 || strcmp(key, "spec_varp") == 0 ) *out = -1;
    else if( strcmp(key, "spec_max") == 0 ) *out = 1000;
    else *out = 0;
    return true;
}

static bool cfg_string(struct ToriRS_ApiV2* api, char const* key, char const** out)
{
    (void)api; (void)key;
    *out = "";
    return true;
}

static struct ToriRS_UiNodeRef ui_ref(struct ToriRS_ApiV2* api, char const* name)
{
    (void)api;
    if( strcmp(name, "frame.minimap") == 0 ) return (struct ToriRS_UiNodeRef){ 1 };
    if( strcmp(name, "frame.minimap.housing") == 0 ) return (struct ToriRS_UiNodeRef){ 6 };
    if( strcmp(name, "frame.orb.hitpoints") == 0 ) return (struct ToriRS_UiNodeRef){ 2 };
    if( strcmp(name, "frame.orb.prayer") == 0 ) return (struct ToriRS_UiNodeRef){ 3 };
    if( strcmp(name, "frame.orb.run") == 0 ) return (struct ToriRS_UiNodeRef){ 4 };
    if( strcmp(name, "frame.orb.special") == 0 ) return (struct ToriRS_UiNodeRef){ 5 };
    return (struct ToriRS_UiNodeRef){ 0 };
}

static bool ui_info(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef ref,
    struct ToriRS_UiNodeInfo* out)
{
    (void)api;
    if( ref.value == 1 )
    {
        out->bounds = map_rect;
        out->visible = true;
        out->enabled = true;
        out->available_facets = TORIRS_UI_FACET_ALL;
        return true;
    }
    if( ref.value == 6 )
    {
        if( !housing_available ) return false;
        out->bounds = housing_rect;
        out->visible = true;
        out->enabled = true;
        out->available_facets = TORIRS_UI_FACET_ALL;
        return true;
    }
    if( ref.value < 2 || ref.value > 5 ) return false;
    out->bounds = nodes[ref.value].bounds;
    out->visible = (nodes[ref.value].flags & TORIRS_UI_NODE_VISIBLE) != 0;
    out->enabled = (nodes[ref.value].flags & TORIRS_UI_NODE_ENABLED) != 0;
    out->available_facets = TORIRS_UI_FACET_ALL;
    return true;
}

static enum ToriRS_Result ui_update(
    struct ToriRS_ApiV2* api,
    struct ToriRS_UiNodeRef ref,
    uint32_t facets,
    struct ToriRS_UiNode const* value)
{
    (void)api;
    if( ref.value < 2 || ref.value > 5 || facets != TORIRS_UI_FACET_ALL )
        return TORIRS_RESULT_INVALID;
    nodes[ref.value] = *value;
    return TORIRS_RESULT_OK;
}

static enum ToriRS_AssetState asset_request(struct ToriRS_ApiV2* api, char const* name)
{ (void)api; (void)name; return TORIRS_ASSET_MISSING; }

static bool asset_bytes(
    struct ToriRS_ApiV2* api, char const* name, void const** data, size_t* size)
{ (void)api; (void)name; (void)data; (void)size; return false; }

static enum ToriRS_AssetState asset_image(
    struct ToriRS_ApiV2* api,
    char const* name,
    struct ToriRS_ImageRef* out)
{
    (void)api; (void)name;
    out->value = next_image++;
    return TORIRS_ASSET_READY;
}

static void image_release(struct ToriRS_ApiV2* api, struct ToriRS_ImageRef image)
{ (void)api; if( image.value ) released++; }

static void asset_release(struct ToriRS_ApiV2* api, char const* name)
{ (void)api; (void)name; }

static bool named_id(
    struct ToriRS_ApiV2* api, char const* kind, char const* name, int* out)
{
    (void)api;
    if( strcmp(kind, "iface") == 0 ) *out = 100 + (int)strlen(name);
    else if( strcmp(kind, "varp") == 0 )
        *out = strcmp(name, "run_mode") == 0 ? 173 : 300;
    else return false;
    return true;
}

static int cache_varp(struct ToriRS_ApiV2* api, int id)
{ (void)api; return id == 300 ? 500 : 0; }

static bool cache_invoke(struct ToriRS_ApiV2* api, int component, int operation)
{
    (void)api;
    invoked_component = component;
    invoked_operation = operation;
    return true;
}

static bool skill(
    struct ToriRS_ApiV2* api, int index, struct ToriRS_SkillSnapshot* out)
{
    (void)api;
    if( index != 3 && index != 5 ) return false;
    out->current_level = index == 3 ? 42 : 30;
    out->base_level = index == 3 ? 50 : 40;
    return true;
}

static int run_energy(struct ToriRS_ApiV2* api)
{ (void)api; return 75; }

static void draw_image(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_ImageRef image,
    int x,
    int y,
    int alpha)
{ (void)draw; (void)x; (void)y; (void)alpha; if( image.value ) images_drawn++; }

static void draw_image_clip(
    struct ToriRS_DrawBuilder* draw,
    struct ToriRS_ImageRef image,
    int x,
    int y,
    struct ToriRS_Rect clip,
    int alpha)
{
    (void)draw; (void)x; (void)y; (void)alpha;
    if( image.value && clip.width > 0 && clip.height > 0 ) clipped_images++;
}

static void draw_text(
    struct ToriRS_DrawBuilder* draw,
    int x,
    int y,
    char const* text,
    uint32_t rgb)
{ (void)draw; (void)x; (void)y; (void)rgb; if( text && text[0] ) text_drawn++; }

static void notify(struct ToriRS_ApiV2* api, char const* text)
{ (void)api; (void)text; }

static void log_line(struct ToriRS_ApiV2* api, char const* format, ...)
{ (void)api; (void)format; }

int main(void)
{
    struct ToriRS_ApiV2 api;
    struct ToriRS_GameApiV2 game;
    struct ToriRS_DrawBuilder draw;
    void* state;

    memset(&api, 0, sizeof(api));
    memset(&game, 0, sizeof(game));
    memset(&draw, 0, sizeof(draw));
    api.core.notify = notify;
    api.core.log = log_line;
    api.config.get_bool = cfg_bool;
    api.config.get_int = cfg_int;
    api.config.get_string = cfg_string;
    api.ui.ref = ui_ref;
    api.ui.info = ui_info;
    api.ui.update = ui_update;
    api.assets.request = asset_request;
    api.assets.bytes = asset_bytes;
    api.assets.image = asset_image;
    api.assets.image_release = image_release;
    api.assets.release = asset_release;
    api.cache.named_id = named_id;
    api.cache.varp = cache_varp;
    api.cache.invoke = cache_invoke;
    game.skill = skill;
    game.run_energy = run_energy;
    api.game = &game;
    draw.image = draw_image;
    draw.image_clip = draw_image_clip;
    draw.text = draw_text;

    CHECK(TORIRS_PLUGIN_MINIMAP_ORBS.struct_size == sizeof(TORIRS_PLUGIN_MINIMAP_ORBS));
    CHECK(TORIRS_PLUGIN_MINIMAP_ORBS.state_size > 0);
    CHECK(TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_ui_node_draw != NULL);
    CHECK(TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_ui_node_action != NULL);
    CHECK(TORIRS_PLUGIN_MINIMAP_ORBS.ui_contributions != NULL);
    for( int i = 0; i < 4; i++ )
        CHECK(strcmp(
                  TORIRS_PLUGIN_MINIMAP_ORBS.ui_contributions[i].value.parent,
                  "frame.minimap.housing") == 0);
    state = calloc(1, TORIRS_PLUGIN_MINIMAP_ORBS.state_size);
    CHECK(state != NULL);
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_start(&api, state);
    CHECK(nodes[2].parent && strcmp(nodes[2].parent, "frame.minimap.housing") == 0);
    CHECK(nodes[2].bounds.x == map_rect.x + 6 - 57);
    CHECK(nodes[2].bounds.width == 57 && nodes[2].bounds.height == 34);
    CHECK(nodes[2].state_images[TORIRS_UI_VISUAL_IDLE].value != 0);
    CHECK(nodes[2].action_count == 1 && strcmp(nodes[2].actions[0], "Cure") == 0);

    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_ui_node_draw(
        &api, state, (struct ToriRS_UiNodeRef){ 2 }, &draw);
    CHECK(images_drawn == 2);
    CHECK(clipped_images == 1);
    CHECK(text_drawn == 1);
    CHECK(TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_ui_node_action(
              &api, state, (struct ToriRS_UiNodeRef){ 2 }, "Cure") ==
          TORIRS_CALLBACK_CONSUME);
    CHECK(invoked_component >= 100 && invoked_operation == 0);

    map_rect.x = 500;
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_placement_changed(&api, state, 2);
    CHECK(nodes[2].bounds.x == map_rect.x + 6 - 57);
    housing_available = false;
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_placement_changed(&api, state, 3);
    CHECK(nodes[2].parent && strcmp(nodes[2].parent, "frame.minimap") == 0);
    housing_available = true;
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_placement_changed(&api, state, 4);
    CHECK(nodes[2].parent && strcmp(nodes[2].parent, "frame.minimap.housing") == 0);
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_stop(&api, state);
    CHECK(released == 15);
    free(state);
    puts("minimap orbs v2: ok");
    return 0;
}
