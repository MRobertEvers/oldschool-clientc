/* Fishing Contest -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int
fishcompo_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
fishcompo_release_park(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
fishcompo_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
fishcompo_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = fishcompo_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 80 && player->active_script; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( stop_on_choice && chatmenu > 0 && uid == chatmenu )
                return;
            if( !ToriRSServer_ScriptsResumeButton(srv, uid) )
                break;
        }
        else if( exec == SSVM_SUSPENDED || exec == SSVM_NPC_SUSPENDED ||
                 exec == SSVM_WORLD_SUSPENDED )
        {
            selftest_tick(srv);
        }
        else
        {
            break;
        }
    }
}

static void
fishcompo_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = fishcompo_chatmenu();

    assert(srv);
    assert(player);
    fishcompo_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
fishcompo_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    assert(srv);
    assert(player);
    fishcompo_release_park(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
fishcompo_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    fishcompo_drain(srv, player, 0);
    for( i = 0; i < 48; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static int
fishcompo_find_npc_at(const struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int i;

    assert(srv);
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type && srv->npcs[i].x == x &&
            srv->npcs[i].z == z && srv->npcs[i].level == level )
            return i;
    }
    return -1;
}

static int
fishcompo_find_loc_at(int x, int z, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int slot;
    int s;

    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( dx = -radius; dx <= radius; dx++ )
    {
        for( dz = -radius; dz <= radius; dz++ )
        {
            slot = ToriRSServer_SceneFindLocId(x + dx, z + dz, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    }
    for( s = 0;; s++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

        if( !loc )
            break;
        if( !loc->active || loc->level != level )
            continue;
        if( loc_id > 0 && loc->loc_id != loc_id )
            continue;
        if( loc->x + loc->size_x <= x - radius || loc->x > x + radius )
            continue;
        if( loc->z + loc->size_z <= z - radius || loc->z > z + radius )
            continue;
        return s;
    }
    return -1;
}

static void
fishcompo_austri_to_offer(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc, int slot)
{
    fishcompo_talk(srv, player, npc, slot);
    fishcompo_choose(srv, player, 1);
    fishcompo_choose(srv, player, 2);
    fishcompo_choose(srv, player, 3);
    fishcompo_choose(srv, player, 1);
    fishcompo_choose(srv, player, 1);
}

static void
selftest_quest_fishingcompo(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_austri;
    int npc_morris;
    int npc_jack;
    int npc_bonzo;
    int npc_stranger;
    int npc_spot;
    int loc_pipe;
    int loc_gate;
    int loc_vine;
    int loc_stair;
    int varp;
    int vb_garlic;
    int vb_paid;
    int vb_passed;
    int vb_stranger;
    int qp_varp;
    int fishing;
    int obj_pass;
    int obj_coins;
    int obj_rod;
    int obj_garlic;
    int obj_worm;
    int obj_carp;
    int obj_trophy;
    int obj_spade;
    int a_slot;
    int m_slot;
    int j_slot;
    int b_slot;
    int s_slot;
    int spot_slot;
    int spawned_a;
    int spawned_m;
    int spawned_j;
    int spawned_b;
    int spawned_s;
    int spawned_spot;
    int stranger_x;
    int stranger_z;
    int qp_before;
    int xp_before;
    int pipe_slot;
    int vine_slot;
    int stair_slot;

    fprintf(stderr, "ToriRSServer selftest: quest_fishingcompo (Fishing Contest)\n");
    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded && srv->scripts_ok, "quest_fishingcompo selftest needs the compiled script pack");
    if( !loaded || !srv->scripts_ok )
        return;

    npc_austri = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tunnel_dwarf");
    npc_morris = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "morris");
    npc_jack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grandpa_jack");
    npc_bonzo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bonzo");
    npc_stranger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sinister_stranger");
    npc_spot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "0_41_53_sinisterfishspot");
    loc_pipe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "garlicpipe");
    loc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fishinggateclosedl");
    loc_vine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "red_worm_vine");
    (void)loc_vine;
    loc_stair = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tunnelstairstop2");
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fishingcompo");
    vb_garlic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fishingcompo_garlicpipe");
    vb_paid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fishingcompo_paid");
    vb_passed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fishingcompo_passed");
    vb_stranger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fishingcompo_stranger");
    qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    fishing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fishing");
    obj_pass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fishing_competition_pass");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_rod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fishing_rod");
    obj_garlic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garlic");
    obj_worm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "red_vine_worm");
    obj_carp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_giant_carp");
    obj_trophy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hemenster_fishing_trophy");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");

    SELFTEST_CHECK(npc_austri >= 0 && npc_morris >= 0 && npc_jack >= 0 && npc_bonzo >= 0 &&
                       npc_stranger >= 0 && npc_spot >= 0 && varp >= 0 && obj_pass >= 0 &&
                       obj_carp >= 0 && obj_trophy >= 0 && fishing >= 0,
                   "quest_fishingcompo symbols should resolve");
    if( npc_austri < 0 || varp < 0 || fishing < 0 )
        return;

    srv->members_world = 1;
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    player->varps[varp] = 0;
    if( vb_garlic >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_garlic, 0);
    if( vb_paid >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_paid, 0);
    if( vb_passed >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_passed, 0);
    if( vb_stranger >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_stranger, 0);
    spawned_a = spawned_m = spawned_j = spawned_b = spawned_s = spawned_spot = -1;

    /* ---- Austri, Fishing 9: ineligible start ---- */
    player->stat_level[fishing] = 9;
    player->stat_boosted[fishing] = 9;
    fishcompo_snap(srv, player, 0, 2877, 3483);
    a_slot = fishcompo_find_npc_at(srv, npc_austri, 2877, 3483, 0);
    if( a_slot < 0 )
        a_slot = selftest_find_npc(srv, npc_austri);
    if( a_slot < 0 )
    {
        a_slot = ToriRSServer_WorldNpcSpawn(srv, npc_austri, 2877, 3483, 0);
        spawned_a = a_slot;
    }
    SELFTEST_CHECK(a_slot >= 0, "Austri (tunnel_dwarf) should exist at 2877,3483");
    if( a_slot < 0 )
        goto fishcompo_done;

    if( loc_stair > 0 )
    {
        stair_slot = fishcompo_find_loc_at(2877, 3482, 0, loc_stair, 8);
        SELFTEST_CHECK(stair_slot >= 0, "tunnelstairstop2 should be placed at Austri's entrance");
        if( stair_slot >= 0 )
        {
            fishcompo_release_park(srv, player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_stair, -1, stair_slot);
            fishcompo_drain(srv, player, 0);
            SELFTEST_CHECK(player->x == 2877 && player->z == 3483,
                           "surface tunnel must stay closed before complete, now %d,%d",
                           player->x, player->z);
            fprintf(stderr, "PASS fishingcontest tunnel_denied trigger=oploc1,tunnelstairstop2 observable=no_tele\n");
        }
    }

    fishcompo_austri_to_offer(srv, player, npc_austri, a_slot);
    fishcompo_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 0, "Fishing 9 must leave fishingcompo=0, got %d",
                   player->varps[varp]);
    fprintf(stderr, "PASS fishingcontest start_level_refuse trigger=opnpc1,tunnel_dwarf observable=fishingcompo=0\n");

    /* ---- Austri, Fishing 10: No ---- */
    player->stat_level[fishing] = 10;
    player->stat_boosted[fishing] = 10;
    fishcompo_austri_to_offer(srv, player, npc_austri, a_slot);
    fishcompo_choose(srv, player, 2);
    fishcompo_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 0, "No must leave fishingcompo=0, got %d",
                   player->varps[varp]);
    fprintf(stderr, "PASS fishingcontest start_refuse trigger=opnpc1,tunnel_dwarf observable=fishingcompo=0\n");

    /* ---- Austri accept ---- */
    fishcompo_austri_to_offer(srv, player, npc_austri, a_slot);
    fishcompo_choose(srv, player, 1);
    fishcompo_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 1, "accept should write fishingcompo=1, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(obj_pass < 0 || selftest_count(player, obj_pass) == 1,
                   "accept should grant exactly 1 competition pass");
    fprintf(stderr, "PASS fishingcontest start_accept trigger=opnpc1,tunnel_dwarf observable=fishingcompo=1\n");

    /* ---- Morris / gate ---- */
    fishcompo_snap(srv, player, 0, 2643, 3440);
    m_slot = fishcompo_find_npc_at(srv, npc_morris, 2643, 3440, 0);
    if( m_slot < 0 )
        m_slot = selftest_find_npc(srv, npc_morris);
    if( m_slot < 0 )
    {
        m_slot = ToriRSServer_WorldNpcSpawn(srv, npc_morris, 2643, 3440, 0);
        spawned_m = m_slot;
    }
    SELFTEST_CHECK(m_slot >= 0, "Morris should exist at the Hemenster gate");
    if( m_slot >= 0 )
    {
        fishcompo_talk(srv, player, npc_morris, m_slot);
        fishcompo_drain(srv, player, 0);
        fprintf(stderr, "PASS fishingcontest morris_talk trigger=opnpc1,morris observable=pass_dialogue\n");
    }
    if( loc_gate > 0 )
    {
        int loc_gater = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fishinggateclosedr");
        int gate_id = loc_gate;
        int gate_slot = fishcompo_find_loc_at(2642, 3441, 0, loc_gate, 6);

        if( gate_slot < 0 && loc_gater > 0 )
        {
            gate_slot = fishcompo_find_loc_at(2642, 3441, 0, loc_gater, 6);
            if( gate_slot >= 0 )
                gate_id = loc_gater;
        }
        SELFTEST_CHECK(gate_slot >= 0, "fishinggateclosedl/r should be placed at 2642,3441");
        if( gate_slot >= 0 )
        {
            /* East of the fence is outside; loc_coord compare admits from x > gate. */
            fishcompo_snap(srv, player, 0, 2643, 3441);
            fishcompo_release_park(srv, player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, gate_id, -1, gate_slot);
            fishcompo_drain(srv, player, 0);
            if( vb_passed >= 0 )
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_passed) == 1,
                               "gate with pass should set fishingcompo_passed, got %d",
                               ToriRSServer_VarbitGet(player, vb_passed));
            fprintf(stderr, "PASS fishingcontest morris_gate trigger=oploc1,fishinggateclosedl observable=passed\n");
        }
    }

    /* ---- Grandpa Jack 5-coin rod ---- */
    fishcompo_snap(srv, player, 0, 2650, 3452);
    j_slot = fishcompo_find_npc_at(srv, npc_jack, 2650, 3452, 0);
    if( j_slot < 0 )
        j_slot = selftest_find_npc(srv, npc_jack);
    if( j_slot < 0 )
    {
        j_slot = ToriRSServer_WorldNpcSpawn(srv, npc_jack, 2650, 3452, 0);
        spawned_j = j_slot;
    }
    SELFTEST_CHECK(j_slot >= 0, "Grandpa Jack should exist at 2650,3452");
    if( j_slot >= 0 )
    {
        selftest_give(player, obj_coins, 20);
        fishcompo_talk(srv, player, npc_jack, j_slot);
        fishcompo_choose(srv, player, 3);
        fishcompo_choose(srv, player, 1);
        fishcompo_drain(srv, player, 0);
        SELFTEST_CHECK(selftest_count(player, obj_rod) == 1, "Jack should sell exactly one fishing rod");
        SELFTEST_CHECK(selftest_count(player, obj_coins) == 15, "Jack rod should cost 5 coins, have %d",
                       selftest_count(player, obj_coins));
        fprintf(stderr, "PASS fishingcontest grandpa_jack_rod trigger=opnpc1,grandpa_jack observable=rod=1\n");
    }

    /* ---- McGrubor's vines: west of ALS, QH 2631,3496. Any of the eight shells. ---- */
    {
        static const char* vine_names[] = {
            "red_worm_junction", "red_worm_vine",     "red_worm_corner", "red_worm_end",
            "red_worm_diag1",    "red_worm_diag3",    "red_worm_diagfiller", "red_worm_end_diag",
        };
        int vine_id = -1;
        int vine_cat;
        int vi;

        fishcompo_snap(srv, player, 0, 2631, 3496);
        vine_slot = -1;
        vine_cat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_CATEGORY, "red_vine");
        for( vi = 0; vi < 8 && vine_slot < 0; vi++ )
        {
            int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, vine_names[vi]);

            if( id < 0 )
                continue;
            vine_slot = fishcompo_find_loc_at(2631, 3496, 0, id, 12);
            if( vine_slot >= 0 )
                vine_id = id;
        }
        SELFTEST_CHECK(vine_slot >= 0, "a red_worm_* vine should be placed west of ALS at 2631,3496");
        if( vine_slot >= 0 )
        {
            if( obj_spade >= 0 && selftest_count(player, obj_spade) < 1 )
                selftest_give(player, obj_spade, 1);
            fishcompo_release_park(srv, player);
            /* Name-bound oploc is absent; [oploc1,_red_vine] is the category rung. */
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, vine_id, vine_cat, vine_slot);
            fishcompo_drain(srv, player, 0);
            SELFTEST_CHECK(obj_worm < 0 || selftest_count(player, obj_worm) >= 1,
                           "Check vine + spade should grant a red vine worm");
            fprintf(stderr, "PASS fishingcontest vine_worms trigger=oploc1,_red_vine SceneFindLocId=1\n");
        }
    }

    /* ---- Bonzo pay ---- */
    fishcompo_snap(srv, player, 0, 2641, 3437);
    b_slot = fishcompo_find_npc_at(srv, npc_bonzo, 2641, 3437, 0);
    if( b_slot < 0 )
        b_slot = selftest_find_npc(srv, npc_bonzo);
    if( b_slot < 0 )
    {
        b_slot = ToriRSServer_WorldNpcSpawn(srv, npc_bonzo, 2641, 3437, 0);
        spawned_b = b_slot;
    }
    SELFTEST_CHECK(b_slot >= 0, "Bonzo should exist at 2641,3437");
    if( b_slot >= 0 )
    {
        fishcompo_talk(srv, player, npc_bonzo, b_slot);
        fishcompo_choose(srv, player, 1);
        fishcompo_drain(srv, player, 0);
        if( vb_paid >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_paid) == 1,
                           "Bonzo pay should set fishingcompo_paid, got %d",
                           ToriRSServer_VarbitGet(player, vb_paid));
        SELFTEST_CHECK(player->varps[varp] == 2, "pay without garlic should write fishingcompo=2, got %d",
                       player->varps[varp]);
        fprintf(stderr, "PASS fishingcontest bonzo_pay trigger=opnpc1,bonzo observable=fishingcompo=2 paid=1\n");
    }

    /* ---- Garlic isolation: world stranger must not teleport ---- */
    fishcompo_snap(srv, player, 0, 2637, 3440);
    s_slot = fishcompo_find_npc_at(srv, npc_stranger, 2637, 3440, 0);
    if( s_slot < 0 )
        s_slot = selftest_find_npc(srv, npc_stranger);
    if( s_slot < 0 )
    {
        s_slot = ToriRSServer_WorldNpcSpawn(srv, npc_stranger, 2637, 3440, 0);
        spawned_s = s_slot;
    }
    SELFTEST_CHECK(s_slot >= 0, "Sinister Stranger should exist at 2637,3440");
    stranger_x = s_slot >= 0 ? srv->npcs[s_slot].x : -1;
    stranger_z = s_slot >= 0 ? srv->npcs[s_slot].z : -1;
    if( obj_garlic >= 0 && selftest_count(player, obj_garlic) < 1 )
        selftest_give(player, obj_garlic, 1);
    fishcompo_snap(srv, player, 0, 2638, 3446);
    pipe_slot = loc_pipe > 0 ? fishcompo_find_loc_at(2638, 3446, 0, loc_pipe, 4) : -1;
    SELFTEST_CHECK(pipe_slot >= 0 || loc_pipe < 0, "garlicpipe shell should be placed at the pipes");
    if( pipe_slot >= 0 && obj_garlic >= 0 )
    {
        fishcompo_release_park(srv, player);
        player->last_useitem = obj_garlic;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_pipe, -1, pipe_slot);
        fishcompo_drain(srv, player, 0);
    }
    if( s_slot >= 0 )
        SELFTEST_CHECK(srv->npcs[s_slot].active && srv->npcs[s_slot].x == stranger_x &&
                           srv->npcs[s_slot].z == stranger_z,
                       "garlic must not teleport the shared Stranger, now %d,%d was %d,%d",
                       srv->npcs[s_slot].x, srv->npcs[s_slot].z, stranger_x, stranger_z);
    if( vb_garlic >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_garlic) == 1,
                       "garlic should set fishingcompo_garlicpipe, got %d",
                       ToriRSServer_VarbitGet(player, vb_garlic));
    if( vb_stranger >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_stranger) == 1,
                       "garlic after pay should set fishingcompo_stranger, got %d",
                       ToriRSServer_VarbitGet(player, vb_stranger));
    SELFTEST_CHECK(player->varps[varp] == 3, "garlic after pay should write fishingcompo=3, got %d",
                   player->varps[varp]);
    fprintf(stderr,
            "PASS fishingcontest garlic_isolation trigger=oplocu,garlicpipe observable=stranger_unmoved fishingcompo=3\n");

    /* ---- One carp at the pipe shell ---- */
    fishcompo_snap(srv, player, 0, 2637, 3444);
    spot_slot = fishcompo_find_npc_at(srv, npc_spot, 2637, 3444, 0);
    if( spot_slot < 0 )
        spot_slot = selftest_find_npc(srv, npc_spot);
    if( spot_slot < 0 )
    {
        spot_slot = ToriRSServer_WorldNpcSpawn(srv, npc_spot, 2637, 3444, 0);
        spawned_spot = spot_slot;
    }
    SELFTEST_CHECK(spot_slot >= 0, "0_41_53_sinisterfishspot should exist at 2637,3444");
    if( spot_slot >= 0 )
    {
        int xp_pre = player->stat_xp_tenths[fishing];

        if( obj_worm >= 0 && selftest_count(player, obj_worm) < 1 )
            selftest_give(player, obj_worm, 1);
        if( obj_rod >= 0 && selftest_count(player, obj_rod) < 1 )
            selftest_give(player, obj_rod, 1);
        fishcompo_release_park(srv, player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_spot, -1, spot_slot);
        fishcompo_drain(srv, player, 0);
        SELFTEST_CHECK(selftest_count(player, obj_carp) == 1, "pipe spot + worm should grant one raw giant carp");
        SELFTEST_CHECK(player->stat_xp_tenths[fishing] == xp_pre, "giant carp catch is zero Fishing XP");
        SELFTEST_CHECK(player->varps[varp] == 3, "one carp must not auto-end the contest, fishingcompo=%d",
                       player->varps[varp]);
        fprintf(stderr, "PASS fishingcontest catch_carp trigger=opnpc1,0_41_53_sinisterfishspot observable=carp=1\n");
    }

    /* ---- Bonzo one-carp hand-in ---- */
    if( b_slot < 0 )
        b_slot = selftest_find_npc(srv, npc_bonzo);
    if( b_slot >= 0 )
    {
        fishcompo_snap(srv, player, 0, 2641, 3437);
        fishcompo_talk(srv, player, npc_bonzo, b_slot);
        fishcompo_choose(srv, player, 1);
        fishcompo_drain(srv, player, 0);
        SELFTEST_CHECK(player->varps[varp] == 4, "hand-in should write fishingcompo=4, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(selftest_count(player, obj_carp) == 0, "win should take the raw giant carp");
        SELFTEST_CHECK(selftest_count(player, obj_trophy) == 1, "win should grant one trophy");
        fprintf(stderr, "PASS fishingcontest bonzo_handin trigger=opnpc1,bonzo observable=fishingcompo=4\n");
    }

    /* ---- Trophy return / complete ---- */
    fishcompo_snap(srv, player, 0, 2877, 3483);
    if( spawned_a < 0 )
        a_slot = fishcompo_find_npc_at(srv, npc_austri, 2877, 3483, 0);
    if( a_slot < 0 )
        a_slot = selftest_find_npc(srv, npc_austri);
    if( a_slot < 0 )
    {
        a_slot = ToriRSServer_WorldNpcSpawn(srv, npc_austri, 2877, 3483, 0);
        spawned_a = a_slot;
    }
    SELFTEST_CHECK(a_slot >= 0, "Austri should exist for trophy return");
    if( a_slot >= 0 )
    {
        qp_before = qp_varp >= 0 ? player->varps[qp_varp] : 0;
        xp_before = player->stat_xp_tenths[fishing];
        fishcompo_talk(srv, player, npc_austri, a_slot);
        fishcompo_drain(srv, player, 0);
        fishcompo_drain_rewards(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 5, "completion should commit fishingcompo=5, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(player->stat_xp_tenths[fishing] == xp_before + 24370,
                       "completion should grant 2437 Fishing XP, %d -> %d", xp_before,
                       player->stat_xp_tenths[fishing]);
        if( qp_varp >= 0 )
            SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 1,
                           "completion should award 1 QP, %d -> %d", qp_before,
                           player->varps[qp_varp]);
        fprintf(stderr,
                "PASS fishingcontest complete trigger=opnpc1,tunnel_dwarf observable=fishingcompo=5 "
                "xp_delta=24370 qp_delta=1\n");

        fishcompo_talk(srv, player, npc_austri, a_slot);
        fishcompo_drain(srv, player, 0);
        SELFTEST_CHECK(player->varps[varp] == 5, "post-quest talk must stay at 5, got %d",
                       player->varps[varp]);
        fprintf(stderr, "PASS fishingcontest postquest trigger=opnpc1,tunnel_dwarf observable=fishingcompo=5\n");
    }

fishcompo_done:
    if( spawned_a >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_a);
    if( spawned_m >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_m);
    if( spawned_j >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_j);
    if( spawned_b >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_b);
    if( spawned_s >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_s);
    if( spawned_spot >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_spot);
    ToriRSServer_WorldNpcReap(srv);
    fishcompo_release_park(srv, player);
    fprintf(stderr, "PASS fishingcontest start-to-complete via real triggers\n");
}
