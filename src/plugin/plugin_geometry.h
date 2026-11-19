#ifndef SRC_PLUGIN_PLUGIN_GEOMETRY_H
#define SRC_PLUGIN_PLUGIN_GEOMETRY_H

/*
 * The tables of plugin-owned geometry: the world objects a plugin stands in
 * the scene, the meshes it authors, and the model files it ships. Each is
 * indexed by the handle the plugin holds.
 *
 * This decides which slot a new object or mesh takes (the first free one, -1
 * when the table is full), what a fresh slot starts as, whether a handle names
 * a live slot, what the "built from" stamps of an object are reset to, the
 * identity of an object's recolour list, the revision of the geometry an
 * object stands on, and whether any mesh or shipped model has moved since the
 * last settle.
 *
 * A mesh or a shipped model is the plugin's SHAPE, and each object standing on
 * one builds its own drawable copy from it. A mesh's revision starts at 1, so
 * 0 can mean "stands on no mesh" -- the answer for a destroyed one, which is
 * what takes the objects still standing on it down. A shipped model's
 * revision moves when its geometry arrives or goes.
 *
 * An object record is the plugin's intent: an ABSOLUTE tile and a model, kept
 * so a scene rebuild, which despawns every entity and element, can put the
 * object back. The built_* stamps say what the live element was made from,
 * so a plugin that re-states the same intent every frame rebuilds nothing.
 *
 * Nothing here builds a model, frees a mesh's arrays or a decoded model,
 * touches a scene element or a world entity, loads an asset or prints. The
 * caller frees what a slot points at before releasing it, logs a full table,
 * marks the geometry dirty after editing a mesh in place, and does the
 * scene and world work an object's intent asks for.
 */

#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_mesh.h"
#include "plugin/torirs_plugin_types.h"

#include <stdbool.h>

/** World objects across every plugin at once. Per-plugin budgeting is the
 *  host's (TORIRS_PLUGIN_OBJECT_BUDGET); this is the ceiling on the table
 *  those handles index into. */
#define PLUGIN_GEOMETRY_OBJECTS_MAX 256
/** Authored meshes across every plugin at once: TORIRS_PLUGIN_MAX plugins at
 *  TORIRS_PLUGIN_MESH_BUDGET each, which is what the host will hand out and so
 *  what this has to be able to hold. */
#define PLUGIN_GEOMETRY_MESHES_MAX 128
/** Shipped models. The host bounds this at TORIRS_PLUGIN_MODELS_MAX, which is
 *  a slot table shared across every plugin -- so unlike the mesh table it is
 *  not per plugin and needs no multiplying. */
#define PLUGIN_GEOMETRY_MODELS_MAX TORIRS_PLUGIN_MODELS_MAX

struct ToriRS_Model;

/*
 * One model FILE a plugin ships, decoded and held for as long as it runs.
 *
 * Held as a ToriRS_Model -- the decoded form, not the bytes and not a drawable
 * model -- for the same reason a mesh is held as triangles: the file is the
 * plugin's SHAPE, and every object standing on it builds its own drawable copy
 * with its own recolours and its own lighting baked in.
 *
 * `revision` moves when the geometry arrives or goes, which is what tells the
 * objects already pointed at this slot that there is now something to build.
 */
struct PluginAssetModel
{
    int in_use;
    int revision;
    struct ToriRS_Model* model;
};

/*
 * One plugin-owned model in the world.
 *
 * The plugin names an ABSOLUTE tile and a model id; everything between that
 * and a drawn element lives here. Only the absolute tile survives a scene
 * rebuild, and this record is what puts the entity and its element back.
 */
struct PluginObject
{
    int in_use;
    /* -- what the plugin asked for -- */
    int source; /* enum ToriRS_HostModelSource */
    int model_id;
    int seq_id;
    int loop;
    /** Lighting offsets against the actor profile (ToriDraw_LightModelActor). */
    int ambient;
    int contrast;
    int tile_x; /* ABSOLUTE */
    int tile_z;
    int level;
    int height;
    int yaw;
    int active;
    int recolor_from[TORIRS_PLUGIN_OBJECT_RECOLORS_MAX];
    int recolor_to[TORIRS_PLUGIN_OBJECT_RECOLORS_MAX];
    int recolor_count;
    /* -- what has been built from it -- */
    int element_id;
    int world_index;
    /** The (source, model, recolour) the live element was built from, so a
     *  set_model or a recolour that changes nothing does not churn the model
     *  cache every frame a plugin re-states its intent. */
    int built_source;
    int built_model_id;
    int built_recolor_stamp;
    /** The revision of the geometry the live element was built from -- a mesh's
     *  or a shipped model's -- so re-authoring it, or its arriving late,
     *  rebuilds the object, and re-stating the same geometry does not. 0 for
     *  the cache-sourced kinds, which have nothing of the sort. */
    int built_geometry_revision;
    /** A load task is in flight. Without this a plugin polling a not-yet-
     *  resident model on every frame would queue a task per frame. */
    int load_pending;
};

