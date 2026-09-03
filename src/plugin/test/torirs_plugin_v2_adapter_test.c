#include "plugin/torirs_plugin_v2_adapter.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(expr, message)                                                                       \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if( !(expr) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));                    \
        }                                                                                          \
    } while( 0 )

struct Fake
{
    char log[128];
    char notification[128];
    char config_value[64];
    char config_set[64];
    int text_input;
    int last_a;
    int last_b;
    int last_c;
    int last_d;
    int last_e;
    int last_f;
    int calls;

    int placement_area;
    int placement_anchor;
    char reservation[64];

    int asset_resident;
    int image_ready;
    unsigned char bytes[4];

    int panel_count;
    int panel_kind[12];
    char panel_id[12][32];
    char panel_label[12][64];
    char panel_options[128];
    int panel_value;

    int hook_calls;
    char hook_name[128];
};

static struct Fake fake;
static int context_storage;

static struct Fake*
state(struct ToriRS_PluginCtx* context)
{
    CHECK(context == (struct ToriRS_PluginCtx*)&context_storage, "legacy context is preserved");
    return &fake;
}

static void
fake_log(
    struct ToriRS_PluginCtx* context,
    char const* format,
    ...)
{
    va_list args;
    struct Fake* f = state(context);

    va_start(args, format);
    (void)vsnprintf(f->log, sizeof(f->log), format, args);
    va_end(args);
}

static void
fake_notify(
    struct ToriRS_PluginCtx* context,
    char const* text)
{
    (void)snprintf(state(context)->notification, sizeof(fake.notification), "%s", text);
}

static int
fake_screen(struct ToriRS_PluginCtx* context)
{
    return state(context)->calls++, 30;
}
static uint64_t
fake_frame_ms(struct ToriRS_PluginCtx* context)
{
    return state(context)->calls++, 1234;
}
static uint64_t
fake_frame_work_us(struct ToriRS_PluginCtx* context)
{
    return state(context)->calls++, 567;
}

static int
fake_lane(
    struct ToriRS_PluginCtx* context,
    struct ToriRS_PluginLane* out)
{
    (void)state(context);
    *out = (struct ToriRS_PluginLane){ TORIRS_PLUGIN_GAME_RS2, TORIRS_PLUGIN_EPOCH_DAT1, 245 };
    return 1;
}

static int
fake_cfg_has(
    struct ToriRS_PluginCtx* context,
    char const* key)
{
    (void)state(context);
    return strcmp(key, "known") == 0;
}

static int
fake_cfg_bool(
    struct ToriRS_PluginCtx* context,
    char const* key)
{
    return state(context)->calls++, strcmp(key, "known") == 0;
}
static int
fake_cfg_int(
    struct ToriRS_PluginCtx* context,
    char const* key)
{
    return state(context)->calls++, strcmp(key, "known") == 0 ? 42 : 0;
}
static uint32_t
fake_cfg_color(
    struct ToriRS_PluginCtx* context,
    char const* key)
{
    return state(context)->calls++, strcmp(key, "known") == 0 ? 0x123456u : 0;
}
static char const*
fake_cfg_str(
    struct ToriRS_PluginCtx* context,
    char const* key)
{
    return state(context)->calls++, strcmp(key, "known") == 0 ? "value" : "";
}

static void
fake_cfg_set(
    struct ToriRS_PluginCtx* context,
    char const* key,
    char const* value)
{
    struct Fake* f = state(context);
    (void)snprintf(f->config_set, sizeof(f->config_set), "%s=%s", key, value);
}

#define FAKE_QUERY(name, type, answer)                                                             \
    static int name(struct ToriRS_PluginCtx* context, type* out)                                   \
    {                                                                                              \
        (void)out;                                                                                 \
        return state(context)->calls++, (answer);                                                  \
    }

FAKE_QUERY(
    fake_local_player,
    struct ToriRS_PluginPlayerSnap,
    1)

static int
fake_npc_next(
    struct ToriRS_PluginCtx* context,
    int iterator,
    struct ToriRS_PluginNpcSnap* out)
{
    (void)out;
    return state(context)->calls++, iterator < 0 ? 4 : -1;
}

static int
fake_npc_by_slot(
    struct ToriRS_PluginCtx* context,
    int slot,
    struct ToriRS_PluginNpcSnap* out)
{
    (void)out;
    return state(context)->last_a = slot, slot == 7;
}

static int
fake_player_next(
    struct ToriRS_PluginCtx* context,
    int iterator,
    struct ToriRS_PluginPlayerSnap* out)
{
    (void)out;
    return state(context)->calls++, iterator < 0 ? 5 : -1;
}

static int
fake_obj_next(
    struct ToriRS_PluginCtx* context,
    int iterator,
    struct ToriRS_PluginObjSnap* out)
{
    (void)out;
    return state(context)->calls++, iterator < 0 ? 6 : -1;
}

static int
fake_loc_next(
    struct ToriRS_PluginCtx* context,
    int iterator,
    struct ToriRS_PluginLocSnap* out)
{
    (void)out;
    return state(context)->calls++, iterator < 0 ? 8 : -1;
}

static int
fake_key(
    struct ToriRS_PluginCtx* context,
    int key)
{
    return state(context)->last_a = key, key == 42;
}

static int
fake_pointer(
    struct ToriRS_PluginCtx* context,
    int* x,
    int* y)
{
    (void)state(context);
    *x = 11;
    *y = 22;
    return 1;
}

static int
fake_hover_tile(
    struct ToriRS_PluginCtx* context,
    int* x,
    int* z,
    int* level)
{
    (void)state(context);
    *x = 3200;
    *z = 3201;
    *level = 2;
    return 1;
}

static int
fake_hover_entity(
    struct ToriRS_PluginCtx* context,
    struct ToriRS_PluginHoverEntity* out)
{
    (void)state(context);
    out->element_id = 99;
    return 1;
}

static void
fake_text_input(
    struct ToriRS_PluginCtx* context,
    int on)
{
    state(context)->text_input = on;
}

static uint32_t
fake_placement_revision(struct ToriRS_PluginCtx* context)
{
    return state(context)->calls++, 77;
}

static int
fake_placement_rect_next(
    struct ToriRS_PluginCtx* context,
    int area,
    int iterator,
    struct ToriRS_PlacementRect* out)
{
    struct Fake* f = state(context);
    f->placement_area = area;
    if( iterator < 0 )
    {
        *out = (struct ToriRS_PlacementRect){ 0, 0, 10, 10 };
        return 0;
    }
    if( iterator == 0 )
    {
        *out = (struct ToriRS_PlacementRect){ 20, 30, 40, 50 };
        return 1;
    }
    return -1;
}

static int
fake_placement_place(
    struct ToriRS_PluginCtx* context,
    int area,
    int anchor,
    int width,
    int height,
    int margin,
    struct ToriRS_PlacementRect* out)
{
    struct Fake* f = state(context);
    f->placement_area = area;
    f->placement_anchor = anchor;
    f->last_a = width;
    f->last_b = height;
    f->last_c = margin;
    *out = (struct ToriRS_PlacementRect){ 4, 5, width, height };
    return 1;
}

static int
fake_placement_contains(
    struct ToriRS_PluginCtx* context,
    int area,
    struct ToriRS_PlacementRect const* rect)
{
    struct Fake* f = state(context);
    f->placement_area = area;
    return rect->x == 1 && rect->w == 3;
}

