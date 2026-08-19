#ifndef SRC_FEATURES_FEATURES_H
#define SRC_FEATURES_FEATURES_H

/*
 * Per-era client feature table — THE modularity seam for *client behaviour*
 * that changed between game generations, the way
 * net/rev/gameproto_revisions.h is the seam for the wire and
 * engine/cache_provider.h is the seam for the cache format.
 *
 * The rule is the same as the rev tables': a zero field means "classic",
 * i.e. the 2004/LostCity behaviour torirs was originally ported against
 * (Client-TS, `Client-TS/src/client/Client.ts`). An era only fills the slots
 * where it genuinely diverges, so `{0}` is a working table and adding a field
 * cannot silently change an existing era.
 *
 * Unlike the rev table this is resolved on EVERY boot, online or offline —
 * an offline dat2 boot still wants the modern approach model when you click a
 * loc. Resolution order (app.c App_Init):
 *
 *   [features:boot] era=<name>  >  TORIRS_FEATURES_ERA  >
 *   ToriRS_Features_ForCache(epoch, revision)
 *
 * Interaction/routing was the first genuine two-model split (see
 * docs/PATHING_INTERACTION_PARITY.md); painter, lighting, and audio differences
 * live here as well. Other era-conditional behaviour should move here rather
 * than growing new `cache_revision >= N` tests at the call site.
 */

/** Named client-behaviour generations. */
enum ToriRS_FeatureEra
{
    TORIRS_FEATURE_ERA_INVALID = 0,
    /** 2004-era / LostCity (dat1 caches, lc245_2 + lc254 protocol). The client
     *  owns pathing; loc approach is decided from the loc's placed shape. */
    TORIRS_FEATURE_ERA_LOSTCITY = 1,
    /** OldSchool rev 230-ish (dat2 caches, osrs230 protocol). Server-authoritative
     *  pathing since end-2013: MOVE_GAMECLICK is destination-only; reach uses
     *  rsmod's shape-keyed exitStrategy under RECT. */
    TORIRS_FEATURE_ERA_OSRS = 2,
    /** Named alias that shares osrs pathing; still carries xrsps lighting flags
     *  for manifests that already state it. */
    TORIRS_FEATURE_ERA_SERVER_ROUTED = 3,
};

/** Who computes the route for a click. */
enum ToriRS_PathingMode
{
    /** Client-TS `tryMove`: BFS on the local collision map at click time, then
     *  a MOVE_GAMECLICK / MINIMAPCLICK / OPCLICK waypoint packet. */
    TORIRS_PATHING_CLIENT_BFS = 0,
    /** Server owns the route: destination / interaction target only on the
     *  wire; SET_MAP_FLAG comes from the server. */
    TORIRS_PATHING_SERVER_AUTHORITATIVE = 1,
};

/**
 * "Could not reach it — walk as close as possible."
 *
 * Both references flood the map first and only rank *already-flooded* tiles, so
 * a model is a box size plus a ranking rule; the implementation of both lives in
 * collision_nearest_fallback (src/engine/world_builder/collision_map.c) and the
 * numeric encoding of each model in collision_nearest_opts_from_model.
 *
 * The zero value is the 2004 behaviour, per the table's zero-is-classic rule.
 */
enum ToriRS_NearestModel
{
    /**
     * Client-TS `tryMove` (`Client-TS/src/client/Client.ts`, the `tryNearest`
     * block): the 3x3 ring around the destination, first tile with the lowest
     * step count wins, step count capped at 100. Its `for (padding = 1;
     * padding < 2; padding++)` never widens past one tile.
     */
    TORIRS_NEAREST_RING3_STEPS = 0,
    /**
     * Official OSRS. The rev-239 client carries this routine verbatim
     * (`Statics.method5592`, the `!arrived` branch) and the rsmod/LostCity
     * servers call it `findClosestApproachPoint`: scan the 21x21 box (+/-10)
     * around the destination, keep tiles the flood reached in under 100 steps,
     * rank by squared distance to the *target rectangle*, break ties on the
     * shorter flood. Nothing in the box -> no movement at all.
     *
     * The two only agree when the destination's immediate neighbours are
     * reachable. Clicking across a river, into a walled compound or past a
     * fence is exactly where RING3 gives up and this one walks you to the wall.
     */
    TORIRS_NEAREST_BOX10_RECT = 1,
    /** No fallback: unreachable means no route. */
    TORIRS_NEAREST_NONE = 2,
};

