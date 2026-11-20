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
#define APP_ENTITY_SPOT_SLOTS ENTITY_SPOTANIM_SLOTS
_Static_assert(ENTITY_SPOTANIM_SLOTS == WORLD_NPC_SPOTANIM_SLOTS,
               "one combine slot per world graphic slot");
_Static_assert((int)WORLD_PLAYER_SPOTANIM_SLOTS == (int)WORLD_NPC_SPOTANIM_SLOTS,
               "a player's graphic slots combine as an npc's");

/* Private to this unit, declared up front so definition order is free. */
static void
app_entity_spotanim_detach(
    struct App* app,
    struct EntitySpotanim* entry,
    bool restore);
/* The whole asset chain an attached graphic needs before it can be combined:
 * the spotanimtype, its model, and a playable seq. One test for the per-frame
 * combine below and for the packet-time prefetch, so the two cannot disagree
 * about what "loaded" means. */
int
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

/* Want an attached graphic's assets (ASSET_KIND_SPOTANIM: its type, model
 * and sequence). One load per graphic in flight however many entities wear
 * it, and however many frames ask while it loads; a load that ends without
 * them is retried after a back-off while anything still wears it. */
static void
app_entity_spotanim_want(
    struct App* app,
    int spotanim_id)
{
    (void)Assets_Want(app->assets, AssetKey_Make(ASSET_KIND_SPOTANIM, spotanim_id, 0));
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
    app_entity_spotanim_want(app, spotanim_id);
}

static void
app_world_sync_one_entity_spotanim(
    struct App* app,
    struct WorldEntityFacet_EntitySpotanim const* spots,
    int spot_count,
    int element_id,
    int owner_entity_id);

/* Want a sequence (ASSET_KIND_SEQUENCE): one load however often it is asked.
 *
 * Asked where something changes to it, never per frame: the world asks when
 * an entity's track is set (World_SeqWant -> app_world_seq_want), a hull's
 * config names it, the editor poses a preview. The assets table holds one
 * entry per sequence in flight with no capacity at which the dedupe stops,
 * and remembers a sequence the cache cannot serve (ASSET_FAIL_REMEMBER), so a
 * miss is answered once. */
void
app_request_entity_seq(
    struct App* app,
    int seq_id)
{
    if( seq_id < 0 || ToriDraw_SceneAnimationHas(app->scene, seq_id) )
        return;
    (void)Assets_Want(app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, seq_id, 0));
}

