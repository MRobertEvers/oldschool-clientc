#ifndef TORIRSSERVER_TEST_QUEST_TROLL_LOVE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_TROLL_LOVE_SELFTEST_U_H

/* Troll Romance Gate D C walk. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD / OPHELDU dispatch on the
 * authored path. Silent success is forbidden: each step prints TROLL PASS.
 * player->godmode = 1 for the whole walk (not a death test).
 */

static void
troll_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "TROLL PASS: %s\n", step);
}

static void
troll_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static void
troll_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
troll_resume_mesbox(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    int com;
    int i;

    assert(srv);
    player = srv->active_player;
    assert(player);
    com = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
    if( com > 0 && player->active_script )
        ToriRSServer_ScriptsResumeButton(srv, com);
    for( i = 0; i < 4; i++ )
        selftest_tick(srv);
}

static void
troll_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
troll_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
troll_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = troll_chatmenu();
    if( chatmenu <= 0 )
        return;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static void
troll_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = troll_chatmenu();
    clicks = 0;
    while( clicks < max_pages && player->active_script )
    {
        if( player->resume_button_count > 0 && chatmenu > 0 &&
            player->resume_buttons[0] == chatmenu )
            return;
        if( selftest_click_through(srv, 1) <= 0 )
            break;
        clicks++;
    }
}

static int
troll_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    return n;
}

static void
troll_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( troll_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
    }
}

