/*
 * PLUGIN-AUTHORED OBJECTS -- world objects a plugin creates and owns, rather
 * than ones the server spawned: their slots, the models they resolve to, and
 * the bookkeeping that keeps a plugin's object in the scene across a rebuild.
 *
 * A unity fragment of app.c, not a module. It is textually part of app.c's
 * translation unit and included at exactly the point it was cut from, so every
 * helper here stays static and every App field it reads stays where it was.
 * The split is for the reader.
 *
 * The mesh building has left: ToriRS_PluginMeshBuildModel turns a plugin's
 * vertex and face arrays into a model, with no App in sight. What stays is the
 * part that needs one -- which slot, which provider, which scene, and when a
 * stale model has to be rebuilt because the plugin changed its mind.
 *
 * @see src/app.c, src/plugin/torirs_plugin_mesh.h
 */

/*
 * A plugin's model, standing in the scene.
 *
 * Three things have to agree for one of these to draw, and they arrive at
 * different times: the plugin's intent (a model id, a tile, a colour), the
 * cache assets that intent names, and a scene element to hang them on. The
 * record in app->plugin_objects is the intent; everything below is the
 * machinery that keeps the other two chasing it.
 *
 * The rule that makes it tractable: intent is never applied in place. A change
 * to anything the MODEL is built from tears the element down and rebuilds it;
 * a change to where it stands or whether it is drawn is applied to the live
 * entity. Deciding which is which is `built_*` versus what the plugin now
 * says, which is also what stops a plugin that re-states the same intent every
 * frame from rebuilding a model every frame.
 */

static struct AppPluginObject*
app_plugin_object_at(
    struct App* app,
    int handle)
{
    assert(app);
    if( handle < 0 || handle >= APP_PLUGIN_OBJECTS_MAX )
        return NULL;
    if( !app->plugin_objects[handle].in_use )
        return NULL;
    return &app->plugin_objects[handle];
}

/* A cheap identity for the recolour list, so "did the colours change?" does not
 * mean comparing two arrays on every call. Order-sensitive on purpose: recolour
 * is applied in order and two lists that differ only in order can genuinely
 * produce different models. */
static int
app_plugin_object_recolor_stamp(struct AppPluginObject const* obj)
{
    int stamp = 17;
    assert(obj);
    for( int i = 0; i < obj->recolor_count; i++ )
        stamp = stamp * 31 + (obj->recolor_from[i] * 65599 + obj->recolor_to[i]);
    return stamp * 31 + obj->recolor_count;
}

/* The revision of the geometry this object stands on, or 0 when it stands on
 * none -- a cache-sourced object, or one whose mesh was destroyed or whose
 * shipped model never decoded. Compared against built_geometry_revision, so
 * geometry that is re-authored, or that arrives late off the IO queue,
 * rebuilds the objects made from it, and geometry re-stated unchanged rebuilds
 * nothing. */
static int
app_plugin_object_geometry_revision(
    struct App* app,
    struct AppPluginObject const* obj)
{
    assert(app);
    assert(obj);

    if( obj->source == TORIRS_HOST_MODEL_MESH )
    {
        struct ToriRS_PluginMesh const* mesh = app_plugin_mesh_at(app, obj->model_id);
        return mesh ? mesh->revision : 0;
    }
    if( obj->source == TORIRS_HOST_MODEL_ASSET )
    {
        struct AppPluginAssetModel const* shipped = app_plugin_asset_model_at(app, obj->model_id);
        return shipped ? shipped->revision : 0;
    }
    return 0;
}

/* The model ids this object's source needs resident before it can be built:
 * a CACHE object names its model directly, a SPOTANIM object names it through
 * the spotanimtype. Returns -1 when it is not knowable yet. */
static int
app_plugin_object_model_id(
    struct App* app,
    struct AppPluginObject const* obj)
{
    assert(app);
    assert(obj);

    if( obj->source == TORIRS_HOST_MODEL_SPOTANIM )
    {
        struct ToriRS_Spotanimtype* spot =
            obj->model_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, obj->model_id) : NULL;
        return spot ? spot->model : -1;
    }
    /* A MESH object's model_id is a mesh handle and an ASSET object's is a
     * model-load handle -- neither is a cache id, and neither has anything for
     * the spawn task to make resident. That is the whole point of both: the
     * geometry travels with the plugin. */
    if( obj->source == TORIRS_HOST_MODEL_MESH || obj->source == TORIRS_HOST_MODEL_ASSET )
        return -1;
    return obj->model_id;
}

/* The sequence the object should play: the plugin's if it named one, else the
 * spotanimtype's own. -1 = no animation. */
