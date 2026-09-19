/*
 * The spawn machinery: model build, lighting, and the spawn task.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Has this npc id already been reported as unavailable?  16384 bits is 2KB and
 * covers the whole npc id space of every revision here (osrs239 tops out near
 * 13000); an id past the end warns every time rather than being dropped. */
enum
{
    APP_NPC_WARN_BITS = 16384
};

/* Private to this unit, declared up front so definition order is free. */
static bool
app_model_zbuffer_kernels_enabled(void);
static int
app_warn_once_npc(int npc_id);
static void
app_world_spawn_projectile_now(
    struct App* app,
    struct World* world,
    int model_id,
    int seq_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int tile_x,
    int tile_z,
    int target);
static int
app_world_ground_composed(
    struct App* app,
    int fine_x,
    int fine_z,
    int level);
static void
app_world_spawn_projectile_spot_now(
    struct App* app,
    struct World* world,
    int spotanim_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int dst_tile_x,
    int dst_tile_z,
    int dst_level,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target,
    int late);
static void
app_loc_change_apply_ops(
    struct App* app,
    struct World* world,
    const struct Task_AppSpawn* self);
static void
app_spawn_fan_spotanim_assets(struct Task_AppSpawn* self);
static int
Task_AppSpawn_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_AppSpawn_Free(struct ToriRS_Task* base);
static void
app_spawn_loc_lane_release(struct Task_AppSpawn* self);

static struct ToriRS_TaskVTable Task_AppSpawn_VTable = {
    .run = Task_AppSpawn_Run,
    .free = Task_AppSpawn_Free,
};

struct Task_AppSpawn*
app_spawn_task_new(
    struct App* app,
    enum AppSpawnKind kind,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppSpawn_VTable;
    strncpy(task->task.name, "AppSpawn", sizeof(task->task.name) - 1);
    task->app = app;
    /* Single capture point for every spawn kind: the enqueue happens while the
     * addressing SET_ACTIVE_WORLD is still in force. */
    task->view = app->active_world;
    task->kind = kind;
    task->tile_x = tile_x;
    task->tile_z = tile_z;
    task->level = level;
    PT_INIT(&task->pt);
    return task;
}
/*
 * SPAWNING things into the world -- building the model an entity will be drawn
 * with, putting the entity in the scene, and the task that waits for whatever
 * the cache has not handed over yet.
 *
 * A unity fragment of app.c, not a module. It is textually part of app.c's
 * translation unit and included at exactly the point it was cut from, so every
 * helper here stays static and every App field it reads stays where it was.
 * The split is for the reader: a spawn is one story, from "the server said
 * there is an npc here" to "there is a lit, posed, scaled model in the scene",
 * and it reads better as one.
 *
 * Two ordering rules live in here and are commented where they bite. A model
 * is recoloured BEFORE it is lit, because lighting bakes face colours into
 * per-vertex shaded ones and a swap afterwards is a no-op. And a resize is
 * RECORDED rather than applied, because the reference resizes after animating
 * and applying it early puts every keyframe's authored-size translations
 * against a shrunken model -- which is what threw Xarpus into the air above his
 * own arena.
 *
 * What has left: the multinpc rung/shell gap-fill is
 * ToriRS_NpctypeEntityFacts', and the placement op menu is
 * WorldEntity_SceneryApplyPlacementOps'. Both are rules about cache records
 * and neither needed an App.
 *
 * @see src/app.c
 */

/**
 * Which npcs were imported from a z-buffered client.
 *
 * What that then MEANS for the render is
 * app_model_apply_import_render_flags' answer, not this one's: the face
 * priorities always go, and the depth-tested kernels are separately switchable
 * (see app_model_zbuffer_kernels_enabled). This function only identifies the
 * models; it does not decide how they are resolved.
 *
 * The content says so, per npc, with the `zbuffer_model` param -- see
 * OSRS-Content/.../minigame_rs2012_qbd/configs/rs2012_qbd.param. The client
 * reads it off the npc type it already has (ToriRS_Npctype::zbuffer_model), so
 * nothing here has to know which npcs those are: adding one is a content edit
 * and a repack, not a client change.
 *
 * TORIRS_ZBUFFER_NPCS overrides the content entirely -- a comma list of npc ids
 * to treat as imported instead, or the empty string for nobody. That is the A/B
 * knob: it takes the decision away from the config without editing it.
 *
 * Per npc rather than globally because that is the unit of the question. The
 * goblin standing next to the dragon was authored for a painter's sort and
 * paying a depth test for it buys nothing.
 *
 * A model carrying TORIDRAW_MODEL_FLAG_ZBUFFER also has its face priorities
 * dropped by the sort -- see the flag's own comment. The two cannot both decide
 * a pixel, and a priority would win.
 */
bool
app_npc_wants_zbuffer(
    int npc_id,
    struct ToriRS_Npctype const* npctype)
{
    char const* list = getenv("TORIRS_ZBUFFER_NPCS");
    if( !list )
        return npctype && npctype->zbuffer_model != 0;
    return ToriRS_EnvIdListHas(list, npc_id);
}

/**
 * Are the depth-tested kernels switched on for the models that ask for them?
 *
 * On by default: an imported model's parts genuinely interpenetrate and the
 * depth test is the only thing that resolves them correctly.
 *
 * TORIRS_MODEL_ZBUFFER=0 leaves those models the OTHER half of the opt-in --
 * their face priorities still dropped (TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY),
 * which is right for them either way, but resolved by the painter's sort. That
 * is the A/B knob for the reported QBD symptom, where faces on the Queen and
 * the other rs2012 npcs blink out at some camera angles: a per-pixel reject
 * looks nothing like a bad sort, so splitting the two halves says which one is
 * doing it without editing content or reverting code.
 */
static bool
app_model_zbuffer_kernels_enabled(void)
{
    char const* off = getenv("TORIRS_MODEL_ZBUFFER");
    return !(off && *off && *off == '0');
}

/**
 * Stamp the render policy for a model imported from a z-buffered client.
 *
 * `imported` is the content's `zbuffer_model` answer (app_npc_wants_zbuffer).
 * Written both ways because the model may be a cache copy of one that was
 * stamped under a different npc id.
 */
void
app_model_apply_import_render_flags(
    struct ToriDraw_Model* model,
    bool imported)
{
    uint8_t const both =
        (uint8_t)(TORIDRAW_MODEL_FLAG_ZBUFFER | TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY);
    assert(model);
    model->flags &= (uint8_t)~both;
    if( !imported )
        return;
    model->flags |= TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY;
    if( app_model_zbuffer_kernels_enabled() )
        model->flags |= TORIDRAW_MODEL_FLAG_ZBUFFER;
}

