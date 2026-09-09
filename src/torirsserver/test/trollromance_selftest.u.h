/* Troll Romance (quest_troll_love / QH trollromance).
 *
 * Wiki RAW oldids: article 15326713, Quick_guide 14845426,
 * Transcript 15327938. QH steps.put 0/5/10/15/20/22/25/30/35/40.
 *
 * Placed immediately before a selftest_reset_world so spawned npcs and
 * ticks cannot re-aim later stanzas. Every critical-path step is a real
 * opnpc1 / opheldu / oploc1 / oploc2 dispatch, not a label call.
 */
static int
trollromance_on_chatmenu(struct ToriRSServer* srv)
{
    int chatmenu;

    assert(srv);
    assert(srv->active_player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    return chatmenu > 0 && srv->active_player->resume_button_count > 0 &&
           srv->active_player->resume_buttons[0] == chatmenu;
}

static void
trollromance_continue(struct ToriRSServer* srv, int max_pages)
{
    int clicks = 0;
    int chatmenu;

    assert(srv);
    assert(srv->active_player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    while( clicks < max_pages && srv->active_player->active_script )
    {
        int uid;
        uint8_t resume[4];

        if( srv->active_player->resume_button_count <= 0 )
            break;
        uid = srv->active_player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            break;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(srv->active_player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        clicks++;
    }
}

static int
trollromance_choose(struct ToriRSServer* srv, int row)
{
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    assert(srv->active_player);
    assert(row > 0);
    trollromance_continue(srv, 32);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    if( chatmenu <= 0 || !trollromance_on_chatmenu(srv) )
        return 0;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(srv->active_player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    return 1;
}

static void
trollromance_close(struct ToriRSServer* srv)
{
    assert(srv);
    trollromance_continue(srv, 32);
    ToriRSServer_WorldCloseModal(srv);
}

static void
trollromance_pass(const char* line)
{
    assert(line);
    fprintf(stderr, "ToriRSServer selftest: trollromance PASS %s\n", line);
}

static int
trollromance_find_type(struct ToriRSServer* srv, int type)
{
    int i;

    assert(srv);
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
        if( srv->npcs[i].active && srv->npcs[i].type == type )
            return i;
    return -1;
}

static void
selftest_trollromance(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int saved_god;
    int varp_love;
    int varp_troll;
    int varp_death;
    int varp_map;
    int npc_ug;
    int npc_aga;
    int npc_tenzing;
    int npc_dunstan;
    int npc_arrg;
    int npc_arrg_atk;
    int loc_slope;
    int loc_flower;
    int obj_yew;
    int obj_iron;
    int obj_rope;
    int obj_tar;
    int obj_wax_bucket;
    int obj_tin;
    int obj_wax;
    int obj_sled;
    int obj_sled_waxed;
    int obj_flower;
    int obj_diamond;
    int obj_ruby;
    int obj_emerald;
    int stat_agility;
    int stat_strength;
    int love;
    int st_started;
    int st_aga;
    int st_tenzing;
    int st_materials;
    int st_sled;
    int st_waxed;
    int st_picked;
    int st_arrg;
    int st_defeated;
    int st_complete;
    int ug_slot;
    int aga_slot;
    int tenzing_slot;
    int dunstan_slot;
    int arrg_slot;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: trollromance\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  trollromance: no compiled script pack\n");
        return;
    }

    saved_god = player->godmode;
    player->godmode = 1;
    player->stat_level[TORIRSSERVER_STAT_HITPOINTS] = 99;
    player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS] = 99;
    player->hitpoints = 99;
    player->max_hitpoints = 99;

    varp_love = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "troll_love");
    varp_troll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "troll_quest");
    varp_death = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_equiproom");
    varp_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_map");
    npc_ug = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trollromance_ug");
    npc_aga = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trollromance_aga");
    npc_tenzing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_sherpa");
    npc_dunstan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_smithy");
    npc_arrg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trollromance_arrg");
    npc_arrg_atk =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trollromance_arrg_attackable");
    loc_slope =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "trollromance_piste_walk_barrier_down");
    loc_flower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "trollromance_rareflowers");
    obj_yew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yew_logs");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_tar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamp_tar");
    obj_wax_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_wax");
    obj_tin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cake_tin");
    obj_wax = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trollromance_wax");
    obj_sled = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trollromance_toboggon");
    obj_sled_waxed =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trollromance_toboggon_waxed");
    obj_flower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trollromance_rare_flower");
    obj_diamond = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "uncut_diamond");
    obj_ruby = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "uncut_ruby");
    obj_emerald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "uncut_emerald");
    stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_strength = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "strength");
    st_started = ToriRSServer_ContentConstantInt("troll_love_started", 5);
    st_aga = ToriRSServer_ContentConstantInt("troll_love_aga_wants_trollweiss", 10);
    st_tenzing = ToriRSServer_ContentConstantInt("troll_love_learnt_about_trollweiss", 15);
    st_materials = ToriRSServer_ContentConstantInt("troll_love_bring_dunstan_materials", 20);
    st_sled = ToriRSServer_ContentConstantInt("troll_love_dunstan_made_sled", 22);
    st_waxed = ToriRSServer_ContentConstantInt("troll_love_waxed_sled", 25);
    st_picked = ToriRSServer_ContentConstantInt("troll_love_picked_trollweiss", 30);
    st_arrg = ToriRSServer_ContentConstantInt("troll_love_dispose_of_arrg", 35);
    st_defeated = ToriRSServer_ContentConstantInt("troll_love_defeated_arrg", 40);
    st_complete = ToriRSServer_ContentConstantInt("troll_love_complete", 45);

    SELFTEST_CHECK(varp_love >= 0 && varp_troll >= 0 && varp_death >= 0 && npc_ug >= 0 &&
                       npc_aga >= 0 && npc_tenzing >= 0 && npc_dunstan >= 0 && npc_arrg >= 0 &&
                       npc_arrg_atk >= 0 && loc_slope >= 0 && loc_flower >= 0 && obj_yew >= 0 &&
                       obj_iron >= 0 && obj_rope >= 0 && obj_tar >= 0 && obj_wax_bucket >= 0 &&
                       obj_tin >= 0 && obj_wax >= 0 && obj_sled >= 0 && obj_sled_waxed >= 0 &&
                       obj_flower >= 0 && obj_diamond >= 0 && obj_ruby >= 0 && obj_emerald >= 0 &&
                       stat_agility >= 0 && stat_strength >= 0,
                   "trollromance pack names should all resolve");
    if( varp_love < 0 || npc_ug < 0 || npc_aga < 0 || npc_tenzing < 0 || npc_dunstan < 0 ||
        loc_slope < 0 || loc_flower < 0 )
    {
        player->godmode = saved_god;
        return;
    }

    selftest_clear_inv(player);
    player->varps[varp_love] = 0;
    if( varp_troll >= 0 )
        player->varps[varp_troll] = ToriRSServer_ContentConstantInt("troll_complete", 50);
    if( varp_death >= 0 )
        player->varps[varp_death] = ToriRSServer_ContentConstantInt("death_complete", 80);
    if( varp_map >= 0 )
        player->varps[varp_map] = ToriRSServer_ContentConstantInt("death_scouted_area", 8);
    player->stat_level[stat_agility] = 28;
    player->stat_boosted[stat_agility] = 28;

    ug_slot = ToriRSServer_WorldNpcSpawn(srv, npc_ug, player->x + 1, player->z, player->level);
    aga_slot = ToriRSServer_WorldNpcSpawn(srv, npc_aga, player->x + 2, player->z, player->level);
    tenzing_slot =
        ToriRSServer_WorldNpcSpawn(srv, npc_tenzing, player->x + 1, player->z + 1, player->level);
    dunstan_slot =
        ToriRSServer_WorldNpcSpawn(srv, npc_dunstan, player->x + 2, player->z + 1, player->level);
    arrg_slot = ToriRSServer_WorldNpcSpawn(srv, npc_arrg, player->x + 3, player->z, player->level);
    SELFTEST_CHECK(ug_slot >= 0 && aga_slot >= 0 && tenzing_slot >= 0 && dunstan_slot >= 0 &&
                       arrg_slot >= 0,
                   "trollromance npcs should spawn");
    if( ug_slot < 0 || aga_slot < 0 || tenzing_slot < 0 || dunstan_slot < 0 || arrg_slot < 0 )
    {
        player->godmode = saved_god;
        return;
    }

    /* QH 0 -> 5: [opnpc1,trollromance_ug] accept. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,trollromance_ug] should run");
    SELFTEST_CHECK(trollromance_choose(srv, 1), "Ug offer row 1 (Awww)");
    SELFTEST_CHECK(trollromance_choose(srv, 3), "Ug offer row 3 (Don't worry)");
    trollromance_close(srv);
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_started, "Ug accept should set troll_love %d, got %d", st_started,
                   love);
    if( love == st_started )
        trollromance_pass("step 0->5 Ug accept via opnpc1");

    /* QH 5 -> 10: [opnpc1,trollromance_aga]. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aga, -1, aga_slot) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,trollromance_aga] should run");
    SELFTEST_CHECK(trollromance_choose(srv, 2), "Aga row 2 (love life)");
    trollromance_choose(srv, 1);
    trollromance_close(srv);
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_aga, "Aga should set troll_love %d, got %d", st_aga, love);
    if( love == st_aga )
        trollromance_pass("step 5->10 Aga via opnpc1");

    /* QH 10 -> 15: [opnpc1,death_sherpa] Trollweiss + sled question. */
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tenzing, -1, tenzing_slot) ==
            TORIRSSERVER_TRIGGER_RAN,
        "[opnpc1,death_sherpa] should run");
    SELFTEST_CHECK(trollromance_choose(srv, 4), "Tenzing row 4 (Trollweiss)");
    SELFTEST_CHECK(trollromance_choose(srv, 2), "Tenzing sled row 2 (What would I need)");
    trollromance_close(srv);
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_tenzing, "Tenzing should set troll_love %d, got %d", st_tenzing,
                   love);
    if( love == st_tenzing )
        trollromance_pass("step 10->15 Tenzing via opnpc1");

    /* QH 15 -> 20: first Dunstan talk requests materials only. */
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1, dunstan_slot) ==
            TORIRSSERVER_TRIGGER_RAN,
        "[opnpc1,death_smithy] first sled talk should run");
    SELFTEST_CHECK(trollromance_choose(srv, 1), "Dunstan row 1 (Talk about a quest)");
    SELFTEST_CHECK(trollromance_choose(srv, 2), "Dunstan row 2 (I need a sled!!)");
    SELFTEST_CHECK(trollromance_choose(srv, 2), "Dunstan Yes/No row 2 (No.)");
    trollromance_close(srv);
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_materials, "first Dunstan talk should set troll_love %d, got %d",
                   st_materials, love);
    SELFTEST_CHECK(selftest_count(player, obj_sled) == 0,
                   "first Dunstan talk must not hand a sled yet");
    if( love == st_materials )
        trollromance_pass("step 15->20 Dunstan request via opnpc1");

    /* QH 20 -> 22: second Dunstan talk with materials. */
    selftest_clear_inv(player);
    selftest_give(player, obj_yew, 1);
    selftest_give(player, obj_iron, 1);
    selftest_give(player, obj_rope, 1);
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1, dunstan_slot) ==
            TORIRSSERVER_TRIGGER_RAN,
        "[opnpc1,death_smithy] materials hand-in should run");
    SELFTEST_CHECK(trollromance_choose(srv, 1), "Dunstan second talk (Talk about a quest)");
    trollromance_choose(srv, 4); /* See you! on the wax-hint menu */
    trollromance_close(srv);
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_sled, "second Dunstan talk should set troll_love %d, got %d", st_sled,
                   love);
    SELFTEST_CHECK(selftest_count(player, obj_sled) == 1, "Dunstan should grant trollromance_toboggon");
    if( love == st_sled && selftest_count(player, obj_sled) == 1 )
        trollromance_pass("step 20->22 Dunstan sled via opnpc1");

    /* QH 22 -> 25: real opheldu wax recipe + wax-the-sled. */
    selftest_give(player, obj_tar, 1);
    selftest_give(player, obj_wax_bucket, 1);
    selftest_give(player, obj_tin, 1);
    player->last_item = obj_wax_bucket;
    player->last_slot = 0;
    player->last_useitem = obj_tar;
    player->last_useslot = 1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunOpheldu(srv, obj_wax_bucket, -1, obj_tar, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[opheldu,bucket_wax] swamp_tar should run");
    trollromance_close(srv);
    SELFTEST_CHECK(selftest_count(player, obj_wax) == 1, "tar on bucket_wax should make sled wax");
    player->last_item = obj_sled;
    player->last_useitem = obj_wax;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunOpheldu(srv, obj_sled, -1, obj_wax, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[opheldu,trollromance_toboggon] wax should run");
    trollromance_close(srv);
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_waxed, "waxing the sled should set troll_love %d, got %d", st_waxed,
                   love);
    SELFTEST_CHECK(selftest_count(player, obj_sled_waxed) == 1, "wax + sled should make waxed sled");
    if( love == st_waxed )
        trollromance_pass("step 22->25 wax via opheldu");

    /* Slide is travel (soft-skip teleport). Keep state 25. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_slope, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[oploc1,trollromance_piste_walk_barrier_down] should run");
    trollromance_close(srv);
    SELFTEST_CHECK(player->varps[varp_love] == st_waxed, "slide must not skip past waxed_sled");
    trollromance_pass("slide via oploc1 (state stays 25)");

    /* QH 25 -> 30: [oploc2,trollromance_rareflowers]. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_flower, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[oploc2,trollromance_rareflowers] should run");
    trollromance_close(srv);
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_picked, "picking Trollweiss should set troll_love %d, got %d",
                   st_picked, love);
    SELFTEST_CHECK(selftest_count(player, obj_flower) == 1, "pick should grant trollromance_rare_flower");
    if( love == st_picked )
        trollromance_pass("step 25->30 flower via oploc2");

    /* Lost-flower replacement stays at 30. */
    {
        int s;

        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
            if( player->inv[s].obj_id == obj_flower )
            {
                player->inv[s].obj_id = -1;
                player->inv[s].count = 0;
            }
    }
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_flower, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "re-pick after loss should run");
    trollromance_close(srv);
    SELFTEST_CHECK(player->varps[varp_love] == st_picked && selftest_count(player, obj_flower) == 1,
                   "lost flower must be replaceable without leaving state %d", st_picked);
    trollromance_pass("lost-flower replacement via oploc2");

    /* QH 30 -> 35: give flower to Ug. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,trollromance_ug] flower hand-in should run");
    trollromance_close(srv);
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_arrg, "Ug flower hand-in should set troll_love %d, got %d", st_arrg,
                   love);
    SELFTEST_CHECK(selftest_count(player, obj_flower) == 0, "Ug should take the Trollweiss");
    if( love == st_arrg )
        trollromance_pass("step 30->35 Ug flower via opnpc1");

    /* QH 35 -> 40: challenge Arrg, kill the attackable shell. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_arrg, -1, arrg_slot) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,trollromance_arrg] should run");
    SELFTEST_CHECK(trollromance_choose(srv, 1), "Arrg row 1 (I am here to kill you!)");
    SELFTEST_CHECK(trollromance_choose(srv, 1), "Arrg confirm row 1 (enter arena)");
    trollromance_close(srv);
    {
        int atk = trollromance_find_type(srv, npc_arrg_atk);
        int t;

        SELFTEST_CHECK(atk >= 0, "challenging Arrg should npc_add trollromance_arrg_attackable");
        if( atk >= 0 )
        {
            struct ToriRSServerNpc* npc = &srv->npcs[atk];

            ToriRSServer_CombatHitNpc(srv, atk, 0, npc->hitpoints);
            for( t = 0; t < 16 && player->varps[varp_love] != st_defeated; t++ )
                selftest_tick(srv);
        }
    }
    love = player->varps[varp_love];
    SELFTEST_CHECK(love == st_defeated, "killing Arrg should set troll_love %d, got %d",
                   st_defeated, love);
    if( love == st_defeated )
        trollromance_pass("step 35->40 Arrg kill via opnpc1 + combat");

    /* QH 40 -> 45: return to Ug. */
    selftest_clear_inv(player);
    {
        int xp_agi = player->stat_xp_tenths[stat_agility];
        int xp_str = player->stat_xp_tenths[stat_strength];
        int drain;

        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot) ==
                           TORIRSSERVER_TRIGGER_RAN,
                       "[opnpc1,trollromance_ug] completion should run");
        selftest_click_through(srv, 24);
        for( drain = 0; drain < 40 && player->varps[varp_love] != st_complete; drain++ )
        {
            ToriRSServer_WorldCloseModal(srv);
            selftest_tick(srv);
        }
        love = player->varps[varp_love];
        SELFTEST_CHECK(love == st_complete, "Ug reward should set troll_love %d, got %d",
                       st_complete, love);
        SELFTEST_CHECK(selftest_count(player, obj_diamond) == 1 &&
                           selftest_count(player, obj_ruby) == 2 &&
                           selftest_count(player, obj_emerald) == 4,
                       "reward should be 1 uncut diamond, 2 uncut ruby, 4 uncut emerald");
        SELFTEST_CHECK(player->stat_xp_tenths[stat_agility] > xp_agi &&
                           player->stat_xp_tenths[stat_strength] > xp_str,
                       "completion should award Agility and Strength xp");
        SELFTEST_CHECK(player->godmode == 1 && player->hitpoints > 0,
                       "player must stay alive (godmode), hp=%d", player->hitpoints);
        if( love == st_complete )
            trollromance_pass("step 40->45 complete via opnpc1");
    }

    if( ug_slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, ug_slot);
    if( aga_slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, aga_slot);
    if( tenzing_slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, tenzing_slot);
    if( dunstan_slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, dunstan_slot);
    if( arrg_slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, arrg_slot);
    {
        int leftover = trollromance_find_type(srv, npc_arrg_atk);

        if( leftover >= 0 )
            ToriRSServer_WorldNpcFree(srv, leftover);
    }
    ToriRSServer_WorldNpcReap(srv);
    selftest_clear_inv(player);
    player->varps[varp_love] = 0;
    player->godmode = saved_god;
}
