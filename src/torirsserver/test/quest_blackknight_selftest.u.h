/* Black Knights' Fortress -- real OPNPC1 / OPLOC / OPHELD, not ::blackknightrun. */

static int g_quest_blackknight_pass_failures;

static void
bkf_pass_begin(void)
{
    g_quest_blackknight_pass_failures = g_selftest_failures;
}

static void
bkf_pass(const char* step)
{
    assert(step);
    if( g_selftest_failures == g_quest_blackknight_pass_failures )
        fprintf(stderr, "BKF PASS: %s\n", step);
    g_quest_blackknight_pass_failures = g_selftest_failures;
}

static int
bkf_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
bkf_release_park(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
bkf_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = bkf_chatmenu();
    int chat_left = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chat_left:continue");
    int chat_right = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chat_right:continue");
    int messagebox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
    int round;

    for( round = 0; round < 96 && player->active_script; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_SUSPENDED || exec == SSVM_NPC_SUSPENDED ||
            exec == SSVM_WORLD_SUSPENDED )
        {
            selftest_tick(srv);
            continue;
        }
        if( exec != SSVM_PAUSEBUTTON )
            break;
        if( stop_on_choice && chatmenu > 0 && player->resume_button_count > 0 &&
            player->resume_buttons[0] == chatmenu )
            return;
        if( chatmenu > 0 && !stop_on_choice && player->resume_button_count > 0 &&
            player->resume_buttons[0] == chatmenu )
        {
            player->last_slot = 1;
            if( ToriRSServer_ScriptsResumeButton(srv, chatmenu) )
                continue;
        }
        if( chat_left > 0 && ToriRSServer_ScriptsResumeButton(srv, chat_left) )
            continue;
        if( chat_right > 0 && ToriRSServer_ScriptsResumeButton(srv, chat_right) )
            continue;
        if( messagebox > 0 && ToriRSServer_ScriptsResumeButton(srv, messagebox) )
            continue;
        if( player->resume_button_count > 0 &&
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]) )
            continue;
        break;
    }
}

static void
bkf_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = bkf_chatmenu();

    bkf_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