/** How "close enough to interact" is decided for a loc / npc target. */
enum ToriRS_ApproachModel
{
    /** Client-TS CollisionMap.testWall / testWDecor / testLoc, keyed off the
     *  loc's placed shape + angle, with LocType.forceapproach vetoing sides. */
    TORIRS_APPROACH_LEGACY_SHAPE = 0,
    /** rsmod ReachStrategy via collision_approach_from_shape: wall / wall-decor
     *  / rectangle / exclusive-rectangle from the placed loc shape. Size-1
     *  rectangle adjacency reads the source tile's facing wall bit. */
    TORIRS_APPROACH_RECT = 1,
};

/* Modern OSRS class112.method3959 clamps its scene draw-distance preference
 * to this interval. Client-TS has no preference and hard-codes the minimum. */
enum
{
    TORIRS_PAINTER_DRAW_DISTANCE_MIN = 25,
    TORIRS_PAINTER_DRAW_DISTANCE_MAX = 90,
};

/**
 * How a running tick spends energy and an idle one gets it back — the one
 * place Agility level is arithmetic rather than a level gate.
 *
 * Both models charge ONCE PER TICK, not per step, and both stop at a 64 kg
 * ceiling; they disagree about what the level does. Implemented in
 * net/mock/mock230_runenergy.c, which is the only place either appears.
 */
enum ToriRS_RunEnergyModel
{
    /**
     * LostCity / 2004 (`Player.ts:705-713`, and xrsps agrees on neither half):
     *
     *     drain   = 67 + 67 * clamp(weight_kg, 0, 64) / 64
     *     restore = agility / 6 + 8
     *
     * Agility appears in the restore only, so a level-99 player burns energy
     * exactly as fast as a level-1 one and merely refills faster. Zero, per
     * the table's zero-is-classic rule.
     */
    TORIRS_RUN_ENERGY_CLASSIC = 0,
    /**
     * OldSchool after the 8 January 2025 run-energy rework
     * (https://oldschool.runescape.wiki/w/Run_energy):
     *
     *     drain   = floor((60 + 67 * clamp(weight_kg, 0, 64) / 64)
     *                     * (1 - agility / 300))
     *     restore = floor(agility / 10) + 15
     *
     * Agility is now in BOTH halves: at 99 the drain is a third lower and the
     * restore roughly twice the level-1 rate. This is the model every current
     * wiki number describes, and the one the graceful set (restore x1.3),
     * stamina potions (drain x0.3) and a charged ring of endurance (drain
     * x0.85) modify.
     */
    TORIRS_RUN_ENERGY_OSRS_2025 = 1,
};

/**
 * How an actor's draw position is integrated between the tiles the server
 * hands out — the difference between a walk that glides and one that ticks.
 *
 * Both models pick the step speed the same way (4 walking, 2 mid-turn when the
 * actor is free to turn, 6/8 once the queue has run 3/4 deep, doubled for a run
 * step, 8 to repay a DELAYMOVE hold). They disagree only about the clock the
 * speed is spent against.
 */
enum ToriRS_MoverModel
{
    /**
     * 2004 / LostCity (`Client.ts` routeMove): one integer `moveSpeed` applied
     * per 20ms client cycle, inside the same pass that picks the facing and the
     * walk sequence. A frame that is not a whole cycle contributes nothing, and
     * one that is two contributes exactly two. The teleport-snap test is the
     * era's own: per axis, `|dst - pos| > 256`.
     *
     * Zero, per the table's zero-is-classic rule — and a legacy server on a
     * legacy client should keep it, because the era's own client looks like
     * this and a smoother one is a difference, not a fix.
     */
    TORIRS_MOVER_CYCLE_INTEGER = 0,
    /**
     * rev-239 (`class105.method3611`, driven per rendered frame from
     * `client.method2324` with `elapsed_ns / 2.0E7F`): the speed is spent
     * against real elapsed time expressed in fractional 20ms cycles, into a
     * float position, carrying the remainder of a frame's budget onto the next
     * queued tile. `class105.method3520` keeps the per-cycle half — facing,
     * sequence choice, tile retirement — and moves nothing.
     *
     * The snap test moves with it: `max(|dx|, |dz|) > 288` measured on the
     * float position, because a two-tile run step taken from a fractional
     * position is ordinary here and the 256 rule would teleport through it.
     */
    TORIRS_MOVER_FRAME_DELTA = 1,
};

