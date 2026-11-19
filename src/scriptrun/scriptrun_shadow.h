#ifndef SRC_SCRIPTRUN_SCRIPTRUN_SHADOW_H
#define SRC_SCRIPTRUN_SCRIPTRUN_SHADOW_H

/*
 * The headless client's shadow of the packet model.
 *
 * WHAT IT IS. A second set of the packet model's records (scriptrun_model.h:
 * VarPManager, RS_PlayerStats, InvManager, the interface text/hide/colour/event
 * stores, RS_Chat, RS_Social, the session records, one event ring), owned by
 * one struct ScriptrunCore and fed every packet the core decodes through
 * RS_ServerState_Apply (game/rs_server_state.h) -- the ONE copy of the rules
 * -- and nothing else. The core holds the first set (`core->model`), fed the
 * same packets and then presented: the bot's mount table drops a leaving
 * group's arming and stores (RS_ServerState_SlotGroupLeaves), its entity
 * stream adds public chat lines. The comparer reports where the core's set,
 * after that presentation, differs from the bare model's: what the bot's
 * presentation changed, and any packet one side saw and the other did not.
 * Nothing the bot reads comes from here: the shadow only reads the core, and
 * the core never reads the shadow.
 *
 * WHEN. With TORIRS_SCRIPTRUN_SHADOW unset the core holds no shadow and no
 * packet is fed to one. Set, it names the TSV file the rows go to (the first
 * shadow of the process truncates it, later ones -- other bots, a relogged
 * bot's new core -- append).
 *
 * WHAT IS COMPARED, AND WHEN.
 *   - At each SERVER_TICK_END, after both sides applied it: varps (int and
 *     long arms), stats (level, xp, run energy), inventories (presence, size
 *     and each slot), interface texts, hides and IF_SETEVENTS ranges. A row is
 *     written when a key starts to differ or its differing pair changes, not
 *     on every tick it stays different.
 *   - Per packet: the chat lines each side added, and the event stream: the
 *     events of the kinds the shared apply stamps (varp_changed, inv_changed,
 *     inv_packet, chat_message, server_tick, npc_seq) that each side stamped
 *     while applying that packet, in order. Every mismatch is a row.
 *
 * ROW. tick, family, key, old (the core's model, presented), shared (the bare
 * model's), the packet
 * kind that last touched the key, the bot's local pid; tab-separated, "-" for
 * a value a side does not hold.
 *
 * WHAT IT CANNOT SHADOW. The shared apply's entity setters (ANIM_SPECIFIC,
 * NPC_ANIM_SPECIFIC, the SPOTANIM_SPECIFICs, NPC_HEADICON_SPECIFIC) address
 * World entities, which only the core's own entity stream creates; the
 * shadow's World has none, so those packets are applied and change nothing
 * (the core does not feed them at all, scriptrun_core.c core_model_feeds).
 * They are counted (`entity_setters`) so the report can say how many there
 * were. UPDATE_UID192 writes process-global login state and is fed to
 * neither.
 */

#include <stdint.h>

struct RevPacket;
struct ScriptrunCore;
struct ScriptrunShadow;

/** RSProt's name for a packet kind, for the rows; NULL prints the number. */
typedef char const* (*ScriptrunShadow_PktNameFn)(int pkt_name);

enum ScriptrunShadowFamily
{
    SCRIPTRUN_SHADOW_VARPS = 0,
    SCRIPTRUN_SHADOW_STATS,
    SCRIPTRUN_SHADOW_INVS,
    SCRIPTRUN_SHADOW_MESSAGES,
    SCRIPTRUN_SHADOW_IF_TEXTS,
    SCRIPTRUN_SHADOW_IF_HIDES,
    SCRIPTRUN_SHADOW_IF_EVENTS,
    SCRIPTRUN_SHADOW_EVENTS,
    SCRIPTRUN_SHADOW_FAMILY_COUNT
};

/** "varps", "stats", ... */
char const*
ScriptrunShadow_FamilyName(int family);

/**
 * A shadow writing its rows to `path`, or NULL when `path` is NULL or empty
 * (the variable unset: no shadow). `rev_number` is the wire revision's number
 * (GameProtoRevTable.number); `cycle` the owning core's world cycle, which
 * the shadow's events and chat lines are stamped with (read, never written);
 * `pkt_name` names packet kinds in the rows.
 */
struct ScriptrunShadow*
ScriptrunShadow_New(
    char const* path,
    int rev_number,
    int const* cycle,
    ScriptrunShadow_PktNameFn pkt_name);

/** Print the per-family counts to stderr and release everything. NULL is a
 *  no-op. */
void
ScriptrunShadow_Free(struct ScriptrunShadow* shadow);

/**
 * One parsed packet, BEFORE the core applies it: notes the keys it touches
 * (reading the core's mounts as they were), marks the core's event ring, and
 * applies the packet to the shadow's records.
 */
void
ScriptrunShadow_Before(
    struct ScriptrunShadow* shadow,
    struct ScriptrunCore const* core,
    struct RevPacket const* packet);

/**
 * The same packet AFTER the core applied it: compares the two sides' chat
 * lines and events from this packet, and at SERVER_TICK_END every state
 * family.
 */
void
ScriptrunShadow_After(
    struct ScriptrunShadow* shadow,
    struct ScriptrunCore const* core,
    struct RevPacket const* packet);

/** Rows written for `family` so far. */
int
ScriptrunShadow_Rows(
    struct ScriptrunShadow const* shadow,
    int family);

/** Distinct keys that have differed in `family` so far. */
int
ScriptrunShadow_Keys(
    struct ScriptrunShadow const* shadow,
    int family);

#endif
