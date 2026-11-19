/*
 * The plugin geometry tables (plugin/plugin_geometry.h).
 *
 * A claim takes the first free slot and starts it fresh, a full table answers
 * -1, a handle out of range or free names nothing, a released slot is taken
 * again; forgetting what was built leaves the intent; the recolour stamp is
 * order-sensitive; an object's geometry revision follows its mesh or shipped
 * model and is 0 when that is gone; and the dirty flag is taken once.
 *
 * Run: make -C src test-plugin-geometry
 */

#include "plugin/plugin_geometry.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* One table, emptied by each test: it is too large for the stack. */
static struct PluginGeometry g_geom;

static struct PluginGeometry*
geometry_new(void)
{
    memset(&g_geom, 0, sizeof(g_geom));
    return &g_geom;
}

static void
test_object_claim_is_fresh(void)
{
    struct PluginGeometry* geom = geometry_new();
    struct PluginObject* obj;
    int handle;

    memset(&geom->objects[0], 0x5a, sizeof(geom->objects[0]));
    geom->objects[0].in_use = 0;
    handle = PluginGeometry_ObjectClaim(geom);
    TEST_ASSERT(handle == 0, "the first free slot is taken");
    obj = PluginGeometry_Object(geom, handle);
    TEST_ASSERT(obj == &geom->objects[0], "the handle names the slot");
    TEST_ASSERT(obj->in_use == 1, "claimed: in use");
    TEST_ASSERT(obj->source == TORIRS_HOST_MODEL_CACHE, "claimed: a cache model");
    TEST_ASSERT(obj->model_id == -1, "claimed: no model");
    TEST_ASSERT(obj->seq_id == -1, "claimed: no sequence");
    TEST_ASSERT(obj->loop == 1, "claimed: loops");
    TEST_ASSERT(obj->level == -1, "claimed: no level");
    TEST_ASSERT(obj->element_id == -1, "claimed: no element");
    TEST_ASSERT(obj->world_index == -1, "claimed: no world entity");
    TEST_ASSERT(obj->built_source == -1, "claimed: no built source");
    TEST_ASSERT(obj->built_model_id == -1, "claimed: no built model");
    TEST_ASSERT(obj->recolor_count == 0, "claimed: the previous tenant's recolours are gone");
    TEST_ASSERT(obj->tile_x == 0 && obj->yaw == 0, "claimed: the rest is zero");
    TEST_ASSERT(obj->load_pending == 0, "claimed: no load in flight");

    TEST_ASSERT(PluginGeometry_ObjectClaim(geom) == 1, "the next claim takes the next slot");
}

static void
test_object_table_bounds_and_reuse(void)
{
    struct PluginGeometry* geom = geometry_new();

    for( int i = 0; i < PLUGIN_GEOMETRY_OBJECTS_MAX; i++ )
        TEST_ASSERT(PluginGeometry_ObjectClaim(geom) == i, "claims fill in order");
    TEST_ASSERT(PluginGeometry_ObjectClaim(geom) == -1, "a full table answers -1");

    TEST_ASSERT(PluginGeometry_Object(geom, -1) == NULL, "a negative handle names nothing");
    TEST_ASSERT(
        PluginGeometry_Object(geom, PLUGIN_GEOMETRY_OBJECTS_MAX) == NULL,
        "a handle past the table names nothing");

    PluginGeometry_ObjectRelease(geom, 7);
    TEST_ASSERT(PluginGeometry_Object(geom, 7) == NULL, "a released slot names nothing");
    TEST_ASSERT(PluginGeometry_ObjectClaim(geom) == 7, "a released slot is taken again");
}

static void
test_forget_built_keeps_the_intent(void)
{
    struct PluginGeometry* geom = geometry_new();
    struct PluginObject* obj = PluginGeometry_Object(geom, PluginGeometry_ObjectClaim(geom));

    obj->model_id = 43330;
    obj->tile_x = 3222;
    obj->load_pending = 1;
    obj->element_id = 9;
    obj->world_index = 4;
    obj->built_source = TORIRS_HOST_MODEL_MESH;
    obj->built_model_id = 2;
    obj->built_recolor_stamp = 77;
    obj->built_geometry_revision = 5;

    PluginObject_ForgetBuilt(obj);
    TEST_ASSERT(obj->element_id == -1, "no element");
    TEST_ASSERT(obj->world_index == -1, "no world entity");
    TEST_ASSERT(obj->built_source == -1, "no built source");
    TEST_ASSERT(obj->built_model_id == -1, "no built model");
    TEST_ASSERT(obj->built_recolor_stamp == 0, "no built recolour");
    TEST_ASSERT(obj->built_geometry_revision == 0, "no built geometry");
    TEST_ASSERT(obj->model_id == 43330, "the model asked for stays");
    TEST_ASSERT(obj->tile_x == 3222, "the tile asked for stays");
    TEST_ASSERT(obj->load_pending == 1, "the load latch is left alone");
}

