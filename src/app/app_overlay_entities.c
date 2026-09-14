/*
 * Building the entity overlays: health bars, hitsplats, headicons, overhead chat, and their anchors.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/**
 * The three anchor points an overlay's band chooses between
 * (`Client::GetAllOverlayPositions`): the top of the subject, its middle, and
 * its feet. Screen pixels.
 *
 * `subject_live` and `ok` are deliberately two answers, not one. An npc that
 * is merely behind the camera does not project, and treating that as "the
 * subject has gone" reaped every overlay the moment its npc left the view --
 * which, with the global npc-add trigger giving every npc a name plate, meant
 * the whole table churned back to index 0 every frame.
 */
struct AppOverlayPos
{
    bool subject_live;
    bool ok;
    int top_x;
    int top_y;
    int mid_x;
    int mid_y;
    int foot_x;
    int foot_y;
};

/* Private to this unit, declared up front so definition order is free. */
static void
app_overlay_build_healthbar(
    struct App* app,
    struct WorldEntityFacet_Combat const* combat,
    int screen_x,
    int screen_y);
static void
app_overlay_build_entity(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Combat const* combat,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int font_id,
    int hitmarks_scene,
    int type_height);
static struct AppOverlayPos
app_overlay_anchor(
    struct App* app,
    struct RS_Overlay const* item);

/*
 * The overhead health bar, as the rev-239 client draws it.
 *
 * Three things the old two-rectangle version got wrong, all of them the same
 * mistake -- treating the server's fill byte as a pixel count:
 *
 *   - The bar's span is the FRONT SPRITE's width, minus the type's padding at
 *     both ends. It runs 30..160 across cache.osrs239's 85 records. Only a
 *     type naming no sprites falls back to `width` pixels.
 *   - `width` (opcode 14) is the denominator the fill arrives as a fraction of,
 *     which is a different number from the span for `healthbar_8` and equal to
 *     it for the other 84.
 *   - A block carries a start fill, an end fill and a duration; the bar
 *     travels between them and then fades, rather than snapping to one value.
 *
 * Reference: the health-bar block of drawEntities, class381 and class66.
 */