struct ToriDraw_Model*
app_world_build_model(
    struct App* app,
    const int* model_ids,
    int count,
    const struct AppModelRecolorSpec* recolors,
    int scale_xz,
    int scale_y,
    int light_actor,
    int light_contrast,
    int light_ambient)
{
    struct ToriDraw_Model* parts[16];
    struct ToriDraw_Model* model = NULL;
    int part_count = 0;

    for( int i = 0; i < count && part_count < 16; i++ )
    {
        struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, model_ids[i]);
        struct ToriDraw_Model* part = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
        if( part )
            parts[part_count++] = part;
    }
    if( part_count == 0 )
        return NULL;

    if( part_count > 1 )
    {
        model = ToriDraw_ModelMerge(parts, part_count);
        for( int i = 0; i < part_count; i++ )
            ToriDraw_ModelFree(parts[i]);
    }
    else
        model = parts[0];
    if( !model )
        return NULL;

    /* Recolor before lighting: lighting bakes face colors into per-vertex
     * shaded colors, so a swap afterwards would be a no-op (same order as
     * scenery apply_transforms and the obj icon path in the scene bridge). */
    if( recolors )
    {
        for( int i = 0; i < recolors->recolor_count; i++ )
            ToriDraw_ModelRecolor(model, recolors->recolors_from[i], recolors->recolors_to[i]);
        for( int i = 0; i < recolors->retexture_count; i++ )
            ToriDraw_ModelRetexture(
                model, recolors->retextures_from[i], recolors->retextures_to[i]);
        /* Swapped-in ids are new to the loader's registry — see
         * ToriDraw_ModelNoteTextureWants. */
        if( recolors->retexture_count > 0 )
            ToriDraw_ModelNoteTextureWants(model);
    }

    /*
     * Recorded, not applied: the reference resizes the model AFTER animating it
     * (NpcType.getModel), and the animation is applied to the bind pose this
     * function is about to capture. Applying it here instead put every
     * keyframe's translations and ORIGIN pivots -- authored at full size --
     * against a shrunken model, which is what threw Xarpus (resizeh/resizev 64)
     * into the air above his arena. ToriDraw_ModelApplyPostTransforms below puts
     * this instance into render scale for the un-animated case; every pose
     * re-applies it.
     *
     * Lighting therefore also runs at the authored size, which is where the
     * reference lights its cached base too.
     */
    ToriDraw_ModelSetPostResize(model, scale_xz, scale_xz, scale_y);

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate.
     * Without this, every face whose material is HD-only keeps a texture id,
     * lighting stores 0–127 lightness (not HSL16), and the software raster
     * skips the face when the texture map has no entry. Steel titan (30469)
     * is entirely HD-textured (materials 238/288/241, all valid=0). */
    ToriDraw_ModelDropNonSdTextures(app->provider, model);
    ToriDraw_ModelNoteTextureWants(model);

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        if( light_actor )
            ToriDraw_LightModelActor(hnd, light_contrast, light_ambient);
        else
            ToriDraw_LightModelScene(hnd, light_contrast, light_ambient);
    }
    /* Capture first: the bind pose is the authored-size model. */
    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_ModelApplyPostTransforms(model);
    ToriDraw_ModelSetBoundsCylinder(model);
    return model;
}

/* Is every model the type names resident? A body that is not is expected
 * while its load is on the wire (game/task_entity_assets.c), and then the
 * spawn and the retype mount nothing rather than report a failure. */
int
app_world_npc_models_resident(
    struct App* app,
    struct ToriRS_Npctype const* npctype)
{
    assert(app);
    assert(npctype);
    for( int i = 0; i < npctype->models_count; i++ )
        if( npctype->models[i] >= 0 && !CacheProvider_ModelHas(app->provider, npctype->models[i]) )
            return 0;
    return 1;
}

/*
 * The lit/transformed base for an npc type, cached like the spotanim path
 * (Client-TS NpcType model cache, 30 entries).
 *
 * Every input app_world_build_model consumes here -- part models, recolours,
 * retextures, the two scales, ambient/contrast -- is read off the npctype, and
 * app->npc_light_uses_type_ambient_contrast is resolved once during feature
 * setup and never rewritten, so the npc id alone identifies the result. Without
 * this an npc walking into view paid a full merge + recolour + scale +
 * DropNonSdTextures + per-vertex light + bounds + vertex capture EVERY time,
 * even though the source models were already resident: one server tick's worth
 * of arrivals was ~13ms of a 20ms frame.
 *
 * Returns an owned mutable instance -- the scene animates it in place, so the
 * cache keeps its own copy and never hands out the base. Render flags are
 * deliberately left off the cached base; the callers set TORIDRAW_MODEL_FLAG_
 * ZBUFFER per npc id after this returns.
 */
struct ToriDraw_Model*
app_world_build_npc_model(
    struct App* app,
    int npc_id,
    struct ToriRS_Npctype* npctype)
{
    struct ToriDraw_Model* model;

    assert(app && npctype);

    model = TorirsModelInstCache_CopyGet(
        &app->model_inst_cache, TORIRS_MODEL_INST_NPC, (int64_t)npc_id);
    if( model )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_NPC_MODEL_CACHE_HIT, 1);
        return model;
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_NPC_MODEL_CACHE_MISS, 1);

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
            npctype->models,
            npctype->models_count,
            &recolors,
            npctype->width_scale,
            npctype->height_scale,
            APP_LIGHT_ACTOR,
            app->npc_light_uses_type_ambient_contrast ? npctype->contrast : 0,
            app->npc_light_uses_type_ambient_contrast ? npctype->ambient : 0);
    }
    if( !model )
        return NULL;

    {
        struct ToriDraw_Model* base_copy = ToriDraw_ModelCopy(model);
        if( base_copy )
            TorirsModelInstCache_Put(
                &app->model_inst_cache, TORIRS_MODEL_INST_NPC, (int64_t)npc_id, base_copy);
    }
    return model;
}

/* Build the drawable model for a spotanim (reference SpotType.getTempModel2 +
 * MapSpotAnim.getTempModel static transforms): a single model, recoloured/
 * retextured, resized, angle-rotated and lit with the config ambient/contrast.
 * The seq animation itself is bound onto the element and stepped per-tick by
 * app_world_tick_animations (the projectile path), so only the static
 * transforms are baked here. SYNCHRONOUS — the model must already be resident.
 * Returns an owned model or NULL. */
struct ToriDraw_Model*
app_world_build_spotanim_model(
    struct App* app,
    const struct ToriRS_Spotanimtype* spot)
{
    struct ToriDraw_Model* cached;
    struct ToriDraw_Model* model;
    int retextured = 0;
    int64_t key;

    assert(spot);
    /* Key by spotanim id — transforms are baked into the cached base, matching
     * Client-TS SpotType.modelCache keyed on spot id. */
    key = (int64_t)spot->id;
    cached = TorirsModelInstCache_CopyGet(&app->model_inst_cache, TORIRS_MODEL_INST_SPOT, key);
    if( cached )
        return cached;

    {
        struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, spot->model);
        model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
    }
    if( !model )
        return NULL;

    /* Recolour: reference guards the whole 6-pair loop on recol_s[0] != 0. */
    if( spot->recol_s[0] != 0 )
    {
        for( int i = 0; i < 6; i++ )
            ToriDraw_ModelRecolor(model, spot->recol_s[i], spot->recol_d[i]);
    }
    /* Retexture (dat2 only; dat1 leaves these zero). */
    for( int i = 0; i < 6; i++ )
    {
        if( spot->retex_s[i] != 0 )
        {
            ToriDraw_ModelRetexture(model, spot->retex_s[i], spot->retex_d[i]);
            retextured = 1;
        }
    }
    if( retextured )
        ToriDraw_ModelNoteTextureWants(model);

    /* Recorded rather than applied, for the reason app_world_build_model gives:
     * MapSpotAnim.getModel animates the copy and only then resizes it. */
    ToriDraw_ModelSetPostResize(model, spot->resizeh, spot->resizeh, spot->resizev);

    if( spot->angle != 0 )
        ToriDraw_ModelOrient(model, spot->angle / 90);

    ToriDraw_ModelDropNonSdTextures(app->provider, model);
    ToriDraw_ModelNoteTextureWants(model);

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_LightModelActor(hnd, spot->contrast, spot->ambient);
    }
    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_ModelApplyPostTransforms(model);
    ToriDraw_ModelSetBoundsCylinder(model);

    /* Cache the lit base; return a copy so the scene owns a mutable instance. */
    {
        struct ToriDraw_Model* base_copy = ToriDraw_ModelCopy(model);
        if( base_copy )
            TorirsModelInstCache_Put(
                &app->model_inst_cache, TORIRS_MODEL_INST_SPOT, key, base_copy);
    }
    return model;
}

