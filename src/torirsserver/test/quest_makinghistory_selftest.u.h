#ifndef TORIRSSERVER_TEST_QUEST_MAKINGHISTORY_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_MAKINGHISTORY_SELFTEST_U_H

/* Making History Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Jorral /
 * Erin / Blanin / Dron / Droalak / Melina / Lathas cannot leak. Real OPNPC1 /
 * OPHELD1 / OPHELDU on the authored path. player->godmode = 1 for the whole
 * walk (not a death test). Completion goes through Jorral finish ->
 * queue(makinghistory_quest_complete) -> ~quest_complete_rewards.
 *
 * Gate: TORIRSSERVER_SELFTEST_MH_ONLY=1
 *
 * No ::makinghistoryrun soft-skip walk. Priest in Peril FINISHED + Restless
 * Ghost STARTED are set on the real varps.
 */

#define MH_NOT_STARTED 0
#define MH_STARTED 1
#define MH_CASTLE 2
#define MH_LATHAS_DONE 3
#define MH_COMPLETE 4

#define MH_TRADER_NONE 0
#define MH_TRADER_KEY 1
#define MH_TRADER_CHEST 2
#define MH_TRADER_JOURNAL 3

#define MH_WARR_NONE 0
#define MH_WARR_BLANIN 1
#define MH_WARR_DONE 2

#define MH_GHOST_NONE 0
#define MH_GHOST_DROALAK 1
#define MH_GHOST_MELINA 2
#define MH_GHOST_SCROLL 3
#define MH_GHOST_FAREWELL 4

#define MH_PIP_COMPLETE 60
#define MH_PRIEST_STARTED 1
#define MH_REWARD_CRAFT_TENTHS 10000
#define MH_REWARD_PRAY_TENTHS 10000
#define MH_REWARD_COINS 750
#define MH_NECK 2

#define MH_JORRAL_X 2436
#define MH_JORRAL_Z 3346
#define MH_ERIN_X 2658
#define MH_ERIN_Z 3316
#define MH_DIG_X 2442
#define MH_DIG_Z 3140
#define MH_DIG_MISS_X 2444
#define MH_DIG_MISS_Z 3142
#define MH_BLANIN_X 2675
#define MH_BLANIN_Z 3671
#define MH_DRON_X 2658
#define MH_DRON_Z 3700
#define MH_DROALAK_X 3657
#define MH_DROALAK_Z 3469
#define MH_MELINA_X 3674
#define MH_MELINA_Z 3484
#define MH_LATHAS_X 2578
#define MH_LATHAS_Z 3293

static void
mh_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "MH PASS: %s\n", step);
}

static void
mh_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
mh_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
mh_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    /* Drain resume buttons only. Do not WorldCloseModal -- that aborts the
     * active script and drops a finish talk before ~makinghistory_do_complete. */
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 8) <= 0 )
            selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
}

static int
mh_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
mh_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mh_chatmenu();
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
mh_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mh_chatmenu();
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
mh_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    mh_god(player);
    selftest_tick(srv);
}

static int
mh_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    mh_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
mh_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
mh_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
mh_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( mh_inv_total(player, obj_id) >= count )
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
mh_wear(struct ToriRSServerPlayer* player, int slot, int obj_id)
{
    assert(player);
    assert(slot >= 0);
    assert(slot < TORIRSSERVER_WORN_SLOTS);
    assert(obj_id > 0);
    worn_set(player, slot, obj_id, 1);
}

static void
mh_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mh_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
mh_set_varp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        player->varps[varp] = value;
}

static void
mh_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
mh_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    mh_talk(srv, npc_type, slot);
    mh_finish(srv);
}

static void
mh_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    mh_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        mh_click_until_menu(srv, 16);
        mh_pick_row(srv, rows[i]);
    }
    mh_finish(srv);
}

static void
mh_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    mh_set_bit(srv, "makinghistory_prog", MH_NOT_STARTED);
    mh_set_bit(srv, "makinghistory_trader_prog", MH_TRADER_NONE);
    mh_set_bit(srv, "makinghistory_warr_prog", MH_WARR_NONE);
    mh_set_bit(srv, "makinghistory_ghost_prog", MH_GHOST_NONE);
    mh_set_bit(srv, "makinghistory_melina_pres", 0);
    mh_set_bit(srv, "makinghistory_droalak_pres", 0);
}

