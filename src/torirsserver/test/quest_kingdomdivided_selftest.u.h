/*
 * A Kingdom Divided C-walk. Worker-branch only -- do not merge onto parent v3.
 *
 * Gate:
 *   TORIRSSERVER_SELFTEST_AKD_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0
 *   ./src/<obj>_opt/torirsserver --selftest
 *
 * Included immediately before the shop stanza. Focused runs goto this label
 * from the ONLY gates at the top of ToriRSServer_Selftest so the rest of the
 * suite does not run first. Full-suite runs skip this block unless the env
 * is set (no RNG shift).
 *
 * Player is unkillable for the whole walk (player->godmode = 1). There is
 * no death case.
 */
    if( getenv("TORIRSSERVER_SELFTEST_AKD_ONLY") )
    {
        int loaded;
        int akd_bit;
        int hosidiusquest;
        int piscquest;
        int arcquest;
        int lovaquest;
        int shayzienquest;
        int qp_varp;
        int martin_type;
        int book;
        int agility;
        int thieving;
        int woodcutting;
        int herblore;
        int mining;
        int crafting;
        int magic;
        int chatmenu;
        int qp_before;
        int slot;
        int pages;
        uint8_t button[6];

        fprintf(stderr, "ToriRSServer selftest: A Kingdom Divided\n");

        assert(srv);
        assert(player);

        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
        SELFTEST_CHECK(loaded, "AKD lane loads a compiled script pack");

        player->godmode = 1;
        SELFTEST_CHECK(player->godmode == 1, "AKD walk keeps the player unkillable");

        akd_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "akd");
        hosidiusquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosidiusquest");
        piscquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "piscquest");
        arcquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "arcquest");
        lovaquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lovaquest");
        shayzienquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "shayzienquest");
        qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        martin_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "akd_martin_holt_castle");
        book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "book_of_the_dead");
        agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
        thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
        woodcutting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
        herblore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
        mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
        crafting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
        magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
        chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

        SELFTEST_CHECK(akd_bit >= 0, "varbit akd resolves");
        SELFTEST_CHECK(hosidiusquest >= 0, "varbit hosidiusquest resolves");
        SELFTEST_CHECK(piscquest >= 0, "varbit piscquest resolves");
        SELFTEST_CHECK(arcquest >= 0, "varbit arcquest resolves");
        SELFTEST_CHECK(lovaquest >= 0, "varbit lovaquest resolves");
        SELFTEST_CHECK(shayzienquest >= 0, "varbit shayzienquest resolves");
        SELFTEST_CHECK(qp_varp >= 0, "varp qp resolves");
        SELFTEST_CHECK(martin_type > 0, "akd_martin_holt_castle resolves");
        SELFTEST_CHECK(book > 0, "book_of_the_dead resolves");
        SELFTEST_CHECK(agility >= 0 && thieving >= 0 && woodcutting >= 0 &&
                           herblore >= 0 && mining >= 0 && crafting >= 0 &&
                           magic >= 0,
                       "AKD skill symbols resolve");
        SELFTEST_CHECK(chatmenu > 0, "chatmenu:options resolves");

        /* --- qualify fail: missing Depths of Despair does not start --- */
        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        ToriRSServer_WorldSetActive(srv, player);
        if( agility >= 0 )
            player->stat_level[agility] = 99;
        if( thieving >= 0 )
            player->stat_level[thieving] = 99;
        if( woodcutting >= 0 )
            player->stat_level[woodcutting] = 99;
        if( herblore >= 0 )
            player->stat_level[herblore] = 99;
        if( mining >= 0 )
            player->stat_level[mining] = 99;
        if( crafting >= 0 )
            player->stat_level[crafting] = 99;
        if( magic >= 0 )
            player->stat_level[magic] = 99;
        if( hosidiusquest >= 0 )
            ToriRSServer_VarbitSet(srv, hosidiusquest, 0);
        if( piscquest >= 0 )
            ToriRSServer_VarbitSet(srv, piscquest, 13);
        if( arcquest >= 0 )
            ToriRSServer_VarbitSet(srv, arcquest, 14);
        if( lovaquest >= 0 )
            ToriRSServer_VarbitSet(srv, lovaquest, 11);
        if( shayzienquest >= 0 )
            ToriRSServer_VarbitSet(srv, shayzienquest, 17);
        if( akd_bit >= 0 )
            ToriRSServer_VarbitSet(srv, akd_bit, 0);
        slot = ToriRSServer_WorldNpcSpawn(srv, martin_type, player->x + 1, player->z,
                                          player->level);
        SELFTEST_CHECK(slot >= 0, "Martin Holt spawn for qualify-fail");
        if( slot >= 0 )
        {
            SELFTEST_CHECK(
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, martin_type, -1,
                                               slot) == TORIRSSERVER_TRIGGER_RAN,
                "[opnpc1,akd_martin_holt_castle] runs on qualify-fail");
            SELFTEST_CHECK(player->active_script != NULL,
                           "qualify-fail parks on the Depths of Despair mesbox");
            pages = selftest_click_through(srv, 8);
            SELFTEST_CHECK(akd_bit < 0 || ToriRSServer_VarbitGet(player, akd_bit) == 0,
                           "missing Depths of Despair must not write %%akd, got %d",
                           akd_bit >= 0 ? ToriRSServer_VarbitGet(player, akd_bit) : -1);
            (void)pages;
            ToriRSServer_ScriptsFree(srv);
            ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }

        /* --- qualify fail: Agility 54 is stat_base, not boostable --- */
        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        ToriRSServer_WorldSetActive(srv, player);
        if( agility >= 0 )
        {
            player->stat_level[agility] = 1;
            player->stat_boosted[agility] = 99;
        }
        if( thieving >= 0 )
            player->stat_level[thieving] = 99;
        if( woodcutting >= 0 )
            player->stat_level[woodcutting] = 99;
        if( herblore >= 0 )
            player->stat_level[herblore] = 99;
        if( mining >= 0 )
            player->stat_level[mining] = 99;
        if( crafting >= 0 )
            player->stat_level[crafting] = 99;
        if( magic >= 0 )
            player->stat_level[magic] = 99;
        if( hosidiusquest >= 0 )
            ToriRSServer_VarbitSet(srv, hosidiusquest, 11);
        if( piscquest >= 0 )
            ToriRSServer_VarbitSet(srv, piscquest, 13);
        if( arcquest >= 0 )
            ToriRSServer_VarbitSet(srv, arcquest, 14);
        if( lovaquest >= 0 )
            ToriRSServer_VarbitSet(srv, lovaquest, 11);
        if( shayzienquest >= 0 )
            ToriRSServer_VarbitSet(srv, shayzienquest, 17);
        if( akd_bit >= 0 )
            ToriRSServer_VarbitSet(srv, akd_bit, 0);
        slot = ToriRSServer_WorldNpcSpawn(srv, martin_type, player->x + 1, player->z,
                                          player->level);
        SELFTEST_CHECK(slot >= 0, "Martin Holt spawn for agility qualify-fail");
        if( slot >= 0 )
        {
            SELFTEST_CHECK(
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, martin_type, -1,
                                               slot) == TORIRSSERVER_TRIGGER_RAN,
                "[opnpc1,akd_martin_holt_castle] runs on agility qualify-fail");
            pages = selftest_click_through(srv, 8);
            SELFTEST_CHECK(akd_bit < 0 || ToriRSServer_VarbitGet(player, akd_bit) == 0,
                           "boosted Agility must not start AKD, got %d",
                           akd_bit >= 0 ? ToriRSServer_VarbitGet(player, akd_bit) : -1);
            (void)pages;
            ToriRSServer_ScriptsFree(srv);
            ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }

        /* --- refuse: Yes/Not now, picking Not now does not write %akd --- */
        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        ToriRSServer_WorldSetActive(srv, player);
        if( agility >= 0 )
            player->stat_level[agility] = 99;
        if( thieving >= 0 )
            player->stat_level[thieving] = 99;
        if( woodcutting >= 0 )
            player->stat_level[woodcutting] = 99;
        if( herblore >= 0 )
            player->stat_level[herblore] = 99;
        if( mining >= 0 )
            player->stat_level[mining] = 99;
        if( crafting >= 0 )
            player->stat_level[crafting] = 99;
        if( magic >= 0 )
            player->stat_level[magic] = 99;
        if( hosidiusquest >= 0 )
            ToriRSServer_VarbitSet(srv, hosidiusquest, 11);
        if( piscquest >= 0 )
            ToriRSServer_VarbitSet(srv, piscquest, 13);
        if( arcquest >= 0 )
            ToriRSServer_VarbitSet(srv, arcquest, 14);
        if( lovaquest >= 0 )
            ToriRSServer_VarbitSet(srv, lovaquest, 11);
        if( shayzienquest >= 0 )
            ToriRSServer_VarbitSet(srv, shayzienquest, 17);
        if( akd_bit >= 0 )
            ToriRSServer_VarbitSet(srv, akd_bit, 0);
        slot = ToriRSServer_WorldNpcSpawn(srv, martin_type, player->x + 1, player->z,
                                          player->level);
        SELFTEST_CHECK(slot >= 0, "Martin Holt spawn for refuse");
        if( slot >= 0 && chatmenu > 0 )
        {
            SELFTEST_CHECK(
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, martin_type, -1,
                                               slot) == TORIRSSERVER_TRIGGER_RAN,
                "[opnpc1,akd_martin_holt_castle] runs the offer");
            /* Drain hello + offer chatheads onto p_choice2. */
            selftest_click_through(srv, 2);
            SELFTEST_CHECK(player->active_script != NULL,
                           "Martin offer parks on p_choice2 Yes / Not now");
            SELFTEST_CHECK(
                player->resume_button_count == 1 && player->resume_buttons[0] == chatmenu,
                "Martin offer arms chatmenu:options");
            button[0] = (uint8_t)(chatmenu >> 24);
            button[1] = (uint8_t)(chatmenu >> 16);
            button[2] = (uint8_t)(chatmenu >> 8);
            button[3] = (uint8_t)chatmenu;
            button[4] = 0;
            button[5] = 2; /* Not now. */
            selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
            pages = selftest_click_through(srv, 8);
            SELFTEST_CHECK(akd_bit < 0 || ToriRSServer_VarbitGet(player, akd_bit) == 0,
                           "refusing Martin must not write %%akd, got %d",
                           akd_bit >= 0 ? ToriRSServer_VarbitGet(player, akd_bit) : -1);
            (void)pages;
            ToriRSServer_ScriptsFree(srv);
            ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }

        /* --- accept: Yes writes %akd = ^akd_fullore (4) --- */
        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        ToriRSServer_WorldSetActive(srv, player);
        if( agility >= 0 )
            player->stat_level[agility] = 99;
        if( thieving >= 0 )
            player->stat_level[thieving] = 99;
        if( woodcutting >= 0 )
            player->stat_level[woodcutting] = 99;
        if( herblore >= 0 )
            player->stat_level[herblore] = 99;
        if( mining >= 0 )
            player->stat_level[mining] = 99;
        if( crafting >= 0 )
            player->stat_level[crafting] = 99;
        if( magic >= 0 )
            player->stat_level[magic] = 99;
        if( hosidiusquest >= 0 )
            ToriRSServer_VarbitSet(srv, hosidiusquest, 11);
        if( piscquest >= 0 )
            ToriRSServer_VarbitSet(srv, piscquest, 13);
        if( arcquest >= 0 )
            ToriRSServer_VarbitSet(srv, arcquest, 14);
        if( lovaquest >= 0 )
            ToriRSServer_VarbitSet(srv, lovaquest, 11);
        if( shayzienquest >= 0 )
            ToriRSServer_VarbitSet(srv, shayzienquest, 17);
        if( akd_bit >= 0 )
            ToriRSServer_VarbitSet(srv, akd_bit, 0);
        slot = ToriRSServer_WorldNpcSpawn(srv, martin_type, player->x + 1, player->z,
                                          player->level);
        SELFTEST_CHECK(slot >= 0, "Martin Holt spawn for accept");
        if( slot >= 0 )
        {
            SELFTEST_CHECK(
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, martin_type, -1,
                                               slot) == TORIRSSERVER_TRIGGER_RAN,
                "[opnpc1,akd_martin_holt_castle] runs the accept path");
            pages = selftest_click_through(srv, 24);
            SELFTEST_CHECK(akd_bit < 0 || ToriRSServer_VarbitGet(player, akd_bit) == 4,
                           "accepting Martin writes %%akd = ^akd_fullore (4), got %d",
                           akd_bit >= 0 ? ToriRSServer_VarbitGet(player, akd_bit) : -1);
            (void)pages;
            ToriRSServer_ScriptsFree(srv);
            ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }

        /* --- ::akdrun headless walk to complete --- */
        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        ToriRSServer_WorldSetActive(srv, player);
        qp_before = (qp_varp >= 0) ? player->varps[qp_varp] : 0;
        if( akd_bit >= 0 )
            ToriRSServer_VarbitSet(srv, akd_bit, 0);
        ToriRSServer_ScriptsRunDebugproc(srv, "akdrun");
        pages = selftest_click_through(srv, 24);
        selftest_tick(srv);
        selftest_tick(srv);
        SELFTEST_CHECK(akd_bit < 0 || ToriRSServer_VarbitGet(player, akd_bit) == 150,
                       "::akdrun reaches ^akd_complete (150), got %d",
                       akd_bit >= 0 ? ToriRSServer_VarbitGet(player, akd_bit) : -1);
        SELFTEST_CHECK(book <= 0 || selftest_count_obj(player, book) >= 1,
                       "::akdrun grants Book of the Dead");
        if( qp_varp >= 0 )
        {
            SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 2,
                           "::akdrun grants 2 Quest Points, qp %d -> %d",
                           qp_before, player->varps[qp_varp]);
        }
        SELFTEST_CHECK(player->godmode == 1, "AKD complete still leaves the player unkillable");
        SELFTEST_CHECK(player->hitpoints > 0, "AKD walk must not kill the player");
        (void)pages;
        ToriRSServer_ScriptsFree(srv);

        fprintf(stderr,
                "ToriRSServer selftest: A Kingdom Divided %lu checks, %d failures\n",
                g_selftest_checks, g_selftest_failures);
        selftest_evidence_end("akd");
        return g_selftest_failures;
    }
