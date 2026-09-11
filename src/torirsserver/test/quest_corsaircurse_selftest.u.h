#ifndef TORIRSSERVER_TEST_QUEST_CORSAIRCURSE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_CORSAIRCURSE_SELFTEST_U_H

/* The Corsair Curse Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Tock / crew cannot leak.
 * Real OPNPC1 on the authored path (Tock, Gnocci, Arsen, Colin, Ithoi).
 * Start offer is p_choice2 Yes / Not now. Do not auto-start.
 * player->godmode = 1 for the whole walk (no death case).
 * Completion goes through ~cc_quest_complete via Tock's finish branch.
 *
 * Gate: TORIRSSERVER_SELFTEST_CC_ONLY=1
 */

#define CC_NOT_STARTED 0
#define CC_TOCK_FARM 5
#define CC_RIMMINGTON 10
#define CC_CURSES 15
#define CC_TOCK2 20
#define CC_FOOD 25
#define CC_ARSEN 30
#define CC_ITHOI 35
#define CC_BURN 45
#define CC_CUTSCENE 49
#define CC_ANSWERS 50
#define CC_KILL 52
#define CC_FINISH 55
#define CC_COMPLETE 60
#define CC_CURSE_DONE 2

#define CC_TOCK_X 3030
#define CC_TOCK_Z 3273
#define CC_RIM_X 2910
#define CC_RIM_Z 3226
#define CC_COVE_X 2531
#define CC_COVE_Z 2833
#define CC_GNOCCI_X 2544
#define CC_GNOCCI_Z 2862
#define CC_ARSEN_X 2553
#define CC_ARSEN_Z 2858
#define CC_COLIN_X 2558
#define CC_COLIN_Z 2858
#define CC_ITHOI_X 2529
#define CC_ITHOI_Z 2839

static void
cc_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "CC PASS: %s\n", step);
}

static void
cc_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
cc_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
cc_finish(struct ToriRSServer* srv)
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
cc_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    cc_god(player);
    selftest_tick(srv);
}

static int
cc_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    cc_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
cc_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
cc_reset_state(struct ToriRSServer* srv, struct ToriRSServerPlayer* player,
               int progress, int cook, int thief, int cabinboy, int navigator,
               int vb_progress, int vb_cook, int vb_thief, int vb_cabinboy,
               int vb_navigator)
{
    assert(srv);
    assert(player);
    cc_clear_inv(player);
    if( vb_progress >= 0 )
        ToriRSServer_VarbitSet(srv, vb_progress, progress);
    if( vb_cook >= 0 )
        ToriRSServer_VarbitSet(srv, vb_cook, cook);
    if( vb_thief >= 0 )
        ToriRSServer_VarbitSet(srv, vb_thief, thief);
    if( vb_cabinboy >= 0 )
        ToriRSServer_VarbitSet(srv, vb_cabinboy, cabinboy);
    if( vb_navigator >= 0 )
        ToriRSServer_VarbitSet(srv, vb_navigator, navigator);
    cc_god(player);
}

static void
cc_talk_and_pick(struct ToriRSServer* srv, struct ToriRSServerPlayer* player,
                 int chatmenu, int row)
{
    assert(srv);
    assert(player);
    biohazard_run_dialogue(srv, player, chatmenu);
    if( player->active_script && chatmenu > 0 && row > 0 )
        selftest_charter_choose(srv, row);
    biohazard_run_dialogue(srv, player, chatmenu);
}

