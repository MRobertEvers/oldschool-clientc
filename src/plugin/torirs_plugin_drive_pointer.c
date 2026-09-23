/*
 * quest-driver: projection, the pickset, the real minimenu click, movement and the camera.
 *
 * Owner: verbs-pointer (docs/ARCHITECT.md). Nobody else defines a function in this
 * file, and this file defines no function declared under another owner's
 * block in src/plugin/torirs_plugin_drive.h.
 *
 * click_minimenu is the PRIMARY action and this file is its machinery:
 * project, move, let a frame RENDER, confirm the pickset holds the target
 * (the pickset is stamped at the render-time hover point, so asking before
 * the move is meaningless), right-press, find the row by ACTION ID and pick
 * identity -- never by row text -- then left-press so the client's own
 * dispatcher runs. Budget one extra frame everywhere: a CmdBus push from a
 * plugin lands one loop iteration later than the TORIRS_SIM_* precedent.
 * DrivePointer_WorldOp is the LOGGED bypass, never the default.
 *
 * PENDING CROSS-GROUP SEAMS (reported, not owned here):
 *
 *   - app_plugin_world_op / app_plugin_world_walk_to / app_plugin_world_walk_near
 *     are defined in src/plugin/torirs_plugin_bridge.u.c (verbs-pointer's own
 *     slice of that shared file -- ARCHITECT.md S1/S5), which is textually
 *     part of app.c and can therefore reach app_try_move/app_try_move_npc/
 *     app_try_move_loc and app_minimenu_run_option. This TU is a separate
 *     translation unit outside the app/ layer (app/app_internal.h is
 *     layer-private, app.h is closed per ARCHITECT S5), so the three are
 *     forward-declared below rather than pulled in through a header. Report:
 *     promote them into app.h, or a small public app_plugin_world.h, once
 *     app.h reopens.
 *   - PluginDriveCore_CmdBus() is core-scheduler's seam (torirs_plugin_drive.c,
 *     closed to this file) -- QD-01 flagged this as undefined/unlinked, but
 *     core-scheduler has since landed both PluginDriveCore_SetCmdBus and
 *     PluginDriveCore_CmdBus (torirs_plugin_drive.c) and content_test.c wires
 *     the live bus through it (src/game/content_test.c:443,
 *     PluginDriveCore_SetCmdBus(bus), the same way it already hands
 *     PluginDriveCore_SetEmbed the embedded server) -- resolved, no longer a
 *     pending seam.
 *   - App_SetCameraPose and App_LocalPlayerIdle (plan S4) are verbs-ui's
 *     (src/app/app_plugin_api.c, closed to every other owner) and do not
 *     exist yet either, and neither is declared anywhere this file can reach
 *     (app.h is closed; they are not part of the shared drive contract).
 *     DrivePointer_Camera and DrivePointer_PlayerIdle therefore implement the
 *     same validation+write directly against struct App's own public fields
 *     (mirroring src/game/content_test.c:545-556 and
 *     src/app/app_plugin_api.c's App_LocalPlayerTiles idleness reading) --
 *     small, self-contained, and duplicated on purpose rather than blocked on
 *     a file this group does not own. Reported for a later dedupe once
 *     verbs-ui lands the shared helpers.
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "app.h"
#include "plugin/torirs_plugin_lua.h"

#include "cmd/cmdbus.h"
#include "game/rs_entity_sync.h"
#include "game/rs_minimenu_world.h"
#include "input/torirs_input.h"
#include "render/torirs_world_projection.h"
#include "revconfig/revconfig.h"
#include "ui/uitree_minimenu.h"
#include "world/entity_pool.h"
#include "world/world.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/* See the file banner: defined in torirs_plugin_bridge.u.c (this group's
 * slice of it), which is #included into app.c and can therefore reach
 * app_try_move and app_minimenu_run_option. Not declared in any header this
 * file may include (app.h is closed; app_internal.h is app-layer-private). */
extern int app_plugin_world_op(struct App* app, enum DrivePickKind kind, int element_id, int option);
extern int app_plugin_world_walk_to(struct App* app, int abs_x, int abs_z);
extern int app_plugin_world_walk_near(struct App* app, enum DrivePickKind kind, int element_id);
extern int app_plugin_inv_op(
    struct App* app,
    int component_id,
    int slot,
    int obj_id,
    int count,
    int option,
    char const** out_reason);

/* app_minimenu.c's "clicked off" convergence point (its own banner: every
 * site that ends a selection funnels through here, so a mode added later
 * cannot be forgotten at one of them).  Non-static there, but app_minimenu.c
 * is #included into app.c and app_internal.h is layer-private, so it is
 * declared here for the same reason the four above are.  Used by
 * drive_pointer_inv_use_on to make sure a FAILED item-on-item leaves no armed
 * selection behind for the next verb's click to spend. */
extern int app_selection_clear(struct App* app);

/* See the file banner: core-scheduler's seam, not landed yet. */
extern struct ToriRS_CmdBus* PluginDriveCore_CmdBus(void);

/* ------------------------------------------------------------ local player */

/* Mirrors src/app/app_world_query.c's app_local_player -- app-layer-private,
 * so re-derived here from the same two public leaf calls (RS_EntitySync_Find
 * Player + World_EntityPoolGet) rather than reached through app_internal.h. */
static struct WorldEntity_Player*
drive_pointer_local_player(struct App* app)
{
    int world_idx;

    if( !app->world )
        return NULL;
    if( !RS_EntitySync_FindPlayer(
            &app->esync, app->esync.local_pid >= 0 ? app->esync.local_pid : 2047, &world_idx, NULL) )
        return NULL;
    return World_EntityPoolGet(&app->world->entities.player, world_idx);
}

/* Defined below, beside the element-id resolver that is its other caller:
 * the loc projector ranks candidates by distance to the local player rather
 * than to the viewport centre, and needs the player's scene tile to do it. */
static int
drive_pointer_player_tile(struct App* app, int* out_x, int* out_z);

/* -------------------------------------------------------------- projection */

/* Mirrors app_world_project_at (app-layer-private): pure math over public
 * struct App fields plus the public ToriRS_WorldProjectPoint helper. Root
 * projection only -- see the file banner / U2: a loc or ground stack carries
 * no view_placement at all, and a player aboard a world-entity view is
 * refused (drive_pointer_project_ground below) rather than guessed through. */
static int
drive_pointer_project_point(
    struct App* app, int fine_x, int fine_z, int world_y, int* out_x, int* out_y)
{
    if( !app->world || !app->world_view_valid )
        return 0;
    if( fine_x < 128 || fine_z < 128 )
        return 0;
    return ToriRS_WorldProjectPoint(
        &app->world_camera,
        &app->world_camera_pos,
        app->world_emit_desc.x,
        app->world_emit_desc.y,
        app->world_emit_desc.w,
        app->world_emit_desc.h,
        50,
        fine_x,
        world_y,
        fine_z,
        out_x,
        out_y);
}

static int
drive_pointer_project_ground(
    struct App* app,
    int fine_x,
    int fine_z,
    int level,
    int height_above_ground,
    int* out_x,
    int* out_y)
{
    int ground_y;

    if( !app->world )
        return 0;
    ground_y = World_HeightAt(app->world, fine_x, fine_z, level);
    return drive_pointer_project_point(app, fine_x, fine_z, ground_y - height_above_ground, out_x, out_y);
}

