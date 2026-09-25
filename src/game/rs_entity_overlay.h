#ifndef RS_ENTITY_OVERLAY_H
#define RS_ENTITY_OVERLAY_H

/*
 * Scripted entity overlays -- the `_7200..7214` opcode family, as state.
 *
 * A cache script can hang an ORDINARY INTERFACE COMPONENT off something in the
 * world: an npc, the local player, a loc, or a bare tile. The op hands back an
 * index, and from there the script decorates the thing with the same `cc_*`
 * ops it would use on a panel:
 *
 *     [clientscript,script6498]                       // a fishing spot
 *     $int5 = _7206(5);                               //   already have one?
 *     if ($int5 = -1) {
 *         $int5 = _7201(5, 1, 60, 60, 1);             //   no: 60x60 over the loc
 *     } else {
 *         _104($int5);                                //   yes: empty it
 *     }
 *     _103($int5, 5, 0);                              //   a graphic at sub 0
 *     cc_setsize(36, 32, 0, 0);
 *     cc_setposition(4, 4, 1, 1);
 *     cc_setobject_nonum($obj4, 1);
 *
 * That is how the Activities rows that are NOT highlights are drawn -- the
 * fishing spot icons, the Agility shortcut markers, the cannon hud, the clue
 * scroll helper. See NXT_CLIENT_PLUGINS.md.
 *
 * ---- what the reference client does, exactly ----
 *
 * `jag::oldscape::EntityOverlays` in the decompile
 * (~/Documents/git_repos/osclient_decompile, see the memory note
 * `osrs-cpp-client-decompile`). An overlay is an array of `IfType` whose
 * element 0 is the LAYER; `GetLayer(index)` hands that layer back, and every
 * op that decorates one takes the overlay INDEX rather than a component id.
 *
 * The layer's field `+0x70` is `IfType::OverlayTypes`, and the scene pass draws
 * the three values in three separate sweeps against a three-point anchor
 * (`Client::GetAllOverlayPositions`) -- so the value is both WHERE the overlay
 * sits and WHEN it draws relative to the health bar:
 *
 *   band 0  centred on the entity's mid-height point   drawn under the bar
 *   band 1  stacked UPWARD above the head              drawn over the bar
 *   band 2  stacked DOWNWARD below the feet            drawn under the bar
 *
 * Only bands 1 and 2 advance their cursor, which is what lets two overlays in
 * the same band stack instead of overprinting.
 *
 * Nothing here includes an engine header, so it links into a test on its own:
 * the UITree layer this names by `component_id` is the host's business, and
 * the projection that turns an anchor into pixels is the App's.
 */

#include <stdbool.h>

/**
 * Live overlays.
 *
 * The reference's own store is unbounded. This is sized against what the cache
 * actually asks for, which is not "a few": the GLOBAL npc-add trigger
 * (clientscript 6693) gives EVERY npc on screen two overlays -- a name plate
 * and a hit-marker anchor -- so a busy scene at the reference's 250-npc
 * ceiling wants 500 before any loc or clue overlay is counted.
 *
 * Past the ceiling a create is REFUSED and answers -1, which the scripts
 * already handle (`if ($int5 = -1)`), rather than evicting: an overlay
 * silently thrown away to make room would leave its script believing it still
 * owns one, and the script only ever rebuilds on a var change.
 */
#define RS_OVERLAY_MAX 640

/** What an overlay hangs off. */
enum RS_OverlayAnchor
{
    /** An npc, by the uid `_6751` reports (this client's is the server slot). */
    RS_OVERLAY_ANCHOR_NPC = 0,
    /** A player, by list index. */
    RS_OVERLAY_ANCHOR_PLAYER,
    /** A tile, plus a `StaticEntityOverlayType` saying what on the tile. */
    RS_OVERLAY_ANCHOR_STATIC,
    RS_OVERLAY_ANCHOR_COUNT
};

