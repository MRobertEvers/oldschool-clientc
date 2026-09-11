/* Secrets of the North Gate D stanza. Included from
 * torirs_server_world_selftest.c. The function is called immediately before
 * the shop `selftest_reset_world` so spawned npcs cannot leak into later
 * RNG-gated checks. Parent will not merge this .u.h onto v3.
 *
 * Qualify is authored (QoT pattern, stat_base). Offer is p_choice2 Yes /
 * Not now. Refuse must not write %sotn. Player stays in godmode.
 */
static void
sotn_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SOTN PASS: %s\n", step);
}

static void
sotn_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
sotn_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int max_pages)
{
    int chatmenu;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    biohazard_run_dialogue(srv, player, chatmenu);
    if( player->active_script != NULL && max_pages > 0 )
        selftest_click_through(srv, max_pages);
    sotn_close(srv);
}

static void
sotn_choose_row(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int rows_uid;
    uint8_t button[6];
    int chatmenu;

    assert(srv);
    assert(player);
    assert(row > 0);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    biohazard_run_dialogue(srv, player, chatmenu);
    rows_uid = chatmenu;
    if( rows_uid <= 0 || player->active_script == NULL )
        return;
    button[0] = (uint8_t)(rows_uid >> 24);
    button[1] = (uint8_t)(rows_uid >> 16);
    button[2] = (uint8_t)(rows_uid >> 8);
    button[3] = (uint8_t)rows_uid;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    sotn_drain(srv, player, 8);
}

