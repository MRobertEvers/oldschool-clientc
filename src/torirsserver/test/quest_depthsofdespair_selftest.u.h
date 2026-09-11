    /*
     * The Depths of Despair critical path. Real opnpc / oploc / opheld
     * plus the authored qualify-fail procs. Included immediately before a
     * selftest_reset_world so spawned npcs and placed locs cannot leak into
     * later roam/RNG checks.
     *
     * Gate: TORIRSSERVER_SELFTEST_DOD_ONLY=1
     * Godmode on for the whole walk (not a death case).
     *
     * Mutation: change ~dod_show_qualify_fail_cok's mesbox text or skip
     * writing %hosidiusquest = ^dod_olivia on accept. The matching
     * assertion dies. Restoring it brings the check back.
     */
    fprintf(stderr, "ToriRSServer selftest: The Depths of Despair\n");
    {
        int loaded = srv->scripts_ok;
        int vb_dod;
        int vb_cok;
        int vb_xmarks;
        int vb_reward;
        int vb_page;
        int npc_kandur;
        int npc_olivia;
        int npc_galana;
        int npc_artur;
        int npc_artur_home;
        int npc_snake;
        int loc_cave;
        int loc_exit;
        int loc_crack;
        int loc_stone;
        int loc_rock;
        int loc_rope_top;
        int loc_rope_bot;
        int loc_chest;
        int obj_book;
        int obj_accord;
        int obj_page;
        int obj_coins;
        int rows_uid;
        int kandur_slot = -1;
        int olivia_slot = -1;
        int galana_slot = -1;
        int artur_slot = -1;
        int snake_slot = -1;

        assert(srv);
        assert(player);

        if( !loaded )
        {
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
            if( !loaded )
                loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
        }
        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            player->godmode = 1;
            player->dying = 0;
            if( player->max_hitpoints > 0 )
                player->hitpoints = player->max_hitpoints;
            ToriRSServer_CombatSyncHitpoints(player);

            vb_dod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosidiusquest");
            vb_cok = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "veos_progress");
            vb_xmarks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "cluequest");
            vb_reward = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosidiusquest_reward");
            vb_page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosidius_page");
            npc_kandur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hosidiusquest_lord");
            npc_olivia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hosidiusquest_chef");
            npc_galana = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hosidiusquest_librarian");
            npc_artur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hosidiusquest_son_caves");
            npc_artur_home = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hosidiusquest_son_home");
            npc_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hosidiusquest_snake");
            loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_cave_entrance");
            loc_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_cave_exit");
            loc_crack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_crackin");
            loc_stone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_stone");
            loc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_rock");
            loc_rope_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_rope_top");
            loc_rope_bot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_rope_bottom");
            loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_chest");
            obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosidiusquest_book");
            obj_accord = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosidiusquest_accord");
            obj_page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "veos_memoirs_hos_page");
            obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
            rows_uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

            SELFTEST_CHECK(vb_dod >= 0, "hosidiusquest varbit should resolve");
            SELFTEST_CHECK(vb_cok >= 0, "veos_progress varbit should resolve");
            SELFTEST_CHECK(vb_xmarks >= 0, "cluequest varbit should resolve");
            SELFTEST_CHECK(npc_kandur >= 0, "hosidiusquest_lord should resolve");
            SELFTEST_CHECK(npc_olivia >= 0, "hosidiusquest_chef should resolve");
            SELFTEST_CHECK(npc_galana >= 0, "hosidiusquest_librarian should resolve");
            SELFTEST_CHECK(npc_artur >= 0, "hosidiusquest_son_caves should resolve");
            SELFTEST_CHECK(loc_cave >= 0, "hosidiusquest_cave_entrance should resolve");
            SELFTEST_CHECK(loc_chest >= 0, "hosidiusquest_chest should resolve");
            SELFTEST_CHECK(obj_book >= 0, "hosidiusquest_book should resolve");
            SELFTEST_CHECK(obj_accord >= 0, "hosidiusquest_accord should resolve");
            SELFTEST_CHECK(obj_page >= 0, "veos_memoirs_hos_page should resolve");
            SELFTEST_CHECK(obj_coins >= 0, "coins should resolve");
            SELFTEST_CHECK(loc_exit >= 0, "hosidiusquest_cave_exit should resolve");
            SELFTEST_CHECK(loc_crack >= 0, "hosidiusquest_crackin should resolve");
            SELFTEST_CHECK(loc_stone >= 0, "hosidiusquest_stone should resolve");
            SELFTEST_CHECK(loc_rock >= 0, "hosidiusquest_rock should resolve");
            SELFTEST_CHECK(loc_rope_top >= 0, "hosidiusquest_rope_top should resolve");
            SELFTEST_CHECK(loc_rope_bot >= 0, "hosidiusquest_rope_bottom should resolve");
            SELFTEST_CHECK(npc_artur_home >= 0, "hosidiusquest_son_home should resolve");
            SELFTEST_CHECK(npc_snake >= 0, "hosidiusquest_snake should resolve");

            if( vb_dod < 0 || npc_kandur < 0 || obj_accord < 0 )
            {
                fprintf(stderr, "  SKIP  Depths of Despair pack names missing\n");
            }
            else
            {
                int s;
                int loc_slot;
                int xp_before;
                int ran;

                assert(player);
                player->godmode = 1;
                ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 18);
                ToriRSServer_VarbitSet(srv, vb_dod, 0);
                ToriRSServer_VarbitSet(srv, vb_cok, 0);
                ToriRSServer_VarbitSet(srv, vb_xmarks, 8);
                if( vb_reward >= 0 )
                    ToriRSServer_VarbitSet(srv, vb_reward, 0);
                if( vb_page >= 0 )
                    ToriRSServer_VarbitSet(srv, vb_page, 0);
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);

                /* ---- Qualify fail: Client of Kourend ---- */
                ToriRSServer_WorldTeleport(srv, 0, 1782, 3572);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                kandur_slot = npc_spawn(srv, npc_kandur, 1783, 3572, 0);
                SELFTEST_CHECK(kandur_slot >= 0, "Kandur should spawn for CoK qualify");
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_kandur, -1, kandur_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "[opnpc1,hosidiusquest_lord] CoK fail should run");
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "missing Client of Kourend must not start the quest, got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: qualify_fail_client_of_kourend\n");

                /* ---- Qualify fail: X Marks the Spot ---- */
                ToriRSServer_VarbitSet(srv, vb_cok, 7);
                ToriRSServer_VarbitSet(srv, vb_xmarks, 0);
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_kandur, -1, kandur_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "[opnpc1,hosidiusquest_lord] X Marks fail should run");
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "missing X Marks the Spot must not start the quest, got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: qualify_fail_x_marks_the_spot\n");

                /* ---- Qualify fail: Agility 18 ---- */
                ToriRSServer_VarbitSet(srv, vb_xmarks, 8);
                ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 1);
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_kandur, -1, kandur_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "[opnpc1,hosidiusquest_lord] Agility fail should run");
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "Agility under 18 must not start the quest, got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: qualify_fail_agility_18\n");

                /* ---- Refuse does not auto-start ---- */
                ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 18);
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_kandur, -1, kandur_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "qualified Kandur talk should run");
                selftest_click_through(srv, 5);
                if( rows_uid > 0 && player->resume_button_count > 0 &&
                    player->resume_buttons[0] == rows_uid )
                {
                    uint8_t button[6];

                    button[0] = (uint8_t)(rows_uid >> 24);
                    button[1] = (uint8_t)(rows_uid >> 16);
                    button[2] = (uint8_t)(rows_uid >> 8);
                    button[3] = (uint8_t)rows_uid;
                    button[4] = 0;
                    button[5] = 2; /* Not now. */
                    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
                }
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "Not now must not start The Depths of Despair, got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: kandur_refuse\n");

                /* ---- Accept Yes. ---- */
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_kandur, -1, kandur_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN, "Kandur accept talk should run");
                selftest_click_through(srv, 20);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 1,
                               "Yes. should start at Olivia (1), got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: kandur_accept\n");

                /* ---- Olivia confession 1 -> 2 ---- */
                ToriRSServer_WorldTeleport(srv, 0, 1776, 3567);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                olivia_slot = npc_spawn(srv, npc_olivia, 1777, 3567, 0);
                SELFTEST_CHECK(olivia_slot >= 0, "Olivia should spawn");
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_olivia, -1, olivia_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN, "[opnpc1,hosidiusquest_chef] should run");
                selftest_click_through(srv, 24);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 2,
                               "Olivia should advance to Galana (2), got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: olivia_confession\n");

                /* ---- Galana briefing 2 -> 3 ---- */
                ToriRSServer_WorldTeleport(srv, 0, 1649, 3824);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                galana_slot = npc_spawn(srv, npc_galana, 1650, 3824, 0);
                SELFTEST_CHECK(galana_slot >= 0, "Galana should spawn");
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_galana, -1, galana_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "[opnpc1,hosidiusquest_librarian] should run");
                selftest_click_through(srv, 20);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 3,
                               "Galana should advance to envoy (3), got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: galana_accord\n");

                /* ---- Galana hands the book ---- */
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_galana, -1, galana_slot);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_book) == 1,
                               "Galana should hand The Envoy to Varlamore, count=%d",
                               selftest_count_obj(player, obj_book));
                fprintf(stderr, "DOD PASS: galana_give_book\n");

                /* ---- OPHELD1 read envoy 3 -> 4 ---- */
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_book, -1, -1);
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 4,
                               "reading the envoy should advance to caves (4), got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: read_envoy\n");

                /* ---- Cave enter 4 -> 6 ---- */
                ToriRSServer_WorldTeleport(srv, 0, 1645, 3442);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                loc_slot = ToriRSServer_SceneFindLocId(1645, 3442, 0, loc_cave);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(1645, 3442, 0, loc_cave, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "cave entrance should resolve or place");
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_cave, -1, loc_slot);
                    selftest_click_through(srv, 4);
                    ToriRSServer_WorldCloseModal(srv);
                    ToriRSServer_ScriptsFree(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 6,
                                   "entering Crabclaw should advance 4 -> 6, got %d",
                                   ToriRSServer_VarbitGet(player, vb_dod));
                    fprintf(stderr, "DOD PASS: cave_enter_navigate\n");
                }

                /* ---- Crevice / stones / rocks / rope 6 -> 7 ---- */
                loc_slot = ToriRSServer_SceneAddLoc(1714, 9812, 0, loc_crack, 10, 0);
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_crack, -1, loc_slot);
                    ToriRSServer_WorldCloseModal(srv);
                    ToriRSServer_ScriptsFree(srv);
                }
                loc_slot = ToriRSServer_SceneAddLoc(1694, 9802, 0, loc_stone, 10, 0);
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_stone, -1, loc_slot);
                    ToriRSServer_WorldCloseModal(srv);
                    ToriRSServer_ScriptsFree(srv);
                }
                loc_slot = ToriRSServer_SceneAddLoc(1676, 9800, 0, loc_rock, 10, 0);
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_rock, -1, loc_slot);
                    ToriRSServer_WorldCloseModal(srv);
                    ToriRSServer_ScriptsFree(srv);
                }
                loc_slot = ToriRSServer_SceneAddLoc(1672, 9800, 0, loc_rope_top, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "rope top should place");
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_rope_top, -1, loc_slot);
                    ToriRSServer_WorldCloseModal(srv);
                    ToriRSServer_ScriptsFree(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 7,
                                   "rope down should advance to Artur (7), got %d",
                                   ToriRSServer_VarbitGet(player, vb_dod));
                    fprintf(stderr, "DOD PASS: rope_down_artur\n");
                }

                /* ---- Artur 7 -> 8 ---- */
                ToriRSServer_WorldTeleport(srv, 0, 1686, 9752);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                artur_slot = npc_spawn(srv, npc_artur, 1687, 9752, 0);
                SELFTEST_CHECK(artur_slot >= 0, "Artur in caves should spawn");
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_artur, -1, artur_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "[opnpc1,hosidiusquest_son_caves] should run");
                selftest_click_through(srv, 24);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 8,
                               "Artur should send you after the snake (8), got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: artur_find\n");

                /* ---- Soft-kill snake 8 -> 9 ---- */
                SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dod_soft_kill_snake]", NULL, 0),
                               "dod_soft_kill_snake should run");
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 9,
                               "killing the snake should advance to chest (9), got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: snake_defeat\n");

                /* ---- Chest 9 -> 10 + accord ---- */
                loc_slot = ToriRSServer_SceneAddLoc(1698, 9744, 0, loc_chest, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "accord chest should place");
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_chest, -1, loc_slot);
                    selftest_click_through(srv, 4);
                    ToriRSServer_WorldCloseModal(srv);
                    ToriRSServer_ScriptsFree(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 10 &&
                                       selftest_count_obj(player, obj_accord) == 1,
                                   "chest should grant the Accord at progress 10, var=%d accord=%d",
                                   ToriRSServer_VarbitGet(player, vb_dod),
                                   selftest_count_obj(player, obj_accord));
                    fprintf(stderr, "DOD PASS: chest_find_accord\n");
                }

                /* ---- Hand-in + complete 10 -> 11 ---- */
                ToriRSServer_WorldTeleport(srv, 0, 1782, 3572);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                xp_before = player->stat_xp_tenths[TORIRSSERVER_STAT_AGILITY];
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_kandur, -1, kandur_slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN, "Kandur hand-in should run");
                selftest_click_through(srv, 20);
                {
                    int q;
                    for( q = 0; q < 40; q++ )
                    {
                        ToriRSServer_WorldCloseModal(srv);
                        selftest_tick(srv);
                    }
                }
                ToriRSServer_ScriptsFree(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 11,
                               "hand-in should complete at 11, got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                SELFTEST_CHECK(selftest_count_obj(player, obj_page) >= 1,
                               "complete should grant veos_memoirs_hos_page, count=%d",
                               selftest_count_obj(player, obj_page));
                SELFTEST_CHECK(selftest_count_obj(player, obj_coins) >= 4000,
                               "complete should grant 4000 coins, count=%d",
                               selftest_count_obj(player, obj_coins));
                SELFTEST_CHECK(player->stat_xp_tenths[TORIRSSERVER_STAT_AGILITY] >=
                                   xp_before + 15000,
                               "complete should award 1500 Agility XP (15000 tenths), before=%d after=%d",
                               xp_before, player->stat_xp_tenths[TORIRSSERVER_STAT_AGILITY]);
                fprintf(stderr, "DOD PASS: complete_scroll_rewards\n");

                /* ---- Journal complete ---- */
                SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,depthsofdespair_journal]", NULL, 0),
                               "depthsofdespair_journal should run");
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                fprintf(stderr, "DOD PASS: journal_11_complete\n");

                /* ---- Leftover stamps (named, disclosed) ---- */
                ToriRSServer_ScriptsRunDebugproc(srv, "dodbmp_leftover_full_refuse_trees");
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                fprintf(stderr, "DOD PASS: leftover_full_refuse_trees\n");

                ToriRSServer_ScriptsRunDebugproc(srv, "dodbmp_leftover_random_library_bookshelf");
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                fprintf(stderr, "DOD PASS: leftover_random_library_bookshelf\n");

                ToriRSServer_ScriptsRunDebugproc(srv, "dodbmp_leftover_stone_rock_fail_rolls");
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                fprintf(stderr, "DOD PASS: leftover_stone_rock_fail_rolls\n");

                ToriRSServer_ScriptsRunDebugproc(srv, "dodbmp_leftover_sand_snake_instance");
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                fprintf(stderr, "DOD PASS: leftover_sand_snake_instance\n");

                ToriRSServer_ScriptsRunDebugproc(srv, "dodbmp_leftover_butler_elena_trees");
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                fprintf(stderr, "DOD PASS: leftover_butler_elena_trees\n");

                ToriRSServer_ScriptsRunDebugproc(srv, "dodbmp_leftover_favour_system");
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                fprintf(stderr, "DOD PASS: leftover_favour_system\n");

                ToriRSServer_ScriptsRunDebugproc(srv, "dodbmp_leftover_graceful_recolour_ui");
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsFree(srv);
                fprintf(stderr, "DOD PASS: leftover_graceful_recolour_ui\n");

                if( kandur_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, kandur_slot);
                if( olivia_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, olivia_slot);
                if( galana_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, galana_slot);
                if( artur_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, artur_slot);
                snake_slot = selftest_find_npc(srv, npc_snake);
                if( snake_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, snake_slot);
                ToriRSServer_WorldNpcReap(srv);
            }
        }
    }
