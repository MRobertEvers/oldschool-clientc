/*
 * Syncing positions, animations and frame sounds from the simulation to the scene, once per frame.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static uint32_t
app_next_random(struct App* app);
static void
app_play_frame_sounds(
    struct App* app,
    const struct ToriDraw_Animation* anim,
    int current_frame,
    int world_x,
    int world_z);

/**
 * Position an actor that is standing in a non-root view (SAILING_PLAN C5.2).
 * Returns 1 when it handled the element, 0 when the actor is ashore and the
 * caller's ordinary root-space path applies.
 *
 * The element carries DECK-LOCAL coordinates and the actor's deck-local yaw;
 * C3's descent transform is what puts it back in root space at emit time,
 * composing the hull's yaw onto the element's. Writing root coordinates here
 * instead would rotate the actor around the boat twice. Height comes from the
 * DECK's heightmap, at the deck's own plane (@see app_wev_deck_level), so an
 * actor stands on the planking rather than on the sea floor beneath it — or,
 * as level 0 gave, inside the hull under the planking.
 */
int
app_world_sync_placement(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int element_id,
    int yaw,
    int actor_level)
{
    struct Worldview* view;
    struct Wev const* wev;
    int deck_yaw;
    int deck_y;

    assert(app);
    assert(placement);
    assert(element_id >= 0);

    if( placement->view_id == WORLDVIEW_ROOT )
        return 0;
    /* The view can die between the routing pass and this one only through
     * App_WevDespawn, which evicts first — so a stale id here would be a bug,
     * not a race. Guarded rather than asserted all the same: the alternative
     * is aborting the client over one frame of one actor. */
    if( !WorldviewRegistry_IsLive(&app->worldviews, placement->view_id) ||
        !Wevs_IsLive(&app->wevs, placement->view_id) )
        return 0;
    view = WorldviewRegistry_Get(&app->worldviews, placement->view_id);
    assert(view->world);
    wev = Wevs_Get(&app->wevs, placement->view_id);
    assert(wev);

    /* The element's yaw is composed with the hull's on the way out, so what
     * goes on the element is the actor's heading RELATIVE to the deck. A
     * HOMED actor's whole frame is already the deck's — their movement, and
     * so the yaw the mover derives from it, happens in view-local space — so
     * their yaw goes on unrotated; only a projected (footprint-routed) actor
     * carries a root-frame heading that needs the hull's yaw taken out. */
    /* The legacy NPC bridge projects positions but still sends each crew
     * member's native deck-facing direction (NPC_INFO face_dir). */
    deck_yaw =
        actor_level < 0 || (placement->home_view == placement->view_id && placement->home_view != 0)
            ? yaw & 0x7ff
            : (yaw - wev->angle) & 0x7ff;
    /* The deck plane: a PLAYER stands at their own wire plane inside the
     * boat's world — the deob copies the PLAYER_INFO coordinate's plane
     * verbatim (class60.field552) and samples the sub-view's heights there;
     * the config's plane opcode is only ever a menu/click plane selector.
     * A caller passing -1 (npcs) gets the config plane, matching the deob's
     * npc rule (class86 never overrides getPlane, so an npc reads the
     * view's plane). Boarding at level 0 therefore KEEPS you at level 0. */
    {
        int deck_level = actor_level;

        if( deck_level < 0 )
            deck_level = app_wev_deck_level(app, placement->view_id);
        if( deck_level >= COLLISION_LEVELS )
            deck_level = COLLISION_LEVELS - 1;
        deck_y = World_HeightAt(view->world, placement->x, placement->z, deck_level);
    }
    ToriDraw_SceneElementSetPosition(
        app->scene, element_id, placement->x, deck_y, placement->z, deck_yaw);
    return 1;
}

/* Movers (players/npcs) and in-flight projectiles push their sim positions
 * into the scene elements the frame emitter draws (v1 synced projectiles;
 * movers were spawn-time only there because nothing pathed them). */
