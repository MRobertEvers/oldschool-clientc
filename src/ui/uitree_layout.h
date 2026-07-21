#ifndef SRC_UITREE_LAYOUT_H
#define SRC_UITREE_LAYOUT_H

#include "uitree.h"

#include <stdint.h>

/** Host size the root interface is laid out against — the client canvas in the
 *  standalone viewer, the gameframe slot in the real client. Interfaces read it
 *  back through if_getwidth/if_getheight on their own root (which is
 *  widthMode/heightMode 1, i.e. "fill my parent") and size themselves from it,
 *  so a viewer that always hands over the full canvas makes any interface whose
 *  page mixes centred (yMode 1) and absolute (yMode 0) children lay out wrong —
 *  e.g. bank 12's settings page in cache.jan2026, which is authored for the
 *  fixed-mode modal slot. Runtime-settable so the viewer can host an interface
 *  at the size the client would. */
extern int UITree_LayoutRootWidth;
extern int UITree_LayoutRootHeight;

void
UITree_LayoutSetRootSize(int width, int height);

#define UITREE_LAYOUT_ROOT_W (UITree_LayoutRootWidth)
#define UITREE_LAYOUT_ROOT_H (UITree_LayoutRootHeight)
#define UITREE_SIDEBAR_PANEL_W 190
#define UITREE_SIDEBAR_PANEL_H 261

static inline int
UITree_MulShift14(int a, int b)
{
    return (int)(((int64_t)a * (int64_t)b) >> 14);
}

void
UITree_LayoutInvalidate(struct UITree* tree);

/** Re-resolve at root dims if any layout was invalidated since the last
 *  resolve (reference WidgetManager.ensureLayout — CS2 getters must not read
 *  stale geometry mid-script). No-op when layout is current. */
void
UITree_EnsureLayout(struct UITree const* tree);

void
UITree_LayoutResolve(
    struct UITree* tree,
    int root_x,
    int root_y,
    int root_w,
    int root_h);

void
UITree_LayoutGetBounds(
    struct UITreeElemPosition const* position,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

#endif
