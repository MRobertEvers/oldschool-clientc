/*
 * The server's view of an entity applied to its scene element: animation
 * tracks, held items, and attached graphics.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* An entry's graphic slots: the rev-239 actor's, a player's as an npc's. */
#define APP_ENTITY_SPOT_SLOTS ((int)(sizeof(((struct AppEntitySpotanim*)0)->slots) / \
                                     sizeof(((struct AppEntitySpotanim*)0)->slots[0])))
_Static_assert(sizeof(((struct AppEntitySpotanim*)0)->slots) /
                       sizeof(((struct AppEntitySpotanim*)0)->slots[0]) ==
                   WORLD_NPC_SPOTANIM_SLOTS,
               "one combine slot per world graphic slot");
_Static_assert(WORLD_PLAYER_SPOTANIM_SLOTS == WORLD_NPC_SPOTANIM_SLOTS,
               "a player's graphic slots combine as an npc's");

/* Private to this unit, declared up front so definition order is free. */
static void
app_entity_spotanim_detach(
    struct App* app,
    struct AppEntitySpotanim* entry,
    bool restore);
/* The whole asset chain an attached graphic needs before it can be combined:
 * the spotanimtype, its model, and a playable seq. One test for the per-frame
 * combine below and for the packet-time prefetch, so the two cannot disagree
 * about what "loaded" means. */
static int
app_entity_spotanim_resident(
    struct App* app,
    int spotanim_id)
{
    struct ToriRS_Spotanimtype* type;
    struct ToriDraw_Animation* anim;

    assert(app);
    type = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !type || !CacheProvider_ModelGet(app->provider, type->model) )
        return 0;
    anim = type->seq >= 0 ? ToriDraw_SceneAnimationGet(app->scene, type->seq) : NULL;
    /* Classic (frames + framemap) or skeletal (animaya): Kephri's dung mark
     * (`kephri_indicate_player_for_dung`, the scarab swarm on the raider) plays
     * a skeletal seq, and requiring `frames` here kept it from ever counting as
     * loaded, so it was never drawn. */
    return anim && anim->frame_count > 0 && ((anim->frames && anim->base) || anim->skeletal);
}

static void
app_entity_spotanim_load(
    struct App* app,
    int spotanim_id,
    int element_id)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_ENTITY_SPOTANIM, 0, 0, 0);
    task->spotanim_id = spotanim_id;
    task->entity_element_id = element_id;
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
}

void
App_EntitySpotanimPrefetch(
    struct App* app,
    int spotanim_id)
{
    assert(app);
    assert(spotanim_id >= 0);
    if( app_entity_spotanim_resident(app, spotanim_id) )
        return;
    app_entity_spotanim_load(app, spotanim_id, -1);
}

