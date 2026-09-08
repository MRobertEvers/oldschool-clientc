/* Minimap orbs on owned image controls, against a fake widget/asset API.
 * Two lanes: one without native orbs (controls beside the minimap, clamped
 * out of the map disc) and one whose cache draws interface 160 (controls
 * cover the native roots and press the native button through checked
 * actions). */
#include "plugin/torirs_plugin_api.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS;
void minimap_orbs_test_binding(struct ToriRS_Api*, void*, struct ToriRS_WidgetEvent const*);

#define CHECK(x) do { if( !(x) ) { fprintf(stderr, "minimap v2: %s (line %d)\n", #x, __LINE__); exit(1); } } while( 0 )

/* --- fake tree: a few widgets by id ------------------------------------ */
enum { W_MINIMAP = 1, W_MAP_PARENT = 2, W_ORB_HP = 3, W_ORB_PRAYER = 4, W_ORB_RUN = 5, W_ORB_SPEC = 6,
       W_BTN_PRAYER = 7, W_BTN_RUN = 8, W_BTN_SPEC = 9, W_FIRST_OWNED = 20 };
struct FakeWidget { int parent; int x, y, w, h; int alive; char key[32]; int image; int img_w, img_h; char op_label[64];
    ToriRS_WidgetListener op; void* op_user; int opacity; };
static struct FakeWidget widgets[64];
static int next_owned = W_FIRST_OWNED;
static int native_orbs;        /* the lane has interface 160 */
static int cutscene_status;    /* varbit cutscene_status */
static int button_available[3]; /* prayer, run, spec: the lane declares the role */
/* The client has hidden the button (rev-239 does exactly this to the special
 * attack button when the wielded weapon has no special): the node is still
 * there, so `find` answers, and the live action query is blocked. */
static int button_hidden[3];
static int run_mode;
static int spec_alias;         /* the profile's legacy [iface:orb_spec_button] */
static enum ToriRS_ContractResult invoke_result = TORIRS_CONTRACT_OK;
static int invoked_component = -1, invoked_operation = -1, invoked_actions;
static struct ToriRS_WidgetRef last_action_widget;
static int composes, sets_image, positions, removes, released, logs, revalidates, notifies;
static uint32_t last_composed[57 * 34];
static char last_log[256], spec_value_log[256], last_notify[256];
static char watched[8][32];
static int watch_count;

static struct ToriRS_WidgetRef ref_of(int id) { return (struct ToriRS_WidgetRef){{ 77, (uint64_t)id, 1 }}; }
static int id_of(struct ToriRS_WidgetRef r) { return r.opaque[0] == 77 && r.opaque[2] == 1 ? (int)r.opaque[1] : -1; }

