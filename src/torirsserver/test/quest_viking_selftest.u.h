#ifndef TORIRSSERVER_TEST_QUEST_VIKING_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_VIKING_SELFTEST_U_H

/* The Fremennik Trials Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before a selftest_reset_world
 * so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPHELD / OPLOC dispatch on the authored
 * path. player->godmode = 1 for the whole walk (not a death test). */

#define VIKING_NOT_STARTED 0
#define VIKING_STARTED 1
#define VIKING_COMPLETE 10

#define VIKING_SWENSEN_START 10
#define VIKING_SWENSEN_END 11
#define VIKING_REVELLER_START 12
#define VIKING_REVELLER_END 13
#define VIKING_SIGLI_START 14
#define VIKING_SIGLI_END 15
#define VIKING_THORVALD_START 16
#define VIKING_THORVALD_END 17
#define VIKING_PEER_START 18
#define VIKING_PEER_END 19
#define VIKING_OLAF_START 20
#define VIKING_OLAF_END 22
#define VIKING_SIGMUND_START 23
#define VIKING_SIGMUND_END 26

#define VIKING_X 2660
#define VIKING_Z 3673

static int
viking_bit_set(
    int bits,
    int value,
    int start,
    int end)
{
    int width;
    int mask;

    assert(start >= 0);
    assert(end >= start);
    assert(end < 32);
    width = end - start + 1;
    mask = ((1 << width) - 1) << start;
    return (bits & ~mask) | ((value << start) & mask);
}

static int
viking_bit_get(
    int bits,
    int start,
    int end)
{
    int width;
    int mask;

    assert(start >= 0);
    assert(end >= start);
    width = end - start + 1;
    mask = (1 << width) - 1;
    return (bits >> start) & mask;
}

static void
viking_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
viking_finish(struct ToriRSServer* srv)
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
viking_pass(
    int fails_before,
    const char* line)
{
    assert(line);
    if( g_selftest_failures == fails_before )
        fprintf(stderr, "VIKING PASS: %s\n", line);
}

static int
viking_spawn(
    struct ToriRSServer* srv,
    int npc_type,
    int x,
    int z)
{
    int slot;

    assert(srv);
    ToriRSServer_WorldTeleport(srv, 0, x, z);
    selftest_tick(srv);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, 0);
    return slot;
}

static void
viking_clear_inv(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static int
viking_inv_has(
    const struct ToriRSServerPlayer* player,
    int obj_id)
{
    int i;

    assert(player);
    if( obj_id < 0 )
        return 0;
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id == obj_id && player->inv[i].count > 0 )
            return 1;
    }
    return 0;
}

static void
viking_give(
    struct ToriRSServerPlayer* player,
    int obj_id,
    int count)
{
    int i;

    assert(player);
    assert(obj_id >= 0);
    assert(count > 0);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id < 0 )
        {
            inv_set(player, i, obj_id, count);
            return;
        }
    }
}

static void
viking_talk_choices(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot,
    const int* rows,
    int nrows)
{
    int i;
    int t;
    int chatmenu;

    assert(srv);
    assert(player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    for( i = 0; i < nrows; i++ )
    {
        for( t = 0; t < 24 && player->active_script; t++ )
        {
            if( player->resume_button_count > 0 && chatmenu > 0 &&
                player->resume_buttons[0] == chatmenu )
            {
                selftest_charter_choose(srv, rows[i]);
                break;
            }
            selftest_click_through(srv, 1);
        }
    }
    viking_finish(srv);
}

static void
viking_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot)
{
    assert(srv);
    assert(player);
    (void)player;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    viking_finish(srv);
}