bkf_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    bkf_release_park(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
bkf_drain_complete(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int varp, int want)
{
    int i;

    bkf_drain(srv, player, 0);
    for( i = 0; i < 64 && player->varps[varp] != want; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static int
bkf_find_loc(int cx, int cz, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int slot;

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
    return -1;
}

static void
bkf_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
bkf_clear_worn(struct ToriRSServerPlayer* player)
{
    int i;

    for( i = 0; i < TORIRSSERVER_WORN_SLOTS; i++ )
    {
        player->worn[i].obj_id = -1;
        player->worn[i].count = 0;
    }
}

static void
bkf_wear(struct ToriRSServerPlayer* player, int slot, int obj_id)
{
    player->worn[slot].obj_id = obj_id;
    player->worn[slot].count = 1;
}

static void
selftest_quest_blackknight(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int amik_type;
    int guard_type;
    int varp;
    int cauldron_varp;
    int qp_varp;
    int obj_dossier;
    int obj_cabbage;
    int obj_magic_cabbage;
    int obj_coins;
    int obj_helm;
    int obj_chain;
    int obj_bhelm;
    int obj_bbody;
    int obj_blegs;
    int loc_door1;
    int loc_door2;
    int loc_grill;
    int loc_hole;
    int loc_cauldron;
    int npc_witch;
    int npc_captain;
    int npc_greldo;
    int npc_cat;
    int slot;
    int guard_slot;
    int scene_slots[4];
    int loc_slot;
    int qp_saved;
    int qp_before;
    int coins_before;
    int amik_x;
    int amik_z;
    int amik_level;
    int i;

    fprintf(stderr, "ToriRSServer selftest: Black Knights' Fortress OPNPC1\n");
    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    amik_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sir_amik_varze");
    guard_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fortressguard_01");
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "spy");
    cauldron_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "spy_cauldron");
    qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    obj_dossier = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bk_dossier");
    obj_cabbage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cabbage");
    obj_magic_cabbage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "magic_cabbage");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_helm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_med_helm");
    obj_chain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_chainbody");
    obj_bhelm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "black_full_helm");
    obj_bbody = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "black_platebody");
    obj_blegs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "black_platelegs");
    loc_door1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bkfortressdoor1");
    loc_door2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bkfortressdoor2");
    loc_grill = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "witchgrill");
    loc_hole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "blackknighthole");
    loc_cauldron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bkf_cauldron_multi");
    npc_witch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fortwitch");
    npc_captain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grillknight");
    npc_greldo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "greldo");
    npc_cat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bkf_cat");
    SELFTEST_CHECK(amik_type >= 0 && varp >= 0 && qp_varp >= 0 && obj_dossier >= 0 &&
                       obj_cabbage >= 0 && obj_helm >= 0 && loc_door1 >= 0 && loc_grill >= 0,
                   "Black Knights' Fortress symbols should resolve");
    if( amik_type < 0 || varp < 0 || obj_dossier < 0 )
        return;

    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    bkf_clear_worn(player);
    player->varps[varp] = 0;
    if( cauldron_varp >= 0 )
        player->varps[cauldron_varp] = 0;
    qp_saved = qp_varp >= 0 ? player->varps[qp_varp] : 0;
    selftest_park_player(srv, player->x, player->z);
    amik_x = player->x;
    amik_z = player->z;
    amik_level = player->level;
    slot = npc_spawn(srv, amik_type, player->x + 1, player->z, player->level);
    SELFTEST_CHECK(slot >= 0, "Sir Amik should spawn beside the player");
    if( slot < 0 )
        return;
    guard_slot = -1;
    if( guard_type >= 0 )
        guard_slot = npc_spawn(srv, guard_type, player->x + 2, player->z, player->level);
    for( i = 0; i < 4; i++ )
        scene_slots[i] = -1;
    bkf_pass_begin();

    /* ---- 11 QP gate ---- */
    if( qp_varp >= 0 )
        player->varps[qp_varp] = 11;
    bkf_talk(srv, player, amik_type, slot);
    bkf_choose(srv, player, 1); /* I seek a quest */
    bkf_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 0, "11 QP must leave spy=0, got %d", player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_dossier) == 0, "11 QP must not grant a dossier");
    bkf_pass("11 QP left state 0 and no dossier");

    /* ---- refuse (looking around) ---- */
    if( qp_varp >= 0 )
        player->varps[qp_varp] = 12;
    bkf_talk(srv, player, amik_type, slot);
    bkf_choose(srv, player, 2); /* just looking around */
    bkf_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 0, "looking-around refuse must leave spy=0, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_dossier) == 0, "refuse must not grant a dossier");
    bkf_pass("refuse left state 0 and no dossier");

    /* ---- explicit No after briefing ---- */
    bkf_talk(srv, player, amik_type, slot);
    bkf_choose(srv, player, 1); /* seek quest */
    bkf_choose(srv, player, 1); /* laugh at danger */
    bkf_choose(srv, player, 2); /* No */
    bkf_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 0, "start No must leave spy=0, got %d", player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_dossier) == 0, "start No must not grant a dossier");
    bkf_pass("start No left state 0 and no dossier");

    /* ---- accept via Talk-to ---- */
    bkf_talk(srv, player, amik_type, slot);
    bkf_choose(srv, player, 1); /* seek quest */
    bkf_choose(srv, player, 1); /* laugh */
    bkf_choose(srv, player, 1); /* Yes */
    bkf_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 1, "accept via OPNPC1 should write spy=1, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_dossier) == 1,
                   "Talk-to accept should grant exactly 1 dossier, have %d",
                   selftest_count(player, obj_dossier));
    bkf_pass("accept via OPNPC1 granted dossier, state 1");

    /* ---- Read dossier (OPHELD1) ---- */
    bkf_release_park(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_dossier, -1, -1);
    bkf_drain(srv, player, 0);
    SELFTEST_CHECK(selftest_count(player, obj_dossier) == 0, "Read should destroy the dossier, have %d",
                   selftest_count(player, obj_dossier));
    SELFTEST_CHECK(player->varps[varp] == 1, "Read must not change spy, got %d", player->varps[varp]);
    bkf_pass("OPHELD1 Read destroyed the dossier");

    /* ---- SNAP fortress locs ---- */
    bkf_snap(srv, player, 0, 3016, 3514);
    loc_slot = loc_door1 >= 0 ? bkf_find_loc(3016, 3514, 0, loc_door1, 12) : -1;
    SELFTEST_CHECK(loc_slot >= 0, "bkfortressdoor1 should exist near 3016,3514");
    if( loc_slot >= 0 )
        bkf_pass("SceneFindLocId bkfortressdoor1");

    /* disguise door: bronze + iron */
    if( loc_slot >= 0 && obj_helm >= 0 && obj_chain >= 0 )
    {
        bkf_wear(player, TORIRSSERVER_WEAR_HEAD, obj_helm);
        bkf_wear(player, TORIRSSERVER_WEAR_BODY, obj_chain);
        bkf_release_park(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door1, -1, loc_slot);
        bkf_drain(srv, player, 0);
        SELFTEST_CHECK(player->varps[varp] == 1, "disguise door must not change spy, got %d",
                       player->varps[varp]);
        bkf_pass("disguise door OPLOC1 in bronze med helm + iron chainbody");
    }

    /* alt black plate disguise still accepted (door already proven; wear check via re-click) */
    if( loc_slot >= 0 && obj_bhelm >= 0 && obj_bbody >= 0 && obj_blegs >= 0 )
    {
        bkf_wear(player, TORIRSSERVER_WEAR_HEAD, obj_bhelm);
        bkf_wear(player, TORIRSSERVER_WEAR_BODY, obj_bbody);
        bkf_wear(player, TORIRSSERVER_WEAR_LEGS, obj_blegs);
        bkf_release_park(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door1, -1, loc_slot);
        bkf_drain(srv, player, 0);
        SELFTEST_CHECK(player->varps[varp] == 1, "black-plate door must not change spy, got %d",
                       player->varps[varp]);
        bkf_pass("disguise door OPLOC1 in black full helm + plate");
    }

    /* banquet + grill + cauldron existence */
    bkf_snap(srv, player, 0, 3021, 3510);
    loc_slot = loc_door2 >= 0 ? bkf_find_loc(3021, 3510, 0, loc_door2, 16) : -1;
    SELFTEST_CHECK(loc_slot >= 0, "bkfortressdoor2 banquet door should exist");
    if( loc_slot >= 0 )
    {
        bkf_pass("SceneFindLocId bkfortressdoor2");
        bkf_release_park(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door2, -1, loc_slot);
        bkf_choose(srv, player, 2); /* going in anyway, if the warning appeared */
        bkf_drain(srv, player, 0);
    }

    bkf_snap(srv, player, 0, 3026, 3509);
    loc_slot = loc_grill >= 0 ? bkf_find_loc(3026, 3509, 0, loc_grill, 16) : -1;
    SELFTEST_CHECK(loc_slot >= 0, "witchgrill should exist near 3026,3509");
    if( loc_slot >= 0 )
    {
        bkf_pass("SceneFindLocId witchgrill");
        if( npc_witch >= 0 )
            scene_slots[0] = npc_spawn(srv, npc_witch, 3027, 3506, 0);
        if( npc_captain >= 0 )
            scene_slots[1] = npc_spawn(srv, npc_captain, 3028, 3508, 0);
        if( npc_greldo >= 0 )
            scene_slots[2] = npc_spawn(srv, npc_greldo, 3030, 3505, 0);
        if( npc_cat >= 0 )
            scene_slots[3] = npc_spawn(srv, npc_cat, 3028, 3505, 0);
        bkf_release_park(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_grill, -1, loc_slot);
        bkf_drain(srv, player, 0);
        SELFTEST_CHECK(player->varps[varp] == 2, "grill Listen-at should write spy=2, got %d",
                       player->varps[varp]);
        bkf_pass("grill OPLOC1 advanced to state 2");
    }

    loc_slot = loc_cauldron >= 0 ? bkf_find_loc(3027, 3506, 0, loc_cauldron, 16) : -1;
    SELFTEST_CHECK(loc_slot >= 0, "bkf_cauldron_multi should exist near 3027,3506");
    if( loc_slot >= 0 )
        bkf_pass("SceneFindLocId bkf_cauldron_multi");

    /* cabbage discrimination + sabotage */
    bkf_snap(srv, player, 1, 3026, 3510);
    loc_slot = loc_hole >= 0 ? bkf_find_loc(3026, 3510, 1, loc_hole, 16) : -1;
    if( loc_slot < 0 && loc_hole >= 0 )
    {
        bkf_snap(srv, player, 2, 3026, 3510);
        loc_slot = bkf_find_loc(3026, 3510, 2, loc_hole, 16);
    }
    if( loc_slot < 0 && loc_hole >= 0 )
    {
        bkf_snap(srv, player, 0, 3026, 3510);
        loc_slot = bkf_find_loc(3026, 3510, 0, loc_hole, 16);
    }
    SELFTEST_CHECK(loc_slot >= 0, "blackknighthole should exist above the cauldron");
    if( loc_slot >= 0 )
    {
        bkf_pass("SceneFindLocId blackknighthole");
        if( obj_magic_cabbage >= 0 )
        {
            selftest_give(player, obj_magic_cabbage, 1);
            player->last_useitem = obj_magic_cabbage;
            bkf_release_park(srv, player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_hole, -1, loc_slot);
            bkf_drain(srv, player, 0);
            SELFTEST_CHECK(player->varps[varp] == 2, "Draynor cabbage must not sabotage, spy=%d",
                           player->varps[varp]);
            SELFTEST_CHECK(selftest_count(player, obj_magic_cabbage) == 1,
                           "Draynor cabbage must not be consumed");
            bkf_pass("magic_cabbage on hole rejected, state 2");
        }
        selftest_give(player, obj_cabbage, 1);
        player->last_useitem = obj_cabbage;
        bkf_release_park(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_hole, -1, loc_slot);
        bkf_drain(srv, player, 0);
        SELFTEST_CHECK(player->varps[varp] == 3, "ordinary cabbage should write spy=3, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(selftest_count(player, obj_cabbage) == 0, "ordinary cabbage should be consumed");
        if( cauldron_varp >= 0 )
            SELFTEST_CHECK(player->varps[cauldron_varp] != 0,
                           "sabotage should write spy_cauldron side state, got %d",
                           player->varps[cauldron_varp]);
        bkf_pass("cabbage OPLOCU sabotaged, state 3");
    }

    /* ---- post-sabotage grill ---- */
    bkf_snap(srv, player, 0, 3026, 3509);
    loc_slot = loc_grill >= 0 ? bkf_find_loc(3026, 3509, 0, loc_grill, 16) : -1;
    if( loc_slot >= 0 )
    {
        bkf_release_park(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_grill, -1, loc_slot);
        bkf_drain(srv, player, 0);
        SELFTEST_CHECK(player->varps[varp] == 3, "post-sabotage grill must stay at 3, got %d",
                       player->varps[varp]);
        bkf_pass("post-sabotage grill OPLOC1 stayed at 3");
    }

    /* ---- complete via Sir Amik ---- */
    selftest_clear_inv(player);
    qp_before = qp_varp >= 0 ? player->varps[qp_varp] : 0;
    coins_before = selftest_count(player, obj_coins);
    bkf_snap(srv, player, amik_level, amik_x, amik_z);
    bkf_talk(srv, player, amik_type, slot);
    bkf_drain_complete(srv, player, varp, 4);
    SELFTEST_CHECK(player->varps[varp] == 4, "hand-in should commit spy=4, got %d", player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_coins) == coins_before + 2500,
                   "hand-in should grant 2500 coins, %d -> %d", coins_before,
                   selftest_count(player, obj_coins));
    if( qp_varp >= 0 )
        SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 3, "hand-in should award 3 QP, %d -> %d",
                       qp_before, player->varps[qp_varp]);
    bkf_pass("complete via OPNPC1 +2500 coins +3 QP, state 4");

    /* ---- postquest ---- */
    bkf_talk(srv, player, amik_type, slot);
    bkf_choose(srv, player, 2); /* Okay, bye / looking around equivalent */
    bkf_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 4, "post-quest talk must stay at 4, got %d",
                   player->varps[varp]);
    bkf_pass("post-quest OPNPC1 stayed complete");

    if( slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot);
    if( guard_slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, guard_slot);
    for( i = 0; i < 4; i++ )
    {
        if( scene_slots[i] >= 0 )
            ToriRSServer_WorldNpcFree(srv, scene_slots[i]);
    }
    ToriRSServer_WorldNpcReap(srv);
    selftest_clear_inv(player);
    bkf_clear_worn(player);
    player->varps[varp] = 0;
    if( cauldron_varp >= 0 )
        player->varps[cauldron_varp] = 0;
    if( qp_varp >= 0 )
        player->varps[qp_varp] = qp_saved;
    bkf_pass("start-to-complete via real triggers");
}