static enum ToriRS_ContractResult f_find(void* c, char const* role, struct ToriRS_WidgetRef* out)
{
    (void)c;
    if( strcmp(role, "minimap") == 0 ) { *out = ref_of(W_MINIMAP); return TORIRS_CONTRACT_OK; }
    if( native_orbs )
    {
        if( strcmp(role, "action_frame_orb_prayer_activate") == 0 && button_available[0] ) { *out = ref_of(W_BTN_PRAYER); return TORIRS_CONTRACT_OK; }
        if( (strcmp(role, "action_frame_orb_run_enable") == 0 || strcmp(role, "action_frame_orb_run_disable") == 0) && button_available[1] ) { *out = ref_of(W_BTN_RUN); return TORIRS_CONTRACT_OK; }
        if( strcmp(role, "action_frame_orb_special_activate") == 0 && button_available[2] ) { *out = ref_of(W_BTN_SPEC); return TORIRS_CONTRACT_OK; }
    }
    return TORIRS_CONTRACT_UNAVAILABLE;
}
static enum ToriRS_ContractResult f_actions(void* c, struct ToriRS_WidgetRef w, struct ToriRS_WidgetAction* out, size_t cap, size_t* count)
{
    (void)c; int id = id_of(w);
    /* The bridge answers NATIVE_BLOCKED for a display-hidden node, before it
     * has any options to report. */
    if( id >= W_BTN_PRAYER && id <= W_BTN_SPEC && button_hidden[id - W_BTN_PRAYER] ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
    *count = id >= W_BTN_PRAYER && id <= W_BTN_SPEC ? 1 : 0;
    if( *count && cap ) out[0] = (struct ToriRS_WidgetAction){ .ref = { w, 1, 9 }, .label = "Activate" };
    return *count > cap ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult f_invoke(void* c, struct ToriRS_WidgetActionRef a)
{ (void)c; invoked_actions++; last_action_widget = a.widget; return invoke_result; }
static enum ToriRS_ContractResult f_position(void* c, struct ToriRS_WidgetRef w, struct ToriRS_WidgetBounds* out)
{
    (void)c; int id = id_of(w); if( id < 0 || !widgets[id].alive ) return TORIRS_CONTRACT_STALE_REFERENCE;
    *out = (struct ToriRS_WidgetBounds){ widgets[id].x, widgets[id].y, widgets[id].w, widgets[id].h }; return TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult f_parent(void* c, struct ToriRS_WidgetRef w, struct ToriRS_WidgetRef* out)
{ (void)c; int id = id_of(w); if( id < 0 || !widgets[id].alive ) return TORIRS_CONTRACT_STALE_REFERENCE; *out = ref_of(widgets[id].parent); return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult f_create_image(void* c, struct ToriRS_WidgetRef parent, char const* key, struct ToriRS_WidgetRef* out)
{
    (void)c; int pid = id_of(parent); if( pid < 0 || !widgets[pid].alive ) return TORIRS_CONTRACT_STALE_REFERENCE;
    for( int i = W_FIRST_OWNED; i < next_owned; i++ )
        if( widgets[i].alive && widgets[i].parent == pid && strcmp(widgets[i].key, key) == 0 ) { *out = ref_of(i); return TORIRS_CONTRACT_OK; }
    int id = next_owned++; memset(&widgets[id], 0, sizeof(widgets[id])); widgets[id].alive = 1; widgets[id].parent = pid;
    snprintf(widgets[id].key, sizeof(widgets[id].key), "%s", key); *out = ref_of(id); return TORIRS_CONTRACT_OK;
}
static enum ToriRS_ContractResult f_set_image(void* c, struct ToriRS_WidgetRef w, struct ToriRS_ImageRef img, int iw, int ih)
{ (void)c; int id = id_of(w); if( id < W_FIRST_OWNED || !widgets[id].alive ) return TORIRS_CONTRACT_NATIVE_BLOCKED; widgets[id].image = img.value; widgets[id].img_w = iw; widgets[id].img_h = ih; widgets[id].w = iw; widgets[id].h = ih; sets_image++; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult f_set_position(void* c, struct ToriRS_WidgetRef w, int32_t x, int32_t y)
{ (void)c; int id = id_of(w); if( id < W_FIRST_OWNED ) return TORIRS_CONTRACT_NATIVE_BLOCKED; widgets[id].x = x; widgets[id].y = y; positions++; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult f_set_on_op(void* c, struct ToriRS_WidgetRef w, char const* label, ToriRS_WidgetListener l, void* user)
{ (void)c; int id = id_of(w); if( id < W_FIRST_OWNED ) return TORIRS_CONTRACT_NATIVE_BLOCKED; snprintf(widgets[id].op_label, sizeof(widgets[id].op_label), "%s", label ? label : ""); widgets[id].op = l; widgets[id].op_user = user; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult f_revalidate(void* c, struct ToriRS_WidgetRef w) { (void)c; (void)w; revalidates++; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult f_bounds(void* c, struct ToriRS_WidgetRef w, struct ToriRS_WidgetBounds* out)
{ (void)c; int id = id_of(w); int px = widgets[widgets[id].parent].x, py = widgets[widgets[id].parent].y; *out = (struct ToriRS_WidgetBounds){ px + widgets[id].x, py + widgets[id].y, widgets[id].w, widgets[id].h }; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult f_remove(void* c, struct ToriRS_WidgetRef w) { (void)c; int id = id_of(w); if( id < W_FIRST_OWNED ) return TORIRS_CONTRACT_NATIVE_BLOCKED; widgets[id].alive = 0; removes++; return TORIRS_CONTRACT_OK; }
static enum ToriRS_ContractResult f_watch(void* c, char const* role, ToriRS_WidgetListener l, void* user)
{ (void)c; (void)l; (void)user; if( watch_count < 8 ) snprintf(watched[watch_count++], 32, "%s", role); return TORIRS_CONTRACT_OK; }

/* --- fake assets: every image is a 4x4 (frame 57x34) opaque block --------- */
static int next_image = 1;
static char image_name[64][32];
static enum ToriRS_AssetState a_image(struct ToriRS_Api* api, char const* name, struct ToriRS_ImageRef* out)
{ (void)api; out->value = next_image; snprintf(image_name[next_image], 32, "%s", name); next_image++; return TORIRS_ASSET_READY; }
static bool a_image_size(struct ToriRS_Api* api, struct ToriRS_ImageRef img, int* w, int* h)
{ (void)api; if( !img.value ) return false; if( strcmp(image_name[img.value], "frame.png") == 0 || strcmp(image_name[img.value], "frame_over.png") == 0 ) { *w = 57; *h = 34; } else if( strcmp(image_name[img.value], "digits.png") == 0 ) { *w = 60; *h = 30; } else { *w = 26; *h = 26; } return true; }
static bool a_image_pixels(struct ToriRS_Api* api, struct ToriRS_ImageRef img, uint32_t* out, size_t cap, size_t* count)
{
    int w, h; if( !a_image_size(api, img, &w, &h) || cap < (size_t)(w * h) ) return false;
    /* Icons are transparent glyphs in the real art; a fake that painted them
     * opaque would hide the meter this test reads back. */
    uint32_t colour = strncmp(image_name[img.value], "icon_", 5) == 0 ? 0x00000000u
        : strcmp(image_name[img.value], "fill_empty.png") == 0 ? 0xff000000u
        : strcmp(image_name[img.value], "fill_red.png") == 0 ? 0xffff0000u
        : strcmp(image_name[img.value], "frame.png") == 0 ? 0xff808080u : 0xff00ff00u;
    for( int i = 0; i < w * h; i++ ) out[i] = colour; *count = (size_t)(w * h); return true;
}
static enum ToriRS_AssetState a_compose(struct ToriRS_Api* api, char const* name, int w, int h, uint32_t const* argb, struct ToriRS_ImageRef* out)
{ (void)api; CHECK(w == 57 && h == 34); if( strncmp(name, "orb_hitpoints", 13) == 0 ) memcpy(last_composed, argb, sizeof(last_composed)); composes++; out->value = 100; return TORIRS_ASSET_READY; }
static void a_release(struct ToriRS_Api* api, struct ToriRS_ImageRef img) { (void)api; if( img.value ) released++; }
static enum ToriRS_AssetState a_request(struct ToriRS_Api* api, char const* name) { (void)api; (void)name; return TORIRS_ASSET_MISSING; }
static bool a_bytes(struct ToriRS_Api* api, char const* name, void const** d, size_t* s) { (void)api; (void)name; (void)d; (void)s; return false; }
static void a_asset_release(struct ToriRS_Api* api, char const* name) { (void)api; (void)name; }

/* --- fake config / cache / game ------------------------------------------ */
static int replace_native = 1;
static int show_prayer = 1;
static bool cfg_bool(struct ToriRS_Api* api, char const* key, bool* out)
{ (void)api; *out = strcmp(key, "replace_native") == 0 ? replace_native != 0
    : strcmp(key, "show_prayer") == 0 ? show_prayer != 0 : strncmp(key, "show_", 5) == 0; return true; }
static bool cfg_int(struct ToriRS_Api* api, char const* key, int* out)
{ (void)api; if( !strcmp(key, "offset_x") ) *out = 6; else if( !strcmp(key, "offset_y") ) *out = -3; else if( !strcmp(key, "run_varp") || !strcmp(key, "spec_varp") || !strcmp(key, "spec_armed_varp") ) *out = -1; else if( !strcmp(key, "spec_max") ) *out = 1000; else *out = 0; return true; }
static bool cfg_string(struct ToriRS_Api* api, char const* key, char const** out) { (void)api; (void)key; *out = ""; return true; }
static bool named_id(struct ToriRS_Api* api, char const* kind, char const* name, int* out)
{ (void)api; if( !strcmp(kind, "varp") ) { if( !strcmp(name, "run_mode") ) { *out = 173; return true; }
      if( !strcmp(name, "special_attack_energy") ) { *out = 300; return true; } return false; }
  if( !strcmp(kind, "varbit") && !strcmp(name, "cutscene_status") ) { *out = 4606; return true; }
  if( !strcmp(kind, "iface") && !native_orbs && !strcmp(name, "orb_run_on") ) { *out = 153; return true; }
  /* Both shipped profiles carry this legacy alias for the special orb; on the
   * interface-160 lane it names a button the client may have hidden. */
  if( !strcmp(kind, "iface") && spec_alias && !strcmp(name, "orb_spec_button") ) { *out = native_orbs ? (160 << 16) | 36 : 400; return true; } return false; }
static int cache_varbit(struct ToriRS_Api* api, int id) { (void)api; return id == 4606 ? cutscene_status : 0; }
static int cache_varp(struct ToriRS_Api* api, int id) { (void)api; return id == 300 ? 500 : id == 173 ? run_mode : 0; }
static bool cache_invoke(struct ToriRS_Api* api, int component, int operation) { (void)api; invoked_component = component; invoked_operation = operation; return true; }
static bool skill(struct ToriRS_Api* api, int index, struct ToriRS_SkillSnapshot* out) { (void)api; if( index != 3 && index != 5 ) return false; out->current_level = index == 3 ? 42 : 30; out->base_level = index == 3 ? 50 : 40; return true; }
static int run_energy(struct ToriRS_Api* api) { (void)api; return 75; }
static void notify(struct ToriRS_Api* api, char const* text) { (void)api; notifies++; snprintf(last_notify, sizeof(last_notify), "%s", text ? text : ""); }
static void log_line(struct ToriRS_Api* api, char const* format, ...)
{ (void)api; va_list ap; va_start(ap, format); vsnprintf(last_log, sizeof(last_log), format, ap); va_end(ap); logs++;
  if( strstr(last_log, "MINIMAP_ORBS_VALUE orb=orb_special") ) snprintf(spec_value_log, sizeof(spec_value_log), "%s", last_log); }

static int orb_covers_map(struct FakeWidget const* map, struct FakeWidget const* orb)
{
    long const cx = 2L * map->x + map->w, cy = 2L * map->y + map->h, r = map->w < map->h ? map->w : map->h;
    for( int y = orb->y; y < orb->y + orb->h; y++ ) for( int x = orb->x; x < orb->x + orb->w; x++ )
    { long dx = 2L * x + 1 - cx, dy = 2L * y + 1 - cy; if( dx * dx + dy * dy < r * r ) return 1; }
    return 0;
}
static int owned_under(int parent, char const* key)
{ for( int i = W_FIRST_OWNED; i < next_owned; i++ ) if( widgets[i].alive && widgets[i].parent == parent && !strcmp(widgets[i].key, key) ) return i; return -1; }
static int alive_owned(void) { int n = 0; for( int i = W_FIRST_OWNED; i < next_owned; i++ ) n += widgets[i].alive; return n; }
static void bind(struct ToriRS_PluginDef const* def, struct ToriRS_Api* api, void* state, char const* role, int id, int bound)
{ (void)def; struct ToriRS_WidgetEvent ev = { .type = bound ? TORIRS_WIDGET_BOUND : TORIRS_WIDGET_UNBOUND, .widget = ref_of(id), .role = role }; minimap_orbs_test_binding(api, state, &ev); }
/* The widget a role names, so a test can replay a publication in the order the
 * host would deliver it: watch-registration order. */
static int widget_for_role(char const* role)
{
    if( !strcmp(role, "minimap") ) return W_MINIMAP;
    if( !strcmp(role, "orb_hitpoints") ) return W_ORB_HP;
    if( !strcmp(role, "orb_prayer") ) return W_ORB_PRAYER;
    if( !strcmp(role, "orb_run") ) return W_ORB_RUN;
    if( !strcmp(role, "orb_spec") ) return W_ORB_SPEC;
    return -1;
}

int main(void)
{
    struct ToriRS_Api api; struct ToriRS_GameApi game; void* state;
    memset(&api, 0, sizeof(api)); memset(&game, 0, sizeof(game)); memset(widgets, 0, sizeof(widgets));
    api.core.notify = notify; api.core.log = log_line;
    api.config.get_bool = cfg_bool; api.config.get_int = cfg_int; api.config.get_string = cfg_string;
    api.widgets.find = f_find; api.widgets.actions = f_actions; api.widgets.invoke = f_invoke; api.widgets.position = f_position;
    api.widgets.parent = f_parent; api.widgets.create_image = f_create_image; api.widgets.set_image = f_set_image;
    api.widgets.set_position = f_set_position; api.widgets.set_on_op = f_set_on_op; api.widgets.revalidate = f_revalidate;
    api.widgets.bounds = f_bounds; api.widgets.remove = f_remove; api.widgets.watch = f_watch;
    api.assets.image = a_image; api.assets.image_size = a_image_size; api.assets.image_pixels = a_image_pixels;
    api.assets.image_compose = a_compose; api.assets.image_release = a_release; api.assets.request = a_request; api.assets.bytes = a_bytes; api.assets.release = a_asset_release;
    api.cache.named_id = named_id; api.cache.varp = cache_varp; api.cache.varbit = cache_varbit; api.cache.invoke = cache_invoke;
    game.skill = skill; game.run_energy = run_energy; api.game = &game;

    /* The minimap (parent-local 600,20 146x151 inside a 765x503 layer). */
    widgets[W_MAP_PARENT] = (struct FakeWidget){ .parent = 0, .x = 0, .y = 0, .w = 765, .h = 503, .alive = 1 };
    widgets[W_MINIMAP] = (struct FakeWidget){ .parent = W_MAP_PARENT, .x = 600, .y = 20, .w = 146, .h = 151, .alive = 1 };

    CHECK(TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start && TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed);
    state = calloc(1, TORIRS_PLUGIN_MINIMAP_ORBS.state_size); CHECK(state);
    spec_alias = 1;
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_start(&api, state);
    CHECK(alive_owned() == 0);
    /* The minimap is watched LAST: every native orb role has had its say
     * before the plugin decides the column goes beside the map. */
    CHECK(watch_count == 5 && strcmp(watched[watch_count - 1], "minimap") == 0);

    /* Lane without native orbs: bind the minimap only. */
    bind(&TORIRS_PLUGIN_MINIMAP_ORBS, &api, state, "minimap", W_MINIMAP, 1);
    CHECK(alive_owned() == 4);
    {
        int hp = owned_under(W_MAP_PARENT, "orb_hitpoints"), prayer = owned_under(W_MAP_PARENT, "orb_prayer");
        int run = owned_under(W_MAP_PARENT, "orb_run"), spec = owned_under(W_MAP_PARENT, "orb_special");
        CHECK(hp >= 0 && prayer >= 0 && run >= 0 && spec >= 0);
        CHECK(widgets[hp].w == 57 && widgets[hp].h == 34 && widgets[hp].image == 100);
        CHECK(widgets[hp].x <= widgets[W_MINIMAP].x + 6 - 57);
        for( int i = W_FIRST_OWNED; i < next_owned; i++ ) CHECK(!orb_covers_map(&widgets[W_MINIMAP], &widgets[i]));
        /* The hitpoints picture (captured by name): plate everywhere, red disc in the lower part, dark cap over the top rows (42/50 -> 4 hidden rows). */
        CHECK((last_composed[0] >> 24) != 0);
        CHECK(last_composed[(4 + 24) * 57 + 27 + 13] != last_composed[(4 + 1) * 57 + 27 + 13]);
        /* No native button on this lane: the run orb arms through the compat iface name. */
        CHECK(strcmp(widgets[run].op_label, "Toggle Run") == 0 && widgets[run].op != NULL);
        CHECK(widgets[hp].op_label[0] == 0);
        struct ToriRS_WidgetEvent press = { .type = TORIRS_WIDGET_OPERATION, .widget = ref_of(run), .operation = 1 };
        widgets[run].op(&api, widgets[run].op_user, &press);
        CHECK(invoked_component == 153 && invoked_operation == 0 && invoked_actions == 0);
        /* A quiet frame composes and moves nothing -- including the inactive
         * (walking) run orb and the inactive special orb, whose grey
         * substitution must be part of the hashed inputs. */
        int c = composes, p = positions, v = revalidates;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
        CHECK(composes == c && positions == p && revalidates == v);
        /* The column closes up: switching the prayer orb off slides the two
         * below it into the vacated slots -- the table is a column, not four
         * fixed addresses. */
        int const prayer_y = widgets[prayer].y, run_y = widgets[run].y, hp_y = widgets[hp].y;
        CHECK(prayer_y != run_y);
        show_prayer = 0;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(&api, state, "show_prayer");
        CHECK(owned_under(W_MAP_PARENT, "orb_prayer") < 0 && alive_owned() == 3);
        CHECK(widgets[hp].y == hp_y && widgets[run].y == prayer_y);
        CHECK(!orb_covers_map(&widgets[W_MINIMAP], &widgets[run]));
        show_prayer = 1;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(&api, state, "show_prayer");
        prayer = owned_under(W_MAP_PARENT, "orb_prayer");
        CHECK(prayer >= 0 && widgets[run].y == run_y && widgets[prayer].y == prayer_y);
        /* A world that names no special-attack button has no special attack:
         * the orb is not drawn at all, rather than a meter over whatever that
         * world keeps in the varp its profile copied. */
        spec_alias = 0;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(&api, state, "spec_button");
        CHECK(owned_under(W_MAP_PARENT, "orb_special") < 0 && alive_owned() == 3);
        spec_value_log[0] = 0;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
        CHECK(spec_value_log[0] == 0);
        spec_alias = 1;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(&api, state, "spec_button");
        CHECK(owned_under(W_MAP_PARENT, "orb_special") >= 0 && alive_owned() == 4);
        /* The map moves: the column follows without re-creating controls. */
        widgets[W_MINIMAP].x = 500; int alive = next_owned;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
        CHECK(next_owned == alive && widgets[hp].x <= 500 + 6 - 57 && !orb_covers_map(&widgets[W_MINIMAP], &widgets[hp]));
    }
    /* A cutscene hides the cache's orbs: the covers go with them and come
     * back, on the same frame path, when it ends. */
    cutscene_status = 1;
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
    CHECK(alive_owned() == 0);
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
    CHECK(alive_owned() == 0);
    cutscene_status = 0;
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
    CHECK(alive_owned() == 4);
    /* The minimap unbinds: the beside-map controls are gone with their parent. */
    bind(&TORIRS_PLUGIN_MINIMAP_ORBS, &api, state, "minimap", W_MINIMAP, 0);
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_stop(&api, state);
    CHECK(released >= 15);

    /* Lane with interface 160: controls cover the native roots and press the native button. */
    native_orbs = 1; button_available[0] = 1; button_available[1] = 1; button_available[2] = 1;
    button_hidden[2] = 1; /* no special-attack weapon wielded: the client hid the button */
    released = 0; invoked_actions = 0; watch_count = 0;
    for( int i = W_FIRST_OWNED; i < 64; i++ ) widgets[i].alive = 0;
    for( int id = W_ORB_HP; id <= W_ORB_SPEC; id++ ) widgets[id] = (struct FakeWidget){ .parent = 0, .x = 516 + 10 * (id - W_ORB_HP), .y = 41 + 33 * (id - W_ORB_HP), .w = 57, .h = 34, .alive = 1 };
    for( int id = W_BTN_PRAYER; id <= W_BTN_SPEC; id++ ) widgets[id] = (struct FakeWidget){ .parent = W_ORB_PRAYER + (id - W_BTN_PRAYER), .x = 3, .y = 5, .w = 50, .h = 26, .alive = 1 };
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_start(&api, state);
    {
        /* One publication, delivered the way the host delivers it: every watch
         * in registration order. Each control is created once, under its own
         * native root, and no orb is ever placed beside the map on the way. */
        int const p = positions, v = revalidates;
        CHECK(watch_count == 5);
        for( int i = 0; i < watch_count; i++ )
        {
            bind(&TORIRS_PLUGIN_MINIMAP_ORBS, &api, state, watched[i], widget_for_role(watched[i]), 1);
            for( int j = W_FIRST_OWNED; j < next_owned; j++ )
                CHECK(!(widgets[j].alive && widgets[j].parent == W_MAP_PARENT));
        }
        CHECK(alive_owned() == 4 && positions - p == 4 && revalidates - v == 4);
    }
    {
        int run = owned_under(W_ORB_RUN, "orb_run"), prayer = owned_under(W_ORB_PRAYER, "orb_prayer"), spec = owned_under(W_ORB_SPEC, "orb_special");
        CHECK(run >= 0 && prayer >= 0 && spec >= 0 && owned_under(W_MAP_PARENT, "orb_run") < 0);
        CHECK(widgets[run].x == 0 && widgets[run].y == 0 && widgets[run].w == 57);
        CHECK(strcmp(widgets[prayer].op_label, "Quick-prayers") == 0);
        /* The client has hidden the special-attack button, and it says so in
         * the live action query. The profile's static [iface:] alias still
         * names that component -- it is a press target, not a claim that the
         * press would land -- so the cover stays unarmed and wears the grey
         * the client is drawing underneath, instead of a full cyan disc. */
        CHECK(widgets[spec].op_label[0] == 0);
        CHECK(strstr(spec_value_log, "inactive=1") != NULL);
        struct ToriRS_WidgetEvent press = { .type = TORIRS_WIDGET_OPERATION, .widget = ref_of(run), .operation = 1 };
        widgets[run].op(&api, widgets[run].op_user, &press);
        CHECK(invoked_actions == 1 && id_of(last_action_widget) == W_BTN_RUN && strstr(last_log, "via=native"));
        /* A native action that refuses between the arming and the press is not
         * a silent no-op: nothing else can be pressed here, and the user is
         * told rather than left clicking a live-looking orb. */
        int const n = notifies;
        invoke_result = TORIRS_CONTRACT_NATIVE_BLOCKED;
        widgets[run].op(&api, widgets[run].op_user, &press);
        CHECK(notifies == n + 1 && strstr(last_log, "via=none") && strstr(last_notify, "Toggle Run"));
        invoke_result = TORIRS_CONTRACT_OK;
        /* The original readiness gap was run-OFF with no compatibility alias.
         * A display-hidden native action must remove the cover's verb before
         * any attempted press; merely returning an error on press is too late. */
        run_mode = 1;
        button_hidden[1] = 1;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
        CHECK(widgets[run].op == NULL && widgets[run].op_label[0] == 0);
        int const blocked_invocations = invoked_actions;
        button_hidden[1] = 0;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
        CHECK(widgets[run].op != NULL && !strcmp(widgets[run].op_label, "Toggle Run"));
        invoked_component = -1;
        widgets[run].op(&api, widgets[run].op_user, &press);
        CHECK(invoked_actions == blocked_invocations + 1 && invoked_component == -1);
        run_mode = 0;
        /* The special-attack button appears (a special weapon): the orb arms
         * and the disc goes live on the same frame path. */
        button_hidden[2] = 0;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_frame_start(&api, state, NULL);
        CHECK(strcmp(widgets[spec].op_label, "Use Special Attack") == 0);
        CHECK(strstr(spec_value_log, "inactive=0") != NULL);
        /* Keeping native orbs removes the plugin's covers. */
        replace_native = 0;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(&api, state, "replace_native");
        CHECK(alive_owned() == 0);
        replace_native = 1;
        TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_config_changed(&api, state, "replace_native");
        CHECK(alive_owned() == 4);
        /* A native remount: the root unbinds with its owned child, rebinds, and the cover comes back. */
        bind(&TORIRS_PLUGIN_MINIMAP_ORBS, &api, state, "orb_run", W_ORB_RUN, 0);
        widgets[run].alive = 0;
        bind(&TORIRS_PLUGIN_MINIMAP_ORBS, &api, state, "orb_run", W_ORB_RUN, 1);
        CHECK(owned_under(W_ORB_RUN, "orb_run") >= 0);
    }
    TORIRS_PLUGIN_MINIMAP_ORBS.callbacks.on_stop(&api, state);
    free(state);
    puts("minimap orbs v2: ok");
    return 0;
}