void
app_world_sync_positions(struct App* app)
{
    struct World* world = app->world;
    struct World_EntityPool* pool;
    /* Probed once: the two prints below run for every player and every npc in
     * the scene, every frame. */
    static int npcpos_debug = -1;
    if( npcpos_debug < 0 )
        npcpos_debug = getenv("TORIRS_NPCPOS_DEBUG") != NULL;
    /* Reference getAvH(minusedlevel, …) — all movers sit on the local plane. */
    int local_level = app_cinema_level(app);

    pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( !player || player->element_id < 0 )
            continue;
        /* Rebuild-parked movers sit outside the scene until the server's
         * next info packet removes them; the heightmap has no data there. */
        if( player->grid_position.x < 0 || player->grid_position.z < 0 ||
            player->grid_position.x >= world->_scene_size ||
            player->grid_position.z >= world->_scene_size )
            continue;
        int wx = (int)player->draw_position.x;
        int wz = (int)player->draw_position.z;
        int wy;
        if( app_world_sync_placement(
                app,
                &player->view_placement,
                player->element_id,
                player->orientation.yaw,
                player->grid_position.level) )
            continue;
        wy = app_world_height(app, wx, wz, local_level);
        if( npcpos_debug )
            TORIRS_LOG(
                "plrpos: tile=%d,%d lvl=%d(local %d) w=%d,%d y=%d\n",
                player->grid_position.x,
                player->grid_position.z,
                player->grid_position.level,
                local_level,
                wx,
                wz,
                wy);
        ToriDraw_SceneElementSetPosition(
            app->scene, player->element_id, wx, wy, wz, player->orientation.yaw);
    }

    pool = &world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( !npc || npc->element_id < 0 )
            continue;
        if( npc->grid_position.x < 0 || npc->grid_position.z < 0 ||
            npc->grid_position.x >= world->_scene_size ||
            npc->grid_position.z >= world->_scene_size )
            continue;
        int wx = (int)npc->draw_position.x;
        int wz = (int)npc->draw_position.z;
        int wy;
        /* -1: npcs take the deck's config plane, the deob's rule (class86
         * does not override getPlane, so an npc reads its view's plane). */
        if( app_world_sync_placement(
                app, &npc->view_placement, npc->element_id, npc->orientation.yaw, -1) )
            continue;
        wy = app_world_height(app, wx, wz, local_level);
        if( npcpos_debug )
            TORIRS_LOG(
                "npcpos: id=%d tile=%d,%d lvl=%d(local %d) w=%d,%d y=%d size=%d "
                "h0=%d h1=%d h2=%d lb=%d\n",
                npc->npc_id,
                npc->grid_position.x,
                npc->grid_position.z,
                npc->grid_position.level,
                local_level,
                wx,
                wz,
                wy,
                npc->size,
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 0),
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 1),
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 2),
                (World_TileFlagGet(app->world, wx >> 7, wz >> 7, 1) & RSCACHE_FLOFLAG_LINK_BELOW) !=
                    0);
        ToriDraw_SceneElementSetPosition(
            app->scene, npc->element_id, wx, wy, wz, npc->orientation.yaw);
    }

    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* proj = World_EntityPoolGet(pool, i);
        if( !proj || proj->element_id < 0 || !proj->launched )
            continue;
        ToriDraw_SceneElementSetPositionPitchYaw(
            app->scene,
            proj->element_id,
            (int)proj->x,
            (int)proj->y,
            (int)proj->z,
            proj->orientation.pitch,
            proj->orientation.yaw);
    }
}

/*
 * Sequence-frame-sound picker RNG.
 *
 * The reference uses `Math.random()`. A fixed-seed LCG is used instead so a
 * headless run picks the same alternatives every time -- the screenshot and BMP
 * harnesses compare frames, and a genuinely random audio path would still be a
 * genuinely random *counter* in the audio ledger `TORIRS_AUDIO_DEBUG` prints.
 * The sequence is long enough that no frame's alternatives correlate.
 */
static uint32_t g_frame_sound_rng = 0x9e3779b9u;

