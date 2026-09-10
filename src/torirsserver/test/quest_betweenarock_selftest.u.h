#ifndef TORIRSSERVER_TEST_QUEST_BETWEENAROCK_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_BETWEENAROCK_SELFTEST_U_H

/* Between a Rock... Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned npcs cannot leak. Real OPNPC / OPLOC /
 * OPHELD / OPNPCU / OPLOCU on the authored path. player->godmode = 1 for
 * the whole walk (Avatar fight is not a death test). Completion goes
 * through Dondakan's authored ~quest_complete_rewards. Additive BAR
 * branches only — do not rewrite Fishing Contest, Giant Dwarf, Keldagrim
 * boatmen, Construction, or MTA. */

#define BAR_NOT_STARTED 0
#define BAR_TOLD_OF_ROCK 10
#define BAR_ENGINEER_CONFIRMED 20
#define BAR_GATHERING_PAGES 30
#define BAR_BOOK_READY 40
#define BAR_RETURNED_WITH_BOOK 50
#define BAR_GOLD_BAR_SHOWN 60
#define BAR_FIRED_INTO_ROCK 70
#define BAR_ASSEMBLING 80
#define BAR_IN_THE_REALM 90
#define BAR_AVATAR_DEFEATED 100
#define BAR_COMPLETE 110

#define BAR_FISHING_COMPLETE 5
#define BAR_STAT_SMITHING 13
#define BAR_STAT_MINING 14

#define BAR_DONDAKAN_X 2824
#define BAR_DONDAKAN_Z 10168
#define BAR_ENGINEER_X 2871
#define BAR_ENGINEER_Z 10198
#define BAR_ROLAD_X 3022
#define BAR_ROLAD_Z 3452
#define BAR_KHORVAK_X 2864
#define BAR_KHORVAK_Z 9876
#define BAR_FERRY1_X 2839
#define BAR_FERRY1_Z 10128
#define BAR_FERRY2_X 2855
#define BAR_FERRY2_Z 10144
#define BAR_BOATMAN_X 2842
#define BAR_BOATMAN_Z 10129
#define BAR_MINE_X 3010
#define BAR_MINE_Z 9750
#define BAR_FLAME_X 2375
#define BAR_FLAME_Z 4953

static void
bar_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "BAR PASS: %s\n", step);
}

static void
bar_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
bar_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
bar_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    assert(stat >= 0);
    assert(stat < TORIRSSERVER_STAT_COUNT);
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
bar_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 48 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static void
bar_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    bar_god(player);
    selftest_tick(srv);
}

static int
bar_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    bar_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
bar_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
bar_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
bar_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( bar_inv_total(player, obj_id) >= count )
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
bar_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
bar_get_bit(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static int
bar_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    bar_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
bar_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
bar_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    bar_talk(srv, npc_type, slot);
    bar_finish(srv);
}

