#include <limits.h>

/* Included after the ordinary emitter. This shortcut changes no layout or draw
 * rules: it accounts a narrow mutation window and reuses emit_walk_node for the
 * only subtree allowed to change. All unclassified work uses the full walk. */
static int
emit_overlay_plain_subtree(struct UITree const* tree, int32_t node, unsigned depth, unsigned* budget)
{
    for( ; node >= 0; node = tree->components[node].next_sibling )
    {
        if( depth >= 63 || !*budget || (uint32_t)node >= tree->component_count ) return 0;
        --*budget;
        struct UITreeComponent const* c = &tree->components[node];
        if( c->freed ) return 0;
        switch( c->type )
        {
        case UIELEM_RS_LAYER: case UIELEM_RS_RECT: case UIELEM_RS_TEXT:
        case UIELEM_RS_GRAPHIC: case UIELEM_RS_LINE: case UIELEM_RS_ARC:
            break;
        default: return 0;
        }
        if( !emit_overlay_plain_subtree(tree, c->first_child, depth + 1, budget) ) return 0;
    }
    return 1;
}

static int
emit_overlay_plain_desc(struct UITreeEmitDesc const* d)
{
    return d->kind >= UITREE_EMIT_SPRITE && d->kind <= UITREE_EMIT_ARC &&
        !d->entity_overlays && !d->minimap_dots && !d->worldmap_tiles && !d->debug_prims;
}

static int
emit_overlay_root_valid(struct UITree const* tree)
{
    int32_t root = tree->entity_overlay_index;
    if( root < 0 || (uint32_t)root >= tree->component_count || UITree_FrameActive(tree) ||
        UITree_FrameHasDepth(tree) || UITree_HasActiveDrag(tree) ||
        tree->layout_resolved_root_w != UITREE_LAYOUT_ROOT_W ||
        tree->layout_resolved_root_h != UITREE_LAYOUT_ROOT_H ) return 0;
    struct UITreeComponent const* c = &tree->components[root];
    return c->parent < 0 && c->type == UIELEM_BUILTIN_ENTITY_OVERLAY &&
        !c->freed && !c->behavior.hide && !c->screen_hidden && !c->projection_hidden &&
        !c->frame_hidden && !c->replacement_hidden && !c->replacement_paint_hidden &&
        UITree_RootIsDisplayable(tree, root) && !UITree_ContainerHasMounts(tree, c->component_id);
}

static void
emit_overlay_range_capture(struct UITree const* tree, struct UITreeEmitBuffer* out)
{
    out->overlay_range_valid = 0;
    if( !emit_overlay_retain_enabled() || out->volatile_unrefreshable ||
        !(out->volatile_overlay_seen & (1u << UITREE_EMIT_OVERLAY_ENTITY)) ||
        !emit_overlay_root_valid(tree) ) return;
    unsigned budget = tree->component_count;
    int32_t root = tree->entity_overlay_index;
    if( !emit_overlay_plain_subtree(tree, tree->components[root].first_child, 1, &budget) ) return;
    int world = -1;
    for( int i = 0; i < out->count; ++i )
        if( out->cmds[i].kind == UITREE_EMIT_WORLD ) world = i;
    if( world < 0 ) return;
    /* Hoisting only examines descriptors after WORLD. Require that this root
     * is traversed after the last world, including the all-empty range case. */
    int32_t world_root = out->cmds[world].node_index;
    for( unsigned guard = 0; world_root >= 0 && guard < tree->component_count; ++guard )
    {
        if( (uint32_t)world_root >= tree->component_count ) return;
        if( tree->components[world_root].parent < 0 ) break;
        world_root = tree->components[world_root].parent;
    }
    int seen_world = 0, ordered = 0;
    for( int32_t r = tree->root_index; r >= 0; r = tree->components[r].next_sibling )
    {
        if( r == world_root ) seen_world = 1;
        if( r == root ) { ordered = seen_world && r != world_root; break; }
    }
    if( !ordered ) return;
    int start = world + 1;
    if( start < out->count && out->cmds[start].entity_overlay_source == UITREE_EMIT_OVERLAY_ENTITY ) ++start;
    int count = 0;
    for( int i = 0; i < out->count; ++i )
    {
        struct UITreeEmitDesc const* d = &out->cmds[i];
        if( d->node_index == root || !emit_is_in_entity_overlay(tree, d->node_index) ) continue;
        if( i != start + count || !emit_overlay_plain_desc(d) ) return;
        ++count;
    }
    out->overlay_range_start = start;
    out->overlay_range_count = count;
    out->overlay_range_valid = 1;
}

bool
UITree_EmitOverlayMotionBegin(struct UITree* tree, struct UITreeHost const* host,
    struct UITreeEmitBuffer const* buf, int hovered, struct UITreeEmitRetainGate const* gate)
{
    tree->overlay_motion_tracking = 0;
    if( !emit_overlay_retain_enabled() || !buf->overlay_range_valid ||
        !UITree_EmitRetainGateQuiet(tree, host, buf, hovered, gate) ||
        !emit_overlay_root_valid(tree) || tree->layout_dirty_count || tree->layout_dirty_overflow ||
        !tree->layout_resolved_valid || !tree->layout_resolved_root_valid ||
        tree->layout_resolved_root_x || tree->layout_resolved_root_y ||
        tree->layout_resolved_root_w != UITREE_LAYOUT_ROOT_W ||
        tree->layout_resolved_root_h != UITREE_LAYOUT_ROOT_H ) return false;
    tree->overlay_motion_dirty = tree->overlay_motion_layout = 0;
    tree->overlay_motion_tracking = 1;
    return true;
}

