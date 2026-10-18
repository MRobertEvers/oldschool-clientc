/*
 * The server's view of an entity applied to its scene element: animation
 * tracks, held items, and attached graphics.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* An entry's graphic slots: the rev-239 npc's, of which a player uses one. */
#define APP_ENTITY_SPOT_SLOTS ((int)(sizeof(((struct AppEntitySpotanim*)0)->slots) / \
                                     sizeof(((struct AppEntitySpotanim*)0)->slots[0])))
_Static_assert(sizeof(((struct AppEntitySpotanim*)0)->slots) /
                       sizeof(((struct AppEntitySpotanim*)0)->slots[0]) ==
                   WORLD_NPC_SPOTANIM_SLOTS,
               "one combine slot per world graphic slot");

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
    return anim && anim->frame_count > 0 && anim->frames && anim->base;
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
    if( anim->frames[frame].length > 0 )
        ToriDraw_ModelAnimateFrame(posed, anim->base, &anim->frames[frame]);
    else
        ToriDraw_ModelApplyPostTransforms(posed);
    /* Reference nulls the spot copy's labels before Model.combine. */
    ToriDraw_BonesFree(posed->vertex_bones);
    posed->vertex_bones = NULL;
    ToriDraw_BonesFree(posed->face_bones);
    posed->face_bones = NULL;
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
         * app_world_build_model leaves a fresh body. */
        ToriDraw_ModelApplyPostTransforms(merged);
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
                &player->spotanim,
                1,
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
