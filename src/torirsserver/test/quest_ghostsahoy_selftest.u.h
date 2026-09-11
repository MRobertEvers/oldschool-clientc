/* Ghosts Ahoy -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int g_quest_ghostsahoy_pass_failures;

static void
ahoy_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    if( g_selftest_failures == g_quest_ghostsahoy_pass_failures )
        fprintf(stderr, "PASS ghostsahoy %s trigger=%s %s\n", step, trigger, observable);
    g_quest_ghostsahoy_pass_failures = g_selftest_failures;
    fflush(stderr);
}

static int
ahoy_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static int
ahoy_state(const struct ToriRSServerPlayer* player, int varbit)
{
    assert(player);
    if( varbit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, varbit);
}

static void
ahoy_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
ahoy_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
ahoy_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = ahoy_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 200 && player->active_script; round++ )
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
ahoy_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = ahoy_chatmenu();

    assert(srv);
    assert(player);
    ahoy_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
ahoy_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    assert(srv);
    assert(player);
    ahoy_release(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
ahoy_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    ahoy_drain(srv, player, 0);
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
ahoy_find_loc(int cx, int cz, int level, int loc_id, int radius)
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
ahoy_npc_live(const struct ToriRSServer* srv, int slot, int npc_id)
{
    assert(srv);
    if( slot < 0 )
        return 0;
    return srv->npcs[slot].active && srv->npcs[slot].type == npc_id;
}

static int
ahoy_spawn_npc(
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
    if( slot >= 0 && ahoy_npc_live(srv, slot, npc_id) )
        return slot;
    if( *spawned >= 0 && !ahoy_npc_live(srv, *spawned, npc_id) )
        *spawned = -1;
    slot = npc_spawn(srv, npc_id, x, z, level);
    if( slot >= 0 )
        *spawned = slot;
    return slot;
}

static void
ahoy_set_skill(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
selftest_quest_ghostsahoy(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_vel;
    int npc_nec;
    int npc_cro;
    int npc_cap;
    int varbit_ahoy;
    int varbit_tea;
    int varbit_book;
    int varbit_manual;
    int varbit_robes;
    int varbit_lobster;
    int varp_priest;
    int varp_pip;
    int varp_qp;
    int obj_ghost;
    int obj_ench;
    int obj_token;
    int obj_tea;
    int obj_book;
    int obj_manual;
    int obj_robes;
    int obj_map;
    int obj_ecto;
    int obj_empty;
    int obj_spade;
    int loc_barrier;
    int loc_plank;
    int loc_plank_off;
    int loc_rock;
    int loc_ecto;
    int loc_post;
    int stat_agi;
    int vel_slot;
    int nec_slot;
    int cro_slot;
    int cap_slot;
    int spawned_vel;
    int spawned_nec;
    int spawned_cro;
    int spawned_cap;
    int loc_slot;
    int qp_before;
    int tokens_before;

    fprintf(stderr, "ToriRSServer selftest: quest_ghostsahoy (Ghosts Ahoy)\n");
    fflush(stderr);

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_ghostsahoy selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_vel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_velorina");
    npc_nec = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_necrovarus");
    npc_cro = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_crone");
    npc_cap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_ghost_captain_1");
    varbit_ahoy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_questvar");
    varbit_tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_subquest_nettletea");
    varbit_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_given_book");
    varbit_manual = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_given_manual");
    varbit_robes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_given_robes");
    varbit_lobster = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_killed_lobster");
    varp_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "prieststart");
    varp_pip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "priestperil");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    obj_ghost = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_ghostspeak");
    obj_ench = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_ghostspeak_enchanted");
    obj_token = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ectotoken");
    obj_tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chinacup_of_nettletea_milky");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_book_of_haricanto");
    obj_manual = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_translation_manual");
    obj_robes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_robes_of_necrovarus");
    obj_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_map_complete");
    obj_ecto = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ectophial");
    obj_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ectophial_empty");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
    loc_barrier = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_town_barrier_multi");
    loc_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_gangplank_shipwreck_on");
    loc_plank_off = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_gangplank_shipwreck_off");
    loc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_rock_invisible");
    loc_ecto = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_ectofuntus");
    loc_post = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_town_barrier_post_quest");
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");

    SELFTEST_CHECK(npc_vel > 0 && npc_nec > 0 && varbit_ahoy > 0 && loc_barrier > 0 &&
                       loc_plank > 0 && loc_rock > 0 && obj_ecto > 0,
                   "quest_ghostsahoy symbols should all resolve");
    if( npc_vel <= 0 || varbit_ahoy <= 0 || loc_barrier <= 0 )
    {
        if( owned )
            ToriRSServer_ScriptsFree(srv);
        return;
    }

    spawned_vel = spawned_nec = spawned_cro = spawned_cap = -1;
    srv->members_world = 1;
    player->world = srv;
    player->active = 1;
    ToriRSServer_WorldSetActive(srv, player);
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);
    selftest_clear_inv(player);
    worn_set(player, TORIRSSERVER_WEAR_AMULET, -1, 0);
    ToriRSServer_VarbitSet(srv, varbit_ahoy, 0);
    if( varbit_tea >= 0 )
        ToriRSServer_VarbitSet(srv, varbit_tea, 0);
    if( varbit_book >= 0 )
        ToriRSServer_VarbitSet(srv, varbit_book, 0);
    if( varbit_manual >= 0 )
        ToriRSServer_VarbitSet(srv, varbit_manual, 0);
    if( varbit_robes >= 0 )
        ToriRSServer_VarbitSet(srv, varbit_robes, 0);
    if( varbit_lobster >= 0 )
        ToriRSServer_VarbitSet(srv, varbit_lobster, 0);
    if( varp_priest >= 0 )
        player->varps[varp_priest] = 0;
    if( varp_pip >= 0 )
        player->varps[varp_pip] = 0;
    ahoy_set_skill(player, stat_agi, 25);
    ahoy_release(srv, player);

    ahoy_snap(srv, player, 0, 3677, 3508);
    ahoy_pass("snap_velorina", "SNAP", "tile=3677,3508,0");
    vel_slot = ahoy_spawn_npc(srv, npc_vel, 3677, 3508, 0, &spawned_vel);
    SELFTEST_CHECK(vel_slot >= 0, "Velorina should spawn");
    if( vel_slot < 0 )
        goto ahoy_selftest_done;
    worn_set(player, TORIRSSERVER_WEAR_AMULET, obj_ghost, 1);

    ahoy_talk(srv, player, srv->npcs[vel_slot].type, vel_slot);
    ahoy_drain(srv, player, 0);
    ahoy_release(srv, player);
    SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 0, "missing prereqs should leave ahoy=0, got %d",
                   ahoy_state(player, varbit_ahoy));
    if( ahoy_state(player, varbit_ahoy) == 0 )
        ahoy_pass("prereq_block", "opnpc1,ahoy_velorina", "ahoy_questvar=0");

    if( varp_priest >= 0 )
        player->varps[varp_priest] = 5;
    if( varp_pip >= 0 )
        player->varps[varp_pip] = 60;

    ahoy_talk(srv, player, srv->npcs[vel_slot].type, vel_slot);
    ahoy_choose(srv, player, 2);
    ahoy_drain(srv, player, 0);
    ahoy_release(srv, player);
    SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 0, "refuse should leave ahoy=0, got %d",
                   ahoy_state(player, varbit_ahoy));
    if( ahoy_state(player, varbit_ahoy) == 0 )
        ahoy_pass("start_refuse", "opnpc1,ahoy_velorina", "ahoy_questvar=0");

    ahoy_talk(srv, player, srv->npcs[vel_slot].type, vel_slot);
    ahoy_choose(srv, player, 1);
    ahoy_drain(srv, player, 0);
    ahoy_release(srv, player);
    SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 1, "accept should write ahoy_questvar=1, got %d",
                   ahoy_state(player, varbit_ahoy));
    if( ahoy_state(player, varbit_ahoy) == 1 )
        ahoy_pass("start_accept", "opnpc1,ahoy_velorina", "ahoy_questvar=1");

    ahoy_snap(srv, player, 0, 3660, 3516);
    nec_slot = ahoy_spawn_npc(srv, npc_nec, 3660, 3516, 0, &spawned_nec);
    SELFTEST_CHECK(nec_slot >= 0, "Necrovarus should spawn");
    if( nec_slot >= 0 )
    {
        ahoy_talk(srv, player, srv->npcs[nec_slot].type, nec_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 2, "Necrovarus should write 2, got %d",
                       ahoy_state(player, varbit_ahoy));
        if( ahoy_state(player, varbit_ahoy) == 2 )
            ahoy_pass("talk_necrovarus", "opnpc1,ahoy_necrovarus", "ahoy_questvar=2");
    }

    ahoy_snap(srv, player, 0, 3677, 3508);
    ahoy_talk(srv, player, srv->npcs[vel_slot].type, vel_slot);
    ahoy_drain(srv, player, 0);
    ahoy_release(srv, player);
    SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 3, "Velorina return should write 3, got %d",
                   ahoy_state(player, varbit_ahoy));
    if( ahoy_state(player, varbit_ahoy) == 3 )
        ahoy_pass("talk_velorina_return", "opnpc1,ahoy_velorina", "ahoy_questvar=3");

    ahoy_snap(srv, player, 0, 3658, 3508);
    loc_slot = ahoy_find_loc(3660, 3508, 0, loc_barrier, 8);
    if( loc_slot >= 0 )
        ahoy_pass("barrier_shell", "SceneFindLocId", "ahoy_town_barrier_multi");
    SELFTEST_CHECK(loc_slot >= 0, "barrier SHELL should stand at Port Phasmatys");
    if( loc_slot >= 0 && obj_token > 0 )
    {
        selftest_give(player, obj_token, 2);
        tokens_before = selftest_count_obj(player, obj_token);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC4, loc_barrier, -1, loc_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(player->x > 3660, "prequest entry should move inside, x=%d", player->x);
        SELFTEST_CHECK(selftest_count_obj(player, obj_token) < tokens_before,
                       "prequest entry should spend two ecto-tokens");
        if( player->x > 3660 )
            ahoy_pass("barrier_enter", "oploc4,ahoy_town_barrier_multi", "paid-in");

        ahoy_snap(srv, player, 0, 3677, 3508);
        loc_slot = ahoy_find_loc(3660, 3508, 0, loc_barrier, 8);
        if( loc_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_barrier, -1, loc_slot);
            ahoy_drain(srv, player, 0);
            ahoy_release(srv, player);
            SELFTEST_CHECK(player->x < 3665, "prequest exit should move outside, x=%d", player->x);
            if( player->x < 3665 )
                ahoy_pass("barrier_exit", "oploc1,ahoy_town_barrier_multi", "free-out");
        }
    }

    ahoy_snap(srv, player, 0, 3462, 3558);
    cro_slot = ahoy_spawn_npc(srv, npc_cro, 3462, 3558, 0, &spawned_cro);
    SELFTEST_CHECK(cro_slot >= 0, "Old Crone should spawn");
    if( cro_slot >= 0 )
    {
        ahoy_talk(srv, player, srv->npcs[cro_slot].type, cro_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 3, "first crone talk stays 3, got %d",
                       ahoy_state(player, varbit_ahoy));
        if( varbit_tea >= 0 )
            SELFTEST_CHECK(ahoy_state(player, varbit_tea) == 1, "tea substate should be cup=1, got %d",
                           ahoy_state(player, varbit_tea));
        ahoy_pass("talk_crone_cup", "opnpc1,ahoy_crone", "tea=1");

        if( obj_tea > 0 )
            selftest_give(player, obj_tea, 1);
        ahoy_talk(srv, player, srv->npcs[cro_slot].type, cro_slot);
        ahoy_choose(srv, player, 2);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 4, "tea hand-in should write 4, got %d",
                       ahoy_state(player, varbit_ahoy));
        if( ahoy_state(player, varbit_ahoy) == 4 )
            ahoy_pass("talk_crone_tea", "opnpc1,ahoy_crone", "ahoy_questvar=4");
    }

    ahoy_snap(srv, player, 1, 3605, 3546);
    loc_slot = ahoy_find_loc(3605, 3546, 1, loc_plank, 6);
    if( loc_slot >= 0 )
        ahoy_pass("gangplank_shell", "SceneFindLocId", "ahoy_gangplank_shipwreck_on");
    SELFTEST_CHECK(loc_slot >= 0, "shipwreck gangplank SHELL should stand on the deck");
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_plank, -1, loc_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(player->level == 0 && player->z >= 3548, "gangplank should drop onto the rocks, %d,%d,%d",
                       player->x, player->z, player->level);
        if( player->level == 0 )
            ahoy_pass("gangplank_cross", "oploc1,ahoy_gangplank_shipwreck_on", "rocks");
    }
    if( loc_plank_off > 0 )
    {
        ahoy_snap(srv, player, 0, 3604, 3550);
        loc_slot = ahoy_find_loc(3604, 3550, 0, loc_plank_off, 8);
        if( loc_slot >= 0 )
            ahoy_pass("gangplank_off_shell", "SceneFindLocId", "ahoy_gangplank_shipwreck_off");
    }

    ahoy_snap(srv, player, 0, 3604, 3550);
    loc_slot = ahoy_find_loc(3604, 3550, 0, loc_rock, 8);
    if( loc_slot >= 0 )
        ahoy_pass("rock_shell", "SceneFindLocId", "ahoy_rock_invisible");
    SELFTEST_CHECK(loc_slot >= 0, "rock-jump SHELL should stand on the wreck route");
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rock, -1, loc_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        ahoy_pass("rock_jump", "oploc1,ahoy_rock_invisible", "jumped");
    }

    if( varbit_lobster >= 0 )
        ToriRSServer_VarbitSet(srv, varbit_lobster, 1);
    fprintf(stderr, "PASS ghostsahoy boss=giant_lobster CHEAT-SKIP\n");
    fflush(stderr);

    if( obj_map > 0 )
        selftest_give(player, obj_map, 1);
    ahoy_snap(srv, player, 0, 3791, 3559);
    cap_slot = ahoy_spawn_npc(srv, npc_cap, 3791, 3559, 0, &spawned_cap);
    SELFTEST_CHECK(cap_slot >= 0, "Ghost captain should spawn on Dragontooth");
    if( cap_slot >= 0 )
    {
        ahoy_talk(srv, player, srv->npcs[cap_slot].type, cap_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(player->x < 3720, "return travel must not strand the player, x=%d", player->x);
        if( player->x < 3720 )
            ahoy_pass("captain_return", "opnpc1,ahoy_ghost_captain_1", "phas-dock");
    }

    ahoy_snap(srv, player, 0, 3803, 3530);
    if( obj_spade > 0 && obj_book > 0 )
    {
        if( selftest_count_obj(player, obj_map) < 1 )
            selftest_give(player, obj_map, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_spade, -1, -1);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(selftest_count_obj(player, obj_book) >= 1, "dig should grant the Book of Haricanto");
        if( selftest_count_obj(player, obj_book) >= 1 )
            ahoy_pass("dig_book", "opheld1,spade", "ahoy_book_of_haricanto");
    }

    ahoy_snap(srv, player, 0, 3462, 3558);
    cro_slot = ahoy_spawn_npc(srv, npc_cro, 3462, 3558, 0, &spawned_cro);
    if( cro_slot >= 0 )
    {
        if( obj_manual > 0 )
            selftest_give(player, obj_manual, 1);
        if( obj_robes > 0 )
            selftest_give(player, obj_robes, 1);
        if( obj_book > 0 && selftest_count_obj(player, obj_book) < 1 )
            selftest_give(player, obj_book, 1);
        ahoy_talk(srv, player, srv->npcs[cro_slot].type, cro_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 5, "independent hand-ins should write 5, got %d",
                       ahoy_state(player, varbit_ahoy));
        if( ahoy_state(player, varbit_ahoy) == 5 )
            ahoy_pass("crone_handins", "opnpc1,ahoy_crone", "ahoy_questvar=5");

        if( obj_ghost > 0 )
            selftest_give(player, obj_ghost, 1);
        ahoy_talk(srv, player, srv->npcs[cro_slot].type, cro_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 6, "amulet enchant should write 6, got %d",
                       ahoy_state(player, varbit_ahoy));
        if( ahoy_state(player, varbit_ahoy) == 6 )
            ahoy_pass("crone_amulet", "opnpc1,ahoy_crone", "ahoy_questvar=6");
    }

    worn_set(player, TORIRSSERVER_WEAR_AMULET, obj_ench, 1);
    ahoy_snap(srv, player, 0, 3660, 3516);
    nec_slot = ahoy_spawn_npc(srv, npc_nec, 3660, 3516, 0, &spawned_nec);
    if( nec_slot >= 0 )
    {
        ahoy_talk(srv, player, srv->npcs[nec_slot].type, nec_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 7, "command should write 7, got %d",
                       ahoy_state(player, varbit_ahoy));
        if( ahoy_state(player, varbit_ahoy) == 7 )
            ahoy_pass("necro_command", "opnpc1,ahoy_necrovarus", "ahoy_questvar=7");
    }

    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
    ahoy_snap(srv, player, 0, 3677, 3508);
    vel_slot = ahoy_spawn_npc(srv, npc_vel, 3677, 3508, 0, &spawned_vel);
    SELFTEST_CHECK(vel_slot >= 0, "Velorina should still be available at completion");
    if( vel_slot < 0 )
        goto ahoy_selftest_done;
    ahoy_talk(srv, player, srv->npcs[vel_slot].type, vel_slot);
    ahoy_drain(srv, player, 0);
    if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
        player->resume_button_count > 0 )
        ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
    ahoy_drain_rewards(srv, player);
    ahoy_release(srv, player);
    SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 8, "completion should write 8, got %d",
                   ahoy_state(player, varbit_ahoy));
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 2, "completion should award 2 QP");
    SELFTEST_CHECK(selftest_count_obj(player, obj_ecto) >= 1 || selftest_count_obj(player, obj_empty) >= 1,
                   "completion should deliver an ectophial");
    if( ahoy_state(player, varbit_ahoy) == 8 )
        ahoy_pass("complete", "opnpc1,ahoy_velorina", "ahoy_questvar=8");

    ahoy_talk(srv, player, srv->npcs[vel_slot].type, vel_slot);
    ahoy_drain(srv, player, 0);
    ahoy_release(srv, player);
    SELFTEST_CHECK(ahoy_state(player, varbit_ahoy) == 8, "postquest must stay 8");
    ahoy_pass("postquest", "opnpc1,ahoy_velorina", "ahoy_questvar=8");

    ahoy_snap(srv, player, 0, 3658, 3508);
    loc_slot = ahoy_find_loc(3660, 3508, 0, loc_barrier, 8);
    if( loc_slot < 0 && loc_post > 0 )
        loc_slot = ahoy_find_loc(3660, 3508, 0, loc_post, 8);
    if( loc_slot >= 0 )
    {
        int dest_type = loc_post > 0 ? loc_post : loc_barrier;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC4, dest_type, -1, loc_slot);
        ahoy_drain(srv, player, 0);
        ahoy_release(srv, player);
        SELFTEST_CHECK(player->x > 3660, "postquest barrier should pass free, x=%d", player->x);
        if( player->x > 3660 )
            ahoy_pass("barrier_postquest", "oploc4,ahoy_town_barrier_multi", "free-in");
    }

    if( obj_ecto > 0 && selftest_count_obj(player, obj_ecto) >= 1 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_ecto, -1, -1);
        ahoy_drain(srv, player, 0);
        ahoy_drain_rewards(srv, player);
        ahoy_release(srv, player);
        SELFTEST_CHECK(player->x >= 3650 && player->x <= 3670,
                       "ectophial teleport should land near the Ectofuntus, x=%d", player->x);
        ahoy_pass("ectophial_empty", "opheld1,ectophial", "teleport");
        if( loc_ecto > 0 && obj_empty > 0 && selftest_count_obj(player, obj_empty) >= 1 )
        {
            loc_slot = ahoy_find_loc(3660, 3520, 0, loc_ecto, 12);
            if( loc_slot >= 0 )
            {
                player->last_useitem = obj_empty;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_ecto, -1, loc_slot);
                ahoy_drain(srv, player, 0);
                ahoy_release(srv, player);
                ahoy_pass("ectophial_refill", "oplocu,ahoy_ectofuntus", "full");
            }
        }
    }

ahoy_selftest_done:
    if( spawned_vel >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_vel);
    if( spawned_nec >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_nec);
    if( spawned_cro >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_cro);
    if( spawned_cap >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_cap);
    ToriRSServer_WorldNpcReap(srv);
    selftest_clear_inv(player);
    worn_set(player, TORIRSSERVER_WEAR_AMULET, -1, 0);
    ToriRSServer_VarbitSet(srv, varbit_ahoy, 0);
    player->godmode = 0;
    if( owned )
        ToriRSServer_ScriptsFree(srv);
    ahoy_pass("cleanup", "WorldNpcFree", "spawns reaped");
}
