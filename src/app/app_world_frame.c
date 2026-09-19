/*
 * The per-frame world step and the entity-removed drain.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_texture_anim_on_cpu(const struct App* app);

/* Apply queued WorldEventKind_EntityRemoved: free the DYNAMIC scene element.
 * Must run before World_ResetSceneAlloc (which asserts the queue is empty) and
 * after bulk despawns in App_WorldRebuildShift — silent drops used to orphan
 * elements across ClearPool(STATIC) and climb the scene id high-water mark.
 * Parameterized over the world because every view's World feeds the one shared
 * scene: a boat view drains its own queue through here before its deck rebuild
 * (SAILING_PLAN C2), the root through the wrapper below. */
void
App_WorldDrainEntityRemovedFor(
    struct App* app,
    struct World* world)
{
    int count;

    assert(app);
    assert(world);

    count = World_EventsCount(world);
    for( int i = 0; i < count; i++ )
    {
        const struct World_Event* ev = World_EventsPeek(world, i);
        if( ev->kind == WorldEventKind_EntityRemoved && ev->element_id >= 0 )
        {
            /* The world releases its pool slot when it queues the removal, but
             * preserves an immutable NPC copy on that event. Prefer it over a
             * live lookup: the slot may be absent or may already belong to a
             * different NPC by the time this render-side drain runs. */
            if( app->plugins )
            {
                struct WorldEntity_NPC const* going = ev->removed_npc;
                struct ToriRS_NpcSnapshot snap;
                if( !going )
                    going = World_NpcGetByElementId(world, ev->element_id, NULL);
                if( going )
                    app_plugin_fill_npc_for_world(app, world, going, &snap);
                else
                {
                    memset(&snap, 0, sizeof(snap));
                    snap.server_slot = -1;
                    snap.npc_id = -1;
                    snap.base_npc_id = -1;
                    snap.element_id = ev->element_id;
                    /* Explicitly, because a zeroed health_ratio is the value
                     * that means DEAD and this snapshot knows nothing at all.
                     * A loot tracker reading it as a kill would open a record
                     * for every entity the render side cleaned up late. */
                    snap.health_ratio = -1;
                    snap.health_scale = -1;
                }
                PluginHost_NpcDespawn(app->plugins, &snap);
            }
            app_entity_spotanim_drop(app, ev->element_id);
            app_seq_bind_pending_drop(app, ev->element_id);
            if( app->scene )
                ToriDraw_SceneElementRemove(app->scene, ev->element_id);
        }
        else if( ev->kind == WorldEventKind_SpotanimStarted && ev->element_id >= 0 && app->scene )
        {
            /* The delayed map spotanim parked in app_world_spawn_spotanim_now
             * is drawing for the first time this cycle: rewind to frame 0 and
             * hand it back to the per-element tick.
             *
             * The rewind is not redundant with parking it. A sequence that was
             * not resident at spawn binds later through seq_bind_pending, and
             * that path catches the animation up by (now - start_cycle) — a
             * span measured from SPAWN, which for a delayed spotanim is the
             * wrong origin and can land it mid-sequence or past the end. Frame
             * 0 here is what makes the start independent of when the seq
             * happened to load. */
            struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, ev->element_id);
            if( el && el->anim_external )
            {
                el->anim_frame = 0;
                el->anim_cycle = 0;
                el->anim_external = false;
                ToriDraw_SceneAnimListInvalidate(app->scene);
            }
        }
    }
    World_EventsClear(world);
}

void
App_WorldDrainEntityRemoved(struct App* app)
{
    assert(app);
    /* No world before the first root rebuild is a legitimate boot state, not
     * a caller bug — the drain is simply a no-op then. */
    if( !app->world )
        return;
    App_WorldDrainEntityRemovedFor(app, app->world);
}

