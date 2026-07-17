#include "uitree_builder_inv.h"

#include "uitree_builder.h"
#include "uitree_builder_manifest.h"

#include "inv/inv_manager.h"
#include "ui/uitree.h"

#include <assert.h>
#include <string.h>

void
uitree_builder_inv_seed(
    struct InvManager* invs,
    struct UIBuilderManifest const* manifest)
{
    assert(invs);
    assert(manifest);

    for( int i = 0; i < manifest->inv_count; i++ )
    {
        struct UIBuilderInvSeed const* seed = &manifest->invs[i];
        assert(seed->name[0] != '\0');

        int source_id = InvManager_ResolveSource(invs, seed->name);
        assert(source_id != INV_MANAGER_SOURCE_INVALID);

        int container_id = InvManager_ContainerForSource(invs, source_id);
        assert(container_id >= 0);

        if( seed->item_count <= 0 )
            continue;

        int const* counts = seed->obj_counts;
        int ones_buf[REVCONFIG_INV_MAX_ITEMS];
        if( !counts )
        {
            assert(seed->item_count <= REVCONFIG_INV_MAX_ITEMS);
            for( int s = 0; s < seed->item_count; s++ )
                ones_buf[s] = 1;
            counts = ones_buf;
        }

        int ok = InvManager_ApplyFull(invs, container_id, seed->obj_ids, counts, seed->item_count);
        assert(ok);
    }
}

void
uitree_builder_inv_bind_tree(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest,
    struct InvManager* invs)
{
    assert(tree);
    assert(builder);
    assert(manifest);
    assert(invs);

    /* Re-bind any inv_grid / sidebar that still has an inv name on the matching tree op. */
    for( int oi = 0; oi < manifest->op_count; oi++ )
    {
        struct UIBuilderTreeOp const* op = &manifest->ops[oi];
        if( op->inv_name[0] == '\0' )
            continue;

        int source_id = InvManager_ResolveSource(invs, op->inv_name);
        assert(source_id != INV_MANAGER_SOURCE_INVALID);

        for( uint32_t ni = 0; ni < tree->component_count; ni++ )
        {
            struct UITreeComponent* c = &tree->components[ni];
            if( c->type == UIELEM_BUILTIN_SIDEBAR && c->component_id == op->componentno )
            {
                c->u.sidebar.inv_source_id = source_id;
            }
            else if( c->type == UIELEM_INV_GRID && c->component_id == op->componentno )
            {
                c->u.inv_grid.inv_source_id = source_id;
            }
        }
    }

    (void)builder;
}
