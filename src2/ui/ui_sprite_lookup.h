#ifndef UI_SPRITE_LOOKUP_H
#define UI_SPRITE_LOOKUP_H

#include <stdbool.h>
#include <stdint.h>

#define UI_SPRITE_LOOKUP_MAX 2048

struct UISpriteLookupEntry
{
    char name[64];
    int element_id;
    int atlas_count;
};

struct UISpriteLookup
{
    struct UISpriteLookupEntry entries[UI_SPRITE_LOOKUP_MAX];
    int count;
};

void
ui_sprite_lookup_init(struct UISpriteLookup* lookup);

bool
ui_sprite_lookup_add(
    struct UISpriteLookup* lookup,
    char const* name,
    int element_id,
    int atlas_count);

/** Returns element_id or -1. Optionally writes atlas_count. */
int
ui_sprite_lookup_find(
    struct UISpriteLookup const* lookup,
    char const* name,
    int* out_atlas_count);

/**
 * Parse sprite ref strings: "name", "name,index", "name[index]".
 * Returns element_id or -1; writes atlas index to out_atlas_index when non-NULL.
 */
int
ui_sprite_lookup_resolve_ref(
    struct UISpriteLookup const* lookup,
    char const* sprite_ref,
    int* out_atlas_index);

#endif
