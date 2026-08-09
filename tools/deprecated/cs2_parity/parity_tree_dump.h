#ifndef PARITY_TREE_DUMP_H
#define PARITY_TREE_DUMP_H

#include <stdio.h>

struct UITree;

int
parity_uitree_type_osrs(int uitype);

int
parity_uitree_dump_json(struct UITree const* tree, int iface_id, FILE* fp);

int
parity_uitree_dump_artifact(
    FILE* fp,
    char const* case_id,
    int iface_id,
    int root_w,
    int root_h,
    struct UITree const* tree);

#endif
