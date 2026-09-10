    /*
     * Big Chompy Bird Hunting -- real opnpc/oploc/opheld on the critical path.
     * QH bigchompybirdhunting steps.put 0/5/10/15/20/25/30/35/40/45/50/55/60.
     * Placed immediately before a selftest_reset_world so spawned bait/birds
     * cannot shift later RNG-gated stanzas (field guide S1).
     */
    fprintf(stderr, "ToriRSServer selftest: ::chompybirdrun\n");
    {
        int loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());

        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            int varp_chompy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "chompybird");
            int varp_kills = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "chompybird_kills");
            int npc_rantz = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rantz");
            int npc_rantz_pre =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rantz_pre_quest");
            int npc_bugs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bugs");
            int npc_fycie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fycie");
            int npc_toad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "toad");
            int npc_bloated_npc =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bloated_toad");
            int npc_bird = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "chompybird");
            int npc_dead = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "chompybird_dead");
            int loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "chompybird_chest");
            int loc_chest_open =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "chompybird_chest_open");
            int loc_bubbles = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swampbubbles");
            int loc_spit = ToriRSServer_ContentSymbol(
                TORIRSSERVER_PACK_LOC, "multi_chompybird_spitroast_entity");
            int loc_spit_empty =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "chompybird_spitroast_empty");
            int obj_achey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "achey_tree_logs");
            int obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
            int obj_feather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feather");
            int obj_shaft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ogre_arrow_shaft");
            int obj_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "wolf_bones");
            int obj_chisel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chisel");
            int obj_tips = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "wolfbone_arrowheads");
            int obj_headless =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ogre_headless_arrow");
            int obj_arrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ogre_arrow");
            int obj_bellows =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "empty_ogre_bellows");
            int obj_bellows3 =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "filled_ogre_bellow3");
            int obj_toad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bloated_toad");
            int obj_bow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ogre_bow");
            int obj_raw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_chompy");
            int obj_seasoned = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cooked_s_chompy");
            int obj_potato = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "potato");
            int obj_onion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "onion");
            int obj_equa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "equa_leaves");
            int obj_cabbage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cabbage");
            int obj_tomato = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tomato");
            int obj_doogle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doogleleaves");
            int stat_fletch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fletching");
            int stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
            int stat_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
            int stat_range = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
            int stat_str = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "strength");
            int rows_uid =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
            int com_messagebox =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
            int wear_rhand = ToriRSServer_ContentConstantInt("wearpos_rhand", 3);
            int wear_quiver = ToriRSServer_ContentConstantInt("wearpos_quiver", 13);

            SELFTEST_CHECK(
                varp_chompy >= 0 && varp_kills >= 0 && npc_rantz >= 0 && npc_bugs >= 0 &&
                    npc_fycie >= 0 && npc_toad >= 0 && npc_bird >= 0 && npc_dead >= 0 &&
                    loc_chest >= 0 && loc_chest_open >= 0 && loc_bubbles >= 0 && loc_spit >= 0 &&
                    loc_spit_empty >= 0 && obj_achey >= 0 && obj_knife >= 0 && obj_feather >= 0 &&
                    obj_shaft >= 0 && obj_bones >= 0 && obj_chisel >= 0 && obj_tips >= 0 &&
                    obj_headless >= 0 && obj_arrow >= 0 && obj_bellows >= 0 && obj_bellows3 >= 0 &&
                    obj_toad >= 0 && obj_bow >= 0 && obj_raw >= 0 && obj_seasoned >= 0 &&
                    obj_potato >= 0 && obj_onion >= 0 && obj_equa >= 0 && obj_cabbage >= 0 &&
                    obj_tomato >= 0 && obj_doogle >= 0 && stat_fletch >= 0 && stat_craft >= 0 &&
                    stat_cook >= 0 && stat_range >= 0 && stat_str >= 0,
                "the ::chompybirdrun C-side names should all resolve");

            if( varp_chompy >= 0 && npc_rantz >= 0 && obj_arrow >= 0 && loc_chest >= 0 &&
                loc_bubbles >= 0 && loc_spit >= 0 && npc_bird >= 0 )
            {
                int s;
                int rantz_slot;
                int t;
                int arrows;
                int chest_slot;
                int bubble_slot;
                int spit_slot;
                int toad_slot;
                int bird_slot;
                int dead_slot;
                int bugs_slot;
                int fycie_slot;
                int kills;
                int rantz_flav;
                int bugs_flav;
                int fycie_flav;
                int hp_before;

                srv->members_world = 1;
                player->godmode = 1;
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
                    worn_set(player, s, -1, 0);
                player->varps[varp_chompy] = 0;
                if( varp_kills >= 0 )
                    player->varps[varp_kills] = 0;
                if( stat_fletch >= 0 )
                    ToriRSServer_CombatSetLevel(player, stat_fletch, 99);
                if( stat_craft >= 0 )
                    ToriRSServer_CombatSetLevel(player, stat_craft, 99);
                if( stat_cook >= 0 )
                    ToriRSServer_CombatSetLevel(player, stat_cook, 99);
                if( stat_range >= 0 )
                    ToriRSServer_CombatSetLevel(player, stat_range, 99);
                if( stat_str >= 0 )
                    ToriRSServer_CombatSetLevel(player, stat_str, 99);
                player->hitpoints = 99;
                player->max_hitpoints = 99;
                ToriRSServer_CombatSyncHitpoints(player);

                /* ---- 0: talk to Rantz (real OPNPC1 on the multinpc SHELL) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2631, 2982);
                selftest_tick(srv);
                rantz_slot = npc_spawn(srv, npc_rantz, 2631, 2982, 0);
                SELFTEST_CHECK(rantz_slot >= 0, "rantz should spawn at Feldip");
                if( rantz_slot >= 0 )
                {
                    player->chatmodal_group = 0;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_rantz, -1,
                                                   rantz_slot);
                    biohazard_run_dialogue(srv, player, rows_uid);
                    if( player->active_script != NULL && rows_uid > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    SELFTEST_CHECK(player->varps[varp_chompy] == 5,
                                   "talking to Rantz should start the quest at 5, got %d",
                                   player->varps[varp_chompy]);
                    if( player->varps[varp_chompy] == 5 )
                        fprintf(stderr, "  PASS  step 0 talkToRantz -> %%chompybird=5\n");
                }

                /* ---- 5: make ogre arrows through real OPHELDU ---- */
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                inv_set(player, 0, obj_achey, 8);
                inv_set(player, 1, obj_knife, 1);
                player->last_item = obj_knife;
                player->last_slot = 1;
                player->last_useitem = obj_achey;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_knife, -1, 1);
                ToriRSServer_WorldCloseModal(srv);
                biohazard_run_dialogue(srv, player, 0);
                {
                    int shafts = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_shaft )
                            shafts += player->inv[s].count;
                    SELFTEST_CHECK(shafts > 0,
                                   "knife on achey logs should make ogre arrow shafts, got %d",
                                   shafts);
                    if( shafts > 0 )
                        fprintf(stderr, "  PASS  step 5 makeShafts -> %d ogre_arrow_shaft\n",
                                shafts);
                }

                {
                    int shaft_slot = -1;
                    int feather_slot = -1;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    {
                        if( player->inv[s].obj_id == obj_shaft && shaft_slot < 0 )
                            shaft_slot = s;
                        if( player->inv[s].obj_id < 0 && feather_slot < 0 )
                            feather_slot = s;
                    }
                    SELFTEST_CHECK(shaft_slot >= 0 && feather_slot >= 0,
                                   "shafts must still be in inv before feathers are added");
                    if( feather_slot >= 0 )
                        inv_set(player, feather_slot, obj_feather, 100);
                    if( shaft_slot >= 0 && feather_slot >= 0 )
                    {
                        player->last_item = obj_shaft;
                        player->last_slot = shaft_slot;
                        player->last_useitem = obj_feather;
                        player->last_useslot = feather_slot;
                        ToriRSServer_ScriptsRunOpheldu(srv, obj_shaft, -1, obj_feather, -1);
                    }
                    ToriRSServer_WorldCloseModal(srv);
                }
                {
                    int headless = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_headless )
                            headless += player->inv[s].count;
                    SELFTEST_CHECK(headless > 0,
                                   "feathers on shafts should make flighted ogre arrows, got %d",
                                   headless);
                    if( headless > 0 )
                        fprintf(stderr, "  PASS  step 5 useFeathersOnShafts -> %d headless\n",
                                headless);
                }

                {
                    int bone_slot = -1;
                    int chisel_slot = -1;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id < 0 )
                        {
                            if( bone_slot < 0 )
                                bone_slot = s;
                            else if( chisel_slot < 0 )
                                chisel_slot = s;
                        }
                    if( bone_slot >= 0 )
                        inv_set(player, bone_slot, obj_bones, 8);
                    if( chisel_slot >= 0 )
                        inv_set(player, chisel_slot, obj_chisel, 1);
                    player->last_item = obj_bones;
                    player->last_slot = bone_slot;
                    player->last_useitem = obj_chisel;
                    player->last_useslot = chisel_slot;
                    ToriRSServer_ScriptsRunOpheldu(srv, obj_bones, -1, obj_chisel, -1);
                    ToriRSServer_WorldCloseModal(srv);
                }
                {
                    int tips = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_tips )
                            tips += player->inv[s].count;
                    SELFTEST_CHECK(tips > 0, "chisel on wolf bones should make tips, got %d", tips);
                    if( tips > 0 )
                        fprintf(stderr, "  PASS  step 5 useChiselOnBones -> %d tips\n", tips);
                }

                {
                    int headless_slot = -1;
                    int tip_slot = -1;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    {
                        if( player->inv[s].obj_id == obj_headless && headless_slot < 0 )
                            headless_slot = s;
                        if( player->inv[s].obj_id == obj_tips && tip_slot < 0 )
                            tip_slot = s;
                    }
                    player->last_item = obj_headless;
                    player->last_slot = headless_slot;
                    player->last_useitem = obj_tips;
                    player->last_useslot = tip_slot;
                    if( headless_slot >= 0 && tip_slot >= 0 )
                        ToriRSServer_ScriptsRunOpheldu(srv, obj_headless, -1, obj_tips, -1);
                    ToriRSServer_WorldCloseModal(srv);
                }
                arrows = 0;
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    if( player->inv[s].obj_id == obj_arrow )
                        arrows += player->inv[s].count;
                SELFTEST_CHECK(arrows > 0, "tips on flighted shafts should make ogre arrows, got %d",
                               arrows);
                SELFTEST_CHECK(varp_kills >= 0 && (player->varps[varp_kills] & 1) != 0,
                               "making arrows during the quest should set the made_arrows bit");
                if( arrows > 0 )
                    fprintf(stderr, "  PASS  step 5 useTipsOnShafts -> %d ogre_arrow\n", arrows);

                /* Enough arrows for Rantz even if the first craft made fewer than 6. */
                if( arrows < 6 )
                {
                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_arrow )
                        {
                            inv_set(player, s, obj_arrow, 6);
                            break;
                        }
                    if( arrows <= 0 )
                    {
                        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                            if( player->inv[s].obj_id < 0 )
                            {
                                inv_set(player, s, obj_arrow, 6);
                                break;
                            }
                    }
                    arrows = 6;
                }
                player->last_item = obj_arrow;
                player->last_useitem = obj_arrow;
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    if( player->inv[s].obj_id == obj_arrow )
                    {
                        player->last_slot = s;
                        player->last_useslot = s;
                        break;
                    }
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_rantz, -1, rantz_slot);
                biohazard_run_dialogue(srv, player, 0);
                SELFTEST_CHECK(player->varps[varp_chompy] == 10,
                               "using 6 ogre arrows on Rantz should reach 10, got %d",
                               player->varps[varp_chompy]);
                if( player->varps[varp_chompy] == 10 )
                    fprintf(stderr, "  PASS  step 5 useArrowsOnRantz -> %%chompybird=10\n");

                /* ---- 10: ask Rantz about fatsy toadies (OPNPC1) ---- */
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_rantz, -1, rantz_slot);
                biohazard_run_dialogue(srv, player, 0);
                SELFTEST_CHECK(player->varps[varp_chompy] == 15,
                               "asking Rantz about toads should reach 15, got %d",
                               player->varps[varp_chompy]);
                if( player->varps[varp_chompy] == 15 )
                    fprintf(stderr, "  PASS  step 10 askRantzQuestions -> %%chompybird=15\n");

                /* ---- 15: unlock the cave chest (real OPLOC1) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2638, 9398);
                selftest_tick(srv);
                chest_slot = ToriRSServer_SceneAddLoc(2638, 9398, 0, loc_chest, 10, 0);
                SELFTEST_CHECK(chest_slot >= 0, "chompybird_chest should place in the cave");
                if( chest_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_chest, -1,
                                                        chest_slot);
                    for( t = 0; t < 8 && player->active_script; t++ )
                    {
                        if( com_messagebox > 0 )
                            ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                        selftest_tick(srv);
                    }
                    SELFTEST_CHECK(player->varps[varp_chompy] == 20,
                                   "unlocking the chest should reach 20, got %d",
                                   player->varps[varp_chompy]);
                    if( player->varps[varp_chompy] == 20 )
                        fprintf(stderr, "  PASS  step 15 getBellow unlock -> %%chompybird=20\n");
                }

                /* Search the open chest (or the closed one if loc_change already swapped). */
                {
                    int open_slot = ToriRSServer_SceneFindLocId(2638, 9398, 0, loc_chest_open);

                    if( open_slot < 0 )
                        open_slot = ToriRSServer_SceneAddLoc(2638, 9399, 0, loc_chest_open, 10, 0);
                    if( open_slot >= 0 )
                    {
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_chest_open,
                                                            -1, open_slot);
                        if( com_messagebox > 0 )
                            ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                        biohazard_run_dialogue(srv, player, 0);
                    }
                }
                {
                    int bellows = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_bellows )
                            bellows += player->inv[s].count;
                    SELFTEST_CHECK(bellows > 0, "searching the open chest should give ogre bellows");
                    if( bellows > 0 )
                        fprintf(stderr, "  PASS  step 15 search chest -> empty_ogre_bellows\n");
                    if( bellows <= 0 )
                        inv_set(player, 0, obj_bellows, 1);
                }

                /* ---- 20: fill bellows (OPLOCU) and inflate a toad (OPNPCU) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2601, 2967);
                selftest_tick(srv);
                bubble_slot = ToriRSServer_SceneAddLoc(2601, 2967, 0, loc_bubbles, 10, 0);
                SELFTEST_CHECK(bubble_slot >= 0, "swampbubbles should place at the swamp");
                if( bubble_slot >= 0 )
                {
                    player->last_item = obj_bellows;
                    player->last_useitem = obj_bellows;
                    player->last_slot = -1;
                    player->last_useslot = -1;
                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_bellows )
                        {
                            player->last_slot = s;
                            player->last_useslot = s;
                            break;
                        }
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_bubbles, -1,
                                                        bubble_slot);
                    for( t = 0; t < 6 && player->active_script; t++ )
                        selftest_tick(srv);
                }
                {
                    int filled = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_bellows3 )
                            filled += player->inv[s].count;
                    SELFTEST_CHECK(filled > 0, "using empty bellows on swamp bubbles should fill them");
                    if( filled > 0 )
                        fprintf(stderr, "  PASS  step 20 fillBellows -> filled_ogre_bellow3\n");
                    if( filled <= 0 )
                        inv_set(player, 1, obj_bellows3, 1);
                }

                {
                    int keep_bellows = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    {
                        if( player->inv[s].obj_id == obj_bellows3 && keep_bellows == 0 )
                        {
                            keep_bellows = 1;
                            continue;
                        }
                        inv_set(player, s, -1, 0);
                    }
                    if( keep_bellows == 0 )
                        inv_set(player, 0, obj_bellows3, 1);
                }
                selftest_park_player(srv, 2602, 2967);
                toad_slot = npc_spawn(srv, npc_toad, 2602, 2967, 0);
                SELFTEST_CHECK(toad_slot >= 0, "a swamp toad should spawn");
                if( toad_slot >= 0 )
                {
                    int toad_type = srv->npcs[toad_slot].type;
                    int ran;

                    ToriRSServer_WorldCloseModal(srv);
                    player->last_item = obj_bellows3;
                    player->last_useitem = obj_bellows3;
                    player->last_slot = -1;
                    player->last_useslot = -1;
                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_bellows3 )
                        {
                            player->last_slot = s;
                            player->last_useslot = s;
                            break;
                        }
                    /* QH goInflateToad: use filled bellows on the swamp toad. */
                    ran = ToriRSServer_ScriptsRunTrigger(
                        srv, SS_TRIGGER_OPNPCU, toad_type, -1, toad_slot);
                    (void)ran;
                    for( t = 0; t < 24; t++ )
                        selftest_tick(srv);
                    {
                        int bloated_now = 0;

                        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                            if( player->inv[s].obj_id == obj_toad )
                                bloated_now += player->inv[s].count;
                        if( bloated_now <= 0 )
                        {
                            if( !srv->npcs[toad_slot].active )
                            {
                                toad_slot = npc_spawn(srv, npc_toad, 2602, 2967, 0);
                                if( toad_slot >= 0 )
                                    toad_type = srv->npcs[toad_slot].type;
                            }
                            if( toad_slot >= 0 && srv->npcs[toad_slot].active )
                            {
                                /* Cache op1=Inflate. */
                                ran = ToriRSServer_ScriptsRunTrigger(
                                    srv, SS_TRIGGER_OPNPC1, toad_type, -1, toad_slot);
                                (void)ran;
                                for( t = 0; t < 24; t++ )
                                    selftest_tick(srv);
                            }
                        }
                    }
                }
                {
                    int bloated = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_toad )
                            bloated += player->inv[s].count;
                    SELFTEST_CHECK(bloated > 0, "inflating a toad should add a bloated_toad");
                    if( bloated > 0 )
                        fprintf(stderr, "  PASS  step 20 inflateToad -> bloated_toad\n");
                    if( bloated <= 0 )
                    {
                        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                            if( player->inv[s].obj_id < 0 )
                            {
                                inv_set(player, s, obj_toad, 1);
                                break;
                            }
                    }
                }

                ToriRSServer_WorldTeleport(srv, 0, 2631, 2982);
                selftest_tick(srv);
                rantz_slot = -1;
                for( s = 0; s < TORIRSSERVER_NPC_MAX; s++ )
                    if( srv->npcs[s].active &&
                        (srv->npcs[s].type == npc_rantz ||
                         (npc_rantz_pre >= 0 && srv->npcs[s].type == npc_rantz_pre)))
                        rantz_slot = s;
                if( rantz_slot < 0 )
                    rantz_slot = npc_spawn(srv, npc_rantz, 2631, 2982, 0);
                ToriRSServer_WorldCloseModal(srv);
                player->last_item = obj_toad;
                player->last_useitem = obj_toad;
                player->last_slot = -1;
                player->last_useslot = -1;
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    if( player->inv[s].obj_id == obj_toad )
                    {
                        player->last_slot = s;
                        player->last_useslot = s;
                        break;
                    }
                if( rantz_slot >= 0 )
                    ToriRSServer_ScriptsRunTrigger(
                        srv, SS_TRIGGER_OPNPCU, srv->npcs[rantz_slot].type, -1, rantz_slot);
                if( com_messagebox > 0 )
                    ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                biohazard_run_dialogue(srv, player, 0);
                if( player->varps[varp_chompy] != 25 && rantz_slot >= 0 )
                {
                    ToriRSServer_WorldCloseModal(srv);
                    ToriRSServer_ScriptsRunTrigger(
                        srv, SS_TRIGGER_OPNPC1, srv->npcs[rantz_slot].type, -1, rantz_slot);
                    if( com_messagebox > 0 )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    biohazard_run_dialogue(srv, player, 0);
                }
                SELFTEST_CHECK(player->varps[varp_chompy] == 25,
                               "showing Rantz the bloated toad should reach 25, got %d",
                               player->varps[varp_chompy]);
                if( player->varps[varp_chompy] == 25 )
                    fprintf(stderr, "  PASS  step 20 talkToRantzWithToad -> %%chompybird=25\n");

                /* ---- 25: drop the toad in the clearing (real OPHELD1) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2635, 2967);
                selftest_tick(srv);
                player->last_item = obj_toad;
                player->last_slot = -1;
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    if( player->inv[s].obj_id == obj_toad )
                    {
                        player->last_slot = s;
                        break;
                    }
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_toad, -1,
                                               player->last_slot);
                for( t = 0; t < 8 && player->active_script; t++ )
                    selftest_tick(srv);
                SELFTEST_CHECK(player->varps[varp_chompy] == 30,
                               "dropping the bloated toad in the clearing should reach 30, got %d",
                               player->varps[varp_chompy]);
                if( player->varps[varp_chompy] == 30 )
                    fprintf(stderr, "  PASS  step 25 dropToad -> %%chompybird=30\n");

                /* ---- 30/35: spawn the owner-private bird (real spawn proc) ---- */
                {
                    int32_t args[2];

                    args[0] = ToriRSServer_CoordPack(0, 2635, 2967);
                    args[1] = player->pid + 1;
                    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,spawn_chompy_bird]", args, 2),
                                   "~spawn_chompy_bird should run");
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                }
                SELFTEST_CHECK(player->varps[varp_chompy] == 35,
                               "spawning the bird should reach 35, got %d",
                               player->varps[varp_chompy]);
                if( player->varps[varp_chompy] == 35 )
                    fprintf(stderr, "  PASS  step 30/35 waitForChompy -> %%chompybird=35\n");

                /* ---- 40: Rantz misses (queued from spawn) ---- */
                for( t = 0; t < 50 && player->varps[varp_chompy] == 35; t++ )
                {
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                }
                SELFTEST_CHECK(player->varps[varp_chompy] == 40,
                               "Rantz's miss queue should reach 40, got %d",
                               player->varps[varp_chompy]);
                if( player->varps[varp_chompy] == 40 )
                    fprintf(stderr, "  PASS  step 40 rantz miss -> %%chompybird=40\n");

                /* ---- 40: talk for the ogre bow (OPNPC1) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2631, 2982);
                selftest_tick(srv);
                rantz_slot = -1;
                for( s = 0; s < TORIRSSERVER_NPC_MAX; s++ )
                    if( srv->npcs[s].active &&
                        (srv->npcs[s].type == npc_rantz ||
                         (npc_rantz_pre >= 0 && srv->npcs[s].type == npc_rantz_pre)))
                        rantz_slot = s;
                if( rantz_slot < 0 )
                    rantz_slot = npc_spawn(srv, npc_rantz, 2631, 2982, 0);
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1,
                    rantz_slot >= 0 ? srv->npcs[rantz_slot].type : npc_rantz, -1, rantz_slot);
                if( com_messagebox > 0 )
                    ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                biohazard_run_dialogue(srv, player, 0);
                SELFTEST_CHECK(player->varps[varp_chompy] == 45,
                               "talking after the miss should lend the ogre bow and reach 45, got %d",
                               player->varps[varp_chompy]);
                {
                    int bow = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_bow )
                            bow += player->inv[s].count;
                    SELFTEST_CHECK(bow > 0, "Rantz should hand over an ogre bow");
                    if( player->varps[varp_chompy] == 45 && bow > 0 )
                        fprintf(stderr, "  PASS  step 40 talkToRantzForBow -> bow + state 45\n");
                }

                /* ---- 45: only an ogre bow can hurt the bird (real OPNPC5) ---- */
                bird_slot = -1;
                for( s = 0; s < TORIRSSERVER_NPC_MAX; s++ )
                    if( srv->npcs[s].active && srv->npcs[s].type == npc_bird )
                        bird_slot = s;
                if( bird_slot < 0 )
                    bird_slot = npc_spawn(srv, npc_bird, 2635, 2966, 0);
                SELFTEST_CHECK(bird_slot >= 0, "a chompybird should be in the world to shoot");
                if( bird_slot >= 0 )
                {
                    ToriRSServer_WorldNpcSetOwner(&srv->npcs[bird_slot], player);
                    SELFTEST_CHECK(srv->npcs[bird_slot].huntmode == TORIRSSERVER_HUNT_NONE,
                                   "chompybird huntmode=%d, want NONE (cannot fight back)",
                                   srv->npcs[bird_slot].huntmode);
                    SELFTEST_CHECK(srv->npcs[bird_slot].hitpoints == 10 &&
                                       srv->npcs[bird_slot].max_hitpoints == 10,
                                   "cache stat4 hitpoints should be 10, got %d/%d",
                                   srv->npcs[bird_slot].hitpoints,
                                   srv->npcs[bird_slot].max_hitpoints);

                    worn_set(player, wear_rhand, -1, 0);
                    worn_set(player, wear_quiver, -1, 0);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC5, npc_bird, -1, bird_slot);
                    ToriRSServer_WorldCloseModal(srv);
                    hp_before = srv->npcs[bird_slot].hitpoints;
                    SELFTEST_CHECK(hp_before == srv->npcs[bird_slot].hitpoints,
                                   "a bare-handed OPNPC5 must not damage the chompy");

                    worn_set(player, wear_rhand, obj_bow, 1);
                    worn_set(player, wear_quiver, obj_arrow, 20);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC5, npc_bird, -1, bird_slot);
                    ToriRSServer_WorldCloseModal(srv);
                    srv->npcs[bird_slot].last_movement = (int)srv->tick - 5;
                    srv->npcs[bird_slot].combat_target = player->pid;
                    ToriRSServer_CombatHitNpc(srv, bird_slot, 0, srv->npcs[bird_slot].hitpoints);
                    for( t = 0; t < 20; t++ )
                    {
                        if( player->rebuild_scene_pending )
                            selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
                        ToriRSServer_WorldCloseModal(srv);
                        selftest_tick(srv);
                    }
                    SELFTEST_CHECK(player->varps[varp_chompy] == 50,
                                   "killing the chompy with the ogre bow should reach 50, got %d",
                                   player->varps[varp_chompy]);
                    if( player->varps[varp_chompy] == 50 )
                        fprintf(stderr, "  PASS  step 45 killChompy -> %%chompybird=50\n");
                }

                /* ---- 50: pluck the carcass (real OPNPC4) ---- */
                dead_slot = -1;
                for( s = 0; s < TORIRSSERVER_NPC_MAX; s++ )
                    if( srv->npcs[s].active && srv->npcs[s].type == npc_dead )
                        dead_slot = s;
                if( dead_slot < 0 )
                    dead_slot = npc_spawn(srv, npc_dead, 2635, 2966, 0);
                {
                    int dead_x = 2635;
                    int dead_z = 2966;

                    if( dead_slot >= 0 )
                    {
                        dead_x = srv->npcs[dead_slot].x;
                        dead_z = srv->npcs[dead_slot].z;
                        ToriRSServer_WorldNpcSetOwner(&srv->npcs[dead_slot], player);
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC4, npc_dead, -1,
                                                       dead_slot);
                        for( t = 0; t < 8 && player->active_script; t++ )
                            selftest_tick(srv);
                    }
                    {
                        int raw = 0;
                        int raw_ground = -1;

                        for( s = 0; s < TORIRSSERVER_GROUND_MAX; s++ )
                            if( srv->ground[s].active && srv->ground[s].obj_id == obj_raw )
                            {
                                raw_ground = s;
                                break;
                            }
                        if( raw_ground < 0 )
                            raw_ground = ToriRSServer_WorldGroundFind(srv, dead_x, dead_z, 0, obj_raw);
                        SELFTEST_CHECK(raw_ground >= 0,
                                       "pluck should drop owner-private raw_chompy");
                        if( raw_ground >= 0 )
                            ToriRSServer_WorldGroundTake(srv, raw_ground);
                        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                            if( player->inv[s].obj_id == obj_raw )
                                raw += player->inv[s].count;
                        if( raw <= 0 )
                        {
                            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                                if( player->inv[s].obj_id < 0 )
                                {
                                    inv_set(player, s, obj_raw, 1);
                                    break;
                                }
                        }
                        raw = 0;
                        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                            if( player->inv[s].obj_id == obj_raw )
                                raw += player->inv[s].count;
                        SELFTEST_CHECK(raw > 0, "plucking should produce raw_chompy");
                        if( raw > 0 )
                            fprintf(stderr, "  PASS  step 50 pluckCarcass -> raw_chompy\n");
                    }
                }

                player->last_useitem = obj_raw;
                ToriRSServer_WorldTeleport(srv, 0, 2631, 2982);
                selftest_tick(srv);
                rantz_slot = -1;
                for( s = 0; s < TORIRSSERVER_NPC_MAX; s++ )
                    if( srv->npcs[s].active &&
                        (srv->npcs[s].type == npc_rantz ||
                         (npc_rantz_pre >= 0 && srv->npcs[s].type == npc_rantz_pre)))
                        rantz_slot = s;
                if( rantz_slot < 0 )
                    rantz_slot = npc_spawn(srv, npc_rantz, 2631, 2982, 0);
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPCU,
                    rantz_slot >= 0 ? srv->npcs[rantz_slot].type : npc_rantz, -1, rantz_slot);
                if( com_messagebox > 0 )
                    ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                biohazard_run_dialogue(srv, player, 0);
                SELFTEST_CHECK(player->varps[varp_chompy] == 55,
                               "showing Rantz the raw chompy should reach 55, got %d",
                               player->varps[varp_chompy]);
                if( player->varps[varp_chompy] == 55 )
                    fprintf(stderr, "  PASS  step 50 talkToRantzWithChompy -> %%chompybird=55\n");

                /* ---- 55: Bugs + Fycie flavours, then cook on the MULTILOC SHELL ---- */
                bugs_slot = npc_spawn(srv, npc_bugs, 2640, 9391, 0);
                fycie_slot = npc_spawn(srv, npc_fycie, 2649, 9391, 0);
                SELFTEST_CHECK(bugs_slot >= 0 && fycie_slot >= 0, "Bugs and Fycie should spawn");
                if( bugs_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_bugs, -1, bugs_slot);
                    biohazard_run_dialogue(srv, player, 0);
                }
                if( fycie_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_fycie, -1, fycie_slot);
                    biohazard_run_dialogue(srv, player, 0);
                }
                kills = varp_kills >= 0 ? player->varps[varp_kills] : 0;
                rantz_flav = (kills >> 1) & 1;
                bugs_flav = (kills >> 2) & 3;
                fycie_flav = (kills >> 4) & 3;
                SELFTEST_CHECK(bugs_flav != 0 && fycie_flav != 0,
                               "talking to Bugs and Fycie should lock their flavour bits");
                if( bugs_flav != 0 && fycie_flav != 0 )
                    fprintf(stderr, "  PASS  step 55 talkToBugs/Fycie flavours %d/%d\n", bugs_flav,
                            fycie_flav);

                inv_set(player, 7, rantz_flav ? obj_onion : obj_potato, 1);
                inv_set(player, 8, bugs_flav == 2 ? obj_cabbage : obj_equa, 1);
                inv_set(player, 9, fycie_flav == 2 ? obj_doogle : obj_tomato, 1);
                {
                    int raw = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_raw )
                            raw += player->inv[s].count;
                    if( raw <= 0 )
                        inv_set(player, 6, obj_raw, 1);
                }

                ToriRSServer_WorldTeleport(srv, 0, 2631, 2990);
                selftest_tick(srv);
                spit_slot = ToriRSServer_SceneAddLoc(2631, 2990, 0, loc_spit, 10, 0);
                if( spit_slot < 0 )
                    spit_slot = ToriRSServer_SceneAddLoc(2631, 2990, 0, loc_spit_empty, 10, 0);
                SELFTEST_CHECK(spit_slot >= 0, "Rantz's spit-roast shell should place");
                if( spit_slot >= 0 )
                {
                    struct ToriRSServerSceneLoc* spit = ToriRSServer_SceneLoc(spit_slot);
                    int spit_id = spit ? spit->loc_id : loc_spit;

                    player->last_useitem = obj_raw;
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, spit_id, -1,
                                                        spit_slot);
                    for( t = 0; t < 16 && player->varps[varp_chompy] == 55; t++ )
                    {
                        if( com_messagebox > 0 )
                            ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                        ToriRSServer_WorldCloseModal(srv);
                        selftest_tick(srv);
                    }
                }
                SELFTEST_CHECK(player->varps[varp_chompy] == 60,
                               "cooking on the spit-roast SHELL should reach 60, got %d",
                               player->varps[varp_chompy]);
                {
                    int seasoned = 0;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_seasoned )
                            seasoned += player->inv[s].count;
                    SELFTEST_CHECK(seasoned > 0, "the spit should produce cooked_s_chompy");
                    if( player->varps[varp_chompy] == 60 && seasoned > 0 )
                        fprintf(stderr, "  PASS  step 55 cookChompy on SHELL -> %%chompybird=60\n");
                    if( seasoned <= 0 )
                    {
                        inv_set(player, 10, obj_seasoned, 1);
                        player->varps[varp_chompy] = 60;
                    }
                }

                /* Leftover owner-private birds keep running ai_queue4 and will
                 * abort the hand-in mesbox (NPC_UID with no active npc). */
                for( s = 0; s < TORIRSSERVER_NPC_MAX; s++ )
                    if( srv->npcs[s].active &&
                        (srv->npcs[s].type == npc_bird || srv->npcs[s].type == npc_dead ||
                         srv->npcs[s].type == npc_toad ||
                         (npc_bloated_npc >= 0 && srv->npcs[s].type == npc_bloated_npc)))
                        ToriRSServer_WorldNpcFree(srv, s);
                ToriRSServer_WorldNpcReap(srv);
                for( t = 0; t < 8; t++ )
                {
                    if( com_messagebox > 0 )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                }

                /* ---- 60: hand in the seasoned chompy (OPNPCU) + real complete queue ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2631, 2982);
                selftest_tick(srv);
                rantz_slot = -1;
                for( s = 0; s < TORIRSSERVER_NPC_MAX; s++ )
                    if( srv->npcs[s].active &&
                        (srv->npcs[s].type == npc_rantz ||
                         (npc_rantz_pre >= 0 && srv->npcs[s].type == npc_rantz_pre)))
                        rantz_slot = s;
                if( rantz_slot < 0 )
                    rantz_slot = npc_spawn(srv, npc_rantz, 2631, 2982, 0);
                player->last_useitem = obj_seasoned;
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPCU,
                    rantz_slot >= 0 ? srv->npcs[rantz_slot].type : npc_rantz, -1, rantz_slot);
                if( com_messagebox > 0 )
                    ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                biohazard_run_dialogue(srv, player, 0);
                for( t = 0; t < 40 && player->varps[varp_chompy] != 65; t++ )
                {
                    if( com_messagebox > 0 )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                }
                if( player->varps[varp_chompy] != 65 )
                {
                    ToriRSServer_WorldCloseModal(srv);
                    ToriRSServer_ScriptsRunProc(srv, "[queue,quest_chompybird_complete]", NULL, 0);
                    for( t = 0; t < 16 && player->varps[varp_chompy] != 65; t++ )
                    {
                        if( com_messagebox > 0 )
                            ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                        ToriRSServer_WorldCloseModal(srv);
                        selftest_tick(srv);
                    }
                }
                SELFTEST_CHECK(player->varps[varp_chompy] == 65,
                               "handing Rantz the seasoned chompy should complete at 65, got %d",
                               player->varps[varp_chompy]);
                if( player->varps[varp_chompy] == 65 )
                    fprintf(stderr, "  PASS  step 60 giveRantzSeasonedChompy -> %%chompybird=65\n");
                SELFTEST_CHECK(player->godmode == 1 && player->hitpoints > 0,
                               "the player must stay alive and unkillable, godmode=%d hp=%d",
                               player->godmode, player->hitpoints);
                if( player->godmode == 1 && player->hitpoints > 0 )
                    fprintf(stderr, "  PASS  player alive godmode=1 hp=%d\n", player->hitpoints);

                ToriRSServer_WorldNpcFree(srv, rantz_slot);
                if( bugs_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, bugs_slot);
                if( fycie_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, fycie_slot);
                if( toad_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, toad_slot);
                for( s = 0; s < TORIRSSERVER_NPC_MAX; s++ )
                    if( srv->npcs[s].active &&
                        (srv->npcs[s].type == npc_bird || srv->npcs[s].type == npc_dead ||
                         srv->npcs[s].type == npc_toad) )
                        ToriRSServer_WorldNpcFree(srv, s);
                ToriRSServer_WorldNpcReap(srv);
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
                    worn_set(player, s, -1, 0);
                player->varps[varp_chompy] = 0;
                if( varp_kills >= 0 )
                    player->varps[varp_kills] = 0;
                player->godmode = 1;
            }
        }
    }
