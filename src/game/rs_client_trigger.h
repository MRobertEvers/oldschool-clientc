#ifndef RS_CLIENT_TRIGGER_H
#define RS_CLIENT_TRIGGER_H

#include <stdbool.h>

/*
 * Client triggers -- how the cache binds a clientscript to a THING happening.
 *
 * Most of what a cache script does, this client already runs: the panels open,
 * the hooks fire, the buttons work. What it had no way to run at all is the
 * family of scripts nothing ever calls -- the ones the client is supposed to
 * find and run itself when an npc walks on screen, when the scene builder
 * places a loc, when the right-click menu opens.
 *
 * That is 218 loc scripts and 23 npc scripts in cache.osrs239, and they are
 * where the Activities category's non-highlight half lives: the fishing spot
 * indicators, the Agility shortcut markers, the cannon hud, the clue scroll
 * helper. Every one of them was unreachable.
 *
 * ---- how the binding is written down ----
 *
 * Not in a table. In the clientscript index's GROUP NAMES:
 *
 *     hash        = subject * 256 + trigger          -- this npc / this loc
 *                 = trigger - category * 256 - 0x300 -- any of this category
 *                 = trigger - 0x200                  -- any subject at all
 *     group name  = the decimal string of that hash
 *     name hash   = djb2 of that string
 *
 * and the client walks the three forms in that order, narrowest first
 * (reference `ClientScript::Get(ClientTriggerType2::ID, int, int)`).
 *
 * Verified against cache.osrs239 rather than assumed: the subject form at
 * trigger 37 resolves 218 loc types onto scripts 5110..5720, which are exactly
 * the Agility shortcut handlers, and the category form at trigger 35 resolves
 * 17 npc categories onto 4528..4546, which are exactly the fishing spot
 * handlers. Neither set is reachable any other way.
 *
 * Nothing here includes an engine header, so it links into a test on its own.
 */

/*
 * The trigger ids, read off the reference client's call sites.
 *
 * Only the ones this client can actually raise are named. The family is
 * larger -- the decompile's `ExecuteScript(ClientTriggerType2::ID, ...)` sites
 * cover 0x23..0x53 -- and an unnamed one is not a gap in the mechanism, only a
 * trigger nothing here fires yet.
 */
/** An npc became visible (`Client::GetNPCPosNewVis`). Subject = npc type. */
#define RS_TRIGGER_NPC_ADD 35
/** The scene builder placed a loc (`Client::OnLoadLocation`). Subject = loc
 *  type. This is the big one: 218 scripts in this cache. */
#define RS_TRIGGER_LOC_ADD 37
/** A zone packet changed a loc (`Client::LocChangeUnchecked`). */
#define RS_TRIGGER_LOC_CHANGE 38
/* 36 (npc gone, `Client::DestroyNpc`), 41 and 42 (a player became visible /
 * went away) are not named, and nothing raises them (FAULTS E10, settled):
 *  - the rev-239 deob raises none of them. Its lookup is Statics.method983
 *    (trigger, subject, category): `(subject << 8) + trigger`, then
 *    `(-3 - category << 8) + trigger`, then `trigger - 512` -- the three forms
 *    above -- called only from the script dispatch Statics.method4335, and the
 *    only triggers it is ever handed are the ones Statics.method9753 builds
 *    events for: 10-14 (menu actions 1008-1012 on a map element,
 *    Statics.method12866), 15-17
 *    (map elements entering, leaving and staying in view, class597), 18
 *    (class85), 76 (client.java ~3824), 85 (class603) and 86 (class59). No
 *    npc removal and no player add or removal is among them;
 *  - no cache this client reads binds a script to them in any form
 *    (tools/client_trigger_scan.py: 0 in cache.osrs239 and in cache.osrs242,
 *    against 24 and 31 for trigger 35).
 * Firing one would look up nothing; `make check-client-triggers` says so again
 * after a cache upgrade. */
/** The right-click menu opened (`Minimenu::Open`). Global form only. */
#define RS_TRIGGER_MINIMENU_OPEN 82