static uint32_t
app_next_random(struct App* app)
{
    (void)app;
    g_frame_sound_rng = g_frame_sound_rng * 1664525u + 1013904223u;
    return g_frame_sound_rng >> 16;
}

/*
 * Play a frame sound for a sequence animation when its frame advances.
 *
 * `world_x`/`world_z` are the element's position in world units, so the sound is
 * attenuated and panned from where the thing making it is standing -- a smithing
 * hammer three squares away should not be as loud as one under the camera. Pass
 * -1 for a sound with no place in the scene.
 *
 * A frame may declare **several alternative** sounds and exactly one of them
 * plays, chosen in proportion to the entries' weights (rev226+; 67 osrs239
 * frames carry up to six). The map is sorted by frame index with repeats, so the
 * alternatives for a frame are a contiguous run -- the binary search finds *a*
 * member of that run and the run has to be widened from there. Stopping at the
 * first hit, which is what this used to do, made the choice a function of where
 * the search happened to land.
 */
static void
app_play_frame_sounds(
    struct App* app,
    const struct ToriDraw_Animation* anim,
    int current_frame,
    int world_x,
    int world_z)
{
    int left = 0;
    int right;
    int hit = -1;
    int first;
    int last;
    int total_weight = 0;
    int roll;
    int chosen;
    struct ToriDraw_AnimFrameSound const* sound;

    assert(anim);
    if( anim->frame_sounds.count <= 0 )
        return;
    assert(app);

    right = anim->frame_sounds.count - 1;
    while( left <= right )
    {
        int mid = (left + right) / 2;
        int frame_idx = anim->frame_sounds.frame_indices[mid];
        if( frame_idx == current_frame )
        {
            hit = mid;
            break;
        }
        if( frame_idx < current_frame )
            left = mid + 1;
        else
            right = mid - 1;
    }
    if( hit < 0 )
        return;

    first = hit;
    while( first > 0 && anim->frame_sounds.frame_indices[first - 1] == current_frame )
        first--;
    last = hit;
    while( last + 1 < anim->frame_sounds.count &&
           anim->frame_sounds.frame_indices[last + 1] == current_frame )
        last++;

    chosen = first;
    if( last > first )
    {
        /*
         * Weights are relative, and an entry that declares none (-1, the pre-226
         * shape) counts as one -- otherwise a single unweighted alternative in a
         * weighted run could never be picked.
         */
        for( int i = first; i <= last; i++ )
            total_weight +=
                anim->frame_sounds.sounds[i].weight > 0 ? anim->frame_sounds.sounds[i].weight : 1;
        roll = (int)(app_next_random(app) % (uint32_t)(total_weight > 0 ? total_weight : 1));
        for( int i = first; i <= last; i++ )
        {
            int w =
                anim->frame_sounds.sounds[i].weight > 0 ? anim->frame_sounds.sounds[i].weight : 1;
            if( roll < w )
            {
                chosen = i;
                break;
            }
            roll -= w;
        }
    }

    sound = &anim->frame_sounds.sounds[chosen];
    if( sound->id < 0 )
        return;
    if( world_x >= 0 )
        RS_Audio_SynthAt(
            &app->audio, sound->id, sound->loops, 0, world_x >> 7, world_z >> 7, sound->radius, 0);
    else
        RS_Audio_Synth(&app->audio, sound->id, sound->loops, 0);
}

/*
 * World_AnimSoundSink: one entity animation frame, as the world steps onto it.
 *
 * The world knows nothing about the animation registry, so resolving the seq is
 * this side's job -- and a seq whose async load has not landed yet is an
 * ordinary runtime state, not a caller bug: the entity holds frame 0 of it and
 * simply makes no sound until it arrives.
 */
void
app_world_anim_frame_sound(
    void* userdata,
    int seq_id,
    int frame,
    int world_x,
    int world_z)
{
    struct App* app = userdata;
    struct ToriDraw_Animation* anim;

    assert(app);
    anim = ToriDraw_SceneAnimationGet(app->scene, seq_id);
    if( !anim )
        return;
    app_play_frame_sounds(app, anim, frame, world_x, world_z);
}

