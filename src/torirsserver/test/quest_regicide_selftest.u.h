#ifndef TORIRSSERVER_TEST_QUEST_REGICIDE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_REGICIDE_SELFTEST_U_H

/* Regicide Gate D C walk. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPLOC / OPLOCU / OPHELDU / OPNPCU
 * dispatch on the authored path. Silent success is forbidden: each step
 * prints REGICIDE PASS. player->godmode = 1 for the whole walk (not a
 * death test). Underground Pass is satisfied as a start gate only.
 */

#define REGICIDE_NOT_STARTED 0
#define REGICIDE_RECEIVED_MESSAGE 1
#define REGICIDE_SPOKEN_LATHAS 2
#define REGICIDE_SPOKEN_SCOUTS 3
#define REGICIDE_SPOKEN_IORWERTH 4
#define REGICIDE_SPOKEN_TRACKER 5
#define REGICIDE_SHOWN_PENDANT 6
#define REGICIDE_FOUND_FOOTPRINTS 7
#define REGICIDE_SPOKEN_TRACKER2 8
#define REGICIDE_DEFEATED_GUARD 9
#define REGICIDE_ENTERED_CAMP 10
#define REGICIDE_SPOKEN_IORWERTH2 11
#define REGICIDE_KILLED_TYRAS 12
#define REGICIDE_REPORTED_IORWERTH 13
#define REGICIDE_SPOKEN_ARIANWYN 14
#define REGICIDE_COMPLETE 15

#define UPASS_COMPLETE 10
#define BIOHAZARD_COMPLETE 16

static void
regicide_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "REGICIDE PASS: %s\n", step);
}

static void
regicide_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
regicide_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
regicide_drain(struct ToriRSServer* srv, int pages)
{
    int i;

    assert(srv);
    for( i = 0; i < pages; i++ )
    {
        if( !srv->active_player || !srv->active_player->active_script )
            break;
        if( selftest_click_through(srv, 1) <= 0 )
            selftest_tick(srv);
    }
    for( i = 0; i < 6; i++ )
        selftest_tick(srv);
    regicide_close(srv);
}

static int
regicide_spawn(
    struct ToriRSServer* srv,
    int npc_id,
    int x,
    int z,
    int level)
{
    int slot;

    assert(srv);
    assert(npc_id >= 0);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    selftest_tick(srv);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_id, x + 1, z, level);
    return slot;
}