static void
app_world_sync_one_entity_spotanim(
    struct App* app,
    struct WorldEntityFacet_EntitySpotanim const* spots,
    int spot_count,
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
    /* A parked ANIM (WorldEntityFacet_Animation.pending_anim_id) is judged the
     * cycle its seq registers; keep asking for it until then. */
    if( anim->pending_set )
        app_request_entity_seq(app, anim->pending_anim_id);
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
 * is false when the body element is already gone (despawn/scene teardown).
 *
 * Restore only while the element is still wearing `combined`. The body
 * reconcile runs first in the frame and SetModels a fresh body when a seq's
 * replaceheldright/left ends (home teleport's book) or the appearance
 * changes. That frees `combined`. Putting the snapshot back on top would
 * resurrect the hand item, and the reconcile would keep it: body.slots
 * already match the appearance it just wrote, so the next frame is a no-op. */
static void
app_entity_spotanim_detach(
    struct App* app,
    struct AppEntitySpotanim* entry,
    bool restore)
{
    struct ToriDraw_SceneElement* el = NULL;
    int wearing_combined = 0;

    if( ToriDraw_SceneElementIsLive(app->scene, entry->body_element_id) )
    {
        el = ToriDraw_SceneElementGet(app->scene, entry->body_element_id);
        assert(el);
        /* `combined` is an identity only. A rebuild this frame freed it. */
        wearing_combined = ToriDraw_ModelKindIsFull(el->model.kind) &&
                           el->model.u.model.model == entry->combined;
    }
    if( restore && entry->body && wearing_combined )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = entry->body;
        /* The renderer's per-frame AnimateReset needs captured originals. */
        ToriDraw_ModelCaptureOriginalVertices(entry->body);
        ToriDraw_SceneElementSetModel(app->scene, entry->body_element_id, hnd);
        /* The snapshot was taken at the bind pose; the overlays measure it
         * before the renderer runs. */
        app_element_pose_after_model_swap(app, entry->body_element_id);
        entry->body = NULL; /* ownership moved to the element */
        app->need_redraw = 1;
    }
    if( entry->body )
        ToriDraw_ModelFree(entry->body);
    for( int i = 0; i < APP_ENTITY_SPOT_SLOTS; i++ )
        if( entry->slots[i].spot )
            ToriDraw_ModelFree(entry->slots[i].spot);
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

/* The scale (128 == 1.0) that a later resize by `resize` takes back to 128. */
static int
app_entity_spotanim_unscale(int resize)
{
    assert(resize > 0);
    return (128 * 128 + resize / 2) / resize;
}

/*
 * Can the graphic be pre-scaled by 128*128/resize for `body`'s resize and
 * still fit int16 vertices? Not for an extreme resize: Verzik's pillar npc is
 * `resizeh=1` (a sliver, the loc beside it is the column you see) and its
 * impact graphic 1582 is ~920 units across at its own resize 520, so the
 * pre-scale is x128 and every vertex past 255 wrapped - the lightning on a hit
 * pillar drew as garbage (2026-10-18, `ev_swing --bounds`).
 */
static int
app_entity_spotanim_unscale_fits(
    struct ToriDraw_Model const* spot,
    struct ToriDraw_Model const* body)
{
    int scale[3] = { app_entity_spotanim_unscale(body->post_resize_x),
                     app_entity_spotanim_unscale(body->post_resize_height),
                     app_entity_spotanim_unscale(body->post_resize_z) };

    for( int i = 0; i < spot->vertex_count; i++ )
    {
        int v[3] = { spot->vertices_x[i], spot->vertices_y[i], spot->vertices_z[i] };

        for( int k = 0; k < 3; k++ )
        {
            long scaled = (long)v[k] * scale[k] / 128;

            if( scaled > 32767 || scaled < -32768 )
                return 0;
        }
    }
    return 1;
}


/* The posed, height-raised, label-free copy of one slot's graphic at `frame`
 * (reference SpotType.getTempModel2 + temp.translate). Owned by the caller. */
static struct ToriDraw_Model*
app_entity_spotanim_pose(
    struct AppEntitySpotanimSlot const* slot,
    struct ToriDraw_Animation const* anim,
    int frame,
    int height)
{
    struct ToriDraw_Model* posed = ToriDraw_ModelCopy(slot->spot);

    assert(posed);
    /* Pose from the bind pose. The cached spot base is already in render
     * scale, and ToriDraw_ModelAnimateFrame applies the spot's resize
     * again after posing: animating it in place drew a resized graphic
     * (tzhaar_heal, 200) at 200*200/128. Hole frames (missing archive)
     * hold the rest pose, like the renderer. */
    ToriDraw_ModelAnimateReset(posed);
    if( anim->skeletal )
    {
        struct ToriDraw_SkeletalAnim const* skeletal = anim->skeletal;
        /* As the scene's own skeletal pose (toridraw_scene.c): a model with no
         * animaya skin holds its rest pose rather than fault on the arrays. */
        if( skeletal->frame_count > 0 && posed->animaya_vertex_count > 0 &&
            posed->animaya_group_counts && posed->animaya_groups && posed->animaya_scales )
        {
            if( !posed->original_vertices_x )
                ToriDraw_ModelCaptureOriginalVertices(posed);
            ToriDraw_ModelAnimateSkeletal(
                posed, skeletal, frame < skeletal->frame_count ? frame : skeletal->frame_count - 1);
        }
    }
    else if( anim->frames[frame].length > 0 )
        ToriDraw_ModelAnimateFrame(posed, anim->base, &anim->frames[frame]);
    else
        ToriDraw_ModelApplyPostTransforms(posed);
    /* Reference nulls the spot copy's labels before Model.combine. */
    ToriDraw_BonesFree(posed->vertex_bones);
    posed->vertex_bones = NULL;
    ToriDraw_BonesFree(posed->face_bones);
    posed->face_bones = NULL;
    /* And its animaya skin. The pre-z-buffer client poses the spot at its own
     * frame and combines it into the npc's already-posed model, which is never
     * animated again (rev-184 NPC.getModel: SpotAnimationDefinition.getModel(
     * spotAnimationFrame), offsetBy, new Model({body, spot}, 2)). Here the
     * renderer re-poses the merge with the BODY's seq, so a graphic skinned on
     * its own rig was posed again by the body's bones: the Wardens' phase-three
     * shards (45289, 14 groups) flew with Tumeken's Warden's arms in every
     * phase. With no groups, the merge's spot vertices hold this pose. */
    ToriDraw_ModelAnimayaFree(posed);
    /* Model y is negative-up: reference temp.translate(-spotanimHeight,0,0). */
    if( height != 0 )
        ToriDraw_ModelTranslate(posed, 0, -height, 0);
    return posed;
}

/* Put the pristine body back on the element and keep the entry (its slots'
 * load state) -- every graphic it carries is still loading. */
static void
app_entity_spotanim_unmerge(
    struct App* app,
    struct AppEntitySpotanim* entry)
{
    struct ToriDraw_SceneElement* el;

    if( !entry->combined || !entry->body ||
        !ToriDraw_SceneElementIsLive(app->scene, entry->body_element_id) )
        return;
    el = ToriDraw_SceneElementGet(app->scene, entry->body_element_id);
    if( ToriDraw_ModelKindIsFull(el->model.kind) && el->model.u.model.model == entry->combined )
    {
        struct ToriDraw_ModelHandle hnd;

        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = entry->body;
        ToriDraw_ModelCaptureOriginalVertices(entry->body);
        ToriDraw_SceneElementSetModel(app->scene, entry->body_element_id, hnd);
        app_element_pose_after_model_swap(app, entry->body_element_id);
        entry->body = NULL; /* ownership moved to the element */
        app->need_redraw = 1;
    }
    entry->combined = NULL;
    entry->merged_mask = 0;
    for( int i = 0; i < APP_ENTITY_SPOT_SLOTS; i++ )
        entry->slots[i].applied_frame = -1;
}

/*
 * One entity's attached graphics, one per rev-239 slot (a player has one).
 *
 * The rev-239 actor keeps its graphics in slots and draws every one that is
 * playing (class106.getSpotAnims): a Tormented Demon's shield loop in slot 4
 * and its claw arc in slot 2 on the same frame. So the element's model is
 * merge(body, spot[0], spot[1], ...) over every slot whose graphic is due and
 * resident; a slot still loading is left out and the rest drawn.
 */
static void
app_world_sync_one_entity_spotanim(
    struct App* app,
    struct WorldEntityFacet_EntitySpotanim const* spots,
    int spot_count,
    int element_id,
    int owner_entity_id)
{
    struct World* world = app->world;
    struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, element_id, owner_entity_id);
    struct ToriDraw_SceneElement* el;
    struct ToriDraw_Animation* anims[APP_ENTITY_SPOT_SLOTS] = { 0 };
    int frames[APP_ENTITY_SPOT_SLOTS];
    int active_mask = 0;
    int draw_mask = 0;
    int changed;
    int first_combine;

    assert(spot_count > 0);
    assert(spot_count <= APP_ENTITY_SPOT_SLOTS);
    for( int i = 0; i < spot_count; i++ )
        if( spots[i].id != -1 && world->cycle >= spots[i].last_cycle && spots[i].frame >= 0 )
            active_mask |= 1 << i;

    if( !active_mask )
    {
        if( entry )
            app_entity_spotanim_detach(app, entry, true);
        return;
    }
    if( !entry )
    {
        entry = app_entity_spotanim_find(app, -1, 0);
        if( !entry )
            return; /* table full */
        *entry = (struct AppEntitySpotanim){
            .body_element_id = element_id,
            .owner_entity_id = owner_entity_id,
        };
        for( int i = 0; i < APP_ENTITY_SPOT_SLOTS; i++ )
            entry->slots[i] = (struct AppEntitySpotanimSlot){ .spotanim_id = -1,
                                                              .applied_frame = -1 };
    }

    /* A slot whose graphic changed (replaced, or ended) drops its model; the
     * body snapshot stays, and the merge below is rebuilt without it. */
    for( int i = 0; i < APP_ENTITY_SPOT_SLOTS; i++ )
    {
        struct AppEntitySpotanimSlot* slot = &entry->slots[i];
        int want = (active_mask & (1 << i)) ? spots[i].id : -1;

        if( slot->spotanim_id == want )
            continue;
        if( slot->spot )
            ToriDraw_ModelFree(slot->spot);
        *slot = (struct AppEntitySpotanimSlot){ .spotanim_id = want, .applied_frame = -1 };
    }

    /* Asset gate, per slot: spotanimtype + model + seq must be resident. Kick
     * the async load chain once; the frame it lands the combine below takes
     * it in (reference precondition: SpotType.getTempModel2 assumes loaded).
     *
     * On the ASSET runner, not the exec one. The load has no completion step
     * and no place in the packet order — the residency test above is re-run
     * every frame and combines the graphic the frame it lands — while the exec
     * queue is strict FIFO, so parking it there stopped the world from
     * receiving packets for the round trips the load takes. On a streamed
     * cache (the browser's) that was most of the stall on the first attack
     * after a login: the combat graphics are the first spotanims a session
     * ever asks for. */
    for( int i = 0; i < spot_count; i++ )
    {
        struct AppEntitySpotanimSlot* slot = &entry->slots[i];
        struct ToriRS_Spotanimtype* type;

        if( !(active_mask & (1 << i)) )
            continue;
        if( !app_entity_spotanim_resident(app, slot->spotanim_id) )
        {
            /* Normally already in flight: the packet that set the graphic
             * prefetched it (App_EntitySpotanimPrefetch), so the start delay
             * covers the load. This is the fallback for anything that did
             * not. */
            if( !slot->load_enqueued )
            {
                app_entity_spotanim_load(app, slot->spotanim_id, element_id);
                slot->load_enqueued = 1;
            }
            continue;
        }
        type = CacheProvider_SpotanimtypeGet(app->provider, slot->spotanim_id);
        if( !slot->spot )
            slot->spot = app_world_build_spotanim_model(app, type);
        if( !slot->spot )
            continue;
        anims[i] = ToriDraw_SceneAnimationGet(app->scene, type->seq);
        frames[i] = spots[i].frame < anims[i]->frame_count ? spots[i].frame
                                                           : anims[i]->frame_count - 1;
        draw_mask |= 1 << i;
    }

    if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) || !el->model.u.model.model )
        return;
    if( !draw_mask )
    {
        app_entity_spotanim_unmerge(app, entry);
        return;
    }

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
        entry->merged_mask = 0;
        for( int i = 0; i < APP_ENTITY_SPOT_SLOTS; i++ )
            entry->slots[i].applied_frame = -1;
        if( !entry->body )
            return;
    }

    changed = !entry->combined || entry->merged_mask != draw_mask;
    for( int i = 0; i < spot_count && !changed; i++ )
        if( (draw_mask & (1 << i)) && frames[i] != entry->slots[i].applied_frame )
            changed = 1;
    if( !changed )
        return; /* merged model already at these spot frames; body animates live */
    first_combine = !entry->combined;

    {
        struct ToriDraw_Model* parts[1 + APP_ENTITY_SPOT_SLOTS];
        struct ToriDraw_Model* posed[APP_ENTITY_SPOT_SLOTS];
        int posed_count = 0;
        struct ToriDraw_Model* merged;
        struct ToriDraw_Model* baked = NULL;
        int carry_resize = entry->body->post_resize;

        for( int i = 0; i < spot_count; i++ )
            if( draw_mask & (1 << i) )
                posed[posed_count++] =
                    app_entity_spotanim_pose(&entry->slots[i], anims[i], frames[i], spots[i].height);

        /* The body is at its authored size and the renderer re-poses it every
         * frame, resizing after the pose (NpcType.getModel's order), so the
         * merge must carry the body's resize or a resized npc draws at 128 for
         * as long as the graphic is on it (JalTok-Jad, 110, grew by 128/110
         * under every Yt-HurKot heal). That resize then lands on the spot part
         * too, which the reference combines at its own scale: undo it here. */
        assert(entry->body->post_orient == 0);
        assert(entry->body->post_offset_x == 0);
        assert(entry->body->post_offset_y == 0);
        assert(entry->body->post_offset_z == 0);
        /* When the undo cannot fit int16 (`app_entity_spotanim_unscale_fits`)
         * for any graphic, the body takes its resize into its own vertices
         * instead and the merge carries no post-resize: every graphic then
         * stands at its own scale, as the reference combines it. Exact for a
         * body that does not animate (the pillar); a resized body that does is
         * posed after its resize rather than before - the only case this path
         * is taken. */
        for( int k = 0; carry_resize && k < posed_count; k++ )
            if( !app_entity_spotanim_unscale_fits(posed[k], entry->body) )
                carry_resize = 0;
        if( entry->body->post_resize && !carry_resize )
        {
            baked = ToriDraw_ModelCopy(entry->body);
            assert(baked);
            ToriDraw_ModelScale(
                baked,
                entry->body->post_resize_x,
                entry->body->post_resize_z,
                entry->body->post_resize_height);
            ToriDraw_ModelSetPostResize(baked, 128, 128, 128);
        }
        else if( carry_resize )
            for( int k = 0; k < posed_count; k++ )
                ToriDraw_ModelScale(
                    posed[k],
                    app_entity_spotanim_unscale(entry->body->post_resize_x),
                    app_entity_spotanim_unscale(entry->body->post_resize_z),
                    app_entity_spotanim_unscale(entry->body->post_resize_height));

        parts[0] = baked ? baked : entry->body;
        for( int k = 0; k < posed_count; k++ )
            parts[1 + k] = posed[k];
        merged = ToriDraw_ModelMerge(parts, 1 + posed_count);
        for( int k = 0; k < posed_count; k++ )
            ToriDraw_ModelFree(posed[k]);
        if( baked )
            ToriDraw_ModelFree(baked);
        if( !merged )
            return;
        ToriDraw_ModelCaptureOriginalVertices(merged);
        if( carry_resize )
            ToriDraw_ModelSetPostResize(
                merged,
                entry->body->post_resize_x,
                entry->body->post_resize_z,
                entry->body->post_resize_height);
        /* Render scale for a frame the renderer does not pose, as
         * app_world_build_model leaves a fresh body - and its cylinder with
         * it. ToriDraw_ModelMerge measured the pre-resize vertices, which for
         * Verzik's pillar (resizeh=1) are the lightning pre-scaled x128: a
         * 32,524 radius that culled the whole element, and the pillar has no
         * animation, so the renderer never re-posed it and never re-measured.
         * Every pillar hit's lightning was there and never drawn. */
        ToriDraw_ModelApplyPostTransforms(merged);
        ToriDraw_ModelSetBoundsCylinder(merged);
        {
            struct ToriDraw_ModelHandle hnd;
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = merged;
            ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
        }
        /* Mounted at the bind pose, and the health bar, splats and overhead
         * text read its height before the renderer poses it -- once per spot
         * frame, so unposed this flickered for the whole graphic. */
        app_element_pose_after_model_swap(app, element_id);
        entry->combined = merged;
        entry->merged_mask = draw_mask;
        for( int i = 0; i < spot_count; i++ )
            if( draw_mask & (1 << i) )
                entry->slots[i].applied_frame = frames[i];
        app->need_redraw = 1;
        if( first_combine && getenv("TORIRS_ANIM_DEBUG") )
            TORIRS_LOG(
                "entity_spotanim: combine element=%d slots=0x%x\n", element_id, draw_mask);
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
                player->spotanims,
                WORLD_PLAYER_SPOTANIM_SLOTS,
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
                npc->spotanims,
                WORLD_NPC_SPOTANIM_SLOTS,
                npc->element_id,
                WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, npc->server_slot));
    }
}

