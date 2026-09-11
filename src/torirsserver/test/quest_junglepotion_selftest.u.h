/* Jungle Potion -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int g_jungle_pass_failures;

static void
jungle_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    if( g_selftest_failures == g_jungle_pass_failures )
        fprintf(stderr, "PASS junglepotion %s trigger=%s %s\n", step, trigger, observable);
    g_jungle_pass_failures = g_selftest_failures;
    fflush(stderr);
}

static int
jungle_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
jungle_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
jungle_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
jungle_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = jungle_chatmenu();
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
jungle_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = jungle_chatmenu();

    assert(srv);
    assert(player);
    jungle_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
jungle_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    /* Resume pause FIRST -- WorldCloseModal aborts a parked script. */
    jungle_drain(srv, player, 0);
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
jungle_find_loc(int cx, int cz, int level, int loc_id, int radius)
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
jungle_find_loc_anywhere(int loc_id, int* out_x, int* out_z)
{
    int s;

    for( s = 0;; s++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

        if( !loc )
            break;
        if( !loc->active || loc->loc_id != loc_id )
            continue;
        if( out_x )
            *out_x = loc->x;
        if( out_z )
            *out_z = loc->z;
        return s;
    }
    return -1;
}

static int
jungle_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            return i;
    return -1;
}

static void
jungle_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    assert(srv);
    assert(player);
    jungle_release(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
jungle_start_to_confirm(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc, int slot)
{
    jungle_talk(srv, player, npc, slot);
    jungle_choose(srv, player, 3);
    jungle_choose(srv, player, 1);
    jungle_choose(srv, player, 2);
}

static int
jungle_search(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_id,
    int cx,
    int cz,
    int level,
    int grimy)
{
    int loc_slot;
    int before;

    jungle_snap(srv, player, level, cx, cz);
    loc_slot = jungle_find_loc(cx, cz, level, loc_id, 8);
    if( loc_slot < 0 )
        return -1;
    before = selftest_count(player, grimy);
    jungle_release(srv, player);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_id, -1, loc_slot);
    jungle_drain(srv, player, 0);
    jungle_release(srv, player);
    return selftest_count(player, grimy) > before ? 1 : 0;
}

static int
jungle_clean(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int grimy,
    int clean)
{
    int slot;

    slot = jungle_inv_slot(player, grimy);
    if( slot < 0 )
        return 0;
    player->last_slot = slot;
    selftest_opheld(srv, 1, slot);
    jungle_drain(srv, player, 0);
    jungle_release(srv, player);
    return selftest_count(player, clean) > 0;
}

static void
jungle_handin(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc,
    int slot)
{
    jungle_talk(srv, player, npc, slot);
    jungle_choose(srv, player, 1);
    jungle_drain(srv, player, 0);
    jungle_release(srv, player);
}