/* Hotkey 9 body: default player model on the hovered tile. SYNCHRONOUS —
 * the appearance kit + ready seq must be resident (Task_AppSpawn awaits).
 * Returns the world player-pool index, or -1. */
int
app_world_spawn_player_now(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    int scene_model_id;
    struct ToriDraw_ModelHandle reg;
    struct ToriDraw_Model* copy;
    int world_x = tile_x * 128 + 64;
    int world_z = tile_z * 128 + 64;
    int world_y;
    int element_id;

    int idx;

    scene_model_id = UITreeSceneBridge_EnsurePlayerModel(&app->bridge);
    if( scene_model_id <= 0 )
    {
        TORIRS_LOG("spawn_player: player model unavailable\n");
        return -1;
    }
    reg = ToriDraw_SceneModelGet(app->scene, scene_model_id);
    if( !ToriDraw_ModelKindIsFull(reg.kind) || !reg.u.model.model )
        return -1;
    copy = ToriDraw_ModelCopy(reg.u.model.model);
    if( !copy )
        return -1;
    ToriDraw_ModelSetBoundsCylinder(copy);
    ToriDraw_ModelCaptureOriginalVertices(copy);

    world_y = app_world_height(app, world_x, world_z, level);
    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_PLAYER, copy, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;
    app_world_apply_seq(app, element_id, APP_PLAYER_SEQ_READY);
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
            el->anim_external = true;
        ToriDraw_SceneAnimListInvalidate(app->scene);
    }

    {
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = APP_PLAYER_SEQ_READY,
            .walkanim = APP_PLAYER_SEQ_WALK,
            .turnanim = APP_PLAYER_SEQ_TURN,
            .runanim = APP_PLAYER_SEQ_RUN,
            .walkanim_b = APP_PLAYER_SEQ_WALK_B,
            .walkanim_r = APP_PLAYER_SEQ_WALK_R,
            .walkanim_l = APP_PLAYER_SEQ_WALK_L,
        };
        idx = World_PlayerSpawn(app->world, element_id, level, tile_x, tile_z, idle);
    }
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&app->world->entities.player, idx);
        if( player )
            player->server_pid = -1;
    }
    TORIRS_LOG("spawn_player: element=%d tile=%d,%d level=%d\n", element_id, tile_x, tile_z, level);
    app_sync_textures(app);
    app->need_redraw = 1;
    return idx;
}

static int
app_warn_once_npc(int npc_id)
{
    static uint32_t seen[APP_NPC_WARN_BITS / 32];

    if( npc_id < 0 || npc_id >= APP_NPC_WARN_BITS )
        return 0;
    if( seen[npc_id / 32] & (1u << (npc_id % 32)) )
        return 1;
    seen[npc_id / 32] |= (1u << (npc_id % 32));
    return 0;
}

/* The rung/shell gap-fill is ToriRS_NpctypeEntityFacts'. This is the spelling
 * that resolves the shell: the wire sends the multinpc's own id, and the cache
 * is where the record for it lives. */
void
app_npc_entity_facts(
    struct App* app,
    int base_npc_id,
    struct ToriRS_Npctype const* drawn,
    struct ToriRS_NpcEntityFacts* out)
{
    assert(app);
    ToriRS_NpctypeEntityFacts(
        drawn,
        base_npc_id >= 0 ? CacheProvider_NpctypeGet(app->provider, base_npc_id) : NULL,
        out);
}

/* Hotkey 8 body: npc on the hovered tile. SYNCHRONOUS — the npc config and
 * its models must be resident (Task_AppSpawn awaits them first).
 *
 * `npc_id` is the type whose MODEL is drawn (a multinpc rung, once resolved);
 * `base_npc_id` is the id the wire sent, and is what `app_npc_entity_facts`
 * fills the entity's own facts from. Pass -1 when there is no separate
 * shell.
 *
 * Returns the world npc-pool index, or -1. */
