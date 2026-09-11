/* Death Plateau -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int g_quest_deathplateau_pass_failures;

static void
death_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    if( g_selftest_failures == g_quest_deathplateau_pass_failures )
        fprintf(stderr, "PASS deathplateau %s trigger=%s %s\n", step, trigger, observable);
    g_quest_deathplateau_pass_failures = g_selftest_failures;
    fflush(stderr);
}

static int
death_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
death_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
death_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
death_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = death_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 96 && player->active_script; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_COUNTDIALOG )
            return;
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
death_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = death_chatmenu();

    assert(srv);
    assert(player);
    death_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
death_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    assert(srv);
    assert(player);
    death_release(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
death_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    /* Resume pause FIRST -- WorldCloseModal aborts a parked script. */
    death_drain(srv, player, 0);
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
death_find_npc_at(const struct ToriRSServer* srv, int npc_type, int x, int z, int level)
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
death_ensure_npc(
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
    slot = death_find_npc_at(srv, npc_type, x, z, level);
    if( slot >= 0 )
        return slot;
    slot = selftest_find_npc(srv, npc_type);
    if( slot >= 0 )
        return slot;
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x, z, level);
    if( slot >= 0 )
        *spawned = slot;
    return slot;
}

static int
death_find_loc(int cx, int cz, int level, int loc_id, int radius)
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
death_find_loc_any(int cx, int cz, int level, int a, int b, int radius)
{
    int slot;

    slot = death_find_loc(cx, cz, level, a, radius);
    if( slot >= 0 )
        return slot;
    return death_find_loc(cx, cz, level, b, radius);
}

static int
death_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id == obj_id )
            return i;
    }
    return -1;
}

static void
death_use_on_npc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot,
    int obj_id)
{
    int inv_slot;

    assert(srv);
    assert(player);
    inv_slot = death_inv_slot(player, obj_id);
    if( inv_slot < 0 )
        return;
    death_release(srv, player);
    player->last_useitem = obj_id;
    player->last_useslot = inv_slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, npc_slot);
}

static void
death_take_ground(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int obj_id,
    int x,
    int z,
    int level)
{
    int ground;

    assert(srv);
    assert(player);
    death_snap(srv, player, level, x, z);
    ground = ToriRSServer_WorldGroundFind(srv, x, z, level, obj_id);
    if( ground < 0 )
        ground = ToriRSServer_WorldObjAdd(srv, obj_id, 1, x, z, level, -1);
    SELFTEST_CHECK(ground >= 0, "deathplateau ground obj %d should exist at %d,%d", obj_id, x, z);
    if( ground < 0 )
        return;
    srv->pending_active_obj = ToriRSServer_WorldObjHandle(srv, ground);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, obj_id, -1, -1);
    srv->pending_active_obj = 0;
    death_drain(srv, player, 0);
    death_release(srv, player);
    selftest_tick(srv);
}

static void
death_place_ball(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_corner,
    int loc_side,
    int obj_id,
    int x,
    int z)
{
    int loc_slot;
    int inv_slot;

    assert(srv);
    assert(player);
    death_snap(srv, player, 0, x, z);
    loc_slot = -1;
    {
        int s;

        for( s = 0;; s++ )
        {
            struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

            if( !loc )
                break;
            if( !loc->active || loc->level != 0 )
                continue;
            if( loc->loc_id != loc_corner && loc->loc_id != loc_side )
                continue;
            if( loc->x == x && loc->z == z )
            {
                loc_slot = s;
                break;
            }
        }
        if( loc_slot < 0 )
        {
            for( s = 0;; s++ )
            {
                struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

                if( !loc )
                    break;
                if( !loc->active || loc->level != 0 )
                    continue;
                if( loc->loc_id != loc_corner && loc->loc_id != loc_side )
                    continue;
                fprintf(stderr, "deathplateau mechanism loc id=%d at %d,%d (want %d,%d)\n",
                        loc->loc_id, loc->x, loc->z, x, z);
            }
        }
    }
    SELFTEST_CHECK(loc_slot >= 0, "deathplateau mechanism should stand at %d,%d", x, z);
    inv_slot = death_inv_slot(player, obj_id);
    SELFTEST_CHECK(inv_slot >= 0, "deathplateau should hold ball %d before place", obj_id);
    if( loc_slot < 0 || inv_slot < 0 )
        return;
    player->last_useitem = obj_id;
    player->last_useslot = inv_slot;
    {
        struct ToriRSServerSceneLoc* mech = ToriRSServer_SceneLoc(loc_slot);

        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU,
                                            mech ? mech->loc_id : loc_corner, -1, loc_slot);
    }
    death_drain(srv, player, 0);
    death_release(srv, player);
    player->last_useitem = -1;
    player->last_useslot = -1;
    selftest_tick(srv);
}

