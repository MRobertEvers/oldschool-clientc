#ifndef QUEST_TWILIGHTSPROMISE_SELFTEST_U_H
#define QUEST_TWILIGHTSPROMISE_SELFTEST_U_H

/*
 * Twilight's Promise Gate D C-walk. Player stays unkillable. Qualify
 * (Children of the Sun) runs before %vmq2 is written. Yes/No offer is
 * real p_choice2. Refuse must not write %vmq2. Furia starts the same
 * quest as Ennius. Keep ::twilightspromise / ::tprun names.
 *
 * Run focused:
 *   TORIRSSERVER_SELFTEST_TWP_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0 \
 *       TORIRSSERVER_CACHE=cache.osrs239 ./src/<obj>_opt/torirsserver --selftest
 */

static int
twp_selftest_said(
    struct ToriRSServer* srv,
    const struct ToriRSServerCapture* cap,
    const char* needle)
{
    int i;

    assert(srv);
    assert(cap);
    assert(needle);
    for( i = ToriRSServer_CaptureFindNamed(cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(cap, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const char* text = selftest_message_text(srv, &cap->packets[i]);

        if( text && strstr(text, needle) != NULL )
            return 1;
    }
    return 0;
}

static void
selftest_quest_twilightspromise(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int vb_vmq1;
    int vb_vmq2;
    static struct ToriRSServerCapture cap;

    assert(srv);
    assert(player);
    player->godmode = 1;

    vb_vmq1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1");
    vb_vmq2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq2");
    SELFTEST_CHECK(vb_vmq1 > 0, "varbit vmq1 should resolve");
    SELFTEST_CHECK(vb_vmq2 > 0, "varbit vmq2 should resolve");
    if( vb_vmq1 <= 0 || vb_vmq2 <= 0 )
        return;

    /* Keep the authored debug names exactly. Do not ScriptsFree between
     * steps -- that unloads the pack and sets scripts_ok = 0. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "twilightspromise") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::twilightspromise should reach content");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                   "::twilightspromise should reset %%vmq2 to not_started, got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq2));

    ToriRSServer_CaptureBegin(srv, &cap);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "tpwalk") == TORIRSSERVER_TRIGGER_RAN,
                   "::tpwalk should reach content");
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(twp_selftest_said(srv, &cap, "TPWALK QUALIFY_COTS"),
                   "::tpwalk should name the Children of the Sun mesbox gate");
    SELFTEST_CHECK(twp_selftest_said(srv, &cap, "TPWALK QUALIFY_PASS"),
                   "::tpwalk should pass qualify once %%vmq1 is complete");
    SELFTEST_CHECK(twp_selftest_said(srv, &cap, "TPWALK REFUSE"),
                   "::tpwalk refuse must leave %%vmq2 unwritten");
    SELFTEST_CHECK(twp_selftest_said(srv, &cap, "TPWALK ACCEPT"),
                   "::tpwalk Yes should write %%vmq2 = ^tp_metzli");
    SELFTEST_CHECK(twp_selftest_said(srv, &cap, "TPWALK FURIA"),
                   "::tpwalk Furia Yes should write the same start state");
    SELFTEST_CHECK(twp_selftest_said(srv, &cap, "TPWALK OK"),
                   "::tpwalk should reach its OK line");
    ToriRSServer_WorldCloseModal(srv);

    /* Named BMP hooks must resolve. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "tpbmp_01_qualify_fail_cots") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::tpbmp_01_qualify_fail_cots should park on the CotS mesbox");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                   "the CotS qualify BMP must not write %%vmq2, got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq2));
    ToriRSServer_WorldCloseModal(srv);

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "tpbmp_02_ennius_offer_p_choice2") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::tpbmp_02_ennius_offer_p_choice2 should park on Yes/No");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                   "the Ennius offer BMP must not write %%vmq2, got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq2));

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "tpbmp_03_ennius_refuse") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::tpbmp_03_ennius_refuse should show the refuse chathead");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                   "the Ennius refuse BMP must not write %%vmq2, got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq2));

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "tpbmp_05_furia_offer_p_choice2") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::tpbmp_05_furia_offer_p_choice2 should park on Yes/No");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                   "the Furia offer BMP must not write %%vmq2, got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq2));

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "tpbmp_06_furia_refuse") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::tpbmp_06_furia_refuse should show the refuse chathead");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                   "the Furia refuse BMP must not write %%vmq2, got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq2));

    ToriRSServer_VarbitSet(srv, vb_vmq1, 24);
    ToriRSServer_VarbitSet(srv, vb_vmq2, 0);
    ToriRSServer_CaptureBegin(srv, &cap);
    ToriRSServer_ScriptsRunDebugproc(srv, "tprun");
    ToriRSServer_CaptureEnd(srv);
    selftest_click_through(srv, 16);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 50,
                   "::tprun should leave %%vmq2 = ^tp_complete (50), got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq2));
}

#endif /* QUEST_TWILIGHTSPROMISE_SELFTEST_U_H */