/* World_SeqSource.want: a track of some entity was just set to `seq_id`. */
void
app_world_seq_want(
    void* want_userdata,
    int seq_id)
{
    app_request_entity_seq((struct App*)want_userdata, seq_id);
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

    /* Nothing is asked for here: the world asked when each track was set to
     * its sequence (World_SeqWant), a parked ANIM's too, so a steady frame
     * asks for nothing. A sequence not registered yet is bound the frame its
     * load lands. */

    if( primary_active && anim->primary.delay == 0 )
    {
        struct ToriDraw_Animation* pa =
            ToriDraw_SceneAnimationGet(app->scene, anim->primary.anim_id);
        /* Not registered yet: the want only queued the load, so the first
         * ticks after a spawn legitimately have no animation. That is the
         * caller's condition — the predicate asserts. */
        if( pa && ToriDraw_ElementAnimPlayable(pa) )
        {
            /* TORIRS_MERGE_TRACE=<seq>: every frame that seq is bound as the
             * action track, whether the movement track was bound beside it
             * for the walkmerge blend, and if not, which test refused it. */
            {
                static int trace_seq = -2;
                if( trace_seq == -2 )
                    trace_seq = getenv("TORIRS_MERGE_TRACE") ? atoi(getenv("TORIRS_MERGE_TRACE")) : -1;
                if( trace_seq >= 0 && anim->primary.anim_id == (uint16_t)trace_seq )
                {
                    struct ToriDraw_Animation* sa_t =
                        secondary_active ? ToriDraw_SceneAnimationGet(app->scene, anim->secondary.anim_id) : NULL;
                    TORIRS_LOG(
                        "merge_trace: element=%d cycle=%d primary=%d frame=%d walkmerge=%d secondary=%d "
                        "frame2=%d ready=%d sa_loaded=%d sa_frames=%d\n",
                        element_id, app->world ? app->world->cycle : -1, (int)anim->primary.anim_id,
                        (int)anim->primary.frame, pa->walkmerge != NULL,
                        secondary_active ? (int)anim->secondary.anim_id : -1, (int)anim->secondary.frame,
                        idle ? idle->readyanim : -1, sa_t != NULL, sa_t && sa_t->frames != NULL);
                }
            }
            ToriDraw_ElementSetAnim(el, pa);
            el->anim_seq_id = anim->primary.anim_id;
            el->anim_frame = anim->primary.frame < pa->frame_count ? anim->primary.frame : 0;
            /* The walkmerge blend takes the walk alongside: a classic primary
             * masks transform groups with a classic walk's frames, an Animaya
             * primary with an opcode-17 mask hands bones to an Animaya walk
             * on its rig (rev-241 class394.method9194). An Animaya primary
             * with no mask poses every bone itself, so it takes none. */
            if( secondary_active && idle && anim->secondary.anim_id != (uint16_t)idle->readyanim &&
                (!pa->skeletal || pa->maya_masks) )
            {
                struct ToriDraw_Animation* sa =
                    ToriDraw_SceneAnimationGet(app->scene, anim->secondary.anim_id);
                int merges = 0;
                if( sa && sa->frame_count > 0 )
                    merges = pa->skeletal
                                 ? sa->skeletal && ToriDraw_SkeletalAnimSharesRig(pa->skeletal, sa->skeletal)
                                 : sa->frames != NULL;
                if( merges )
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
 * lives in app->entity_spotanims (engine/entity_spotanim.h) keyed by the body
 * element id and its owner. ---- */

/* Find the entry for (element, owner). `owner_entity_id` 0 means "any owner",
 * used only when scanning for a free slot.
 *
 * An entry matching the element but NOT the owner is a recycled element id: the
 * previous owner despawned and this id was handed to somebody else. Drop it
 * WITHOUT restoring -- restoring would move the dead entity's body model onto
 * the new occupant. See the note on EntitySpotanim::owner_entity_id. */
struct EntitySpotanim*
app_entity_spotanim_find(
    struct App* app,
    int body_element_id,
    int owner_entity_id)
{
    struct EntitySpotanim* recycled;
    struct EntitySpotanim* entry = EntitySpotanims_Find(
        &app->entity_spotanims, body_element_id, owner_entity_id, &recycled);

    if( recycled )
        app_entity_spotanim_detach(app, recycled, false);
    return entry;
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
    struct EntitySpotanim* entry,
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
        RedrawRequest_Raise(&app->redraw_request);
    }
    if( entry->body )
        ToriDraw_ModelFree(entry->body);
    for( int i = 0; i < APP_ENTITY_SPOT_SLOTS; i++ )
        if( entry->slots[i].spot )
            ToriDraw_ModelFree(entry->slots[i].spot);
    EntitySpotanim_Clear(entry);
}

/* EntityRemoved drain hook: the entity element (and with it the combined
 * model) is going away — free the snapshots without touching the element. */
void
app_entity_spotanim_drop(
    struct App* app,
    int body_element_id)
{
    struct EntitySpotanim* entry = app_entity_spotanim_find(app, body_element_id, 0);
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
    struct EntitySpotanimSlot const* slot,
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
    struct EntitySpotanim* entry)
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
        RedrawRequest_Raise(&app->redraw_request);
    }
    EntitySpotanim_ForgetMerge(entry);
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
    struct EntitySpotanim* entry = app_entity_spotanim_find(app, element_id, owner_entity_id);
    struct ToriDraw_SceneElement* el;
    struct ToriDraw_Animation* anims[APP_ENTITY_SPOT_SLOTS] = { 0 };
    int frames[APP_ENTITY_SPOT_SLOTS];
    int active_mask = 0;
    int draw_mask = 0;
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
        entry = EntitySpotanims_Claim(&app->entity_spotanims, element_id, owner_entity_id);
    }

    /* A slot whose graphic changed (replaced, or ended) drops its model; the
     * body snapshot stays, and the merge below is rebuilt without it. */
    for( int i = 0; i < APP_ENTITY_SPOT_SLOTS; i++ )
    {
        int want = (active_mask & (1 << i)) ? spots[i].id : -1;
        struct ToriDraw_Model* old = EntitySpotanim_SlotWant(&entry->slots[i], want);

        if( old )
            ToriDraw_ModelFree(old);
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
        struct EntitySpotanimSlot* slot = &entry->slots[i];
        struct ToriRS_Spotanimtype* type;

        if( !(active_mask & (1 << i)) )
            continue;
        if( !app_entity_spotanim_resident(app, slot->spotanim_id) )
        {
            /* Normally already in flight: the packet that set the graphic
             * prefetched it (App_EntitySpotanimPrefetch), so the start delay
             * covers the load. This is the fallback for anything that did
             * not; the table answers "already loading" for every frame after
             * the first. */
            app_entity_spotanim_want(app, slot->spotanim_id);
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
        EntitySpotanim_ForgetMerge(entry);
        if( !entry->body )
            return;
    }

    if( !EntitySpotanim_NeedsMerge(entry, draw_mask, frames, spot_count) )
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
        EntitySpotanim_NoteMerged(entry, merged, draw_mask, frames, spot_count);
        RedrawRequest_Raise(&app->redraw_request);
        if( first_combine && getenv("TORIRS_ANIM_DEBUG") )
            TORIRS_LOG(
                "entity_spotanim: combine element=%d slots=0x%x\n", element_id, draw_mask);
    }
}

