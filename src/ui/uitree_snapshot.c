#include "uitree_snapshot.h"

#include "uitree.h"
#include "uitree_emit.h"
#include "uitree_hook.h"
#include "uitree_scroll.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The largest production trees measured while developing this exporter are
 * roughly 7,100 nodes. Leave headroom, but never let a corrupt cache turn a
 * preview request into an unbounded diagnostic allocation or response. */
#define UITREE_SNAPSHOT_MAX_NODES 8192u
#define UITREE_SNAPSHOT_MAX_BYTES (32u * 1024u * 1024u)
#define UITREE_SNAPSHOT_LINE_MAX 4096u

struct SnapshotWriter
{
    FILE* fp;
    size_t bytes;
    int failed;
};

struct SnapshotDraw
{
    int count;
    struct UITreeEmitDesc first;
};

static void
snapshot_write(
    struct SnapshotWriter* writer,
    char const* format,
    ...)
{
    char line[UITREE_SNAPSHOT_LINE_MAX];
    va_list ap;
    int length;

    if( !writer || writer->failed )
        return;
    va_start(ap, format);
    length = vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    if( length < 0 || (size_t)length >= sizeof(line) ||
        writer->bytes + (size_t)length > UITREE_SNAPSHOT_MAX_BYTES )
    {
        writer->failed = 1;
        return;
    }
    if( fwrite(line, 1, (size_t)length, writer->fp) != (size_t)length )
    {
        writer->failed = 1;
        return;
    }
    writer->bytes += (size_t)length;
}

static char const*
snapshot_kind(struct UITreeComponent const* component)
{
    switch( component->type )
    {
    case UIELEM_RS_LAYER:
        return "layer";
    case UIELEM_RS_INV:
        return "inventory";
    case UIELEM_RS_RECT:
        return "rectangle";
    case UIELEM_RS_TEXT:
        return "text";
    case UIELEM_RS_GRAPHIC:
        return "graphic";
    case UIELEM_RS_MODEL:
        return "model";
    case UIELEM_RS_INV_TEXT:
        return "inventory_text";
    case UIELEM_RS_LINE:
        return "line";
    default:
        return "native";
    }
}

static int
snapshot_widget_type(struct UITreeComponent const* component)
{
    switch( component->type )
    {
    case UIELEM_RS_LAYER:
        return 0;
    case UIELEM_RS_INV:
        return 2;
    case UIELEM_RS_RECT:
        return 3;
    case UIELEM_RS_TEXT:
        return 4;
    case UIELEM_RS_GRAPHIC:
        return 5;
    case UIELEM_RS_MODEL:
        return 6;
    case UIELEM_RS_INV_TEXT:
        return 8;
    case UIELEM_RS_LINE:
        return 9;
    default:
        return -(int)component->type;
    }
}

static int
snapshot_depth(
    struct UITree const* tree,
    int32_t node)
{
    uint32_t guard = 0;
    int depth = 0;

    while( node >= 0 && (uint32_t)node < tree->component_count &&
           guard++ < tree->component_count )
    {
        node = tree->components[node].parent;
        if( node >= 0 )
            depth++;
    }
    return depth;
}

static int
snapshot_node_or_ancestor_culled(
    struct UITree const* tree,
    int32_t node)
{
    uint32_t guard = 0;

    while( node >= 0 && (uint32_t)node < tree->component_count &&
           guard++ < tree->component_count )
    {
        struct UITreeComponent const* component = &tree->components[node];
        if( UITree_LayerCullsChildren(
                component, component->position.abs_w, component->position.abs_h) )
            return 1;
        node = component->parent;
    }
    return 0;
}

static void
snapshot_write_hooks(
    struct SnapshotWriter* writer,
    struct UITreeComponent const* component)
{
    struct UITreeRuntimeHooks const* hooks = UITree_Hooks(component);
    int first = 1;

