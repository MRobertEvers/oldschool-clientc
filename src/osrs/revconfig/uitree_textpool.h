#ifndef UITREE_TEXTPOOL_H
#define UITREE_TEXTPOOL_H

#include <stddef.h>

/** Expanded UI text for TORIRS_GFX_DRAW_FONT; reset each frame, freed with UITree. */
struct UITreeTextPool
{
    char* buf;
    size_t cap;
    size_t used;
};

void
uitree_textpool_reset(struct UITreeTextPool* p);

void
uitree_textpool_free(struct UITreeTextPool* p);

/** Append `len` bytes from `src` plus a trailing NUL. Returns the copy, or NULL on OOM. */
char*
uitree_textpool_push_dup(struct UITreeTextPool* p, char const* src, size_t len);

#endif
