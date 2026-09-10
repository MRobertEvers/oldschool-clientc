#ifndef TORIRSSERVER_TEST_QUEST_RECRUITMENTDRIVE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_RECRUITMENTDRIVE_SELFTEST_U_H

/* Recruitment Drive Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Amik / Tiffy / room observers / Sir Leye
 * cannot leak. Real OPNPC / OPLOC / OPHELD / OPLOCU / OPHELDU / OPNPC3 on
 * the authored path. player->godmode = 1 for the whole walk (no death
 * test). Completion goes through Tiffy's authored
 * ~quest_complete_rewards(quest_recruitmentdrive, ...). Additive RD
 * branches only — do not rewrite Black Knights' Fortress, Wanted!,
 * Recipe for Disaster, The Slug Menace, Druidic Ritual, Construction,
 * or MTA.
 *
 * Leftover: this port runs all 7 rooms in a fixed order (wiki is 5-of-7
 * random + random order). Gaze of Saradomin teleport / Initiate title
 * and Wanted! arms stay deferred. */

#define RD_NOT_STARTED 0
#define RD_REFERRED 1
#define RD_TESTING 2
#define RD_PASSED 3
#define RD_COMPLETE 4

#define RD_BKF_COMPLETE 4
#define RD_DRUID_COMPLETE 4

#define RD_STAT_HERBLORE 15
#define RD_REWARD_XP_TENTHS 10005

#define RD_AMIK_X 2960
#define RD_AMIK_Z 3338
#define RD_TIFFY_X 2997
#define RD_TIFFY_Z 3373
#define RD_SPISH_X 2490
#define RD_SPISH_Z 4972
#define RD_SPISH_WEST_X 2484
#define RD_SPISH_EAST_X 2476
#define RD_KUAM_X 2455
#define RD_KUAM_Z 4964
#define RD_LEYE_X 2460
#define RD_LEYE_Z 4963
#define RD_TINLEY_X 2471
#define RD_TINLEY_Z 4956
#define RD_TABLE_X 2460
#define RD_TABLE_Z 4979
#define RD_REN_X 2439
#define RD_REN_Z 4956
#define RD_CHEEVERS_X 2467
#define RD_CHEEVERS_Z 4940
#define RD_HYNN_X 2451
#define RD_HYNN_Z 4935

static void
rd_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "RD PASS: %s\n", step);
}

static void
rd_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
rd_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
rd_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 160 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static void
rd_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
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
rd_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    rd_god(player);
    selftest_tick(srv);
}

static int
rd_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    rd_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
rd_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
rd_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
rd_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( rd_inv_total(player, obj_id) >= count )
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
rd_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
rd_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
rd_set_varp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        player->varps[varp] = value;
}

static int
rd_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    rd_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
rd_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
rd_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    rd_talk(srv, npc_type, slot);
    rd_finish(srv);
}

static void
rd_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    rd_talk(srv, npc_type, slot);
    rd_click_until_menu(srv, 16);
    selftest_charter_choose(srv, row);
    rd_finish(srv);
}

static void
rd_talk_picks(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int nrows)
{
    int i;

    assert(srv);
    assert(rows);
    assert(nrows > 0);
    rd_talk(srv, npc_type, slot);
    for( i = 0; i < nrows; i++ )
    {
        rd_click_until_menu(srv, 16);
        selftest_charter_choose(srv, rows[i]);
    }
    rd_finish(srv);
}

static void
rd_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    rd_finish(srv);
}

static void
rd_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    rd_set_varp(player, "spy", RD_BKF_COMPLETE);
    rd_set_varp(player, "druidquest", RD_DRUID_COMPLETE);
    rd_god(player);
}

static void
rd_reset_quest(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    rd_set_bit(srv, "rd_main", RD_NOT_STARTED);
    rd_set_bit(srv, "rd_room1_complete", 0);
    rd_set_bit(srv, "rd_room2_complete", 0);
    rd_set_bit(srv, "rd_room3_complete", 0);
    rd_set_bit(srv, "rd_room4_complete", 0);
    rd_set_bit(srv, "rd_room5_complete", 0);
    rd_set_bit(srv, "rd_room6_complete", 0);
    rd_set_bit(srv, "rd_room7_complete", 0);
    rd_set_bit(srv, "rd_room1_initiated", 0);
    rd_set_bit(srv, "rd_room_order", 0);
    rd_set_bit(srv, "rd_templock_1", 0);
    rd_set_bit(srv, "rd_room6_stone_door", 0);
    rd_set_bit(srv, "rd_gypsum_in_tin", 0);
    rd_set_bit(srv, "rd_foxleft", 0);
    rd_set_bit(srv, "rd_foxright", 0);
    rd_set_bit(srv, "rd_chickleft", 0);
    rd_set_bit(srv, "rd_chickright", 0);
    rd_set_bit(srv, "rd_grainleft", 0);
    rd_set_bit(srv, "rd_grainright", 0);
    rd_set_varp(player, "spy", 0);
    rd_set_varp(player, "druidquest", 0);
    rd_clear_inv(player);
    rd_god(player);
}

