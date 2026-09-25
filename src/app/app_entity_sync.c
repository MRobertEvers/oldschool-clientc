/*
 * The server's view of an entity applied to its scene element: animation
 * tracks, held items, and attached graphics.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_entity_spotanim_detach(
    struct App* app,
    struct AppEntitySpotanim* entry,
    bool restore);
static void
app_world_sync_one_entity_spotanim(
    struct App* app,
    struct WorldEntityFacet_EntitySpotanim const* spot,
    int element_id,
    int owner_entity_id);

/* Request a sequence load once (deduped through the entity seq tracker).
 *
 * The tracker is what stops this from re-queueing a load every world tick for
 * the whole of a sequence's load window: `ToriDraw_SceneAnimationHas` above only
 * goes true once the load LANDS, so between the request and the registration
 * that test says "missing" every tick.
 *
 * It is a fixed 64-entry table and it used to only *record* under the capacity
 * check while queueing unconditionally — so once 64 distinct entity sequences
 * had been seen in a session (an afternoon of walking and fighting passes that
 * easily; it is never pruned), the dedupe silently stopped working and every
 * tick queued another Dat2SequenceLoad for the same seq. Each one that landed
 * called ToriDraw_SceneAnimationAdd, which used to free the animation live
 * elements were already pointing at. Overwrite the oldest entry instead: the
 * table stays bounded, dedupe keeps working, and an evicted-but-still-loading
 * seq costs at worst one redundant task, which the registry now absorbs safely. */
void
app_request_entity_seq(
    struct App* app,
    int seq_id)
{
    int const capacity =
        (int)(sizeof(app->entity_seq_loads.seq_ids) / sizeof(app->entity_seq_loads.seq_ids[0]));
    struct ToriRS_Task* task;

    if( seq_id < 0 || ToriDraw_SceneAnimationHas(app->scene, seq_id) )
        return;
    for( int i = 0; i < app->entity_seq_loads.count; i++ )
        if( app->entity_seq_loads.seq_ids[i] == seq_id )
            return;
    if( app->entity_seq_loads.count < capacity )
    {
        app->entity_seq_loads.seq_ids[app->entity_seq_loads.count++] = seq_id;
    }
    else
    {
        memmove(
            &app->entity_seq_loads.seq_ids[0],
            &app->entity_seq_loads.seq_ids[1],
            (size_t)(capacity - 1) * sizeof(app->entity_seq_loads.seq_ids[0]));
        app->entity_seq_loads.seq_ids[capacity - 1] = seq_id;
    }
    task = CreateTask_SequenceLoad(app->provider, app->scene, seq_id);
    if( task )
        ToriRS_TaskQueue_Add(app->runner.queue, task);
}

/* Bind one entity's World animation state onto its scene element (reference
 * getTempModel2 selection: primary when playing and undelayed — with the
 * secondary bound alongside for the walkmerge blend when it is a real walk —
 * else the secondary alone). */
void
app_world_apply_entity_anim_tracks(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Animation const* anim,
    struct WorldEntityFacet_IdleAnimations const* idle)
{
    struct ToriDraw_SceneElement* el;
    int primary_active = anim->primary.anim_id != (uint16_t)-1 && anim->primary.anim_id != 0;
    int secondary_active = anim->secondary.anim_id != (uint16_t)-1 && anim->secondary.anim_id != 0;

    if( element_id < 0 || !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el )
        return;

    /* Frame sounds are NOT emitted from here. Entities are world-sim driven
     * (anim_external), and this runs once per rendered frame on whichever
     * single track ends up bound — so a sound was heard only when a render
     * happened to land on the frame carrying it, and the readyanim's sounds
     * went missing entirely for as long as an action animation covered it.
     * World_StepEntityAnimation announces every frame it crosses on both
     * tracks instead (app_world_anim_frame_sound is the listener), which is
     * where the reference emits them too. */

    if( primary_active )
        app_request_entity_seq(app, anim->primary.anim_id);
    if( secondary_active )
        app_request_entity_seq(app, anim->secondary.anim_id);

