#ifndef TORIRSSERVER_TEST_QUEST_BEARYOURSOUL_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_BEARYOURSOUL_SELFTEST_U_H

/* Bear Your Soul (miniquest) Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Aretha / Key Master cannot leak.
 * Real OPHELD1 (Soul journey, spade) and OPNPC1 (Aretha, Key Master)
 * on the authored path. player->godmode = 1 for the whole walk (no
 * death case). Completion goes through ~bys_quest_complete via the
 * Key Master's existing [opnpc1,keeper_of_keys] splice.
 *
 * Start is a book read, not an NPC offer. Aretha uses p_choice2
 * Yes / Not now. Client of Kourend is not a hard start gate.
 *
 * Gate: TORIRSSERVER_SELFTEST_BYS_ONLY=1
 */

#define BYS_NOT_STARTED 0
#define BYS_ARETHA 1
#define BYS_REPAIR 2
#define BYS_COMPLETE 3

#define BYS_ARETHA_X 1814
#define BYS_ARETHA_Z 3851
#define BYS_DIG_X 1699
#define BYS_DIG_Z 3794
#define BYS_KEY_X 1310
#define BYS_KEY_Z 1251

static void
bys_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "BYS PASS: %s\n", step);
}

static void
bys_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
bys_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
bys_finish(struct ToriRSServer* srv)
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
bys_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    bys_god(player);
    selftest_tick(srv);
}

static int
bys_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    bys_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
bys_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
bys_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, obj_id, 1);
}

static void
bys_reset_state(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int story)
{
    int vb;

    assert(srv);
    assert(player);
    vb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "arceuus_soulbearer_story");
    bys_clear_inv(player);
    if( vb >= 0 )
        ToriRSServer_VarbitSet(srv, vb, story);
    bys_god(player);
}

static int
bys_find_obj(const struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            return s;
    }
    return -1;
}

static void
bys_talk_and_pick(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int chatmenu,
                  int row)
{
    assert(srv);
    assert(player);
    biohazard_run_dialogue(srv, player, chatmenu);
    if( player->active_script && chatmenu > 0 && row > 0 )
        selftest_charter_choose(srv, row);
    biohazard_run_dialogue(srv, player, chatmenu);
}

