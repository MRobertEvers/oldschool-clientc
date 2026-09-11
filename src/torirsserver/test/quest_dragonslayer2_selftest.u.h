/* Dragon Slayer II C-walk. Not MM2 / MISTMYST / COK / BV / QOT / DOD / CC.
 * Gate: TORIRSSERVER_SELFTEST_DS2_ONLY=1
 * Godmode on for the whole walk. Plugins stay off (TORIRS_PLUGINS=0).
 */
static void
ds2_selftest_clear(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    assert(srv->active_player);
    srv->active_player->active_script = NULL;
}

static int
ds2_selftest_varbit(struct ToriRSServerPlayer* player, const char* name)
{
    int id;

    assert(player);
    assert(name);
    id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    assert(id >= 0);
    return ToriRSServer_VarbitGet(player, id);
}

static void
selftest_quest_dragonslayer2(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    static struct ToriRSServerCapture cap;
    int loaded;
    int qp;
    int ds2_bit;
    int said_pass;
    int said_qp_gate;
    int said_ready;
    int said_refuse;
    int said_accept;
    int i;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Dragon Slayer II C-walk\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "DS2 C-walk loads a compiled script pack");
    if( !loaded )
        return;

    player->godmode = 1;
    ToriRSServer_WorldSetActive(srv, player);

    ds2_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ds2");
    qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    SELFTEST_CHECK(ds2_bit >= 0, "ds2 varbit must resolve");
    SELFTEST_CHECK(qp >= 0, "qp varp must resolve");
    if( ds2_bit < 0 || qp < 0 )
        return;

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_01_qualify_fail_qp") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::ds2bmp_01_qualify_fail_qp should reach content");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ds2_selftest_varbit(player, "ds2") == 0,
                   "qualify-fail qp must not start the quest, ds2=%d",
                   ds2_selftest_varbit(player, "ds2"));

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_02_qualify_fail_ds1") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::ds2bmp_02_qualify_fail_ds1 should reach content");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_10_qualify_fail_magic") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::ds2bmp_10_qualify_fail_magic should reach content");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_18_alec_offer_p_choice2") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::ds2bmp_18_alec_offer_p_choice2 should park on Yes/Not now");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_19_alec_refuse") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::ds2bmp_19_alec_refuse should reach content");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ds2_selftest_varbit(player, "ds2") == 0,
                   "refuse path must leave ds2 at 0, got %d",
                   ds2_selftest_varbit(player, "ds2"));

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_20_alec_accept") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::ds2bmp_20_alec_accept should reach content");
    ds2_selftest_clear(srv);

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_leftover_map_rotation_if") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "leftover_map_rotation_if is authored");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_leftover_ship_defense_minigame_play") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "leftover_ship_defense_minigame_play is authored");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_leftover_galvek_tile_hazards") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "leftover_galvek_tile_hazards is authored");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_leftover_dining_room_cutscene") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "leftover_dining_room_cutscene is authored");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_leftover_lamp_rub_ui") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "leftover_lamp_rub_ui is authored");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_leftover_full_refuse_trees") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "leftover_full_refuse_trees is authored");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_leftover_shayzien_riddle_random") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "leftover_shayzien_riddle_random is authored");
    ds2_selftest_clear(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_leftover_vorkath_lab_chase") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "leftover_vorkath_lab_chase is authored");
    ds2_selftest_clear(srv);

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2bmp_journal_10_complete") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "complete journal should open");
    ds2_selftest_clear(srv);

    said_pass = 0;
    said_qp_gate = 0;
    said_ready = 0;
    said_refuse = 0;
    said_accept = 0;
    ToriRSServer_CaptureBegin(srv, &cap);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "ds2cwalk") == TORIRSSERVER_TRIGGER_RAN,
                   "::ds2cwalk should reach content");
    ToriRSServer_CaptureEnd(srv);
    for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const char* text = selftest_message_text(srv, &cap.packets[i]);

        if( !text )
            continue;
        if( strstr(text, "DS2CWALK") == NULL )
            continue;
        fprintf(stderr, "  %s\n", text);
        if( strstr(text, "DS2CWALK qp_gate") != NULL )
            said_qp_gate = 1;
        if( strstr(text, "DS2CWALK ready") != NULL )
            said_ready = 1;
        if( strstr(text, "DS2CWALK refuse_holds") != NULL )
            said_refuse = 1;
        if( strstr(text, "DS2CWALK accept") != NULL )
            said_accept = 1;
        if( strstr(text, "DS2CWALK PASS") != NULL )
            said_pass = 1;
        if( strstr(text, "DS2CWALK FAIL") != NULL )
            SELFTEST_CHECK(0, "ds2cwalk reported FAIL: %s", text);
    }
    SELFTEST_CHECK(said_qp_gate, "ds2cwalk should hit the 200 QP gate");
    SELFTEST_CHECK(said_ready, "ds2cwalk should report ready after prereqs");
    SELFTEST_CHECK(said_refuse, "ds2cwalk should prove refuse does not auto-start");
    SELFTEST_CHECK(said_accept, "ds2cwalk should accept to ^ds2_dallas");
    SELFTEST_CHECK(said_pass, "ds2cwalk should reach PASS at endstate 215");
    SELFTEST_CHECK(ds2_selftest_varbit(player, "ds2") == 215,
                   "ds2 endstate must be 215, got %d",
                   ds2_selftest_varbit(player, "ds2"));
    SELFTEST_CHECK(player->varps[qp] >= 200,
                   "complete awards quest points, qp=%d", player->varps[qp]);

    ds2_selftest_clear(srv);
}
