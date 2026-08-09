#include "parity_tree_dump.h"

#include "ui/uitree.h"

#include <stdio.h>
#include <string.h>

int
parity_uitree_type_osrs(int uitype)
{
    switch( uitype )
    {
    case UIELEM_RS_LAYER:
        return 0;
    case UIELEM_INV_GRID:
        return 2;
    case UIELEM_RS_RECT:
        return 3;
    case UIELEM_RS_TEXT:
        return 4;
    case UIELEM_RS_GRAPHIC:
        return 5;
    case UIELEM_RS_MODEL:
    case UIELEM_CC_OBJ:
        return 6;
    case UIELEM_RS_LINE:
        return 9;
    default:
        return 0;
    }
}

static void
json_escape(FILE* fp, char const* text)
{
    fputc('"', fp);
    if( !text )
    {
        fputc('"', fp);
        return;
    }
    for( char const* p = text; *p; p++ )
    {
        if( *p == '"' || *p == '\\' )
            fputc('\\', fp);
        fputc(*p, fp);
    }
    fputc('"', fp);
}

static void
dump_node_fields(
    FILE* fp,
    struct UITree const* tree,
    int32_t parent_index,
    struct StaticUIComponent const* node,
    char const* path,
    bool need_comma)
{
    int type = parity_uitree_type_osrs((int)node->type);
    int sprite_id = -1;
    char const* text = NULL;
    int color = 0;
    int obj = -1;

    if( node->type == UIELEM_RS_GRAPHIC )
    {
        sprite_id = node->u.rs_graphic.scene_id;
        if( sprite_id <= 0 )
            sprite_id = -1;
    }
    else if( node->type == UIELEM_RS_TEXT )
    {
        text = node->u.rs_text.text;
        color = node->u.rs_text.color;
    }
    else if( node->type == UIELEM_RS_RECT )
        color = node->u.rs_rect.color;
    else if( node->type == UIELEM_CC_OBJ )
        obj = node->u.cc_obj.obj_id;
    if( node->item_id > 0 )
        obj = node->item_id;

    int x = node->position.x;
    int y = node->position.y;
    int w = node->position.width;
    int h = node->position.height;
    if( node->position.layout_resolved && tree )
    {
        if( parent_index >= 0 && (uint32_t)parent_index < tree->component_count )
        {
            struct StaticUIComponent const* parent = &tree->components[parent_index];
            if( parent->position.layout_resolved )
            {
                x = node->position.abs_x - parent->position.abs_x;
                y = node->position.abs_y - parent->position.abs_y;
            }
            else
            {
                x = node->position.abs_x;
                y = node->position.abs_y;
            }
        }
        else
        {
            x = node->position.abs_x;
            y = node->position.abs_y;
        }
        w = node->position.abs_w;
        h = node->position.abs_h;
    }

    if( need_comma )
        fprintf(fp, ",\n");
    fprintf(fp, "    {\n");
    fprintf(fp, "      \"path\": \"%s\",\n", path);
    fprintf(fp, "      \"type\": %d,\n", type);
    fprintf(fp, "      \"x\": %d,\n", x);
    fprintf(fp, "      \"y\": %d,\n", y);
    fprintf(fp, "      \"w\": %d,\n", w);
    fprintf(fp, "      \"h\": %d,\n", h);
    fprintf(fp, "      \"spriteId\": %d,\n", sprite_id);
    fprintf(fp, "      \"text\": ");
    if( text )
        json_escape(fp, text);
    else
        fprintf(fp, "null");
    fprintf(fp, ",\n");
    fprintf(fp, "      \"color\": %d,\n", color);
    fprintf(fp, "      \"obj\": %d,\n", obj);
    fprintf(fp, "      \"dynamic\": %s\n", node->dynamic ? "true" : "false");
    fprintf(fp, "    }");
}

static void
walk_children(
    struct UITree const* tree,
    int parent_index,
    char const* parent_path,
    FILE* fp,
    int* node_count)
{
    int child_index = 0;
    for( int32_t child = tree->components[parent_index].first_child; child >= 0;
         child = tree->components[child].next_sibling, child_index++ )
    {
        if( (uint32_t)child >= tree->component_count )
            break;
        struct StaticUIComponent const* node = &tree->components[child];
        char path[128];
        if( node->dynamic )
            snprintf(path, sizeof(path), "%s/d%d", parent_path, child_index);
        else
        {
            int file_id = node->component_id >= 0 ? (node->component_id & 0xFFFF) : child;
            snprintf(path, sizeof(path), "%s/s%d", parent_path, file_id);
        }
        dump_node_fields(fp, tree, parent_index, node, path, *node_count > 0);
        (*node_count)++;
        walk_children(tree, child, path, fp, node_count);
    }
}

int
parity_uitree_dump_json(struct UITree const* tree, int iface_id, FILE* fp)
{
    if( !tree || !fp )
        return -1;

    int node_count = 0;
    fprintf(fp, "{\n");
    fprintf(fp, "  \"iface\": %d,\n", iface_id);
    fprintf(fp, "  \"nodes\": [\n");

    if( tree->root_index >= 0 )
    {
        for( int32_t root = tree->root_index; root >= 0;
             root = tree->components[root].next_sibling )
        {
            struct StaticUIComponent const* root_node = &tree->components[root];
            char path[64];
            snprintf(
                path,
                sizeof(path),
                "s%d",
                root_node->component_id >= 0 ? (root_node->component_id & 0xFFFF) : 0);
            dump_node_fields(fp, tree, -1, root_node, path, node_count > 0);
            node_count++;
            walk_children(tree, root, path, fp, &node_count);
        }
    }

    fprintf(fp, "\n  ]\n}\n");
    return 0;
}

int
parity_uitree_dump_artifact(
    FILE* fp,
    char const* case_id,
    int iface_id,
    int root_w,
    int root_h,
    struct UITree const* tree)
{
    if( !fp || !case_id || !tree )
        return -1;
    fprintf(fp, "{\n");
    fprintf(fp, "  \"caseId\": \"%s\",\n", case_id);
    fprintf(fp, "  \"iface\": %d,\n", iface_id);
    fprintf(fp, "  \"rootW\": %d,\n", root_w);
    fprintf(fp, "  \"rootH\": %d,\n", root_h);
    fprintf(fp, "  \"nodes\": [\n");

    int node_count = 0;
    if( tree->root_index >= 0 )
    {
        for( int32_t root = tree->root_index; root >= 0;
             root = tree->components[root].next_sibling )
        {
            struct StaticUIComponent const* root_node = &tree->components[root];
            char path[64];
            snprintf(
                path,
                sizeof(path),
                "s%d",
                root_node->component_id >= 0 ? (root_node->component_id & 0xFFFF) : 0);
            dump_node_fields(fp, tree, -1, root_node, path, node_count > 0);
            node_count++;
            walk_children(tree, root, path, fp, &node_count);
        }
    }

    fprintf(fp, "\n  ]\n}\n");
    return 0;
}
