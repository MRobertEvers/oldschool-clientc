/* Ranged/cast map flag, npc-vs-npc melee walls, mode-path swing claim.
 * Included from torirs_server_world_selftest.c after the shared helpers.
 *
 * Three seam22 leftovers (build/seam_state/seam23, seam
 * ranged_cast_map_flag_and_npc_npc_reach), one stanza each:
 *
 *   1. A press that fires FROM RANGE -- a cast's [apnpct] or a bow's
 *      in-range engaged fight -- abandons the walk that press queued, and the
 *      SET_MAP_FLAG that named it has to go with it. LostCity never routes an
 *      interaction that fires in range, and its unsetMapFlag() is
 *      clearWaypoints + the UnsetMapFlag packet (Player.ts processInteraction,
 *      2249); a flag left standing kept api_drive.player_idle false for a
 *      whole magic fight (seam22 s22cast_after4).
 *   2. Npc-vs-npc melee reads the wall on the shared edge, as the player's
 *      melee does since seam22 (PathingEntity.inOperableDistance ->
 *      reachExclusiveRectangle / RectangleBoundary.reachRectangleN).
 *   3. A mode-path swing (npc_setmode(opplayer2) -> [ai_opplayer2]) stamps the
 *      single-way claim, and gives the player up when another npc holds him
 *      (LostCity npc_combat.rs2 [proc,npc_default_attack] 40-55,
 *      [proc,npc_check_notcombat] 343-352, [proc,npc_set_attack_vars]).
 */

static int g_combat_reach_flag_failures;

static void
combat_reach_flag_pass_begin(void)
{
    g_combat_reach_flag_failures = g_selftest_failures;
}

static void
combat_reach_flag_pass(const char* step)
{
    assert(step);
    if( g_selftest_failures == g_combat_reach_flag_failures )
        fprintf(stderr, "ToriRSServer selftest: combat_reach_flag PASS %s\n", step);
    g_combat_reach_flag_failures = g_selftest_failures;
}

/* A fresh copy of `type` standing at x,z, held still: no roam, no mode, no
 * spawn animation pending. Returns the slot or -1. */
static int
combat_reach_flag_spawn(
    struct ToriRSServer* srv,
    int type,
    int x,
    int z)
{
    int slot;
    struct ToriRSServerNpc* npc;

    assert(srv);
    slot = ToriRSServer_WorldNpcSpawn(srv, type, x, z, 0);
    if( slot < 0 )
        return -1;
    npc = &srv->npcs[slot];
    npc->spawn_pending = 0;
    npc->x = x;
    npc->z = z;
    npc->mode = TORIRSSERVER_NPCMODE_NONE;
    npc->waypoint_index = -1;
    npc->next_roam_tick = srv->tick + 1000;
    npc->combat_target = -1;
    npc->combat_target_npc = -1;
    npc->attack_clock = 0;
    return slot;
}

/*
 * Quiet every other npc near (x,z) for stanza 3: whatever the world left
 * standing around Lumbridge (a goblin wandered onto 3223,3218 and swung at the
 * parked player on the stanza's tick) must not stamp a claim the stanza
 * reads. Hunt mode off and any fight stopped; the hunt modes are stashed so
 * combat_reach_flag_unquiet can give them back.
 */
#define COMBAT_REACH_FLAG_QUIET_MAX 64

static int g_combat_reach_flag_quiet_slot[COMBAT_REACH_FLAG_QUIET_MAX];
static int g_combat_reach_flag_quiet_hunt[COMBAT_REACH_FLAG_QUIET_MAX];
static int g_combat_reach_flag_quiet_count;

static void
combat_reach_flag_quiet(
    struct ToriRSServer* srv,
    int x,
    int z)
{
    assert(srv);
    g_combat_reach_flag_quiet_count = 0;
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[i];

        if( !npc->active || abs(npc->x - x) > 16 || abs(npc->z - z) > 16 )
            continue;
        if( g_combat_reach_flag_quiet_count >= COMBAT_REACH_FLAG_QUIET_MAX )
            break;
        g_combat_reach_flag_quiet_slot[g_combat_reach_flag_quiet_count] = i;
        g_combat_reach_flag_quiet_hunt[g_combat_reach_flag_quiet_count] = npc->huntmode;
        g_combat_reach_flag_quiet_count++;
        npc->huntmode = TORIRSSERVER_HUNT_NONE;
        ToriRSServer_CombatStopNpc(srv, i);
    }
}