/* Same MARGIN/centre-distance rule as App_NpcScreenPosition
 * (src/app/app_plugin_api.c:131-198): a body on the viewport's edge is half
 * under the frame, and the frame takes the click. */
static int
drive_pointer_in_viewport(struct App* app, int x, int y)
{
    enum
    {
        MARGIN = 12
    };
    struct UITreeEmitDesc const* viewport = &app->world_emit_desc;
    return x >= viewport->x + MARGIN && x < viewport->x + viewport->w - MARGIN &&
           y >= viewport->y + MARGIN && y < viewport->y + viewport->h - MARGIN;
}

static enum DriveResult
drive_pointer_screen_position_npc(struct App* app, int id, int* out_x, int* out_y, int* out_element_id)
{
    struct World_EntityPool* pool = &app->world->entities.npc;
    int x, y, npc_type;
    int slot;
    int found_any = 0;
    int i;
    struct WorldEntity_NPC* npc;

    /* App_NpcScreenPosition (verbs-ui, app_plugin_api.c) answers -1 for both
     * "no such npc anywhere in the pool" and "found but off-screen" (QD-17):
     * a pre-check over the same pool, using only public leaf calls, is what
     * lets not_found and not_visible stay separable without editing that
     * file. */
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* candidate = World_EntityPoolGet(pool, i);
        if( candidate && candidate->server_slot >= 0 && (id < 0 || candidate->npc_id == id) )
        {
            found_any = 1;
            break;
        }
    }
    slot = App_NpcScreenPosition(app, id, &x, &y, &npc_type);
    if( slot < 0 )
        return found_any ? DRIVE_NOT_VISIBLE : DRIVE_NOT_FOUND;
    npc = World_NpcGetByServerSlot(app->world, slot);
    if( !npc )
        return DRIVE_NOT_VISIBLE;
    *out_x = x;
    *out_y = y;
    *out_element_id = npc->element_id;
    return DRIVE_OK;
}

static enum DriveResult
drive_pointer_screen_position_loc(struct App* app, int id, int* out_x, int* out_y, int* out_element_id)
{
    struct World_EntityPool* pool;
    int best_element = -1;
    long best_distance = 0;
    int centre_x;
    int centre_z;
    int found_any = 0;
    int i;

    if( !app->world || !app->world_view_valid )
        return DRIVE_NOT_VISIBLE;
    pool = &app->world->entities.scenery;
    /*
     * Ranked by distance to the LOCAL PLAYER in scene tiles, not by distance
     * to the viewport centre the way App_NpcScreenPosition ranks npcs.
     *
     * The centre rule is right for an npc, which is a body in the open; it is
     * wrong for scenery, because a loc TYPE is usually planted dozens of
     * times across the scene and the copy nearest the middle of the frame is
     * routinely a distant one standing behind a nearer, larger loc. Lumbridge
     * is the case that named this: the "tree" nearest the viewport centre was
     * 23 tiles north, its projected point landed inside the castle fountain's
     * model, and the right-click menu there offered "Examine Fountain" and
     * nothing else -- a real `covered`, on a tree the player could have
     * touched. The copy a quest test means is the one it is standing next to.
     */
    centre_x = 0;
    centre_z = 0;
    if( !drive_pointer_player_tile(app, &centre_x, &centre_z) )
    {
        centre_x = app->world_emit_desc.x + app->world_emit_desc.w / 2;
        centre_z = app->world_emit_desc.y + app->world_emit_desc.h / 2;
    }
    else
    {
        centre_x *= 128;
        centre_z *= 128;
    }
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* loc = World_EntityPoolGet(pool, i);
        int size_x;
        int size_z;
        int fine_x;
        int fine_z;
        int x, y;
        long distance;

        if( !loc || (id >= 0 && loc->loc_id != id) )
            continue;
        /* found_any tracks EXISTENCE, independent of projection (QD-17): a
         * loc that fails to project or falls outside the viewport is still
         * a match, so a caller asking for one that is nowhere in the pool
         * gets not_found instead of the same not_visible every off-screen
         * loc also answers. */
        found_any = 1;
        /* Footprint centroid, not a corner -- U3 is open on which screen
         * point a rotated multi-tile loc should give, and a centroid is the
         * documented simplification (docs/QUEST_DRIVER_PLAN.md S7 U3). */
        size_x = loc->size_x > 0 ? loc->size_x : 1;
        size_z = loc->size_z > 0 ? loc->size_z : 1;
        fine_x = loc->grid_position.x * 128 + size_x * 64;
        fine_z = loc->grid_position.z * 128 + size_z * 64;
        if( !drive_pointer_project_ground(app, fine_x, fine_z, loc->grid_position.level, 0, &x, &y) )
            continue;
        if( !drive_pointer_in_viewport(app, x, y) )
            continue;
        /* Over the loc's own FINE position against the player's, both in
         * scene fine units -- see the ranking note above. */
        distance = (long)(fine_x - centre_x) * (fine_x - centre_x) +
                   (long)(fine_z - centre_z) * (fine_z - centre_z);
        if( best_element >= 0 && distance >= best_distance )
            continue;
        best_element = loc->element_id;
        best_distance = distance;
        *out_x = x;
        *out_y = y;
    }
    if( best_element < 0 )
        return found_any ? DRIVE_NOT_VISIBLE : DRIVE_NOT_FOUND;
    *out_element_id = best_element;
    return DRIVE_OK;
}

static enum DriveResult
drive_pointer_screen_position_obj(struct App* app, int id, int* out_x, int* out_y, int* out_element_id)
{
    struct World_EntityPool* pool;
    int best_element = -1;
    long best_distance = 0;
    int centre_x;
    int centre_z;
    int found_any = 0;
    int i;

    if( !app->world || !app->world_view_valid )
        return DRIVE_NOT_VISIBLE;
    pool = &app->world->entities.obj_stack;
    /* Ranked by distance to the LOCAL PLAYER, for the reason the loc
     * projector above gives: one obj id is commonly on the floor in several
     * places at once (a test that drops the same item twice makes two), and
     * the stack a caller means is the one it walked to. Ranking by distance
     * to the viewport centre instead chose a DIFFERENT stack from the one
     * player.click_obj had just walked up to, pressed at it across the map,
     * and reported `covered`. */
    centre_x = 0;
    centre_z = 0;
    if( !drive_pointer_player_tile(app, &centre_x, &centre_z) )
    {
        centre_x = app->world_emit_desc.x + app->world_emit_desc.w / 2;
        centre_z = app->world_emit_desc.y + app->world_emit_desc.h / 2;
    }
    else
    {
        centre_x *= 128;
        centre_z *= 128;
    }
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, i);
        int x, y;
        long distance;

        if( !stack || (id >= 0 && stack->obj_id != id) )
            continue;
        /* Existence, independent of projection (QD-17) -- see the loc case
         * above. */
        found_any = 1;
        /* stack->draw_position, never a tile centre -- the corrected reading
         * in docs/QUEST_DRIVER_PLAN.md S0 ("pointer"). */
        if( !drive_pointer_project_ground(
                app,
                (int)stack->draw_position.x,
                (int)stack->draw_position.z,
                stack->grid_position.level,
                0,
                &x,
                &y) )
            continue;
        if( !drive_pointer_in_viewport(app, x, y) )
            continue;
        /* Over the stack's own fine draw position against the player's -- see
         * the ranking note above. */
        distance = (long)((int)stack->draw_position.x - centre_x) *
                       ((int)stack->draw_position.x - centre_x) +
                   (long)((int)stack->draw_position.z - centre_z) *
                       ((int)stack->draw_position.z - centre_z);
        if( best_element >= 0 && distance >= best_distance )
            continue;
        best_element = stack->element_id;
        best_distance = distance;
        *out_x = x;
        *out_y = y;
    }
    if( best_element < 0 )
        return found_any ? DRIVE_NOT_VISIBLE : DRIVE_NOT_FOUND;
    *out_element_id = best_element;
    return DRIVE_OK;
}

