/* Included by torirs_server_vessel.c. Durable records intentionally exclude
 * pool handles, NPC/player leases, timers and network encoder history. */

uint32_t
ToriRSServer_VesselNavigatorMask(struct ToriRSServer* srv,
                               const struct ToriRSServerVessel* vessel)
{
    assert(srv);
    assert(vessel);
    _Static_assert(TORIRSSERVER_PLAYER_MAX <= TORIRSSERVER_VESSEL_NAVIGATOR_MAX,
                   "navigator grants cover every player slot");
    uint32_t mask = 0;
    for( int pid = 0; pid < TORIRSSERVER_PLAYER_MAX; ++pid )
    {
        const struct ToriRSServerPlayer* player = &srv->players[pid];
        if( player->active && vessel->navigator_generation[pid] &&
            vessel->navigator_generation[pid] == player->login_generation &&
            ToriRSServer_VesselAtTile(srv, player->x, player->z) == vessel )
            mask |= 1u << pid;
    }
    return mask;
}

int
ToriRSServer_VesselCanNavigate(struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player, const struct ToriRSServerVessel* vessel)
{
    assert(srv);
    assert(player);
    assert(vessel);
    if( vessel->owner_uid == 0 || vessel->owner_uid == player->pid + 1 ) return 1;
    return player->pid >= 0 && player->pid < TORIRSSERVER_VESSEL_NAVIGATOR_MAX &&
        (ToriRSServer_VesselNavigatorMask(srv, vessel) & (1u << player->pid)) != 0;
}

int
ToriRSServer_VesselSetNavigators(struct ToriRSServer* srv,
    struct ToriRSServerPlayer* captain, struct ToriRSServerVessel* vessel, uint32_t mask)
{
    assert(srv);
    assert(captain);
    assert(vessel);
    if( vessel->owner_uid != captain->pid + 1 ||
        ToriRSServer_VesselAtTile(srv, captain->x, captain->z) != vessel ||
        (mask >> TORIRSSERVER_PLAYER_MAX) ) return 0;
    for( int pid = 0; pid < TORIRSSERVER_PLAYER_MAX; ++pid )
        if( (mask & (1u << pid)) && (!srv->players[pid].active ||
            ToriRSServer_VesselAtTile(srv, srv->players[pid].x, srv->players[pid].z) != vessel) )
            return 0;
    for( int pid = 0; pid < TORIRSSERVER_PLAYER_MAX; ++pid )
    {
        struct ToriRSServerPlayer* player = &srv->players[pid];
        vessel->navigator_generation[pid] = (mask & (1u << pid)) ? player->login_generation : 0;
        if( player->active && player->navigating_vessel == vessel->index &&
            player->navigating_vessel_serial == vessel->serial &&
            !ToriRSServer_VesselCanNavigate(srv, player, vessel) )
            player->navigating_vessel = player->navigating_vessel_serial = 0;
    }
    return 1;
}

int
ToriRSServer_VesselCargoAllowed(struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player, const struct ToriRSServerVessel* vessel)
{
    assert(srv);
    assert(player);
    assert(vessel);
    if( vessel->owner_uid == player->pid + 1 ) return 1;
    int pid = vessel->owner_uid - 1;
    if( pid < 0 || pid >= TORIRSSERVER_PLAYER_MAX || !srv->players[pid].active ) return 0;
    int bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "settings_cargo_hold_privacy");
    int privacy = bit >= 0 ? ToriRSServer_VarbitGet(&srv->players[pid], bit) : 0;
    /* Native enum244: Navigators, All players, No players. The default grants
     * only the captain until another player receives explicit navigation. */
    return privacy == 1 || (privacy == 0 && ToriRSServer_VesselCanNavigate(srv, player, vessel));
}

static int
vessel_owned_varbit(struct ToriRSServerPlayer* player, int slot, const char* suffix)
{
    char key[96];
    assert(player);
    assert(suffix);
    snprintf(key, sizeof(key), "sailing_boat_%d_%s", slot, suffix);
    int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, key);
    return id >= 0 ? ToriRSServer_VarbitGet(player, id) : 0;
}

static void
vessel_forget_instance(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int instance)
{
    assert(srv);
    assert(player);
    int varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "map_instance_handle");
    if( varp >= 0 && player->varps[varp] == instance )
        ToriRSServer_WorldSetVarpOn(srv, player, varp, 0);
}

