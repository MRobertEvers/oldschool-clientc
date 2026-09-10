#ifndef TORIRSSERVER_TEST_QUEST_WANTED_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_WANTED_SELFTEST_U_H

/* Wanted! Gate D C walk. Included from torirs_server_world_selftest.c and
 * invoked immediately before a selftest_reset_world so spawned npcs cannot
 * leak. Real OPNPC / OPHELD1 / OPHELD2 on the authored path. player->godmode
 * = 1 for the whole walk (no death test). Completion goes through Amik
 * hand-in -> ~wanted_quest_complete. Additive Wanted! only.
 *
 * Gate: TORIRSSERVER_SELFTEST_WANTED_ONLY=1
 *
 * ::wantedrun is a narrated soft-skip and is not used here.
 */

#define WANTED_NOT_STARTED 0
#define WANTED_AMIK_FIRST 3
#define WANTED_TIFFY_SECOND 4
#define WANTED_AMIK_SECOND 5
#define WANTED_TIFFY_THIRD 6
#define WANTED_GET_COMMORB 7
#define WANTED_INVESTIGATION 8
#define WANTED_HUNT 9
#define WANTED_FINAL_BATTLE 10
#define WANTED_COMPLETE 11

#define WANTED_DAQ_NONE 0
#define WANTED_DAQ_TALKED 1
#define WANTED_DAQ_BK_DEAD 2

#define WANTED_RD_COMPLETE 4
#define WANTED_ETA_COMPLETE 4
#define WANTED_LT_COMPLETE 11
#define WANTED_PIP_COMPLETE 60
#define WANTED_BKF_COMPLETE 4
#define WANTED_REQ_QP 32
#define WANTED_REWARD_SLAYER 50000
#define WANTED_COMMORB_GP 10000
#define WANTED_ESSENCE_NEEDED 20

#define WANTED_TIFFY_X 2997
#define WANTED_TIFFY_Z 3373
#define WANTED_AMIK_X 2961
#define WANTED_AMIK_Z 3339
#define WANTED_DAQ_X 2891
#define WANTED_DAQ_Z 9681
#define WANTED_MAGE_X 3258
#define WANTED_MAGE_Z 3388
#define WANTED_CANIFIS_X 3485
#define WANTED_CANIFIS_Z 3481
#define WANTED_RELLEKKA_X 2659
#define WANTED_RELLEKKA_Z 3657
#define WANTED_MUSA_X 2916
#define WANTED_MUSA_Z 3160
#define WANTED_WIZARDS_X 3109
#define WANTED_WIZARDS_Z 3160
#define WANTED_DORGESH_X 3318
#define WANTED_DORGESH_Z 9628
#define WANTED_ARDOUGNE_X 2661
#define WANTED_ARDOUGNE_Z 3307
#define WANTED_CHAMPIONS_X 3191
#define WANTED_CHAMPIONS_Z 3357
#define WANTED_ESSENCE_X 2909
#define WANTED_ESSENCE_Z 4827

static void
wanted_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "WANTED PASS: %s\n", step);
}

static void
wanted_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
wanted_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
wanted_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 64 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
wanted_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
wanted_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = wanted_chatmenu();
    if( chatmenu <= 0 )
        return;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
wanted_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = wanted_chatmenu();
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

static void
wanted_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    wanted_god(player);
    selftest_tick(srv);
}

static int
wanted_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    wanted_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
wanted_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
wanted_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    return n;
}

static void
wanted_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( wanted_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
        if( player->inv[s].obj_id == obj_id )
        {
            inv_set(player, s, obj_id, player->inv[s].count + count);
            return;
        }
    }
}

static void
wanted_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
wanted_get_bit(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
wanted_set_varp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        player->varps[varp] = value;
}

static void
wanted_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
wanted_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    wanted_talk(srv, npc_type, slot);
    wanted_finish(srv);
}

