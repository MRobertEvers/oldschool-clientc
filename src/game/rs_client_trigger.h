#ifndef RS_CLIENT_TRIGGER_H
#define RS_CLIENT_TRIGGER_H

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
/** An npc went away (`Client::DestroyNpc`). Subject = npc type. */
#define RS_TRIGGER_NPC_DEL 36
/** The scene builder placed a loc (`Client::OnLoadLocation`). Subject = loc
 *  type. This is the big one: 218 scripts in this cache. */
#define RS_TRIGGER_LOC_ADD 37
/** A zone packet changed a loc (`Client::LocChangeUnchecked`). */
#define RS_TRIGGER_LOC_CHANGE 38
/** A player became visible / went away. Subject is unused (players have no
 *  type), so only the global form can match. */
#define RS_TRIGGER_PLAYER_ADD 41
#define RS_TRIGGER_PLAYER_DEL 42
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

#endif /* RS_CLIENT_TRIGGER_H */
