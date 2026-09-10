#ifndef TORIRSSERVER_TEST_QUEST_RAGANDBONEMAN_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_RAGANDBONEMAN_SELFTEST_U_H

/* Rag and Bone Man I Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Odd Old Man / Fortunato / goblin /
 * pot-boiler locs cannot leak. Real OPNPC1 / OPNPC3 / OPHELDU / OPLOC1 /
 * OPLOCU / AI_QUEUE3 on the authored path. player->godmode = 1 for the
 * whole walk (not a death test).
 *
 * Odd Old Man is reached through the existing `[opnpc1,rag_odd_old_man]`
 * (Bone Voyage charm talk stays on that same trigger). Fortunato is
 * reached through the existing `[opnpc1,rag_wine_merchant]` /
 * `[opnpc3,rag_wine_merchant]`. Quest-bone drops use the already-spliced
 * `~rag_try_quest_bone` helper. No second trigger is added.
 *
 * Gate: TORIRSSERVER_SELFTEST_RAGBONE_ONLY=1
 */

#define RB_NOT_STARTED 0
#define RB_COLLECTING 1
#define RB_COMPLETE 4

#define RB_BOILER_NOLOGS 1
#define RB_BOILER_NOPOT 2
#define RB_BOILER_WITH_POT 3
#define RB_BOILER_ONFIRE 4
#define RB_BOILER_BOILED 5

#define RB_KIND_NONE 0
#define RB_KIND_GOBLIN 1

#define RB_BIT_GOBLIN 0
#define RB_BIT_BEAR 1
#define RB_BIT_FROG 2
#define RB_BIT_RAM 3
#define RB_BIT_UNICORN 4
#define RB_BIT_MONKEY 5
#define RB_BIT_GIANTRAT 6
#define RB_BIT_GIANTBAT 7

#define RB_REWARD_COOK_TENTHS 5000
#define RB_REWARD_PRAY_TENTHS 5000

#define RB_OOM_X 3360
#define RB_OOM_Z 3506
#define RB_BOILER_X 3361
#define RB_BOILER_Z 3505
#define RB_FORT_X 3080
#define RB_FORT_Z 3250

static void
rb_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "RB PASS: %s\n", step);
}

static void
rb_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
rb_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
rb_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 8) <= 0 )
            selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
}

static void
rb_ticks(struct ToriRSServer* srv, int n)
{
    int i;

    assert(srv);
    for( i = 0; i < n; i++ )
        selftest_tick(srv);
}

static int
rb_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
rb_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = rb_chatmenu();
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
rb_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = rb_chatmenu();
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
rb_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    rb_god(player);
    selftest_tick(srv);
}

static int
rb_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    rb_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
rb_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
rb_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
rb_set_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    assert(srv->active_player);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        srv->active_player->varps[varp] = value;
}

static int
rb_get_varp(const struct ToriRSServerPlayer* player, const char* name)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp < 0 )
        return 0;
    return player->varps[varp];
}

static void
rb_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( rb_inv_total(player, obj_id) >= count )
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
rb_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
            inv_set(player, s, obj_id, 1);
    }
}

static void
rb_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
rb_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
rb_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
rb_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    rb_talk(srv, npc_type, slot);
    rb_finish(srv);
}

static void
rb_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    rb_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        rb_click_until_menu(srv, 24);
        rb_pick_row(srv, rows[i]);
    }
    rb_finish(srv);
}

static void
rb_opnpc(struct ToriRSServer* srv, int trigger, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, trigger, npc_type, -1, slot);
    rb_finish(srv);
}

static int
rb_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    rb_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
rb_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    rb_finish(srv);
}

static void
rb_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    rb_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
rb_use_loc_ticks(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id, int ticks)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    rb_ticks(srv, ticks);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
rb_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    rb_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
rb_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    rb_set_bit(srv, "fossilquest_progress", 0);
    rb_set_varp(srv, "rag_quest", RB_NOT_STARTED);
    rb_set_varp(srv, "rag_submit", 0);
    rb_set_bit(srv, "rag_boiler", RB_BOILER_NOLOGS);
    rb_set_bit(srv, "rag_potboiler", RB_KIND_NONE);
}

