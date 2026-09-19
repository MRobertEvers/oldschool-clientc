#ifndef UITREE_ANCHOR_REFERENCE_H
#define UITREE_ANCHOR_REFERENCE_H

/*
 * The anchor reorder as it stood at 82f3e3e24, before it was made linear and
 * allocation-free: the thing the differential test compares against and the
 * thing the bench times against.
 *
 * Copied VERBATIM -- allocations, guards, the -2 memo sentinel and every
 * tie-break. It is frozen history and deliberately not brought up to the
 * project's assert conventions: the moment it is tidied it stops being the
 * implementation under comparison. Nothing in the client links it.
 */

#include "uitree.h"
#include "uitree_frame.h"
#include "uitree_host.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct RefAnchorWork
{
    struct UITree const* tree;
    struct UITreeHost const* host;
    unsigned char const* input;
    unsigned char* output;
    size_t stride;
    int count;
    int written;
    int32_t* record_unit;   /* per record: unit node, or -1 */
    int32_t* unit_of;       /* per node memo: -2 unknown, -1 none, else the unit */
    int32_t* target;        /* per node: anchor target, -1 when not anchored */
    unsigned char* relation;/* per node: effective relation */
    unsigned char* is_target;
    unsigned char* handled; /* per node: unit already written or dropped */
    int* first;             /* per node: earliest record of the unit, count when absent */
};

static int32_t
ref_anchor_unit_of(struct RefAnchorWork* w, int32_t node)
{
    struct UITree const* tree = w->tree;
    int32_t walk = node;
    uint32_t guard = 0;
    while( walk >= 0 && guard++ < tree->component_count )
    {
        if( w->unit_of[walk] != -2 ) break;
        if( w->target[walk] >= 0 || w->is_target[walk] ) { w->unit_of[walk] = walk; break; }
        walk = tree->components[walk].parent;
    }
    int32_t unit = walk < 0 ? -1 : w->unit_of[walk];
    /* Memoise the whole path walked. */
    for( int32_t p = node; p >= 0 && p != walk; p = tree->components[p].parent )
        w->unit_of[p] = unit;
    return unit;
}

static int
ref_anchor_node_visible(struct RefAnchorWork const* w, int32_t node)
{
    return UITree_NodeNativeVisible(w->tree, w->host, node, -1);
}

/* The earliest record of a target's whole tree, children included. */
static int
ref_anchor_tree_first(struct RefAnchorWork const* w, int32_t unit, int depth)
{
    int first = w->first[unit];
    if( depth > 64 ) return first;
    for( uint32_t n = 0; n < w->tree->component_count; n++ )
        if( w->target[n] == (int32_t)unit )
        {
            int child = ref_anchor_tree_first(w, (int32_t)n, depth + 1);
            if( child < first ) first = child;
        }
    return first;
}

static void
ref_anchor_write_tree(struct RefAnchorWork* w, int32_t unit, int depth)
{
    struct UITree const* tree = w->tree;
    int32_t children[64];
    int count = 0, replacement = -1;
    if( depth > 64 || w->handled[unit] ) return;
    w->handled[unit] = 1;
    for( uint32_t n = 0; n < tree->component_count && count < 64; n++ )
    {
        if( w->target[n] != unit ) continue;
        int at = count++;
        while( at > 0 && w->first[children[at - 1]] > w->first[n] )
        { children[at] = children[at - 1]; at--; }
        children[at] = (int32_t)n;
    }
    for( int i = 0; i < count; i++ )
    {
        int32_t child = children[i];
        if( w->relation[child] == UITREE_WIDGET_RELATION_BEHIND )
            ref_anchor_write_tree(w, child, depth + 1);
        else if( w->relation[child] == UITREE_WIDGET_RELATION_REPLACE &&
                 ref_anchor_node_visible(w, child) && ref_anchor_node_visible(w, unit) )
            replacement = child;
    }
    if( replacement >= 0 )
        ref_anchor_write_tree(w, replacement, depth + 1);
    else
        for( int i = 0; i < w->count; i++ )
            if( w->record_unit[i] == unit )
                memcpy(w->output + (size_t)w->written++ * w->stride,
                       w->input + (size_t)i * w->stride, w->stride);
    for( int i = 0; i < count; i++ )
    {
        int32_t child = children[i];
        if( w->relation[child] == UITREE_WIDGET_RELATION_OVER )
            ref_anchor_write_tree(w, child, depth + 1);
        else if( w->relation[child] == UITREE_WIDGET_RELATION_REPLACE )
            w->handled[child] = 1; /* not presented: inherits the target's veto */
    }
}

static int
reference_reorder(struct UITree const* tree, struct UITreeHost const* host, void* records,
                  int count, size_t stride, size_t node_offset)
{
    struct RefAnchorWork w = { .tree = tree, .host = host, .input = records, .stride = stride, .count = count };
    uint32_t const n = tree->component_count;
    int anchored = 0;
    if( count <= 0 || UITree_WidgetAnchorCount(tree) <= 0 ) return count;
    w.target = malloc((size_t)n * sizeof(*w.target));
    w.unit_of = malloc((size_t)n * sizeof(*w.unit_of));
    w.first = malloc((size_t)n * sizeof(*w.first));
    w.relation = calloc(n, sizeof(*w.relation));
    w.is_target = calloc(n, sizeof(*w.is_target));
    w.handled = calloc(n, sizeof(*w.handled));
    w.record_unit = malloc((size_t)count * sizeof(*w.record_unit));
    w.output = malloc((size_t)count * stride);
    assert(w.target);
    assert(w.unit_of);
    assert(w.first);
    assert(w.relation);
    assert(w.is_target);
    assert(w.handled);
    assert(w.record_unit);
    assert(w.output);
    for( uint32_t i = 0; i < n; i++ )
    {
        int32_t target;
        w.unit_of[i] = -2;
        w.first[i] = count;
        w.relation[i] = (unsigned char)UITree_WidgetAnchorAt(tree, (int32_t)i, &target);
        w.target[i] = w.relation[i] == UITREE_WIDGET_RELATION_NATIVE ? -1 : target;
        if( w.target[i] >= 0 ) { w.is_target[target] = 1; anchored++; }
    }
    if( !anchored )
        goto done;
    for( int i = 0; i < count; i++ )
    {
        int32_t node_plus_one;
        memcpy(&node_plus_one, w.input + (size_t)i * stride + node_offset, sizeof(node_plus_one));
        w.record_unit[i] = node_plus_one > 0 && (uint32_t)(node_plus_one - 1) < n
                               ? ref_anchor_unit_of(&w, node_plus_one - 1) : -1;
        if( w.record_unit[i] >= 0 && w.first[w.record_unit[i]] == count )
            w.first[w.record_unit[i]] = i;
    }
    for( int i = 0; i <= count; i++ )
    {
        for( uint32_t r = 0; r < n; r++ )
            if( w.is_target[r] && w.target[r] < 0 && !w.handled[r] && ref_anchor_tree_first(&w, (int32_t)r, 0) == i )
                ref_anchor_write_tree(&w, (int32_t)r, 0);
        if( i == count ) break;
        int32_t unit = w.record_unit[i];
        if( unit >= 0 && w.handled[unit] ) continue;
        memcpy(w.output + (size_t)w.written++ * stride, w.input + (size_t)i * stride, stride);
    }
    memcpy(records, w.output, (size_t)w.written * stride);
    count = w.written;
done:
    free(w.target); free(w.unit_of); free(w.first); free(w.relation);
    free(w.is_target); free(w.handled); free(w.record_unit); free(w.output);
    return count;
}


#endif /* UITREE_ANCHOR_REFERENCE_H */