static int
fake_placement_reserve(
    struct ToriRS_PluginCtx* context,
    char const* name,
    int area,
    int edge,
    int pixels)
{
    struct Fake* f = state(context);
    (void)snprintf(f->reservation, sizeof(f->reservation), "%s", name);
    f->placement_area = area;
    f->last_a = edge;
    f->last_b = pixels;
    return pixels != 999;
}

static int
fake_reservation_rect(
    struct ToriRS_PluginCtx* context,
    char const* name,
    struct ToriRS_PlacementRect* out)
{
    (void)state(context);
    if( strcmp(name, "dock") != 0 )
        return 0;
    *out = (struct ToriRS_PlacementRect){ 90, 0, 10, 100 };
    return 1;
}

static int
fake_frame_offer_next(
    struct ToriRS_PluginCtx* context,
    int iterator,
    struct ToriRS_PluginFrameInfo* out)
{
    (void)state(context);
    if( iterator >= 0 )
        return -1;
    memset(out, 0, sizeof(*out));
    (void)snprintf(out->id, sizeof(out->id), "%s", "frames/window");
    (void)snprintf(out->title, sizeof(out->title), "%s", "Window");
    (void)snprintf(out->provider, sizeof(out->provider), "%s", "frames");
    out->canvas = TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW;
    out->width = 765;
    out->height = 503;
    out->available = 1;
    return 0;
}

static void
fake_frame_selection(
    struct ToriRS_PluginCtx* context,
    struct ToriRS_PluginFrameSelection* out)
{
    (void)state(context);
    memset(out, 0, sizeof(*out));
    (void)snprintf(out->requested, sizeof(out->requested), "%s", "frames/window");
    (void)snprintf(out->active, sizeof(out->active), "%s", "core/native");
    out->status = TORIRS_PLUGIN_FRAME_FALLBACK;
    (void)snprintf(out->reason, sizeof(out->reason), "%s", "not ready");
    out->revision = 12;
}

static int
fake_frame_select(
    struct ToriRS_PluginCtx* context,
    char const* id)
{
    return state(context)->last_a = (int)strlen(id), strcmp(id, "bad") != 0;
}
static void
fake_frame_invalidate(struct ToriRS_PluginCtx* context)
{
    state(context)->calls++;
}

static int
fake_project(
    struct ToriRS_PluginCtx* context,
    int fine_x,
    int fine_z,
    int height,
    int* x,
    int* y)
{
    struct Fake* f = state(context);
    f->last_a = fine_x;
    f->last_b = fine_z;
    f->last_c = height;
    *x = 12;
    *y = 13;
    return 1;
}

static int
fake_element_height(
    struct ToriRS_PluginCtx* context,
    int id)
{
    return state(context)->last_a = id, 222;
}
static int
fake_hsl_from_rgb(
    struct ToriRS_PluginCtx* context,
    uint32_t rgb)
{
    return state(context)->last_a = (int)rgb, 123;
}
static uint32_t
fake_hsl_to_rgb(
    struct ToriRS_PluginCtx* context,
    int hsl)
{
    return state(context)->last_a = hsl, 0xabcdef;
}

static int
fake_asset_load(
    struct ToriRS_PluginCtx* context,
    char const* name)
{
    struct Fake* f = state(context);
    (void)name;
    return f->asset_resident;
}

static void const*
fake_asset_data(
    struct ToriRS_PluginCtx* context,
    char const* name,
    int* out_size)
{
    struct Fake* f = state(context);
    (void)name;
    if( out_size )
        *out_size = f->asset_resident ? 4 : 0;
    return f->asset_resident ? f->bytes : NULL;
}

static int
fake_asset_save(
    struct ToriRS_PluginCtx* context,
    char const* name,
    void const* data,
    int size)
{
    struct Fake* f = state(context);
    (void)data;
    (void)snprintf(f->config_set, sizeof(f->config_set), "%s", name);
    f->last_a = size;
    return strcmp(name, "fail.bin") != 0;
}

static void
fake_asset_release(
    struct ToriRS_PluginCtx* context,
    char const* name)
{
    (void)snprintf(state(context)->config_set, sizeof(fake.config_set), "%s", name);
}
static int
fake_image_load(
    struct ToriRS_PluginCtx* context,
    char const* name)
{
    return state(context)->last_a = (int)strlen(name), strcmp(name, "full.png") == 0 ? -1 : 14;
}

static int
fake_image_size(
    struct ToriRS_PluginCtx* context,
    int image,
    int* width,
    int* height)
{
    struct Fake* f = state(context);
    f->last_a = image;
    if( !f->image_ready )
        return 0;
    *width = 32;
    *height = 24;
    return 1;
}

static void
fake_image_release(
    struct ToriRS_PluginCtx* context,
    int image)
{
    state(context)->last_a = image;
}
static int
fake_model_load(
    struct ToriRS_PluginCtx* context,
    char const* name)
{
    return state(context)->last_a = (int)strlen(name), strcmp(name, "full.model") == 0 ? -1 : 18;
}

static int
fake_screenshot(
    struct ToriRS_PluginCtx* context,
    char const* destination,
    char const* name,
    char* out,
    int out_size)
{
    struct Fake* f = state(context);
    f->last_a = out_size;
    (void)snprintf(out, (size_t)out_size, "%s/%s", destination ? destination : "", name);
    return strcmp(name, "fail.png") != 0;
}

static int
fake_mesh_create(struct ToriRS_PluginCtx* context)
{
    return state(context)->last_a == -1 ? -1 : 21;
}
static void
fake_mesh_destroy(
    struct ToriRS_PluginCtx* context,
    int mesh)
{
    state(context)->last_a = mesh;
}
static int
fake_mesh_vertex(
    struct ToriRS_PluginCtx* context,
    int mesh,
    int x,
    int y,
    int z)
{
    struct Fake* f = state(context);
    f->last_a = mesh;
    f->last_b = x;
    f->last_c = y;
    f->last_d = z;
    return x == 999 ? -1 : 2;
}

static int
fake_mesh_face(
    struct ToriRS_PluginCtx* context,
    int mesh,
    int a,
    int b,
    int c,
    int hsl,
    int alpha)
{
    struct Fake* f = state(context);
    f->last_a = mesh;
    f->last_b = a + b + c;
    f->last_c = hsl;
    f->last_d = alpha;
    return a == 999 ? -1 : 3;
}

static int
fake_object_create(struct ToriRS_PluginCtx* context)
{
    return state(context)->last_a == -1 ? -1 : 31;
}
static void
fake_object_destroy(
    struct ToriRS_PluginCtx* context,
    int object)
{
    state(context)->last_a = object;
}

static void
fake_object_model(
    struct ToriRS_PluginCtx* context,
    int object,
    enum ToriRS_PluginModelSource source,
    int id)
{
    struct Fake* f = state(context);
    f->last_a = object;
    f->last_b = source;
    f->last_c = id;
}

static void
fake_object_position(
    struct ToriRS_PluginCtx* context,
    int object,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int yaw)
{
    struct Fake* f = state(context);
    f->last_a = object;
    f->last_b = tile_x + tile_z;
    f->last_c = level;
    f->last_d = height;
    f->last_e = yaw;
}

static void
fake_object_active(
    struct ToriRS_PluginCtx* context,
    int object,
    int active)
{
    struct Fake* f = state(context);
    f->last_a = object;
    f->last_b = active;
}

