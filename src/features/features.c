#include "features/features.h"

#include "rscache_profile.h" /* enum RSCache_Epoch */

#include <string.h>

/*
 * The era tables. Read features.h first: a zero slot means "classic", so the
 * LostCity table is deliberately almost empty and each later era states only
 * what it changed. Keep it that way — a new field must default to the
 * 2004 behaviour so adding one cannot move an existing era.
 */

/* 2004-era / LostCity: everything classic, every slot at its zero default.
 * Written out with explicit designated initializers anyway, because "this era
 * is the zero table" is the fact worth stating. */
static struct ToriRS_FeatureTable const k_features_lostcity = {
    .era = TORIRS_FEATURE_ERA_LOSTCITY,
    .name = "lostcity",
    .pathing_mode = TORIRS_PATHING_CLIENT_BFS,
    .approach_model = TORIRS_APPROACH_LEGACY_SHAPE,
    .npc_approach_uses_size = 0,
    .op_click_nearest_range = 0,
    .nearest_ranks_by_rect_distance = 0,
};

/*
 * OldSchool rev 230-ish. Still a waypoint-packet client — the wire is
 * unchanged, so pathing_mode stays CLIENT_BFS — but the *server* it talks to
 * decides reachability with rsmod's rectangle strategies. Three consequences,
 * all of them "agree with the server or flag a tile it will refuse":
 *
 *   - approach_model RECT: the shared edge is wall-checked from both tiles,
 *     and a loc that does not clip is stood on rather than approached.
 *   - npc_approach_uses_size: a size-3 NPC is a 3x3 target, not its SW tile.
 *   - op_click_nearest_range 10: rsmod always runs its alternative-route
 *     search for interactions, so an obstructed target still walks you most of
 *     the way instead of producing no movement at all.
 */
static struct ToriRS_FeatureTable const k_features_osrs = {
    .era = TORIRS_FEATURE_ERA_OSRS,
    .name = "osrs",
    .pathing_mode = TORIRS_PATHING_CLIENT_BFS,
    .approach_model = TORIRS_APPROACH_RECT,
    .npc_approach_uses_size = 1,
    .op_click_nearest_range = 10,
    .nearest_ranks_by_rect_distance = 1,
};

/*
 * xrsps233 and friends: the click packet carries the target and the server
 * paths. The client runs no BFS, sends no waypoints, and only latches the
 * minimap flag so the UI still reads correctly.
 *
 * The approach fields still matter even here: nothing routes, but the same
 * predicates answer "is the player already adjacent?" for the UI (xrsps's own
 * isLocalPlayerAdjacentToLoc), so state the modern model rather than leaving
 * the legacy default sitting under a modern server.
 */
static struct ToriRS_FeatureTable const k_features_server_routed = {
    .era = TORIRS_FEATURE_ERA_SERVER_ROUTED,
    .name = "server_routed",
    .pathing_mode = TORIRS_PATHING_SERVER_AUTHORITATIVE,
    .approach_model = TORIRS_APPROACH_RECT,
    .npc_approach_uses_size = 1,
    .op_click_nearest_range = 0,
    .nearest_ranks_by_rect_distance = 1,
};

struct ToriRS_FeatureTable const*
ToriRS_Features_LostCity(void)
{
    return &k_features_lostcity;
}

struct ToriRS_FeatureTable const*
ToriRS_Features_OSRS(void)
{
    return &k_features_osrs;
}

struct ToriRS_FeatureTable const*
ToriRS_Features_ServerRouted(void)
{
    return &k_features_server_routed;
}

struct ToriRS_FeatureTable const*
ToriRS_Features_ByName(char const* name)
{
    if( !name || !name[0] )
        return NULL;
    if( strcmp(name, "lostcity") == 0 )
        return ToriRS_Features_LostCity();
    if( strcmp(name, "osrs") == 0 )
        return ToriRS_Features_OSRS();
    if( strcmp(name, "server_routed") == 0 )
        return ToriRS_Features_ServerRouted();
    return NULL;
}

struct ToriRS_FeatureTable const*
ToriRS_Features_ForCache(int cache_game, int cache_epoch, int cache_revision)
{
    (void)cache_revision; /* lineage decides; see the header. */
    if( cache_epoch == RSCACHE_EPOCH_DAT2 && cache_game == RSCACHE_GAME_OLDSCHOOL )
        return ToriRS_Features_OSRS();
    return ToriRS_Features_LostCity();
}
