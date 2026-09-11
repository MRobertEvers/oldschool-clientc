/*
 * The Path of Glouphrie Gate D C-walk. Worker-only; parent will not merge
 * this header onto v3. Included immediately before the shop
 * selftest_reset_world. TORIRSSERVER_SELFTEST_POG_ONLY jumps here.
 *
 * Qualify is split (one named mesbox per gate) and runs before %pog is
 * written. The Bolren p_choice2 strings stay exact. Refuse must not write
 * %pog. Unique MERGE hunks: none.
 */
{
    int loaded;
    int pog_bit;
    int eyeglo_bit;
    int waterfall_varp;
    int tree_varp;
    int roving_varp;
    int bolren_type;
    int chatmenu;
    int32_t why;
    int slot;
    int pog_complete;

    fprintf(stderr, "ToriRSServer selftest: The Path of Glouphrie\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Path of Glouphrie selftest needs a compiled script pack");
    if( !loaded )
        goto pog_selftest_done;

    player->godmode = 1;
    pog_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "pog");
    eyeglo_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eyeglo_quest");
    waterfall_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "waterfall_quest");
    tree_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "treequest");
    roving_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "rovingelves_quest");
    bolren_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "king_bolren");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    pog_complete = ToriRSServer_ContentConstantInt("pog_complete", 50);

    SELFTEST_CHECK(pog_bit >= 0, "varbit pog should resolve");
    SELFTEST_CHECK(eyeglo_bit >= 0, "varbit eyeglo_quest should resolve");
    SELFTEST_CHECK(waterfall_varp >= 0, "varp waterfall_quest should resolve");
    SELFTEST_CHECK(tree_varp >= 0, "varp treequest should resolve");
    SELFTEST_CHECK(roving_varp >= 0, "varp rovingelves_quest should resolve");
    SELFTEST_CHECK(bolren_type > 0, "npc king_bolren should resolve");
    SELFTEST_CHECK(chatmenu > 0, "chatmenu:options should resolve");
    SELFTEST_CHECK(pog_complete == 50, "pog_complete should be 50, got %d", pog_complete);

    /* Fresh player at King Bolren (0_39_49_46_33 = 2542, 3169). */
    selftest_reset_world(srv, player, 2542, 3169);
    player->godmode = 1;
    ToriRSServer_VarbitSet(srv, pog_bit, 0);
    ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_STRENGTH, 1);
    ToriRSServer_CombatSetLevel(player, 18, 1); /* slayer */
    ToriRSServer_CombatSetLevel(player, 17, 1); /* thieving */
    ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_RANGED, 1);
    ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 1);
    if( eyeglo_bit >= 0 )
        ToriRSServer_VarbitSet(srv, eyeglo_bit, 0);
    if( waterfall_varp >= 0 )
        player->varps[waterfall_varp] = 0;
    if( tree_varp >= 0 )
        player->varps[tree_varp] = 0;

    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer");
    SELFTEST_CHECK(why == 1, "missing Eyes of Glouphrie is qualify 1, got %d", why);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pog_bit) == 0,
                   "qualify must not write pog, got %d",
                   ToriRSServer_VarbitGet(player, pog_bit));

    if( eyeglo_bit >= 0 )
        ToriRSServer_VarbitSet(srv, eyeglo_bit, 60);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer after Eyes");
    SELFTEST_CHECK(why == 2, "missing Waterfall Quest is qualify 2, got %d", why);

    if( waterfall_varp >= 0 )
        player->varps[waterfall_varp] = 10;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer after Waterfall");
    SELFTEST_CHECK(why == 3, "missing Tree Gnome Village is qualify 3, got %d", why);

    if( tree_varp >= 0 )
        player->varps[tree_varp] = 9;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer after Tree");
    SELFTEST_CHECK(why == 4, "Strength 60 is qualify 4, got %d", why);

    ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_STRENGTH, 60);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer after Strength");
    SELFTEST_CHECK(why == 5, "Slayer 56 is qualify 5, got %d", why);

    ToriRSServer_CombatSetLevel(player, 18, 56);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer after Slayer");
    SELFTEST_CHECK(why == 6, "Thieving 56 is qualify 6, got %d", why);

    ToriRSServer_CombatSetLevel(player, 17, 56);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer after Thieving");
    SELFTEST_CHECK(why == 7, "Ranged 47 is qualify 7, got %d", why);

    ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_RANGED, 47);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer after Ranged");
    SELFTEST_CHECK(why == 8, "Agility 45 is qualify 8, got %d", why);

    ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 45);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,pog_qualify_fail_reason]", NULL, 0, &why),
                   "pog_qualify_fail_reason should answer when qualified");
    SELFTEST_CHECK(why == 0, "all gates clear is qualify 0, got %d", why);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pog_bit) == 0,
                   "qualify-before-start must leave pog at 0, got %d",
                   ToriRSServer_VarbitGet(player, pog_bit));

    /* Named mesbox per gate: each fail reason parks on ~mesbox. */
    if( eyeglo_bit >= 0 )
        ToriRSServer_VarbitSet(srv, eyeglo_bit, 0);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(
                       srv, "[proc,pog_show_qualify_fail]", NULL, 0),
                   "Eyes qualify mesbox should run");
    player->active_script = NULL;
    if( eyeglo_bit >= 0 )
        ToriRSServer_VarbitSet(srv, eyeglo_bit, 60);
    if( waterfall_varp >= 0 )
        player->varps[waterfall_varp] = 0;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(
                       srv, "[proc,pog_show_qualify_fail]", NULL, 0),
                   "Waterfall qualify mesbox should run");
    player->active_script = NULL;
    if( waterfall_varp >= 0 )
        player->varps[waterfall_varp] = 10;
    if( tree_varp >= 0 )
        player->varps[tree_varp] = 0;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(
                       srv, "[proc,pog_show_qualify_fail]", NULL, 0),
                   "Tree Gnome Village qualify mesbox should run");
    player->active_script = NULL;
    if( tree_varp >= 0 )
        player->varps[tree_varp] = 9;

    /* Bolren hub: refuse must not write %pog. */
    if( roving_varp >= 0 )
        player->varps[roving_varp] = 60;
    slot = -1;
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        struct ToriRSServerNpc* n = &srv->npcs[i];
        int dx;
        int dz;

        if( !n->active )
        {
            slot = ToriRSServer_WorldNpcSpawn(
                srv, bolren_type, player->x + 1, player->z, player->level);
            break;
        }
        dx = n->x - player->x;
        dz = n->z - player->z;
        if( dx > 32 || dx < -32 || dz > 32 || dz < -32 )
        {
            ToriRSServer_WorldNpcOccupancy(n, 0);
            n->active = 0;
            slot = ToriRSServer_WorldNpcSpawn(
                srv, bolren_type, player->x + 1, player->z, player->level);
            break;
        }
    }
    SELFTEST_CHECK(slot >= 0, "King Bolren should be spawnable");
    ToriRSServer_VarbitSet(srv, pog_bit, 0);
    player->active_script = NULL;
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunTrigger(
            srv, SS_TRIGGER_OPNPC1, bolren_type, -1, slot) == TORIRSSERVER_TRIGGER_RAN,
        "[opnpc1,king_bolren] should reach pog_king_bolren_hub");
    SELFTEST_CHECK(player->active_script != NULL,
                   "qualified Bolren hello should park on chat");
    selftest_click_through(srv, 4);
    SELFTEST_CHECK(player->active_script != NULL,
                   "Bolren should park on the existing p_choice2");
    SELFTEST_CHECK(
        player->resume_button_count == 1 && player->resume_buttons[0] == chatmenu,
        "Bolren offer should arm chatmenu:options");
    {
        uint8_t button[6];

        button[0] = (uint8_t)(chatmenu >> 24);
        button[1] = (uint8_t)(chatmenu >> 16);
        button[2] = (uint8_t)(chatmenu >> 8);
        button[3] = (uint8_t)chatmenu;
        button[4] = 0;
        button[5] = 2; /* "I'm sure it's nothing." */
        selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    }
    selftest_click_through(srv, 4);
    player->active_script = NULL;
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pog_bit) == 0,
                   "refuse must not write pog, got %d",
                   ToriRSServer_VarbitGet(player, pog_bit));

    /* Accept writes ^pog_told_evil_creature = 2. */
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunTrigger(
            srv, SS_TRIGGER_OPNPC1, bolren_type, -1, slot) == TORIRSSERVER_TRIGGER_RAN,
        "Bolren re-talk after refuse should run");
    selftest_click_through(srv, 8);
    player->active_script = NULL;
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pog_bit) == 2,
                   "accept should write pog=2, got %d",
                   ToriRSServer_VarbitGet(player, pog_bit));

    /* Debugprocs stay live. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "pog") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::pog should reach content");
    player->active_script = NULL;
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pog_bit) == 0,
                   "::pog should reset to not_started, got %d",
                   ToriRSServer_VarbitGet(player, pog_bit));

    {
        static struct ToriRSServerCapture pogrun_capture;
        int pogrun_ok = 0;

        ToriRSServer_CaptureBegin(srv, &pogrun_capture);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "pogrun") ==
                           TORIRSSERVER_TRIGGER_RAN,
                       "::pogrun should reach content");
        ToriRSServer_CaptureEnd(srv);
        for( int i = ToriRSServer_CaptureFindNamed(&pogrun_capture, PKT_NAME_MESSAGE_GAME, 0);
             i >= 0;
             i = ToriRSServer_CaptureFindNamed(&pogrun_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const char* text = selftest_message_text(srv, &pogrun_capture.packets[i]);

            if( !text )
                continue;
            if( strstr(text, "softrun OK") != NULL )
                pogrun_ok = 1;
        }
        SELFTEST_CHECK(pogrun_ok, "::pogrun should reach softrun OK");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pog_bit) == pog_complete,
                       "::pogrun should finish at pog_complete=%d, got %d",
                       pog_complete, ToriRSServer_VarbitGet(player, pog_bit));
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
    }

    /* Leftover stamps (allowed names only). */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_leftover_monolith_puzzle") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_monolith_puzzle should run");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_leftover_yewnock_disc_machine") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_yewnock_disc_machine should run");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_leftover_dumpling_cutscene") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_dumpling_cutscene should run");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_leftover_terrorbird_instance") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_terrorbird_instance should run");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_leftover_lamp_rub_ui") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_lamp_rub_ui should run");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_leftover_full_refuse_trees") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_full_refuse_trees should run");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_leftover_spirit_tree_visual") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_spirit_tree_visual should run");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_leftover_grapple_crossing") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_grapple_crossing should run");
    player->active_script = NULL;

    /* Journal complete prints QUEST COMPLETE! */
    ToriRSServer_VarbitSet(srv, pog_bit, pog_complete);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,pog_journal]", NULL, 0),
                   "pog_journal should run at complete");
    player->active_script = NULL;
    ToriRSServer_WorldCloseModal(srv);

    /* BMP debugprocs used for Gate D captures must bind. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_01_qualify_fail_eyeglo") == TORIRSSERVER_TRIGGER_RAN,
                   "pogbmp_01_qualify_fail_eyeglo should bind");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_11_bolren_offer_p_choice2") == TORIRSSERVER_TRIGGER_RAN,
                   "pogbmp_11_bolren_offer_p_choice2 should bind");
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(
                       srv, "pogbmp_78_complete_scroll") == TORIRSSERVER_TRIGGER_RAN,
                   "pogbmp_78_complete_scroll should bind");
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pog_bit) == pog_complete,
                   "complete scroll should leave pog at %d, got %d",
                   pog_complete, ToriRSServer_VarbitGet(player, pog_bit));

pog_selftest_done:
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
}