static void
selftest_quest_bearyoursoul(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int vb_story;
    int npc_aretha;
    int npc_key;
    int obj_book;
    int obj_spade;
    int obj_damaged;
    int obj_bearer;
    int obj_pot;
    int chatmenu;
    int aretha_slot;
    int key_slot;
    int rc;
    int story;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: ::bearyoursoul / Bear Your Soul\n");

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

    vb_story = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "arceuus_soulbearer_story");
    npc_aretha = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "arceuus_soulguardian");
    npc_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "keeper_of_keys");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "arceuus_library_soulbearerbook");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
    obj_damaged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "arceuus_soulbearer_damaged");
    obj_bearer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "arceuus_soulbearer");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(vb_story >= 0, "arceuus_soulbearer_story varbit should resolve");
    SELFTEST_CHECK(npc_aretha >= 0, "arceuus_soulguardian (Aretha) should resolve");
    SELFTEST_CHECK(npc_key >= 0, "keeper_of_keys (Key Master) should resolve");
    SELFTEST_CHECK(obj_book >= 0, "arceuus_library_soulbearerbook should resolve");
    SELFTEST_CHECK(obj_spade >= 0, "spade should resolve");
    SELFTEST_CHECK(obj_damaged >= 0, "arceuus_soulbearer_damaged should resolve");
    SELFTEST_CHECK(obj_bearer >= 0, "arceuus_soulbearer should resolve");
    if( vb_story < 0 || npc_aretha < 0 || npc_key < 0 || obj_book < 0 ||
        obj_spade < 0 || obj_damaged < 0 || obj_bearer < 0 )
    {
        fprintf(stderr, "  SKIP  missing Bear Your Soul symbols\n");
        return;
    }

    aretha_slot = bys_spawn(srv, npc_aretha, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    key_slot = bys_spawn(srv, npc_key, BYS_KEY_X, BYS_KEY_Z, 0);
    SELFTEST_CHECK(aretha_slot >= 0, "Aretha should spawn");
    SELFTEST_CHECK(key_slot >= 0, "Key Master should spawn");

    /* ---- Book read start (the natural start, not an NPC offer) ---- */
    bys_reset_state(srv, player, BYS_NOT_STARTED);
    inv_set(player, 0, obj_book, 1);
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    selftest_opheld(srv, 1, 0);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_ARETHA,
                   "OPHELD1 Soul journey first read should write story=1, got %d", story);
    bys_pass("book_read_start");

    /* ---- Re-read does not reset ---- */
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    selftest_opheld(srv, 1, 0);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_ARETHA, "re-read must leave story=1, got %d", story);
    bys_pass("book_already_read");

    /* ---- Aretha before the book ---- */
    bys_reset_state(srv, player, BYS_NOT_STARTED);
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aretha, -1, aretha_slot);
    bys_finish(srv);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 Aretha before read should run, got %d", rc);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_NOT_STARTED,
                   "Aretha before read must not start the miniquest, got %d", story);
    bys_pass("aretha_before_read");

    /* ---- Refuse: Not now. ---- */
    bys_reset_state(srv, player, BYS_ARETHA);
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aretha, -1, aretha_slot);
    bys_talk_and_pick(srv, player, chatmenu, 2);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_ARETHA, "Not now must leave story=1, got %d", story);
    bys_pass("aretha_refuse");

    /* ---- Accept: Yes. ---- */
    bys_reset_state(srv, player, BYS_ARETHA);
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aretha, -1, aretha_slot);
    bys_talk_and_pick(srv, player, chatmenu, 1);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_REPAIR, "Yes should write story=2, got %d", story);
    bys_pass("aretha_accept");

    /* ---- Mid-repair reminder ---- */
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aretha, -1, aretha_slot);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_REPAIR, "mid-repair reminder must leave story=2, got %d",
                   story);
    bys_pass("aretha_mid_dig");

    /* ---- Dig outside the crypt must not grant ---- */
    bys_reset_state(srv, player, BYS_REPAIR);
    inv_set(player, 0, obj_spade, 1);
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    selftest_opheld(srv, 1, 0);
    bys_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_damaged) == 0,
                   "dig outside the crypt must not grant a damaged Soul Bearer");
    bys_pass("dig_outside_crypt");

    /* ---- Full inventory in the crypt ---- */
    bys_reset_state(srv, player, BYS_REPAIR);
    if( obj_pot > 0 )
        bys_fill_inv(player, obj_pot);
    else
        bys_fill_inv(player, obj_spade);
    bys_tele(srv, BYS_DIG_X, BYS_DIG_Z, 0);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bys_try_dig]", NULL, 0);
    bys_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_damaged) == 0,
                   "full inv must not receive a damaged Soul Bearer");
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_REPAIR, "full-inv dig must leave story=2, got %d", story);
    bys_pass("dig_full_inv");

    /* ---- Crypt dig via shared spade OPHELD1 ---- */
    bys_reset_state(srv, player, BYS_REPAIR);
    inv_set(player, 0, obj_spade, 1);
    bys_tele(srv, BYS_DIG_X, BYS_DIG_Z, 0);
    selftest_opheld(srv, 1, 0);
    bys_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_damaged) == 1,
                   "crypt dig should grant one damaged Soul Bearer, got %d",
                   selftest_count_obj(player, obj_damaged));
    bys_pass("dig_crypt");

    /* ---- Already have damaged ---- */
    bys_tele(srv, BYS_DIG_X, BYS_DIG_Z, 0);
    selftest_opheld(srv, 1, 0);
    bys_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_damaged) == 1,
                   "second crypt dig must not duplicate the damaged bearer, got %d",
                   selftest_count_obj(player, obj_damaged));
    bys_pass("dig_already_have");

    /* ---- Damaged Check ---- */
    rc = bys_find_obj(player, obj_damaged);
    SELFTEST_CHECK(rc >= 0, "damaged Soul Bearer should be in inv for Check");
    if( rc >= 0 )
        selftest_opheld(srv, 3, rc);
    bys_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_damaged) == 1,
                   "Check must leave the damaged Soul Bearer");
    bys_pass("damaged_check");

    /* ---- Aretha after dig ---- */
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aretha, -1, aretha_slot);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_REPAIR, "after-dig Aretha must leave story=2, got %d",
                   story);
    bys_pass("aretha_after_dig");

    /* ---- Key Master with no damaged item ---- */
    bys_reset_state(srv, player, BYS_REPAIR);
    bys_tele(srv, BYS_KEY_X, BYS_KEY_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_key, -1, key_slot);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_REPAIR, "Key Master without the item must leave story=2, got %d",
                   story);
    SELFTEST_CHECK(selftest_count_obj(player, obj_bearer) == 0,
                   "Key Master without the item must not grant a Soul Bearer");
    bys_pass("keymaster_no_item");

    /* ---- Key Master repair + authored complete scroll ---- */
    bys_reset_state(srv, player, BYS_REPAIR);
    inv_set(player, 0, obj_damaged, 1);
    bys_tele(srv, BYS_KEY_X, BYS_KEY_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_key, -1, key_slot);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_COMPLETE, "repair should complete story=3, got %d", story);
    SELFTEST_CHECK(selftest_count_obj(player, obj_damaged) == 0,
                   "repair should consume the damaged Soul Bearer");
    SELFTEST_CHECK(selftest_count_obj(player, obj_bearer) == 1,
                   "complete should grant one Soul Bearer, got %d",
                   selftest_count_obj(player, obj_bearer));
    bys_pass("keymaster_repair_complete_scroll");

    /* ---- Post-complete Aretha ---- */
    bys_tele(srv, BYS_ARETHA_X, BYS_ARETHA_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aretha, -1, aretha_slot);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_COMPLETE, "post-complete Aretha must leave story=3, got %d",
                   story);
    bys_pass("aretha_complete");

    /* ---- Post-complete Key Master ---- */
    bys_tele(srv, BYS_KEY_X, BYS_KEY_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_key, -1, key_slot);
    bys_finish(srv);
    story = ToriRSServer_VarbitGet(player, vb_story);
    SELFTEST_CHECK(story == BYS_COMPLETE, "post-complete Key Master must leave story=3, got %d",
                   story);
    bys_pass("keymaster_complete");

    /* ---- Soul bearer Check ---- */
    rc = bys_find_obj(player, obj_bearer);
    SELFTEST_CHECK(rc >= 0, "Soul Bearer should be in inv for Check");
    if( rc >= 0 )
        selftest_opheld(srv, 3, rc);
    bys_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_bearer) == 1,
                   "Check must leave the Soul Bearer");
    bys_pass("soulbearer_check");

    /* ---- Crypt reclaim when the reward is lost ---- */
    bys_clear_inv(player);
    inv_set(player, 0, obj_spade, 1);
    bys_tele(srv, BYS_DIG_X, BYS_DIG_Z, 0);
    selftest_opheld(srv, 1, 0);
    bys_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_bearer) == 1,
                   "post-complete crypt dig should reclaim one Soul Bearer, got %d",
                   selftest_count_obj(player, obj_bearer));
    bys_pass("dig_reclaim");

    /* ---- Journal at every authored state ---- */
    bys_reset_state(srv, player, BYS_NOT_STARTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bearyoursoul_journal]", NULL, 0);
    bys_finish(srv);
    bys_pass("journal_not_started");

    ToriRSServer_VarbitSet(srv, vb_story, BYS_ARETHA);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bearyoursoul_journal]", NULL, 0);
    bys_finish(srv);
    bys_pass("journal_aretha");

    ToriRSServer_VarbitSet(srv, vb_story, BYS_REPAIR);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bearyoursoul_journal]", NULL, 0);
    bys_finish(srv);
    bys_pass("journal_repair");

    ToriRSServer_VarbitSet(srv, vb_story, BYS_COMPLETE);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bearyoursoul_journal]", NULL, 0);
    bys_finish(srv);
    bys_pass("journal_complete");

    SELFTEST_CHECK(player->hitpoints > 0 && player->godmode == 1,
                   "player must stay alive (godmode) through the walk");

    bys_free_npc(srv, aretha_slot);
    bys_free_npc(srv, key_slot);
    bys_reset_state(srv, player, BYS_NOT_STARTED);
    bys_god(player);

    fprintf(stderr, "ToriRSServer bys selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before, g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_BEARYOURSOUL_SELFTEST_U_H */