void
app_world_sync_entity_spotanims(struct App* app)
{
    struct World_EntityPool* pool;

    if( !app->world )
        return;

    /* Entries whose body element died outside the event drain (scene
     * teardown): free the snapshots. */
    for( int i = 0; i < app->entity_spotanims.capacity; i++ )
    {
        struct EntitySpotanim* entry = &app->entity_spotanims.entries[i];
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
            struct EntitySpotanim* entry = app_entity_spotanim_find(app, npc->element_id, 0);
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

/* The body an npc draws: its server customisation over its type when it has
 * one (app_world_build_npc_override_model: the override's models or the
 * type's, the override's recolours and retextures over the type's), the
 * type's own otherwise. NULL while a model is not resident; a missing one is
 * wanted through the assets table every frame the body waits (one load each
 * in flight, a failed one retried after a back-off). */
static struct ToriDraw_Model*
app_world_build_npc_body(
    struct App* app,
    struct WorldEntity_NPC* npc,
    struct ToriRS_Npctype* npctype)
{
    struct WorldNpcOverride const* o = &npc->body_custom;
    struct ToriDraw_Model* model;
    int const* ids;
    int count;

    if( !o->active )
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
    ids = o->models_sent ? o->models : npctype->models;
    count = o->models_sent ? o->model_count : npctype->models_count;
    if( count <= 0 )
    {
        /* An override that sent no models over a type with none, or an
         * empty list (the reference's `new int[0]`): an empty body. */
        model = ToriDraw_ModelNew(0, 0, 0);
        assert(model);
        ToriDraw_ModelSetBoundsCylinder(model);
        return model;
    }
    {
        int missing = 0;
        for( int i = 0; i < count; i++ )
        {
            if( ids[i] < 0 || CacheProvider_ModelHas(app->provider, ids[i]) )
                continue;
            (void)Assets_Want(app->assets, AssetKey_Make(ASSET_KIND_MODEL, ids[i], 0));
            missing = 1;
        }
        if( missing )
            return NULL;
    }
    return app_world_build_npc_override_model(app, npc->npc_id, npctype, o);
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
        app_model_apply_rszbuf_two_pass(model, npctype->zbuf_off);
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_SceneElementSetModel(app->scene, npc->element_id, hnd);
        app_element_pose_after_model_swap(app, npc->element_id);
        npc->body_mounted_serial = npc->body_serial;
        app_sync_textures(app);
        RedrawRequest_Raise(&app->redraw_request);
    }
}

int
App_WorldSpawnSyncedPlayer(
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    return app_world_spawn_synced_player_now(app, scene_x, scene_z, level);
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
            RedrawRequest_Raise(&app->redraw_request);
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
    struct EntitySpotanim* spot;
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
    RedrawRequest_Raise(&app->redraw_request);
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

/* ---- Npc TRANSPARENCY (rev-241 NPC_INFO transparency, `npc_transparency`) --
 *
 * The official client fades the BODY at raster time too: Npc.getModel hands
 * the body model one alpha for the cycle (class86.method2930 ->
 * Model.method4830), each face of alpha a < 253 draws at a + m*(253-a)>>8, and
 * a model of alpha 255 is not drawn (Model.method4901 / method4912;
 * world/entity_transparency.h). An attached graphic is its own model there and
 * is never faded: Ocellus's teleport swirl plays while he goes.
 *
 * The same frame discipline as the tint above, and nested inside it: applied
 * after the tint, reverted before it, so each save holds what the other left.
 * A faded body writes its face alphas; a gone one hides its faces through
 * channel C, which every rasteriser already skips. A body with no alpha array
 * is given one, zeroed (opaque), and keeps it: the model owns it and frees it,
 * and an element freed between frames never holds a buffer of ours.
 */
#define APP_NPC_TRANSPARENCY_SAVES 16

struct AppNpcTransparencySave
{
    struct ToriDraw_Scene* scene;
    int element_id;
    struct ToriDraw_Model* model; /* NULL: free */
    int face_count;
    int faces; /* the body's faces: [0, faces) */
    int hidden; /* saved holds channel C, else the face alphas */
    hsl16_t* saved_c;
    alphaint_t* saved_alphas;
    int cap;
};

static struct AppNpcTransparencySave g_npc_transparency_saves[APP_NPC_TRANSPARENCY_SAVES];

void
app_world_revert_npc_transparency(struct App* app)
{
    assert(app);
    for( int i = 0; i < APP_NPC_TRANSPARENCY_SAVES; i++ )
    {
        struct AppNpcTransparencySave* save = &g_npc_transparency_saves[i];
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
                if( save->hidden )
                    model->face_colors_c[f] = save->saved_c[f];
                else
                    model->face_alphas[f] = save->saved_alphas[f];
            }
            RedrawRequest_Raise(&app->redraw_request);
        }
        save->model = NULL;
    }
}