static int
fake_frame_root(struct ToriRS_PluginCtx* context)
{
    return state(context)->calls++, 548;
}
static int
fake_varbit(
    struct ToriRS_PluginCtx* context,
    int id)
{
    return state(context)->last_a = id, id + 1;
}
static int
fake_varp(
    struct ToriRS_PluginCtx* context,
    int id)
{
    return state(context)->last_a = id, id + 2;
}

static int
fake_component_rect(
    struct ToriRS_PluginCtx* context,
    int id,
    int* x,
    int* y,
    int* width,
    int* height)
{
    state(context)->last_a = id;
    if( id < 0 )
        return 0;
    *x = 1;
    *y = 2;
    *width = 3;
    *height = 4;
    return 1;
}

static int
fake_if_click(
    struct ToriRS_PluginCtx* context,
    int id,
    int op)
{
    struct Fake* f = state(context);
    f->last_a = id;
    f->last_b = op;
    return id >= 0;
}

static bool
fake_panel_request(
    struct ToriRS_PluginCtx* context,
    struct ToriRS_PluginPanelDesc const* desc)
{
    return state(context)->last_a = desc->preferred_width, true;
}

static bool
fake_panel_widget(
    struct ToriRS_PluginCtx* context,
    int kind,
    char const* id,
    char const* label)
{
    struct Fake* f = state(context);
    int const at = f->panel_count++;
    f->panel_kind[at] = kind;
    (void)snprintf(f->panel_id[at], sizeof(f->panel_id[at]), "%s", id);
    (void)snprintf(f->panel_label[at], sizeof(f->panel_label[at]), "%s", label ? label : "");
    return true;
}

static bool
fake_panel_set_value(
    struct ToriRS_PluginCtx* context,
    char const* id,
    int value)
{
    struct Fake* f = state(context);
    (void)id;
    f->panel_value = value;
    return true;
}
static bool
fake_panel_set_options(
    struct ToriRS_PluginCtx* context,
    char const* id,
    char const* choices,
    int selected)
{
    struct Fake* f = state(context);
    (void)id;
    (void)snprintf(f->panel_options, sizeof(f->panel_options), "%s", choices);
    f->panel_value = selected;
    return true;
}
static bool
fake_panel_set_height(
    struct ToriRS_PluginCtx* context,
    char const* id,
    int height)
{
    struct Fake* f = state(context);
    (void)id;
    f->last_a = height;
    return true;
}
static bool
fake_panel_attention(
    struct ToriRS_PluginCtx* context,
    bool wanted)
{
    state(context)->last_a = wanted;
    return true;
}
static void
fake_panel_clear(struct ToriRS_PluginCtx* context)
{
    state(context)->calls++;
}

static void
fake_draw_rect(
    struct ToriRS_PluginCtx* context,
    void* surface,
    int x,
    int y,
    int width,
    int height,
    uint32_t rgb,
    int alpha)
{
    struct Fake* f = state(context);
    CHECK(surface == f, "draw surface is preserved");
    f->last_a = x;
    f->last_b = y;
    f->last_c = width;
    f->last_d = height;
    f->last_e = (int)rgb;
    f->last_f = alpha;
}
static void
fake_draw_line(
    struct ToriRS_PluginCtx* context,
    void* surface,
    int x0,
    int y0,
    int x1,
    int y1,
    uint32_t rgb)
{
    struct Fake* f = state(context);
    CHECK(surface == f, "line surface");
    f->last_a = x0;
    f->last_b = y0;
    f->last_c = x1;
    f->last_d = y1;
    f->last_e = (int)rgb;
}
static void
fake_draw_text(
    struct ToriRS_PluginCtx* context,
    void* surface,
    int x,
    int y,
    char const* text,
    uint32_t rgb)
{
    struct Fake* f = state(context);
    CHECK(surface == f, "text surface");
    f->last_a = x;
    f->last_b = y;
    f->last_c = (int)rgb;
    (void)snprintf(f->notification, sizeof(f->notification), "%s", text);
}
static void
fake_draw_image(
    struct ToriRS_PluginCtx* context,
    void* surface,
    int image,
    int x,
    int y,
    int cx,
    int cy,
    int cw,
    int ch,
    int trans)
{
    struct Fake* f = state(context);
    CHECK(surface == f, "image surface");
    f->last_a = image;
    f->last_b = x;
    f->last_c = y;
    f->last_d = cx + cy + cw + ch;
    f->last_e = trans;
}
static void
fake_draw_tile(
    struct ToriRS_PluginCtx* context,
    void* surface,
    int tx,
    int tz,
    int level,
    uint32_t outline,
    uint32_t fill,
    int alpha)
{
    struct Fake* f = state(context);
    CHECK(surface == f, "tile surface");
    f->last_a = tx;
    f->last_b = tz;
    f->last_c = level;
    f->last_d = (int)outline;
    f->last_e = (int)fill;
    f->last_f = alpha;
}
static void
fake_draw_hull(
    struct ToriRS_PluginCtx* context,
    void* surface,
    int element,
    uint32_t rgb,
    int alpha,
    int shape)
{
    struct Fake* f = state(context);
    CHECK(surface == f, "hull surface");
    f->last_a = element;
    f->last_b = (int)rgb;
    f->last_c = alpha;
    f->last_d = shape;
}
static int
fake_hit_region(
    struct ToriRS_PluginCtx* context,
    void* surface,
    int x,
    int y,
    int width,
    int height,
    char const* const* ops,
    int count,
    uint32_t tag)
{
    struct Fake* f = state(context);
    CHECK(surface == f, "hit surface");
    f->last_a = x;
    f->last_b = y;
    f->last_c = width;
    f->last_d = height;
    f->last_e = count;
    f->last_f = (int)tag;
    (void)snprintf(f->notification, sizeof(f->notification), "%s", ops[0]);
    return 1;
}

static int
fake_layout_slot(
    struct ToriRS_PluginCtx* context,
    int slot,
    int x,
    int y,
    int width,
    int height)
{
    struct Fake* f = state(context);
    f->last_a = slot;
    f->last_b = x;
    f->last_c = y;
    f->last_d = width;
    f->last_e = height;
    return 1;
}
static int
fake_layout_slot_at(
    struct ToriRS_PluginCtx* context,
    int slot,
    int member,
    int x,
    int y,
    int width,
    int height)
{
    struct Fake* f = state(context);
    f->last_a = slot;
    f->last_b = member;
    f->last_c = x + y;
    f->last_d = width;
    f->last_e = height;
    return 1;
}
static int
fake_layout_skin(
    struct ToriRS_PluginCtx* context,
    int slot,
    int image,
    int mask)
{
    struct Fake* f = state(context);
    f->last_a = slot;
    f->last_b = image;
    f->last_c = mask;
    return 1;
}
static int
fake_layout_overlay(
    struct ToriRS_PluginCtx* context,
    int slot,
    int image,
    int x,
    int y,
    int trans)
{
    struct Fake* f = state(context);
    f->last_a = slot;
    f->last_b = image;
    f->last_c = x;
    f->last_d = y;
    f->last_e = trans;
    return 1;
}
static int
fake_layout_scrollbar(
    struct ToriRS_PluginCtx* context,
    int track,
    int top,
    int mid,
    int bottom,
    int up,
    int down)
{
    struct Fake* f = state(context);
    f->last_a = track;
    f->last_b = top;
    f->last_c = mid;
    f->last_d = bottom;
    f->last_e = up;
    f->last_f = down;
    return 1;
}