static void
mh_set_prereqs(struct ToriRSServerPlayer* player)
{
    assert(player);
    mh_set_varp(player, "priestperil", MH_PIP_COMPLETE);
    mh_set_varp(player, "prieststart", MH_PRIEST_STARTED);
}

static void
mh_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,makinghistory_journal]", NULL, 0);
    mh_finish(srv);
    mh_pass(step);
}

static void
mh_dron_fail_at(struct ToriRSServer* srv, int npc_dron, int slot, int fail_index, int fail_row)
{
    /* Intro + 12 answers. fail_index is 0..11 in that answer list. */
    static const int k_pass[] = { 1, 2, 1, 2, 2, 3, 2, 2, 2, 2, 2, 3, 2 };
    int rows[16];
    int n;
    int i;

    assert(srv);
    assert(fail_index >= 0);
    assert(fail_index < 12);
    n = fail_index + 2; /* intro + answers through the fail */
    for( i = 0; i < n; i++ )
        rows[i] = k_pass[i];
    rows[fail_index + 1] = fail_row;
    mh_talk_rows(srv, npc_dron, slot, rows, n);
}

static void
selftest_quest_makinghistory(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_jorral;
    int npc_erin;
    int npc_blanin;
    int npc_dron;
    int npc_droalak;
    int npc_melina;
    int npc_lathas;
    int obj_key;
    int obj_chest;
    int obj_journal;
    int obj_scroll;
    int obj_letter1;
    int obj_letter2;
    int obj_spade;
    int obj_ghostspeak;
    int obj_sapphire;
    int obj_coins;
    int stat_craft;
    int stat_pray;
    int slot;
    int craft_before;
    int pray_before;
    static const int k_accept[] = { 1, 1, 1 };
    static const int k_erin_key[] = { 1 };
    static const int k_dron_pass[] = { 1, 2, 1, 2, 2, 3, 2, 2, 2, 2, 2, 3, 2 };

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: making history critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer makinghistory selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    mh_god(player);

    npc_jorral = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "makinghistory_jorral");
    npc_erin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "silver_merchant_ardougne");
    npc_blanin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "makinghistory_blanin");
    npc_dron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "makinghistory_dron");
    npc_droalak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "makinghistory_droalak");
    npc_melina = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "makinghistory_melina");
    npc_lathas = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "kinglathas");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "makinghistory_key");
    obj_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "makinghistory_chest");
    obj_journal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "makinghistory_journal");
    obj_scroll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "makinghistory_scroll1");
    obj_letter1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "makinghistory_letter1");
    obj_letter2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "makinghistory_letter2");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
    obj_ghostspeak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_ghostspeak");
    obj_sapphire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "strung_sapphire_amulet");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_pray = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "makinghistory_prog") >= 0,
                   "varbit makinghistory_prog should resolve");
    SELFTEST_CHECK(npc_jorral > 0, "npc makinghistory_jorral should resolve");
    SELFTEST_CHECK(npc_erin > 0, "npc silver_merchant_ardougne should resolve");
    SELFTEST_CHECK(npc_blanin > 0, "npc makinghistory_blanin should resolve");
    SELFTEST_CHECK(npc_dron > 0, "npc makinghistory_dron should resolve");
    SELFTEST_CHECK(npc_droalak > 0, "npc makinghistory_droalak should resolve");
    SELFTEST_CHECK(npc_melina > 0, "npc makinghistory_melina should resolve");
    SELFTEST_CHECK(npc_lathas > 0, "npc kinglathas should resolve");
    SELFTEST_CHECK(obj_key > 0, "obj makinghistory_key should resolve");
    SELFTEST_CHECK(obj_journal > 0, "obj makinghistory_journal should resolve");
    if( npc_jorral <= 0 || npc_erin <= 0 || npc_dron <= 0 || obj_key <= 0 )
    {
        fprintf(stderr, "ToriRSServer makinghistory selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    mh_clear_inv(player);
    mh_reset_quest(srv);
    mh_god(player);
    mh_journal(srv, "journal_not_started");

    /* ---- Jorral refuse-reqs / offer / refuse / accept ---- */
    slot = mh_spawn(srv, npc_jorral, MH_JORRAL_X, MH_JORRAL_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Jorral should spawn");
    if( slot >= 0 )
    {
        mh_set_varp(player, "priestperil", 0);
        mh_set_varp(player, "prieststart", 0);
        mh_talk_rows(srv, npc_jorral, slot, k_accept, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_NOT_STARTED,
                       "missing Priest in Peril + Restless Ghost must not start");
        mh_pass("opnpc1_jorral_refuse_reqs");

        mh_set_prereqs(player);
        {
            static const int k_shame[] = { 2 };

            mh_talk_rows(srv, npc_jorral, slot, k_shame, 1);
        }
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_NOT_STARTED,
                       "That's a shame. must not start");
        mh_pass("opnpc1_jorral_choice_refuse_shame");

        {
            static const int k_no[] = { 1, 2 };

            mh_talk_rows(srv, npc_jorral, slot, k_no, 2);
        }
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_NOT_STARTED,
                       "I'm not interested. must not start");
        mh_pass("opnpc1_jorral_choice_not_interested");

        {
            static const int k_busy[] = { 1, 1, 2 };

            mh_talk_rows(srv, npc_jorral, slot, k_busy, 3);
        }
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_NOT_STARTED,
                       "I don't have time must not start");
        mh_pass("opnpc1_jorral_choice_refuse_no_time");

        mh_talk_rows(srv, npc_jorral, slot, k_accept, 3);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_STARTED,
                       "accept must write started, got %d",
                       mh_get_bit(player, "makinghistory_prog"));
        mh_pass("opnpc1_jorral_accept");

        mh_talk_finish(srv, npc_jorral, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_STARTED,
                       "mid reminder must stay started");
        mh_pass("opnpc1_jorral_mid_reminder");
    }

    mh_journal(srv, "journal_started");

    /* ---- Erin too-early (before start) then key / lost-key / hint ---- */
    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_erin, MH_ERIN_X, MH_ERIN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Erin should spawn");
    if( slot >= 0 )
    {
        mh_reset_quest(srv);
        mh_set_prereqs(player);
        mh_clear_inv(player);
        mh_talk_finish(srv, npc_erin, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_trader_prog") == MH_TRADER_NONE,
                       "Erin before start must not hand a key");
        SELFTEST_CHECK(mh_inv_total(player, obj_key) == 0,
                       "Erin too-early must not add makinghistory_key");
        mh_pass("opnpc1_erin_too_early");

        mh_set_bit(srv, "makinghistory_prog", MH_STARTED);
        {
            static const int k_erin_no[] = { 2 };

            mh_talk_rows(srv, npc_erin, slot, k_erin_no, 1);
        }
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_trader_prog") == MH_TRADER_NONE,
                       "Erin Not right now must not advance trader");
        mh_pass("opnpc1_erin_choice_refuse");

        mh_talk_rows(srv, npc_erin, slot, k_erin_key, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_trader_prog") == MH_TRADER_KEY,
                       "Erin ask must write got_key, got %d",
                       mh_get_bit(player, "makinghistory_trader_prog"));
        SELFTEST_CHECK(mh_inv_total(player, obj_key) == 1, "Erin must hand the enchanted key");
        mh_pass("opnpc1_erin_key");

        mh_clear_inv(player);
        mh_talk_finish(srv, npc_erin, slot);
        SELFTEST_CHECK(mh_inv_total(player, obj_key) == 1, "lost-key must replace the key");
        mh_pass("opnpc1_erin_lost_key");

        mh_talk_finish(srv, npc_erin, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_trader_prog") == MH_TRADER_KEY,
                       "Erin hint must stay on got_key");
        mh_pass("opnpc1_erin_hint");
    }

    /* ---- Dig miss + hit + chest ---- */
    mh_clear_inv(player);
    mh_give(player, obj_key, 1);
    if( obj_spade > 0 )
        mh_give(player, obj_spade, 1);
    mh_set_bit(srv, "makinghistory_prog", MH_STARTED);
    mh_set_bit(srv, "makinghistory_trader_prog", MH_TRADER_KEY);
    mh_tele(srv, MH_DIG_MISS_X, MH_DIG_MISS_Z, 0);
    if( obj_spade > 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_spade, -1, -1);
        mh_finish(srv);
    }
    SELFTEST_CHECK(mh_inv_total(player, obj_chest) == 0, "dig miss must not unearth the chest");
    SELFTEST_CHECK(mh_get_bit(player, "makinghistory_trader_prog") == MH_TRADER_KEY,
                   "dig miss must stay on got_key");
    mh_pass("opheld1_spade_dig_miss");

    mh_tele(srv, MH_DIG_X, MH_DIG_Z, 0);
    if( obj_spade > 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_spade, -1, -1);
        mh_finish(srv);
    }
    SELFTEST_CHECK(mh_inv_total(player, obj_chest) == 1, "dig hit must unearth the chest");
    SELFTEST_CHECK(mh_get_bit(player, "makinghistory_trader_prog") == MH_TRADER_CHEST,
                   "dig hit must write dug_chest, got %d",
                   mh_get_bit(player, "makinghistory_trader_prog"));
    mh_pass("opheld1_spade_dig_hit");

    if( obj_key > 0 && obj_chest > 0 )
    {
        player->last_useitem = obj_chest;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_key, -1, -1);
        mh_finish(srv);
        player->last_useitem = -1;
    }
    SELFTEST_CHECK(mh_inv_total(player, obj_journal) == 1, "key-on-chest must yield the journal");
    SELFTEST_CHECK(mh_get_bit(player, "makinghistory_trader_prog") == MH_TRADER_JOURNAL,
                   "chest open must write got_journal, got %d",
                   mh_get_bit(player, "makinghistory_trader_prog"));
    mh_pass("opheldu_key_chest");

    /* ---- Dron too-early / Blanin / every riddle fail + pass ---- */
    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_dron, MH_DRON_X, MH_DRON_Z, 0);
    if( slot >= 0 )
    {
        mh_set_prereqs(player);
        mh_set_bit(srv, "makinghistory_prog", MH_STARTED);
        mh_set_bit(srv, "makinghistory_warr_prog", MH_WARR_NONE);
        mh_talk_finish(srv, npc_dron, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_NONE,
                       "Dron before Blanin must refuse");
        mh_pass("opnpc1_dron_too_early");
    }

    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_blanin, MH_BLANIN_X, MH_BLANIN_Z, 0);
    if( slot >= 0 )
    {
        mh_set_bit(srv, "makinghistory_prog", MH_STARTED);
        mh_set_bit(srv, "makinghistory_warr_prog", MH_WARR_NONE);
        mh_talk_finish(srv, npc_blanin, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "Blanin briefing must write talked_blanin, got %d",
                       mh_get_bit(player, "makinghistory_warr_prog"));
        mh_pass("opnpc1_blanin_briefing");

        mh_talk_finish(srv, npc_blanin, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "Blanin reminder must stay talked_blanin");
        mh_pass("opnpc1_blanin_reminder");
    }

    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_dron, MH_DRON_X, MH_DRON_Z, 0);
    if( slot >= 0 )
    {
        mh_set_bit(srv, "makinghistory_prog", MH_STARTED);
        mh_set_bit(srv, "makinghistory_warr_prog", MH_WARR_BLANIN);
        {
            static const int k_dron_no[] = { 2 };

            mh_talk_rows(srv, npc_dron, slot, k_dron_no, 1);
        }
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "Dron intro refuse must not complete");
        mh_pass("opnpc1_dron_intro_refuse");

        mh_dron_fail_at(srv, npc_dron, slot, 0, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "weapon fail must not complete");
        mh_pass("opnpc1_dron_weapon_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 1, 2);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "meal/rats fail must not complete");
        mh_pass("opnpc1_dron_meal_rats_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 2, 3);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "meal/kittens fail must not complete");
        mh_pass("opnpc1_dron_meal_kittens_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 3, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "tea fail must not complete");
        mh_pass("opnpc1_dron_tea_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 4, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "spider fail must not complete");
        mh_pass("opnpc1_dron_spider_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 5, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "age years fail must not complete");
        mh_pass("opnpc1_dron_age_years_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 6, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "age months fail must not complete");
        mh_pass("opnpc1_dron_age_months_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 7, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "battles fail must not complete");
        mh_pass("opnpc1_dron_battles_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 8, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "house fail must not complete");
        mh_pass("opnpc1_dron_house_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 9, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "brother fail must not complete");
        mh_pass("opnpc1_dron_brother_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 10, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "cat fail must not complete");
        mh_pass("opnpc1_dron_cat_fail");

        mh_dron_fail_at(srv, npc_dron, slot, 11, 1);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_BLANIN,
                       "arithmetic fail must not complete");
        mh_pass("opnpc1_dron_arithmetic_fail");

        mh_talk_rows(srv, npc_dron, slot, k_dron_pass, 13);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_warr_prog") == MH_WARR_DONE,
                       "all Dron answers must write warr_complete, got %d",
                       mh_get_bit(player, "makinghistory_warr_prog"));
        mh_pass("opnpc1_dron_riddle_pass");
    }

    /* ---- Ghosts: ghostspeak fail / Droalak / Melina / scroll / farewell ---- */
    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_droalak, MH_DROALAK_X, MH_DROALAK_Z, 0);
    if( slot >= 0 )
    {
        mh_clear_inv(player);
        mh_set_prereqs(player);
        mh_set_bit(srv, "makinghistory_prog", MH_STARTED);
        mh_set_bit(srv, "makinghistory_ghost_prog", MH_GHOST_NONE);
        mh_set_bit(srv, "makinghistory_droalak_pres", 1);
        mh_talk_finish(srv, npc_droalak, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_ghost_prog") == MH_GHOST_NONE,
                       "Droalak without ghostspeak must not advance");
        mh_pass("opnpc1_droalak_ghostspeak_fail");

        if( obj_ghostspeak > 0 )
            mh_wear(player, MH_NECK, obj_ghostspeak);
        mh_talk_finish(srv, npc_droalak, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_ghost_prog") == MH_GHOST_DROALAK,
                       "Droalak intro must write talked_droalak, got %d",
                       mh_get_bit(player, "makinghistory_ghost_prog"));
        mh_pass("opnpc1_droalak_intro");

        mh_talk_finish(srv, npc_droalak, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_ghost_prog") == MH_GHOST_DROALAK,
                       "Droalak reminder must stay on Melina");
        mh_pass("opnpc1_droalak_ask_melina");
    }

    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_melina, MH_MELINA_X, MH_MELINA_Z, 0);
    if( slot >= 0 )
    {
        mh_clear_inv(player);
        mh_set_bit(srv, "makinghistory_prog", MH_STARTED);
        mh_set_bit(srv, "makinghistory_ghost_prog", MH_GHOST_DROALAK);
        mh_set_bit(srv, "makinghistory_melina_pres", 1);
        mh_talk_finish(srv, npc_melina, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_ghost_prog") == MH_GHOST_DROALAK,
                       "Melina without ghostspeak must not advance");
        mh_pass("opnpc1_melina_ghostspeak_fail");

        if( obj_ghostspeak > 0 )
            mh_wear(player, MH_NECK, obj_ghostspeak);
        mh_talk_finish(srv, npc_melina, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_ghost_prog") == MH_GHOST_DROALAK,
                       "Melina without the sapphire amulet must not forgive");
        mh_pass("opnpc1_melina_no_amulet");

        if( obj_sapphire > 0 )
            mh_give(player, obj_sapphire, 1);
        mh_talk_finish(srv, npc_melina, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_ghost_prog") == MH_GHOST_MELINA,
                       "Melina with the amulet must write melina_done, got %d",
                       mh_get_bit(player, "makinghistory_ghost_prog"));
        mh_pass("opnpc1_melina_forgive");
    }

    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_droalak, MH_DROALAK_X, MH_DROALAK_Z, 0);
    if( slot >= 0 )
    {
        if( obj_ghostspeak > 0 )
            mh_wear(player, MH_NECK, obj_ghostspeak);
        mh_set_bit(srv, "makinghistory_ghost_prog", MH_GHOST_MELINA);
        mh_talk_finish(srv, npc_droalak, slot);
        SELFTEST_CHECK(mh_inv_total(player, obj_scroll) == 1, "Droalak must hand the scroll");
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_ghost_prog") == MH_GHOST_SCROLL,
                       "scroll hand-over must write got_scroll, got %d",
                       mh_get_bit(player, "makinghistory_ghost_prog"));
        mh_pass("opnpc1_droalak_scroll");

        mh_clear_inv(player);
        if( obj_ghostspeak > 0 )
            mh_wear(player, MH_NECK, obj_ghostspeak);
        mh_talk_finish(srv, npc_droalak, slot);
        SELFTEST_CHECK(mh_inv_total(player, obj_scroll) == 1, "lost scroll must be replaced");
        mh_pass("opnpc1_droalak_lost_scroll");
    }

    /* ---- Jorral hand-in / Lathas / finish / complete ---- */
    mh_clear_inv(player);
    mh_give(player, obj_journal, 1);
    mh_give(player, obj_scroll, 1);
    mh_set_bit(srv, "makinghistory_prog", MH_STARTED);
    mh_set_bit(srv, "makinghistory_trader_prog", MH_TRADER_JOURNAL);
    mh_set_bit(srv, "makinghistory_warr_prog", MH_WARR_DONE);
    mh_set_bit(srv, "makinghistory_ghost_prog", MH_GHOST_SCROLL);
    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_jorral, MH_JORRAL_X, MH_JORRAL_Z, 0);
    if( slot >= 0 )
    {
        mh_talk_finish(srv, npc_jorral, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_CASTLE,
                       "hand-in must write castle, got %d",
                       mh_get_bit(player, "makinghistory_prog"));
        SELFTEST_CHECK(mh_inv_total(player, obj_letter1) == 1, "hand-in must give Jorral's letter");
        mh_pass("opnpc1_jorral_handin");
    }
    mh_journal(srv, "journal_castle");

    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_lathas, MH_LATHAS_X, MH_LATHAS_Z, 1);
    if( slot >= 0 )
    {
        mh_clear_inv(player);
        mh_set_bit(srv, "makinghistory_prog", MH_CASTLE);
        mh_talk_finish(srv, npc_lathas, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_CASTLE,
                       "Lathas without the letter must stay castle");
        mh_pass("opnpc1_lathas_no_letter");

        mh_give(player, obj_letter1, 1);
        mh_talk_finish(srv, npc_lathas, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_LATHAS_DONE,
                       "Lathas letter must write lathas_done, got %d",
                       mh_get_bit(player, "makinghistory_prog"));
        SELFTEST_CHECK(mh_inv_total(player, obj_letter2) == 1, "Lathas must return a letter");
        mh_pass("opnpc1_lathas_letter");
    }
    mh_journal(srv, "journal_lathas_done");

    mh_free_npc(srv, slot);
    slot = mh_spawn(srv, npc_jorral, MH_JORRAL_X, MH_JORRAL_Z, 0);
    if( slot >= 0 )
    {
        mh_set_bit(srv, "makinghistory_prog", MH_LATHAS_DONE);
        mh_clear_inv(player);
        mh_talk_finish(srv, npc_jorral, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_LATHAS_DONE,
                       "finish without the king's letter must stay lathas_done");
        mh_pass("opnpc1_jorral_finish_no_letter");

        mh_give(player, obj_letter2, 1);
        craft_before = 0;
        pray_before = 0;
        if( stat_craft >= 0 )
            craft_before = player->stat_xp_tenths[stat_craft];
        if( stat_pray >= 0 )
            pray_before = player->stat_xp_tenths[stat_pray];
        mh_talk_finish(srv, npc_jorral, slot);
        {
            int t;

            for( t = 0; t < 8; t++ )
                selftest_tick(srv);
        }
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_prog") == MH_COMPLETE,
                       "Jorral finish must complete Making History, got %d",
                       mh_get_bit(player, "makinghistory_prog"));
        if( stat_craft >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >=
                               craft_before + MH_REWARD_CRAFT_TENTHS,
                           "complete must advance crafting by 10000 tenths");
        if( stat_pray >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_pray] >=
                               pray_before + MH_REWARD_PRAY_TENTHS,
                           "complete must advance prayer by 10000 tenths");
        if( obj_coins > 0 )
            SELFTEST_CHECK(mh_inv_total(player, obj_coins) >= MH_REWARD_COINS,
                           "complete must grant 750 coins");
        SELFTEST_CHECK(mh_inv_total(player, obj_key) >= 1, "complete must grant the enchanted key");
        mh_pass("opnpc1_jorral_finish_complete");

        mh_talk_finish(srv, npc_jorral, slot);
        mh_pass("opnpc1_jorral_post_complete");
    }

    mh_journal(srv, "journal_complete");

    /* Droalak farewell after castle (scroll already handed in). */
    mh_free_npc(srv, slot);
    mh_set_bit(srv, "makinghistory_prog", MH_CASTLE);
    mh_set_bit(srv, "makinghistory_ghost_prog", MH_GHOST_SCROLL);
    mh_set_bit(srv, "makinghistory_droalak_pres", 1);
    if( obj_ghostspeak > 0 )
        mh_wear(player, MH_NECK, obj_ghostspeak);
    slot = mh_spawn(srv, npc_droalak, MH_DROALAK_X, MH_DROALAK_Z, 0);
    if( slot >= 0 )
    {
        mh_talk_finish(srv, npc_droalak, slot);
        SELFTEST_CHECK(mh_get_bit(player, "makinghistory_ghost_prog") == MH_GHOST_FAREWELL,
                       "Droalak after castle must farewell, got %d",
                       mh_get_bit(player, "makinghistory_ghost_prog"));
        mh_pass("opnpc1_droalak_farewell");
    }

    mh_free_npc(srv, slot);

    fprintf(stderr, "ToriRSServer makinghistory selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_MAKINGHISTORY_SELFTEST_U_H */