static void
app_overlay_build_healthbar(
    struct App* app,
    struct WorldEntityFacet_Combat const* combat,
    int screen_x,
    int screen_y)
{
    struct RS_HealthbarType const* type =
        RS_Healthbars_TypeFor(&app->healthbars, combat->healthbar_type);
    int cycle = app->world->cycle;
    /* -1 is the common "this type has no sprites" state, and EnsureSprite
     * answers -1 for it as well -- so the pair is resolved unconditionally and
     * only the resulting scene ids are tested. */
    int front_scene = UITreeSceneBridge_EnsureSprite(&app->bridge, type->front_sprite);
    int back_scene = UITreeSceneBridge_EnsureSprite(&app->bridge, type->back_sprite);
    int front_w = 0;
    int front_h = 0;
    int back_w = 0;
    int back_h = 0;
    bool sprites = app_scene_sprite_size(app, front_scene, &front_w, &front_h) &&
                   app_scene_sprite_size(app, back_scene, &back_w, &back_h);
    int padding = 0;
    int span;
    int elapsed = cycle - combat->healthbar_start_cycle;
    int end_span;
    int drawn;
    int alpha = 255;
    int bar_x;

    /* Denominator, so a type that somehow declares 0 would divide by zero. The
     * reference has no such guard because its constructor cannot produce one;
     * ours reads a cache, so it can. */
    assert(type->width > 0);

    if( sprites )
    {
        /* The reference only accepts the padding when it fits inside the
         * sprite -- an oversized one would invert the span. */
        if( type->padding < front_w )
            padding = type->padding;
        span = front_w - padding * 2;
    }
    else
    {
        span = type->width;
    }

    end_span = combat->healthbar_end_fill * span / type->width;
    if( combat->healthbar_duration > elapsed )
    {
        int start_span = combat->healthbar_start_fill * span / type->width;
        drawn = (end_span - start_span) * elapsed / combat->healthbar_duration + start_span;
    }
    else
    {
        drawn = end_span;
        /* Past the travel, the bar fades over the tail of its persist window.
         * -1 (the constructor default, and what most records keep) never
         * fades, which is why this is not a plain subtraction. */
        if( type->fade_threshold >= 0 && type->persist_cycles > type->fade_threshold )
        {
            int remaining = combat->healthbar_duration + type->persist_cycles - elapsed;
            alpha = (remaining << 8) / (type->persist_cycles - type->fade_threshold);
        }
    }
    /* A living entity never renders as empty: any non-zero fill keeps a pixel. */
    if( combat->healthbar_end_fill > 0 && drawn < 1 )
        drawn = 1;
    if( drawn > span )
        drawn = span;
    if( drawn < 0 )
        drawn = 0;

    bar_x = screen_x - (span >> 1);

    if( !sprites )
    {
        struct UITreeEntityOverlay bar = {
            .kind = UITREE_ENTITY_OVERLAY_RECT,
            .x = bar_x,
            .y = screen_y - 3,
            .w = drawn,
            .h = RS_HEALTHBAR_FALLBACK_HEIGHT,
            .color = 0xFF00FF00u, /* Colour.GREEN */
        };
        app_overlay_push(app, &bar);
        bar.x = bar_x + drawn;
        bar.w = span - drawn;
        bar.color = 0xFFFF0000u; /* Colour.RED */
        app_overlay_push(app, &bar);
        return;
    }

    {
        /* Both halves are blitted at the same origin; the filled one is cut
         * off at the current fill. The full bar gets both paddings back
         * because its right edge is the sprite's own, not a cut. */
        int clip_w = (drawn == span) ? padding * 2 + drawn : padding + drawn;
        int x = bar_x - padding;
        /* Centred on the same row the rectangle path uses, so switching
         * between the two does not move the bar. */
        int y = screen_y - back_h / 2;
        int trans = (alpha >= 0 && alpha < 255) ? 255 - alpha : 0;
        struct UITreeEntityOverlay back = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = x,
            .y = y,
            .scene_id = back_scene,
            .trans = trans,
        };
        struct UITreeEntityOverlay front = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = x,
            .y = y,
            .scene_id = front_scene,
            .trans = trans,
            .clip_x = x,
            .clip_y = y,
            .clip_w = clip_w,
            .clip_h = front_h,
        };

        app_overlay_push(app, &back);
        if( clip_w > 0 )
            app_overlay_push(app, &front);
    }
}