/**
 * `StaticEntityOverlayType`, bounded by the reference's
 * `IsTypeValid(t) { return t < 5; }`.
 *
 * `OverlayTypeFromLocLayer` is the identity, so 0..3 are the four loc layers a
 * tile can hold and 4 is "the tile itself, no loc". A static overlay is keyed
 * on (coord, type), which is why two locs on one tile can each carry their own.
 */
#define RS_OVERLAY_TYPE_MAX 5
#define RS_OVERLAY_TYPE_COORD 4

/** `IfType::OverlayTypes`, the layer's `+0x70`. See the file comment. */
#define RS_OVERLAY_BAND_MIDDLE 0
#define RS_OVERLAY_BAND_ABOVE 1
#define RS_OVERLAY_BAND_BELOW 2

struct RS_Overlay
{
    bool in_use;
    /** The script's own id for this overlay WITHIN its subject. Two scripts
     *  can both own an overlay on one npc by picking different slots. */
    int slot;
    /** enum RS_OverlayAnchor. */
    int anchor;
    /** npc or player uid; -1 for a static anchor. */
    int uid;
    /** Packed absolute coord, `plane << 28 | x << 14 | z`; -1 for an entity. */
    int coord;
    /** `StaticEntityOverlayType` 0..4; -1 for an entity. */
    int static_type;
    /** RS_OVERLAY_BAND_*. */
    int band;
    /** The layer's requested box. Children lay out inside it. */
    int width;
    int height;
    /** The UITree layer node standing in for the reference's element 0 IfType.
     *  -1 until the host makes one, which is what a test without a tree sees. */
    int component_id;
};

struct RS_OverlayState
{
    struct RS_Overlay items[RS_OVERLAY_MAX];
};

/** No overlays. Does not free any component: that is the host's, which knows
 *  about trees. */
void RS_OverlayReset(struct RS_OverlayState* state);

/** True for a `StaticEntityOverlayType` the reference would accept. */
bool RS_OverlayTypeValid(int static_type);

/**
 * Take a free slot for an entity-anchored overlay, replacing any this subject
 * already had in `slot`.
 *
 * Replacing rather than refusing is the reference's rule
 * (`DestroyOverlay(GetScriptedOverlayIndex(slot))` before `CreateOverlay`), and
 * it is what makes a script that rebuilds its overlay every tick cost one
 * overlay instead of all of them.
 *
 * Returns the index, or -1 when the table is full.
 */
int RS_OverlayCreateEntity(
    struct RS_OverlayState* state,
    int anchor,
    int uid,
    int slot,
    int band,
    int width,
    int height);

/** The static-anchor form. Keyed on (coord, static_type, slot). */
int RS_OverlayCreateStatic(
    struct RS_OverlayState* state,
    int coord,
    int static_type,
    int slot,
    int band,
    int width,
    int height);

/** The index this subject's `slot` overlay is at, or -1. */
int RS_OverlayFindEntity(
    struct RS_OverlayState const* state,
    int anchor,
    int uid,
    int slot);
int RS_OverlayFindStatic(
    struct RS_OverlayState const* state,
    int coord,
    int static_type,
    int slot);

/** Free one index. Freeing a free index is a no-op, not an error: the reference
 *  destroys by "the index for this slot", which is -1 when there is none, and
 *  every caller would otherwise repeat the same test. */
void RS_OverlayDestroy(struct RS_OverlayState* state, int index);

/** Free every overlay whose anchor is the given entity. */
void RS_OverlayDestroyEntity(struct RS_OverlayState* state, int anchor, int uid);

/** The record at `index`, or NULL when the index is out of range or free. */
struct RS_Overlay const* RS_OverlayGet(struct RS_OverlayState const* state, int index);

/** The mutable form, for the host writing `component_id` back. */
struct RS_Overlay* RS_OverlayGetMut(struct RS_OverlayState* state, int index);

/** Live overlays, for a caller that wants to walk them. */
int RS_OverlayCount(struct RS_OverlayState const* state);

#endif /* RS_ENTITY_OVERLAY_H */
