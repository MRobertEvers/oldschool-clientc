/*
 * The right-click menu: building the rows, and running the one that was
 * clicked.
 *
 * Included into app.c rather than compiled on its own. app_minimenu_run_option
 * is a switch over every verb the client has, so it reaches nearly every
 * subsystem by construction; a per-verb dispatch table is the change that would
 * make this a module, and that is a behavioural change rather than a move.
 */

/* Snapshot the armed use/target selection for the minimenu builder (reference
 * useMode/targetMode). objsel and targetsel are mutually exclusive — arming one
 * clears the other. */
static struct RS_MinimenuSelection
app_minimenu_selection(struct App const* app)
{
    struct RS_MinimenuSelection sel = { .mode = RS_MINIMENU_SELECT_NONE };
    if( app->objsel.active )
    {
        sel.mode = RS_MINIMENU_SELECT_USE_ITEM;
        snprintf(sel.obj_name, sizeof(sel.obj_name), "%s", app->objsel.name);
        sel.obj_slot = app->objsel.slot;
        sel.obj_com_id = app->objsel.component_id;
    }
    else if( app->targetsel.active )
    {
        sel.mode = RS_MINIMENU_SELECT_TARGET;
        snprintf(sel.target_op, sizeof(sel.target_op), "%s", app->targetsel.op);
        sel.target_mask = app->targetsel.mask;
        sel.target_mask_held_bit = app->features ? app->features->target_mask_held : 0;
    }
    return sel;
}

/* RS_MinimenuBuildCtx.view_world_fn: a deck loc's SCENERY pick (view_id set)
 * resolves through its view's OWN world, which the root tables cannot see
 * (SAILING_PLAN C5.2's non-terrain half). NULL for a dead view — the row is
 * then dropped, like every other dead-view pick. */
static struct World*
app_minimenu_view_world(
    void* user,
    int view_id)
{
    struct App* app = (struct App*)user;

    assert(app);
    if( !WorldviewRegistry_IsLive(&app->worldviews, view_id) )
        return NULL;
    return WorldviewRegistry_Get(&app->worldviews, view_id)->world;
}

/*
 * MINIMENU_ENTRY (7101) and MINIMENU_NUMOPS (7110), from the menu the hover
 * line was composed from.
 *
 * The acting row is the LAST one after the priority sort -- the same row
 * UIHoverText_Compose draws -- and `option_count - 1` is the reference's
 * numops, which excludes Cancel. `menu` NULL means the pointer is on nothing
 * the menu speaks for (popup open, off canvas): entry empty, numops 0, which
 * is what 4726 bails on.
 *
 * The whole row goes in the OP and the target is left empty. See
 * RS_ClientOpState::mouseover_op for why the halves cannot be split back apart
 * here, and for the tooltip-width bug an empty op caused.
 */
static void
app_minimenu_entry_publish(
    struct App* app,
    struct UIMinimenu const* menu)
{
    struct RS_ClientOpState* clientop = &app->host.clientop;
    int const num_ops = menu ? menu->option_count - 1 : 0;

    assert(app);

    clientop->mouseover_op[0] = '\0';
    clientop->mouseover_target[0] = '\0';
    clientop->mouseover_opcount = num_ops > 0 ? num_ops : 0;
    clientop->mouseover_component = -1;
    if( num_ops > 0 )
    {
        struct UIMinimenuOption const* acting = &menu->options[menu->option_count - 1];
        snprintf(clientop->mouseover_op, sizeof(clientop->mouseover_op), "%s", acting->text);
        /* `_7109`'s subject: the component the acting row is about, for the
         * two row kinds that have one. The reference reads the same field off
         * its own entry and gates on the entry TYPE being one of the two
         * interface kinds, which is what these two are. */
        if( acting->pick.kind == UI_MINIMENU_PICK_UI ||
            acting->pick.kind == UI_MINIMENU_PICK_INV_SLOT )
            clientop->mouseover_component = acting->pick.id;
    }
}

/*
 * Mouseover text, rebuilt every frame from a scratch menu at the pointer.
 *
 * This is the client half of what the reference gets from the cache: script
 * 4726 (re-armed each cycle by 4725) bails on minimenu_isopen, reads
 * minimenu_entry / _numops / _type, and has proc 4727 draw one line. Building
 * the same menu the right click would show is exactly what those opcodes
 * report, so both the line drawn here and the CS2 snapshot come from one pass.
 */
static void
app_hover_text_update(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    struct UIMinimenu scratch;
    char prev[UITREE_HOVERTEXT_LEN];
    bool const prev_visible = app->hover_text.visible;
    int click_in_world;

    snprintf(prev, sizeof(prev), "%s", app->hover_text.text);

    /* 4726's first gate: no hover line while the Choose Option popup is up.
     * Nor under a chrome window: the line would name whatever the window is
     * drawn over, and the right click that row promises is refused. Nor with
     * no pointer on the canvas at all -- after a touch tap the position is
     * still there but nothing is hovering it, and a line naming what a finger
     * touched a minute ago is the same ghost as an unmoving cursor. */
    if( app->interact.minimenu.visible || app->pointer_absent ||
        app_chrome_wants_pointer(app, mouse_x, mouse_y) || mouse_x < 0 || mouse_y < 0 ||
        mouse_x >= UITREE_LAYOUT_ROOT_W || mouse_y >= UITREE_LAYOUT_ROOT_H )
    {
        app->hover_text.visible = false;
        app->hover_text.text[0] = '\0';
        app_minimenu_entry_publish(app, NULL);
    }
    else
    {
        click_in_world = app_world_mouse_gate(app, mouse_x, mouse_y) && app_world_drawable(app);
        {
            struct RS_MinimenuBuildCtx mctx = {
                .tree = app->tree,
                .ui_host = &app->ui_host,
                .provider = app->provider,
                .runner = &app->runner,
                .invs = &app->invs,
                .chat = &app->chat_source,
                .events_for_component = app_minimenu_events_for_component,
                .events_user = app,
                .selection = app_minimenu_selection(app),
                .player_ops = (char const(*)[40])app->player_ops,
                .player_ops_primary = app->player_ops_primary,
                .player_attack_option = app->player_attack_option,
                .npc_attack_option = app->npc_attack_option,
                .attack_option_model = app->features->attack_option_model,
                .world = app->world,
                /* Same rule the click paths use: world rows only when the
                 * pointer is over bare viewport. */
                .world_pickset = click_in_world ? &app->world_pickset : NULL,
                .click_in_world = click_in_world != 0,
                .wevs = &app->wevs,
                .view_world_fn = app_minimenu_view_world,
                .view_world_user = app,
                .locedit_active = app->locedit.visible != 0,
                .mapedit_select_active = app_mapedit_select_active(app),
                .plugin_io_down = app_plugin_io_down(app) != 0,
            };
            UIMinimenu_Reset(&scratch);
            scratch.font_id = app->hover_text.font_id;
            app_sailing_menu_context(app, &mctx, mouse_x, mouse_y);
            RS_Minimenu_Build(&mctx, mouse_x, mouse_y, &scratch);
            app_minimenu_stamp_node_identities(app, &scratch);
            app_plugin_menu_build(app, &scratch, 1);
        }
        UIHoverText_Compose(&scratch, &app->hover_text);
        app_minimenu_entry_publish(app, &scratch);
    }

    /* Anchor at the world viewport's top-left (4726's container origin), or
     * the canvas when no viewport is on screen. */
    if( app->world_view_valid )
    {
        app->hover_text.x = app->world_emit_desc.clip.x + APP_HOVERTEXT_INSET_X;
        app->hover_text.y = app->world_emit_desc.clip.y + APP_HOVERTEXT_INSET_Y;
        app->hover_text.w = app->world_emit_desc.clip.w - APP_HOVERTEXT_INSET_X;
    }
    else
    {
        app->hover_text.x = APP_HOVERTEXT_INSET_X;
        app->hover_text.y = APP_HOVERTEXT_INSET_Y;
        app->hover_text.w = UITREE_LAYOUT_ROOT_W - APP_HOVERTEXT_INSET_X;
    }

    if( app->hover_text.visible != prev_visible || strcmp(app->hover_text.text, prev) != 0 )
        app->need_redraw = 1;
}

/*
 * Give "Walk here" a destination when this click's pickset holds no terrain.
 *
 * OFF unless features->ground_click_offmap_nearest says otherwise, because the
 * reference has no such destination to give: no ground triangle contained the
 * point, so nothing was recorded and nothing is sent. See the field.
 *
 * Only the click paths call this: resolving the tile sweeps the scene, and the
 * hover-text builder runs the same menu every frame for a row whose text does
 * not depend on the tile. A pickset that DOES hold terrain is left alone — the
 * row targets the picked tile there, exactly as before.
 */
static void
app_minimenu_ctx_ground_fallback(
    struct App* app,
    struct RS_MinimenuBuildCtx* mctx,
    int click_x,
    int click_y)
{
    int x = 0, z = 0, level = 0;

    if( !app->features->ground_click_offmap_nearest )
        return;
    if( !mctx->click_in_world || !mctx->world_pickset )
        return;
    for( int i = 0; i < mctx->world_pickset->count; i++ )
        if( mctx->world_pickset->items[i].type == WORLD_PICK_TERRAIN )
            return;
    if( !app_world_nearest_ground_tile(app, click_x, click_y, &x, &z, &level) )
        return;
    mctx->ground_fallback_valid = true;
    mctx->ground_fallback_x = x;
    mctx->ground_fallback_z = z;
    mctx->ground_fallback_level = level;
}

/* ------------------------------------------------------------- client ops --
 *
 * The rows the CACHE installed with CLIENTOP_* (6700..6709): "Mark tile" on a
 * tile, "Tag" and "Tag-All" on an npc, "Lookup" on a player. Client-side rows
 * that run a clientscript and are never sent to a server. See rs_clientop.h.
 */

/*
 * Held SHIFT is what makes them appear, and the cache says so rather than this
 * client deciding it: setting 112's own description is "When enabled, hold
 * shift and right-click the ground to place highlights". Nothing in the
 * installing scripts tests a key, so the gate is the client's to apply — and
 * without it every right click on the ground would carry a "Mark tile" row
 * above "Walk here".
 */
static bool
app_clientop_armed(struct App const* app)
{
    assert(app);
    if( !app->plugin_input )
        return false;
    return LibToriRS_Input_IsKeyHeld(app->plugin_input, TORIRSK_SHIFT);
}

/** Append every installed op of `kind`, labelled against `subject` (which may
 *  be NULL or empty for a tile, which names nothing). */
static void
app_clientop_add_rows(
    struct App* app,
    struct UIMinimenu* menu,
    enum RS_ClientOpKind kind,
    char const* subject,
    struct UIMinimenuPick pick)
{
    char text[UITREE_MINIMENU_OPTION_LEN];

    assert(app);
    assert(menu);

    for( int slot = 0; slot < RS_CLIENTOP_SLOT_MAX; slot++ )
    {
        struct RS_ClientOpSlot const* op = RS_ClientOpGet(&app->host.clientop, kind, slot);
        if( !op )
            continue;

        /* "Tag @whi@Goblin", the shape every other targeted row uses; a tile
         * names nothing, so its row is the bare label. */
        if( subject && subject[0] )
            snprintf(text, sizeof(text), "%s @whi@%s", op->label, subject);
        else
            snprintf(text, sizeof(text), "%s", op->label);

        UIMinimenu_AddOption(
            menu, text, RS_MINIMENU_ACTION_CLIENTOP, RS_MINIMENU_CLIENTOP_INDEX(kind, slot), pick);
    }
}

/*
 * Add the client-op rows to a freshly built menu.
 *
 * Driven off the ROWS the builder produced rather than off the pickset, for
 * the same reason the npc-highlight plugin is: the rows are what the menu is
 * about, and a pick the builder decided not to offer (a non-interactive wall,
 * an npc filtered out by the attack setting) is not something the user can act
 * on and must not grow an op either.
 *
 * The tile row is the exception -- it hangs off the TERRAIN pick, which is the
 * "Walk here" row, and that row is offered even when it is inert.
 */
