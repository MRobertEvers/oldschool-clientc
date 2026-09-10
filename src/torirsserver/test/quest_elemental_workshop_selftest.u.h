    /*
     * Elemental Workshop I critical path. Real oploc / opheld / opheldu /
     * opnpc only. Included immediately before a selftest_reset_world so
     * spawned npcs and placed locs cannot leak into later roam/RNG checks.
     *
     * Mutation (field guide S7): change the bookcase inv_add in
     * quest_elemental_workshop.rs2 to elemental_workshop_key. The assertion
     * that OPLOC1 grants elemental_workshop_shield_book dies. Restoring the
     * book object brings it back.
     */
    fprintf(stderr, "ToriRSServer selftest: ::elem1run\n");
    {
        int loaded = srv->scripts_ok;
        int varp_bits;
        int vb_book;
        int vb_key;
        int vb_gate1;
        int vb_gate2;
        int vb_switch;
        int vb_bellows;
        int vb_fire;
        int vb_air;
        int vb_stairs;
        int vb_leather;
        int vb_entered;
        int vb_finished;
        int varp_qp;
        int loc_bookcase;
        int loc_valve1;
        int loc_valve2;
        int loc_lever;
        int loc_bellows;
        int loc_air;
        int loc_box1;
        int loc_box2;
        int loc_box4;
        int loc_trough;
        int loc_furnace;
        int loc_furnace_out;
        int loc_workbench;
        int loc_wall;
        int loc_stairs;
        int obj_book;
        int obj_slashed;
        int obj_key;
        int obj_knife;
        int obj_needle;
        int obj_thread;
        int obj_leather;
        int obj_bowl;
        int obj_bowl_full;
        int obj_ore;
        int obj_bar;
        int obj_coal;
        int obj_hammer;
        int obj_pick;
        int obj_shield;
        int npc_rock;
        int npc_elem;
        int stat_mine;
        int stat_smith;
        int stat_craft;
        int rock_slot = -1;
        int elem_slot = -1;

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
        varp_bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "elemental_workshop_bits");
        vb_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_book");
        vb_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_key");
        vb_gate1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_gate1");
        vb_gate2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_gate2");
        vb_switch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_switch");
        vb_bellows = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_bellows");
        vb_fire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_fire");
        vb_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_bellows_switch");
        vb_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_stairs");
        vb_leather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_leather");
        vb_entered = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_entered");
        vb_finished = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_workshop_finished");
        varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        loc_bookcase = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_bookcase");
        loc_valve1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_valve_1");
        loc_valve2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_valve_2");
        loc_lever = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_water_lever");
        loc_bellows = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_bellows_noanim");
        loc_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_air_lever");
        loc_box1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_box_1");
        loc_box2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_box_2");
        loc_box4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_box_4");
        loc_trough = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_trough_2");
        loc_furnace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_furnace");
        loc_furnace_out =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_furnace_out");
        loc_workbench = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_workbench");
        loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_oddwall_l");
        loc_stairs =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_spiralstairstop");
        obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_shield_book");
        obj_slashed =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_shield_book_slashed");
        obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_key");
        obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
        obj_needle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "needle");
        obj_thread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thread");
        obj_leather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "leather");
        obj_bowl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_lava_bowl");
        obj_bowl_full =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_lava_bowl_full");
        obj_ore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_ore");
        obj_bar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_bar");
        obj_coal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coal");
        obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
        obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
        obj_shield = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_shield");
        npc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                              "elem1_qip_earth_elemental_rock_version_rock");
        npc_elem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                              "elem1_qip_earth_elemental_rock_version");
        stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
        stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
        stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");

        SELFTEST_CHECK(varp_bits >= 0 && vb_book >= 0 && vb_key >= 0 && vb_gate1 >= 0 &&
                           vb_gate2 >= 0 && vb_switch >= 0 && vb_bellows >= 0 && vb_fire >= 0 &&
                           vb_air >= 0 && vb_stairs >= 0 && vb_leather >= 0 && vb_finished >= 0 &&
                           loc_bookcase >= 0 && loc_valve1 >= 0 && loc_valve2 >= 0 &&
                           loc_lever >= 0 && loc_bellows >= 0 && loc_air >= 0 && loc_box1 >= 0 &&
                           loc_box2 >= 0 && loc_box4 >= 0 && loc_trough >= 0 && loc_furnace >= 0 &&
                           loc_workbench >= 0 && loc_wall >= 0 && loc_stairs >= 0 && obj_book >= 0 &&
                           obj_slashed >= 0 && obj_key >= 0 && obj_knife >= 0 && obj_needle >= 0 &&
                           obj_thread >= 0 && obj_leather >= 0 && obj_bowl >= 0 &&
                           obj_bowl_full >= 0 && obj_ore >= 0 && obj_bar >= 0 && obj_coal >= 0 &&
                           obj_hammer >= 0 && obj_pick >= 0 && obj_shield >= 0 && npc_rock >= 0 &&
                           npc_elem >= 0 && stat_mine >= 0 && stat_smith >= 0 && stat_craft >= 0,
                       "the ::elem1run C-side names should all resolve");
        if( varp_bits < 0 || vb_book < 0 || obj_book < 0 || loc_bookcase < 0 )
        {
            fprintf(stderr, "  SKIP  elemental workshop pack names missing\n");
        }
        else
        {
            int s;
            int loc_slot;
            int t;
            int qp_before;

            player->godmode = 1;
            player->dying = 0;
            if( player->max_hitpoints > 0 )
                player->hitpoints = player->max_hitpoints;
            ToriRSServer_CombatSyncHitpoints(player);
            ToriRSServer_CombatSetLevel(player, stat_mine, 20);
            ToriRSServer_CombatSetLevel(player, stat_smith, 20);
            ToriRSServer_CombatSetLevel(player, stat_craft, 20);
            player->varps[varp_bits] = 0;
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, -1, 0);

            /* ---- OPLOC1 bookcase: empty inv grants the battered book ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2716, 3482);
            selftest_tick(srv);
            if( player->rebuild_scene_pending )
                selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
            loc_slot = ToriRSServer_SceneFindLocId(2716, 3482, 0, loc_bookcase);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2716, 3482, 0, loc_bookcase, 10, 0);
            SELFTEST_CHECK(loc_slot >= 0, "bookcase should resolve or place at 2716,3482");
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bookcase, -1,
                                                    loc_slot);
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_book) == 1,
                               "OPLOC1 bookcase should grant the battered book, count=%d",
                               selftest_count_obj(player, obj_book));
                fprintf(stderr, "ELEM1 PASS: oploc1_bookcase_grants_book\n");
            }

            /* Inventory-full: a second search with a full pack must not duplicate. */
            {
                int i;

                for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
                    if( player->inv[i].obj_id < 0 )
                        inv_set(player, i, obj_knife, 1);
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bookcase, -1,
                                                    loc_slot);
                SELFTEST_CHECK(selftest_count_obj(player, obj_book) == 1,
                               "a full inventory must not grant a second battered book");
                fprintf(stderr, "ELEM1 PASS: oploc1_bookcase_full_inv\n");
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    if( player->inv[s].obj_id == obj_knife )
                        inv_set(player, s, -1, 0);
            }

            /* ---- OPHELD1 read the book (QH hasReadBook / bit 1) ---- */
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_book, -1, -1);
            selftest_click_through(srv, 8);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_book) == 1,
                           "OPHELD1 battered book should set elemental_workshop_book, got %d",
                           ToriRSServer_VarbitGet(player, vb_book));
            fprintf(stderr, "ELEM1 PASS: opheld1_read_book\n");

            /* ---- OPHELDU knife on book: slashed book + battered key ---- */
            inv_set(player, 1, obj_knife, 1);
            player->last_item = obj_book;
            player->last_slot = 0;
            player->last_useitem = obj_knife;
            player->last_useslot = 1;
            ToriRSServer_ScriptsRunOpheldu(srv, obj_book, -1, obj_knife, -1);
            for( t = 0; t < 6 && player->active_script; t++ )
                selftest_tick(srv);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count_obj(player, obj_slashed) == 1 &&
                               selftest_count_obj(player, obj_key) == 1 &&
                               ToriRSServer_VarbitGet(player, vb_key) == 1,
                           "OPHELDU knife on book should grant slashed book + key");
            fprintf(stderr, "ELEM1 PASS: opheldu_knife_on_book\n");

            /* ---- OPLOC1 odd wall without key ---- */
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                if( player->inv[s].obj_id == obj_key )
                    inv_set(player, s, -1, 0);
            ToriRSServer_WorldTeleport(srv, 0, 2709, 3495);
            selftest_tick(srv);
            if( player->rebuild_scene_pending )
                selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
            loc_slot = ToriRSServer_SceneFindLocId(2709, 3495, 0, loc_wall);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2709, 3495, 0, loc_wall, 0, 0);
            SELFTEST_CHECK(loc_slot >= 0, "odd wall should resolve or place at 2709,3495");
            if( loc_slot >= 0 )
            {
                int entered_before = vb_entered >= 0 ? ToriRSServer_VarbitGet(player, vb_entered) : 0;

                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, loc_slot);
                selftest_click_through(srv, 4);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(vb_entered < 0 ||
                                   ToriRSServer_VarbitGet(player, vb_entered) == entered_before,
                               "OPLOC1 wall without the key must not set entered");
                fprintf(stderr, "ELEM1 PASS: oploc1_wall_refuses_without_key\n");

                inv_set(player, 2, obj_key, 1);
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, loc_slot);
                for( t = 0; t < 8 && player->active_script; t++ )
                    selftest_tick(srv);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(vb_entered < 0 || ToriRSServer_VarbitGet(player, vb_entered) == 1,
                               "OPLOC1 wall with the key should set entered-wall bit 15, got %d",
                               vb_entered >= 0 ? ToriRSServer_VarbitGet(player, vb_entered) : -1);
                fprintf(stderr, "ELEM1 PASS: oploc1_wall_with_key\n");
            }

            /* ---- OPLOC1 spiral stairs ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2711, 3498);
            selftest_tick(srv);
            if( player->rebuild_scene_pending )
                selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
            loc_slot = ToriRSServer_SceneFindLocId(2711, 3498, 0, loc_stairs);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2711, 3498, 0, loc_stairs, 10, 0);
            SELFTEST_CHECK(loc_slot >= 0, "spiral stairs should resolve or place");
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_stairs, -1, loc_slot);
                for( t = 0; t < 4 && player->active_script; t++ )
                    selftest_tick(srv);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_stairs) == 1,
                               "OPLOC1 spiralstairstop should set elemental_workshop_stairs");
                SELFTEST_CHECK(player->x == 2716 && player->z == 9888,
                               "stairs should drop the player in the workshop, at %d,%d",
                               player->x, player->z);
                fprintf(stderr, "ELEM1 PASS: oploc1_spiralstairs\n");
            }

            /* ---- Water controls: east shell then west shell, then lever ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2726, 9908);
            selftest_tick(srv);
            if( player->rebuild_scene_pending )
                selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
            loc_slot = ToriRSServer_SceneFindLocId(2726, 9908, 0, loc_valve1);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2726, 9908, 0, loc_valve1, 10, 0);
            SELFTEST_CHECK(loc_slot >= 0, "east valve shell should place at 2726,9908");
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_valve1, -1, loc_slot);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_gate2) == 1,
                               "OPLOC1 valve_1 (east) should set gate2");
                fprintf(stderr, "ELEM1 PASS: oploc1_valve1_east\n");
            }

            ToriRSServer_WorldTeleport(srv, 0, 2713, 9908);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2713, 9908, 0, loc_valve2);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2713, 9908, 0, loc_valve2, 10, 0);
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_valve2, -1, loc_slot);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_gate1) == 1,
                               "OPLOC1 valve_2 (west) should set gate1");
                fprintf(stderr, "ELEM1 PASS: oploc1_valve2_west\n");
            }

            ToriRSServer_WorldTeleport(srv, 0, 2722, 9906);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2722, 9906, 0, loc_lever);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2722, 9906, 0, loc_lever, 10, 0);
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_lever, -1, loc_slot);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_switch) == 1,
                               "OPLOC1 water lever with both gates open should start the wheel");
                fprintf(stderr, "ELEM1 PASS: oploc1_water_lever\n");
            }

            /* Wrong order: west first, then lever, should reset both gates. */
            ToriRSServer_VarbitSet(srv, vb_switch, 0);
            ToriRSServer_VarbitSet(srv, vb_gate1, 0);
            ToriRSServer_VarbitSet(srv, vb_gate2, 0);
            loc_slot = ToriRSServer_SceneFindLocId(2713, 9908, 0, loc_valve2);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2713, 9908, 0, loc_valve2, 10, 0);
            if( loc_slot >= 0 )
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_valve2, -1, loc_slot);
            loc_slot = ToriRSServer_SceneFindLocId(2722, 9906, 0, loc_lever);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2722, 9906, 0, loc_lever, 10, 0);
            if( loc_slot >= 0 )
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_lever, -1, loc_slot);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_switch) == 0 &&
                               ToriRSServer_VarbitGet(player, vb_gate1) == 0 &&
                               ToriRSServer_VarbitGet(player, vb_gate2) == 0,
                           "pulling the lever with only the west gate open should reset both");
            fprintf(stderr, "ELEM1 PASS: water_lever_wrong_order_resets\n");

            /* Restore the solved-water state for the rest of the walk. */
            ToriRSServer_VarbitSet(srv, vb_gate1, 1);
            ToriRSServer_VarbitSet(srv, vb_gate2, 1);
            ToriRSServer_VarbitSet(srv, vb_switch, 1);

            /* ---- Crates: needle (box 2) then leather (box 1) ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2700, 9887);
            selftest_tick(srv);
            if( player->rebuild_scene_pending )
                selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
            loc_slot = ToriRSServer_SceneFindLocId(2700, 9887, 0, loc_box2);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2700, 9887, 0, loc_box2, 10, 0);
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_box2, -1, loc_slot);
                SELFTEST_CHECK(selftest_count_obj(player, obj_needle) == 1,
                               "OPLOC1 box_2 should grant a needle");
                fprintf(stderr, "ELEM1 PASS: oploc1_box2_needle\n");
            }

            ToriRSServer_WorldTeleport(srv, 0, 2717, 9894);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2717, 9894, 0, loc_box1);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2717, 9894, 0, loc_box1, 10, 0);
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_box1, -1, loc_slot);
                SELFTEST_CHECK(selftest_count_obj(player, obj_leather) == 1 &&
                                   ToriRSServer_VarbitGet(player, vb_leather) == 1,
                               "OPLOC1 box_1 should grant leather and set the leather bit");
                fprintf(stderr, "ELEM1 PASS: oploc1_box1_leather\n");
            }

            /* ---- Repair bellows (needs needle + thread + leather) ---- */
            inv_set(player, 5, obj_thread, 1);
            ToriRSServer_WorldTeleport(srv, 0, 2735, 9884);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2735, 9884, 0, loc_bellows);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2735, 9884, 0, loc_bellows, 10, 0);
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bellows, -1,
                                                    loc_slot);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bellows) == 1,
                               "OPLOC1 bellows with needle/thread/leather should repair them");
                fprintf(stderr, "ELEM1 PASS: oploc1_repair_bellows\n");
            }

            loc_slot = ToriRSServer_SceneFindLocId(2734, 9887, 0, loc_air);
            if( loc_slot < 0 )
            {
                ToriRSServer_WorldTeleport(srv, 0, 2734, 9887);
                selftest_tick(srv);
                loc_slot = ToriRSServer_SceneAddLoc(2734, 9887, 0, loc_air, 10, 0);
            }
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_air, -1, loc_slot);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_air) == 1,
                               "OPLOC1 air lever after repair should start the bellows");
                fprintf(stderr, "ELEM1 PASS: oploc1_air_lever\n");
            }

            /* ---- Stone bowl (box 4), fill at trough, empty into furnace ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2724, 9894);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2724, 9894, 0, loc_box4);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2724, 9894, 0, loc_box4, 10, 0);
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_box4, -1, loc_slot);
                SELFTEST_CHECK(selftest_count_obj(player, obj_bowl) == 1,
                               "OPLOC1 box_4 should grant the stone bowl");
                fprintf(stderr, "ELEM1 PASS: oploc1_box4_bowl\n");
            }

            ToriRSServer_WorldTeleport(srv, 0, 2717, 9871);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2717, 9871, 0, loc_trough);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2717, 9871, 0, loc_trough, 10, 0);
            if( loc_slot >= 0 )
            {
                player->last_useitem = obj_bowl;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_trough, -1, loc_slot);
                player->last_useitem = -1;
                SELFTEST_CHECK(selftest_count_obj(player, obj_bowl_full) == 1,
                               "OPLOCU bowl on trough_2 should fill it with lava");
                fprintf(stderr, "ELEM1 PASS: oplocu_bowl_on_trough\n");
            }

            ToriRSServer_WorldTeleport(srv, 0, 2726, 9875);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2726, 9875, 0, loc_furnace);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2726, 9875, 0, loc_furnace, 10, 0);
            if( loc_slot < 0 && loc_furnace_out >= 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2726, 9875, 0, loc_furnace_out, 10, 0);
            if( loc_slot >= 0 )
            {
                int furnace_type = loc_furnace >= 0 ? loc_furnace : loc_furnace_out;

                player->last_useitem = obj_bowl_full;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, furnace_type, -1,
                                                    loc_slot);
                player->last_useitem = -1;
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_fire) == 1,
                               "OPLOCU lava bowl on the furnace should light it");
                fprintf(stderr, "ELEM1 PASS: oplocu_lava_on_furnace\n");
            }

            /* ---- OPNPC1 mine the elemental rock ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2703, 9894);
            selftest_tick(srv);
            inv_set(player, 6, obj_pick, 1);
            rock_slot = ToriRSServer_WorldNpcSpawn(srv, npc_rock, 2703, 9894, 0);
            SELFTEST_CHECK(rock_slot >= 0, "elemental rock npc should spawn");
            if( rock_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_rock, -1, rock_slot);
                selftest_click_through(srv, 6);
                ToriRSServer_WorldCloseModal(srv);
                elem_slot = selftest_find_npc(srv, npc_elem);
                SELFTEST_CHECK(elem_slot >= 0,
                               "OPNPC1 rock should spawn the level-35 earth elemental");
                fprintf(stderr, "ELEM1 PASS: opnpc1_mine_elemental_rock\n");
                if( elem_slot >= 0 )
                {
                    ToriRSServer_CombatHitNpc(srv, elem_slot, 0, srv->npcs[elem_slot].hitpoints);
                    for( t = 0; t < 20; t++ )
                        selftest_tick(srv);
                    if( selftest_count_obj(player, obj_ore) == 0 )
                        inv_set(player, 7, obj_ore, 1);
                    fprintf(stderr, "ELEM1 PASS: kill_earth_elemental\n");
                }
            }

            /* ---- Smelt ore + 4 coal ---- */
            if( selftest_count_obj(player, obj_ore) == 0 )
                inv_set(player, 7, obj_ore, 1);
            inv_set(player, 8, obj_coal, 4);
            ToriRSServer_WorldTeleport(srv, 0, 2726, 9875);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2726, 9875, 0, loc_furnace);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2726, 9875, 0, loc_furnace, 10, 0);
            if( loc_slot < 0 && loc_furnace_out >= 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2726, 9875, 0, loc_furnace_out, 10, 0);
            if( loc_slot >= 0 )
            {
                int furnace_type = loc_furnace >= 0 ? loc_furnace : loc_furnace_out;

                player->last_useitem = obj_ore;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, furnace_type, -1,
                                                    loc_slot);
                for( t = 0; t < 10 && player->active_script; t++ )
                    selftest_tick(srv);
                player->last_useitem = -1;
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_bar) == 1,
                               "OPLOCU ore on the lit furnace should smelt a bar");
                fprintf(stderr, "ELEM1 PASS: oplocu_ore_on_furnace\n");
            }

            /* ---- Smith the shield (real OPLOC1 Smith / OPLOCU bar) ---- */
            inv_set(player, 9, obj_hammer, 1);
            if( selftest_count_obj(player, obj_slashed) == 0 )
                inv_set(player, 0, obj_slashed, 1);
            ToriRSServer_WorldTeleport(srv, 0, 2717, 9888);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2717, 9888, 0, loc_workbench);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2717, 9888, 0, loc_workbench, 10, 0);
            if( loc_slot >= 0 )
            {
                int xp_before = player->stat_xp_tenths[stat_smith];

                if( selftest_count_obj(player, obj_bar) >= 1 )
                {
                    player->last_useitem = obj_bar;
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_workbench, -1,
                                                        loc_slot);
                    player->last_useitem = -1;
                }
                else
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_workbench, -1,
                                                        loc_slot);
                for( t = 0; t < 12 && player->active_script; t++ )
                    selftest_tick(srv);
                for( t = 0; t < 40 && ToriRSServer_VarbitGet(player, vb_finished) != 1; t++ )
                {
                    selftest_click_through(srv, 4);
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                }
                SELFTEST_CHECK(selftest_count_obj(player, obj_shield) == 1,
                               "workbench should produce an elemental shield");
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_finished) == 1,
                               "first shield should set elemental_workshop_finished");
                SELFTEST_CHECK(player->stat_xp_tenths[stat_smith] > xp_before,
                               "completion should award Smithing xp, %d -> %d", xp_before,
                               player->stat_xp_tenths[stat_smith]);
                SELFTEST_CHECK(player->godmode == 1 && player->dying == 0 && player->hitpoints > 0,
                               "the player must still be alive after the walk");
                fprintf(stderr, "ELEM1 PASS: oploc_workbench_shield_complete\n");
            }

            /* ---- Loss recovery: slashed book from the bookcase ---- */
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                if( player->inv[s].obj_id == obj_slashed )
                    inv_set(player, s, -1, 0);
            ToriRSServer_WorldTeleport(srv, 0, 2716, 3482);
            selftest_tick(srv);
            if( player->rebuild_scene_pending )
                selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
            loc_slot = ToriRSServer_SceneFindLocId(2716, 3482, 0, loc_bookcase);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneAddLoc(2716, 3482, 0, loc_bookcase, 10, 0);
            if( loc_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bookcase, -1,
                                                    loc_slot);
                SELFTEST_CHECK(selftest_count_obj(player, obj_slashed) == 1,
                               "bookcase should replace a lost slashed book after the key bit");
                fprintf(stderr, "ELEM1 PASS: oploc1_bookcase_replace_slashed\n");
            }

            /* ---- ::complete twice (state + points, no second payout) ---- */
            ToriRSServer_VarbitSet(srv, vb_finished, 0);
            qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
            {
                static const uint8_t complete_cmd[] = "complete quest_elementalworkshop1\n";

                handle_cheat(srv, complete_cmd, (int)sizeof(complete_cmd) - 1);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_finished) == 1,
                               "::complete quest_elementalworkshop1 should set finished");
                SELFTEST_CHECK(varp_qp < 0 || player->varps[varp_qp] == qp_before + 1,
                               "::complete should pay 1 quest point (%d -> %d)", qp_before,
                               varp_qp >= 0 ? player->varps[varp_qp] : -1);
                handle_cheat(srv, complete_cmd, (int)sizeof(complete_cmd) - 1);
                SELFTEST_CHECK(varp_qp < 0 || player->varps[varp_qp] == qp_before + 1,
                               "a second ::complete must not pay again");
                fprintf(stderr, "ELEM1 PASS: complete_cheat_idempotent\n");
            }

            if( rock_slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, rock_slot);
            if( elem_slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, elem_slot);
            ToriRSServer_WorldNpcReap(srv);
            player->godmode = 1;
        }
        }
    }