bool
UITree_EmitOverlayMotionEnd(struct UITree* tree, struct UITreeEmitRetainGate const* gate,
    struct UITreeEmitRetainGate* moved_gate)
{
    int tracking = tree->overlay_motion_tracking;
    tree->overlay_motion_tracking = 0;
    if( !tracking || !tree->overlay_motion_layout || tree != gate->source_tree ||
        tree->generation != gate->tree_generation || tree->dirty_gen < gate->dirty_gen ||
        tree->dirty_gen - gate->dirty_gen != tree->overlay_motion_dirty ||
        tree->layout_resolve_seq != gate->layout_resolve_seq ||
        tree->layout_resolve_seq == UINT32_MAX || tree->layout_force_full ||
        tree->layout_dirty_overflow || tree->layout_dirty_count != tree->overlay_motion_layout ||
        !emit_overlay_root_valid(tree) ||
        tree->layout_resolved_root_w != UITREE_LAYOUT_ROOT_W ||
        tree->layout_resolved_root_h != UITREE_LAYOUT_ROOT_H ) return false;
    /* Check the actual seeds too: a generic invalidation must never be counted
     * as an overlay move merely because the numbers happened to agree. */
    for( uint32_t i = 0; i < tree->layout_dirty_count; ++i )
    {
        int32_t node = tree->layout_dirty[i];
        if( node < 0 || (uint32_t)node >= tree->component_count ||
            tree->components[node].parent != tree->entity_overlay_index ) return false;
    }
    UITree_EnsureLayout(tree);
    if( tree->layout_stale || tree->layout_force_full ||
        tree->layout_resolve_seq != gate->layout_resolve_seq + 1u ) return false;
    *moved_gate = *gate;
    moved_gate->dirty_gen = tree->dirty_gen;
    moved_gate->layout_resolve_seq = tree->layout_resolve_seq;
    return true;
}

bool
UITree_EmitOverlayMotionRefresh(struct UITree const* tree, struct UITreeHost const* host,
    struct UITreeEmitBuffer* out, int const* hovered, struct UITreeEmitRetainGate const* gate)
{
    if( !out->overlay_range_valid || !emit_overlay_root_valid(tree) || out->volatile_unrefreshable ||
        !UITree_EmitRetainGateQuiet(tree, host, out, *hovered, gate) ) return false;
    if( !UITree_EmitRefreshVolatile(tree, host, out) ||
        !emit_retain_gate_sources_quiet(tree, host, out, *hovered, gate) ||
        !emit_overlay_root_valid(tree) ) return false;

    struct UITreeEmitBuffer scratch = {0};
    scratch.cmds = out->overlay_scratch;
    scratch.cap = out->overlay_scratch_cap;
    for( int slot = 0; slot < UITREE_FRAME_SLOT_COUNT; ++slot )
        scratch.frame_order.position[slot] = scratch.frame_order.sequence[slot] = -1;
    struct UITreeHost const* stamp_host = host;
    struct UITreeHost observed;
    if( host )
    {
        observed = *host;
        observed.observed_input_mask = &scratch.host_input_dependencies;
        host = &observed;
    }
    int32_t root = tree->entity_overlay_index;
    struct UITreeComponent const* c = &tree->components[root];
    struct UITreeEmitClip clip = {0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H};
    int x, y, w, h;
    UITree_LayoutGetBounds(&c->position, &x, &y, &w, &h);
    if( w > 0 && h > 0 )
    {
        struct UITreeEmitClip canvas = clip;
        clip_intersect(&clip, &canvas, x, y, w, h);
    }
    for( int32_t child = c->first_child; child >= 0; child = tree->components[child].next_sibling )
        emit_walk_node(tree, host, &scratch, child, &clip, &clip, 0, 0, *hovered,
            0, 0, 0, 0, 0, NULL, 0);
    out->overlay_scratch = scratch.cmds;
    out->overlay_scratch_cap = scratch.cap;
    /* Use the original host identity, not the observing shallow copy. */
    if( !emit_retain_gate_sources_quiet(tree, stamp_host,
            out, *hovered, gate) ||
        !emit_overlay_root_valid(tree) ||
        (scratch.host_input_dependencies & ~out->host_input_dependencies) ) return false;
    for( int i = 0; i < scratch.count; ++i )
        if( !emit_overlay_plain_desc(&scratch.cmds[i]) ||
            !emit_is_in_entity_overlay(tree, scratch.cmds[i].node_index) ) return false;
    int start = out->overlay_range_start, old = out->overlay_range_count;
    if( start < 0 || old < 0 || start > out->count || old > out->count - start ||
        scratch.count > INT_MAX - (out->count - old) ) return false;
    int needed = out->count - old + scratch.count;
    if( needed > out->cap )
    {
        struct UITreeEmitDesc* grown = realloc(out->cmds, (size_t)needed * sizeof(*grown));
        if( !grown ) return false;
        out->cmds = grown; out->cap = needed;
    }
    if( scratch.count != old )
    {
        memmove(out->cmds + start + scratch.count, out->cmds + start + old,
            (size_t)(out->count - start - old) * sizeof(*out->cmds));
        for( int source = UITREE_EMIT_OVERLAY_ENTITY; source <= UITREE_EMIT_OVERLAY_FRAME; ++source )
            if( out->volatile_overlay_insert_at[source] >= start + old &&
                !(source == UITREE_EMIT_OVERLAY_ENTITY && out->volatile_overlay_insert_at[source] == start) )
                out->volatile_overlay_insert_at[source] += scratch.count - old;
    }
    if( scratch.count ) memcpy(out->cmds + start, scratch.cmds, (size_t)scratch.count * sizeof(*scratch.cmds));
    out->count = needed;
    out->overlay_range_count = scratch.count;
    emit_buffer_advance_publication(out);
    return true;
}