static enum DriveResult
drive_pointer_screen_position_player(struct App* app, int id, int* out_x, int* out_y, int* out_element_id)
{
    struct World_EntityPool* pool;
    int best_element = -1;
    long best_distance = 0;
    int centre_x;
    int centre_z;
    int found_any = 0;
    int i;

    if( !app->world || !app->world_view_valid )
        return DRIVE_NOT_VISIBLE;
    pool = &app->world->entities.player;
    centre_x = app->world_emit_desc.x + app->world_emit_desc.w / 2;
    centre_z = app->world_emit_desc.y + app->world_emit_desc.h / 2;
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
        int x, y;
        long distance;

        if( !player || (id >= 0 && player->server_pid != id) )
            continue;
        /* Existence, independent of projection (QD-17) -- see the loc case
         * above. The U2 aboard-refusal just below is deliberately NOT
         * counted as "found": an aboard player is a root-projection refusal
         * (not_visible), the same answer this function already gave before
         * QD-17, not a new not_found. */
        found_any = 1;
        /* U2: hard-refuse an aboard target rather than project it through a
         * world-entity view -- docs/QUEST_DRIVER_PLAN.md S7 U2 leaves this
         * open, and a root-only reading is the documented choice here. */
        if( player->view_placement.view_id != 0 )
            continue;
        if( !drive_pointer_project_ground(
                app,
                (int)player->draw_position.x,
                (int)player->draw_position.z,
                player->grid_position.level,
                60,
                &x,
                &y) )
            continue;
        if( !drive_pointer_in_viewport(app, x, y) )
            continue;
        distance = (long)(x - centre_x) * (x - centre_x) + (long)(y - centre_z) * (y - centre_z);
        if( best_element >= 0 && distance >= best_distance )
            continue;
        best_element = player->element_id;
        best_distance = distance;
        *out_x = x;
        *out_y = y;
    }
    if( best_element < 0 )
        return found_any ? DRIVE_NOT_VISIBLE : DRIVE_NOT_FOUND;
    *out_element_id = best_element;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_ScreenPosition(
    struct App* app,
    enum DrivePickKind kind,
    int id,
    int* out_x,
    int* out_y,
    int* out_element_id)
{
    assert(app);
    assert(out_x);
    assert(out_y);
    assert(out_element_id);
    *out_x = 0;
    *out_y = 0;
    *out_element_id = -1;
    if( !app->world )
        return DRIVE_NOT_VISIBLE;
    switch( kind )
    {
    case DRIVE_PICK_NPC:
        return drive_pointer_screen_position_npc(app, id, out_x, out_y, out_element_id);
    case DRIVE_PICK_LOC:
        return drive_pointer_screen_position_loc(app, id, out_x, out_y, out_element_id);
    case DRIVE_PICK_OBJ:
        return drive_pointer_screen_position_obj(app, id, out_x, out_y, out_element_id);
    case DRIVE_PICK_PLAYER:
        return drive_pointer_screen_position_player(app, id, out_x, out_y, out_element_id);
    default:
        return DRIVE_NOT_VISIBLE;
    }
}

enum DriveResult
DrivePointer_PickHolds(struct App* app, int element_id, int* out_held)
{
    int i;

    assert(app);
    assert(out_held);
    *out_held = 0;
    for( i = 0; i < app->world_pickset.count; i++ )
    {
        if( app->world_pickset.items[i].element_id == element_id )
        {
            *out_held = 1;
            break;
        }
    }
    return DRIVE_OK;
}

/* Which pixel that set answers for, plus the viewport the frame drew with --
 * see the header's banner. Pure reads of two public struct App fields, like
 * DrivePointer_PickHolds just above. */