/* The body an npc draws: its customisation's models when it has one, the
 * type's own otherwise -- recoloured, scaled and lit as the type's (deob
 * NpcType.getModel with the override's models). NULL while a model is not
 * resident; the fetch is asked for once per change. */
static struct ToriDraw_Model*
app_world_build_npc_body(
    struct App* app,
    struct WorldEntity_NPC* npc,
    struct ToriRS_Npctype* npctype)
{
    struct ToriDraw_Model* model;

    if( npc->body_model_count <= 0 )
    {
        if( npctype->models_count <= 0 )
        {
            model = ToriDraw_ModelNew(0, 0, 0);
            assert(model);
            ToriDraw_ModelSetBoundsCylinder(model);
            return model;
        }
        if( !app_world_npc_models_resident(app, npctype) )
            return NULL; /* NpcBodyLand dresses it */
        return app_world_build_npc_model(app, npc->npc_id, npctype);
    }
    for( int i = 0; i < npc->body_model_count; i++ )
    {
        if( npc->body_models[i] < 0 || CacheProvider_ModelHas(app->provider, npc->body_models[i]) )
            continue;
        if( npc->body_load_serial != npc->body_serial )
        {
            for( int k = 0; k < npc->body_model_count; k++ )
            {
                struct Task_AppSpawn* task;

                if( npc->body_models[k] < 0 ||
                    CacheProvider_ModelHas(app->provider, npc->body_models[k]) )
                    continue;
                task = app_spawn_task_new(app, APP_SPAWN_ENTITY_MODEL, 0, 0, 0);
                task->model_id = npc->body_models[k];
                ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
            }
            npc->body_load_serial = npc->body_serial;
        }
        return NULL;
    }
    {
        struct AppModelRecolorSpec recolors = {
            .recolors_from = npctype->recolors_from,
            .recolors_to = npctype->recolors_to,
            .recolor_count = npctype->recolor_count,
            .retextures_from = npctype->retextures_from,
            .retextures_to = npctype->retextures_to,
            .retexture_count = npctype->retexture_count,
        };
        model = app_world_build_model(
            app,
            npc->body_models,
            npc->body_model_count,
            &recolors,
            npctype->width_scale,
            npctype->height_scale,
            APP_LIGHT_ACTOR,
            app->npc_light_uses_type_ambient_contrast ? npctype->contrast : 0,
            app->npc_light_uses_type_ambient_contrast ? npctype->ambient : 0);
    }
    return model;
}