static void
selftest_quest_troll_love(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_ug;
    int npc_aga;
    int npc_arrg;
    int npc_tenzing;
    int npc_dunstan;
    int loc_piste;
    int loc_flowers;
    int varp_love;
    int varp_troll;
    int varp_death;
    int varp_map;
    int obj_flower;
    int obj_sled;
    int obj_waxed;
    int obj_wax;
    int obj_bucket;
    int obj_tar;
    int obj_tin;
    int obj_yew;
    int obj_iron;
    int obj_rope;
    int obj_law;
    int obj_diamond;
    int stat_agility;
    int ug_slot;
    int aga_slot;
    int arrg_slot;
    int tenzing_slot;
    int dunstan_slot;
    int i;

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    troll_god(player);

    npc_ug = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trollromance_ug");
    npc_aga = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trollromance_aga");
    npc_arrg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trollromance_arrg");
    npc_tenzing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_sherpa");
    npc_dunstan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_smithy");
    loc_piste = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "trollromance_piste_walk_barrier_down");
    loc_flowers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "trollromance_rareflowers");
    varp_love = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "troll_love");
    varp_troll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "troll_quest");
    varp_death = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_equiproom");
    varp_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_map");
    obj_flower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trollromance_rare_flower");
    obj_sled = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trollromance_toboggon");
    obj_waxed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trollromance_toboggon_waxed");
    obj_wax = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trollromance_wax");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_wax");
    obj_tar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamp_tar");
    obj_tin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cake_tin");
    obj_yew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yew_logs");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_law = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "law_talisman");
    obj_diamond = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "uncut_diamond");
    stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");

    SELFTEST_CHECK(npc_ug >= 0 && npc_aga >= 0 && npc_arrg >= 0 &&
                       npc_tenzing >= 0 && npc_dunstan >= 0 && loc_piste >= 0 &&
                       loc_flowers >= 0 && varp_love >= 0 && varp_troll >= 0 &&
                       varp_death >= 0 && varp_map >= 0 && obj_flower >= 0 &&
                       obj_sled >= 0 && obj_waxed >= 0 && obj_wax >= 0 &&
                       obj_bucket >= 0 && obj_tar >= 0 && obj_tin >= 0 &&
                       obj_yew >= 0 && obj_iron >= 0 && obj_rope >= 0 &&
                       obj_diamond >= 0 && stat_agility >= 0,
                   "troll romance C-side names should all resolve");
    if( npc_ug < 0 || varp_love < 0 || npc_aga < 0 || npc_tenzing < 0 ||
        npc_dunstan < 0 )
    {
        ToriRSServer_ScriptsFree(srv);
        return;
    }

    troll_clear_inv(player);
    player->varps[varp_love] = 0;
    player->varps[varp_troll] = 50; /* ^troll_complete */
    player->varps[varp_death] = 80; /* ^death_complete */
    player->varps[varp_map] = 6;    /* ^death_given_supplies */
    ToriRSServer_CombatSetLevel(player, stat_agility, 99);

    /* ---- Ug start: Troll Stronghold prereq mesbox ---- */
    ToriRSServer_WorldTeleport(srv, 1, 2827, 10064);
    selftest_tick(srv);
    ug_slot = ToriRSServer_WorldNpcSpawn(srv, npc_ug, 2827, 10064, 1);
    SELFTEST_CHECK(ug_slot >= 0, "trollromance_ug should spawn");
    if( ug_slot >= 0 )
    {
        player->varps[varp_troll] = 0;
        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "opnpc1 Ug without Troll Stronghold should open the prereq mesbox");
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 0,
                       "prereq refuse must leave troll_love at 0, got %d",
                       player->varps[varp_love]);
        troll_pass("opnpc1_ug_prereq");

        player->varps[varp_troll] = 50;
        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 0,
                       "declining Ug must leave troll_love at 0, got %d",
                       player->varps[varp_love]);
        troll_pass("opnpc1_ug_decline");

        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 1);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 3);
        selftest_click_through(srv, 12);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 5,
                       "accepting Ug should write troll_love=5, got %d",
                       player->varps[varp_love]);
        troll_pass("opnpc1_ug_accept");
    }

    /* ---- Aga learn Trollweiss ---- */
    ToriRSServer_WorldTeleport(srv, 1, 2828, 10104);
    selftest_tick(srv);
    aga_slot = ToriRSServer_WorldNpcSpawn(srv, npc_aga, 2828, 10104, 1);
    SELFTEST_CHECK(aga_slot >= 0, "trollromance_aga should spawn");
    if( aga_slot >= 0 )
    {
        player->varps[varp_love] = 5;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aga, -1, aga_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 2);
        selftest_click_through(srv, 16);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 10,
                       "Aga love-life branch should write troll_love=10, got %d",
                       player->varps[varp_love]);
        troll_pass("opnpc1_aga_learn_trollweiss");

        troll_clear_inv(player);
        troll_give(player, obj_flower, 1);
        player->last_useitem = obj_flower;
        player->last_useslot = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_aga, -1, aga_slot);
        troll_close(srv);
        SELFTEST_CHECK(troll_inv_total(player, obj_flower) == 1,
                       "using Trollweiss on Aga must not consume the flower");
        troll_pass("opnpcu_aga_flower");
    }

    /* ---- Tenzing tenzing_trollweiss ---- */
    ToriRSServer_WorldTeleport(srv, 0, 2820, 3556);
    selftest_tick(srv);
    tenzing_slot = ToriRSServer_WorldNpcSpawn(srv, npc_tenzing, 2820, 3556, 0);
    SELFTEST_CHECK(tenzing_slot >= 0, "death_sherpa should spawn");
    if( tenzing_slot >= 0 )
    {
        player->varps[varp_love] = 10;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tenzing, -1,
                                       tenzing_slot);
        selftest_click_through(srv, 24);
        troll_resume_mesbox(srv);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 15,
                       "Tenzing trollweiss talk should write troll_love=15, got %d",
                       player->varps[varp_love]);
        troll_pass("opnpc1_tenzing_trollweiss");
    }

    /* ---- Dunstan sled-materials handoff ---- */
    ToriRSServer_WorldTeleport(srv, 0, 2919, 3574);
    selftest_tick(srv);
    dunstan_slot = ToriRSServer_WorldNpcSpawn(srv, npc_dunstan, 2919, 3574, 0);
    SELFTEST_CHECK(dunstan_slot >= 0, "death_smithy should spawn");
    if( dunstan_slot >= 0 )
    {
        troll_clear_inv(player);
        if( obj_law >= 0 )
            troll_give(player, obj_law, 1);
        troll_give(player, obj_yew, 1);
        troll_give(player, obj_iron, 1);
        troll_give(player, obj_rope, 1);
        player->varps[varp_love] = 15;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1,
                                       dunstan_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 4);
        selftest_click_through(srv, 16);
        troll_resume_mesbox(srv);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 22,
                       "Dunstan sled handoff should write troll_love=22, got %d",
                       player->varps[varp_love]);
        SELFTEST_CHECK(troll_inv_total(player, obj_sled) >= 1,
                       "Dunstan should grant trollromance_toboggon");
        SELFTEST_CHECK(troll_inv_total(player, obj_yew) == 0 &&
                           troll_inv_total(player, obj_iron) == 0 &&
                           troll_inv_total(player, obj_rope) == 0,
                       "Dunstan should take logs, iron bar and rope");
        troll_pass("opnpc1_dunstan_sled");
    }

    /* ---- Wax recipe + wax-on-sled ---- */
    troll_close(srv);
    player->active_script = NULL;
    troll_clear_inv(player);
    troll_give(player, obj_bucket, 1);
    troll_give(player, obj_tar, 1);
    selftest_useon(srv, obj_bucket, 1, obj_tar, 1, NULL, 0);
    troll_close(srv);
    SELFTEST_CHECK(troll_inv_total(player, obj_wax) == 0,
                   "wax without cake tin must not produce trollromance_wax");
    troll_pass("opheldu_wax_need_tin");

    troll_clear_inv(player);
    troll_give(player, obj_bucket, 1);
    troll_give(player, obj_tar, 1);
    troll_give(player, obj_tin, 1);
    selftest_useon(srv, obj_bucket, 1, obj_tar, 1, NULL, 0);
    troll_close(srv);
    SELFTEST_CHECK(troll_inv_total(player, obj_wax) == 1,
                   "bucket_wax + swamp_tar + cake_tin should make trollromance_wax");
    troll_pass("opheldu_make_wax");

    troll_clear_inv(player);
    troll_give(player, obj_wax, 1);
    troll_give(player, obj_sled, 1);
    selftest_useon(srv, obj_wax, 1, obj_sled, 1, NULL, 0);
    troll_close(srv);
    SELFTEST_CHECK(troll_inv_total(player, obj_waxed) == 1,
                   "wax on sled should make trollromance_toboggon_waxed");
    troll_pass("opheldu_wax_sled");

    /* ---- Slide + pick flower ---- */
    ToriRSServer_WorldTeleport(srv, 0, 2752, 3712);
    selftest_tick(srv);
    player->varps[varp_love] = 22;
    troll_clear_inv(player);
    troll_give(player, obj_waxed, 1);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_piste, -1, -1);
    troll_resume_mesbox(srv);
    troll_close(srv);
    SELFTEST_CHECK(player->varps[varp_love] == 25,
                   "sliding the waxed sled should write troll_love=25, got %d",
                   player->varps[varp_love]);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "sliding must leave the player alive");
    troll_pass("oploc1_piste_slide");

    player->varps[varp_love] = 25;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC2, loc_flowers, -1, -1);
    troll_resume_mesbox(srv);
    troll_close(srv);
    SELFTEST_CHECK(player->varps[varp_love] == 30,
                   "picking Trollweiss should write troll_love=30, got %d",
                   player->varps[varp_love]);
    SELFTEST_CHECK(troll_inv_total(player, obj_flower) >= 1,
                   "picking should grant trollromance_rare_flower");
    troll_pass("oploc2_pick_trollweiss");

    /* ---- Ug flower hand-in + wielding refuse ---- */
    ToriRSServer_WorldTeleport(srv, 1, 2827, 10064);
    selftest_tick(srv);
    if( ug_slot < 0 )
        ug_slot = ToriRSServer_WorldNpcSpawn(srv, npc_ug, 2827, 10064, 1);
    if( ug_slot >= 0 )
    {
        troll_clear_inv(player);
        player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id = obj_flower;
        player->worn[TORIRSSERVER_WEAR_WEAPON].count = 1;
        player->varps[varp_love] = 30;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 30,
                       "wielding Trollweiss must not hand the flower in");
        troll_pass("opnpc1_ug_wielding_refuse");
        player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id = -1;
        player->worn[TORIRSSERVER_WEAR_WEAPON].count = 0;

        troll_clear_inv(player);
        troll_give(player, obj_flower, 1);
        player->varps[varp_love] = 30;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot);
        selftest_click_through(srv, 24);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 35,
                       "handing Trollweiss to Ug should write troll_love=35, got %d",
                       player->varps[varp_love]);
        SELFTEST_CHECK(troll_inv_total(player, obj_flower) == 0,
                       "Ug should take the Trollweiss");
        troll_pass("opnpc1_ug_flower_handin");
    }

    /* ---- Arrg cheese-off / fight confirm ---- */
    ToriRSServer_WorldTeleport(srv, 1, 2828, 10095);
    selftest_tick(srv);
    arrg_slot = ToriRSServer_WorldNpcSpawn(srv, npc_arrg, 2828, 10095, 1);
    SELFTEST_CHECK(arrg_slot >= 0, "trollromance_arrg should spawn");
    if( arrg_slot >= 0 )
    {
        player->varps[varp_love] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_arrg, -1, arrg_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "opnpc1 Arrg before Aga should open the cheese-off");
        troll_close(srv);
        troll_pass("opnpc1_arrg_cheese_off");

        player->varps[varp_love] = 35;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_arrg, -1, arrg_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 1);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 35,
                       "declining the arena must leave troll_love at 35, got %d",
                       player->varps[varp_love]);
        troll_pass("opnpc1_arrg_fight_decline");
    }

    /* ---- Ug completion + real quest-complete scroll ---- */
    if( ug_slot >= 0 )
    {
        troll_clear_inv(player);
        player->varps[varp_love] = 40;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ug, -1, ug_slot);
        selftest_click_through(srv, 24);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp_love] == 45,
                       "Ug reward talk should write troll_love=45, got %d",
                       player->varps[varp_love]);
        SELFTEST_CHECK(troll_inv_total(player, obj_diamond) >= 1,
                       "completion should grant an uncut diamond");
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "completion must leave the player alive");
        troll_pass("opnpc1_ug_complete_scroll");
    }

    /* Journal states open a mesbox / questjournal modal. */
    player->varps[varp_love] = 0;
    ToriRSServer_ScriptsRunProc(srv, "[proc,troll_love_journal]", NULL, 0);
    SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                   "journal at not-started should open");
    troll_close(srv);
    troll_pass("journal_not_started");

    player->varps[varp_love] = 45;
    ToriRSServer_ScriptsRunProc(srv, "[proc,troll_love_journal]", NULL, 0);
    troll_close(srv);
    troll_pass("journal_complete");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "the Troll Romance walk must leave the player alive");
    for( i = 0; i < 2; i++ )
        selftest_tick(srv);
    troll_close(srv);
}

#endif