static void
wanted_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    wanted_talk(srv, npc_type, slot);
    wanted_click_until_menu(srv, 16);
    wanted_pick_row(srv, row);
    wanted_finish(srv);
}

static void
wanted_talk_pick2(
    struct ToriRSServer* srv,
    int npc_type,
    int slot,
    int row1,
    int row2)
{
    assert(srv);
    wanted_talk(srv, npc_type, slot);
    wanted_click_until_menu(srv, 16);
    wanted_pick_row(srv, row1);
    wanted_click_until_menu(srv, 16);
    wanted_pick_row(srv, row2);
    wanted_finish(srv);
}

static void
wanted_opheld(struct ToriRSServer* srv, int trigger, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, trigger, obj_id, -1, -1);
    wanted_finish(srv);
}

static void
wanted_opheld_pick(struct ToriRSServer* srv, int trigger, int obj_id, int row)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, trigger, obj_id, -1, -1);
    wanted_click_until_menu(srv, 16);
    wanted_pick_row(srv, row);
    wanted_finish(srv);
}

static void
wanted_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, obj_id, 1);
}

static void
wanted_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    wanted_set_bit(srv, "wanted_main", WANTED_NOT_STARTED);
    wanted_set_bit(srv, "wanted_joke_option", 0);
    wanted_set_bit(srv, "wanted_commorb_intel", 0);
    wanted_set_bit(srv, "wanted_daquarius_hint", WANTED_DAQ_NONE);
    wanted_set_bit(srv, "wanted_lord_d_exposition", 0);
    wanted_set_bit(srv, "wanted_zammy_mage_hint", 0);
    wanted_set_bit(srv, "wanted_mission1", 0);
    wanted_set_bit(srv, "wanted_mission1complete", 0);
    wanted_set_bit(srv, "wanted_mission2", 0);
    wanted_set_bit(srv, "wanted_mission2complete", 0);
    wanted_set_bit(srv, "wanted_mission3", 0);
    wanted_set_bit(srv, "wanted_mission3complete", 0);
    wanted_set_bit(srv, "wanted_mission4", 0);
    wanted_set_bit(srv, "wanted_mission4complete", 0);
    wanted_set_bit(srv, "wanted_mission5", 0);
    wanted_set_bit(srv, "wanted_mission5complete", 0);
    wanted_set_bit(srv, "wanted_mission8", 0);
    wanted_set_bit(srv, "wanted_mission8complete", 0);
    wanted_set_bit(srv, "wanted_mission12", 0);
    wanted_set_bit(srv, "wanted_mission12complete", 0);
    wanted_set_bit(srv, "wanted_mission15", 0);
    wanted_set_bit(srv, "wanted_mission15complete", 0);
}

static void
wanted_set_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    wanted_set_varp(player, "qp", WANTED_REQ_QP);
    wanted_set_varp(player, "abyssal_miniquest", WANTED_ETA_COMPLETE);
    wanted_set_varp(player, "priestperil", WANTED_PIP_COMPLETE);
    wanted_set_varp(player, "spy", WANTED_BKF_COMPLETE);
    wanted_set_bit(srv, "rd_main", WANTED_RD_COMPLETE);
    wanted_set_bit(srv, "lost_tribe_quest", WANTED_LT_COMPLETE);
}

static void
wanted_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,wanted_journal]", NULL, 0);
    wanted_finish(srv);
    wanted_pass(step);
}

