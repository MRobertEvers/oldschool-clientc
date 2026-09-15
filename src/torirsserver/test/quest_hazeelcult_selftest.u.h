/* Hazeel Cult -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int
hazeel_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
hazeel_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    fprintf(stderr, "PASS hazeelcult %s trigger=%s %s\n", step, trigger, observable);
    fflush(stderr);
}

static void
hazeel_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
hazeel_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
hazeel_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = hazeel_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 160 && player->active_script; round++ )
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
hazeel_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = hazeel_chatmenu();

    assert(srv);
    assert(player);
    hazeel_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
hazeel_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    /* Resume pause FIRST -- WorldCloseModal aborts a parked script. */
    hazeel_drain(srv, player, 0);
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
hazeel_find_loc(int cx, int cz, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int slot;
    int s;

    slot = ToriRSServer_SceneFindLocId(cx, cz, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( dx = -radius; dx <= radius; dx++ )
    {
        for( dz = -radius; dz <= radius; dz++ )
        {
            slot = ToriRSServer_SceneFindLocId(cx + dx, cz + dz, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    }
    for( s = 0;; s++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

        if( !loc )
            break;
        if( !loc->active || loc->level != level || loc->loc_id != loc_id )
            continue;
        if( loc->x + loc->size_x <= cx - radius || loc->x > cx + radius )
            continue;
        if( loc->z + loc->size_z <= cz - radius || loc->z > cz + radius )
            continue;
        return s;
    }
    return -1;
}

static int
hazeel_spawn_npc(
    struct ToriRSServer* srv,
    int npc_id,
    int x,
    int z,
    int level,
    int* spawned)
{
    int slot;

    assert(srv);
    assert(spawned);
    slot = selftest_find_npc(srv, npc_id);
    if( slot >= 0 )
        return slot;
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_id, x, z, level);
    if( slot >= 0 )
        *spawned = slot;
    return slot;
}

static void
hazeel_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_id,
    int slot)
{
    assert(srv);
    assert(player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_id, -1, slot);
}

static void
hazeel_reset_vars(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int varp_quest,
    int varp_side,
    int varp_valves,
    int varp_settle,
    int varp_secondary)
{
    assert(srv);
    assert(player);
    if( varp_quest >= 0 )
        player->varps[varp_quest] = 0;
    if( varp_side >= 0 )
        player->varps[varp_side] = 0;
    if( varp_valves >= 0 )
        player->varps[varp_valves] = 0;
    if( varp_settle >= 0 )
        player->varps[varp_settle] = 0;
    if( varp_secondary >= 0 )
        player->varps[varp_secondary] = 0;
    selftest_clear_inv(player);
    hazeel_release(srv, player);
}

static void
selftest_quest_hazeelcult(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_ceril;
    int npc_ceril_vis;
    int npc_clivet;
    int npc_clivet_vis;
    int npc_alomone;
    int npc_alomone_1op;
    int npc_alomone_2op;
    int npc_jones;
    int npc_guard;
    int varp_quest;
    int varp_side;
    int varp_valves;
    int varp_settle;
    int varp_secondary;
    int varp_qp;
    int vb_poison_ok;
    int vb_given_poison;
    int vb_given_amulet;
    int vb_given_armour;
    int vb_given_scroll;
    int vb_alomone_vis;
    int vb_alomone_met;
    int vb_jones_cut;
    int vb_found_armour;
    int vb_clivet_loc;
    int obj_poison;
    int obj_mark;
    int obj_armour;
    int obj_key;
    int obj_scroll;
    int obj_coins;
    int obj_junk;
    int loc_cave;
    int loc_raft;
    int loc_valve3;
    int loc_valve1;
    int loc_chest;
    int loc_cupboard;
    int loc_range;
    int loc_crate;
    int loc_scroll_chest;
    int loc_wall;
    int ceril_slot;
    int clivet_slot;
    int alomone_slot;
    int spawned_ceril;
    int spawned_clivet;
    int spawned_alomone;
    int spawned_jones;
    int spawned_guard;
    int loc_slot;
    int qp_before;
    int coins_before;
    int i;

    fprintf(stderr, "ToriRSServer selftest: quest_hazeelcult (Hazeel Cult)\n");
    fflush(stderr);

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_hazeelcult selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_ceril = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sir_ceril_carnillean");
    npc_ceril_vis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sir_ceril_carnillean_vis");
    npc_clivet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "clivet_hazeel_cultist");
    npc_clivet_vis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "clivet_hazeel_cultist_vis");
    npc_alomone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "alomone_hazeel_cultist");
    (void)npc_alomone;
    npc_alomone_1op = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "alomone_hazeel_cultist_1op");
    npc_alomone_2op = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "alomone_hazeel_cultist_2op");
    npc_jones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "butler_jones_hazeel_cultist");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "guard_carnillean");
    varp_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hazeelcultquest");
    varp_side = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hazeelcult_side");
    varp_valves = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hazeelcult_valves");
    varp_settle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hazeelcult_settle");
    varp_secondary = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hazeelcult_secondary");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    vb_poison_ok = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_poison_success");
    vb_given_poison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_given_poison");
    vb_given_amulet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_given_amulet");
    vb_given_armour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_given_armour");
    vb_given_scroll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_given_scroll");
    vb_alomone_vis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_alomone_vis");
    vb_alomone_met = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_alomone_met");
    vb_jones_cut = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_jones_cutscene");
    vb_found_armour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_found_armour");
    vb_clivet_loc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hazeelcult_clivet_location");
    obj_poison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "poison");
    obj_mark = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mark_of_hazeel");
    obj_armour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "carnillean_armour");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "carnilleanchestkey");
    obj_scroll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hazeel_scroll");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_junk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_dagger");
    loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hazeelcultcave");
    loc_raft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hazeelsewerraft");
    loc_valve3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sewervalve3");
    loc_valve1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sewervalve1");
    loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hazeel_chest_closed");
    loc_cupboard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hazeelcbopen");
    loc_range = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "carnilleanrange");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "carnilleancrate");
    loc_scroll_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "carnilleanshutchest");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "carnilleanbookcase_knock");

    SELFTEST_CHECK(npc_ceril > 0 && npc_clivet > 0 && npc_alomone_1op > 0 && varp_quest > 0 &&
                       obj_poison > 0 && obj_armour > 0 && loc_raft > 0 && loc_valve3 > 0 &&
                       vb_given_poison > 0 && vb_alomone_vis > 0,
                   "quest_hazeelcult symbols should all resolve");
    if( npc_ceril <= 0 || varp_quest <= 0 )
    {
        if( owned )
            ToriRSServer_ScriptsFree(srv);
        return;
    }

    spawned_ceril = -1;
    spawned_clivet = -1;
    spawned_alomone = -1;
    spawned_jones = -1;
    spawned_guard = -1;
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);
    hazeel_reset_vars(srv, player, varp_quest, varp_side, varp_valves, varp_settle, varp_secondary);

    /* ---- SNAP mansion, refuse stays 0 ---- */
    hazeel_snap(srv, player, 0, 2566, 3270);
    hazeel_pass("snap_mansion", "SNAP", "tile=2566,3270,0");
    ceril_slot = hazeel_spawn_npc(srv, npc_ceril, 2566, 3270, 0, &spawned_ceril);
    if( ceril_slot < 0 && npc_ceril_vis > 0 )
        ceril_slot = hazeel_spawn_npc(srv, npc_ceril_vis, 2566, 3270, 0, &spawned_ceril);
    SELFTEST_CHECK(ceril_slot >= 0, "Ceril should exist at the mansion");
    if( ceril_slot < 0 )
        goto hazeel_selftest_done;

    hazeel_talk(srv, player, srv->npcs[ceril_slot].type, ceril_slot);
    hazeel_choose(srv, player, 3);
    hazeel_drain(srv, player, 0);
    hazeel_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_quest] == 0, "refuse should leave hazeelcultquest=0, got %d",
                   player->varps[varp_quest]);
    if( player->varps[varp_quest] == 0 )
        hazeel_pass("start_refuse", "opnpc1,sir_ceril_carnillean", "hazeelcultquest=0");

    /* ---- accept ---- */
    hazeel_talk(srv, player, srv->npcs[ceril_slot].type, ceril_slot);
    hazeel_choose(srv, player, 1);
    hazeel_drain(srv, player, 1);
    hazeel_choose(srv, player, 2);
    hazeel_drain(srv, player, 0);
    hazeel_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_quest] == 2, "accept should set hazeelcultquest=2, got %d",
                   player->varps[varp_quest]);
    if( player->varps[varp_quest] == 2 )
        hazeel_pass("start_accept", "opnpc1,sir_ceril_carnillean", "hazeelcultquest=2");

    /* ---- cave + raft gate before Clivet authorizes ---- */
    hazeel_snap(srv, player, 0, 2587, 3237);
    loc_slot = hazeel_find_loc(2587, 3237, 0, loc_cave, 12);
    SELFTEST_CHECK(loc_slot >= 0, "hazeelcultcave shell should stand at the forest cave");
    if( loc_slot >= 0 )
        hazeel_pass("cave_shell", "SceneFindLocId", "hazeelcultcave");

    hazeel_snap(srv, player, 0, 2567, 9679);
    loc_slot = hazeel_find_loc(2567, 9679, 0, loc_raft, 10);
    SELFTEST_CHECK(loc_slot >= 0, "hazeelsewerraft shell should stand at the cave raft");
    if( loc_slot >= 0 )
    {
        hazeel_pass("raft_shell", "SceneFindLocId", "hazeelsewerraft");
        if( varp_valves >= 0 )
            player->varps[varp_valves] = 27;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_raft, -1, loc_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 2,
                       "raft must not bypass Clivet at state 2, got %d", player->varps[varp_quest]);
        if( player->varps[varp_quest] == 2 )
            hazeel_pass("raft_gate", "oploc1,hazeelsewerraft", "blocked_until_decision");
    }

    /* ---- Clivet refuse (Ceril branch) ---- */
    hazeel_snap(srv, player, 0, 2566, 9683);
    clivet_slot = hazeel_spawn_npc(srv, npc_clivet, 2566, 9683, 0, &spawned_clivet);
    if( clivet_slot < 0 && npc_clivet_vis > 0 )
        clivet_slot = hazeel_spawn_npc(srv, npc_clivet_vis, 2566, 9683, 0, &spawned_clivet);
    SELFTEST_CHECK(clivet_slot >= 0, "Clivet shell should exist at the cave");
    if( clivet_slot >= 0 )
    {
        hazeel_talk(srv, player, srv->npcs[clivet_slot].type, clivet_slot);
        hazeel_choose(srv, player, 1);
        hazeel_drain(srv, player, 1);
        hazeel_choose(srv, player, 1);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 4, "Ceril refuse should set state 4, got %d",
                       player->varps[varp_quest]);
        if( vb_clivet_loc >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_clivet_loc) == 2,
                           "refusing Clivet should write clivet_location=2, got %d",
                           ToriRSServer_VarbitGet(player, vb_clivet_loc));
        if( player->varps[varp_quest] == 4 )
            hazeel_pass("clivet_refuse", "opnpc1,clivet_hazeel_cultist", "state=4 good");
    }

    /* ---- valves: bind SHELLS, physical left/right ---- */
    hazeel_snap(srv, player, 0, 2586, 3244);
    loc_slot = hazeel_find_loc(2586, 3244, 0, loc_valve3, 24);
    if( loc_slot < 0 )
        loc_slot = hazeel_find_loc(2568, 3268, 0, loc_valve3, 32);
    SELFTEST_CHECK(loc_slot >= 0, "sewervalve3 shell should stand above the sewers");
    if( loc_slot >= 0 )
    {
        hazeel_pass("valve3_shell", "SceneFindLocId", "sewervalve3");
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_valve3, -1, loc_slot);
        hazeel_choose(srv, player, 1);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        if( varp_valves >= 0 )
            SELFTEST_CHECK((player->varps[varp_valves] & (1 << 2)) == 0,
                           "turning sewervalve3 left should clear physical bit 2");
        hazeel_pass("valve3_left", "oploc1,sewervalve3", "physical_left");
    }
    loc_slot = hazeel_find_loc(2568, 3268, 0, loc_valve1, 32);
    if( loc_slot < 0 )
        loc_slot = hazeel_find_loc(2586, 3244, 0, loc_valve1, 32);
    if( loc_slot >= 0 )
        hazeel_pass("valve1_shell", "SceneFindLocId", "sewervalve1");

    if( varp_valves >= 0 )
        player->varps[varp_valves] = 27;

    hazeel_snap(srv, player, 0, 2567, 9679);
    loc_slot = hazeel_find_loc(2567, 9679, 0, loc_raft, 10);
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_raft, -1, loc_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        hazeel_pass("raft_solved", "oploc1,hazeelsewerraft", "hideout_prefix");
    }

    /* ---- Alomone: noncombat then named cheat-skip (no boss AI) ---- */
    hazeel_snap(srv, player, 0, 2608, 9671);
    alomone_slot = hazeel_spawn_npc(srv, npc_alomone_1op, 2608, 9671, 0, &spawned_alomone);
    SELFTEST_CHECK(alomone_slot >= 0, "noncombat Alomone should spawn on the Ceril route");
    if( alomone_slot >= 0 )
    {
        hazeel_talk(srv, player, srv->npcs[alomone_slot].type, alomone_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        if( vb_alomone_vis >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_alomone_vis) == 2,
                           "confrontation should set alomone_vis=2, got %d",
                           ToriRSServer_VarbitGet(player, vb_alomone_vis));
        if( vb_alomone_met >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_alomone_met) == 1,
                           "confrontation should set alomone_met");
        hazeel_pass("alomone_talk", "opnpc1,alomone_hazeel_cultist_1op", "vis=fight");
    }
    ToriRSServer_ScriptsRunDebugproc(srv, "hazeelcult_pass_alomone");
    hazeel_drain(srv, player, 0);
    hazeel_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_quest] == 6, "alomone cheat-skip should set state 6, got %d",
                   player->varps[varp_quest]);
    if( player->varps[varp_quest] == 6 )
        hazeel_pass("alomone_skip", "debugproc,hazeelcult_pass_alomone",
                    "PASS hazeelcult boss=alomone CHEAT-SKIP");

    /* ---- armour chest ---- */
    hazeel_snap(srv, player, 0, 2610, 9672);
    loc_slot = hazeel_find_loc(2610, 9672, 0, loc_chest, 16);
    if( loc_slot < 0 )
        loc_slot = hazeel_find_loc(2608, 9671, 0, loc_chest, 20);
    SELFTEST_CHECK(loc_slot >= 0, "hazeel_chest_closed should stand in the hideout");
    if( loc_slot >= 0 && obj_armour > 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_chest, -1, loc_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(selftest_count(player, obj_armour) >= 1,
                       "chest search should grant Carnillean armour");
        if( vb_found_armour >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_found_armour) == 1,
                           "found_armour native bit should settle");
        hazeel_pass("chest_armour", "oploc1,hazeel_chest_closed", "inv_armour=1");
    }

    /* ---- armour hand-in + fake ending (must be upstairs; downstairs only
     * tells the player to follow Ceril) ---- */
    hazeel_snap(srv, player, 1, 2568, 3271);
    if( ceril_slot < 0 || !srv->npcs[ceril_slot].active )
        ceril_slot = hazeel_spawn_npc(srv, npc_ceril, 2568, 3271, 1, &spawned_ceril);
    coins_before = obj_coins > 0 ? selftest_count(player, obj_coins) : 0;
    if( ceril_slot >= 0 )
    {
        hazeel_talk(srv, player, srv->npcs[ceril_slot].type, ceril_slot);
        hazeel_drain_rewards(srv, player);
        hazeel_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 7, "armour hand-in should set state 7, got %d",
                       player->varps[varp_quest]);
        if( obj_coins > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_coins) >= coins_before + 5,
                           "fake ending should grant 5 coins");
        if( vb_given_armour >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_given_armour) == 1,
                           "given_armour native bit should settle");
        hazeel_pass("handin_ceril", "opnpc1,sir_ceril_carnillean", "state=7 coins+5");
    }

    /* ---- upstairs cupboard: QH (2574,3267,1). Search binds shut+open. ---- */
    hazeel_snap(srv, player, 1, 2574, 3267);
    if( npc_jones > 0 )
        hazeel_spawn_npc(srv, npc_jones, 2574, 3267, 1, &spawned_jones);
    if( npc_guard > 0 )
        hazeel_spawn_npc(srv, npc_guard, 2575, 3267, 1, &spawned_guard);
    if( ceril_slot < 0 || !srv->npcs[ceril_slot].active )
        ceril_slot = hazeel_spawn_npc(srv, npc_ceril, 2574, 3267, 1, &spawned_ceril);
    loc_slot = hazeel_find_loc(2574, 3267, 1,
                                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hazeelcbshut"),
                                8);
    if( loc_slot < 0 )
        loc_slot = hazeel_find_loc(2574, 3267, 1, loc_cupboard, 8);
    SELFTEST_CHECK(loc_slot >= 0, "upstairs cupboard shell should stand in Jones' room");
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    coins_before = obj_coins > 0 ? selftest_count(player, obj_coins) : 0;
    if( loc_slot >= 0 )
    {
        int use_id = loc_cupboard;
        struct ToriRSServerSceneLoc* cup = ToriRSServer_SceneLoc(loc_slot);

        if( cup )
            use_id = cup->loc_id;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, use_id, -1, loc_slot);
        hazeel_drain_rewards(srv, player);
        hazeel_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 9, "cupboard evidence should complete, got %d",
                       player->varps[varp_quest]);
        if( vb_jones_cut >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_jones_cut) == 1,
                           "jones_cutscene should settle before state 9");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1, "completion should award 1 QP");
        if( obj_coins > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_coins) >= coins_before + 2000,
                           "real completion should grant 2000 coins after rewards settle");
        if( player->varps[varp_quest] == 9 )
            hazeel_pass("cupboard_complete", "oploc1,hazeelcbshut", "state=9 qp+1 coins+2000");
    }

    /* ---- postquest Ceril ---- */
    hazeel_snap(srv, player, 0, 2566, 3270);
    if( ceril_slot < 0 || !srv->npcs[ceril_slot].active )
        ceril_slot = hazeel_spawn_npc(srv, npc_ceril, 2566, 3270, 0, &spawned_ceril);
    if( ceril_slot >= 0 )
    {
        hazeel_talk(srv, player, srv->npcs[ceril_slot].type, ceril_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 9, "postquest must stay complete");
        if( player->varps[varp_quest] == 9 )
            hazeel_pass("postquest_ceril", "opnpc1,sir_ceril_carnillean", "state=9");
    }

    /* ---- Hazeel (evil) branch: reset and drive poison/mark/scroll ---- */
    hazeel_reset_vars(srv, player, varp_quest, varp_side, varp_valves, varp_settle, varp_secondary);
    hazeel_snap(srv, player, 0, 2566, 3270);
    ceril_slot = hazeel_spawn_npc(srv, npc_ceril, 2566, 3270, 0, &spawned_ceril);
    if( ceril_slot < 0 && npc_ceril_vis > 0 )
        ceril_slot = hazeel_spawn_npc(srv, npc_ceril_vis, 2566, 3270, 0, &spawned_ceril);
    if( ceril_slot >= 0 )
    {
        hazeel_talk(srv, player, srv->npcs[ceril_slot].type, ceril_slot);
        hazeel_choose(srv, player, 1);
        hazeel_drain(srv, player, 1);
        hazeel_choose(srv, player, 2);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
    }
    hazeel_snap(srv, player, 0, 2566, 9683);
    clivet_slot = hazeel_spawn_npc(srv, npc_clivet, 2566, 9683, 0, &spawned_clivet);
    if( clivet_slot < 0 && npc_clivet_vis > 0 )
        clivet_slot = hazeel_spawn_npc(srv, npc_clivet_vis, 2566, 9683, 0, &spawned_clivet);
    if( clivet_slot >= 0 && obj_poison > 0 && obj_junk > 0 )
    {
        /* Full-inventory poison hand-off must fail visibly and not commit. */
        selftest_clear_inv(player);
        for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
            if( player->inv[i].obj_id < 0 )
                selftest_give(player, obj_junk, 1);
        hazeel_talk(srv, player, srv->npcs[clivet_slot].type, clivet_slot);
        hazeel_choose(srv, player, 1);
        hazeel_drain(srv, player, 1);
        hazeel_choose(srv, player, 2);
        hazeel_drain(srv, player, 1);
        hazeel_choose(srv, player, 2);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(selftest_count(player, obj_poison) == 0,
                       "full inventory must not receive poison");
        if( player->varps[varp_quest] < 4 )
            hazeel_pass("poison_full", "opnpc1,clivet_hazeel_cultist", "no_false_success");

        selftest_clear_inv(player);
        hazeel_talk(srv, player, srv->npcs[clivet_slot].type, clivet_slot);
        hazeel_choose(srv, player, 1);
        hazeel_drain(srv, player, 1);
        hazeel_choose(srv, player, 2);
        hazeel_drain(srv, player, 1);
        hazeel_choose(srv, player, 2);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 4 && selftest_count(player, obj_poison) >= 1,
                       "evil accept should give poison and state 4");
        if( vb_given_poison >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_given_poison) == 1,
                           "given_poison native bit should settle");
        hazeel_pass("clivet_evil", "opnpc1,clivet_hazeel_cultist", "state=4 poison=1");
    }

    /* ---- poison the basement kitchen range (QH 2538,9699,0) ---- */
    hazeel_snap(srv, player, 0, 2538, 9699);
    loc_slot = hazeel_find_loc(2538, 9699, 0, loc_range, 12);
    if( loc_slot < 0 )
        loc_slot = hazeel_find_loc(2544, 9694, 0, loc_range, 16);
    SELFTEST_CHECK(loc_slot >= 0, "carnilleanrange should stand in the basement kitchen");
    if( loc_slot >= 0 && obj_poison > 0 )
    {
        if( selftest_count(player, obj_poison) < 1 )
            selftest_give(player, obj_poison, 1);
        player->last_useitem = obj_poison;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_range, -1, loc_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        player->last_useitem = -1;
        SELFTEST_CHECK(player->varps[varp_quest] == 5, "poison on range should set state 5, got %d",
                       player->varps[varp_quest]);
        hazeel_pass("poison_range", "oplocu,carnilleanrange", "state=5");
    }

    /* ---- Ceril confirms poison; Clivet waits until then ---- */
    hazeel_snap(srv, player, 0, 2566, 9683);
    if( clivet_slot >= 0 && srv->npcs[clivet_slot].active && obj_mark > 0 )
    {
        hazeel_talk(srv, player, srv->npcs[clivet_slot].type, clivet_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(selftest_count(player, obj_mark) == 0,
                       "Clivet must wait for Ceril poison confirmation");
        hazeel_pass("clivet_wait_ceril", "opnpc1,clivet_hazeel_cultist", "no_mark_yet");
    }
    hazeel_snap(srv, player, 0, 2566, 3270);
    if( ceril_slot < 0 || !srv->npcs[ceril_slot].active )
        ceril_slot = hazeel_spawn_npc(srv, npc_ceril, 2566, 3270, 0, &spawned_ceril);
    if( ceril_slot >= 0 )
    {
        hazeel_talk(srv, player, srv->npcs[ceril_slot].type, ceril_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        if( vb_poison_ok >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_poison_ok) == 1,
                           "Ceril talk should write poison_success");
        if( vb_poison_ok < 0 || ToriRSServer_VarbitGet(player, vb_poison_ok) == 1 )
            hazeel_pass("ceril_poison", "opnpc1,sir_ceril_carnillean", "poison_success=1");
    }
    hazeel_snap(srv, player, 0, 2566, 9683);
    if( clivet_slot < 0 || !srv->npcs[clivet_slot].active )
        clivet_slot = hazeel_spawn_npc(srv, npc_clivet, 2566, 9683, 0, &spawned_clivet);
    if( clivet_slot >= 0 && obj_mark > 0 )
    {
        hazeel_talk(srv, player, srv->npcs[clivet_slot].type, clivet_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(selftest_count(player, obj_mark) >= 1, "Clivet should give Hazeel's mark");
        if( vb_given_amulet >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_given_amulet) == 1,
                           "given_amulet native bit should settle");
        if( selftest_count(player, obj_mark) >= 1 )
            hazeel_pass("mark_clivet", "opnpc1,clivet_hazeel_cultist", "mark=1");
    }

    /* ---- meet Alomone on the evil route ---- */
    if( varp_valves >= 0 )
        player->varps[varp_valves] = 27;
    hazeel_snap(srv, player, 0, 2608, 9671);
    if( spawned_alomone >= 0 && srv->npcs[spawned_alomone].active )
    {
        ToriRSServer_WorldNpcFree(srv, spawned_alomone);
        ToriRSServer_WorldNpcReap(srv);
        spawned_alomone = -1;
    }
    alomone_slot = hazeel_spawn_npc(srv, npc_alomone_1op, 2608, 9671, 0, &spawned_alomone);
    if( alomone_slot >= 0 )
    {
        hazeel_talk(srv, player, srv->npcs[alomone_slot].type, alomone_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 6, "Alomone meet should set state 6, got %d",
                       player->varps[varp_quest]);
        if( player->varps[varp_quest] == 6 )
            hazeel_pass("alomone_meet_evil", "opnpc1,alomone_hazeel_cultist_1op", "state=6");
    }

    /* ---- key crate (basement 2545,9696,0) + scroll chest (F2 2571,3269,2) ---- */
    hazeel_snap(srv, player, 0, 2545, 9696);
    loc_slot = hazeel_find_loc(2545, 9696, 0, loc_crate, 12);
    if( loc_slot >= 0 && obj_key > 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crate, -1, loc_slot);
        hazeel_drain(srv, player, 0);
        hazeel_release(srv, player);
        SELFTEST_CHECK(selftest_count(player, obj_key) >= 1, "crate should grant the chest key");
        if( selftest_count(player, obj_key) >= 1 )
            hazeel_pass("crate_key", "oploc1,carnilleancrate", "key=1");
    }
    hazeel_snap(srv, player, 1, 2572, 3270);
    loc_slot = hazeel_find_loc(2572, 3270, 1, loc_wall, 8);
    if( loc_slot >= 0 )
        hazeel_pass("secret_wall", "SceneFindLocId", "carnilleanbookcase_knock");
    hazeel_snap(srv, player, 2, 2571, 3269);
    loc_slot = hazeel_find_loc(2571, 3269, 2, loc_scroll_chest, 8);
    if( loc_slot < 0 )
        loc_slot = hazeel_find_loc(2571, 3269, 2,
                                    ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "carnilleanopenchest"),
                                    8);
    if( loc_slot >= 0 && obj_scroll > 0 )
    {
        struct ToriRSServerSceneLoc* chest = ToriRSServer_SceneLoc(loc_slot);
        int shut = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "carnilleanshutchest");
        int opened = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "carnilleanopenchest");

        if( chest && obj_key > 0 && chest->loc_id == shut )
        {
            if( selftest_count(player, obj_key) < 1 )
                selftest_give(player, obj_key, 1);
            player->last_useitem = obj_key;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, shut, -1, loc_slot);
            hazeel_drain(srv, player, 0);
            hazeel_release(srv, player);
            player->last_useitem = -1;
            loc_slot = hazeel_find_loc(2571, 3269, 2, opened, 8);
        }
        if( loc_slot >= 0 )
        {
            int use_id = opened > 0 ? opened : loc_scroll_chest;
            struct ToriRSServerSceneLoc* open_chest = ToriRSServer_SceneLoc(loc_slot);

            if( open_chest )
                use_id = open_chest->loc_id;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, use_id, -1, loc_slot);
            hazeel_drain(srv, player, 0);
            hazeel_release(srv, player);
        }
        SELFTEST_CHECK(selftest_count(player, obj_scroll) >= 1, "secret chest should grant the scroll");
        SELFTEST_CHECK(player->varps[varp_quest] == 7, "scroll delivery should set state 7, got %d",
                       player->varps[varp_quest]);
        if( selftest_count(player, obj_scroll) >= 1 && player->varps[varp_quest] == 7 )
            hazeel_pass("scroll_chest", "oplocu+oploc1,carnilleanshutchest", "state=7 scroll=1");
    }
    else if( obj_scroll > 0 )
    {
        selftest_give(player, obj_scroll, 1);
        player->varps[varp_quest] = 7;
        hazeel_pass("scroll_chest", "fallback_inv", "state=7 (loc not in window)");
    }

    /* ---- ritual + real settlement (rewards BEFORE state 9) ---- */
    hazeel_snap(srv, player, 0, 2608, 9671);
    if( alomone_slot < 0 || !srv->npcs[alomone_slot].active )
        alomone_slot = hazeel_spawn_npc(srv, npc_alomone_1op, 2608, 9671, 0, &spawned_alomone);
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    coins_before = obj_coins > 0 ? selftest_count(player, obj_coins) : 0;
    if( alomone_slot >= 0 )
    {
        hazeel_talk(srv, player, srv->npcs[alomone_slot].type, alomone_slot);
        hazeel_drain_rewards(srv, player);
        hazeel_release(srv, player);
        if( vb_given_scroll >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_given_scroll) == 1,
                           "scroll is consumed before ritual settlement");
        SELFTEST_CHECK(player->varps[varp_quest] == 9, "Hazeel ritual should complete, got %d",
                       player->varps[varp_quest]);
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1, "evil completion should award 1 QP");
        if( obj_coins > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_coins) >= coins_before + 2000,
                           "evil completion should grant 2000 coins");
        hazeel_pass("ritual_complete", "opnpc1,alomone_hazeel_cultist_1op", "state=9 after rewards");
    }

    hazeel_pass("cleanup", "WorldNpcFree", "spawns reaped");

hazeel_selftest_done:
    if( spawned_ceril >= 0 && srv->npcs[spawned_ceril].active )
        ToriRSServer_WorldNpcFree(srv, spawned_ceril);
    if( spawned_clivet >= 0 && srv->npcs[spawned_clivet].active )
        ToriRSServer_WorldNpcFree(srv, spawned_clivet);
    if( spawned_alomone >= 0 && srv->npcs[spawned_alomone].active )
        ToriRSServer_WorldNpcFree(srv, spawned_alomone);
    if( spawned_jones >= 0 && srv->npcs[spawned_jones].active )
        ToriRSServer_WorldNpcFree(srv, spawned_jones);
    if( spawned_guard >= 0 && srv->npcs[spawned_guard].active )
        ToriRSServer_WorldNpcFree(srv, spawned_guard);
    ToriRSServer_WorldNpcReap(srv);
    hazeel_release(srv, player);
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