static void
selftest_quest_junglepotion(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_trufitus;
    int varp_jp;
    int varp_druid;
    int varp_qp;
    int varp_zq;
    int stat_herb;
    int obj_u_snake;
    int obj_snake;
    int obj_u_ard;
    int obj_ard;
    int obj_u_sito;
    int obj_sito;
    int obj_u_vol;
    int obj_vol;
    int obj_u_purse;
    int obj_purse;
    int obj_coins;
    int obj_belt;
    int loc_vine;
    int loc_palm;
    int loc_soil;
    int loc_rock;
    int loc_wall;
    int loc_wall_empty;
    int loc_cave;
    int loc_holds;
    int truf_slot;
    int spawned;
    int qp_before;
    int xp_before;
    int i;
    int got;
    int fails;
    int wall_slot;

    fprintf(stderr, "ToriRSServer selftest: quest_junglepotion (Jungle Potion)\n");
    fflush(stderr);
    g_jungle_pass_failures = g_selftest_failures;

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_junglepotion selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_trufitus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trufitus");
    varp_jp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "junglepotion");
    varp_druid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "druidquest");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    varp_zq = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "zombiequeen");
    stat_herb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
    obj_u_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_snake_weed");
    obj_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "snake_weed");
    obj_u_ard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_ardrigal");
    obj_ard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ardrigal");
    obj_u_sito = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_sito_foil");
    obj_sito = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sito_foil");
    obj_u_vol = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_volencia_moss");
    obj_vol = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "volencia_moss");
    obj_u_purse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_rogues_purse");
    obj_purse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rogues_purse");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_belt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mosol_wampum_belt");
    loc_vine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "snake_vine_full");
    loc_palm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ardrigal_palm_full");
    loc_soil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sito_soil_full");
    loc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "volencia_moss_rock_full");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rogues_purse_cave_full");
    loc_wall_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rogues_purse_cave_empty");
    loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pothole_cave_entrance");
    loc_holds = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "jp_caverocksout");

    SELFTEST_CHECK(npc_trufitus >= 0 && varp_jp >= 0 && varp_druid >= 0 && stat_herb >= 0 &&
                       loc_vine >= 0 && loc_palm >= 0 && loc_soil >= 0 && loc_rock >= 0 &&
                       loc_wall >= 0 && loc_wall_empty >= 0 && loc_cave >= 0 && loc_holds >= 0 &&
                       obj_u_snake >= 0 && obj_snake >= 0 && obj_u_ard >= 0 && obj_ard >= 0 &&
                       obj_u_sito >= 0 && obj_sito >= 0 && obj_u_vol >= 0 && obj_vol >= 0 &&
                       obj_u_purse >= 0 && obj_purse >= 0,
                   "quest_junglepotion symbols should resolve");
    if( npc_trufitus < 0 || varp_jp < 0 || varp_druid < 0 || stat_herb < 0 )
        return;

    srv->members_world = 1;
    ToriRSServer_WorldSetActive(srv, player);
    player->godmode = 1;
    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    player->varps[varp_jp] = 0;
    player->varps[varp_druid] = 0;
    if( varp_zq >= 0 )
        player->varps[varp_zq] = 0;
    ToriRSServer_CombatSetLevel(player, stat_herb, 3);
    spawned = -1;

    jungle_snap(srv, player, 0, 2809, 3086);
    truf_slot = selftest_find_npc(srv, npc_trufitus);
    if( truf_slot < 0 )
    {
        truf_slot = ToriRSServer_WorldNpcSpawn(srv, npc_trufitus, 2809, 3086, 0);
        spawned = truf_slot;
    }
    SELFTEST_CHECK(truf_slot >= 0, "trufitus should exist at 2809,3086");
    if( truf_slot < 0 )
        goto jungle_done;

    /* ---- refuse: busy farewell leaves 0 ---- */
    jungle_talk(srv, player, npc_trufitus, truf_slot);
    jungle_choose(srv, player, 3);
    jungle_choose(srv, player, 2);
    jungle_drain(srv, player, 0);
    jungle_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_jp] == 0, "busy refuse should leave junglepotion=0, got %d",
                   player->varps[varp_jp]);
    jungle_pass("start_refuse", "opnpc1,trufitus", "junglepotion=0");

    /* ---- Yes confirm without Druidic Ritual stays 0 ---- */
    jungle_start_to_confirm(srv, player, npc_trufitus, truf_slot);
    jungle_choose(srv, player, 1);
    jungle_drain(srv, player, 0);
    jungle_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_jp] == 0, "Druid gate should leave junglepotion=0, got %d",
                   player->varps[varp_jp]);
    jungle_pass("start_druid_refuse", "opnpc1,trufitus", "junglepotion=0");

    /* ---- No on Yes/No confirm stays 0 ---- */
    player->varps[varp_druid] = 4;
    jungle_start_to_confirm(srv, player, npc_trufitus, truf_slot);
    jungle_choose(srv, player, 2);
    jungle_drain(srv, player, 0);
    jungle_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_jp] == 0, "No confirm should leave junglepotion=0, got %d",
                   player->varps[varp_jp]);
    jungle_pass("start_confirm_no", "opnpc1,trufitus", "junglepotion=0");

    /* ---- accept after Yes + Druid complete ---- */
    jungle_start_to_confirm(srv, player, npc_trufitus, truf_slot);
    jungle_choose(srv, player, 1);
    jungle_drain(srv, player, 0);
    jungle_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_jp] == 1, "accept should write junglepotion=1, got %d",
                   player->varps[varp_jp]);
    jungle_pass("start_accept", "opnpc1,trufitus", "junglepotion=1");

    /* ---- future source gated ---- */
    jungle_snap(srv, player, 0, 2871, 3116);
    SELFTEST_CHECK(jungle_find_loc(2871, 3116, 0, loc_palm, 8) >= 0,
                   "ardrigal_palm_full should stand at 2871,3116");
    got = jungle_search(srv, player, loc_palm, 2871, 3116, 0, obj_u_ard);
    SELFTEST_CHECK(got == 0 && selftest_count(player, obj_u_ard) == 0,
                   "ardrigal must stay locked at state 1");
    jungle_pass("gate_ardrigal", "oploc2,ardrigal_palm_full", "no_item");

    /* ---- snake roll can fail at Herblore 1 ---- */
    ToriRSServer_CombatSetLevel(player, stat_herb, 1);
    jungle_snap(srv, player, 0, 2763, 3044);
    SELFTEST_CHECK(jungle_find_loc(2763, 3044, 0, loc_vine, 8) >= 0,
                   "snake_vine_full should stand at 2763,3044");
    fails = 0;
    for( i = 0; i < 12; i++ )
    {
        got = jungle_search(srv, player, loc_vine, 2763, 3044, 0, obj_u_snake);
        if( got == 0 )
            fails++;
        if( selftest_count(player, obj_u_snake) > 0 )
            break;
    }
    SELFTEST_CHECK(fails > 0, "snake-weed Herblore-1 roll must fail at least once in 12 tries");
    jungle_pass("snake_roll_fail", "oploc2,snake_vine_full", "herblore1_miss");
    selftest_clear_inv(player);
    for( i = 0; i < 110; i++ )
        selftest_tick(srv);

    /* ---- snake gather at 99, clean, hand-in ---- */
    ToriRSServer_CombatSetLevel(player, stat_herb, 99);
    got = 0;
    for( i = 0; i < 40 && !got; i++ )
        got = jungle_search(srv, player, loc_vine, 2763, 3044, 0, obj_u_snake) == 1;
    SELFTEST_CHECK(got && player->varps[varp_jp] == 2, "snake harvest should set junglepotion=2, got %d",
                   player->varps[varp_jp]);
    jungle_pass("gather_snake", "oploc2,snake_vine_full", "junglepotion=2");
    SELFTEST_CHECK(jungle_clean(srv, player, obj_u_snake, obj_snake),
                   "OPHELD1 should clean unidentified_snake_weed");
    jungle_pass("clean_snake", "opheld1,unidentified_snake_weed", "inv_snake_weed");
    jungle_snap(srv, player, 0, 2809, 3086);
    jungle_handin(srv, player, npc_trufitus, truf_slot);
    SELFTEST_CHECK(player->varps[varp_jp] == 3 && selftest_count(player, obj_snake) == 0,
                   "snake hand-in should delete herb and write 3, got %d", player->varps[varp_jp]);
    jungle_pass("handin_snake", "opnpc1,trufitus", "junglepotion=3");

    /* ---- ardrigal ---- */
    got = jungle_search(srv, player, loc_palm, 2871, 3116, 0, obj_u_ard);
    SELFTEST_CHECK(got == 1 && player->varps[varp_jp] == 4, "ardrigal harvest should set 4, got %d",
                   player->varps[varp_jp]);
    jungle_pass("gather_ardrigal", "oploc2,ardrigal_palm_full", "junglepotion=4");
    SELFTEST_CHECK(jungle_clean(srv, player, obj_u_ard, obj_ard), "clean ardrigal");
    jungle_pass("clean_ardrigal", "opheld1,unidentified_ardrigal", "inv_ardrigal");
    jungle_snap(srv, player, 0, 2809, 3086);
    jungle_handin(srv, player, npc_trufitus, truf_slot);
    SELFTEST_CHECK(player->varps[varp_jp] == 5 && selftest_count(player, obj_ard) == 0,
                   "ardrigal hand-in should write 5, got %d", player->varps[varp_jp]);
    jungle_pass("handin_ardrigal", "opnpc1,trufitus", "junglepotion=5");

    /* ---- sito foil ---- */
    jungle_snap(srv, player, 0, 2791, 3047);
    SELFTEST_CHECK(jungle_find_loc(2791, 3047, 0, loc_soil, 8) >= 0,
                   "sito_soil_full should stand at 2791,3047");
    got = jungle_search(srv, player, loc_soil, 2791, 3047, 0, obj_u_sito);
    SELFTEST_CHECK(got == 1 && player->varps[varp_jp] == 6, "sito harvest should set 6, got %d",
                   player->varps[varp_jp]);
    jungle_pass("gather_sito", "oploc2,sito_soil_full", "junglepotion=6");
    SELFTEST_CHECK(jungle_clean(srv, player, obj_u_sito, obj_sito), "clean sito foil");
    jungle_pass("clean_sito", "opheld1,unidentified_sito_foil", "inv_sito_foil");
    jungle_snap(srv, player, 0, 2809, 3086);
    jungle_handin(srv, player, npc_trufitus, truf_slot);
    SELFTEST_CHECK(player->varps[varp_jp] == 7 && selftest_count(player, obj_sito) == 0,
                   "sito hand-in should write 7, got %d", player->varps[varp_jp]);
    jungle_pass("handin_sito", "opnpc1,trufitus", "junglepotion=7");

    /* ---- volencia moss ---- */
    jungle_snap(srv, player, 0, 2851, 3036);
    SELFTEST_CHECK(jungle_find_loc(2851, 3036, 0, loc_rock, 8) >= 0,
                   "volencia_moss_rock_full should stand at 2851,3036");
    got = jungle_search(srv, player, loc_rock, 2851, 3036, 0, obj_u_vol);
    SELFTEST_CHECK(got == 1 && player->varps[varp_jp] == 8, "volencia harvest should set 8, got %d",
                   player->varps[varp_jp]);
    jungle_pass("gather_volencia", "oploc2,volencia_moss_rock_full", "junglepotion=8");
    SELFTEST_CHECK(jungle_clean(srv, player, obj_u_vol, obj_vol), "clean volencia moss");
    jungle_pass("clean_volencia", "opheld1,unidentified_volencia_moss", "inv_volencia_moss");
    jungle_snap(srv, player, 0, 2809, 3086);
    jungle_handin(srv, player, npc_trufitus, truf_slot);
    SELFTEST_CHECK(player->varps[varp_jp] == 9 && selftest_count(player, obj_vol) == 0,
                   "volencia hand-in should write 9, got %d", player->varps[varp_jp]);
    jungle_pass("handin_volencia", "opnpc1,trufitus", "junglepotion=9");

    /* ---- Pothole enter + rogue's purse wall depletes ---- */
    jungle_snap(srv, player, 0, 2825, 3119);
    SELFTEST_CHECK(jungle_find_loc(2825, 3119, 0, loc_cave, 8) >= 0,
                   "pothole_cave_entrance should stand at 2825,3119");
    {
        int cave_slot = jungle_find_loc(2825, 3119, 0, loc_cave, 8);

        jungle_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_cave, -1, cave_slot);
        jungle_choose(srv, player, 1);
        jungle_drain(srv, player, 0);
        jungle_release(srv, player);
        selftest_tick(srv);
    }
    SELFTEST_CHECK(player->z >= 9400, "cave Yes should telejump into Pothole, z=%d", player->z);
    jungle_pass("cave_enter", "oploc2,pothole_cave_entrance", "tele_pothole");

    {
        int wall_x = 2824;
        int wall_z = 9462;

        jungle_snap(srv, player, 0, 2824, 9462);
        wall_slot = jungle_find_loc(2824, 9462, 0, loc_wall, 16);
        if( wall_slot < 0 )
        {
            jungle_snap(srv, player, 0, 2883, 9533);
            wall_slot = jungle_find_loc(2883, 9533, 0, loc_wall, 16);
            wall_x = 2883;
            wall_z = 9533;
        }
        if( wall_slot < 0 )
        {
            jungle_snap(srv, player, 0, 2830, 9522);
            wall_slot = jungle_find_loc_anywhere(loc_wall, &wall_x, &wall_z);
        }
        SELFTEST_CHECK(wall_slot >= 0, "rogues_purse_cave_full should stand in Pothole");
        if( wall_slot >= 0 )
        {
            struct ToriRSServerSceneLoc* wall = ToriRSServer_SceneLoc(wall_slot);

            if( wall )
            {
                wall_x = wall->x;
                wall_z = wall->z;
            }
        }
        got = jungle_search(srv, player, loc_wall, wall_x, wall_z, 0, obj_u_purse);
        SELFTEST_CHECK(got == 1 && player->varps[varp_jp] == 10, "purse harvest should set 10, got %d",
                       player->varps[varp_jp]);
        jungle_pass("gather_purse", "oploc2,rogues_purse_cave_full", "junglepotion=10");
        SELFTEST_CHECK(jungle_find_loc(wall_x, wall_z, 0, loc_wall_empty, 8) >= 0 ||
                           jungle_find_loc_anywhere(loc_wall_empty, NULL, NULL) >= 0,
                       "rogues_purse_cave_empty should replace the searched wall");
        jungle_pass("purse_deplete", "oploc2,rogues_purse_cave_full", "empty_100t");
    }
    SELFTEST_CHECK(jungle_clean(srv, player, obj_u_purse, obj_purse), "clean rogue's purse");
    jungle_pass("clean_purse", "opheld1,unidentified_rogues_purse", "inv_rogues_purse");

    jungle_snap(srv, player, 0, 2830, 9520);
    {
        int hold_slot = jungle_find_loc(2830, 9520, 0, loc_holds, 24);

        if( hold_slot < 0 )
            hold_slot = jungle_find_loc_anywhere(loc_holds, NULL, NULL);
        if( hold_slot >= 0 )
        {
            jungle_release(srv, player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_holds, -1, hold_slot);
            jungle_drain(srv, player, 0);
            jungle_release(srv, player);
            selftest_tick(srv);
        }
    }
    SELFTEST_CHECK(player->z < 4000, "handholds should climb out of Pothole, z=%d", player->z);
    jungle_pass("cave_exit", "oploc1,jp_caverocksout", "tele_surface");

    /* ---- final hand-in + atomic complete ---- */
    jungle_snap(srv, player, 0, 2809, 3086);
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    xp_before = player->stat_xp_tenths[stat_herb];
    jungle_handin(srv, player, npc_trufitus, truf_slot);
    SELFTEST_CHECK(player->varps[varp_jp] == 11 || player->varps[varp_jp] == 12,
                   "final hand-in should reach 11 (or 12 if queue already fired), got %d",
                   player->varps[varp_jp]);
    if( player->varps[varp_jp] == 11 )
    {
        jungle_talk(srv, player, npc_trufitus, truf_slot);
        jungle_drain_rewards(srv, player);
    }
    else
    {
        jungle_drain_rewards(srv, player);
    }
    SELFTEST_CHECK(player->varps[varp_jp] == 12, "completion should write junglepotion=12, got %d",
                   player->varps[varp_jp]);
    SELFTEST_CHECK(player->stat_xp_tenths[stat_herb] >= xp_before + 7750,
                   "completion should grant 775 Herblore XP");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1, "completion should grant 1 QP");
    jungle_pass("complete", "opnpc1,trufitus", "junglepotion=12 qp+1 xp+775");

    /* ---- replay at 12 does not grant again ---- */
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    xp_before = player->stat_xp_tenths[stat_herb];
    jungle_talk(srv, player, npc_trufitus, truf_slot);
    jungle_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp_jp] == 13, "postquest talk should write junglepotion=13, got %d",
                   player->varps[varp_jp]);
    SELFTEST_CHECK(player->stat_xp_tenths[stat_herb] == xp_before, "postquest must not re-grant XP");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before, "postquest must not re-grant QP");
    jungle_pass("postquest", "opnpc1,trufitus", "junglepotion=13");

    /* ---- post-quest herb sale pays 1-4 ---- */
    selftest_clear_inv(player);
    selftest_give(player, obj_snake, 1);
    player->last_useitem = obj_snake;
    player->last_useslot = jungle_inv_slot(player, obj_snake);
    jungle_release(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, truf_slot);
    jungle_drain(srv, player, 0);
    jungle_release(srv, player);
    {
        int coins = obj_coins >= 0 ? selftest_count(player, obj_coins) : 0;

        SELFTEST_CHECK(selftest_count(player, obj_snake) == 0, "sale should consume one clean herb");
        SELFTEST_CHECK(coins >= 1 && coins <= 4, "sale should pay 1-4 coins, got %d", coins);
        jungle_pass("herb_sale", "opnpcu,trufitus", "coins_1to4");
    }

    /* ---- Shilo Wampum still reachable after Jungle complete ---- */
    if( obj_belt >= 0 && varp_zq >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_belt, 1);
        player->varps[varp_zq] = 0;
        player->last_useitem = obj_belt;
        player->last_useslot = jungle_inv_slot(player, obj_belt);
        jungle_release(srv, player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, truf_slot);
        jungle_choose(srv, player, 1);
        jungle_choose(srv, player, 1);
        jungle_drain(srv, player, 0);
        jungle_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_zq] == 1, "Wampum on Trufitus should start Shilo, zq=%d",
                       player->varps[varp_zq]);
        SELFTEST_CHECK(player->varps[varp_jp] == 13, "Shilo start must not rewrite junglepotion, got %d",
                       player->varps[varp_jp]);
        jungle_pass("shilo_wampum", "opnpcu,trufitus", "zombiequeen=1");
    }

jungle_done:
    if( spawned >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, spawned);
        ToriRSServer_WorldNpcReap(srv);
        jungle_pass("cleanup", "WorldNpcFree", "spawns reaped");
    }
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
