#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/core/toriauxlibcore_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fprintf(stderr, "\n");                                                                     \
        return 1;                                                                                  \
    } while( 0 )

static struct ToriAuxLibCore_Model*
make_test_model(void)
{
    struct ToriAuxLibCore_Model* model = calloc(1, sizeof(*model));
    if( !model )
        return NULL;

    model->vertex_count = 3;
    model->face_count = 1;
    model->vertices_x = calloc(3, sizeof(gc_vertexint_t));
    model->vertices_y = calloc(3, sizeof(gc_vertexint_t));
    model->vertices_z = calloc(3, sizeof(gc_vertexint_t));
    model->face_indices_a = calloc(1, sizeof(gc_faceint_t));
    model->face_indices_b = calloc(1, sizeof(gc_faceint_t));
    model->face_indices_c = calloc(1, sizeof(gc_faceint_t));
    if( !model->vertices_x || !model->vertices_y || !model->vertices_z || !model->face_indices_a ||
        !model->face_indices_b || !model->face_indices_c )
    {
        ToriAuxLibCore_ModelFree(model);
        return NULL;
    }

    return model;
}

static int
test_memory_counters(void)
{
    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    if( !core )
        FAIL("ToriAuxLibCore_New failed");

    const size_t baseline = ToriAuxLibCore_BytesTotal(core);
    struct ToriAuxLibCore_MemReport report;

    struct ToriAuxLibCore_Model* model_a = make_test_model();
    struct ToriAuxLibCore_Model* model_b = make_test_model();
    if( !model_a || !model_b )
    {
        free(model_a);
        free(model_b);
        ToriAuxLibCore_Free(core);
        FAIL("make_test_model failed");
    }

    const size_t model_a_bytes = ToriAuxLibCore_ModelSizeOf(model_a);

    ToriAuxLibCore_ModelAdd(core, 1001, model_a);
    if( ToriAuxLibCore_BytesTotal(core) != baseline + model_a_bytes )
        FAIL("bytes did not increase after ModelAdd");

    ToriAuxLibCore_MemBytes(core, &report);
    if( report.asset_bytes[TORIAUXLIBCORE_KIND_MODEL] != model_a_bytes )
        FAIL("per-type model bytes mismatch after add");

    ToriAuxLibCore_ModelAdd(core, 1001, model_b);
    const size_t model_b_bytes = ToriAuxLibCore_ModelSizeOf(model_b);
    if( ToriAuxLibCore_BytesTotal(core) != baseline + model_b_bytes )
        FAIL("bytes changed on replace ModelAdd");

    ToriAuxLibCore_ModelsClearAll(core);
    if( ToriAuxLibCore_BytesTotal(core) != baseline )
        FAIL("bytes did not return to baseline after ModelsClearAll");

    ToriAuxLibCore_Free(core);
    return 0;
}

int
main(void)
{
    if( test_memory_counters() != 0 )
        return 1;

    printf("gamecache memory test ok\n");
    return 0;
}