static void
combat_reach_flag_unquiet(struct ToriRSServer* srv)
{
    assert(srv);
    for( int i = 0; i < g_combat_reach_flag_quiet_count; i++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[g_combat_reach_flag_quiet_slot[i]];

        if( npc->active )
            npc->huntmode = g_combat_reach_flag_quiet_hunt[i];
    }
    g_combat_reach_flag_quiet_count = 0;
}

static void
selftest_combat_reach_flag(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int goblin;
    int man;

    assert(srv);
    assert(player);
    combat_reach_flag_pass_begin();
    goblin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "goblin_unarmed_melee_1");
    man = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "man");
    SELFTEST_CHECK(goblin >= 0, "combat_reach_flag: goblin_unarmed_melee_1 resolves");
    SELFTEST_CHECK(man >= 0, "combat_reach_flag: man resolves");
    if( goblin < 0 || man < 0 )
        return;

    /*
     * 1a. A cast from range takes its flag down.
     *
     * OPNPCT on a goblin five tiles east: spell_interact queues the walk to
     * melee adjacency and sends SET_MAP_FLAG for it, then the immediate try
     * resolves `[apnpct,magic_spellbook:league_home_teleport]` (selftest_cast.rs2,
     * writes 50) at range before a tile is walked. The route is dropped; the
     * flag used to stay (dest kept, no clear_map_flag), because advance_player
     * only takes it down after a step or on the flag's own tile.
     */
    {
        int spell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT,
                                               "magic_spellbook:league_home_teleport");
        int slot;

        selftest_park_player(srv, 3222, 3218);
        slot = combat_reach_flag_spawn(srv, goblin, 3227, 3218);
        SELFTEST_CHECK(spell > 0, "combat_reach_flag: the selftest spell resolves");
        SELFTEST_CHECK(slot >= 0, "combat_reach_flag: a goblin spawns five tiles east");
        if( spell > 0 && slot >= 0 )
        {
            uint8_t payload[16];
            struct RSAreaBuf out;

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            player->clear_map_flag = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, ToriRSServer_SlotMapAcquire(player, slot));
            rsab_p4(&out, spell);
            selftest_handle(player, PKTOUT_NAME_OPNPCT, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 50,
                           "combat_reach_flag: the cast resolved at range, progress %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            SELFTEST_CHECK(player->x == 3222 && player->z == 3218,
                           "combat_reach_flag: the caster did not move, at %d,%d", player->x,
                           player->z);
            SELFTEST_CHECK(player->waypoint_index < 0,
                           "combat_reach_flag: the walk the cast queued is dropped");
            SELFTEST_CHECK(player->clear_map_flag == 1,
                           "combat_reach_flag: the cast's map flag is taken down with the "
                           "walk (clear_map_flag %d, dest %d,%d)",
                           player->clear_map_flag, player->dest_x, player->dest_z);
            SELFTEST_CHECK(player->dest_x < 0 && player->dest_z < 0,
                           "combat_reach_flag: and no destination is left behind, dest %d,%d",
                           player->dest_x, player->dest_z);
            selftest_tick(srv);
            ToriRSServer_WorldNpcFree(srv, slot);
        }
        combat_reach_flag_pass("cast_from_range_unsets_the_map_flag");
    }

    /*
     * 1b. A bow's fight in range takes the flag down.
     *
     * Every swing re-arms through p_opnpc(2), which walks toward melee
     * adjacency and sends SET_MAP_FLAG; CombatPlayerApproach then finds the
     * target inside the bow's reach and drops the walk. Stand-in for the
     * re-arm: the same WalkToApproach p_opnpc does.
     */
    {
        int bow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "shortbow");
        int saved_weapon = player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id;
        int saved_count = player->worn[TORIRSSERVER_WEAR_WEAPON].count;
        int slot;

        selftest_park_player(srv, 3222, 3218);
        slot = combat_reach_flag_spawn(srv, goblin, 3227, 3218);
        SELFTEST_CHECK(bow >= 0, "combat_reach_flag: shortbow resolves");
        SELFTEST_CHECK(slot >= 0, "combat_reach_flag: a goblin spawns for the bow");
        if( bow >= 0 && slot >= 0 )
        {
            struct CollisionApproach approach;

            player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id = bow;
            player->worn[TORIRSSERVER_WEAR_WEAPON].count = 1;
            ToriRSServer_SceneNpcApproach(1, &approach);
            ToriRSServer_WorldWalkToApproach(srv, 3227, 3218, &approach);
            SELFTEST_CHECK(player->waypoint_index >= 0 && player->dest_x >= 0,
                           "combat_reach_flag: the re-arm's walk and flag are queued, "
                           "dest %d,%d",
                           player->dest_x, player->dest_z);
            player->combat_target = slot;
            player->clear_map_flag = 0;
            ToriRSServer_CombatPlayerApproach(srv);
            SELFTEST_CHECK(player->waypoint_index < 0,
                           "combat_reach_flag: in bow range the walk is dropped");
            SELFTEST_CHECK(player->clear_map_flag == 1,
                           "combat_reach_flag: and the bow fight's map flag with it "
                           "(clear_map_flag %d, dest %d,%d)",
                           player->clear_map_flag, player->dest_x, player->dest_z);
            player->combat_target = -1;
            player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id = saved_weapon;
            player->worn[TORIRSSERVER_WEAR_WEAPON].count = saved_count;
            player->dest_x = -1;
            player->dest_z = -1;
            selftest_tick(srv);
            ToriRSServer_WorldNpcFree(srv, slot);
        }
        combat_reach_flag_pass("bow_in_range_unsets_the_map_flag");
    }

    /*
     * 2. Npc-vs-npc melee reads the wall on the shared edge.
     *
     * Found, not pinned: the first east-west pair of squares in the castle
     * courtyard block whose shared edge has a wall (SceneMeleeReached false
     * from a flush cardinal square), and an open pair for the control. A
     * goblin on one side fighting a goblin on the other must not swing
     * (attack_clock untouched); the same two across an open edge must.
     */
    {
        int wall_x = -1;
        int wall_z = -1;

        selftest_park_player(srv, 3222, 3218);
        for( int x = 3205; x <= 3240 && wall_x < 0; x++ )
        {
            for( int z = 3205; z <= 3235; z++ )
            {
                if( !ToriRSServer_SceneContains(x, z) || !ToriRSServer_SceneContains(x + 1, z) )
                    continue;
                if( !ToriRSServer_SceneMeleeReached(0, x, z, x + 1, z, 1) )
                {
                    wall_x = x;
                    wall_z = z;
                    break;
                }
            }
        }
        SELFTEST_CHECK(wall_x >= 0, "combat_reach_flag: Lumbridge has a walled east-west edge");
        SELFTEST_CHECK(ToriRSServer_SceneMeleeReached(0, 3222, 3216, 3223, 3216, 1),
                       "combat_reach_flag: the control edge 3222,3216 | 3223,3216 is open");
        if( wall_x >= 0 )
        {
            struct
            {
                int x;
                int z;
                int want_swing;
                const char* what;
            } cases[] = {
                { wall_x, wall_z, 0, "across a wall" },
                { 3222, 3216, 1, "across an open edge" },
            };

            for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
            {
                int a = combat_reach_flag_spawn(srv, goblin, cases[i].x, cases[i].z);
                int b = combat_reach_flag_spawn(srv, goblin, cases[i].x + 1, cases[i].z);

                SELFTEST_CHECK(a >= 0 && b >= 0, "combat_reach_flag: two goblins %s spawn",
                               cases[i].what);
                if( a >= 0 && b >= 0 )
                {
                    struct ToriRSServerNpc* na = &srv->npcs[a];
                    int swung;

                    na->combat_target_npc = b;
                    na->combat_target_npc_gen = srv->npcs[b].generation;
                    na->attack_clock = 0;
                    ToriRSServer_CombatNpcTick(srv, a);
                    swung = na->attack_clock > srv->tick;
                    SELFTEST_CHECK(swung == cases[i].want_swing,
                                   "combat_reach_flag: an npc melee swing %s at %d,%d -> %d,%d "
                                   "%s (attack_clock %d, tick %d)",
                                   cases[i].what, cases[i].x, cases[i].z, cases[i].x + 1,
                                   cases[i].z, cases[i].want_swing ? "lands" : "is refused",
                                   na->attack_clock, (int)srv->tick);
                    na->combat_target_npc = -1;
                }
                if( a >= 0 )
                    ToriRSServer_WorldNpcFree(srv, a);
                if( b >= 0 )
                    ToriRSServer_WorldNpcFree(srv, b);
                selftest_tick(srv);
            }
        }
        combat_reach_flag_pass("npc_npc_melee_reads_walls");
    }

    /*
     * 3. A mode-path swing stamps the single-way claim -- and yields.
     *
     * A man set to opplayer2 against a player standing flush east of him (the
     * npc_retaliate shape) fires [ai_opplayer2] from npc_run_mode, not from the
     * combat clock. The first swing must claim the player. A second man on
     * the other side, set the same way while the first holds him, gives him up:
     * the claim still names the first.
     */
    {
        int first;
        int second;
        /* Auto-retaliate OFF for the stanza (option_nodef 1 = "no defend",
         * skill_combat.rs2's ^player_auto_retaliate_on is 0): the player's own
         * retaliation re-arms p_opnpc(2), which stamps the same claim, and would
         * hide a mode-path swing that stamped nothing. */
        int option_nodef = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "option_nodef");
        int saved_nodef = 0;

        SELFTEST_CHECK(option_nodef >= 0, "combat_reach_flag: option_nodef resolves");
        if( option_nodef < 0 )
            return;
        saved_nodef = player->varps[option_nodef];
        player->varps[option_nodef] = 1;
        selftest_park_player(srv, 3222, 3218);
        combat_reach_flag_quiet(srv, 3222, 3218);
        first = combat_reach_flag_spawn(srv, man, 3221, 3218);
        SELFTEST_CHECK(first >= 0, "combat_reach_flag: a man spawns west of the player");
        if( first >= 0 )
        {
            struct ToriRSServerNpc* npc = &srv->npcs[first];

            player->combat_claim_npc = -1;
            player->combat_claim_tick = 0;
            npc->mode = TORIRSSERVER_NPCMODE_OPPLAYER1 + 1; /* opplayer2 */
            ToriRSServer_NpcSetModeTarget(npc, player);
            selftest_tick(srv);
            SELFTEST_CHECK(player->combat_claim_npc == first &&
                               player->combat_claim_tick > srv->tick,
                           "combat_reach_flag: the mode-path swing claims the player "
                           "(claim slot %d type %d at %d,%d until %d, want slot %d, tick %d; "
                           "man at %d,%d mode %d)",
                           player->combat_claim_npc,
                           player->combat_claim_npc >= 0 ? srv->npcs[player->combat_claim_npc].type
                                                         : -1,
                           player->combat_claim_npc >= 0 ? srv->npcs[player->combat_claim_npc].x
                                                         : -1,
                           player->combat_claim_npc >= 0 ? srv->npcs[player->combat_claim_npc].z
                                                         : -1,
                           player->combat_claim_tick, first, (int)srv->tick, npc->x, npc->z,
                           npc->mode);

            second = combat_reach_flag_spawn(srv, man, 3223, 3218);
            SELFTEST_CHECK(second >= 0, "combat_reach_flag: a second man spawns east");
            if( second >= 0 && !ToriRSServer_CombatMultiway(&srv->npcs[second]) )
            {
                struct ToriRSServerNpc* other = &srv->npcs[second];

                npc->mode = TORIRSSERVER_NPCMODE_NONE;
                other->mode = TORIRSSERVER_NPCMODE_OPPLAYER1 + 1; /* opplayer2 */
                ToriRSServer_NpcSetModeTarget(other, player);
                selftest_tick(srv);
                SELFTEST_CHECK(player->combat_claim_npc == first,
                               "combat_reach_flag: a second npc's mode swing gives a claimed "
                               "player up, claim slot %d, want %d",
                               player->combat_claim_npc, first);
                ToriRSServer_CombatStopNpc(srv, second);
                ToriRSServer_WorldNpcFree(srv, second);
            }
            ToriRSServer_CombatStopNpc(srv, first);
            ToriRSServer_WorldNpcFree(srv, first);
        }
        combat_reach_flag_unquiet(srv);
        player->varps[option_nodef] = saved_nodef;
        player->combat_claim_npc = -1;
        player->combat_claim_tick = 0;
        selftest_tick(srv);
        combat_reach_flag_pass("mode_path_swing_claims_and_yields");
    }
}