static bool
hook_capability(
    void* user,
    struct ToriRS_PluginCtx* context,
    char const* name)
{
    CHECK(user == &fake, "hook user");
    (void)state(context);
    return strcmp(name, "touch") == 0;
}
static struct ToriRS_UiNodeRef
hook_ui_ref(
    void* user,
    struct ToriRS_PluginCtx* context,
    char const* name)
{
    struct Fake* f = user;
    (void)state(context);
    f->hook_calls++;
    (void)snprintf(f->hook_name, sizeof(f->hook_name), "%s", name);
    return (struct ToriRS_UiNodeRef){ 91 };
}
static bool
hook_ui_info(
    void* user,
    struct ToriRS_PluginCtx* context,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_UiNodeInfo* out)
{
    struct Fake* f = user;
    (void)state(context);
    f->hook_calls++;
    out->struct_size = sizeof(*out);
    out->bounds = (struct ToriRS_Rect){ 1, 2, 3, 4 };
    out->clip = TORIRS_UI_CLIP_PARENT;
    out->state_images[TORIRS_UI_VISUAL_HOVER].value = 22;
    (void)snprintf(out->label, sizeof(out->label), "%s", "Rich");
    out->label_x = 5;
    out->hit_rect = (struct ToriRS_Rect){ 0, 1, 5, 6 };
    out->action_count = 2;
    (void)snprintf(out->actions[0], sizeof(out->actions[0]), "%s", "activate");
    (void)snprintf(out->actions[1], sizeof(out->actions[1]), "%s", "inspect");
    return node.value == 91;
}
static bool
hook_ui_invoke(
    void* user,
    struct ToriRS_PluginCtx* context,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    struct Fake* f = user;
    (void)state(context);
    f->hook_calls++;
    (void)snprintf(f->hook_name, sizeof(f->hook_name), "%s", action);
    return node.value == 91;
}
static bool
hook_ui_contribution(
    void* user,
    struct ToriRS_PluginCtx* context,
    char const* node,
    uint32_t facets,
    struct ToriRS_UiContributionInfo* out)
{
    struct Fake* f = user;
    (void)state(context);
    f->hook_calls++;
    out->struct_size = sizeof(*out);
    out->active_facets = facets;
    (void)snprintf(f->hook_name, sizeof(f->hook_name), "%s", node);
    return true;
}
static void
hook_frame_node(
    void* user,
    struct ToriRS_PluginCtx* context,
    char const* name,
    struct ToriRS_UiNode const* node)
{
    struct Fake* f = user;
    (void)state(context);
    f->hook_calls++;
    f->last_a = node->bounds.x;
    f->last_b = node->clip;
    f->last_c = node->state_images[TORIRS_UI_VISUAL_HOVER].value;
    f->last_d = node->label_x;
    f->last_e = node->hit_rect.width;
    f->last_f = (int)node->action_count;
    (void)snprintf(f->hook_name, sizeof(f->hook_name), "%s", name);
}
static void
hook_frame_reason(
    void* user,
    struct ToriRS_PluginCtx* context,
    char const* reason)
{
    struct Fake* f = user;
    (void)state(context);
    f->hook_calls++;
    (void)snprintf(f->hook_name, sizeof(f->hook_name), "%s", reason);
}
static void
hook_model_release(
    void* user,
    struct ToriRS_PluginCtx* context,
    struct ToriRS_ModelRef model)
{
    struct Fake* f = user;
    (void)state(context);
    f->hook_calls++;
    f->last_a = model.value;
}
static void
hook_panel_select(
    void* user,
    struct ToriRS_PluginCtx* context,
    char const* id,
    char const* label,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int count)
{
    struct Fake* f = user;
    (void)state(context);
    (void)label;
    (void)options;
    f->hook_calls++;
    f->last_a = count;
    (void)snprintf(f->hook_name, sizeof(f->hook_name), "%s=%s", id, value);
}

static struct ToriRS_PluginApi
legacy_api(void)
{
    return (struct ToriRS_PluginApi){
        .abi_version = TORIRS_PLUGIN_ABI,
        .log = fake_log,
        .notify = fake_notify,
        .screen = fake_screen,
        .frame_ms = fake_frame_ms,
        .frame_work_us = fake_frame_work_us,
        .local_player = fake_local_player,
        .npc_next = fake_npc_next,
        .npc_by_slot = fake_npc_by_slot,
        .player_next = fake_player_next,
        .obj_next = fake_obj_next,
        .loc_next = fake_loc_next,
        .key_held = fake_key,
        .hover_tile = fake_hover_tile,
        .hover_entity = fake_hover_entity,
        .mouse_pos = fake_pointer,
        .text_input = fake_text_input,
        .project = fake_project,
        .element_height = fake_element_height,
        .hsl_from_rgb = fake_hsl_from_rgb,
        .hsl_to_rgb = fake_hsl_to_rgb,
        .cfg_has = fake_cfg_has,
        .cfg_bool = fake_cfg_bool,
        .cfg_int = fake_cfg_int,
        .cfg_color = fake_cfg_color,
        .cfg_str = fake_cfg_str,
        .cfg_set = fake_cfg_set,
        .lane = fake_lane,
        .placement_revision = fake_placement_revision,
        .placement_rect_next = fake_placement_rect_next,
        .placement_place = fake_placement_place,
        .placement_contains = fake_placement_contains,
        .placement_reserve = fake_placement_reserve,
        .placement_reservation_rect = fake_reservation_rect,
        .frame_offer_next = fake_frame_offer_next,
        .frame_selection = fake_frame_selection,
        .frame_select = fake_frame_select,
        .frame_invalidate = fake_frame_invalidate,
        .asset_load = fake_asset_load,
        .asset_data = fake_asset_data,
        .asset_save = fake_asset_save,
        .asset_release = fake_asset_release,
        .image_load = fake_image_load,
        .image_size = fake_image_size,
        .image_release = fake_image_release,
        .model_load = fake_model_load,
        .screenshot = fake_screenshot,
        .mesh_create = fake_mesh_create,
        .mesh_destroy = fake_mesh_destroy,
        .mesh_vertex = fake_mesh_vertex,
        .mesh_face = fake_mesh_face,
        .object_create = fake_object_create,
        .object_destroy = fake_object_destroy,
        .object_set_model = fake_object_model,
        .object_set_position = fake_object_position,
        .object_set_active = fake_object_active,
        .frame_root = fake_frame_root,
        .varbit = fake_varbit,
        .varp = fake_varp,
        .component_rect = fake_component_rect,
        .if_click = fake_if_click,
        .panel_request = fake_panel_request,
        .panel_widget = fake_panel_widget,
        .panel_set_value = fake_panel_set_value,
        .panel_set_options = fake_panel_set_options,
        .panel_set_height = fake_panel_set_height,
        .panel_set_attention = fake_panel_attention,
        .panel_clear = fake_panel_clear,
        .draw_rect = fake_draw_rect,
        .draw_line = fake_draw_line,
        .draw_text = fake_draw_text,
        .draw_image = fake_draw_image,
        .draw_tile = fake_draw_tile,
        .draw_hull = fake_draw_hull,
        .hit_region = fake_hit_region,
        .layout_slot = fake_layout_slot,
        .layout_slot_at = fake_layout_slot_at,
        .layout_slot_skin = fake_layout_skin,
        .layout_slot_overlay = fake_layout_overlay,
        .layout_scrollbar = fake_layout_scrollbar,
    };
}