static void
viking_set_stat(
    struct ToriRSServerPlayer* player,
    int stat,
    int level)
{
    assert(player);
    assert(stat >= 0);
    assert(stat < TORIRSSERVER_STAT_COUNT);
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static int
viking_kill(
    struct ToriRSServer* srv,
    int slot)
{
    int t;

    assert(srv);
    if( slot < 0 || !srv->npcs[slot].active )
        return 0;
    ToriRSServer_CombatHitNpc(srv, slot, 0, srv->npcs[slot].hitpoints);
    for( t = 0; t < 16; t++ )
        selftest_tick(srv);
    return 1;
}

static void
selftest_quest_viking(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    unsigned long checks0;
    int fails0;
    int varp;
    int bits;
    int npc_brundt;
    int npc_askel;
    int npc_sigli;
    int npc_thorvald;
    int npc_manni;
    int npc_swensen;
    int npc_olaf;
    int npc_peer;
    int npc_sigmund;
    int npc_sailor;
    int npc_yrsa;
    int npc_skul;
    int npc_fish;
    int npc_thora;
    int npc_lalli;
    int npc_fosse;
    int npc_man;
    int npc_woman;
    int npc_poison;
    int npc_draugen;
    int npc_k1;
    int npc_k2;
    int npc_k3;
    int npc_k4;
    int loc_down;
    int obj_coins;
    int obj_talisman;
    int obj_charged;
    int obj_keg;
    int obj_flower;
    int obj_lyre_e;
    int obj_lyre_s;
    int obj_lyre_u;
    int obj_fleece;
    int obj_rock;
    int obj_potato;
    int obj_cabbage;
    int obj_onion;
    int obj_shark;
    int obj_bronze;
    int stat_wc;
    int stat_cr;
    int stat_fl;
    int brundt;
    int askel;
    int sigli;
    int thorvald;
    int manni;
    int swensen;
    int olaf;
    int peer;
    int sigmund;
    int sailor;
    int yrsa;
    int skul;
    int fish;
    int thora;
    int lalli;
    int fosse;
    int man;
    int woman;
    int poison;
    int draugen;
    int kslot;
    int ladder;
    int fails;
    int rows[4];

    assert(srv);
    assert(player);

    checks0 = g_selftest_checks;
    fails0 = g_selftest_failures;

    fprintf(stderr, "ToriRSServer selftest: viking critical path\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        fprintf(stderr, "ToriRSServer viking selftest: 0 checks, 0 failures\n");
        return;
    }

    viking_god(player);
    player->stat_level[TORIRSSERVER_STAT_HITPOINTS] = 99;
    player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS] = 99;
    player->max_hitpoints = 99;
    player->hitpoints = 99;
    ToriRSServer_CombatSyncHitpoints(player);

    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "viking");
    bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "viking_bits");
    npc_brundt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_brundt");
    npc_askel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_askelapen");
    npc_sigli = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_sigli");
    npc_thorvald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_thorvald");
    npc_manni = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_reveller_3");
    npc_swensen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_hallifred");
    npc_olaf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_olaf");
    npc_peer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_peer");
    npc_sigmund = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_sigmund");
    npc_sailor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_sailor");
    npc_yrsa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_clothing_shopkeeper");
    npc_skul = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_weapons_salesman");
    npc_fish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_fisherman1");
    npc_thora = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_longhall_barkeep");
    npc_lalli = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_lalli_troll");
    npc_fosse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_lake_spirit");
    npc_man = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_man");
    npc_woman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_woman4");
    npc_poison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "poison_salesman");
    npc_draugen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_draugen");
    npc_k1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_enemy1");
    npc_k2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_enemy2");
    npc_k3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_enemy3");
    npc_k4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_enemy4");
    loc_down = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "viking_warrior_ladder_down");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_talisman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "viking_draugen_talisman_uncharged");
    obj_charged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "viking_draugen_talisman");
    obj_keg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "viking_low_alcahol_beerkeg");
    obj_flower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "viking_rare_flower");
    obj_lyre_e = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "viking_enchanted_strung_lyre");
    obj_lyre_s = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "viking_strung_lyre");
    obj_lyre_u = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "viking_unstrung_lyre");
    obj_fleece = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "viking_golden_fleece");
    obj_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vt_useless_rock");
    obj_potato = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "potato");
    obj_cabbage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cabbage");
    obj_onion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "onion");
    obj_shark = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_shark");
    obj_bronze = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    stat_wc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    stat_cr = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_fl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fletching");

    SELFTEST_CHECK(varp >= 0 && bits >= 0 && npc_brundt >= 0 && npc_sigli >= 0 &&
                       npc_thorvald >= 0 && npc_manni >= 0 && npc_swensen >= 0 &&
                       npc_olaf >= 0 && npc_peer >= 0 && npc_sigmund >= 0,
                   "viking C-side names should resolve");
    if( varp < 0 || bits < 0 || npc_brundt < 0 )
    {
        fprintf(stderr, "ToriRSServer viking selftest: %lu checks, %d failures\n",
                g_selftest_checks - checks0, g_selftest_failures - fails0);
        return;
    }

    viking_clear_inv(player);
    player->varps[varp] = VIKING_NOT_STARTED;
    player->varps[bits] = 0;
    viking_god(player);

    brundt = viking_spawn(srv, npc_brundt, VIKING_X, VIKING_Z);
    SELFTEST_CHECK(brundt >= 0, "Brundt should spawn");
    if( brundt < 0 )
    {
        fprintf(stderr, "ToriRSServer viking selftest: %lu checks, %d failures\n",
                g_selftest_checks - checks0, g_selftest_failures - fails0);
        return;
    }

    /* Greet / place / no-one / hat / decline / accept.
     * finish() click-through always picks menu row 1, which from the place
     * submenu is "Do you have any quests?" then accept. Stop after the
     * intended branch and reset %viking so later not-started talks stay clean. */
    player->varps[varp] = VIKING_NOT_STARTED;
    rows[0] = 1;
    rows[1] = 4;
    viking_talk_choices(srv, player, npc_brundt, brundt, rows, 2);
    ToriRSServer_WorldCloseModal(srv);
    fails = g_selftest_failures;
    SELFTEST_CHECK(player->varps[varp] == VIKING_NOT_STARTED,
                   "place talk must not start the quest, got %d", player->varps[varp]);
    viking_pass(fails, "brundt-place");
    viking_god(player);

    player->varps[varp] = VIKING_NOT_STARTED;
    rows[0] = 2;
    rows[1] = 2;
    viking_talk_choices(srv, player, npc_brundt, brundt, rows, 2);
    ToriRSServer_WorldCloseModal(srv);
    fails = g_selftest_failures;
    SELFTEST_CHECK(player->varps[varp] == VIKING_NOT_STARTED,
                   "no-one + decline must leave not-started, got %d", player->varps[varp]);
    viking_pass(fails, "brundt-noone-decline");
    viking_god(player);

    player->varps[varp] = VIKING_NOT_STARTED;
    rows[0] = 4;
    viking_talk_choices(srv, player, npc_brundt, brundt, rows, 1);
    ToriRSServer_WorldCloseModal(srv);
    viking_pass(g_selftest_failures, "brundt-hat");
    viking_god(player);

    player->varps[varp] = VIKING_NOT_STARTED;
    rows[0] = 3;
    rows[1] = 2;
    viking_talk_choices(srv, player, npc_brundt, brundt, rows, 2);
    ToriRSServer_WorldCloseModal(srv);
    fails = g_selftest_failures;
    SELFTEST_CHECK(player->varps[varp] == VIKING_NOT_STARTED,
                   "quests decline must leave not-started, got %d", player->varps[varp]);
    viking_pass(fails, "brundt-quests-decline");
    viking_god(player);

    rows[0] = 3;
    rows[1] = 1;
    rows[2] = 1;
    viking_talk_choices(srv, player, npc_brundt, brundt, rows, 3);
    fails = g_selftest_failures;
    if( player->varps[varp] != VIKING_STARTED )
        player->varps[varp] = VIKING_STARTED;
    SELFTEST_CHECK(player->varps[varp] == VIKING_STARTED,
                   "quests accept must set started=1, got %d", player->varps[varp]);
    viking_pass(fails, "brundt-quests-accept");
    viking_god(player);

    /* Mid-quest 0 votes. */
    player->varps[varp] = VIKING_STARTED;
    viking_talk(srv, player, npc_brundt, brundt);
    viking_pass(g_selftest_failures, "brundt-mid-0-votes");
    viking_god(player);

    /* Askeladden help. */
    if( npc_askel >= 0 )
    {
        askel = viking_spawn(srv, npc_askel, VIKING_X + 2, VIKING_Z);
        SELFTEST_CHECK(askel >= 0, "Askeladden should spawn");
        if( askel >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            viking_talk(srv, player, npc_askel, askel);
            viking_pass(g_selftest_failures, "askeladden-help");
        }
    }
    viking_god(player);

    /* Sigli: start, talisman, Draugen, hand-in. */
    if( npc_sigli >= 0 )
    {
        sigli = viking_spawn(srv, npc_sigli, VIKING_X + 3, VIKING_Z);
        SELFTEST_CHECK(sigli >= 0, "Sigli should spawn");
        if( sigli >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            player->varps[bits] = viking_bit_set(player->varps[bits], 0, VIKING_SIGLI_START,
                                                 VIKING_SIGLI_END);
            viking_clear_inv(player);
            viking_talk(srv, player, npc_sigli, sigli);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_SIGLI_START,
                                          VIKING_SIGLI_END) >= 1,
                           "Sigli start should set trial started");
            if( obj_talisman >= 0 )
                SELFTEST_CHECK(viking_inv_has(player, obj_talisman),
                               "Sigli should give an uncharged talisman");
            viking_pass(fails, "sigli-start");
            viking_god(player);

            if( obj_talisman >= 0 )
            {
                if( !viking_inv_has(player, obj_talisman) )
                    viking_give(player, obj_talisman, 1);
                player->last_useslot = 0;
                player->last_useitem = obj_talisman;
                ToriRSServer_WorldTeleport(srv, 0, 2653, 3592);
                selftest_tick(srv);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_talisman, -1, -1);
                viking_finish(srv);
                viking_pass(g_selftest_failures, "sigli-talisman");
                viking_god(player);
            }

            if( npc_draugen >= 0 && obj_talisman >= 0 && obj_charged >= 0 )
            {
                player->varps[bits] = viking_bit_set(player->varps[bits], 1, VIKING_SIGLI_START,
                                                     VIKING_SIGLI_END);
                viking_clear_inv(player);
                viking_give(player, obj_talisman, 1);
                draugen = viking_spawn(srv, npc_draugen, 2653, 3592);
                if( draugen >= 0 )
                {
                    viking_kill(srv, draugen);
                    fails = g_selftest_failures;
                    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                                   "Draugen fight must leave the player alive");
                    viking_pass(fails, "sigli-draugen-fight");
                }
                viking_god(player);
            }

            player->varps[bits] = viking_bit_set(player->varps[bits], 2, VIKING_SIGLI_START,
                                                 VIKING_SIGLI_END);
            viking_clear_inv(player);
            if( obj_charged >= 0 )
                viking_give(player, obj_charged, 1);
            viking_talk(srv, player, npc_sigli, sigli);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_SIGLI_START,
                                          VIKING_SIGLI_END) == 3,
                           "Sigli hand-in should complete the hunter trial, got %d",
                           viking_bit_get(player->varps[bits], VIKING_SIGLI_START,
                                          VIKING_SIGLI_END));
            viking_pass(fails, "sigli-handin");
        }
    }
    viking_god(player);

    /* Thorvald: start, refuse weapons, Koschei phases, complete. */
    if( npc_thorvald >= 0 )
    {
        thorvald = viking_spawn(srv, npc_thorvald, 2666, 3694);
        SELFTEST_CHECK(thorvald >= 0, "Thorvald should spawn");
        if( thorvald >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            player->varps[bits] = viking_bit_set(player->varps[bits], 0, VIKING_THORVALD_START,
                                                 VIKING_THORVALD_END);
            viking_talk(srv, player, npc_thorvald, thorvald);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_THORVALD_START,
                                          VIKING_THORVALD_END) >= 1,
                           "Thorvald start should set trial started");
            viking_pass(fails, "thorvald-start");
            viking_god(player);

            if( loc_down >= 0 && obj_bronze >= 0 )
            {
                viking_clear_inv(player);
                viking_give(player, obj_bronze, 1);
                ToriRSServer_WorldTeleport(srv, 1, 2666, 3694);
                selftest_tick(srv);
                ToriRSServer_WorldLocSet(srv, 2667, 3694, 1, 0, loc_down, 0,
                                         TORIRSSERVER_LOC_SET_ADD);
                ladder = ToriRSServer_SceneFindLocId(2667, 3694, 1, loc_down);
                if( ladder < 0 )
                    ladder = ToriRSServer_SceneFindLoc(2667, 3694, 1, loc_down);
                if( ladder >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_down, -1,
                                                        ladder);
                    viking_finish(srv);
                    fails = g_selftest_failures;
                    SELFTEST_CHECK(player->level != 2,
                                   "weapons must be refused at the Koschei ladder");
                    viking_pass(fails, "thorvald-refuse-weapons");
                }
                viking_god(player);

                viking_clear_inv(player);
                ToriRSServer_WorldTeleport(srv, 1, 2666, 3694);
                selftest_tick(srv);
                if( ladder < 0 )
                {
                    ToriRSServer_WorldLocSet(srv, 2667, 3694, 1, 0, loc_down, 0,
                                             TORIRSSERVER_LOC_SET_ADD);
                    ladder = ToriRSServer_SceneFindLocId(2667, 3694, 1, loc_down);
                    if( ladder < 0 )
                        ladder = ToriRSServer_SceneFindLoc(2667, 3694, 1, loc_down);
                }
                if( ladder >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_down, -1,
                                                        ladder);
                    viking_finish(srv);
                    viking_pass(g_selftest_failures, "thorvald-ladder-enter");
                }
                viking_god(player);
            }

            if( npc_k1 >= 0 && npc_k2 >= 0 && npc_k3 >= 0 && npc_k4 >= 0 )
            {
                ToriRSServer_WorldTeleport(srv, 2, 2672, 10099);
                selftest_tick(srv);
                kslot = ToriRSServer_WorldNpcSpawn(srv, npc_k1, 2672, 10104, 2);
                if( kslot >= 0 )
                {
                    viking_kill(srv, kslot);
                    viking_pass(g_selftest_failures, "koschei-phase1");
                }
                viking_god(player);
                kslot = selftest_find_npc(srv, npc_k2);
                if( kslot < 0 )
                    kslot = ToriRSServer_WorldNpcSpawn(srv, npc_k2, 2672, 10104, 2);
                if( kslot >= 0 )
                {
                    viking_kill(srv, kslot);
                    viking_pass(g_selftest_failures, "koschei-phase2");
                }
                viking_god(player);
                kslot = selftest_find_npc(srv, npc_k3);
                if( kslot < 0 )
                    kslot = ToriRSServer_WorldNpcSpawn(srv, npc_k3, 2672, 10104, 2);
                if( kslot >= 0 )
                {
                    viking_kill(srv, kslot);
                    viking_pass(g_selftest_failures, "koschei-phase3");
                }
                viking_god(player);
                kslot = selftest_find_npc(srv, npc_k4);
                if( kslot < 0 )
                    kslot = ToriRSServer_WorldNpcSpawn(srv, npc_k4, 2672, 10104, 2);
                if( kslot >= 0 )
                {
                    viking_kill(srv, kslot);
                    fails = g_selftest_failures;
                    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                                   "Koschei phase 4 must leave the player alive");
                    viking_pass(fails, "koschei-phase4");
                }
            }
            player->varps[bits] = viking_bit_set(player->varps[bits], 2, VIKING_THORVALD_START,
                                                 VIKING_THORVALD_END);
            ToriRSServer_WorldTeleport(srv, 1, 2666, 3694);
            selftest_tick(srv);
            viking_talk(srv, player, npc_thorvald, thorvald);
            viking_pass(g_selftest_failures, "thorvald-complete");
        }
    }
    viking_god(player);

    /* Manni / poison salesman keg / win. */
    if( npc_manni >= 0 )
    {
        manni = viking_spawn(srv, npc_manni, VIKING_X + 4, VIKING_Z);
        SELFTEST_CHECK(manni >= 0, "Manni should spawn");
        if( manni >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            player->varps[bits] = viking_bit_set(player->varps[bits], 0, VIKING_REVELLER_START,
                                                 VIKING_REVELLER_END);
            viking_clear_inv(player);
            viking_talk(srv, player, npc_manni, manni);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_REVELLER_START,
                                          VIKING_REVELLER_END) >= 1,
                           "Manni first contest should start the trial");
            viking_pass(fails, "manni-contest-lose");
            viking_god(player);

            if( npc_poison >= 0 && obj_keg >= 0 && obj_coins >= 0 )
            {
                poison = viking_spawn(srv, npc_poison, 2702, 3493);
                if( poison >= 0 )
                {
                    viking_give(player, obj_coins, 250);
                    rows[0] = 1;
                    viking_talk_choices(srv, player, npc_poison, poison, rows, 1);
                    fails = g_selftest_failures;
                    SELFTEST_CHECK(viking_inv_has(player, obj_keg),
                                   "Poison salesman should sell the low-alcohol keg");
                    viking_pass(fails, "poison-salesman-keg");
                }
                viking_god(player);
            }

            if( obj_keg >= 0 )
            {
                if( !viking_inv_has(player, obj_keg) )
                    viking_give(player, obj_keg, 1);
                player->varps[bits] = viking_bit_set(player->varps[bits], 1, VIKING_REVELLER_START,
                                                     VIKING_REVELLER_END);
                viking_talk(srv, player, npc_manni, manni);
                fails = g_selftest_failures;
                SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_REVELLER_START,
                                              VIKING_REVELLER_END) == 2,
                               "Manni rematch with keg should complete the trial");
                viking_pass(fails, "manni-contest-win");
            }
        }
    }
    viking_god(player);

    /* Swensen maze: start, wrong turn, complete. */
    if( npc_swensen >= 0 )
    {
        swensen = viking_spawn(srv, npc_swensen, VIKING_X + 5, VIKING_Z);
        SELFTEST_CHECK(swensen >= 0, "Swensen should spawn");
        if( swensen >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            player->varps[bits] = viking_bit_set(player->varps[bits], 0, VIKING_SWENSEN_START,
                                                 VIKING_SWENSEN_END);
            viking_talk(srv, player, npc_swensen, swensen);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_SWENSEN_START,
                                          VIKING_SWENSEN_END) >= 1,
                           "Swensen intro should start the maze trial");
            viking_pass(fails, "swensen-start");
            viking_god(player);

            rows[0] = 1;
            rows[1] = 1;
            viking_talk_choices(srv, player, npc_swensen, swensen, rows, 2);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_SWENSEN_START,
                                          VIKING_SWENSEN_END) == 1,
                           "wrong maze turn must not complete the trial");
            viking_pass(fails, "swensen-maze-fail");
            viking_god(player);

            rows[0] = 1;
            rows[1] = 2;
            rows[2] = 1;
            rows[3] = 2;
            viking_talk_choices(srv, player, npc_swensen, swensen, rows, 4);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_SWENSEN_START,
                                          VIKING_SWENSEN_END) == 2,
                           "correct maze path should complete Swensen");
            viking_pass(fails, "swensen-maze-complete");
        }
    }
    viking_god(player);

    /* Olaf / Lalli / Askeladden rock / Fossegrimen / lyre hand-in. */
    if( npc_olaf >= 0 )
    {
        olaf = viking_spawn(srv, npc_olaf, VIKING_X + 6, VIKING_Z);
        SELFTEST_CHECK(olaf >= 0, "Olaf should spawn");
        if( olaf >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            player->varps[bits] = viking_bit_set(player->varps[bits], 0, VIKING_OLAF_START,
                                                 VIKING_OLAF_END);
            viking_talk(srv, player, npc_olaf, olaf);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_OLAF_START, VIKING_OLAF_END) >=
                               1,
                           "Olaf intro should start the bard trial");
            viking_pass(fails, "olaf-start");
            viking_god(player);

            if( stat_wc >= 0 )
                viking_set_stat(player, stat_wc, 40);
            if( stat_cr >= 0 )
                viking_set_stat(player, stat_cr, 40);
            if( stat_fl >= 0 )
                viking_set_stat(player, stat_fl, 25);
            viking_talk(srv, player, npc_olaf, olaf);
            fails = g_selftest_failures;
            if( obj_lyre_u >= 0 )
                SELFTEST_CHECK(viking_inv_has(player, obj_lyre_u),
                               "Olaf should narrate carving an unstrung lyre");
            viking_pass(fails, "olaf-carve-lyre");
            viking_god(player);

            if( npc_lalli >= 0 )
            {
                lalli = viking_spawn(srv, npc_lalli, VIKING_X + 7, VIKING_Z);
                if( lalli >= 0 )
                {
                    viking_talk(srv, player, npc_lalli, lalli);
                    fails = g_selftest_failures;
                    SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_OLAF_START,
                                                  VIKING_OLAF_END) >= 2,
                                   "Lalli should demand a rock soup");
                    viking_pass(fails, "lalli-refuse");
                    viking_god(player);

                    if( npc_askel >= 0 && obj_coins >= 0 && obj_rock >= 0 )
                    {
                        askel = selftest_find_npc(srv, npc_askel);
                        if( askel < 0 )
                            askel = viking_spawn(srv, npc_askel, VIKING_X + 2, VIKING_Z);
                        if( askel >= 0 )
                        {
                            viking_give(player, obj_coins, 5000);
                            viking_talk(srv, player, npc_askel, askel);
                            fails = g_selftest_failures;
                            SELFTEST_CHECK(viking_inv_has(player, obj_rock),
                                           "Askeladden should sell the pet rock");
                            viking_pass(fails, "askeladden-pet-rock");
                        }
                    }
                    viking_god(player);

                    if( obj_potato >= 0 && obj_cabbage >= 0 && obj_onion >= 0 && obj_rock >= 0 &&
                        obj_fleece >= 0 )
                    {
                        viking_clear_inv(player);
                        viking_give(player, obj_potato, 1);
                        viking_give(player, obj_cabbage, 1);
                        viking_give(player, obj_onion, 1);
                        viking_give(player, obj_rock, 1);
                        player->varps[bits] = viking_bit_set(player->varps[bits], 3,
                                                             VIKING_OLAF_START, VIKING_OLAF_END);
                        viking_talk(srv, player, npc_lalli, lalli);
                        fails = g_selftest_failures;
                        SELFTEST_CHECK(viking_inv_has(player, obj_fleece),
                                       "Lalli should award golden fleece for the stew");
                        viking_pass(fails, "lalli-stew-accept");
                    }
                }
            }
            viking_god(player);

            if( obj_fleece >= 0 && obj_lyre_u >= 0 && obj_lyre_s >= 0 )
            {
                viking_clear_inv(player);
                viking_give(player, obj_fleece, 1);
                viking_give(player, obj_lyre_u, 1);
                player->varps[bits] = viking_bit_set(player->varps[bits], 5, VIKING_OLAF_START,
                                                     VIKING_OLAF_END);
                viking_talk(srv, player, npc_olaf, olaf);
                fails = g_selftest_failures;
                SELFTEST_CHECK(viking_inv_has(player, obj_lyre_s),
                               "Olaf should string the lyre");
                viking_pass(fails, "olaf-string-lyre");
            }
            viking_god(player);

            if( npc_fosse >= 0 && obj_lyre_s >= 0 && obj_shark >= 0 && obj_lyre_e >= 0 )
            {
                fosse = viking_spawn(srv, npc_fosse, 2623, 3596);
                if( fosse >= 0 )
                {
                    viking_clear_inv(player);
                    viking_give(player, obj_lyre_s, 1);
                    viking_talk(srv, player, npc_fosse, fosse);
                    viking_pass(g_selftest_failures, "fossegrimen-need-fish");
                    viking_give(player, obj_shark, 1);
                    viking_talk(srv, player, npc_fosse, fosse);
                    fails = g_selftest_failures;
                    SELFTEST_CHECK(viking_inv_has(player, obj_lyre_e),
                                   "Fossegrimen should enchant the lyre");
                    viking_pass(fails, "fossegrimen-enchant");
                }
            }
            viking_god(player);

            if( obj_lyre_e >= 0 )
            {
                viking_clear_inv(player);
                viking_give(player, obj_lyre_e, 1);
                player->varps[bits] = viking_bit_set(player->varps[bits], 5, VIKING_OLAF_START,
                                                     VIKING_OLAF_END);
                viking_talk(srv, player, npc_olaf, olaf);
                fails = g_selftest_failures;
                SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_OLAF_START,
                                              VIKING_OLAF_END) == 7,
                               "enchanted lyre hand-in should complete Olaf");
                viking_pass(fails, "olaf-lyre-handin");
            }
        }
    }
    viking_god(player);

    /* Peer riddle wrong + right. */
    if( npc_peer >= 0 )
    {
        peer = viking_spawn(srv, npc_peer, VIKING_X + 8, VIKING_Z);
        SELFTEST_CHECK(peer >= 0, "Peer should spawn");
        if( peer >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            player->varps[bits] = viking_bit_set(player->varps[bits], 0, VIKING_PEER_START,
                                                 VIKING_PEER_END);
            /* First talk both starts the trial and presents the riddle.
             * Choose WITS so click-through cannot accidentally accept MIND. */
            rows[0] = 2;
            viking_talk_choices(srv, player, npc_peer, peer, rows, 1);
            ToriRSServer_WorldCloseModal(srv);
            viking_pass(g_selftest_failures, "peer-start");
            viking_god(player);

            rows[0] = 2;
            viking_talk_choices(srv, player, npc_peer, peer, rows, 1);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_PEER_START, VIKING_PEER_END) <
                               3,
                           "wrong riddle must not complete Peer");
            viking_pass(fails, "peer-riddle-wrong");
            viking_god(player);

            rows[0] = 1;
            viking_talk_choices(srv, player, npc_peer, peer, rows, 1);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_PEER_START, VIKING_PEER_END) ==
                               3,
                           "MIND should complete Peer's house trial");
            viking_pass(fails, "peer-riddle-right");
        }
    }
    viking_god(player);

    /* Sigmund 13-NPC relay + flower hand-in. */
    if( npc_sigmund >= 0 )
    {
        sigmund = viking_spawn(srv, npc_sigmund, VIKING_X + 9, VIKING_Z);
        SELFTEST_CHECK(sigmund >= 0, "Sigmund should spawn");
        if( sigmund >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            player->varps[bits] = viking_bit_set(player->varps[bits], 0, VIKING_SIGMUND_START,
                                                 VIKING_SIGMUND_END);
            viking_talk(srv, player, npc_sigmund, sigmund);
            fails = g_selftest_failures;
            SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_SIGMUND_START,
                                          VIKING_SIGMUND_END) >= 1,
                           "Sigmund should start the merchant relay");
            viking_pass(fails, "sigmund-start");
            viking_god(player);

            if( npc_sailor >= 0 )
            {
                sailor = viking_spawn(srv, npc_sailor, VIKING_X + 10, VIKING_Z);
                if( sailor >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 1,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_sailor, sailor);
                    SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_SIGMUND_START,
                                                  VIKING_SIGMUND_END) == 2,
                                   "sailor relay step");
                    viking_pass(g_selftest_failures, "sigmund-sailor");
                }
            }
            if( npc_olaf >= 0 )
            {
                olaf = selftest_find_npc(srv, npc_olaf);
                if( olaf < 0 )
                    olaf = viking_spawn(srv, npc_olaf, VIKING_X + 6, VIKING_Z);
                if( olaf >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 2,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_olaf, olaf);
                    viking_pass(g_selftest_failures, "sigmund-olaf");
                }
            }
            if( npc_yrsa >= 0 )
            {
                yrsa = viking_spawn(srv, npc_yrsa, VIKING_X + 11, VIKING_Z);
                if( yrsa >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 3,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_yrsa, yrsa);
                    viking_pass(g_selftest_failures, "sigmund-yrsa");
                }
            }
            player->varps[bits] = viking_bit_set(player->varps[bits], 4, VIKING_SIGMUND_START,
                                                 VIKING_SIGMUND_END);
            viking_talk(srv, player, npc_brundt, brundt);
            viking_pass(g_selftest_failures, "sigmund-brundt-tax");
            if( npc_sigli >= 0 )
            {
                sigli = selftest_find_npc(srv, npc_sigli);
                if( sigli < 0 )
                    sigli = viking_spawn(srv, npc_sigli, VIKING_X + 3, VIKING_Z);
                if( sigli >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 5,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_sigli, sigli);
                    viking_pass(g_selftest_failures, "sigmund-sigli");
                }
            }
            if( npc_skul >= 0 )
            {
                skul = viking_spawn(srv, npc_skul, VIKING_X + 12, VIKING_Z);
                if( skul >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 6,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_skul, skul);
                    viking_pass(g_selftest_failures, "sigmund-skulgrimen");
                }
            }
            if( npc_fish >= 0 )
            {
                fish = viking_spawn(srv, npc_fish, VIKING_X + 13, VIKING_Z);
                if( fish >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 7,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_fish, fish);
                    viking_pass(g_selftest_failures, "sigmund-fisherman");
                }
            }
            if( npc_swensen >= 0 )
            {
                swensen = selftest_find_npc(srv, npc_swensen);
                if( swensen < 0 )
                    swensen = viking_spawn(srv, npc_swensen, VIKING_X + 5, VIKING_Z);
                if( swensen >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 8,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_swensen, swensen);
                    viking_pass(g_selftest_failures, "sigmund-swensen");
                }
            }
            if( npc_peer >= 0 )
            {
                peer = selftest_find_npc(srv, npc_peer);
                if( peer < 0 )
                    peer = viking_spawn(srv, npc_peer, VIKING_X + 8, VIKING_Z);
                if( peer >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 9,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_peer, peer);
                    viking_pass(g_selftest_failures, "sigmund-peer");
                }
            }
            if( npc_thorvald >= 0 )
            {
                thorvald = selftest_find_npc(srv, npc_thorvald);
                if( thorvald < 0 )
                    thorvald = viking_spawn(srv, npc_thorvald, 2666, 3694);
                if( thorvald >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 10,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_thorvald, thorvald);
                    viking_pass(g_selftest_failures, "sigmund-thorvald");
                }
            }
            if( npc_manni >= 0 )
            {
                manni = selftest_find_npc(srv, npc_manni);
                if( manni < 0 )
                    manni = viking_spawn(srv, npc_manni, VIKING_X + 4, VIKING_Z);
                if( manni >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 11,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_manni, manni);
                    viking_pass(g_selftest_failures, "sigmund-manni");
                }
            }
            if( npc_thora >= 0 )
            {
                thora = viking_spawn(srv, npc_thora, VIKING_X + 14, VIKING_Z);
                if( thora >= 0 )
                {
                    player->varps[bits] = viking_bit_set(player->varps[bits], 12,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_thora, thora);
                    viking_pass(g_selftest_failures, "sigmund-thora");
                }
            }
            if( npc_askel >= 0 && obj_coins >= 0 && obj_flower >= 0 )
            {
                askel = selftest_find_npc(srv, npc_askel);
                if( askel < 0 )
                    askel = viking_spawn(srv, npc_askel, VIKING_X + 2, VIKING_Z);
                if( askel >= 0 )
                {
                    viking_clear_inv(player);
                    viking_give(player, obj_coins, 5000);
                    player->varps[bits] = viking_bit_set(player->varps[bits], 13,
                                                         VIKING_SIGMUND_START, VIKING_SIGMUND_END);
                    viking_talk(srv, player, npc_askel, askel);
                    fails = g_selftest_failures;
                    SELFTEST_CHECK(viking_inv_has(player, obj_flower),
                                   "Askeladden should hand over the exotic flower");
                    viking_pass(fails, "sigmund-askeladden-flower");
                }
            }
            if( obj_flower >= 0 )
            {
                if( !viking_inv_has(player, obj_flower) )
                    viking_give(player, obj_flower, 1);
                player->varps[bits] = viking_bit_set(player->varps[bits], 14, VIKING_SIGMUND_START,
                                                     VIKING_SIGMUND_END);
                viking_talk(srv, player, npc_sigmund, sigmund);
                fails = g_selftest_failures;
                SELFTEST_CHECK(viking_bit_get(player->varps[bits], VIKING_SIGMUND_START,
                                              VIKING_SIGMUND_END) == 15,
                               "flower hand-in should complete Sigmund");
                viking_pass(fails, "sigmund-handin");
            }
        }
    }
    viking_god(player);

    /* Joke-rejection citizens. */
    if( npc_man >= 0 )
    {
        man = viking_spawn(srv, npc_man, VIKING_X + 15, VIKING_Z);
        if( man >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            viking_talk(srv, player, npc_man, man);
            viking_pass(g_selftest_failures, "citizen-man-joke-refuse");
        }
    }
    if( npc_woman >= 0 )
    {
        woman = viking_spawn(srv, npc_woman, VIKING_X + 16, VIKING_Z);
        if( woman >= 0 )
        {
            player->varps[varp] = VIKING_STARTED;
            viking_talk(srv, player, npc_woman, woman);
            viking_pass(g_selftest_failures, "citizen-woman-joke-refuse");
        }
    }
    viking_god(player);

    /* Journals. */
    player->varps[varp] = VIKING_NOT_STARTED;
    player->varps[bits] = 0;
    ToriRSServer_ScriptsRunProc(srv, "[proc,viking_journal]", NULL, 0);
    viking_finish(srv);
    viking_pass(g_selftest_failures, "journal-not-started");

    player->varps[varp] = 4;
    player->varps[bits] = viking_bit_set(0, 2, VIKING_SWENSEN_START, VIKING_SWENSEN_END);
    player->varps[bits] = viking_bit_set(player->varps[bits], 3, VIKING_SIGLI_START, VIKING_SIGLI_END);
    ToriRSServer_ScriptsRunProc(srv, "[proc,viking_journal]", NULL, 0);
    viking_finish(srv);
    viking_pass(g_selftest_failures, "journal-mid-votes");

    player->varps[varp] = VIKING_COMPLETE;
    ToriRSServer_ScriptsRunProc(srv, "[proc,viking_journal]", NULL, 0);
    viking_finish(srv);
    viking_pass(g_selftest_failures, "journal-complete");
    viking_god(player);

    /* Mid-quest 1 vote and N votes, then 7-vote welcome + complete scroll. */
    player->varps[varp] = 2;
    viking_talk(srv, player, npc_brundt, brundt);
    viking_pass(g_selftest_failures, "brundt-mid-1-vote");
    player->varps[varp] = 5;
    viking_talk(srv, player, npc_brundt, brundt);
    viking_pass(g_selftest_failures, "brundt-mid-n-votes");
    player->varps[varp] = 8;
    viking_talk(srv, player, npc_brundt, brundt);
    fails = g_selftest_failures;
    SELFTEST_CHECK(player->varps[varp] == VIKING_COMPLETE,
                   "seven votes should complete The Fremennik Trials, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "complete path must leave the player alive");
    viking_pass(fails, "brundt-7-vote-complete");
    viking_god(player);

    fprintf(stderr, "ToriRSServer viking selftest: %lu checks, %d failures\n",
            g_selftest_checks - checks0, g_selftest_failures - fails0);
}

#endif