int
app_world_spawn_npc_now(
    struct App* app,
    int npc_id,
    int base_npc_id,
    int tile_x,
    int tile_z,
    int level)
{
    struct ToriRS_NpcEntityFacts facts;
    struct ToriRS_Npctype* npctype;
    struct ToriDraw_Model* model;
    int size;
    int world_x, world_z, world_y;
    int element_id;
    int idx;
    /* TORIRS_SPAWN_BREAKDOWN=<us>: split one npc spawn when it exceeds <us>.
     * TORIRS_SPAWN_LOG=1: restore the old per-spawn narration line. */
    static int bd_us = -1;
    static int spawn_log = -1;
    uint64_t bd_t0, bd_t;
    uint64_t bd_model = 0, bd_elem = 0, bd_world = 0, bd_seq = 0, bd_tex = 0, bd_log = 0;
    extern uint64_t PlatformWindow_TicksUs(void);

    if( bd_us < 0 )
    {
        char const* v = getenv("TORIRS_SPAWN_BREAKDOWN");
        bd_us = (v && v[0]) ? atoi(v) : 0;
        spawn_log = getenv("TORIRS_SPAWN_LOG") ? 1 : 0;
    }
    bd_t0 = bd_us ? PlatformWindow_TicksUs() : 0;

    npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
    if( !npctype )
    {
        /* Once per id. The server re-sends the same missing npc every time the
         * player walks back into its zone, and on Windows an unbuffered stderr
         * write costs milliseconds -- see the spawn narration below. Which ids
         * are unavailable is the whole diagnostic; the repeat count is not. */
        if( !app_warn_once_npc(npc_id) )
            TORIRS_LOG("spawn_npc: npc %d unavailable\n", npc_id);
        return -1;
    }

    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    if( npctype->models_count <= 0 )
    {
        /*
         * A model-less npc is legal content, not a broken record.
         *
         * The reference client builds the entity straight off the wire and only
         * resolves a model at draw time, where a null model skips the body and
         * leaves the entity otherwise intact (rev239 deob: the npc add path
         * constructs and registers unconditionally; Renderable.draw returns
         * early on a null model). OldSchool ships such npcs deliberately --
         * `invisible_npc_softblocking`, `hw22_trick_ghost_invis` -- as pure
         * server-side markers that still carry hitsplats, overhead text and
         * collision.
         *
         * Rejecting them here dropped the entity entirely: no element, so no
         * entry in the npc pool, so every later NPC_INFO mask for that slot
         * resolved to -1 and was discarded. An empty model keeps the entity in
         * the world and draws nothing; its zeroed bounds cylinder gives
         * height 0, which anchors overlays at the marker's own tile.
         */
        model = ToriDraw_ModelNew(0, 0, 0);
        if( model )
            ToriDraw_ModelSetBoundsCylinder(model);
    }
    else if( !app_world_npc_models_resident(app, npctype) )
    {
        /* The body is still on the wire: the same empty placeholder, without
         * the error below -- this is the expected state between the packet
         * and NpcBodyLand, not a failure. */
        model = ToriDraw_ModelNew(0, 0, 0);
        if( model )
            ToriDraw_ModelSetBoundsCylinder(model);
    }
    else
    {
        model = app_world_build_npc_model(app, npc_id, npctype);
    }
    if( bd_us )
        bd_model = PlatformWindow_TicksUs() - bd_t;
    if( !model )
    {
        /* Same rationale as the models_count<=0 branch above: a missing
         * model must not drop the entity itself, or every later NPC_INFO
         * mask for this slot resolves to -1 and is silently discarded for
         * the rest of the session, with no retry (npc_add only fires once
         * per spawn). This path is reached on a transient/real model-load
         * failure -- large multi-part npcs like QBD are the most exposed --
         * so register an empty placeholder and keep going; that degrades to
         * an invisible npc instead of erasing it outright. */
        TORIRS_ERR("spawn_npc: npc %d models failed to load\n", npc_id);
        model = ToriDraw_ModelNew(0, 0, 0);
        if( model )
            ToriDraw_ModelSetBoundsCylinder(model);
        if( !model )
            return -1;
    }
    /* Set explicitly both ways. Models arrive from ToriDraw_ModelFromToriRS with
     * no render flags, so the opt-in is this line and nothing else; clearing is
     * still written out because this model may be a cache copy of one that was
     * opted in earlier under a different npc id. */
    app_model_apply_import_render_flags(model, app_npc_wants_zbuffer(npc_id, npctype));

    app_npc_entity_facts(app, base_npc_id, npctype, &facts);
    size = facts.size;
    world_x = tile_x * 128 + size * 64;
    world_z = tile_z * 128 + size * 64;
    world_y = app_world_height(app, world_x, world_z, level);
    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_NPC, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
            el->anim_external = true;
        ToriDraw_SceneAnimListInvalidate(app->scene);
    }
    if( bd_us )
        bd_elem = PlatformWindow_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    {
        /* Config movement anims (dat1 has no turn/run for npcs; the reference
         * walkanim_l/r swap applies here at spawn like at CHANGE_TYPE). */
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = facts.readyanim,
            .walkanim = facts.walkanim,
            /* Opcodes 15/114 rather than the -1 pair that used to sit here.
             * `World_EntityFace` takes turnanim over walkanim and
             * `World_UpdateMoverMovementAndAnimation` takes runanim over
             * walkanim at speed; both were already written and neither had
             * anything to read. The run set gets the same left/right swap the
             * walk set does -- it is the same reference line (Client.ts
             * 8460-8462), which swaps the pair for every movement set. */
            .turnanim = facts.turnanim,
            .runanim = facts.runanim,
            .walkanim_b = facts.walkanim_b,
            .walkanim_r = facts.walkanim_l,
            .walkanim_l = facts.walkanim_r,
            .idle_anim_restart = npctype->idle_anim_restart ? 1 : 0,
        };
        idx = World_NpcSpawn(app->world, element_id, npc_id, level, tile_x, tile_z, size, idle);
    }
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( npc )
            npc->server_slot = -1;
    }
    if( bd_us )
        bd_world = PlatformWindow_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    app_world_apply_seq(app, element_id, facts.readyanim);
    if( bd_us )
        bd_seq = PlatformWindow_TicksUs() - bd_t;
    /* Spawn does not carry menu data; the minimenu rows read it off the
     * entity, so copy name/actions/level from the config here. */
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( npc )
        {
            npc->combat_level = npctype->combat_level;
            npc->alwaysontop = npctype->alwaysontop;
            npc->minimap_visible = npctype->minimap_visible;
            npc->interactable = npctype->interactable;
            npc->facing.turn_speed = npctype->turn_speed;
            snprintf(npc->name, sizeof(npc->name), "%s", npctype->name);
            for( int i = 0; i < 5; i++ )
                snprintf(
                    npc->actions[i].name, sizeof(npc->actions[i].name), "%s", npctype->actions[i]);
        }
    }
    /* Was unconditional. stderr is unbuffered, so this is a synchronous write
     * per npc arrival on the packet-apply path -- and npcs arrive in bursts of
     * 20+ when the player crosses into a populated zone. Gated behind its own
     * switch so the cost stays measurable (spawn_bd's `log` column). */
    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    if( spawn_log )
        TORIRS_LOG(
            "spawn_npc: npc=%d element=%d tile=%d,%d level=%d size=%d recolors=%d "
            "retextures=%d\n",
            npc_id,
            element_id,
            tile_x,
            tile_z,
            level,
            size,
            npctype->recolor_count,
            npctype->retexture_count);
    if( bd_us )
        bd_log = PlatformWindow_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    app_sync_textures(app);
    if( bd_us )
    {
        uint64_t total;

        bd_tex = PlatformWindow_TicksUs() - bd_t;
        total = PlatformWindow_TicksUs() - bd_t0;
        if( total >= (uint64_t)bd_us )
            TORIRS_LOG(
                "spawn_bd: npc=%d total %llu model %llu elem %llu world %llu seq %llu "
                "log %llu tex %llu (us)\n",
                npc_id,
                (unsigned long long)total,
                (unsigned long long)bd_model,
                (unsigned long long)bd_elem,
                (unsigned long long)bd_world,
                (unsigned long long)bd_seq,
                (unsigned long long)bd_log,
                (unsigned long long)bd_tex);
    }
    app->need_redraw = 1;
    if( idx != WORLD_ENTITY_NIL )
    {
        struct WorldEntity_NPC* spawned = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( spawned )
        {
            /* DRIVE_STAMP: npc_spawn -- a=npc slot b=npc_id. Unconditional --
             * the ring is present in every build, unlike the plugin-host
             * snapshot below, which stays gated on app->plugins. */
            App_DriveEvent(app, DRIVE_EVENT_NPC_SPAWN, idx, spawned->npc_id, 0, 0);
        }
        /* After the entity is in the pool and carries its name and facts, so
         * a handler's snapshot is the finished npc rather than a half-built
         * one. */
        if( app->plugins && spawned )
        {
            struct ToriRS_NpcSnapshot snap;
            app_plugin_fill_npc(app, spawned, &snap);
            PluginHost_NpcSpawn(app->plugins, &snap);
        }
    }
    return idx;
}

/* Hotkey 0 fire body: launch source -> destination. SYNCHRONOUS — the
 * projectile model must be resident (Task_AppSpawn awaits it). Arc math
 * lives in World_ProjectileSetTarget/Move (TS reference parity). `target` is
 * the wire target-entity id when a synced NPC sits on the destination tile,
 * WORLD_PROJECTILE_TARGET_NONE for a plain tile shot. */
