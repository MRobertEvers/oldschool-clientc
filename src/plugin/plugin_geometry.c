#include "plugin/plugin_geometry.h"

#include <assert.h>
#include <string.h>

int
PluginGeometry_ObjectClaim(struct PluginGeometry* geom)
{
    assert(geom);

    for( int i = 0; i < PLUGIN_GEOMETRY_OBJECTS_MAX; i++ )
    {
        struct PluginObject* obj = &geom->objects[i];
        if( obj->in_use )
            continue;
        memset(obj, 0, sizeof(*obj));
        obj->in_use = 1;
        obj->source = TORIRS_HOST_MODEL_CACHE;
        obj->model_id = -1;
        obj->seq_id = -1;
        obj->loop = 1;
        obj->level = -1;
        obj->element_id = -1;
        obj->world_index = -1;
        obj->built_source = -1;
        obj->built_model_id = -1;
        return i;
    }
    return -1;
}

struct PluginObject*
PluginGeometry_Object(
    struct PluginGeometry* geom,
    int handle)
{
    assert(geom);
    if( handle < 0 || handle >= PLUGIN_GEOMETRY_OBJECTS_MAX )
        return NULL;
    if( !geom->objects[handle].in_use )
        return NULL;
    return &geom->objects[handle];
}

void
PluginGeometry_ObjectRelease(
    struct PluginGeometry* geom,
    int handle)
{
    assert(geom);
    assert(handle >= 0);
    assert(handle < PLUGIN_GEOMETRY_OBJECTS_MAX);
    assert(geom->objects[handle].in_use);
    memset(&geom->objects[handle], 0, sizeof(geom->objects[handle]));
}

void
PluginObject_ForgetBuilt(struct PluginObject* obj)
{
    assert(obj);
    obj->element_id = -1;
    obj->world_index = -1;
    obj->built_source = -1;
    obj->built_model_id = -1;
    obj->built_recolor_stamp = 0;
    obj->built_geometry_revision = 0;
}

int
PluginObject_RecolorStamp(struct PluginObject const* obj)
{
    int stamp = 17;
    assert(obj);
    for( int i = 0; i < obj->recolor_count; i++ )
        stamp = stamp * 31 + (obj->recolor_from[i] * 65599 + obj->recolor_to[i]);
    return stamp * 31 + obj->recolor_count;
}

int
PluginGeometry_ObjectRevision(
    struct PluginGeometry* geom,
    struct PluginObject const* obj)
{
    assert(geom);
    assert(obj);

    if( obj->source == TORIRS_HOST_MODEL_MESH )
    {
        struct ToriRS_PluginMesh const* mesh = PluginGeometry_Mesh(geom, obj->model_id);
        return mesh ? mesh->revision : 0;
    }
    if( obj->source == TORIRS_HOST_MODEL_ASSET )
    {
        struct PluginAssetModel const* shipped = PluginGeometry_Model(geom, obj->model_id);
        return shipped ? shipped->revision : 0;
    }
    return 0;
}

int
PluginGeometry_MeshClaim(struct PluginGeometry* geom)
{
    assert(geom);

    for( int i = 0; i < PLUGIN_GEOMETRY_MESHES_MAX; i++ )
    {
        struct ToriRS_PluginMesh* mesh = &geom->meshes[i];
        if( mesh->in_use )
            continue;
        memset(mesh, 0, sizeof(*mesh));
        mesh->in_use = 1;
        mesh->revision = 1;
        return i;
    }
    return -1;
}

struct ToriRS_PluginMesh*
PluginGeometry_Mesh(
    struct PluginGeometry* geom,
    int handle)
{
    assert(geom);
    if( handle < 0 || handle >= PLUGIN_GEOMETRY_MESHES_MAX )
        return NULL;
    if( !geom->meshes[handle].in_use )
        return NULL;
    return &geom->meshes[handle];
}

struct PluginAssetModel*
PluginGeometry_Model(
    struct PluginGeometry* geom,
    int handle)
{
    assert(geom);
    if( handle < 0 || handle >= PLUGIN_GEOMETRY_MODELS_MAX )
        return NULL;
    if( !geom->models[handle].in_use )
        return NULL;
    return &geom->models[handle];
}

struct PluginAssetModel*
PluginGeometry_ModelSlot(
    struct PluginGeometry* geom,
    int handle)
{
    assert(geom);
    assert(handle >= 0);
    assert(handle < PLUGIN_GEOMETRY_MODELS_MAX);
    return &geom->models[handle];
}

void
PluginGeometry_ModelPublish(
    struct PluginGeometry* geom,
    int handle,
    struct ToriRS_Model* model)
{
    struct PluginAssetModel* shipped = PluginGeometry_ModelSlot(geom, handle);

    shipped->in_use = 1;
    shipped->model = model;
    shipped->revision++;
    geom->dirty = 1;
}

void
PluginGeometry_ModelRelease(
    struct PluginGeometry* geom,
    int handle)
{
    struct PluginAssetModel* shipped = PluginGeometry_ModelSlot(geom, handle);

    assert(shipped->in_use);
    memset(shipped, 0, sizeof(*shipped));
    geom->dirty = 1;
}

void
PluginGeometry_MarkDirty(struct PluginGeometry* geom)
{
    assert(geom);
    geom->dirty = 1;
}

bool
PluginGeometry_TakeDirty(struct PluginGeometry* geom)
{
    assert(geom);
    if( !geom->dirty )
        return false;
    geom->dirty = 0;
    return true;
}