static void
app_clientop_menu_build(
    struct App* app,
    struct UIMinimenu* menu,
    int hover_pass)
{
    bool seen[RS_CLIENTOP_KIND_COUNT] = { false, false, false, false, false };
    int const count = menu->option_count;

    assert(app);
    assert(menu);

    /* The hover pass runs every frame to compose the top-left readout; a row
     * added there would be offered as the LEFT-click action. */
    if( hover_pass || !app->world || !app_clientop_armed(app) )
        return;

    for( int i = 0; i < count; i++ )
    {
        struct UIMinimenuOption const* opt = &menu->options[i];
        enum RS_ClientOpKind kind;
        char const* subject = NULL;

        switch( opt->pick.kind )
        {
        case UI_MINIMENU_PICK_NPC:
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt->pick.id, NULL);
            if( !npc )
                continue;
            kind = RS_CLIENTOP_NPC;
            subject = npc->name;
            break;
        }
        case UI_MINIMENU_PICK_SCENERY:
        {
            struct WorldEntity_Scenery* loc = World_SceneryGetByElementId(app->world, opt->pick.id);
            if( !loc )
                continue;
            kind = RS_CLIENTOP_LOC;
            subject = loc->info->name;
            break;
        }
        case UI_MINIMENU_PICK_OBJ:
        {
            struct WorldEntity_ObjStack* stack =
                World_ObjStackGetByElementId(app->world, opt->pick.id);
            if( !stack )
                continue;
            kind = RS_CLIENTOP_OBJ;
            subject = stack->name;
            break;
        }
        case UI_MINIMENU_PICK_PLAYER:
        {
            struct WorldEntity_Player* player =
                World_PlayerGetByElementId(app->world, opt->pick.id);
            if( !player )
                continue;
            kind = RS_CLIENTOP_PLAYER;
            subject = player->name;
            break;
        }
        case UI_MINIMENU_PICK_TERRAIN:
            /* An inert Walk here row (a click on the sky) carries no tile, and
             * a "Mark tile" over nothing would mark the corner of the map. */
            if( opt->pick.secondary_id < 0 )
                continue;
            kind = RS_CLIENTOP_TILE;
            break;
        default:
            continue;
        }

        /* One set of rows per KIND, not per row: an npc with five ops already
         * has five rows in this menu, and adding "Tag" beside each of them
         * would offer the same op five times. */
        if( seen[kind] )
            continue;
        seen[kind] = true;
        app_clientop_add_rows(app, menu, kind, subject, opt->pick);
    }
}

/*
 * Run a client op: record what it is about, then queue its script.
 *
 * NOT a begin/run/clear bracket. RS_CS2_RunScript queues a task; the script
 * runs during the frame's settle, well below this call, so clearing on the way
 * out would land before it ever started and every context op would read -1 --
 * which is exactly what the first version of this did.
 *
 * The context is scoped by the script's IDENTITY instead, and left standing
 * afterwards: only a root frame of `op->script_id` can read it. See
 * RS_ClientOpContext::script_id.
 */
static int
app_clientop_run(
    struct App* app,
    struct UIMinimenuOption const* opt)
{
    struct RS_ClientOpContext ctx;
    struct RS_ClientOpSlot const* op;
    int const kind = RS_MINIMENU_CLIENTOP_KIND(opt->action_index);
    int const slot = RS_MINIMENU_CLIENTOP_SLOT(opt->action_index);
    int base_x;
    int base_z;

    assert(app);
    assert(opt);

    if( !app->world )
        return 1;
    op = RS_ClientOpGet(&app->host.clientop, (enum RS_ClientOpKind)kind, slot);
    if( !op )
        return 1;

    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = kind;
    ctx.uid = -1;
    ctx.type = -1;
    ctx.count = -1;
    ctx.coord = -1;

    switch( (enum RS_ClientOpKind)kind )
    {
    case RS_CLIENTOP_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt->pick.id, NULL);
        if( !npc )
            return 1;
        /* The uid IS the server slot here -- see RS_ClientOpContext::uid for
         * why that is allowed to differ from the reference's. */
        ctx.uid = npc->server_slot;
        ctx.type = npc->npc_id;
        ctx.coord = RS_CLIENTOP_COORD(
            npc->grid_position.level, base_x + npc->grid_position.x, base_z + npc->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", npc->name);
        break;
    }
    case RS_CLIENTOP_LOC:
    {
        struct WorldEntity_Scenery* loc = World_SceneryGetByElementId(app->world, opt->pick.id);
        if( !loc )
            return 1;
        ctx.type = loc->loc_id;
        ctx.coord = RS_CLIENTOP_COORD(
            loc->grid_position.level, base_x + loc->grid_position.x, base_z + loc->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", loc->info->name);
        break;
    }
    case RS_CLIENTOP_OBJ:
    {
        struct WorldEntity_ObjStack* stack = World_ObjStackGetByElementId(app->world, opt->pick.id);
        if( !stack )
            return 1;
        ctx.type = stack->obj_id;
        ctx.count = stack->count;
        ctx.coord = RS_CLIENTOP_COORD(
            stack->grid_position.level,
            base_x + stack->grid_position.x,
            base_z + stack->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", stack->name);
        break;
    }
    case RS_CLIENTOP_PLAYER:
    {
        struct WorldEntity_Player* player = World_PlayerGetByElementId(app->world, opt->pick.id);
        if( !player )
            return 1;
        ctx.uid = player->server_pid;
        ctx.coord = RS_CLIENTOP_COORD(
            player->grid_position.level,
            base_x + player->grid_position.x,
            base_z + player->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", player->name);
        break;
    }
    case RS_CLIENTOP_TILE:
        /* A TERRAIN pick carries scene tile x/z and the level, in
         * secondary/tertiary/quaternary -- see the Walk here row. */
        ctx.coord = RS_CLIENTOP_COORD(
            opt->pick.quaternary_id,
            base_x + opt->pick.secondary_id,
            base_z + opt->pick.tertiary_id);
        break;
    default:
        return 1;
    }

    if( torirs_env_clientop_debug() )
        TORIRS_LOG(
            "clientop: %s slot %d '%s' -> script %d (uid=%d type=%d coord=%d '%s')\n",
            RS_ClientOpKindName((enum RS_ClientOpKind)kind),
            slot,
            op->label,
            op->script_id,
            ctx.uid,
            ctx.type,
            ctx.coord,
            ctx.name);

    ctx.script_id = op->script_id;
    RS_ClientOpContextBegin(&app->host.clientop, &ctx);
    RS_CS2_RunScript(&app->host, &app->runner, op->script_id, NULL, 0, 0, NULL, 0);
    return 1;
}

/* Stamp the exact tree occupant behind every retained native UI row. Scratch
 * menus are consumed synchronously, but a popup can stay open while an earlier
 * hook deletes/rebuilds the same component id into the same array slot. */
static void
app_minimenu_stamp_node_identities(
    struct App const* app,
    struct UIMinimenu* menu)
{
    assert(app);
    assert(menu);

    for( int i = 0; i < menu->option_count; i++ )
    {
        struct UIMinimenuPick* pick = &menu->options[i].pick;
        int32_t idx = -1;

        if( pick->id < 0 )
            continue;
        if( pick->kind == UI_MINIMENU_PICK_UI )
            idx = UITree_FindByComponentId(app->tree, pick->id);
        else if( pick->kind == UI_MINIMENU_PICK_INV_SLOT )
        {
            int32_t parent = UITree_FindByComponentId(app->tree, pick->id);
            if( parent >= 0 && app->tree->components[parent].type == UIELEM_RS_INV )
                idx = parent;
            else
                (void)UITree_ObjCellDynamicAtSlot(
                    app->tree, pick->id, pick->secondary_id, &idx, NULL, NULL);
        }
        if( idx < 0 || (uint32_t)idx >= app->tree->component_count ||
            app->tree->components[idx].freed )
            continue;
        UITree_StampMenuPick(app->tree, idx, pick);
        pick->has_native_events = 1;
        pick->native_events =
            pick->kind == UI_MINIMENU_PICK_INV_SLOT
                ? App_IfEventsGetAt(app, pick->id, pick->secondary_id)
                : App_IfEventsGetEffective(app, app->tree->components[idx].component_id);
    }
}

/* Build + show the minimenu for a right click (reference openMenu: width from
 * the widest row, centered on the click, clamped to the canvas). The tree
 * node stays unpositioned — emit and the interact gesture read the model. */
static void
app_minimenu_open(
    struct App* app,
    int click_x,
    int click_y,
    int click_in_world)
{
    struct RS_MinimenuBuildCtx mctx = {
        .tree = app->tree,
        .ui_host = &app->ui_host,
        .provider = app->provider,
        .runner = &app->runner,
        .invs = &app->invs,
        .chat = &app->chat_source,
        .events_for_component = app_minimenu_events_for_component,
        .events_user = app,
        .selection = app_minimenu_selection(app),
        .player_ops = (char const(*)[40])app->player_ops,
        .player_ops_primary = app->player_ops_primary,
        .player_attack_option = app->player_attack_option,
        .npc_attack_option = app->npc_attack_option,
        .attack_option_model = app->features->attack_option_model,
        .world = app->world,
        .world_pickset = &app->world_pickset,
        .click_in_world = click_in_world != 0,
        .wevs = &app->wevs,
        .view_world_fn = app_minimenu_view_world,
        .view_world_user = app,
        .locedit_active = app->locedit.visible != 0,
        .mapedit_select_active = app_mapedit_select_active(app),
        .plugin_io_down = app_plugin_io_down(app) != 0,
    };
    struct UIMinimenu* menu = &app->interact.minimenu;
    struct UIMinimenuLayout layout;
    int content_w = 0;
    int line_box = 0;

    app_minimenu_ctx_ground_fallback(app, &mctx, click_x, click_y);
    app_sailing_menu_context(app, &mctx, click_x, click_y);
    RS_Minimenu_Build(&mctx, click_x, click_y, menu);
    /* Before the plugins, so their re-sort covers these rows too -- the client
     * ops are the cache's, and a plugin adding a row after them must not leave
     * the menu half-sorted. */
    app_clientop_menu_build(app, menu, 0);
    /* Fence native rows before a plugin menu subscriber gets control. Even a
     * synchronous rebuild from inside that callback must not transfer a row
     * which was authored for the prior occupant to its same-id replacement. */
    app_minimenu_stamp_node_identities(app, menu);
    app_plugin_menu_build(app, menu, 0);

    /* TORIRS_MINIMENU_DEBUG=1: the world pickset that fed the rows plus every
     * row built from it — the one place to see why a loc/obj came up bare. */
    if( getenv("TORIRS_MINIMENU_DEBUG") )
    {
        /* The two Attack options ride this dump because a missing or
         * right-click-only Attack row is otherwise indistinguishable from a
         * pick that never happened — and Hidden is what both settings hold
         * until the server transmits varp clientcode 18/22. The local combat
         * level rides it for the same reason: under "Depends on combat levels"
         * the setting alone does not say whether a row was sunk, the
         * comparison against THIS number does. */
        {
            struct WorldEntity_Player* lp =
                app->world ? World_PlayerGetByServerPid(app->world, app->world->local_pid) : NULL;
            TORIRS_LOG(
                "minimenu: open at %d,%d in_world=%d picks=%d attackopt player=%d npc=%d "
                "mylevel=%d\n",
                click_x,
                click_y,
                click_in_world,
                click_in_world ? app->world_pickset.count : 0,
                app->player_attack_option,
                app->npc_attack_option,
                lp ? lp->combat_level : -1);
        }
        if( click_in_world )
            for( int i = 0; i < app->world_pickset.count; i++ )
            {
                struct World_Picked const* picked = &app->world_pickset.items[i];
                TORIRS_LOG(
                    "  pick[%d] type=%d element=%d tile=%d,%d,%d\n",
                    i,
                    (int)picked->type,
                    picked->element_id,
                    picked->tile_x,
                    picked->tile_z,
                    picked->tile_level);
            }
        for( int i = 0; i < menu->option_count; i++ )
            TORIRS_LOG(
                "  row[%d] action=%d op=%d kind=%d id=%d \"%s\"\n",
                i,
                menu->options[i].action,
                menu->options[i].action_index,
                (int)menu->options[i].pick.kind,
                menu->options[i].pick.id,
                menu->options[i].text);
    }

    /* The popup is sized from measured text, so a font the scene cannot hand
     * back is not a cosmetic miss: PrepareShow falls back to a per-character
     * estimate and the rows draw past the border. The boot-time resolve can
     * land before the b12 load does, so re-resolve on a miss and carry the id
     * to the node that draws the rows — measure and draw must share a font. */
    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, menu->font_id);
        if( !font )
        {
            int const resolved = app_minimenu_font_scene_id(app);
            if( resolved > 0 )
            {
                menu->font_id = resolved;
                for( uint32_t i = 0; i < app->tree->component_count; i++ )
                    if( !app->tree->components[i].freed &&
                        app->tree->components[i].type == UIELEM_BUILTIN_MINIMENU )
                    {
                        (void)UITree_SetMinimenuFontAt(app->tree, (int32_t)i, resolved);
                        break;
                    }
                font = ToriDraw_SceneFontGet(app->scene, resolved);
            }
        }
        if( font )
            line_box = ToriDraw_FontLineBoxHeight(font);
    }
    if( UIMinimenu_PrepareShowStyled(
            menu,
            line_box,
            app->touch_ui ? UI_MINIMENU_STYLE_TOUCH : UI_MINIMENU_STYLE_DESKTOP,
            app_measure_text_cb,
            app,
            &layout,
            &content_w) )
    {
        UIMinimenu_ShowAt(
            menu, layout, content_w, click_x, click_y, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app->need_redraw = 1;
        /* Geometry beside the rows: a popup too narrow for its own text is a
         * measure that returned nothing, and this line is what says so. */
        if( getenv("TORIRS_MINIMENU_DEBUG") )
        {
            TORIRS_LOG(
                "minimenu: font=%d line_box=%d content_w=%d width=%d height=%d\n",
                menu->font_id,
                line_box,
                content_w,
                menu->width,
                menu->height);
            /* Each row's hit band, so a headless run can aim a click at a
             * retained row and prove what it does (or no longer does). */
            for( int i = 0; i < menu->option_count; i++ )
            {
                int const row_y = UIMinimenu_OptionY(menu, i);
                /* REPORT, not LOG: opted in by the env, and wanted in the OPT
                 * build the headless harness runs. */
                TORIRS_REPORT(
                    "minimenu: row[%d] '%s' action=%d band=%d,%d..%d,%d\n",
                    i,
                    menu->options[i].text,
                    menu->options[i].action,
                    menu->x + 1,
                    row_y - menu->layout.hover_above + 1,
                    menu->x + menu->width - 1,
                    row_y + menu->layout.hover_below - 1);
            }
        }
    }
}

/* IF3 inventory paint (`interface_inv_draw_slot_big`) installs
 * `cc_setonop(cc_settrans_temporarily(...))` — the modern stand-in for
 * Client-TS `selectedArea` flash (transPlotSprite 128 for ~15 cycles). That
 * flash is for Wear/Drop/INV_BUTTON/IF_BUTTON only: `OPHELDT_START` ("Use")
 * arms useMode with a white outline and must stay opaque (Client.ts:9183 /
 * TYPE_INV draw 9700 vs selectedArea branch 9752). */
static void
app_inv_cell_op_flash(
    struct App* app,
    int com_id,
    int slot,
    int op_index)
{
    int32_t idx = -1;
    int hook_com_id = -1;
    struct UITreeRuntimeScriptHook const* hook;
    struct UITreeRuntimeScriptHook hook_copy;

    assert(app);
    if( !app->tree )
        return;
    if( !UITree_ObjCellDynamicAtSlot(app->tree, com_id, slot, &idx, NULL, NULL) )
        idx = UITree_FindByComponentId(app->tree, com_id);
    if( idx < 0 )
        return;
    hook = UITree_ResolveClickHook(app->tree, idx, &hook_com_id);
    if( !hook || hook->script_id <= 0 )
        return;
    /* A SNAPSHOT, and it has to be a deep one: the dispatch below can run
     * scripts that rewrite this very hook, and a slot's arguments are owned
     * allocations now, so the old `hook_copy = *hook` aliased tails the
     * dispatch could free underneath it. */
    UITree_HookInitCopy(&hook_copy, hook);
    RS_CS2_SetEventOp(&app->host, op_index > 0 ? op_index : 1, 0);
    RS_CS2_DispatchHook(&app->host, &app->runner, hook_com_id, &hook_copy);
    RS_CS2_SetEventOp(&app->host, 1, 0);
    UITree_HookClear(&hook_copy);
}

/* Target-mode visuals are script-owned. The spellbook registers
 * IF_SETONTARGETENTER/LEAVE on every targetable spell; those hooks set outline
 * 2 (white) while armed and restore outline 0 when the selection ends. */
static void
app_targetsel_dispatch_hook(
    struct App* app,
    int entering)
{
    int32_t idx;
    struct UITreeRuntimeScriptHook hook;

    assert(app);
    if( !app->tree || app->targetsel.component_id < 0 )
        return;
    idx = UITree_FindByComponentId(app->tree, app->targetsel.component_id);
    if( idx < 0 )
        return;
    hook = entering ? UITree_Hooks(&app->tree->components[idx])->on_target_enter
                    : UITree_Hooks(&app->tree->components[idx])->on_target_leave;
    RS_CS2_DispatchHook(&app->host, &app->runner, app->targetsel.component_id, &hook);
}

static void
app_targetsel_clear(struct App* app)
{
    assert(app);
    if( !app->targetsel.active )
        return;
    app_targetsel_dispatch_hook(app, 0);
    app->targetsel.active = 0;
    app->need_redraw = 1;
}

/* Disarm every "armed" selection at once — the "Use <item> with..." pick and
 * the spell/target pick.
 *
 * The reference clears both together at the tail of doAction (Client.ts:9506),
 * so anything that ends a selection ends BOTH; the two modes are mutually
 * exclusive at arm time and there is no path that should retire one while
 * leaving the other lit. Every "clicked off" site funnels through here so a new
 * armed mode added later cannot be forgotten at one of them. Returns nonzero
 * when something was actually armed. */
static int
app_selection_clear(struct App* app)
{
    int was_armed;

    assert(app);
    was_armed = app->objsel.active || app->targetsel.active;
    if( !was_armed )
        return 0;
    /* TORIRS_CLICK_DEBUG=1: which armed mode a click retired. "Use is stuck" and
     * "Use was never armed" produce the same screen, and the outline is script
     * -painted, so the state itself has to say so. */
    if( getenv("TORIRS_CLICK_DEBUG") )
        TORIRS_LOG("selclear: obj=%d tgt=%d\n", app->objsel.active, app->targetsel.active);
    app->objsel.active = 0;
    app_targetsel_clear(app);
    app->need_redraw = 1;
    return 1;
}

/* Execute one selected (or defaulted) menu row: cross feedback + hook
 * dispatch with the row's op index (v1 ui_click_use_minimenu_option). The
 * cross colour comes from the action alone (RS_Minimenu_CrossModeForAction,
 * reference doAction) and is decided once: an action the reference gives no
 * cross — UI buttons, inventory ops, Examine, Cancel — leaves a cross already
 * in flight running rather than clearing or recolouring it. Returns nonzero
 * when a CS2 hook was dispatched. */
/* Send the held-item / component op for the current inventory pick. Returns 1
 * when a use-mode selection or examine consumed the click without a packet. */
static int
app_minimenu_inv_action(
    struct App* app,
    struct UIMinimenuOption const* opt)
{
    int obj_id = opt->pick.tertiary_id;
    int slot = opt->pick.secondary_id;
    int com_id = opt->pick.id;

