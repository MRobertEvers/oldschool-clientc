/* X Marks the Spot -- worker-branch C-walk.
 *
 * Gated on TORIRSSERVER_SELFTEST_XMARKS_ONLY=1 so the default suite is
 * unchanged. Player is unkillable for the whole walk. Required pointers
 * are asserted; allocation failure is assert; one assert per condition.
 *
 *   TORIRSSERVER_SELFTEST_XMARKS_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0 \
 *       ./src/<objdir>_opt/torirsserver --selftest
 */
static int
selftest_xmarks_count_obj(const struct ToriRSServerPlayer* player, int obj_id)
{
    int total;

    assert(player);
    total = 0;
    for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            total += player->inv[i].count;
    return total;
}

static int
selftest_xmarks_saw_text(struct ToriRSServer* srv,
                         const struct ToriRSServerCapture* capture,
                         const char* needle)
{
    assert(srv);
    assert(capture);
    assert(needle);
    for( int i = ToriRSServer_CaptureFindNamed(capture, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(capture, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const char* text = selftest_message_text(srv, &capture->packets[i]);

        if( text && strstr(text, needle) != NULL )
            return 1;
    }
    return 0;
}

static void
selftest_xmarks(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int cluequest;
    int coins;
    int lamp;
    int beginner;
    int complete_state;
    static struct ToriRSServerCapture refuse_capture;
    static struct ToriRSServerCapture run_capture;
    int refuse_kept_zero;
    int run_ok;

    assert(srv);
    assert(player);

    player->godmode = 1;

    fprintf(stderr, "ToriRSServer selftest: X Marks the Spot walk\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "X Marks the Spot walk loads a compiled script pack");
    if( !loaded )
        return;

    cluequest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "cluequest");
    coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cluequest_lamp");
    beginner = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trail_clue_beginner");
    SELFTEST_CHECK(cluequest >= 0, "cache names varbit cluequest");
    SELFTEST_CHECK(coins >= 0, "cache names obj coins");
    SELFTEST_CHECK(lamp >= 0, "cache names obj cluequest_lamp");
    SELFTEST_CHECK(beginner >= 0, "cache names obj trail_clue_beginner");
    if( cluequest < 0 || coins < 0 || lamp < 0 || beginner < 0 )
        return;

    selftest_reset_world(srv, player, 3228, 3242);

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "xmarksthespot") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::xmarksthespot should reach content");
    ToriRSServer_ScriptsProcessQueues(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, cluequest) == 0,
                   "reset leaves %%cluequest at not-started, got %d",
                   ToriRSServer_VarbitGet(player, cluequest));

    ToriRSServer_CaptureBegin(srv, &refuse_capture);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "xmarksrefuse") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::xmarksrefuse should reach content");
    ToriRSServer_CaptureEnd(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    if( srv->active_player )
        srv->active_player->active_script = NULL;
    refuse_kept_zero = ToriRSServer_VarbitGet(player, cluequest) == 0;
    SELFTEST_CHECK(refuse_kept_zero,
                   "refuse must not start the quest, %%cluequest=%d",
                   ToriRSServer_VarbitGet(player, cluequest));

    ToriRSServer_CaptureBegin(srv, &run_capture);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "xmarksrun") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::xmarksrun should reach content");
    ToriRSServer_CaptureEnd(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    if( srv->active_player )
        srv->active_player->active_script = NULL;

    complete_state = ToriRSServer_VarbitGet(player, cluequest);
    run_ok = selftest_xmarks_saw_text(srv, &run_capture, "xmarksrun OK");
    SELFTEST_CHECK(run_ok, "::xmarksrun should reach its OK line");
    SELFTEST_CHECK(complete_state == 8,
                   "xmarksrun writes ^xmarks_complete=8, got %d",
                   complete_state);
    SELFTEST_CHECK(selftest_xmarks_count_obj(player, lamp) > 0,
                   "completion awards cluequest_lamp");
    SELFTEST_CHECK(selftest_xmarks_count_obj(player, beginner) > 0,
                   "completion awards trail_clue_beginner");
    SELFTEST_CHECK(selftest_xmarks_count_obj(player, coins) >= 200,
                   "completion awards 200 coins, got %d",
                   selftest_xmarks_count_obj(player, coins));

    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    if( srv->active_player )
        srv->active_player->active_script = NULL;
}