/* One client tick of scene-element animation frames. UITreeAnim only advances
 * UI model widgets; world scene elements (scenery + entities) advance here
 * (v1 GameRunescape_TickAnimations, both classic and skeletal branches). */
void
app_world_tick_animations(struct App* app)
{
    /* Only elements with a seq bound, rather than every slot in a pool that is
     * overwhelmingly static scenery. The list is a hint (it can hold ids that
     * have since died or lost their seq), so the per-element checks below still
     * stand — they are just no longer paid once per slot per cycle. */
    int anim_count = 0;
    int const* anim_ids = ToriDraw_SceneAnimatedElements(app->scene, &anim_count);
    TORIRS_PERF_COUNT_SET(
        TORIRS_PERF_CTR_SCENE_ELEMENTS,
        app->scene ? ToriDraw_SceneElementSlotCount(app->scene) : 0);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_SCENE_ANIM_LIST, anim_count);
    if( app->provider )
    {
        TORIRS_PERF_COUNT_SET(
            TORIRS_PERF_CTR_CACHE_MODEL_SIZE,
            app->provider->model_cache ? (int64_t)app->provider->model_cache->size : 0);
        TORIRS_PERF_COUNT_SET(
            TORIRS_PERF_CTR_CACHE_SPRITE_SIZE,
            app->provider->sprite_cache ? (int64_t)app->provider->sprite_cache->size : 0);
    }
    for( int k = 0; k < anim_count; k++ )
    {
        int element_id = anim_ids[k];
        struct ToriDraw_SceneElement* element;

        if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
            continue;
        element = ToriDraw_SceneElementGet(app->scene, element_id);
        if( !element || element->anim_seq_id == -1 )
            continue;
        /* Entity elements: the world sim steps their frames (delay/loop/
         * priority semantics) — the naive modulo tick must not touch them. */
        if( element->anim_external )
            continue;

        if( element->is_skeletal )
        {
            const struct ToriDraw_SkeletalAnim* skeletal = element->skeletal_animation;
            const struct ToriDraw_Animation* anim = element->animation;
            int play_frames;
            if( !skeletal || skeletal->frame_count <= 0 )
                continue;
            play_frames = element->skeletal_play_frames;
            if( play_frames <= 0 || play_frames > skeletal->frame_count )
                play_frames = skeletal->frame_count;
            element->anim_cycle++;
            if( element->anim_cycle >= 1 )
            {
                /* Location DynamicObjects do not modulo-wrap: frameStep
                 * decides whether the final pose is retained or the sequence
                 * is discarded. Keep the skeletal playback span as the
                 * authoritative bound when the config limits it. */
                if( anim && anim->frame_count == play_frames )
                {
                    if( !ToriDraw_AnimationAdvanceObjectFrame(anim, &element->anim_frame) )
                        ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
                }
                else
                    element->anim_frame = (element->anim_frame + 1) % play_frames;
                element->anim_cycle = 0;
            }
        }
        else
        {
            const struct ToriDraw_Animation* anim = element->animation;
            if( !anim || anim->frame_count <= 0 || !anim->frames )
                continue;
            {
                int old_frame = element->anim_frame;

                if( element->anim_loop )
                    ToriDraw_AnimationAdvanceLoopCycles(
                        anim, &element->anim_frame, &element->anim_cycle, 1);
                else if( !ToriDraw_AnimationAdvanceObjectCycles(
                             anim, &element->anim_frame, &element->anim_cycle, 1) )
                    ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
                /* Play any frame sounds for the new frame. A finished
                 * DynamicObject has no sequence, so it cannot emit another. */
                if( element->anim_seq_id != -1 && element->anim_frame != old_frame )
                    app_play_frame_sounds(
                        app,
                        anim,
                        element->anim_frame,
                        element->world_position.x,
                        element->world_position.z);
            }
        }
    }
}