static int
app_plugin_object_seq_id(
    struct App* app,
    struct AppPluginObject const* obj)
{
    assert(app);
    assert(obj);

    /* An AUTHORED mesh carries no rig, and a cache sequence is a table of
     * transforms addressed by transform group -- there is nothing in a mesh
     * for one to drive. Binding one is a plugin bug rather than a shape this
     * can express, so it stops here instead of animating nothing.
     *
     * A SHIPPED model is not covered: a model file can carry bones, and a
     * plugin that ships one alongside a cache revision it knows may legitimately
     * name that revision's sequence. Whether the two agree is its business. */
    assert(!(obj->source == TORIRS_HOST_MODEL_MESH && obj->seq_id >= 0));

    if( obj->seq_id >= 0 )
        return obj->seq_id;
    if( obj->source == TORIRS_HOST_MODEL_SPOTANIM )
    {
        struct ToriRS_Spotanimtype* spot =
            obj->model_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, obj->model_id) : NULL;
        return spot ? spot->seq : -1;
    }
    return -1;
}

/*
 * Build the drawable model. SYNCHRONOUS -- everything it reads must already be
 * resident (the spawn task awaits it). Returns an owned model or NULL.
 *
 * Not routed through app_world_build_spotanim_model even for a SPOTANIM
 * object, and not through the instance cache either, for one reason: the
 * plugin's recolours have to be applied BEFORE lighting. Lighting bakes the
 * face colours into the per-vertex a/b/c triples the rasteriser reads, and a
 * recolour after that point rewrites a table nothing looks at again -- the
 * model comes out exactly the colour it started. That is a silent failure, so
 * this path is written out rather than layered on one that cannot express it.
 */
static struct ToriDraw_Model*
app_plugin_object_build_model(
    struct App* app,
    struct AppPluginObject const* obj)
{
    struct ToriRS_Spotanimtype const* spot = NULL;
    struct ToriDraw_Model* model;
    int retextured = 0;

    assert(app);
    assert(obj);

    if( obj->source == TORIRS_HOST_MODEL_ASSET )
    {
        /* Not resident yet is the ordinary state for the first frame or two:
         * the file crosses the IO queue like every other asset, and the settle
         * builds the object when it lands. */
        struct AppPluginAssetModel const* shipped = app_plugin_asset_model_at(app, obj->model_id);
        if( !shipped || !shipped->model )
            return NULL;
        model = ToriDraw_ModelFromToriRS(shipped->model);
        if( !model )
            return NULL;
    }
    else if( obj->source == TORIRS_HOST_MODEL_MESH )
    {
        struct ToriRS_PluginMesh const* mesh = app_plugin_mesh_at(app, obj->model_id);
        /* An empty mesh is not a bug: a plugin that has taken a handle and not
         * yet authored into it is mid-build, and the object has nothing to
         * draw until it has. A handle that names no mesh at all is the same
         * answer from the object's side -- it was destroyed under it. */
        if( !mesh || mesh->face_count <= 0 )
            return NULL;
        model = ToriRS_PluginMeshBuildModel(mesh);
    }
    else
    {
        int const model_id = app_plugin_object_model_id(app, obj);

        if( obj->source == TORIRS_HOST_MODEL_SPOTANIM )
            spot = obj->model_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, obj->model_id)
                                      : NULL;
        if( model_id < 0 )
            return NULL;

        {
            struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, model_id);
            model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
        }
        if( !model )
            return NULL;

        /* The spotanimtype's own recolours first, so the plugin's pairs are
         * stated against the colours it can actually see on the finished
         * graphic. */
        if( spot )
        {
            if( spot->recol_s[0] != 0 )
            {
                for( int i = 0; i < 6; i++ )
                    ToriDraw_ModelRecolor(model, spot->recol_s[i], spot->recol_d[i]);
            }
            for( int i = 0; i < 6; i++ )
            {
                if( spot->retex_s[i] != 0 )
                {
                    ToriDraw_ModelRetexture(model, spot->retex_s[i], spot->retex_d[i]);
                    retextured = 1;
                }
            }
        }
    }

    for( int i = 0; i < obj->recolor_count; i++ )
        ToriDraw_ModelRecolor(model, obj->recolor_from[i], obj->recolor_to[i]);

    if( retextured )
        ToriDraw_ModelNoteTextureWants(model);

    /* Recorded, not applied: the resize belongs after every animation frame,
     * for the reason app_world_build_spotanim_model gives. */
    if( spot )
    {
        ToriDraw_ModelSetPostResize(model, spot->resizeh, spot->resizeh, spot->resizev);
        if( spot->angle != 0 )
            ToriDraw_ModelOrient(model, spot->angle / 90);
    }

    ToriDraw_ModelDropNonSdTextures(app->provider, model);
    ToriDraw_ModelNoteTextureWants(model);

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        /* The plugin's offsets ON TOP of the type's, so a SPOTANIM object with
         * no light of its own looks exactly like the server-drawn graphic. */
        ToriDraw_LightModelActor(
            hnd,
            (spot ? spot->contrast : 0) + obj->contrast,
            (spot ? spot->ambient : 0) + obj->ambient);
    }
    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_ModelApplyPostTransforms(model);
    ToriDraw_ModelSetBoundsCylinder(model);
    return model;
}

