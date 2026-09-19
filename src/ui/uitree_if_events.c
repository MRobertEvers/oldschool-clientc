#include "ui/uitree_if_events.h"

#include <assert.h>
#include <stdlib.h>

void
UIIfEventTable_Free(struct UIIfEventTable* table)
{
    assert(table);
    free(table->ranges);
    table->ranges = NULL;
    table->count = 0;
    table->cap = 0;
}

void
UIIfEventTable_Set(
    struct UIIfEventTable* table,
    int com_id,
    int from,
    int to,
    int events)
{
    struct UIIfEventRange* replacement;
    int replacement_count = 0;
    int replacement_cap;

    assert(table);
    if( from > to )
    {
        int swap = from;
        from = to;
        to = swap;
    }

    /* IfSetEventsV2 replaces only the addressed interval. Preserve the pieces
     * on either side of an overlap; one component may have many independently
     * armed dynamic-child ranges. */
    replacement_cap = table->count + 3;
    replacement = malloc((size_t)replacement_cap * sizeof(*replacement));
    assert(replacement);
    for( int i = 0; i < table->count; i++ )
    {
        struct UIIfEventRange old = table->ranges[i];
        int old_from = old.from;
        int old_to = old.to;

        if( old.com_id != com_id || old.to < from || old.from > to )
        {
            replacement[replacement_count++] = old;
            continue;
        }
        if( old_from < from )
        {
            old.from = old_from;
            old.to = from - 1;
            replacement[replacement_count++] = old;
        }
        if( old_to > to )
        {
            old.to = old_to;
            old.from = to + 1;
            replacement[replacement_count++] = old;
        }
    }
    replacement[replacement_count].com_id = com_id;
    replacement[replacement_count].from = from;
    replacement[replacement_count].to = to;
    replacement[replacement_count].events = events;
    replacement_count++;
    free(table->ranges);
    table->ranges = replacement;
    table->count = replacement_count;
    table->cap = replacement_cap;
}

void
UIIfEventTable_Clear(struct UIIfEventTable* table)
{
    assert(table);
    table->count = 0;
}

int
UIIfEventTable_At(
    struct UIIfEventTable const* table,
    int com_id,
    int sub_id)
{
    assert(table);
    for( int i = 0; i < table->count; i++ )
    {
        if( table->ranges[i].com_id == com_id && table->ranges[i].from <= sub_id &&
            table->ranges[i].to >= sub_id )
            return table->ranges[i].events;
    }
    return 0;
}

int
UIIfEventTable_Lookup(
    struct UIIfEventTable const* table,
    int com_id,
    int sub_id,
    unsigned* out_events)
{
    assert(table);
    for( int i = 0; i < table->count; i++ )
    {
        if( table->ranges[i].com_id == com_id && table->ranges[i].from <= sub_id &&
            table->ranges[i].to >= sub_id )
        {
            if( out_events )
                *out_events = (unsigned)table->ranges[i].events;
            return 1;
        }
    }
    return 0;
}

void
UIIfEventTable_ButtonTarget(
    struct UITree const* tree,
    int com_id,
    int* out_com_id,
    int* out_sub_id)
{
    int32_t idx;
    struct UITreeComponent const* node;
    int32_t parent;

    assert(out_com_id);
    assert(out_sub_id);
    *out_com_id = com_id;
    *out_sub_id = -1;
    if( !tree )
        return;

    idx = UITree_FindByComponentId(tree, com_id);
    if( idx < 0 )
        return;
    node = &tree->components[idx];
    if( !node->dynamic )
        return;

    parent = node->parent;
    if( parent < 0 || (uint32_t)parent >= tree->component_count )
        return;
    *out_com_id = tree->components[parent].component_id;
    *out_sub_id = node->dynamic_child_index;
}

unsigned
UIIfEventTable_Effective(
    struct UIIfEventTable const* table,
    struct UITree const* tree,
    int com_id)
{
    int target;
    int sub;
    unsigned events;
    int32_t idx;

    assert(table);

    /* An override whose value is zero is meaningful: it disables
     * cache-authored ops and must not be confused with an absent entry, which
     * is why this is the presence lookup and not the value one. */
    if( UIIfEventTable_Lookup(table, com_id, -1, &events) )
        return events;

    UIIfEventTable_ButtonTarget(tree, com_id, &target, &sub);
    if( target != com_id && UIIfEventTable_Lookup(table, target, sub, &events) )
        return events;

    if( !tree )
        return 0;
    idx = UITree_FindByComponentId(tree, com_id);
    if( idx >= 0 )
        return (unsigned)tree->components[idx].behavior.click_mask;
    return 0;
}