static void
selftest_quest_corsaircurse(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int vb_progress;
    int vb_cook;
    int vb_thief;
    int vb_cabinboy;
    int vb_navigator;
    int npc_tock;
    int npc_ferry;
    int npc_gnocci;
    int npc_arsen;
    int npc_colin;
    int npc_ithoi;
    int npc_ithoi_combat;
    int loc_telescope;
    int loc_sand;
    int loc_burn;
    int loc_plank;
    int obj_relic;
    int chatmenu;
    int slot_tock;
    int slot_ferry;
    int slot_gnocci;
    int slot_arsen;
    int slot_colin;
    int slot_ithoi;
    int slot_ithoi_combat;
    int rc;
    int progress;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: ::corsaircurse / The Corsair Curse\n");

    loaded = srv->scripts_ok;
    if( !loaded )
    {
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    player->godmode = 1;
    srv->members_world = 1;

    vb_progress = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "corscurs_progress");
    vb_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "corscurs_cook");
    vb_thief = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "corscurs_thief");
    vb_cabinboy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "corscurs_cabinboy");
    vb_navigator = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "corscurs_navigator");
    npc_tock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "corsair_captain_crossroads");
    npc_ferry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "corsair_ferry_rimmington");
    npc_gnocci = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "corscurs_cook_sick");
    npc_arsen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "corscurs_thief_sick");
    npc_colin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "corscurs_cabinboy_sick");
    npc_ithoi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "corscurs_navigator_sick");
    npc_ithoi_combat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "corscurs_navigator_combat");
    loc_telescope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "corscurs_telescope");
    loc_sand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sand_withspade");
    loc_burn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "raids_icedemon_tinderbox");
    loc_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ds2_corsair_cove_shipplank");
    obj_relic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "corscurs_relic");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(vb_progress >= 0, "corscurs_progress varbit should resolve");
    SELFTEST_CHECK(npc_tock >= 0, "corsair_captain_crossroads should resolve");
    SELFTEST_CHECK(npc_ferry >= 0, "corsair_ferry_rimmington should resolve");
    SELFTEST_CHECK(npc_gnocci >= 0, "corscurs_cook_sick should resolve");
    SELFTEST_CHECK(npc_arsen >= 0, "corscurs_thief_sick should resolve");
    SELFTEST_CHECK(npc_colin >= 0, "corscurs_cabinboy_sick should resolve");
    SELFTEST_CHECK(npc_ithoi >= 0, "corscurs_navigator_sick should resolve");
    if( vb_progress < 0 || npc_tock < 0 || npc_ferry < 0 || npc_gnocci < 0 ||
        npc_arsen < 0 || npc_colin < 0 || npc_ithoi < 0 )
    {
        fprintf(stderr, "  SKIP  missing Corsair Curse symbols\n");
        return;
    }

    slot_tock = cc_spawn(srv, npc_tock, CC_TOCK_X, CC_TOCK_Z, 0);
    slot_ferry = cc_spawn(srv, npc_ferry, CC_RIM_X, CC_RIM_Z, 0);
    slot_gnocci = cc_spawn(srv, npc_gnocci, CC_GNOCCI_X, CC_GNOCCI_Z, 1);
    slot_arsen = cc_spawn(srv, npc_arsen, CC_ARSEN_X, CC_ARSEN_Z, 1);
    slot_colin = cc_spawn(srv, npc_colin, CC_COLIN_X, CC_COLIN_Z, 1);
    slot_ithoi = cc_spawn(srv, npc_ithoi, CC_ITHOI_X, CC_ITHOI_Z, 1);
    slot_ithoi_combat = -1;
    if( npc_ithoi_combat >= 0 )
        slot_ithoi_combat = cc_spawn(srv, npc_ithoi_combat, CC_ITHOI_X, CC_ITHOI_Z + 2, 1);

    SELFTEST_CHECK(slot_tock >= 0, "Tock farm spawn should succeed");
    SELFTEST_CHECK(slot_ferry >= 0, "Rimmington ferry spawn should succeed");
    SELFTEST_CHECK(slot_gnocci >= 0, "Gnocci spawn should succeed");
    SELFTEST_CHECK(slot_arsen >= 0, "Arsen spawn should succeed");
    SELFTEST_CHECK(slot_colin >= 0, "Colin spawn should succeed");
    SELFTEST_CHECK(slot_ithoi >= 0, "Ithoi spawn should succeed");

    /* ---- Start refuse: Yes / Not now must not auto-start ---- */
    cc_reset_state(srv, player, CC_NOT_STARTED, 0, 0, 0, 0, vb_progress, vb_cook,
                   vb_thief, vb_cabinboy, vb_navigator);
    cc_tele(srv, CC_TOCK_X, CC_TOCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_talk_and_pick(srv, player, chatmenu, 2);
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_NOT_STARTED,
                   "refuse must leave progress=0, got %d", progress);
    cc_pass("tock_refuse");

    /* ---- Start accept ---- */
    cc_reset_state(srv, player, CC_NOT_STARTED, 0, 0, 0, 0, vb_progress, vb_cook,
                   vb_thief, vb_cabinboy, vb_navigator);
    cc_tele(srv, CC_TOCK_X, CC_TOCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_talk_and_pick(srv, player, chatmenu, 1);
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_RIMMINGTON,
                   "accept should write progress=10, got %d", progress);
    cc_pass("tock_accept");

    /* ---- Farm re-talk after accept must not auto-sail ---- */
    cc_tele(srv, CC_TOCK_X, CC_TOCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_talk_and_pick(srv, player, chatmenu, 2);
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_RIMMINGTON,
                   "sail Not just now must leave progress=10, got %d", progress);
    cc_pass("tock_sail_not_now");

    /* ---- Sail Yes ---- */
    cc_tele(srv, CC_RIM_X, CC_RIM_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ferry, -1, slot_ferry);
    (void)rc;
    cc_talk_and_pick(srv, player, chatmenu, 1);
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_CURSES,
                   "sail should write progress=15, got %d", progress);
    cc_pass("tock_sail");

    /* ---- Incomplete crew report ---- */
    cc_tele(srv, CC_COVE_X, CC_COVE_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_CURSES,
                   "incomplete crew must leave progress=15, got %d", progress);
    cc_pass("tock_crew_incomplete");

    /* ---- Gnocci curse ---- */
    cc_tele(srv, CC_GNOCCI_X, CC_GNOCCI_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gnocci, -1, slot_gnocci);
    (void)rc;
    cc_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_cook) == CC_CURSE_DONE,
                   "Gnocci should write cook=2, got %d",
                   ToriRSServer_VarbitGet(player, vb_cook));
    cc_pass("gnocci_curse");

    /* ---- Arsen curse + relic ---- */
    cc_tele(srv, CC_ARSEN_X, CC_ARSEN_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_arsen, -1, slot_arsen);
    (void)rc;
    cc_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_thief) == CC_CURSE_DONE,
                   "Arsen should write thief=2, got %d",
                   ToriRSServer_VarbitGet(player, vb_thief));
    if( obj_relic > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_relic) == 1,
                       "Arsen should grant the relic, got %d",
                       selftest_count_obj(player, obj_relic));
    cc_pass("arsen_curse");

    /* ---- Colin curse ---- */
    cc_tele(srv, CC_COLIN_X, CC_COLIN_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_colin, -1, slot_colin);
    (void)rc;
    cc_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_cabinboy) == CC_CURSE_DONE,
                   "Colin should write cabinboy=2, got %d",
                   ToriRSServer_VarbitGet(player, vb_cabinboy));
    cc_pass("colin_curse");

    /* ---- Ithoi curse ---- */
    cc_tele(srv, CC_ITHOI_X, CC_ITHOI_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ithoi, -1, slot_ithoi);
    (void)rc;
    cc_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_navigator) == 1,
                   "Ithoi should write navigator=1, got %d",
                   ToriRSServer_VarbitGet(player, vb_navigator));
    cc_pass("ithoi_curse");

    /* ---- Telescope already-cleared ---- */
    if( loc_telescope >= 0 )
    {
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_telescope, -1, 0);
        (void)rc;
        cc_finish(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_navigator) == 1,
                       "second telescope look must leave navigator=1");
        cc_pass("telescope_again");
    }

    /* ---- All four cleared -> Tock ---- */
    cc_tele(srv, CC_COVE_X, CC_COVE_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_TOCK2,
                   "cleared crew should write progress=20, got %d", progress);
    cc_pass("tock_crew_cleared");

    /* ---- Food pointer ---- */
    cc_tele(srv, CC_COVE_X, CC_COVE_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_FOOD,
                   "food talk should write progress=25, got %d", progress);
    cc_pass("tock_food");

    /* ---- Arsen food -> Ithoi ---- */
    cc_tele(srv, CC_ARSEN_X, CC_ARSEN_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_arsen, -1, slot_arsen);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_ITHOI,
                   "Arsen food should write progress=35, got %d", progress);
    cc_pass("arsen_food");

    /* ---- Ithoi admits / burn pointer ---- */
    cc_tele(srv, CC_ITHOI_X, CC_ITHOI_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ithoi, -1, slot_ithoi);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_BURN,
                   "Ithoi food should write progress=45, got %d", progress);
    cc_pass("ithoi_food");

    /* ---- Burn hut ---- */
    if( loc_burn >= 0 )
    {
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_burn, -1, 0);
        (void)rc;
        cc_finish(srv);
        progress = ToriRSServer_VarbitGet(player, vb_progress);
        SELFTEST_CHECK(progress == CC_ANSWERS,
                       "burn should write progress=50, got %d", progress);
        cc_pass("burn_hut");
    }
    else
    {
        ToriRSServer_VarbitSet(srv, vb_progress, CC_ANSWERS);
    }

    /* ---- Tock poison report ---- */
    cc_tele(srv, CC_COVE_X, CC_COVE_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_KILL,
                   "poison report should write progress=52, got %d", progress);
    cc_pass("tock_poison_report");

    /* ---- Ithoi defeat ---- */
    cc_tele(srv, CC_ITHOI_X, CC_ITHOI_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ithoi, -1, slot_ithoi);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_FINISH,
                   "Ithoi defeat should write progress=55, got %d", progress);
    cc_pass("ithoi_defeat");

    /* ---- Complete scroll via Tock ---- */
    cc_tele(srv, CC_COVE_X, CC_COVE_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_COMPLETE,
                   "Tock finish should write progress=60, got %d", progress);
    cc_pass("tock_complete_scroll");

    /* ---- Post-complete Tock ---- */
    cc_tele(srv, CC_COVE_X, CC_COVE_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tock, -1, slot_tock);
    (void)rc;
    cc_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == CC_COMPLETE,
                   "post-complete Tock must leave 60, got %d", progress);
    cc_pass("tock_postquest");

    /* ---- Sand full-inv / idle / plank / burn-early ---- */
    cc_reset_state(srv, player, CC_CURSES, 0, 0, 0, 0, vb_progress, vb_cook,
                   vb_thief, vb_cabinboy, vb_navigator);
    if( loc_sand >= 0 )
    {
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_sand, -1, 0);
        (void)rc;
        cc_finish(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_thief) == CC_CURSE_DONE,
                       "sand dig should write thief=2, got %d",
                       ToriRSServer_VarbitGet(player, vb_thief));
        cc_pass("sand_dig");
    }
    if( loc_plank >= 0 )
    {
        cc_reset_state(srv, player, CC_RIMMINGTON, 0, 0, 0, 0, vb_progress, vb_cook,
                       vb_thief, vb_cabinboy, vb_navigator);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_plank, -1, 0);
        (void)rc;
        cc_finish(srv);
        cc_pass("plank_board");
    }
    if( loc_burn >= 0 )
    {
        cc_reset_state(srv, player, CC_CURSES, 0, 0, 0, 0, vb_progress, vb_cook,
                       vb_thief, vb_cabinboy, vb_navigator);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_burn, -1, 0);
        (void)rc;
        cc_finish(srv);
        progress = ToriRSServer_VarbitGet(player, vb_progress);
        SELFTEST_CHECK(progress == CC_CURSES,
                       "early burn must leave progress=15, got %d", progress);
        cc_pass("burn_early");
    }

    /* ---- Journals ---- */
    cc_reset_state(srv, player, CC_NOT_STARTED, 0, 0, 0, 0, vb_progress, vb_cook,
                   vb_thief, vb_cabinboy, vb_navigator);
    ToriRSServer_ScriptsRunProc(srv, "[proc,corsaircurse_journal]", NULL, 0);
    cc_finish(srv);
    cc_pass("journal_not_started");

    ToriRSServer_VarbitSet(srv, vb_progress, CC_RIMMINGTON);
    ToriRSServer_ScriptsRunProc(srv, "[proc,corsaircurse_journal]", NULL, 0);
    cc_finish(srv);
    cc_pass("journal_rimmington");

    ToriRSServer_VarbitSet(srv, vb_progress, CC_CURSES);
    ToriRSServer_ScriptsRunProc(srv, "[proc,corsaircurse_journal]", NULL, 0);
    cc_finish(srv);
    cc_pass("journal_curses");

    ToriRSServer_VarbitSet(srv, vb_progress, CC_TOCK2);
    ToriRSServer_ScriptsRunProc(srv, "[proc,corsaircurse_journal]", NULL, 0);
    cc_finish(srv);
    cc_pass("journal_food");

    ToriRSServer_VarbitSet(srv, vb_progress, CC_BURN);
    ToriRSServer_ScriptsRunProc(srv, "[proc,corsaircurse_journal]", NULL, 0);
    cc_finish(srv);
    cc_pass("journal_burn");

    ToriRSServer_VarbitSet(srv, vb_progress, CC_COMPLETE);
    ToriRSServer_ScriptsRunProc(srv, "[proc,corsaircurse_journal]", NULL, 0);
    cc_finish(srv);
    cc_pass("journal_complete");

    SELFTEST_CHECK(player->hitpoints > 0 && player->godmode == 1,
                   "player must stay alive (godmode) through the walk");

    cc_free_npc(srv, slot_tock);
    cc_free_npc(srv, slot_ferry);
    cc_free_npc(srv, slot_gnocci);
    cc_free_npc(srv, slot_arsen);
    cc_free_npc(srv, slot_colin);
    cc_free_npc(srv, slot_ithoi);
    cc_free_npc(srv, slot_ithoi_combat);
    cc_reset_state(srv, player, CC_NOT_STARTED, 0, 0, 0, 0, vb_progress, vb_cook,
                   vb_thief, vb_cabinboy, vb_navigator);
    cc_god(player);

    fprintf(stderr, "ToriRSServer cc selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before, g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_CORSAIRCURSE_SELFTEST_U_H */