static void
selftest_quest_secretsofthenorth(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int32_t why;
    int npc_slot;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Secrets of the North\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "guard_carnillean_vis");
        int varbit_sotn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "sotn");
        int varbit_my2arm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "my2arm_status");
        int varp_hazeel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hazeelcultquest");
        int stat_hunter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
        int stat_thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
        int stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");

        SELFTEST_CHECK(npc_guard >= 0 && varbit_sotn >= 0 && varbit_my2arm >= 0 &&
                           varp_hazeel >= 0 && stat_hunter >= 0 && stat_thieving >= 0 &&
                           stat_agility >= 0,
                       "Secrets of the North C-side names should all resolve");
        if( npc_guard < 0 || varbit_sotn < 0 || varbit_my2arm < 0 || varp_hazeel < 0 ||
            stat_hunter < 0 || stat_thieving < 0 || stat_agility < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        player->godmode = 1;
        player->dying = 0;
        if( player->max_hitpoints > 0 )
            player->hitpoints = player->max_hitpoints;
        ToriRSServer_CombatSyncHitpoints(player);

        ToriRSServer_VarbitSet(srv, varbit_sotn, 0);
        ToriRSServer_VarbitSet(srv, varbit_my2arm, 0);
        ToriRSServer_WorldSetVarp(srv, varp_hazeel, 0);
        ToriRSServer_CombatSetLevel(player, stat_hunter, 1);
        ToriRSServer_CombatSetLevel(player, stat_thieving, 1);
        ToriRSServer_CombatSetLevel(player, stat_agility, 1);

        why = -1;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,sn_qualify_fail_reason]", NULL, 0,
                                                      &why) == 1,
                       "sn_qualify_fail_reason should answer");
        SELFTEST_CHECK(why == 1,
                       "qualify fails on Making Friends with My Arm first, got %d", why);
        sotn_pass("qualify_fail_mfwma");

        ToriRSServer_VarbitSet(srv, varbit_my2arm, 200);
        why = -1;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,sn_qualify_fail_reason]", NULL, 0,
                                                      &why) == 1,
                       "sn_qualify_fail_reason should answer after MFWMA");
        SELFTEST_CHECK(why == 2, "qualify fails on Hazeel Cult second, got %d", why);
        sotn_pass("qualify_fail_hazeelcult");

        ToriRSServer_WorldSetVarp(srv, varp_hazeel, 9);
        why = -1;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,sn_qualify_fail_reason]", NULL, 0,
                                                      &why) == 1,
                       "sn_qualify_fail_reason should answer after Hazeel Cult");
        SELFTEST_CHECK(why == 3, "qualify fails on Hunter 69, got %d", why);
        sotn_pass("qualify_fail_hunter");

        ToriRSServer_CombatSetLevel(player, stat_hunter, 69);
        why = -1;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,sn_qualify_fail_reason]", NULL, 0,
                                                      &why) == 1,
                       "sn_qualify_fail_reason should answer after Hunter");
        SELFTEST_CHECK(why == 4, "qualify fails on Thieving 64, got %d", why);
        sotn_pass("qualify_fail_thieving");

        ToriRSServer_CombatSetLevel(player, stat_thieving, 64);
        why = -1;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,sn_qualify_fail_reason]", NULL, 0,
                                                      &why) == 1,
                       "sn_qualify_fail_reason should answer after Thieving");
        SELFTEST_CHECK(why == 5, "qualify fails on Agility 56, got %d", why);
        sotn_pass("qualify_fail_agility");

        ToriRSServer_CombatSetLevel(player, stat_agility, 56);
        why = -1;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,sn_qualify_fail_reason]", NULL, 0,
                                                      &why) == 1,
                       "sn_qualify_fail_reason should answer with all gates");
        SELFTEST_CHECK(why == 0, "qualify passes with MFWMA + Hazeel + skills, got %d", why);
        sotn_pass("qualify_pass");

        ToriRSServer_WorldTeleport(srv, 0, 2570, 3276);
        selftest_tick(srv);
        npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_guard, 2570, 3276, 0);
        SELFTEST_CHECK(npc_slot >= 0, "guard_carnillean_vis should spawn for the start talk");
        if( npc_slot >= 0 )
        {
            ToriRSServer_VarbitSet(srv, varbit_sotn, 0);
            player->chatmodal_group = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, npc_slot);
            sotn_choose_row(srv, player, 2);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, varbit_sotn) == 0,
                           "refusing Yes/Not now must not write %%sotn, got %d",
                           ToriRSServer_VarbitGet(player, varbit_sotn));
            sotn_pass("guard_refuse_no_write");

            ToriRSServer_VarbitSet(srv, varbit_sotn, 0);
            player->chatmodal_group = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, npc_slot);
            sotn_choose_row(srv, player, 1);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, varbit_sotn) == 4,
                           "accepting Yes must write %%sotn=^sn_upstairs (4), got %d",
                           ToriRSServer_VarbitGet(player, varbit_sotn));
            sotn_pass("guard_accept_writes_upstairs");

            ToriRSServer_VarbitSet(srv, varbit_my2arm, 0);
            ToriRSServer_VarbitSet(srv, varbit_sotn, 0);
            player->chatmodal_group = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, npc_slot);
            sotn_drain(srv, player, 8);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, varbit_sotn) == 0,
                           "qualify-fail OPNPC1 must not write %%sotn, got %d",
                           ToriRSServer_VarbitGet(player, varbit_sotn));
            sotn_pass("qualify_before_start");

            ToriRSServer_WorldNpcFree(srv, npc_slot);
            ToriRSServer_WorldNpcReap(srv);
        }

        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_crime_scene_explain_matrix]",
                                                   NULL, 0) == 1,
                       "leftover_crime_scene_explain_matrix should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_hunter_trail]", NULL, 0) == 1,
                       "leftover_hunter_trail should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_evelot_fight]", NULL, 0) == 1,
                       "leftover_evelot_fight should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_hazeel_cult_qa]", NULL, 0) ==
                           1,
                       "leftover_hazeel_cult_qa should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_carnillean_chest_lockpick]",
                                                   NULL, 0) == 1,
                       "leftover_carnillean_chest_lockpick should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_ghorrock_brazier_puzzle]",
                                                   NULL, 0) == 1,
                       "leftover_ghorrock_brazier_puzzle should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_muspah_fight]", NULL, 0) == 1,
                       "leftover_muspah_fight should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_full_refuse_trees]", NULL,
                                                   0) == 1,
                       "leftover_full_refuse_trees should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_snowflake_questions]", NULL,
                                                   0) == 1,
                       "leftover_snowflake_questions should exist");
        sotn_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,sn_leftover_assassin_fight]", NULL, 0) ==
                           1,
                       "leftover_assassin_fight should exist");
        sotn_close(srv);
        sotn_pass("leftover_procs");

        ToriRSServer_VarbitSet(srv, varbit_sotn, 0);
        ToriRSServer_VarbitSet(srv, varbit_my2arm, 200);
        ToriRSServer_WorldSetVarp(srv, varp_hazeel, 9);
        ToriRSServer_CombatSetLevel(player, stat_hunter, 69);
        ToriRSServer_CombatSetLevel(player, stat_thieving, 64);
        ToriRSServer_CombatSetLevel(player, stat_agility, 56);
        {
            static struct ToriRSServerCapture snrun_capture;
            int snrun_said_ok = 0;

            ToriRSServer_CaptureBegin(srv, &snrun_capture);
            ToriRSServer_ScriptsRunDebugproc(srv, "snrun");
            ToriRSServer_CaptureEnd(srv);
            for( int i = ToriRSServer_CaptureFindNamed(&snrun_capture, PKT_NAME_MESSAGE_GAME, 0);
                 i >= 0;
                 i = ToriRSServer_CaptureFindNamed(&snrun_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
            {
                const struct ToriRSServerCapturedPacket* packet = &snrun_capture.packets[i];
                const char* text;

                text = selftest_message_text(srv, packet);
                if( !text )
                    continue;
                if( strstr(text, "snrun") == NULL )
                    continue;
                fprintf(stderr, "  %s\n", text);
                if( strstr(text, "snrun OK") != NULL )
                    snrun_said_ok = 1;
            }
            SELFTEST_CHECK(snrun_said_ok, "::snrun should reach its OK line");
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, varbit_sotn) == 90,
                           "::snrun should leave %%sotn at ^sn_complete (90), got %d",
                           ToriRSServer_VarbitGet(player, varbit_sotn));
            sotn_pass("snrun_complete");
            {
                int drain_tick;

                ToriRSServer_ScriptsProcessQueues(srv);
                for( drain_tick = 0; drain_tick < 40; drain_tick++ )
                {
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                }
            }
        }

        ToriRSServer_VarbitSet(srv, varbit_sotn, 0);
        player->godmode = 1;
        player->dying = 0;
    }
    ToriRSServer_ScriptsFree(srv);
}
