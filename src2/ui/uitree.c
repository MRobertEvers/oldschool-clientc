#include "uitree.h"

#include <stdlib.h>
#include <string.h>

static inline void
grow_array(
    void** array,
    int* capacity,
    int element_size)
{
    if( *capacity == 0 )
        *capacity = 16;
    int new_capacity = *capacity * 2;
    *array = realloc(*array, new_capacity * element_size);
    if( !*array )
        return;
}

struct UITree*
uitree_new(int capacity)
{
    struct UITree* tree = malloc(sizeof(struct UITree));
    if( !tree )
        return NULL;
    memset(tree, 0, sizeof(struct UITree));
    tree->capacity = capacity;
    tree->nodes = malloc(sizeof(struct UITreeNode) * capacity);
    if( !tree->nodes )
        return NULL;
    memset(tree->nodes, 0, sizeof(struct UITreeNode) * capacity);
    return tree;
}

void
uitree_free(struct UITree* tree)
{
    if( !tree )
        return;
    free(tree->nodes);
    free(tree);
}

void
uitree_push_builtin_sprite(
    struct UITree* tree,
    int parent,
    int scene_id,
    int atlas_index)
{
    if( tree->count >= tree->capacity )
    {
        grow_array((void**)&tree->nodes, &tree->capacity, sizeof(struct UITreeNode));
    }

    struct UITreeNode* node = &tree->nodes[tree->count];
    memset(node, 0, sizeof(struct UITreeNode));
    node->type = UITREE_BUILTIN_SPRITE;
    node->parent = parent;
    node->first_child = -1;
    node->next_sibling = -1;
    node->component_id = -1;

    node->u.builtin_sprite.scene_id = scene_id;
    node->u.builtin_sprite.atlas_index = atlas_index;

    tree->count++;
}
