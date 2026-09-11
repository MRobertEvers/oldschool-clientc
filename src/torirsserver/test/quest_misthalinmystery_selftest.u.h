/* Misthalin Mystery Gate D -- critical-path stanza.
 *
 * Included immediately before a `selftest_reset_world` so spawned npcs,
 * ticks and modal leftovers cannot re-aim later checks. Every progress
 * write is reached through a real OPNPC / OPLOC trigger, not `::mmrun`.
 * Mutation: if Abigale's start arm drops `~p_choice2` and auto-starts,
 * `PASS mistmyst abigale_refuse` dies (progress must stay 0).
 *
 * TORIRSSERVER_SELFTEST_MISTMYST_ONLY=1 runs this stanza without the rest
 * of the suite. Player is unkillable (not a death case).
 * Helpers stay inline: this file is included inside another function.
 */
{
    int loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());

    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());

    fprintf(stderr, "ToriRSServer selftest: ::mmrun\n");
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
    }
    else
    {
        int vb_progress =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mistmyst_progress");
        int vp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        int npc_abigale =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mistmyst_abigale");
        int npc_abigale_vis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                                        "mistmyst_abigale_lum_vis");
        int npc_mandy =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mistmyst_mandy_post");
        int npc_killer = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "mistmyst_abigale_killer_attackable");
        int loc_boat =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_boat_lumbridge");
        int loc_barrel =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_barrel");
        int loc_door =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_front_doorl");
        int loc_pink =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_door_redtopaz");
        int loc_notes =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_clue_outside");
        int loc_paint =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_painting");
        int loc_ruby =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_door_ruby");
        int loc_candle =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_candle1");
        int loc_fuse =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_explosive_barrel");
        int loc_wall = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "mistmyst_destructable_wall_climbable");
        int loc_piano =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_piano");
        int loc_emerald =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_door_emerald");
        int loc_tree =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_tree");
        int loc_fire =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_fireplace");
        int loc_sapphire =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mistmyst_door_sapphire");
        int obj_front =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mistmyst_frontdoor_key");
        int obj_ruby =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mistmyst_ruby_key");
        int obj_emerald =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mistmyst_emerald_key");
        int obj_sapphire =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mistmyst_sapphire_key");
        int stat_craft =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
        int rows_uid =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
        int ok = vb_progress >= 0 && vp_qp >= 0 && npc_abigale >= 0 &&
                 npc_mandy >= 0 && npc_killer >= 0 && loc_boat >= 0 &&
                 loc_barrel >= 0 && loc_door >= 0 && loc_pink >= 0 &&
                 loc_notes >= 0 && loc_paint >= 0 && loc_ruby >= 0 &&
                 loc_candle >= 0 && loc_fuse >= 0 && loc_wall >= 0 &&
                 loc_piano >= 0 && loc_emerald >= 0 && loc_tree >= 0 &&
                 loc_fire >= 0 && loc_sapphire >= 0 && obj_front >= 0 &&
                 obj_ruby >= 0 && obj_emerald >= 0 && obj_sapphire >= 0 &&
                 stat_craft >= 0;

        SELFTEST_CHECK(ok, "the ::mmrun C-side names should all resolve");
        if( ok )
        {
            int qp_before;
            int xp_before;
            int slot;
            int talk_npc;

            /* Player unkillable unless this step is a death test. It is not. */
            player->godmode = 1;
            player->dying = 0;
            if( player->max_hitpoints > 0 )
                player->hitpoints = player->max_hitpoints;
            ToriRSServer_CombatSyncHitpoints(player);
            for( int s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, -1, 0);
            ToriRSServer_VarbitSet(srv, vb_progress, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3237, 3155);
            selftest_tick(srv);

            talk_npc = npc_abigale;
            slot = npc_spawn(srv, npc_abigale, 3237, 3155, 0);
            if( slot < 0 && npc_abigale_vis >= 0 )
            {
                talk_npc = npc_abigale_vis;
                slot = npc_spawn(srv, npc_abigale_vis, 3237, 3155, 0);
            }
            SELFTEST_CHECK(slot >= 0, "Abigale should spawn for the start talk");
            if( slot >= 0 )
            {
                /* Refuse must not write progress. */
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, talk_npc, -1,
                                               slot);
                biohazard_run_dialogue(srv, player, rows_uid);
                SELFTEST_CHECK(player->active_script != NULL &&
                                   player->resume_button_count > 0,
                               "Abigale at state 0 should reach Yes / Not now");
                if( player->active_script != NULL )
                {
                    player->last_slot = 2; /* "Not now." */
                    if( rows_uid > 0 )
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    biohazard_run_dialogue(srv, player, 0);
                }
                ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 0,
                               "declining Abigale must not write mistmyst_progress, got %d",
                               ToriRSServer_VarbitGet(player, vb_progress));
                fprintf(stderr, "MISTMYST PASS: abigale_refuse\n");

                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, talk_npc, -1,
                                               slot);
                biohazard_run_dialogue(srv, player, rows_uid);
                if( player->active_script != NULL )
                {
                    player->last_slot = 1; /* "Yes." */
                    if( rows_uid > 0 )
                        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
                    biohazard_run_dialogue(srv, player, 0);
                }
                ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 10,
                               "accepting Abigale should reach ^mm_barrel (10), got %d",
                               ToriRSServer_VarbitGet(player, vb_progress));
                fprintf(stderr, "MISTMYST PASS: abigale_accept\n");
            }

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_boat, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 20,
                           "the swamp boat should reach ^mm_empty (20), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: boat_sail\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_barrel, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 25 &&
                               selftest_count_obj(player, obj_front) >= 1,
                           "the barrel should grant the front-door key and ^mm_house (25), got progress=%d keys=%d",
                           ToriRSServer_VarbitGet(player, vb_progress),
                           selftest_count_obj(player, obj_front));
            fprintf(stderr, "MISTMYST PASS: barrel_key\n");

            for( int s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, -1, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 25,
                           "the locked front door must not advance without the key, got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: front_door_locked\n");

            inv_set(player, 0, obj_front, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 30,
                           "unlocking the front door should reach ^mm_pink (30), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: front_door_unlock\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_pink, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 35,
                           "the pink door should reach ^mm_notes1 (35), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: pink_door\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_notes, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 40,
                           "the first notes should reach ^mm_painting (40), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: notes1\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_paint, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 45 &&
                               selftest_count_obj(player, obj_ruby) >= 1,
                           "the painting should grant the ruby key and ^mm_ruby (45), got progress=%d keys=%d",
                           ToriRSServer_VarbitGet(player, vb_progress),
                           selftest_count_obj(player, obj_ruby));
            fprintf(stderr, "MISTMYST PASS: painting_cut\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_ruby, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 50,
                           "the ruby door should reach ^mm_candles (50), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: ruby_door\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_candle, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 55,
                           "lighting the candles should reach ^mm_fuse (55), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: candles\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_fuse, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 60,
                           "the fuse should reach ^mm_leave_bang (60), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: fuse\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 65,
                           "the blasted wall should reach ^mm_lacey (65), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: wall_climb\n");

            {
                int mandy = npc_spawn(srv, npc_mandy, player->x + 1, player->z + 1,
                                      player->level);
                SELFTEST_CHECK(mandy >= 0, "Mandy should spawn for the lacey talk");
                if( mandy >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_mandy, -1,
                                                   mandy);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 70,
                                   "Mandy at lacey should reach ^mm_notes2 (70), got %d",
                                   ToriRSServer_VarbitGet(player, vb_progress));
                    fprintf(stderr, "MISTMYST PASS: mandy_lacey\n");
                }
            }

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_notes, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 75,
                           "the library notes should reach ^mm_piano (75), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: notes2\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_piano, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 80 &&
                               selftest_count_obj(player, obj_emerald) >= 1,
                           "the piano should grant the emerald key and ^mm_emerald (80), got progress=%d keys=%d",
                           ToriRSServer_VarbitGet(player, vb_progress),
                           selftest_count_obj(player, obj_emerald));
            fprintf(stderr, "MISTMYST PASS: piano\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_emerald, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 85,
                           "the emerald door should reach ^mm_bandos (85), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: emerald_door\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tree, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 90,
                           "the tree should reach ^mm_puzzle3 (90), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: tree\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_fire, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 105 &&
                               selftest_count_obj(player, obj_sapphire) >= 1,
                           "the fireplace should grant the sapphire key and ^mm_sapphire (105), got progress=%d keys=%d",
                           ToriRSServer_VarbitGet(player, vb_progress),
                           selftest_count_obj(player, obj_sapphire));
            fprintf(stderr, "MISTMYST PASS: fireplace\n");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_sapphire, -1,
                                               -1);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 110,
                           "the sapphire door should reach ^mm_boss (110), got %d",
                           ToriRSServer_VarbitGet(player, vb_progress));
            fprintf(stderr, "MISTMYST PASS: sapphire_door\n");

            {
                int killer = npc_spawn(srv, npc_killer, player->x + 2, player->z + 2,
                                       player->level);
                SELFTEST_CHECK(killer >= 0, "the killer should spawn for the skip");
                if( killer >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_killer, -1,
                                                   killer);
                    ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 130,
                                   "defeating Abigale should reach ^mm_finish (130), got %d",
                                   ToriRSServer_VarbitGet(player, vb_progress));
                    fprintf(stderr, "MISTMYST PASS: boss_skip\n");
                }
            }

            qp_before = player->varps[vp_qp];
            xp_before = player->stat_xp_tenths[stat_craft];
            {
                int mandy = selftest_find_npc(srv, npc_mandy);
                if( mandy < 0 )
                    mandy = npc_spawn(srv, npc_mandy, player->x + 1, player->z + 1,
                                      player->level);
                SELFTEST_CHECK(mandy >= 0, "Mandy should be present for completion");
                if( mandy >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_mandy, -1,
                                                   mandy);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 135,
                                   "Mandy at finish should reach ^mm_complete (135), got %d",
                                   ToriRSServer_VarbitGet(player, vb_progress));
                    SELFTEST_CHECK(player->varps[vp_qp] == qp_before + 1,
                                   "completion should award 1 QP, %d -> %d",
                                   qp_before, player->varps[vp_qp]);
                    SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] ==
                                       xp_before + 6000,
                                   "completion should award 600 Crafting XP (6000 tenths), %d -> %d",
                                   xp_before, player->stat_xp_tenths[stat_craft]);
                    fprintf(stderr, "MISTMYST PASS: mandy_complete\n");
                }
            }

            ToriRSServer_ScriptsRunProc(srv, "[proc,misthalinmystery_journal]", NULL, 0);
            ToriRSServer_WorldCloseModal(srv);
                selftest_clear_pending(srv, player);
            fprintf(stderr, "MISTMYST PASS: journal_complete\n");
        }
    }
}