static void
app_world_spawn_projectile_now(
    struct App* app,
    struct World* world,
    int model_id,
    int seq_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int tile_x,
    int tile_z,
    int target)
{
    int model_ids[1];
    struct ToriDraw_Model* model;
    int src_x, src_z, dst_x, dst_z, src_y;
    int range, t2;
    int element_id;

    assert(app);
    assert(world);
    model_ids[0] = model_id;
    model = app_world_build_model(app, model_ids, 1, NULL, 128, 128, APP_LIGHT_ACTOR, 0, 0);
    if( !model )
    {
        TORIRS_ERR("spawn_projectile: model %d failed to load\n", model_id);
        return;
    }

    src_x = src_tile_x * 128 + 64;
    src_z = src_tile_z * 128 + 64;
    dst_x = tile_x * 128 + 64;
    dst_z = tile_z * 128 + 64;
    /* World y is negative-up: start slightly above the source ground. */
    src_y = app_world_height(app, src_x, src_z, src_level) - 160;

    range = abs(tile_x - src_tile_x);
    if( abs(tile_z - src_tile_z) > range )
        range = abs(tile_z - src_tile_z);
    t2 = 60 + range * 5; /* ticks: base flight + per-tile stretch */

    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_PROJECTILE, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    World_ProjectileSpawn(
        world,
        element_id,
        src_level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        src_y,
        144, /* end height above target ground (36 * 4) */
        0,
        t2,
        15, /* launch slope (1/2048 circle units) */
        64,
        target);
    /* Bind the spotanim's sequence so the projectile model animates in flight
     * (v1 Task_*ProjectileAdd loads the seq, then ElementSetSequenceId). The
     * element is left non-external, so app_world_tick_animations advances the
     * frame each tick — matching ClientProj.move's plain frame loop, which is
     * why the element is marked anim_loop. */
    ToriDraw_SceneElementSetAnimLoop(app->scene, element_id, true);
    app_world_apply_seq(app, element_id, seq_id);
    TORIRS_LOG(
        "spawn_projectile: element=%d %d,%d -> %d,%d t2=%d target=%d\n",
        element_id,
        src_tile_x,
        src_tile_z,
        tile_x,
        tile_z,
        t2,
        target);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/*
 * The ground under a ROOT-frame point, composed through a hull when one is
 * there: a point over a live boat's deck answers the DECK's heightmap plus
 * the hull's y and bob — the same composition the emit path and the overlay
 * anchors use — and root terrain otherwise. A projectile fired from (or at)
 * the deck must measure its height from the planking, not from the grass or
 * water the hull happens to be sitting on: the deob never faces the question
 * because its projectile lives in the boat's own world, where "ground" IS
 * the deck; this is that answer for the root-world approximation.
 */
static int
app_world_ground_composed(
    struct App* app,
    int fine_x,
    int fine_z,
    int level)
{
    for( int view_id = 1; view_id < WORLDVIEW_MAX; view_id++ )
    {
        struct Wev* wev;
        struct WevDeckBox box;
        int deck_x;
        int deck_z;
        int deck_level;
        struct Worldview* view;

        if( !Wevs_IsLive(&app->wevs, view_id) ||
            !WorldviewRegistry_IsLive(&app->worldviews, view_id) )
            continue;
        wev = Wevs_Get(&app->wevs, view_id);
        if( wev->parent_view_id != WORLDVIEW_ROOT )
            continue;
        /* Membership is the HULL's footprint — the gunwale rule. The deck box
         * is the whole zone reservation and the templates carry walkable
         * staging ground around the parked hull, so a shore tile beside the
         * boat is inside the box but not on the planking; composing its
         * ground from the deck template would bend a shot aimed past the
         * rail. The footprint is the painter's own answer to "which root
         * tiles does this hull cover", so the two cannot disagree; a config
         * with no stated extent (the wire-sized Zenith) makes the footprint
         * the whole box, which is the right fallback too. */
        if( wev->config )
        {
            int min_tile_x;
            int min_tile_z;
            int size_x;
            int size_z;
            int abs_tile_x = (fine_x >> 7) + app->world->_base_tile_x;
            int abs_tile_z = (fine_z >> 7) + app->world->_base_tile_z;

            Wev_FootprintTiles(wev, 0, &min_tile_x, &min_tile_z, &size_x, &size_z);
            if( abs_tile_x < min_tile_x || abs_tile_x >= min_tile_x + size_x ||
                abs_tile_z < min_tile_z || abs_tile_z >= min_tile_z + size_z )
                continue;
        }
        app_wev_deck_box(app, wev, app->world, &box);
        Wev_DeckFromParent(&box, fine_x, fine_z, &deck_x, &deck_z);
        if( !Wev_DeckContainsDeckPoint(&box, deck_x, deck_z) )
            continue;
        view = WorldviewRegistry_Get(&app->worldviews, view_id);
        deck_level = app_wev_deck_level(app, view_id);
        if( deck_level >= COLLISION_LEVELS )
            deck_level = COLLISION_LEVELS - 1;
        return wev->y + wev->bob_y + World_HeightAt(view->world, deck_x, deck_z, deck_level);
    }
    return app_world_height(app, fine_x, fine_z, level);
}

/* Server-driven projectile (reference ClientProj / MAP_PROJANIM). Builds the
 * transformed spotanim model (recolour/resize/angle/lighting), spawns the world
 * projectile with the wire trajectory params, and binds the spotanim seq so the
 * model animates in flight. SYNCHRONOUS — the spotanimtype, its model and its
 * seq must already be resident (Task_AppSpawn awaits them). src_height/dst_height
 * are raw wire bytes (×4, matching Client.ts h1/h2). */
static void
app_world_spawn_projectile_spot_now(
    struct App* app,
    struct World* world,
    int spotanim_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int dst_tile_x,
    int dst_tile_z,
    int dst_level,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target,
    int late)
{
    struct ToriRS_Spotanimtype* spot;
    struct ToriDraw_Model* model;
    int src_x, src_z, dst_x, dst_z, src_y;
    int element_id;
    int idx;

    assert(app);
    assert(world);
    assert(late >= 0);
    /* Its assets arrived after the server had it land: nothing to show. */
    if( late > end_delay )
    {
        TORIRS_LOG(
            "spawn_projectile_spot: spotanim %d landed %d cycles before its assets\n",
            spotanim_id,
            late - end_delay);
        return;
    }
    spot = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !spot )
    {
        TORIRS_LOG("spawn_projectile_spot: spotanim %d not resident\n", spotanim_id);
        return;
    }

    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        TORIRS_ERR(
            "spawn_projectile_spot: spotanim %d model %d failed\n", spotanim_id, spot->model);
        return;
    }

    src_x = src_tile_x * 128 + 64;
    src_z = src_tile_z * 128 + 64;
    dst_x = dst_tile_x * 128 + 64;
    dst_z = dst_tile_z * 128 + 64;
    /* World y is negative-up: reference y = getAvH(src) - h1, h1 = src_height*4.
     * The SOURCE ground composes through a hull when the shooter stands on
     * one — a bow fired from the deck leaves at deck + chest, not at the
     * terrain the hull is parked over (a beached hull's planking can sit well
     * below the bank beside it). The deob never faces this: its projectile
     * lives in the boat's world, where ground IS the deck. The DESTINATION
     * deliberately does NOT compose: the landing samples this world's own
     * ground exactly as the deob's `getAvH(dst, proj.plane) - h2` does, and
     * World re-aims a tracked target with that same rule every cycle — a
     * composed one-shot correction here went stale the moment the target
     * moved and bent the arc whenever the hull's footprint brushed the
     * target's tile. */
    src_y = app_world_ground_composed(app, src_x, src_z, src_level) - src_height * 4;

    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_PROJECTILE, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    /* peak -> angle, arc -> startpos, end_height = dst_height*4 (World computes
     * dst y as height_fn(dst) - end_height, matching getAvH(dst) - h2). `target`
     * goes through in its wire encoding so World re-aims the arc at that
     * entity's live position every cycle (reference addProjectiles). */
    idx = World_ProjectileSpawn(
        world,
        element_id,
        dst_level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        src_y,
        dst_height * 4,
        start_delay,
        end_delay,
        peak,
        arc,
        target);
    /* The projectile's clock starts at enqueue, not at apply: aged by the
     * cycles its assets took, the next world cycle aims it from where it
     * stands to the target in the time that is left (World_ProjectileSetTarget
     * recomputes the velocity from the current position), so a graphic that
     * took 300 ms to arrive joins the flight rather than restarting it. */
    if( late > 0 )
    {
        struct WorldEntity_Projectile* p =
            World_EntityPoolGet(&world->entities.projectile, idx);
        assert(p);
        p->cycle = late;
    }
    /* Reference ClientProj.move wraps animFrame to 0 at the end of the frame
     * list and never drops the sequence. Flight time routinely outlasts the
     * sequence — a 12-cycle spotanim seq against a 30+ cycle flight is normal —
     * so without this the model spends most of the flight back in its un-posed
     * bind pose. Spotanim models hide geometry by scaling it to zero in every
     * frame, so what that actually looks like is extra parts of the model
     * appearing mid-air partway to the target. */
    ToriDraw_SceneElementSetAnimLoop(app->scene, element_id, true);
    app_world_apply_seq(app, element_id, spot->seq);

    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "spawn_projectile_spot: element=%d spotanim=%d model=%d seq=%d "
            "%d,%d -> %d,%d lvl=%d/%d t1=%d t2=%d target=%d src_y=%d ground=%d "
            "dst_ground=%d h1=%d h2=%d\n",
            element_id,
            spotanim_id,
            spot->model,
            spot->seq,
            src_tile_x,
            src_tile_z,
            dst_tile_x,
            dst_tile_z,
            src_level,
            dst_level,
            start_delay,
            end_delay,
            target,
            src_y,
            app_world_ground_composed(app, src_x, src_z, src_level),
            app_world_height(app, dst_x, dst_z, dst_level),
            src_height * 4,
            dst_height * 4);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Free-standing spotanim (reference MapSpotAnim / MAP_ANIM). SYNCHRONOUS — the
 * spotanimtype, its model and its seq must already be resident (Task_AppSpawn
 * awaits them). Builds the transformed model, spawns the world entity with a
 * single-shot lifetime equal to one seq loop, and binds the seq so the element
 * animates per-tick. */
