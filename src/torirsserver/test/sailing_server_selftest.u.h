/* Shared by the complete world selftest and SAILING_ONLY. Keep the real
 * cache/mover/packet assertions in one place so the focused run cannot drift. */
static void
selftest_sailing(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    fprintf(stderr,
            "ToriRSServer selftest: two far-apart players each hold their own window\n");
    {
        /*
         * The point of the per-player scene window (docs/SAILING_PLAN.md S0).
         * Before it the world had ONE 104x104 collision window: a second
         * player more than a scene away either stood on ground the server had
         * no collision for at all, or the window recentred on whoever acted
         * last and thrashed back and forth every time both of them moved. So
         * the row is exactly that shape — two players more than 70 tiles
         * apart — and the claim is that BOTH route against correct local
         * collision, from windows that hold still.
         */
        struct ToriRSServerPlayer* far_player = ToriRSServer_WorldAddPlayer(srv, NULL);

        SELFTEST_CHECK(far_player != NULL, "a second player joins to hold the far window");
        if( far_player )
        {
            int path_x[TORIRSSERVER_STEP_MAX];
            int path_z[TORIRSSERVER_STEP_MAX];
            int dist_x;
            int dist_z;
            int dist;

            ToriRSServer_WorldPlayerInit(far_player);

            /* The suite's player keeps their home window; the second player
             * parks in Draynor. `selftest_park_player` parks the ACTIVE
             * player and ends in maybe_rebuild, which builds every owed
             * window — so after the second park both windows stand. */
            selftest_park_player(srv, 3222, 3218);
            ToriRSServer_WorldSetActive(srv, far_player);
            selftest_park_player(srv, 3093, 3244);

            dist_x = abs(far_player->x - player->x);
            dist_z = abs(far_player->z - player->z);
            dist = dist_x > dist_z ? dist_x : dist_z;
            SELFTEST_CHECK(dist > 70,
                           "the fixture has to separate them by more than half a scene, "
                           "got %d", dist);

            SELFTEST_CHECK(
                ToriRSServer_SceneWindowBuilt(ToriRSServer_PlayerSceneWindow(player)),
                "the home player's window should be built");
            SELFTEST_CHECK(
                ToriRSServer_SceneWindowBuilt(ToriRSServer_PlayerSceneWindow(far_player)),
                "and the far player's own, at the same time");

            /* Each window holds its own player and cannot also hold the
             * other: 70+ tiles does not fit one 104-tile window with both
             * players inside the rebuild margin. Asked per WINDOW —
             * ToriRSServer_SceneContains would say yes for both tiles here,
             * because it answers for ANY built window, which is the point of
             * the whole change. */
            SELFTEST_CHECK(
                ToriRSServer_SceneWindowContains(
                    ToriRSServer_PlayerSceneWindow(far_player), far_player->x, far_player->z),
                "the far player's own tile is inside their window");
            SELFTEST_CHECK(!ToriRSServer_SceneWindowContains(
                               ToriRSServer_PlayerSceneWindow(far_player), player->x, player->z),
                           "which cannot also hold the home player's tile");
            SELFTEST_CHECK(
                ToriRSServer_SceneWindowContains(
                    ToriRSServer_PlayerSceneWindow(player), player->x, player->z),
                "the home player's window holds the home player");
            SELFTEST_CHECK(
                !ToriRSServer_SceneWindowContains(
                    ToriRSServer_PlayerSceneWindow(player), far_player->x, far_player->z),
                "and not the far ground");
            /* And the tile-level union view agrees both are in SOME scene —
             * this is what npc logic far from the acting player leans on. */
            SELFTEST_CHECK(ToriRSServer_SceneContains(player->x, player->z) &&
                               ToriRSServer_SceneContains(far_player->x, far_player->z),
                           "both tiles are covered by the union of windows");

            /* Correct collision is a route, not a flag: find open ground near
             * the far player and require a legal walk to it, judged by the
             * far window's own collision (the courtyard stanza's measure). */
            {
                struct CollisionMap* cm = ToriRSServer_SceneCollision(0);
                int routed = 0;

                SELFTEST_CHECK(cm != NULL, "the far window should have collision for level 0");
                for( int x = far_player->x - 12; cm && x <= far_player->x + 12 && !routed; x++ )
                {
                    for( int z = far_player->z - 12; z <= far_player->z + 12; z++ )
                    {
                        int dx = x - far_player->x;
                        int dz = z - far_player->z;

                        if( dx * dx + dz * dz < 9 )
                            continue;
                        if( !ToriRSServer_SceneContains(x, z) ||
                            collision_map_tile(cm, x - ToriRSServer_SceneBaseX(),
                                               z - ToriRSServer_SceneBaseZ()) !=
                                COLL_FLAG_OPEN )
                            continue;
                        if( ToriRSServer_SceneRoute(0, far_player->x, far_player->z, x, z,
                                                path_x, path_z, TORIRSSERVER_STEP_MAX) < 1 )
                            continue;
                        routed = 1;
                        break;
                    }
                }
                SELFTEST_CHECK(routed, "the far player routes on their own local ground");
            }

            /* And the home player's window still answers for home, untouched
             * by everything the far player just did. */
            ToriRSServer_WorldSetActive(srv, player);
            SELFTEST_CHECK(ToriRSServer_SceneRoute(0, 3222, 3218, 3234, 3226, path_x,
                                               path_z, TORIRSSERVER_STEP_MAX) > 0,
                           "the courtyard route still exists in it");

            /* No thrash: neither window moves while both players stand still.
             * Under the one-window model this is exactly what could not hold —
             * every rebuild for one player moved the ground out from under the
             * other. */
            {
                int home_zone_x = player->zone_x;
                int home_zone_z = player->zone_z;
                int far_zone_x = far_player->zone_x;
                int far_zone_z = far_player->zone_z;
                uint64_t home_builds = ToriRSServer_SceneWindowBuildCount(ToriRSServer_PlayerSceneWindow(player));
                uint64_t far_builds = ToriRSServer_SceneWindowBuildCount(ToriRSServer_PlayerSceneWindow(far_player));

                for( int i = 0; i < 5; i++ )
                    ToriRSServer_WorldTick(srv);
                SELFTEST_CHECK(player->zone_x == home_zone_x &&
                                   player->zone_z == home_zone_z,
                               "five ticks with both standing still move nobody's window");
                SELFTEST_CHECK(far_player->zone_x == far_zone_x &&
                                   far_player->zone_z == far_zone_z,
                               "the far one included");
                SELFTEST_CHECK(home_builds == ToriRSServer_SceneWindowBuildCount(ToriRSServer_PlayerSceneWindow(player)) &&
                               far_builds == ToriRSServer_SceneWindowBuildCount(ToriRSServer_PlayerSceneWindow(far_player)),
                               "neither window is reconstructed at the same centre while stationary");
            }

            /* The free releases the far window (the WorldPlayerFree choke
             * point owns that) and the roster sync retires Draynor's spawns
             * with it. */
            ToriRSServer_WorldSetActive(srv, player);
            ToriRSServer_WorldPlayerFree(srv, far_player->pid);
            ToriRSServer_WorldPlayerReap(srv);
            selftest_park_player(srv, 3222, 3218);
            player->rebuild_pending = 0;
        }
    }

    fprintf(stderr, "ToriRSServer selftest: a vessel spawns and its deck projection round-trips\n");
    {
        /* Real revision-239 ocean: map m48_49, full sea overlays, no artificial
         * floor stamp. Boats and walkers read separate maps of these tiles. */
        int instances_before = ToriRSServer_MapInstanceLiveCount();
        int patch_x = 3072;
        int patch_z = 3160;
        int patch_found = 1;
        int handle = 0;
        struct ToriRSServerVessel* vessel = NULL;

        selftest_park_player(srv, patch_x, patch_z);
        for( int dx = -8; dx <= 8; dx++ )
            for( int dz = -8; dz <= 8; dz++ )
                if( !ToriRSServer_VesselTileSailable(0, patch_x + dx, patch_z + dz) ||
                    !ToriRSServer_SceneWalkBlocked(0, patch_x + dx, patch_z + dz) )
                    patch_found = 0;
        SELFTEST_CHECK(patch_found,
                       "the real ocean's 17x17 area permits boats and blocks walking");
        SELFTEST_CHECK(ToriRSServer_SceneBoatCollision(0) != ToriRSServer_SceneCollision(0),
                       "boat and player collision maps have independent storage");
        SELFTEST_CHECK(!ToriRSServer_VesselTileSailable(1, patch_x, patch_z),
                       "empty sky over the sea is not another ocean");

        if( patch_found )
        {
            handle = ToriRSServer_VesselSpawn(srv, 1, 2, 3, 0, patch_x, patch_z, 0);
            SELFTEST_CHECK(handle > 0, "a 2x3 vessel spawns on it");
            vessel = ToriRSServer_VesselGet(srv, handle);
            SELFTEST_CHECK(vessel != NULL, "and is addressable by its handle");
        }
        if( vessel )
        {
            /* The projection's constants for a 2x3 hull spawned as CONFIG 1:
             * the recenter is the CLIENT's — half the ZONE-ROUNDED deck box
             * (2x3 tiles reserve 1x1 zones = 8x8 tiles -> 512 fine) plus the
             * config's authored pivot (archive-72 ops 4/5; config 1 carries
             * -64,-64) — because the wire publishes zone counts and the
             * client's descent recenters by them, so the server projecting
             * with the raw hull size drew every rider (zones*8 - size)/2
             * tiles away from where it thought they stood. */
            const int pivot_x = 8 * 64 - 64;
            const int pivot_z = 8 * 64 - 64;
            static const int k_angles[] = { 0, 512, 1024, 1536, 256, 300, 1877 };
            static const int k_deck[][2] = {
                { 64, 64 }, { 192, 320 }, { 32, 160 }, { 128, 0 }
            };
            int base_tile_x = 0;
            int base_tile_z = 0;
            int fx;
            int fz;

            SELFTEST_CHECK(ToriRSServer_VesselLiveCount(srv) == 1,
                           "the pool holds exactly the one vessel");
            SELFTEST_CHECK(vessel->instance > 0 &&
                               ToriRSServer_MapInstanceBase(
                                   vessel->instance, &base_tile_x, &base_tile_z),
                           "the vessel owns a live deck instance");
            SELFTEST_CHECK(ToriRSServer_MapInstanceLiveCount() == instances_before + 1,
                           "allocated out of the shared reservation pool");

            /* The pivot projects to the hull's own position at EVERY angle,
             * exactly — rotation about anywhere else would drag boarded
             * players sideways whenever the hull turned. */
            for( int i = 0; i < (int)(sizeof(k_angles) / sizeof(k_angles[0])); i++ )
            {
                vessel->angle = k_angles[i];
                ToriRSServer_VesselDeckToRoot(vessel, pivot_x, pivot_z, &fx, &fz);
                SELFTEST_CHECK(fx == vessel->fine_x && fz == vessel->fine_z,
                               "the pivot maps to the hull position at angle %d",
                               k_angles[i]);
            }

            /* Deck -> root -> deck round-trips stay within two fine units.
             * The actual client uses a truncated angular constant, including
             * its cardinal entries — both directions FLOOR (no rounding term),
             * deliberately: the client's Wev_ParentFromDeck/DeckFromParent
             * floor over the same truncated table, and landing on the same
             * tile as the client outranks internal round-trip exactness. */
            for( int i = 0; i < (int)(sizeof(k_angles) / sizeof(k_angles[0])); i++ )
            {
                int tolerance = 2;

                vessel->angle = k_angles[i];
                for( int j = 0; j < (int)(sizeof(k_deck) / sizeof(k_deck[0])); j++ )
                {
                    int back_x;
                    int back_z;

                    ToriRSServer_VesselDeckToRoot(vessel, k_deck[j][0], k_deck[j][1], &fx, &fz);
                    ToriRSServer_VesselRootToDeck(vessel, fx, fz, &back_x, &back_z);
                    SELFTEST_CHECK(abs(back_x - k_deck[j][0]) <= tolerance &&
                                       abs(back_z - k_deck[j][1]) <= tolerance,
                                   "deck (%d,%d) round-trips at angle %d, got (%d,%d)",
                                   k_deck[j][0], k_deck[j][1], k_angles[i], back_x, back_z);
                }
            }

            /* Direction: one tile bow-ward of the pivot lands one tile SOUTH
             * of the hull at angle 0 and one tile WEST at 512 — the same
             * (-sin, -cos) the mover advances along, so the deck and the
             * wake cannot disagree about which way the boat points. */
            vessel->angle = 0;
            ToriRSServer_VesselDeckToRoot(vessel, pivot_x, pivot_z - 128, &fx, &fz);
            SELFTEST_CHECK(fx == vessel->fine_x && fz == vessel->fine_z - 128,
                           "the bow points south at angle 0, got (%d,%d)", fx, fz);
            vessel->angle = 512;
            ToriRSServer_VesselDeckToRoot(vessel, pivot_x, pivot_z - 128, &fx, &fz);
            SELFTEST_CHECK(fx == vessel->fine_x - 128 && fz == vessel->fine_z,
                           "and west at angle 512, got (%d,%d)", fx, fz);

            /* A boarded player's ABSOLUTE deck tile projects through the
             * instance base: the deck's south-west tile center sits at
             * (64,64) deck-fine, i.e. recenter-minus-64 west and south of
             * the hull at angle 0. */
            vessel->angle = 0;
            ToriRSServer_VesselDeckTileToRoot(vessel, base_tile_x, base_tile_z, &fx, &fz);
            SELFTEST_CHECK(fx == vessel->fine_x - (pivot_x - 64) &&
                               fz == vessel->fine_z - (pivot_z - 64),
                           "the deck's south-west tile projects half a deck box away, got (%d,%d)",
                           fx, fz);
        }

        fprintf(stderr, "ToriRSServer selftest: native heading requires the current helm\n");
        if( vessel )
        {
            int base_tile_x = 0, base_tile_z = 0;
            int saved_x = player->x, saved_z = player->z;
            int saved_nav = player->navigating_vessel;
            int saved_serial = player->navigating_vessel_serial;
            SELFTEST_CHECK(ToriRSServer_MapInstanceBase(vessel->instance, &base_tile_x, &base_tile_z),
                           "heading fixture has a live deck");
            selftest_park_player(srv, base_tile_x + 4, base_tile_z + 4);
            player->navigating_vessel = 0;
            vessel->heading = 0;
            uint8_t heading = 12;
            selftest_handle(player, PKTOUT_NAME_SET_HEADING, &heading, 1);
            SELFTEST_CHECK(vessel->heading == 0, "a passenger cannot steer with SET_HEADING");
            player->navigating_vessel = vessel->index;
            player->navigating_vessel_serial = vessel->serial;
            for( heading = 0; heading < 16; ++heading )
            {
                selftest_handle(player, PKTOUT_NAME_SET_HEADING, &heading, 1);
                SELFTEST_CHECK(vessel->heading == heading, "native compass heading %u reaches the hull", heading);
            }
            heading = 255;
            selftest_handle(player, PKTOUT_NAME_SET_HEADING, &heading, 1);
            SELFTEST_CHECK(vessel->heading == 15, "malformed headings are rejected without wrapping");
            selftest_park_player(srv, saved_x, saved_z);
            heading = 4;
            selftest_handle(player, PKTOUT_NAME_SET_HEADING, &heading, 1);
            SELFTEST_CHECK(vessel->heading == 15, "a stale helm after leaving the deck cannot steer");
            player->navigating_vessel = saved_nav;
            player->navigating_vessel_serial = saved_serial;
        }

        fprintf(stderr,
                "ToriRSServer selftest: a rider reaches across the gunwale from the projected tile\n");
        if( vessel )
        {
            int base_tile_x = 0;
            int base_tile_z = 0;
            int deck_x;
            int deck_z;
            int proj_x = 0;
            int proj_z = 0;
            int reach_x = 0;
            int reach_z = 0;
            int goblin;
            int saved_x = player->x;
            int saved_z = player->z;
            int saved_level = player->level;

            vessel->angle = 0;
            vessel->fine_x = patch_x * 128 + 64;
            vessel->fine_z = patch_z * 128 + 64;
            SELFTEST_CHECK(
                ToriRSServer_MapInstanceBase(vessel->instance, &base_tile_x, &base_tile_z),
                "the reach fixture reads the deck base");
            /* Board at the deck-box centre — the tile ::vesselboard picks. */
            deck_x = base_tile_x + ((vessel->size_x_tiles + 7) / 8) * 4;
            deck_z = base_tile_z + ((vessel->size_z_tiles + 7) / 8) * 4;
            selftest_park_player(srv, deck_x, deck_z);
            player->level = 0;
            SELFTEST_CHECK(ToriRSServer_VesselAtTile(srv, player->x, player->z) == vessel,
                           "the rider is aboard for the reach seam");

            /*
             * The reach seam itself (ToriRSServer_PlayerReachTile): judged
             * against anything OFF the deck the rider answers with the
             * projected tile — the deck tile pushed through the hull — and
             * against a point on their OWN deck, with the raw pool tile.
             * This is what lets a rider fish, shoot and melee over the
             * gunwale while two things on one deck still measure in the
             * deck's own space.
             */
            ToriRSServer_PlayerSceneAnchor(srv, player, &proj_x, &proj_z);
            SELFTEST_CHECK(proj_x >= patch_x - 8 && proj_x <= patch_x + 8 &&
                               proj_z >= patch_z - 8 && proj_z <= patch_z + 8,
                           "the projected tile is at the hull, got %d,%d", proj_x, proj_z);
            ToriRSServer_PlayerReachTile(srv, player, proj_x + 1, proj_z, &reach_x, &reach_z);
            SELFTEST_CHECK(reach_x == proj_x && reach_z == proj_z,
                           "reach against a shore point answers the projected tile");
            ToriRSServer_PlayerReachTile(srv, player, deck_x, deck_z, &reach_x, &reach_z);
            SELFTEST_CHECK(reach_x == player->x && reach_z == player->z,
                           "and against the own deck, the raw pool tile");

            /* A melee attack across the gunwale: target one tile off the
             * projected position — thousands of tiles from the rider's FEET,
             * which is exactly what made this impossible before the seam. */
            goblin = ToriRSServer_WorldNpcSpawn(srv, 1, proj_x + 1, proj_z, 0);
            SELFTEST_CHECK(goblin >= 0, "a goblin stands one tile off the gunwale");
            if( goblin >= 0 )
            {
                struct ToriRSServerNpc* npc = &srv->npcs[goblin];
                const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfo(npc->type);
                int saved_god = player->godmode;

                player->godmode = 1;
                /* The latch processes instead of giving up: before the seam,
                 * distance-to-target from the rider's FEET was thousands of
                 * tiles, and continue_or_give_up dropped the interaction with
                 * cannot_reach on the first tick. */
                ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_NPC, 2, goblin,
                                              npc->type, npc->x, npc->z, npc->level,
                                              info->size, info->size);
                selftest_tick(srv);
                SELFTEST_CHECK(player->face_entity == goblin,
                               "the latched attack faces the shore npc, got %d",
                               player->face_entity);
                ToriRSServer_WorldClearPendingAction(srv);

                /* Engaged combat HOLDS: the per-tick range check measures
                 * from the projected tile, so the fight neither disengages
                 * nor tries to walk the rider off the deck. */
                ToriRSServer_CombatEngage(srv, goblin);
                selftest_tick(srv);
                SELFTEST_CHECK(player->combat_target == goblin,
                               "combat engages across the gunwale, got %d",
                               player->combat_target);
                selftest_tick(srv);
                selftest_tick(srv);
                SELFTEST_CHECK(player->combat_target == goblin,
                               "and holds at range from the projected tile, got %d",
                               player->combat_target);
                player->godmode = saved_god;
                ToriRSServer_WorldClearPendingAction(srv);
                ToriRSServer_WorldNpcFree(srv, goblin);
            }

            /* The RANGED half: a weapon that out-ranges melee counts as
             * REACHED from the projected tile (ToriRSServer_CombatAtRangeReady)
             * so the OP dispatch — where content's combat loop swings — runs
             * with no walk to adjacency, which a rider cannot perform across
             * the gunwale. The engine has no attack clock, so with no script
             * pack loaded here nothing engages; what the engine owns and this
             * pins is the reach verdict and the dispatch-without-a-step. Four
             * tiles off the hull is out of melee reach and inside the
             * crossbow's five. */
            {
                int xbow =
                    ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "phoenix_crossbow");
                int far_goblin =
                    ToriRSServer_WorldNpcSpawn(srv, 1, proj_x + 4, proj_z, 0);

                SELFTEST_CHECK(xbow >= 0, "the ranged fixture's crossbow resolves");
                SELFTEST_CHECK(far_goblin >= 0, "a goblin stands four tiles off the gunwale");
                if( xbow >= 0 && far_goblin >= 0 )
                {
                    struct ToriRSServerNpc* npc = &srv->npcs[far_goblin];
                    const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfo(npc->type);
                    int saved_weapon = player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id;
                    int saved_god = player->godmode;

                    player->godmode = 1;
                    player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id = xbow;
                    SELFTEST_CHECK(ToriRSServer_CombatAtRangeReady(srv, far_goblin),
                                   "the crossbow is at-range ready across the gunwale");
                    ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_NPC, 2, far_goblin,
                                                  npc->type, npc->x, npc->z, npc->level,
                                                  info->size, info->size);
                    selftest_tick(srv);
                    SELFTEST_CHECK(player->interaction.kind == TORIRSSERVER_INTERACT_NONE,
                                   "the attack dispatched as reached, got kind %d",
                                   player->interaction.kind);
                    SELFTEST_CHECK(player->x == deck_x && player->z == deck_z,
                                   "without the rider taking a single step, at %d,%d",
                                   player->x, player->z);
                    player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id = saved_weapon;
                    player->godmode = saved_god;
                    ToriRSServer_WorldClearPendingAction(srv);
                    ToriRSServer_WorldNpcFree(srv, far_goblin);
                }
            }
            selftest_park_player(srv, saved_x, saved_z);
            player->level = saved_level;
        }

        fprintf(stderr,
                "ToriRSServer selftest: the mover turns under the cap and steps on the quantum\n");
        if( vessel )
        {
            vessel->angle = 0;
            vessel->fine_x = patch_x * 128 + 64;
            vessel->fine_z = patch_z * 128 + 64;
            ToriRSServer_VesselSetSpeed(vessel, 1);
            ToriRSServer_VesselSetHeading(vessel, 8);
            /* These rows test the MOVER; the launch-model sail gate
             * (vessel.sails_set) is the helm's concern, so hoist it. */
            vessel->sails_set = 1;

            /* South to north is a half turn: 1024 units at the default cap of
             * 128 per tick is exactly eight ticks, every intermediate angle a
             * multiple of the cap, every intermediate position still on the
             * 32-unit quantum (the hull sails while it turns). */
            for( int i = 1; i <= 8; i++ )
            {
                ToriRSServer_VesselTickAll(srv);
                SELFTEST_CHECK(vessel->angle == i * 128,
                               "tick %d turns to exactly %d, got %d", i, i * 128,
                               vessel->angle);
                SELFTEST_CHECK((vessel->fine_x & 31) == 0 && (vessel->fine_z & 31) == 0,
                               "tick %d lands on the quarter-tile quantum, got (%d,%d)", i,
                               vessel->fine_x, vessel->fine_z);
            }
            SELFTEST_CHECK(vessel->state == TORIRSSERVER_VESSEL_HEADING,
                           "a heading sail never parks on its own");
        }

        fprintf(stderr,
                "ToriRSServer selftest: a targeted sail arrives at speed and parks\n");
        if( vessel )
        {
            int ticks = 0;

            ToriRSServer_VesselStop(vessel);
            vessel->angle = 1024;
            vessel->fine_x = patch_x * 128 + 64;
            vessel->fine_z = patch_z * 128 + 64;
            ToriRSServer_VesselSetSpeed(vessel, 2);
            ToriRSServer_VesselSetTarget(vessel, patch_x, patch_z + 4);

            /* Driven through the WHOLE world tick, not TickAll directly: the
             * claim includes that the vessels phase actually runs. */
            while( vessel->state != TORIRSSERVER_VESSEL_IDLE && ticks < 20 )
            {
                ToriRSServer_WorldTick(srv);
                ticks++;
            }
            SELFTEST_CHECK(vessel->state == TORIRSSERVER_VESSEL_IDLE, "the sail ends parked");
            SELFTEST_CHECK(vessel->fine_x == patch_x * 128 + 64 &&
                               vessel->fine_z == (patch_z + 4) * 128 + 64,
                           "exactly on the target tile center, got (%d,%d)", vessel->fine_x,
                           vessel->fine_z);
            /* Four tiles at tier 2 (one tile per tick): three full steps, then
             * the arrival check folds the last tile into the snap. */
            SELFTEST_CHECK(ticks == 4, "and took exactly four ticks, got %d", ticks);
        }

        fprintf(stderr, "ToriRSServer selftest: a vessel refuses to drive onto land\n");
        {
            /* Find a real two-tile sea approach to a north-facing coast in
             * this loaded window; the shoreline comes from the cache. */
            int coast_x = 0;
            int coast_z = 0;
            int coast_found = 0;
            int best_distance = 100000;
            int base_x = ToriRSServer_SceneBaseX();
            int base_z = ToriRSServer_SceneBaseZ();
            for( int x = base_x + 2; x < base_x + 102; x++ )
                for( int z = base_z + 2; z < base_z + 100; z++ )
                {
                    int distance = abs(x - patch_x) + abs(z - patch_z);
                    if( distance < best_distance &&
                        ToriRSServer_VesselTileSailable(0, x, z) &&
                        ToriRSServer_VesselTileSailable(0, x, z + 1) &&
                        !ToriRSServer_VesselTileSailable(0, x, z + 2) )
                    {
                        coast_x = x;
                        coast_z = z;
                        coast_found = 1;
                        best_distance = distance;
                    }
                }

            SELFTEST_CHECK(coast_found, "the arena edge gives a shoreline running north");

            if( coast_found )
            {
                int skiff = ToriRSServer_VesselSpawn(srv, 0, 1, 1, 0, coast_x, coast_z, 1024);
                struct ToriRSServerVessel* boat = ToriRSServer_VesselGet(srv, skiff);

                SELFTEST_CHECK(skiff > 0 && boat != NULL, "a 1x1 vessel spawns at the shore");
                if( boat )
                {
                    ToriRSServer_VesselSetSpeed(boat, 1);
                    ToriRSServer_VesselSetTarget(boat, coast_x, coast_z + 2);
                    for( int i = 0; i < 10; i++ )
                        ToriRSServer_VesselTickAll(srv);

                    SELFTEST_CHECK(boat->state == TORIRSSERVER_VESSEL_IDLE,
                                   "the blocked sail parks");
                    SELFTEST_CHECK((boat->fine_z >> 7) < coast_z + 2,
                                   "short of the land tile, at tile z %d of %d",
                                   boat->fine_z >> 7, coast_z + 2);
                    SELFTEST_CHECK(
                        ToriRSServer_VesselTileSailable(0, boat->fine_x >> 7, boat->fine_z >> 7),
                        "still afloat where it parked");
                    {
                        int parked_x = boat->fine_x;
                        int parked_z = boat->fine_z;

                        ToriRSServer_VesselTickAll(srv);
                        SELFTEST_CHECK(boat->fine_x == parked_x && boat->fine_z == parked_z,
                                       "and stays parked");
                    }
                    ToriRSServer_VesselFree(srv, skiff);
                }
            }
        }

        /* Teardown: handles die, and the deck reservations go back with them
         * (the pool leak check at the end of the suite counts on it). */
        if( handle )
        {
            /*
             * Walk-then-melee across the rail (the off-deck WalkToApproach
             * seam): a melee target past the FAR rail forces the rider to
             * close DECK distance first — the approach walks them to the
             * rail tile nearest the target — and only the projected
             * adjacency from that rail tile lands the swing. Runs LAST in
             * this hull's life because building the deck window rebinds the
             * scene, which the mover stanzas above must not see.
             */
            if( vessel )
            {
                int deck_base_x = 0;
                int deck_base_z = 0;
                int zones_x = 0;
                int zones_z = 0;
                int deck_x;
                int deck_z;
                int proj_x = 0;
                int proj_z = 0;
                int rail_goblin;
                int saved_x = player->x;
                int saved_z = player->z;
                int saved_level = player->level;

                uint32_t saved_rng = srv->rng;

                fprintf(stderr,
                        "ToriRSServer selftest: melee closes deck distance to the rail\n");
                vessel->angle = 0;
                vessel->fine_x = patch_x * 128 + 64;
                vessel->fine_z = patch_z * 128 + 64;
                ToriRSServer_MapInstanceBuild(vessel->instance);
                ToriRSServer_WorldMapInstanceBuilt(srv, vessel->instance);
                ToriRSServer_MapInstanceBase(vessel->instance, &deck_base_x, &deck_base_z);
                ToriRSServer_VesselDeckZones(vessel, &zones_x, &zones_z);
                deck_x = deck_base_x + zones_x * 4;
                deck_z = deck_base_z + zones_z * 4;
                selftest_park_player(srv, deck_x, deck_z);
                player->level = 0;
                ToriRSServer_PlayerSceneAnchor(srv, player, &proj_x, &proj_z);

                rail_goblin = ToriRSServer_WorldNpcSpawn(srv, 1, proj_x - 2, proj_z, 0);
                SELFTEST_CHECK(rail_goblin >= 0, "a goblin stands past the far rail");
                if( rail_goblin >= 0 )
                {
                    struct ToriRSServerNpc* npc = &srv->npcs[rail_goblin];
                    const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfo(npc->type);
                    int saved_god = player->godmode;

                    player->godmode = 1;
                    ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_NPC, 2,
                                                  rail_goblin, npc->type, npc->x, npc->z,
                                                  npc->level, info->size, info->size);
                    for( int t = 0; t < 6 && player->x != deck_x - 1; t++ )
                        selftest_tick(srv);
                    SELFTEST_CHECK(player->x == deck_x - 1 && player->z == deck_z,
                                   "the rider closed deck distance to the west rail, at %d,%d",
                                   player->x, player->z);
                    SELFTEST_CHECK(player->face_entity == rail_goblin,
                                   "and faces the target across it, got %d",
                                   player->face_entity);
                    ToriRSServer_WorldClearPendingAction(srv);
                    /* The engine fight itself holds from the rail: range and
                     * level judged from the projected tile. */
                    ToriRSServer_CombatEngage(srv, rail_goblin);
                    selftest_tick(srv);
                    SELFTEST_CHECK(player->combat_target == rail_goblin,
                                   "engaged melee holds across the rail, got %d",
                                   player->combat_target);
                    player->godmode = saved_god;
                    ToriRSServer_WorldClearPendingAction(srv);
                    ToriRSServer_WorldNpcFree(srv, rail_goblin);
                }
                selftest_park_player(srv, saved_x, saved_z);
                player->level = saved_level;
                /* RNG-neutral: this stanza's goblin AI and swings consume
                 * draws, and a downstream fixture (the running-npc rows)
                 * pins behaviour that flips on the draw count — the exact
                 * brittleness its own comment documents. Restore the seed
                 * so inserting this stanza is invisible to it. */
                srv->rng = saved_rng;
            }

            SELFTEST_CHECK(ToriRSServer_VesselFree(srv, handle) == 1, "the vessel frees");
            SELFTEST_CHECK(ToriRSServer_VesselGet(srv, handle) == NULL,
                           "and its handle is dead");
            SELFTEST_CHECK(ToriRSServer_VesselFree(srv, handle) == 0,
                           "freeing it again answers 0, the deallocator contract");
        }
        SELFTEST_CHECK(ToriRSServer_VesselLiveCount(srv) == 0, "no vessel survives the rows");
        SELFTEST_CHECK(ToriRSServer_MapInstanceLiveCount() == instances_before,
                       "and the deck reservations went back to the pool");

        /*
         * docs/SAILING_PLAN.md S2 — the wire.
         *
         * Same real ocean: the rows above proved the transform, these prove
         * the three packets that put it on a client, and the cross-world
         * placement that lets the shore and the deck see each other.
         *
         * Guarded on the revision because WORLDENTITY_INFO and
         * REBUILD_WORLDENTITY are rev-239 packets that the osrs230 wire
         * REFUSES by omission — its transcribed list does not name them, so
         * `flush` drops them at the opcode lookup. Asserting their presence
         * under `TORIRSSERVER_REV=osrs230` would be asserting that a
         * deliberate refusal is a bug.
         */
        if( patch_found && srv->wire->revision >= 239 )
        {
            static struct ToriRSServerCapture wev_cap;
            struct selftest_wev_packet decoded;
            int hull = 0;
            struct ToriRSServerVessel* boat = NULL;
            int deck_base_x = 0;
            int deck_base_z = 0;
            int zones_x = 0;
            int zones_z = 0;
            int at;

            /*
             * The observer stands on the shore nine tiles east of the arena's
             * centre — inside PLAYER_INFO's own +/-15, so the cross-world row
             * below measures the projection and not the range test.
             */
            selftest_park_player(srv, patch_x + 9, patch_z);
            player->wev_tracked_count = 0;

            /*
             * 6x12 tiles is two zones deep and one wide, so the spawn
             * trailer's size nibbles are 1 and 2 — DIFFERENT, which is the
             * only way the row can tell a swapped pair from a correct one.
             * Centred on the arena (VesselSpawn's tile is the hull's centre,
             * not its corner), leaving two tiles of open ocean past each
             * end for the sail below.
             */
            hull = ToriRSServer_VesselSpawn(srv, 9, 6, 12, 0, patch_x, patch_z, 0);
            boat = ToriRSServer_VesselGet(srv, hull);
            SELFTEST_CHECK(boat != NULL, "a 6x12 hull spawns in the arena");
            if( boat )
            {
                /* Every window derives the same ocean from cache terrain;
                 * boarding and rebuilding need no special water restamp. */
                ToriRSServer_VesselDeckZones(boat, &zones_x, &zones_z);
                SELFTEST_CHECK(zones_x == 1 && zones_z == 2,
                               "whose deck reserves 1x2 zones, got %dx%d", zones_x, zones_z);
                ToriRSServer_MapInstanceBase(boat->instance, &deck_base_x, &deck_base_z);
                /* A real source zone under the whole deck: an unset zone
                 * decodes to "void" and the client draws a hole. */
                for( int zx = 0; zx < zones_x; zx++ )
                    for( int zz = 0; zz < zones_z; zz++ )
                        ToriRSServer_MapInstanceSetchunk(
                            boat->instance, 0, zx, zz, 3216, 3216, 0, 0);
                ToriRSServer_MapInstanceBuild(boat->instance);
                ToriRSServer_WorldMapInstanceBuilt(srv, boat->instance);
            }

            ToriRSServer_CaptureBegin(srv, &wev_cap);
            selftest_tick(srv);
            ToriRSServer_CaptureEnd(srv);

            at = ToriRSServer_CaptureFindNamed(&wev_cap, PKT_NAME_WORLDENTITY_INFO, 0);
            SELFTEST_CHECK(at >= 0, "the spawn tick sends WORLDENTITY_INFO");
            {
                /*
                 * The deck map follows the spawn, never precedes it: the
                 * client's SET_ACTIVE_WORLD asserts that the id names a LIVE
                 * view, and the spawn record is what makes it live. The
                 * trailing select is the return to the root the rest of the
                 * tick's packets are addressed to.
                 */
                static const int k_sandwich[] = {
                    PKT_NAME_WORLDENTITY_INFO,
                    PKT_NAME_SET_ACTIVE_WORLD,
                    PKT_NAME_REBUILD_WORLDENTITY,
                    PKT_NAME_SET_ACTIVE_WORLD,
                };

                SELFTEST_CHECK(
                    ToriRSServer_CaptureHasSequenceNamed(&wev_cap, k_sandwich, 4),
                    "and the deck map after it, inside a set-active-world sandwich");
            }

            if( at >= 0 && boat )
            {
                selftest_wev_decode(
                    wev_cap.packets[at].data, wev_cap.packets[at].len, &decoded);

                SELFTEST_CHECK(decoded.trailing == 0,
                               "the client's own read consumes the packet exactly, "
                               "%d byte(s) left over",
                               decoded.trailing);
                SELFTEST_CHECK(decoded.count == 0,
                               "with no positional records on the spawn tick, got %d",
                               decoded.count);
                SELFTEST_CHECK(decoded.spawn_count == 1,
                               "and exactly one spawn trailer, got %d", decoded.spawn_count);
                if( decoded.spawn_count == 1 )
                {
                    const struct selftest_wev_spawn* spawn = &decoded.spawns[0];

                    SELFTEST_CHECK(spawn->view_id == boat->view_id,
                                   "the trailer names the hull's view id %d, got %d",
                                   boat->view_id, spawn->view_id);
                    SELFTEST_CHECK(spawn->view_id >= 1 && spawn->view_id <= 15,
                                   "which has to be a registry slot the client will accept, "
                                   "got %d",
                                   spawn->view_id);
                    SELFTEST_CHECK(spawn->zones_x == 1 && spawn->zones_z == 2,
                                   "the size nibbles are the deck's zone counts, x first, "
                                   "got %dx%d",
                                   spawn->zones_x, spawn->zones_z);
                    SELFTEST_CHECK(spawn->config_id == 9,
                                   "the config id survives its little-endian transform, got %d",
                                   spawn->config_id);
                    SELFTEST_CHECK(spawn->priority == boat->priority,
                                   "as does the priority group, got %d", spawn->priority);
                    SELFTEST_CHECK(spawn->fine_x == boat->fine_x && spawn->fine_z == boat->fine_z,
                                   "the trailer's transform is the ABSOLUTE position "
                                   "(%d,%d), got (%d,%d)",
                                   boat->fine_x, boat->fine_z, spawn->fine_x, spawn->fine_z);
                    SELFTEST_CHECK(spawn->fine_y == 0, "with no height, got %d", spawn->fine_y);
                    SELFTEST_CHECK(spawn->angle == (boat->angle & 0x7ff),
                                   "and the hull's yaw, got %d", spawn->angle);
                    SELFTEST_CHECK(spawn->update_flags == 0x2,
                                   "flag bit 0x2 announces the op mask, got 0x%x",
                                   spawn->update_flags);
                    SELFTEST_CHECK(spawn->has_op_mask &&
                                       spawn->op_mask == TORIRSSERVER_WEV_OP_MASK_ALL,
                                   "which reads back as all five ops, got %d", spawn->op_mask);
                }
                SELFTEST_CHECK(player->wev_tracked_count == 1 &&
                                   player->wev_view_ids[0] == boat->view_id,
                               "and the observer's list now mirrors the client's, slot 0 "
                               "holding view %d",
                               boat->view_id);
            }

            at = ToriRSServer_CaptureFindNamed(&wev_cap, PKT_NAME_REBUILD_WORLDENTITY, 0);
            SELFTEST_CHECK(at >= 0, "the spawn tick sends the deck's REBUILD_WORLDENTITY");
            if( at >= 0 && boat )
            {
                struct RSAreaBuf rb;
                int got_base_x;
                int got_base_z;
                int got_squares;
                int grid_bits = 0;

                rsab_wrap(&rb, (void*)wev_cap.packets[at].data,
                          (size_t)wev_cap.packets[at].len);
                got_base_x = rsab_g2(&rb);
                got_base_z = rsab_g2(&rb);
                got_squares = rsab_g2(&rb);

                /* The header is the deck's SW TILE — the client asserts it is
                 * zone-aligned and strides its own grid from it. */
                SELFTEST_CHECK(got_base_x == deck_base_x && got_base_z == deck_base_z,
                               "its header is the deck's SW tile (%d,%d), got (%d,%d)",
                               deck_base_x, deck_base_z, got_base_x, got_base_z);
                SELFTEST_CHECK((got_base_x % 8) == 0 && (got_base_z % 8) == 0,
                               "zone-aligned, as the client asserts");
                SELFTEST_CHECK(got_squares == 1,
                               "and names the one source map square the deck came from, got %d",
                               got_squares);

                /*
                 * The grid is the VIEW's extent, not a fixed 13x13: one
                 * presence bit per zone over four levels, plus 26 descriptor
                 * bits for the ones that are set. The client asserts it
                 * consumed exactly this many bytes, so a wrong extent is a
                 * hard stop there rather than a wrong-looking deck.
                 */
                grid_bits = 4 * zones_x * zones_z + 26 * zones_x * zones_z;
                SELFTEST_CHECK(wev_cap.packets[at].len == 6 + (grid_bits + 7) / 8,
                               "the descriptor grid is %d bits over %dx%d zones, so the "
                               "packet is %d bytes, got %d",
                               grid_bits, zones_x, zones_z, 6 + (grid_bits + 7) / 8,
                               wev_cap.packets[at].len);
            }

            /*
             * The sandwich's own addressing: the select before the deck map
             * names the hull's view, and the one after it returns to the root.
             * Read rather than assumed — a rebuild addressed to world 0 would
             * overwrite the player's own scene with the deck.
             */
            at = ToriRSServer_CaptureFindNamed(&wev_cap, PKT_NAME_REBUILD_WORLDENTITY, 0);
            if( at > 0 && boat )
            {
                int before = -1;
                int after = -1;

                for( int i = at - 1; i >= 0; i-- )
                    if( wev_cap.packets[i].name == PKT_NAME_SET_ACTIVE_WORLD )
                    {
                        before = i;
                        break;
                    }
                after = ToriRSServer_CaptureFindNamed(&wev_cap, PKT_NAME_SET_ACTIVE_WORLD, at);
                SELFTEST_CHECK(before >= 0 && after >= 0,
                               "the deck map is bracketed by two world selects");
                if( before >= 0 && after >= 0 )
                {
                    struct RSAreaBuf rb;
                    int selected;
                    int restored;

                    rsab_wrap(&rb, (void*)wev_cap.packets[before].data,
                              (size_t)wev_cap.packets[before].len);
                    selected = rsab_g2(&rb);
                    rsab_wrap(&rb, (void*)wev_cap.packets[after].data,
                              (size_t)wev_cap.packets[after].len);
                    restored = rsab_g2(&rb);
                    SELFTEST_CHECK(selected == boat->view_id,
                                   "the first names the hull's view %d, got %d", boat->view_id,
                                   selected);
                    SELFTEST_CHECK(restored == 0,
                                   "and the second puts the cursor back on the root, got %d",
                                   restored);
                }
            }

            /* Explicit relocations snap once for EACH observer; consuming the
             * stamp for one watcher must not turn another watcher's jump into
             * an ordinary interpolated movement. */
            if( boat )
            {
                struct ToriRSServerPlayer* observer = malloc(sizeof(*observer));
                assert(observer);
                *observer = *player;
                int original_x = boat->fine_x;
                boat->fine_x += 64;
                boat->teleport_stamp++;
                for( int watcher = 0; watcher < 3; watcher++ )
                {
                    ToriRSServer_CaptureBegin(srv, &wev_cap);
                    ToriRSServer_SendWorldEntityInfo(watcher == 1 ? observer : player);
                    ToriRSServer_CaptureEnd(srv);
                    at = ToriRSServer_CaptureFindNamed(&wev_cap, PKT_NAME_WORLDENTITY_INFO, 0);
                    SELFTEST_CHECK(at >= 0, "relocation observer receives entity info");
                    if( at >= 0 )
                    {
                        selftest_wev_decode(wev_cap.packets[at].data,
                                            wev_cap.packets[at].len, &decoded);
                        SELFTEST_CHECK(decoded.count == 1 &&
                                           decoded.moves[0].op == (watcher < 2 ? 3 : 1),
                                       "watcher%d sees one teleport then idle, got op%d", watcher,
                                       decoded.count ? decoded.moves[0].op : -1);
                    }
                }
                boat->fine_x = original_x;
                boat->teleport_stamp++;
                ToriRSServer_CaptureBegin(srv, &wev_cap);
                ToriRSServer_SendWorldEntityInfo(player);
                ToriRSServer_CaptureEnd(srv);
                free(observer);
            }

            /*
             * A hull under way: op 2 every tick, and the deltas SUM to the
             * path. Summing rather than checking one tick is the point — the
             * client's position is the running total of everything this packet
             * ever said, so a per-tick delta measured from the wrong origin
             * looks right for one tick and drifts forever after.
             */
            if( boat )
            {
                int start_x = boat->fine_x;
                int start_z = boat->fine_z;
                int sum_dx = 0;
                int sum_dz = 0;
                int sum_dangle = 0;
                int op2_ticks = 0;
                int start_angle = boat->angle;

                /* Heading 0 is the yaw the hull already carries, so it sails
                 * without turning and stays inside the open ocean. */
                ToriRSServer_VesselSetHeading(boat, 0);
                ToriRSServer_VesselSetSpeed(boat, 1);
                boat->sails_set = 1;
                for( int tick = 0; tick < 3; tick++ )
                {
                    int index;

                    ToriRSServer_CaptureBegin(srv, &wev_cap);
                    selftest_tick(srv);
                    ToriRSServer_CaptureEnd(srv);

                    index = ToriRSServer_CaptureFindNamed(&wev_cap, PKT_NAME_WORLDENTITY_INFO, 0);
                    if( index < 0 )
                        continue;
                    selftest_wev_decode(wev_cap.packets[index].data, wev_cap.packets[index].len,
                                        &decoded);
                    if( decoded.count != 1 || decoded.spawn_count != 0 )
                        continue;
                    if( decoded.moves[0].op != 2 )
                        continue;
                    op2_ticks++;
                    sum_dx += decoded.moves[0].dx;
                    sum_dz += decoded.moves[0].dz;
                    sum_dangle += decoded.moves[0].dangle;
                }
                SELFTEST_CHECK(op2_ticks == 3,
                               "every tick under way carries an op-2 record, got %d of 3",
                               op2_ticks);
                SELFTEST_CHECK(boat->fine_z < start_z,
                               "the hull actually moved (heading 0 sails north), %d -> %d",
                               start_z, boat->fine_z);
                SELFTEST_CHECK(sum_dx == boat->fine_x - start_x && sum_dz == boat->fine_z - start_z,
                               "and the deltas sum to the path (%d,%d), got (%d,%d)",
                               boat->fine_x - start_x, boat->fine_z - start_z, sum_dx, sum_dz);
                SELFTEST_CHECK(sum_dangle == 0,
                               "with no yaw change on a heading it already held, got %d",
                               sum_dangle);
                SELFTEST_CHECK(boat->angle == start_angle, "nor any on the hull itself");
                ToriRSServer_VesselStop(boat);
            }

            /*
             * Cross-world visibility (S2.4, plan risk R1).
             *
             * A player standing on the deck is at the deck INSTANCE's
             * coordinates — hundreds of tiles away in the pool, in a square no
             * shore player's window covers. Without the projection the two are
             * invisible to each other while standing side by side on screen.
             */
            if( boat )
            {
                struct ToriRSServerPlayer* sailor = ToriRSServer_WorldAddPlayer(srv, NULL);

                SELFTEST_CHECK(sailor != NULL, "a second player joins to stand on the deck");
                if( sailor )
                {
                    int deck_x = deck_base_x + boat->size_x_tiles / 2;
                    int deck_z = deck_base_z + boat->size_z_tiles / 2;
                    int expect_fine_x = 0;
                    int expect_fine_z = 0;
                    int sailor_pid;

                    ToriRSServer_WorldPlayerInit(sailor);
                    sailor_pid = sailor->pid;
                    ToriRSServer_WorldSetActive(srv, sailor);
                    ToriRSServer_WorldTeleport(srv, 0, deck_x, deck_z);
                    ToriRSServer_WorldSetActive(srv, player);
                    selftest_tick(srv);

                    ToriRSServer_VesselDeckTileToRoot(
                        boat, deck_x, deck_z, &expect_fine_x, &expect_fine_z);
                    SELFTEST_CHECK(ToriRSServer_VesselAtTile(srv, deck_x, deck_z) == boat,
                                   "the deck tile answers with the hull that owns it");
                    SELFTEST_CHECK(sailor->x == deck_x && sailor->z == deck_z,
                                   "the sailor's OWN position stays the deck tile (%d,%d), "
                                   "got (%d,%d)",
                                   deck_x, deck_z, sailor->x, sailor->z);
                    SELFTEST_CHECK(sailor->obs_x == (expect_fine_x >> 7) &&
                                       sailor->obs_z == (expect_fine_z >> 7),
                                   "while their OBSERVED position is the root projection "
                                   "(%d,%d), got (%d,%d)",
                                   expect_fine_x >> 7, expect_fine_z >> 7, sailor->obs_x,
                                   sailor->obs_z);
                    SELFTEST_CHECK(abs(sailor->obs_x - player->x) <= 15 &&
                                       abs(sailor->obs_z - player->z) <= 15,
                                   "which lands beside the shore player, %d,%d vs %d,%d",
                                   sailor->obs_x, sailor->obs_z, player->x, player->z);
                    /*
                     * The projection has to be LOAD-BEARING, or the rows below
                     * pass on a fixture that never needed them: the two are
                     * only in range once projected. Their feet are in the
                     * map-instance pool, hundreds of squares off the arena.
                     */
                    SELFTEST_CHECK(abs(sailor->x - player->x) > 15 ||
                                       abs(sailor->z - player->z) > 15,
                                   "while their FEET are out of range of each other, "
                                   "%d,%d vs %d,%d",
                                   sailor->x, sailor->z, player->x, player->z);
                    SELFTEST_CHECK(player->player_tracked[sailor_pid] &&
                                   sailor->player_tracked[player->pid],
                                   "revision239 sends both sides of shore/deck visibility");
                    SELFTEST_CHECK(ToriRSServer_PlayerObservable(player, sailor),
                                   "so PLAYER_INFO admits them to the shore player's set");
                    SELFTEST_CHECK(ToriRSServer_PlayerObservable(sailor, player),
                                   "and the shore player to the deck player's");

                    /* Stepping OFF the deck is a jump in the offset, never a
                     * walk step: the same pid moves hundreds of tiles in one
                     * tick as far as PLAYER_INFO's v5 delta state is
                     * concerned. The remove-and-re-add that answers plan risk
                     * R1 is driven by exactly this flag. */
                    ToriRSServer_WorldSetActive(srv, sailor);
                    ToriRSServer_WorldTeleport(srv, 0, patch_x + 9, patch_z + 1);
                    ToriRSServer_WorldSetActive(srv, player);
                    selftest_tick(srv);
                    SELFTEST_CHECK(sailor->obs_x == sailor->x && sailor->obs_z == sailor->z,
                                   "ashore again, observed and own coordinates are one thing");
                    SELFTEST_CHECK(sailor->obs_off_x == 0 && sailor->obs_off_z == 0,
                                   "with no offset left over");
                    SELFTEST_CHECK(ToriRSServer_PlayerObservable(player, sailor),
                                   "and the shore player still has them in view");
                    SELFTEST_CHECK(player->player_tracked[sailor_pid] &&
                                   sailor->player_tracked[player->pid],
                                   "both observers continue tracking after disembarking");

                    ToriRSServer_WorldPlayerFree(srv, sailor_pid);
                    ToriRSServer_WorldPlayerReap(srv);
                }
            }

            /*
             * The two notions of "aboard", and where they part company
             * (docs/sailing_coverage.csv, area `aboard`).
             *
             * The server's notion is membership of the deck INSTANCE: a player
             * is on a hull when `ToriRSServer_VesselAtTile` answers for the
             * tile they stand on, and only the boarding teleport ever puts
             * them on such a tile. The client's is purely geometric — its own
             * root draw position falling inside a hull's footprint
             * (`app_wev_route_point`, src/app.c). Nothing reconciles the two,
             * so they agree only when a player got aboard the server's way.
             *
             * Several rows below pin CURRENT behaviour that is not the WANTED
             * behaviour, and say so where they do. A cost that is pinned is a
             * cost somebody can watch move; a cost described only in a header
             * comment is one the next change silently doubles.
             */
            if( boat )
            {
                struct ToriRSServerPlayer* rider = ToriRSServer_WorldAddPlayer(srv, NULL);

                fprintf(stderr,
                        "ToriRSServer selftest: the two notions of aboard\n");
                SELFTEST_CHECK(rider != NULL, "a rider joins to stand on the deck");
                if( rider )
                {
                    int deck_x = deck_base_x + boat->size_x_tiles / 2;
                    int deck_z = deck_base_z + boat->size_z_tiles / 2;
                    int rider_pid;
                    int own_x;
                    int own_z;
                    int first_obs_x;
                    int first_obs_z;
                    int sail_ticks = 8;
                    int moving_ticks = 0;
                    int jumped_on_moving = 0;
                    int obs_matched = 0;
                    int obs_still_while_hull_moved = 0;
                    int own_moved = 0;

                    ToriRSServer_WorldPlayerInit(rider);
                    rider_pid = rider->pid;
                    ToriRSServer_WorldSetActive(srv, rider);
                    ToriRSServer_WorldTeleport(srv, 0, deck_x, deck_z);
                    ToriRSServer_WorldSetActive(srv, player);

                    /*
                     * Back to the spawn transform before anything is measured.
                     * The wire rows above sailed this hull three ticks north
                     * and the surveyed ocean is 17 tiles across against a hull
                     * 12 tiles long, so from where they left it there is
                     * barely a tick of water ahead of the bow. Assigning the
                     * transform is a fixture reset — the same move the
                     * projection rows make with `vessel->angle` — not a claim
                     * about how a hull is repositioned in play.
                     */
                    boat->fine_x = patch_x * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
                    boat->fine_z = patch_z * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
                    boat->angle = 0;
                    boat->residual_x = 0;
                    boat->residual_z = 0;
                    selftest_tick(srv);

                    SELFTEST_CHECK(ToriRSServer_VesselAtTile(srv, rider->x, rider->z) == boat,
                                   "on a tile the hull owns, which is the whole of the "
                                   "server's definition of aboard");

                    /*
                     * Boarding must not freeze them.
                     *
                     * The boarding teleport lands inside a map instance, so the
                     * tick that carries it used to take the rev-239 branch in
                     * the per-tick scene refresh — REBUILD_REGION, then
                     * `rebuild_scene_pending = 1`, a barrier only
                     * MAP_BUILD_COMPLETE lifts. `phase_players` skips every
                     * player holding one, so until the client answered, the
                     * rider took no step, ran no queue, resumed no script and
                     * swung at nothing: a round trip in play, however long the
                     * load takes on a slow one, and paid on EVERY boarding.
                     *
                     * The branch now excludes vessel decks. Pinned rather than
                     * merely fixed because every row below is about the rider
                     * MOVING, and with the barrier up "a standing rider's own
                     * tile never moves" is also true of a rider who cannot move
                     * at all — it would have gone on passing either way.
                     */
                    SELFTEST_CHECK(rider->rebuild_scene_pending == 0,
                                   "boarding does not arm the rev-239 scene barrier "
                                   "(rebuild_scene_pending=%d) — a vessel deck carries no "
                                   "cutscene zone events to protect and phase_players "
                                   "skips whoever holds one",
                                   rider->rebuild_scene_pending);
                    SELFTEST_CHECK(rider->login_scene_pending == 0,
                                   "nor is the login barrier still up (login_scene_pending"
                                   "=%d)",
                                   rider->login_scene_pending);
                    selftest_tick(srv);

                    own_x = rider->x;
                    own_z = rider->z;
                    first_obs_x = rider->obs_x;
                    first_obs_z = rider->obs_z;

                    /*
                     * A. The hull carries them.
                     *
                     * Measured per tick rather than end to end: a rider whose
                     * observed tile is right at the start and right at the end
                     * and wrong in between is exactly the bug this is for, and
                     * two endpoint reads cannot see it.
                     */
                    ToriRSServer_VesselSetHeading(boat, 0);
                    ToriRSServer_VesselSetSpeed(boat, 1);
                    boat->sails_set = 1;
                    for( int tick = 0; tick < sail_ticks; tick++ )
                    {
                        int hull_fine_x = boat->fine_x;
                        int hull_fine_z = boat->fine_z;
                        int was_obs_x = rider->obs_x;
                        int was_obs_z = rider->obs_z;
                        int expect_fine_x = 0;
                        int expect_fine_z = 0;

                        selftest_tick(srv);

                        ToriRSServer_VesselDeckTileToRoot(
                            boat, rider->x, rider->z, &expect_fine_x, &expect_fine_z);
                        if( rider->obs_x == (expect_fine_x >> 7) &&
                            rider->obs_z == (expect_fine_z >> 7) )
                            obs_matched++;
                        if( rider->x != own_x || rider->z != own_z )
                            own_moved++;
                        if( boat->fine_x != hull_fine_x || boat->fine_z != hull_fine_z )
                        {
                            moving_ticks++;
                            if( rider->obs_jumped )
                                jumped_on_moving++;
                            if( rider->obs_x == was_obs_x && rider->obs_z == was_obs_z )
                                obs_still_while_hull_moved++;
                        }
                    }
                    ToriRSServer_VesselStop(boat);

                    SELFTEST_CHECK(moving_ticks >= 3,
                                   "the arena leaves the bow at least three ticks of "
                                   "water, got %d of %d",
                                   moving_ticks, sail_ticks);
                    SELFTEST_CHECK(own_moved == 0,
                                   "a standing rider's OWN tile never moves — the deck "
                                   "tile is the whole of their position, and %d tick(s) "
                                   "moved it",
                                   own_moved);
                    SELFTEST_CHECK(obs_matched == sail_ticks,
                                   "while their observed tile is that deck tile projected "
                                   "through the hull, on every tick, got %d of %d",
                                   obs_matched, sail_ticks);
                    SELFTEST_CHECK(rider->obs_x != first_obs_x || rider->obs_z != first_obs_z,
                                   "and it travelled: %d,%d -> %d,%d",
                                   first_obs_x, first_obs_z, rider->obs_x, rider->obs_z);

                    /* Visibility coordinates are whole tiles. Hull movement can
                     * change the observation offset with or without crossing a
                     * tile boundary; both cases must occur and partition these
                     * moving ticks. The actual v5 decoder tests below verify
                     * that this diagnostic flag no longer removes players. */
                    SELFTEST_CHECK(jumped_on_moving > 0 &&
                                       jumped_on_moving + obs_still_while_hull_moved ==
                                           moving_ticks,
                                   "observation offset changed on %d of %d moving ticks; "
                                   "the other %d stayed within the same root tile",
                                   jumped_on_moving, moving_ticks, obs_still_while_hull_moved);
                    SELFTEST_CHECK(obs_still_while_hull_moved > 0,
                                   "fine hull motion also occurs without changing the "
                                   "rider visibility tile: %d of %d moving ticks",
                                   obs_still_while_hull_moved, moving_ticks);

                    /*
                     * B. A step taken ON a moving deck.
                     *
                     * The rider's own motion and the hull's have to reach the
                     * observer as ONE position, and the tick they are composed
                     * on is the only place that can happen: the wire's high
                     * resolution section describes a tracked player as walk
                     * steps, and no sequence of steps expresses "walked one
                     * east while the ground moved half a tile north".
                     */
                    {
                        int walk_from_x = rider->x;
                        int walk_from_z = rider->z;
                        int walk_ticks = 4;
                        int walked = 0;
                        int composed = 0;
                        int jumped_while_under_way = 0;
                        int under_way_ticks = 0;
                        int walk_to_x = 0;
                        int walk_to_z = 0;
                        static const int k_step_dx[4] = { 1, -1, 0, 0 };
                        static const int k_step_dz[4] = { 0, 0, 1, -1 };

                        boat->fine_x = patch_x * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
                        boat->fine_z = patch_z * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
                        boat->angle = 0;
                        boat->residual_x = 0;
                        boat->residual_z = 0;
                        selftest_tick(srv);

                        /* Somewhere to walk TO that is still this hull's deck:
                         * a step off the reservation is a different test, and
                         * a different bug. */
                        for( int i = 0; i < 4 && walk_to_x == 0; i++ )
                        {
                            int try_x = rider->x + k_step_dx[i];
                            int try_z = rider->z + k_step_dz[i];

                            if( ToriRSServer_VesselAtTile(srv, try_x, try_z) == boat )
                            {
                                walk_to_x = try_x;
                                walk_to_z = try_z;
                            }
                        }
                        SELFTEST_CHECK(walk_to_x != 0,
                                       "the deck has a neighbouring tile to walk to — a "
                                       "deck with nowhere to stand is a deck nobody can "
                                       "walk");

                        ToriRSServer_WorldSetActive(srv, rider);
                        if( walk_to_x != 0 )
                            ToriRSServer_WorldWalkTo(srv, walk_to_x, walk_to_z);
                        SELFTEST_CHECK(walk_to_x == 0 || rider->waypoint_index >= 0,
                                       "and the route to %d,%d comes back with waypoints — "
                                       "a failed route leaves the queue empty and the rider "
                                       "simply stands there",
                                       walk_to_x, walk_to_z);
                        ToriRSServer_WorldSetActive(srv, player);

                        ToriRSServer_VesselSetHeading(boat, 0);
                        ToriRSServer_VesselSetSpeed(boat, 1);
                        for( int tick = 0; tick < walk_ticks; tick++ )
                        {
                            int hull_fine_x = boat->fine_x;
                            int hull_fine_z = boat->fine_z;
                            int expect_fine_x = 0;
                            int expect_fine_z = 0;

                            selftest_tick(srv);

                            ToriRSServer_VesselDeckTileToRoot(
                                boat, rider->x, rider->z, &expect_fine_x, &expect_fine_z);
                            if( rider->obs_x == (expect_fine_x >> 7) &&
                                rider->obs_z == (expect_fine_z >> 7) )
                                composed++;
                            if( rider->x != walk_from_x || rider->z != walk_from_z )
                                walked = 1;
                            if( boat->fine_x != hull_fine_x || boat->fine_z != hull_fine_z )
                            {
                                under_way_ticks++;
                                if( rider->obs_jumped )
                                    jumped_while_under_way++;
                            }
                        }
                        ToriRSServer_VesselStop(boat);

                        SELFTEST_CHECK(walked,
                                       "the rider takes a walk step along the deck while "
                                       "it sails, %d,%d -> %d,%d",
                                       walk_from_x, walk_from_z, rider->x, rider->z);
                        SELFTEST_CHECK(composed == walk_ticks,
                                       "and their step and the hull's compose into the ONE "
                                       "observed tile every tick, got %d of %d",
                                       composed, walk_ticks);
                        SELFTEST_CHECK(under_way_ticks > 0,
                                       "with the hull genuinely under way for the walk, "
                                       "%d tick(s)",
                                       under_way_ticks);
                        SELFTEST_CHECK(jumped_while_under_way > 0,
                                       "the observation offset tracks the hull while the "
                                       "rider walks (%d of %d under-way ticks)",
                                       jumped_while_under_way, under_way_ticks);
                    }

                    /*
                     * C. An npc standing on the deck, seen from the shore.
                     *
                     * Two things had to be true and neither was. NPC_INFO
                     * measured raw tiles, so a deckhand read as a pool square
                     * hundreds off the arena; and the shore player's zonemap
                     * does not subscribe to the pool, so the candidate query
                     * could not have offered the npc even at zero range. Both
                     * are checked here, and the second explicitly — a range
                     * fix alone would have looked right and shipped an empty
                     * deck (docs/sailing_coverage.csv SAIL-50).
                     */
                    {
                        int chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                                                 "chicken");
                        int npc_slot = -1;

                        if( chicken > 0 )
                            npc_slot = ToriRSServer_WorldNpcSpawn(
                                srv, chicken, rider->x, rider->z + 1, 0);
                        SELFTEST_CHECK(npc_slot >= 0,
                                       "a deck hand spawns on the deck beside the rider");
                        if( npc_slot >= 0 )
                        {
                            struct ToriRSServerNpc* hand = &srv->npcs[npc_slot];
                            int shore_dx = 0;
                            int shore_dz = 0;
                            int deck_dx = 0;
                            int deck_dz = 0;

                            selftest_tick(srv);
                            ToriRSServer_NpcViewDeltas(hand, player, &shore_dx, &shore_dz);
                            ToriRSServer_NpcViewDeltas(hand, rider, &deck_dx, &deck_dz);

                            SELFTEST_CHECK(ToriRSServer_VesselAtTile(srv, hand->x, hand->z) ==
                                               boat,
                                           "onto a tile the hull owns");
                            SELFTEST_CHECK(deck_dx <= 1 && deck_dz <= 1,
                                           "the rider is standing next to it, %d,%d away",
                                           deck_dx, deck_dz);
                            SELFTEST_CHECK(ToriRSServer_WorldNpcVisibleTo(srv, hand, rider),
                                           "and may address it");
                            SELFTEST_CHECK(hand->obs_x != hand->x || hand->obs_z != hand->z,
                                           "it is OBSERVED somewhere other than it stands, "
                                           "%d,%d rather than %d,%d",
                                           hand->obs_x, hand->obs_z, hand->x, hand->z);
                            SELFTEST_CHECK(abs(hand->obs_x - rider->obs_x) <= 1 &&
                                               abs(hand->obs_z - rider->obs_z) <= 1,
                                           "and observed beside the rider it is standing "
                                           "beside, %d,%d vs %d,%d — the projection carries "
                                           "the pair together",
                                           hand->obs_x, hand->obs_z, rider->obs_x,
                                           rider->obs_z);
                            SELFTEST_CHECK(shore_dx <= TORIRSSERVER_NPC_VIEW_TILES &&
                                               shore_dz <= TORIRSSERVER_NPC_VIEW_TILES,
                                           "so the shore player measures it at %d,%d — the "
                                           "projected gap, not the pool one",
                                           shore_dx, shore_dz);
                            /*
                             * And the half a range fix cannot supply: the
                             * candidate set. The zonemap walk is asked first
                             * and must come back empty — the pool is in nobody
                             * else's subscription — so if the cross-frame
                             * query does not offer this npc, nothing does and
                             * the deck renders empty however close the boat is.
                             */
                            {
                                int candidates[TORIRSSERVER_TRACKED_NPC_MAX];
                                int zonemap_has = 0;
                                int crossframe_has = 0;
                                int count;

                                count = ToriRSServer_PlayerzonemapNpcs(
                                    player, TORIRSSERVER_NPC_VIEW_TILES, candidates,
                                    TORIRSSERVER_TRACKED_NPC_MAX);
                                for( int i = 0; i < count; i++ )
                                    zonemap_has = zonemap_has || candidates[i] == npc_slot;
                                count = ToriRSServer_PlayerCrossFrameNpcs(
                                    player, TORIRSSERVER_NPC_VIEW_TILES, candidates,
                                    TORIRSSERVER_TRACKED_NPC_MAX);
                                for( int i = 0; i < count; i++ )
                                    crossframe_has = crossframe_has || candidates[i] == npc_slot;

                                SELFTEST_CHECK(!zonemap_has,
                                               "the shore player's zonemap cannot offer a "
                                               "deck npc — it subscribes to no pool zone");
                                SELFTEST_CHECK(crossframe_has,
                                               "so the cross-frame query is what puts it in "
                                               "their candidate set, and it does");
                            }
                            SELFTEST_CHECK(abs(rider->obs_x - player->x) <=
                                                   TORIRSSERVER_NPC_VIEW_TILES &&
                                               abs(rider->obs_z - player->z) <=
                                                   TORIRSSERVER_NPC_VIEW_TILES,
                                           "and the rider standing beside it is in that "
                                           "range too, %d,%d vs %d,%d — deck and deckhand "
                                           "arrive together rather than one without the "
                                           "other",
                                           rider->obs_x, rider->obs_z, player->x, player->z);

                            ToriRSServer_WorldNpcFree(srv, npc_slot);
                            ToriRSServer_WorldNpcReap(srv);
                        }
                    }

                    /* Ocean NPCs must also enter the rider's ordinary ROOT
                     * subscription. Filtering its zones by deck feet/plane
                     * silently discarded visible sharks and shoals. */
                    {
                        int chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "chicken");
                        int ocean_npc = chicken > 0 ? ToriRSServer_WorldNpcSpawn(
                            srv, chicken, rider->obs_x + 3, rider->obs_z, boat->level) : -1;
                        SELFTEST_CHECK(ocean_npc >= 0, "an ocean NPC spawns beside the hull");
                        if( ocean_npc >= 0 )
                        {
                            int candidates[TORIRSSERVER_TRACKED_NPC_MAX];
                            int found = 0;
                            selftest_tick(srv);
                            int count = ToriRSServer_PlayerzonemapNpcs(
                                rider, TORIRSSERVER_NPC_VIEW_TILES, candidates,
                                TORIRSSERVER_TRACKED_NPC_MAX);
                            for( int i = 0; i < count; ++i ) found |= candidates[i] == ocean_npc;
                            SELFTEST_CHECK(found, "rider's root zones offer the nearby ocean NPC despite distant deck coordinates");
                            ToriRSServer_WorldNpcFree(srv, ocean_npc);
                            ToriRSServer_WorldNpcReap(srv);
                        }
                    }

                    /*
                     * D. The hull is freed under them.
                     *
                     * `ToriRSServer_VesselFree` used to release the deck
                     * instance and say nothing to whoever was standing in it:
                     * no disembark, no teleport, no death, just a rider left on
                     * a raw pool square that no longer belonged to anything —
                     * unreachable by any route and, once `obs_*` collapsed back
                     * onto that raw tile, invisible to every other client.
                     *
                     * Freeing a vessel evacuates every rider to a validated shore
                     * tile before releasing the private deck reservation.
                     */
                    {
                        int stranded_x = rider->x;
                        int stranded_z = rider->z;
                        int replacement;

                        SELFTEST_CHECK(ToriRSServer_VesselFree(srv, hull) == 1,
                                       "the hull frees with the rider still on its deck");
                        selftest_tick(srv);

                        SELFTEST_CHECK(rider->x != stranded_x || rider->z != stranded_z,
                                       "the free does not leave them on the pool square "
                                       "the deck used to occupy (%d,%d)",
                                       stranded_x, stranded_z);
                        SELFTEST_CHECK(!ToriRSServer_SceneWalkBlocked(rider->level, rider->x, rider->z) &&
                                           !ToriRSServer_VesselTileSailable(rider->level, rider->x, rider->z),
                                       "free evacuates the rider to walkable shore, got %d,%d",
                                       rider->x, rider->z);
                        SELFTEST_CHECK(ToriRSServer_VesselAtTile(srv, rider->x, rider->z) ==
                                           NULL,
                                       "on a tile no hull owns any more");
                        SELFTEST_CHECK(rider->obs_x == rider->x && rider->obs_z == rider->z,
                                       "so observed and own coordinates are one thing "
                                       "again, %d,%d",
                                       rider->obs_x, rider->obs_z);
                        SELFTEST_CHECK(!ToriRSServer_PlayerObservable(player, rider) &&
                                           !player->player_tracked[rider_pid],
                                       "the ocean observer retires the rider after safe shore evacuation");

                        /* Put a hull back under the fixture the rows below
                         * inherit: the next block frees `hull` itself and
                         * asserts the free answers 1. */
                        replacement =
                            ToriRSServer_VesselSpawn(srv, 9, 6, 12, 0, patch_x, patch_z, 0);
                        hull = replacement;
                        boat = ToriRSServer_VesselGet(srv, replacement);
                        SELFTEST_CHECK(boat != NULL, "a fresh hull takes the fixture back");
                        if( boat )
                        {
                            for( int zx = 0; zx < zones_x; zx++ )
                                for( int zz = 0; zz < zones_z; zz++ )
                                    ToriRSServer_MapInstanceSetchunk(
                                        boat->instance, 0, zx, zz, 3216, 3216, 0, 0);
                            ToriRSServer_MapInstanceBuild(boat->instance);
                            ToriRSServer_WorldMapInstanceBuilt(srv, boat->instance);
                        }
                    }

                    ToriRSServer_WorldPlayerFree(srv, rider_pid);
                    ToriRSServer_WorldPlayerReap(srv);
                    /* The observer has to be TRACKING the replacement before
                     * the next block, which reads its view id and serial and
                     * asserts the swap comes out as op 0 plus a fresh spawn. */
                    selftest_tick(srv);
                }
            }

            /*
             * A view id changing hulls between two ticks.
             *
             * View ids are handed out lowest-free and pool slots are reused, so
             * a hull freed and another spawned before the next encode carry the
             * SAME view id and the same handle. Described as a move, the client
             * keeps the sunk boat's config model and deck size and slides it
             * across the water — no packet malformed anywhere. The serial is
             * the only thing that distinguishes them, and the record that has
             * to come out is a despawn followed by a fresh spawn trailer.
             */
            if( boat )
            {
                int old_view = boat->view_id;
                int old_serial = boat->serial;
                int replacement;

                SELFTEST_CHECK(ToriRSServer_VesselFree(srv, hull) == 1,
                               "the hull frees mid-tick");
                replacement = ToriRSServer_VesselSpawn(srv, 11, 6, 12, 0, patch_x, patch_z, 0);
                hull = replacement;
                boat = ToriRSServer_VesselGet(srv, replacement);
                SELFTEST_CHECK(boat != NULL, "and another takes its place before the encode");
                if( boat )
                {
                    SELFTEST_CHECK(boat->view_id == old_view,
                                   "reusing view %d, as lowest-free must", old_view);
                    SELFTEST_CHECK(boat->serial != old_serial,
                                   "but never the serial, %d vs %d", old_serial, boat->serial);
                    for( int zx = 0; zx < zones_x; zx++ )
                        for( int zz = 0; zz < zones_z; zz++ )
                            ToriRSServer_MapInstanceSetchunk(
                                boat->instance, 0, zx, zz, 3216, 3216, 0, 0);
                    ToriRSServer_MapInstanceBuild(boat->instance);
                    ToriRSServer_WorldMapInstanceBuilt(srv, boat->instance);

                    ToriRSServer_CaptureBegin(srv, &wev_cap);
                    selftest_tick(srv);
                    ToriRSServer_CaptureEnd(srv);
                    at = ToriRSServer_CaptureFindNamed(&wev_cap, PKT_NAME_WORLDENTITY_INFO, 0);
                    SELFTEST_CHECK(at >= 0, "the swap tick sends WORLDENTITY_INFO");
                    if( at >= 0 )
                    {
                        selftest_wev_decode(wev_cap.packets[at].data, wev_cap.packets[at].len,
                                            &decoded);
                        SELFTEST_CHECK(decoded.count == 1 && decoded.moves[0].op == 0,
                                       "retiring the old hull's slot with op 0, got count %d "
                                       "op %d",
                                       decoded.count, decoded.moves[0].op);
                        SELFTEST_CHECK(decoded.spawn_count == 1 &&
                                           decoded.spawns[0].view_id == old_view,
                                       "and respawning view %d in the trailer, got %d spawns",
                                       old_view, decoded.spawn_count);
                        SELFTEST_CHECK(decoded.spawn_count == 1 &&
                                           decoded.spawns[0].config_id == 11,
                                       "carrying the NEW hull's config 11, got %d",
                                       decoded.spawn_count == 1 ? decoded.spawns[0].config_id
                                                                : -1);
                        SELFTEST_CHECK(decoded.trailing == 0, "with nothing left over, %d",
                                       decoded.trailing);
                    }
                    SELFTEST_CHECK(player->wev_tracked_count == 1 &&
                                       player->wev_serials[0] == boat->serial,
                                   "after which the observer's slot holds the new serial");
                }
            }

            /*
             * Teardown, and the despawn it owes: the hull goes, the observer
             * still holds a slot for it, and the next tick has to say op 0 in
             * that slot or the client keeps a boat nothing will ever move
             * again.
             */
            if( boat )
            {
                int gone_view = boat->view_id;

                SELFTEST_CHECK(ToriRSServer_VesselFree(srv, hull) == 1, "the hull frees");
                ToriRSServer_CaptureBegin(srv, &wev_cap);
                selftest_tick(srv);
                ToriRSServer_CaptureEnd(srv);
                at = ToriRSServer_CaptureFindNamed(&wev_cap, PKT_NAME_WORLDENTITY_INFO, 0);
                SELFTEST_CHECK(at >= 0, "the tick after it still sends WORLDENTITY_INFO");
                if( at >= 0 )
                {
                    selftest_wev_decode(wev_cap.packets[at].data, wev_cap.packets[at].len,
                                        &decoded);
                    SELFTEST_CHECK(decoded.count == 1 && decoded.moves[0].op == 0,
                                   "carrying op 0 in view %d's slot, got count %d op %d",
                                   gone_view, decoded.count, decoded.moves[0].op);
                    SELFTEST_CHECK(decoded.trailing == 0,
                                   "and op 0 carries no flags byte, %d left over",
                                   decoded.trailing);
                }
                SELFTEST_CHECK(player->wev_tracked_count == 0,
                               "after which the observer tracks nothing, got %d",
                               player->wev_tracked_count);
            }
            SELFTEST_CHECK(ToriRSServer_VesselLiveCount(srv) == 0,
                           "no hull survives the wire rows");
            SELFTEST_CHECK(ToriRSServer_MapInstanceLiveCount() == instances_before,
                           "and the deck went back to the pool with it");
        }

        /* Back to the suite's home window, the way the window row above left
         * it for everything after. */
        selftest_park_player(srv, 3222, 3218);
        player->rebuild_pending = 0;
    }

}