static void
vessel_evacuate_player(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldSetActive(srv, player);
    struct ToriRSServerVessel* aboard = ToriRSServer_VesselAtTile(srv, player->x, player->z);
    if( aboard ) vessel_forget_instance(srv, player, aboard->instance);
    if( !ToriRSServer_VesselDisembarkPlayer(srv, player) )
    {
        int x = player->sailing.shore_x;
        int z = player->sailing.shore_z;
        int level = player->sailing.shore_level;
        /* The real post-Pandemonium shipwright's shore is the fallback for
         * legacy saves/debug boats which have never passed a gangplank. */
        if( x <= 0 || x >= 6400 || z <= 0 || z >= 16384 || level != 0 )
        { x = 3059; z = 2979; level = 0; }
        ToriRSServer_WorldTeleport(srv, level, x, z);
        /* A formerly safe tile may now carry a blocking dynamic loc. Search
         * the built shore window rather than trusting a saved collision flag. */
        int found = !ToriRSServer_SceneWalkBlocked(level, x, z) &&
                    !ToriRSServer_VesselTileSailable(level, x, z);
        for( int r = 1; r <= 12 && !found; ++r )
            for( int dz = -r; dz <= r && !found; ++dz )
                for( int dx = -r; dx <= r && !found; ++dx )
                    if( (abs(dx) == r || abs(dz) == r) &&
                        !ToriRSServer_SceneWalkBlocked(level, x + dx, z + dz) &&
                        !ToriRSServer_VesselTileSailable(level, x + dx, z + dz) )
                    {
                        ToriRSServer_WorldTeleport(srv, level, x + dx, z + dz);
                        found = 1;
                    }
        if( !found ) ToriRSServer_WorldTeleport(srv, 0, 3059, 2979);
    }
    player->navigating_vessel = 0;
    player->navigating_vessel_serial = 0;
    player->sailing.aboard_slot = 0;
    ToriRSServer_WorldInteractionClearAt(player);
    ToriRSServer_ScriptsRunProc(srv, "[proc,sailing_evacuated]", NULL, 0);
}

void
ToriRSServer_VesselCapturePlayer(const struct ToriRSServerPlayer* player,
                               struct ToriRSServerSailingSave* out)
{
    assert(player);
    assert(out);
    *out = player->sailing;
    if( !player->world ) return;
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
    {
        const struct ToriRSServerVessel* vessel = &player->world->vessels[i];
        int slot = vessel->cargo_slot;
        if( !vessel->in_use || vessel->owner_uid != player->pid + 1 ||
            slot < 1 || slot > 5 || vessel->config_id < 1 || vessel->config_id > 3 ) continue;
        struct ToriRSServerSavedBoat* saved = &out->boats[slot - 1];
        saved->config_id = vessel->config_id;
        saved->fine_x = vessel->fine_x;
        saved->fine_z = vessel->fine_z;
        saved->angle = vessel->angle;
        saved->hp = vessel->hp;
        saved->name_descriptor = vessel->name_descriptor;
        saved->name_noun = vessel->name_noun;
        saved->anchored = vessel->anchored;
        saved->motes = ToriRSServer_MapInstanceVarGet(vessel->instance, 0);
        for( int h = 0; h < 13; ++h )
            for( int k = 0; k < 2; ++k )
                saved->resources[h][k] = ToriRSServer_MapInstanceVarGet(vessel->instance, 9 + h * 7 + k);
        if( ToriRSServer_VesselAtTile(player->world, player->x, player->z) == vessel )
        {
            int bx = 0, bz = 0;
            ToriRSServer_MapInstanceBase(vessel->instance, &bx, &bz);
            out->aboard_slot = slot;
            out->deck_x = player->x - bx;
            out->deck_z = player->z - bz;
        }
    }
}

void
ToriRSServer_VesselRestoreOwned(struct ToriRSServer* srv, struct ToriRSServerVessel* vessel)
{
    assert(srv);
    assert(vessel);
    struct ToriRSServerPlayer* player = srv->active_player;
    int slot = vessel->cargo_slot;
    if( !player || vessel->owner_uid != player->pid + 1 || slot < 1 || slot > 5 ) return;
    const struct ToriRSServerSavedBoat* saved = &player->sailing.boats[slot - 1];
    int present = saved->config_id == vessel->config_id;
    vessel->hp = present ? saved->hp : vessel_owned_varbit(player, slot, "stored_hp");
    if( !present && vessel_owned_varbit(player, slot, "stored_maxhp") == 0 )
        vessel->hp = vessel->hp_max;
    if( vessel->hp < 0 ) vessel->hp = 0;
    if( vessel->hp > vessel->hp_max ) vessel->hp = vessel->hp_max;
    vessel->name_descriptor = present ? saved->name_descriptor : vessel_owned_varbit(player, slot, "name_2");
    vessel->name_noun = present ? saved->name_noun : vessel_owned_varbit(player, slot, "name_3");
    if( !present ) return;
    vessel->anchored = saved->anchored != 0;
    ToriRSServer_MapInstanceVarSet(vessel->instance, 0, saved->motes);
    for( int h = 0; h < 13; ++h )
        for( int k = 0; k < 2; ++k )
            ToriRSServer_MapInstanceVarSet(vessel->instance, 9 + h * 7 + k, saved->resources[h][k]);
    /* Filled kegs and dropped anchors choose different native LOCs. The first
     * furnishing pass had empty registers, so apply their real appearances
     * after restoring resources instead of leaving usable contents invisible. */
    int32_t args[1] = { vessel->index };
    ToriRSServer_ScriptsRunProc(srv, "[proc,sailing_facilities_restore]", args, 1);
}