static void
test_recolor_stamp(void)
{
    struct PluginObject a;
    struct PluginObject b;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    TEST_ASSERT(PluginObject_RecolorStamp(&a) == 17 * 31, "no recolours: the seed alone");

    /* Small colours: the stamp is plain int arithmetic, and these keep it
     * inside int's range. */
    a.recolor_count = 2;
    a.recolor_from[0] = 1;
    a.recolor_to[0] = 2;
    a.recolor_from[1] = 3;
    a.recolor_to[1] = 4;
    b = a;
    TEST_ASSERT(
        PluginObject_RecolorStamp(&a) == PluginObject_RecolorStamp(&b),
        "the same list, the same stamp");

    b.recolor_from[0] = 3;
    b.recolor_to[0] = 4;
    b.recolor_from[1] = 1;
    b.recolor_to[1] = 2;
    TEST_ASSERT(
        PluginObject_RecolorStamp(&a) != PluginObject_RecolorStamp(&b),
        "the same pairs in another order are another stamp");

    b = a;
    b.recolor_from[2] = 999;
    TEST_ASSERT(
        PluginObject_RecolorStamp(&a) == PluginObject_RecolorStamp(&b),
        "entries past the count are not part of it");
}

static void
test_geometry_revision(void)
{
    struct PluginGeometry* geom = geometry_new();
    struct PluginObject* obj = PluginGeometry_Object(geom, PluginGeometry_ObjectClaim(geom));
    int mesh;
    int stand_in;

    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 0, "a cache object stands on none");

    mesh = PluginGeometry_MeshClaim(geom);
    TEST_ASSERT(mesh == 0, "the first free mesh slot is taken");
    TEST_ASSERT(PluginGeometry_Mesh(geom, mesh)->revision == 1, "a new mesh starts at 1");
    obj->source = TORIRS_HOST_MODEL_MESH;
    obj->model_id = mesh;
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 1, "a mesh object reads its mesh");
    PluginGeometry_Mesh(geom, mesh)->revision++;
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 2, "and follows its edits");
    /* What ToriRS_PluginMeshRelease leaves: a zeroed slot. */
    memset(&geom->meshes[mesh], 0, sizeof(geom->meshes[mesh]));
    TEST_ASSERT(PluginGeometry_Mesh(geom, mesh) == NULL, "a released mesh names nothing");
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 0, "and its objects stand on none");

    obj->source = TORIRS_HOST_MODEL_ASSET;
    obj->model_id = 3;
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 0, "a model not shipped yet is 0");
    TEST_ASSERT(PluginGeometry_Model(geom, 3) == NULL, "and names nothing");
    PluginGeometry_ModelPublish(geom, 3, (struct ToriRS_Model*)(void*)&stand_in);
    TEST_ASSERT(PluginGeometry_Model(geom, 3) != NULL, "a published model is live");
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 1, "its first arrival is revision 1");
    PluginGeometry_ModelPublish(geom, 3, NULL);
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 2, "a republish moves it");
    TEST_ASSERT(PluginGeometry_Model(geom, 3)->model == NULL, "a model that did not convert is NULL");
    PluginGeometry_ModelRelease(geom, 3);
    TEST_ASSERT(PluginGeometry_Model(geom, 3) == NULL, "a released model names nothing");
    TEST_ASSERT(PluginGeometry_ModelSlot(geom, 3)->revision == 0, "and its slot is zeroed");
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 0, "its objects stand on none");

    obj->model_id = -1;
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 0, "a handle out of range is 0");
    obj->model_id = PLUGIN_GEOMETRY_MODELS_MAX;
    TEST_ASSERT(PluginGeometry_ObjectRevision(geom, obj) == 0, "past the table too");
}

static void
test_mesh_table_full(void)
{
    struct PluginGeometry* geom = geometry_new();

    for( int i = 0; i < PLUGIN_GEOMETRY_MESHES_MAX; i++ )
        TEST_ASSERT(PluginGeometry_MeshClaim(geom) == i, "mesh claims fill in order");
    TEST_ASSERT(PluginGeometry_MeshClaim(geom) == -1, "a full mesh table answers -1");
    TEST_ASSERT(PluginGeometry_Mesh(geom, -1) == NULL, "a negative mesh handle names nothing");
    TEST_ASSERT(
        PluginGeometry_Mesh(geom, PLUGIN_GEOMETRY_MESHES_MAX) == NULL,
        "a mesh handle past the table names nothing");
}

static void
test_dirty_is_taken_once(void)
{
    struct PluginGeometry* geom = geometry_new();
    int stand_in;

    TEST_ASSERT(!PluginGeometry_TakeDirty(geom), "nothing moved yet");
    PluginGeometry_MarkDirty(geom);
    TEST_ASSERT(PluginGeometry_TakeDirty(geom), "a mark is taken");
    TEST_ASSERT(!PluginGeometry_TakeDirty(geom), "once");

    PluginGeometry_ModelPublish(geom, 0, (struct ToriRS_Model*)(void*)&stand_in);
    TEST_ASSERT(PluginGeometry_TakeDirty(geom), "a publish marks it");
    PluginGeometry_ModelRelease(geom, 0);
    TEST_ASSERT(PluginGeometry_TakeDirty(geom), "a release marks it");

    PluginGeometry_ObjectClaim(geom);
    PluginGeometry_MeshClaim(geom);
    TEST_ASSERT(!PluginGeometry_TakeDirty(geom), "claiming an object or a mesh does not");
}

int
main(void)
{
    test_object_claim_is_fresh();
    test_object_table_bounds_and_reuse();
    test_forget_built_keeps_the_intent();
    test_recolor_stamp();
    test_geometry_revision();
    test_mesh_table_full();
    test_dirty_is_taken_once();
    if( g_failures )
    {
        printf("plugin_geometry_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("plugin_geometry_test: ok\n");
    return 0;
}
