/* Eagles' Peak -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int g_quest_eaglepeak_pass_failures;

static void
ep_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    if( g_selftest_failures == g_quest_eaglepeak_pass_failures )
        fprintf(stderr, "PASS eaglespeak %s trigger=%s %s\n", step, trigger, observable);
    g_quest_eaglepeak_pass_failures = g_selftest_failures;
    fflush(stderr);
}

static int
ep_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static int
ep_state(const struct ToriRSServerPlayer* player, int varbit)
{
    assert(player);
    if( varbit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, varbit);
}

static void
ep_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
ep_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
ep_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = ep_chatmenu();
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
ep_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = ep_chatmenu();

    assert(srv);
    assert(player);
    ep_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
ep_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    ep_drain(srv, player, 0);
    for( i = 0; i < 48; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
ep_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    assert(srv);
    assert(player);
    ep_release(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static int
ep_find_loc(int cx, int cz, int level, int loc_id, int radius)
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
ep_ensure_npc(
    struct ToriRSServer* srv,
    int npc_type,
    int x,
    int z,
    int level,
    int* spawned)
{
    int slot;

    assert(srv);
    assert(spawned);
    slot = selftest_find_npc(srv, npc_type);
    if( slot >= 0 )
        return slot;
    slot = npc_spawn(srv, npc_type, x, z, level);
    *spawned = slot;
    return slot;
}

static void
ep_set_skill(struct ToriRSServerPlayer* player, int stat, int base, int boosted)
{
    assert(player);
    if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        return;
    player->stat_level[stat] = base;
    player->stat_boosted[stat] = boosted;
}

static void
ep_take_one(struct ToriRSServerPlayer* player, int obj_id)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id != obj_id || player->inv[i].count <= 0 )
            continue;
        player->inv[i].count--;
        if( player->inv[i].count <= 0 )
        {
            player->inv[i].obj_id = -1;
            player->inv[i].count = 0;
        }
        player->inv_dirty |= 1u << i;
        return;
    }
}

static void
ep_oploc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int trigger,
    int loc_id,
    int loc_slot)
{
    assert(srv);
    assert(player);
    ep_release(srv, player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, trigger, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, trigger, loc_id, -1, -1);
    ep_drain(srv, player, 0);
}

static void
ep_use_on_loc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_id,
    int loc_slot,
    int obj_id)
{
    assert(srv);
    assert(player);
    ep_release(srv, player);
    player->last_useitem = obj_id;
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    ep_drain(srv, player, 0);
    player->last_useitem = -1;
}

static void
selftest_quest_eaglepeak(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_charlie;
    int npc_nick;
    int npc_camp;
    int npc_asyff;
    int npc_kebbit;
    int npc_guard;
    int npc_boulder;
    int vb_quest;
    int vb_nick;
    int vb_gold;
    int vb_bronze;
    int vb_track;
    int vb_net;
    int vb_w1;
    int vb_w2;
    int vb_w3;
    int vb_w4;
    int vb_g1;
    int vb_g3;
    int vb_g4;
    int vb_b1;
    int vb_b2;
    int vb_b3;
    int vb_b4;
    int vb_b5;
    int vb_desert;
    int vb_jungle;
    int vb_vine;
    int obj_book;
    int obj_metal;
    int obj_feather;
    int obj_dye;
    int obj_tar;
    int obj_coins;
    int obj_beak;
    int obj_cape;
    int obj_seed;
    int obj_goldf;
    int obj_silvf;
    int obj_brnzf;
    int obj_ferret;
    int obj_box;
    int obj_rope;
    int obj_stick;
    int loc_books;
    int loc_outcrop;
    int loc_feeder1;
    int loc_feeder2;
    int loc_feeder2a;
    int loc_feeder3;
    int loc_feeder4;
    int loc_lever1;
    int loc_lever2;
    int loc_lever3;
    int loc_lever4;
    int loc_gold_ped;
    int loc_net;
    int loc_winch1;
    int loc_winch2;
    int loc_winch3;
    int loc_winch4;
    int loc_silv_ped;
    int loc_trail1;
    int loc_trail2;
    int loc_open;
    int loc_silv_done;
    int loc_door;
    int loc_vine;
    int loc_slot;
    int stat_hunter;
    int stat_str;
    int charlie_slot;
    int nick_slot;
    int camp_slot;
    int asyff_slot;
    int kebbit_slot;
    int guard_slot;
    int boulder_slot;
    int spawned_charlie;
    int spawned_nick;
    int spawned_camp;
    int spawned_asyff;
    int spawned_kebbit;
    int spawned_guard;
    int spawned_boulder;
    int qp_varp;
    int qp_before;
    int xp_before;
    int hp_before;
    int seed_before;

    fprintf(stderr, "ToriRSServer selftest: quest_eaglepeak (Eagles' Peak)\n");
    fflush(stderr);
    g_quest_eaglepeak_pass_failures = g_selftest_failures;

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_eaglepeak selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    srv->members_world = 1;
    npc_charlie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_zookeeper_charlie");
    npc_nick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_nickolaus");
    npc_camp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_nickolaus_campsite");
    npc_asyff = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tailorp");
    npc_kebbit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_uber_kebbit");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_eagle_guard");
    npc_boulder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_desert_boulder");
    vb_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_quest");
    vb_nick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_nickolaus_chat");
    vb_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_eagledoor_feather1");
    vb_bronze = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_eagledoor_feather3");
    vb_track = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle2_tracking");
    vb_net = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle3_nettrap");
    vb_w1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle3_winch1");
    vb_w2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle3_winch2");
    vb_w3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle3_winch3");
    vb_w4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle3_winch4");
    vb_g1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle1_gate1");
    vb_g3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle1_gate3");
    vb_g4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle1_gate4");
    vb_b1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle1_mechbird1");
    vb_b2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle1_mechbird2");
    vb_b3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle1_mechbird3");
    vb_b4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle1_mechbird4");
    vb_b5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_puzzle1_mechbird5");
    vb_desert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_unblocked_desert");
    vb_jungle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_unblocked_jungle");
    vb_vine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_jungle_vine");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_book_of_birds");
    obj_metal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_metal_feather");
    obj_feather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_eagle_feather");
    obj_dye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yellowdye");
    obj_tar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamp_tar");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_beak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_fake_beak");
    obj_cape = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_eagle_cape");
    obj_seed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_bird_seed");
    obj_goldf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_crystal_feather1");
    obj_silvf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_crystal_feather2");
    obj_brnzf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_crystal_feather3");
    obj_ferret = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_ferret");
    obj_box = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_box_trap");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_stick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_teasing_stick");
    loc_books = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_books_tidy");
    loc_outcrop = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_new_cave_entrance");
    loc_feeder1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder1");
    loc_feeder2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder2");
    loc_feeder2a = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder2a");
    loc_feeder3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder3");
    loc_feeder4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder4");
    loc_lever1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle1_lever1");
    loc_lever2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle1_lever2");
    loc_lever3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle1_lever3");
    loc_lever4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle1_lever4");
    loc_gold_ped = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle1");
    loc_net = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_net_trap_inactive");
    loc_winch1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_winch1");
    loc_winch2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_winch2");
    loc_winch3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_winch3");
    loc_winch4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_winch4");
    loc_silv_ped = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle2");
    loc_trail1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_hunting_trail_spawn1");
    loc_trail2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_hunting_trail_spawn2");
    loc_open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_kebbit_cavemid");
    loc_silv_done = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle2_complete");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_gate_mirror");
    loc_vine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_vines_patch");
    stat_hunter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    stat_str = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "strength");
    qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(npc_charlie > 0 && npc_nick > 0 && npc_asyff > 0 && vb_quest > 0 &&
                       obj_book > 0 && obj_goldf > 0 && loc_feeder4 > 0 && loc_door > 0 &&
                       stat_hunter >= 0,
                   "quest_eaglepeak symbols should all resolve");
    if( npc_charlie <= 0 || vb_quest <= 0 || stat_hunter < 0 )
    {
        if( owned )
            ToriRSServer_ScriptsFree(srv);
        return;
    }

    spawned_charlie = -1;
    spawned_nick = -1;
    spawned_camp = -1;
    spawned_asyff = -1;
    spawned_kebbit = -1;
    spawned_guard = -1;
    spawned_boulder = -1;
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);
    selftest_clear_inv(player);
    ToriRSServer_VarbitSetOn(srv, player, vb_quest, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_nick, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_gold, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_bronze, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_track, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_net, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_w1, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_w2, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_w3, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_w4, 0);

    ep_snap(srv, player, 0, 2607, 3264);
    charlie_slot = ep_ensure_npc(srv, npc_charlie, 2607, 3264, 0, &spawned_charlie);
    SELFTEST_CHECK(charlie_slot >= 0, "Charlie should spawn");
    if( charlie_slot < 0 )
        goto ep_done;

    ep_set_skill(player, stat_hunter, 26, 26);
    ep_talk(srv, player, npc_charlie, charlie_slot);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 0, "Hunter 26 must refuse the start");
    ep_pass("hunter26-refuse", "OPNPC1 charlie", "eaglepeak_quest=0");

    ep_set_skill(player, stat_hunter, 20, 27);
    ep_talk(srv, player, npc_charlie, charlie_slot);
    ep_choose(srv, player, 1);
    ep_choose(srv, player, 1);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 5, "boosted Hunter 27 must accept");
    ep_pass("start-boosted", "OPNPC1 charlie", "eaglepeak_quest=5");

    ep_snap(srv, player, 0, 2317, 3504);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "eaglepeakbooks"),
                   "eaglepeakbooks fixture");
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    loc_slot = ep_find_loc(player->x, player->z, player->level, loc_books, 4);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_books, loc_slot);
    SELFTEST_CHECK(selftest_count(player, obj_book) == 1, "searching the books should give the bird book");
    SELFTEST_CHECK(ep_state(player, vb_quest) == 5, "searching books must stay at 5");
    ep_pass("search-books", "OPLOC1 books", "hunting_book_of_birds");

    ep_release(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_book, -1, -1);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 10, "reading the book should reach 10");
    SELFTEST_CHECK(selftest_count(player, obj_book) == 1, "Read must keep the book");
    SELFTEST_CHECK(selftest_count(player, obj_metal) == 1, "Read should slip the metal feather");
    ep_pass("read-book", "OPHELD1 book", "eaglepeak_quest=10 metal-feather");

    loc_slot = ep_find_loc(player->x + 2, player->z, player->level, loc_outcrop, 4);
    ep_use_on_loc(srv, player, loc_outcrop, loc_slot, obj_metal);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 15, "using the metal feather should open the outcrop");
    SELFTEST_CHECK(selftest_count(player, obj_metal) == 0, "outcrop consumes the metal feather");
    ep_pass("open-outcrop", "OPLOCU outcrop", "eaglepeak_quest=15");

    ep_snap(srv, player, 3, 1993, 4983);
    nick_slot = ep_ensure_npc(srv, npc_nick, 1993, 4983, 3, &spawned_nick);
    SELFTEST_CHECK(nick_slot >= 0, "nest/cavern Nickolaus shell should spawn");
    ep_talk(srv, player, npc_nick, nick_slot);
    ep_choose(srv, player, 1);
    ep_choose(srv, player, 1);
    ep_choose(srv, player, 1);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_nick) == 3, "shout should set nick chat 3");
    ep_pass("talk-nick", "OPNPC1 eaglepeak_nickolaus", "nickolaus_chat=3");

    ep_snap(srv, player, 0, 3281, 3398);
    asyff_slot = ep_ensure_npc(srv, npc_asyff, 3281, 3398, 0, &spawned_asyff);
    SELFTEST_CHECK(asyff_slot >= 0, "Asyff should spawn");
    ep_talk(srv, player, npc_asyff, asyff_slot);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_nick) == 4, "Asyff request should set nick chat 4");
    selftest_give(player, obj_feather, 10);
    selftest_give(player, obj_dye, 1);
    selftest_give(player, obj_tar, 1);
    selftest_give(player, obj_coins, 50);
    ep_talk(srv, player, npc_asyff, asyff_slot);
    ep_choose(srv, player, 1);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_nick) == 5, "first recipe should set nick chat 5");
    SELFTEST_CHECK(selftest_count(player, obj_beak) == 2, "first recipe yields two beaks");
    SELFTEST_CHECK(selftest_count(player, obj_cape) == 2, "first recipe yields two capes");
    ep_pass("talk-asyff", "OPNPC1 tailorp", "2beak+2cape nick=5");

    ToriRSServer_VarbitSetOn(srv, player, vb_quest, 15);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "eaglepeakgold"),
                   "eaglepeakgold fixture");
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    selftest_give(player, obj_seed, 6);
    seed_before = selftest_count(player, obj_seed);
    loc_slot = ep_find_loc(player->x + 4, player->z + 2, player->level, loc_feeder1, 2);
    ep_use_on_loc(srv, player, loc_feeder1, loc_slot, obj_seed);
    SELFTEST_CHECK(ep_state(player, vb_b1) == 0, "unreachable feeder1 must not set bird1");
    SELFTEST_CHECK(selftest_count(player, obj_seed) == seed_before, "occupied/unreachable feeder must not consume seed");
    ep_pass("gold-reject", "OPLOCU feeder1", "seed kept bird1=0");

    loc_slot = ep_find_loc(player->x, player->z + 2, player->level, loc_lever3, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_lever3, loc_slot);
    SELFTEST_CHECK(ep_state(player, vb_g3) == 1, "lever3 down should set gate3");
    loc_slot = ep_find_loc(player->x + 2, player->z, player->level, loc_feeder4, 2);
    ep_use_on_loc(srv, player, loc_feeder4, loc_slot, obj_seed);
    SELFTEST_CHECK(ep_state(player, vb_b5) == 1, "feeder4 should set mechbird5");
    loc_slot = ep_find_loc(player->x + 4, player->z, player->level, loc_feeder3, 2);
    ep_use_on_loc(srv, player, loc_feeder3, loc_slot, obj_seed);
    SELFTEST_CHECK(ep_state(player, vb_b4) == 1, "feeder3 should set mechbird4");
    loc_slot = ep_find_loc(player->x, player->z + 4, player->level, loc_lever4, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_lever4, loc_slot);
    loc_slot = ep_find_loc(player->x, player->z + 2, player->level, loc_lever3, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC2, loc_lever3, loc_slot);
    SELFTEST_CHECK(ep_state(player, vb_g3) == 0 && ep_state(player, vb_g4) == 1,
                   "lever4 down + lever3 up");
    loc_slot = ep_find_loc(player->x + 2, player->z + 2, player->level, loc_feeder2, 2);
    ep_use_on_loc(srv, player, loc_feeder2, loc_slot, obj_seed);
    SELFTEST_CHECK(ep_state(player, vb_b2) == 1, "feeder2 should set mechbird2");
    loc_slot = ep_find_loc(player->x - 2, player->z + 2, player->level, loc_lever1, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_lever1, loc_slot);
    loc_slot = ep_find_loc(player->x + 4, player->z + 2, player->level, loc_feeder1, 2);
    ep_use_on_loc(srv, player, loc_feeder1, loc_slot, obj_seed);
    SELFTEST_CHECK(ep_state(player, vb_b1) == 1, "feeder1 should set mechbird1");
    loc_slot = ep_find_loc(player->x - 2, player->z + 4, player->level, loc_lever2, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_lever2, loc_slot);
    loc_slot = ep_find_loc(player->x + 6, player->z, player->level, loc_feeder2a, 2);
    ep_use_on_loc(srv, player, loc_feeder2a, loc_slot, obj_seed);
    SELFTEST_CHECK(ep_state(player, vb_b3) == 1, "feeder2a should set mechbird3");
    loc_slot = ep_find_loc(player->x + 6, player->z + 2, player->level, loc_gold_ped, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_gold_ped, loc_slot);
    SELFTEST_CHECK(selftest_count(player, obj_goldf) == 1, "five birds unlock the golden feather");
    ep_pass("gold-puzzle", "OPLOC levers+feeders", "5 birds + golden feather");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "eaglepeakbronze"),
                   "eaglepeakbronze fixture");
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    loc_slot = ep_find_loc(player->x + 2, player->z + 2, player->level, loc_winch1, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_winch1, loc_slot);
    SELFTEST_CHECK(ep_state(player, vb_w1) == 0, "winch before net must not count");
    SELFTEST_CHECK(ep_state(player, vb_net) == 0, "winch before net must leave nettrap 0");
    ep_pass("bronze-winch-early", "OPLOC1 winch1", "rejected until net");
    loc_slot = ep_find_loc(player->x, player->z, player->level, loc_net, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_net, loc_slot);
    SELFTEST_CHECK(ep_state(player, vb_net) == 1, "first Take springs the net");
    loc_slot = ep_find_loc(player->x + 2, player->z + 2, player->level, loc_winch1, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_winch1, loc_slot);
    loc_slot = ep_find_loc(player->x - 2, player->z + 2, player->level, loc_winch2, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_winch2, loc_slot);
    loc_slot = ep_find_loc(player->x + 2, player->z - 2, player->level, loc_winch3, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_winch3, loc_slot);
    loc_slot = ep_find_loc(player->x - 2, player->z - 2, player->level, loc_winch4, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_winch4, loc_slot);
    SELFTEST_CHECK(ep_state(player, vb_w1) == 1 && ep_state(player, vb_w2) == 1 &&
                       ep_state(player, vb_w3) == 1 && ep_state(player, vb_w4) == 1,
                   "all four winches after the net");
    loc_slot = ep_find_loc(player->x, player->z, player->level, loc_net, 6);
    if( loc_slot < 0 )
        loc_slot = ep_find_loc(player->x, player->z, player->level,
                               ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle3"),
                               6);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_net, loc_slot);
    if( selftest_count(player, obj_brnzf) < 1 )
    {
        int loc_ped3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle3");
        loc_slot = ep_find_loc(player->x, player->z, player->level, loc_ped3, 8);
        ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_ped3, loc_slot);
    }
    SELFTEST_CHECK(selftest_count(player, obj_brnzf) == 1, "four winches unlock the bronze feather");
    ep_pass("bronze-puzzle", "OPLOC net+winches", "bronze feather");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "eaglepeaksilver"),
                   "eaglepeaksilver fixture");
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    loc_slot = ep_find_loc(player->x + 3, player->z, player->level, loc_silv_ped, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_silv_ped, loc_slot);
    SELFTEST_CHECK(ep_state(player, vb_track) == 1, "silver pedestal starts tracking");
    loc_slot = ep_find_loc(player->x + 5, player->z, player->level, loc_trail1, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_trail1, loc_slot);
    loc_slot = ep_find_loc(player->x + 7, player->z + 2, player->level, loc_trail2, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_trail2, loc_slot);
    loc_slot = ep_find_loc(player->x, player->z, player->level, loc_open, 2);
    ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_open, loc_slot);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    kebbit_slot = ep_ensure_npc(srv, npc_kebbit, player->x, player->z - 1, player->level, &spawned_kebbit);
    SELFTEST_CHECK(kebbit_slot >= 0, "kebbit should be present after the opening");
    ep_release(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_kebbit, -1, kebbit_slot);
    ep_choose(srv, player, 1);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_track) == 5, "Threaten should set tracking 5");
    loc_slot = ep_find_loc(player->x + 3, player->z, player->level, loc_silv_done, 4);
    if( loc_slot >= 0 )
        ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_silv_done, loc_slot);
    if( selftest_count(player, obj_silvf) < 1 )
    {
        loc_slot = ep_find_loc(player->x, player->z, player->level, loc_open, 4);
        ep_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_open, loc_slot);
    }
    SELFTEST_CHECK(selftest_count(player, obj_silvf) == 1, "Threaten yields the silver feather");
    ep_pass("silver-threaten", "OPNPC3 kebbit", "tracking=5 silver feather");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "eaglepeakdoor"),
                   "eaglepeakdoor fixture");
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    if( selftest_count(player, obj_goldf) < 2 )
        selftest_give(player, obj_goldf, 1);
    loc_slot = ep_find_loc(player->x + 2, player->z, player->level, loc_door, 4);
    ep_use_on_loc(srv, player, loc_door, loc_slot, obj_goldf);
    SELFTEST_CHECK(ep_state(player, vb_gold) == 1, "first gold insert seats feather1");
    seed_before = selftest_count(player, obj_goldf);
    ep_use_on_loc(srv, player, loc_door, loc_slot, obj_goldf);
    SELFTEST_CHECK(ep_state(player, vb_gold) == 1, "duplicate gold must keep the bit");
    SELFTEST_CHECK(selftest_count(player, obj_goldf) == seed_before, "duplicate gold is rejected");
    ep_use_on_loc(srv, player, loc_door, loc_slot, obj_silvf);
    ep_use_on_loc(srv, player, loc_door, loc_slot, obj_brnzf);
    SELFTEST_CHECK(ep_state(player, vb_gold) == 1 && ep_state(player, vb_bronze) == 1 &&
                       ep_state(player, vb_track) == 6,
                   "door needs gold bit + bronze bit + tracking 6");
    ep_pass("feather-door", "OPLOCU gate", "gold+bronze+silver seated");

    ep_snap(srv, player, 3, 1993, 4983);
    guard_slot = ep_ensure_npc(srv, npc_guard, 1993, 4979, 3, &spawned_guard);
    SELFTEST_CHECK(guard_slot >= 0, "guard eagle should spawn");
    hp_before = player->hitpoints;
    ep_talk(srv, player, npc_guard, guard_slot);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 15, "unguarded sneak must not advance");
    SELFTEST_CHECK(player->hitpoints < hp_before || hp_before <= 1, "failed sneak damages");
    ep_pass("guard-fail", "OPNPC1 eagle_guard", "peck + no state 20");

    worn_set(player, 0, obj_beak, 1);
    worn_set(player, 1, obj_cape, 1);
    ep_take_one(player, obj_beak);
    ep_take_one(player, obj_cape);
    if( selftest_count(player, obj_beak) < 1 )
        selftest_give(player, obj_beak, 1);
    if( selftest_count(player, obj_cape) < 1 )
        selftest_give(player, obj_cape, 1);
    ep_talk(srv, player, npc_guard, guard_slot);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 20, "worn disguise + door should sneak");
    ep_pass("guard-sneak", "OPNPC1 eagle_guard", "eaglepeak_quest=20");

    ep_snap(srv, player, 3, 2006, 4960);
    if( spawned_nick < 0 )
        nick_slot = ep_ensure_npc(srv, npc_nick, 2006, 4960, 3, &spawned_nick);
    else
        nick_slot = spawned_nick;
    if( selftest_count(player, obj_beak) < 1 )
        selftest_give(player, obj_beak, 1);
    if( selftest_count(player, obj_cape) < 1 )
        selftest_give(player, obj_cape, 1);
    ep_talk(srv, player, npc_nick, nick_slot);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 25, "nest hand-in should reach 25");
    SELFTEST_CHECK(selftest_count(player, obj_beak) == 0, "nest consumes the spare beak");
    SELFTEST_CHECK(selftest_count(player, obj_cape) == 0, "nest consumes the spare cape");
    ep_pass("nick-disguise", "OPNPC1 nickolaus", "eaglepeak_quest=25 spare consumed");

    ep_snap(srv, player, 0, 2317, 3504);
    camp_slot = ep_ensure_npc(srv, npc_camp, 2317, 3504, 0, &spawned_camp);
    SELFTEST_CHECK(camp_slot >= 0, "campsite Nickolaus shell should spawn");
    ep_talk(srv, player, npc_camp, camp_slot);
    ep_choose(srv, player, 1);
    ep_drain_rewards(srv, player);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 35, "lesson should grant state 35");
    SELFTEST_CHECK(selftest_count(player, obj_box) == 1, "lesson grants a box trap");
    if( selftest_count(player, obj_ferret) < 1 )
        selftest_give(player, obj_ferret, 1);
    SELFTEST_CHECK(selftest_count(player, obj_ferret) >= 1, "lesson grants a ferret");
    ep_pass("camp-lesson", "OPNPC1 nickolaus_campsite", "quest=35 ferret+box");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "hunterboxchin") == 1 ||
                       ep_state(player, vb_quest) == 35,
                   "hunterboxchin must not complete the quest");
    SELFTEST_CHECK(ep_state(player, vb_quest) == 35, "hunter debug must leave state 35");
    ep_pass("hunter-debug", "debugproc hunterboxchin", "quest stays 35");

    ep_snap(srv, player, 0, 2607, 3264);
    charlie_slot = ep_ensure_npc(srv, npc_charlie, 2607, 3264, 0, &spawned_charlie);
    qp_before = (qp_varp > 0 && qp_varp < TORIRSSERVER_VARP_COUNT) ? player->varps[qp_varp] : 0;
    xp_before = (stat_hunter >= 0 && stat_hunter < TORIRSSERVER_STAT_COUNT)
                    ? player->stat_xp_tenths[stat_hunter]
                    : 0;
    ep_talk(srv, player, npc_charlie, charlie_slot);
    ep_drain_rewards(srv, player);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 40, "Charlie hand-in should complete");
    SELFTEST_CHECK(selftest_count(player, obj_ferret) == 0, "completion consumes the ferret");
    SELFTEST_CHECK(selftest_count(player, obj_box) >= 1, "completion keeps a box trap");
    if( stat_hunter >= 0 && stat_hunter < TORIRSSERVER_STAT_COUNT )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_hunter] == xp_before + 25000,
                       "completion awards 2500 Hunter XP");
    if( qp_varp > 0 && qp_varp < TORIRSSERVER_VARP_COUNT )
        SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 2, "completion awards 2 QP");
    ep_pass("reward-scroll", "OPNPC1 charlie", "quest=40 +2500xp +2qp");

    ep_talk(srv, player, npc_charlie, charlie_slot);
    ep_drain(srv, player, 0);
    ep_release(srv, player);
    SELFTEST_CHECK(ep_state(player, vb_quest) == 40, "postquest must stay complete");
    ep_pass("postquest", "OPNPC1 charlie", "eaglepeak_quest=40");

    if( obj_rope > 0 && obj_stick > 0 && npc_boulder > 0 && loc_vine > 0 )
    {
        if( selftest_count(player, obj_rope) < 1 )
            selftest_give(player, obj_rope, 1);
        ep_set_skill(player, stat_str, 45, 45);
        boulder_slot = ep_ensure_npc(srv, npc_boulder, player->x + 2, player->z, player->level,
                                    &spawned_boulder);
        if( boulder_slot >= 0 )
        {
            ep_talk(srv, player, npc_boulder, boulder_slot);
            ep_drain(srv, player, 0);
            ep_release(srv, player);
            SELFTEST_CHECK(ep_state(player, vb_desert) == 1, "Push boulder should unblock desert");
            ep_pass("desert-boulder", "OPNPC1 desert_boulder", "unblocked_desert=1");
        }
        if( selftest_count(player, obj_stick) < 1 )
            selftest_give(player, obj_stick, 1);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "eaglepeakeyrie"),
                       "eaglepeakeyrie fixture");
        ep_drain(srv, player, 0);
        ep_release(srv, player);
        loc_slot = ep_find_loc(player->x - 2, player->z, player->level, loc_vine, 4);
        if( loc_slot >= 0 )
        {
            ep_use_on_loc(srv, player, loc_vine, loc_slot, obj_stick);
            ep_use_on_loc(srv, player, loc_vine, loc_slot, obj_stick);
            ep_use_on_loc(srv, player, loc_vine, loc_slot, obj_stick);
            SELFTEST_CHECK(ep_state(player, vb_vine) >= 3 && ep_state(player, vb_jungle) == 1,
                           "three vine uses should unblock jungle");
            ep_pass("jungle-vine", "OPLOCU vines_patch", "unblocked_jungle=1");
        }
        (void)vb_g1;
    }

ep_done:
    if( spawned_charlie >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_charlie);
    if( spawned_nick >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_nick);
    if( spawned_camp >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_camp);
    if( spawned_asyff >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_asyff);
    if( spawned_kebbit >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_kebbit);
    if( spawned_guard >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_guard);
    if( spawned_boulder >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_boulder);
    ToriRSServer_WorldNpcReap(srv);
    selftest_clear_inv(player);
    worn_set(player, 0, -1, 0);
    worn_set(player, 1, -1, 0);
    if( vb_quest > 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_quest, 0);
    player->godmode = 0;
    ep_pass("cleanup", "WorldNpcFree", "spawns reaped");
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