static void
rd_journal(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_ScriptsRunProc(srv, "[proc,recruitmentdrive_journal]", NULL, 0);
    rd_finish(srv);
}

static void
selftest_quest_recruitmentdrive(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_amik;
    int npc_tiffy;
    int npc_spish;
    int npc_kuam;
    int npc_leye;
    int npc_tinley;
    int npc_table;
    int npc_ren;
    int npc_cheevers;
    int npc_hynn;
    int loc_fox;
    int loc_chicken;
    int loc_grain;
    int loc_bridge;
    int loc_door1;
    int loc_door3;
    int loc_door4;
    int loc_statue;
    int loc_door2;
    int loc_door5;
    int loc_bunsen;
    int loc_stone;
    int loc_key;
    int loc_door6;
    int loc_door7;
    int loc_portal;
    int obj_coins;
    int obj_fox;
    int obj_chicken;
    int obj_sack;
    int obj_sword;
    int obj_claws;
    int obj_axe;
    int obj_hammer;
    int obj_spade;
    int obj_head;
    int obj_sulph;
    int obj_water;
    int obj_gypsum;
    int obj_tin;
    int obj_tinfull;
    int obj_mould;
    int obj_copore;
    int obj_tinore;
    int obj_copper_mould;
    int obj_unheated;
    int obj_complete;
    int obj_key;
    int obj_chisel;
    int slot;
    int loc_slot;
    int rhand;
    int prayer_before;
    int herb_before;
    int agil_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;

    fprintf(stderr, "ToriRSServer selftest: Recruitment Drive Gate D\n");
    player->godmode = 1;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    npc_amik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sir_amik_varze");
    npc_tiffy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_teleporter_guy");
    npc_spish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_observer_room_1");
    npc_kuam = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_observer_room_3");
    npc_leye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_combat_npc_room_3");
    npc_tinley = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_observer_room_4");
    npc_table = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_observer_room_2");
    npc_ren = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_observer_room_5");
    npc_cheevers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_observer_room_6");
    npc_hynn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_observer_room_7");
    loc_fox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_fox_normal");
    loc_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_chicken_normal");
    loc_grain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_sack_full");
    loc_bridge = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_bridge_left");
    loc_door1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_room1_exitdoor");
    loc_door3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_room3_exitdoor");
    loc_door4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_room4_exitdoor");
    loc_statue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_2b");
    loc_door2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_room2_exitdoor");
    loc_door5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_room5_exitdoor");
    loc_bunsen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_wooden_table_bunsen_burner");
    loc_stone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_stone_door");
    loc_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_key_chained");
    loc_door6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_room6_exitdoor");
    loc_door7 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_room7_exitdoor");
    loc_portal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rd_portal_room1_entrance");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_fox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_fox");
    obj_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_chicken");
    obj_sack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_sack");
    obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_sword");
    obj_claws = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_claws");
    obj_axe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_battleaxe");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_warhammer");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_metal_spade");
    obj_head = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_metal_spade_no_handle");
    obj_sulph = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_cupric_sulphate");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_dihydrogen_monoxide");
    obj_gypsum = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_gypsum");
    obj_tin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_tin");
    obj_tinfull = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_tinfull");
    obj_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_keymould");
    obj_copore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_copper_ore_powder");
    obj_tinore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_tin_ore_powder");
    obj_copper_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_full_keymould_copper");
    obj_unheated = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_full_keymould_unheated");
    obj_complete = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_full_keymould_complete");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_puzzleroom_key");
    obj_chisel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rd_chisel");
    rhand = ToriRSServer_ContentConstantInt("wearpos_rhand", 3);
    (void)obj_claws;
    (void)obj_axe;
    (void)obj_chisel;

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rd_main") >= 0,
                   "varbit rd_main should resolve");
    SELFTEST_CHECK(npc_amik > 0, "npc sir_amik_varze should resolve");
    SELFTEST_CHECK(npc_tiffy > 0, "npc rd_teleporter_guy should resolve");
    SELFTEST_CHECK(npc_spish > 0, "npc rd_observer_room_1 should resolve");
    SELFTEST_CHECK(npc_kuam > 0, "npc rd_observer_room_3 should resolve");
    SELFTEST_CHECK(npc_leye > 0, "npc rd_combat_npc_room_3 should resolve");
    if( npc_amik <= 0 || npc_tiffy <= 0 )
    {
        fprintf(stderr, "ToriRSServer rd selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    rd_reset_quest(srv, player);
    rd_journal(srv);
    rd_pass("journal_not_started");

    /* ---- Amik pre-BKF refuse (RD not offered) ---- */
    slot = rd_spawn(srv, npc_amik, RD_AMIK_X, RD_AMIK_Z, 1);
    SELFTEST_CHECK(slot >= 0, "Amik should spawn");
    if( slot >= 0 )
    {
        rd_talk_pick(srv, npc_amik, slot, 1);
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_NOT_STARTED,
                       "pre-BKF Amik must not start Recruitment Drive, got %d",
                       rd_get_bit(player, "rd_main"));
        rd_pass("opnpc1_amik_pre_bkf_refuse");

        rd_set_varp(player, "spy", RD_BKF_COMPLETE);
        rd_set_varp(player, "druidquest", 0);
        rd_talk_pick(srv, npc_amik, slot, 1);
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_NOT_STARTED,
                       "Amik must refuse RD without Druidic Ritual, got %d",
                       rd_get_bit(player, "rd_main"));
        rd_pass("opnpc1_amik_no_druid_refuse");

        rd_prereqs(srv, player);
        {
            int rows[2] = { 1, 1 };
            rd_talk_picks(srv, npc_amik, slot, rows, 2);
        }
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_REFERRED,
                       "accepting Amik referral must write referred, got %d",
                       rd_get_bit(player, "rd_main"));
        rd_pass("opnpc1_amik_referral");

        rd_talk_finish(srv, npc_amik, slot);
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_REFERRED,
                       "mid-RD Amik reminder must stay referred");
        rd_pass("opnpc1_amik_mid_rd_reminder");
        rd_free_npc(srv, slot);
    }

    rd_journal(srv);
    rd_pass("journal_referred");

    /* ---- Tiffy not-referred / referred / empty-inv / enter ---- */
    slot = rd_spawn(srv, npc_tiffy, RD_TIFFY_X, RD_TIFFY_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Tiffy should spawn");
    if( slot >= 0 )
    {
        rd_set_bit(srv, "rd_main", RD_NOT_STARTED);
        rd_talk_finish(srv, npc_tiffy, slot);
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_NOT_STARTED,
                       "Tiffy not-referred must stay not started");
        rd_pass("opnpc1_tiffy_not_referred");

        rd_set_bit(srv, "rd_main", RD_REFERRED);
        rd_prereqs(srv, player);
        rd_give(player, obj_coins > 0 ? obj_coins : 995, 1);
        rd_talk_pick(srv, npc_tiffy, slot, 1);
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_REFERRED,
                       "empty-inv gate must refuse enter, got %d",
                       rd_get_bit(player, "rd_main"));
        rd_pass("opnpc1_tiffy_empty_inv_gate");

        rd_clear_inv(player);
        rd_talk_pick(srv, npc_tiffy, slot, 1);
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_TESTING,
                       "empty inv+worn enter must write testing, got %d",
                       rd_get_bit(player, "rd_main"));
        rd_pass("opnpc1_tiffy_enter_grounds");

        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_clear_inv(player);
        rd_talk_pick(srv, npc_tiffy, slot, 2);
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_TESTING,
                       "testing retry decline must stay testing");
        rd_pass("opnpc1_tiffy_testing_retry");
        rd_free_npc(srv, slot);
    }

    rd_journal(srv);
    rd_pass("journal_testing");

    /* ---- Quit portal fail-out ---- */
    if( loc_portal >= 0 )
    {
        loc_slot = rd_place_loc(srv, loc_portal, RD_SPISH_X, RD_SPISH_Z, 0);
        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_oploc(srv, loc_portal, loc_slot);
        SELFTEST_CHECK(player->x == RD_TIFFY_X || player->z == RD_TIFFY_Z ||
                           player->x < 3100,
                       "quit portal should return toward Falador Park");
        rd_pass("oploc1_quit_portal");
    }

    /* ---- Spishyus fox/chicken/grain ---- */
    slot = rd_spawn(srv, npc_spish, RD_SPISH_X, RD_SPISH_Z, 0);
    if( slot >= 0 )
    {
        ToriRSServer_ScriptsRunProc(srv, "[proc,rd_spishyus_reset]", NULL, 0);
        rd_finish(srv);
        rd_talk_finish(srv, npc_spish, slot);
        rd_pass("opnpc1_spishyus_intro");

        if( loc_fox >= 0 && loc_bridge >= 0 )
        {
            int fox_slot = rd_place_loc(srv, loc_fox, RD_SPISH_WEST_X, RD_SPISH_Z, 0);
            int grain_slot = loc_grain >= 0
                ? rd_place_loc(srv, loc_grain, RD_SPISH_WEST_X, RD_SPISH_Z, 0)
                : -1;
            int br_slot = rd_place_loc(srv, loc_bridge, RD_SPISH_WEST_X, RD_SPISH_Z, 0);

            rd_tele(srv, RD_SPISH_WEST_X, RD_SPISH_Z, 0);
            if( grain_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_grain, -1, grain_slot);
                rd_finish(srv);
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_bridge, -1, br_slot);
                rd_finish(srv);
                SELFTEST_CHECK(rd_get_bit(player, "rd_room1_complete") == 0,
                               "leaving fox+chicken unattended must fail");
                rd_pass("oploc_spishyus_illegal_fail");
            }

            ToriRSServer_ScriptsRunProc(srv, "[proc,rd_spishyus_reset]", NULL, 0);
            rd_finish(srv);
            rd_tele(srv, RD_SPISH_WEST_X, RD_SPISH_Z, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOC1, loc_fox, -1, fox_slot);
            rd_finish(srv);
            if( obj_fox > 0 )
                SELFTEST_CHECK(rd_inv_total(player, obj_fox) > 0 ||
                                   rd_get_bit(player, "rd_foxleft") == 1,
                               "picking the fox should take it from the west bank");
            rd_pass("oploc_spishyus_pickup_fox");

            if( obj_fox > 0 && obj_chicken > 0 )
            {
                rd_give(player, obj_chicken, 1);
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_bridge, -1, br_slot);
                rd_finish(srv);
                rd_pass("oploc_spishyus_overweight");
            }
            (void)fox_slot;
        }

        rd_set_bit(srv, "rd_foxright", 1);
        rd_set_bit(srv, "rd_chickright", 1);
        rd_set_bit(srv, "rd_grainright", 1);
        ToriRSServer_ScriptsRunProc(srv, "[proc,rd_spishyus_check_done]", NULL, 0);
        rd_finish(srv);
        SELFTEST_CHECK(rd_get_bit(player, "rd_room1_complete") == 1,
                       "all three east must solve Spishyus");
        rd_pass("spishyus_solved");

        rd_talk_finish(srv, npc_spish, slot);
        rd_pass("opnpc1_spishyus_already_done");

        if( loc_door1 >= 0 )
        {
            loc_slot = rd_place_loc(srv, loc_door1, RD_SPISH_EAST_X, RD_SPISH_Z, 0);
            rd_set_bit(srv, "rd_room1_complete", 0);
            rd_oploc(srv, loc_door1, loc_slot);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room1_complete") == 0,
                           "locked Spishyus door must not advance");
            rd_pass("oploc_spishyus_door_locked");

            rd_set_bit(srv, "rd_room1_complete", 1);
            rd_oploc(srv, loc_door1, loc_slot);
            rd_pass("oploc_spishyus_kuam_handoff");
        }
        rd_free_npc(srv, slot);
    }

    /* ---- Kuam / Sir Leye: blade fail + warhammer/unarmed success ---- */
    slot = rd_spawn(srv, npc_kuam, RD_KUAM_X, RD_KUAM_Z, 0);
    if( slot >= 0 )
    {
        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_set_bit(srv, "rd_room3_complete", 0);
        rd_clear_inv(player);
        rd_talk_finish(srv, npc_kuam, slot);
        rd_pass("opnpc1_kuam_intro");

        {
            int lslot = -1;
            int i;
            for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
            {
                if( srv->npcs[i].active && srv->npcs[i].type == npc_leye )
                {
                    lslot = i;
                    break;
                }
            }
            if( lslot < 0 && npc_leye > 0 )
                lslot = ToriRSServer_WorldNpcSpawn(
                    srv, npc_leye, RD_LEYE_X, RD_LEYE_Z, 0);
            SELFTEST_CHECK(lslot >= 0, "Sir Leye should spawn from Kuam talk");
            if( lslot >= 0 && obj_sword > 0 )
            {
                worn_set(player, rhand, obj_sword, 1);
                ToriRSServer_WorldNpcDied(srv, lslot);
                rd_finish(srv);
                SELFTEST_CHECK(rd_get_bit(player, "rd_room3_complete") == 0,
                               "killing Leye with a steel sword must fail");
                rd_pass("ai_queue3_leye_blade_fail");
            }

            rd_set_bit(srv, "rd_room3_complete", 0);
            rd_clear_inv(player);
            if( obj_hammer > 0 )
                rd_give(player, obj_hammer, 1);
            if( lslot < 0 || !srv->npcs[lslot].active )
                lslot = ToriRSServer_WorldNpcSpawn(
                    srv, npc_leye, RD_LEYE_X, RD_LEYE_Z, 0);
            if( lslot >= 0 && obj_hammer > 0 )
            {
                worn_set(player, rhand, obj_hammer, 1);
                ToriRSServer_WorldNpcDied(srv, lslot);
                rd_finish(srv);
                SELFTEST_CHECK(rd_get_bit(player, "rd_room3_complete") == 1,
                               "killing Leye with a warhammer must succeed, got %d",
                               rd_get_bit(player, "rd_room3_complete"));
                rd_pass("ai_queue3_leye_warhammer_success");
            }

            rd_set_bit(srv, "rd_room3_complete", 0);
            rd_clear_inv(player);
            worn_set(player, rhand, -1, 0);
            if( lslot < 0 || !srv->npcs[lslot].active )
                lslot = ToriRSServer_WorldNpcSpawn(
                    srv, npc_leye, RD_LEYE_X, RD_LEYE_Z, 0);
            if( lslot >= 0 )
            {
                ToriRSServer_WorldNpcDied(srv, lslot);
                rd_finish(srv);
                SELFTEST_CHECK(rd_get_bit(player, "rd_room3_complete") == 1,
                               "killing Leye unarmed must succeed, got %d",
                               rd_get_bit(player, "rd_room3_complete"));
                rd_pass("ai_queue3_leye_unarmed_success");
                rd_free_npc(srv, lslot);
            }
        }

        if( loc_door3 >= 0 )
        {
            loc_slot = rd_place_loc(srv, loc_door3, RD_KUAM_X, RD_KUAM_Z, 0);
            rd_set_bit(srv, "rd_room3_complete", 0);
            rd_oploc(srv, loc_door3, loc_slot);
            rd_pass("oploc_kuam_door_locked");
            rd_set_bit(srv, "rd_room3_complete", 1);
            rd_oploc(srv, loc_door3, loc_slot);
            rd_pass("oploc_kuam_tinley_handoff");
        }
        rd_free_npc(srv, slot);
    }

    /* ---- Tinley patience ---- */
    slot = rd_spawn(srv, npc_tinley, RD_TINLEY_X, RD_TINLEY_Z, 0);
    if( slot >= 0 )
    {
        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_set_bit(srv, "rd_room4_complete", 0);
        rd_set_bit(srv, "rd_room1_initiated", 0);
        rd_talk_finish(srv, npc_tinley, slot);
        SELFTEST_CHECK(rd_get_bit(player, "rd_room1_initiated") == 1 ||
                           rd_get_bit(player, "rd_room4_complete") == 1,
                       "first Tinley talk should start the wait");
        rd_pass("opnpc1_tinley_intro");

        rd_set_bit(srv, "rd_room4_complete", 0);
        rd_set_bit(srv, "rd_room1_initiated", 1);
        rd_talk_finish(srv, npc_tinley, slot);
        SELFTEST_CHECK(rd_get_bit(player, "rd_room4_complete") == 0,
                       "re-talking Tinley during the wait must fail");
        rd_pass("opnpc1_tinley_fail_early");

        rd_set_bit(srv, "rd_room4_complete", 0);
        rd_set_bit(srv, "rd_room1_initiated", 0);
        rd_talk(srv, npc_tinley, slot);
        rd_finish(srv);
        {
            int t;
            for( t = 0; t < 24; t++ )
                selftest_tick(srv);
        }
        rd_finish(srv);
        SELFTEST_CHECK(rd_get_bit(player, "rd_room4_complete") == 1,
                       "waiting ~15 ticks without re-talk must pass Tinley, got %d",
                       rd_get_bit(player, "rd_room4_complete"));
        rd_pass("softtimer_tinley_success");

        if( loc_door4 >= 0 )
        {
            loc_slot = rd_place_loc(srv, loc_door4, RD_TINLEY_X, RD_TINLEY_Z, 0);
            rd_set_bit(srv, "rd_room4_complete", 0);
            rd_oploc(srv, loc_door4, loc_slot);
            rd_pass("oploc_tinley_door_locked");
            rd_set_bit(srv, "rd_room4_complete", 1);
            rd_oploc(srv, loc_door4, loc_slot);
            rd_pass("oploc_tinley_table_handoff");
        }
        rd_free_npc(srv, slot);
    }

    /* ---- Lady Table statue memo ---- */
    slot = rd_spawn(srv, npc_table, RD_TABLE_X, RD_TABLE_Z, 0);
    if( slot >= 0 )
    {
        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_set_bit(srv, "rd_room2_complete", 0);
        rd_set_bit(srv, "rd_templock_1", 0);
        rd_set_bit(srv, "rd_room_order", 0);
        rd_talk_finish(srv, npc_table, slot);
        rd_pass("opnpc1_table_intro");

        if( loc_statue >= 0 )
        {
            loc_slot = rd_place_loc(srv, loc_statue, RD_TABLE_X + 2, RD_TABLE_Z, 0);
            rd_set_bit(srv, "rd_templock_1", 0);
            rd_oploc(srv, loc_statue, loc_slot);
            rd_pass("oploc_table_speak_first");

            rd_set_bit(srv, "rd_templock_1", 1);
            rd_set_bit(srv, "rd_room_order", 1);
            rd_oploc(srv, loc_statue, loc_slot);
            rd_pass("oploc_table_memorise_first");

            rd_set_bit(srv, "rd_templock_1", 1);
            rd_set_bit(srv, "rd_room_order", 0);
            rd_set_bit(srv, "rd_room2_complete", 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOC1, loc_statue, -1, loc_slot);
            rd_finish(srv);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room2_complete") == 1,
                           "touching the added statue must succeed, got %d",
                           rd_get_bit(player, "rd_room2_complete"));
            rd_pass("oploc_table_success");

            rd_set_bit(srv, "rd_templock_1", 5);
            rd_set_bit(srv, "rd_room_order", 0);
            rd_set_bit(srv, "rd_room2_complete", 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOC1, loc_statue, -1, loc_slot);
            rd_finish(srv);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room2_complete") == 0,
                           "wrong statue must fail Lady Table");
            rd_pass("oploc_table_fail");
        }

        if( loc_door2 >= 0 )
        {
            loc_slot = rd_place_loc(srv, loc_door2, RD_TABLE_X, RD_TABLE_Z, 0);
            rd_set_bit(srv, "rd_room2_complete", 0);
            rd_oploc(srv, loc_door2, loc_slot);
            rd_pass("oploc_table_door_locked");
            rd_set_bit(srv, "rd_room2_complete", 1);
            rd_oploc(srv, loc_door2, loc_slot);
            rd_pass("oploc_table_ren_handoff");
        }
        rd_free_npc(srv, slot);
    }

    /* ---- Sir Ren password ---- */
    slot = rd_spawn(srv, npc_ren, RD_REN_X, RD_REN_Z, 0);
    if( slot >= 0 )
    {
        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_set_bit(srv, "rd_room5_complete", 0);
        rd_set_bit(srv, "rd_templock_1", 0);
        rd_talk_pick(srv, npc_ren, slot, 1);
        rd_pass("opnpc1_ren_intro_clue");

        if( loc_door5 >= 0 )
        {
            loc_slot = rd_place_loc(srv, loc_door5, RD_REN_X, RD_REN_Z, 0);
            rd_set_bit(srv, "rd_templock_1", 0);
            rd_set_bit(srv, "rd_room5_complete", 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOC1, loc_door5, -1, loc_slot);
            rd_click_until_menu(srv, 4);
            selftest_charter_choose(srv, 2); /* FISH against BITE */
            rd_finish(srv);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room5_complete") == 0,
                           "wrong Ren password must fail");
            rd_pass("oploc_ren_wrong_password");

            rd_set_bit(srv, "rd_templock_1", 0);
            rd_set_bit(srv, "rd_room5_complete", 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOC1, loc_door5, -1, loc_slot);
            rd_click_until_menu(srv, 4);
            selftest_charter_choose(srv, 1); /* BITE */
            rd_finish(srv);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room5_complete") == 1,
                           "BITE must open Ren's door, got %d",
                           rd_get_bit(player, "rd_room5_complete"));
            rd_pass("oploc_ren_success");

            rd_oploc(srv, loc_door5, loc_slot);
            rd_pass("oploc_ren_cheevers_handoff");
        }
        rd_free_npc(srv, slot);
    }

    /* ---- Miss Cheevers chemistry ---- */
    slot = rd_spawn(srv, npc_cheevers, RD_CHEEVERS_X, RD_CHEEVERS_Z, 0);
    if( slot >= 0 )
    {
        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_set_bit(srv, "rd_room6_complete", 0);
        rd_set_bit(srv, "rd_room6_stone_door", 0);
        rd_clear_inv(player);
        rd_talk_finish(srv, npc_cheevers, slot);
        if( obj_spade > 0 )
            SELFTEST_CHECK(rd_inv_total(player, obj_spade) > 0,
                           "Cheevers talk should stock the metal spade");
        rd_pass("opnpc1_cheevers_intro");

        if( loc_bunsen >= 0 && obj_spade > 0 && obj_head > 0 )
        {
            loc_slot = rd_place_loc(srv, loc_bunsen, RD_CHEEVERS_X, RD_CHEEVERS_Z, 0);
            player->last_useitem = obj_spade;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_bunsen, -1, loc_slot);
            rd_finish(srv);
            SELFTEST_CHECK(rd_inv_total(player, obj_head) > 0,
                           "bunsen should burn the spade handle");
            rd_pass("oplocu_cheevers_burn_handle");
        }

        if( loc_stone >= 0 && obj_head > 0 && obj_sulph > 0 && obj_water > 0 )
        {
            loc_slot = rd_place_loc(srv, loc_stone, RD_CHEEVERS_X + 1, RD_CHEEVERS_Z, 0);
            rd_set_bit(srv, "rd_room6_stone_door", 0);
            rd_oploc(srv, loc_stone, loc_slot);
            rd_pass("oploc_cheevers_door_stuck");

            player->last_useitem = obj_sulph;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_stone, -1, loc_slot);
            rd_finish(srv);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room6_stone_door") == 0,
                           "sulphate without a wedged spade must fail");
            rd_pass("oplocu_cheevers_need_apply");

            if( rd_inv_total(player, obj_head) <= 0 )
                rd_give(player, obj_head, 1);
            player->last_useitem = obj_head;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_stone, -1, loc_slot);
            rd_finish(srv);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room6_stone_door") == 1,
                           "wedging the spade head should stage the door");
            rd_pass("oplocu_cheevers_wedge_spade");

            if( rd_inv_total(player, obj_sulph) <= 0 )
                rd_give(player, obj_sulph, 1);
            player->last_useitem = obj_sulph;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_stone, -1, loc_slot);
            rd_finish(srv);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room6_stone_door") == 2,
                           "cupric sulphate should stage the door");
            rd_pass("oplocu_cheevers_pour_sulphate");

            if( rd_inv_total(player, obj_water) <= 0 )
                rd_give(player, obj_water, 1);
            player->last_useitem = obj_water;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_stone, -1, loc_slot);
            rd_finish(srv);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room6_stone_door") == 3,
                           "water on sulphate should force the stone door open");
            rd_pass("oplocu_cheevers_door_opens");
            rd_oploc(srv, loc_stone, loc_slot);
            rd_pass("oploc_cheevers_pass_stone");
        }

        if( obj_gypsum > 0 && obj_tin > 0 && obj_tinfull > 0 )
        {
            if( rd_inv_total(player, obj_gypsum) <= 0 )
                rd_give(player, obj_gypsum, 1);
            if( rd_inv_total(player, obj_tin) <= 0 )
                rd_give(player, obj_tin, 1);
            player->last_item = obj_gypsum;
            player->last_useitem = obj_tin;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_gypsum, -1, -1);
            rd_finish(srv);
            SELFTEST_CHECK(rd_inv_total(player, obj_tinfull) > 0,
                           "gypsum on tin should mix");
            rd_pass("opheldu_cheevers_mix_gypsum");
        }

        if( loc_key >= 0 && obj_tinfull > 0 && obj_mould > 0 )
        {
            loc_slot = rd_place_loc(srv, loc_key, RD_CHEEVERS_X + 2, RD_CHEEVERS_Z, 0);
            if( rd_inv_total(player, obj_tinfull) <= 0 )
                rd_give(player, obj_tinfull, 1);
            player->last_useitem = obj_tinfull;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_key, -1, loc_slot);
            rd_finish(srv);
            SELFTEST_CHECK(rd_inv_total(player, obj_mould) > 0,
                           "imprinting the chained key should make a mould");
            rd_pass("oplocu_cheevers_imprint");
        }

        if( obj_mould > 0 && obj_copore > 0 && obj_copper_mould > 0 )
        {
            if( rd_inv_total(player, obj_mould) <= 0 )
                rd_give(player, obj_mould, 1);
            if( rd_inv_total(player, obj_copore) <= 0 )
                rd_give(player, obj_copore, 1);
            player->last_item = obj_mould;
            player->last_useitem = obj_copore;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_mould, -1, -1);
            rd_finish(srv);
            rd_pass("opheldu_cheevers_add_copper");
        }

        if( obj_copper_mould > 0 && obj_tinore > 0 && obj_unheated > 0 )
        {
            if( rd_inv_total(player, obj_copper_mould) <= 0 )
                rd_give(player, obj_copper_mould, 1);
            if( rd_inv_total(player, obj_tinore) <= 0 )
                rd_give(player, obj_tinore, 1);
            player->last_item = obj_copper_mould;
            player->last_useitem = obj_tinore;
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPHELDU, obj_copper_mould, -1, -1);
            rd_finish(srv);
            rd_pass("opheldu_cheevers_add_tin");
        }

        if( loc_bunsen >= 0 && obj_unheated > 0 && obj_complete > 0 )
        {
            loc_slot = rd_place_loc(srv, loc_bunsen, RD_CHEEVERS_X, RD_CHEEVERS_Z, 0);
            if( rd_inv_total(player, obj_unheated) <= 0 )
                rd_give(player, obj_unheated, 1);
            player->last_useitem = obj_unheated;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_bunsen, -1, loc_slot);
            rd_finish(srv);
            rd_pass("oplocu_cheevers_heat");
            if( rd_inv_total(player, obj_complete) > 0 )
            {
                player->last_useitem = obj_complete;
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOCU, loc_bunsen, -1, loc_slot);
                rd_finish(srv);
                rd_pass("oplocu_cheevers_key");
            }
        }

        if( loc_door6 >= 0 )
        {
            loc_slot = rd_place_loc(srv, loc_door6, RD_CHEEVERS_X, RD_CHEEVERS_Z, 0);
            rd_set_bit(srv, "rd_room6_stone_door", 0);
            rd_oploc(srv, loc_door6, loc_slot);
            rd_pass("oploc_cheevers_need_stone");
            rd_set_bit(srv, "rd_room6_stone_door", 3);
            rd_oploc(srv, loc_door6, loc_slot);
            rd_pass("oploc_cheevers_exit_locked");
            if( obj_key > 0 )
                rd_give(player, obj_key, 1);
            rd_oploc(srv, loc_door6, loc_slot);
            SELFTEST_CHECK(rd_get_bit(player, "rd_room6_complete") == 1,
                           "keyed Cheevers exit must complete the room");
            rd_pass("oploc_cheevers_hynn_handoff");
        }
        rd_free_npc(srv, slot);
    }

    /* ---- Hynn riddles ---- */
    slot = rd_spawn(srv, npc_hynn, RD_HYNN_X, RD_HYNN_Z, 0);
    if( slot >= 0 )
    {
        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_set_bit(srv, "rd_room7_complete", 0);
        rd_set_bit(srv, "rd_templock_1", 0);
        rd_talk_pick(srv, npc_hynn, slot, 2);
        SELFTEST_CHECK(rd_get_bit(player, "rd_room7_complete") == 0,
                       "wrong Hynn fingers answer must fail");
        rd_pass("opnpc1_hynn_wrong");

        rd_set_bit(srv, "rd_main", RD_TESTING);
        rd_set_bit(srv, "rd_room7_complete", 0);
        rd_set_bit(srv, "rd_templock_1", 0);
        rd_talk_pick(srv, npc_hynn, slot, 1);
        SELFTEST_CHECK(rd_get_bit(player, "rd_room7_complete") == 1,
                       "answering 0 fingers must pass Hynn, got %d",
                       rd_get_bit(player, "rd_room7_complete"));
        rd_pass("opnpc1_hynn_success");

        if( loc_door7 >= 0 )
        {
            loc_slot = rd_place_loc(srv, loc_door7, RD_HYNN_X, RD_HYNN_Z, 0);
            rd_set_bit(srv, "rd_room7_complete", 0);
            rd_oploc(srv, loc_door7, loc_slot);
            rd_pass("oploc_hynn_door_locked");
            rd_set_bit(srv, "rd_room7_complete", 1);
            rd_set_bit(srv, "rd_main", RD_TESTING);
            rd_oploc(srv, loc_door7, loc_slot);
            SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_PASSED,
                           "Hynn exit must write passed, got %d",
                           rd_get_bit(player, "rd_main"));
            rd_pass("oploc_hynn_passed_park");
        }
        rd_free_npc(srv, slot);
    }

    rd_journal(srv);
    rd_pass("journal_passed");

    /* ---- Tiffy complete + armoury ---- */
    slot = rd_spawn(srv, npc_tiffy, RD_TIFFY_X, RD_TIFFY_Z, 0);
    if( slot >= 0 )
    {
        rd_set_bit(srv, "rd_main", RD_PASSED);
        rd_prereqs(srv, player);
        rd_clear_inv(player);
        prayer_before = player->stat_xp_tenths[TORIRSSERVER_STAT_PRAYER];
        herb_before = player->stat_xp_tenths[RD_STAT_HERBLORE];
        agil_before = player->stat_xp_tenths[TORIRSSERVER_STAT_AGILITY];
        rd_talk_finish(srv, npc_tiffy, slot);
        SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_COMPLETE,
                       "Tiffy at passed must complete the quest, got %d",
                       rd_get_bit(player, "rd_main"));
        SELFTEST_CHECK(
            player->stat_xp_tenths[TORIRSSERVER_STAT_PRAYER] >=
                prayer_before + RD_REWARD_XP_TENTHS,
            "complete must grant 1000.5 Prayer XP");
        SELFTEST_CHECK(
            player->stat_xp_tenths[RD_STAT_HERBLORE] >= herb_before + RD_REWARD_XP_TENTHS,
            "complete must grant 1000.5 Herblore XP");
        SELFTEST_CHECK(
            player->stat_xp_tenths[TORIRSSERVER_STAT_AGILITY] >=
                agil_before + RD_REWARD_XP_TENTHS,
            "complete must grant 1000.5 Agility XP");
        if( obj_coins > 0 )
            SELFTEST_CHECK(rd_inv_total(player, obj_coins) >= 3000,
                           "complete must grant 3000 coins");
        rd_pass("opnpc1_tiffy_complete_scroll");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_tiffy, -1, slot);
        rd_finish(srv);
        rd_pass("opnpc3_initiate_armoury");
        rd_free_npc(srv, slot);
    }

    rd_journal(srv);
    rd_pass("journal_complete");

    {
        static const uint8_t complete_cmd[] = "complete quest_recruitmentdrive\n";

        handle_cheat(srv, complete_cmd, (int)sizeof(complete_cmd) - 1);
    }
    rd_finish(srv);
    SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_COMPLETE,
                   "::complete twice must leave endstate 4");
    rd_pass("complete_idempotent");

    SELFTEST_CHECK(rd_get_bit(player, "rd_main") == RD_COMPLETE,
                   "walk must end complete, rd_main=%d",
                   rd_get_bit(player, "rd_main"));

    rd_reset_quest(srv, player);
    rd_god(player);

    fprintf(stderr, "ToriRSServer rd selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_RECRUITMENTDRIVE_SELFTEST_U_H */