static struct ToriRS_PluginV2AdapterHooks
hooks(void)
{
    return (struct ToriRS_PluginV2AdapterHooks){
        .struct_size = sizeof(struct ToriRS_PluginV2AdapterHooks),
        .user = &fake,
        .capability = hook_capability,
        .ui_ref = hook_ui_ref,
        .ui_info = hook_ui_info,
        .ui_invoke = hook_ui_invoke,
        .ui_contribution_info = hook_ui_contribution,
        .frame_ui_node = hook_frame_node,
        .frame_reason = hook_frame_reason,
        .model_release = hook_model_release,
        .panel_select = hook_panel_select,
    };
}

static void
test_construction_and_basic_modules(
    struct ToriRS_PluginV2Adapter* adapter,
    struct ToriRS_PluginApi* legacy,
    struct ToriRS_PluginV2AdapterHooks* adapter_hooks)
{
    struct ToriRS_PluginV2Adapter rejected;
    struct ToriRS_PluginApi wrong = *legacy;
    struct ToriRS_PluginV2AdapterHooks short_hooks = { .struct_size = 1 };
    struct ToriRS_ApiV2* api;
    struct ToriRS_PluginLane lane;
    struct ToriRS_PluginPlayerSnap player;
    struct ToriRS_PluginNpcSnap npc;
    struct ToriRS_PluginObjSnap item;
    struct ToriRS_PluginLocSnap scenery;
    struct ToriRS_PluginHoverEntity hover;
    struct ToriRS_UiNodeInfo ui_info = { .struct_size = sizeof(ui_info) };
    struct ToriRS_UiContributionInfo contribution_info;
    struct ToriRS_UiNodeRef ref;
    char const* string = NULL;
    uint32_t color = 0;
    bool boolean = false;
    int integer = 0;
    int x;
    int y;
    int z;

    wrong.abi_version--;
    CHECK(
        !ToriRS_PluginV2Adapter_Init(
            &rejected, &wrong, (struct ToriRS_PluginCtx*)&context_storage, adapter_hooks),
        "an incompatible legacy ABI is rejected");
    CHECK(
        !ToriRS_PluginV2Adapter_Init(
            &rejected, legacy, (struct ToriRS_PluginCtx*)&context_storage, &short_hooks),
        "a truncated hook header is rejected");
    CHECK(
        ToriRS_PluginV2Adapter_Init(
            adapter, legacy, (struct ToriRS_PluginCtx*)&context_storage, adapter_hooks),
        "a current legacy table constructs a v2 instance");
    api = ToriRS_PluginV2Adapter_Api(adapter);
    CHECK(api->struct_size == sizeof(*api), "top-level struct_size is exact");
    CHECK(
        api->major_version == TORIRS_PLUGIN_API_V2_MAJOR &&
            api->minor_version == TORIRS_PLUGIN_API_V2_MINOR,
        "the v2 version is published");
    CHECK(
        api->core.struct_size == sizeof(api->core) &&
            api->config.struct_size == sizeof(api->config) &&
            api->world.struct_size == sizeof(api->world) &&
            api->input.struct_size == sizeof(api->input) &&
            api->ui.struct_size == sizeof(api->ui) &&
            api->placement.struct_size == sizeof(api->placement) &&
            api->frame.struct_size == sizeof(api->frame) &&
            api->draw.struct_size == sizeof(api->draw) &&
            api->assets.struct_size == sizeof(api->assets) &&
            api->scene.struct_size == sizeof(api->scene) &&
            api->panel.struct_size == sizeof(api->panel) &&
            api->cache.struct_size == sizeof(api->cache),
        "every API module carries its complete size");

    api->core.log(api, "value=%d", 17);
    api->core.notify(api, "hello");
    CHECK(strcmp(fake.log, "value=17") == 0, "variadic logging is forwarded safely");
    CHECK(strcmp(fake.notification, "hello") == 0, "notification is forwarded");
    CHECK(api->core.screen(api) == 30, "screen is forwarded");
    CHECK(api->core.frame_ms(api) == 1234, "frame clock is forwarded");
    CHECK(api->core.frame_work_us(api) == 567, "work clock is forwarded");
    CHECK(
        api->core.lane(api, &lane) && lane.game == TORIRS_PLUGIN_GAME_RS2 && lane.revision == 245,
        "lane snapshot is forwarded");
    CHECK(api->core.capability(api, "touch"), "new capability semantics use the host hook");
    CHECK(!api->core.capability(api, "vr"), "missing capability is false");

    CHECK(api->config.has(api, "known"), "config presence is forwarded");
    CHECK(!api->config.has(api, "missing"), "missing config remains ordinary absence");
    CHECK(api->config.get_bool(api, "known", &boolean) && boolean, "bool config copies out");
    CHECK(api->config.get_int(api, "known", &integer) && integer == 42, "int config copies out");
    CHECK(
        api->config.get_color(api, "known", &color) && color == 0x123456,
        "color config copies out");
    CHECK(
        api->config.get_string(api, "known", &string) && strcmp(string, "value") == 0,
        "string config copies out");
    integer = 99;
    CHECK(
        !api->config.get_int(api, "missing", &integer) && integer == 99,
        "a missing config leaves output untouched");
    CHECK(
        api->config.set(api, "known", "next") == TORIRS_RESULT_OK &&
            strcmp(fake.config_set, "known=next") == 0,
        "config writes translate to OK");

    CHECK(api->world.local_player(api, &player), "local-player presence translates to bool");
    CHECK(api->world.npc_next(api, -1, &npc) == 4, "npc iterator is unchanged");
    CHECK(api->world.npc_by_slot(api, 7, &npc), "npc lookup translates to bool");
    CHECK(api->world.player_next(api, -1, &player) == 5, "player iterator is unchanged");
    CHECK(api->world.item_next(api, -1, &item) == 6, "ground obj becomes item vocabulary");
    CHECK(api->world.scenery_next(api, -1, &scenery) == 8, "loc becomes scenery vocabulary");

    CHECK(api->input.key_held(api, 42), "key state translates to bool");
    CHECK(api->input.pointer(api, &x, &y) && x == 11 && y == 22, "pointer copies out");
    CHECK(
        api->input.hover_tile(api, &x, &z, &integer) && x == 3200 && z == 3201 && integer == 2,
        "hovered tile copies out");
    CHECK(
        api->input.hover_entity(api, &hover) && hover.element_id == 99,
        "hovered entity copies out");
    api->input.text_input(api, true);
    CHECK(fake.text_input == 1, "text input bool translates to legacy integer");

    ref = api->ui.ref(api, "frame.minimap");
    CHECK(ref.value == 91 && strcmp(fake.hook_name, "frame.minimap") == 0, "ui.ref uses hook");
    CHECK(
        api->ui.info(api, ref, &ui_info) && ui_info.struct_size == sizeof(ui_info) &&
            ui_info.bounds.width == 3 && ui_info.clip == TORIRS_UI_CLIP_PARENT &&
            ui_info.state_images[TORIRS_UI_VISUAL_HOVER].value == 22 &&
            strcmp(ui_info.label, "Rich") == 0 && ui_info.label_x == 5 &&
            ui_info.hit_rect.width == 5 && ui_info.action_count == 2 &&
            strcmp(ui_info.actions[1], "inspect") == 0,
        "ui.info preserves the hook's complete pointer-free facet snapshot");
    CHECK(
        api->ui.invoke(api, ref, "activate") && strcmp(fake.hook_name, "activate") == 0,
        "ui.invoke uses named-action hook");
    CHECK(
        api->ui.contribution_info(
            api, "frame.minimap", TORIRS_UI_FACET_APPEARANCE, &contribution_info) &&
            contribution_info.active_facets == TORIRS_UI_FACET_APPEARANCE,
        "contribution diagnostics use hook");
    adapter->hooks.ui_ref = NULL;
    CHECK(
        api->ui.ref(api, "report_button").value == 0,
        "without a host hook UI does not fake semantics through legacy roles");
    adapter->hooks.ui_ref = hook_ui_ref;
}