void
ToriRSServer_VesselLogout(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    struct ToriRSServerPlayer* previous = srv->active_player;
    struct ToriRSServerSailingSave saved;
    ToriRSServer_VesselCapturePlayer(player, &saved);
    /* Guest work leases also use pid+1. Retain the resources but retire each
     * matching job generation before that pid can name an unrelated login. */
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
        if( srv->vessels[i].in_use )
        {
            srv->vessels[i].navigator_generation[player->pid] = 0;
            for( int h = 0; h < 13; ++h )
            {
                int instance = srv->vessels[i].instance, base = 8 + h * 7;
                if( ToriRSServer_MapInstanceVarGet(instance, base + 5) == player->pid + 1 )
                {
                    ToriRSServer_MapInstanceVarSet(instance, base, 0);
                    ToriRSServer_MapInstanceVarSet(instance, base + 5, 0);
                    uint32_t generation = (uint32_t)ToriRSServer_MapInstanceVarGet(instance, base + 6) + 1u;
                    ToriRSServer_MapInstanceVarSet(instance, base + 6, (int)(generation ? generation : 1));
                }
            }
        }
    /* Free all owned vessels, including boats whose captain logged out ashore.
     * Otherwise a later login reusing pid+1 inherits those live boats. */
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
        if( srv->vessels[i].in_use && srv->vessels[i].owner_uid == player->pid + 1 )
            ToriRSServer_VesselFree(srv, srv->vessels[i].index);
    /* A guest cannot resume an instance owned by another login. Its captain's
     * boat remains intact, and the guest returns to their own boarding shore. */
    if( ToriRSServer_VesselAtTile(srv, player->x, player->z) )
        vessel_evacuate_player(srv, player);
    player->sailing = saved;
    player->navigating_vessel = player->navigating_vessel_serial = 0;
    ToriRSServer_WorldSetActive(srv, previous);
}

void
ToriRSServer_VesselLogin(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    int slot = player->sailing.aboard_slot;
    if( slot < 1 || slot > 5 ) return;
    struct ToriRSServerSavedBoat saved = player->sailing.boats[slot - 1];
    int deck_x = player->sailing.deck_x, deck_z = player->sailing.deck_z;
    int shore_x = player->x, shore_z = player->z, shore_level = player->level;
    player->sailing.aboard_slot = 0;
    if( saved.config_id < 1 || saved.config_id > 3 ||
        !vessel_owned_varbit(player, slot, "owned") ||
        vessel_owned_varbit(player, slot, "type") + 1 != saved.config_id ||
        saved.fine_x < 0 || saved.fine_x >= 16384 * 128 ||
        saved.fine_z < 0 || saved.fine_z >= 16384 * 128 ) return;
    ToriRSServer_WorldSetActive(srv, player);
    /* Build the actual ocean collision window before validating the restored
     * footprint. Login has not emitted its first scene packet yet. */
    ToriRSServer_WorldTeleport(srv, 0, saved.fine_x >> 7, saved.fine_z >> 7);
    static const int widths[] = {1, 2, 3}, lengths[] = {3, 5, 10};
    int handle = ToriRSServer_VesselSpawn(srv, saved.config_id, widths[saved.config_id - 1],
        lengths[saved.config_id - 1], 0, saved.fine_x >> 7, saved.fine_z >> 7, saved.angle);
    struct ToriRSServerVessel* vessel = ToriRSServer_VesselGet(srv, handle);
    if( vessel )
    {
        vessel->owner_uid = player->pid + 1;
        vessel->cargo_slot = slot;
        vessel->fine_x = saved.fine_x;
        vessel->fine_z = saved.fine_z;
        if( ToriRSServer_VesselBuildPlayerDeck(srv, handle) &&
            ToriRSServer_VesselBoardPlayer(srv, player, vessel) )
        {
            int bx = 0, bz = 0;
            ToriRSServer_MapInstanceBase(vessel->instance, &bx, &bz);
            /* Saved offsets are relative to a rebuilt hull, never pool ids.
             * A changed model/collision layout falls back to normal boarding. */
            int tx = bx + deck_x, tz = bz + deck_z;
            if( deck_x >= 0 && deck_x < 16 && deck_z >= 0 && deck_z < 16 &&
                ToriRSServer_MapInstanceFind(tx, tz) == vessel->instance &&
                !ToriRSServer_SceneWalkBlocked(ToriRSServer_VesselDeckPlane(vessel), tx, tz) )
                ToriRSServer_WorldTeleport(srv, ToriRSServer_VesselDeckPlane(vessel), tx, tz);
            vessel->sails_set = vessel->reversing = 0;
            ToriRSServer_VesselStop(vessel);
            return;
        }
        ToriRSServer_VesselFree(srv, handle);
    }
    /* Full instance pool or changed coastline: keep every owned record/cargo,
     * return to the saved shore, and let the shipwright retrieve the vessel. */
    ToriRSServer_WorldTeleport(srv, shore_level, shore_x, shore_z);
    if( ToriRSServer_SceneWalkBlocked(shore_level, shore_x, shore_z) ||
        ToriRSServer_VesselTileSailable(shore_level, shore_x, shore_z) )
        vessel_evacuate_player(srv, player);
}
