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
    .los_symmetric_pvp = 0,
    .route_window_tiles = 0,
    .npc_light_uses_type_ambient_contrast = 0,
    .player_head_light_ambient = 0,
    .effects_monophonic = 1,
};

/*
 * OldSchool rev 230-ish. Since end-2013 the server owns routefinding (Ash);
 * MOVE_GAMECLICK is a fixed 5-byte destination body and the client never
 * emits waypoints. Reach uses rsmod's shape-keyed exitStrategy under RECT.
 *
 *   - pathing_mode SERVER_AUTHORITATIVE: no client BFS; server SET_MAP_FLAG.
 *   - approach_model RECT: collision_approach_from_shape on placed shape.
 *   - npc_approach_uses_size: a size-3 NPC is a 3x3 exclusive-rect target.
 *   - op_click_nearest_range 10: rsmod's alternative-route search (server).
 *   - los_symmetric_pvp: the 2019 LMS update — PvP LoS is symmetric; PvM is
 *     not.
 *   - route_window_tiles 128: rsmod's PathFinder floods a fixed 128x128 box
 *     around the mover rather than whatever map is resident.
 */
static struct ToriRS_FeatureTable const k_features_osrs = {
    .era = TORIRS_FEATURE_ERA_OSRS,
    .name = "osrs",
    .pathing_mode = TORIRS_PATHING_SERVER_AUTHORITATIVE,
    .approach_model = TORIRS_APPROACH_RECT,
    .npc_approach_uses_size = 1,
    .op_click_nearest_range = 10,
    .nearest_ranks_by_rect_distance = 1,
    .los_symmetric_pvp = 1,
    .route_window_tiles = 128,
    .npc_light_uses_type_ambient_contrast = 0,
    .player_head_light_ambient = 0,
    /* The modern client mixes effects; only the 2004 one is monophonic. */
    .effects_monophonic = 0,
};

/*
 * Named alias for manifests that already state server_routed. Same pathing
 * mode and approach model as osrs; keeps the xrsps lighting flags.
 *
 * The approach fields still matter: nothing routes on the client, but the
 * same predicates answer "is the player already adjacent?" for the UI.
 */
static struct ToriRS_FeatureTable const k_features_server_routed = {
    .era = TORIRS_FEATURE_ERA_SERVER_ROUTED,
    .name = "server_routed",
    .pathing_mode = TORIRS_PATHING_SERVER_AUTHORITATIVE,
    .approach_model = TORIRS_APPROACH_RECT,
    .npc_approach_uses_size = 1,
    .op_click_nearest_range = 0,
    .nearest_ranks_by_rect_distance = 1,
    .los_symmetric_pvp = 1,
    .route_window_tiles = 128,
    /* xrsps: NpcModelLoader applies type ambient/contrast; player chatheads
     * light with absolute ambient 128 + actor dir (PlayerChatheadFactory). */
    .npc_light_uses_type_ambient_contrast = 1,
    .player_head_light_ambient = 128,
    .effects_monophonic = 0,
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
