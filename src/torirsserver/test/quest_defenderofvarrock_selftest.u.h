/* Defender of Varrock C-walk. Worker branch only — parent will not merge
 * this .u.h onto v3.
 *
 *   TORIRSSERVER_SELFTEST_DOV_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0 \
 *       ./src/build_opt/torirsserver --selftest
 *
 * Qualify (QoT pattern, stat_base) is checked before any %dov write.
 * Elias p_choice2 strings stay "Yes." / "Not right now, sorry." then
 * "Ready when you are." / "I'm rather busy, sorry." Either refuse must
 * leave %dov at 0. ::dovrun walks the authored ladder to endstate 56.
 */
{
    int loaded;
    int dov_bit;
    int smithing;
    int hunter;
    int32_t reason;
    int refuse_ok;
    int dovrun_ok;
    static struct ToriRSServerCapture refuse_capture;
    static struct ToriRSServerCapture dovrun_capture;

    fprintf(stderr, "ToriRSServer selftest: Defender of Varrock\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;

    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        if( getenv("TORIRSSERVER_SELFTEST_DOV_ONLY") )
        {
            fprintf(stderr,
                    "ToriRSServer Defender of Varrock selftest: %lu checks, %d failures\n",
                    g_selftest_checks,
                    g_selftest_failures);
            return g_selftest_failures;
        }
    }
    else
    {
        dov_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "dov");
        smithing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
        hunter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
        SELFTEST_CHECK(dov_bit >= 0, "%%dov varbit resolves");
        SELFTEST_CHECK(smithing >= 0 && hunter >= 0, "smithing and hunter stats resolve");

        ToriRSServer_VarbitSet(srv, dov_bit, 0);
        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "phoenixgang"),
            0);
        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "blackarmgang"),
            0);
        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ikov"),
            0);
        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bim"),
            0);
        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "crestquest"),
            0);
        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_quest"),
            0);
        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "surok_quest"),
            0);
        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "rjquest"),
            0);
        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "demonstart"),
            0);
        if( smithing >= 0 )
            ToriRSServer_CombatSetLevel(player, smithing, 1);
        if( hunter >= 0 )
            ToriRSServer_CombatSetLevel(player, hunter, 1);

        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 1,
            "qualify fails Shield of Arrav first, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "phoenixgang"),
            ToriRSServer_ContentConstantInt("phoenixgang_complete", 10));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 2,
            "qualify fails Temple of Ikov second, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ikov"),
            ToriRSServer_ContentConstantInt("ikov_completed_armadyl", 80));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 3,
            "qualify fails Below Ice Mountain third, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bim"),
            ToriRSServer_ContentConstantInt("bim_complete", 45));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 4,
            "qualify fails Family Crest fourth, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "crestquest"),
            ToriRSServer_ContentConstantInt("crest_complete", 11));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 5,
            "qualify fails Garden of Tranquillity fifth, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_quest"),
            ToriRSServer_ContentConstantInt("garden_complete", 60));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 6,
            "qualify fails What Lies Below sixth, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "surok_quest"),
            ToriRSServer_ContentConstantInt("wlb_complete", 150));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 7,
            "qualify fails Romeo & Juliet seventh, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "rjquest"),
            ToriRSServer_ContentConstantInt("romeojuliet_complete", 100));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 8,
            "qualify fails Demon Slayer eighth, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_WorldSetVarp(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "demonstart"),
            ToriRSServer_ContentConstantInt("demon_complete", 30));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 9,
            "qualify fails Smithing 55 ninth, got %d",
            reason);
        player->active_script = NULL;

        if( smithing >= 0 )
            ToriRSServer_CombatSetLevel(player, smithing, 55);
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 10,
            "qualify fails Hunter 52 tenth, got %d",
            reason);
        player->active_script = NULL;

        if( hunter >= 0 )
            ToriRSServer_CombatSetLevel(player, hunter, 52);
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,dov_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 0,
            "qualify passes once every gate is met, got %d",
            reason);
        player->active_script = NULL;
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, dov_bit) == 0,
            "qualify-before-start does not write %%dov, got %d",
            ToriRSServer_VarbitGet(player, dov_bit));

        ToriRSServer_VarbitSet(srv, dov_bit, 0);
        refuse_ok = 0;
        ToriRSServer_CaptureBegin(srv, &refuse_capture);
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunDebugproc(srv, "dovbmp_13_elias_refuse") ==
                TORIRSSERVER_TRIGGER_RAN,
            "::dovbmp_13_elias_refuse should reach content");
        ToriRSServer_CaptureEnd(srv);
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, dov_bit) == 0,
            "first refuse does not write %%dov, got %d",
            ToriRSServer_VarbitGet(player, dov_bit));

        ToriRSServer_CaptureBegin(srv, &refuse_capture);
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunDebugproc(srv, "dovbmp_16_elias_busy_refuse") ==
                TORIRSSERVER_TRIGGER_RAN,
            "::dovbmp_16_elias_busy_refuse should reach content");
        ToriRSServer_CaptureEnd(srv);
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, dov_bit) == 0,
            "second refuse does not write %%dov, got %d",
            ToriRSServer_VarbitGet(player, dov_bit));
        refuse_ok = ToriRSServer_VarbitGet(player, dov_bit) == 0;
        SELFTEST_CHECK(refuse_ok, "both Elias refuse arms leave %%dov at 0");

        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunDebugproc(srv, "defenderofvarrock") ==
                TORIRSSERVER_TRIGGER_RAN,
            "::defenderofvarrock should reach content");
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, dov_bit) == 0,
            "::defenderofvarrock does not auto-start, got %d",
            ToriRSServer_VarbitGet(player, dov_bit));

        dovrun_ok = 0;
        ToriRSServer_CaptureBegin(srv, &dovrun_capture);
        ToriRSServer_ScriptsRunDebugproc(srv, "dovrun");
        ToriRSServer_CaptureEnd(srv);
        for( int i = ToriRSServer_CaptureFindNamed(&dovrun_capture, PKT_NAME_MESSAGE_GAME, 0);
             i >= 0;
             i = ToriRSServer_CaptureFindNamed(&dovrun_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const struct ToriRSServerCapturedPacket* packet = &dovrun_capture.packets[i];
            const char* text;

            text = selftest_message_text(srv, packet);
            if( !text )
                continue;
            if( strstr(text, "dovrun") == NULL )
                continue;
            fprintf(stderr, "  %s\n", text);
            if( strstr(text, "dovrun OK") != NULL )
                dovrun_ok = 1;
        }
        SELFTEST_CHECK(dovrun_ok, "::dovrun should reach its OK line");
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, dov_bit) ==
                ToriRSServer_ContentConstantInt("dov_complete", 56),
            "::dovrun writes endstate 56, got %d",
            ToriRSServer_VarbitGet(player, dov_bit));

        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
    }

    if( getenv("TORIRSSERVER_SELFTEST_DOV_ONLY") )
    {
        fprintf(stderr,
                "ToriRSServer Defender of Varrock selftest: %lu checks, %d failures\n",
                g_selftest_checks,
                g_selftest_failures);
        return g_selftest_failures;
    }
}