void
app_world_sync_npc_bodies(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);
    if( !app->world )
        return;
    pool = &app->world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        struct ToriRS_Npctype* npctype;
        struct ToriDraw_Model* model;
        struct ToriDraw_ModelHandle hnd;

        if( !npc || npc->element_id < 0 || npc->body_serial == npc->body_mounted_serial )
            continue;
        if( !ToriDraw_SceneElementIsLive(app->scene, npc->element_id) )
            continue;
        npctype = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
        if( !npctype )
            continue;
        model = app_world_build_npc_body(app, npc, npctype);
        if( !model )
            continue;
        app_model_apply_import_render_flags(model, app_npc_wants_zbuffer(npc->npc_id, npctype));
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_SceneElementSetModel(app->scene, npc->element_id, hnd);
        app_element_pose_after_model_swap(app, npc->element_id);
        npc->body_mounted_serial = npc->body_serial;
        app_sync_textures(app);
        app->need_redraw = 1;
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

/* ---- Player TINT (rev-239 PLAYER_INFO tinting, `p_tint`) ----------------
 *
 * The official client recolours the body at RASTER time: Player.getModel
 * hands the model the tint and the count of body faces (class60 ->
 * Model.method4855), and every untextured face below that count is drawn
 * through class158.method5310 (world/entity_tint.h has the port and the
 * citations). This client's rasterisers -- software and every GPU bake --
 * read a placed model's lit colours (face_colors_a/b/c) at draw time, so the
 * same picture is made by writing the tinted colours into the element's model
 * for the frames that draw it and putting the lit ones back before anything
 * else in the next frame reads them.
 *
 * Applied LAST in the world frame (after the body reconcile and the attached-
 * graphic combine, which snapshot and re-merge the body) and reverted FIRST in
 * the next one (before either), so neither ever sees a tinted colour: a body
 * snapshot taken while tinted would otherwise come back tinted for good. The
 * edits go through ToriDraw_SceneElementModelForWrite, the one door for an
 * in-place edit, which also bumps the element's model revision.
 *
 * The saved colours live here, keyed by scene and element, rather than on
 * struct App: they exist only between one frame's apply and the next frame's
 * revert. A revert finds its model by identity (pointer and face count) and
 * drops the save untouched when the element no longer wears it -- a despawn,
 * a new body, a graphic detached by the event drain between frames.
 */