    /*
     * "Use <held> with <this item>" / "<spell> <this item>": the previous click
     * armed objsel or targetsel and this one completes it (reference OPHELDU /
     * OPHELDT).
     *
     * NO `app_inv_cell_op_flash` on either. Client-TS does set selectedArea for
     * both (Client.ts:9299/9322), but our IF3 stand-in for that flash is to
     * dispatch the cell's own on_op with an op index — and these two rows are
     * not component ops, so there is no index to report and passing 1 fabricates
     * one. On rev-239's backpack op 1 is a LIVE op: the inventory slot builder
     * puts the shift-click-drop handler there (script 6014, see
     * `shift-click-drop-chain`), and it answers by naming the real Drop op
     * through `cc_triggerop`. So every use-on between two carried items sent its
     * OPHELDU and then an IF_BUTTON7 that dropped the target on the floor —
     * "use knife on logs" put the logs on the ground. The flash is cosmetic; the
     * op it fabricated was not. The `OPHELDT_START` arming case below already
     * skips the flash for the same reason.
     */
    if( app->objsel.active )
    {
        APP_NET_SEND(
            app,
            net_out_opheldu(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                obj_id,
                slot,
                com_id,
                app->objsel.obj_id,
                app->objsel.slot,
                app->objsel.component_id));
        app_selection_clear(app);
        return 1;
    }

    if( opt->action == REVCONFIG_MINIMENU_TGT_HELD && app->targetsel.active )
    {
        APP_NET_SEND(
            app,
            net_out_opheldt(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                obj_id,
                slot,
                com_id,
                app_targetsel_wire_component(app)));
        app_selection_clear(app);
        return 1;
    }