/* Drop the live element and world entity, leaving the intent alone. The
 * EntityRemoved event frees the scene element on the next drain, which is the
 * same path every other despawn takes. */
static void
app_plugin_object_teardown(
    struct App* app,
    struct AppPluginObject* obj)
{
    assert(app);
    assert(obj);

    if( obj->world_index >= 0 && app->world )
        World_PluginObjectDespawn(app->world, obj->world_index);
    obj->world_index = -1;
    obj->element_id = -1;
    obj->built_source = -1;
    obj->built_model_id = -1;
    obj->built_recolor_stamp = 0;
    obj->built_geometry_revision = 0;
}

/* Scene-local placement for an object's absolute tile, or 0 when it is off the
 * current scene. */
static int
app_plugin_object_scene_pos(
    struct App* app,
    struct AppPluginObject const* obj,
    int* out_world_x,
    int* out_world_z,
    int* out_world_y)
{
    int scene_x;
    int scene_z;

    assert(app);
    assert(obj);
    assert(out_world_x);
    assert(out_world_z);
    assert(out_world_y);

    if( !app->world )
        return 0;
    scene_x = obj->tile_x - app->world->_base_tile_x;
    scene_z = obj->tile_z - app->world->_base_tile_z;
    if( scene_x < 0 || scene_z < 0 || scene_x >= app->world->_scene_size ||
        scene_z >= app->world->_scene_size )
        return 0;

    *out_world_x = scene_x * 128 + 64;
    *out_world_z = scene_z * 128 + 64;
    /* World y is negative-up, so subtracting `height` raises the model off the
     * ground -- the same arithmetic a map spotanim's height uses. */
    *out_world_y = app_world_height(app, *out_world_x, *out_world_z, obj->level) - obj->height;
    return 1;
}

/* Build the element and hand it to World. SYNCHRONOUS -- the model and seq
 * must be resident. */
static void
app_plugin_object_materialize_now(
    struct App* app,
    int handle)
{
    struct AppPluginObject* obj = app_plugin_object_at(app, handle);
    struct ToriDraw_Model* model;
    int world_x;
    int world_z;
    int world_y;
    int element_id;
    int seq_id;

    assert(app);
    if( !obj )
        return; /* destroyed while its load was in flight */
    obj->load_pending = 0;
    if( obj->element_id >= 0 )
        return; /* a second task raced ahead of this one */
    if( !app_plugin_object_scene_pos(app, obj, &world_x, &world_z, &world_y) )
        return;

    model = app_plugin_object_build_model(app, obj);
    if( !model )
        return;

    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_NONE, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return;
    /* The element carries the yaw, not the entity: the painter is handed an
     * element id and reads the orientation off it, so an object whose yaw
     * lived only on the WorldEntity stood in its bind orientation forever --
     * a documented parameter that turned nothing. */
    ToriDraw_SceneElementSetPosition(app->scene, element_id, world_x, world_y, world_z, obj->yaw);

    obj->element_id = element_id;
    obj->built_source = obj->source;
    obj->built_model_id = obj->model_id;
    obj->built_recolor_stamp = app_plugin_object_recolor_stamp(obj);
    obj->built_geometry_revision = app_plugin_object_geometry_revision(app, obj);
    obj->world_index = World_PluginObjectSpawn(
        app->world,
        element_id,
        obj->level,
        world_x,
        world_z,
        world_y,
        obj->yaw,
        /*size_x=*/1,
        /*size_z=*/1);
    World_PluginObjectSetActive(app->world, obj->world_index, obj->active != 0);

    seq_id = app_plugin_object_seq_id(app, obj);
    if( seq_id >= 0 )
    {
        /* Before the bind, not after: the flag is read by the per-element tick
         * from the first cycle the sequence advances, and a beam that plays
         * once and freezes on its terminal frame is the whole difference
         * between an idle loop and a one-shot graphic. */
        ToriDraw_SceneElementSetAnimLoop(app->scene, element_id, obj->loop != 0);
        app_world_apply_seq(app, element_id, seq_id);
    }

    app_sync_textures(app);
    app->need_redraw = 1;
}