struct ToriRS_FeatureTable
{
    enum ToriRS_FeatureEra era;
    char const* name;

    /* --- interaction pathing -------------------------------------------- */

    /** enum ToriRS_PathingMode. 0 = client-side BFS. */
    int pathing_mode;
    /** enum ToriRS_MoverModel. 0 = the 2004 per-cycle integer mover. */
    int mover_model;
    /** enum ToriRS_ApproachModel. 0 = legacy shape tests. */
    int approach_model;
    /**
     * 0 = approach an NPC as a 1x1 target at its route tile, which is literally
     * what Client-TS does (`tryMove(..., npc.routeX[0], npc.routeZ[0], 2, 1, 1,
     * ...)`) regardless of NPC size. 1 = use the NPC's own size as the target
     * rectangle, which is what every rsmod-derived server expects — with 0
     * against such a server the client flags a tile *inside* a large NPC.
     */
    int npc_approach_uses_size;
    /**
     * How a mover standing INSIDE its pathing target's footprint gets out from
     * under it. See docs/OSRS_PATHING_LOS.md §2.5.
     *
     * 0 = one random cardinal tile, re-rolled every tick and not validated
     *     against collision — LostCity `PathingEntity.randomWalk`. In the
     *     reference that branch is guarded by `moveStrategy === NAIVE`, so it
     *     is the *npc* answer; this tree has always run it for the player too,
     *     and zero keeps that until an era asks otherwise.
     * 1 = no special case at all: route with the ordinary exclusive-rectangle
     *     approach (shape -2), whose `reached` refuses every overlapping tile,
     *     so the BFS floods across the footprint and terminates on the nearest
     *     perimeter tile. rsmod has no under-target branch in
     *     `PlayerInteractionProcessor` — only in the npc one — which is what
     *     makes this the modern behaviour rather than an invention.
     *
     * The difference is visible, not academic. Under a 5x5 boss, 0 wanders a
     * coin-flip axis one tile per tick and can walk back in; 1 leaves by the
     * nearest face in a straight line, at two tiles per tick when running, and
     * the op fires on the tick of arrival because the post-move `tryInteract`
     * already acts for pathing entities.
     *
     * The npc side keeps the random cardinal under both values: rsmod's
     * `NpcInteractionProcessor.stepAwayFromTarget` picks uniformly, and the
     * safespot / "walk under Graardor to make it miss" behaviour depends on it.
     */
    int under_target_routes_out;
    /**
     * Radius of the "could not reach it, walk as close as possible" box for an
     * *interaction* click. 0 = no fallback at all (Client-TS passes
     * tryNearest=false for every type-2 tryMove); 10 = the rsmod/XRSPS
     * alternative-route search, which is TORIRS_NEAREST_BOX10_RECT's radius.
     *
     * Ground and minimap clicks are NOT affected — they carry their own model,
     * `ground_click_nearest_model`, because the two click kinds genuinely
     * diverge in the 2004 client (ring for the ground, nothing for an op).
     */
    int op_click_nearest_range;
    /**
     * Ranking for that fallback. 0 = the reference's "first tile with the
     * lowest step count wins" (only meaningful for the 3x3 ring); 1 = "lowest
     * squared distance to the target rectangle, ties broken by fewest steps",
     * which is the only sane rule once the box is 21x21.
     */
    int nearest_ranks_by_rect_distance;
    /**
     * enum ToriRS_NearestModel for a *ground or minimap* click on a tile that
     * cannot be reached. 0 = RING3_STEPS, the Client-TS behaviour.
     *
     * Modern OSRS uses BOX10_RECT, and it is a *server* behaviour there: the
     * client sends MOVE_GAMECLICK (opcode 114, 5 bytes: absolute x, absolute z,
     * key combination) with no reachability test of its own, and the server
     * answers with the route to the closest approach point. The rev-239 client
     * still carries the identical routine for reconstructing a reported move,
     * which is where the constants above were read from.
     *
     * Both halves of this tree read the same field: the client for its
     * legacy-era local BFS (app.c), the mock server for every ground click it
     * routes (mock230_scene_route). They must not disagree.
     */
    int ground_click_nearest_model;
    /*
     * ---- The two permissive extensions -------------------------------------
     *
     * Everything else in this table states what some revision does. These two
     * state what this client may do INSTEAD, and both are 0 in every era table
     * on purpose: out of the box the ground click is deob-exact, down to the
     * clicks that do nothing. Turn one on per boot ([features:boot] keys /
     * TORIRS_GROUND_CLICK_* env), never by editing an era table — an era table
     * that carries a deviation stops being a statement about the era.
     */
    /**
     * 1 = when `ground_click_nearest_model` finds nothing in its box, walk to
     * the flooded tile closest to the click anyway.
     *
     * The references bound that search (3x3 or 21x21) and a click further out
     * than the box does nothing: Statics.method5592 ends `if (var22 ==
     * Integer.MAX_VALUE) return -1`. That is the whole of "I clicked past the
     * Inferno's lava skirt and my player stood there" — the void ringing an
     * instance's floor is more than ten tiles from anything walkable.
     *
     * Ground and minimap clicks only. An interaction click that cannot reach
     * its target still runs the era's own model, because "walk somewhere near
     * it and do nothing" is not what an op means.
     *
     * Read by both halves of the tree, like the model above, and under a
     * server-authoritative era it is the SERVER's copy that decides anything —
     * the client sends a tile and never routes. Both halves read
     * TORIRS_GROUND_CLICK_UNBOUNDED so an embedded boot cannot end up with one
     * half permissive and the other strict.
     */
    int ground_click_nearest_unbounded;
    /**
     * 1 = a click that hit no ground triangle at all resolves to the scene
     * tile whose centre is nearest the cursor on screen, instead of walking
     * nowhere.
     *
     * The reference records a ground tile only from inside the rasterizer
     * (class155 -> class112.method4269), so a click on the sky, on the void
     * outside an instance's floor, or on a tile the plane filter refused
     * leaves field1664 at -1 — and class112.method3951 (`field1670 &&
     * field1681 != -1`) then never lets the move packet be built. No packet,
     * no map flag, and no cross either.
     */
    int ground_click_offmap_nearest;
    /* ---- end permissive extensions ---------------------------------------- */
    /**
     * Ceiling, in tiles, on how far from the local player a GROUND pick may
     * land. A pick further out is pulled back along the line to the player
     * until it sits exactly this far away. 0 = no ceiling.
     *
     * Deob class112.method4269, which is where the rev-239 client records the
     * tile a ground hittest landed on:
     *
     *     int var12 = (int) Math.hypot(var7 - var1, var8 - var2) - 70;
     *     if (var12 > 0) {
     *         var4 = (var7 * var12 + var1 * 70) / (var12 + 70);
     *         var5 = (var8 * var12 + var2 * 70) / (var12 + 70);
     *     }
     *
     * var7/var8 are the local player's tile, var1/var2 the hit tile: the
     * weights put the result at distance exactly 70. It is guarded by the
     * view's own flag and `isTopLevel()`, i.e. only the main world view, which
     * is the only view this client has.
     *
     * 0 for the classic eras on purpose — the 2004 client stores the hit tile
     * verbatim (Client-TS `World.groundX = tileX`), so a ceiling there would
     * be an invention.
     */
    int ground_click_clamp_tiles;
    /**
     * 0 = asymmetric LoS (2004 / LostCity / live PvM): A can range B while B
     * cannot range back. 1 = modern symmetric LoS for *player-vs-player only*
     * (the 29 Aug 2019 LMS update): los(a→b) && los(b→a). PvM stays asymmetric
     * either way. See docs/OSRS_PATHING_LOS.md.
     */
    int los_symmetric_pvp;
    /**
     * Width of the BFS search window in tiles, centred on the mover.
     *
     * 0 = flood the whole collision map, which is literally what Client-TS does
     * (its BFS is over the resident 104x104 scene, so the scene *is* the
     * window). 128 = rsmod / LostCity `PathFinder.DEFAULT_SEARCH_MAP_SIZE`,
     * where the window is the router's own constant and a bigger map does not
     * make longer routes possible.
     *
     * The two only differ once a map wider than the window exists: a 104-tile
     * scene is covered whole either way. Stating it here is what keeps the
     * route from silently becoming a function of how much terrain is loaded.
     */
    int route_window_tiles;