    snapshot_write(writer, "\"hooks\":[");
    for( int i = 0; i < UITree_HooksSlotCount(); i++ )
    {
        struct UITreeRuntimeScriptHook const* hook = UITree_HooksSlotAtConst(hooks, i);
        if( !UITree_HookIsSet(hook) )
            continue;
        snapshot_write(
            writer,
            "%s{\"name\":\"%s\",\"script\":%d,\"argc\":%d,\"string_argc\":%d}",
            first ? "" : ",",
            UITree_HooksSlotName(i),
            hook->script_id,
            hook->argc,
            hook->str_argc);
        first = 0;
    }
    snapshot_write(writer, "]");
}

static void
snapshot_write_node(
    struct SnapshotWriter* writer,
    struct UITree const* tree,
    struct SnapshotDraw const* draws,
    uint32_t node,
    int first)
{
    struct UITreeComponent const* component = &tree->components[node];
    struct UITreeElemPosition const* position = &component->position;
    struct SnapshotDraw const* draw = &draws[node];
    int const uid = component->component_id;
    int const group = uid >= 0 ? (uid >> 16) & 0xffff : -1;
    int const file = uid >= 0 ? uid & 0xffff : -1;
    int const own_hidden = component->behavior.hide ? 1 : 0;
    int const effective_hidden = UITree_NodeOrAncestorDisplayHidden(tree, (int32_t)node) ? 1 : 0;
    int const culled = snapshot_node_or_ancestor_culled(tree, (int32_t)node);
    int const walked = tree->emit_visited && node < tree->emit_visited_cap
                           ? (tree->emit_visited[node] == tree->emit_epoch ? 1 : 0)
                           : 0;
    int const scroll_width = component->type == UIELEM_RS_LAYER
                                 ? component->u.rs_layer.scroll_width
                                 : 0;
    int const scroll_height = component->type == UIELEM_RS_LAYER
                                  ? component->u.rs_layer.scroll_height
                                  : 0;

    snapshot_write(
        writer,
        "%s    {\"node\":%u,\"uid\":%d,\"group\":%d,\"file\":%d,"
        "\"parent\":%d,\"first_child\":%d,\"next_sibling\":%d,\"depth\":%d,"
        "\"dynamic\":%s,\"child_index\":%d,\"kind\":\"%s\","
        "\"type\":%d,\"widget_type\":%d,\"if3\":%s,\"transparency\":%d,"
        "\"client_code\":%d,\"item_id\":%d,\"item_count\":%d,",
        first ? "" : ",\n",
        node,
        uid,
        group,
        file,
        component->parent,
        component->first_child,
        component->next_sibling,
        snapshot_depth(tree, (int32_t)node),
        component->dynamic ? "true" : "false",
        component->dynamic ? component->dynamic_child_index : -1,
        snapshot_kind(component),
        (int)component->type,
        snapshot_widget_type(component),
        component->if3 ? "true" : "false",
        component->trans,
        component->behavior.client_code,
        component->item_id,
        component->item_count);
    snapshot_write(
        writer,
        "\"raw\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
        "\"x_mode\":%d,\"y_mode\":%d,\"width_mode\":%d,\"height_mode\":%d},"
        "\"box\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
        "\"resolved\":%s},"
        "\"scroll\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},",
        position->x,
        position->y,
        position->width,
        position->height,
        (int)position->x_mode,
        (int)position->y_mode,
        (int)position->width_mode,
        (int)position->height_mode,
        position->abs_x,
        position->abs_y,
        position->abs_w,
        position->abs_h,
        position->layout_resolved ? "true" : "false",
        component->scroll_x,
        component->scroll_y,
        scroll_width,
        scroll_height);
    snapshot_write(
        writer,
        "\"visibility\":{\"own_hidden\":%s,\"frame_hidden\":%s,"
        "\"replacement_hidden\":%s,\"effective_hidden\":%s,\"culled\":%s,"
        "\"walked\":%s,\"displayable\":%s},",
        own_hidden ? "true" : "false",
        component->frame_hidden ? "true" : "false",
        component->replacement_hidden ? "true" : "false",
        effective_hidden ? "true" : "false",
        culled ? "true" : "false",
        walked ? "true" : "false",
        !effective_hidden && !culled ? "true" : "false");
    snapshot_write_hooks(writer, component);
    if( draw->count > 0 )
    {
        struct UITreeEmitDesc const* desc = &draw->first;
        snapshot_write(
            writer,
            ",\"draw\":{\"count\":%d,\"kind\":%d,\"x\":%d,\"y\":%d,"
            "\"width\":%d,\"height\":%d,\"clip\":{\"x\":%d,\"y\":%d,"
            "\"width\":%d,\"height\":%d},\"scroll_x\":%d,\"scroll_y\":%d}",
            draw->count,
            (int)desc->kind,
            desc->x,
            desc->y,
            desc->w,
            desc->h,
            desc->clip.x,
            desc->clip.y,
            desc->clip.w,
            desc->clip.h,
            desc->scroll_off_x,
            desc->scroll_off_y);
    }
    else
        snapshot_write(writer, ",\"draw\":null");
    snapshot_write(writer, "}");
}

