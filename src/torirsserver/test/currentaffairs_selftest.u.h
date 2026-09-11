/*
 * Current Affairs focused C-walk. Included only from
 * torirs_server_world_selftest.c on this worker branch. Gate with
 * TORIRSSERVER_SELFTEST_CA_ONLY=1 immediately before the shop fprintf.
 * Unset CA_ONLY leaves the shop / default suite unmoved.
 *
 * Proves: qualify parks / first-missing mesbox; refuse picks row 2
 * (%current_affairs==0); accept writes 5; ::carun reaches 45. Godmode on.
 */
static int
ca_capture_has(struct ToriRSServer* srv, struct ToriRSServerCapture* cap, const char* needle)
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
ca_drain_until_choice_or_pages(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int max_pages)
{
    int chatmenu;
    int clicks;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    clicks = 0;
    while( clicks < max_pages && player->active_script != NULL )
    {
        int uid;
        uint8_t resume[6];

        if( player->resume_button_count <= 0 )
            break;
        uid = player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            return;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        clicks++;
    }
}

static void
ca_pick_choice_row(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int row)
{
    int rows_uid;
    uint8_t button[6];

    assert(srv);
    assert(player);
    rows_uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    SELFTEST_CHECK(rows_uid > 0, "chatmenu:options should resolve");
    SELFTEST_CHECK(player->active_script != NULL, "should be parked on p_choice");
    SELFTEST_CHECK(
        player->resume_button_count == 1 && player->resume_buttons[0] == rows_uid,
        "should arm chatmenu:options for Yes./No.");
    button[0] = (uint8_t)(rows_uid >> 24);
    button[1] = (uint8_t)(rows_uid >> 16);
    button[2] = (uint8_t)(rows_uid >> 8);
    button[3] = (uint8_t)rows_uid;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static int
ca_spawn_arhein(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int type;
    int slot;

    assert(srv);
    assert(player);
    type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "arhein");
    SELFTEST_CHECK(type > 0, "npc arhein should resolve by name");
    slot = ToriRSServer_WorldNpcSpawn(srv, type, player->x + 1, player->z, player->level);
    SELFTEST_CHECK(slot >= 0, "arhein should be spawnable");
    return slot;
}

static void
ca_set_stat(struct ToriRSServerPlayer* player, const char* name, int level)
{
    int id;

    assert(player);
    assert(name);
    id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, name);
    SELFTEST_CHECK(id >= 0, "stat %s should resolve", name);
    if( id >= 0 && id < TORIRSSERVER_STAT_COUNT )
    {
        player->stat_level[id] = level;
        player->stat_boosted[id] = level;
    }
}

static void
ca_set_varbit(struct ToriRSServer* srv, const char* name, int value)
{
    int id;

    assert(srv);
    assert(name);
    id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    SELFTEST_CHECK(id > 0, "varbit %s should resolve", name);
    ToriRSServer_VarbitSet(srv, id, value);
}

static int
ca_get_varbit(struct ToriRSServerPlayer* player, const char* name)
{
    int id;

    assert(player);
    assert(name);
    id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    SELFTEST_CHECK(id > 0, "varbit %s should resolve", name);
    return ToriRSServer_VarbitGet(player, id);
}

static void
ca_talk_arhein(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot)
{
    assert(srv);
    assert(player);
    player->active_script = NULL;
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunTrigger(
            srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot) == TORIRSSERVER_TRIGGER_RAN,
        "[opnpc1,arhein] should run");
}