    /* --- widget targeting ------------------------------------------------ */

    /**
     * Which bit of a component's target mask means "may be cast on a held
     * item", the one flag in that mask whose position moved between eras.
     *
     * 0 = Client-TS's 0x10: `targetMask & 0x10` in the inventory arm of
     * addComponentOptions, and the `targetMask === 0x10` test that snaps the
     * sidebar to the backpack when a spell can only target items.
     * 0x20 = OldSchool, where the deob reads `(targetMask & 0x20) == 32` in the
     * same place and `magic_spellbook:high_alchemy` decodes to exactly 0x20.
     *
     * The other four flags (obj 0x1, npc 0x2, loc 0x4, player 0x8) are shared
     * by both and need no entry. Zero is the classic value, per the table rule.
     */
    int target_mask_held;

    /* --- scene painter -------------------------------------------------- */

    /**
     * Painter radius in tiles. 0 means Client-TS's fixed 25-tile radius, which
     * preserves the table's zero-is-classic rule. Values 25..90 select the
     * configurable modern OSRS radius (deob class112.method3959).
     */
    int painter_draw_distance;

    /* --- model lighting -------------------------------------------------- */

    /**
     * 0 = Client-TS: NPC body lighting ignores NpcType ambient/contrast
     * (opcodes 100/101) and always uses the actor profile alone. 1 = xrsps:
     * ambient += npctype.ambient, contrast += npctype.contrast (decoder already
     * pre-scales contrast by 5).
     */
    int npc_light_uses_type_ambient_contrast;
    /**
     * Absolute ambient used when lighting a player chathead with the actor
     * direction/attenuation. 0 = Client-TS: bridge lights with the scene
     * regime (IfType.getTempModel). 128 = xrsps PlayerChatheadFactory
     * (`light(..., 64 + 64, 850, -30, -50, -30)` — passed as absolute ambient
     * to LightModelParams, not an Actor offset).
     */
    int player_head_light_ambient;