static void
test_placement_and_frame(struct ToriRS_ApiV2* api)
{
    struct ToriRS_PlacementAreaRef area;
    struct ToriRS_Rect rect;
    struct ToriRS_FrameOfferInfo offer;
    struct ToriRS_FrameSelection selection;
    int calls;

    CHECK(api->placement.revision(api) == 77, "placement revision is forwarded");
    area = api->placement.area(api, TORIRS_AREA_OVERLAY_SAFE);
    CHECK(area.value == TORIRS_AREA_OVERLAY_SAFE + 1, "area token is non-zero and reversible");
    CHECK(api->placement.area(api, 99).value == 0, "invalid area has no token");
    CHECK(
        api->placement.primary(api, area, &rect) && rect.x == 20 && rect.y == 30 &&
            rect.width == 40 && rect.height == 50,
        "primary chooses the largest exact fragment");
    CHECK(
        api->placement.rect_next(api, area, -1, &rect) == 0 && rect.width == 10,
        "fragment iteration translates rectangle spelling");
    CHECK(
        api->placement.place(
            api, TORIRS_AREA_OVERLAY_SAFE, TORIRS_ANCHOR_BOTTOM_RIGHT, 12, 13, 4, &rect) &&
            rect.x == 4 && rect.width == 12 &&
            fake.placement_anchor == TORIRS_PLACEMENT_ANCHOR_BOTTOM_RIGHT,
        "purpose placement preserves area, anchor, size, and margin");
    CHECK(
        api->placement.contains(api, area, (struct ToriRS_Rect){ 1, 2, 3, 4 }),
        "containment translates rectangle spelling");
    CHECK(
        api->placement.reserve(api, "dock", TORIRS_AREA_OVERLAY_SAFE, TORIRS_EDGE_LEFT, 25) ==
                TORIRS_RESERVE_OK &&
            strcmp(fake.reservation, "dock") == 0 && fake.last_a == TORIRS_EDGE_LEFT &&
            fake.last_b == 25,
        "named reservations forward without collapsing to legacy safe slots");
    CHECK(
        api->placement.reserve(api, "dock", TORIRS_AREA_OVERLAY_SAFE, TORIRS_EDGE_LEFT, 999) ==
            TORIRS_RESERVE_BUDGET,
        "a valid refused reservation translates to budget");
    CHECK(
        api->placement.reserve(api, "Bad_Name", TORIRS_AREA_OVERLAY_SAFE, TORIRS_EDGE_LEFT, 2) ==
            TORIRS_RESERVE_INVALID,
        "invalid reservation names are distinguished from budget");
    CHECK(
        api->placement.reservation_rect(api, "dock", &rect) && rect.x == 90 && rect.width == 10,
        "assigned reservation rectangles copy out");

    CHECK(
        api->frame.offer_next(api, -1, &offer) == 0 && offer.struct_size == sizeof(offer) &&
            offer.canvas == TORIRS_FRAME_CANVAS_WINDOW && offer.width == 0 &&
            offer.min_width == 765,
        "window offer dimensions translate into minimums");
    api->frame.selection(api, &selection);
    CHECK(
        selection.struct_size == sizeof(selection) &&
            strcmp(selection.requested_id, "frames/window") == 0 &&
            strcmp(selection.active_id, "core/native") == 0 &&
            selection.status == TORIRS_FRAME_STATUS_FALLBACK && selection.revision == 12,
        "frame selection field names and status translate");
    CHECK(api->frame.select(api, "frames/window") == TORIRS_RESULT_OK, "valid frame select is OK");
    CHECK(
        api->frame.select(api, "bad") == TORIRS_RESULT_INVALID, "refused frame select is invalid");
    calls = fake.calls;
    api->frame.invalidate(api);
    CHECK(fake.calls == calls + 1, "frame invalidation forwards explicitly");
}

static void
test_assets_scene_and_cache(struct ToriRS_ApiV2* api)
{
    struct ToriRS_ImageRef image;
    struct ToriRS_ModelRef model;
    struct ToriRS_MeshRef mesh;
    struct ToriRS_SceneInstanceRef instance;
    struct ToriRS_Rect rect;
    void const* bytes = NULL;
    size_t size = 0;
    char path[64];
    int width;
    int height;

    fake.asset_resident = 0;
    CHECK(
        api->assets.request(api, "data.bin") == TORIRS_ASSET_PENDING,
        "a queued legacy asset is pending");
    CHECK(
        api->assets.request(api, "../data") == TORIRS_ASSET_INVALID,
        "an invalid asset name is reported distinctly");
    fake.asset_resident = 1;
    CHECK(api->assets.request(api, "data.bin") == TORIRS_ASSET_READY, "resident bytes are ready");
    CHECK(
        api->assets.bytes(api, "data.bin", &bytes, &size) && bytes == fake.bytes && size == 4,
        "resident bytes and size copy out");
    CHECK(
        api->assets.save(api, "save.bin", fake.bytes, sizeof(fake.bytes)) == TORIRS_RESULT_OK &&
            fake.last_a == 4,
        "asset save size_t translates safely");
    CHECK(
        api->assets.save(api, "fail.bin", fake.bytes, 4) == TORIRS_RESULT_ERROR,
        "failed legacy save translates to error");
    api->assets.release(api, "save.bin");
    CHECK(strcmp(fake.config_set, "save.bin") == 0, "asset release forwards");

    fake.image_ready = 0;
    CHECK(
        api->assets.image(api, "orb.png", &image) == TORIRS_ASSET_PENDING && image.value == 14,
        "image handle is usable while pixels remain pending");
    fake.image_ready = 1;
    CHECK(
        api->assets.image(api, "orb.png", &image) == TORIRS_ASSET_READY &&
            api->assets.image_size(api, image, &width, &height) && width == 32 && height == 24,
        "resident image state and dimensions translate");
    CHECK(
        api->assets.image(api, "full.png", &image) == TORIRS_ASSET_BUDGET,
        "negative legacy image handle translates to budget");
    image.value = 14;
    api->assets.image_release(api, image);
    CHECK(fake.last_a == 14, "image release forwards typed handle");

    fake.asset_resident = 0;
    CHECK(
        api->assets.model(api, "beam.model", &model) == TORIRS_ASSET_PENDING && model.value == 18,
        "model with pending source bytes is pending");
    fake.asset_resident = 1;
    CHECK(
        api->assets.model(api, "beam.model", &model) == TORIRS_ASSET_READY,
        "resident model is ready");
    api->assets.model_release(api, model);
    CHECK(fake.last_a == 18, "new model release uses explicit host hook");
    CHECK(
        api->assets.screenshot(api, "shots", "now.png", path, sizeof(path)) == TORIRS_RESULT_OK &&
            strcmp(path, "shots/now.png") == 0 && fake.last_a == (int)sizeof(path),
        "screenshot path and buffer size translate");

    fake.last_a = 0;
    CHECK(
        api->scene.mesh_create(api, &mesh) == TORIRS_RESULT_OK && mesh.value == 21,
        "mesh allocation returns typed handle");
    CHECK(
        api->scene.mesh_vertex(api, mesh, 1, 2, 3) == TORIRS_RESULT_OK && fake.last_a == 21 &&
            fake.last_b == 1,
        "mesh vertex forwards");
    CHECK(
        api->scene.mesh_vertex(api, mesh, 999, 2, 3) == TORIRS_RESULT_BUDGET,
        "exhausted vertex append translates to budget");
    CHECK(
        api->scene.mesh_face(api, mesh, 0, 1, 2, 333, 44) == TORIRS_RESULT_OK && fake.last_b == 3 &&
            fake.last_d == 44,
        "mesh face forwards");
    api->scene.mesh_destroy(api, mesh);
    CHECK(fake.last_a == 21, "mesh destroy forwards typed handle");

    fake.last_a = 0;
    CHECK(
        api->scene.instance_create(api, &instance) == TORIRS_RESULT_OK && instance.value == 31,
        "scene instance allocation returns typed handle");
    model.value = 18;
    CHECK(
        api->scene.instance_model(api, instance, model) == TORIRS_RESULT_OK &&
            fake.last_b == TORIRS_PLUGIN_MODEL_ASSET && fake.last_c == 18,
        "scene model uses the shipped-model legacy source");
    CHECK(
        api->scene.instance_position(api, instance, 3200, 3201, 2, 70, 1024) == TORIRS_RESULT_OK &&
            fake.last_b == 6401 && fake.last_c == 2 && fake.last_e == 1024,
        "scene position forwards all coordinates");
    api->scene.instance_active(api, instance, true);
    CHECK(fake.last_a == 31 && fake.last_b == 1, "scene active bool translates");
    api->scene.instance_destroy(api, instance);
    CHECK(fake.last_a == 31, "scene destroy forwards typed handle");

    CHECK(api->cache.frame_root(api) == 548, "cache frame root stays in escape hatch");
    CHECK(api->cache.varbit(api, 10) == 11, "cache varbit forwards");
    CHECK(api->cache.varp(api, 10) == 12, "cache varp forwards");
    CHECK(
        api->cache.component_rect(api, 123, &rect) && rect.x == 1 && rect.height == 4,
        "cache component rectangle translates");
    CHECK(api->cache.invoke(api, 123, 4) && fake.last_b == 4, "numeric cache invoke forwards");
    CHECK(!api->cache.invoke(api, 123, 11), "numeric op range is validated before legacy call");
}

