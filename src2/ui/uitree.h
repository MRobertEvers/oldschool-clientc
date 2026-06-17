#ifndef UITREE_H
#define UITREE_H

enum UITreeNodeType
{
    UITREE_BUILTIN_SPRITE,
    UITREE_BUILTIN_WORLD,
    UITREE_RS_LAYER,
    UITREE_RS_RECT,
    UITREE_RS_GRAPHIC,
};

struct UITreeNode_BuiltinSprite
{
    int scene_id;
    int atlas_index;
};

struct UITreeNode_BuiltinWorld
{
    int level_mask;
};

struct UITreeNode_RSLayer
{
    int component_id;
};

struct UITreeNode_RSRect
{
    int component_id;
};

struct UITreeNode_RSGraphic
{
    int scene_id;
    int atlas_index;
    int scene_id_active;
    int atlas_index_active;
};

struct UITreeNode
{
    enum UITreeNodeType type;
    int parent;
    int first_child;
    int next_sibling;
    int component_id;

    union
    {
        struct UITreeNode_BuiltinSprite builtin_sprite;
        struct UITreeNode_BuiltinWorld builtin_world;
        struct UITreeNode_RSLayer rs_layer;
        struct UITreeNode_RSRect rs_rect;
        struct UITreeNode_RSGraphic rs_graphic;
    } u;
};

struct UITree
{
    struct UITreeNode* nodes;
    int count;
    int capacity;
};

struct UITree*
uitree_new(int capacity);

void
uitree_free(struct UITree* tree);

void
uitree_push_builtin_sprite(
    struct UITree* tree,
    int parent,
    int scene_id,
    int atlas_index);

void
uitree_push_builtin_world(
    struct UITree* tree,
    int parent,
    int level_mask);

void
uitree_push_rs_layer(
    struct UITree* tree,
    int parent,
    int component_id,
    int x,
    int y,
    int width,
    int height);

void
uitree_push_rs_rect(
    struct UITree* tree,
    int parent,
    int component_id,
    int color,
    int filled,
    int x,
    int y,
    int width,
    int height);

void
uitree_push_rs_graphic(
    struct UITree* tree,
    int parent,
    int component_id,
    int scene_id,
    int atlas_index,
    int scene_id_active,
    int atlas_index_active,
    int x,
    int y,
    int width,
    int height);

#endif