    switch( opt->action )
    {
    case REVCONFIG_MINIMENU_OPHELD1:
    case REVCONFIG_MINIMENU_OPHELD2:
    case REVCONFIG_MINIMENU_OPHELD3:
    case REVCONFIG_MINIMENU_OPHELD4:
    case REVCONFIG_MINIMENU_OPHELD5:
        APP_NET_SEND(
            app,
            net_out_opheld(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                opt->action_index + 1,
                obj_id,
                slot,
                com_id));
        /* selectedArea / cc_settrans_temporarily — not for Use (below). */
        app_inv_cell_op_flash(app, com_id, slot, opt->action_index + 1);
        return 1;
    case REVCONFIG_MINIMENU_INV_BUTTON1:
    case REVCONFIG_MINIMENU_INV_BUTTON2:
    case REVCONFIG_MINIMENU_INV_BUTTON3:
    case REVCONFIG_MINIMENU_INV_BUTTON4:
    case REVCONFIG_MINIMENU_INV_BUTTON5:
        APP_NET_SEND(
            app,
            net_out_inv_button(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                opt->action_index + 1,
                obj_id,
                slot,
                com_id));
        app_inv_cell_op_flash(app, com_id, slot, opt->action_index + 1);
        return 1;
    case REVCONFIG_MINIMENU_IF_BUTTON:
        /* Component ops 1..10 on an inventory cell (bank withdraw ladder,
         * farming tools, backpack, …). Rev239's collapsed IF_BUTTONX also
         * carries the object id; without it the server cannot distinguish a
         * held-item operation from a plain component click. method3476 sends
         * only when the effective op bit is armed, though on_op still runs. */
        if( opt->action_index < 0 || opt->action_index >= 10 )
            return 0;
        {
            unsigned events = 0;
            int const op_num = opt->action_index + 1;
            if( !UIIfEventTable_Lookup(&app->if_events, com_id, slot, &events) )
                events = App_IfEventsGetEffective(app, com_id);
            if( events & (1u << op_num) )
            {
                APP_NET_SEND(
                    app,
                    net_out_if_button_obj_op(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        op_num,
                        com_id,
                        slot,
                        obj_id));
            }
        }
        app_inv_cell_op_flash(app, com_id, slot, opt->action_index + 1);
        return 1;
    case REVCONFIG_MINIMENU_OPHELDT_START:
    {
        /* "Use <item>": enter selection mode; the next click targets it.
         * No selectedArea / settrans flash — white outline only. */
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
        app_targetsel_clear(app);
        app->objsel.active = 1;
        app->objsel.obj_id = obj_id;
        app->objsel.slot = slot;
        app->objsel.component_id = com_id;
        /* Precision caps the copy at sizeof(objsel.name)-1 so this is
         * provably in-bounds regardless of obj->name's declared size, rather
         * than relying on snprintf's implicit (GCC-unprovable) truncation. */
        snprintf(
            app->objsel.name,
            sizeof(app->objsel.name),
            "%.*s",
            (int)sizeof(app->objsel.name) - 1,
            obj && obj->name[0] ? obj->name : "item");
        if( getenv("TORIRS_CLICK_DEBUG") )
            TORIRS_LOG("selarm: obj '%s' slot=%d com=0x%x\n", app->objsel.name, slot, com_id);
        app->need_redraw = 1;
        return 1;
    }
    case REVCONFIG_MINIMENU_OPHELD6:
    {
        /* Examine: client-side chat print (no packet). Reference doAction
         * OP_HELD6: huge stacks show the exact count, else the config desc,
         * else the generic fallback. */
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
        char const* name = (obj && obj->name[0]) ? obj->name : "item";
        int count = opt->pick.quaternary_id;
        char line[TORIRS_DESC_MAX + 32];
        if( count >= 100000 )
            snprintf(line, sizeof(line), "%d x %s", count, name);
        else if( obj && obj->desc[0] )
            snprintf(line, sizeof(line), "%s", obj->desc);
        else
            snprintf(line, sizeof(line), "It's a %s.", name);
        RS_CS2Host_ChatAdd(&app->host, RS_CHAT_TYPE_GAME, NULL, NULL, line);
        app->need_redraw = 1;
        return 1;
    }
    default:
        return 0;
    }
}

#include "ui/uitree_inv_view.h"
#include "ui/uitree_scroll.h"

/* Filled item cell under a canvas point, of either shape the tree can express
 * one in — a TYPE_INV grid slot or a rev-230 CS2 item child. Resolution and
 * the obj lookup both live in UITree_ObjCellForNode / InvManager so the drag
 * machine and the right-click builder cannot disagree about which slot was
 * pressed. Returns false when the point is over no filled cell. */
static bool
app_obj_cell_at(
    struct App* app,
    int px,
    int py,
    struct UITreeObjCell* out)
{
    if( !UITree_ObjCellAt(app->tree, &app->ui_host, px, py, out) )
        return false;
    if( out->kind == UITREE_OBJ_CELL_GRID )
    {
        struct InvSlot inv_slot;
        if( !InvManager_GetSlot(&app->invs, out->inv_source_id, out->slot, &inv_slot) )
            return false;
        if( inv_slot.obj_id <= 0 )
            return false;
        out->obj_id = inv_slot.obj_id;
        out->obj_count = inv_slot.obj_count;
    }
    /* IF_SETEVENTS is the rev-230 authority for drag / drop / Use-target bits
     * (deob method5697). Without this, CS2 cells kept a hardcoded can_drag=1. */
    UITree_ObjCellApplyEvents(out, App_IfEventsGetAt(app, out->component_id, out->slot));
    return true;
}

static int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

static int
app_minimenu_use_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

/* Run the default (top) menu row for a click at (x, y) — the reference
 * doAction(menuNumEntries - 1) short-click path. UI rows only (no world
 * pickset): used by the inventory slot machine below, where the click is by
 * construction over a component. */
static void
app_run_default_ui_row(
    struct App* app,
    int click_x,
    int click_y)
{
    struct RS_MinimenuBuildCtx mctx = {
        .tree = app->tree,
        .ui_host = &app->ui_host,
        .provider = app->provider,
        .runner = &app->runner,
        .invs = &app->invs,
        .chat = &app->chat_source,
        .events_for_component = app_minimenu_events_for_component,
        .events_user = app,
        .player_ops = (char const(*)[40])app->player_ops,
        .player_ops_primary = app->player_ops_primary,
        .player_attack_option = app->player_attack_option,
        .npc_attack_option = app->npc_attack_option,
        .attack_option_model = app->features->attack_option_model,
        .world = app->world,
        .world_pickset = NULL,
        .click_in_world = false,
        .locedit_active = app->locedit.visible != 0,
        .mapedit_select_active = app_mapedit_select_active(app),
        .plugin_io_down = app_plugin_io_down(app) != 0,
    };
    struct UIMinimenu scratch;
    int default_idx;

    mctx.selection = app_minimenu_selection(app);
    UIMinimenu_Reset(&scratch);
    scratch.font_id = app->interact.minimenu.font_id;
    RS_Minimenu_Build(&mctx, click_x, click_y, &scratch);
    app_minimenu_stamp_node_identities(app, &scratch);
    app_plugin_menu_build(app, &scratch, 0);
    default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
    /*
     * TORIRS_CLICK_DEBUG=1: the same readout the generic left-click path
     * prints, for the clicks that never reach it.
     *
     * A click on a FILLED item cell is claimed by the drag machine and resolved
     * here instead, so `clickdbg` never sees the one case where the armed
     * selection decides the answer — a spell aimed at an inventory item. The
     * selection is printed with the rows because the difference between "no
     * cast row was built" and "the cast row was built but Wear outranked it" is
     * the whole question, and neither is visible from the outcome.
     */
    if( getenv("TORIRS_CLICK_DEBUG") )
    {
        TORIRS_LOG(
            "invclick: at %d,%d rows=%d default=%d selmode=%d mask=0x%x\n",
            click_x,
            click_y,
            scratch.option_count,
            default_idx,
            (int)mctx.selection.mode,
            (unsigned)mctx.selection.target_mask);
        for( int i = 0; i < scratch.option_count; i++ )
            TORIRS_LOG(
                "  row[%d] '%s' action=%d\n",
                i,
                scratch.options[i].text,
                scratch.options[i].action);
    }
    if( default_idx >= 0 )
    {
        struct UIMinimenu saved = app->interact.minimenu;
        app->interact.minimenu = scratch;
        app_minimenu_use_option(app, default_idx, click_x, click_y);
        app->interact.minimenu = saved;
    }
    else
        /* Cancel-only menu on a FILLED cell, which is what clicking the armed
         * item itself produces: add_inv_slot_select_row skips the arming slot,
         * and while a mode is armed the plain ops are not built, so the menu has
         * no row and nothing ran. The reference still runs doAction on that
         * Cancel row and its tail drops useMode/targetMode — clicking the lit
         * item again is how you cancel. Without this it could never be
         * un-selected by clicking it. */
        app_selection_clear(app);
}

/*
 * A real drag was released at (mx, my): find the destination slot — any
 * container that IF_SETEVENTS armed as a drag target (bit 20), not only the
 * source — and tell the server (deob field804 / method1239 IfButtonD).
 *
 * An empty slot is a legal drop target and is exactly what a filled-cell
 * hit-test cannot see: a TYPE_INV grid has no obj there, and rev 230's paint
 * script hides the child of an empty cell outright. Filled cells go through
 * UITree_ObjCellAt; empty CS2 slots walk each drag-target parent's children.
 *
 * CS1 / dat1 (2004 Client.ts): optimistic InvManager swap then classic
 * INV_BUTTOND with a mode byte (same-container only). CS2 / rev-230 runs
 * onDragComplete synchronously before class108.method3759 sends the dual-
 * endpoint IfButtonD packet. Inventory hooks such as bankside_reorder and
 * interface_inv_dragcomplete_swap_big repaint the two cells from the unchanged
 * local container, making the gesture optimistic without mutating server state.
 */
static int
app_inv_resolve_drop(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int* out_com,
    int* out_slot,
    int* out_obj,
    int32_t* out_node)
{
    struct UITreeObjCell dest;
    int i;

    *out_com = -1;
    *out_slot = -1;
    *out_obj = -1;
    *out_node = -1;

    /* Filled cell under the cursor (any container). */
    if( UITree_ObjCellAt(app->tree, &app->ui_host, mouse_x, mouse_y, &dest) )
    {
        int events = App_IfEventsGetAt(app, dest.component_id, dest.slot);
        UITree_ObjCellApplyEvents(&dest, events);
        /* CS1 / no events: can_drop stays 1 from the grid default. */
        if( dest.can_drop )
        {
            *out_com = dest.component_id;
            *out_slot = dest.slot;
            *out_node = dest.node_index;
            if( dest.kind == UITREE_OBJ_CELL_GRID )
            {
                struct InvSlot inv_slot;
                if( InvManager_GetSlot(&app->invs, dest.inv_source_id, dest.slot, &inv_slot) &&
                    inv_slot.obj_id > 0 )
                    *out_obj = inv_slot.obj_id;
            }
            else if( dest.obj_id > 0 )
            {
                *out_obj = dest.obj_id;
            }
            return 1;
        }
    }

    /* Empty CS2 slots: walk every IF_SETEVENTS entry with bit 20. */
    for( i = 0; i < app->if_events.count; i++ )
    {
        int com = app->if_events.ranges[i].com_id;
        int events = app->if_events.ranges[i].events;
        int32_t parent;
        int32_t node = -1;
        int slot;

        if( (events & UITREE_FLAG_DRAG_ON) == 0 )
            continue;
        parent = app_displayable_component_node(app, com);
        if( parent < 0 )
            continue;
        slot = UITree_ObjCellDynamicSlotNodeAt(app->tree, com, mouse_x, mouse_y, &node);
        if( slot < 0 )
            continue;
        /* Empty dynamic cells are commonly behavior-hidden by their paint
         * script and remain legitimate drop positions, so do not reject that
         * terminal flag.  Their container/ancestors must still be effectively
         * visible, and an explicitly frame-suppressed cell is not a target. */
        if( node < 0 || (uint32_t)node >= app->tree->component_count ||
            app->tree->components[node].freed || app->tree->components[node].frame_hidden ||
            (app->tree->components[node].parent >= 0 &&
             UITree_NodeOrAncestorDisplayHidden(app->tree, app->tree->components[node].parent)) )
            continue;
        /* from/to of -1 mean a plain widget (not a sub-id range). */
        if( app->if_events.ranges[i].from >= 0 && app->if_events.ranges[i].to >= 0 &&
            (slot < app->if_events.ranges[i].from || slot > app->if_events.ranges[i].to) )
            continue;
        *out_com = com;
        *out_slot = slot;
        *out_node = node;
        if( node >= 0 && app->tree->components[node].item_id > 0 )
            *out_obj = app->tree->components[node].item_id;
        return 1;
    }
    return 0;
}

static void
app_inv_drag_drop(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    int to_slot = -1;
    int src_obj = -1;
    int dst_obj = -1;
    int dst_com = -1;
    int32_t src_node = -1;
    int32_t dst_node = -1;

    if( app->inv_drag.source_id >= 0 )
    {
        struct InvSlot inv_slot;
        if( InvManager_GetSlot(
                &app->invs, app->inv_drag.source_id, app->inv_drag.from_slot, &inv_slot) &&
            inv_slot.obj_id > 0 )
            src_obj = inv_slot.obj_id;
    }
    else
    {
        int obj = 0;
        if( UITree_ObjCellDynamicAtSlot(
                app->tree, app->inv_drag.component_id, app->inv_drag.from_slot, &src_node, &obj, NULL) &&
            obj > 0 )
            src_obj = obj;
    }

    if( !app_inv_resolve_drop(app, mouse_x, mouse_y, &dst_com, &to_slot, &dst_obj, &dst_node) )
        return;

    /* Same cell: not a drop. Cross-container with the same slot index is fine. */
    if( dst_com == app->inv_drag.component_id && to_slot == app->inv_drag.from_slot )
        return;

    if( App_UiLogic(app) == APP_UI_LOGIC_CS1 )
    {
        /* 2004 Client.ts: apply locally so the drag feels instant; the
         * server's UPDATE_INV echo repaints either way. Same-container only —
         * cross-container waits on the server. */
        if( dst_com == app->inv_drag.component_id )
        {
            if( app->inv_drag.source_id >= 0 )
            {
                InvManager_SwapSlots(
                    &app->invs, app->inv_drag.source_id, app->inv_drag.from_slot, to_slot);
                RS_CS2Host_NotifyInvChanged(
                    &app->host, InvManager_ContainerForSource(&app->invs, app->inv_drag.source_id));
            }
            else
            {
                UITree_ObjCellDynamicSwap(
                    app->tree, app->inv_drag.component_id, app->inv_drag.from_slot, to_slot);
            }
        }
        APP_NET_SEND(
            app,
            net_out_inv_buttond(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                app->inv_drag.component_id,
                src_obj,
                app->inv_drag.from_slot,
                dst_com,
                dst_obj,
                to_slot,
                0));
        return;
    }

    /* Rev-230: onDragComplete first, then the dual-endpoint packet. The
     * gamepack invokes this ScriptEvent synchronously; use an isolated queue so
     * an unrelated yielding clientscript cannot delay the optimistic redraw
     * until after the server's UPDATE_INV echo. */
    {
        int hook_com = app->inv_drag.component_id;
        struct UITreeRuntimeScriptHook const* hook = NULL;
        int32_t hook_idx = src_node;
        int bx = 0, by = 0, bw = 0, bh = 0;
        int offx = 0, offy = 0;
        int32_t parent_idx;

        if( hook_idx < 0 )
            hook_idx = UITree_FindByComponentId(app->tree, app->inv_drag.component_id);
        if( hook_idx >= 0 )
        {
            hook = &UITree_Hooks(&app->tree->components[hook_idx])->on_drag_complete;
            hook_com = app->tree->components[hook_idx].component_id;
            if( hook->script_id <= 0 )
            {
                /* Fall back to the container: some paint scripts put the hook
                 * on the parent layer rather than every CC_CREATE child. */
                parent_idx = UITree_FindByComponentId(app->tree, app->inv_drag.component_id);
                if( parent_idx >= 0 )
                {
                    hook = &UITree_Hooks(&app->tree->components[parent_idx])->on_drag_complete;
                    hook_com = app->inv_drag.component_id;
                }
            }
        }

        parent_idx = UITree_FindByComponentId(app->tree, app->inv_drag.component_id);
        if( parent_idx >= 0 )
        {
            UITree_LayoutGetBounds(&app->tree->components[parent_idx].position, &bx, &by, &bw, &bh);
            UITree_AccumScrollOffset(app->tree, parent_idx, &offx, &offy);
        }

        if( hook && hook->script_id > 0 )
        {
            struct UITreeRuntimeScriptHook hook_copy;
            struct ToriRS_TaskQueue* drag_queue = ToriRS_TaskQueue_New();
            struct TaskRunner drag_runner = {
                .queue = drag_queue,
                .io = app->runner.io,
                .px = app->runner.px,
            };
            int target_id = dst_com;
            UITree_HookInitCopy(&hook_copy, hook);
            if( dst_node >= 0 )
                target_id = app->tree->components[dst_node].component_id;
            RS_CS2_SetEventOp(&app->host, 1, 0);
            RS_CS2_SetEventMouse(&app->host, mouse_x - (bx - offx), mouse_y - (by - offy));
            RS_CS2_SetEventDragTarget(&app->host, app->tree, target_id);
            RS_CS2_DispatchHook(&app->host, &drag_runner, hook_com, &hook_copy);
            TaskRunner_Drain(&drag_runner);
            ToriRS_TaskQueue_Free(drag_queue);
            UITree_HookClear(&hook_copy);
            app->need_redraw = 1;
        }
    }

    APP_NET_SEND(
        app,
        net_out_inv_buttond(
            app->net->rev,
            app->net->random_out,
            _nsbuf,
            sizeof(_nsbuf),
            app->inv_drag.component_id,
            src_obj,
            app->inv_drag.from_slot,
            dst_com,
            dst_obj,
            to_slot,
            0));
}

/* A press has become a real drag once it clears the deadzone and the dead
 * time — the same test the release branch uses to choose swap over click, so
 * the ghost and the swap can never disagree about what a gesture was. */
static int
app_inv_drag_promoted(struct App const* app)
{
    return UIInvDrag_Promoted(&app->inv_drag) ? 1 : 0;
}

/* The drag latch stores the server-facing parent component id.  Re-resolve it
 * every frame, then (for CS2 dynamic cells) also re-resolve the concrete slot
 * child: a plugin layout can hide either level after mouse-down. */
static int
app_inv_drag_source_live(struct App const* app)
{
    struct UITreeComponent const* armed;
    int32_t parent;

    if( app->inv_drag.component_id < 0 || !app->tree || app->inv_drag.node_index < 0 ||
        (uint32_t)app->inv_drag.node_index >= app->tree->component_count )
        return 0;
    armed = &app->tree->components[app->inv_drag.node_index];
    if( armed->freed || app->inv_drag.node_incarnation == 0 ||
        armed->incarnation != app->inv_drag.node_incarnation ||
        UITree_NodeOrAncestorDisplayHidden(app->tree, app->inv_drag.node_index) )
        return 0;
    parent = app_displayable_component_node(app, app->inv_drag.component_id);
    if( parent < 0 )
        return 0;
    if( app->inv_drag.source_id >= 0 )
    {
        struct InvSlot slot;
        return app->inv_drag.obj_id > 0 &&
               InvManager_GetSlot(
                   &app->invs, app->inv_drag.source_id, app->inv_drag.from_slot, &slot) &&
               slot.obj_id == app->inv_drag.obj_id;
    }
    {
        int32_t node = -1;
        int obj = 0;
        if( !UITree_ObjCellDynamicAtSlot(
                app->tree, app->inv_drag.component_id, app->inv_drag.from_slot, &node, &obj, NULL) ||
            obj <= 0 || obj != app->inv_drag.obj_id ||
            UITree_NodeOrAncestorDisplayHidden(app->tree, node) )
            return 0;
    }
    return 1;
}

static void
app_inv_drag_cancel(struct App* app)
{
    UIInvDrag_Reset(&app->inv_drag);
    /* Releasing the gesture also releases the tree's suppression of the
     * generic node drag, which is the tree's state and not the gesture's. */
    if( app->tree )
        app->tree->anti_drag = 0;
    app->need_redraw = 1;
}

/*
 * Whether emit should ghost the armed slot at trans 128.
 *
 * From the *press*, not from the promotion. The reference sets `objDragArea`
 * on the mouse-down that arms the cell and the inventory draw fades that one
 * icon for as long as it is non-zero (Client.ts:8589 arms, :10207 fades); the
 * deadzone and the dead-time only zero the (dx, dy) the faded icon is drawn
 * at, so a press that never moves still fades in place and un-fades on
 * release. Waiting for promotion here is what "items don't dim when the left
 * button is down" was.
 *
 * Gated on `can_drag` for the same reason the reference gates arming on
 * `com.objSwap || com.objReplace`: a container that cannot be rearranged shows
 * no drag feedback at all. At IF3 that flag is the IF_SETEVENTS drag depth, so
 * a worn slot stays solid while a backpack or bank cell fades.
 */
static int
app_inv_drag_ghosting(struct App const* app)
{
    enum UIInvDragLane const lane = App_UiLogic(app) == APP_UI_LOGIC_CS1
                                        ? UI_INV_DRAG_LANE_UNIFORM
                                        : UI_INV_DRAG_LANE_PER_CELL;
    return UIInvDrag_Ghosting(&app->inv_drag, lane) ? 1 : 0;
}

/* Inventory slot press/drag/click (reference objDrag* machine, Client.ts
 * mouseLoop 8584 + gameLoop 2476 for CS1; rev-230 Deobfuscator class415 for
 * CS2):
 *  - left press over a FILLED slot arms; nothing fires on the down edge.
 *  - each held cycle: cycles++, >5px of travel sets the grab threshold, and
 *    the emit offset dx/dy is recomputed (+-5px deadzone, zero before 5
 *    cycles) so a promoted drag icon follows the mouse at trans 128.
 *  - release: real drag (app_inv_drag_promoted) resolves the slot under
 *    the mouse — CS1 does an optimistic local swap + classic INV_BUTTOND;
 *    CS2 fires onDragComplete then the dual-endpoint IfButtonD and waits
 *    for the server echo (no local item mutation). Anything else is a
 *    SHORT CLICK and runs the default menu row (how a left click submits
 *    OPHELD*).
 * Ghosting: from arm time on both logics, for any cell the interface says may
 * be rearranged — see app_inv_drag_ghosting. While armed the generic node drag is suppressed
 * (tree->anti_drag; the reference freezes mouseLoop/buildMinimenu during
 * objDragArea != 0) so the grid's ancestors can never pick the press up as
 * a whole-panel drag. */
static void
app_inv_drag_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed)
{
    int mx = input->curr.mouse_x;
    int my = input->curr.mouse_y;

    /* Never arm while the right-click popup is open: its option rows overlap
     * the grid and the reference routes those clicks through the menu first. */
    if( !UIInvDrag_Armed(&app->inv_drag) && !pointer_consumed &&
        !app->interact.minimenu.visible && LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
    {
        struct UITreeObjCell cell;
        if( app_obj_cell_at(app, mx, my, &cell) )
        {
            struct UITreeComponent const* node =
                (cell.node_index >= 0 && (uint32_t)cell.node_index < app->tree->component_count)
                    ? &app->tree->components[cell.node_index]
                    : NULL;
            struct UIInvDragPress press;
            press.component_id = cell.component_id;
            press.node_index = cell.node_index;
            press.node_incarnation = node ? node->incarnation : 0;
            press.can_drag = cell.can_drag != 0;
            press.slot = cell.slot;
            press.inv_source_id = cell.inv_source_id;
            press.obj_id = cell.obj_id;
            press.dead_zone = node ? node->drag_dead_zone : 0;
            press.dead_time = node ? node->drag_dead_time : 0;
            UIInvDrag_Arm(&app->inv_drag, &press, mx, my);
            /* Both logics fade the armed cell on the down edge, so the frame
             * that arms is a frame that changed. */
            app->need_redraw = 1;
        }
    }

    if( !UIInvDrag_Armed(&app->inv_drag) )
    {
        if( app->tree )
            app->tree->anti_drag = 0;
        return;
    }
    if( !app_inv_drag_source_live(app) )
    {
        /* display:none during a held gesture cancels ownership.  In
         * particular, release must not turn into a default menu action or a
         * packet against a component which is no longer painted. */
        app_inv_drag_cancel(app);
        return;
    }
    if( app->tree )
        app->tree->anti_drag = 1;

    if( !input->curr.mouse_button_up[TORIRSM_LEFT] &&
        LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
    {
        if( UIInvDrag_Hold(&app->inv_drag, mx, my) )
            app->need_redraw = 1;
        return;
    }

    /* Released. */
    if( app_inv_drag_promoted(app) )
        app_inv_drag_drop(app, mx, my);
    else
    {
        app_run_default_ui_row(app, mx, my);
    }
    app_inv_drag_cancel(app);
}

/* A right-click menu is retained across frames, so its component rows must be
 * validated again when selected.  Component ids survive array realloc but not
 * deletion, and effective visibility may change while the popup is open. */
static int
app_minimenu_ui_pick_live(
    struct App const* app,
    struct UIMinimenuPick const* pick)
{
    int32_t idx;

    if( pick->kind != UI_MINIMENU_PICK_UI && pick->kind != UI_MINIMENU_PICK_INV_SLOT )
        return 1;
    if( pick->has_node_identity )
    {
        if( !UITree_MenuPickCurrent(app->tree, pick) )
            return 0;
        /*
         * `pick->id` names the stamped node itself only for a UI pick.
         *
         * An INV_SLOT pick carries the CONTAINER's id (pick_inv_slot is handed
         * UITreeObjCell::component_id, which is dynamic_parent_component_id)
         * while node_index is the cell inside it -- and a rev-239 item cell is
         * a CC_CREATEd dynamic child that UITree_CcCreate gave a component id
         * of its own out of UITree_AllocateDynamicComponentId. The two never
         * match, so this equality rejected every dynamic inventory row: the
         * left-click default op returned before it ran, and one dead row in an
         * open popup made app_minimenu_close_if_stale hide the whole menu, so
         * a right click on the backpack drew nothing at all.
         *
         * The INV_SLOT kinds are identified below instead, on their own terms:
         * re-resolve (container, slot) and require the same node back.
         */
        if( pick->kind == UI_MINIMENU_PICK_UI && pick->id >= 0 &&
            app->tree->components[pick->node_index].component_id != pick->id )
            return 0;
        if( UITree_NodeOrAncestorDisplayHiddenEx(
                app->tree, pick->node_index, pick->allow_frame_hidden) )
            return 0;
        idx = pick->node_index;
    }
    else
    {
        /* Some authored builtin/client rows have no component id. They have no
         * stale node identity to validate; preserve their local action path. */
        if( pick->id < 0 )
            return 1;
        idx = app_displayable_component_node(app, pick->id);
        if( idx < 0 )
            return 0;
    }
    if( pick->has_native_events )
    {
        unsigned current =
            pick->kind == UI_MINIMENU_PICK_INV_SLOT
                ? App_IfEventsGetAt(app, pick->id, pick->secondary_id)
                : App_IfEventsGetEffective(app, app->tree->components[idx].component_id);
        if( current != pick->native_events )
            return 0;
    }
    if( pick->kind == UI_MINIMENU_PICK_INV_SLOT &&
        app->tree->components[idx].type == UIELEM_RS_INV )
    {
        struct InvSlot slot;
        if( pick->has_node_identity && pick->node_index != idx )
            return 0;
        if( !InvManager_GetSlot(
                &app->invs,
                app->tree->components[idx].u.rs_inv.inv_source_id,
                pick->secondary_id,
                &slot) ||
            slot.obj_id <= 0 || (pick->tertiary_id > 0 && slot.obj_id != pick->tertiary_id) )
            return 0;
    }
    else if( pick->kind == UI_MINIMENU_PICK_INV_SLOT )
    {
        int32_t cell = -1;
        int obj = 0;
        if( !UITree_ObjCellDynamicAtSlot(
                app->tree, pick->id, pick->secondary_id, &cell, &obj, NULL) ||
            obj <= 0 || UITree_NodeOrAncestorDisplayHidden(app->tree, cell) )
            return 0;
        if( pick->has_node_identity && pick->node_index != cell )
            return 0;
        /* Do not execute an old item's row on a new item which was painted
         * into the same dynamic slot while the menu was open. */
        if( pick->tertiary_id > 0 && obj != pick->tertiary_id )
            return 0;
    }
    else if( pick->has_node_identity && pick->node_index != idx )
        return 0;
    return 1;
}

static void
app_minimenu_close_if_stale(struct App* app)
{
    struct UIMinimenu* menu = &app->interact.minimenu;

    if( !menu->visible )
        return;
    for( int i = 0; i < menu->option_count; i++ )
    {
        if( app_minimenu_ui_pick_live(app, &menu->options[i].pick) )
            continue;
        UIMinimenu_Hide(menu);
        app->need_redraw = 1;
        return;
    }
}

static int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y)
{
    struct UIMinimenu* menu = &app->interact.minimenu;
    struct UIMinimenuOption opt;

    if( option_index < 0 || option_index >= menu->option_count )
        return 0;
    opt = menu->options[option_index];
    UIMinimenu_Hide(menu);
    app->need_redraw = 1;

    /* Reference doAction has no CANCEL branch at all: dismissing the menu is
     * not an interaction and must not disturb a running cross. */
    if( opt.action == REVCONFIG_MINIMENU_CANCEL )
        return 0;

    /* The menu model outlives the visibility snapshot which built it.  A row
     * belonging to a now-suppressed/deleted widget only dismisses the popup;
     * it must not paint a cross, reach a plugin, run a hook, or send a packet. */
    if( !app_minimenu_ui_pick_live(app, &opt.pick) )
        return 0;

    {
        /*
         * Every op row paints its cross here, unconditionally, exactly like
         * the reference (Client-TS doAction sets crossMode = 2 in each branch
         * before the packet goes out).
         *
         * A WALK row does NOT. The reference's walk branch touches no cross at
         * all — it only re-arms the hittest — and the cross is set one frame
         * later, inside the block that emits the move, so a walk that resolves
         * to nothing leaves the screen alone. Deob client.java:9305 puts the
         * two in the same basic block, guarded by class112.method3951():
         *
         *     field760 = ...; field714 = ...;   // cross x, y
         *     field800 = 1282583357;            // crossMode 1
         *     field910 = 0;                     // crossCycle
         *
         * Painting it up front here is what made a dead click on the sky look
         * like an executed one. UI_MINIMENU_PICK_TERRAIN shows it on success.
         */
        enum UICrossMode cross_mode = RS_Minimenu_CrossModeForAction(opt.action);
        if( cross_mode != UI_CROSS_OFF && cross_mode != UI_CROSS_WALK )
        {
            UICross_Show(&app->cross, cross_mode, click_x, click_y);
            /* Same answer, applied to the marker that is already running.
             * Inside the guard, not beside it: a row that shows no cross has
             * decided nothing about what the press meant, and the marker is
             * already yellow -- "a touch happened" -- from the press itself. */
            UIInk_SetColour(
                &app->ink,
                cross_mode == UI_CROSS_INTERACT ? TORIRS_INKWELL_RED : TORIRS_INKWELL_YELLOW);
        }
    }

    /* method5229 marks component operations above targetPriority with the
     * +2000 low-priority form. doAction removes that bias before comparing
     * the action id; keeping it made the row render correctly but bypass every
     * inventory/UI dispatch case (for example Deposit-1 arrived as 2231
     * instead of IF_BUTTON 231). */
    opt.action = UIMinimenu_ActionNormalize(opt.action);

    /*
     * The plugins get the row before the engine acts on it.
     *
     * After the cross and the normalize, so a handler sees the action id the
     * dispatcher below will switch on; before every branch of that dispatch,
     * so CONSUME actually suppresses the behaviour rather than racing it. For
     * a plugin-owned row there is no engine behaviour to fall through to and
     * this always returns 1 -- which is what makes menu_add a complete feature
     * rather than a row that draws and does nothing.
     */
    if( app->plugins )
    {
        struct ToriRS_MenuRow row;
        memset(&row, 0, sizeof(row));
        row.text = opt.text;
        row.action = opt.action;
        row.pick_kind = (int)opt.pick.kind;
        row.npc_slot = -1;
        row.player_pid = -1;
        row.target_id = -1;
        if( opt.pick.kind == UI_MINIMENU_PICK_NPC && app->world )
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc )
                row.npc_slot = npc->server_slot;
            row.target_id = opt.pick.secondary_id;
        }
        else if( opt.pick.kind == UI_MINIMENU_PICK_PLAYER )
            row.player_pid = opt.pick.secondary_id;
        else if(
            opt.pick.kind == UI_MINIMENU_PICK_SCENERY || opt.pick.kind == UI_MINIMENU_PICK_OBJ )
            row.target_id = opt.pick.secondary_id;

        if( PluginHost_MenuSelect(app->plugins, &row, click_x, click_y) )
            return 1;
    }

    /* A PASS subscriber may still synchronously rebuild or suppress the row's
     * target. Revalidate after returning from plugin code before native action
     * can transfer the retained row to a new same-id occupant. */
    if( !app_minimenu_ui_pick_live(app, &opt.pick) )
        return 0;

    /* A cache-installed client op. After the plugins, so a plugin may still
     * veto one; before every engine branch, because there is no engine
     * behaviour behind this action id to fall through to. */
    if( opt.action == RS_MINIMENU_ACTION_CLIENTOP )
        return app_clientop_run(app, &opt);

    /*
     * Examine (OPLOC6/OPNPC6/OPOBJ6): resolved locally from the config desc, no
     * packet (Client-TS doAction OP_LOC6/OP_NPC6/OP_OBJ6). Look the desc up from
     * the config by the entity's type id, and fall back to "It's a <name>."
     * when the config carries none. Must run before the pick.kind switch or the
     * scenery/NPC cases would mis-send OPLOC1/OPNPC1.
     *
     * Where the text comes from is worth stating, because the two halves differ.
     * An OBJ record states its own examine in every era. An npc's and a loc's
     * were retired from Jagex's configs in 2006 -- rev-239 OldSchool sends those
     * two from the server (deob: menu ops 1002/1004/1013 each build a packet and
     * print what comes back), so `cache.osrs239` states not one of them. This
     * client answers Examine off the record, so the text is carried in the
     * content pack instead, under the same opcode 3 the field has always had:
     * tools/import_examine.py writes configs/examine.{npc,loc} and the bake puts
     * them in the npc and loc archives. A pristine cache therefore still shows
     * the fallback, and a content-baked one does not.
     */
    if( opt.action == REVCONFIG_MINIMENU_OPLOC6 || opt.action == REVCONFIG_MINIMENU_OPNPC6 ||
        opt.action == REVCONFIG_MINIMENU_OPOBJ6 )
    {
        char const* name = NULL;
        char const* desc = NULL;
        char line[TORIRS_DESC_MAX + 32];
        if( app->world && opt.action == REVCONFIG_MINIMENU_OPLOC6 )
        {
            /* A deck loc's record is in its view's world (C5.2). */
            struct World* loc_world =
                opt.pick.view_id != 0 ? app_minimenu_view_world(app, opt.pick.view_id) : app->world;
            struct WorldEntity_Scenery* scenery =
                loc_world ? World_SceneryGetByElementId(loc_world, opt.pick.id) : NULL;
            if( scenery )
            {
                struct ToriRS_Location* loc =
                    CacheProvider_LocationGet(app->provider, scenery->loc_id);
                if( loc && loc->desc[0] )
                    desc = loc->desc;
                if( scenery->info->name[0] )
                    name = scenery->info->name;
            }
        }
        else if( app->world && opt.action == REVCONFIG_MINIMENU_OPOBJ6 )
        {
            struct WorldEntity_ObjStack* stack =
                World_ObjStackGetByElementId(app->world, opt.pick.id);
            if( stack )
            {
                struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, stack->obj_id);
                if( obj && obj->desc[0] )
                    desc = obj->desc;
                if( stack->name[0] )
                    name = stack->name;
            }
        }
        else if( app->world )
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc )
            {
                struct ToriRS_Npctype* npctype =
                    CacheProvider_NpctypeGet(app->provider, npc->npc_id);
                if( npctype && npctype->desc[0] )
                    desc = npctype->desc;
                if( npc->name[0] )
                    name = npc->name;
            }
        }
        if( desc )
            snprintf(line, sizeof(line), "%s", desc);
        else
            snprintf(line, sizeof(line), "It's a %s.", name ? name : "mystery");
        RS_CS2Host_ChatAdd(&app->host, RS_CHAT_TYPE_GAME, NULL, NULL, line);
        app->need_redraw = 1;
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /*
     * "Manage Plugins": open the plugin window.
     *
     * A profile authors this row -- `op0_action=PLUGIN_PANEL` on the
     * `manage_plugins_button` it places in the logout tab -- so the CLIENT
     * never has to know which gameframe it is running on. The lanes whose
     * frame comes out of a cache and cannot be authored get the same plate
     * built for them instead (app_plugin_button_sync), and answer their own
     * click; this is where the authored ones arrive.
     *
     * Before the pick.kind switch, like the client rows below it: there is no
     * engine behaviour behind this action id to fall through to, and a
     * component-kind pick carrying it would otherwise be read as an IF_BUTTON.
     */
    if( opt.action == RS_MINIMENU_ACTION_PLUGIN_PANEL )
    {
        if( getenv("TORIRS_CHROME_DEBUG") )
            fprintf(stderr, "chrome: Manage Plugins minimenu action\n");
        app_plugin_window_set_open(app, !app->plugin_panel_visible);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /*
     * A privacy button's mode, named outright.
     *
     * Beside the row above and for its reason: there is no engine behaviour
     * behind this id to fall through to, and a UI-kind pick carrying it would
     * be read as an IF_BUTTON and press whatever component id it names.
     */
    if( opt.action == RS_MINIMENU_ACTION_CHAT_FILTER )
    {
        if( RS_UISlots_SetChatFilter(&app->slots, opt.pick.secondary_id, opt.pick.tertiary_id) )
            app->need_redraw = 1;
        return 0;
    }

    /*
     * A plugin canvas region's row -- an orb, a bar, anything a plugin drew on
     * the canvas and claimed.
     *
     * `action_index` is the region's index in this frame's list. Re-checked
     * against the count rather than trusted, because the row outlives the
     * build that made it by a click: a menu opened on one frame is chosen from
     * on a later one, and in between a plugin can have been switched off and
     * its regions cleared.
     */
    if( opt.action == RS_MINIMENU_ACTION_PLUGIN_WIDGET )
    {
        int32_t node = opt.pick.node_index;
        if( opt.pick.has_node_identity && node >= 0 && (uint32_t)node < app->tree->component_count )
        {
            struct UITreeComponent const* c = &app->tree->components[node];
            PluginHost_WidgetOperation(
                app->plugins,
                c->plugin_owner,
                app_widget_ref(app->tree, node),
                c->plugin_op_serial);
        }
        return 0;
    }

    /* Loc editor "Select" row (rs_minimenu_world.c add_scenery_rows, gated on
     * locedit_active): entirely client-side, same shape as the Examine
     * intercept above -- must also run before the pick.kind switch, or a
     * scenery-kind pick with this action would fall into the
     * UI_MINIMENU_PICK_SCENERY case and mis-send an OPLOC. The `locedit_visible`
     * re-check guards a row clicked in the one frame the tool closed on. */
    if( opt.action == RS_MINIMENU_ACTION_LOCEDIT_SELECT )
    {
        if( app->locedit.visible )
            app_loc_editor_select_element(app, opt.pick.id);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /* Its ground twin. The pick already carries the exact tile the terrain hit
     * named — secondary/tertiary are scene x/z and quaternary the mesh level —
     * so nothing is re-derived here and the panel cannot describe a different
     * tile than the row that was clicked. */
    if( opt.action == RS_MINIMENU_ACTION_LOCEDIT_SELECT_TERRAIN )
    {
        if( app->locedit.visible )
            app_loc_editor_select_terrain(
                app, opt.pick.secondary_id, opt.pick.tertiary_id, opt.pick.quaternary_id);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /* Map editor SELECT tool's pair of the two rows above -- same shape, a
     * different panel's latch. The `app_mapedit_select_active` re-check
     * guards a row clicked in the one frame the tool closed or switched off
     * SELECT. */
    if( opt.action == RS_MINIMENU_ACTION_MAPEDIT_SELECT )
    {
        if( app_mapedit_select_active(app) )
            Editor_PanelSelectLoc(&app->editor_panel, app, opt.pick.id);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    if( opt.action == RS_MINIMENU_ACTION_MAPEDIT_SELECT_TERRAIN )
    {
        if( app_mapedit_select_active(app) )
            Editor_PanelSelectTerrain(
                &app->editor_panel,
                app,
                opt.pick.secondary_id,
                opt.pick.tertiary_id,
                opt.pick.quaternary_id);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /* Arm target mode from a spell/prayer button (reference TGT_BUTTON): the
     * next click on a valid target casts. objsel and targetsel are mutually
     * exclusive. */
    if( opt.action == REVCONFIG_MINIMENU_TGT_BUTTON )
    {
        int32_t idx = UITree_FindByComponentId(app->tree, opt.pick.id);
        app->objsel.active = 0;
        app_targetsel_clear(app);
        app->targetsel.active = 1;
        app->targetsel.component_id = opt.pick.id;
        app->targetsel.mask = 0;
        app->targetsel.op[0] = '\0';
        if( idx >= 0 )
        {
            struct UITreeComponent const* node = &app->tree->components[idx];
            /* The EFFECTIVE mask, not the decoded one: the row that was just
             * clicked was built from the server's IF_SETEVENTS declaration
             * (rs_minimenu_build's component_effective_target_mask), so the arm
             * must read the same number or a script-built target button arms
             * with mask 0 and every world row is refused. */
            app->targetsel.mask = app_component_target_mask(app, opt.pick.id);
            /* targetsel.op only feeds the "Cast <spell> on ..." prompt text
             * built below. Each %s below is precision-capped so the sum of
             * parts is provably within sizeof(targetsel.op), rather than
             * relying on snprintf's implicit (GCC-unprovable) truncation. */
            if( node->behavior.button_type == REVCONFIG_BUTTON_TYPE_TARGET )
            {
                /* Classic targetOp = "<verb-prefix> <base> <verb-suffix>", the
                 * verb split on its first space (Client-TS TGT_BUTTON). */
                char const* verb = UITree_MenuOptions(node)->target_verb;
                char const* base = UITree_MenuOptions(node)->target_base;
                char const* space = strchr(verb, ' ');
                if( space )
                {
                    int prefix_len = (int)(space - verb);
                    if( prefix_len > 20 )
                        prefix_len = 20;
                    snprintf(
                        app->targetsel.op,
                        sizeof(app->targetsel.op),
                        "%.*s %.*s%.*s",
                        prefix_len,
                        verb,
                        20,
                        base,
                        20,
                        space);
                }
                else
                    snprintf(
                        app->targetsel.op,
                        sizeof(app->targetsel.op),
                        "%.*s %.*s",
                        31,
                        verb,
                        31,
                        base);
            }
            else
            {
                /* IF3 (deob method523 + the world rows that read it back): the
                 * verb is whole, its base is the component's opBase — IF3 has no
                 * targetText — and the target's own name is joined on with an
                 * arrow rather than a verb suffix, so a row reads
                 * "Cast <col>Wind Strike</col> -> <col>Goblin</col>". Built into
                 * one string here because that is the shape the world/inventory
                 * row builders append the target's name to.
                 *
                 * An empty opBase is the verb ALONE, not "verb + a space" —
                 * the same rule add_if3_target_op_rows applies when it builds
                 * the row. A cc_create'd button (sailing's "Edit navigator")
                 * has no op text at all, and "Edit-navigator  -> Deckhand"
                 * with the hole still in it is what reading the empty string
                 * as a word looks like. */
                if( UITree_MenuOptions(node)->option[0] == '\0' )
                    snprintf(
                        app->targetsel.op,
                        sizeof(app->targetsel.op),
                        "%.*s ->",
                        29,
                        UITree_MenuOptions(node)->target_verb);
                else
                    snprintf(
                        app->targetsel.op,
                        sizeof(app->targetsel.op),
                        "%.*s %.*s ->",
                        29,
                        UITree_MenuOptions(node)->target_verb,
                        29,
                        UITree_MenuOptions(node)->option);
            }
        }
        if( getenv("TORIRS_CLICK_DEBUG") )
            TORIRS_LOG(
                "selarm: tgt com=0x%x wire=0x%x mask=0x%x op='%s'\n",
                app->targetsel.component_id,
                app_targetsel_wire_component(app),
                (unsigned)app->targetsel.mask,
                app->targetsel.op);
        app_targetsel_dispatch_hook(app, 1);
        app->need_redraw = 1;
        return 1;
    }

    /* "Use <held> with <world target>" (reference USEHELD_ONLOC/NPC/OBJ) and
     * "<spell> <world target>" (TGT_LOC/NPC/OBJ). Both read the world pick and
     * the armed selection; handled here so the pick.kind switch stays the
     * plain-op path. */
    if( app->world &&
        (opt.action == REVCONFIG_MINIMENU_USEHELD_ONLOC ||
         opt.action == REVCONFIG_MINIMENU_USEHELD_ONNPC ||
         opt.action == REVCONFIG_MINIMENU_USEHELD_ONOBJ ||
         opt.action == REVCONFIG_MINIMENU_USEHELD_ONPLAYER ||
         opt.action == REVCONFIG_MINIMENU_TGT_LOC || opt.action == REVCONFIG_MINIMENU_TGT_NPC ||
         opt.action == REVCONFIG_MINIMENU_TGT_OBJ || opt.action == REVCONFIG_MINIMENU_TGT_PLAYER) )
    {
        int abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        int abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        int deck_loc = opt.pick.view_id != 0 && (opt.action == REVCONFIG_MINIMENU_USEHELD_ONLOC ||
                                                 opt.action == REVCONFIG_MINIMENU_TGT_LOC);
        if( deck_loc )
        {
            if( !WorldviewRegistry_IsLive(&app->worldviews, opt.pick.view_id) || !app->net )
                return 0;
            const struct Worldview* view =
                WorldviewRegistry_Get(&app->worldviews, opt.pick.view_id);
            abs_x = opt.pick.tertiary_id + view->base_x;
            abs_z = opt.pick.quaternary_id + view->base_z;
        }
        switch( opt.action )
        {
        case REVCONFIG_MINIMENU_USEHELD_ONLOC:
            /* Root locs use the ordinary client route. A deck loc is in a
             * different collision map; the server routes that interaction,
             * using the picked live view's coordinates as plain OPLOC does. */
            if( !deck_loc && !app_try_move_loc(
                                 app,
                                 opt.pick.id,
                                 opt.pick.tertiary_id,
                                 opt.pick.quaternary_id,
                                 app->ctrl_held) )
                break;
            APP_NET_SEND(
                app,
                net_out_oplocu(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    opt.pick.secondary_id,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            break;
        case REVCONFIG_MINIMENU_TGT_LOC:
            if( !deck_loc && !app_try_move_loc(
                                 app,
                                 opt.pick.id,
                                 opt.pick.tertiary_id,
                                 opt.pick.quaternary_id,
                                 app->ctrl_held) )
                break;
            APP_NET_SEND(
                app,
                net_out_oploct(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    opt.pick.secondary_id,
                    app_targetsel_wire_component(app)));
            break;
        case REVCONFIG_MINIMENU_USEHELD_ONOBJ:
            app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id, app->ctrl_held);
            APP_NET_SEND(
                app,
                net_out_opobju(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    opt.pick.secondary_id,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            break;
        case REVCONFIG_MINIMENU_TGT_OBJ:
            app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id, app->ctrl_held);
            APP_NET_SEND(
                app,
                net_out_opobjt(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    opt.pick.secondary_id,
                    app_targetsel_wire_component(app)));
            break;
        case REVCONFIG_MINIMENU_USEHELD_ONNPC:
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc && npc->server_slot >= 0 )
            {
                app_try_move_npc(app, npc, app->ctrl_held);
                APP_NET_SEND(
                    app,
                    net_out_opnpcu(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        npc->server_slot,
                        app->objsel.obj_id,
                        app->objsel.slot,
                        app->objsel.component_id));
            }
            break;
        }
        case REVCONFIG_MINIMENU_TGT_NPC:
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc && npc->server_slot >= 0 )
            {
                app_try_move_npc(app, npc, app->ctrl_held);
                APP_NET_SEND(
                    app,
                    net_out_opnpct(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        npc->server_slot,
                        app_targetsel_wire_component(app)));
            }
            break;
        }
        case REVCONFIG_MINIMENU_USEHELD_ONPLAYER:
        {
            struct WorldEntity_Player* player = World_PlayerGetByElementId(app->world, opt.pick.id);
            if( player && player->server_pid >= 0 && player->server_pid != app->world->local_pid )
            {
                app_try_move_player(app, player, app->ctrl_held);
                APP_NET_SEND(
                    app,
                    net_out_opplayeru(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        player->server_pid,
                        app->objsel.obj_id,
                        app->objsel.slot,
                        app->objsel.component_id));
            }
            break;
        }
        case REVCONFIG_MINIMENU_TGT_PLAYER:
        {
            struct WorldEntity_Player* player = World_PlayerGetByElementId(app->world, opt.pick.id);
            /* The row was BUILT from this same pick, so "the menu offered it
             * and the click sent nothing" can only be one of these three
             * numbers — and none of them is visible from outside. */
            if( getenv("TORIRS_CLICK_DEBUG") )
                TORIRS_LOG(
                    "clickdbg: tgt player elem=0x%x found=%d pid=%d local=%d wire=0x%x\n",
                    opt.pick.id,
                    player ? 1 : 0,
                    player ? player->server_pid : -1,
                    app->world->local_pid,
                    app_targetsel_wire_component(app));
            if( player && player->server_pid >= 0 && player->server_pid != app->world->local_pid )
            {
                app_try_move_player(app, player, app->ctrl_held);
                APP_NET_SEND(
                    app,
                    net_out_opplayert(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        player->server_pid,
                        app_targetsel_wire_component(app)));
            }
            break;
        }
        default:
            break;
        }
        /* The selection was just consumed by the target it was aimed at. */
        app_selection_clear(app);
        return 0;
    }

    switch( opt.pick.kind )
    {
    case UI_MINIMENU_PICK_UI:
    case UI_MINIMENU_PICK_INV_SLOT:
    {
        int32_t idx = UITree_FindByComponentId(app->tree, opt.pick.id);
        struct UITreeRuntimeScriptHook const* hook;
        struct UITreeRuntimeScriptHook hook_copy;
        int hook_com_id = -1;

        if( idx < 0 )
            return 0;
        /*
         * An inventory op is the server's to answer, so it is sent before any
         * hook is looked at. It used to be the fallback for "this component
         * has no CS2 hook", which held only for the backpack: rev 230's worn
         * slots DO carry an onop hook (the container owns the "Remove" verb),
         * and resolving it first meant clicking Remove ran a script and sent
         * nothing — the helmet stayed on.
         */
        /* Whatever app_minimenu_inv_action claimed — a packet sent, an
         * Examine printed, a "Use" armed — is the whole of this click. The
         * container's own hook must not also fire: rev 230's worn slots carry
         * an onop script beside their "Remove" verb, and running it instead of
         * sending left the helmet on. */
        if( opt.pick.kind == UI_MINIMENU_PICK_INV_SLOT && app_minimenu_inv_action(app, &opt) )
            return 0;
        /*
         * A numbered op on a plain IF3 widget goes to the server as IF_BUTTON<n>
         * *and* runs the local onop hook — both, not either. The world map orb
         * is the clearest case: its onop is `opsound(...)`, nothing more, and
         * the whole of "Open World Map" happens server-side. The events mask is
         * the gate (bit n enables op n), so a component the server never armed
         * stays purely client-side, which is what rev 230 means by "no
         * clickable-by-default".
         */
        {
            int if_button_sent = 0;
            if( opt.pick.kind == UI_MINIMENU_PICK_UI && opt.action_index >= 0 &&
                opt.action_index < 10 && app->net )
            {
                int const op_num = opt.action_index + 1;
                unsigned const events = App_IfEventsGetEffective(app, opt.pick.id);
                /* The events mask is the whole of "is this op the server's", so it
                 * is the one number worth printing beside the row that carries it —
                 * a component the server never armed produces a perfectly good menu
                 * row and sends nothing. */
                if( getenv("TORIRS_CLICK_DEBUG") )
                {
                    int dbg_target;
                    int dbg_sub;

                    UIIfEventTable_ButtonTarget(app->tree, opt.pick.id, &dbg_target, &dbg_sub);
                    TORIRS_LOG(
                        "clickdbg: op%d on com=0x%x events=0x%x net=%d "
                        "target=0x%x (%d:%d) sub=%d\n",
                        op_num,
                        opt.pick.id,
                        events,
                        app->net ? 1 : 0,
                        dbg_target,
                        (dbg_target >> 16) & 0xffff,
                        dbg_target & 0xffff,
                        dbg_sub);
                }
                if( events & (1u << op_num) )
                {
                    int target;
                    int sub;

                    UIIfEventTable_ButtonTarget(app->tree, opt.pick.id, &target, &sub);
                    if( getenv("TORIRS_CLICK_DEBUG") )
                        TORIRS_LOG(
                            "clickdbg: send op%d target=0x%x sub=%d state=%d\n",
                            op_num,
                            target,
                            sub,
                            app->net ? (int)app->net->state : -1);
                    APP_NET_SEND(
                        app,
                        net_out_if_button_op(
                            app->net->rev,
                            app->net->random_out,
                            _nsbuf,
                            sizeof(_nsbuf),
                            op_num,
                            target,
                            sub));
                    if_button_sent = 1;
                }
                else if(
                    opt.action == REVCONFIG_MINIMENU_IF_BUTTON && opt.action_index == 0 &&
                    (events & 0x1u) )
                {
                    /* EVENT_CLICK (bit 0): minimenu rows use action_index 0, so
                     * the bit-(action_index+1) check above looks at bit 1 and
                     * misses. Choice-menu rows are dynamic children of a
                     * container the server armed across a sub range — answer
                     * with IF_BUTTON1 (parent, sub) so last_slot names the row
                     * and the server can resume p_pausebutton. Static continue
                     * prompts (no sub) keep the CS2 / plain IF_BUTTON path. */
                    int target;
                    int sub;

                    UIIfEventTable_ButtonTarget(app->tree, opt.pick.id, &target, &sub);
                    if( sub >= 0 )
                    {
                        if( getenv("TORIRS_CLICK_DEBUG") )
                            TORIRS_LOG(
                                "clickdbg: EVENT_CLICK choice target=0x%x sub=%d\n", target, sub);
                        APP_NET_SEND(
                            app,
                            net_out_if_button_op(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                1,
                                target,
                                sub));
                        if_button_sent = 1;
                    }
                }
            }
            hook = UITree_ResolveClickHook(app->tree, idx, &hook_com_id);
            if( !hook || hook->script_id <= 0 )
            {
                /* Already answered the server (choice-menu IF_BUTTON1, or a
                 * numbered op). Do not also sink a plain IF_BUTTON with the
                 * dynamic child's runtime id. */
                if( if_button_sent )
                    return 0;
                /* IF1-style static buttons have no CS2 hook — the button engine
                 * applies buttonType/varp semantics locally (+ server notify via
                 * the sink once networking attaches). */
                if( RS_IF1_ApplyButtonClick(app, opt.pick.id, opt.action) )
                    return 0;
                TORIRS_LOG(
                    "minimenu: no hook for com=0x%x action=%d op=%d\n",
                    opt.pick.id,
                    opt.action,
                    opt.action_index);
                return 0;
            }
        }
        UITree_HookInitCopy(&hook_copy, hook);
        RS_CS2_SetEventOp(&app->host, opt.action_index >= 0 ? opt.action_index + 1 : 1, 0);
        RS_CS2_SetEventMouse(&app->host, click_x, click_y);
        RS_CS2_DispatchHook(&app->host, &app->runner, hook_com_id, &hook_copy);
        RS_CS2_SetEventOp(&app->host, 1, 0);
        RS_CS2_PumpTransmits(&app->host, &app->runner);
        UITree_HookClear(&hook_copy);
        return 1;
    }
    case UI_MINIMENU_PICK_WEV:
        /*
         * A hull's config op (SAILING_PLAN C5.2): the row named one of the
         * WevConfig's five op strings against a picked hull. The real wire's
         * op packet for world entities is undocumented; the mock pair speaks
         * it over the cheat channel — `vesselop <view> <op>` — which is
         * server-authoritative like every other op and costs no protocol
         * invention. Content owns what each op DOES (the server boards on the
         * "Board" slot and shrugs at the rest).
         */
        if( app->net && Wevs_IsLive(&app->wevs, opt.pick.id) )
        {
            char op_cmd[32];

            snprintf(op_cmd, sizeof(op_cmd), "vesselop %d %d", opt.pick.id, opt.pick.secondary_id);
            APP_NET_SEND(
                app,
                net_out_client_cheat(
                    app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), op_cmd));
            UICross_Show(&app->cross, UI_CROSS_INTERACT, click_x, click_y);
        }
        return 0;
    case UI_MINIMENU_PICK_HEADING:
        if( app_sailing_send_heading(app, opt.pick.id) )
            UICross_Show(&app->cross, UI_CROSS_WALK, click_x, click_y);
        return 0;
    case UI_MINIMENU_PICK_TERRAIN:
        /*
         * A WORLD-ENTITY view's tile: the pick coordinates are the DECK's own,
         * and the wire wants the view's staging base added — the deob's
         * per-view walk send is exactly `baseX + localX` into MOVE_GAMECLICK
         * (client.java:9294-9312), with no view id in the packet; the server
         * re-derives the view geometrically, the way boarding itself does.
         * No client-side route: the rider's collision is the deck instance,
         * which only the server holds.
         */
        if( opt.pick.view_id != 0 &&
            (!WorldviewRegistry_IsLive(&app->worldviews, opt.pick.view_id) || !app->net) )
        {
            /* The row's tiles are DECK-LOCAL. With the view gone (the boat
             * despawned while the menu was open) they must not fall through
             * to the root walk below — reinterpreted as root scene tiles they
             * name an unrelated corner of the map. A dead view's row is a
             * no-op, like clicking the sky. */
            return 0;
        }
        if( opt.pick.view_id != 0 )
        {
            struct Worldview const* pview =
                WorldviewRegistry_Get(&app->worldviews, opt.pick.view_id);
            int deck_route_x[1] = { opt.pick.secondary_id };
            int deck_route_z[1] = { opt.pick.tertiary_id };

            if( torirs_env_net_debug() )
                TORIRS_LOG(
                    "minimenu: deck walk-click view=%d local=%d,%d abs=%d,%d\n",
                    opt.pick.view_id,
                    opt.pick.secondary_id,
                    opt.pick.tertiary_id,
                    pview->base_x + opt.pick.secondary_id,
                    pview->base_z + opt.pick.tertiary_id);
            APP_NET_SEND(
                app,
                net_out_move_gameclick(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    pview->base_x,
                    pview->base_z,
                    deck_route_x,
                    deck_route_z,
                    1,
                    app->ctrl_held));
            UICross_Show(&app->cross, UI_CROSS_WALK, click_x, click_y);
            return 0;
        }
        /* ABOARD, a ROOT ground click cannot gate on the client BFS below —
         * the local router would flood from the rider's deck-local scene
         * position through a world it does not stand in, fail, and send
         * nothing. The server owns what the click MEANS (a steering order at
         * the helm, or a walk to the rail tile nearest the click), so send
         * the destination bare, exactly like a deck click. */
        if( app->aboard_view != WORLDVIEW_ROOT && app->net && app->world )
        {
            int route_x[1] = { opt.pick.secondary_id };
            int route_z[1] = { opt.pick.tertiary_id };

            APP_NET_SEND(
                app,
                net_out_move_gameclick(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    app->world->_base_tile_x,
                    app->world->_base_tile_z,
                    route_x,
                    route_z,
                    1,
                    app->ctrl_held));
            UICross_Show(&app->cross, UI_CROSS_WALK, click_x, click_y);
            return 0;
        }

        /* Walk here (reference tryMove type 0): BFS route + MOVE_GAMECLICK
         * waypoints; no local prediction — the PLAYER_INFO echo moves the
         * player. */
        if( torirs_env_net_debug() )
            TORIRS_LOG(
                "minimenu: walk-click scene=%d,%d abs=%d,%d\n",
                opt.pick.secondary_id,
                opt.pick.tertiary_id,
                app->world ? app->world->_base_tile_x + opt.pick.secondary_id : -1,
                app->world ? app->world->_base_tile_z + opt.pick.tertiary_id : -1);
        /* Cross only if the move actually went out — the reference sets
         * crossMode in the same block that emits the packet (Client-TS
         * `if (success)`, deob client.java:9305 under method3951()). A click
         * the router refuses leaves the screen alone. */
        if( app_try_move(
                app, opt.pick.secondary_id, opt.pick.tertiary_id, 0, 0, 0, 0, app->ctrl_held) )
            UICross_Show(&app->cross, UI_CROSS_WALK, click_x, click_y);
        return 0;
    case UI_MINIMENU_PICK_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
        if( !npc || npc->server_slot < 0 )
            return 0; /* not yet server-synced */
        /* Reference OP_NPC1..5 / USEHELD_ONNPC walk toward the NPC (tryMove
         * type 2) on the same click; the OP is sent regardless of the route. */
        app_try_move_npc(app, npc, app->ctrl_held);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_opnpcu(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    npc->server_slot,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            app_selection_clear(app);
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_opnpc(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    opt.action_index + 1,
                    npc->server_slot));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_PLAYER:
    {
        struct WorldEntity_Player* player = World_PlayerGetByElementId(app->world, opt.pick.id);
        /* Menu never emits local-player rows; refuse if one slips through. */
        if( !player || player->server_pid < 0 || player->server_pid == app->world->local_pid )
            return 0;
        app_try_move_player(app, player, app->ctrl_held);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_opplayeru(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    player->server_pid,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            app_selection_clear(app);
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_opplayer(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    opt.action_index + 1,
                    player->server_pid));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_SCENERY:
    {
        int abs_x;
        int abs_z;
        int loc_id = opt.pick.secondary_id;

        /*
         * A DECK loc (SAILING_PLAN C5.2's non-terrain half): the pick's tiles
         * are the VIEW's own, and the wire wants the staging base added —
         * exactly the deck walk-click's shape, and the server resolves the
         * loc at those absolute coordinates out of the vessel's pinned deck
         * window (ToriRSServer_SceneFindLoc searches every window). No
         * client-side route: the deck collision is the server's. A dead view
         * makes the row a no-op, never a root reinterpretation.
         */
        if( opt.pick.view_id != 0 )
        {
            struct Worldview const* pview;

            if( !WorldviewRegistry_IsLive(&app->worldviews, opt.pick.view_id) || !app->net )
                return 0;
            pview = WorldviewRegistry_Get(&app->worldviews, opt.pick.view_id);
            abs_x = opt.pick.tertiary_id + pview->base_x;
            abs_z = opt.pick.quaternary_id + pview->base_z;
            TORIRS_LOG(
                "oploc view: op%d loc=%d at %d,%d (view %d local %d,%d)\n",
                opt.action_index + 1,
                loc_id,
                abs_x,
                abs_z,
                opt.pick.view_id,
                opt.pick.tertiary_id,
                opt.pick.quaternary_id);
            if( app->objsel.active )
            {
                APP_NET_SEND(
                    app,
                    net_out_oplocu(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        abs_x,
                        abs_z,
                        loc_id,
                        app->objsel.obj_id,
                        app->objsel.slot,
                        app->objsel.component_id));
                app_selection_clear(app);
            }
            else
            {
                APP_NET_SEND(
                    app,
                    net_out_oploc(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        opt.action_index + 1,
                        abs_x,
                        abs_z,
                        loc_id));
            }
            UICross_Show(&app->cross, UI_CROSS_INTERACT, click_x, click_y);
            return 0;
        }

        abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        /* Reference interactWithLoc: pathfind toward the loc (tryMove type 2)
         * on the same click, then send the OP below regardless of the walk
         * result. The scene tile is the pick's (tertiary,quaternary). */
        if( !app_try_move_loc(
                app, opt.pick.id, opt.pick.tertiary_id, opt.pick.quaternary_id, app->ctrl_held) )
            return 0; /* reference interactWithLoc: typecode2 == -1 sends nothing */
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_oplocu(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    loc_id,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            app_selection_clear(app);
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_oploc(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    opt.action_index + 1,
                    abs_x,
                    abs_z,
                    loc_id));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_OBJ:
    {
        /* Ground item (reference OP_OBJ1..5 / USEHELD_ONOBJ): the wire
         * carries the tile + obj id, not the scene element. */
        int abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        int abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        int obj_id = opt.pick.secondary_id;
        /* Reference obj doAction: pathfind to the exact tile (tryMove type 2),
         * and on failure retry a 1x1 approach so an adjacent tile still arrives;
         * the OP is sent below on the same click either way. */
        app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id, app->ctrl_held);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_opobju(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    obj_id,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            app_selection_clear(app);
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_opobj(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    opt.action_index + 1,
                    abs_x,
                    abs_z,
                    obj_id));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_NONE:
        /* Cancel, and the "Walk here" row of a click that hit no terrain and
         * for which no fallback tile could be resolved (no world loaded, no
         * viewport, a scene with no ground at all). Nothing to run — not an
         * unhandled kind, so it must not warn. */
        return 0;
    default:
        TORIRS_LOG("minimenu: unhandled pick kind %d\n", (int)opt.pick.kind);
        return 0;
    }
}

/* Execute one menu row, then apply the reference doAction tail
 * (Client.ts:9506): every executed row clears useMode/targetMode EXCEPT the two
 * arming rows (USEHELD_START / TGT_BUTTON), which return early. So any click
 * that isn't "arm Use" or "arm spell" cancels a pending selection — Walk here, a
 * plain op, a UI button, Cancel — i.e. clicking off anything that can't be a use
 * target drops the white outline. The arming rows set the selection inside
 * run_option and must survive; they alone skip the clear. */
static int
app_minimenu_use_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y)
{
    struct UIMinimenu* menu = &app->interact.minimenu;
    int action = -1;
    int result;

    if( option_index >= 0 && option_index < menu->option_count )
        action = UIMinimenu_ActionNormalize(menu->options[option_index].action);

    result = app_minimenu_run_option(app, option_index, click_x, click_y);

    if( action != REVCONFIG_MINIMENU_OPHELDT_START && action != REVCONFIG_MINIMENU_TGT_BUTTON )
        app_selection_clear(app);
    return result;
}