static void
app_overlay_build_entity(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Combat const* combat,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int font_id,
    int hitmarks_scene,
    int type_height)
{
    int cycle = app->world->cycle;
    int height = app_entity_overlay_height(app, element_id, type_height);
    int screen_x, screen_y;

    /*
     * Health bar, 15px above the model top. Two sources, and they are not
     * alternatives so much as two eras:
     *
     *   - A HEADBAR block (dat2/OldSchool) names a healthbar type and carries
     *     fills relative to it. Everything about how it draws is the type's.
     *   - A legacy dat1 hitsplat block carries raw hitpoints and no type at
     *     all, so it keeps the client's own 30-wide rectangle and the
     *     `combatCycle > loopCycle + 100` window (combat_cycle is set to
     *     loopCycle + 400 on every hit -- the same 300 cycles the standard
     *     healthbar type spells as its persist window).
     */
    if( combat->healthbar_type >= 0 && combat->healthbar_end_cycle > cycle &&
        app_world_project_actor(
            app,
            placement,
            actor_level,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
    {
        app_overlay_build_healthbar(app, combat, screen_x, screen_y);
    }
    else if(
        combat->healthbar_type < 0 && combat->combat_cycle > cycle + 100 &&
        combat->total_health > 0 &&
        app_world_project_actor(
            app,
            placement,
            actor_level,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
    {
        int bar_width = RS_HEALTHBAR_DEFAULT_WIDTH;
        int filled = (combat->health * bar_width) / combat->total_health;
        if( filled > bar_width )
            filled = bar_width;
        if( filled < 0 )
            filled = 0;
        struct UITreeEntityOverlay bar = {
            .kind = UITREE_ENTITY_OVERLAY_RECT,
            .x = screen_x - (bar_width >> 1),
            .y = screen_y - 3,
            .w = filled,
            .h = RS_HEALTHBAR_FALLBACK_HEIGHT,
            .color = 0xFF00FF00u, /* Colour.GREEN */
        };
        app_overlay_push(app, &bar);
        bar.x = screen_x - (bar_width >> 1) + filled;
        bar.w = bar_width - filled;
        bar.color = 0xFFFF0000u; /* Colour.RED */
        app_overlay_push(app, &bar);
    }

    /* Hitsplats: up to 4 concurrent, each alive for 70 cycles, positioned by
     * slot (reference nudges slots 1-3 off the centre). */
    for( int i = 0; i < WORLD_ENTITY_DAMAGE_SLOTS; i++ )
    {
        char text[UITREE_ENTITY_OVERLAY_TEXT_LEN];

        if( combat->damage_start_cycles[i] > cycle || combat->damage_cycles[i] <= cycle )
            continue;

        /*
         * The wire named a type; the CACHE decides which one is drawn.
         *
         * 34 of this cache's hitsplat records are opcode 17/18 selectors keyed
         * on the player's own settings — 5 "Hitsplat tinting" and 279 "Max hit
         * hitsplats" — so the id that arrived is a question and this is where it
         * is answered. See `game/rs_hitsplat.h`.
         *
         * Resolved HERE, per frame, rather than once when the hit landed, which
         * is where the reference resolves it too: a splat already on screen
         * re-skins the instant the setting is toggled, and resolving on receipt
         * would leave the ones in flight wearing the old answer.
         *
         * `duration` and `slot_policy` are deliberately NOT resolved — those are
         * read off the type the wire named, at the moment it arrived
         * (`task_exec_entity_info.c`), exactly as the reference reads them from
         * the unresolved type before it swaps.
         */
        int const splat_type =
            RS_Hitsplats_ResolveType(&app->hitsplats, &app->varps, combat->damage_types[i]);

        /* A resolved -1 is the cache saying "draw nothing for this hit". No
         * record in cache.osrs239 says it; it is honoured anyway, because the
         * alternative is drawing a splat the cache asked to hide. */
        if( splat_type < 0 )
            continue;

        if( !app_world_project_actor(
                app,
                placement,
                actor_level,
                (int)draw_position->x,
                (int)draw_position->z,
                height / 2,
                &screen_x,
                &screen_y) )
            continue;

        if( i == 1 )
            screen_y -= 20;
        else if( i == 2 )
        {
            screen_x -= 15;
            screen_y -= 10;
        }
        else if( i == 3 )
        {
            screen_x += 15;
            screen_y -= 10;
        }

        /*
         * The splat behind the number.
         *
         * Two eras, two sources, and the type index means a different thing in
         * each. dat1 packs every splat into one "hitmarks" sprite archive and
         * the damage type is the frame within it. OldSchool gives each type its
         * own *config record* naming an ordinary sprite id (group 32 — rev 239
         * damage is type 28 / sprite 1359 and block is type 26 / sprite 1358),
         * so the type is a table lookup and the resulting sprite has one frame.
         *
         * Preferring the config table means the OldSchool path works; falling
         * back to the archive means the dat1 path is untouched. Neither
         * available draws the number alone, which is what this used to do
         * always.
         */
        {
            int splat_sprite = RS_Hitsplats_SpriteFor(&app->hitsplats, splat_type);
            int splat_scene = -1;
            int splat_frame = 0;

            if( splat_sprite >= 0 )
                splat_scene = UITreeSceneBridge_EnsureSprite(&app->bridge, splat_sprite);
            if( splat_scene < 0 && hitmarks_scene > 0 )
            {
                splat_scene = hitmarks_scene;
                /* The WIRE type, not the resolved one: on the dat1 path this is
                 * a frame index into the hitmarks archive, which is a different
                 * id space from the config table. The two agree today only
                 * because a dat1 cache has no selectors to resolve. */
                splat_frame = combat->damage_types[i];
            }
            if( splat_scene >= 0 )
            {
                struct UITreeEntityOverlay spr = {
                    .kind = UITREE_ENTITY_OVERLAY_SPRITE,
                    .x = screen_x - 12,
                    .y = screen_y - 12,
                    .w = 0,
                    .h = 0,
                    .scene_id = splat_scene,
                    .atlas_index = splat_frame,
                };
                app_overlay_push(app, &spr);
            }
        }
        snprintf(text, sizeof(text), "%d", combat->damage_values[i]);
        if( font_id >= 0 )
        {
            /* Black shadow then white, offset by one px — reference draws the
             * number twice (Client.ts:4931-4932). */
            struct UITreeEntityOverlay num = {
                .kind = UITREE_ENTITY_OVERLAY_TEXT,
                .x = screen_x,
                .y = screen_y + 4,
                .font_id = font_id,
                .color = 0xFF000000u,
            };
            snprintf(num.text, sizeof(num.text), "%s", text);
            app_overlay_push(app, &num);
            num.x = screen_x - 1;
            num.y = screen_y + 3;
            num.color = 0xFFFFFFFFu;
            app_overlay_push(app, &num);
        }
    }
}

/** Where one overlay's subject is this frame, or `ok = false` when the subject
 *  has gone -- which is the signal to reap the overlay, not to hide it. */
static struct AppOverlayPos
app_overlay_anchor(
    struct App* app,
    struct RS_Overlay const* item)
{
    struct AppOverlayPos out;
    int fine_x = 0;
    int fine_z = 0;
    int height = 0;

    assert(app);
    assert(item);

    memset(&out, 0, sizeof(out));
    if( !app->world )
        return out;

    if( item->anchor == RS_OVERLAY_ANCHOR_NPC )
    {
        struct WorldEntity_NPC* npc = World_NpcGetByServerSlot(app->world, item->uid);
        if( !npc )
            return out;
        fine_x = (int)npc->draw_position.x;
        fine_z = (int)npc->draw_position.z;
        height = app_entity_overlay_height(app, npc->element_id, -1);
        out.subject_live = true;
    }
    else if( item->anchor == RS_OVERLAY_ANCHOR_PLAYER )
    {
        struct WorldEntity_Player* pl = World_PlayerGetByServerPid(app->world, item->uid);
        if( !pl )
            return out;
        fine_x = (int)pl->draw_position.x;
        fine_z = (int)pl->draw_position.z;
        height = app_entity_overlay_height(app, pl->element_id, -1);
        out.subject_live = true;
    }
    else
    {
        int x;
        int z;
        int level;
        if( !app->world || !World_CoordToSceneTile(app->world, item->coord, &x, &z, &level) )
            return out;
        /*
         * A tile has no model, so all three anchors are the tile centre at
         * ground height: an "above" overlay stacks up from the floor and a
         * "below" one stacks down from it. The reference measures the loc's own
         * model here; a loc whose overlay wants to clear it says so with its
         * band and its height, which is what every static overlay in this cache
         * does (60x60 above, at the tile).
         */
        fine_x = x * 128 + 64;
        fine_z = z * 128 + 64;
        height = 0;
        out.subject_live = true;
    }

    int32_t overlay_node = app->tree ? UITree_FindByComponentId(app->tree, item->component_id) : -1;
    int lift = UITree_WidgetProjectionHeight(app->tree, overlay_node);
    if( !app_world_project(app, fine_x, fine_z, height + lift, &out.top_x, &out.top_y) )
        return out;
    if( !app_world_project(app, fine_x, fine_z, height / 2 + lift, &out.mid_x, &out.mid_y) )
        return out;
    if( !app_world_project(app, fine_x, fine_z, -15 + lift, &out.foot_x, &out.foot_y) )
        return out;
    out.ok = true;
    return out;
}

/**
 * Move every scripted overlay's layer to where its subject is, and reap the
 * ones whose subject has gone.
 *
 * Runs immediately before the emit walk, off the PREVIOUS frame's world
 * viewport -- the same rect `app_world_project` reads, so an overlay and the
 * health bar over the same npc cannot disagree by a frame.
 */
void
app_entity_overlay_layout(struct App* app)
{
    assert(app);

    if( !app->tree )
        return;

    int32_t const parent = app->tree->entity_overlay_index;
    if( parent < 0 )
        return;

    /* A rebuilt tree took every overlay layer with it (see
     * App::client_trigger_refire_pending). The sweep below sets the flag when
     * it finds an overlay whose layer is gone; it is acted on here, before the
     * sweep, so the refire happens outside the walk it would invalidate. */
    if( app->client_trigger_refire_pending )
    {
        app->client_trigger_refire_pending = 0;
        app_client_triggers_refire(app);
    }

    /* The parent IS the world rect: it is what clips the overlays (see
     * UITree_ComponentClipsChildren), and the App is the only thing that knows
     * the rect. A tree whose world has not been emitted yet has no rect and so
     * no overlays -- which is right, because there is nothing to anchor to. */
    {
        struct UITreeElemPosition const* pos = &app->tree->components[parent].position;
        int const w = app->world_view_valid ? app->world_emit_desc.w : 0;
        int const h = app->world_view_valid ? app->world_emit_desc.h : 0;
        int const x = app->world_view_valid ? app->world_emit_desc.x : 0;
        int const y = app->world_view_valid ? app->world_emit_desc.y : 0;
        if( pos->kind != UIPOS_XY || pos->x != x || pos->y != y || pos->width != w ||
            pos->height != h )
            (void)UITree_SetXYBoxAt(app->tree, parent, x, y, w, h);
    }

    /* Band 1 stacks upward and band 2 downward, per subject -- two overlays on
     * one npc must not overprint. The cursors are keyed by the subject the
     * overlay names, so a second pass over the same npc continues the stack. */
    for( int i = 0; i < RS_OVERLAY_MAX; i++ )
    {
        struct RS_Overlay const* item = RS_OverlayGet(&app->host.overlay, i);
        if( !item )
            continue;

        struct AppOverlayPos anchor = app_overlay_anchor(app, item);
        if( !anchor.subject_live )
        {
            /*
             * The subject is GONE -- the npc despawned, or the tile fell out of
             * the rebuilt scene.
             *
             * Reaped rather than hidden, because nothing else will: an npc that
             * walked out of the scene is never coming back under the same uid,
             * and the script that made the overlay gets no event to tell it so.
             */
            if( torirs_env_overlay_script_debug() )
                TORIRS_LOG(
                    "overlay: reap #%d anchor=%d uid=%d coord=%d slot=%d\n",
                    i,
                    item->anchor,
                    item->uid,
                    item->coord,
                    item->slot);
            RS_CS2Host_OverlayReap(&app->host, i);
            continue;
        }

        int32_t const node = UITree_FindByComponentId(app->tree, item->component_id);
        if( node < 0 )
        {
            /* The layer is gone but the record is not: a tree rebuild. Every
             * overlay is in the same state, so this is raised once and acted
             * on at the top of the next pass. */
            app->client_trigger_refire_pending = 1;
            continue;
        }
        /* Projection failure is camera-owned visibility, not script-owned
         * `hide`. Leaving the old layer visible here freezes an overlay at its
         * last valid coordinates when its subject crosses the near plane. */
        if( !anchor.ok )
        {
            (void)UITree_SetProjectionHiddenAt(app->tree, node, 1);
            continue;
        }
        (void)UITree_SetProjectionHiddenAt(app->tree, node, 0);

        struct UITreeComponent* c = &app->tree->components[node];
        struct UITreeElemPosition allocation = c->position;
        UITree_WidgetPositionOverride(app->tree, node, &allocation);
        int const w = allocation.width;
        int const h = allocation.height;
        int x = anchor.mid_x - w / 2;
        int y = anchor.mid_y - h / 2;

        if( item->band == RS_OVERLAY_BAND_ABOVE )
        {
            x = anchor.top_x - w / 2;
            y = anchor.top_y - h;
        }
        else if( item->band == RS_OVERLAY_BAND_BELOW )
        {
            x = anchor.foot_x - w / 2;
            y = anchor.foot_y;
        }

        /* The box is the parent's, so subtract the world rect the parent sits
         * at -- the projection is in screen pixels and the layout is not. */
        x -= app->tree->components[parent].position.x;
        y -= app->tree->components[parent].position.y;

        if( c->position.x != x || c->position.y != y )
        {
            /* This is a retained-tree mutation, not just a repaint request.
             * The setter clears the cached absolute box and bumps dirty_gen,
             * so the emit-retention gate cannot reuse the sprite command from
             * the previous camera angle. */
            (void)UITree_EntityOverlaySetLayerPosition(app->tree, node, x, y);
        }

        if( torirs_env_overlay_script_debug() )
        {
            int kids = 0;
            for( int32_t k = c->first_child; k >= 0; k = app->tree->components[k].next_sibling )
                kids++;
            TORIRS_LOG(
                "overlay: #%d anchor=%d slot=%d band=%d com=0x%08x box=%d,%d %dx%d kids=%d "
                "hide=%d\n",
                i,
                item->anchor,
                item->slot,
                item->band,
                (unsigned)item->component_id,
                x,
                y,
                w,
                h,
                kids,
                (int)c->behavior.hide);
        }
    }
}

/*
 * The plugin canvas overlay, built on demand.
 *
 * A whole function for four lines because the shape has to match the world
 * list's exactly: empty it, let the pushers fill it, hand the array over. What is
 * NOT here is any of the client's own drawing -- nothing but a plugin ever
 * writes to this list, which is why the pass that asks for it can be
 * unconditional and still cost nothing on a client with no plugins.
 */
int
app_build_canvas_overlays(
    struct App* app,
    struct UITreeEntityOverlay const** out_items)
{
    assert(app);
    assert(out_items);

    *out_items = OverlayStage_Items(&app->overlays, OVERLAY_SURFACE_CANVAS);
    if( app->overlays.canvas_prepared )
        return OverlayStage_Count(&app->overlays, OVERLAY_SURFACE_CANVAS);

    app->overlays.canvas_prepared = true;
    OverlayStage_ResetCanvas(&app->overlays);
    if( !app->plugins )
        return 0;

    PluginHost_DrawCanvas(app->plugins, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    return OverlayStage_Count(&app->overlays, OVERLAY_SURFACE_CANVAS);
}

int
app_build_entity_overlays(
    struct App* app,
    struct UITreeEntityOverlay const** out_items)
{
    struct World* world = app->world;
    struct World_EntityPool* pool;
    int font_id;
    int hitmarks_scene;

    OverlayStage_ResetWorld(&app->overlays);
    *out_items = OverlayStage_Items(&app->overlays, OVERLAY_SURFACE_WORLD);
    if( !world || !world->load_complete || !app->world_view_valid )
        return 0;

    font_id = app_hitsplat_font_scene_id(app);
    hitmarks_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HITMARKS);
    /*
     * Older caches ship one `headicons` pack holding prayer icons and the PK
     * skull together; OldSchool split it, and rev 230 has no `headicons`
     * archive at all — only `headicons_prayer`, `headicons_pk` and
     * `headicons_hint`. The prayer icons keep their indices across the split
     * (0 melee, 1 missiles, 2 magic, 3 retribution, 4 smite, 5 redemption), so
     * the split pack is a drop-in for the overhead pass. Without this the whole
     * feature is silently dead on a modern cache: the mask arrives, the slot is
     * -1, and nothing draws.
     */
    int headicons_scene =
        UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HEADICONS);
    if( headicons_scene <= 0 )
        headicons_scene =
            UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HEADICONS_PRAYER);
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        struct ToriRS_Npctype* npctype;
        if( !npc || npc->multinpc_hidden || npc->element_id < 0 )
            continue;
        /* Only the npc branch can carry an overhead-height override; the
         * reference reads it off the NpcComposition, which players have no
         * equivalent of (Actor.getLogicalHeight is unconditional there). */
        npctype = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
        app_overlay_build_entity(
            app,
            npc->element_id,
            &npc->combat,
            &npc->draw_position,
            &npc->view_placement,
            -1,
            font_id,
            hitmarks_scene,
            npctype ? npctype->height : -1);
        app_overlay_build_npc_headicon(
            app,
            npc->element_id,
            npctype,
            &npc->draw_position,
            &npc->view_placement,
            headicons_scene,
            APP_HEADICONS_PRAYER_GROUP);
    }

    pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
        if( !player || player->element_id < 0 )
            continue;
        app_overlay_build_entity(
            app,
            player->element_id,
            &player->combat,
            &player->draw_position,
            &player->view_placement,
            player->grid_position.level,
            font_id,
            hitmarks_scene,
            -1);
        app_overlay_build_player_headicons(
            app,
            player->element_id,
            player->headicon,
            &player->draw_position,
            &player->view_placement,
            player->grid_position.level,
            headicons_scene);
    }

    /* One arrow, after every entity, so it layers over the health bars and
     * splats of whatever it is pointing at. */
    app_overlay_build_hint_arrow(app);

    /* Overhead chat is a second pass so it layers above every entity's health
     * bar and hitsplats (reference draws chatX/chatY after the entity loop).
     * It uses b12, the bold chat font, not the p11 hitsplat font. */
    {
        int chat_font = app_minimenu_font_scene_id(app);

        pool = &world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
            if( !npc || npc->multinpc_hidden || npc->element_id < 0 )
                continue;
            app_overlay_build_chat(
                app,
                npc->element_id,
                &npc->chat,
                &npc->draw_position,
                &npc->view_placement,
                -1,
                chat_font);
        }

        pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
            if( !player || player->element_id < 0 )
                continue;
            app_overlay_build_chat(
                app,
                player->element_id,
                &player->chat,
                &player->draw_position,
                &player->view_placement,
                player->grid_position.level,
                chat_font);
        }
    }

    /* Debug: hovered loc's painter footprint, in red (see the builder). Last
     * so the outline layers above bars/splats/chat. */
    app_overlay_build_hover_footprint(app);
    /* The map editor's own latch, in green -- separate from the hover mark
     * above so a select-tool session and TORIRS_HOVER_FOOTPRINT can be on at
     * once without one drawing over the other's meaning. */
    app_overlay_build_editor_selection(app);

    /*
     * Plugins last, so their marks layer above every built-in in this pass.
     *
     * This whole layer is hoisted to just above the 3D world by
     * emit_hoist_entity_overlays, which puts plugin drawing exactly where a
     * RuneLite scene overlay sits: over the world, under the interfaces, the
     * cross, the hover line and the minimenu. It costs nothing when no plugin
     * subscribed, and the items land in the pool the built-ins have already
     * taken what they need from -- so a crowded scene clips the plugin, never
     * a health bar.
     */
    PluginHost_DrawWorld(app->plugins);

    /* TORIRS_OVERLAY_DEBUG=1: the primitives this frame, plus the two assets
     * they need — a missing p11 (font -1) or hitmarks pack is the usual
     * reason a hit lands but nothing is drawn. */
    if( torirs_env_overlay_debug() && app->overlays.world_count > 0 )
    {
        TORIRS_LOG(
            "overlay: %d items font=%d hitmarks=%d\n",
            app->overlays.world_count,
            font_id,
            hitmarks_scene);
        for( int i = 0; i < app->overlays.world_count; i++ )
        {
            struct UITreeEntityOverlay const* item = &app->overlays.world[i];
            /* scene/clip/trans are printed because a SPRITE primitive carries
             * no w/h -- it blits at the sprite's own size -- so without them a
             * health bar's line says nothing about how wide it came out. */
            TORIRS_LOG(
                "  overlay[%d] kind=%d at %d,%d %dx%d scene=%d clip=%d,%d %dx%d "
                "trans=%d \"%s\"\n",
                i,
                item->kind,
                item->x,
                item->y,
                item->w,
                item->h,
                item->scene_id,
                item->clip_x,
                item->clip_y,
                item->clip_w,
                item->clip_h,
                item->trans,
                item->text);
        }
    }
    return app->overlays.world_count;
}