#define APP_PLAYER_TINT_SAVES 16

struct AppPlayerTintSave
{
    struct ToriDraw_Scene* scene;
    int element_id;
    struct ToriDraw_Model* model; /* NULL: free */
    int face_count;
    int faces; /* tinted faces: [0, faces) */
    hsl16_t* saved; /* faces * 3: a, b, c */
    int cap;
};

static struct AppPlayerTintSave g_player_tint_saves[APP_PLAYER_TINT_SAVES];

/* The model an element wears right now, when it is a full model; NULL for a
 * dead element or one carrying a shared / lent / ground model (none of which
 * a player body is). */
static struct ToriDraw_Model*
app_player_tint_element_model(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* el;

    if( !ToriDraw_SceneElementIsLive(scene, element_id) )
        return NULL;
    el = ToriDraw_SceneElementGet(scene, element_id);
    if( !el || el->model.kind != TORIDRAWMK_MODEL )
        return NULL;
    return el->model.u.model.model;
}

/* The tinted value of one lit corner colour. A gouraud corner that lands on
 * one of the channel-C sentinels would be read back as flat / hidden; nudge
 * its lightness one step instead (lighting never produces either). */
static hsl16_t
app_player_tint_corner(
    int hsl,
    struct WorldEntityTint const* tint,
    int is_c)
{
    int out = WorldEntityTint_Hsl16(hsl, tint->hue, tint->saturation, tint->lightness,
                                    tint->weight);

    if( is_c && (out == TORIDRAWHSL16_FLAT || out == TORIDRAWHSL16_HIDDEN) )
        out -= 1;
    return (hsl16_t)out;
}