    if( primary_active && anim->primary.delay == 0 )
    {
        struct ToriDraw_Animation* pa =
            ToriDraw_SceneAnimationGet(app->scene, anim->primary.anim_id);
        /* Not registered yet: `app_request_entity_seq` above only queues the
         * load, so the first ticks after a spawn legitimately have no
         * animation. That is the caller's condition — the predicate asserts. */
        if( pa && ToriDraw_ElementAnimPlayable(pa) )
        {
            ToriDraw_ElementSetAnim(el, pa);
            el->anim_seq_id = anim->primary.anim_id;
            el->anim_frame = anim->primary.frame < pa->frame_count ? anim->primary.frame : 0;
            /* The walkmerge blend is a frame-animator operation (it masks
             * transform groups), so a skeletal primary never takes a secondary. */
            if( !pa->skeletal && secondary_active && idle &&
                anim->secondary.anim_id != (uint16_t)idle->readyanim )
            {
                struct ToriDraw_Animation* sa =
                    ToriDraw_SceneAnimationGet(app->scene, anim->secondary.anim_id);
                if( sa && sa->frame_count > 0 && sa->frames )
                {
                    el->secondary_animation = sa;
                    el->anim2_seq_id = anim->secondary.anim_id;
                    el->anim2_frame =
                        anim->secondary.frame < sa->frame_count ? anim->secondary.frame : 0;
                    return;
                }
            }
            el->secondary_animation = NULL;
            el->anim2_seq_id = -1;
            return;
        }
    }

    el->secondary_animation = NULL;
    el->anim2_seq_id = -1;
    if( secondary_active )
    {
        struct ToriDraw_Animation* sa =
            ToriDraw_SceneAnimationGet(app->scene, anim->secondary.anim_id);
        if( sa && ToriDraw_ElementAnimPlayable(sa) )
        {
            ToriDraw_ElementSetAnim(el, sa);
            el->anim_seq_id = anim->secondary.anim_id;
            el->anim_frame = anim->secondary.frame < sa->frame_count ? anim->secondary.frame : 0;
            return;
        }
    }

    ToriDraw_ElementSetAnim(el, NULL);
    el->anim_seq_id = -1;
    el->anim_frame = 0;
}

/* Push World animation state to the entity scene elements each frame (the
 * per-element modulo tick skips anim_external elements). */
void
app_world_sync_entity_animations(struct App* app)
{
    struct World_EntityPool* pool;

    pool = &app->world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( player )
        {
            app_world_apply_entity_anim_tracks(
                app, player->element_id, &player->animation, &player->idle_animations);
            app_world_reconcile_player_body(app, player);
        }
    }

    pool = &app->world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( npc )
            app_world_apply_entity_anim_tracks(
                app, npc->element_id, &npc->animation, &npc->idle_animations);
    }
}

/* ---- Entity attached-graphic (SPOTANIM mask): reference-accurate per-frame
 * Model.combine (ClientNpc/ClientPlayer.getTempModel). While an entity's
 * graphic is active its scene element's model is merge(body, spot): the body
 * part keeps its bones so the element's bound seq keeps animating it live in
 * the renderer, the spot part is pre-posed to the world-stepped spot frame
 * with its bones cleared (reference temp.labelFaces/labelVertices = null, so
 * the body seq cannot drive spot vertices) and raised by the wire height.
 * Assets load once through the async task pipeline; with both models resident
 * the combine itself is synchronous. Re-merged only when the spot frame
 * changes — between merges the visual is identical because the renderer poses
 * the body part live and the spot pose only advances with spot->frame. State
 * lives in app->entity_spotanims keyed by the body element id. ---- */

/* Find the entry for (element, owner). `owner_entity_id` 0 means "any owner",
 * used only when scanning for a free slot.
 *
 * An entry matching the element but NOT the owner is a recycled element id: the
 * previous owner despawned and this id was handed to somebody else. Drop it
 * WITHOUT restoring -- restoring would move the dead entity's body model onto
 * the new occupant. See the note on AppEntitySpotanim::owner_entity_id. */
struct AppEntitySpotanim*
app_entity_spotanim_find(
    struct App* app,
    int body_element_id,
    int owner_entity_id)
{
    int count = (int)(sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]));
    for( int i = 0; i < count; i++ )
    {
        struct AppEntitySpotanim* entry = &app->entity_spotanims[i];
        if( entry->body_element_id != body_element_id )
            continue;
        if( owner_entity_id != 0 && entry->body_element_id >= 0 &&
            entry->owner_entity_id != owner_entity_id )
        {
            app_entity_spotanim_detach(app, entry, false);
            return NULL;
        }
        return entry;
    }
    return NULL;
}

/* End the combine: restore the entity's own model (ownership of the pristine
 * body snapshot moves back to the element) and free the spot base. `restore`
 * is false when the body element is already gone (despawn/scene teardown). */