/* Fade one npc's body for this frame by `alpha` (1..255). False when every
 * save is in use, so the caller stops. */
static int
app_fade_element(
    struct App* app,
    int element_id,
    uint32_t entity_id,
    int alpha)
{
    struct AppNpcTransparencySave* save = NULL;
    struct EntitySpotanim* spot;
    struct ToriDraw_Model* model;
    int faces;

    if( !app_player_tint_element_model(app->scene, element_id) )
        return 1;
    for( int i = 0; i < APP_NPC_TRANSPARENCY_SAVES && !save; i++ )
        if( !g_npc_transparency_saves[i].model )
            save = &g_npc_transparency_saves[i];
    /* More fading npcs on screen than saves: the rest draw opaque this frame
     * rather than lose their alphas. */
    if( !save )
        return 0;
    model = ToriDraw_SceneElementModelForWrite(app->scene, element_id);
    assert(model);
    /* The body's own faces, as the tint counts them. */
    faces = model->face_count;
    spot = app_entity_spotanim_find(app, element_id, entity_id);
    if( spot && spot->combined == model && spot->body && spot->body->face_count < faces )
        faces = spot->body->face_count;
    if( faces <= 0 )
        return 1;
    if( save->cap < faces )
    {
        save->saved_c = (hsl16_t*)ToriMem_Realloc(
            ToriMem_Render(), save->saved_c, (size_t)faces * sizeof(*save->saved_c));
        assert(save->saved_c);
        save->saved_alphas = (alphaint_t*)ToriMem_Realloc(
            ToriMem_Render(), save->saved_alphas, (size_t)faces * sizeof(*save->saved_alphas));
        assert(save->saved_alphas);
        save->cap = faces;
    }
    save->scene = app->scene;
    save->element_id = element_id;
    save->model = model;
    save->face_count = model->face_count;
    save->faces = faces;
    save->hidden = alpha >= 255;
    if( save->hidden )
    {
        for( int f = 0; f < faces; f++ )
        {
            save->saved_c[f] = model->face_colors_c[f];
            model->face_colors_c[f] = TORIDRAWHSL16_HIDDEN;
        }
    }
    else
    {
        if( !model->face_alphas )
        {
            model->face_alphas =
                (alphaint_t*)TORIDRAW_CALLOC((size_t)model->face_count, sizeof(alphaint_t));
            assert(model->face_alphas);
        }
        for( int f = 0; f < faces; f++ )
        {
            save->saved_alphas[f] = model->face_alphas[f];
            model->face_alphas[f] =
                (alphaint_t)WorldEntityTransparency_Face(model->face_alphas[f], alpha);
        }
    }
    RedrawRequest_Raise(&app->redraw_request);
    return 1;
}

void
app_world_sync_npc_transparency(struct App* app)
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
        int alpha;

        if( !npc || npc->element_id < 0 || !npc->transparency.set )
            continue;
        alpha = WorldEntityTransparency_Alpha(&npc->transparency, app->world->cycle);
        if( alpha <= 0 )
            continue;
        if( !app_fade_element(app, npc->element_id,
                              WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, npc->server_slot), alpha) )
            break;
    }
}

/* The player half: rev-241 PLAYER_INFO transparency (`p_transparency`). The
 * deob keeps a player's fade in the same Actor field an npc's lives in
 * (class83.field844, class17) and Player.getModel hands it to the body as
 * Npc.getModel does, so it shares the saves and the revert above. */
void
app_world_sync_player_transparency(struct App* app)
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
        int alpha;

        if( !player || player->element_id < 0 || !player->transparency.set )
            continue;
        alpha = WorldEntityTransparency_Alpha(&player->transparency, app->world->cycle);
        if( alpha <= 0 )
            continue;
        if( !app_fade_element(app, player->element_id,
                              WORLD_ENTITY_ID(WORLD_ENTITY_KIND_PLAYER, player->server_pid), alpha) )
            break;
    }
}

