#include "parity_exec.h"
#include "parity_iface.h"
#include "parity_tree_dump.h"

#include "cs2_runner.h"
#include "fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
parity_tree_case(
    struct RSCacheDat2Disk* cache,
    struct ParityTreeCase const* tree_case,
    char const* out_path)
{
    if( !cache || !tree_case || !out_path )
        return -1;

    struct ParityIfaceLoad iface;
    if( parity_iface_load(cache, tree_case->iface, tree_case->root_w, tree_case->root_h, &iface) != 0 )
        return -1;

    int n = iface.comp_count;
    Component* comps = iface.comps;
    int* lay_x = iface.lay_x;
    int* lay_y = iface.lay_y;
    int* lay_w = iface.lay_w;
    int* lay_h = iface.lay_h;

    struct Interface161Fixture fixture;
    interface161_fixture_init(&fixture);
    if( tree_case->fixture_slot_count > 0 )
    {
        fixture.slots = calloc((size_t)tree_case->fixture_slot_count, sizeof(struct Interface161FixtureSlot));
        if( fixture.slots )
        {
            fixture.slot_count = tree_case->fixture_slot_count;
            for( int i = 0; i < tree_case->fixture_slot_count; i++ )
            {
                fixture.slots[i].file_index = tree_case->fixture_slots[i].file_index;
                fixture.slots[i].obj_id = tree_case->fixture_slots[i].obj_id;
            }
        }
    }

    struct Interface161Cs2Context cs2;
    interface161_cs2_context_init(&cs2);
    cs2.cache = cache;
    cs2.root_w = tree_case->root_w;
    cs2.root_h = tree_case->root_h;
    interface161_cs2_run_interface(
        &cs2, comps, n, lay_x, lay_y, lay_w, lay_h, tree_case->iface, &fixture);

    FILE* fp = fopen(out_path, "w");
    if( !fp )
    {
        interface161_cs2_context_free(&cs2);
        interface161_fixture_free(&fixture);
        parity_iface_free(&iface);
        return -1;
    }

    parity_uitree_dump_artifact(
        fp, tree_case->id, tree_case->iface, tree_case->root_w, tree_case->root_h, cs2.tree);
    fclose(fp);

    interface161_cs2_context_free(&cs2);
    interface161_fixture_free(&fixture);
    parity_iface_free(&iface);
    return 0;
}
