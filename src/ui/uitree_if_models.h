#ifndef SRC_UI_UITREE_IF_MODELS_H
#define SRC_UI_UITREE_IF_MODELS_H

/**
 * The persistent interface-model stores: which model the server told each
 * MODEL component to show, remembered until there is a node to show it on.
 *
 * Three records, kept for the reason the IF_SET* stores beside them are
 * (ui/uitree_if_store.h): the packet that names a component's model usually
 * lands before the interface that holds the component has mounted, and a
 * mount or a bake replaces the nodes underneath, so the binding has to be
 * re-applied whenever the tree's generation moves.
 *
 *  - Heads: IF_SETNPCHEAD, IF_SETPLAYERHEAD, IF_SETOBJECT and IF_SETMODEL.
 *    One entry per component, last write wins. A new entry starts with no
 *    animation; a later head on the same component keeps the one it had.
 *    A removal moves the last entry into the hole, so the order of the
 *    entries is not the order they were set in.
 *  - Player models: the IF_SETPLAYERMODEL_* compositions. Each slot's index
 *    is its scene id (the base the caller names, plus the index), so a slot is
 *    never removed: forgetting one frees it (com_id -1) and the next claim
 *    takes the first free slot, scene id and all, before it grows the table.
 *  - The local player's figure (clientCode 328): the appearance it was last
 *    built from, so an unchanged one is not merged again.
 *
 * Every entry records the generation it was last applied at (0 = not yet)
 * and the group-close serial current when it was set, which is how a later
 * close of its interface is told apart from one that came before it.
 *
 * Nothing here reads a tree, a cache or a scene. The caller decides when an
 * entry's interface has been closed since it was set and forgets it,
 * composites and binds the models, reads the tree's generation and close
 * serial, and redraws when an entry changes.
 */

#include <stdbool.h>
#include <stdint.h>

/** What a head entry shows; stored as an int in UIIfHead.kind. */
enum UIIfHeadKind
{
    UI_IFHEAD_NPC = 0,
    UI_IFHEAD_PLAYER,
    /* IF_SETOBJECT (reference IfType model1Type 4): the obj's lit inventory
     * model bound to a MODEL widget -- e.g. the combat-tab weapon. npc_id
     * carries the obj id; zoom carries the wire zoom. */
    UI_IFHEAD_OBJ,
    /* IF_SETMODEL (reference IfType model1Type 1): npc_id carries the raw
     * cache model id. */
    UI_IFHEAD_MODEL,
};

struct UIIfHead
{
    int com_id;
    int kind;             /* enum UIIfHeadKind: npc, player, obj, or raw model */
    int npc_id;           /* source id: npc, obj, or cache model */
    int zoom;             /* IF_SETOBJECT third field: IF1 zoom, IF3 quantity */
    int anim_id;          /* IF_SETANIM seq for this head, or -1 (reference modelAnim) */
    uint32_t applied_gen; /* tree generation this was last applied at; 0 = pending */
    uint32_t wanted_gen;  /* tree generation its want was last raised at (app_if_head_wants) */
    uint32_t close_serial; /* tree->group_close_serial when set: a later close of
                            * the group unloads it (UITree_GroupCloseSerial) */
    /* What the head's parallel task landed (Task_IfModel, app/
     * app_tasks_if_assets.c): until `landed` its task is still loading, and
     * nothing is attempted for it -- an unresolved head was re-attempted
     * every frame forever. Once landed, `scene_id` is the composite to bind
     * (-1: the assets did not come, nothing to show) and `landed_npc_id` the
     * npc an npc head was resolved to (a multinpc's child, -1 hidden). */
    uint8_t landed;
    int scene_id;
    int landed_npc_id;
    /* The scene id the widget draws: what the bind last applied to it (-1
     * none). A new set of the head leaves it: the widget keeps drawing it
     * until the new head lands and binds. */
    int bound_scene_id;
    /* An IF_SETNPCHEAD_ACTIVE head of an npc that carries a chathead
     * customisation (NPC_INFO HEAD_CUSTOM): the npc's server slot and spawn
     * serial, and the override's serial (WorldNpcOverride.serial), which the
     * head task reads the override by. 0 / -1: the type's own head. */
    uint32_t custom_serial;
    int custom_npc_slot;
    uint32_t custom_npc_spawn_serial;
    /* An IF_SETNPCHEAD_ACTIVE head FOLLOWS its npc: the reference resolves
     * model type 6 against the npc on every draw (class308.method7279), so a
     * HEAD_CUSTOM, its reset or a retype that arrives while the head is shown
     * shows at once. The npc (server slot, spawn serial; slot -1: the head
     * follows nothing) and what the head was set from: the npc's type, its
     * customisation's serial (0: none) and whether that mirrors. */
    int active_npc_slot;
    uint32_t active_npc_spawn_serial;
    int active_npc_id;
    uint32_t active_custom_serial;
    uint8_t active_mirror;
};

/* Revision-239 server-driven player-composition widgets. Each component owns
 * a clone of the local PlayerComposition: slots is the effective kit/equipment
 * layer, identkit is the body underneath worn objects, and colours/gender are
 * independently mutable through the four IF_SETPLAYERMODEL_* packets. A unique
 * scene id prevents one widget's incremental setters from changing any
 * sibling widget. */