int
UITreeSnapshot_WriteJson(
    struct UITree const* tree,
    struct UITreeEmitBuffer const* emit,
    char const* path,
    int interface_id,
    int viewport_w,
    int viewport_h)
{
    struct SnapshotWriter writer;
    struct SnapshotDraw* draws;
    uint32_t limit;
    uint32_t live_count = 0;
    uint32_t exported_count = 0;
    int first = 1;
    int failed;

    if( !tree || !path || !path[0] || viewport_w <= 0 || viewport_h <= 0 )
        return -1;
    limit = tree->component_count < UITREE_SNAPSHOT_MAX_NODES
                ? tree->component_count
                : UITREE_SNAPSHOT_MAX_NODES;
    draws = (struct SnapshotDraw*)calloc(limit ? limit : 1, sizeof(*draws));
    if( !draws )
        return -1;
    if( emit )
    {
        for( int i = 0; i < emit->count; i++ )
        {
            struct UITreeEmitDesc const* desc = &emit->cmds[i];
            if( desc->node_index < 0 || (uint32_t)desc->node_index >= limit )
                continue;
            struct SnapshotDraw* draw = &draws[desc->node_index];
            if( draw->count++ == 0 )
                draw->first = *desc;
        }
    }
    for( uint32_t i = 0; i < tree->component_count; i++ )
        if( !tree->components[i].freed )
            live_count++;
    for( uint32_t i = 0; i < limit; i++ )
        if( !tree->components[i].freed )
            exported_count++;

    writer.fp = fopen(path, "wb");
    writer.bytes = 0;
    writer.failed = writer.fp ? 0 : 1;
    if( !writer.fp )
    {
        free(draws);
        return -1;
    }
    snapshot_write(
        &writer,
        "{\n  \"schema\":1,\n  \"interface\":%d,\n"
        "  \"viewport\":{\"width\":%d,\"height\":%d},\n"
        "  \"root\":%d,\n  \"component_count\":%u,\n"
        "  \"live_count\":%u,\n  \"exported_count\":%u,\n"
        "  \"emit_count\":%d,\n  \"truncated\":%s,\n  \"nodes\":[\n",
        interface_id,
        viewport_w,
        viewport_h,
        tree->root_index,
        tree->component_count,
        live_count,
        exported_count,
        emit ? emit->count : 0,
        exported_count < live_count ? "true" : "false");
    for( uint32_t i = 0; i < limit; i++ )
    {
        if( tree->components[i].freed )
            continue;
        snapshot_write_node(&writer, tree, draws, i, first);
        first = 0;
    }
    snapshot_write(&writer, "\n  ]\n}\n");
    failed = writer.failed || ferror(writer.fp);
    if( fclose(writer.fp) != 0 )
        failed = 1;
    free(draws);
    if( failed )
    {
        remove(path);
        return -1;
    }
    return 0;
}