void
app_world_spawn_spotanim_now(
    struct App* app,
    struct World* world,
    int spotanim_id,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int delay,
    int late)
{
    struct ToriRS_Spotanimtype* spot;
    struct ToriDraw_Model* model;
    int world_x, world_z, world_y;
    int element_id;
    int lifetime;

    assert(app);
    assert(world);
    assert(late >= 0);
    spot = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !spot )
    {
        TORIRS_LOG("spawn_spotanim: spotanim %d not resident\n", spotanim_id);
        return;
    }
    /* Loaded late: the wait comes off its delay first, and a graphic whose
     * whole life passed while its assets were on the wire is not shown at
     * all -- the reference's spot anims are gone by then too. */
    delay -= late;
    if( delay < 0 )
    {
        int const overdue = -delay;
        delay = 0;
        lifetime = WorldSeqSourceToriDraw_TotalDuration(&app->seq_source, spot->seq);
        if( lifetime > 0 && overdue >= lifetime )
        {
            TORIRS_LOG(
                "spawn_spotanim: id=%d expired %d cycles before its assets\n",
                spotanim_id,
                overdue - lifetime);
            return;
        }
    }

    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        TORIRS_ERR("spawn_spotanim: spotanim %d model %d failed\n", spotanim_id, spot->model);
        return;
    }

    world_x = tile_x * 128 + 64;
    world_z = tile_z * 128 + 64;
    /* Reference y = getAvH(x,z) - height; world y is negative-up so subtracting
     * raises the effect above the ground by `height`. */
    world_y = app_world_height(app, world_x, world_z, level) - height;

    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_SPOTANIM, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return;

    lifetime = WorldSeqSourceToriDraw_TotalDuration(&app->seq_source, spot->seq);

    World_SpotanimSpawn(world, element_id, level, world_x, world_z, world_y, 0, delay, lifetime);
    app_world_apply_seq(app, element_id, spot->seq);
    /* A delayed spotanim is invisible until World flips it active, so its
     * sequence must not run in the meantime. Park it as anim_external — the
     * flag the naive per-element tick uses to mean "someone else owns this
     * element's frames" — and let WorldEventKind_SpotanimStarted hand it back
     * on the cycle it first draws. Without this the whole delay is spent
     * animating out of sight: `spotanim_map`'s delay is a projectile's flight
     * time, which is routinely longer than the sequence, so the splash
     * surfaced already finished and sat on a cleared final frame. Elements
     * with no delay are left alone and start immediately, as before. */
    if( delay > 0 )
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
        {
            el->anim_external = true;
            /* anim_list membership is filtered on anim_external, so the cached
             * list is now stale (toridraw_scene.h: the caller mutating this
             * flag directly owns the invalidation). */
            ToriDraw_SceneAnimListInvalidate(app->scene);
        }
    }

    TORIRS_LOG(
        "spawn_spotanim: id=%d element=%d tile=%d,%d level=%d model=%d seq=%d "
        "life=%d delay=%d\n",
        spotanim_id,
        element_id,
        tile_x,
        tile_z,
        level,
        spot->model,
        spot->seq,
        lifetime,
        delay);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* ------------------------------------------------ plugin-authored meshes */

/*
 * Geometry a plugin built itself, and the model made from it.
 *
 * The reason this exists at all is that a cache id is not portable. A plugin
 * that stands a beam of light over a drop by naming model 43330 is naming a
 * number that means that beam in one revision, means something else in
 * another, and is absent from most -- so the plugin works on the cache it was
 * written against and silently draws a crate, or nothing, on the rest. An
 * authored mesh has no such dependency: it is the same triangles wherever it
 * is drawn.
 *
 * Storage is the App's rather than the host's because only this side can turn
 * a mesh into something drawable, and the arrays grow on append because the
 * api ceilings are a limit and not a size.
 */

struct AppPluginAssetModel*
app_plugin_asset_model_at(
    struct App* app,
    int handle)
{
    assert(app);
    if( handle < 0 || handle >= TORIRS_PLUGIN_MODELS_MAX )
        return NULL;
    if( !app->plugin_asset_models[handle].in_use )
        return NULL;
    return &app->plugin_asset_models[handle];
}

struct ToriRS_PluginMesh*
app_plugin_mesh_at(
    struct App* app,
    int handle)
{
    assert(app);
    if( handle < 0 || handle >= APP_PLUGIN_MESHES_MAX )
        return NULL;
    if( !app->plugin_meshes[handle].in_use )
        return NULL;
    return &app->plugin_meshes[handle];
}

/*
 * Find the scenery entity a LOC_ADD_CHANGE_V2 just spawned and dress it with
 * the placement menu the change carried.
 *
 * Which labels survive is WorldEntity_SceneryApplyPlacementOps'. This is the
 * part that needs a scene: which placement the change is talking about, and
 * the interning that keeps one label block per distinct menu.
 */
static void
app_loc_change_apply_ops(
    struct App* app,
    struct World* world,
    const struct Task_AppSpawn* self)
{
    int idx;
    struct WorldEntity_Scenery* sc;

    assert(app);
    (void)app; /* asserted only; unused under NDEBUG */
    assert(world);
    assert(self);
    if( self->loc_id < 0 )
        return; /* a pure delete has no placement to describe */
    idx = World_SceneryFindAt(world, self->tile_x, self->tile_z, self->level, self->loc_shape);
    if( idx < 0 )
        return; /* the spawn was refused (unknown loc, off-scene) — nothing to dress */
    sc = World_EntityPoolGet(&world->entities.scenery, idx);
    if( !sc )
        return;

    /* The label block is shared, so the override is not written in place: the
     * edited copy is interned and the placement repointed. */
    {
        struct WorldEntity_SceneryInfo probe = *sc->info;
        char const* replacements[5];
        bool has_action = false;

        for( int i = 0; i < 5; i++ )
            replacements[i] = self->loc_ops[i];

        sc->placement_op_mask = (uint8_t)self->loc_op_flags;
        sc->placement_op_overrides = WorldEntity_SceneryApplyPlacementOps(
            &probe, (unsigned)self->loc_op_flags, replacements, &has_action);
        sc->info = World_SceneryInfoIntern(world, &probe);
        if( has_action )
            sc->interactive = 1;
    }
}

/*
 * A spotanim's model and its sequence, fanned out TOGETHER.
 *
 * Both ids come off the one spotanimtype and neither read feeds the other, so
 * awaiting them one after another was a round trip apiece on a streamed cache
 * — the same shape the loc-change branch below already fixed for a door. The
 * first attack after a login is where it showed worst: every combat graphic the
 * browser's cache had never seen paid model-then-sequence in series, and
 * because the spawn runs on the EXEC runner, the packet pipeline behind it —
 * every other player, npc, tick — waited out both.
 *
 * Fills in model_id and seq_id for the apply that follows; the caller joins on
 * `pending`.
 */
