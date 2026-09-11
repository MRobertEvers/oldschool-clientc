    /*
     * The Depths of Despair critical path. Qualify fails on the real OPNPC1
     * / authored procs; the 0..4,6..11 ladder is the authored ::dodrun
     * walk. Included immediately before a selftest_reset_world so spawned
     * npcs cannot leak into later roam/RNG checks.
     *
     * Gate: TORIRSSERVER_SELFTEST_DOD_ONLY=1
     * Godmode on for the whole walk (not a death case).
     *
     * Mutation: change ~dod_show_qualify_fail_cok or skip the
     * %hosidiusquest = ^dod_complete write in ~dod_quest_complete. The
     * matching assertion dies. Restoring it brings the check back.
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
        int obj_book;
        int obj_accord;
        int obj_page;
        int obj_coins;
        int loc_cave;
        int loc_chest;

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
            int s;
            int slot;
            int ran;
            int xp_before;
            int loc_slot;
            static struct ToriRSServerCapture cap;

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
            obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosidiusquest_book");
            obj_accord = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosidiusquest_accord");
            obj_page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "veos_memoirs_hos_page");
            obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
            loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_cave_entrance");
            loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosidiusquest_chest");

            SELFTEST_CHECK(vb_dod >= 0, "hosidiusquest varbit should resolve");
            SELFTEST_CHECK(vb_cok >= 0, "veos_progress varbit should resolve");
            SELFTEST_CHECK(vb_xmarks >= 0, "cluequest varbit should resolve");
            SELFTEST_CHECK(npc_kandur >= 0, "hosidiusquest_lord should resolve");
            SELFTEST_CHECK(obj_book >= 0, "hosidiusquest_book should resolve");
            SELFTEST_CHECK(obj_accord >= 0, "hosidiusquest_accord should resolve");
            SELFTEST_CHECK(obj_page >= 0, "veos_memoirs_hos_page should resolve");
            SELFTEST_CHECK(obj_coins >= 0, "coins should resolve");
            SELFTEST_CHECK(loc_cave >= 0, "hosidiusquest_cave_entrance should resolve");
            SELFTEST_CHECK(loc_chest >= 0, "hosidiusquest_chest should resolve");

            if( vb_dod < 0 || npc_kandur < 0 )
            {
                fprintf(stderr, "  SKIP  Depths of Despair pack names missing\n");
            }
            else
            {
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

                ToriRSServer_WorldTeleport(srv, 0, 1782, 3572);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);

                /* Procs first: CloseModal/ScriptsFree after a parked OPNPC1
                 * poisons later RunProc on this shared player. Drain pages
                 * only; free once at the end. */

                /* ---- Qualify fail procs (named, unique) ---- */
                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_show_qualify_fail_cok]", NULL, 0),
                    "dod_show_qualify_fail_cok should run");
                selftest_click_through(srv, 4);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "CoK fail proc must not start the quest");
                fprintf(stderr, "DOD PASS: qualify_fail_client_of_kourend\n");

                ToriRSServer_VarbitSet(srv, vb_cok, 7);
                ToriRSServer_VarbitSet(srv, vb_xmarks, 0);
                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_show_qualify_fail_xmarks]", NULL, 0),
                    "dod_show_qualify_fail_xmarks should run");
                selftest_click_through(srv, 4);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "X Marks fail proc must not start the quest");
                fprintf(stderr, "DOD PASS: qualify_fail_x_marks_the_spot\n");

                ToriRSServer_VarbitSet(srv, vb_xmarks, 8);
                ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 1);
                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_show_qualify_fail_agility]", NULL, 0),
                    "dod_show_qualify_fail_agility should run");
                selftest_click_through(srv, 4);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "Agility fail proc must not start the quest");
                fprintf(stderr, "DOD PASS: qualify_fail_agility_18\n");

                /* ---- Refuse does not write progress ---- */
                ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 18);
                SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dod_refuse]", NULL, 0),
                               "dod_refuse should run");
                selftest_click_through(srv, 6);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "Not now must not start the quest, got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: kandur_refuse\n");

                /* ---- Cave refuse before progress 4 ---- */
                loc_slot = ToriRSServer_SceneFindLocId(1782, 3572, 0, loc_cave);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(1782, 3572, 0, loc_cave, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "cave entrance should place beside Kandur");
                if( loc_slot >= 0 )
                {
                    ToriRSServer_VarbitSet(srv, vb_dod, 0);
                    ran = ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_cave, -1, loc_slot);
                    SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                                   "[oploc1,hosidiusquest_cave_entrance] refuse should run");
                    selftest_click_through(srv, 4);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                                   "cave before envoy-read must stay at 0, got %d",
                                   ToriRSServer_VarbitGet(player, vb_dod));
                    fprintf(stderr, "DOD PASS: cave_refuse\n");

                    ToriRSServer_VarbitSet(srv, vb_dod, 4);
                    ran = ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_cave, -1, loc_slot);
                    SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                                   "[oploc1,hosidiusquest_cave_entrance] enter should run");
                    selftest_click_through(srv, 4);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 6,
                                   "entering Crabclaw should advance 4 -> 6, got %d",
                                   ToriRSServer_VarbitGet(player, vb_dod));
                    fprintf(stderr, "DOD PASS: cave_enter_navigate\n");
                }

                /* ---- Envoy read 3 -> 4 ---- */
                ToriRSServer_VarbitSet(srv, vb_dod, 3);
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                inv_set(player, 0, obj_book, 1);
                SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dod_read_envoy]", NULL, 0),
                               "dod_read_envoy should run");
                selftest_click_through(srv, 4);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 4,
                               "reading the envoy should advance to caves (4), got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: read_envoy\n");

                /* ---- Soft-kill snake 8 -> 9 ---- */
                ToriRSServer_VarbitSet(srv, vb_dod, 8);
                SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dod_soft_kill_snake]", NULL, 0),
                               "dod_soft_kill_snake should run");
                selftest_click_through(srv, 4);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 9,
                               "killing the snake should advance to chest (9), got %d",
                               ToriRSServer_VarbitGet(player, vb_dod));
                fprintf(stderr, "DOD PASS: snake_defeat\n");

                /* ---- Chest 9 -> 10 + accord ---- */
                loc_slot = ToriRSServer_SceneFindLocId(1784, 3572, 0, loc_chest);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(1784, 3572, 0, loc_chest, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "accord chest should place");
                if( loc_slot >= 0 )
                {
                    ToriRSServer_VarbitSet(srv, vb_dod, 9);
                    ran = ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_chest, -1, loc_slot);
                    SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                                   "[oploc1,hosidiusquest_chest] should run");
                    selftest_click_through(srv, 4);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 10 &&
                                       selftest_count_obj(player, obj_accord) == 1,
                                   "chest should grant the Accord at progress 10, var=%d accord=%d",
                                   ToriRSServer_VarbitGet(player, vb_dod),
                                   selftest_count_obj(player, obj_accord));
                    fprintf(stderr, "DOD PASS: chest_find_accord\n");
                }

                /* ---- Complete rewards 10 -> 11 ---- */
                ToriRSServer_VarbitSet(srv, vb_dod, 10);
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    if( player->inv[s].obj_id != obj_accord )
                        inv_set(player, s, -1, 0);
                if( selftest_count_obj(player, obj_accord) < 1 )
                    inv_set(player, 0, obj_accord, 1);
                xp_before = player->stat_xp_tenths[TORIRSSERVER_STAT_AGILITY];
                SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dod_quest_complete]", NULL, 0),
                               "dod_quest_complete should run");
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 11,
                               "complete should write endstate 11, got %d",
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
                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,depthsofdespair_journal]", NULL, 0),
                    "depthsofdespair_journal should run");
                selftest_click_through(srv, 4);
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: journal_11_complete\n");

                /* ---- ::dodrun integration ---- */
                ToriRSServer_VarbitSet(srv, vb_dod, 0);
                ToriRSServer_VarbitSet(srv, vb_cok, 7);
                ToriRSServer_VarbitSet(srv, vb_xmarks, 8);
                if( vb_reward >= 0 )
                    ToriRSServer_VarbitSet(srv, vb_reward, 0);
                if( vb_page >= 0 )
                    ToriRSServer_VarbitSet(srv, vb_page, 0);
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 18);
                ToriRSServer_CaptureBegin(srv, &cap);
                ToriRSServer_ScriptsRunDebugproc(srv, "dodrun");
                ToriRSServer_CaptureEnd(srv);
                {
                    int i;
                    int dodrun_ok = 0;

                    for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
                         i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
                    {
                        const char* text = selftest_message_text(srv, &cap.packets[i]);

                        if( text && strstr(text, "dodrun OK") )
                            dodrun_ok = 1;
                    }
                    SELFTEST_CHECK(dodrun_ok || ToriRSServer_VarbitGet(player, vb_dod) >= 11,
                                   "::dodrun should complete the authored ladder");
                }
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: dodrun\n");

                /* ---- Leftover stamps ---- */
                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_leftover_full_refuse_trees]", NULL, 0),
                    "leftover_full_refuse_trees should run");
                selftest_click_through(srv, 4);
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: leftover_full_refuse_trees\n");

                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_leftover_random_library_bookshelf]", NULL, 0),
                    "leftover_random_library_bookshelf should run");
                selftest_click_through(srv, 4);
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: leftover_random_library_bookshelf\n");

                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_leftover_stone_rock_fail_rolls]", NULL, 0),
                    "leftover_stone_rock_fail_rolls should run");
                selftest_click_through(srv, 4);
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: leftover_stone_rock_fail_rolls\n");

                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_leftover_sand_snake_instance]", NULL, 0),
                    "leftover_sand_snake_instance should run");
                selftest_click_through(srv, 4);
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: leftover_sand_snake_instance\n");

                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_leftover_butler_elena_trees]", NULL, 0),
                    "leftover_butler_elena_trees should run");
                selftest_click_through(srv, 4);
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: leftover_butler_elena_trees\n");

                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_leftover_favour_system]", NULL, 0),
                    "leftover_favour_system should run");
                selftest_click_through(srv, 4);
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: leftover_favour_system\n");

                SELFTEST_CHECK(
                    ToriRSServer_ScriptsRunProc(srv, "[proc,dod_leftover_graceful_recolour_ui]", NULL, 0),
                    "leftover_graceful_recolour_ui should run");
                selftest_click_through(srv, 4);
                fprintf(stderr, "DOD PASS: leftover_graceful_recolour_ui\n");

                /* Real OPNPC1 after the proc walk. */
                slot = npc_spawn(srv, npc_kandur, 1783, 3572, 0);
                SELFTEST_CHECK(slot >= 0, "Kandur should spawn for OPNPC1");
                ToriRSServer_VarbitSet(srv, vb_dod, 0);
                ToriRSServer_VarbitSet(srv, vb_cok, 0);
                ToriRSServer_VarbitSet(srv, vb_xmarks, 8);
                ran = ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, srv->npcs[slot].type, -1, slot);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "[opnpc1,hosidiusquest_lord] should run");
                selftest_click_through(srv, 8);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dod) == 0,
                               "OPNPC1 CoK fail must not start the quest");
                fprintf(stderr, "DOD PASS: opnpc1_kandur\n");

                if( slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
                ToriRSServer_ScriptsFree(srv);
            }
        }
    }