struct PluginGeometry
{
    struct PluginObject objects[PLUGIN_GEOMETRY_OBJECTS_MAX];
    struct ToriRS_PluginMesh meshes[PLUGIN_GEOMETRY_MESHES_MAX];
    struct PluginAssetModel models[PLUGIN_GEOMETRY_MODELS_MAX];
    /** A mesh or a shipped model moved since the last settle, so the objects
     *  standing on one may be holding a model built from geometry that has
     *  changed or that no longer exists. */
    int dirty;
};

/* ---- objects ---- */

/**
 * Take the first free object slot and set it to a fresh intent: a cache
 * model, no model and no sequence yet, looping, no level, nothing built.
 * Returns its handle, or -1 when every slot is in use.
 */
int
PluginGeometry_ObjectClaim(struct PluginGeometry* geom);

/** The live object a handle names; NULL for a handle out of range or free. */
struct PluginObject*
PluginGeometry_Object(
    struct PluginGeometry* geom,
    int handle);

/** Free a live object's slot. Whatever was built from it is the caller's to
 *  take down first. */
void
PluginGeometry_ObjectRelease(
    struct PluginGeometry* geom,
    int handle);

/** Forget what the object's live element was built from: no element, no
 *  world entity, no built source, model, recolour or geometry. The intent and
 *  the load latch are left alone. */
void
PluginObject_ForgetBuilt(struct PluginObject* obj);

/**
 * A cheap identity for the recolour list, so "did the colours change?" does
 * not mean comparing two arrays. Order-sensitive: recolour is applied in
 * order and two lists that differ only in order can produce different models.
 */
int
PluginObject_RecolorStamp(struct PluginObject const* obj);

/**
 * The revision of the geometry an object stands on, or 0 when it stands on
 * none -- a cache-sourced object, or one whose mesh was destroyed or whose
 * shipped model never decoded.
 */
int
PluginGeometry_ObjectRevision(
    struct PluginGeometry* geom,
    struct PluginObject const* obj);

/* ---- meshes ---- */

/** Take the first free mesh slot, empty, at revision 1. Returns its handle,
 *  or -1 when every slot is in use. */
int
PluginGeometry_MeshClaim(struct PluginGeometry* geom);

/** The live mesh a handle names; NULL for a handle out of range or free. */
struct ToriRS_PluginMesh*
PluginGeometry_Mesh(
    struct PluginGeometry* geom,
    int handle);

/* ---- shipped models ---- */

/** The live shipped model a handle names; NULL for a handle out of range or
 *  free. */
struct PluginAssetModel*
PluginGeometry_Model(
    struct PluginGeometry* geom,
    int handle);

/** The slot a handle names, live or not. The handle must be in range. */
struct PluginAssetModel*
PluginGeometry_ModelSlot(
    struct PluginGeometry* geom,
    int handle);

/**
 * The slot now holds `model` (NULL when it did not convert): it is in use,
 * its revision moves, and the geometry is dirty. Whatever the slot held
 * before is the caller's to free first.
 */
void
PluginGeometry_ModelPublish(
    struct PluginGeometry* geom,
    int handle,
    struct ToriRS_Model* model);

/** Empty a live slot and mark the geometry dirty. The model it held is the
 *  caller's to free first. */
void
PluginGeometry_ModelRelease(
    struct PluginGeometry* geom,
    int handle);

/* ---- the settle ---- */

/** A mesh or shipped model moved. */
void
PluginGeometry_MarkDirty(struct PluginGeometry* geom);

/** Whether anything moved since the last call; clears the flag. */
bool
PluginGeometry_TakeDirty(struct PluginGeometry* geom);

#endif