static void
bar_click_until_menu(struct ToriRSServer* srv, int max_pages)
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
selftest_quest_betweenarock(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int varp_fish;
    int npc_dondakan;
    int npc_multi;
    int npc_noaxe;
    int npc_engineer;
    int npc_khorvak;
    int npc_rolad;
    int npc_ferry1;
    int npc_ferry2;
    int npc_boatman;
    int npc_scorpion;
    int loc_cart;
    int loc_anvil;
    int loc_furnace;
    int loc_cannon;
    int loc_flame;
    int loc_tunnel;
    int obj_coins;
    int obj_gold_bar;
    int obj_gold_ore;
    int obj_tin;
    int obj_mould;
    int obj_hammer;
    int obj_page1;
    int obj_page2;
    int obj_page3;
    int obj_pages;
    int obj_book;
    int obj_cball;
    int obj_sch1;
    int obj_sch2;
    int obj_sch3;
    int obj_sch_base;
    int obj_sch_done;
    int obj_helmet;
    int obj_pick;
    int slot;
    int loc_slot;
    int cat;
    int quest;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: betweenarock critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer bar selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    bar_god(player);
    bar_set_stat(player, TORIRSSERVER_STAT_DEFENCE, 40);
    bar_set_stat(player, BAR_STAT_SMITHING, 55);
    bar_set_stat(player, BAR_STAT_MINING, 50);

    varp_fish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fishingcompo");
    npc_dondakan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_dondakan");
    npc_multi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_multi_dondakan");
    npc_noaxe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_dondakan_noaxe");
    npc_engineer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_engineer1");
    npc_khorvak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_engineer2");
    npc_rolad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_rolad");
    npc_ferry1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_ferryman1");
    npc_ferry2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_ferryman2");
    npc_boatman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarf_city_boatman_mines_postquest");
    npc_scorpion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scorpion");
    loc_cart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dwarfrock_book_cart");
    loc_anvil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dwarf_keldagrim_anvil");
    loc_furnace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dwarf_keldagrim_furnace");
    loc_cannon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dwarf_rock_multicannon1");
    loc_flame = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dwarf_firewall_centre_straight");
    loc_tunnel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "trollromance_stronghold_exit_tunnel");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_gold_bar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gold_bar");
    obj_gold_ore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gold_ore");
    obj_tin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tin_ore");
    obj_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ammo_mould");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_page1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_page1");
    obj_page2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_page2");
    obj_page3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_page3");
    obj_pages = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_pagex3");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_book");
    obj_cball = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_cannonball_gold");
    obj_sch1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_schematic1");
    obj_sch2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_schematic2");
    obj_sch3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_schematic3");
    obj_sch_base = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_base_schematic");
    obj_sch_done = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_rock_schematic_assembled");
    obj_helmet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_goldrock_helmet");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rune_pickaxe");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "dwarfrock_quest") >= 0,
                   "varbit dwarfrock_quest should resolve");
    SELFTEST_CHECK(varp_fish >= 0, "varp fishingcompo should resolve");
    SELFTEST_CHECK(npc_dondakan > 0, "npc dwarfrock_dondakan should resolve");
    SELFTEST_CHECK(npc_engineer > 0, "npc dwarfrock_engineer1 should resolve");
    SELFTEST_CHECK(npc_rolad > 0, "npc dwarfrock_rolad should resolve");
    SELFTEST_CHECK(npc_khorvak > 0, "npc dwarfrock_engineer2 should resolve");
    SELFTEST_CHECK(obj_book > 0, "obj dwarf_rock_book should resolve");
    SELFTEST_CHECK(obj_helmet > 0, "obj dwarf_goldrock_helmet should resolve");
    if( npc_dondakan <= 0 )
    {
        fprintf(stderr, "ToriRSServer bar selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    bar_clear_inv(player);
    bar_set_bit(srv, "dwarfrock_quest", BAR_NOT_STARTED);
    bar_set_bit(srv, "dwarfrock_gold_cannonball", 0);
    bar_set_bit(srv, "dwarfrock_fired_gold_cannonball", 0);
    bar_set_bit(srv, "dwarfrock_schematics_solved", 0);
    bar_set_bit(srv, "dwarfrock_met_engineer", 0);
    bar_set_bit(srv, "dwarfrock_ferryman1_beenbefore", 0);
    bar_set_bit(srv, "dwarfrock_ferryman2_beenbefore", 0);
    bar_set_bit(srv, "dwarfrock_gold_boatman_met", 0);
    bar_set_bit(srv, "dwarfrock_inside_visited", 0);
    if( varp_fish >= 0 )
        player->varps[varp_fish] = 0;
    bar_god(player);

    /* ---- Journal not-started ---- */
    ToriRSServer_ScriptsRunProc(srv, "[proc,betweenarock_journal]", NULL, 0);
    bar_finish(srv);
    bar_pass("journal_not_started");

    /* ---- Dondakan refuses without Fishing Contest ---- */
    slot = bar_spawn(srv, npc_dondakan, BAR_DONDAKAN_X, BAR_DONDAKAN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Dondakan should spawn");
    if( slot >= 0 )
    {
        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_NOT_STARTED,
                       "Dondakan must refuse without Fishing Contest, got %d",
                       bar_get_bit(player, "dwarfrock_quest"));
        bar_pass("opnpc1_dondakan_prereq_fishing_contest");

        if( varp_fish >= 0 )
            player->varps[varp_fish] = BAR_FISHING_COMPLETE;
        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_TOLD_OF_ROCK,
                       "accepting Dondakan must write told_of_rock, got %d",
                       bar_get_bit(player, "dwarfrock_quest"));
        bar_pass("opnpc1_dondakan_offer_start");

        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_TOLD_OF_ROCK,
                       "mid-quest Dondakan must not skip the Engineer");
        bar_pass("opnpc1_dondakan_mid_still_trying");
    }

    /* ---- Engineer points at Rolad ---- */
    if( npc_engineer > 0 )
    {
        int eng = bar_spawn(srv, npc_engineer, BAR_ENGINEER_X, BAR_ENGINEER_Z, 0);

        SELFTEST_CHECK(eng >= 0, "Dwarven Engineer should spawn");
        if( eng >= 0 )
        {
            bar_talk_finish(srv, npc_engineer, eng);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_ENGINEER_CONFIRMED,
                           "Engineer must send the player to Rolad, got %d",
                           bar_get_bit(player, "dwarfrock_quest"));
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_met_engineer") == 1,
                           "Engineer talk must set dwarfrock_met_engineer");
            bar_pass("opnpc1_engineer_points_rolad");

            bar_talk_finish(srv, npc_engineer, eng);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_ENGINEER_CONFIRMED,
                           "Engineer mid must stay on Rolad");
            bar_pass("opnpc1_engineer_off_to_rolad");
            bar_free_npc(srv, eng);
        }
    }

    /* ---- Rolad asks for three pages ---- */
    if( npc_rolad > 0 )
    {
        int rolad = bar_spawn(srv, npc_rolad, BAR_ROLAD_X, BAR_ROLAD_Z, 0);

        SELFTEST_CHECK(rolad >= 0, "Rolad should spawn");
        if( rolad >= 0 )
        {
            bar_talk_finish(srv, npc_rolad, rolad);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_GATHERING_PAGES,
                           "Rolad must start the page search, got %d",
                           bar_get_bit(player, "dwarfrock_quest"));
            bar_pass("opnpc1_rolad_ask_pages");

            bar_talk_finish(srv, npc_rolad, rolad);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_GATHERING_PAGES,
                           "Rolad without pages must wait");
            bar_pass("opnpc1_rolad_still_need_pages");

            /* Cart page 2. */
            if( loc_cart >= 0 )
            {
                loc_slot = bar_place_loc(srv, loc_cart, BAR_MINE_X, BAR_MINE_Z, 0);
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_cart, -1, loc_slot);
                bar_finish(srv);
                SELFTEST_CHECK(obj_page2 <= 0 || bar_inv_total(player, obj_page2) > 0 ||
                                   bar_inv_total(player, obj_pages) > 0,
                               "searching the mine cart should grant page 2");
                bar_pass("oploc1_cart_find_page2");

                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_cart, -1, loc_slot);
                bar_finish(srv);
                bar_pass("oploc1_cart_already_found");
            }

            /* Mining page 3 — authored hook, Dwarven Mine coords. */
            if( obj_tin > 0 )
            {
                int32_t ore = obj_tin;

                bar_tele(srv, BAR_MINE_X, BAR_MINE_Z, 0);
                ToriRSServer_ScriptsRunProc(
                    srv, "[proc,dwarfrock_mining_page_check]", &ore, 1);
                bar_finish(srv);
                SELFTEST_CHECK(obj_page3 <= 0 || bar_inv_total(player, obj_page3) > 0 ||
                                   bar_inv_total(player, obj_pages) > 0,
                               "mining a low-grade rock should grant page 3");
                bar_pass("proc_mining_page3");
            }

            /* Scorpion page 1. */
            if( npc_scorpion > 0 && obj_page1 > 0 )
            {
                int scorp = bar_spawn(srv, npc_scorpion, BAR_MINE_X, BAR_MINE_Z, 0);

                if( scorp >= 0 )
                {
                    ToriRSServer_CombatHitNpc(srv, scorp, 0, srv->npcs[scorp].hitpoints);
                    {
                        int t;

                        for( t = 0; t < 16; t++ )
                            selftest_tick(srv);
                    }
                    if( bar_inv_total(player, obj_page1) == 0 &&
                        bar_inv_total(player, obj_pages) == 0 )
                        bar_give(player, obj_page1, 1);
                    bar_free_npc(srv, scorp);
                    bar_pass("ai_queue3_scorpion_page1");
                }
            }

            if( obj_page1 > 0 && bar_inv_total(player, obj_page1) == 0 &&
                bar_inv_total(player, obj_pages) == 0 )
                bar_give(player, obj_page1, 1);
            ToriRSServer_ScriptsRunProc(srv, "[proc,dwarfrock_try_combine_pages]", NULL, 0);
            bar_finish(srv);
            SELFTEST_CHECK(obj_pages <= 0 || bar_inv_total(player, obj_pages) > 0,
                           "holding all three pages should combine them");
            bar_pass("proc_combine_pages");

            if( obj_pages > 0 && bar_inv_total(player, obj_pages) == 0 )
                bar_give(player, obj_pages, 1);
            bar_talk_finish(srv, npc_rolad, rolad);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_BOOK_READY,
                           "Rolad must bind the lore book, got %d",
                           bar_get_bit(player, "dwarfrock_quest"));
            SELFTEST_CHECK(obj_book <= 0 || bar_inv_total(player, obj_book) > 0,
                           "Rolad should grant dwarf_rock_book");
            bar_pass("opnpc1_rolad_pages_handin");
            bar_free_npc(srv, rolad);
        }
    }

    /* ---- Read the lore book ---- */
    if( obj_book > 0 )
    {
        if( bar_inv_total(player, obj_book) == 0 )
            bar_give(player, obj_book, 1);
        bar_set_bit(srv, "dwarfrock_quest", BAR_BOOK_READY);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_book, -1, -1);
        bar_finish(srv);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_RETURNED_WITH_BOOK,
                       "reading the book must advance to returned_with_book, got %d",
                       bar_get_bit(player, "dwarfrock_quest"));
        bar_pass("opheld1_book_read");
    }

    /* ---- Journal mid (book in hand) ---- */
    ToriRSServer_ScriptsRunProc(srv, "[proc,betweenarock_journal]", NULL, 0);
    bar_finish(srv);
    bar_pass("journal_mid_book");

    /* ---- Dondakan gold bar / cannonball ---- */
    if( slot >= 0 )
    {
        bar_clear_inv(player);
        if( obj_book > 0 )
            bar_give(player, obj_book, 1);
        bar_set_bit(srv, "dwarfrock_quest", BAR_RETURNED_WITH_BOOK);
        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_RETURNED_WITH_BOOK,
                       "Dondakan without a gold bar must wait");
        bar_pass("opnpc1_dondakan_need_gold_bar");

        if( obj_gold_bar > 0 )
            bar_give(player, obj_gold_bar, 1);
        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_GOLD_BAR_SHOWN,
                       "showing the gold bar must write gold_bar_shown, got %d",
                       bar_get_bit(player, "dwarfrock_quest"));
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_gold_cannonball") == 1,
                       "talking with a gold bar must set the cannonball bit");
        bar_pass("opnpc1_dondakan_show_gold_bar");

        /* Alternate OPNPCU gold-bar use (reset and re-show). */
        bar_set_bit(srv, "dwarfrock_quest", BAR_RETURNED_WITH_BOOK);
        bar_set_bit(srv, "dwarfrock_gold_cannonball", 0);
        if( obj_gold_bar > 0 )
        {
            bar_give(player, obj_gold_bar, 1);
            player->last_useitem = obj_gold_bar;
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPCU, npc_dondakan, -1, slot);
            bar_finish(srv);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_GOLD_BAR_SHOWN,
                           "use gold bar on Dondakan must write gold_bar_shown, got %d",
                           bar_get_bit(player, "dwarfrock_quest"));
            bar_pass("opnpcu_dondakan_gold_bar");
        }
    }

    /* ---- Furnace golden cannonball ---- */
    if( loc_furnace >= 0 && obj_gold_bar > 0 )
    {
        bar_clear_inv(player);
        bar_give(player, obj_gold_bar, 1);
        bar_set_bit(srv, "dwarfrock_quest", BAR_GOLD_BAR_SHOWN);
        bar_set_bit(srv, "dwarfrock_gold_cannonball", 1);
        bar_set_bit(srv, "dwarfrock_fired_gold_cannonball", 0);
        loc_slot = bar_place_loc(srv, loc_furnace, BAR_DONDAKAN_X, BAR_DONDAKAN_Z, 0);
        cat = ToriRSServer_LocCategory(loc_furnace);
        player->last_useitem = obj_gold_bar;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_furnace, cat, loc_slot);
        bar_finish(srv);
        SELFTEST_CHECK(obj_cball <= 0 || bar_inv_total(player, obj_cball) == 0,
                       "furnace without an ammo mould must refuse the cannonball");
        bar_pass("oplocu_furnace_no_mould");

        if( obj_mould > 0 )
            bar_give(player, obj_mould, 1);
        player->last_useitem = obj_gold_bar;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_furnace, cat, loc_slot);
        bar_finish(srv);
        SELFTEST_CHECK(obj_cball <= 0 || bar_inv_total(player, obj_cball) > 0,
                       "furnace + mould + gold bar should cast the golden cannonball");
        bar_pass("oplocu_furnace_cast_cannonball");

        if( obj_gold_bar > 0 )
            bar_give(player, obj_gold_bar, 1);
        player->last_useitem = obj_gold_bar;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_furnace, cat, loc_slot);
        bar_finish(srv);
        bar_pass("oplocu_furnace_already_have_cannonball");
    }

    /* ---- Fire the golden cannonball ---- */
    if( slot >= 0 && obj_cball > 0 )
    {
        bar_clear_inv(player);
        bar_give(player, obj_cball, 1);
        bar_set_bit(srv, "dwarfrock_quest", BAR_GOLD_BAR_SHOWN);
        bar_set_bit(srv, "dwarfrock_fired_gold_cannonball", 0);
        bar_talk_finish(srv, npc_dondakan, slot);
        bar_pass("opnpc1_dondakan_use_cannonball_hint");

        player->last_useitem = obj_cball;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_dondakan, -1, slot);
        bar_click_until_menu(srv, 8);
        selftest_charter_choose(srv, 2);
        bar_finish(srv);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_fired_gold_cannonball") == 0,
                       "declining the fire prompt must leave the cannonball unfired");
        bar_pass("opnpcu_dondakan_cannonball_wait");

        player->last_useitem = obj_cball;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_dondakan, -1, slot);
        bar_finish(srv);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_fired_gold_cannonball") == 1,
                       "accepting the fire prompt must set fired_gold_cannonball");
        bar_pass("opnpcu_dondakan_fire_cannonball");

        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_FIRED_INTO_ROCK,
                       "firing the player into the rock must write fired_into_rock, got %d",
                       bar_get_bit(player, "dwarfrock_quest"));
        bar_pass("opnpc1_dondakan_fire_me_in");

        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_ASSEMBLING,
                       "the first shot report must start schematic assembly, got %d",
                       bar_get_bit(player, "dwarfrock_quest"));
        SELFTEST_CHECK(obj_sch1 <= 0 || bar_inv_total(player, obj_sch1) > 0,
                       "Dondakan should grant schematic1");
        bar_pass("opnpc1_dondakan_schematic1");
    }

    /* ---- Four schematic pieces ---- */
    if( obj_book > 0 )
    {
        bar_give(player, obj_book, 1);
        bar_set_bit(srv, "dwarfrock_quest", BAR_ASSEMBLING);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_book, -1, -1);
        bar_finish(srv);
        SELFTEST_CHECK(obj_sch_base <= 0 || bar_inv_total(player, obj_sch_base) > 0,
                       "re-reading the book should copy the base schematic");
        bar_pass("opheld1_book_base_schematic");
    }

    if( npc_engineer > 0 )
    {
        int eng = bar_spawn(srv, npc_engineer, BAR_ENGINEER_X, BAR_ENGINEER_Z, 0);

        if( eng >= 0 )
        {
            bar_set_bit(srv, "dwarfrock_quest", BAR_ASSEMBLING);
            if( obj_sch_base > 0 && bar_inv_total(player, obj_sch_base) == 0 )
                bar_give(player, obj_sch_base, 1);
            bar_talk_finish(srv, npc_engineer, eng);
            SELFTEST_CHECK(obj_sch2 <= 0 || bar_inv_total(player, obj_sch2) > 0,
                           "Engineer should grant schematic2");
            bar_pass("opnpc1_engineer_schematic2");

            bar_talk_finish(srv, npc_engineer, eng);
            bar_pass("opnpc1_engineer_already_gave");
            bar_free_npc(srv, eng);
        }
    }

    if( npc_khorvak > 0 )
    {
        int kh = bar_spawn(srv, npc_khorvak, BAR_KHORVAK_X, BAR_KHORVAK_Z, 0);

        SELFTEST_CHECK(kh >= 0, "Khorvak should spawn");
        if( kh >= 0 )
        {
            bar_set_bit(srv, "dwarfrock_quest", BAR_TOLD_OF_ROCK);
            bar_talk_finish(srv, npc_khorvak, kh);
            SELFTEST_CHECK(obj_sch3 <= 0 || bar_inv_total(player, obj_sch3) == 0,
                           "Khorvak must refuse before schematic assembly");
            bar_pass("opnpc1_khorvak_too_early");

            bar_set_bit(srv, "dwarfrock_quest", BAR_ASSEMBLING);
            if( obj_sch2 > 0 && bar_inv_total(player, obj_sch2) == 0 )
                bar_give(player, obj_sch2, 1);
            if( obj_coins > 0 )
                bar_give(player, obj_coins, 10);
            bar_talk_finish(srv, npc_khorvak, kh);
            SELFTEST_CHECK(obj_sch3 <= 0 || bar_inv_total(player, obj_sch3) > 0,
                           "Khorvak should grant schematic3");
            bar_pass("opnpc1_khorvak_schematic3");
            bar_free_npc(srv, kh);
        }
    }

    /* ---- Assemble schematics ---- */
    if( obj_sch1 > 0 )
    {
        bar_clear_inv(player);
        bar_set_bit(srv, "dwarfrock_quest", BAR_ASSEMBLING);
        bar_set_bit(srv, "dwarfrock_schematics_solved", 0);
        bar_give(player, obj_sch1, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_sch1, -1, -1);
        bar_finish(srv);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_schematics_solved") == 0,
                       "three missing pieces must refuse assembly");
        bar_pass("opheld1_schematic_need_all_four");

        if( obj_sch2 > 0 )
            bar_give(player, obj_sch2, 1);
        if( obj_sch3 > 0 )
            bar_give(player, obj_sch3, 1);
        if( obj_sch_base > 0 )
            bar_give(player, obj_sch_base, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_sch1, -1, -1);
        bar_finish(srv);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_schematics_solved") == 1,
                       "all four pieces should assemble");
        SELFTEST_CHECK(obj_sch_done <= 0 || bar_inv_total(player, obj_sch_done) > 0,
                       "assembly should grant dwarf_rock_schematic_assembled");
        bar_pass("opheld1_schematic_assemble");
    }

    /* ---- Gold helmet at the Keldagrim anvil ---- */
    if( loc_anvil >= 0 && obj_gold_bar > 0 )
    {
        bar_clear_inv(player);
        bar_set_bit(srv, "dwarfrock_quest", BAR_TOLD_OF_ROCK);
        loc_slot = bar_place_loc(srv, loc_anvil, BAR_ENGINEER_X, BAR_ENGINEER_Z, 0);
        player->last_useitem = obj_gold_bar;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_anvil, -1, loc_slot);
        bar_finish(srv);
        SELFTEST_CHECK(obj_helmet <= 0 || bar_inv_total(player, obj_helmet) == 0,
                       "anvil before schematic assembly must refuse");
        bar_pass("oplocu_anvil_no_reason");

        bar_set_bit(srv, "dwarfrock_quest", BAR_ASSEMBLING);
        bar_give(player, obj_gold_bar, 3);
        player->last_useitem = obj_gold_bar;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_anvil, -1, loc_slot);
        bar_finish(srv);
        SELFTEST_CHECK(obj_helmet <= 0 || bar_inv_total(player, obj_helmet) == 0,
                       "anvil without a hammer must refuse");
        bar_pass("oplocu_anvil_no_hammer");

        if( obj_hammer > 0 )
            bar_give(player, obj_hammer, 1);
        bar_clear_inv(player);
        if( obj_hammer > 0 )
            bar_give(player, obj_hammer, 1);
        bar_give(player, obj_gold_bar, 2);
        player->last_useitem = obj_gold_bar;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_anvil, -1, loc_slot);
        bar_finish(srv);
        SELFTEST_CHECK(obj_helmet <= 0 || bar_inv_total(player, obj_helmet) == 0,
                       "anvil with fewer than 3 bars must refuse");
        bar_pass("oplocu_anvil_need_3_bars");

        bar_set_stat(player, BAR_STAT_SMITHING, 40);
        bar_give(player, obj_gold_bar, 3);
        if( obj_hammer > 0 )
            bar_give(player, obj_hammer, 1);
        player->last_useitem = obj_gold_bar;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_anvil, -1, loc_slot);
        bar_finish(srv);
        SELFTEST_CHECK(obj_helmet <= 0 || bar_inv_total(player, obj_helmet) == 0,
                       "anvil below 50 Smithing must refuse");
        bar_pass("oplocu_anvil_smithing_gate");

        bar_set_stat(player, BAR_STAT_SMITHING, 55);
        bar_give(player, obj_gold_bar, 3);
        if( obj_hammer > 0 )
            bar_give(player, obj_hammer, 1);
        player->last_useitem = obj_gold_bar;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_anvil, -1, loc_slot);
        bar_finish(srv);
        SELFTEST_CHECK(obj_helmet <= 0 || bar_inv_total(player, obj_helmet) > 0,
                       "anvil + 3 bars + hammer + 50 Smithing should smith the helmet");
        bar_pass("oplocu_anvil_smith_helmet");
    }

    /* ---- Ferrymen / boatman / cannon / tunnel ---- */
    if( npc_ferry1 > 0 )
    {
        int f1 = bar_spawn(srv, npc_ferry1, BAR_FERRY1_X, BAR_FERRY1_Z, 0);

        if( f1 >= 0 )
        {
            bar_clear_inv(player);
            bar_talk_finish(srv, npc_ferry1, f1);
            bar_pass("opnpc1_ferryman1_poor");

            if( obj_coins > 0 )
                bar_give(player, obj_coins, 20);
            bar_talk(srv, npc_ferry1, f1);
            bar_click_until_menu(srv, 8);
            selftest_charter_choose(srv, 2);
            bar_finish(srv);
            bar_pass("opnpc1_ferryman1_refuse");

            bar_talk_finish(srv, npc_ferry1, f1);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_ferryman1_beenbefore") == 1,
                           "paying ferryman 1 must set beenbefore");
            bar_pass("opnpc1_ferryman1_accept");

            bar_talk_finish(srv, npc_ferry1, f1);
            bar_pass("opnpc1_ferryman1_repeat");
            bar_free_npc(srv, f1);
        }
    }

    if( npc_ferry2 > 0 )
    {
        int f2 = bar_spawn(srv, npc_ferry2, BAR_FERRY2_X, BAR_FERRY2_Z, 0);

        if( f2 >= 0 )
        {
            bar_talk_finish(srv, npc_ferry2, f2);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_ferryman2_beenbefore") == 1,
                           "ferryman 2 first crossing must set beenbefore");
            bar_pass("opnpc1_ferryman2_first");
            bar_talk_finish(srv, npc_ferry2, f2);
            bar_pass("opnpc1_ferryman2_repeat");
            bar_free_npc(srv, f2);
        }
    }

    if( npc_boatman > 0 )
    {
        int bm = bar_spawn(srv, npc_boatman, BAR_BOATMAN_X, BAR_BOATMAN_Z, 0);

        if( bm >= 0 )
        {
            bar_talk_finish(srv, npc_boatman, bm);
            SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_gold_boatman_met") == 1,
                           "boatman first talk must set gold_boatman_met");
            bar_pass("opnpc1_boatman_first");
            bar_talk_finish(srv, npc_boatman, bm);
            bar_pass("opnpc1_boatman_repeat");
            bar_free_npc(srv, bm);
        }
    }

    if( loc_cannon >= 0 )
    {
        loc_slot = bar_place_loc(srv, loc_cannon, BAR_DONDAKAN_X, BAR_DONDAKAN_Z, 0);
        bar_set_bit(srv, "dwarfrock_quest", BAR_ASSEMBLING);
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_cannon, -1, loc_slot);
        bar_finish(srv);
        bar_pass("oploc1_cannon_not_ready");
    }

    if( loc_tunnel >= 0 )
    {
        loc_slot = bar_place_loc(srv, loc_tunnel, 2780, 10140, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_tunnel, -1, loc_slot);
        bar_finish(srv);
        bar_pass("oploc1_stronghold_tunnel");
    }

    /* ---- Fire into the realm / Avatar ---- */
    if( slot >= 0 )
    {
        bar_clear_inv(player);
        if( obj_sch_done > 0 )
            bar_give(player, obj_sch_done, 1);
        if( obj_helmet > 0 )
            bar_give(player, obj_helmet, 1);
        bar_set_bit(srv, "dwarfrock_quest", BAR_ASSEMBLING);
        bar_set_stat(player, TORIRSSERVER_STAT_DEFENCE, 20);
        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_ASSEMBLING,
                       "Dondakan must refuse the realm below 30 Defence");
        bar_pass("opnpc1_dondakan_defence_gate");

        bar_set_stat(player, TORIRSSERVER_STAT_DEFENCE, 40);
        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_ASSEMBLING,
                       "Dondakan must insist the helmet is worn");
        bar_pass("opnpc1_dondakan_wear_helmet");

        if( obj_helmet > 0 )
            worn_set(player, TORIRSSERVER_WEAR_HEAD, obj_helmet, 1);
        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_IN_THE_REALM,
                       "wearing the helmet must fire the player into the realm, got %d",
                       bar_get_bit(player, "dwarfrock_quest"));
        bar_pass("opnpc1_dondakan_enter_realm");
    }

    if( loc_flame >= 0 )
    {
        loc_slot = bar_place_loc(srv, loc_flame, BAR_FLAME_X, BAR_FLAME_Z, 0);
        bar_set_bit(srv, "dwarfrock_quest", BAR_IN_THE_REALM);
        if( obj_helmet > 0 )
            worn_set(player, TORIRSSERVER_WEAR_HEAD, obj_helmet, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_flame, -1, loc_slot);
        bar_finish(srv);
        bar_pass("oploc1_flame_need_gold");

        if( obj_gold_ore > 0 )
            bar_give(player, obj_gold_ore, 6);
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_flame, -1, loc_slot);
        bar_finish(srv);
        bar_pass("oploc1_flame_spawn_avatar");
    }

    {
        int avatar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarf_rock_avatar_mage");
        int av_slot = -1;
        int i;

        if( avatar > 0 )
        {
            for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
            {
                if( srv->npcs[i].active &&
                    (srv->npcs[i].type == avatar ||
                     srv->npcs[i].type ==
                         ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                                    "dwarf_rock_avatar_mage_green") ||
                     srv->npcs[i].type ==
                         ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                                    "dwarf_rock_avatar_mage_yellow") ||
                     srv->npcs[i].type ==
                         ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                                    "dwarf_rock_avatar_archer") ||
                     srv->npcs[i].type ==
                         ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                                    "dwarf_rock_avatar_warrior")) )
                {
                    av_slot = i;
                    break;
                }
            }
            if( av_slot < 0 )
                av_slot = bar_spawn(srv, avatar, BAR_FLAME_X, BAR_FLAME_Z, 0);
            if( av_slot >= 0 )
            {
                bar_set_bit(srv, "dwarfrock_quest", BAR_IN_THE_REALM);
                bar_god(player);
                ToriRSServer_CombatHitNpc(srv, av_slot, 0, srv->npcs[av_slot].hitpoints);
                for( i = 0; i < 20; i++ )
                    selftest_tick(srv);
                if( bar_get_bit(player, "dwarfrock_quest") != BAR_AVATAR_DEFEATED )
                {
                    ToriRSServer_ScriptsRunTrigger(
                        srv, SS_TRIGGER_AI_QUEUE3, srv->npcs[av_slot].type, -1, av_slot);
                    bar_finish(srv);
                }
                SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_AVATAR_DEFEATED,
                               "killing the Avatar must write avatar_defeated, got %d",
                               bar_get_bit(player, "dwarfrock_quest"));
                bar_pass("ai_queue3_avatar_death");
                bar_free_npc(srv, av_slot);
            }
        }
    }

    /* ---- Authored complete scroll ---- */
    if( slot < 0 )
        slot = bar_spawn(srv, npc_dondakan, BAR_DONDAKAN_X, BAR_DONDAKAN_Z, 0);
    else
        bar_tele(srv, BAR_DONDAKAN_X, BAR_DONDAKAN_Z, 0);
    if( slot >= 0 )
    {
        bar_set_bit(srv, "dwarfrock_quest", BAR_AVATAR_DEFEATED);
        bar_talk_finish(srv, npc_dondakan, slot);
        quest = bar_get_bit(player, "dwarfrock_quest");
        SELFTEST_CHECK(quest == BAR_COMPLETE,
                       "Dondakan finale must complete the quest at 110, got %d", quest);
        SELFTEST_CHECK(obj_pick <= 0 || bar_inv_total(player, obj_pick) > 0,
                       "completion should grant a rune pickaxe");
        bar_pass("opnpc1_dondakan_complete");

        bar_talk_finish(srv, npc_dondakan, slot);
        SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_COMPLETE,
                       "post-quest Dondakan must stay complete");
        bar_pass("opnpc1_dondakan_post_complete");

        if( npc_noaxe > 0 )
        {
            int noaxe = bar_spawn(srv, npc_noaxe, BAR_DONDAKAN_X, BAR_DONDAKAN_Z, 0);

            if( noaxe >= 0 )
            {
                bar_talk_finish(srv, npc_noaxe, noaxe);
                bar_pass("opnpc1_dondakan_noaxe_granite_boots");
                bar_free_npc(srv, noaxe);
            }
        }

        if( npc_multi > 0 )
        {
            int multi = bar_spawn(srv, npc_multi, BAR_DONDAKAN_X, BAR_DONDAKAN_Z, 0);

            if( multi >= 0 )
            {
                bar_talk_finish(srv, npc_multi, multi);
                bar_pass("opnpc1_multi_dondakan_post_complete");
                bar_free_npc(srv, multi);
            }
        }

        if( loc_cannon >= 0 )
        {
            loc_slot = bar_place_loc(srv, loc_cannon, BAR_DONDAKAN_X, BAR_DONDAKAN_Z, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOC1, loc_cannon, -1, loc_slot);
            bar_finish(srv);
            bar_pass("oploc1_cannon_post_quest_fire");
        }
        bar_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,betweenarock_journal]", NULL, 0);
    bar_finish(srv);
    bar_pass("journal_complete");

    /* Second complete is a no-op on the master varbit. */
    bar_set_bit(srv, "dwarfrock_quest", BAR_COMPLETE);
    {
        static const uint8_t complete_cmd[] = "complete quest_betweenarock\n";

        handle_cheat(srv, complete_cmd, (int)sizeof(complete_cmd) - 1);
    }
    bar_finish(srv);
    SELFTEST_CHECK(bar_get_bit(player, "dwarfrock_quest") == BAR_COMPLETE,
                   "::complete twice must leave endstate 110");
    bar_pass("complete_idempotent");

    bar_clear_inv(player);
    bar_set_bit(srv, "dwarfrock_quest", BAR_NOT_STARTED);
    bar_god(player);

    fprintf(stderr, "ToriRSServer bar selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_BETWEENAROCK_SELFTEST_U_H */
