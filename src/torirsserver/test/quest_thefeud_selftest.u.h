#ifndef TORIRSSERVER_TEST_QUEST_THEFEUD_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_THEFEUD_SELFTEST_U_H

/* The Feud Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned npcs
 * cannot leak. Real OPNPC / OPLOC / OPHELD / OPNPCU / OPLOCU on the
 * critical path. player->godmode = 1 for the whole walk (Tough Guy and
 * Bandit champion fights are not death tests). Completion goes through
 * Ali Morrisane's authored ~quest_complete_rewards(quest_feud, ...).
 * Additive Feud branches only — do not rewrite Shantay / generic Al
 * Kharid / Rogue Trader, Observatory, The Golem, Construction, or MTA. */

#define FEUD_NOT_STARTED 0
#define FEUD_ACCEPTED 1
#define FEUD_DRUNKEN_ALI_DONE 2
#define FEUD_GANGS_QUESTIONED 3
#define FEUD_CAMELS_BOUGHT 4
#define FEUD_RECEIPTS_GIVEN 5
#define FEUD_OPERATOR_JOINED 6
#define FEUD_PICKPOCKET1_DONE 7
#define FEUD_PICKPOCKET2_DONE 8
#define FEUD_PICKPOCKET3_DONE 9
#define FEUD_HEIST_BRIEFED 10
#define FEUD_HOUSE_ENTERED 11
#define FEUD_NOTE_NUMBERS 12
#define FEUD_NOTE_FIB 13
#define FEUD_SAFE_OPENED 14
#define FEUD_TRAITOR_BRIEFED 16
#define FEUD_THUG_QUESTIONED 17
#define FEUD_BARMAN_DONE 18
#define FEUD_SAUCE_BOUGHT 19
#define FEUD_DUNG_BUCKETED 20
#define FEUD_SNAKE_DONE 21
#define FEUD_POISON_MADE 22
#define FEUD_BEER_POISONED 23
#define FEUD_READY_CONFRONT 24
#define FEUD_MENAPHITE_BEATEN 25
#define FEUD_BANDIT_BEATEN 26
#define FEUD_MAYOR_TALKED 27
#define FEUD_COMPLETE 28

#define FEUD_CAMEL_COST 500
#define FEUD_REWARD_COINS 500

static void
feud_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "FEUD PASS: %s\n", step);
}

static void
feud_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
feud_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
feud_finish(struct ToriRSServer* srv)
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

static int
feud_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
feud_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = feud_chatmenu();
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
feud_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = feud_chatmenu();
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
feud_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    feud_god(player);
    selftest_tick(srv);
}

static int
feud_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    feud_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
feud_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
feud_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
feud_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( feud_inv_total(player, obj_id) >= count )
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
feud_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
feud_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
feud_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    feud_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
feud_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
feud_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    feud_talk(srv, npc_type, slot);
    feud_finish(srv);
}

static void
feud_use_on_npc(struct ToriRSServer* srv, int npc_type, int slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    feud_finish(srv);
    player->last_useitem = -1;
}