static int
rb_submit_has(struct ToriRSServerPlayer* player, int bit)
{
    int submit;

    assert(player);
    submit = rb_get_varp(player, "rag_submit");
    return (submit & (1 << bit)) != 0;
}

static void
rb_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,ragandboneman_journal]", NULL, 0);
    rb_finish(srv);
    rb_pass(step);
}

static int
rb_ground_count(struct ToriRSServer* srv, int obj_id, int x, int z)
{
    int n = 0;
    int i;

    assert(srv);
    if( obj_id <= 0 )
        return 0;
    for( i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
    {
        if( srv->ground[i].active && srv->ground[i].obj_id == obj_id &&
            srv->ground[i].x == x && srv->ground[i].z == z )
            n++;
    }
    return n;
}

static void
selftest_quest_ragandboneman(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_oom;
    int npc_fort;
    int npc_goblin;
    int loc_nologs;
    int loc_nopot;
    int loc_with_pot;
    int loc_boiled;
    int obj_vinegar;
    int obj_pot;
    int obj_pot_vin;
    int obj_jug;
    int obj_logs;
    int obj_tinder;
    int obj_coins;
    int obj_sword;
    int obj_goblin_bone;
    int obj_pot_goblin;
    int obj_pol_goblin;
    int obj_pol_bear;
    int obj_pol_frog;
    int obj_pol_ram;
    int obj_pol_unicorn;
    int obj_pol_monkey;
    int obj_pol_rat;
    int obj_pol_bat;
    int stat_cook;
    int stat_pray;
    int slot;
    int loc_slot;
    int cook_before;
    int pray_before;
    static const int k_oom_luck[] = { 2 };
    static const int k_oom_mumble_luck[] = { 3, 2 };
    static const int k_oom_no[] = { 1, 2 };
    static const int k_oom_yes[] = { 1, 1 };
    static const int k_fort_selling[] = { 1 };
    static const int k_fort_decline[] = { 2 };
    static const int k_fort_vinegar[] = { 2 };
    static const int k_fort_collect_decline[] = { 3 };
    static const int k_fort_buy[] = { 2, 1 };
    static const int k_fort_later[] = { 2, 2 };

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: rag and bone man I critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer ragbone selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    rb_god(player);
    rb_reset_quest(srv);
    rb_clear_inv(player);

    npc_oom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rag_odd_old_man");
    npc_fort = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rag_wine_merchant");
    npc_goblin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "goblin");
    loc_nologs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rag_potboiler_nologs");
    loc_nopot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rag_potboiler_nopot");
    loc_with_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rag_potboiler_with_pot");
    loc_boiled = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rag_potboiler_with_pot_boiled");
    obj_vinegar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_vinegar");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    obj_pot_vin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_pot_vinegar");
    obj_jug = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "jug_empty");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    obj_goblin_bone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_goblin_bone");
    obj_pot_goblin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_pot_goblin_bone");
    obj_pol_goblin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_polished_goblin_bone");
    obj_pol_bear = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_polished_bear_bone");
    obj_pol_frog = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_polished_medium_frog_bone");
    obj_pol_ram = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_polished_ram_bone");
    obj_pol_unicorn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_polished_unicorn_bone");
    obj_pol_monkey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_polished_monkey_bone");
    obj_pol_rat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_polished_giant_rat_bone");
    obj_pol_bat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rag_polished_giant_bat_bone");
    stat_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
    stat_pray = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "rag_quest") >= 0,
                   "varp rag_quest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "rag_submit") >= 0,
                   "varp rag_submit should resolve");
    SELFTEST_CHECK(npc_oom > 0, "npc rag_odd_old_man should resolve");
    SELFTEST_CHECK(npc_fort > 0, "npc rag_wine_merchant should resolve");
    SELFTEST_CHECK(obj_vinegar > 0, "obj rag_vinegar should resolve");
    SELFTEST_CHECK(obj_pol_goblin > 0, "obj rag_polished_goblin_bone should resolve");

    rb_journal(srv, "journal_0_not_started");

    slot = rb_spawn(srv, npc_oom, RB_OOM_X, RB_OOM_Z, 0);
    if( slot >= 0 )
    {
        rb_talk_rows(srv, npc_oom, slot, k_oom_luck, 1);
        SELFTEST_CHECK(rb_get_varp(player, "rag_quest") == RB_NOT_STARTED,
                       "Odd Old Man good-luck refuse must stay not_started");
        rb_pass("opnpc1_oom_refuse_good_luck");

        rb_talk_rows(srv, npc_oom, slot, k_oom_mumble_luck, 2);
        SELFTEST_CHECK(rb_get_varp(player, "rag_quest") == RB_NOT_STARTED,
                       "Odd Old Man mumble then luck refuse must stay not_started");
        rb_pass("opnpc1_oom_mumble_then_refuse");

        rb_talk_rows(srv, npc_oom, slot, k_oom_no, 2);
        SELFTEST_CHECK(rb_get_varp(player, "rag_quest") == RB_NOT_STARTED,
                       "Odd Old Man No refuse must stay not_started");
        rb_pass("opnpc1_oom_refuse_no");

        rb_talk_rows(srv, npc_oom, slot, k_oom_yes, 2);
        SELFTEST_CHECK(rb_get_varp(player, "rag_quest") == RB_COLLECTING,
                       "Odd Old Man accept must write collecting, got %d",
                       rb_get_varp(player, "rag_quest"));
        rb_pass("opnpc1_oom_accept_start");

        rb_talk_finish(srv, npc_oom, slot);
        SELFTEST_CHECK(rb_get_varp(player, "rag_quest") == RB_COLLECTING,
                       "collecting talk with no polished bones must stay collecting");
        rb_pass("opnpc1_oom_collecting_talk");
    }
    rb_journal(srv, "journal_1_collecting");

    rb_free_npc(srv, slot);
    slot = rb_spawn(srv, npc_fort, RB_FORT_X, RB_FORT_Z, 0);
    if( slot >= 0 )
    {
        rb_reset_quest(srv);
        rb_clear_inv(player);
        rb_talk_rows(srv, npc_fort, slot, k_fort_decline, 1);
        rb_pass("opnpc1_fortunato_not_collecting_decline");

        rb_talk_rows(srv, npc_fort, slot, k_fort_selling, 1);
        rb_pass("opnpc1_fortunato_not_collecting_shop");

        rb_opnpc(srv, SS_TRIGGER_OPNPC3, npc_fort, slot);
        rb_pass("opnpc3_fortunato_not_collecting");

        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_talk_rows(srv, npc_fort, slot, k_fort_collect_decline, 1);
        rb_pass("opnpc1_fortunato_collecting_decline");

        rb_clear_inv(player);
        rb_talk_rows(srv, npc_fort, slot, k_fort_vinegar, 1);
        SELFTEST_CHECK(rb_inv_total(player, obj_vinegar) == 0,
                       "Fortunato vinegar with no coins must not grant a jug");
        rb_pass("opnpc1_fortunato_no_coins");

        if( obj_pot > 0 )
        {
            rb_clear_inv(player);
            if( obj_coins > 0 )
                rb_give(player, obj_coins, 10);
            rb_fill_inv(player, obj_pot);
            rb_talk_rows(srv, npc_fort, slot, k_fort_vinegar, 1);
            SELFTEST_CHECK(rb_inv_total(player, obj_vinegar) == 0,
                           "Fortunato vinegar with a full pack must not grant a jug");
            rb_pass("opnpc1_fortunato_pack_full");
        }

        rb_clear_inv(player);
        if( obj_coins > 0 )
            rb_give(player, obj_coins, 10);
        rb_talk_rows(srv, npc_fort, slot, k_fort_later, 2);
        SELFTEST_CHECK(rb_inv_total(player, obj_vinegar) == 0,
                       "Fortunato Maybe later must not grant a jug");
        rb_pass("opnpc1_fortunato_decline_buy");

        rb_clear_inv(player);
        if( obj_coins > 0 )
            rb_give(player, obj_coins, 10);
        rb_talk_rows(srv, npc_fort, slot, k_fort_buy, 2);
        SELFTEST_CHECK(rb_inv_total(player, obj_vinegar) == 1,
                       "Fortunato 1gp buy must grant rag_vinegar");
        if( obj_coins > 0 )
            SELFTEST_CHECK(rb_inv_total(player, obj_coins) == 9,
                           "Fortunato 1gp buy must take one coin, left %d",
                           rb_inv_total(player, obj_coins));
        rb_pass("opnpc1_fortunato_buy_1gp");

        rb_clear_inv(player);
        if( obj_coins > 0 )
            rb_give(player, obj_coins, 10);
        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_opnpc(srv, SS_TRIGGER_OPNPC3, npc_fort, slot);
        rb_pass("opnpc3_fortunato_trade_op");
    }

    if( obj_vinegar > 0 && obj_pot > 0 && obj_pot_vin > 0 )
    {
        rb_clear_inv(player);
        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_give(player, obj_vinegar, 1);
        rb_give(player, obj_pot, 1);
        rb_opheldu(srv, obj_vinegar, obj_pot);
        SELFTEST_CHECK(rb_inv_total(player, obj_pot_vin) == 1,
                       "vinegar on empty pot must make rag_pot_vinegar");
        SELFTEST_CHECK(rb_inv_total(player, obj_vinegar) == 0,
                       "pour must consume the vinegar jug");
        if( obj_jug > 0 )
            SELFTEST_CHECK(rb_inv_total(player, obj_jug) == 1,
                           "pour must leave an empty jug");
        rb_pass("opheldu_pour_vinegar");

        if( obj_sword > 0 )
        {
            rb_clear_inv(player);
            rb_give(player, obj_vinegar, 1);
            rb_give(player, obj_sword, 1);
            rb_opheldu(srv, obj_vinegar, obj_sword);
            SELFTEST_CHECK(rb_inv_total(player, obj_pot_vin) == 0,
                           "vinegar on a sword must not make a pot of vinegar");
            rb_pass("opheldu_vinegar_wrong_item");
        }
    }

    if( obj_pot_vin > 0 && obj_goblin_bone > 0 && obj_pot_goblin > 0 )
    {
        rb_clear_inv(player);
        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_give(player, obj_pot_vin, 1);
        rb_give(player, obj_goblin_bone, 1);
        rb_opheldu(srv, obj_pot_vin, obj_goblin_bone);
        SELFTEST_CHECK(rb_inv_total(player, obj_pot_goblin) == 1,
                       "goblin bone in vinegar pot must make rag_pot_goblin_bone");
        SELFTEST_CHECK(rb_inv_total(player, obj_goblin_bone) == 0,
                       "add-bone must consume the raw bone");
        rb_pass("opheldu_add_goblin_bone");

        if( obj_sword > 0 )
        {
            rb_clear_inv(player);
            rb_give(player, obj_pot_vin, 1);
            rb_give(player, obj_sword, 1);
            rb_opheldu(srv, obj_pot_vin, obj_sword);
            SELFTEST_CHECK(rb_inv_total(player, obj_pot_goblin) == 0,
                           "sword in a vinegar pot must not make a bone pot");
            rb_pass("opheldu_pot_wrong_item");
        }
    }

    if( loc_nologs >= 0 && obj_logs > 0 )
    {
        rb_clear_inv(player);
        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_set_bit(srv, "rag_boiler", RB_BOILER_NOLOGS);
        rb_give(player, obj_logs, 1);
        loc_slot = rb_place_loc(srv, loc_nologs, RB_BOILER_X, RB_BOILER_Z, 0);
        if( loc_slot >= 0 )
        {
            rb_use_loc(srv, loc_nologs, loc_slot, obj_logs);
            SELFTEST_CHECK(rb_get_bit(player, "rag_boiler") == RB_BOILER_NOPOT,
                           "logs on nologs boiler must write nopot, got %d",
                           rb_get_bit(player, "rag_boiler"));
            SELFTEST_CHECK(rb_inv_total(player, obj_logs) == 0, "add-logs must consume the logs");
            rb_pass("oplocu_boiler_add_logs");
        }

        if( obj_sword > 0 )
        {
            rb_set_bit(srv, "rag_boiler", RB_BOILER_NOLOGS);
            rb_give(player, obj_sword, 1);
            loc_slot = rb_place_loc(srv, loc_nologs, RB_BOILER_X, RB_BOILER_Z, 0);
            if( loc_slot >= 0 )
            {
                rb_use_loc(srv, loc_nologs, loc_slot, obj_sword);
                SELFTEST_CHECK(rb_get_bit(player, "rag_boiler") == RB_BOILER_NOLOGS,
                               "sword on nologs boiler must leave nologs");
                rb_pass("oplocu_boiler_no_logs");
            }
        }
    }

    if( loc_nopot >= 0 && obj_logs > 0 )
    {
        rb_clear_inv(player);
        rb_set_bit(srv, "rag_boiler", RB_BOILER_NOPOT);
        rb_give(player, obj_logs, 1);
        loc_slot = rb_place_loc(srv, loc_nopot, RB_BOILER_X, RB_BOILER_Z, 0);
        if( loc_slot >= 0 )
        {
            rb_use_loc(srv, loc_nopot, loc_slot, obj_logs);
            SELFTEST_CHECK(rb_get_bit(player, "rag_boiler") == RB_BOILER_NOPOT,
                           "second logs must leave nopot");
            SELFTEST_CHECK(rb_inv_total(player, obj_logs) == 1,
                           "already-logs refuse must not consume the logs");
            rb_pass("oplocu_boiler_already_logs");
        }
    }

    if( loc_nopot >= 0 && obj_pot_goblin > 0 )
    {
        rb_clear_inv(player);
        rb_set_bit(srv, "rag_boiler", RB_BOILER_NOPOT);
        rb_set_bit(srv, "rag_potboiler", RB_KIND_NONE);
        rb_give(player, obj_pot_goblin, 1);
        loc_slot = rb_place_loc(srv, loc_nopot, RB_BOILER_X, RB_BOILER_Z, 0);
        if( loc_slot >= 0 )
        {
            rb_use_loc(srv, loc_nopot, loc_slot, obj_pot_goblin);
            SELFTEST_CHECK(rb_get_bit(player, "rag_boiler") == RB_BOILER_WITH_POT,
                           "pot on nopot boiler must write with_pot, got %d",
                           rb_get_bit(player, "rag_boiler"));
            SELFTEST_CHECK(rb_get_bit(player, "rag_potboiler") == RB_KIND_GOBLIN,
                           "goblin pot must write kind goblin");
            SELFTEST_CHECK(rb_inv_total(player, obj_pot_goblin) == 0,
                           "add-pot must consume the pot");
            rb_pass("oplocu_boiler_add_pot");
        }

        if( loc_with_pot >= 0 )
        {
            loc_slot = rb_place_loc(srv, loc_with_pot, RB_BOILER_X, RB_BOILER_Z, 0);
            if( loc_slot >= 0 )
            {
                rb_set_bit(srv, "rag_boiler", RB_BOILER_WITH_POT);
                rb_set_bit(srv, "rag_potboiler", RB_KIND_GOBLIN);
                rb_oploc(srv, loc_with_pot, loc_slot);
                SELFTEST_CHECK(rb_get_bit(player, "rag_boiler") == RB_BOILER_NOPOT,
                               "Remove-Pot must return nopot, got %d",
                               rb_get_bit(player, "rag_boiler"));
                SELFTEST_CHECK(rb_inv_total(player, obj_pot_goblin) == 1,
                               "Remove-Pot must return the pot");
                rb_pass("oploc1_boiler_remove_pot");
            }
        }
    }

    if( loc_with_pot >= 0 && obj_tinder > 0 && obj_pot_goblin > 0 )
    {
        rb_clear_inv(player);
        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_set_bit(srv, "rag_boiler", RB_BOILER_WITH_POT);
        rb_set_bit(srv, "rag_potboiler", RB_KIND_GOBLIN);
        rb_give(player, obj_tinder, 1);
        loc_slot = rb_place_loc(srv, loc_with_pot, RB_BOILER_X, RB_BOILER_Z, 0);
        if( loc_slot >= 0 )
        {
            rb_use_loc_ticks(srv, loc_with_pot, loc_slot, obj_tinder, 8);
            SELFTEST_CHECK(rb_get_bit(player, "rag_boiler") == RB_BOILER_ONFIRE,
                           "tinderbox on with_pot must light the logs, got %d",
                           rb_get_bit(player, "rag_boiler"));
            rb_pass("oplocu_boiler_light");

            rb_ticks(srv, 24);
            SELFTEST_CHECK(rb_get_bit(player, "rag_boiler") == RB_BOILER_BOILED,
                           "20-tick boil must write boiled, got %d",
                           rb_get_bit(player, "rag_boiler"));
            rb_pass("softtimer_boiler_boiling");
        }
    }

    if( loc_boiled >= 0 && obj_pol_goblin > 0 )
    {
        rb_clear_inv(player);
        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_set_bit(srv, "rag_boiler", RB_BOILER_BOILED);
        rb_set_bit(srv, "rag_potboiler", RB_KIND_GOBLIN);
        loc_slot = rb_place_loc(srv, loc_boiled, RB_BOILER_X, RB_BOILER_Z, 0);
        if( loc_slot >= 0 )
        {
            rb_oploc(srv, loc_boiled, loc_slot);
            SELFTEST_CHECK(rb_inv_total(player, obj_pol_goblin) == 1,
                           "Remove-Bone must grant a polished goblin bone");
            if( obj_pot > 0 )
                SELFTEST_CHECK(rb_inv_total(player, obj_pot) == 1,
                               "Remove-Bone must return an empty pot");
            SELFTEST_CHECK(rb_get_bit(player, "rag_boiler") == RB_BOILER_NOLOGS,
                           "retrieve must reset boiler to nologs, got %d",
                           rb_get_bit(player, "rag_boiler"));
            rb_pass("oploc1_boiler_retrieve");
        }
    }

    rb_free_npc(srv, slot);
    slot = rb_spawn(srv, npc_oom, RB_OOM_X, RB_OOM_Z, 0);
    if( slot >= 0 && obj_pol_goblin > 0 )
    {
        rb_clear_inv(player);
        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_set_varp(srv, "rag_submit", 0);
        rb_give(player, obj_pol_goblin, 1);
        rb_talk_finish(srv, npc_oom, slot);
        SELFTEST_CHECK(rb_submit_has(player, RB_BIT_GOBLIN),
                       "submit one polished goblin bone must set the goblin bit");
        SELFTEST_CHECK(rb_inv_total(player, obj_pol_goblin) == 0,
                       "submit must take the polished goblin bone");
        SELFTEST_CHECK(rb_get_varp(player, "rag_quest") == RB_COLLECTING,
                       "one bone must not complete the quest");
        rb_pass("opnpc1_oom_submit_one");
    }

    if( slot >= 0 && obj_pol_goblin > 0 && obj_pol_bear > 0 && obj_pol_frog > 0 &&
        obj_pol_ram > 0 && obj_pol_unicorn > 0 && obj_pol_monkey > 0 &&
        obj_pol_rat > 0 && obj_pol_bat > 0 )
    {
        rb_clear_inv(player);
        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        rb_set_varp(srv, "rag_submit", (1 << RB_BIT_GOBLIN) | (1 << RB_BIT_BEAR) |
                                          (1 << RB_BIT_FROG) | (1 << RB_BIT_RAM) |
                                          (1 << RB_BIT_UNICORN) | (1 << RB_BIT_MONKEY) |
                                          (1 << RB_BIT_GIANTRAT));
        rb_give(player, obj_pol_bat, 1);
        cook_before = 0;
        pray_before = 0;
        if( stat_cook >= 0 )
            cook_before = player->stat_xp_tenths[stat_cook];
        if( stat_pray >= 0 )
            pray_before = player->stat_xp_tenths[stat_pray];
        rb_talk_finish(srv, npc_oom, slot);
        {
            int t;

            for( t = 0; t < 40; t++ )
            {
                ToriRSServer_WorldCloseModal(srv);
                selftest_tick(srv);
            }
        }
        SELFTEST_CHECK(rb_get_varp(player, "rag_quest") == RB_COMPLETE,
                       "last polished bone must complete the quest, got %d",
                       rb_get_varp(player, "rag_quest"));
        if( stat_cook >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_cook] >=
                               cook_before + RB_REWARD_COOK_TENTHS,
                           "complete must advance Cooking by 5000 tenths");
        if( stat_pray >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_pray] >=
                               pray_before + RB_REWARD_PRAY_TENTHS,
                           "complete must advance Prayer by 5000 tenths");
        rb_pass("opnpc1_oom_submit_last_complete");
    }
    rb_journal(srv, "journal_4_complete");

    if( slot >= 0 )
    {
        rb_talk_finish(srv, npc_oom, slot);
        SELFTEST_CHECK(rb_get_varp(player, "rag_quest") == RB_COMPLETE,
                       "already-complete talk must stay complete");
        rb_pass("opnpc1_oom_already_complete");
    }

    if( npc_goblin > 0 && obj_goblin_bone > 0 )
    {
        int gslot;
        int tile_x;
        int tile_z;
        int tries;
        int found;

        rb_free_npc(srv, slot);
        rb_reset_quest(srv);
        rb_clear_inv(player);
        selftest_clear_ground(srv);
        gslot = rb_spawn(srv, npc_goblin, RB_OOM_X, RB_OOM_Z, 0);
        if( gslot >= 0 )
        {
            tile_x = srv->npcs[gslot].x;
            tile_z = srv->npcs[gslot].z;
            ToriRSServer_WorldNpcDied(srv, gslot);
            SELFTEST_CHECK(rb_ground_count(srv, obj_goblin_bone, tile_x, tile_z) == 0,
                           "goblin must not drop a quest bone before collecting");
            rb_pass("ai_queue3_goblin_bone_gated");
        }

        rb_set_varp(srv, "rag_quest", RB_COLLECTING);
        found = 0;
        for( tries = 0; tries < 32 && !found; tries++ )
        {
            selftest_clear_ground(srv);
            gslot = rb_spawn(srv, npc_goblin, RB_OOM_X, RB_OOM_Z, 0);
            if( gslot < 0 )
                break;
            tile_x = srv->npcs[gslot].x;
            tile_z = srv->npcs[gslot].z;
            ToriRSServer_WorldNpcDied(srv, gslot);
            found = rb_ground_count(srv, obj_goblin_bone, tile_x, tile_z);
            rb_free_npc(srv, gslot);
        }
        SELFTEST_CHECK(found > 0,
                       "collecting goblin death must drop rag_goblin_bone within 32 rolls");
        rb_pass("ai_queue3_goblin_quest_bone");
        selftest_clear_ground(srv);
        slot = -1;
    }

    rb_free_npc(srv, slot);
    rb_clear_inv(player);
    rb_reset_quest(srv);
    fprintf(stderr, "ToriRSServer ragbone selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_RAGANDBONEMAN_SELFTEST_U_H */