/**
 * `ScriptTriggerHelpers::GetScriptHash(subject, trigger)`.
 *
 * The narrowest form: this exact npc type, this exact loc type.
 */
int RS_ClientTriggerHashSubject(int trigger, int subject);

/**
 * `ScriptTriggerHelpers::GetCategoryScriptHash(category, trigger)`.
 *
 * `trigger - category * 256 - 0x300`. The negative bias is what keeps the three
 * forms in disjoint number ranges -- a category hash can never collide with a
 * subject hash, so one namespace holds all three.
 */
int RS_ClientTriggerHashCategory(int trigger, int category);

/** `ScriptTriggerHelpers::GetGlobalScriptHash(trigger)` -- `trigger - 0x200`. */
int RS_ClientTriggerHashGlobal(int trigger);

/**
 * The cache group-name hash of a trigger hash: djb2 over its DECIMAL STRING.
 *
 * The string step is not decoration. `HashToJs5GroupString` sprintf's "%i", so
 * the hash of -717 is the hash of the five characters `-`,`7`,`1`,`7` -- not of
 * the integer. Hashing the integer would answer -1 for every trigger in the
 * cache and look exactly like a cache that binds none.
 */
int RS_ClientTriggerNameHash(int trigger_hash);

/**
 * Look one clientscript group up by its name hash. -1 when the cache has none.
 *
 * A callback so that the walk below can live here with the three hash forms it
 * walks, without this header reaching for the cache provider -- which would
 * cost the standalone test the whole module exists to be able to have.
 */
typedef int (*RS_ClientTriggerScriptLookupFn)(void* user, int name_hash);

/**
 * The clientscript bound to `trigger` for this subject, or -1.
 *
 * Walks the three forms NARROWEST FIRST, exactly as `ClientScript::Get` does:
 * this exact subject, then its category, then any subject at all. The order is
 * the whole of the behaviour -- a global handler that ran in place of a
 * subject-specific one would be a fishing spot drawing the generic marker
 * instead of its own, which looks like the script being wrong rather than the
 * lookup.
 *
 * The category form is skipped for a non-positive `category`, because 0 is
 * "uncategorised" rather than "category zero" and the bias would turn it into
 * a hash that collides with nothing but is asked for on every lookup.
 */
int RS_ClientTriggerScriptFor(
    int trigger,
    int subject,
    int category,
    RS_ClientTriggerScriptLookupFn lookup,
    void* user);

/*
 * ---- when every ADD trigger is fired again ----
 *
 * A tree rebuild -- login's gameframe remount, a Fixed/Resizable switch --
 * reclaims every node under the `entity_overlay` builtin, and the scripted
 * overlays (game/rs_entity_overlay.h) go with them. The `cc_setonvartransmit`
 * hook that would rebuild an overlay is registered ON the layer, so it dies
 * with it, and nothing in the cache fires a client trigger again. The client
 * has to notice and re-raise every ADD trigger for what is already in the
 * world.
 *
 * Watching the builtin's component id does not notice it: the builtin is a
 * manifest node, so its id is the same before and after. A scripted overlay
 * whose layer no longer resolves is the direct observation.
 *
 * The overlay pass raises the refire when it finds such an overlay, and the
 * next pass takes it before it walks the overlays, so the refire runs outside
 * the walk it would invalidate. Raising twice before a take is one refire.
 *
 * This does not know what a tree, a layer or an overlay is, and does not fire
 * anything. The caller decides when a layer is missing, and fires the
 * triggers when Take says to.
 */
struct RS_ClientTriggerRefire
{
    int pending;
};

/** A scripted overlay's layer is gone: refire on the next take. */
void RS_ClientTriggerRefire_Raise(struct RS_ClientTriggerRefire* refire);

/** True, once, when a refire was raised since the last take. */
bool RS_ClientTriggerRefire_Take(struct RS_ClientTriggerRefire* refire);

#endif /* RS_CLIENT_TRIGGER_H */
