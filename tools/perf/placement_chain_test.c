/* Synthetic fixtures are correctness-only. PMU samples come from captures. */
#include "3rd/trspk/trspk_unity.c"
#include "platform/platform_renderer_gles2_placement.h"

#include <stdio.h>
static int checks;
#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if( !(c) )                                                                                 \
        {                                                                                          \
            fprintf(stderr, "line %d: %s\n", __LINE__, #c);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while( 0 )
static void
same(
    struct ToriRS_GLES2* r,
    int id,
    int track,
    int pose)
{
    struct GLES2StaticPrimary a = { 0 }, b = { 0 };
    int expected = gles2_static_resolve_reference(r, id, track, pose, &a);
    int actual = gles2_static_resolve(r, id, track, pose, &b);
    CHECK(actual == expected);
    CHECK(expected != 1 || !memcmp(&a, &b, sizeof(a)));
}
int
main(void)
{
    struct ToriRS_GLES2* r = calloc(1, sizeof(*r));
    CHECK(r);
    r->static_primary_enabled = true;
    r->static_batch_count = 1;
    r->static_batches = calloc(1, sizeof(*r->static_batches));
    CHECK(r->static_batches);
    struct GLES2StaticBatch* b = r->static_batches;
    b->active = true;
    b->cpu = trspk_batch16_create(TRSPK_VERTEX_FORMAT_GLES2);
    CHECK(b->cpu);
    b->cpu->entries = calloc(3, sizeof(*b->cpu->entries));
    CHECK(b->cpu->entries);
    b->cpu->entry_count = b->cpu->entry_capacity = 3;
    b->page_id_capacity = 1;
    b->page_ids = calloc(1, 4);
    CHECK(b->page_ids);
    r->static_page_count = 1;
    r->static_pages = calloc(1, sizeof(*r->static_pages));
    CHECK(r->static_pages);
    r->static_pages[0].valid = true;
    r->static_pages[0].gpu_offset = 100;
    int id = ElementId_Raw(ElementId_Make(TORIDRAW_ELEMENT_KIND_SCENERY, 17));
    int other = ElementId_Raw(ElementId_Make(TORIDRAW_ELEMENT_KIND_NPC, 17));
    b->cpu->entries[0] = (struct TRSPK_Batch16Entry){ id, 0, 0, 0, 6, 30 };
    b->cpu->entries[1] = (struct TRSPK_Batch16Entry){ id, 0, 1, 0, 36, 60 };
    b->cpu->entries[2] = (struct TRSPK_Batch16Entry){ id, 1, 0, 0, 96, 90 };
    for( unsigned i = 0; i < 3; i++ )
        trspk_pose_table_set(
            &r->batch_poses,
            id,
            b->cpu->entries[i].anim_index,
            b->cpu->entries[i].pose_id,
            GLES2_BATCH_POSE_FLAG | i);
    gles2_static_primary_rebuild(r);
    same(r, id, 0, 0);
    same(r, id, 0, 1);
    same(r, id, 1, 0);
    same(r, other, 0, 0);
    same(r, -1, 0, 0);
    same(r, id, -1, 0);
    same(r, id, 0, -1);
    same(r, id, 0, 99);
    CHECK(r->static_primary[17].element_tag == 0);  /* animated poses use reference */
    CHECK(!gles2_primary_prefetch_complete(r, id)); /* animation + secondary track */
    trspk_pose_table_remove_track(&r->batch_poses, id, 1);
    r->batch_poses.elements[17].tracks[0].pose_count = 1;
    gles2_static_primary_rebuild(r);
    CHECK(gles2_primary_prefetch_complete(r, id));
    CHECK(gles2_primary_prefetch_complete(r, other)); /* hint uses index; resolver checks tag */
    CHECK(!gles2_primary_prefetch_complete(r, -1));
    r->has_3d = true;
    gles2_static_prefetch_ids(r, id, other, -1);
    same(r, id, 0, 0);
    /* A page moved during compaction, then the rebuild refreshes its offset. */
    r->static_pages[0].gpu_offset = 999;
    gles2_static_primary_rebuild(r);
    same(r, id, 0, 0);
    CHECK(r->static_primary[17].page_base == 999);
    /* Unload and failed-page states must not leave a positive cached hit. */
    b->active = false;
    gles2_static_primary_rebuild(r);
    same(r, id, 0, 0);
    b->active = true;
    r->static_pages[0].valid = false;
    gles2_static_primary_rebuild(r);
    same(r, id, 0, 0);
    r->static_pages[0].valid = true;
    gles2_static_primary_rebuild(r);
    trspk_pose_table_clear(&r->batch_poses);
    gles2_static_primary_rebuild(r);
    same(r, id, 0, 0);
    /* Index reused under another kind: full raw tag and current mapping. */
    b->cpu->entries[0].element_id = other;
    b->cpu->entries[0].vertex_base = 777;
    trspk_pose_table_set(&r->batch_poses, other, 0, 0, GLES2_BATCH_POSE_FLAG);
    gles2_static_primary_rebuild(r);
    same(r, other, 0, 0);
    same(r, id, 0, 0);
    CHECK(r->static_primary[17].vertex_base == 777);
    uint32_t old_count = r->batch_poses.element_count;
    r->batch_poses.element_count = 1;
    gles2_static_primary_rebuild(r);
    same(r, other, 0, 0);
    r->batch_poses.element_count = old_count;
    gles2_static_primary_rebuild(r);
    same(r, other, 0, 0);
    /* A malformed encoded mapping is rejected, never cached as a hit. */
    trspk_pose_table_set(&r->batch_poses, id, 0, 0, GLES2_BATCH_POSE_FLAG | 99);
    gles2_static_primary_rebuild(r);
    same(r, id, 0, 0);
    trspk_pose_table_set(&r->batch_poses, id, 0, 0, 42);
    gles2_static_primary_rebuild(r);
    same(r, id, 0, 0);
    printf("PASS: %d placement checks, fallback, invalidation, compaction, index reuse\n", checks);
    trspk_pose_table_free(&r->batch_poses);
    trspk_batch16_destroy(b->cpu);
    free(b->page_ids);
    free(r->static_batches);
    free(r->static_pages);
    free(r->static_primary);
    free(r->static_primary_bits);
    free(r);
    return 0;
}