struct UIIfPlayerModel
{
    int com_id;
    int scene_id;
    int slots[12];
    int identkit[12];
    int colors[5];
    int gender;
    int anim_id;          /* IF_SETANIM modelAnim, or -1 */
    int built;            /* scene_id holds this composition, built whole */
    uint32_t applied_gen; /* tree generation scene_id was bound into */
    uint32_t close_serial; /* as UIIfHead's; com_id -1 = a free slot */
    /* The composition the packets last committed (UIIfModels.
     * composition_serial when it was committed). A build of an older one
     * is stale and is not put into scene_id. */
    uint32_t composition;
};

/* Live local-player model widgets (clientCode 328 -- the equipment-stats
 * figure). The composite is rebuilt from the local player's PLAYER_INFO
 * appearance whenever any of it moves, so equip a helmet and the figure is
 * wearing it. The last-built appearance is kept here so an unchanged one is
 * not re-merged every frame; the angles and the animation frame are re-derived
 * by the caller unconditionally. */
struct UIIfLocalPlayerModel
{
    int slots[12];
    int colors[5];
    int gender;
    int built; /* 0 until the first successful composite */
};

struct UIIfModels
{
    struct UIIfHead* heads;
    int head_count;
    int head_cap;
    struct UIIfPlayerModel* player_models;
    int player_model_count;
    int player_model_cap;
    /* Counts every composition a player-model packet commits, for every
     * component, so a composition number is never reused. */
    uint32_t composition_serial;
    struct UIIfLocalPlayerModel local_player;
};

/** Release both tables and empty the store. Accepts a zeroed store, and NULL. */
void
UIIfModels_Free(struct UIIfModels* models);

/** The head entry for `com_id`, or -1 when there is none. */
int
UIIfModels_HeadIndex(
    struct UIIfModels const* models,
    int com_id);

/**
 * Set `com_id`'s head, adding an entry (with no animation) when it has none.
 * Pending again: applied_gen goes to 0 and nothing has landed. Answers the
 * entry's index.
 */
int
UIIfModels_SetHead(
    struct UIIfModels* models,
    int com_id,
    enum UIIfHeadKind kind,
    int npc_id,
    int zoom,
    uint32_t close_serial);

/** Remove entry `index`; the last entry moves into its place. */
void
UIIfModels_RemoveHeadAt(
    struct UIIfModels* models,
    int index);

/**
 * The entry a head's task was made for, if it is still the one `com_id`
 * holds: the same kind, source id, zoom and close serial, and not landed.
 * -1 when the component's head was replaced, removed or has landed since --
 * the task then has nothing to apply.
 */
int
UIIfModels_HeadFor(
    struct UIIfModels const* models,
    int com_id,
    enum UIIfHeadKind kind,
    int npc_id,
    int zoom,
    uint32_t close_serial);

/** Entry `index`'s task landed `scene_id` (-1: nothing to show), resolved to
 *  `landed_npc_id`; it is bound again on the next pass (applied_gen 0). */
void
UIIfModels_HeadLanded(
    struct UIIfModels* models,
    int index,
    int scene_id,
    int landed_npc_id);

/**
 * Give `com_id`'s head animation `anim_id` and make it pending again. False,
 * and nothing changed, when the component has no head.
 */
bool
UIIfModels_SetHeadAnim(
    struct UIIfModels* models,
    int com_id,
    int anim_id);

/** The player-model slot held by `com_id`, or -1 when there is none. */
int
UIIfModels_PlayerModelIndex(
    struct UIIfModels const* models,
    int com_id);

/**
 * A slot for `com_id`, which must not hold one: the first free slot, keeping
 * its scene id, else a new one whose scene id is `scene_base` plus its index.
 * The slot comes back zeroed but for its component, scene id, close serial
 * and an anim_id of -1; the caller fills the composition.
 */
struct UIIfPlayerModel*
UIIfModels_ClaimPlayerModel(
    struct UIIfModels* models,
    int com_id,
    int scene_base,
    uint32_t close_serial);

/** Free slot `index`: no component, nothing built. Its scene id stays. */
void
UIIfModels_FreePlayerModelAt(
    struct UIIfModels* models,
    int index);

/**
 * Give `com_id`'s player model animation `anim_id` and make it pending again.
 * False, and nothing changed, when the component has no player model.
 */
bool
UIIfModels_SetPlayerModelAnim(
    struct UIIfModels* models,
    int com_id,
    int anim_id);

/**
 * Forget everything held for `com_id`: its head entry is removed and every
 * player-model slot it holds is freed.
 */
void
UIIfModels_Forget(
    struct UIIfModels* models,
    int com_id);

/**
 * Put back `count` head entries copied from a checkpoint, growing the table to
 * fit. The caller sets each entry's applied_gen afterwards.
 */
void
UIIfModels_RestoreHeads(
    struct UIIfModels* models,
    struct UIIfHead const* heads,
    int count);

/**
 * Put back `count` player-model slots copied from a checkpoint, growing the
 * table to fit. Slots past `count` are freed rather than dropped, because a
 * slot's index is its scene id. The caller sets each restored slot's
 * applied_gen afterwards.
 */
void
UIIfModels_RestorePlayerModels(
    struct UIIfModels* models,
    struct UIIfPlayerModel const* player_models,
    int count);

/** The local player's appearance differs from what the figure was last built
 *  from, or it has never been built. */
bool
UIIfModels_LocalPlayerChanged(
    struct UIIfModels const* models,
    int const slots[12],
    int const colors[5],
    int gender);

/** The figure was built from this appearance. */
void
UIIfModels_LocalPlayerBuilt(
    struct UIIfModels* models,
    int const slots[12],
    int const colors[5],
    int gender);

#endif /* SRC_UI_UITREE_IF_MODELS_H */
