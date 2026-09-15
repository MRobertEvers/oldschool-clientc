#ifndef SRC_UI_UITREE_IF_EVENTS_H
#define SRC_UI_UITREE_IF_EVENTS_H

/**
 * The server's IF_SETEVENTS store: which slots of which component accept
 * input.
 *
 * At rev 230 nothing is clickable by default. The server declares each armed
 * range, and it does so BEFORE the interface finishes mounting -- so the
 * declarations have to survive until there is a tree to apply them to. Without
 * that, a dialogue renders perfectly and swallows every click.
 *
 * A declaration addresses a RANGE of sub-ids, and that is not decoration: it
 * is how the server arms a list whose entries do not exist yet. The emotes tab
 * is the clearest case -- interface 216's onload creates one cell per emote, so
 * at login there is nothing to address but the container, and the server says
 * "slots 0..55 of emote:contents have op 1".
 *
 * Two lookups, and the difference between them is the whole subtlety here.
 * `At` answers the events value, with 0 for no entry. `Lookup` answers
 * PRESENCE, because an override whose value is zero is meaningful: it disables
 * the cache-authored ops, and confusing that with an absent entry re-enables
 * everything the server just switched off.
 */

#include "ui/uitree.h"

/** One armed interval: `events` applies to sub-ids `from`..`to` of `com_id`. */
struct UIIfEventRange
{
    int com_id;
    int from;
    int to;
    int events;
};

struct UIIfEventTable
{
    struct UIIfEventRange* ranges;
    int count;
    int cap;
};

/** Release the table's storage. Safe on a zeroed table. */
void
UIIfEventTable_Free(struct UIIfEventTable* table);

/**
 * Arm `events` on sub-ids `from`..`to` of `com_id`.
 *
 * Replaces ONLY the addressed interval, preserving the pieces of any
 * overlapping entry on either side of it -- one component may carry many
 * independently armed dynamic-child ranges, and IfSetEventsV2 addresses them
 * one interval at a time. A reversed interval is normalised rather than
 * rejected.
 */
void
UIIfEventTable_Set(
    struct UIIfEventTable* table,
    int com_id,
    int from,
    int to,
    int events);

/**
 * Drop everything armed.
 *
 * Called on IF_OPENTOP, because that is when the real client drops it: the
 * root change rebuilds the widget state and the events map is part of it. The
 * storage is kept, not freed -- the next root re-arms into the same
 * allocation.
 */
void
UIIfEventTable_Clear(struct UIIfEventTable* table);

/** The events armed for (`com_id`, `sub_id`), or 0 when nothing is. */
int
UIIfEventTable_At(
    struct UIIfEventTable const* table,
    int com_id,
    int sub_id);

/**
 * Whether an entry EXISTS for (`com_id`, `sub_id`), writing its value to
 * `out_events` when one does.
 *
 * Distinct from `At` because a declared zero is not the same as no
 * declaration: the reference consults this table first and only falls back to
 * the widget's own decoded flags when there is no entry at all.
 */
int
UIIfEventTable_Lookup(
    struct UIIfEventTable const* table,
    int com_id,
    int sub_id,
    unsigned* out_events);

/**
 * The identity the SERVER knows a component by: (container id, index within
 * it) for a dynamic child, and (its own id, -1) for anything else.
 *
 * These are the two fields RSProt's If3Button carries as `combinedId` and
 * `sub`, and the whole reason `sub` exists. A dynamic child's own component id
 * is a runtime allocation the server has never heard of, so putting it on the
 * wire means the server's handler can never match.
 *
 * A NULL or unknown tree resolves a component to itself, which is what an
 * unarmed click already sends.
 */
void
UIIfEventTable_ButtonTarget(
    struct UITree const* tree,
    int com_id,
    int* out_com_id,
    int* out_sub_id);

/**
 * The events governing a node, including the ones armed on its container.
 *
 * Follows rev239 class545.method12093: consult the server's per-widget and
 * per-child override table first, and fall back to the widget's decoded click
 * mask only when no entry exists.
 *
 * The container step is what makes a dynamic child work. An exact-id match
 * finds nothing for one, so every emote click used to be dropped by the arming
 * gate while the right-click menu still offered "Perform Bow" -- the row comes
 * from the cache's own op list, so the tab hovered, highlighted and named the
 * verb, and clicking did nothing at all. The sub-id must fall inside the
 * declared range: a container armed for slots 0..27 must not arm slot 30.
 */
unsigned
UIIfEventTable_Effective(
    struct UIIfEventTable const* table,
    struct UITree const* tree,
    int com_id);

#endif /* SRC_UI_UITREE_IF_EVENTS_H */
