/* Client of Kourend C walk. Invoked immediately before the shop
 * `selftest_reset_world` so any world side-effects are wiped. Also the
 * body of TORIRSSERVER_SELFTEST_COK_ONLY=1.
 *
 * Player stays unkillable (`godmode = 1`). Plugins are off on the C
 * binary; TORIRS_PLUGINS=0 is still the client-side contract.
 */
static void
selftest_quest_clientofkourend(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int vb_progress;
    int vb_cluequest;
    int obj_memoirs;
    int obj_lamp;
    int varp_qp;
    int want_complete;
    int want_lamps;
    int qp_after;
    static struct ToriRSServerCapture capture;
    int said_ok = 0;
    int said_fail = 0;
    int pass_qualify = 0;
    int pass_refuse = 0;
    int pass_accept = 0;
    int pass_quill = 0;
    int pass_houses = 0;
    int pass_altar = 0;
    int pass_journal = 0;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Client of Kourend (::cokrun)\n");

    player->godmode = 1;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    vb_progress = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "veos_progress");
    vb_cluequest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "cluequest");
    obj_memoirs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "veos_kharedsts_memoirs");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    want_complete = ToriRSServer_ContentConstantInt("cok_complete", 7);
    want_lamps = ToriRSServer_ContentConstantInt("cok_lamp_count", 2);

    SELFTEST_CHECK(vb_progress >= 0, "veos_progress varbit should resolve");
    SELFTEST_CHECK(vb_cluequest >= 0, "cluequest varbit should resolve");
    SELFTEST_CHECK(obj_memoirs >= 0, "veos_kharedsts_memoirs should resolve");
    SELFTEST_CHECK(obj_lamp >= 0, "thosf_reward_lamp should resolve");
    SELFTEST_CHECK(varp_qp >= 0, "qp varp should resolve");
    SELFTEST_CHECK(want_complete == 7, "cok_complete should be 7, got %d", want_complete);
    SELFTEST_CHECK(want_lamps == 2, "cok_lamp_count should be 2, got %d", want_lamps);

    ToriRSServer_CaptureBegin(srv, &capture);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "cokrun") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::cokrun should reach content");
    ToriRSServer_CaptureEnd(srv);

    for( int i = ToriRSServer_CaptureFindNamed(&capture, PKT_NAME_MESSAGE_GAME, 0);
         i >= 0;
         i = ToriRSServer_CaptureFindNamed(&capture, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const struct ToriRSServerCapturedPacket* packet = &capture.packets[i];
        const char* text;

        text = selftest_message_text(srv, packet);
        if( !text )
            continue;
        if( strstr(text, "COKRUN") == NULL )
            continue;
        fprintf(stderr, "  %s\n", text);
        if( strstr(text, "COKRUN OK") != NULL )
            said_ok = 1;
        if( strstr(text, "COKRUN FAIL") != NULL )
            said_fail = 1;
        if( strstr(text, "COKRUN PASS: X Marks") != NULL )
            pass_qualify = 1;
        if( strstr(text, "COKRUN PASS: refuse") != NULL )
            pass_refuse = 1;
        if( strstr(text, "COKRUN PASS: accept") != NULL )
            pass_accept = 1;
        if( strstr(text, "COKRUN PASS: feather") != NULL )
            pass_quill = 1;
        if( strstr(text, "COKRUN PASS: all five house") != NULL )
            pass_houses = 1;
        if( strstr(text, "COKRUN PASS: Dark Altar") != NULL )
            pass_altar = 1;
        if( strstr(text, "COKRUN PASS: journal complete") != NULL )
            pass_journal = 1;
    }

    SELFTEST_CHECK(!said_fail, "::cokrun should report no failures");
    SELFTEST_CHECK(said_ok, "::cokrun should reach its OK line");
    SELFTEST_CHECK(pass_qualify, "::cokrun should pass the X Marks qualify-fail");
    SELFTEST_CHECK(pass_refuse, "::cokrun should pass the refuse no-start");
    SELFTEST_CHECK(pass_accept, "::cokrun should pass accept / scroll");
    SELFTEST_CHECK(pass_quill, "::cokrun should pass the enchanted quill");
    SELFTEST_CHECK(pass_houses, "::cokrun should pass the five house interviews");
    SELFTEST_CHECK(pass_altar, "::cokrun should pass Dark Altar orb activate");
    SELFTEST_CHECK(pass_journal, "::cokrun should pass journal QUEST COMPLETE");

    if( vb_progress >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) >= want_complete,
                       "%%veos_progress should be complete (%d), got %d",
                       want_complete,
                       ToriRSServer_VarbitGet(player, vb_progress));
    if( obj_memoirs >= 0 )
        SELFTEST_CHECK(selftest_count(player, obj_memoirs) >= 1,
                       "Kharedst's memoirs should be awarded, have %d",
                       selftest_count(player, obj_memoirs));
    if( obj_lamp >= 0 )
        SELFTEST_CHECK(selftest_count(player, obj_lamp) >= want_lamps,
                       "should award %d antique lamps, have %d",
                       want_lamps,
                       selftest_count(player, obj_lamp));
    qp_after = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
    SELFTEST_CHECK(qp_after >= 1, "quest points should include Client of Kourend, qp=%d",
                   qp_after);

    /* Qualify-fail is a hard no-start: drop X Marks and the start proc
     * must not write %veos_progress. */
    if( vb_progress >= 0 && vb_cluequest >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_progress, 0);
        ToriRSServer_VarbitSet(srv, vb_cluequest, 0);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,cok_show_qualify_fail]", NULL, 0),
                       "~cok_show_qualify_fail should run");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_progress) == 0,
                       "qualify-fail must leave %%veos_progress at 0, got %d",
                       ToriRSServer_VarbitGet(player, vb_progress));
    }

    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    ToriRSServer_ScriptsFree(srv);
}