void
app_world_revert_player_tints(struct App* app)
{
    assert(app);
    for( int i = 0; i < APP_PLAYER_TINT_SAVES; i++ )
    {
        struct AppPlayerTintSave* save = &g_player_tint_saves[i];
        struct ToriDraw_Model* model;

        if( !save->model || save->scene != app->scene )
            continue;
        model = app_player_tint_element_model(app->scene, save->element_id);
        if( model == save->model && model->face_count == save->face_count )
        {
            model = ToriDraw_SceneElementModelForWrite(app->scene, save->element_id);
            assert(model == save->model);
            for( int f = 0; f < save->faces; f++ )
            {
                model->face_colors_a[f] = save->saved[f * 3];
                model->face_colors_b[f] = save->saved[f * 3 + 1];
                model->face_colors_c[f] = save->saved[f * 3 + 2];
            }
            app->need_redraw = 1;
        }
        save->model = NULL;
    }
}

/* Tint one actor's body for this frame: its element's model, the body's own
 * faces only (an attached graphic merged on after it is not tinted). False
 * when every save is in use, so the caller stops. */
static int
app_tint_element(
    struct App* app,
    int element_id,
    uint32_t entity_id,
    struct WorldEntityTint const* tint)
{
    struct AppPlayerTintSave* save = NULL;
    struct AppEntitySpotanim* spot;
    struct ToriDraw_Model* model;
    int faces;