/*
 * Whether the water and lava texels are scrolled on the CPU this cycle.
 *
 * The GPU renderers (GLES2, GL3, D3D9) animate a texture in the shader from a
 * per-vertex scroll rate and a frame clock; they upload the texels once and
 * never read them again, so ToriDraw_TextureMapAnimate's per-cycle rotate of
 * every animated texture (128 KB of traffic per 128x128 texture) was dead
 * work on those lanes. The software rasteriser samples the texels directly
 * and still needs it. TORIRS_TEXANIM_CPU=1 forces the scroll on every lane
 * (the control arm on a GPU lane). Read once.
 */
static int
app_texture_anim_on_cpu(const struct App* app)
{
    static int forced = -1;

    assert(app);
    if( forced < 0 )
    {
        char const* v = getenv("TORIRS_TEXANIM_CPU");

        forced = (v && v[0] == '1') ? 1 : 0;
    }
    if( forced )
        return 1;
    return !app->renderer_animates_textures;
}

void
app_world_frame(
    struct App* app,
    int cycles,
    float frame_cycles)
{
    struct World* world = app->world;

    /*
     * World entities (sailing, SAILING_PLAN C1): one interpolation step for
     * every live view's boats — root first, nested views via the worklist —
     * with heights re-sampled from the terrain under each hull.
     *
     * Ahead of the world gate, not behind it, because this call is what
     * advances the cycle clock that WORLDENTITY_INFO stamps its targets
     * with. The deob bumps client.field742 unconditionally in doCycle; gate
     * it and a rebuild — exactly when boats are arriving — freezes the clock
     * while packets keep stamping targets against it, so every segment that
     * lands during the gap shares one enqueue cycle and the whole backlog
     * expires the instant the clock moves again. The walk below is a no-op
     * when no entity is live, which is the boot case this gate covers.
     */
    Wevs_Frame(&app->wevs, frame_cycles, app_wev_terrain_height, app);
    /* The bob rides the same clock Wevs_Frame just advanced. */
    app_wev_advance_bobs(app);

    if( !app->world_active || !app->world_view_valid || !world )
        return;

    /* Mirror the local pid so the render cycle's dynamic pass can register the
     * local player first (reference addPlayers(true) precedence). */
    world->local_pid = app->esync.local_pid;

    /*
     * Movement, then the cycle work -- rev-239's steady-state order, where a
     * rendered frame runs client.method2324 -> method1894 and the logic loop
     * then runs method3606 -> method3520 for the cycles that elapsed.
     *
     * Movement is integrated here rather than inside World_Cycle because one
     * call of this covers however much of a 20ms cycle the frame actually
     * took. Advancing per whole cycle instead rounds every frame's travel down,
     * which is what made a player following a moving NPC drift back and then
     * lurch forward. Ahead of World_Cycle so the painter dynamics it publishes
     * describe where the actors are now, not where they were a frame ago.
     */
    World_MoversAdvance(world, frame_cycles);
    /* Membership before registration (SAILING_PLAN C5.1): the actors have just
     * been moved, and World_Cycle's dynamic pass has to already know which of
     * them belong to a deck rather than to the mainland. */
    app_wev_route_actors(app);
    World_Cycle(world, cycles);
    /* Only the root world is cycled; a deck advances nothing of its own but
     * still needs its painter's dynamic half rebuilt every tick. */
    app_wev_cycle_views(app);
    World_LocChangesTick(world, cycles, app_loc_change_apply_cb, app);
    App_WorldDrainEntityRemoved(app);

    app_world_sync_positions(app);
    /* Exactly one of these does anything: the follow cam returns early while a
     * cutscene is up, and the cinema cam returns early when one is not. */
    app_world_camera_cinema(app);
    app_world_camera_follow(app);
    /* Publish the orbit angles to the CS2 host (CAM_GETANGLE_XA/YA, CAM_GETYAW)
     * and take back anything CAM_FORCEANGLE snapped since the last tick. Both
     * sides speak the reference's orbitCameraPitch/Yaw units, which is what
     * app->orbit.pitch and app->orbit.yaw already hold, so no conversion is involved.
     * Order matters: mirror first, then apply a force, so a snap issued this
     * tick is not read back as "the camera moved there on its own". */
    RS_CS2Host_SetCameraAngles(&app->host, app->orbit.pitch, app->orbit.yaw);
    {
        int forced_pitch, forced_yaw;
        if( RS_CS2Host_TakeCameraForce(&app->host, &forced_pitch, &forced_yaw) )
        {
            app->orbit.pitch = forced_pitch;
            app->orbit.yaw = forced_yaw & 0x7ff;
            app->orbit.pitch_velocity = 0;
            app->orbit.yaw_velocity = 0;
        }
    }
    app_world_sync_entity_animations(app);
    /*
     * TORIRS_ELEMENT_ALIAS_CHECK=1: assert that no two live world entities
     * reference the same scene element.
     *
     * Scene element ids are recycled, and three separate subsystems key off
     * them -- the model (AppEntitySpotanim), the animation (AppSeqBindPending)
     * and the POSITION written every frame from the entity that owns the
     * element. If a despawn ever leaves an entity holding an id that has since
     * been handed to somebody else, both write to it: the survivor's model gets
     * swapped, its animation replaced, and it is dragged around by the other
     * entity's movement. "I called my familiar and the Queen's head moved with
     * me" is that last symptom. The first two are fixed by identity checks; this
     * detector is what proves whether the underlying aliasing still happens.
     */
    if( app->world && torirs_env_element_alias_check() )
    {
        struct World_EntityPool* pools[2] = { &app->world->entities.player,
                                              &app->world->entities.npc };
        static int seen_element[8192];
        static int seen_kind[8192];
        static int stamp = 0;
        stamp++;
        for( int k = 0; k < 2; k++ )
        {
            for( int i = World_EntityPoolHead(pools[k]); i != WORLD_ENTITY_NIL;
                 i = World_EntityPoolNext(pools[k], i) )
            {
                /* Both entity structs open with `int element_id`. */
                int const el = *(int const*)World_EntityPoolGet(pools[k], i);
                if( el < 0 || el >= (int)(sizeof(seen_element) / sizeof(seen_element[0])) )
                    continue;
                if( seen_element[el] == stamp )
                    TORIRS_LOG(
                        "element_alias: element=%d claimed by TWO live entities "
                        "(kinds %d and %d) -- position/model/anim will fight\n",
                        el,
                        seen_kind[el],
                        k);
                seen_element[el] = stamp;
                seen_kind[el] = k;
            }
        }
    }
    app_world_sync_entity_spotanims(app);

    /* Expire P_LOCMERGE markers once the ride window ends. */
    {
        struct World_EntityPool* pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* p = World_EntityPoolGet(pool, i);
            if( !p || p->loc_merge_id < 0 )
                continue;
            if( world->cycle < p->loc_start_cycle || world->cycle >= p->loc_stop_cycle )
                p->loc_merge_id = -1;
        }
    }

    for( int c = 0; c < cycles; c++ )
        app_world_tick_animations(app);

    /* Texture scroll (water/lava): dat2 texture defs carry direction/speed;
     * the map advances them per elapsed cycle (v1 runescape.c:3893). */
    if( cycles > 0 && app_texture_anim_on_cpu(app) )
    {
        struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(app->scene);
        /* The rotate buffer is the caller's now -- see the header. This client
         * does animate textures, so it keeps the 64 KB it always had. */
        static int tex_anim_scratch[TORIDRAW_TEXTURE_ANIM_SCRATCH_INTS];
        if( tex_state )
            ToriDraw_TextureMapAnimate(
                &tex_state->texture_map,
                cycles,
                tex_anim_scratch,
                TORIDRAW_TEXTURE_ANIM_SCRATCH_INTS);
    }

    app->need_redraw = 1;
}
