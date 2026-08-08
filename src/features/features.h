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
 * Today the table only carries interaction/routing behaviour, because that is
 * where the first genuine two-model split turned up (see
 * docs/PATHING_INTERACTION_PARITY.md). Other era-conditional behaviour should
 * move here rather than growing new `cache_revision >= N` tests at the call
 * site.
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

struct ToriRS_FeatureTable
{
    enum ToriRS_FeatureEra era;
    char const* name;

    /* --- interaction pathing -------------------------------------------- */

    /** enum ToriRS_PathingMode. 0 = client-side BFS. */
    int pathing_mode;
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
     * Radius of the "could not reach it, walk as close as possible" box for an
     * *interaction* click. 0 = no fallback at all (Client-TS passes
     * tryNearest=false for every type-2 tryMove); 10 = XRSPS
     * Pathfinder.ALTERNATIVE_ROUTE_RANGE.
     *
     * Ground and minimap clicks are NOT affected — both references use the
     * same 3x3 ring there, so it stays hard-coded in collision_map.c.
     */
    int op_click_nearest_range;
    /**
     * Ranking for that fallback. 0 = the reference's "first tile with the
     * lowest step count wins" (only meaningful for the 3x3 ring); 1 = XRSPS's
     * "lowest squared distance to the target rectangle, ties broken by fewest
     * steps", which is the only sane rule once the box is 21x21.
     */
    int nearest_ranks_by_rect_distance;
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
