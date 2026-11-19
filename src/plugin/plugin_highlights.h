#ifndef SRC_PLUGIN_PLUGIN_HIGHLIGHTS_H
#define SRC_PLUGIN_PLUGIN_HIGHLIGHTS_H

/*
 * The cache's highlight groups as resolved things on screen: the list a
 * plugin walks, and the resolved LOC highlights kept across frames.
 *
 * Both lists hold at most PLUGIN_HIGHLIGHTS_MAX items, and a push past that
 * is refused, so a cache that fills cannot outrun what the live list could
 * hold.
 *
 * The loc list is kept because resolving it is the expensive pass: it walks
 * the whole scenery pool -- ~23k entities -- testing each against the
 * highlight member lists, and in an ordinary session that produces nothing at
 * all. Unlike npcs and players, a loc does not move, so the answer changes
 * only when the highlight state changes (its revision covers the member lists
 * and the styles) or when the set of scenery does. This decides when the kept
 * list must be rebuilt from scratch: it was never built, the highlight
 * revision moved since, or the world stopped tracking which scenery changed.
 * Otherwise the caller keeps it current one changed element at a time, by
 * forgetting that element's entries and re-testing it.
 *
 * Nothing here walks a pool, matches a member or reads a style. The caller
 * resolves each subject, passes the highlight revision and the world's
 * overflow flag, and fills in anything recomputed per frame (an item's
 * overhead height) after the push.
 */

#include "plugin/torirs_plugin_types.h"

#include <stdbool.h>

/**
 * Items either list can hold: the resolved highlight items one frame may
 * carry.
 *
 * A *TYPE group resolves to every matching thing in the scene, so one
 * `highlight_loctype_on` can be a hundred items in a crowded map square --
 * every Agility obstacle on a rooftop course, say. 256 is well past what the
 * cache's own groups produce and still bounded; past it, resolution stops and
 * says so, because a highlight that silently covered the first 256 of
 * something would read as the feature half-working.
 */
#define PLUGIN_HIGHLIGHTS_MAX 256

struct PluginHighlights
{
    /* The list a plugin walks, rebuilt at the start of each walk. */
    struct ToriRS_HighlightItem items[PLUGIN_HIGHLIGHTS_MAX];
    int count;
    /* Resolved LOC highlights, kept across frames, and the highlight revision
     * they were last built from scratch against. */
    struct ToriRS_HighlightItem loc[PLUGIN_HIGHLIGHTS_MAX];
    int loc_count;
    int loc_revision;
    bool loc_valid;
};

/* ---- the walked list ---- */

/** Empty the walked list. */
void
PluginHighlights_Clear(struct PluginHighlights* hl);

/** Append a copy of `proto`; the copy, or NULL when the list is full. */
struct ToriRS_HighlightItem*
PluginHighlights_Push(
    struct PluginHighlights* hl,
    struct ToriRS_HighlightItem const* proto);

int
PluginHighlights_Count(struct PluginHighlights const* hl);

struct ToriRS_HighlightItem const*
PluginHighlights_Item(struct PluginHighlights const* hl, int i);

/* ---- the kept loc list ---- */

/**
 * True when the kept loc list must be rebuilt from scratch: never built,
 * built against another highlight revision, or the world lost track of which
 * scenery changed.
 */
bool
PluginHighlights_LocNeedsFull(
    struct PluginHighlights const* hl,
    int revision,
    bool scenery_changed_overflow);

/** Empty the kept loc list, before a rebuild from scratch. */
void
PluginHighlights_LocClear(struct PluginHighlights* hl);

/** A rebuild from scratch against this revision is complete. */
void
PluginHighlights_LocBuilt(struct PluginHighlights* hl, int revision);

/** Append a copy of `proto` to the kept loc list; NULL when it is full. */
struct ToriRS_HighlightItem*
PluginHighlights_LocPush(
    struct PluginHighlights* hl,
    struct ToriRS_HighlightItem const* proto);

/** Drop every kept entry for one scene element, keeping the rest in order. */
void
PluginHighlights_LocForget(struct PluginHighlights* hl, int element_id);

int
PluginHighlights_LocCount(struct PluginHighlights const* hl);

struct ToriRS_HighlightItem const*
PluginHighlights_LocItem(struct PluginHighlights const* hl, int i);

#endif