static void
app_spawn_fan_spotanim_assets(struct Task_AppSpawn* self)
{
    struct App* app;
    struct ToriRS_Spotanimtype* spot;

    assert(self);
    app = self->app;
    assert(app);

    spot = CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
    self->model_id = (spot && spot->model > 0) ? spot->model : -1;
    self->seq_id = spot ? spot->seq : -1;

    if( self->model_id > 0 )
        ToriRS_TaskQueue_AddParallelPoolSubTask(
            app->runner.queue,
            CreateTask_ModelLoad(app->provider, self->model_id),
            &self->pending);
    if( self->seq_id >= 0 )
        ToriRS_TaskQueue_AddParallelPoolSubTask(
            app->runner.queue,
            CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id),
            &self->pending);
}

/*
 * The world an EFFECT applies to, or NULL when it must be dropped.
 *
 * Effects load on the asset runner (app_spawn_effect_queue, app_world_edit.c),
 * so nothing orders their apply against the packets that came after them --
 * including a REBUILD. A rebuild finishing first tears down the scene the
 * effect was aimed at, and the same tile coordinates then name a different
 * place; the reference drops its pending spot anims on rebuild for the same
 * reason. The view dying (a boat despawning) is the other way the scene can
 * go, and both are guards, not asserts: a task cannot be told the world
 * moved under it while it was parked.
 */
static struct World*
app_spawn_effect_world(struct Task_AppSpawn const* self)
{
    struct App* app;
    struct Worldview* wv;

    assert(self);
    app = self->app;
    assert(app);
    if( !WorldviewRegistry_IsLive(&app->worldviews, self->view) )
        return NULL;
    wv = WorldviewRegistry_Get(&app->worldviews, self->view);
    if( !wv->world )
        return NULL;
    if( wv->world->load_seq != self->world_load_seq )
    {
        TORIRS_LOG(
            "spawn: effect kind=%d dropped, scene rebuilt while its assets loaded\n",
            (int)self->kind);
        return NULL;
    }
    /* Mid-rebuild: the scene is being reset under this runner, which the
     * rebuild itself is pumping. An effect that lands now lands in a scene
     * being torn down; one that arrives after belongs to the old scene
     * anyway (the generation above). Dropped, as the reference drops its
     * pending spot anims on a rebuild. */
    if( !wv->world->load_complete )
        return NULL;
    return wv->world;
}

/* Cycles the effect spent loading: what its delays are shortened by, so it
 * appears where the server's timeline has it, not late by its own load. */
static int
app_spawn_effect_late(
    struct Task_AppSpawn const* self,
    struct World const* world)
{
    int late;

    assert(self);
    assert(world);
    late = world->cycle - self->enqueue_cycle;
    return late > 0 ? late : 0;
}