static void
app_entity_spotanim_detach(
    struct App* app,
    struct AppEntitySpotanim* entry,
    bool restore)
{
    if( restore && entry->body && ToriDraw_SceneElementIsLive(app->scene, entry->body_element_id) )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = entry->body;
        /* The renderer's per-frame AnimateReset needs captured originals. */
        ToriDraw_ModelCaptureOriginalVertices(entry->body);
        ToriDraw_SceneElementSetModel(app->scene, entry->body_element_id, hnd);
        entry->body = NULL; /* ownership moved to the element */
        app->need_redraw = 1;
    }
    if( entry->body )
        ToriDraw_ModelFree(entry->body);
    if( entry->spot )
        ToriDraw_ModelFree(entry->spot);
    *entry = (struct AppEntitySpotanim){ .body_element_id = -1, .owner_entity_id = 0 };
}

/* EntityRemoved drain hook: the entity element (and with it the combined
 * model) is going away — free the snapshots without touching the element. */
void
app_entity_spotanim_drop(
    struct App* app,
    int body_element_id)
{
    struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, body_element_id, 0);
    if( entry )
        app_entity_spotanim_detach(app, entry, false);
}

static void
app_world_sync_one_entity_spotanim(
    struct App* app,
    struct WorldEntityFacet_EntitySpotanim const* spot,
    int element_id,
    int owner_entity_id)
{
    struct World* world = app->world;
    struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, element_id, owner_entity_id);
    struct ToriRS_Spotanimtype* type;
    struct ToriDraw_Animation* anim;
    struct ToriDraw_SceneElement* el;
    int active = spot->id != -1 && world->cycle >= spot->last_cycle && spot->frame >= 0;
    int frame;
    int first_combine;

    if( !active )
    {
        if( entry )
            app_entity_spotanim_detach(app, entry, true);
        return;
    }
    /* Graphic replaced mid-flight: restore the body, rebuild for the new id. */
    if( entry && entry->spotanim_id != spot->id )
    {
        app_entity_spotanim_detach(app, entry, true);
        entry = NULL;
    }
    if( !entry )
    {
        entry = app_entity_spotanim_find(app, -1, 0);
        if( !entry )
            return; /* table full */
        *entry = (struct AppEntitySpotanim){
            .body_element_id = element_id,
            .owner_entity_id = owner_entity_id,
            .spotanim_id = spot->id,
            .applied_frame = -1,
        };
    }

    /* Asset gate: spotanimtype + model + seq must be resident. Kick the async
     * load chain once; the frame it lands the combine below runs synchronously
     * (reference precondition: SpotType.getTempModel2 assumes loaded).
     *
     * On the ASSET runner, not the exec one. The load has no completion step
     * and no place in the packet order — the residency test above is re-run
     * every frame and combines the graphic the frame it lands — while the exec
     * queue is strict FIFO, so parking it there stopped the world from
     * receiving packets for the round trips the load takes. On a streamed
     * cache (the browser's) that was most of the stall on the first attack
     * after a login: the combat graphics are the first spotanims a session
     * ever asks for. */
    type = CacheProvider_SpotanimtypeGet(app->provider, spot->id);
    anim = (type && type->seq >= 0) ? ToriDraw_SceneAnimationGet(app->scene, type->seq) : NULL;
    if( !type || !CacheProvider_ModelGet(app->provider, type->model) || !anim ||
        anim->frame_count <= 0 || !anim->frames || !anim->base )
    {
        if( !entry->load_enqueued )
        {
            struct Task_AppSpawn* task =
                app_spawn_task_new(app, APP_SPAWN_ENTITY_SPOTANIM, 0, 0, 0);
            task->spotanim_id = spot->id;
            task->entity_element_id = element_id;
            ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
            entry->load_enqueued = 1;
        }
        return;
    }

    if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) || !el->model.u.model.model )
        return;

    /* Snapshot the pristine body. Also re-snapshot when the element's model
     * changed under us — a held-item/appearance rebuild SetModel'd a fresh
     * body over our combined (`combined` is compared as an identity only,
     * never dereferenced: SetModel freed it). */
    if( !entry->body || el->model.u.model.model != entry->combined )
    {
        if( entry->body )
            ToriDraw_ModelFree(entry->body);
        /* The renderer poses the element model in place each draw; reset to
         * the rest pose so the snapshot is the true base. The element's model
         * no longer holds the pose the renderer last applied, and it must not
         * skip re-applying it. */
        ToriDraw_ModelAnimateReset(el->model.u.model.model);
        ToriDraw_SceneElementPoseInvalidate(app->scene, element_id);
        entry->body = ToriDraw_ModelCopy(el->model.u.model.model);
        entry->combined = NULL;
        entry->applied_frame = -1;
        if( !entry->body )
            return;
    }

    if( !entry->spot )
    {
        entry->spot = app_world_build_spotanim_model(app, type);
        if( !entry->spot )
            return;
    }

    frame = spot->frame < anim->frame_count ? spot->frame : anim->frame_count - 1;
    if( frame == entry->applied_frame && entry->combined )
        return; /* merged model already at this spot frame; body animates live */
    first_combine = entry->applied_frame < 0;

    {
        struct ToriDraw_Model* posed = ToriDraw_ModelCopy(entry->spot);
        struct ToriDraw_Model* parts[2];
        struct ToriDraw_Model* merged;
        if( !posed )
            return;
        /* Hole frames (missing archive) hold the rest pose, like the renderer. */
        if( anim->frames[frame].length > 0 )
            ToriDraw_ModelAnimateFrame(posed, anim->base, &anim->frames[frame]);
        /* Reference nulls the spot copy's labels before Model.combine. */
        ToriDraw_BonesFree(posed->vertex_bones);
        posed->vertex_bones = NULL;
        ToriDraw_BonesFree(posed->face_bones);
        posed->face_bones = NULL;
        /* Model y is negative-up: reference temp.translate(-spotanimHeight,0,0). */
        if( spot->height != 0 )
            ToriDraw_ModelTranslate(posed, 0, -spot->height, 0);

        parts[0] = entry->body;
        parts[1] = posed;
        merged = ToriDraw_ModelMerge(parts, 2);
        ToriDraw_ModelFree(posed);
        if( !merged )
            return;
        ToriDraw_ModelCaptureOriginalVertices(merged);
        {
            struct ToriDraw_ModelHandle hnd;
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = merged;
            ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
        }
        entry->combined = merged;
        entry->applied_frame = frame;
        app->need_redraw = 1;
        if( first_combine && getenv("TORIRS_ANIM_DEBUG") )
            TORIRS_LOG(
                "entity_spotanim: combine id=%d element=%d seq=%d frame=%d height=%d\n",
                spot->id,
                element_id,
                type->seq,
                frame,
                spot->height);
    }
}

