/* The Ribbiting Tale of a Lily Pad Labour Dispute C-walk. Worker branch
 * only — parent will not merge this .u.h onto v3.
 *
 *   TORIRSSERVER_SELFTEST_RIBBIT_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0 \
 *       TORIRSSERVER_CACHE=cache.osrs239 ./src/build_opt/torirsserver --selftest
 *
 * Qualify (QoT pattern, stat_base) is checked before any %frog_quest write.
 * Marcellus p_choice2 strings stay
 * "I see... How about I head over there and take a look?" /
 * "I think I'm just going to go now."
 * Refuse must leave %frog_quest at 0. ::ribbitrun walks the authored
 * ladder to endstate 32.
 */
{
    int loaded;
    int frog_bit;
    int woodcutting;
    int32_t reason;
    int refuse_ok;
    int ribbitrun_ok;
    static struct ToriRSServerCapture refuse_capture;
    static struct ToriRSServerCapture ribbitrun_capture;

    fprintf(stderr, "ToriRSServer selftest: The Ribbiting Tale\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;

    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        if( getenv("TORIRSSERVER_SELFTEST_RIBBIT_ONLY") )
        {
            fprintf(stderr,
                    "ToriRSServer Ribbiting Tale selftest: %lu checks, %d failures\n",
                    g_selftest_checks,
                    g_selftest_failures);
            return g_selftest_failures;
        }
    }
    else
    {
        frog_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "frog_quest");
        woodcutting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
        SELFTEST_CHECK(frog_bit >= 0, "%%frog_quest varbit resolves");
        SELFTEST_CHECK(woodcutting >= 0, "woodcutting stat resolves");

        ToriRSServer_VarbitSet(srv, frog_bit, 0);
        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "frog_quest_patch_unlocked"),
            0);
        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1"),
            0);
        if( woodcutting >= 0 )
            ToriRSServer_CombatSetLevel(player, woodcutting, 1);

        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,ribbit_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 1,
            "qualify fails Children of the Sun first, got %d",
            reason);
        player->active_script = NULL;

        ToriRSServer_VarbitSet(
            srv,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1"),
            ToriRSServer_ContentConstantInt("cots_complete", 24));
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,ribbit_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 2,
            "qualify fails Woodcutting 15 second, got %d",
            reason);
        player->active_script = NULL;

        if( woodcutting >= 0 )
            ToriRSServer_CombatSetLevel(player, woodcutting, 15);
        reason = -1;
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunProcInt(
                srv, "[proc,ribbit_qualify_fail_reason]", NULL, 0, &reason) &&
                reason == 0,
            "qualify passes once every gate is met, got %d",
            reason);
        player->active_script = NULL;
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, frog_bit) == 0,
            "qualify-before-start does not write %%frog_quest, got %d",
            ToriRSServer_VarbitGet(player, frog_bit));

        ToriRSServer_VarbitSet(srv, frog_bit, 0);
        refuse_ok = 0;
        ToriRSServer_CaptureBegin(srv, &refuse_capture);
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunDebugproc(srv, "ribbitbmp_04_marcellus_refuse") ==
                TORIRSSERVER_TRIGGER_RAN,
            "::ribbitbmp_04_marcellus_refuse should reach content");
        ToriRSServer_CaptureEnd(srv);
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, frog_bit) == 0,
            "refuse does not write %%frog_quest, got %d",
            ToriRSServer_VarbitGet(player, frog_bit));
        refuse_ok = ToriRSServer_VarbitGet(player, frog_bit) == 0;
        SELFTEST_CHECK(refuse_ok, "Marcellus refuse leaves %%frog_quest at 0");

        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunDebugproc(srv, "ribbitingtale") ==
                TORIRSSERVER_TRIGGER_RAN,
            "::ribbitingtale should reach content");
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, frog_bit) == 0,
            "::ribbitingtale does not auto-start, got %d",
            ToriRSServer_VarbitGet(player, frog_bit));

        ribbitrun_ok = 0;
        ToriRSServer_CaptureBegin(srv, &ribbitrun_capture);
        ToriRSServer_ScriptsRunDebugproc(srv, "ribbitrun");
        ToriRSServer_CaptureEnd(srv);
        for( int i = ToriRSServer_CaptureFindNamed(&ribbitrun_capture, PKT_NAME_MESSAGE_GAME, 0);
             i >= 0;
             i = ToriRSServer_CaptureFindNamed(&ribbitrun_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const struct ToriRSServerCapturedPacket* packet = &ribbitrun_capture.packets[i];
            const char* text;

            text = selftest_message_text(srv, packet);
            if( !text )
                continue;
            if( strstr(text, "ribbitrun") == NULL )
                continue;
            fprintf(stderr, "  %s\n", text);
            if( strstr(text, "ribbitrun OK") != NULL )
                ribbitrun_ok = 1;
        }
        SELFTEST_CHECK(ribbitrun_ok, "::ribbitrun should reach its OK line");
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(player, frog_bit) ==
                ToriRSServer_ContentConstantInt("ribbit_complete", 32),
            "::ribbitrun writes endstate 32, got %d",
            ToriRSServer_VarbitGet(player, frog_bit));
        SELFTEST_CHECK(
            ToriRSServer_VarbitGet(
                player,
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "frog_quest_patch_unlocked")) == 1,
            "::ribbitrun unlocks the hardwood patch");

        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
    }

    if( getenv("TORIRSSERVER_SELFTEST_RIBBIT_ONLY") )
    {
        fprintf(stderr,
                "ToriRSServer Ribbiting Tale selftest: %lu checks, %d failures\n",
                g_selftest_checks,
                g_selftest_failures);
        return g_selftest_failures;
    }
}
