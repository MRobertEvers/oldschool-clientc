/* The Garden of Death C-walk. Included immediately before the shop stanza.
 * Do not merge this file onto parent v3.
 *
 * Gate: TORIRSSERVER_SELFTEST_TGOD_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0
 * TGOD_ONLY (not GOD_ONLY) so it does not collide with TORIRSSERVER_GOD=1.
 */
selftest_tgod_only:;
{
    int tgod_only = getenv("TORIRSSERVER_SELFTEST_TGOD_ONLY") != NULL;
    int loaded;
    int vb_tgod;
    int vb_translated;
    int loc_tent;
    int loc_camp;
    int loc_g1_entry;
    int loc_g1_table;
    int loc_boat;
    int loc_vines_inspect;
    int loc_vines_cut;
    int obj_journal;
    int obj_tablet1;
    int obj_tablet2;
    int obj_tablet3;
    int obj_tablet4;
    int obj_note;
    int obj_trans;
    int obj_secateurs;
    int obj_coins;
    int stat_farming;
    int if_gold;
    int if_silver;
    int if_flute;
    int spell_telegrab;
    int com_messagebox;
    int com_chatmenu;
    int32_t why;
    int xp_before;
    int s;
    int row;

    fprintf(stderr, "ToriRSServer selftest: The Garden of Death\n");
    assert(srv);
    assert(player);
    player->godmode = 1;
    ToriRSServer_WorldSetActive(srv, player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Garden of Death C-walk needs a compiled script pack");
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        if( tgod_only )
        {
            fprintf(stderr, "ToriRSServer tgod selftest: %lu checks, %d failures\n",
                    g_selftest_checks, g_selftest_failures);
            selftest_evidence_end("tgod");
            return g_selftest_failures;
        }
        goto tgod_walk_done;
    }

    vb_tgod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "tgod");
    vb_translated = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "tgod_fully_translated");
    loc_tent = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tgod_tent");
    loc_camp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tgod_camping_equipment");
    loc_g1_entry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tgod_garden_1_entry");
    loc_g1_table = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tgod_garden_1_tablet_table");
    loc_boat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "aerial_fishing_boat");
    loc_vines_inspect = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tgod_vines_inspect");
    loc_vines_cut = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tgod_vines_cut");
    obj_journal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tgod_journal");
    obj_tablet1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tgod_tablet_1");
    obj_tablet2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tgod_tablet_2");
    obj_tablet3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tgod_tablet_3");
    obj_tablet4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tgod_tablet_4");
    obj_note = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tgod_final_note");
    obj_trans = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tgod_translations");
    obj_secateurs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "secateurs");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    stat_farming = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "farming");
    if_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "crafting_gold");
    if_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "silver_crafting");
    if_flute = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "ratcatcher_flute");
    spell_telegrab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "law_rune");
    if( spell_telegrab < 0 )
        spell_telegrab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "air_rune");
    com_messagebox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
    com_chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(vb_tgod >= 0 && loc_tent >= 0 && obj_journal >= 0 &&
                       stat_farming >= 0 && loc_boat >= 0,
                   "Garden of Death cache names should resolve");
    SELFTEST_CHECK(if_gold >= 0, "jewellery furnace IF crafting_gold is present");
    SELFTEST_CHECK(if_silver >= 0, "jewellery furnace IF silver_crafting is present");
    SELFTEST_CHECK(if_flute == 282, "full flute widget is interface 282, got %d", if_flute);
    SELFTEST_CHECK(spell_telegrab >= 0, "Telekinetic Grab ingredients resolve");
    {
        int32_t day = -1;
        int date_ok = ToriRSServer_ScriptsRunProcInt(
            srv, "[proc,selftest_date_runeday]", NULL, 0, &day);
        SELFTEST_CHECK(date_ok && day >= 0, "date_runeday() answers, got %d ok=%d", day, date_ok);
    }

    if( vb_tgod < 0 || loc_tent < 0 || obj_journal < 0 || stat_farming < 0 )
        goto tgod_cleanup;

    selftest_clear_inv(player);
    ToriRSServer_VarbitSet(srv, vb_tgod, 0);
    if( vb_translated >= 0 )
        ToriRSServer_VarbitSet(srv, vb_translated, 0);
    ToriRSServer_CombatSetLevel(player, stat_farming, 1);
    player->stat_boosted[stat_farming] = 25; /* boost must not pass the gate */

    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,god_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 1,
                   "Farming 1 (boosted 25) fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    ToriRSServer_WorldTeleport(srv, 0, 1314, 3470);
    selftest_tick(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tent, -1, -1);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 0,
                   "tent must not start without Farming 20, tgod=%d",
                   ToriRSServer_VarbitGet(player, vb_tgod));
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    ToriRSServer_CombatSetLevel(player, stat_farming, 20);
    player->stat_boosted[stat_farming] = 20;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,god_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 0,
                   "Farming 20 qualifies, got %d", why);

    /* Refuse: Yes/No offer must not auto-start. */
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tent, -1, -1);
    if( com_messagebox > 0 )
        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
    if( com_chatmenu > 0 && player->active_script && player->resume_button_count > 0 )
    {
        uint8_t resume[6];
        int uid = player->resume_buttons[0];
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        resume[4] = 0;
        resume[5] = 2; /* No. */
        selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, resume, sizeof(resume));
    }
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 0,
                   "refusing the tent offer must leave tgod at 0, got %d",
                   ToriRSServer_VarbitGet(player, vb_tgod));
    SELFTEST_CHECK(selftest_count(player, obj_journal) == 0,
                   "refuse must not grant the journal");

    /* Accept. */
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tent, -1, -1);
    if( com_messagebox > 0 )
        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
    if( com_chatmenu > 0 && player->active_script && player->resume_button_count > 0 )
    {
        uint8_t resume[6];
        int uid = player->resume_buttons[0];
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        resume[4] = 0;
        resume[5] = 1; /* Yes. */
        selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, resume, sizeof(resume));
    }
    if( com_messagebox > 0 )
        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 2,
                   "accepting the tent offer writes ^god_journal=2, got %d",
                   ToriRSServer_VarbitGet(player, vb_tgod));
    SELFTEST_CHECK(selftest_count(player, obj_journal) == 1,
                   "accept grants tgod_journal");

    /* Journal read advances to ^god_t1 = 4. */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_journal, -1, -1);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 4,
                   "reading the journal writes ^god_t1=4, got %d",
                   ToriRSServer_VarbitGet(player, vb_tgod));

    /* Camping equipment grants secateurs. */
    if( loc_camp >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_camp, -1, -1);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(selftest_count(player, obj_secateurs) == 1,
                       "searching camping equipment grants secateurs");
    }

    /* Garden 1 entry without secateurs is blocked if we delete them first. */
    if( loc_g1_entry >= 0 && obj_secateurs >= 0 )
    {
        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        {
            if( player->inv[s].obj_id == obj_secateurs )
                inv_set(player, s, -1, 0);
        }
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_g1_entry, -1, -1);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 4,
                       "garden 1 without secateurs must not advance, tgod=%d",
                       ToriRSServer_VarbitGet(player, vb_tgod));
        selftest_give(player, obj_secateurs, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_g1_entry, -1, -1);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 6,
                       "garden 1 with secateurs writes 6, got %d",
                       ToriRSServer_VarbitGet(player, vb_tgod));
    }

    /* Tablet 1 + soft translate -> 16. */
    if( loc_g1_table >= 0 && obj_tablet1 >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_g1_table, -1, -1);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(selftest_count(player, obj_tablet1) == 1, "garden 1 table grants tablet 1");
        if( obj_trans >= 0 )
            SELFTEST_CHECK(selftest_count(player, obj_trans) == 1,
                           "garden 1 table grants the translations scroll");
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_tablet1, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 16,
                       "translating tablet 1 writes ^god_t1_done=16, got %d",
                       ToriRSServer_VarbitGet(player, vb_tgod));
    }

    /* Boaty header is this file's only copy. */
    if( loc_boat >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_boat, -1, -1);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
    }

    /* Vines inspect -> 22, cut -> 24. */
    if( loc_vines_inspect >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_tgod, 18);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_vines_inspect, -1, -1);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 22,
                       "inspecting vines writes ^god_vines=22, got %d",
                       ToriRSServer_VarbitGet(player, vb_tgod));
        if( loc_vines_cut >= 0 )
        {
            if( selftest_count(player, obj_secateurs) < 1 )
                selftest_give(player, obj_secateurs, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_vines_cut, -1, -1);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 24,
                           "cutting vines writes ^god_vines_cut=24, got %d",
                           ToriRSServer_VarbitGet(player, vb_tgod));
        }
    }

    /* Remaining gardens via the real tablet / note procs. */
    if( obj_tablet2 >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_tgod, 24);
        selftest_give(player, obj_tablet2, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_tablet2, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 30,
                       "tablet 2 writes ^god_t2_done=30, got %d",
                       ToriRSServer_VarbitGet(player, vb_tgod));
    }
    if( obj_tablet3 >= 0 )
    {
        selftest_give(player, obj_tablet3, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_tablet3, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 40,
                       "tablet 3 writes ^god_t3_done=40, got %d",
                       ToriRSServer_VarbitGet(player, vb_tgod));
    }
    if( obj_tablet4 >= 0 )
    {
        selftest_give(player, obj_tablet4, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_tablet4, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 50,
                       "tablet 4 first read writes ^god_final=50, got %d",
                       ToriRSServer_VarbitGet(player, vb_tgod));
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_tablet4, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 54,
                       "tablet 4 re-read writes ^god_warning=54, got %d",
                       ToriRSServer_VarbitGet(player, vb_tgod));
        SELFTEST_CHECK(selftest_count(player, obj_note) == 1, "re-read grants the warning note");
    }

    xp_before = player->stat_xp_tenths[stat_farming];
    if( obj_note >= 0 )
    {
        if( selftest_count(player, obj_note) < 1 )
            selftest_give(player, obj_note, 1);
        ToriRSServer_VarbitSet(srv, vb_tgod, 54);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_note, -1, -1);
        selftest_click_through(srv, 8);
        for( row = 0; row < 40 && ToriRSServer_VarbitGet(player, vb_tgod) != 56; row++ )
        {
            ToriRSServer_WorldCloseModal(srv);
            selftest_tick(srv);
        }
        player->active_script = NULL;
    }
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 56,
                   "reading the warning note completes at 56, got %d",
                   ToriRSServer_VarbitGet(player, vb_tgod));
    SELFTEST_CHECK(player->stat_xp_tenths[stat_farming] >= xp_before + 100000,
                   "completion awards 10000 Farming XP (100000 tenths), %d -> %d",
                   xp_before, player->stat_xp_tenths[stat_farming]);
    if( obj_coins >= 0 )
        SELFTEST_CHECK(selftest_count(player, obj_coins) > 0, "completion grants coins");
    if( vb_translated >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_translated) == 1,
                       "completion sets tgod_fully_translated");

    /* Leftover procs exist and park on a named mesbox. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,god_leftover_translation_if_804]", NULL, 0),
                   "leftover_translation_if_804 runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,god_leftover_boaty_dialog_matrix]", NULL, 0),
                   "leftover_boaty_dialog_matrix runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,god_leftover_poison_damage]", NULL, 0),
                   "leftover_poison_damage runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,god_leftover_full_tablet_note_ui]", NULL, 0),
                   "leftover_full_tablet_note_ui runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,god_leftover_full_refuse_trees]", NULL, 0),
                   "leftover_full_refuse_trees runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* ::godrun still reaches complete. */
    {
        static struct ToriRSServerCapture godrun_capture;
        int said_ok = 0;
        ToriRSServer_VarbitSet(srv, vb_tgod, 0);
        selftest_clear_inv(player);
        ToriRSServer_CaptureBegin(srv, &godrun_capture);
        ToriRSServer_ScriptsRunDebugproc(srv, "godrun");
        selftest_click_through(srv, 40);
        for( row = 0; row < 40 && ToriRSServer_VarbitGet(player, vb_tgod) != 56; row++ )
        {
            ToriRSServer_WorldCloseModal(srv);
            selftest_tick(srv);
        }
        ToriRSServer_CaptureEnd(srv);
        for( s = ToriRSServer_CaptureFindNamed(&godrun_capture, PKT_NAME_MESSAGE_GAME, 0); s >= 0;
             s = ToriRSServer_CaptureFindNamed(&godrun_capture, PKT_NAME_MESSAGE_GAME, s + 1) )
        {
            const struct ToriRSServerCapturePacket* pkt = &godrun_capture.packets[s];
            if( pkt->len > 8 && memmem(pkt->bytes, (size_t)pkt->len, "godrun OK", 9) )
                said_ok = 1;
        }
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_tgod) == 56 || said_ok,
                       "::godrun should complete, tgod=%d ok=%d",
                       ToriRSServer_VarbitGet(player, vb_tgod), said_ok);
    }

    SELFTEST_CHECK(player->godmode == 1, "player stays unkillable for the whole walk");

tgod_cleanup:
    selftest_clear_inv(player);
    if( vb_tgod >= 0 )
        ToriRSServer_VarbitSet(srv, vb_tgod, 0);
    player->active_script = NULL;
    ToriRSServer_WorldCloseModal(srv);

tgod_walk_done:
    if( tgod_only )
    {
        fprintf(stderr, "ToriRSServer tgod selftest: %lu checks, %d failures\n",
                g_selftest_checks, g_selftest_failures);
        selftest_evidence_end("tgod");
        return g_selftest_failures;
    }
}
