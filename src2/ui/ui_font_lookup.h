#ifndef UI_FONT_LOOKUP_H
#define UI_FONT_LOOKUP_H

#include <stdbool.h>
#include <stdint.h>

#define UI_FONT_LOOKUP_MAX 32

struct UIFontLookupEntry
{
    char name[64];
    int font_id;
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
    int font_id);

/** Returns scene font_id or -1. */
int
ui_font_lookup_find(
    struct UIFontLookup const* lookup,
    char const* name);

#endif
