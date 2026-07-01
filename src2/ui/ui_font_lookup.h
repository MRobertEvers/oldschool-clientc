#ifndef UI_FONT_LOOKUP_H
#define UI_FONT_LOOKUP_H

#include <stdbool.h>
#include <stdint.h>

#define UI_FONT_LOOKUP_MAX 32

struct UIFontLookupEntry
{
    char name[64];
    /** Runtime ToriDraw scene font id. */
    int font_id;
    /** Revconfig cache_font_id slot 0–3, or -1 if unset. */
    int cache_font_id;
    /** Dat2 fonts-table archive id, or -1 if unset. */
    int cache_archive_id;
};

struct UIFontLookup
{
    struct UIFontLookupEntry entries[UI_FONT_LOOKUP_MAX];
    int count;
};

void
ui_font_lookup_init(struct UIFontLookup* lookup);

bool
ui_font_lookup_add(
    struct UIFontLookup* lookup,
    char const* name,
    int font_id,
    int cache_font_id,
    int cache_archive_id);

/** Returns scene font_id or -1. */
int
ui_font_lookup_find(
    struct UIFontLookup const* lookup,
    char const* name);

/** Returns scene font_id for cache_font_id slot 0–3, or -1. */
int
ui_font_lookup_find_by_cache_font_id(
    struct UIFontLookup const* lookup,
    int cache_font_id);

/** Returns scene font_id for dat2 fonts-table archive_id, or -1. */
int
ui_font_lookup_find_by_archive_id(
    struct UIFontLookup const* lookup,
    int cache_archive_id);

/** Maps RS cache font slot 0–3; falls back to p11/p12/b12/q8 names if INI unset. */
int
ui_font_lookup_resolve_cache_font_index(
    struct UIFontLookup const* lookup,
    int cache_font_idx);

#endif
