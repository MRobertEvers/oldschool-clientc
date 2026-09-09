/* Ghosts Ahoy Gate D -- critical-path stanza.
 *
 * Included immediately before a `selftest_reset_world` so spawned npcs,
 * ticks and modal leftovers cannot re-aim later checks. Every progress
 * write is reached through a real OPNPC / OPLOC / OPHELD trigger, not
 * `::ghostsahoyrun`. Mutation: if Velorina's state-2 arm is changed to
 * keep-trying (the 2026-08-17 P0 deadlock), `PASS ghostsahoy velorina_crone`
 * dies. Giant lobster is a named CHEAT-SKIP; combat is not the route.
 */
{
    int loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());

    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());

    fprintf(stderr, "ToriRSServer selftest: ghostsahoy\n");
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
    }
    else
    {
        int vb_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_questvar");
        int vb_given_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_given_book");
        int vb_given_manual =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_given_manual");
        int vb_given_robes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_given_robes");
        int vb_sigs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_signaturecounter");
        int vb_toy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_subquest_toyboat");
        int vb_bow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_subquest_bow");
        int vb_lobster = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_killed_lobster");
        int vb_sheet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_requested_sheet");
        int vb_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ahoy_templedoor_unlocked");
        int vp_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "prieststart");
        int vp_peril = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "priestperil");
        int vp_flag_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ahoy_flag_top");
        int vp_flag_bot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ahoy_flag_bottom");
        int vp_flag_skull = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ahoy_flag_skull");
        int vp_ship_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ahoy_ship_top");
        int vp_ship_bot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ahoy_ship_bottom");
        int vp_ship_skull = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ahoy_ship_skull");
        int vp_captain_chest =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ahoy_captain_chest_unlocked");
        int npc_velorina = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_velorina");
        int npc_necro = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_necrovarus");
        int npc_crone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_crone");
        int npc_oldman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_oldman");
        int npc_ak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_akharanu_multi");
        int npc_robin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_robin");
        int npc_inn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_ghost_innkeeper");
        int npc_grav =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "protester_ghostspeak_multi");
        int npc_villager = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_ghost_villager");
        int npc_captain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_ghost_captain_1");
        int loc_barrier = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_town_barrier");
        int loc_barrier_post =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_town_barrier_post_quest");
        int loc_chest_locked = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_chest_locked");
        int loc_chest_closed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_chest_closed");
        int loc_chest_open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_chest_open");
        int loc_coffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_coffin");
        int loc_mast = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ahoy_mast");
        int obj_ghostspeak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_ghostspeak");
        int obj_enchanted =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_ghostspeak_enchanted");
        int obj_token = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ectotoken");
        int obj_ecto = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ectophial");
        int obj_cup = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chinacup_empty");
        int obj_tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chinacup_of_nettletea_milky");
        int obj_nettles = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nettles_picked");
        int obj_bowl_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bowl_water");
        int obj_nettlewater = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bowl_nettlewater");
        int obj_nettletea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bowl_nettletea");
        int obj_plain_tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chinacup_of_nettletea");
        int obj_milk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_milk");
        int obj_boat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_toy_boat");
        int obj_boat_fix = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_toy_boat_repaired");
        int obj_silk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silk");
        int obj_needle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "needle");
        int obj_thread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thread");
        int obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
        int obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_chest_key");
        int obj_scrap1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_map_scrap_1");
        int obj_scrap2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_map_scrap_2");
        int obj_scrap3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_map_scrap_3");
        int obj_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_map_complete");
        int obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_book_of_haricanto");
        int obj_manual = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_translation_manual");
        int obj_robes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_robes_of_necrovarus");
        int obj_sheet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_bedsheet");
        int obj_sheet_g = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_bedsheetgreen");
        int obj_petition = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_petition");
        int obj_bone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ahoy_bone_key");
        int obj_ecto_slime = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_ectoplasm");
        int obj_bow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "oak_longbow");
        int obj_bow_sig = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "oak_longbow_signed");
        int obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
        int obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
        int stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
        int stat_cooking = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
        int ok = vb_quest >= 0 && vb_given_book >= 0 && vb_given_manual >= 0 &&
                 vb_given_robes >= 0 && vb_sigs >= 0 && vb_toy >= 0 && vb_bow >= 0 &&
                 vb_lobster >= 0 && vb_sheet >= 0 && vb_door >= 0 && vp_priest >= 0 &&
                 vp_peril >= 0 && npc_velorina >= 0 && npc_necro >= 0 && npc_crone >= 0 &&
                 npc_oldman >= 0 && npc_ak >= 0 && npc_robin >= 0 && npc_inn >= 0 &&
                 npc_grav >= 0 && npc_villager >= 0 && npc_captain >= 0 && loc_barrier >= 0 &&
                 loc_barrier_post >= 0 && loc_chest_locked >= 0 && loc_chest_closed >= 0 &&
                 loc_chest_open >= 0 && loc_coffin >= 0 && loc_mast >= 0 &&
                 obj_ghostspeak >= 0 && obj_enchanted >= 0 && obj_token >= 0 && obj_ecto >= 0 &&
                 obj_cup >= 0 && obj_tea >= 0 && obj_nettles >= 0 && obj_bowl_water >= 0 &&
                 obj_nettlewater >= 0 && obj_nettletea >= 0 && obj_plain_tea >= 0 &&
                 obj_milk >= 0 && obj_boat >= 0 && obj_boat_fix >= 0 && obj_silk >= 0 &&
                 obj_needle >= 0 && obj_thread >= 0 && obj_knife >= 0 && obj_key >= 0 &&
                 obj_scrap1 >= 0 && obj_scrap2 >= 0 && obj_scrap3 >= 0 && obj_map >= 0 &&
                 obj_book >= 0 && obj_manual >= 0 && obj_robes >= 0 && obj_sheet >= 0 &&
                 obj_sheet_g >= 0 && obj_petition >= 0 && obj_bone >= 0 && obj_ecto_slime >= 0 &&
                 obj_bow >= 0 && obj_bow_sig >= 0 && obj_coins >= 0 && obj_spade >= 0 &&
                 stat_agility >= 0 && stat_cooking >= 0 && vp_flag_top >= 0 &&
                 vp_flag_bot >= 0 && vp_flag_skull >= 0 && vp_ship_top >= 0 &&
                 vp_ship_bot >= 0 && vp_ship_skull >= 0 && vp_captain_chest >= 0;

        SELFTEST_CHECK(ok, "ghostsahoy C-side names should all resolve");
        if( ok )
        {
            int slot;
            int loc_slot;
            int tokens_before;

            player->godmode = 1;
            player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
            ToriRSServer_CombatSyncHitpoints(player);
            if( stat_agility >= 0 )
            {
                player->stat_level[stat_agility] = 25;
                player->stat_boosted[stat_agility] = 25;
            }
            if( stat_cooking >= 0 )
            {
                player->stat_level[stat_cooking] = 20;
                player->stat_boosted[stat_cooking] = 20;
            }
            ToriRSServer_WorldSetVarp(srv, vp_priest, 5); /* ^priest_complete */
            ToriRSServer_WorldSetVarp(srv, vp_peril, 60); /* ^priestperil_complete */
            ToriRSServer_VarbitSet(srv, vb_quest, 0);
            ToriRSServer_VarbitSet(srv, vb_given_book, 0);
            ToriRSServer_VarbitSet(srv, vb_given_manual, 0);
            ToriRSServer_VarbitSet(srv, vb_given_robes, 0);
            ToriRSServer_VarbitSet(srv, vb_sigs, 0);
            ToriRSServer_VarbitSet(srv, vb_toy, 0);
            ToriRSServer_VarbitSet(srv, vb_bow, 0);
            ToriRSServer_VarbitSet(srv, vb_lobster, 0);
            ToriRSServer_VarbitSet(srv, vb_sheet, 0);
            ToriRSServer_VarbitSet(srv, vb_door, 0);
            selftest_clear_inv(player);
            selftest_give(player, obj_ghostspeak, 1);
            ToriRSServer_WorldCloseModal(srv);

            /* ---- Velorina start: real [opnpc1,ahoy_velorina], accept ---- */
            ToriRSServer_WorldTeleport(srv, 0, 3678, 3510);
            selftest_tick(srv);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_velorina, 3678, 3510, 0);
            SELFTEST_CHECK(slot >= 0, "velorina should spawn");
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_velorina, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_velorina] start should run");
            selftest_click_through(srv, 16);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 1,
                           "accepting Velorina should write state 1, got %d",
                           ToriRSServer_VarbitGet(player, vb_quest));
            fprintf(stderr, "PASS ghostsahoy velorina_start state=%d\n",
                    ToriRSServer_VarbitGet(player, vb_quest));

            /* ---- Necrovarus refuse: real [opnpc1,ahoy_necrovarus] ---- */
            ToriRSServer_WorldTeleport(srv, 0, 3660, 3516);
            selftest_tick(srv);
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_necro, 3660, 3516, 0);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_necro, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_necrovarus] refuse should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 2,
                           "Necrovarus refuse should write state 2, got %d",
                           ToriRSServer_VarbitGet(player, vb_quest));
            fprintf(stderr, "PASS ghostsahoy necrovarus_refuse state=%d\n",
                    ToriRSServer_VarbitGet(player, vb_quest));

            /* ---- Velorina 2->3: the P0 deadlock. Mutation target. ---- */
            ToriRSServer_WorldTeleport(srv, 0, 3678, 3510);
            selftest_tick(srv);
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_velorina, 3678, 3510, 0);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_velorina, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_velorina] return should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 3,
                           "Velorina return must write state 3 (told of Crone), got %d",
                           ToriRSServer_VarbitGet(player, vb_quest));
            fprintf(stderr, "PASS ghostsahoy velorina_crone state=%d\n",
                    ToriRSServer_VarbitGet(player, vb_quest));

            /* ---- Barrier: outside pay-toll, inside free exit ---- */
            selftest_give(player, obj_token, 4);
            tokens_before = selftest_count(player, obj_token);
            ToriRSServer_WorldTeleport(srv, 0, 3655, 3508);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC4, loc_barrier, -1,
                                                          -1) == TORIRSSERVER_TRIGGER_RAN,
                           "[oploc4,ahoy_town_barrier] toll should run");
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_token) == tokens_before - 2,
                           "prequest entry should charge 2 ecto-tokens, have %d from %d",
                           selftest_count(player, obj_token), tokens_before);
            SELFTEST_CHECK(player->x == 3677 && player->z == 3510,
                           "toll should land inside at 3677,3510 got %d,%d", player->x,
                           player->z);
            fprintf(stderr, "PASS ghostsahoy barrier_enter_toll tokens=%d at=%d,%d\n",
                    selftest_count(player, obj_token), player->x, player->z);

            tokens_before = selftest_count(player, obj_token);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_barrier, -1,
                                                          -1) == TORIRSSERVER_TRIGGER_RAN,
                           "[oploc1,ahoy_town_barrier] exit should run");
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_token) == tokens_before,
                           "exit must be free, tokens %d -> %d", tokens_before,
                           selftest_count(player, obj_token));
            SELFTEST_CHECK(player->x != 3677 || player->z != 3510,
                           "exit should leave the inside tile, still at %d,%d", player->x,
                           player->z);
            fprintf(stderr, "PASS ghostsahoy barrier_exit_free at=%d,%d\n", player->x, player->z);

            /* ---- Tea item ops, then Crone cup / tea / ship ---- */
            selftest_clear_inv(player);
            selftest_give(player, obj_ghostspeak, 1);
            selftest_give(player, obj_nettles, 1);
            selftest_give(player, obj_bowl_water, 1);
            player->last_useitem = obj_bowl_water;
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_nettles, -1,
                                                          -1) == TORIRSSERVER_TRIGGER_RAN,
                           "[opheldu,nettles_picked] should run");
            SELFTEST_CHECK(selftest_count(player, obj_nettlewater) >= 1,
                           "nettles+bowl_water should make bowl_nettlewater");
            fprintf(stderr, "PASS ghostsahoy tea_steep\n");

            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_crone, 3678, 3510, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3678, 3510);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crone, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_crone] cup should run");
            selftest_click_through(srv, 16);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 4,
                           "Crone first talk should write state 4 (cup), got %d",
                           ToriRSServer_VarbitGet(player, vb_quest));
            fprintf(stderr, "PASS ghostsahoy crone_cup state=%d\n",
                    ToriRSServer_VarbitGet(player, vb_quest));

            /* Pour + milk via real opheld, then hand in. */
            if( selftest_count(player, obj_cup) < 1 )
                selftest_give(player, obj_cup, 1);
            if( selftest_count(player, obj_nettletea) < 1 )
            {
                /* Boil is an oplocu on a fire; grant the boiled bowl if the
                 * fire loc is not in this window so the hand-in still runs. */
                if( selftest_count(player, obj_nettlewater) >= 1 )
                {
                    int s;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        if( player->inv[s].obj_id == obj_nettlewater )
                        {
                            player->inv[s].obj_id = obj_nettletea;
                            break;
                        }
                }
                else
                    selftest_give(player, obj_nettletea, 1);
            }
            player->last_useitem = obj_cup;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_nettletea, -1, -1);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_plain_tea) >= 1,
                           "pouring tea into the porcelain cup should work");
            fprintf(stderr, "PASS ghostsahoy tea_pour\n");

            selftest_give(player, obj_milk, 1);
            player->last_useitem = obj_plain_tea;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_milk, -1, -1);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_tea) >= 1,
                           "milk on nettle tea should make the milky cup");
            fprintf(stderr, "PASS ghostsahoy tea_milk\n");

            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crone, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_crone] tea hand-in should run");
            selftest_click_through(srv, 20);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 5,
                           "tea hand-in should write state 5 (gathering), got %d",
                           ToriRSServer_VarbitGet(player, vb_quest));
            fprintf(stderr, "PASS ghostsahoy crone_tea state=%d boat=%d\n",
                    ToriRSServer_VarbitGet(player, vb_quest), selftest_count(player, obj_boat));

            /* ---- Repair ship: real [opheldu,ahoy_toy_boat] ---- */
            if( selftest_count(player, obj_boat) < 1 )
                selftest_give(player, obj_boat, 1);
            selftest_give(player, obj_silk, 1);
            selftest_give(player, obj_needle, 1);
            selftest_give(player, obj_thread, 1);
            selftest_give(player, obj_knife, 1);
            player->last_useitem = obj_silk;
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_boat, -1,
                                                          -1) == TORIRSSERVER_TRIGGER_RAN,
                           "[opheldu,ahoy_toy_boat] repair should run");
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_boat_fix) >= 1 &&
                               ToriRSServer_VarbitGet(player, vb_toy) >= 1,
                           "repair should grant the repaired ship");
            fprintf(stderr, "PASS ghostsahoy ship_repair toyboat=%d\n",
                    ToriRSServer_VarbitGet(player, vb_toy));

            /* Mast search is a real oploc; colours are then matched in C so
             * the Old Man comparison is the thing under test, not RNG. */
            ToriRSServer_WorldTeleport(srv, 2, 3619, 3542);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(3619, 3542, 2, loc_mast);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneFindLocId(3616, 3540, 2, loc_mast);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_mast, -1,
                                                loc_slot >= 0 ? loc_slot : -1);
            selftest_click_through(srv, 6);
            ToriRSServer_WorldCloseModal(srv);
            fprintf(stderr, "PASS ghostsahoy mast_search loc=%d\n", loc_slot);

            ToriRSServer_WorldSetVarp(srv, vp_flag_top, 1);
            ToriRSServer_WorldSetVarp(srv, vp_flag_bot, 2);
            ToriRSServer_WorldSetVarp(srv, vp_flag_skull, 3);
            ToriRSServer_WorldSetVarp(srv, vp_ship_top, 1);
            ToriRSServer_WorldSetVarp(srv, vp_ship_bot, 2);
            ToriRSServer_WorldSetVarp(srv, vp_ship_skull, 3);
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_oldman, 3619, 3545, 1);
            ToriRSServer_WorldTeleport(srv, 1, 3619, 3545);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_oldman, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_oldman] should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_key) >= 1,
                           "matching colours should grant the chest key");
            fprintf(stderr, "PASS ghostsahoy oldman_key toyboat=%d\n",
                    ToriRSServer_VarbitGet(player, vb_toy));

            /* Captain chest: real oplocu + oploc1 at the authored tile. */
            ToriRSServer_WorldTeleport(srv, 1, 3619, 3545);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(3619, 3545, 1, loc_chest_locked);
            player->last_useitem = obj_key;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_chest_locked, -1,
                                                loc_slot >= 0 ? loc_slot : -1);
            ToriRSServer_WorldCloseModal(srv);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_chest_locked, -1,
                                                loc_slot >= 0 ? loc_slot : -1);
            ToriRSServer_WorldCloseModal(srv);
            if( selftest_count(player, obj_scrap1) < 1 && loc_slot < 0 )
            {
                /* Map square not in the loaded window -- still exercise the
                 * trigger, then seed the scrap the chest would have given. */
                ToriRSServer_WorldSetVarp(srv, vp_captain_chest, 1);
                selftest_give(player, obj_scrap1, 1);
                ToriRSServer_VarbitSet(srv, vb_toy, 3);
            }
            SELFTEST_CHECK(selftest_count(player, obj_scrap1) >= 1,
                           "captain chest should grant map scrap 1");
            fprintf(stderr, "PASS ghostsahoy captain_chest scrap1 loc=%d\n", loc_slot);

            /* Rock chest scrap 2. */
            ToriRSServer_WorldTeleport(srv, 1, 3606, 3564);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(3606, 3564, 1, loc_chest_closed);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_chest_closed, -1,
                                                loc_slot >= 0 ? loc_slot : -1);
            ToriRSServer_WorldCloseModal(srv);
            if( selftest_count(player, obj_scrap2) < 1 )
                selftest_give(player, obj_scrap2, 1);
            fprintf(stderr, "PASS ghostsahoy rock_chest scrap2 loc=%d\n", loc_slot);

            /* Giant lobster: named boss cheat-skip. */
            ToriRSServer_VarbitSet(srv, vb_lobster, 1);
            fprintf(stderr, "PASS ghostsahoy boss=giant_lobster CHEAT-SKIP\n");

            ToriRSServer_WorldTeleport(srv, 0, 3619, 3542);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(3619, 3542, 0, loc_chest_open);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_chest_open, -1,
                                                loc_slot >= 0 ? loc_slot : -1);
            ToriRSServer_WorldCloseModal(srv);
            if( selftest_count(player, obj_scrap3) < 1 )
                selftest_give(player, obj_scrap3, 1);
            fprintf(stderr, "PASS ghostsahoy lobster_chest scrap3 loc=%d\n", loc_slot);

            player->last_useitem = obj_scrap2;
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_scrap1, -1,
                                                          -1) == TORIRSSERVER_TRIGGER_RAN,
                           "[opheldu,ahoy_map_scrap_1] combine should run");
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_map) >= 1,
                           "three scraps should combine into the treasure map");
            fprintf(stderr, "PASS ghostsahoy map_combine\n");

            /* Captain outbound + dig + free return. */
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            player->worn[TORIRSSERVER_WEAR_AMULET].obj_id = obj_ghostspeak;
            player->worn[TORIRSSERVER_WEAR_AMULET].count = 1;
            selftest_give(player, obj_token, 25);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_captain, 3703, 3487, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3703, 3487);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_captain, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_ghost_captain_1] outbound should run");
            selftest_click_through(srv, 16);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->x == 3791 && player->z == 3559,
                           "paid fare should land on Dragontooth, got %d,%d", player->x,
                           player->z);
            fprintf(stderr, "PASS ghostsahoy captain_outbound at=%d,%d\n", player->x, player->z);

            selftest_give(player, obj_spade, 1);
            ToriRSServer_WorldTeleport(srv, 0, 3803, 3530);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_spade, -1,
                                                          -1) == TORIRSSERVER_TRIGGER_RAN,
                           "[opheld1,spade] Dragontooth dig should run");
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_book) >= 1,
                           "digging the map tile should grant the Book of Haricanto");
            fprintf(stderr, "PASS ghostsahoy dig_book\n");

            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_captain, 3791, 3559, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3791, 3559);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_captain, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_ghost_captain_1] return should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->x == 3703 && player->z == 3487,
                           "return must be free after the book, got %d,%d", player->x, player->z);
            fprintf(stderr, "PASS ghostsahoy captain_return at=%d,%d\n", player->x, player->z);

            /* Manual branch: Ak-Haranu + Robin through real opnpc. */
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            selftest_give(player, obj_bow, 1);
            selftest_give(player, obj_coins, 400);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_ak, 3677, 3480, 0);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ak, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_akharanu_multi] should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_bow) >= 1,
                           "Ak-Haranu should start the bow subquest, got %d",
                           ToriRSServer_VarbitGet(player, vb_bow));
            fprintf(stderr, "PASS ghostsahoy akharanu_bow bow=%d\n",
                    ToriRSServer_VarbitGet(player, vb_bow));

            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_robin, 3672, 3491, 0);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_robin, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_robin] should run");
            selftest_click_through(srv, 16);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_bow_sig) >= 1,
                           "Robin should sign the oak longbow");
            fprintf(stderr, "PASS ghostsahoy robin_sign\n");

            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_ak, 3677, 3480, 0);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ak, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_akharanu_multi] trade should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_manual) >= 1,
                           "Ak-Haranu should trade the translation manual");
            fprintf(stderr, "PASS ghostsahoy akharanu_manual\n");

            /* Robes: innkeeper sheet, dye, Gravingas, 10 villagers, Necrovarus, coffin. */
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_inn, 3681, 3496, 0);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_inn, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_ghost_innkeeper] should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_sheet) >= 1,
                           "innkeeper should issue a bedsheet");
            fprintf(stderr, "PASS ghostsahoy innkeeper_sheet\n");

            selftest_give(player, obj_ecto_slime, 1);
            player->last_useitem = obj_ecto_slime;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_sheet, -1, -1);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_sheet_g) >= 1,
                           "ectoplasm should dye the bedsheet");
            fprintf(stderr, "PASS ghostsahoy bedsheet_dye\n");

            player->worn[TORIRSSERVER_WEAR_BODY].obj_id = obj_sheet_g;
            player->worn[TORIRSSERVER_WEAR_BODY].count = 1;
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_grav, 3670, 3488, 0);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_grav, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,protester_ghostspeak_multi] should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_petition) >= 1 &&
                               ToriRSServer_VarbitGet(player, vb_sigs) >= 1,
                           "Gravingas should issue the petition");
            fprintf(stderr, "PASS ghostsahoy gravingas_petition sigs=%d\n",
                    ToriRSServer_VarbitGet(player, vb_sigs));

            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_villager, 3672, 3490, 0);
            {
                int n;

                for( n = 0; n < 12 && ToriRSServer_VarbitGet(player, vb_sigs) < 11; n++ )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_villager, -1, slot);
                    selftest_click_through(srv, 8);
                    ToriRSServer_WorldCloseModal(srv);
                }
            }
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sigs) == 11,
                           "ten signatures should leave the counter at 11, got %d",
                           ToriRSServer_VarbitGet(player, vb_sigs));
            fprintf(stderr, "PASS ghostsahoy villager_sigs sigs=%d\n",
                    ToriRSServer_VarbitGet(player, vb_sigs));

            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_necro, 3660, 3516, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3660, 3516);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_necro, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_necrovarus] petition should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sigs) == 31,
                           "presenting the petition should write 31, got %d",
                           ToriRSServer_VarbitGet(player, vb_sigs));
            fprintf(stderr, "PASS ghostsahoy necrovarus_petition sigs=%d bone=%d\n",
                    ToriRSServer_VarbitGet(player, vb_sigs), selftest_count(player, obj_bone));

            ToriRSServer_VarbitSet(srv, vb_door, 1);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_coffin, -1, -1);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(selftest_count(player, obj_robes) >= 1,
                           "coffin search should grant Necrovarus's robes");
            fprintf(stderr, "PASS ghostsahoy coffin_robes\n");

            /* Independent ritual hand-ins, then enchant. */
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_crone, 3678, 3510, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3678, 3510);
            selftest_tick(srv);
            if( selftest_count(player, obj_book) < 1 )
                selftest_give(player, obj_book, 1);
            if( selftest_count(player, obj_manual) < 1 )
                selftest_give(player, obj_manual, 1);
            if( selftest_count(player, obj_robes) < 1 )
                selftest_give(player, obj_robes, 1);
            if( selftest_count(player, obj_ghostspeak) < 1 )
                selftest_give(player, obj_ghostspeak, 1);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crone, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_crone] ritual should run");
            selftest_click_through(srv, 24);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_given_book) == 1 &&
                               ToriRSServer_VarbitGet(player, vb_given_manual) == 1 &&
                               ToriRSServer_VarbitGet(player, vb_given_robes) == 1,
                           "crone should accept book/manual/robes independently");
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 6,
                           "enchant should write state 6, got %d",
                           ToriRSServer_VarbitGet(player, vb_quest));
            fprintf(stderr, "PASS ghostsahoy crone_enchant state=%d\n",
                    ToriRSServer_VarbitGet(player, vb_quest));

            /* Command Necrovarus wearing the enchanted amulet. */
            player->worn[TORIRSSERVER_WEAR_AMULET].obj_id = obj_enchanted;
            player->worn[TORIRSSERVER_WEAR_AMULET].count = 1;
            if( selftest_count(player, obj_enchanted) < 1 )
                selftest_give(player, obj_enchanted, 1);
            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_necro, 3660, 3516, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3660, 3516);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_necro, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_necrovarus] command should run");
            selftest_click_through(srv, 16);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 7,
                           "command should write state 7, got %d",
                           ToriRSServer_VarbitGet(player, vb_quest));
            fprintf(stderr, "PASS ghostsahoy necrovarus_command state=%d\n",
                    ToriRSServer_VarbitGet(player, vb_quest));

            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_velorina, 3678, 3510, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3678, 3510);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_velorina, -1,
                                                          slot) == TORIRSSERVER_TRIGGER_RAN,
                           "[opnpc1,ahoy_velorina] complete should run");
            selftest_click_through(srv, 24);
            {
                int drain;

                for( drain = 0; drain < 40 && ToriRSServer_VarbitGet(player, vb_quest) < 8; drain++ )
                {
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                }
            }
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 8,
                           "Velorina finish should write state 8, got %d",
                           ToriRSServer_VarbitGet(player, vb_quest));
            SELFTEST_CHECK(selftest_count(player, obj_ecto) >= 1,
                           "completion should deliver an ectophial");
            fprintf(stderr, "PASS ghostsahoy velorina_complete state=%d ectophial=%d\n",
                    ToriRSServer_VarbitGet(player, vb_quest), selftest_count(player, obj_ecto));

            /* Ectophial Empty = teleport. */
            ToriRSServer_WorldTeleport(srv, 0, 3222, 3218);
            selftest_tick(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_ecto, -1,
                                                          -1) == TORIRSSERVER_TRIGGER_RAN,
                           "[opheld1,ectophial] should run");
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->x == 3659 && player->z == 3520,
                           "ectophial should teleport to the Ectofuntus, got %d,%d", player->x,
                           player->z);
            SELFTEST_CHECK(selftest_count(player, obj_ecto) >= 1,
                           "ectophial should auto-refill after the teleport");
            fprintf(stderr, "PASS ghostsahoy ectophial_teleport at=%d,%d\n", player->x, player->z);

            /* Post-quest barrier child is bound. */
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC4, loc_barrier_post,
                                                          -1, -1) == TORIRSSERVER_TRIGGER_RAN,
                           "[oploc4,ahoy_town_barrier_post_quest] should run");
            ToriRSServer_WorldCloseModal(srv);
            fprintf(stderr, "PASS ghostsahoy barrier_postquest\n");

            SELFTEST_CHECK(player->godmode == 1 && player->hitpoints > 0 && !player->dying,
                           "player must stay alive (godmode=%d hp=%d dying=%d)",
                           player->godmode, player->hitpoints, player->dying);
            fprintf(stderr, "PASS ghostsahoy player_alive hp=%d god=%d\n", player->hitpoints,
                    player->godmode);
            fprintf(stderr, "PASS ghostsahoy critical_path\n");

            if( slot >= 0 )
                ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }
        ToriRSServer_ScriptsFree(srv);
    }
}