    /* --- audio ----------------------------------------------------------- */

    /**
     * 1 = a sound effect is refused when a longer one is already sounding
     * (Client-TS's `lastWave*` rule). 0 = effects mix freely.
     *
     * The 2004 client is monophonic for effects because it queues them onto one
     * 8-bit device and skips a clip whose predecessor has not finished. The
     * modern client is not: its PcmPlayer holds eight priority lists of streams
     * and mixes all of them, so refusing a sound because another is playing
     * drops most of a combat tick's audio -- a hit splat, a block and a special
     * all land within a few ticks of each other.
     */
    int effects_monophonic;

    /* --- movement / run energy ------------------------------------------- */

    /**
     * enum ToriRS_RunEnergyModel. 0 = the 2004 pair, which is what this tree
     * shipped with.
     *
     * Server-only by construction: the client is told a percentage
     * (UPDATE_RUNENERGY) and never computes one, so unlike the ground-click
     * fields there is no client half to keep in step. It lives here rather
     * than as a `cache_revision >=` test in the world tick because it is an
     * era fact, and because the two models have to be runnable back to back
     * against the same account — a measurement of "how far can I run" that
     * cannot be compared to the other model proves nothing.
     *
     * Overridable per boot with MOCK230_RUN_ENERGY=classic|osrs2025.
     */
    int run_energy_model;

    /* --- interface settings ---------------------------------------------- */