enum DriveResult
DrivePointer_PickPoint(struct App* app, struct DrivePickPoint* out_point)
{
    assert(app);
    assert(out_point);
    out_point->valid = app->world_pickset.mouse_valid;
    out_point->x = app->world_pickset.mouse_x;
    out_point->y = app->world_pickset.mouse_y;
    /* `world_emit_desc` is only a rectangle once a frame has emitted the world
     * into one -- every other reader in this tree gates on world_view_valid
     * (app_camera.c, app_overlay_entities.c) and this one must too, or a
     * caller is handed the previous layout's rectangle, or zeroes, with
     * nothing saying which.  A zero width is the reader's signal that there is
     * no rectangle to test a candidate pixel against (pointer.lua's
     * QD.drive._hover_inside gives the pixel the benefit of the doubt and lets
     * the probe itself answer). */
    if( app->world_view_valid )
    {
        out_point->view_x = app->world_emit_desc.x;
        out_point->view_y = app->world_emit_desc.y;
        out_point->view_w = app->world_emit_desc.w;
        out_point->view_h = app->world_emit_desc.h;
    }
    else
    {
        out_point->view_x = 0;
        out_point->view_y = 0;
        out_point->view_w = 0;
        out_point->view_h = 0;
    }
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_MouseMove(struct App* app, int x, int y)
{
    struct ToriRS_CmdBus* bus;

    assert(app);
    (void)app;
    bus = PluginDriveCore_CmdBus();
    if( !bus )
        return DRIVE_UNSUPPORTED;
    if( !CmdBus_PushMouseMove(bus, (int16_t)x, (int16_t)y) )
        return DRIVE_REFUSED;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_MouseButton(struct App* app, int button, int down, int x, int y)
{
    struct ToriRS_CmdBus* bus;
    uint32_t type = down ? TORIRS_CMD_INPUT_MOUSE_DOWN : TORIRS_CMD_INPUT_MOUSE_UP;

    assert(app);
    (void)app;
    bus = PluginDriveCore_CmdBus();
    if( !bus )
        return DRIVE_UNSUPPORTED;
    if( !CmdBus_PushMouseButton(bus, type, (uint8_t)button, (int16_t)x, (int16_t)y) )
        return DRIVE_REFUSED;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_MenuVisible(struct App* app, int* out_visible)
{
    assert(app);
    assert(out_visible);
    *out_visible = app->interact.minimenu.visible ? 1 : 0;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_MenuRows(struct App* app, struct DriveMenuRow* out, int cap, int* out_count)
{
    struct UIMinimenu const* menu;
    int n;
    int i;

    assert(app);
    assert(out);
    assert(cap > 0);
    assert(out_count);
    menu = &app->interact.minimenu;
    if( !menu->visible )
    {
        *out_count = 0;
        return DRIVE_OK;
    }
    n = menu->option_count < cap ? menu->option_count : cap;
    for( i = 0; i < n; i++ )
    {
        struct UIMinimenuOption const* opt = &menu->options[i];
        memset(&out[i], 0, sizeof(out[i]));
        out[i].action = UIMinimenu_ActionNormalize(opt->action);
        out[i].pick_kind = (int)opt->pick.kind;
        out[i].target_id = opt->pick.id;
        out[i].component_id = opt->pick.id;
        out[i].slot = opt->action_index;
        out[i].centre_x = menu->x + menu->width / 2;
        out[i].centre_y = UIMinimenu_OptionY(menu, i);
        snprintf(out[i].text, sizeof(out[i].text), "%s", opt->text);
    }
    *out_count = n;
    return DRIVE_OK;
}

/* enum DrivePickKind -> enum UIMinimenuPickKind. UI_MINIMENU_PICK_NONE for
 * anything this contract does not name -- DrivePointer_MenuRowFind then
 * matches nothing, which is the correct DRIVE_NO_ROW rather than a crash. */
static enum UIMinimenuPickKind
drive_pointer_ui_kind(enum DrivePickKind kind)
{
    switch( kind )
    {
    case DRIVE_PICK_NPC:
        return UI_MINIMENU_PICK_NPC;
    case DRIVE_PICK_PLAYER:
        return UI_MINIMENU_PICK_PLAYER;
    case DRIVE_PICK_LOC:
        return UI_MINIMENU_PICK_SCENERY;
    case DRIVE_PICK_OBJ:
        return UI_MINIMENU_PICK_OBJ;
    default:
        return UI_MINIMENU_PICK_NONE;
    }
}

enum DriveResult
DrivePointer_MenuRowFind(
    struct App* app,
    int action,
    enum DrivePickKind kind,
    int target_id,
    struct DriveMenuRow* out_row)
{
    struct UIMinimenu const* menu;
    enum UIMinimenuPickKind ui_kind;
    int i;

    assert(app);
    assert(out_row);
    memset(out_row, 0, sizeof(*out_row));
    menu = &app->interact.minimenu;
    if( !menu->visible )
        return DRIVE_NOT_VISIBLE;
    ui_kind = drive_pointer_ui_kind(kind);
    for( i = 0; i < menu->option_count; i++ )
    {
        struct UIMinimenuOption const* opt = &menu->options[i];
        int normalized;

        if( opt->pick.kind != ui_kind )
            continue;
        /* Pick IDENTITY, never row text and never row order (design doc). */
        if( opt->pick.id != target_id )
            continue;
        normalized = UIMinimenu_ActionNormalize(opt->action);
        /* action < 0 is the WILDCARD (header contract): the collapsed
         * use-item row matches on pick identity alone. */
        if( action >= 0 && normalized != action )
            continue;
        out_row->action = normalized;
        out_row->pick_kind = (int)kind;
        out_row->target_id = target_id;
        out_row->component_id = opt->pick.id;
        out_row->slot = opt->action_index;
        out_row->centre_x = menu->x + menu->width / 2;
        out_row->centre_y = UIMinimenu_OptionY(menu, i);
        snprintf(out_row->text, sizeof(out_row->text), "%s", opt->text);
        return DRIVE_OK;
    }
    return DRIVE_NO_ROW;
}

/* Player op ids, mirroring rs_minimenu_world.c's own opplayer_action_for_slot
 * (static there, and outside verbs-pointer's export list -- ARCHITECT.md S1
 * names only opnpc/oploc/opobj). Not part of the phase-1 world-interact verb
 * set (no player.op verb exists yet), kept here only so
 * DrivePointer_ActionForSlot answers DRIVE_PICK_PLAYER instead of silently
 * mismatching a future caller. */
static int
drive_pointer_opplayer_action_for_slot(int slot)
{
    static int const ids[5] = {
        REVCONFIG_MINIMENU_OPPLAYER1,
        REVCONFIG_MINIMENU_OPPLAYER2,
        REVCONFIG_MINIMENU_OPPLAYER3,
        REVCONFIG_MINIMENU_OPPLAYER4,
        REVCONFIG_MINIMENU_OPPLAYER5,
    };
    return (slot >= 0 && slot < 5) ? ids[slot] : REVCONFIG_MINIMENU_OPPLAYER1;
}

/*
 * op number (1..5) -> the action id the client's own builder would use for
 * that op slot, via the exported opnpc/oploc/opobj_action_for_slot
 * (src/game/rs_minimenu_world.c). `slot` is 0-based (op number - 1), matching
 * those functions directly -- the Lua composition converts. `slot` < 0 means
 * EXAMINE: the only way to reach OP*6 without a numeric client op string
 * living in Lua (ARCHITECT.md S2 naming rule), since click_minimenu's
 * `option` may be the string "examine".
 */
enum DriveResult
DrivePointer_ActionForSlot(enum DrivePickKind kind, int slot, int* out_action)
{
    assert(out_action);
    switch( kind )
    {
    case DRIVE_PICK_NPC:
        *out_action = slot < 0 ? REVCONFIG_MINIMENU_OPNPC6 : opnpc_action_for_slot(slot);
        return DRIVE_OK;
    case DRIVE_PICK_LOC:
        *out_action = slot < 0 ? REVCONFIG_MINIMENU_OPLOC6 : oploc_action_for_slot(slot);
        return DRIVE_OK;
    case DRIVE_PICK_OBJ:
        *out_action = slot < 0 ? REVCONFIG_MINIMENU_OPOBJ6 : opobj_action_for_slot(slot);
        return DRIVE_OK;
    case DRIVE_PICK_PLAYER:
        if( slot < 0 )
        {
            *out_action = -1;
            return DRIVE_UNSUPPORTED;
        }
        *out_action = drive_pointer_opplayer_action_for_slot(slot);
        return DRIVE_OK;
    default:
        *out_action = -1;
        return DRIVE_UNSUPPORTED;
    }
}

/* ------------------------------------------- type id -> live element id */

/*
 * Everything a quest test names is a CONTENT SYMBOL, so `target.id` is an
 * npc_id / loc_id / obj_id -- while every dispatcher below the minimenu is
 * keyed by ELEMENT id (World_NpcGetByElementId and friends). The projectors
 * above have always done that conversion on the way to a pixel; the two
 * bypasses did not, and handed the type id straight to a by-element lookup,
 * which is exactly why drive.op answered not_found on an npc standing in
 * front of the player ("op 1 -> nil -- npc 3106").
 *
 * Nearest the local player wins, on the same reasoning App_NpcScreenPosition
 * picks the candidate nearest the viewport centre: with several of a type in
 * the scene, the one a test means is the one it is standing next to. The
 * distance is over SCENE tiles, which is all three pools' common frame.
 */
static int
drive_pointer_player_tile(struct App* app, int* out_x, int* out_z)
{
    struct WorldEntity_Player const* player;

    assert(app);
    assert(out_x);
    assert(out_z);
    player = drive_pointer_local_player(app);
    if( !player )
        return 0;
    *out_x = player->grid_position.x;
    *out_z = player->grid_position.z;
    return 1;
}

static long
drive_pointer_tile_distance(int have_origin, int origin_x, int origin_z, int x, int z)
{
    if( !have_origin )
        return 0;
    return (long)(x - origin_x) * (x - origin_x) + (long)(z - origin_z) * (z - origin_z);
}

enum DriveResult
DrivePointer_ElementId(struct App* app, enum DrivePickKind kind, int id, int* out_element_id)
{
    struct World_EntityPool* pool;
    int origin_x = 0;
    int origin_z = 0;
    int have_origin;
    int best = -1;
    long best_distance = 0;
    int i;

    assert(app);
    assert(out_element_id);
    *out_element_id = -1;
    if( !app->world )
        return DRIVE_NOT_FOUND;
    have_origin = drive_pointer_player_tile(app, &origin_x, &origin_z);
    if( kind == DRIVE_PICK_NPC )
        pool = &app->world->entities.npc;
    else if( kind == DRIVE_PICK_LOC )
        pool = &app->world->entities.scenery;
    else if( kind == DRIVE_PICK_OBJ )
        pool = &app->world->entities.obj_stack;
    else if( kind == DRIVE_PICK_PLAYER )
        pool = &app->world->entities.player;
    else
        return DRIVE_UNSUPPORTED;
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        void* entry = World_EntityPoolGet(pool, i);
        int element_id;
        int tile_x;
        int tile_z;
        long distance;

        if( !entry )
            continue;
        if( kind == DRIVE_PICK_NPC )
        {
            struct WorldEntity_NPC* npc = entry;
            /* npc_id OR base_npc_id, the same pair QD.npc.by_symbol matches
             * on: a multiNpc's wrapper is the id a test can name, and the
             * child is what the local varp state selected. */
            if( npc->server_slot < 0 )
                continue;
            if( id >= 0 && npc->npc_id != id && npc->base_npc_id != id )
                continue;
            if( npc->multinpc_hidden )
                continue;
            element_id = npc->element_id;
            tile_x = npc->grid_position.x;
            tile_z = npc->grid_position.z;
        }
        else if( kind == DRIVE_PICK_LOC )
        {
            struct WorldEntity_Scenery* loc = entry;
            if( id >= 0 && loc->loc_id != id )
                continue;
            element_id = loc->element_id;
            tile_x = loc->grid_position.x;
            tile_z = loc->grid_position.z;
        }
        else if( kind == DRIVE_PICK_OBJ )
        {
            struct WorldEntity_ObjStack* stack = entry;
            if( id >= 0 && stack->obj_id != id )
                continue;
            element_id = stack->element_id;
            tile_x = stack->grid_position.x;
            tile_z = stack->grid_position.z;
        }
        else
        {
            struct WorldEntity_Player* player = entry;
            if( id >= 0 && player->server_pid != id )
                continue;
            element_id = player->element_id;
            tile_x = player->grid_position.x;
            tile_z = player->grid_position.z;
        }
        distance = drive_pointer_tile_distance(have_origin, origin_x, origin_z, tile_x, tile_z);
        if( best >= 0 && distance >= best_distance )
            continue;
        best = element_id;
        best_distance = distance;
    }
    if( best < 0 )
        return DRIVE_NOT_FOUND;
    *out_element_id = best;
    return DRIVE_OK;
}

/*
 * Does this target OFFER op `option`?
 *
 * app_minimenu_ui_pick_live answers 1 unconditionally for a world pick (it
 * only validates UI and INV_SLOT ones), so the fabricated row a bypass builds
 * is dispatched whatever number it carries, the packet goes out naming an op
 * the entity does not have, and the server answers nothing at all -- a verb
 * that looks like it worked and did not. The row builders in
 * src/game/rs_minimenu_world.c are the authority on which ops exist, and this
 * asks them the same three questions they ask: the npc's visible_ops mask and
 * op name, the loc's interned config ops, the obj stack's ops -- including
 * add_obj_rows' synthesized "Take", which exists only for slot index 2 and
 * only when that slot is empty.
 */
enum DriveResult
DrivePointer_OpAvailable(
    struct App* app, enum DrivePickKind kind, int id, int option, int* out_available)
{
    int element_id = -1;
    int slot = option - 1;
    enum DriveResult resolved;

    assert(app);
    assert(out_available);
    *out_available = 0;
    resolved = DrivePointer_ElementId(app, kind, id, &element_id);
    if( resolved != DRIVE_OK )
        return resolved;
    /* Examine is built for every world target unconditionally (each of the
     * three builders ends with its own OP*6 row), so it is always available
     * and has no slot to test. */
    if( option <= 0 )
    {
        *out_available = 1;
        return DRIVE_OK;
    }
    if( slot < 0 || slot > 4 )
        return DRIVE_OK;
    if( kind == DRIVE_PICK_NPC )
    {
        struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, element_id, NULL);
        if( !npc )
            return DRIVE_NOT_FOUND;
        *out_available =
            (npc->visible_ops & (1u << slot)) != 0 && npc->actions[slot].name[0] != '\0';
        return DRIVE_OK;
    }
    if( kind == DRIVE_PICK_LOC )
    {
        struct WorldEntity_Scenery* loc = World_SceneryGetByElementId(app->world, element_id);
        if( !loc || !loc->info )
            return DRIVE_NOT_FOUND;
        *out_available = loc->info->actions[slot].name[0] != '\0';
        return DRIVE_OK;
    }
    if( kind == DRIVE_PICK_OBJ )
    {
        struct WorldEntity_ObjStack* stack = World_ObjStackGetByElementId(app->world, element_id);
        if( !stack )
            return DRIVE_NOT_FOUND;
        /* add_obj_rows: the defaulted "Take" is emitted for slot index 2 when
         * the config leaves that slot empty, and for no other slot. */
        *out_available = stack->actions[slot].name[0] != '\0' || slot == 2;
        return DRIVE_OK;
    }
    return DRIVE_UNSUPPORTED;
}

enum DriveResult
DrivePointer_WorldOp(struct App* app, enum DrivePickKind kind, int id, int option)
{
    int element_id = -1;
    int available = 0;
    enum DriveResult resolved;

    assert(app);
    if( kind != DRIVE_PICK_NPC && kind != DRIVE_PICK_LOC && kind != DRIVE_PICK_OBJ )
        return DRIVE_UNSUPPORTED;
    /* `id` is a CONTENT TYPE id (see DrivePointer_ElementId): the bridge takes
     * an element id, and handing it the type id is what made this answer
     * not_found for every npc in the world. */
    resolved = DrivePointer_ElementId(app, kind, id, &element_id);
    if( resolved != DRIVE_OK )
        return resolved;
    resolved = DrivePointer_OpAvailable(app, kind, id, option, &available);
    if( resolved != DRIVE_OK )
        return resolved;
    if( !available )
        return DRIVE_NO_ROW;
    if( !app_plugin_world_op(app, kind, element_id, option) )
        return DRIVE_NOT_FOUND;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_InvOp(
    struct App* app,
    int component_id,
    int slot,
    int obj_id,
    int count,
    int option,
    char const** out_reason)
{
    assert(app);
    assert(out_reason);
    *out_reason = NULL;
    if( option > 5 )
    {
        *out_reason = "option above 5 is not an OPHELD row";
        return DRIVE_UNSUPPORTED;
    }
    /*
     * A refusal from the dispatcher is REFUSED, never NOT_FOUND, and it
     * carries the reason: the caller's alternative is to watch for an effect
     * that was never going to arrive and report the silence as the server's.
     */
    if( !app_plugin_inv_op(app, component_id, slot, obj_id, count, option, out_reason) )
    {
        assert(*out_reason);
        return DRIVE_REFUSED;
    }
    /*
     * The arming half of use_on has an observable effect in this process and
     * nothing else does: OPHELD1..5 leave as a packet whose answer arrives
     * ticks later (the Lua verb awaits that), while OPHELDT_START's whole job
     * is to put this item into app->objsel. Checking it here is what stops
     * "the dispatcher ran" from being mistaken for "the item is armed" -- a
     * cell whose config never offered a Use row runs the same code and arms
     * nothing.
     */
    if( option < 0 && (!app->objsel.active || app->objsel.obj_id != obj_id) )
        return DRIVE_REFUSED;
    return DRIVE_OK;
}

/*
 * ARM THIS CELL'S "Use" SELECTION, WHATEVER IS ARMED NOW -- the entry point a
 * caller needs when it must arm a second time and cannot know whether the
 * first arming is still live.
 *
 * Why DrivePointer_InvOp(..., option < 0) cannot be that entry point.
 *
 * app_minimenu_inv_action's objsel branch runs BEFORE the switch on the
 * action, so with a selection already armed EVERY inventory row -- the "Use"
 * row included -- is encoded as an OPHELDU of the clicked cell on the armed
 * one and clears the selection (app_minimenu.c, the branch above
 * REVCONFIG_MINIMENU_TGT_HELD).  Re-arming the SAME cell through that path
 * therefore sends "use this item on itself", a packet no real menu can
 * produce (rs_minimenu_build.c omits the row for the arming cell), and leaves
 * objsel empty -- after which InvOp's own `option < 0 && !objsel.active`
 * check answers DRIVE_REFUSED.  That is what made the quest driver's use_on
 * refuse to re-arm between far-side retry presses at all, and so land an
 * ordinary op row on a target whose arming had not survived the retry
 * (build/quest_gate/golem row 58, build/quest_gate/fishingcompo row 30,
 * 2026-09-20).
 *
 * So this decides against app->objsel FIRST and only then touches the client:
 *
 *   - already armed with THIS cell holding THIS obj: the arming survived,
 *     nothing at all is sent, `*out_was_armed` says so.  A caller that
 *     re-arms before every press is then free of charge in the case that
 *     needed no re-arm, which is the measured cog `redcog` case the driver's
 *     own banner records;
 *   - armed with something else: that selection is dropped here
 *     (app_selection_clear, the reference's own doAction tail) before the
 *     "Use" row is taken, because leaving it lit would spend it on the next
 *     world click and the OPHELDU above would fire instead of the arming;
 *   - nothing armed: the ordinary OPHELDT_START path, with InvOp's own proof
 *     that objsel came back holding this obj.
 *
 * `out_was_armed` is optional; every other argument names the cell exactly as
 * DrivePointer_InvOp does.  InvOp's refusal sentence is read here only to
 * satisfy its contract: an arming has no detail channel of its own, and the
 * word it answers (DRIVE_REFUSED rather than a bare DRIVE_NOT_FOUND) already
 * says the client declined the cell.
 */
enum DriveResult
DrivePointer_InvArm(
    struct App* app, int component_id, int slot, int obj_id, int count, int* out_was_armed)
{
    char const* out_reason = NULL;

    assert(app);
    if( out_was_armed )
        *out_was_armed = 0;
    if( app->objsel.active && app->objsel.component_id == component_id &&
        app->objsel.slot == slot && app->objsel.obj_id == obj_id )
    {
        if( out_was_armed )
            *out_was_armed = 1;
        return DRIVE_OK;
    }
    if( app->objsel.active )
        app_selection_clear(app);
    return DrivePointer_InvOp(app, component_id, slot, obj_id, count, -1, &out_reason);
}

/*
 * THE SECOND HALF OF AN ITEM-ON-ITEM (player.use_item_on_item's phase 2): the
 * click that lands on the OTHER backpack cell while a "Use" selection is
 * armed, so the CLIENT encodes OPHELDU -- app_minimenu_inv_action's first
 * branch, net_out_opheldu(clicked obj/slot/component, armed obj/slot/
 * component) -- and no packet is ever synthesised here.
 *
 * Why this is not just another DrivePointer_InvOp call.
 *
 * app_plugin_inv_op's `option` chooses an ACTION, and the objsel branch in
 * app_minimenu_inv_action runs BEFORE the switch that reads it, so with a
 * selection armed every option value produces the same OPHELDU.  That makes
 * the option irrelevant when the arming held and load-bearing when it did
 * not: option 1 is rev-239's backpack op 1, the shift-click-drop chain, so a
 * Lua verb that armed, lost the arming, and then "clicked" the target cell
 * with op 1 would put the target item on the FLOOR and report it as a use.
 * This passes 0 (Examine) for that reason and then proves the value was never
 * read: the objsel branch is the only thing in that function that CLEARS the
 * selection, so `objsel.active` false on the far side is the client saying it
 * encoded the OPHELDU, and `objsel.active` still true means the row never ran
 * at all (app_minimenu_ui_pick_live rejected the cell -- the backpack tab is
 * not the shown one, or the cell holds something else now) and NOTHING was
 * sent, Examine included.
 *
 * `component_id`/`slot` name the cell that is CLICKED; the armed cell is
 * whatever the caller's arming half (DrivePointer_InvOp with option < 0) put
 * in app->objsel.  Refusing when the two are the same cell mirrors the real
 * menu builder, which omits the "Use A with B" row for the very cell that
 * armed the selection (rs_minimenu_build.c add_inv_slot_select_row, "can't
 * use an item on itself") -- a row the player cannot see is a row this must
 * not click.
 *
 * Every failure path clears the selection before returning.  An arming that
 * stays live is consumed by the NEXT world click whatever it hits, which
 * would turn an unrelated later verb into a use-on; a verb that failed must
 * not leave that behind it.
 */
static enum DriveResult
drive_pointer_inv_use_on(struct App* app, int component_id, int slot, int obj_id, int count)
{
    char const* refusal = NULL;

    assert(app);
    if( !app->objsel.active )
        return DRIVE_REFUSED;
    if( app->objsel.component_id == component_id && app->objsel.slot == slot )
    {
        app_selection_clear(app);
        return DRIVE_NO_ROW;
    }
    if( !app_plugin_inv_op(app, component_id, slot, obj_id, count, 0, &refusal) )
    {
        app_selection_clear(app);
        return DRIVE_NOT_FOUND;
    }
    if( app->objsel.active )
    {
        app_selection_clear(app);
        return DRIVE_REFUSED;
    }
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_MoveTo(struct App* app, int tile_x, int tile_z)
{
    assert(app);
    if( !app_plugin_world_walk_to(app, tile_x, tile_z) )
        return DRIVE_REFUSED;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_MoveNear(struct App* app, enum DrivePickKind kind, int id)
{
    int element_id = -1;
    enum DriveResult resolved;

    assert(app);
    if( kind != DRIVE_PICK_NPC && kind != DRIVE_PICK_LOC )
        return DRIVE_UNSUPPORTED;
    /* Same type-id/element-id conversion DrivePointer_WorldOp needs: the
     * bridge routes by element id and player.walk_near was passing the
     * content type id, so it answered not_found on every target. */
    resolved = DrivePointer_ElementId(app, kind, id, &element_id);
    if( resolved != DRIVE_OK )
        return resolved;
    if( !app_plugin_world_walk_near(app, kind, element_id) )
        return DRIVE_NOT_FOUND;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_Camera(struct App* app, int yaw, int pitch, int zoom)
{
    assert(app);
    /* Mirrors src/game/content_test.c:545-556's validation+write -- see the
     * file banner (App_SetCameraPose does not exist yet). */
    if( pitch < 128 || pitch > 383 || zoom < -1000 || zoom > 10000 )
        return DRIVE_REFUSED;
    app->orbit.yaw = app->world_camera.yaw = yaw & 2047;
    app->orbit.pitch = app->world_camera.pitch = pitch;
    app->world_cam_zoom = zoom;
    app->orbit.yaw_velocity = 0;
    app->orbit.pitch_velocity = 0;
    app->need_redraw = 1;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_CameraPose(
    struct App* app, int* out_yaw, int* out_pitch, int* out_zoom, int* out_owned)
{
    assert(app);
    assert(out_yaw);
    assert(out_pitch);
    assert(out_zoom);
    assert(out_owned);
    /* The same three fields DrivePointer_Camera writes, so a pose read here
     * and written back through it is a no-op on the next follow step. */
    *out_yaw = app->orbit.yaw & 2047;
    *out_pitch = app->orbit.pitch;
    *out_zoom = app->world_cam_zoom;
    /* app_world_camera_follow's own early returns, in its own order: while
     * either holds, the follow step never reads the orbit angles. */
    *out_owned = !app->camera_unlocked && !app->cam_script.scripted && app->net;
    return DRIVE_OK;
}

enum DriveResult
DrivePointer_PlayerIdle(struct App* app, int* out_idle)
{
    struct WorldEntity_Player const* player;

    assert(app);
    assert(out_idle);
    player = drive_pointer_local_player(app);
    /* Movement idleness only (header contract): route_length == 0 AND
     * minimap.flag_tile_x < 0, checked together because they can settle a
     * tick apart. No local player synced yet reads as "not idle" rather than
     * a contract violation -- a quest test can call this before login
     * finishes the world sync. */
    *out_idle = (player && player->pathing.route_length == 0 && app->minimap.flag_tile_x < 0) ? 1 : 0;
    return DRIVE_OK;
}

/* --------------------------------------------------------------- Lua glue */

/* "npc" / "player" / "loc" / "obj" -> enum DrivePickKind, or -1 for a typo --
 * a Lua thunk turns -1 into a raised error naming the string, the same
 * pattern torirs_plugin_drive.c's drive_symbol_kind_from_name uses. */
static int
drive_pointer_kind_from_name(char const* name)
{
    if( !strcmp(name, "npc") )
        return DRIVE_PICK_NPC;
    if( !strcmp(name, "player") )
        return DRIVE_PICK_PLAYER;
    if( !strcmp(name, "loc") )
        return DRIVE_PICK_LOC;
    if( !strcmp(name, "obj") )
        return DRIVE_PICK_OBJ;
    return -1;
}

static void
drive_pointer_push_menu_row(struct lua_State* L, struct DriveMenuRow const* row)
{
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, row->action);
    lua_setfield(L, -2, "action");
    lua_pushinteger(L, row->pick_kind);
    lua_setfield(L, -2, "pick_kind");
    lua_pushinteger(L, row->target_id);
    lua_setfield(L, -2, "target_id");
    lua_pushinteger(L, row->component_id);
    lua_setfield(L, -2, "component_id");
    lua_pushinteger(L, row->slot);
    lua_setfield(L, -2, "slot");
    lua_pushinteger(L, row->centre_x);
    lua_setfield(L, -2, "centre_x");
    lua_pushinteger(L, row->centre_y);
    lua_setfield(L, -2, "centre_y");
    lua_pushstring(L, row->text);
    lua_setfield(L, -2, "text");
}

static int
lua_drive_screen_position(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* kind_name = PluginDrive_ArgString(L, 1);
    int id = PluginDrive_ArgInt(L, 2);
    int kind = drive_pointer_kind_from_name(kind_name);
    int x = 0, y = 0, element_id = -1;
    enum DriveResult result;

    assert(app);
    if( kind < 0 )
        return luaL_error(L, "drive.screen_position: unknown kind '%s'", kind_name);
    result = DrivePointer_ScreenPosition(app, (enum DrivePickKind)kind, id, &x, &y, &element_id);
    lua_pushstring(L, DriveResultName(result));
    if( result != DRIVE_OK )
    {
        lua_pushnil(L);
        return 2;
    }
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, element_id);
    lua_setfield(L, -2, "element_id");
    return 2;
}

static int
lua_drive_pick_holds(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int element_id = PluginDrive_ArgInt(L, 1);
    int held = 0;
    enum DriveResult result;

    assert(app);
    result = DrivePointer_PickHolds(app, element_id, &held);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, held);
    return 2;
}

static int
lua_drive_pick_point(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    struct DrivePickPoint point;
    enum DriveResult result;

    assert(app);
    memset(&point, 0, sizeof(point));
    result = DrivePointer_PickPoint(app, &point);
    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, 0, 7);
    lua_pushboolean(L, point.valid);
    lua_setfield(L, -2, "valid");
    lua_pushinteger(L, point.x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, point.y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, point.view_x);
    lua_setfield(L, -2, "view_x");
    lua_pushinteger(L, point.view_y);
    lua_setfield(L, -2, "view_y");
    lua_pushinteger(L, point.view_w);
    lua_setfield(L, -2, "view_w");
    lua_pushinteger(L, point.view_h);
    lua_setfield(L, -2, "view_h");
    return 2;
}

static int
lua_drive_mouse_move(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int x = PluginDrive_ArgInt(L, 1);
    int y = PluginDrive_ArgInt(L, 2);
    enum DriveResult result;

    assert(app);
    result = DrivePointer_MouseMove(app, x, y);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_mouse_button(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* button_name = PluginDrive_ArgString(L, 1);
    int down = PluginDrive_ArgInt(L, 2);
    int x = PluginDrive_ArgInt(L, 3);
    int y = PluginDrive_ArgInt(L, 4);
    int button;
    enum DriveResult result;

    assert(app);
    if( !strcmp(button_name, "left") )
        button = TORIRSM_LEFT;
    else if( !strcmp(button_name, "right") )
        button = TORIRSM_RIGHT;
    else if( !strcmp(button_name, "middle") )
        button = TORIRSM_MIDDLE;
    else
        return luaL_error(L, "drive.mouse_button: unknown button '%s'", button_name);
    result = DrivePointer_MouseButton(app, button, down, x, y);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_menu_visible(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int visible = 0;
    enum DriveResult result;

    assert(app);
    result = DrivePointer_MenuVisible(app, &visible);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, visible);
    return 2;
}

static int
lua_drive_menu_rows(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    struct DriveMenuRow rows[UITREE_MINIMENU_MAX_OPTIONS];
    int count = 0;
    enum DriveResult result;
    int i;

    assert(app);
    result = DrivePointer_MenuRows(app, rows, UITREE_MINIMENU_MAX_OPTIONS, &count);
    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, count, 0);
    for( i = 0; i < count; i++ )
    {
        drive_pointer_push_menu_row(L, &rows[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

static int
lua_drive_menu_row_find(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int action = PluginDrive_ArgOptInt(L, 1, -1);
    char const* kind_name = PluginDrive_ArgString(L, 2);
    int target_id = PluginDrive_ArgInt(L, 3);
    int kind = drive_pointer_kind_from_name(kind_name);
    struct DriveMenuRow row;
    enum DriveResult result;

    assert(app);
    if( kind < 0 )
        return luaL_error(L, "drive.menu_row_find: unknown kind '%s'", kind_name);
    result = DrivePointer_MenuRowFind(app, action, (enum DrivePickKind)kind, target_id, &row);
    lua_pushstring(L, DriveResultName(result));
    if( result != DRIVE_OK )
    {
        lua_pushnil(L);
        return 2;
    }
    drive_pointer_push_menu_row(L, &row);
    return 2;
}

static int
lua_drive_action_for_slot(struct lua_State* L)
{
    char const* kind_name = PluginDrive_ArgString(L, 1);
    int slot = PluginDrive_ArgInt(L, 2);
    int kind = drive_pointer_kind_from_name(kind_name);
    int action = -1;
    enum DriveResult result;

    if( kind < 0 )
        return luaL_error(L, "drive.action_for_slot: unknown kind '%s'", kind_name);
    result = DrivePointer_ActionForSlot((enum DrivePickKind)kind, slot, &action);
    lua_pushstring(L, DriveResultName(result));
    if( result != DRIVE_OK )
    {
        lua_pushnil(L);
        return 2;
    }
    lua_pushinteger(L, action);
    return 2;
}

static int
lua_drive_world_op(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* kind_name = PluginDrive_ArgString(L, 1);
    int id = PluginDrive_ArgInt(L, 2);
    int option = PluginDrive_ArgInt(L, 3);
    int kind = drive_pointer_kind_from_name(kind_name);
    enum DriveResult result;

    assert(app);
    if( kind < 0 )
        return luaL_error(L, "drive.world_op: unknown kind '%s'", kind_name);
    result = DrivePointer_WorldOp(app, (enum DrivePickKind)kind, id, option);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_inv_op(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = PluginDrive_ArgInt(L, 1);
    int slot = PluginDrive_ArgInt(L, 2);
    int obj_id = PluginDrive_ArgInt(L, 3);
    int count = PluginDrive_ArgInt(L, 4);
    int option = PluginDrive_ArgInt(L, 5);
    char const* refusal = NULL;
    enum DriveResult result;

    assert(app);
    result = DrivePointer_InvOp(app, component_id, slot, obj_id, count, option, &refusal);
    return PluginDrive_PushResult(L, result, refusal);
}

/* api_drive.inv_arm(component_id, slot, obj_id, count) -> (result, detail).
 * use_on's phase 1, idempotent: the detail says whether an arming was already
 * live ("already armed, nothing sent") or this call took the "Use" row.  See
 * DrivePointer_InvArm for why re-arming through inv_op cannot work. */
static int
lua_drive_inv_arm(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = PluginDrive_ArgInt(L, 1);
    int slot = PluginDrive_ArgInt(L, 2);
    int obj_id = PluginDrive_ArgInt(L, 3);
    int count = PluginDrive_ArgInt(L, 4);
    int was_armed = 0;
    enum DriveResult result;

    assert(app);
    result = DrivePointer_InvArm(app, component_id, slot, obj_id, count, &was_armed);
    return PluginDrive_PushResult(
        L, result, was_armed ? "already armed, nothing sent" : "armed by this call");
}

/* api_drive.inv_use_on(component_id, slot, obj_id, count) -> (result, nil).
 * The clicked cell only: the armed one is already in app->objsel, put there
 * by the caller's own api_drive.inv_op(..., -1).  See
 * drive_pointer_inv_use_on. */
static int
lua_drive_inv_use_on(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = PluginDrive_ArgInt(L, 1);
    int slot = PluginDrive_ArgInt(L, 2);
    int obj_id = PluginDrive_ArgInt(L, 3);
    int count = PluginDrive_ArgInt(L, 4);
    enum DriveResult result;

    assert(app);
    result = drive_pointer_inv_use_on(app, component_id, slot, obj_id, count);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_op_available(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* kind_name = PluginDrive_ArgString(L, 1);
    int id = PluginDrive_ArgInt(L, 2);
    int option = PluginDrive_ArgInt(L, 3);
    int kind = drive_pointer_kind_from_name(kind_name);
    int available = 0;
    enum DriveResult result;

    assert(app);
    if( kind < 0 )
        return luaL_error(L, "drive.op_available: unknown kind '%s'", kind_name);
    result = DrivePointer_OpAvailable(app, (enum DrivePickKind)kind, id, option, &available);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, available);
    return 2;
}

static int
lua_drive_move_to(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int tile_x = PluginDrive_ArgInt(L, 1);
    int tile_z = PluginDrive_ArgInt(L, 2);
    enum DriveResult result;

    assert(app);
    result = DrivePointer_MoveTo(app, tile_x, tile_z);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_move_near(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* kind_name = PluginDrive_ArgString(L, 1);
    int id = PluginDrive_ArgInt(L, 2);
    int kind = drive_pointer_kind_from_name(kind_name);
    enum DriveResult result;

    assert(app);
    if( kind < 0 )
        return luaL_error(L, "drive.move_near: unknown kind '%s'", kind_name);
    result = DrivePointer_MoveNear(app, (enum DrivePickKind)kind, id);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_camera(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int yaw = PluginDrive_ArgInt(L, 1);
    int pitch = PluginDrive_ArgInt(L, 2);
    int zoom = PluginDrive_ArgInt(L, 3);
    enum DriveResult result;

    assert(app);
    result = DrivePointer_Camera(app, yaw, pitch, zoom);
    return PluginDrive_PushResult(L, result, NULL);
}

/* api.drive.camera_pose() -> ("ok", {yaw=, pitch=, zoom=, owned=}). */
static int
lua_drive_camera_pose(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int yaw = 0, pitch = 0, zoom = 0, owned = 0;
    enum DriveResult result;

    assert(app);
    result = DrivePointer_CameraPose(app, &yaw, &pitch, &zoom, &owned);
    lua_pushstring(L, DriveResultName(result));
    lua_newtable(L);
    lua_pushinteger(L, yaw);
    lua_setfield(L, -2, "yaw");
    lua_pushinteger(L, pitch);
    lua_setfield(L, -2, "pitch");
    lua_pushinteger(L, zoom);
    lua_setfield(L, -2, "zoom");
    lua_pushboolean(L, owned);
    lua_setfield(L, -2, "owned");
    return 2;
}

static int
lua_drive_player_idle(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int idle = 0;
    enum DriveResult result;

    assert(app);
    result = DrivePointer_PlayerIdle(app, &idle);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, idle);
    return 2;
}


static struct LuaFn const LUA_DRIVE_POINTER_FNS[] = {
    {"screen_position", lua_drive_screen_position},
    {"pick_holds", lua_drive_pick_holds},
    {"pick_point", lua_drive_pick_point},
    {"mouse_move", lua_drive_mouse_move},
    {"mouse_button", lua_drive_mouse_button},
    {"menu_visible", lua_drive_menu_visible},
    {"menu_rows", lua_drive_menu_rows},
    {"menu_row_find", lua_drive_menu_row_find},
    {"action_for_slot", lua_drive_action_for_slot},
    {"world_op", lua_drive_world_op},
    {"op_available", lua_drive_op_available},
    {"inv_op", lua_drive_inv_op},
    {"inv_arm", lua_drive_inv_arm},
    {"inv_use_on", lua_drive_inv_use_on},
    {"move_to", lua_drive_move_to},
    {"move_near", lua_drive_move_near},
    {"camera", lua_drive_camera},
    {"camera_pose", lua_drive_camera_pose},
    {"player_idle", lua_drive_player_idle},
    {NULL, NULL},
};

void
PluginDrivePointer_RegisterLua(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_POINTER_FNS);
}

#else /* !TORIRS_EMBED_SERVER */

/* No embedded server, no driver. The registrar stays so the module assembly
 * in torirs_plugin_drive.c needs no second spelling. */
void
PluginDrivePointer_RegisterLua(struct lua_State* L, void* script)
{
    (void)L;
    (void)script;
}

#endif /* TORIRS_EMBED_SERVER */
