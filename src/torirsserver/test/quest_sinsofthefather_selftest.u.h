/* Sins of the Father Gate D stanza. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned npcs cannot leak into later
 * RNG-gated checks.
 *
 * Gate: TORIRSSERVER_SELFTEST_SINS_ONLY=1
 *       TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0
 *
 * Player is unkillable unless the test is a death case.
 */
static void
sins_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SINS PASS: %s\n", step);
}

static void
sins_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
sins_drain(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    int round;

    assert(srv);
    player = srv->active_player;
    assert(player);
    for( round = 0; round < 80 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            if( player->resume_button_count <= 0 )
                break;
            if( !ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]) )
                break;
        }
        else if( exec == SSVM_SUSPENDED || exec == SSVM_NPC_SUSPENDED ||
                 exec == SSVM_WORLD_SUSPENDED )
        {
            selftest_tick(srv);
        }
        else
        {
            break;
        }
    }
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static int
sins_capture_has(
    struct ToriRSServer* srv,
    struct ToriRSServerCapture* cap,
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

        if( text && strstr(text, needle) )
            return 1;
    }
    return 0;
}

static void
selftest_quest_sinsofthefather(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int vb_myq5;
    int vb_myq4;
    int vp_vampire;
    int obj_flail;
    int obj_medal;
    int obj_lamp;
    static struct ToriRSServerCapture cap;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Sins of the Father\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    sins_god(player);

    vb_myq5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "myq5");
    vb_myq4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "myq4");
    vp_vampire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "vampire");
    obj_flail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blisterwood_flail");
    obj_medal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "drakans_medallion");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");

    SELFTEST_CHECK(vb_myq5 > 0, "myq5 varbit is in the pack");
    SELFTEST_CHECK(vb_myq4 > 0, "myq4 varbit is in the pack");
    SELFTEST_CHECK(vp_vampire > 0, "vampire varp is in the pack");
    SELFTEST_CHECK(obj_flail > 0, "blisterwood_flail is in the pack");
    SELFTEST_CHECK(obj_medal > 0, "drakans_medallion is in the pack");
    SELFTEST_CHECK(obj_lamp > 0, "thosf_reward_lamp is in the pack");
    if( vb_myq5 <= 0 )
        return;

    ToriRSServer_CaptureBegin(srv, &cap);
    ToriRSServer_ScriptsRunDebugproc(srv, "softrun");
    ToriRSServer_CaptureEnd(srv);
    sins_drain(srv);
    SELFTEST_CHECK(sins_capture_has(srv, &cap, "softrun OK"),
                   "::softrun should reach its OK line");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_myq5) == 138,
                   "softrun writes %%myq5 = ^sf_complete (138), got %d",
                   ToriRSServer_VarbitGet(player, vb_myq5));
    SELFTEST_CHECK(selftest_count(player, obj_flail) >= 1,
                   "complete grants the Blisterwood flail");
    SELFTEST_CHECK(selftest_count(player, obj_medal) >= 1,
                   "complete grants Drakan's medallion");
    SELFTEST_CHECK(selftest_count(player, obj_lamp) >= 6,
                   "complete grants six tomes of experience");
    sins_pass("softrun complete 138");

    ToriRSServer_ScriptsRunDebugproc(srv, "sinsofthefather");
    sins_drain(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_myq5) == 0,
                   "::sinsofthefather resets to not started, got %d",
                   ToriRSServer_VarbitGet(player, vb_myq5));
    sins_pass("sinsofthefather reset");

    if( vb_myq4 > 0 )
        ToriRSServer_VarbitSet(srv, vb_myq4, 0);
    ToriRSServer_CaptureBegin(srv, &cap);
    ToriRSServer_ScriptsRunDebugproc(srv, "sf_qualify_why");
    ToriRSServer_CaptureEnd(srv);
    sins_drain(srv);
    SELFTEST_CHECK(sins_capture_has(srv, &cap, "sf_qualify=1"),
                   "missing Taste of Hope is qualify reason 1");
    sins_pass("qualify fail Taste of Hope");

    if( vb_myq4 > 0 )
        ToriRSServer_VarbitSet(srv, vb_myq4, 165);
    if( vp_vampire > 0 )
        player->varps[vp_vampire] = 0;
    ToriRSServer_CaptureBegin(srv, &cap);
    ToriRSServer_ScriptsRunDebugproc(srv, "sf_qualify_why");
    ToriRSServer_CaptureEnd(srv);
    sins_drain(srv);
    SELFTEST_CHECK(sins_capture_has(srv, &cap, "sf_qualify=2"),
                   "missing Vampyre Slayer is qualify reason 2");
    sins_pass("qualify fail Vampyre Slayer");

    ToriRSServer_ScriptsRunDebugproc(srv, "sf_cwalk_ready");
    sins_drain(srv);
    ToriRSServer_CaptureBegin(srv, &cap);
    ToriRSServer_ScriptsRunDebugproc(srv, "sf_qualify_why");
    ToriRSServer_CaptureEnd(srv);
    sins_drain(srv);
    SELFTEST_CHECK(sins_capture_has(srv, &cap, "sf_qualify=0"),
                   "ready skills and prereqs clear qualify");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_myq5) == 0,
                   "ready does not auto-start the quest, myq5=%d",
                   ToriRSServer_VarbitGet(player, vb_myq5));
    sins_pass("qualify ready does not auto-start");

    ToriRSServer_ScriptsRunDebugproc(srv, "sfbmp_08_veliaf_offer_p_choice2");
    sins_drain(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_myq5) == 0,
                   "p_choice2 offer does not write %%myq5, got %d",
                   ToriRSServer_VarbitGet(player, vb_myq5));
    sins_pass("offer Yes/Not now does not auto-start");

    ToriRSServer_ScriptsRunDebugproc(srv, "sfbmp_01_qualify_fail_toh");
    sins_drain(srv);
    ToriRSServer_ScriptsRunDebugproc(srv, "sfbmp_journal_00_not_started");
    sins_drain(srv);
    ToriRSServer_ScriptsRunDebugproc(srv, "sfbmp_journal_138_complete");
    sins_drain(srv);
    ToriRSServer_ScriptsRunDebugproc(srv, "sfbmp_leftover_full_refuse_trees");
    sins_drain(srv);
    ToriRSServer_ScriptsRunDebugproc(srv, "sfbmp_83_complete_scroll");
    sins_drain(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_myq5) == 138,
                   "complete scroll writes %%myq5 = 138, got %d",
                   ToriRSServer_VarbitGet(player, vb_myq5));
    sins_pass("journal leftover and complete scroll");
}