    /**
     * The varbit carrying the player's "interface resizing" setting, or 0 for
     * an era that has no such setting (the classic default: 2004 has no
     * resizable mode at all).
     *
     * This is a *client* setting — the settings panel toggles it with
     * `setvarbit` (clientscript 3965 case 442) and no server transmits it — so
     * an unseeded client comes up with it at 0, which is not what a real
     * account looks like and is not a neutral value.
     *
     * It decides where every main modal is drawn. `~script7925` gates the
     * cache's interface-window helper on it, and the branch it selects places
     * the modal's panel inside the modal's own root two different ways:
     *
     *   on  — clamp the saved default box to the host
     *         (`max(0, min(%varcint1170, host_w - %varcint1168))`), which
     *         collapses to (0,0) when the host is the 512x334 mainmodal slot,
     *         i.e. the panel lands exactly on the slot.
     *   off — `if_setposition(if_getx(mainmodal), if_gety(mainmodal), 0, 0, …)`,
     *         which is the slot's own *parent-relative* origin. In resizable
     *         mode the slot is centred in `hud_container_front`, so that adds
     *         the centring offset a second time and every modal drifts down and
     *         right by half the chrome insets (+374,+219 at 1511x938) — out
     *         from under the dimmer hole clientscript 910 paints for it.
     *
     * Off is only self-consistent where the slot's relative origin is (0,0),
     * which is why the same proc also returns 0 for fixed window mode and for
     * mobile. Stating it here rather than in app.c because the id is a lineage
     * fact: rev 230's varbit table stops at 17425, so the seed no-ops there.
     */
    int varbit_interface_resizing;
};

/* Era getters (static singletons, like the rev tables). */
struct ToriRS_FeatureTable const*
ToriRS_Features_LostCity(void);

struct ToriRS_FeatureTable const*
ToriRS_Features_OSRS(void);

struct ToriRS_FeatureTable const*
ToriRS_Features_ServerRouted(void);

/** Resolve by name ("lostcity", "osrs", "server_routed"); NULL when unknown. */
struct ToriRS_FeatureTable const*
ToriRS_Features_ByName(char const* name);

/**
 * Resolve an enum ToriRS_NearestModel by name, for the manifest key and the
 * mock server's env override: "ring3", "box10_rect", "none". Returns -1 when
 * the name is not one of those, so a typo can be reported rather than silently
 * read as the zero model.
 */
int
ToriRS_Features_NearestModelByName(char const* name);

/**
 * Resolve an enum ToriRS_MoverModel by name, for the manifest key and its env
 * twin. Returns -1 for an unknown name. Names: `cycle` | `frame`.
 */
int
ToriRS_Features_MoverModelByName(char const* name);

/** Name of an enum ToriRS_MoverModel, for diagnostics. */
char const*
ToriRS_Features_MoverModelName(int model);

/**
 * Resolve an enum ToriRS_RunEnergyModel by name, for the mock server's env
 * override: "classic", "osrs2025". Returns -1 when the name is not one of
 * those, so a typo is reported rather than silently read as the zero model.
 */
int
ToriRS_Features_RunEnergyModelByName(char const* name);

/** The name that maps back to a run-energy model, for logging. "?" if none. */
char const*
ToriRS_Features_RunEnergyModelName(int model);

/** Resolve the feature table's 0 sentinel to Client-TS's 25-tile radius. */
int
ToriRS_Features_PainterDrawDistance(struct ToriRS_FeatureTable const* features);

/** The name `ToriRS_Features_NearestModelByName` would map back to a model, for
 *  logging. Unknown values read as "?". */
char const*
ToriRS_Features_NearestModelName(int model);

/**
 * Derive an era from the cache identity, for boots that do not state one.
 *
 * The discriminator is the *lineage*, not the revision. dat1 is always
 * LOSTCITY. dat2 + `oldschool` is OSRS, because the OldSchool server is the
 * rsmod-derived one whose approach model the client has to agree with. dat2 +
 * `rs2` (the rev-634 / rev-643 caches in this tree) stays LOSTCITY: those are
 * still the classic client, whose tryMove decides approach from the loc's
 * placed shape exactly like 2004. Anything unidentified stays LOSTCITY, which
 * is the behaviour torirs was originally ported against.
 *
 * Nothing here can select SERVER_ROUTED — that is a property of the *server*,
 * not the cache, so it must be stated in the manifest.
 *
 * Args are `enum RSCache_Game` / `enum RSCache_Epoch` values, passed as int so
 * this header stays free of the rscache profile header.
 */
struct ToriRS_FeatureTable const*
ToriRS_Features_ForCache(int cache_game, int cache_epoch, int cache_revision);

#endif