static void
selftest_currentaffairs(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int arhein_type;
    int arhein_slot;
    int sailing_intro;
    static struct ToriRSServerCapture cap;

    assert(srv);
    assert(player);
    fprintf(stderr, "ToriRSServer selftest: Current Affairs focused runtime\n");
    player->godmode = 1;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "the focused Current Affairs lane loads a compiled script pack");
    if( !loaded )
        return;

    arhein_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "arhein");
    sailing_intro = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "sailing_intro");
    SELFTEST_CHECK(arhein_type > 0, "npc arhein should resolve");
    SELFTEST_CHECK(sailing_intro > 0, "varbit sailing_intro should resolve");
    SELFTEST_CHECK(
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "current_affairs") > 0,
        "varbit current_affairs should resolve");

    /* ---- qualify: Pandemonium first-missing ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    ca_set_stat(player, "sailing", 1);
    ca_set_stat(player, "fishing", 1);
    ca_set_varbit(srv, "sailing_intro", 0);
    ca_set_varbit(srv, "current_affairs", 0);
    arhein_slot = ca_spawn_arhein(srv, player);
    ToriRSServer_CaptureBegin(srv, &cap);
    ca_talk_arhein(srv, player, arhein_type, arhein_slot);
    SELFTEST_CHECK(player->active_script != NULL, "Pandemonium fail should park");
    ca_drain_until_choice_or_pages(srv, player, 5);
    SELFTEST_CHECK(player->active_script != NULL, "Pandemonium fail should park on mesbox");
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(
        ca_capture_has(srv, &cap, "You need to complete Pandemonium before starting Current Affairs."),
        "first-missing Pandemonium mesbox");
    SELFTEST_CHECK(ca_get_varbit(player, "current_affairs") == 0,
                   "Pandemonium fail must not write %%current_affairs, got %d",
                   ca_get_varbit(player, "current_affairs"));
    player->active_script = NULL;
    ToriRSServer_ScriptsFree(srv);

    /* ---- qualify: Sailing 22 first-missing (Pandemonium done, Fishing high) ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    ca_set_stat(player, "sailing", 1);
    ca_set_stat(player, "fishing", 99);
    ca_set_varbit(srv, "sailing_intro", 50);
    ca_set_varbit(srv, "current_affairs", 0);
    arhein_slot = ca_spawn_arhein(srv, player);
    ToriRSServer_CaptureBegin(srv, &cap);
    ca_talk_arhein(srv, player, arhein_type, arhein_slot);
    ca_drain_until_choice_or_pages(srv, player, 5);
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(
        ca_capture_has(srv, &cap, "You need a Sailing level of 22 to start Current Affairs."),
        "first-missing Sailing 22 mesbox");
    SELFTEST_CHECK(ca_get_varbit(player, "current_affairs") == 0,
                   "Sailing fail must not write %%current_affairs, got %d",
                   ca_get_varbit(player, "current_affairs"));
    player->active_script = NULL;
    ToriRSServer_ScriptsFree(srv);

    /* ---- qualify: Fishing 10 first-missing ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    ca_set_stat(player, "sailing", 22);
    ca_set_stat(player, "fishing", 1);
    ca_set_varbit(srv, "sailing_intro", 50);
    ca_set_varbit(srv, "current_affairs", 0);
    arhein_slot = ca_spawn_arhein(srv, player);
    ToriRSServer_CaptureBegin(srv, &cap);
    ca_talk_arhein(srv, player, arhein_type, arhein_slot);
    ca_drain_until_choice_or_pages(srv, player, 5);
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(
        ca_capture_has(srv, &cap, "You need a Fishing level of 10 to start Current Affairs."),
        "first-missing Fishing 10 mesbox");
    SELFTEST_CHECK(ca_get_varbit(player, "current_affairs") == 0,
                   "Fishing fail must not write %%current_affairs, got %d",
                   ca_get_varbit(player, "current_affairs"));
    player->active_script = NULL;
    ToriRSServer_ScriptsFree(srv);

    /* ---- refuse: pick row 2, varp stays 0 ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    ca_set_stat(player, "sailing", 22);
    ca_set_stat(player, "fishing", 10);
    ca_set_varbit(srv, "sailing_intro", 50);
    ca_set_varbit(srv, "current_affairs", 0);
    arhein_slot = ca_spawn_arhein(srv, player);
    ca_talk_arhein(srv, player, arhein_type, arhein_slot);
    /* 5 duck-intro chats + offer mesbox, then p_choice2 */
    ca_drain_until_choice_or_pages(srv, player, 6);
    ca_pick_choice_row(srv, player, 2);
    ca_drain_until_choice_or_pages(srv, player, 4);
    SELFTEST_CHECK(ca_get_varbit(player, "current_affairs") == 0,
                   "refuse row 2 must leave %%current_affairs==0, got %d",
                   ca_get_varbit(player, "current_affairs"));
    player->active_script = NULL;
    ToriRSServer_ScriptsFree(srv);

    /* ---- accept: pick row 1, varp writes 5 ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    ca_set_stat(player, "sailing", 22);
    ca_set_stat(player, "fishing", 10);
    ca_set_varbit(srv, "sailing_intro", 50);
    ca_set_varbit(srv, "current_affairs", 0);
    arhein_slot = ca_spawn_arhein(srv, player);
    ca_talk_arhein(srv, player, arhein_type, arhein_slot);
    ca_drain_until_choice_or_pages(srv, player, 6);
    ca_pick_choice_row(srv, player, 1);
    ca_drain_until_choice_or_pages(srv, player, 6);
    SELFTEST_CHECK(ca_get_varbit(player, "current_affairs") == 5,
                   "accept row 1 must write %%current_affairs==5, got %d",
                   ca_get_varbit(player, "current_affairs"));
    player->active_script = NULL;
    ToriRSServer_ScriptsFree(srv);

    /* ---- ::carun reaches 45 ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    ca_set_stat(player, "sailing", 22);
    ca_set_stat(player, "fishing", 10);
    ca_set_varbit(srv, "sailing_intro", 50);
    ca_set_varbit(srv, "current_affairs", 0);
    {
        int carun_ok = 0;

        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "carun") == TORIRSSERVER_TRIGGER_RAN,
                       "::carun should reach content");
        ToriRSServer_CaptureEnd(srv);
        carun_ok = ca_capture_has(srv, &cap, "carun OK");
        SELFTEST_CHECK(carun_ok, "::carun should reach its OK line");
        SELFTEST_CHECK(ca_get_varbit(player, "current_affairs") == 45,
                       "::carun must write %%current_affairs==45, got %d",
                       ca_get_varbit(player, "current_affairs"));
    }
    player->active_script = NULL;
    ToriRSServer_ScriptsFree(srv);

    fprintf(stderr, "ToriRSServer Current Affairs focused selftest: %lu checks, %d failures\n",
            g_selftest_checks, g_selftest_failures);
}