static void
selftest_quest_thefeud(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_thieving;
    int npc_ali;
    int npc_drunken;
    int npc_menaphite;
    int npc_bandit;
    int npc_camel;
    int npc_operator;
    int npc_urchin;
    int npc_villager;
    int npc_barman;
    int npc_kebab;
    int npc_snake;
    int npc_hag;
    int npc_menap_boss;
    int npc_bandit_boss;
    int npc_mayor;
    int npc_tough;
    int npc_champion;
    int loc_door;
    int loc_desk;
    int loc_bed;
    int loc_picture;
    int loc_trough;
    int loc_table;
    int obj_coins;
    int obj_beer;
    int obj_receipt;
    int obj_headpiece;
    int obj_disguise;
    int obj_keys;
    int obj_nos;
    int obj_fib;
    int obj_jewels;
    int obj_sauce;
    int obj_bucket;
    int obj_dung;
    int obj_poison;
    int obj_blackjack;
    int slot;
    int loc_slot;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: thefeud critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer thefeud selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    feud_god(player);

    stat_thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    npc_ali = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_ali_m");
    npc_drunken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_drunken_ali");
    npc_menaphite = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_egyptian_doorman_multi");
    npc_bandit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_arabian_guard_multi");
    npc_camel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_ali_the_discount_camel_seller");
    npc_operator = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_egyptian_minder");
    npc_urchin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_street_urchin");
    npc_villager = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_villager_multi_1");
    npc_barman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_ali_the_barman");
    npc_kebab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_kebabman");
    npc_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_snakecharmer");
    npc_hag = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_hag");
    npc_menap_boss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_menap_boss");
    npc_bandit_boss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_bandit_boss");
    npc_mayor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_mayor");
    npc_tough = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_menap_toughguy");
    npc_champion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_bandit_toughguy");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "feud_closed_door_left");
    loc_desk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "feud_mayors_desk");
    loc_bed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "feud_mayors_bed");
    loc_picture = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "feud_mayors_picture");
    loc_trough = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "feud_foodtrough");
    loc_table = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "feud_poison_beer_table");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_beer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "beer");
    obj_receipt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_camel_receipt");
    obj_headpiece = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_karidian_fakebeard_and_hat");
    obj_disguise = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_desert_disguise");
    obj_keys = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_mayors_house_keys");
    obj_nos = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_nos_note");
    obj_fib = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_fib_hint");
    obj_jewels = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_mayors_jewels");
    obj_sauce = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "superhot_kebab_sauce");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
    obj_dung = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_camel_pooh_bucket");
    obj_poison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_camel_poison_pooh_bucket");
    obj_blackjack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blackjack_oak");

    SELFTEST_CHECK(npc_ali > 0, "npc feud_ali_m should resolve");
    SELFTEST_CHECK(npc_drunken > 0, "npc feud_drunken_ali should resolve");
    SELFTEST_CHECK(npc_menaphite > 0, "npc feud_egyptian_doorman_multi should resolve");
    SELFTEST_CHECK(npc_bandit > 0, "npc feud_arabian_guard_multi should resolve");
    SELFTEST_CHECK(npc_camel > 0, "npc feud_ali_the_discount_camel_seller should resolve");
    SELFTEST_CHECK(npc_operator > 0, "npc feud_egyptian_minder should resolve");
    SELFTEST_CHECK(npc_urchin > 0, "npc feud_street_urchin should resolve");
    SELFTEST_CHECK(npc_villager > 0, "npc feud_villager_multi_1 should resolve");
    SELFTEST_CHECK(npc_barman > 0, "npc feud_ali_the_barman should resolve");
    SELFTEST_CHECK(npc_kebab > 0, "npc feud_kebabman should resolve");
    SELFTEST_CHECK(npc_snake > 0, "npc feud_snakecharmer should resolve");
    SELFTEST_CHECK(npc_hag > 0, "npc feud_hag should resolve");
    SELFTEST_CHECK(npc_menap_boss > 0, "npc feud_menap_boss should resolve");
    SELFTEST_CHECK(npc_bandit_boss > 0, "npc feud_bandit_boss should resolve");
    SELFTEST_CHECK(npc_mayor > 0, "npc feud_mayor should resolve");
    SELFTEST_CHECK(npc_tough > 0, "npc feud_menap_toughguy should resolve");
    SELFTEST_CHECK(npc_champion > 0, "npc feud_bandit_toughguy should resolve");
    SELFTEST_CHECK(loc_door >= 0, "loc feud_closed_door_left should resolve");
    SELFTEST_CHECK(loc_desk >= 0, "loc feud_mayors_desk should resolve");
    SELFTEST_CHECK(loc_bed >= 0, "loc feud_mayors_bed should resolve");
    SELFTEST_CHECK(loc_picture >= 0, "loc feud_mayors_picture should resolve");
    SELFTEST_CHECK(loc_trough >= 0, "loc feud_foodtrough should resolve");
    SELFTEST_CHECK(loc_table >= 0, "loc feud_poison_beer_table should resolve");
    SELFTEST_CHECK(obj_coins > 0, "obj coins should resolve");
    SELFTEST_CHECK(obj_beer > 0, "obj beer should resolve");
    SELFTEST_CHECK(stat_thieving >= 0, "stat thieving should resolve");
    if( npc_ali <= 0 )
    {
        fprintf(stderr, "ToriRSServer thefeud selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    feud_clear_inv(player);
    feud_set_bit(srv, "feud_var", FEUD_NOT_STARTED);
    feud_set_bit(srv, "feud_var_drink", 0);
    feud_set_bit(srv, "feud_var_talk_gangs", 0);
    feud_set_bit(srv, "feud_var_comp_gangs", 0);
    feud_set_bit(srv, "feud_ali_money", 0);
    feud_set_bit(srv, "feud_distracted", 0);
    feud_set_bit(srv, "feud_hag_list", 0);
    feud_set_bit(srv, "feud_found_trait", 0);
    feud_set_bit(srv, "feud_used_sauce", 0);
    feud_set_bit(srv, "feud_given_jewels", 0);
    feud_set_bit(srv, "feud_var_menaboss", 0);
    feud_set_bit(srv, "feud_var_banditboss", 0);
    feud_set_bit(srv, "feud_boss_vis", 0);
    feud_set_bit(srv, "feud_boss_vis2", 0);
    feud_set_bit(srv, "feud_bandit_boss_vis", 0);
    feud_set_bit(srv, "feud_mayor_multivar", 0);
    feud_god(player);

    /* ---- Ali Morrisane: decline / thieving gate / accept ---- */
    slot = feud_spawn(srv, npc_ali, 3304, 3211, 0);
    SELFTEST_CHECK(slot >= 0, "Ali Morrisane should spawn");
    if( slot >= 0 )
    {
        if( stat_thieving >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_thieving, 30);

        feud_talk(srv, npc_ali, slot);
        feud_click_until_menu(srv, 8);
        feud_pick_row(srv, 2);
        feud_finish(srv);
        SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_NOT_STARTED,
                       "busy decline must not start The Feud, got %d",
                       feud_get_bit(player, "feud_var"));
        feud_pass("opnpc1_ali_decline_busy");

        if( stat_thieving >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_thieving, 29);
        feud_talk(srv, npc_ali, slot);
        feud_click_until_menu(srv, 8);
        feud_pick_row(srv, 1);
        feud_finish(srv);
        SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_NOT_STARTED,
                       "thieving < 30 must gate the offer, got %d",
                       feud_get_bit(player, "feud_var"));
        feud_pass("opnpc1_ali_thieving_gate");

        if( stat_thieving >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_thieving, 30);
        feud_talk(srv, npc_ali, slot);
        feud_click_until_menu(srv, 8);
        feud_pick_row(srv, 1);
        feud_click_until_menu(srv, 8);
        feud_pick_row(srv, 2);
        feud_finish(srv);
        SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_NOT_STARTED,
                       "family-problem decline must not start The Feud, got %d",
                       feud_get_bit(player, "feud_var"));
        feud_pass("opnpc1_ali_decline_family");

        feud_talk_finish(srv, npc_ali, slot);
        SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_ACCEPTED,
                       "accepting Ali must start The Feud, got %d",
                       feud_get_bit(player, "feud_var"));
        SELFTEST_CHECK(obj_headpiece <= 0 || feud_inv_total(player, obj_headpiece) > 0,
                       "Ali should grant feud_karidian_fakebeard_and_hat");
        feud_pass("opnpc1_ali_accept");

        feud_talk_finish(srv, npc_ali, slot);
        SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_ACCEPTED,
                       "mid-quest Ali talk must not skip ahead, got %d",
                       feud_get_bit(player, "feud_var"));
        feud_pass("opnpc1_ali_any_word");
        feud_free_npc(srv, slot);
    }

    /* ---- Drunken Ali: three beers ---- */
    if( npc_drunken > 0 && obj_beer > 0 )
    {
        slot = feud_spawn(srv, npc_drunken, 3360, 2957, 0);
        SELFTEST_CHECK(slot >= 0, "Drunken Ali should spawn");
        if( slot >= 0 )
        {
            feud_set_bit(srv, "feud_var", FEUD_NOT_STARTED);
            feud_give(player, obj_beer, 1);
            feud_use_on_npc(srv, npc_drunken, slot, obj_beer);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_NOT_STARTED,
                           "beer before accept must not advance drunken Ali");
            feud_pass("opnpcu_drunken_before_quest");

            feud_set_bit(srv, "feud_var", FEUD_ACCEPTED);
            feud_set_bit(srv, "feud_var_drink", 0);
            feud_clear_inv(player);
            feud_give(player, obj_beer, 3);
            feud_use_on_npc(srv, npc_drunken, slot, obj_beer);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var_drink") == 1,
                           "first beer should set feud_var_drink=1, got %d",
                           feud_get_bit(player, "feud_var_drink"));
            feud_pass("opnpcu_drunken_beer1");

            feud_use_on_npc(srv, npc_drunken, slot, obj_beer);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var_drink") == 2,
                           "second beer should set feud_var_drink=2, got %d",
                           feud_get_bit(player, "feud_var_drink"));
            feud_pass("opnpcu_drunken_beer2");

            feud_use_on_npc(srv, npc_drunken, slot, obj_beer);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_DRUNKEN_ALI_DONE,
                           "third beer should finish drunken Ali, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpcu_drunken_beer3");

            feud_give(player, obj_beer, 1);
            feud_use_on_npc(srv, npc_drunken, slot, obj_beer);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_DRUNKEN_ALI_DONE,
                           "further beers must not rewind drunken Ali");
            feud_pass("opnpcu_drunken_already_done");
            feud_free_npc(srv, slot);
        }
    }

    /* ---- Question both gangs, then receipts ---- */
    if( npc_menaphite > 0 && npc_bandit > 0 )
    {
        int mena;
        int band;

        feud_set_bit(srv, "feud_var", FEUD_DRUNKEN_ALI_DONE);
        feud_set_bit(srv, "feud_var_talk_gangs", 0);
        mena = feud_spawn(srv, npc_menaphite, 3333, 2952, 0);
        SELFTEST_CHECK(mena >= 0, "Menaphite doorman should spawn");
        if( mena >= 0 )
        {
            feud_talk_finish(srv, npc_menaphite, mena);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var_talk_gangs") == 1,
                           "Menaphite camel talk should set talk_gangs=1, got %d",
                           feud_get_bit(player, "feud_var_talk_gangs"));
            feud_pass("opnpc1_menaphite_stolen_camel");
        }

        band = feud_spawn(srv, npc_bandit, 3354, 2987, 0);
        SELFTEST_CHECK(band >= 0, "Bandit guard should spawn");
        if( band >= 0 )
        {
            feud_talk_finish(srv, npc_bandit, band);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_GANGS_QUESTIONED,
                           "questioning both gangs should advance, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_bandit_stolen_camel");
        }

        /* Camel seller: buy two receipts. */
        if( npc_camel > 0 && obj_coins > 0 && obj_receipt > 0 )
        {
            int camel_slot = feud_spawn(srv, npc_camel, 3350, 2967, 0);

            SELFTEST_CHECK(camel_slot >= 0, "Ali the Camel Man should spawn");
            if( camel_slot >= 0 )
            {
                feud_clear_inv(player);
                feud_talk_finish(srv, npc_camel, camel_slot);
                SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_GANGS_QUESTIONED,
                               "camel seller without coin must not sell");
                feud_pass("opnpc1_camel_no_coin");

                feud_give(player, obj_coins, FEUD_CAMEL_COST);
                feud_talk_finish(srv, npc_camel, camel_slot);
                SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_CAMELS_BOUGHT,
                               "buying both camels should grant receipts, got %d",
                               feud_get_bit(player, "feud_var"));
                SELFTEST_CHECK(feud_inv_total(player, obj_receipt) >= 2,
                               "camel purchase should grant two receipts, got %d",
                               feud_inv_total(player, obj_receipt));
                feud_pass("opnpc1_camel_buy_receipts");
                feud_free_npc(srv, camel_slot);
            }
        }

        if( mena >= 0 && obj_receipt > 0 )
        {
            feud_set_bit(srv, "feud_var_comp_gangs", 0);
            if( feud_inv_total(player, obj_receipt) < 2 )
                feud_give(player, obj_receipt, 2);
            feud_talk_finish(srv, npc_menaphite, mena);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var_comp_gangs") == 1,
                           "Menaphite receipt should be refused, got %d",
                           feud_get_bit(player, "feud_var_comp_gangs"));
            feud_pass("opnpc1_menaphite_receipt_refuse");
        }
        if( band >= 0 && obj_receipt > 0 )
        {
            if( feud_inv_total(player, obj_receipt) < 1 )
                feud_give(player, obj_receipt, 1);
            feud_talk_finish(srv, npc_bandit, band);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_RECEIPTS_GIVEN,
                           "both receipt refusals should advance, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_bandit_receipt_refuse");
        }
        feud_free_npc(srv, mena);
        feud_free_npc(srv, band);
    }

    /* ---- Ali the Operator: recruit, three tasks, heist, traitor, finale ---- */
    if( npc_operator > 0 )
    {
        slot = feud_spawn(srv, npc_operator, 3334, 2949, 0);
        SELFTEST_CHECK(slot >= 0, "Ali the Operator should spawn");
        if( slot >= 0 )
        {
            feud_set_bit(srv, "feud_var", FEUD_RECEIPTS_GIVEN);
            feud_talk_finish(srv, npc_operator, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_OPERATOR_JOINED,
                           "Operator recruit should demand a pickpocket, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_operator_recruit");

            if( npc_villager > 0 )
            {
                int vill = feud_spawn(srv, npc_villager, 3352, 2960, 0);

                SELFTEST_CHECK(vill >= 0, "villager should spawn for pickpocket 1");
                if( vill >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_villager, -1, vill);
                    feud_finish(srv);
                    SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_PICKPOCKET1_DONE,
                                   "first villager pickpocket should advance, got %d",
                                   feud_get_bit(player, "feud_var"));
                    feud_pass("opnpc3_pickpocket_task1");
                    feud_free_npc(srv, vill);
                }
            }

            feud_talk_finish(srv, npc_operator, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_PICKPOCKET1_DONE,
                           "Operator task-2 briefing keeps pickpocket1, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_operator_task2_urchin");

            if( npc_urchin > 0 && obj_coins > 0 )
            {
                int urchin = feud_spawn(srv, npc_urchin, 3352, 2958, 0);

                SELFTEST_CHECK(urchin >= 0, "street urchin should spawn");
                if( urchin >= 0 )
                {
                    feud_clear_inv(player);
                    feud_talk_finish(srv, npc_urchin, urchin);
                    SELFTEST_CHECK(feud_get_bit(player, "feud_distracted") == 0,
                                   "urchin without coin must not distract");
                    feud_pass("opnpc1_urchin_no_coin");

                    feud_give(player, obj_coins, 10);
                    feud_talk_finish(srv, npc_urchin, urchin);
                    SELFTEST_CHECK(feud_get_bit(player, "feud_distracted") == 1,
                                   "paid urchin should set feud_distracted");
                    feud_pass("opnpc1_urchin_distraction");
                    feud_free_npc(srv, urchin);
                }
            }

            if( npc_villager > 0 )
            {
                int vill = feud_spawn(srv, npc_villager, 3352, 2960, 0);

                if( vill >= 0 )
                {
                    feud_set_bit(srv, "feud_distracted", 0);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_villager, -1, vill);
                    feud_finish(srv);
                    SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_PICKPOCKET1_DONE,
                                   "pickpocket 2 without distraction must refuse");
                    feud_pass("opnpc3_pickpocket_need_distraction");

                    feud_set_bit(srv, "feud_distracted", 1);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_villager, -1, vill);
                    feud_finish(srv);
                    SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_PICKPOCKET2_DONE,
                                   "distracted pickpocket should advance, got %d",
                                   feud_get_bit(player, "feud_var"));
                    feud_pass("opnpc3_pickpocket_task2");
                    feud_free_npc(srv, vill);
                }
            }

            feud_talk_finish(srv, npc_operator, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_PICKPOCKET2_DONE,
                           "Operator blackjack briefing keeps pickpocket2, got %d",
                           feud_get_bit(player, "feud_var"));
            SELFTEST_CHECK(obj_blackjack <= 0 || feud_inv_total(player, obj_blackjack) > 0,
                           "Operator should grant blackjack_oak");
            feud_pass("opnpc1_operator_task3_blackjack");

            if( npc_villager > 0 && obj_blackjack > 0 )
            {
                int vill = feud_spawn(srv, npc_villager, 3352, 2960, 0);

                if( vill >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_villager, -1, vill);
                    feud_finish(srv);
                    SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_PICKPOCKET2_DONE,
                                   "blackjack task without wielded oak must refuse");
                    feud_pass("opnpc3_pickpocket_need_blackjack");

                    worn_set(player, TORIRSSERVER_WEAR_WEAPON, obj_blackjack, 1);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_villager, -1, vill);
                    feud_finish(srv);
                    SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_PICKPOCKET3_DONE,
                                   "wielded blackjack pickpocket should finish the lesson, got %d",
                                   feud_get_bit(player, "feud_var"));
                    feud_pass("opnpc3_pickpocket_task3");
                    feud_free_npc(srv, vill);
                }
            }

            feud_talk_finish(srv, npc_operator, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_HEIST_BRIEFED,
                           "Operator heist briefing should grant disguise and keys, got %d",
                           feud_get_bit(player, "feud_var"));
            SELFTEST_CHECK(obj_disguise <= 0 || feud_inv_total(player, obj_disguise) > 0,
                           "heist briefing should grant feud_desert_disguise");
            SELFTEST_CHECK(obj_keys <= 0 || feud_inv_total(player, obj_keys) > 0,
                           "heist briefing should grant feud_mayors_house_keys");
            feud_pass("opnpc1_operator_heist_briefing");
            feud_free_npc(srv, slot);
        }
    }

    /* ---- Mayor house heist ---- */
    if( loc_door >= 0 )
    {
        loc_slot = feud_place_loc(srv, loc_door, 3358, 2969, 0);
        SELFTEST_CHECK(loc_slot >= 0, "mayor door should place");
        if( loc_slot >= 0 )
        {
            feud_set_bit(srv, "feud_var", FEUD_PICKPOCKET3_DONE);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, loc_slot);
            feud_finish(srv);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_PICKPOCKET3_DONE,
                           "door before heist briefing must refuse");
            feud_pass("oploc1_door_no_reason");

            feud_set_bit(srv, "feud_var", FEUD_HEIST_BRIEFED);
            feud_clear_inv(player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, loc_slot);
            feud_finish(srv);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_HEIST_BRIEFED,
                           "locked door without keys must refuse");
            feud_pass("oploc1_door_need_key");

            if( obj_keys > 0 )
                feud_give(player, obj_keys, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, loc_slot);
            feud_finish(srv);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_HEIST_BRIEFED,
                           "door without disguise must refuse");
            feud_pass("oploc1_door_need_disguise");

            if( obj_disguise > 0 )
                worn_set(player, TORIRSSERVER_WEAR_HEAD, obj_disguise, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, loc_slot);
            feud_finish(srv);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_HOUSE_ENTERED,
                           "disguised keyed entry should enter the house, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("oploc1_door_enter");
        }
    }

    if( loc_desk >= 0 && obj_nos > 0 )
    {
        loc_slot = feud_place_loc(srv, loc_desk, 3360, 2971, 0);
        if( loc_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_desk, -1, loc_slot);
            feud_finish(srv);
            SELFTEST_CHECK(feud_inv_total(player, obj_nos) > 0,
                           "desk search should grant feud_nos_note");
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_NOTE_NUMBERS,
                           "desk note should advance to note_numbers, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("oploc1_desk_numbers_note");
        }
    }

    if( loc_bed >= 0 && obj_fib > 0 )
    {
        loc_slot = feud_place_loc(srv, loc_bed, 3361, 2971, 0);
        if( loc_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bed, -1, loc_slot);
            feud_finish(srv);
            SELFTEST_CHECK(feud_inv_total(player, obj_fib) > 0,
                           "bed search should grant feud_fib_hint");
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_NOTE_FIB,
                           "bed note should advance to note_fib, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("oploc1_bed_fib_note");
        }
    }

    if( loc_picture >= 0 && obj_jewels > 0 )
    {
        loc_slot = feud_place_loc(srv, loc_picture, 3362, 2971, 0);
        if( loc_slot >= 0 )
        {
            int saved_nos = (obj_nos > 0) ? feud_inv_total(player, obj_nos) : 1;
            int saved_fib = (obj_fib > 0) ? feud_inv_total(player, obj_fib) : 1;

            if( obj_nos > 0 )
            {
                feud_clear_inv(player);
                if( obj_fib > 0 )
                    feud_give(player, obj_fib, 1);
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_picture, -1,
                                                    loc_slot);
                feud_finish(srv);
                SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_NOTE_FIB,
                               "safe without both notes must stay closed");
                feud_pass("oploc1_picture_need_notes");
                if( saved_nos > 0 )
                    feud_give(player, obj_nos, saved_nos);
                if( saved_fib > 0 && feud_inv_total(player, obj_fib) == 0 )
                    feud_give(player, obj_fib, saved_fib);
            }
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_picture, -1, loc_slot);
            feud_finish(srv);
            SELFTEST_CHECK(feud_inv_total(player, obj_jewels) > 0,
                           "opening the safe should grant feud_mayors_jewels");
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_SAFE_OPENED,
                           "safe should advance to safe_opened, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("oploc1_picture_open_safe");
        }
    }

    if( npc_operator > 0 && obj_jewels > 0 )
    {
        slot = feud_spawn(srv, npc_operator, 3334, 2949, 0);
        if( slot >= 0 )
        {
            if( feud_inv_total(player, obj_jewels) == 0 )
                feud_give(player, obj_jewels, 1);
            feud_talk_finish(srv, npc_operator, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_TRAITOR_BRIEFED,
                           "jewel hand-in should brief the traitor hunt, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_operator_traitor_briefing");
            feud_free_npc(srv, slot);
        }
    }

    /* ---- Traitor hunt ---- */
    if( npc_menaphite > 0 )
    {
        slot = feud_spawn(srv, npc_menaphite, 3333, 2952, 0);
        if( slot >= 0 )
        {
            feud_talk_finish(srv, npc_menaphite, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_THUG_QUESTIONED,
                           "thug traitor question should point at the barman, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_thug_traitor_question");
            feud_free_npc(srv, slot);
        }
    }

    if( npc_barman > 0 )
    {
        slot = feud_spawn(srv, npc_barman, 3361, 2955, 0);
        if( slot >= 0 )
        {
            feud_talk_finish(srv, npc_barman, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_BARMAN_DONE,
                           "barman should identify the waiting drink, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_barman_traitor_drink");
            feud_free_npc(srv, slot);
        }
    }

    if( npc_kebab > 0 && obj_sauce > 0 )
    {
        slot = feud_spawn(srv, npc_kebab, 3352, 2974, 0);
        if( slot >= 0 )
        {
            feud_talk_finish(srv, npc_kebab, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_SAUCE_BOUGHT,
                           "kebab seller should grant superhot sauce, got %d",
                           feud_get_bit(player, "feud_var"));
            SELFTEST_CHECK(feud_inv_total(player, obj_sauce) > 0,
                           "kebab seller should add superhot_kebab_sauce");
            feud_pass("opnpc1_kebab_sauce");
            feud_free_npc(srv, slot);
        }
    }

    if( loc_trough >= 0 && obj_sauce > 0 && obj_bucket > 0 && obj_dung > 0 )
    {
        loc_slot = feud_place_loc(srv, loc_trough, 3350, 2968, 0);
        if( loc_slot >= 0 )
        {
            if( feud_inv_total(player, obj_sauce) == 0 )
                feud_give(player, obj_sauce, 1);
            player->last_useitem = obj_sauce;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_trough, -1, loc_slot);
            feud_finish(srv);
            player->last_useitem = -1;
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_SAUCE_BOUGHT,
                           "trough without a bucket must refuse");
            feud_pass("oplocu_trough_need_bucket");

            feud_give(player, obj_bucket, 1);
            if( feud_inv_total(player, obj_sauce) == 0 )
                feud_give(player, obj_sauce, 1);
            player->last_useitem = obj_sauce;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_trough, -1, loc_slot);
            feud_finish(srv);
            player->last_useitem = -1;
            SELFTEST_CHECK(feud_inv_total(player, obj_dung) > 0,
                           "trough should grant feud_camel_pooh_bucket");
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_DUNG_BUCKETED,
                           "bucketing dung should advance, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("oplocu_trough_sauce_dung");
        }
    }

    if( npc_snake > 0 )
    {
        slot = feud_spawn(srv, npc_snake, 3354, 2953, 0);
        if( slot >= 0 )
        {
            feud_talk_finish(srv, npc_snake, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_SNAKE_DONE,
                           "snake charmer should hand over the charmed snake, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_snake_have_a_go");
            feud_free_npc(srv, slot);
        }
    }

    if( npc_hag > 0 && obj_dung > 0 && obj_poison > 0 )
    {
        slot = feud_spawn(srv, npc_hag, 3346, 2987, 0);
        if( slot >= 0 )
        {
            feud_clear_inv(player);
            feud_talk_finish(srv, npc_hag, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_SNAKE_DONE,
                           "hag without dung must refuse");
            feud_pass("opnpc1_hag_no_dung");

            feud_give(player, obj_dung, 1);
            feud_talk_finish(srv, npc_hag, slot);
            SELFTEST_CHECK(feud_inv_total(player, obj_poison) > 0,
                           "hag should brew feud_camel_poison_pooh_bucket");
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_POISON_MADE,
                           "hag brew should advance to poison_made, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_hag_brew_poison");
            feud_free_npc(srv, slot);
        }
    }

    if( loc_table >= 0 && obj_poison > 0 )
    {
        loc_slot = feud_place_loc(srv, loc_table, 3361, 2956, 0);
        if( loc_slot >= 0 )
        {
            if( feud_inv_total(player, obj_poison) == 0 )
                feud_give(player, obj_poison, 1);
            player->last_useitem = obj_poison;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_table, -1, loc_slot);
            feud_finish(srv);
            player->last_useitem = -1;
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_BEER_POISONED,
                           "poisoning the beer should advance, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("oplocu_poison_beer");
        }
    }

    if( npc_operator > 0 )
    {
        slot = feud_spawn(srv, npc_operator, 3334, 2949, 0);
        if( slot >= 0 )
        {
            feud_talk_finish(srv, npc_operator, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_READY_CONFRONT,
                           "Operator final orders should send the player to both bosses, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_operator_final_orders");
            feud_free_npc(srv, slot);
        }
    }

    /* ---- Both bosses (godmode: these are not death tests) ---- */
    if( npc_menap_boss > 0 && npc_tough > 0 )
    {
        slot = feud_spawn(srv, npc_menap_boss, 3334, 2956, 0);
        if( slot >= 0 )
        {
            int tough;

            feud_god(player);
            feud_talk_finish(srv, npc_menap_boss, slot);
            tough = selftest_find_npc(srv, npc_tough);
            SELFTEST_CHECK(tough >= 0, "Menaphite Leader should spawn the Tough Guy");
            feud_pass("opnpc1_menap_leader_confront");
            if( tough >= 0 )
            {
                struct ToriRSServerNpc* npc = &srv->npcs[tough];
                int waited;

                ToriRSServer_CombatHitNpc(srv, tough, 0, npc->hitpoints);
                for( waited = 0; waited < 40 &&
                     feud_get_bit(player, "feud_var") != FEUD_MENAPHITE_BEATEN;
                     waited++ )
                    selftest_tick(srv);
                SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_MENAPHITE_BEATEN,
                               "defeating the Tough Guy should advance, got %d",
                               feud_get_bit(player, "feud_var"));
                feud_pass("ai_queue3_tough_guy");
                feud_free_npc(srv, tough);
            }
            feud_free_npc(srv, slot);
        }
    }

    if( npc_bandit_boss > 0 && npc_champion > 0 )
    {
        slot = feud_spawn(srv, npc_bandit_boss, 3353, 3002, 0);
        if( slot >= 0 )
        {
            int champ;

            feud_god(player);
            feud_talk_finish(srv, npc_bandit_boss, slot);
            champ = selftest_find_npc(srv, npc_champion);
            SELFTEST_CHECK(champ >= 0, "Bandit Leader should spawn the Bandit champion");
            feud_pass("opnpc1_bandit_leader_confront");
            if( champ >= 0 )
            {
                struct ToriRSServerNpc* npc = &srv->npcs[champ];
                int waited;

                ToriRSServer_CombatHitNpc(srv, champ, 0, npc->hitpoints);
                for( waited = 0; waited < 40 &&
                     feud_get_bit(player, "feud_var") != FEUD_BANDIT_BEATEN;
                     waited++ )
                    selftest_tick(srv);
                SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_BANDIT_BEATEN,
                               "defeating the Bandit champion should advance, got %d",
                               feud_get_bit(player, "feud_var"));
                feud_pass("ai_queue3_bandit_champion");
                feud_free_npc(srv, champ);
            }
            feud_free_npc(srv, slot);
        }
    }

    if( npc_mayor > 0 )
    {
        slot = feud_spawn(srv, npc_mayor, 3360, 2970, 0);
        if( slot >= 0 )
        {
            feud_talk_finish(srv, npc_mayor, slot);
            SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_MAYOR_TALKED,
                           "mayor reveal should hide the nephew no longer, got %d",
                           feud_get_bit(player, "feud_var"));
            feud_pass("opnpc1_mayor_reveal");
            feud_free_npc(srv, slot);
        }
    }

    /* ---- Ali finish + authored complete scroll ---- */
    slot = feud_spawn(srv, npc_ali, 3304, 3211, 0);
    if( slot >= 0 )
    {
        int xp_before = 0;

        if( stat_thieving >= 0 )
            xp_before = player->stat_xp_tenths[stat_thieving];
        feud_clear_inv(player);
        feud_talk_finish(srv, npc_ali, slot);
        SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_COMPLETE,
                       "Ali finish must run quest_feud complete rewards, got %d",
                       feud_get_bit(player, "feud_var"));
        SELFTEST_CHECK(obj_coins <= 0 || feud_inv_total(player, obj_coins) >= FEUD_REWARD_COINS,
                       "completion should grant 500 coins");
        SELFTEST_CHECK(obj_disguise <= 0 || feud_inv_total(player, obj_disguise) > 0,
                       "completion should grant feud_desert_disguise");
        if( stat_thieving >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_thieving] > xp_before,
                           "completion should award Thieving xp, %d -> %d",
                           xp_before, player->stat_xp_tenths[stat_thieving]);
        feud_pass("opnpc1_ali_complete");

        feud_talk_finish(srv, npc_ali, slot);
        SELFTEST_CHECK(feud_get_bit(player, "feud_var") == FEUD_COMPLETE,
                       "post-complete Ali talk must stay at endstate 28");
        feud_pass("opnpc1_ali_post_complete");
        feud_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,feud_journal]", NULL, 0);
    feud_finish(srv);
    feud_pass("proc_feud_journal_complete");

    feud_clear_inv(player);
    feud_set_bit(srv, "feud_var", FEUD_NOT_STARTED);
    feud_god(player);
    fprintf(stderr, "ToriRSServer thefeud selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_THEFEUD_SELFTEST_U_H */