void
app_world_sync_entity_spotanims(struct App* app)
{
    struct World_EntityPool* pool;
    int count = (int)(sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]));

    if( !app->world )
        return;

    /* Entries whose body element died outside the event drain (scene
     * teardown): free the snapshots. */
    for( int i = 0; i < count; i++ )
    {
        struct AppEntitySpotanim* entry = &app->entity_spotanims[i];
        if( entry->body_element_id >= 0 &&
            !ToriDraw_SceneElementIsLive(app->scene, entry->body_element_id) )
            app_entity_spotanim_detach(app, entry, false);
    }

    pool = &app->world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( player && player->element_id >= 0 )
            app_world_sync_one_entity_spotanim(
                app,
                &player->spotanim,
                player->element_id,
                WORLD_ENTITY_ID(WORLD_ENTITY_KIND_PLAYER, player->server_pid));
    }

    pool = &app->world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( npc && npc->multinpc_hidden && npc->element_id >= 0 )
        {
            struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, npc->element_id, 0);
            if( entry )
                app_entity_spotanim_detach(app, entry, false);
        }
        else if( npc && npc->element_id >= 0 )
            app_world_sync_one_entity_spotanim(
                app,
                &npc->spotanim,
                npc->element_id,
                WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, npc->server_slot));
    }
}

int
App_WorldSpawnSyncedPlayer(
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    return app_world_spawn_player_now(app, scene_x, scene_z, level);
}

int
App_WorldSpawnSyncedNpc(
    struct App* app,
    int npc_id,
    int base_npc_id,
    int scene_x,
    int scene_z,
    int level)
{
    return app_world_spawn_npc_now(app, npc_id, base_npc_id, scene_x, scene_z, level);
}

/* See the declaration in app.h. This is the resident-config half of the
 * reference NPCType.method461/getMultiNPC walk. Packet application uses the
 * async CreateTask_NpcMultiLoad wrapper above so the first lookup of a cold
 * shell cannot fail before the config has had a chance to load. */
int
App_NpctypeResolveMultiId(
    struct App* app,
    int npc_id)
{
    assert(app);

    for( int guard = 0; guard < TORIRS_NPC_MULTI_MAX_DEPTH && npc_id >= 0; guard++ )
    {
        struct ToriRS_Npctype* npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
        int resolved;

        if( !npctype || npctype->transform_count <= 0 )
            return npc_id;

        resolved = VarPManager_ResolveTransform(
            &app->varps,
            npctype->transforms,
            npctype->transform_count,
            npctype->transform_varbit,
            npctype->transform_varp);
        if( resolved < 0 )
            return -1;
        if( resolved == npc_id )
            return npc_id;
        npc_id = resolved;
    }
    return npc_id;
}