static int
Task_AppSpawn_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AppSpawn* self = (struct Task_AppSpawn*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    if( self->kind == APP_SPAWN_PLAYER )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
        PT_TASK_AWAITSELF_IF(
            CreateTask_SequenceLoad(app->provider, app->scene, APP_PLAYER_SEQ_READY));
        app_world_spawn_player_now(app, self->tile_x, self->tile_z, self->level);
    }
    else if( self->kind == APP_SPAWN_NPC )
    {
        /* npc_id is the requested/wrapper id; model_id temporarily carries
         * this player's selected child so developer/content spawns follow the
         * same multiNpc path as NPC_INFO. */
        PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiLoad(app, self->npc_id, &self->model_id));
        {
            int effective = self->model_id >= 0 ? self->model_id : self->npc_id;
            int idx = app_world_spawn_npc_now(
                app, effective, self->npc_id, self->tile_x, self->tile_z, self->level);
            struct WorldEntity_NPC* npc =
                idx >= 0 ? World_EntityPoolGet(&app->world->entities.npc, idx) : NULL;
            if( npc )
            {
                npc->base_npc_id = self->npc_id;
                npc->multinpc_hidden = self->model_id < 0;
            }
        }
    }
    else if( self->kind == APP_SPAWN_OBJ )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->obj_id));
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, self->obj_id);
            if( obj && obj->inventory_model_id > 0 )
                self->model_id = obj->inventory_model_id;
        }
        if( self->model_id > 0 )
        {
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
            /* The applicator resolves the cursor itself, so re-arm it with the
             * view captured at enqueue for the duration of the call. The view
             * can have died while the task was parked (a despawned boat takes
             * its late drops with it) — that is the guard, not a contract. */
            if( WorldviewRegistry_IsLive(&app->worldviews, self->view) )
            {
                int prev_view = app->active_world;
                app->active_world = self->view;
                App_WorldObjStackAdd(app, self->tile_x, self->tile_z, self->level, self->obj_id, 1);
                app->active_world = prev_view;
            }
        }
        else
            TORIRS_LOG("spawn_obj: obj %d has no inventory model\n", self->obj_id);
    }
    else if( self->kind == APP_SPAWN_SPOTANIM )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        app_spawn_fan_spotanim_assets(self);
        PT_TASK_JOIN(pending);
        {
            struct World* world = app_spawn_effect_world(self);
            if( world )
                app_world_spawn_spotanim_now(
                    app,
                    world,
                    self->spotanim_id,
                    self->tile_x,
                    self->tile_z,
                    self->level,
                    self->spotanim_height,
                    self->spotanim_delay,
                    app_spawn_effect_late(self, world));
        }
    }
    else if( self->kind == APP_SPAWN_ENTITY_SPOTANIM )
    {
        /* Attached graphic (SPOTANIM mask): load the same asset chain as the
         * free-standing spotanim. No completion callback — once resident,
         * app_world_sync_entity_spotanims combines synchronously next frame. */
        PT_TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        app_spawn_fan_spotanim_assets(self);
        PT_TASK_JOIN(pending);
        app->need_redraw = 1;
    }
    else if( self->kind == APP_SPAWN_PROJECTILE_SPOT )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        app_spawn_fan_spotanim_assets(self);
        PT_TASK_JOIN(pending);
        {
            struct World* world = app_spawn_effect_world(self);
            if( world )
                app_world_spawn_projectile_spot_now(
                    app,
                    world,
                    self->spotanim_id,
                    self->src_tile_x,
                    self->src_tile_z,
                    self->src_level,
                    self->tile_x,
                    self->tile_z,
                    self->level,
                    self->proj_src_height,
                    self->proj_dst_height,
                    self->proj_start_delay,
                    self->proj_end_delay,
                    self->proj_peak,
                    self->proj_arc,
                    self->proj_target,
                    app_spawn_effect_late(self, world));
        }
    }
    else if( self->kind == APP_SPAWN_PLUGIN_OBJECT )
    {
        /*
         * Same asset chain as a spotanim, resolved through whichever source
         * the object named. Every step re-reads the record through its handle
         * rather than caching a pointer across the awaits: a plugin may have
         * destroyed the object, or restated its model, while this was parked.
         */
        {
            struct AppPluginObject* obj = app_plugin_object_at(app, self->plugin_object);
            self->spotanim_id =
                (obj && obj->source == TORIRS_HOST_MODEL_SPOTANIM) ? obj->model_id : -1;
        }
        if( self->spotanim_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        {
            struct AppPluginObject* obj = app_plugin_object_at(app, self->plugin_object);
            self->model_id = obj ? app_plugin_object_model_id(app, obj) : -1;
            self->seq_id = obj ? app_plugin_object_seq_id(app, obj) : -1;
        }
        if( self->model_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        if( self->seq_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        app_plugin_object_materialize_now(app, self->plugin_object);
    }
    else if( self->kind == APP_SPAWN_LOC_ANIM )
    {
        /* Nothing to load: the point of the task is its TICKET on the loc
         * lane, behind any LOC_ADD_CHANGE for the same tile in the same
         * packet. See App_WorldSceneryAnim and app_spawn_loc_lane_queue. */
        TASK_AWAIT_STATE(&self->task, &self->pt, app->loc_lane_applied + 1 == self->loc_ticket);
        {
            struct World* world = app_spawn_effect_world(self);
            if( world )
                app_world_scenery_anim_apply(
                    app,
                    world,
                    self->tile_x,
                    self->tile_z,
                    self->level,
                    self->loc_shape,
                    self->seq_id);
        }
        app_spawn_loc_lane_release(self);
    }
    else if( self->kind == APP_SPAWN_LOC_CHANGE )
    {
        /* Reference locChangeDoQueue (Client.ts:7701): a zone loc change only
         * applies once changeLocAvailable — the loc config and every model it
         * references are resident (an open-door variant is usually absent from
         * the static map build's preload). LOC_DEL (loc_id < 0) has nothing to
         * load but still runs through this task so same-tile changes apply in
         * packet order on the serial exec FIFO. */
        if( self->loc_id >= 0 )
        {
            self->loc_resolved_id = self->loc_id;
            self->loc_base_seq = -1;
            for( self->loc_resolve_depth = 0;
                 self->loc_resolved_id >= 0 && self->loc_resolve_depth < 16;
                 ++self->loc_resolve_depth )
            {
                PT_TASK_AWAITSELF_IF(CreateTask_LocLoad(app->provider, self->loc_resolved_id));
                struct ToriRS_Location* cfg =
                    CacheProvider_LocationGet(app->provider, self->loc_resolved_id);
                if( !cfg )
                {
                    self->loc_resolved_id = -1;
                    break;
                }
                if( self->loc_resolve_depth == 0 )
                    self->loc_base_seq = cfg->seq_id;
                if( cfg->transform_count <= 0 || !cfg->transforms )
                    break;
                int next = VarPManager_ResolveTransform(
                    &app->varps,
                    cfg->transforms,
                    cfg->transform_count,
                    cfg->transform_varbit,
                    cfg->transform_varp);
                if( next == self->loc_resolved_id )
                    break;
                self->loc_resolved_id = next;
            }
            /*
             * Every model the resolved child names, and its sequence, TOGETHER: they
             * are independent reads with one consumer (the placement below),
             * and awaiting them one after another was a round trip each on a
             * streamed cache -- an open-door variant is commonly absent from
             * the map build's preload, so a door was that chain every time.
             * Queued as siblings on the asset queue and joined.
             */
            {
                struct ToriRS_Location* cfg =
                    self->loc_resolved_id >= 0
                        ? CacheProvider_LocationGet(app->provider, self->loc_resolved_id)
                        : NULL;
                int entries = 0;
                if( cfg && cfg->models && cfg->lengths )
                    entries = cfg->shapes ? cfg->shapes_and_model_count : 1;
                for( int i = 0; i < entries; i++ )
                    for( int j = 0; j < cfg->lengths[i]; j++ )
                        if( cfg->models[i][j] >= 0 )
                            ToriRS_TaskQueue_AddParallelPoolSubTask(
                                app->runner.queue,
                                CreateTask_ModelLoad(app->provider, cfg->models[i][j]),
                                &self->pending);
                self->seq_id = cfg && cfg->seq_id >= 0 ? cfg->seq_id : self->loc_base_seq;
                if( self->seq_id >= 0 )
                    ToriRS_TaskQueue_AddParallelPoolSubTask(
                        app->runner.queue,
                        CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id),
                        &self->pending);
            }
            PT_TASK_JOIN(pending);
        }
        /* Loaded; now its turn. The wait is on the lane's counter, which
         * only the task before this one advances -- state this queue does
         * not own in the runner's sense, so it is a blocked yield, retested
         * once per pass and never spun. */
        TASK_AWAIT_STATE(&self->task, &self->pt, app->loc_lane_applied + 1 == self->loc_ticket);
        /* The scene captured at enqueue: the view can die and the world can be
         * rebuilt while the change loaded, and both drop it (see
         * app_spawn_effect_world). */
        struct World* world = app_spawn_effect_world(self);
        struct Worldview* wv = world ? WorldviewRegistry_Get(&app->worldviews, self->view) : NULL;
        if( wv && wv->builder && world && world->load_complete )
        {
            int old_type = -1;
            int old_angle = 0;
            int old_shape = -1;
            int old_idx = World_SceneryFindAt(
                world, self->tile_x, self->tile_z, self->level, self->loc_shape);
            if( old_idx >= 0 )
            {
                struct WorldEntity_Scenery* old =
                    World_EntityPoolGet(&world->entities.scenery, old_idx);
                if( old )
                {
                    old_type = old->loc_id;
                    old_angle = old->angle;
                    old_shape = old->shape;
                }
            }
            World_LocChangePush(
                world,
                self->level,
                World_LocShapeToLayer(self->loc_shape),
                self->tile_x,
                self->tile_z,
                old_type,
                old_angle,
                old_shape,
                self->loc_id,
                self->loc_angle,
                self->loc_shape,
                world->cycle,
                -1);
            WorldBuilder_ApplyLocChange(
                wv->builder,
                self->tile_x,
                self->tile_z,
                self->level,
                self->loc_id,
                self->loc_shape,
                self->loc_angle);
            /*
             * The placement's own menu, over the loctype's.
             *
             * After the spawn and not before it: the scenery entity is created
             * by ApplyLocChange with the loctype's actions copied in, so this
             * is the only point where both the entity and the override exist.
             * The reference does the same thing in the same order — its scene
             * loc carries the mask and the labels, and the menu builder reads
             * the loctype first and lets the placement win (deob class108).
             */
            app_loc_change_apply_ops(app, world, self);
            /*
             * The cache's own "a loc was placed" script, for a loc that arrived
             * AFTER the scene was built.
             *
             * The world-loaded sweep covers the map's own locs; this covers a
             * zone packet's, and the difference is not academic -- a Dwarf
             * multicannon is placed by the server four ticks after you click,
             * and clientscript 6672 is bound to `dwarf_multicannon1` by exactly
             * this trigger. Without it the cannon hud (All Settings row 247)
             * can never appear, because the cannon is never a loc the sweep
             * saw. See game/rs_client_trigger.h.
             *
             * After the ops, for the same reason they are applied after the
             * spawn: this is the first point at which the entity exists and is
             * finished, and the script reads its type and its coord.
             */
            if( self->loc_id >= 0 )
            {
                int const placed_idx = World_SceneryFindAt(
                    world, self->tile_x, self->tile_z, self->level, self->loc_shape);
                struct WorldEntity_Scenery* placed =
                    placed_idx >= 0 ? World_EntityPoolGet(&world->entities.scenery, placed_idx)
                                    : NULL;
                if( placed )
                    app_client_trigger_loc(app, placed, RS_TRIGGER_LOC_ADD);
            }
            app_sync_textures(app);
            app->need_redraw = 1;
        }
        app_spawn_loc_lane_release(self);
    }
    else
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        if( app_spawn_effect_world(self) )
            app_world_spawn_projectile_now(
                app,
                app_spawn_effect_world(self),
                self->model_id,
                self->seq_id,
                self->src_tile_x,
                self->src_tile_z,
                self->src_level,
                self->tile_x,
                self->tile_z,
                self->proj_target);
    }

    PT_END(&self->pt);
}

/*
 * Give the loc lane its next turn. Exactly once per lane task, whether it
 * applied, was dropped for a dead scene, or was freed unrun with its queue:
 * a ticket that never released would hold every loc change after it forever.
 */
static void
app_spawn_loc_lane_release(struct Task_AppSpawn* self)
{
    assert(self);
    if( self->loc_ticket == 0 || self->loc_ticket_released )
        return;
    self->loc_ticket_released = 1;
    self->app->loc_lane_applied++;
}

static void
Task_AppSpawn_Free(struct ToriRS_Task* base)
{
    app_spawn_loc_lane_release((struct Task_AppSpawn*)base);
    free(base);
}