    if( !app_player_tint_element_model(app->scene, element_id) )
        return 1;
    for( int i = 0; i < APP_PLAYER_TINT_SAVES && !save; i++ )
        if( !g_player_tint_saves[i].model )
            save = &g_player_tint_saves[i];
    /* More tinted actors on screen than saves: the rest draw untinted this
     * frame rather than lose their colours. */
    if( !save )
        return 0;
    model = ToriDraw_SceneElementModelForWrite(app->scene, element_id);
    assert(model);
    /* The body's own faces only: while a graphic rides the actor the element
     * wears merge(body, spot) with the body first, and the reference counts
     * the body before the merge (class60 var13). */
    faces = model->face_count;
    spot = app_entity_spotanim_find(app, element_id, entity_id);
    if( spot && spot->combined == model && spot->body && spot->body->face_count < faces )
        faces = spot->body->face_count;
    if( faces <= 0 )
        return 1;
    if( save->cap < faces * 3 )
    {
        save->saved = (hsl16_t*)ToriMem_Realloc(
            ToriMem_Render(), save->saved, (size_t)faces * 3 * sizeof(*save->saved));
        assert(save->saved);
        save->cap = faces * 3;
    }
    save->scene = app->scene;
    save->element_id = element_id;
    save->model = model;
    save->face_count = model->face_count;
    save->faces = faces;
    for( int f = 0; f < faces; f++ )
    {
        hsl16_t const c = model->face_colors_c[f];

        save->saved[f * 3] = model->face_colors_a[f];
        save->saved[f * 3 + 1] = model->face_colors_b[f];
        save->saved[f * 3 + 2] = c;
        /* class144.method4849: textured faces never take the tint. */
        if( model->face_textures && model->face_textures[f] != -1 )
            continue;
        if( c == TORIDRAWHSL16_HIDDEN )
            continue;
        model->face_colors_a[f] = app_player_tint_corner(model->face_colors_a[f], tint, 0);
        if( c == TORIDRAWHSL16_FLAT )
            continue;
        model->face_colors_b[f] = app_player_tint_corner(model->face_colors_b[f], tint, 0);
        model->face_colors_c[f] = app_player_tint_corner(c, tint, 1);
    }
    app->need_redraw = 1;
    return 1;
}

void
app_world_sync_player_tints(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);
    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);

        if( !player || player->element_id < 0 ||
            !WorldEntityTint_Active(&player->tint, app->world->cycle) )
            continue;
        if( !app_tint_element(app, player->element_id,
                              WORLD_ENTITY_ID(WORLD_ENTITY_KIND_PLAYER, player->server_pid),
                              &player->tint) )
            break;
    }
}

/* The npc half: rev-239 NPC_INFO tinting (`npc_tint`), drawn the same way --
 * Npc.getModel hands its model the tint as Player.getModel does (deob class60
 * / Model.method4855), so it shares the saves and the revert above. */
void
app_world_sync_npc_tints(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);
    if( !app->world )
        return;
    pool = &app->world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);

        if( !npc || npc->element_id < 0 ||
            !WorldEntityTint_Active(&npc->tint, app->world->cycle) )
            continue;
        if( !app_tint_element(app, npc->element_id,
                              WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, npc->server_slot),
                              &npc->tint) )
            break;
    }
}