static void
test_callback_scoped_builders(struct ToriRS_PluginV2Adapter* adapter)
{
    struct ToriRS_ApiV2* api = &adapter->api;
    struct ToriRS_PluginV2DrawScope draw_scope;
    struct ToriRS_PluginV2FrameScope frame_scope;
    struct ToriRS_PluginV2PanelScope panel_scope;
    struct ToriRS_DrawBuilder draw;
    struct ToriRS_FrameBuilder frame;
    struct ToriRS_PanelBuilder panel;
    struct ToriRS_FrameSkin skin = { .struct_size = sizeof(skin), .image = { 7 }, .mask = { 8 } };
    struct ToriRS_FrameScrollbar scrollbar = {
        .struct_size = sizeof(scrollbar),
        .up = { 1 },
        .down = { 2 },
        .track = { 3 },
        .thumb = { 4 },
        .split_thumb = true,
        .thumb_top = { 14 },
        .thumb_middle = { 15 },
        .thumb_bottom = { 16 },
    };
    struct ToriRS_FrameSurfaceOverlay overlay = {
        .struct_size = sizeof(overlay),
        .image = { 19 },
        .x = 20,
        .y = 21,
        .alpha = 200,
    };
    struct ToriRS_UiNode node = {
        .struct_size = sizeof(node),
        .bounds = { 6, 7, 8, 9 },
        .clip = TORIRS_UI_CLIP_PARENT,
        .state_image_mask = 1u << TORIRS_UI_VISUAL_HOVER,
        .state_images = { [TORIRS_UI_VISUAL_HOVER] = { 23 } },
        .label = "Node",
        .label_x = 4,
        .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM,
        .hit_rect = { 5, 6, 10, 11 },
        .action_count = 2,
        .actions = { "activate", "inspect" },
    };
    struct ToriRS_SelectOption options[] = {
        { .struct_size = sizeof(struct ToriRS_SelectOption),
          .value = "a",
          .label = "Alpha",
          .enabled = true },
        { .struct_size = sizeof(struct ToriRS_SelectOption),
          .value = "b",
          .label = "Beta",
          .enabled = true },
    };
    int hook_calls;

    ToriRS_PluginV2Adapter_DrawBegin(adapter, &fake, &draw_scope, &draw);
    CHECK(draw.struct_size == sizeof(draw), "draw builder publishes its size");
    draw.rect(&draw, (struct ToriRS_Rect){ 1, 2, 3, 4 }, 0x112233, 77);
    CHECK(fake.last_a == 1 && fake.last_d == 4 && fake.last_f == 77, "rect builder forwards");
    draw.line(&draw, 5, 6, 7, 8, 0x445566, 255);
    CHECK(fake.last_a == 5 && fake.last_d == 8, "line builder forwards");
    draw.text(&draw, 9, 10, "text", 0x778899);
    CHECK(strcmp(fake.notification, "text") == 0 && fake.last_a == 9, "text builder forwards");
    draw.image(&draw, (struct ToriRS_ImageRef){ 14 }, 11, 12, 200);
    CHECK(
        fake.last_a == 14 && fake.last_b == 11 && fake.last_e == 55,
        "image alpha becomes legacy transparency");
    CHECK(
        draw.world_tile(&draw, 3200, 3201, 2, 0x010203, 0xaabbcc, 66) == TORIRS_RESULT_OK &&
            fake.last_d == 0xaabbcc && fake.last_e == 0x010203,
        "world tile fill and outline map in the right order");
    CHECK(
        draw.world_hull(&draw, 44, 0xabcdef, 88, TORIRS_PLUGIN_HULL_MESH) == TORIRS_RESULT_OK &&
            fake.last_a == 44 && fake.last_d == TORIRS_PLUGIN_HULL_MESH,
        "world hull forwards");
    CHECK(
        draw.action_region(&draw, (struct ToriRS_Rect){ 2, 3, 40, 20 }, "activate") ==
                TORIRS_RESULT_OK &&
            strcmp(fake.notification, "activate") == 0 && fake.last_e == 1 && fake.last_f != 0,
        "semantic action region becomes one stable legacy route");
    ToriRS_PluginV2Adapter_DrawEnd(&draw_scope, &draw);
    CHECK(!draw_scope.active && !draw.implementation, "draw scope is explicitly ended");

    ToriRS_PluginV2Adapter_FrameBegin(adapter, &frame_scope, &frame);
    CHECK(frame.struct_size == sizeof(frame), "frame builder publishes its size");
    frame.surface(&frame, TORIRS_SURFACE_VIEWPORT, (struct ToriRS_Rect){ 0, 0, 512, 334 });
    frame.surface(&frame, TORIRS_SURFACE_SIDEBAR, (struct ToriRS_Rect){ 1, 2, 3, 4 });
    CHECK(
        fake.last_a == TORIRS_PLUGIN_SLOT_SIDEBAR && fake.last_d == 3,
        "surface vocabulary maps explicitly");
    frame.surface_member(
        &frame, TORIRS_SURFACE_CHAT_BUTTONS, 3, (struct ToriRS_Rect){ 5, 6, 7, 8 });
    CHECK(
        fake.last_a == TORIRS_PLUGIN_SLOT_CHAT_BUTTONS && fake.last_b == 3 && fake.last_d == 7,
        "surface member maps role number and geometry");
    frame.skin(&frame, TORIRS_SURFACE_MINIMAP, &skin);
    CHECK(
        fake.last_a == TORIRS_PLUGIN_SLOT_MINIMAP && fake.last_b == 7 && fake.last_c == 8,
        "surface skin forwards handles");
    frame.surface(&frame, TORIRS_SURFACE_COMPASS, (struct ToriRS_Rect){ 20, 21, 33, 33 });
    frame.skin(&frame, TORIRS_SURFACE_COMPASS, &skin);
    CHECK(fake.last_a == TORIRS_PLUGIN_SLOT_COMPASS, "compass is a first-class live surface");
    frame.surface(&frame, TORIRS_SURFACE_ORBS, (struct ToriRS_Rect){ 9, 10, 11, 12 });
    CHECK(fake.last_a == TORIRS_PLUGIN_SLOT_ORBS, "orb pack is a first-class live surface");
    frame.surface_overlay(&frame, TORIRS_SURFACE_COMPASS, &overlay);
    CHECK(
        fake.last_a == TORIRS_PLUGIN_SLOT_COMPASS && fake.last_b == 19 && fake.last_c == 20 &&
            fake.last_d == 21 && fake.last_e == 55,
        "surface overlay forwards retained art and converts alpha to transparency");
    frame.scrollbar(&frame, &scrollbar);
    CHECK(
        fake.last_a == 3 && fake.last_b == 14 && fake.last_c == 15 && fake.last_d == 16 &&
            fake.last_e == 1 && fake.last_f == 2,
        "split v2 scrollbar faithfully maps all six legacy pieces");
    scrollbar.struct_size = TORIRS_FRAME_SCROLLBAR_LEGACY_SIZE;
    frame.scrollbar(&frame, &scrollbar);
    CHECK(
        fake.last_b == 4 && fake.last_c == 4 && fake.last_d == 4,
        "the original one-piece scrollbar prefix still expands compatibly");
    scrollbar.struct_size = sizeof(scrollbar);
    hook_calls = fake.hook_calls;
    frame.ui_node(&frame, "frame.minimap.housing", &node);
    CHECK(
        fake.hook_calls == hook_calls + 1 && strcmp(fake.hook_name, "frame.minimap.housing") == 0 &&
            fake.last_a == 6 && fake.last_b == TORIRS_UI_CLIP_PARENT && fake.last_c == 23 &&
            fake.last_d == 4 && fake.last_e == 10 && fake.last_f == 2,
        "canonical frame nodes pass the complete rich descriptor to the host hook");
    frame.reason(&frame, "assets pending");
    CHECK(
        strcmp(ToriRS_PluginV2Adapter_FrameReason(&frame_scope), "assets pending") == 0 &&
            strcmp(fake.hook_name, "assets pending") == 0,
        "frame reason is retained and offered to host hook");
    CHECK(
        ToriRS_PluginV2Adapter_FrameValid(&frame_scope),
        "a valid frame transaction has one viewport and no duplicate declarations");
    ToriRS_PluginV2Adapter_FrameEnd(&frame_scope, &frame);
    CHECK(!frame_scope.active && !frame.implementation, "frame scope is explicitly ended");

    ToriRS_PluginV2Adapter_FrameBegin(adapter, &frame_scope, &frame);
    frame.surface(&frame, TORIRS_SURFACE_VIEWPORT, (struct ToriRS_Rect){ 0, 0, 10, 10 });
    frame.surface(&frame, TORIRS_SURFACE_VIEWPORT, (struct ToriRS_Rect){ 0, 0, 20, 20 });
    CHECK(
        !ToriRS_PluginV2Adapter_FrameValid(&frame_scope),
        "duplicate surface declarations invalidate the scratch transaction");
    ToriRS_PluginV2Adapter_FrameEnd(&frame_scope, &frame);

    ToriRS_PluginV2Adapter_FrameBegin(adapter, &frame_scope, &frame);
    frame.surface(&frame, TORIRS_SURFACE_CHAT, (struct ToriRS_Rect){ 0, 0, 0, 10 });
    CHECK(
        !ToriRS_PluginV2Adapter_FrameValid(&frame_scope),
        "invalid rectangles and a missing viewport invalidate the transaction");
    ToriRS_PluginV2Adapter_FrameEnd(&frame_scope, &frame);

    fake.panel_count = 0;
    ToriRS_PluginV2Adapter_PanelBegin(adapter, &panel_scope, &panel);
    CHECK(panel.struct_size == sizeof(panel), "panel builder publishes its size");
    panel.heading(&panel, "Heading");
    panel.paragraph(&panel, "Words");
    panel.toggle(&panel, "enabled", "Enabled", true);
    CHECK(
        fake.panel_count == 3 && fake.panel_kind[0] == TORIRS_PLUGIN_W_SECTION &&
            fake.panel_kind[1] == TORIRS_PLUGIN_W_PARAGRAPH &&
            fake.panel_kind[2] == TORIRS_PLUGIN_W_TOGGLE && fake.panel_value == 1,
        "semantic panel rows map to legacy widget kinds");
    hook_calls = fake.hook_calls;
    panel.select(&panel, "layout", "Layout", "b", options, 2);
    CHECK(
        fake.hook_calls == hook_calls + 1 && strcmp(fake.hook_name, "layout=b") == 0 &&
            fake.last_a == 2,
        "structured select uses lossless host hook");
    adapter->hooks.panel_select = NULL;
    panel.select(&panel, "legacy", "Legacy", "b", options, 2);
    CHECK(
        strcmp(fake.panel_options, "Alpha|Beta") == 0 && fake.panel_value == 1,
        "plain options have a legacy compatibility path");
    adapter->hooks.panel_select = hook_panel_select;
    panel.button(&panel, "reset", "Reset", false);
    CHECK(
        fake.panel_kind[fake.panel_count - 1] == TORIRS_PLUGIN_W_BUTTON && fake.panel_value == 0,
        "button kind and enabled state forward");
    panel.custom(&panel, "chart", 144);
    CHECK(
        fake.panel_kind[fake.panel_count - 1] == TORIRS_PLUGIN_W_CUSTOM && fake.last_a == 144,
        "custom well and height forward");
    ToriRS_PluginV2Adapter_PanelEnd(&panel_scope, &panel);
    CHECK(!panel_scope.active && !panel.implementation, "panel scope is explicitly ended");

    CHECK(
        api->panel.request(
            api,
            &(struct ToriRS_PluginPanelDesc){ .icon_asset = "icon.png", .preferred_width = 333 }) ==
                TORIRS_RESULT_OK &&
            fake.last_a == 333,
        "panel registration forwards through module");
    hook_calls = fake.calls;
    api->panel.invalidate(api);
    CHECK(fake.calls == hook_calls + 1, "panel invalidation maps to retained clear/rebuild");
    api->panel.attention(api, true);
    CHECK(fake.last_a == 1, "panel attention bool forwards");
}

int
main(void)
{
    struct ToriRS_PluginV2Adapter adapter;
    struct ToriRS_PluginApi legacy = legacy_api();
    struct ToriRS_PluginV2AdapterHooks adapter_hooks = hooks();

    memset(&fake, 0, sizeof(fake));
    fake.bytes[0] = 1;
    test_construction_and_basic_modules(&adapter, &legacy, &adapter_hooks);
    test_placement_and_frame(&adapter.api);
    test_assets_scene_and_cache(&adapter.api);
    test_callback_scoped_builders(&adapter);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