static void
selftest_quest_wanted(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_tiffy;
    int npc_amik;
    int npc_daq;
    int npc_mage;
    int npc_bk;
    int npc_solus;
    int obj_commorb;
    int obj_trophy;
    int obj_coins;
    int obj_law;
    int obj_gem;
    int obj_glass;
    int obj_essence;
    int obj_sword;
    int stat_slayer;
    int slot;
    int slayer_xp_before;
    int qp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: wanted critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer wanted selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    wanted_god(player);

    npc_tiffy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_teleporter_guy");
    npc_amik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sir_amik_varze");
    npc_daq = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lord_daquarius");
    npc_mage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1_edgeb");
    npc_bk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "black_knight");
    npc_solus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wanted_solus_attackable");
    obj_commorb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "wanted_crystal_ball");
    obj_trophy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "wanted_solus_trophy");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_law = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lawrune");
    obj_gem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slayer_gem");
    obj_glass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "molten_glass");
    obj_essence = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blankrune");
    obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    stat_slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "wanted_main") >= 0,
                   "varbit wanted_main should resolve");
    SELFTEST_CHECK(npc_tiffy > 0, "npc rd_teleporter_guy should resolve");
    SELFTEST_CHECK(npc_amik > 0, "npc sir_amik_varze should resolve");
    SELFTEST_CHECK(npc_daq > 0, "npc lord_daquarius should resolve");
    SELFTEST_CHECK(obj_commorb > 0, "obj wanted_crystal_ball should resolve");
    SELFTEST_CHECK(obj_trophy > 0, "obj wanted_solus_trophy should resolve");
    if( npc_tiffy <= 0 || npc_amik <= 0 || obj_commorb <= 0 )
    {
        fprintf(stderr, "ToriRSServer wanted selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    wanted_clear_inv(player);
    wanted_reset_quest(srv);
    wanted_god(player);

    wanted_journal(srv, "journal_not_started");

    /* ---- Tiffy refuse-reqs: RD complete, other gates missing ---- */
    slot = wanted_spawn(srv, npc_tiffy, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Tiffy should spawn");
    if( slot >= 0 )
    {
        wanted_set_bit(srv, "rd_main", WANTED_RD_COMPLETE);
        wanted_set_varp(player, "qp", 0);
        wanted_set_varp(player, "abyssal_miniquest", 0);
        wanted_set_varp(player, "priestperil", 0);
        wanted_set_bit(srv, "lost_tribe_quest", 0);
        wanted_talk_finish(srv, npc_tiffy, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_NOT_STARTED,
                       "missing real gates must not start Wanted!");
        wanted_pass("opnpc1_tiffy_refuse_reqs");

        wanted_set_prereqs(srv, player);
        wanted_talk_pick(srv, npc_tiffy, slot, 2);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_NOT_STARTED,
                       "refusing Tiffy's offer must not start Wanted!");
        wanted_pass("opnpc1_tiffy_choice_refuse");

        wanted_talk_pick2(srv, npc_tiffy, slot, 1, 1);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_AMIK_FIRST,
                       "accepting Tiffy + Ask about Wanted! must write amik_first, got %d",
                       wanted_get_bit(player, "wanted_main"));
        wanted_pass("opnpc1_tiffy_choice_accept");

        wanted_talk_finish(srv, npc_tiffy, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_AMIK_FIRST,
                       "mid Tiffy at amik_first must stay on Amik");
        wanted_pass("opnpc1_tiffy_mid_amik_first");
    }

    wanted_journal(srv, "journal_amik_first");

    /* ---- Amik first bounce: refuse-Squire (authored path) + joke path ---- */
    wanted_free_npc(srv, slot);
    slot = wanted_spawn(srv, npc_amik, WANTED_AMIK_X, WANTED_AMIK_Z, 1);
    SELFTEST_CHECK(slot >= 0, "Amik should spawn");
    if( slot >= 0 )
    {
        wanted_set_prereqs(srv, player);
        wanted_set_bit(srv, "wanted_main", WANTED_AMIK_FIRST);
        wanted_set_bit(srv, "wanted_joke_option", 0);
        wanted_talk_pick(srv, npc_amik, slot, 2);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_TIFFY_SECOND,
                       "accept-Squire joke path must bounce to tiffy_second, got %d",
                       wanted_get_bit(player, "wanted_main"));
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_joke_option") == 1,
                       "accept-Squire must set wanted_joke_option");
        wanted_pass("opnpc1_amik_accept_squire");

        wanted_set_bit(srv, "wanted_main", WANTED_AMIK_FIRST);
        wanted_set_bit(srv, "wanted_joke_option", 0);
        wanted_talk_pick(srv, npc_amik, slot, 1);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_TIFFY_SECOND,
                       "refuse-Squire must bounce to tiffy_second, got %d",
                       wanted_get_bit(player, "wanted_main"));
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_joke_option") == 0,
                       "refuse-Squire must not set the joke bit");
        wanted_pass("opnpc1_amik_refuse_squire");
    }

    wanted_journal(srv, "journal_tiffy_second");

    /* ---- Tiffy second / Amik second / Tiffy third ---- */
    wanted_free_npc(srv, slot);
    slot = wanted_spawn(srv, npc_tiffy, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    if( slot >= 0 )
    {
        wanted_set_prereqs(srv, player);
        wanted_set_bit(srv, "wanted_main", WANTED_TIFFY_SECOND);
        wanted_set_bit(srv, "wanted_joke_option", 0);
        wanted_talk_pick(srv, npc_tiffy, slot, 1);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_AMIK_SECOND,
                       "Tiffy second Ask about Wanted! must write amik_second, got %d",
                       wanted_get_bit(player, "wanted_main"));
        wanted_pass("opnpc1_tiffy_second_ask");

        wanted_talk_finish(srv, npc_tiffy, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_AMIK_SECOND,
                       "mid Tiffy at amik_second must stay on Amik");
        wanted_pass("opnpc1_tiffy_mid_amik_second");
    }

    wanted_journal(srv, "journal_amik_second");

    wanted_free_npc(srv, slot);
    slot = wanted_spawn(srv, npc_amik, WANTED_AMIK_X, WANTED_AMIK_Z, 1);
    if( slot >= 0 )
    {
        wanted_set_prereqs(srv, player);
        wanted_set_bit(srv, "wanted_main", WANTED_AMIK_SECOND);
        wanted_talk_finish(srv, npc_amik, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_TIFFY_THIRD,
                       "Amik second must bounce to tiffy_third, got %d",
                       wanted_get_bit(player, "wanted_main"));
        wanted_pass("opnpc1_amik_second");
    }

    wanted_journal(srv, "journal_tiffy_third");

    /* ---- Commorb obtain: empty-inv fail, pay path, craft path ---- */
    wanted_free_npc(srv, slot);
    slot = wanted_spawn(srv, npc_tiffy, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    if( slot >= 0 )
    {
        wanted_set_prereqs(srv, player);
        wanted_set_bit(srv, "wanted_main", WANTED_TIFFY_THIRD);
        wanted_talk_pick(srv, npc_tiffy, slot, 1);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_GET_COMMORB,
                       "Tiffy third Ask about Wanted! must write get_commorb, got %d",
                       wanted_get_bit(player, "wanted_main"));
        wanted_pass("opnpc1_tiffy_commorb_offer");

        wanted_journal(srv, "journal_get_commorb");

        wanted_clear_inv(player);
        if( obj_sword > 0 )
            wanted_fill_inv(player, obj_sword);
        if( obj_coins > 0 )
            inv_set(player, 0, obj_coins, WANTED_COMMORB_GP);
        wanted_talk_pick2(srv, npc_tiffy, slot, 1, 1);
        SELFTEST_CHECK(wanted_inv_total(player, obj_commorb) == 0,
                       "full inventory must not receive a paid Commorb");
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_GET_COMMORB,
                       "empty-inv pay fail must stay on get_commorb");
        wanted_pass("opnpc1_tiffy_commorb_empty_inv");

        wanted_clear_inv(player);
        wanted_talk_pick2(srv, npc_tiffy, slot, 1, 1);
        SELFTEST_CHECK(wanted_inv_total(player, obj_commorb) == 0,
                       "no coins must not receive a paid Commorb");
        wanted_pass("opnpc1_tiffy_commorb_no_coins");

        wanted_clear_inv(player);
        if( obj_law > 0 )
            wanted_give(player, obj_law, 1);
        wanted_talk_pick2(srv, npc_tiffy, slot, 2, 1);
        SELFTEST_CHECK(wanted_inv_total(player, obj_commorb) == 0,
                       "missing craft parts must not receive a Commorb");
        wanted_pass("opnpc1_tiffy_commorb_craft_missing");

        wanted_clear_inv(player);
        if( obj_coins > 0 )
            wanted_give(player, obj_coins, WANTED_COMMORB_GP);
        wanted_talk_pick2(srv, npc_tiffy, slot, 1, 1);
        SELFTEST_CHECK(wanted_inv_total(player, obj_commorb) == 1,
                       "paying 10000 coins must hand a Commorb");
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_INVESTIGATION,
                       "paid Commorb must write investigation, got %d",
                       wanted_get_bit(player, "wanted_main"));
        wanted_pass("opnpc1_tiffy_commorb_pay_ok");

        wanted_clear_inv(player);
        wanted_set_bit(srv, "wanted_main", WANTED_GET_COMMORB);
        if( obj_law > 0 )
            wanted_give(player, obj_law, 1);
        if( obj_gem > 0 )
            wanted_give(player, obj_gem, 1);
        if( obj_glass > 0 )
            wanted_give(player, obj_glass, 1);
        wanted_talk_pick2(srv, npc_tiffy, slot, 2, 1);
        SELFTEST_CHECK(wanted_inv_total(player, obj_commorb) == 1,
                       "crafted Commorb must be added");
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_INVESTIGATION,
                       "crafted Commorb must write investigation, got %d",
                       wanted_get_bit(player, "wanted_main"));
        wanted_pass("opnpc1_tiffy_commorb_craft_ok");
    }

    wanted_journal(srv, "journal_investigation_contact");

    /* ---- Savant contact (opheld2) ---- */
    wanted_clear_inv(player);
    wanted_give(player, obj_commorb, 1);
    wanted_set_bit(srv, "wanted_main", WANTED_INVESTIGATION);
    wanted_set_bit(srv, "wanted_commorb_intel", 0);
    wanted_opheld_pick(srv, SS_TRIGGER_OPHELD2, obj_commorb, 1);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_commorb_intel") == 1,
                   "opheld2 Current Assignment must set commorb intel");
    wanted_pass("opheld2_savant_assignment");

    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_INVESTIGATION,
                   "scan before hunt must stay static");
    wanted_pass("opheld1_scan_not_hunt");

    wanted_journal(srv, "journal_investigation_daq");

    /* ---- Daquarius too-early / talk / proof / exposition ---- */
    wanted_free_npc(srv, slot);
    slot = wanted_spawn(srv, npc_daq, WANTED_DAQ_X, WANTED_DAQ_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Daquarius should spawn");
    if( slot >= 0 )
    {
        wanted_set_prereqs(srv, player);
        wanted_set_bit(srv, "wanted_main", WANTED_INVESTIGATION);
        wanted_set_bit(srv, "wanted_commorb_intel", 0);
        wanted_talk_finish(srv, npc_daq, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_daquarius_hint") == WANTED_DAQ_NONE,
                       "Daquarius too-early must not start the proof");
        wanted_pass("opnpc1_daq_too_early");

        wanted_set_bit(srv, "wanted_commorb_intel", 1);
        wanted_talk_pick(srv, npc_daq, slot, 2);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_daquarius_hint") == WANTED_DAQ_NONE,
                       "refusing Daquarius must not set the hint");
        wanted_pass("opnpc1_daq_threat_refuse");

        wanted_talk_pick(srv, npc_daq, slot, 1);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_daquarius_hint") == WANTED_DAQ_TALKED,
                       "threatening Daquarius must demand a Black Knight");
        wanted_pass("opnpc1_daq_talk");

        wanted_talk_finish(srv, npc_daq, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_daquarius_hint") == WANTED_DAQ_TALKED,
                       "mid Daquarius must wait for the kill");
        wanted_pass("opnpc1_daq_waiting");
    }

    wanted_journal(srv, "journal_investigation_kill_bk");

    if( npc_bk > 0 )
    {
        int bk_slot = wanted_spawn(srv, npc_bk, WANTED_DAQ_X, WANTED_DAQ_Z + 2, 0);

        SELFTEST_CHECK(bk_slot >= 0, "Black Knight should spawn for the proof kill");
        if( bk_slot >= 0 )
        {
            ToriRSServer_ScriptsRunProcOnNpc(srv, "[ai_queue3,black_knight]", bk_slot);
            wanted_finish(srv);
            SELFTEST_CHECK(wanted_get_bit(player, "wanted_daquarius_hint") == WANTED_DAQ_BK_DEAD,
                           "real black_knight death must write daq proof, got %d",
                           wanted_get_bit(player, "wanted_daquarius_hint"));
            wanted_pass("ai_queue3_black_knight_proof");
            wanted_free_npc(srv, bk_slot);
        }
    }

    wanted_journal(srv, "journal_investigation_daq_again");

    if( slot >= 0 )
    {
        wanted_tele(srv, WANTED_DAQ_X, WANTED_DAQ_Z, 0);
        wanted_talk_finish(srv, npc_daq, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_lord_d_exposition") == 1,
                       "Daquarius after the kill must give the mage pointer");
        wanted_pass("opnpc1_daq_exposition");
        wanted_talk_finish(srv, npc_daq, slot);
        wanted_pass("opnpc1_daq_after");
    }

    wanted_journal(srv, "journal_investigation_mage");

    /* ---- Mage too-early / refuse / agree / essence fail+hand-in ---- */
    wanted_free_npc(srv, slot);
    slot = wanted_spawn(srv, npc_mage, WANTED_MAGE_X, WANTED_MAGE_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Mage of Zamorak should spawn");
    if( slot >= 0 )
    {
        wanted_set_prereqs(srv, player);
        wanted_set_bit(srv, "wanted_main", WANTED_INVESTIGATION);
        wanted_set_bit(srv, "wanted_lord_d_exposition", 0);
        wanted_set_bit(srv, "wanted_zammy_mage_hint", 0);
        wanted_talk_finish(srv, npc_mage, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_zammy_mage_hint") == 0,
                       "Mage too-early must not start the essence deal");
        wanted_pass("opnpc1_mage_too_early");

        wanted_set_bit(srv, "wanted_lord_d_exposition", 1);
        wanted_talk_pick(srv, npc_mage, slot, 2);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_zammy_mage_hint") == 0,
                       "refusing the Mage must not set the hint");
        wanted_pass("opnpc1_mage_refuse");

        wanted_talk_pick(srv, npc_mage, slot, 1);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_zammy_mage_hint") == 1,
                       "agreeing with the Mage must set the essence price");
        wanted_pass("opnpc1_mage_agree");

        wanted_clear_inv(player);
        wanted_talk_finish(srv, npc_mage, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_INVESTIGATION,
                       "essence fail must stay on investigation");
        wanted_pass("opnpc1_mage_essence_fail");

        wanted_journal(srv, "journal_investigation_essence");

        if( obj_essence > 0 )
            wanted_give(player, obj_essence, WANTED_ESSENCE_NEEDED);
        wanted_talk_finish(srv, npc_mage, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_HUNT,
                       "essence hand-in must write hunt, got %d",
                       wanted_get_bit(player, "wanted_main"));
        wanted_pass("opnpc1_mage_essence_handin");
    }

    wanted_journal(srv, "journal_hunt");

    /* ---- Hunt scans: wrong-zone + in-zone including leftover narrations ---- */
    wanted_clear_inv(player);
    wanted_give(player, obj_commorb, 1);
    wanted_set_bit(srv, "wanted_main", WANTED_HUNT);

    wanted_tele(srv, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission1complete") == 0,
                   "scan outside Canifis must not complete mission1");
    wanted_pass("opheld1_scan_canifis_wrong");

    wanted_tele(srv, WANTED_CANIFIS_X, WANTED_CANIFIS_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission1complete") == 1,
                   "scan in Canifis must complete mission1");
    wanted_pass("opheld1_scan_canifis_in");

    wanted_opheld(srv, SS_TRIGGER_OPHELD2, obj_commorb);
    wanted_pass("opheld2_savant_hunt_hint");

    wanted_tele(srv, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission15complete") == 0,
                   "scan outside Rellekka must not complete mission15");
    wanted_pass("opheld1_scan_rellekka_wrong");

    wanted_tele(srv, WANTED_RELLEKKA_X, WANTED_RELLEKKA_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission15complete") == 1,
                   "scan in Rellekka must complete mission15");
    wanted_pass("opheld1_scan_rellekka_in");

    wanted_tele(srv, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission5complete") == 0,
                   "scan outside Musa Point must not complete mission5");
    wanted_pass("opheld1_scan_musa_wrong");

    wanted_tele(srv, WANTED_MUSA_X, WANTED_MUSA_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission5complete") == 1,
                   "scan in Musa Point must complete mission5");
    wanted_pass("opheld1_scan_musa_in");

    wanted_tele(srv, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission12complete") == 0,
                   "scan outside Wizards' Tower must not complete mission12");
    wanted_pass("opheld1_scan_wizards_wrong");

    wanted_tele(srv, WANTED_WIZARDS_X, WANTED_WIZARDS_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission12complete") == 1,
                   "scan in Wizards' Tower must complete mission12");
    wanted_pass("opheld1_scan_wizards_in");

    wanted_tele(srv, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission3complete") == 0,
                   "scan outside Dorgesh-Kaan must not complete mission3");
    wanted_pass("opheld1_scan_dorgesh_wrong");

    wanted_tele(srv, WANTED_DORGESH_X, WANTED_DORGESH_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission3complete") == 1,
                   "scan in Dorgesh-Kaan (Flames of Zamorak leftover) must complete mission3");
    wanted_pass("opheld1_scan_dorgesh_flames");

    wanted_tele(srv, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission8complete") == 0,
                   "scan outside Ardougne Market must not complete mission8");
    wanted_pass("opheld1_scan_ardougne_wrong");

    wanted_tele(srv, WANTED_ARDOUGNE_X, WANTED_ARDOUGNE_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission8complete") == 1,
                   "scan in Ardougne Market must complete mission8");
    wanted_pass("opheld1_scan_ardougne_in");

    wanted_tele(srv, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission2complete") == 0,
                   "scan outside Champions' Guild must not complete mission2");
    wanted_pass("opheld1_scan_champions_wrong");

    wanted_tele(srv, WANTED_CHAMPIONS_X, WANTED_CHAMPIONS_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission2complete") == 1,
                   "scan in Champions' Guild (decoy leftover) must complete mission2");
    wanted_pass("opheld1_scan_champions_decoy");

    wanted_tele(srv, WANTED_TIFFY_X, WANTED_TIFFY_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission4complete") == 0,
                   "scan outside the essence mine must not find Solus");
    wanted_pass("opheld1_scan_essence_wrong");

    wanted_tele(srv, WANTED_ESSENCE_X, WANTED_ESSENCE_Z, 0);
    wanted_opheld(srv, SS_TRIGGER_OPHELD1, obj_commorb);
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_HUNT,
                   "finding Solus must leave the hunt open for the fight");
    wanted_pass("opheld1_solus_found");

    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission6") == 0,
                   "unused wanted_mission6 must stay unused");
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission7") == 0,
                   "unused wanted_mission7 must stay unused");
    SELFTEST_CHECK(wanted_get_bit(player, "wanted_mission9") == 0,
                   "unused wanted_mission9 must stay unused");
    wanted_pass("leftover_unused_mission_bits");

    /* ---- Solus fight / trophy / Amik hand-in / complete ---- */
    if( npc_solus > 0 )
    {
        int solus_slot = selftest_find_npc(srv, npc_solus);

        if( solus_slot < 0 )
            solus_slot = wanted_spawn(srv, npc_solus, WANTED_ESSENCE_X, WANTED_ESSENCE_Z, 0);
        SELFTEST_CHECK(solus_slot >= 0, "Solus should be present for the fight");
        if( solus_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_solus, -1, solus_slot);
            wanted_finish(srv);
            wanted_pass("opnpc2_solus_attack");
            ToriRSServer_ScriptsRunProcOnNpc(
                srv, "[ai_queue3,wanted_solus_attackable]", solus_slot);
            wanted_finish(srv);
            SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_FINAL_BATTLE,
                           "Solus death must write final_battle, got %d",
                           wanted_get_bit(player, "wanted_main"));
            wanted_pass("ai_queue3_solus_falls");
            wanted_free_npc(srv, solus_slot);
        }
    }

    wanted_journal(srv, "journal_final_contact");

    wanted_clear_inv(player);
    wanted_give(player, obj_commorb, 1);
    if( obj_sword > 0 )
        wanted_fill_inv(player, obj_sword);
    inv_set(player, 0, obj_commorb, 1);
    wanted_opheld_pick(srv, SS_TRIGGER_OPHELD2, obj_commorb, 1);
    SELFTEST_CHECK(wanted_inv_total(player, obj_trophy) == 0,
                   "full inventory must not receive Solus's hat");
    wanted_pass("opheld2_savant_trophy_empty_inv");

    wanted_clear_inv(player);
    wanted_give(player, obj_commorb, 1);
    wanted_opheld_pick(srv, SS_TRIGGER_OPHELD2, obj_commorb, 1);
    SELFTEST_CHECK(wanted_inv_total(player, obj_trophy) == 1,
                   "Savant must hand Solus's hat as proof");
    wanted_pass("opheld2_savant_trophy");

    wanted_journal(srv, "journal_final_handin");

    wanted_free_npc(srv, slot);
    slot = wanted_spawn(srv, npc_amik, WANTED_AMIK_X, WANTED_AMIK_Z, 1);
    if( slot >= 0 )
    {
        wanted_set_prereqs(srv, player);
        wanted_set_bit(srv, "wanted_main", WANTED_FINAL_BATTLE);
        wanted_clear_inv(player);
        wanted_talk_finish(srv, npc_amik, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_FINAL_BATTLE,
                       "Amik without the trophy must not complete");
        wanted_pass("opnpc1_amik_final_no_trophy");

        wanted_give(player, obj_trophy, 1);
        slayer_xp_before = 0;
        if( stat_slayer >= 0 )
            slayer_xp_before = player->stat_xp_tenths[stat_slayer];
        qp_before = 0;
        {
            int varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

            if( varp_qp >= 0 )
                qp_before = player->varps[varp_qp];
        }
        wanted_talk_finish(srv, npc_amik, slot);
        SELFTEST_CHECK(wanted_get_bit(player, "wanted_main") == WANTED_COMPLETE,
                       "Amik hat hand-in must complete Wanted!, got %d",
                       wanted_get_bit(player, "wanted_main"));
        if( stat_slayer >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_slayer] >=
                               slayer_xp_before + WANTED_REWARD_SLAYER,
                           "complete must advance slayer by 50000 tenths (5000 XP)");
        (void)qp_before;
        wanted_pass("opnpc1_amik_handin_complete");

        wanted_talk_finish(srv, npc_amik, slot);
        wanted_pass("opnpc1_amik_post_complete");
    }

    wanted_journal(srv, "journal_complete");
    wanted_free_npc(srv, slot);

    fprintf(stderr, "ToriRSServer wanted selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_WANTED_SELFTEST_U_H */
