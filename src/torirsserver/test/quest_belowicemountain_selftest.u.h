    /*
     * Below Ice Mountain critical path. Real opnpc / oploc / opheldu only.
     * Included immediately before the shop fprintf so spawned npcs cannot
     * leak into later roam/RNG checks. Focused gate:
     *   TORIRSSERVER_SELFTEST_BIM_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0
     *
     * Do not merge this .u.h onto parent v3.
     */
    fprintf(stderr, "ToriRSServer selftest: ::bimrun / Below Ice Mountain\n");
    {
        int loaded = srv->scripts_ok;

        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            int varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
            int vb_bim = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bim");
            int vb_checkal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bim_checkal");
            int vb_marley = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bim_marley");
            int vb_burntof = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bim_burntof");
            int npc_willow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bim_willow");
            int npc_checkal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bim_checkal");
            int npc_atlas = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bim_atlas");
            int npc_marley = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bim_marley");
            int npc_cook =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fai_varrock_bluemoon_chef");
            int npc_burntof = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bim_burntof");
            int npc_barmaid =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "risingsun_barmaid");
            int npc_golem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bim_golem_boss");
            int loc_entrance = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bim_entrance");
            int loc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bim_boss_rock");
            int obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
            int obj_bread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread");
            int obj_meat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cooked_meat");
            int obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
            int obj_sandwich =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bim_steak_sandwich");
            int obj_ale = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "asgarnian_ale");
            int stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
            int rows_uid =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
            int c_started = ToriRSServer_ContentConstantInt("bim_started", 10);
            int c_crew = ToriRSServer_ContentConstantInt("bim_crew", 15);
            int c_dungeon = ToriRSServer_ContentConstantInt("bim_dungeon", 20);
            int c_guardian = ToriRSServer_ContentConstantInt("bim_guardian", 35);
            int c_cutscene = ToriRSServer_ContentConstantInt("bim_cutscene", 40);
            int c_complete = ToriRSServer_ContentConstantInt("bim_complete", 45);
            int c_side_need = ToriRSServer_ContentConstantInt("bim_side_need", 5);
            int c_side_mid = ToriRSServer_ContentConstantInt("bim_side_mid", 10);
            int c_side_flex = ToriRSServer_ContentConstantInt("bim_side_flex", 15);
            int c_side_done = ToriRSServer_ContentConstantInt("bim_side_done", 40);
            int c_qp_req = ToriRSServer_ContentConstantInt("bim_qp_req", 16);
            int c_coin_reward = ToriRSServer_ContentConstantInt("bim_coin_reward", 2000);

            SELFTEST_CHECK(varp_qp >= 0 && vb_bim >= 0 && vb_checkal >= 0 && vb_marley >= 0 &&
                               vb_burntof >= 0 && npc_willow >= 0 && npc_checkal >= 0 &&
                               npc_atlas >= 0 && npc_marley >= 0 && npc_cook >= 0 &&
                               npc_burntof >= 0 && npc_barmaid >= 0 && npc_golem >= 0 &&
                               loc_entrance >= 0 && loc_rock >= 0 && obj_coins >= 0 &&
                               obj_bread >= 0 && obj_meat >= 0 && obj_knife >= 0 &&
                               obj_sandwich >= 0 && obj_ale >= 0 && stat_mine >= 0,
                           "the ::bimrun C-side names should all resolve");
            if( vb_bim < 0 || npc_willow < 0 || varp_qp < 0 )
            {
                fprintf(stderr, "  SKIP  Below Ice Mountain pack names missing\n");
            }
            else
            {
                int s;
                int slot;
                int loc_slot;
                int qp_before;
                int coins_before;

                player->godmode = 1;
                player->dying = 0;
                srv->members_world = 1;
                if( player->max_hitpoints > 0 )
                    player->hitpoints = player->max_hitpoints;
                ToriRSServer_CombatSyncHitpoints(player);
                if( stat_mine >= 0 )
                    ToriRSServer_CombatSetLevel(player, stat_mine, 10);

                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                ToriRSServer_VarbitSet(srv, vb_bim, 0);
                ToriRSServer_VarbitSet(srv, vb_checkal, 0);
                ToriRSServer_VarbitSet(srv, vb_marley, 0);
                ToriRSServer_VarbitSet(srv, vb_burntof, 0);
                if( varp_qp >= 0 )
                    player->varps[varp_qp] = 0;

                /* ---- QP16 fail: OPNPC1 Willow must not write %bim ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3003, 3435);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                slot = npc_spawn(srv, npc_willow, 3004, 3435, 0);
                SELFTEST_CHECK(slot >= 0, "bim_willow should spawn south of Ice Mountain");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_willow, -1, slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == 0,
                                   "QP16 fail must not start the quest, bim=%d",
                                   ToriRSServer_VarbitGet(player, vb_bim));
                    fprintf(stderr, "BIM PASS: qp16_qualify_blocks_start\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Refuse: Yes/No, pick No, %bim stays 0 ---- */
                if( varp_qp >= 0 )
                    player->varps[varp_qp] = c_qp_req;
                ToriRSServer_VarbitSet(srv, vb_bim, 0);
                slot = npc_spawn(srv, npc_willow, 3004, 3435, 0);
                SELFTEST_CHECK(slot >= 0, "bim_willow should spawn for the refuse offer");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_willow, -1, slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 2;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == 0,
                                   "refusing Willow must not auto-start, bim=%d",
                                   ToriRSServer_VarbitGet(player, vb_bim));
                    fprintf(stderr, "BIM PASS: willow_refuse_keeps_not_started\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Accept: pick Yes, %bim = started ---- */
                ToriRSServer_VarbitSet(srv, vb_bim, 0);
                slot = npc_spawn(srv, npc_willow, 3004, 3435, 0);
                SELFTEST_CHECK(slot >= 0, "bim_willow should spawn for accept");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_willow, -1, slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == c_started,
                                   "accepting Willow should write %%bim=started (%d), got %d",
                                   c_started, ToriRSServer_VarbitGet(player, vb_bim));
                    fprintf(stderr, "BIM PASS: willow_accept_starts\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Checkal: OPNPC1 sends the player to Atlas ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3087, 3415);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_checkal, 3088, 3415, 0);
                SELFTEST_CHECK(slot >= 0, "bim_checkal should spawn in Barbarian Village");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_checkal, -1, slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_checkal) == c_side_need,
                                   "Checkal should send the player to Atlas, checkal=%d",
                                   ToriRSServer_VarbitGet(player, vb_checkal));
                    fprintf(stderr, "BIM PASS: checkal_sends_to_atlas\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Atlas: OPNPC1 Yes unlocks Flex ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3076, 3440);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_atlas, 3077, 3440, 0);
                SELFTEST_CHECK(slot >= 0, "bim_atlas should spawn in the Long Hall");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_atlas, -1, slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_checkal) == c_side_flex,
                                   "Atlas Yes should unlock Flex, checkal=%d",
                                   ToriRSServer_VarbitGet(player, vb_checkal));
                    fprintf(stderr, "BIM PASS: atlas_unlocks_flex\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Checkal flex: OPNPC1 Flex recruits him ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3087, 3415);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_checkal, 3088, 3415, 0);
                SELFTEST_CHECK(slot >= 0, "bim_checkal should spawn for the flex");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_checkal, -1, slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_checkal) == c_side_done,
                                   "flexing for Checkal should recruit him, checkal=%d",
                                   ToriRSServer_VarbitGet(player, vb_checkal));
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == c_crew,
                                   "recruiting Checkal should write %%bim=crew (%d), got %d",
                                   c_crew, ToriRSServer_VarbitGet(player, vb_bim));
                    fprintf(stderr, "BIM PASS: checkal_flex_recruits\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Marley: OPNPC1 wants a steak sandwich ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3088, 3470);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_marley, 3089, 3470, 0);
                SELFTEST_CHECK(slot >= 0, "bim_marley should spawn in Edgeville");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_marley, -1, slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_marley) == c_side_need,
                                   "Marley should ask for a steak sandwich, marley=%d",
                                   ToriRSServer_VarbitGet(player, vb_marley));
                    fprintf(stderr, "BIM PASS: marley_wants_sandwich\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Cook: OPNPC1 gives the recipe ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3230, 3401);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_cook, 3231, 3401, 0);
                SELFTEST_CHECK(slot >= 0, "Blue Moon cook should spawn");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cook, -1, slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_marley) == c_side_mid,
                                   "the cook should give the recipe, marley=%d",
                                   ToriRSServer_VarbitGet(player, vb_marley));
                    fprintf(stderr, "BIM PASS: cook_gives_recipe\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- OPHELDU knife on cooked meat with bread: steak sandwich ---- */
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                inv_set(player, 0, obj_meat, 1);
                inv_set(player, 1, obj_knife, 1);
                inv_set(player, 2, obj_bread, 1);
                player->last_item = obj_meat;
                player->last_slot = 0;
                player->last_useitem = obj_knife;
                player->last_useslot = 1;
                ToriRSServer_ScriptsRunOpheldu(srv, obj_meat, -1, obj_knife, -1);
                biohazard_run_dialogue(srv, player, 0);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_sandwich) == 1,
                               "knife on cooked meat with bread should make a steak sandwich, count=%d",
                               selftest_count_obj(player, obj_sandwich));
                fprintf(stderr, "BIM PASS: opheldu_steak_sandwich\n");

                /* ---- Marley hand-in ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3088, 3470);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_marley, 3089, 3470, 0);
                SELFTEST_CHECK(slot >= 0, "bim_marley should spawn for the hand-in");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_marley, -1, slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_marley) == c_side_done,
                                   "feeding Marley should recruit him, marley=%d",
                                   ToriRSServer_VarbitGet(player, vb_marley));
                    fprintf(stderr, "BIM PASS: marley_eats_sandwich\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Barmaid: OPNPC1 buys Asgarnian Ale during the errand ---- */
                ToriRSServer_VarbitSet(srv, vb_burntof, c_side_need);
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                inv_set(player, 0, obj_coins, 10);
                ToriRSServer_WorldTeleport(srv, 0, 2954, 3368);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_barmaid, 2955, 3368, 0);
                SELFTEST_CHECK(slot >= 0, "risingsun_barmaid should spawn");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_barmaid, -1, slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_ale) == 1,
                                   "Kaylee should sell an Asgarnian Ale, count=%d",
                                   selftest_count_obj(player, obj_ale));
                    fprintf(stderr, "BIM PASS: barmaid_sells_ale\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Burntof: drink then RPS ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2956, 3367);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_burntof, 2957, 3367, 0);
                SELFTEST_CHECK(slot >= 0, "bim_burntof should spawn in the Rising Sun");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_burntof, -1, slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_burntof) == c_side_mid,
                                   "giving Burntof a drink should start RPS, burntof=%d",
                                   ToriRSServer_VarbitGet(player, vb_burntof));
                    fprintf(stderr, "BIM PASS: burntof_takes_drink\n");

                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_burntof, -1, slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_burntof) == c_side_done,
                                   "RPS should recruit Burntof, burntof=%d",
                                   ToriRSServer_VarbitGet(player, vb_burntof));
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == c_dungeon,
                                   "the full crew should write %%bim=dungeon (%d), got %d",
                                   c_dungeon, ToriRSServer_VarbitGet(player, vb_bim));
                    fprintf(stderr, "BIM PASS: burntof_rps_recruits\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Entrance: OPLOC1 wakes the guardian ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3000, 3494);
                selftest_tick(srv);
                if( player->rebuild_scene_pending )
                    selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                loc_slot = ToriRSServer_SceneFindLocId(3000, 3494, 0, loc_entrance);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(3000, 3494, 0, loc_entrance, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "bim_entrance should resolve or place");
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_entrance, -1,
                                                        loc_slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == c_guardian,
                                   "entering the dig should wake the guardian, bim=%d",
                                   ToriRSServer_VarbitGet(player, vb_bim));
                    fprintf(stderr, "BIM PASS: entrance_wakes_guardian\n");
                }

                /* ---- Golem: OPNPC1 soft-clears the fight ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2996, 3494);
                selftest_tick(srv);
                slot = npc_spawn(srv, npc_golem, 2997, 3494, 0);
                SELFTEST_CHECK(slot >= 0, "bim_golem_boss should spawn");
                if( slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_golem, -1, slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == c_cutscene,
                                   "defeating the golem should write %%bim=cutscene (%d), got %d",
                                   c_cutscene, ToriRSServer_VarbitGet(player, vb_bim));
                    fprintf(stderr, "BIM PASS: golem_defeat\n");
                    ToriRSServer_WorldNpcFree(srv, slot);
                    ToriRSServer_WorldNpcReap(srv);
                }

                /* ---- Finish cutscene on the entrance: complete + 1 QP + 2000 coins ---- */
                qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
                coins_before = selftest_count_obj(player, obj_coins);
                loc_slot = ToriRSServer_SceneFindLocId(3000, 3494, 0, loc_entrance);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(3000, 3494, 0, loc_entrance, 10, 0);
                if( loc_slot >= 0 )
                {
                    ToriRSServer_WorldTeleport(srv, 0, 3000, 3494);
                    selftest_tick(srv);
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_entrance, -1,
                                                        loc_slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == c_complete,
                                   "the finish cutscene should complete the quest, bim=%d",
                                   ToriRSServer_VarbitGet(player, vb_bim));
                    SELFTEST_CHECK(varp_qp < 0 || player->varps[varp_qp] == qp_before + 1,
                                   "completion should award 1 QP, %d -> %d", qp_before,
                                   varp_qp >= 0 ? player->varps[varp_qp] : -1);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_coins) >= coins_before + c_coin_reward,
                                   "completion should award 2000 coins, before=%d after=%d",
                                   coins_before, selftest_count_obj(player, obj_coins));
                    fprintf(stderr, "BIM PASS: complete_qp_and_coins\n");
                }

                /* ---- Pillars: Mining 10 skip still works from a fresh guardian state ---- */
                ToriRSServer_VarbitSet(srv, vb_bim, c_guardian);
                loc_slot = ToriRSServer_SceneFindLocId(2996, 3494, 0, loc_rock);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(2996, 3494, 0, loc_rock, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "bim_boss_rock should resolve or place");
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rock, -1,
                                                        loc_slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bim) == c_cutscene,
                                   "mining the pillars at 10 should skip the golem, bim=%d",
                                   ToriRSServer_VarbitGet(player, vb_bim));
                    fprintf(stderr, "BIM PASS: pillars_mine_skip\n");
                }

                ToriRSServer_ScriptsFree(srv);
            }
        }
    }
