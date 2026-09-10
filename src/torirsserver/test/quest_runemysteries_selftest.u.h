/*
 * Rune Mysteries Gate D walk. Included immediately before a
 * `selftest_reset_world` so spawned npcs/locs and ticks cannot re-aim later
 * stanzas. QH steps.put 0..5; complete = 6. Real OPNPC1 / OPNPCU / OPLOC1 /
 * OPHELD4 on the critical path. Player is unkillable (`godmode = 1`).
 *
 * Wiki: Rune_Mysteries oldid=15275863, Transcript:Rune_Mysteries oldid=15331800.
 */
    fprintf(stderr, "ToriRSServer selftest: ::runemysteriesrun\n");
    {
        int loaded = srv->scripts_ok;

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
            int varp_rm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "runemysteries");
            int npc_duke = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "duke_of_lumbridge");
            int npc_sedridor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "head_wizard");
            int npc_sed_shell =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "head_wizard_1op");
            int npc_aubury = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "aubury");
            int npc_aub_shell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "aubury_2op");
            int obj_talisman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "air_talisman");
            int obj_package = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "research_package");
            int obj_notes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "research_notes");
            int loc_ladder =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "wizards_tower_laddertop");
            int chatmenu =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
            int com_messagebox =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
            int s;
            int duke_slot = -1;
            int sed_slot = -1;
            int aub_slot = -1;
            int loc_slot;
            int drain;
            int qp_before;
            int varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
            int varp_dragonquest =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "dragonquest");
            int varp_dragon_shield =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "dragon_shield");
            int vb_tote = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "tote");
            int vb_cowquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "cowquest");
            int vb_lost_tribe =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lost_tribe_quest");
            int vb_dttd = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "dttd_main");

            player->godmode = 1;

            SELFTEST_CHECK(varp_rm >= 0 && npc_duke >= 0 && npc_sedridor >= 0 &&
                               npc_aubury >= 0 && obj_talisman >= 0 && obj_package >= 0 &&
                               obj_notes >= 0 && loc_ladder >= 0 && chatmenu >= 0,
                           "Rune Mysteries pack names should all resolve");
            if( varp_rm < 0 || npc_duke < 0 || npc_sedridor < 0 || npc_aubury < 0 ||
                obj_talisman < 0 || obj_package < 0 || obj_notes < 0 || loc_ladder < 0 )
            {
                fprintf(stderr, "  SKIP  missing Rune Mysteries symbols\n");
            }
            else
            {
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                player->varps[varp_rm] = 0;
                if( varp_dragonquest >= 0 )
                    player->varps[varp_dragonquest] = 0;
                if( varp_dragon_shield >= 0 )
                    player->varps[varp_dragon_shield] = 0;
                if( vb_tote >= 0 )
                    ToriRSServer_VarbitSet(srv, vb_tote, 0);
                if( vb_cowquest >= 0 )
                    ToriRSServer_VarbitSet(srv, vb_cowquest, 0);
                if( vb_lost_tribe >= 0 )
                    ToriRSServer_VarbitSet(srv, vb_lost_tribe, 0);
                if( vb_dttd >= 0 )
                    ToriRSServer_VarbitSet(srv, vb_dttd, 0);

                /* ---- refuse: OPNPC1 Duke, pick Not right now. ---- */
                ToriRSServer_WorldTeleport(srv, 1, 3209, 3222);
                selftest_tick(srv);
                duke_slot = ToriRSServer_WorldNpcSpawn(srv, npc_duke, 3210, 3222, 1);
                SELFTEST_CHECK(duke_slot >= 0, "Duke Horacio should spawn for OPNPC1");
                if( duke_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_duke, -1,
                                                   duke_slot);
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( player->active_script && chatmenu > 0 )
                    {
                        player->last_slot = 1; /* Have you any quests for me? */
                        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
                    }
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( player->active_script && chatmenu > 0 )
                    {
                        player->last_slot = 2; /* Not right now. */
                        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
                    }
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(player->varps[varp_rm] == 0,
                                   "OPNPC1 Duke refuse must leave runemysteries=0, got %d",
                                   player->varps[varp_rm]);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_talisman) == 0,
                                   "refuse must not grant the air talisman");
                    if( player->varps[varp_rm] == 0 )
                        fprintf(stderr, "  PASS  opnpc1 duke refuse stays not_started\n");
                }

                /* ---- step 0: real OPNPC1 Duke Horacio, accept Yes. ---- */
                if( duke_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_duke, -1,
                                                   duke_slot);
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( player->active_script && chatmenu > 0 )
                    {
                        player->last_slot = 1; /* Have you any quests for me? */
                        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
                    }
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( player->active_script && chatmenu > 0 )
                    {
                        player->last_slot = 1; /* Sure, no problem. */
                        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
                    }
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( com_messagebox > 0 && player->active_script )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(player->varps[varp_rm] == 1,
                                   "OPNPC1 Duke Yes should write runemysteries=1, got %d",
                                   player->varps[varp_rm]);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_talisman) == 1,
                                   "Duke should hand over air_talisman, got count %d",
                                   selftest_count_obj(player, obj_talisman));
                    if( player->varps[varp_rm] == 1 &&
                        selftest_count_obj(player, obj_talisman) == 1 )
                        fprintf(stderr, "  PASS  opnpc1 duke starts the quest (step 0->1)\n");
                }

                /* ---- OPHELD4 Locate on the air talisman (critical-path item) ---- */
                player->last_item = obj_talisman;
                player->last_slot = 0;
                ToriRSServer_WorldTeleport(srv, 0, 3208, 3220);
                selftest_tick(srv);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD4, obj_talisman, -1, -1);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_talisman) == 1,
                               "OPHELD4 Locate must not consume the air talisman");
                if( selftest_count_obj(player, obj_talisman) == 1 )
                    fprintf(stderr, "  PASS  opheld4 air_talisman Locate\n");

                /* ---- OPLOC1 wizards_tower_laddertop (maplink into the basement) ----
                 * maplink.dbrow is keyed on the PLAYER tile, not the loc tile.
                 * 3104,3162 is the loc; the harvested stand tiles are
                 * 3103,3162 / 3104,3161 / 3104,3163. */
                ToriRSServer_WorldTeleport(srv, 0, 3104, 3161);
                selftest_tick(srv);
                loc_slot = ToriRSServer_SceneFindLocId(3104, 3162, 0, loc_ladder);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(3104, 3162, 0, loc_ladder, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0,
                               "wizards_tower_laddertop should stand at 3104,3162");
                if( loc_slot >= 0 )
                {
                    int ladder_cat = ToriRSServer_LocCategory(loc_ladder);
                    int climb_rc;

                    climb_rc = ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, loc_ladder, ladder_cat, loc_slot);
                    SELFTEST_CHECK(climb_rc == TORIRSSERVER_TRIGGER_RAN,
                                   "OPLOC1 wizards_tower_laddertop should bind "
                                   "climb_down_ladder, got trigger %d cat %d",
                                   climb_rc, ladder_cat);
                    for( drain = 0; drain < 24 && player->z < 9500; drain++ )
                    {
                        if( player->active_script && player->resume_button_count > 0 )
                            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
                        selftest_tick(srv);
                    }
                    SELFTEST_CHECK(player->z >= 9500,
                                   "OPLOC1 wizards_tower_laddertop should maplink into "
                                   "the basement, got z=%d",
                                   player->z);
                    if( player->z >= 9500 )
                        fprintf(stderr,
                                "  PASS  oploc1 wizards_tower_laddertop (step 1 travel)\n");
                }

                /* ---- step 1+2: OPNPC1 Sedridor shell, give talisman, take package ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3104, 9571);
                selftest_tick(srv);
                sed_slot = ToriRSServer_WorldNpcSpawn(srv, npc_sedridor, 3105, 9571, 0);
                SELFTEST_CHECK(sed_slot >= 0, "Sedridor should spawn for OPNPC1");
                if( sed_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sedridor, -1,
                                                   sed_slot);
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( player->active_script && chatmenu > 0 )
                    {
                        player->last_slot = 3; /* I'm looking for the head wizard. */
                        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
                    }
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( player->active_script && chatmenu > 0 )
                    {
                        player->last_slot = 1; /* Ok, here you are. */
                        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
                    }
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( com_messagebox > 0 && player->active_script )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( player->active_script && chatmenu > 0 )
                    {
                        player->last_slot = 1; /* Yes, certainly. */
                        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
                    }
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( com_messagebox > 0 && player->active_script )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(player->varps[varp_rm] == 3,
                                   "OPNPC1 Sedridor should reach received_package=3, got %d",
                                   player->varps[varp_rm]);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_package) == 1,
                                   "Sedridor should hand over research_package, got count %d",
                                   selftest_count_obj(player, obj_package));
                    SELFTEST_CHECK(selftest_count_obj(player, obj_talisman) == 0,
                                   "giving the talisman should consume it");
                    if( player->varps[varp_rm] == 3 &&
                        selftest_count_obj(player, obj_package) == 1 )
                        fprintf(stderr,
                                "  PASS  opnpc1 sedridor talisman+package (step 1->3)\n");
                }

                /* Visible basement child must take the same Rune Mysteries body. */
                if( npc_sed_shell >= 0 && player->varps[varp_rm] == 3 )
                {
                    int child = ToriRSServer_WorldNpcSpawn(srv, npc_sed_shell, 3106, 9571, 0);

                    SELFTEST_CHECK(child >= 0, "head_wizard_1op should spawn");
                    if( child >= 0 )
                    {
                        int trig;

                        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                            inv_set(player, s, -1, 0);
                        trig = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1,
                                                              npc_sed_shell, -1, child);
                        SELFTEST_CHECK(trig == TORIRSSERVER_TRIGGER_RAN,
                                       "OPNPC1 head_wizard_1op should bind Sedridor, got %d",
                                       trig);
                        biohazard_run_dialogue(srv, player, 0);
                        ToriRSServer_WorldCloseModal(srv);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_package) == 1,
                                       "head_wizard_1op lost-package talk should replace "
                                       "research_package");
                        ToriRSServer_WorldNpcFree(srv, child);
                        if( selftest_count_obj(player, obj_package) == 1 )
                            fprintf(stderr,
                                    "  PASS  opnpc1 head_wizard_1op is Rune Mysteries\n");
                    }
                }

                /* ---- step 3: OPNPCU research_package on Aubury ---- */
                ToriRSServer_WorldTeleport(srv, 0, 3253, 3401);
                selftest_tick(srv);
                aub_slot = ToriRSServer_WorldNpcSpawn(srv, npc_aubury, 3254, 3401, 0);
                SELFTEST_CHECK(aub_slot >= 0, "Aubury should spawn for OPNPCU");
                if( aub_slot >= 0 )
                {
                    player->last_useitem = obj_package;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_aubury, -1,
                                                   aub_slot);
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( com_messagebox > 0 && player->active_script )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(player->varps[varp_rm] == 4,
                                   "OPNPCU package on Aubury should write given_package=4, "
                                   "got %d",
                                   player->varps[varp_rm]);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_package) == 0,
                                   "delivering the package should consume it");
                    if( player->varps[varp_rm] == 4 )
                        fprintf(stderr,
                                "  PASS  opnpcu research_package on aubury (step 3->4)\n");

                    /* ---- step 4: OPNPC1 Aubury again for notes ---- */
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aubury, -1,
                                                   aub_slot);
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( com_messagebox > 0 && player->active_script )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(player->varps[varp_rm] == 5,
                                   "OPNPC1 Aubury should write received_notes=5, got %d",
                                   player->varps[varp_rm]);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_notes) == 1,
                                   "Aubury should hand over research_notes, got count %d",
                                   selftest_count_obj(player, obj_notes));
                    if( player->varps[varp_rm] == 5 &&
                        selftest_count_obj(player, obj_notes) == 1 )
                        fprintf(stderr, "  PASS  opnpc1 aubury notes (step 4->5)\n");
                }

                if( npc_aub_shell >= 0 && player->varps[varp_rm] == 5 )
                {
                    int child = ToriRSServer_WorldNpcSpawn(srv, npc_aub_shell, 3255, 3401, 0);

                    SELFTEST_CHECK(child >= 0, "aubury_2op should spawn");
                    if( child >= 0 )
                    {
                        int trig = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1,
                                                                 npc_aub_shell, -1, child);

                        SELFTEST_CHECK(trig == TORIRSSERVER_TRIGGER_RAN,
                                       "OPNPC1 aubury_2op should bind Aubury, got %d", trig);
                        biohazard_run_dialogue(srv, player, 0);
                        ToriRSServer_WorldCloseModal(srv);
                        ToriRSServer_WorldNpcFree(srv, child);
                        fprintf(stderr, "  PASS  opnpc1 aubury_2op is Rune Mysteries\n");
                    }
                }

                /* ---- step 5: OPNPCU research_notes on Sedridor, complete ---- */
                if( sed_slot >= 0 )
                {
                    int com_mb = com_messagebox;

                    ToriRSServer_WorldTeleport(srv, 0, 3104, 9571);
                    selftest_tick(srv);
                    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
                    player->last_useitem = obj_notes;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_sedridor, -1,
                                                   sed_slot);
                    /* Long Sedridor reveal: click every pause, including the
                     * notes mesbox that arms queue(rune_mysteries_complete).
                     * WorldCloseModal aborts a parked script, so do not close
                     * until the queue has written complete=6. */
                    biohazard_run_dialogue(srv, player, 0);
                    selftest_click_through(srv, 80);
                    for( drain = 0; drain < 80 && player->varps[varp_rm] != 6; drain++ )
                    {
                        if( player->active_script && player->resume_button_count > 0 )
                        {
                            int uid = player->resume_buttons[0];

                            if( com_mb > 0 && uid == com_mb )
                                ToriRSServer_ScriptsResumeButton(srv, com_mb);
                            else
                                ToriRSServer_ScriptsResumeButton(srv, uid);
                        }
                        selftest_tick(srv);
                    }
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(player->varps[varp_rm] == 6,
                                   "OPNPCU notes on Sedridor should complete "
                                   "runemysteries=6, got %d",
                                   player->varps[varp_rm]);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_talisman) == 1,
                                   "completion should return the air talisman");
                    SELFTEST_CHECK(selftest_count_obj(player, obj_notes) == 0,
                                   "completion should consume the research notes");
                    SELFTEST_CHECK(varp_qp >= 0 && player->varps[varp_qp] > qp_before,
                                   "completion should award quest points, %d -> %d",
                                   qp_before,
                                   (varp_qp >= 0) ? player->varps[varp_qp] : -1);
                    SELFTEST_CHECK(player->hitpoints > 0 && player->godmode == 1,
                                   "player must stay alive (godmode) through the walk");
                    if( player->varps[varp_rm] == 6 )
                        fprintf(stderr,
                                "  PASS  opnpcu research_notes on sedridor (step 5->6)\n");
                }

                /* ---- lost-item replacement: package ---- */
                if( sed_slot >= 0 )
                {
                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        inv_set(player, s, -1, 0);
                    player->varps[varp_rm] = 3;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sedridor, -1,
                                                   sed_slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_package) == 1,
                                   "Sedridor should replace a lost research_package");
                    fprintf(stderr, "  PASS  lost research_package replacement\n");
                }

                /* ---- lost-item replacement: notes ---- */
                if( aub_slot >= 0 )
                {
                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        inv_set(player, s, -1, 0);
                    player->varps[varp_rm] = 5;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_aubury, -1,
                                                   aub_slot);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_notes) == 1,
                                   "Aubury should replace lost research_notes");
                    fprintf(stderr, "  PASS  lost research_notes replacement\n");
                }

                /* ---- lost-item replacement: talisman ---- */
                if( duke_slot >= 0 )
                {
                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        inv_set(player, s, -1, 0);
                    player->varps[varp_rm] = 1;
                    ToriRSServer_WorldTeleport(srv, 1, 3209, 3222);
                    selftest_tick(srv);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_duke, -1,
                                                   duke_slot);
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( player->active_script && chatmenu > 0 )
                    {
                        player->last_slot = 1;
                        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
                    }
                    biohazard_run_dialogue(srv, player, chatmenu);
                    if( com_messagebox > 0 && player->active_script )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    biohazard_run_dialogue(srv, player, 0);
                    ToriRSServer_WorldCloseModal(srv);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_talisman) == 1,
                                   "Duke should replace a lost air_talisman");
                    fprintf(stderr, "  PASS  lost air_talisman replacement\n");
                }

                /* ---- ::complete twice: first sets, second is a no-op ---- */
                {
                    static const uint8_t complete_cmd[] = "complete quest_runemysteries\n";
                    int qp_after_first;

                    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                        inv_set(player, s, -1, 0);
                    player->varps[varp_rm] = 0;
                    handle_cheat(srv, complete_cmd, (int)sizeof(complete_cmd) - 1);
                    SELFTEST_CHECK(player->varps[varp_rm] == 6,
                                   "::complete quest_runemysteries should write 6, got %d",
                                   player->varps[varp_rm]);
                    qp_after_first = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
                    handle_cheat(srv, complete_cmd, (int)sizeof(complete_cmd) - 1);
                    SELFTEST_CHECK(player->varps[varp_rm] == 6 && varp_qp >= 0 &&
                                       player->varps[varp_qp] == qp_after_first,
                                   "second ::complete must be a no-op, state %d qp %d->%d",
                                   player->varps[varp_rm], qp_after_first,
                                   (varp_qp >= 0) ? player->varps[varp_qp] : -1);
                    fprintf(stderr, "  PASS  ::complete quest_runemysteries twice\n");
                }

                if( duke_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, duke_slot);
                if( sed_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, sed_slot);
                if( aub_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, aub_slot);
                ToriRSServer_WorldNpcReap(srv);
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                player->varps[varp_rm] = 0;
                player->godmode = 1;
            }
        }
    }
