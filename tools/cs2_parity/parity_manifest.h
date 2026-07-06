#ifndef PARITY_MANIFEST_H
#define PARITY_MANIFEST_H

#include <stdbool.h>

#define PARITY_ID_MAX 64
#define PARITY_ARGS_MAX 16
#define PARITY_STRING_ARG_MAX 8
#define PARITY_STRING_LEN 128
#define PARITY_MOUNT_MAX 8
#define PARITY_FIXTURE_SLOTS_MAX 32

struct ParityCs2Case
{
    char id[PARITY_ID_MAX];
    int script_id;
    bool synthetic;
    int int_argc;
    int int_argv[PARITY_ARGS_MAX];
    int string_argc;
    char string_argv[PARITY_STRING_ARG_MAX][PARITY_STRING_LEN];
    int active_component;
    int iface;
    int component_file;
};

struct ParitySpriteCase
{
    char id[PARITY_ID_MAX];
    int sprite_id;
};

struct ParityFixtureSlot
{
    int file_index;
    int obj_id;
};

struct ParityTreeCase
{
    char id[PARITY_ID_MAX];
    int iface;
    bool panel;
    int root_w;
    int root_h;
    int fixture_slot_count;
    struct ParityFixtureSlot fixture_slots[PARITY_FIXTURE_SLOTS_MAX];
    int mount_count;
    struct
    {
        int child_file;
        int iface;
    } mounts[PARITY_MOUNT_MAX];
};

int
parity_manifest_load(const char* path);

struct ParityCs2Case const*
parity_manifest_find_cs2(const char* id);

struct ParitySpriteCase const*
parity_manifest_find_sprite(const char* id);

struct ParityTreeCase const*
parity_manifest_find_tree(const char* id);

int
parity_manifest_cs2_count(void);

struct ParityCs2Case const*
parity_manifest_cs2_at(int index);

int
parity_manifest_sprite_count(void);

struct ParitySpriteCase const*
parity_manifest_sprite_at(int index);

int
parity_manifest_tree_count(void);

struct ParityTreeCase const*
parity_manifest_tree_at(int index);

#endif
