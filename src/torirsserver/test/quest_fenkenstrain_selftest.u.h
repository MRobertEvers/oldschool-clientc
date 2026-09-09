{
    /*
     * Creature of Fenkenstrain critical path. Real opnpc/oploc/opheld only.
     * Placed immediately before the crop `selftest_reset_world` so spawned
     * npcs and placed locs cannot leak into later roam/RNG checks.
     *
     * Mutation (field guide S7): change the hire write in fenkenstrain.rs2
     * from `~fenk_set_progress(^fenk_hired)` to `~fenk_set_progress(2)`.
     * The assertion `fenk_quest == 1` after the Doctor interview dies.
     * Restoring the literal 1 (via ^fenk_hired = 1) brings it back.
     */
    fprintf(stderr, "ToriRSServer selftest: Creature of Fenkenstrain critical path\n");
    {
        int owned = 0;
        int loaded = srv->scripts_ok;
        int varp_fenk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fenk_quest");
        int varp_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "prieststart");
        int varp_pip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "priestperil");
        int varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        int vb_sign = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fenk_read_signpost");
        int vb_packed =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "creatureoffenkenstrain");
        int vb_coffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fenk_coffin");
        int vb_cavern = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fenk_unlocked_cavern");
        int vb_repaired =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fenk_conductor_repaired");
        int loc_sign = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_signpost");
        int loc_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_bookcase");
        int loc_coffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_coffin");
        int loc_grave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_grave");
        int loc_grave_poor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_grave_poor");
        int loc_maus_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_mausoleum_door");
        int loc_shed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_shed_door");
        int loc_cupboard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_broomcupboard");
        int loc_cupboard_open =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_broomcupboard_open");
        int loc_canes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_canepile");
        int loc_fire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_fireplace");
        int loc_furnace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "furnace");
        int loc_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_conductor_broken");
        int loc_tower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fenk_tower_door");
        int npc_doc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fenk_fenkenstrain_model");
        int npc_roavar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "werewolfinnkeeper");
        int npc_gardener = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fenk_gardener");
        int npc_exp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fenk_experiment_1");
        int npc_creature = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fenk_creature_model");
        int obj_marble = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_marble_amulet");
        int obj_obsidian = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_obsidian_amulet");
        int obj_star = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_star_amulet");
        int obj_brain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_brain");
        int obj_head = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_head_empty");
        int obj_head_full = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_head_full");
        int obj_torso = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_torso");
        int obj_arms = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_arms");
        int obj_legs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_legs");
        int obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
        int obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
        int obj_ghostspeak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_ghostspeak");
        int obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_mausoleum_key");
        int obj_needle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "needle");
        int obj_thread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thread");
        int obj_shedkey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_shed_key");
        int obj_brush0 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_brush0");
        int obj_brush3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_brush3");
        int obj_cane = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_cane");
        int obj_wire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronzecraftwire");
        int obj_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_lightning_mould");
        int obj_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silver_bar");
        int obj_conductor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_conductor");
        int obj_towerkey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_tower_key");
        int obj_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ring_of_charos");
        int stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
        int stat_thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
        int ok;

        player->godmode = 1;
        if( !loaded )
        {
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
            if( !loaded )
                loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
            owned = loaded != 0;
        }
        ok = loaded && varp_fenk >= 0 && varp_priest >= 0 && varp_pip >= 0 && loc_sign >= 0 &&
             npc_doc >= 0 && obj_ring >= 0 && stat_craft >= 0 && stat_thieve >= 0;
        SELFTEST_CHECK(ok, "fenkenstrain: core pack names should resolve");
        if( ok )
        {
            const int yes = 1;
            const int hire_rows[] = { 1, 4, 4 };
            const int wrong_q1[] = { 1, 1 };
            const int parts_row[] = { 1 };
            const int brain_rows[] = { 5, 1 };
            int slot;
            int npc_slot;
            int i;
            int qp_before;
            int xp_before;
            int craft_before;
            int x0;
            int z0;
            int level0;
            int creatures_before;
            uint8_t payload[16];
            struct RSAreaBuf out;

            selftest_clear_inv(player);
            player->varps[varp_fenk] = 0;
            if( vb_packed >= 0 )
                ToriRSServer_VarbitSet(srv, vb_packed, 0);
            player->varps[varp_priest] = 1; /* partial Restless Ghost */
            player->varps[varp_pip] = 61;   /* ^fenk_pip_gate */
            ToriRSServer_CombatSetLevel(player, stat_craft, 20);
            ToriRSServer_CombatSetLevel(player, stat_thieve, 25);

            /* 1. Sign Yes records the notice only. */
            ToriRSServer_WorldTeleport(srv, 0, 3496, 3489);
            slot = ToriRSServer_SceneAddLoc(3496, 3489, 0, loc_sign, 10, 0);
            SELFTEST_CHECK(slot >= 0, "fenkenstrain: signpost should place");
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_sign,
                                                ToriRSServer_LocCategory(loc_sign), slot);
            selftest_drain_choices(srv, &yes, 1, 16);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(vb_sign < 0 || ToriRSServer_VarbitGet(player, vb_sign) == 1,
                           "fenkenstrain: sign Yes should set fenk_read_signpost");
            SELFTEST_CHECK(player->varps[varp_fenk] == 0,
                           "fenkenstrain: sign must not hire, fenk_quest=%d",
                           player->varps[varp_fenk]);
            fprintf(stderr, "PASS fenkenstrain sign Yes leaves fenk_quest=0\n");

            /* 2. Roavar brain at state 0 (QH step 0). */
            if( npc_roavar >= 0 && obj_brain >= 0 && obj_coins >= 0 )
            {
                selftest_give(player, obj_coins, 50);
                npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_roavar, 3496, 3471, 0);
                ToriRSServer_WorldTeleport(srv, 0, 3496, 3471);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_roavar, -1, npc_slot);
                selftest_drain_choices(srv, brain_rows, 2, 24);
                ToriRSServer_WorldCloseModal(srv);
                player->active_script = NULL;
                SELFTEST_CHECK(selftest_count(player, obj_brain) == 1,
                               "fenkenstrain: Roavar should sell a pickled brain at state 0");
                if( npc_slot >= 0 )
                {
                    ToriRSServer_WorldNpcFree(srv, npc_slot);
                    ToriRSServer_WorldNpcReap(srv);
                }
                fprintf(stderr, "PASS fenkenstrain Roavar brain at fenk_quest=0\n");
            }

            /* 3. Wrong Q1 must not ask Q2 / hire. */
            ToriRSServer_WorldTeleport(srv, 0, 3551, 3548);
            npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_doc, 3551, 3548, 0);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_doc, -1, npc_slot);
            selftest_drain_choices(srv, wrong_q1, 2, 40);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[varp_fenk] == 0,
                           "fenkenstrain: wrong Q1 must leave fenk_quest=0, got %d",
                           player->varps[varp_fenk]);
            fprintf(stderr, "PASS fenkenstrain wrong Q1 does not hire\n");

            /* 4. Interview Yes + Braindead + Grave-digging -> 1. */
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_doc, -1, npc_slot);
            selftest_drain_choices(srv, hire_rows, 3, 64);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[varp_fenk] == 1,
                           "fenkenstrain: Doctor interview should hire at fenk_quest=1, got %d",
                           player->varps[varp_fenk]);
            SELFTEST_CHECK(vb_packed < 0 || ToriRSServer_VarbitGet(player, vb_packed) == 1,
                           "fenkenstrain: packed creatureoffenkenstrain should lockstep to 1");
            fprintf(stderr, "PASS fenkenstrain interview hires fenk_quest=1\n");

            /* 5-6. West/east bookcases then combine. */
            ToriRSServer_WorldTeleport(srv, 1, 3542, 3558);
            slot = ToriRSServer_SceneAddLoc(3542, 3558, 1, loc_book, 10, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_book,
                                                ToriRSServer_LocCategory(loc_book), slot);
            selftest_drain_choices(srv, &yes, 1, 16);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(selftest_count(player, obj_marble) == 1,
                           "fenkenstrain: west bookcase should give marble amulet");
            ToriRSServer_WorldTeleport(srv, 1, 3555, 3558);
            slot = ToriRSServer_SceneAddLoc(3555, 3558, 1, loc_book, 10, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_book,
                                                ToriRSServer_LocCategory(loc_book), slot);
            selftest_drain_choices(srv, &yes, 1, 16);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(selftest_count(player, obj_obsidian) == 1,
                           "fenkenstrain: east bookcase should give obsidian amulet");
            fprintf(stderr, "PASS fenkenstrain bookcases give marble and obsidian\n");

            {
                int a_slot = -1;
                int b_slot = -1;

                for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
                {
                    if( player->inv[i].obj_id == obj_marble )
                        a_slot = i;
                    if( player->inv[i].obj_id == obj_obsidian )
                        b_slot = i;
                }
                if( a_slot >= 0 && b_slot >= 0 )
                {
                    rsab_wrap(&out, payload, sizeof(payload));
                    rsab_p2(&out, obj_marble);
                    rsab_p2(&out, a_slot);
                    rsab_p4(&out, 0);
                    rsab_p2(&out, obj_obsidian);
                    rsab_p2(&out, b_slot);
                    rsab_p4(&out, 0);
                    selftest_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
                    selftest_drain_choices(srv, &yes, 1, 8);
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }
            }
            SELFTEST_CHECK(selftest_count(player, obj_star) == 1,
                           "fenkenstrain: combining amulets should make the star");
            fprintf(stderr, "PASS fenkenstrain OPHELDU combines star amulet\n");

            /* 7. Dig Ed's grave with no spoken-to-gardener flag. */
            if( obj_spade >= 0 )
                selftest_give(player, obj_spade, 1);
            ToriRSServer_WorldTeleport(srv, 0, 3608, 3490);
            slot = ToriRSServer_SceneAddLoc(3608, 3490, 0, loc_grave_poor, 10, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_grave_poor,
                                                ToriRSServer_LocCategory(loc_grave_poor), slot);
            for( i = 0; i < 4; i++ )
                selftest_tick(srv);
            selftest_drain_choices(srv, &yes, 1, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(selftest_count(player, obj_head) == 1,
                           "fenkenstrain: Ed grave should give the empty head");
            fprintf(stderr, "PASS fenkenstrain Ed grave head without gardener flag\n");

            /* 8. Brain into head. */
            {
                int a_slot = -1;
                int b_slot = -1;

                for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
                {
                    if( player->inv[i].obj_id == obj_brain )
                        a_slot = i;
                    if( player->inv[i].obj_id == obj_head )
                        b_slot = i;
                }
                if( a_slot >= 0 && b_slot >= 0 )
                {
                    rsab_wrap(&out, payload, sizeof(payload));
                    rsab_p2(&out, obj_head);
                    rsab_p2(&out, b_slot);
                    rsab_p4(&out, 0);
                    rsab_p2(&out, obj_brain);
                    rsab_p2(&out, a_slot);
                    rsab_p4(&out, 0);
                    selftest_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
                    selftest_drain_choices(srv, &yes, 1, 8);
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }
            }
            SELFTEST_CHECK(selftest_count(player, obj_head_full) == 1,
                           "fenkenstrain: brain+head should make the pickled head");
            fprintf(stderr, "PASS fenkenstrain OPHELDU brain into head\n");

            /* 9. Star on memorial, then push. */
            ToriRSServer_WorldTeleport(srv, 0, 3578, 3528);
            slot = ToriRSServer_SceneAddLoc(3578, 3528, 0, loc_coffin, 10, 0);
            player->last_useitem = obj_star;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_coffin,
                                                ToriRSServer_LocCategory(loc_coffin), slot);
            selftest_drain_choices(srv, &yes, 1, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            player->last_useitem = -1;
            SELFTEST_CHECK(vb_coffin < 0 || ToriRSServer_VarbitGet(player, vb_coffin) == 1,
                           "fenkenstrain: star on memorial should set fenk_coffin");
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_coffin,
                                                ToriRSServer_LocCategory(loc_coffin), slot);
            SELFTEST_CHECK(player->x == 3578 && player->z == 9928,
                           "fenkenstrain: push memorial should enter the cave at 3578,9928, got %d,%d",
                           player->x, player->z);
            fprintf(stderr, "PASS fenkenstrain memorial star and push\n");

            /* 10. Kill the level-51 experiment for the private cavern key. */
            npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_exp, player->x, player->z, 0);
            SELFTEST_CHECK(npc_slot >= 0, "fenkenstrain: experiment should spawn");
            if( npc_slot >= 0 )
            {
                ToriRSServer_CombatHitNpc(srv, npc_slot, 0, 9999);
                for( i = 0; i < 8; i++ )
                    selftest_tick(srv);
            }
            {
                int found = 0;

                for( i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
                {
                    if( srv->ground[i].active && srv->ground[i].obj_id == obj_key &&
                        srv->ground[i].receiver_pid == player->pid )
                    {
                        found = 1;
                        srv->ground[i].active = 0;
                        break;
                    }
                }
                SELFTEST_CHECK(found, "fenkenstrain: experiment death should drop a private cavern key");
                if( found )
                    selftest_give(player, obj_key, 1);
                fprintf(stderr, "PASS fenkenstrain experiment private cavern key\n");
            }
            if( npc_slot >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, npc_slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* 11. Key the mausoleum door. */
            ToriRSServer_WorldTeleport(srv, 0, 3504, 3570);
            slot = ToriRSServer_SceneAddLoc(3504, 3570, 0, loc_maus_door, 0, 0);
            player->last_useitem = obj_key;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_maus_door,
                                                ToriRSServer_LocCategory(loc_maus_door), slot);
            player->last_useitem = -1;
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(vb_cavern < 0 || ToriRSServer_VarbitGet(player, vb_cavern) == 1,
                           "fenkenstrain: mausoleum key should set fenk_unlocked_cavern");
            fprintf(stderr, "PASS fenkenstrain mausoleum door unlock\n");

            /* 12. Three QH mausoleum graves. */
            {
                const struct
                {
                    int x;
                    int z;
                    int obj;
                    const char* name;
                } graves[] = {
                    { 3503, 3576, obj_torso, "torso" },
                    { 3504, 3576, obj_arms, "arms" },
                    { 3505, 3576, obj_legs, "legs" },
                };

                for( i = 0; i < 3; i++ )
                {
                    ToriRSServer_WorldTeleport(srv, 0, graves[i].x, graves[i].z);
                    slot = ToriRSServer_SceneAddLoc(graves[i].x, graves[i].z, 0, loc_grave, 10, 0);
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_grave,
                                                        ToriRSServer_LocCategory(loc_grave), slot);
                    {
                        int t;

                        for( t = 0; t < 4; t++ )
                            selftest_tick(srv);
                    }
                    selftest_drain_choices(srv, &yes, 1, 8);
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                    SELFTEST_CHECK(selftest_count(player, graves[i].obj) == 1,
                                   "fenkenstrain: %s grave should yield its part", graves[i].name);
                }
                fprintf(stderr, "PASS fenkenstrain mausoleum torso/arms/legs\n");
            }

            /* 13. Hand parts to the Doctor -> 2. */
            ToriRSServer_WorldTeleport(srv, 0, 3551, 3548);
            if( npc_slot >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, npc_slot);
                ToriRSServer_WorldNpcReap(srv);
            }
            npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_doc, 3551, 3548, 0);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_doc, -1, npc_slot);
            selftest_drain_choices(srv, parts_row, 1, 32);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[varp_fenk] == 2,
                           "fenkenstrain: body-part hand-in should set fenk_quest=2, got %d",
                           player->varps[varp_fenk]);
            fprintf(stderr, "PASS fenkenstrain body parts hand-in fenk_quest=2\n");

            /* 14. Needle + five thread -> 3. */
            if( obj_needle >= 0 )
                selftest_give(player, obj_needle, 1);
            if( obj_thread >= 0 )
                selftest_give(player, obj_thread, 5);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_doc, -1, npc_slot);
            selftest_drain_choices(srv, &yes, 1, 40);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[varp_fenk] == 3,
                           "fenkenstrain: needle+thread should sew the body, fenk_quest=3, got %d",
                           player->varps[varp_fenk]);
            fprintf(stderr, "PASS fenkenstrain needle and thread fenk_quest=3\n");

            /* 15. Gardener shed key, cupboard brush, three canes + wire. */
            if( obj_ghostspeak >= 0 )
                worn_set(player, TORIRSSERVER_WEAR_AMULET, obj_ghostspeak, 1);
            ToriRSServer_WorldTeleport(srv, 0, 3548, 3550);
            {
                int gslot = ToriRSServer_WorldNpcSpawn(srv, npc_gardener, 3548, 3550, 0);

                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gardener, -1, gslot);
                selftest_drain_choices(srv, parts_row, 1, 24);
                ToriRSServer_WorldCloseModal(srv);
                player->active_script = NULL;
                SELFTEST_CHECK(selftest_count(player, obj_shedkey) == 1,
                               "fenkenstrain: gardener should hand the shed key");
                if( gslot >= 0 )
                {
                    ToriRSServer_WorldNpcFree(srv, gslot);
                    ToriRSServer_WorldNpcReap(srv);
                }
            }
            ToriRSServer_WorldTeleport(srv, 0, 3544, 3552);
            slot = ToriRSServer_SceneAddLoc(3544, 3552, 0, loc_shed, 0, 0);
            player->last_useitem = obj_shedkey;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_shed,
                                                ToriRSServer_LocCategory(loc_shed), slot);
            player->last_useitem = -1;
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            slot = ToriRSServer_SceneAddLoc(3545, 3552, 0, loc_cupboard, 10, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cupboard,
                                                ToriRSServer_LocCategory(loc_cupboard), slot);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_cupboard_open,
                                                ToriRSServer_LocCategory(loc_cupboard_open), slot);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(selftest_count(player, obj_brush0) == 1,
                           "fenkenstrain: cupboard should give a garden brush");
            slot = ToriRSServer_SceneAddLoc(3546, 3552, 0, loc_canes, 10, 0);
            for( i = 0; i < 3; i++ )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_canes,
                                                    ToriRSServer_LocCategory(loc_canes), slot);
                ToriRSServer_WorldCloseModal(srv);
                player->active_script = NULL;
            }
            SELFTEST_CHECK(selftest_count(player, obj_cane) >= 3,
                           "fenkenstrain: cane pile should give three canes");
            if( obj_wire >= 0 )
                selftest_give(player, obj_wire, 3);
            for( i = 0; i < 3; i++ )
            {
                int b_slot = -1;
                int c_slot = -1;
                int brush = obj_brush0;
                int t;

                for( t = 0; t < TORIRSSERVER_INV_SLOTS; t++ )
                {
                    if( player->inv[t].obj_id == obj_brush0 ||
                        player->inv[t].obj_id ==
                            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_brush1") ||
                        player->inv[t].obj_id ==
                            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fenk_brush2") ||
                        player->inv[t].obj_id == obj_brush3 )
                    {
                        b_slot = t;
                        brush = player->inv[t].obj_id;
                    }
                    if( player->inv[t].obj_id == obj_cane )
                        c_slot = t;
                }
                if( b_slot < 0 || c_slot < 0 )
                    break;
                rsab_wrap(&out, payload, sizeof(payload));
                rsab_p2(&out, brush);
                rsab_p2(&out, b_slot);
                rsab_p4(&out, 0);
                rsab_p2(&out, obj_cane);
                rsab_p2(&out, c_slot);
                rsab_p4(&out, 0);
                selftest_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
                selftest_drain_choices(srv, &yes, 1, 8);
                ToriRSServer_WorldCloseModal(srv);
                player->active_script = NULL;
            }
            SELFTEST_CHECK(selftest_count(player, obj_brush3) == 1,
                           "fenkenstrain: three canes+wire should make the extended brush");
            fprintf(stderr, "PASS fenkenstrain shed brush and extended canes\n");

            /* 16. Fireplace mould, furnace conductor, repair. */
            ToriRSServer_WorldTeleport(srv, 1, 3544, 3555);
            slot = ToriRSServer_SceneAddLoc(3544, 3555, 1, loc_fire, 10, 0);
            player->last_useitem = obj_brush3;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_fire,
                                                ToriRSServer_LocCategory(loc_fire), slot);
            selftest_drain_choices(srv, &yes, 1, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            player->last_useitem = -1;
            SELFTEST_CHECK(selftest_count(player, obj_mould) == 1,
                           "fenkenstrain: west fireplace should give the conductor mould");
            if( obj_silver >= 0 )
                selftest_give(player, obj_silver, 1);
            ToriRSServer_CombatSetLevel(player, stat_craft, 20);
            craft_before = player->stat_xp_tenths[stat_craft];
            ToriRSServer_WorldTeleport(srv, 0, 3225, 3254);
            slot = ToriRSServer_SceneAddLoc(3225, 3254, 0, loc_furnace, 10, 0);
            player->last_useitem = obj_silver;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_furnace,
                                                ToriRSServer_LocCategory(loc_furnace), slot);
            for( i = 0; i < 6; i++ )
                selftest_tick(srv);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            player->last_useitem = -1;
            SELFTEST_CHECK(selftest_count(player, obj_conductor) == 1,
                           "fenkenstrain: furnace should cast a silver conductor");
            SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] - craft_before == 500,
                           "fenkenstrain: casting should award 50 Crafting XP, delta %d",
                           player->stat_xp_tenths[stat_craft] - craft_before);
            fprintf(stderr, "PASS fenkenstrain furnace conductor + 50 Crafting XP\n");

            ToriRSServer_WorldTeleport(srv, 2, 3548, 3538);
            slot = ToriRSServer_SceneAddLoc(3548, 3538, 2, loc_broken, 10, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_broken,
                                                ToriRSServer_LocCategory(loc_broken), slot);
            selftest_drain_choices(srv, &yes, 1, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[varp_fenk] == 4,
                           "fenkenstrain: repairing the conductor should set fenk_quest=4, got %d",
                           player->varps[varp_fenk]);
            SELFTEST_CHECK(vb_repaired < 0 || ToriRSServer_VarbitGet(player, vb_repaired) == 1,
                           "fenkenstrain: repair should set fenk_conductor_repaired");
            fprintf(stderr, "PASS fenkenstrain conductor repair fenk_quest=4\n");

            /* 17. Doctor gives the tower key -> 5. */
            ToriRSServer_WorldTeleport(srv, 0, 3551, 3548);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_doc, -1, npc_slot);
            selftest_drain_choices(srv, &yes, 1, 40);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[varp_fenk] == 5,
                           "fenkenstrain: Doctor after lightning should set fenk_quest=5, got %d",
                           player->varps[varp_fenk]);
            SELFTEST_CHECK(selftest_count(player, obj_towerkey) == 1,
                           "fenkenstrain: Doctor should give the tower key");
            fprintf(stderr, "PASS fenkenstrain tower key fenk_quest=5\n");

            /* 18. Tower door unlocks in place: no teleport, no spawned creature. */
            x0 = player->x;
            z0 = player->z;
            level0 = player->level;
            creatures_before = 0;
            for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
                if( srv->npcs[i].active && srv->npcs[i].type == npc_creature )
                    creatures_before++;
            slot = ToriRSServer_SceneAddLoc(3548, 3552, 1, loc_tower, 0, 0);
            ToriRSServer_WorldTeleport(srv, 1, 3548, 3552);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tower,
                                                ToriRSServer_LocCategory(loc_tower), slot);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            {
                int creatures_after = 0;

                for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
                    if( srv->npcs[i].active && srv->npcs[i].type == npc_creature )
                        creatures_after++;
                SELFTEST_CHECK(player->x == 3548 && player->z == 3552 && player->level == 1,
                               "fenkenstrain: tower door must not teleport, got %d,%d level %d",
                               player->x, player->z, player->level);
                SELFTEST_CHECK(creatures_after == creatures_before,
                               "fenkenstrain: tower door must not spawn a creature (%d -> %d)",
                               creatures_before, creatures_after);
                (void)x0;
                (void)z0;
                (void)level0;
            }
            fprintf(stderr, "PASS fenkenstrain tower door unlocks without teleport\n");

            /* 19. Talk to the placed creature -> 6. */
            ToriRSServer_WorldTeleport(srv, 2, 3548, 3555);
            {
                int cslot = ToriRSServer_WorldNpcSpawn(srv, npc_creature, 3548, 3555, 2);

                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_creature, -1, cslot);
                selftest_drain_choices(srv, &yes, 1, 64);
                ToriRSServer_WorldCloseModal(srv);
                player->active_script = NULL;
                SELFTEST_CHECK(player->varps[varp_fenk] == 6,
                               "fenkenstrain: Rologarth reveal should set fenk_quest=6, got %d",
                               player->varps[varp_fenk]);
                if( cslot >= 0 )
                {
                    ToriRSServer_WorldNpcFree(srv, cslot);
                    ToriRSServer_WorldNpcReap(srv);
                }
                fprintf(stderr, "PASS fenkenstrain creature talk fenk_quest=6\n");
            }

            /* 20. Pickpocket -> 9, one ring, 1000 Thieving XP, 2 QP. */
            ToriRSServer_CombatSetLevel(player, stat_thieve, 25);
            xp_before = player->stat_xp_tenths[stat_thieve];
            qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
            ToriRSServer_WorldTeleport(srv, 0, 3551, 3548);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_doc, -1, npc_slot);
            for( i = 0; i < 4; i++ )
                selftest_tick(srv);
            selftest_drain_choices(srv, &yes, 1, 16);
            {
                int drain_tick;

                for( drain_tick = 0; drain_tick < 40 && player->varps[varp_fenk] != 9; drain_tick++ )
                {
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                }
            }
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[varp_fenk] == 9,
                           "fenkenstrain: pickpocket should complete at fenk_quest=9, got %d",
                           player->varps[varp_fenk]);
            SELFTEST_CHECK(selftest_count(player, obj_ring) == 1,
                           "fenkenstrain: completion should grant exactly one Ring of Charos");
            SELFTEST_CHECK(player->stat_xp_tenths[stat_thieve] - xp_before == 10000,
                           "fenkenstrain: completion should award 1000 Thieving XP, delta %d",
                           player->stat_xp_tenths[stat_thieve] - xp_before);
            SELFTEST_CHECK(varp_qp < 0 || player->varps[varp_qp] - qp_before == 2,
                           "fenkenstrain: completion should award 2 QP, delta %d",
                           varp_qp >= 0 ? player->varps[varp_qp] - qp_before : -1);
            fprintf(stderr, "PASS fenkenstrain pickpocket complete fenk_quest=9 ring+1000xp+2qp\n");

            SELFTEST_CHECK(player->godmode == 1 && player->hitpoints > 0,
                           "fenkenstrain: player must stay alive (godmode=%d hp=%d)",
                           player->godmode, player->hitpoints);
            fprintf(stderr, "PASS fenkenstrain player alive godmode=1\n");

            if( npc_slot >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, npc_slot);
                ToriRSServer_WorldNpcReap(srv);
            }
            worn_set(player, TORIRSSERVER_WEAR_AMULET, -1, 0);
            selftest_clear_inv(player);
            selftest_clear_ground(srv);
        }
        (void)owned;
    }
}