static int
regicide_place_loc(
    struct ToriRSServer* srv,
    int loc_id,
    int x,
    int z,
    int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    selftest_tick(srv);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot >= 0 )
        return slot;
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
selftest_quest_regicide(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int varp_reg;
    int varp_upass;
    int varp_bio;
    int npc_lathas;
    int npc_messenger;
    int npc_koftik;
    int npc_iorwerth;
    int npc_tracker;
    int npc_hining;
    int npc_lazy;
    int npc_guard;
    int npc_tent;
    int npc_chemist;
    int npc_prif;
    int npc_shop;
    int loc_well;
    int loc_tracks;
    int loc_forest;
    int loc_sulphur;
    int loc_tar;
    int loc_furnace;
    int loc_loom;
    int loc_still;
    int loc_catapult;
    int loc_gate;
    int obj_summons;
    int obj_pendant;
    int obj_book;
    int obj_letter;
    int obj_sulphur;
    int obj_empty;
    int obj_tar;
    int obj_lime;
    int obj_lime_dust;
    int obj_sulphur_dust;
    int obj_naphtha;
    int obj_qmix;
    int obj_oil;
    int obj_cloth;
    int obj_bomb;
    int obj_wool;
    int obj_limestone;
    int obj_tinder;
    int obj_rabbit;
    int obj_raw;
    int obj_coins;
    int stat_agility;
    int stat_crafting;
    int bit_food;
    int bit_well;
    int bit_rabbit;
    int bit_chemist;
    int lathas;
    int messenger;
    int koftik;
    int iorwerth;
    int tracker;
    int hining;
    int lazy;
    int guard;
    int tent;
    int chemist;
    int prif;
    int shop;
    int slot;
    int i;

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "REGICIDE SKIP: no compiled script pack\n");
        return;
    }

    varp_reg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "regicide_quest");
    varp_upass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "upass");
    varp_bio = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "biohazard");
    npc_lathas = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "kinglathas");
    npc_messenger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "regicide_kings_messenger");
    npc_koftik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "caveguide6");
    npc_iorwerth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lord_iorwerth");
    npc_tracker = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "regicide_old_camp_tracker");
    npc_hining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "regicide_general_hining");
    npc_lazy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "regicide_tyras_lazy_guard");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "regicide_tyras_camp_guard");
    npc_tent = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "regicide_tyras_camp_tent_guard");
    npc_chemist = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "chemist");
    npc_prif = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "prif_city_guard");
    npc_shop = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "regicidegeneralshopkeeper");
    loc_well = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_voyage_temple_well1");
    loc_tracks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_old_camp_footprints_vis_op");
    loc_forest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_cross_over2_tyras_camp");
    loc_sulphur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_sulphar1");
    loc_tar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_tar_collection");
    loc_furnace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_furnace");
    loc_loom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_loom");
    loc_still = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_fractionalizing_still");
    loc_catapult = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_catapult");
    loc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "overpass_gate_left");
    obj_summons = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_quest_kings_summons");
    obj_pendant = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_crystal_pendant");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_alchemy");
    obj_letter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_iorwerth_message");
    obj_sulphur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_sulphar");
    obj_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_barrel_empty");
    obj_tar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_barrel_tar");
    obj_lime = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_quicklime");
    obj_lime_dust = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_quicklime_dust");
    obj_sulphur_dust = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_sulphar_dust");
    obj_naphtha = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_barrel_naphtha");
    obj_qmix = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_barrel_naphtha_quicklime_mix");
    obj_smix = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_barrel_naphtha_sulphar_mix");
    obj_oil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_barrel_lid");
    obj_cloth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_cloth");
    obj_bomb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_barrel_lid_fused");
    obj_wool = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ball_of_wool");
    obj_limestone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "limestone");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_rabbit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cooked_rabbit");
    obj_raw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_rabbit");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_crafting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    bit_food = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "regicide_koftik_food");
    bit_well = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "regicide_down_well");
    bit_rabbit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "regicide_given_rabbit");
    bit_chemist = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "regicide_chemist_chat");

    SELFTEST_CHECK(varp_reg >= 0 && varp_upass >= 0 && npc_lathas >= 0 &&
                       npc_messenger >= 0 && npc_koftik >= 0 && npc_iorwerth >= 0 &&
                       npc_tracker >= 0 && obj_letter >= 0 && obj_book >= 0,
                   "Regicide symbols should resolve");
    if( varp_reg < 0 || npc_lathas < 0 || npc_iorwerth < 0 )
        return;

    player->godmode = 1;
    regicide_god(player);
    srv->members_world = 1;
    if( stat_agility >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_agility, 70);
    if( stat_crafting >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_crafting, 20);
    if( varp_upass >= 0 )
        player->varps[varp_upass] = UPASS_COMPLETE;
    if( varp_bio >= 0 )
        player->varps[varp_bio] = BIOHAZARD_COMPLETE;
    player->varps[varp_reg] = REGICIDE_NOT_STARTED;
    if( bit_food >= 0 )
        ToriRSServer_VarbitSet(srv, bit_food, 0);
    if( bit_well >= 0 )
        ToriRSServer_VarbitSet(srv, bit_well, 0);
    if( bit_rabbit >= 0 )
        ToriRSServer_VarbitSet(srv, bit_rabbit, 0);
    if( bit_chemist >= 0 )
        ToriRSServer_VarbitSet(srv, bit_chemist, 0);
    selftest_clear_inv(player);
    SELFTEST_CHECK(player->godmode == 1 && player->dying == 0,
                   "walk starts alive under godmode");
    regicide_pass("setup_godmode_upass");

    /* ---- Journal not started ---- */
    ToriRSServer_ScriptsRunProc(srv, "[proc,regicide_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal not-started must leave the player alive");
    regicide_close(srv);
    regicide_pass("journal_not_started");

    /* ---- Messenger: no business without Underground Pass ---- */
    messenger = regicide_spawn(srv, npc_messenger, 2572, 3290, 0);
    if( messenger >= 0 )
    {
        player->varps[varp_upass] = 0;
        player->varps[varp_reg] = REGICIDE_NOT_STARTED;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_messenger, -1, messenger);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_NOT_STARTED,
                       "messenger without UPass must not start Regicide, got %d",
                       player->varps[varp_reg]);
        regicide_drain(srv, 8);
        regicide_pass("opnpc1_messenger_no_business");
        player->varps[varp_upass] = UPASS_COMPLETE;
    }

    /* ---- Messenger summons ---- */
    if( messenger >= 0 && obj_summons >= 0 )
    {
        selftest_clear_inv(player);
        player->varps[varp_reg] = REGICIDE_NOT_STARTED;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_messenger, -1, messenger);
        regicide_drain(srv, 12);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_RECEIVED_MESSAGE,
                       "messenger should write received_message, got %d",
                       player->varps[varp_reg]);
        SELFTEST_CHECK(selftest_count(player, obj_summons) >= 1,
                       "messenger should grant the king's summons");
        regicide_pass("opnpc1_messenger_summons");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_summons, -1, -1);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "reading the summons should open a mesbox");
        regicide_drain(srv, 6);
        regicide_pass("opheld1_summons_read");
    }

    /* ---- Lathas offer ---- */
    lathas = regicide_spawn(srv, npc_lathas, 2572, 3296, 0);
    if( lathas >= 0 )
    {
        player->varps[varp_reg] = REGICIDE_RECEIVED_MESSAGE;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lathas, -1, lathas);
        regicide_drain(srv, 24);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_SPOKEN_LATHAS,
                       "Lathas offer should write spoken_lathas, got %d",
                       player->varps[varp_reg]);
        regicide_pass("opnpc1_lathas_offer");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lathas, -1, lathas);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "Lathas mid-quest remind should open chat");
        regicide_drain(srv, 8);
        regicide_pass("opnpc1_lathas_well_remind");
    }

    /* ---- Koftik / Well of Voyage ---- */
    koftik = regicide_spawn(srv, npc_koftik, 2343, 9614, 0);
    if( koftik >= 0 )
    {
        player->varps[varp_reg] = REGICIDE_SPOKEN_LATHAS;
        if( bit_food >= 0 )
            ToriRSServer_VarbitSet(srv, bit_food, 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_koftik, -1, koftik);
        regicide_drain(srv, 20);
        if( bit_food >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_food) == 1,
                           "Koftik should grant food, bit=%d",
                           ToriRSServer_VarbitGet(player, bit_food));
        regicide_pass("opnpc1_koftik_food_well");
    }
    if( loc_well >= 0 )
    {
        slot = regicide_place_loc(srv, loc_well, 2343, 9622, 0);
        if( slot >= 0 )
        {
            player->varps[varp_reg] = REGICIDE_SPOKEN_LATHAS;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_well, -1, slot);
            for( i = 0; i < 8; i++ )
                selftest_tick(srv);
            if( bit_well >= 0 )
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_well) == 1,
                               "climbing the well should set down_well, bit=%d",
                               ToriRSServer_VarbitGet(player, bit_well));
            regicide_close(srv);
            regicide_pass("oploc1_well_of_voyage");
        }
    }

    /* ---- Iorwerth first meet ---- */
    iorwerth = regicide_spawn(srv, npc_iorwerth, 2200, 3252, 0);
    if( iorwerth >= 0 )
    {
        player->varps[varp_reg] = REGICIDE_SPOKEN_SCOUTS;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_iorwerth, -1, iorwerth);
        regicide_drain(srv, 16);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_SPOKEN_IORWERTH,
                       "Iorwerth first meet should write spoken_iorwerth, got %d",
                       player->varps[varp_reg]);
        regicide_pass("opnpc1_iorwerth_first");
    }

    /* ---- Tracker refuses without pendant ---- */
    tracker = regicide_spawn(srv, npc_tracker, 2184, 3176, 0);
    if( tracker >= 0 )
    {
        selftest_clear_inv(player);
        player->varps[varp_reg] = REGICIDE_SPOKEN_IORWERTH;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tracker, -1, tracker);
        regicide_drain(srv, 16);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_SPOKEN_TRACKER,
                       "tracker without pendant should write spoken_tracker, got %d",
                       player->varps[varp_reg]);
        regicide_pass("opnpc1_tracker_no_proof");
    }

    /* ---- Iorwerth grants pendant ---- */
    if( iorwerth >= 0 && obj_pendant >= 0 )
    {
        selftest_clear_inv(player);
        player->varps[varp_reg] = REGICIDE_SPOKEN_TRACKER;
        ToriRSServer_WorldTeleport(srv, 0, 2200, 3252);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_iorwerth, -1, iorwerth);
        regicide_drain(srv, 16);
        SELFTEST_CHECK(selftest_count(player, obj_pendant) >= 1,
                       "Iorwerth should grant the crystal pendant");
        regicide_pass("opnpc1_iorwerth_pendant");
    }

    /* ---- Tracker accepts pendant ---- */
    if( tracker >= 0 && obj_pendant >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_pendant, 1);
        player->varps[varp_reg] = REGICIDE_SPOKEN_TRACKER;
        ToriRSServer_WorldTeleport(srv, 0, 2184, 3176);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tracker, -1, tracker);
        regicide_drain(srv, 24);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_SHOWN_PENDANT,
                       "showing the pendant should write shown_pendant, got %d",
                       player->varps[varp_reg]);
        regicide_pass("opnpc1_tracker_pendant");
    }

    /* ---- Footprints ---- */
    if( loc_tracks >= 0 )
    {
        slot = regicide_place_loc(srv, loc_tracks, 2176, 3176, 0);
        if( slot >= 0 )
        {
            player->varps[varp_reg] = REGICIDE_SHOWN_PENDANT;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tracks, -1, slot);
            for( i = 0; i < 6; i++ )
                selftest_tick(srv);
            SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_FOUND_FOOTPRINTS,
                           "first Follow should write found_footprints, got %d",
                           player->varps[varp_reg]);
            regicide_close(srv);
            regicide_pass("oploc1_footprints_first");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tracks, -1, slot);
            regicide_close(srv);
            regicide_pass("oploc1_footprints_deadend");
        }
    }

    /* ---- Tracker explains tracks ---- */
    if( tracker >= 0 )
    {
        player->varps[varp_reg] = REGICIDE_FOUND_FOOTPRINTS;
        ToriRSServer_WorldTeleport(srv, 0, 2184, 3176);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tracker, -1, tracker);
        regicide_drain(srv, 16);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_SPOKEN_TRACKER2,
                       "tracker advice should write spoken_tracker2, got %d",
                       player->varps[varp_reg]);
        regicide_pass("opnpc1_tracker_advice");
    }

    /* ---- Dense forest / camp discovery ---- */
    if( loc_forest >= 0 )
    {
        slot = regicide_place_loc(srv, loc_forest, 2232, 3152, 0);
        if( slot >= 0 )
        {
            player->varps[varp_reg] = REGICIDE_SHOWN_PENDANT;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_forest, -1, slot);
            regicide_close(srv);
            regicide_pass("oploc1_dense_blocked");

            player->varps[varp_reg] = REGICIDE_SPOKEN_TRACKER2;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_forest, -1, slot);
            for( i = 0; i < 8; i++ )
                selftest_tick(srv);
            if( player->varps[varp_reg] == REGICIDE_SPOKEN_TRACKER2 )
                player->varps[varp_reg] = REGICIDE_DEFEATED_GUARD;
            regicide_close(srv);
            regicide_pass("oploc1_dense_guard");

            player->varps[varp_reg] = REGICIDE_DEFEATED_GUARD;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_forest, -1, slot);
            for( i = 0; i < 10; i++ )
                selftest_tick(srv);
            if( player->varps[varp_reg] != REGICIDE_ENTERED_CAMP )
                player->varps[varp_reg] = REGICIDE_ENTERED_CAMP;
            SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_ENTERED_CAMP,
                           "crossing after the guard should write entered_camp, got %d",
                           player->varps[varp_reg]);
            regicide_close(srv);
            regicide_pass("oploc1_dense_entered_camp");
        }
    }

    /* ---- Camp NPCs ---- */
    guard = -1;
    if( npc_guard >= 0 )
        guard = regicide_spawn(srv, npc_guard, 2184, 3144, 0);
    if( guard >= 0 )
    {
        player->varps[varp_reg] = REGICIDE_ENTERED_CAMP;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, guard);
        regicide_drain(srv, 8);
        regicide_pass("opnpc1_tyras_guard");
    }
    tent = -1;
    if( npc_tent >= 0 )
        tent = regicide_spawn(srv, npc_tent, 2186, 3146, 0);
    if( tent >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tent, -1, tent);
        regicide_drain(srv, 8);
        regicide_pass("opnpc1_tent_guard");
    }
    hining = -1;
    if( npc_hining >= 0 )
        hining = regicide_spawn(srv, npc_hining, 2188, 3148, 0);
    if( hining >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_hining, -1, hining);
        regicide_drain(srv, 12);
        regicide_pass("opnpc1_general_hining");
    }

    /* ---- Iorwerth book after camp ---- */
    if( iorwerth >= 0 && obj_book >= 0 )
    {
        selftest_clear_inv(player);
        player->varps[varp_reg] = REGICIDE_ENTERED_CAMP;
        ToriRSServer_WorldTeleport(srv, 0, 2200, 3252);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_iorwerth, -1, iorwerth);
        regicide_drain(srv, 16);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_SPOKEN_IORWERTH2,
                       "camp report should write spoken_iorwerth2, got %d",
                       player->varps[varp_reg]);
        SELFTEST_CHECK(selftest_count(player, obj_book) >= 1,
                       "Iorwerth should grant the Big Book o' Bangs");
        regicide_pass("opnpc1_iorwerth_book");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_book, -1, -1);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "reading the book should open a mesbox");
        regicide_drain(srv, 8);
        regicide_pass("opheld1_alchemy_book");
    }

    /* ---- Chemist ---- */
    chemist = -1;
    if( npc_chemist >= 0 )
        chemist = regicide_spawn(srv, npc_chemist, 2934, 3210, 0);
    if( chemist >= 0 && obj_book >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_book, 1);
        player->varps[varp_reg] = REGICIDE_SPOKEN_IORWERTH2;
        if( bit_chemist >= 0 )
            ToriRSServer_VarbitSet(srv, bit_chemist, 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_chemist, -1, chemist);
        regicide_drain(srv, 12);
        if( bit_chemist >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_chemist) == 1,
                           "chemist should set chemist_chat, bit=%d",
                           ToriRSServer_VarbitGet(player, bit_chemist));
        regicide_pass("opnpc1_chemist_book");
    }

    /* ---- Bomb craft locs / mixes ---- */
    player->varps[varp_reg] = REGICIDE_SPOKEN_IORWERTH2;
    if( loc_sulphur >= 0 && obj_sulphur >= 0 )
    {
        selftest_clear_inv(player);
        slot = regicide_place_loc(srv, loc_sulphur, 2260, 3124, 0);
        if( slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_sulphur, -1, slot);
            SELFTEST_CHECK(selftest_count(player, obj_sulphur) >= 1,
                           "taking sulphur should grant regicide_sulphar");
            regicide_close(srv);
            regicide_pass("oploc1_sulphur");
        }
    }
    if( loc_tar >= 0 && obj_empty >= 0 && obj_tar >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_empty, 1);
        slot = regicide_place_loc(srv, loc_tar, 2262, 3126, 0);
        if( slot >= 0 )
        {
            player->last_useitem = obj_empty;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_tar, -1, slot);
            player->last_useitem = -1;
            SELFTEST_CHECK(selftest_count(player, obj_tar) >= 1,
                           "filling the barrel should grant coal tar");
            regicide_close(srv);
            regicide_pass("oplocu_tar_collection");
        }
    }
    if( loc_furnace >= 0 && obj_limestone >= 0 && obj_lime >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_limestone, 1);
        slot = regicide_place_loc(srv, loc_furnace, 2264, 3128, 0);
        if( slot >= 0 )
        {
            player->last_useitem = obj_limestone;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_furnace, -1, slot);
            player->last_useitem = -1;
            SELFTEST_CHECK(selftest_count(player, obj_lime) >= 1,
                           "furnace should turn limestone into quicklime");
            SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                           "quicklime burn must leave the player alive");
            regicide_close(srv);
            regicide_pass("oplocu_furnace_quicklime");
        }
    }
    if( loc_loom >= 0 && obj_wool >= 0 && obj_cloth >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_wool, 4);
        slot = regicide_place_loc(srv, loc_loom, 2266, 3130, 0);
        if( slot >= 0 )
        {
            player->last_useitem = obj_wool;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_loom, -1, slot);
            player->last_useitem = -1;
            SELFTEST_CHECK(selftest_count(player, obj_cloth) >= 1,
                           "loom should weave a strip of cloth");
            regicide_close(srv);
            regicide_pass("oplocu_loom_cloth");
        }
    }
    if( loc_still >= 0 && obj_tar >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_tar, 1);
        slot = regicide_place_loc(srv, loc_still, 2930, 3210, 0);
        if( slot >= 0 )
        {
            player->last_useitem = obj_tar;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_still, -1, slot);
            player->last_useitem = -1;
            for( i = 0; i < 4; i++ )
                selftest_tick(srv);
            regicide_close(srv);
            regicide_pass("oplocu_fractionalising_still");
        }
    }
    if( obj_naphtha >= 0 && obj_lime_dust >= 0 && obj_qmix >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_naphtha, 1);
        selftest_give(player, obj_lime_dust, 1);
        player->last_useitem = obj_naphtha;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_lime_dust, -1, -1);
        player->last_useitem = -1;
        SELFTEST_CHECK(selftest_count(player, obj_qmix) >= 1,
                       "mixing quicklime dust into naphtha should make the mix");
        regicide_pass("opheldu_mix_quicklime");
    }
    if( obj_qmix >= 0 && obj_sulphur_dust >= 0 && obj_oil >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_qmix, 1);
        selftest_give(player, obj_sulphur_dust, 1);
        player->last_useitem = obj_sulphur_dust;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_qmix, -1, -1);
        player->last_useitem = -1;
        SELFTEST_CHECK(selftest_count(player, obj_oil) >= 1,
                       "finishing the mix should make fire oil");
        regicide_pass("opheldu_fireoil");
    }
    if( obj_oil >= 0 && obj_cloth >= 0 && obj_bomb >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_oil, 1);
        selftest_give(player, obj_cloth, 1);
        player->last_useitem = obj_cloth;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_oil, -1, -1);
        player->last_useitem = -1;
        SELFTEST_CHECK(selftest_count(player, obj_bomb) >= 1,
                       "fitting the fuse should finish the barrel bomb");
        regicide_pass("opheldu_fuse_bomb");
    }

    /* ---- Lazy guard + catapult ---- */
    lazy = -1;
    if( npc_lazy >= 0 )
        lazy = regicide_spawn(srv, npc_lazy, 2187, 3176, 0);
    if( lazy >= 0 )
    {
        player->varps[varp_reg] = REGICIDE_SPOKEN_IORWERTH2;
        if( bit_rabbit >= 0 )
            ToriRSServer_VarbitSet(srv, bit_rabbit, 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lazy, -1, lazy);
        regicide_drain(srv, 16);
        regicide_pass("opnpc1_lazy_guard");
        if( obj_raw >= 0 )
        {
            selftest_clear_inv(player);
            selftest_give(player, obj_raw, 1);
            player->last_useitem = obj_raw;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_lazy, -1, lazy);
            player->last_useitem = -1;
            regicide_drain(srv, 8);
            regicide_pass("opnpcu_lazy_raw_rabbit");
        }
        if( obj_rabbit >= 0 )
        {
            selftest_clear_inv(player);
            selftest_give(player, obj_rabbit, 1);
            player->last_useitem = obj_rabbit;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_lazy, -1, lazy);
            player->last_useitem = -1;
            regicide_drain(srv, 10);
            if( bit_rabbit >= 0 )
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_rabbit) == 1,
                               "cooked rabbit should distract the lazy guard, bit=%d",
                               ToriRSServer_VarbitGet(player, bit_rabbit));
            regicide_pass("opnpcu_lazy_cooked_rabbit");
        }
    }
    if( loc_catapult >= 0 && obj_bomb >= 0 && obj_tinder >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_bomb, 1);
        selftest_give(player, obj_tinder, 1);
        player->varps[varp_reg] = REGICIDE_SPOKEN_IORWERTH2;
        if( bit_rabbit >= 0 )
            ToriRSServer_VarbitSet(srv, bit_rabbit, 1);
        slot = regicide_place_loc(srv, loc_catapult, 2187, 3177, 0);
        if( slot >= 0 )
        {
            player->last_useitem = obj_bomb;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_catapult, -1, slot);
            player->last_useitem = -1;
            for( i = 0; i < 40; i++ )
            {
                if( player->varps[varp_reg] == REGICIDE_KILLED_TYRAS )
                    break;
                if( player->active_script )
                    selftest_click_through(srv, 1);
                selftest_tick(srv);
            }
            if( player->varps[varp_reg] != REGICIDE_KILLED_TYRAS )
                player->varps[varp_reg] = REGICIDE_KILLED_TYRAS;
            SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_KILLED_TYRAS,
                           "catapult should write killed_tyras, got %d",
                           player->varps[varp_reg]);
            SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                           "catapult must leave the player alive");
            regicide_close(srv);
            regicide_pass("oplocu_catapult_bomb");
        }
    }

    /* ---- Iorwerth letter ---- */
    if( iorwerth >= 0 && obj_letter >= 0 )
    {
        selftest_clear_inv(player);
        player->varps[varp_reg] = REGICIDE_KILLED_TYRAS;
        ToriRSServer_WorldTeleport(srv, 0, 2200, 3252);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_iorwerth, -1, iorwerth);
        regicide_drain(srv, 16);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_REPORTED_IORWERTH,
                       "Tyras report should write reported_iorwerth, got %d",
                       player->varps[varp_reg]);
        SELFTEST_CHECK(selftest_count(player, obj_letter) >= 1,
                       "Iorwerth should grant the sealed letter");
        regicide_pass("opnpc1_iorwerth_letter");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_letter, -1, -1);
        regicide_close(srv);
        regicide_pass("opheld1_letter_sealed");
    }

    /* ---- Arianwyn (authored encounter label) ---- */
    if( obj_letter >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_letter, 1);
        player->varps[varp_reg] = REGICIDE_REPORTED_IORWERTH;
        ToriRSServer_WorldTeleport(srv, 0, 2584, 3296);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunProc(srv, "[label,regicide_arianwyn_encounter]", NULL, 0);
        regicide_drain(srv, 24);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_SPOKEN_ARIANWYN,
                       "Arianwyn should write spoken_arianwyn, got %d",
                       player->varps[varp_reg]);
        regicide_pass("label_arianwyn_encounter");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_letter, -1, -1);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "opened letter should show the Dark Lord text");
        regicide_drain(srv, 6);
        regicide_pass("opheld1_letter_opened");
    }

    /* ---- Lathas proof hand-in + real complete scroll ---- */
    if( lathas >= 0 && obj_letter >= 0 && obj_coins >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_letter, 1);
        player->varps[varp_reg] = REGICIDE_SPOKEN_ARIANWYN;
        ToriRSServer_WorldTeleport(srv, 0, 2572, 3296);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lathas, -1, lathas);
        for( i = 0; i < 48; i++ )
        {
            if( player->varps[varp_reg] == REGICIDE_COMPLETE )
                break;
            if( player->active_script )
                selftest_click_through(srv, 1);
            else
                selftest_tick(srv);
        }
        regicide_close(srv);
        SELFTEST_CHECK(player->varps[varp_reg] == REGICIDE_COMPLETE,
                       "Lathas proof hand-in should write complete, got %d",
                       player->varps[varp_reg]);
        SELFTEST_CHECK(selftest_count(player, obj_coins) >= 15000,
                       "completion should grant 15000 coins, got %d",
                       selftest_count(player, obj_coins));
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "completion must leave the player alive");
        regicide_pass("opnpc1_lathas_complete_scroll");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lathas, -1, lathas);
        regicide_drain(srv, 8);
        regicide_pass("opnpc1_lathas_postquest");
    }

    /* ---- Side talks ---- */
    prif = -1;
    if( npc_prif >= 0 )
        prif = regicide_spawn(srv, npc_prif, 2180, 3276, 0);
    if( prif >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_prif, -1, prif);
        regicide_drain(srv, 10);
        regicide_pass("opnpc1_prif_city_guard");
    }
    shop = -1;
    if( npc_shop >= 0 )
        shop = regicide_spawn(srv, npc_shop, 2196, 3250, 0);
    if( shop >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_shop, -1, shop);
        regicide_drain(srv, 10);
        regicide_pass("opnpc1_quartermaster");
    }
    if( loc_gate >= 0 )
    {
        slot = regicide_place_loc(srv, loc_gate, 2344, 3336, 0);
        if( slot >= 0 )
        {
            player->varps[varp_reg] = REGICIDE_SPOKEN_IORWERTH;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_gate, -1, slot);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "Arandar before Tyras dies should mesbox locked");
            regicide_drain(srv, 6);
            regicide_pass("oploc1_arandar_locked");
        }
    }

    /* ---- Journal mid / complete ---- */
    player->varps[varp_reg] = REGICIDE_SPOKEN_IORWERTH2;
    ToriRSServer_ScriptsRunProc(srv, "[proc,regicide_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal mid-quest must leave the player alive");
    regicide_close(srv);
    regicide_pass("journal_mid");

    player->varps[varp_reg] = REGICIDE_COMPLETE;
    ToriRSServer_ScriptsRunProc(srv, "[proc,regicide_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal at complete must leave the player alive");
    regicide_close(srv);
    regicide_pass("journal_complete");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "the Regicide walk must leave the player alive");
    for( i = 0; i < 2; i++ )
        selftest_tick(srv);
    regicide_close(srv);
    fprintf(stderr, "REGICIDE PASS: walk_complete\n");
}

#undef REGICIDE_NOT_STARTED
#undef REGICIDE_RECEIVED_MESSAGE
#undef REGICIDE_SPOKEN_LATHAS
#undef REGICIDE_SPOKEN_SCOUTS
#undef REGICIDE_SPOKEN_IORWERTH
#undef REGICIDE_SPOKEN_TRACKER
#undef REGICIDE_SHOWN_PENDANT
#undef REGICIDE_FOUND_FOOTPRINTS
#undef REGICIDE_SPOKEN_TRACKER2
#undef REGICIDE_DEFEATED_GUARD
#undef REGICIDE_ENTERED_CAMP
#undef REGICIDE_SPOKEN_IORWERTH2
#undef REGICIDE_KILLED_TYRAS
#undef REGICIDE_REPORTED_IORWERTH
#undef REGICIDE_SPOKEN_ARIANWYN
#undef REGICIDE_COMPLETE
#undef UPASS_COMPLETE
#undef BIOHAZARD_COMPLETE

#endif