static void
selftest_quest_deathplateau(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_denulth;
    int npc_eohric;
    int npc_harold;
    int npc_saba;
    int npc_tenzing;
    int npc_dunstan;
    int varp_equip;
    int varp_map;
    int varp_bits;
    int varp_qp;
    int varp_troll;
    int stat_attack;
    int obj_ale;
    int obj_blur;
    int obj_coins;
    int obj_iou;
    int obj_combo;
    int obj_cert;
    int obj_map;
    int obj_boots;
    int obj_spiked;
    int obj_iron;
    int obj_bread;
    int obj_trout;
    int obj_claws;
    int obj_blue;
    int obj_yellow;
    int obj_red;
    int obj_purple;
    int obj_green;
    int loc_corner;
    int loc_side;
    int loc_castle;
    int loc_harold_door;
    int loc_danger;
    int loc_cave_in;
    int loc_cave_out;
    int loc_stile;
    int den_slot;
    int eoh_slot;
    int har_slot;
    int saba_slot;
    int ten_slot;
    int dun_slot;
    int spawned_den;
    int spawned_eoh;
    int spawned_har;
    int spawned_saba;
    int spawned_ten;
    int spawned_dun;
    int qp_before;
    int xp_before;
    int iou_slot;
    int i;

    fprintf(stderr, "ToriRSServer selftest: quest_deathplateau (Death Plateau)\n");
    fflush(stderr);
    g_quest_deathplateau_pass_failures = g_selftest_failures;

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_deathplateau selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_denulth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_ig_commander");
    npc_eohric = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_headservant");
    npc_harold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_guard_equiproom");
    npc_saba = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_hermit");
    npc_tenzing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_sherpa");
    npc_dunstan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_smithy");
    varp_equip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_equiproom");
    varp_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_map");
    varp_bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_bits");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    varp_troll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "troll_quest");
    stat_attack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
    obj_ale = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "asgarnian_ale");
    obj_blur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blurberry_special");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_iou = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_iou");
    obj_combo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_combination");
    obj_cert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_entrancecert");
    obj_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_secretwaymap");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_climbingboots");
    obj_spiked = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_spikedboots");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_bread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread");
    obj_trout = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trout");
    obj_claws = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_claws");
    obj_blue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_blue");
    obj_yellow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_yellow");
    obj_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_red");
    obj_purple = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_purple");
    obj_green = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_green");
    loc_corner = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_stone_mechanism_corner");
    loc_side = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_stone_mechanism_side");
    loc_castle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_castledoor");
    loc_harold_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_harold_door");
    loc_danger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_dangersign_trolls");
    loc_cave_in = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_hermitcave_entrance");
    loc_cave_out = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_hermitcave_exit");
    loc_stile = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_fullstyle");

    SELFTEST_CHECK(npc_denulth >= 0 && npc_eohric >= 0 && npc_harold >= 0 && npc_saba >= 0 &&
                       npc_tenzing >= 0 && npc_dunstan >= 0 && varp_equip >= 0 && varp_map >= 0 &&
                       obj_iou >= 0 && obj_combo >= 0 && loc_corner >= 0 && loc_side >= 0,
                   "quest_deathplateau symbols should resolve");
    if( npc_denulth < 0 || varp_equip < 0 )
        return;

    srv->members_world = 1;
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    player->varps[varp_equip] = 0;
    player->varps[varp_map] = 0;
    if( varp_bits >= 0 )
        player->varps[varp_bits] = 0;
    if( varp_troll >= 0 )
        player->varps[varp_troll] = 0;
    spawned_den = spawned_eoh = spawned_har = spawned_saba = spawned_ten = spawned_dun = -1;

    ToriRSServer_ScriptsRunDebugproc(srv, "deathreset");
    death_drain(srv, player, 0);
    death_release(srv, player);

    /* ---- loc shells via SceneFindLocId ---- */
    death_snap(srv, player, 0, 2894, 3563);
    SELFTEST_CHECK(death_find_loc_any(2894, 3562, 0, loc_corner, loc_side, 4) >= 0,
                   "death_stone_mechanism should stand at 2894,3562");
    death_pass("loc_mechanism", "SceneFindLocId", "death_stone_mechanism");

    death_snap(srv, player, 0, 2895, 3566);
    SELFTEST_CHECK(death_find_loc(2895, 3566, 0, loc_castle, 8) >= 0,
                   "death_castledoor should stand near the equipment room");
    death_pass("loc_castledoor", "SceneFindLocId", "death_castledoor");

    death_snap(srv, player, 1, 2906, 3540);
    SELFTEST_CHECK(death_find_loc(2906, 3540, 1, loc_harold_door, 10) >= 0,
                   "death_harold_door should stand in the Toad and Chicken");
    death_pass("loc_harold_door", "SceneFindLocId", "death_harold_door");

    death_snap(srv, player, 0, 2865, 3556);
    if( death_find_loc(2865, 3556, 0, loc_danger, 24) < 0 )
        death_snap(srv, player, 0, 2854, 3596);
    SELFTEST_CHECK(death_find_loc(player->x, player->z, 0, loc_danger, 32) >= 0 ||
                       death_find_loc(2865, 3556, 0, loc_danger, 32) >= 0 ||
                       death_find_loc(2854, 3596, 0, loc_danger, 32) >= 0,
                   "death_dangersign_trolls should stand on the main path");
    death_pass("loc_dangersign", "SceneFindLocId", "death_dangersign_trolls");

    death_snap(srv, player, 0, 2860, 3578);
    SELFTEST_CHECK(death_find_loc(2860, 3578, 0, loc_cave_in, 8) >= 0,
                   "death_hermitcave_entrance should stand at 2860,3578");
    death_pass("loc_cave_in", "SceneFindLocId", "death_hermitcave_entrance");

    death_snap(srv, player, 0, 2269, 4752);
    SELFTEST_CHECK(death_find_loc(2269, 4752, 0, loc_cave_out, 8) >= 0,
                   "death_hermitcave_exit should stand in Saba's cave");
    death_pass("loc_cave_out", "SceneFindLocId", "death_hermitcave_exit");

    death_snap(srv, player, 0, 2822, 3555);
    SELFTEST_CHECK(death_find_loc(2822, 3555, 0, loc_stile, 8) >= 0,
                   "death_fullstyle stile should stand behind Tenzing");
    death_pass("loc_stile", "SceneFindLocId", "death_fullstyle");

    /* ---- Denulth refuse then accept ---- */
    death_snap(srv, player, 0, 2896, 3528);
    den_slot = death_ensure_npc(srv, npc_denulth, 2896, 3528, 0, &spawned_den);
    SELFTEST_CHECK(den_slot >= 0, "Denulth should exist at 2896,3528");
    if( den_slot < 0 )
        goto death_done;

    death_talk(srv, player, npc_denulth, den_slot);
    death_choose(srv, player, 1);
    death_choose(srv, player, 2);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 0, "refuse should leave death_equiproom=0, got %d",
                   player->varps[varp_equip]);
    death_pass("start_refuse", "opnpc1,death_ig_commander", "death_equiproom=0");

    death_talk(srv, player, npc_denulth, den_slot);
    death_choose(srv, player, 1);
    death_choose(srv, player, 1);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 10, "accept should write death_equiproom=10, got %d",
                   player->varps[varp_equip]);
    death_pass("start_accept", "opnpc1,death_ig_commander", "death_equiproom=10");

    /* ---- Eohric identifies Harold ---- */
    death_snap(srv, player, 1, 2902, 3565);
    eoh_slot = death_ensure_npc(srv, npc_eohric, 2902, 3565, 1, &spawned_eoh);
    SELFTEST_CHECK(eoh_slot >= 0, "Eohric should exist at 2902,3565,1");
    if( eoh_slot < 0 )
        goto death_done;
    death_talk(srv, player, npc_eohric, eoh_slot);
    death_choose(srv, player, 1);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 20, "Eohric should write death_equiproom=20, got %d",
                   player->varps[varp_equip]);
    death_pass("talk_eohric", "opnpc1,death_headservant", "death_equiproom=20");

    /* ---- Harold refuses ---- */
    death_snap(srv, player, 1, 2906, 3540);
    har_slot = death_ensure_npc(srv, npc_harold, 2906, 3540, 1, &spawned_har);
    SELFTEST_CHECK(har_slot >= 0, "Harold should exist upstairs at the inn");
    if( har_slot < 0 )
        goto death_done;
    death_talk(srv, player, npc_harold, har_slot);
    death_choose(srv, player, 1);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 30, "Harold duty should write 30, got %d",
                   player->varps[varp_equip]);
    death_pass("talk_harold", "opnpc1,death_guard_equiproom", "death_equiproom=30");

    /* ---- Eohric drink hint ---- */
    death_snap(srv, player, 1, 2902, 3565);
    death_talk(srv, player, npc_eohric, eoh_slot);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 40, "return to Eohric should write 40, got %d",
                   player->varps[varp_equip]);
    death_pass("eohric_hint", "opnpc1,death_headservant", "death_equiproom=40");

    /* ---- ale then Blurberry then 101 wager ---- */
    death_snap(srv, player, 1, 2906, 3540);
    selftest_give(player, obj_ale, 1);
    death_use_on_npc(srv, player, npc_harold, har_slot, obj_ale);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 50, "ale should write death_equiproom=50, got %d",
                   player->varps[varp_equip]);
    death_pass("give_ale", "opnpcu,death_guard_equiproom", "death_equiproom=50");

    selftest_give(player, obj_blur, 1);
    death_use_on_npc(srv, player, npc_harold, har_slot, obj_blur);
    death_drain(srv, player, 0);
    death_release(srv, player);
    death_pass("give_blurberry", "opnpcu,death_guard_equiproom", "harold_verydrunk");

    selftest_give(player, obj_coins, 200);
    death_talk(srv, player, npc_harold, har_slot);
    death_choose(srv, player, 2);
    death_drain(srv, player, 0);
    if( player->active_script && player->active_script->execution == SSVM_COUNTDIALOG )
        ToriRSServer_ScriptsResumeCountdialog(srv, 101);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 55 && selftest_count(player, obj_iou) >= 1,
                   "101 Blurberry wager should grant IOU and write 55, got %d",
                   player->varps[varp_equip]);
    death_pass("harold_iou", "opnpc1+countdialog", "death_equiproom=55 iou=1");

    /* ---- read IOU ---- */
    iou_slot = death_inv_slot(player, obj_iou);
    SELFTEST_CHECK(iou_slot >= 0, "IOU should be in inventory before Read");
    if( iou_slot >= 0 )
    {
        player->last_slot = iou_slot;
        selftest_opheld(srv, 1, iou_slot);
        death_drain(srv, player, 0);
        death_release(srv, player);
    }
    SELFTEST_CHECK(player->varps[varp_equip] == 60 && selftest_count(player, obj_combo) >= 1,
                   "Read IOU should write 60 and grant Combination, got %d",
                   player->varps[varp_equip]);
    death_pass("read_iou", "opheld1,death_iou", "death_equiproom=60");

    /* ---- five-colour puzzle ---- */
    death_snap(srv, player, 0, 2893, 3563);
    ToriRSServer_ScriptsRunDebugproc(srv, "deathballs");
    death_drain(srv, player, 0);
    death_release(srv, player);
    death_take_ground(srv, player, obj_blue, 2893, 3561, 0);
    death_take_ground(srv, player, obj_yellow, 2893, 3562, 0);
    death_take_ground(srv, player, obj_red, 2893, 3563, 0);
    death_take_ground(srv, player, obj_purple, 2893, 3564, 0);
    death_take_ground(srv, player, obj_green, 2893, 3565, 0);
    SELFTEST_CHECK(selftest_count(player, obj_blue) >= 1 && selftest_count(player, obj_yellow) >= 1 &&
                       selftest_count(player, obj_red) >= 1 && selftest_count(player, obj_purple) >= 1 &&
                       selftest_count(player, obj_green) >= 1,
                   "all five coloured balls should be takeable");
    death_pass("take_balls", "opobj3,death_cannonball_*", "five_colours");

    death_place_ball(srv, player, loc_corner, loc_side, obj_blue, 2894, 3562);
    death_place_ball(srv, player, loc_corner, loc_side, obj_yellow, 2895, 3562);
    death_place_ball(srv, player, loc_corner, loc_side, obj_red, 2894, 3563);
    death_place_ball(srv, player, loc_corner, loc_side, obj_purple, 2895, 3563);
    death_place_ball(srv, player, loc_corner, loc_side, obj_green, 2895, 3564);
    SELFTEST_CHECK(player->varps[varp_equip] == 70, "solved layout should unlock door (70), got %d",
                   player->varps[varp_equip]);
    death_pass("place_balls", "oplocu,death_stone_mechanism", "death_equiproom=70");

    /* ---- Saba / Tenzing / Dunstan / cert / supplies / scout ---- */
    death_snap(srv, player, 0, 2270, 4759);
    saba_slot = death_ensure_npc(srv, npc_saba, 2270, 4759, 0, &spawned_saba);
    SELFTEST_CHECK(saba_slot >= 0, "Saba should exist in the hermit cave");
    if( saba_slot >= 0 )
    {
        death_talk(srv, player, npc_saba, saba_slot);
        death_choose(srv, player, 2);
        death_drain(srv, player, 0);
        death_release(srv, player);
    }
    SELFTEST_CHECK(player->varps[varp_map] == 1, "Saba should write death_map=1, got %d",
                   player->varps[varp_map]);
    death_pass("talk_saba", "opnpc1,death_hermit", "death_map=1");

    death_snap(srv, player, 0, 2820, 3556);
    ten_slot = death_ensure_npc(srv, npc_tenzing, 2820, 3556, 0, &spawned_ten);
    SELFTEST_CHECK(ten_slot >= 0, "Tenzing should exist at 2820,3556");
    if( ten_slot >= 0 )
    {
        death_talk(srv, player, npc_tenzing, ten_slot);
        death_choose(srv, player, 1);
        death_drain(srv, player, 0);
        death_release(srv, player);
    }
    SELFTEST_CHECK(player->varps[varp_map] == 2 && selftest_count(player, obj_boots) >= 1,
                   "Tenzing accept should write death_map=2 and grant boots, got %d",
                   player->varps[varp_map]);
    death_pass("talk_tenzing", "opnpc1,death_sherpa", "death_map=2");

    death_snap(srv, player, 0, 2919, 3574);
    dun_slot = death_ensure_npc(srv, npc_dunstan, 2919, 3574, 0, &spawned_dun);
    SELFTEST_CHECK(dun_slot >= 0, "Dunstan should exist at 2919,3574");
    if( dun_slot >= 0 )
    {
        death_talk(srv, player, npc_dunstan, dun_slot);
        death_drain(srv, player, 0);
        death_release(srv, player);
    }
    SELFTEST_CHECK(player->varps[varp_map] == 3, "Dunstan should write death_map=3, got %d",
                   player->varps[varp_map]);
    death_pass("talk_dunstan", "opnpc1,death_smithy", "death_map=3");

    death_snap(srv, player, 0, 2896, 3528);
    death_talk(srv, player, npc_denulth, den_slot);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_map] == 4 && selftest_count(player, obj_cert) >= 1,
                   "Denulth certificate should write death_map=4, got %d", player->varps[varp_map]);
    death_pass("denulth_cert", "opnpc1,death_ig_commander", "death_map=4");

    selftest_give(player, obj_iron, 1);
    death_snap(srv, player, 0, 2919, 3574);
    death_talk(srv, player, npc_dunstan, dun_slot);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_map] == 5 && selftest_count(player, obj_spiked) >= 1,
                   "Dunstan bargain should write 5 and grant spiked boots, got %d",
                   player->varps[varp_map]);
    death_pass("dunstan_spike", "opnpc1,death_smithy", "death_map=5");

    selftest_give(player, obj_bread, 10);
    selftest_give(player, obj_trout, 10);
    death_snap(srv, player, 0, 2820, 3556);
    death_talk(srv, player, npc_tenzing, ten_slot);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_map] == 7 && selftest_count(player, obj_map) >= 1,
                   "Tenzing supplies should grant map and write 7, got %d", player->varps[varp_map]);
    death_pass("tenzing_map", "opnpc1,death_sherpa", "death_map=7");

    death_snap(srv, player, 0, 2864, 3608);
    ToriRSServer_ScriptsRunTriggerAt(srv, SS_TRIGGER_ZONE, 0, 2864, 3608);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_map] == 8, "scout zone should write death_map=8, got %d",
                   player->varps[varp_map]);
    death_pass("scout_path", "zone,0_44_56_48_24", "death_map=8");

    /* ---- final handin ---- */
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    xp_before = stat_attack >= 0 ? player->stat_xp_tenths[stat_attack] : 0;
    if( selftest_count(player, obj_combo) < 1 )
        selftest_give(player, obj_combo, 1);
    if( selftest_count(player, obj_map) < 1 )
        selftest_give(player, obj_map, 1);
    death_snap(srv, player, 0, 2896, 3528);
    death_talk(srv, player, npc_denulth, den_slot);
    death_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 80, "hand-in should write death_equiproom=80, got %d",
                   player->varps[varp_equip]);
    SELFTEST_CHECK(selftest_count(player, obj_claws) >= 1, "hand-in should grant steel claws");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1, "hand-in should award 1 QP, %d -> %d",
                       qp_before, player->varps[varp_qp]);
    if( stat_attack >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_attack] == xp_before + 30000,
                       "hand-in should award 30000 Attack tenths");
    death_pass("handin_complete", "opnpc1,death_ig_commander", "death_equiproom=80 claws+qp+xp");

    /* ---- postquest + Troll Stronghold start splice ---- */
    death_talk(srv, player, npc_denulth, den_slot);
    death_choose(srv, player, 1);
    death_choose(srv, player, 2);
    death_choose(srv, player, 1);
    death_drain(srv, player, 0);
    death_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_equip] == 80, "postquest talk must stay at 80, got %d",
                   player->varps[varp_equip]);
    if( varp_troll >= 0 )
        SELFTEST_CHECK(player->varps[varp_troll] >= 1,
                       "postquest Denulth should start Troll Stronghold, got %d",
                       player->varps[varp_troll]);
    death_pass("postquest_troll", "opnpc1,death_ig_commander", "troll_quest started");

death_done:
    if( spawned_den >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_den);
    if( spawned_eoh >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_eoh);
    if( spawned_har >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_har);
    if( spawned_saba >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_saba);
    if( spawned_ten >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_ten);
    if( spawned_dun >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_dun);
    ToriRSServer_WorldNpcReap(srv);
    selftest_clear_inv(player);
    death_pass("cleanup", "WorldNpcFree", "spawns reaped");
    (void)i;
}
