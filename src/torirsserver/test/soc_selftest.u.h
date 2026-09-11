/*
 * Shadows of Custodia C-walk. Driven only when
 * TORIRSSERVER_SELFTEST_SOC_ONLY=1. Lives immediately before the shop
 * stanza so an unset env leaves the default suite unmoved.
 *
 * Walks the live [oploc1,soc_missing_persons] hook: split qualify must
 * not write %soc, the transcript Yes./No. parks on chatmenu:options so
 * refuse can pick row 2, accept writes ^soc_citizens (2), and the rest
 * of the authored path reaches ^soc_complete (24).
 */
static int
soc_sym(
    int kind,
    const char* name)
{
    return ToriRSServer_ContentSymbol(kind, name);
}

static int
soc_vb(
    struct ToriRSServer* srv,
    const char* name)
{
    int id = soc_sym(TORIRSSERVER_PACK_VARBIT, name);

    if( id < 0 )
        return -1;
    return ToriRSServer_VarbitGet(srv->active_player, id);
}

static void
soc_set_vb(
    struct ToriRSServer* srv,
    const char* name,
    int value)
{
    int id = soc_sym(TORIRSSERVER_PACK_VARBIT, name);

    if( id >= 0 )
        ToriRSServer_VarbitSet(srv, id, value);
}

static void
soc_set_stat(
    struct ToriRSServerPlayer* player,
    const char* name,
    int level)
{
    int stat = soc_sym(TORIRSSERVER_PACK_STAT, name);

    if( stat >= 0 && stat < TORIRSSERVER_STAT_COUNT )
    {
        player->stat_level[stat] = level;
        player->stat_boosted[stat] = level;
    }
}

static void
soc_reset_quest(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    soc_set_vb(srv, "soc", 0);
    soc_set_vb(srv, "soc_citizen", 0);
    soc_set_vb(srv, "soc_barkeep", 0);
    soc_set_vb(srv, "soc_shopkeep", 0);
    soc_set_vb(srv, "soc_sillyman", 0);
    soc_set_vb(srv, "soc_bowsmade", 0);
    soc_set_vb(srv, "soc_wall_state", 0);
    soc_set_vb(srv, "soc_stalkers_encountered", 0);
    soc_set_vb(srv, "vmq1", 0);
    player->godmode = 1;
    player->x = 1396;
    player->z = 3356;
    player->level = 0;
    player->place_dirty = 1;
    ToriRSServer_WorldStepsClear(player);
    selftest_clear_inv(player);
}

static void
soc_qualify_ok(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    soc_set_vb(srv, "vmq1", 24);
    soc_set_stat(player, "slayer", 54);
    soc_set_stat(player, "fishing", 45);
    soc_set_stat(player, "construction", 41);
    soc_set_stat(player, "hunter", 36);
}

static int
soc_on_chatmenu(
    struct ToriRSServer* srv,
    int chatmenu)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    return player->active_script && player->resume_button_count == 1 &&
           player->resume_buttons[0] == chatmenu;
}

/* Continue past mesbox / chathead pages. Stops on chatmenu:options. */
static void
soc_click_to_menu_or_end(
    struct ToriRSServer* srv,
    int chatmenu,
    int max_pages)
{
    int clicks = 0;

    while( clicks < max_pages && srv->active_player->active_script )
    {
        int uid;
        uint8_t resume[4];

        if( srv->active_player->resume_button_count <= 0 )
            break;
        uid = srv->active_player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            return;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(srv->active_player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        clicks++;
    }
}

static void
soc_choose_row(
    struct ToriRSServer* srv,
    int chatmenu,
    int row)
{
    uint8_t button[6];

    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(srv->active_player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static int
soc_run_board(struct ToriRSServer* srv)
{
    int loc = soc_sym(TORIRSSERVER_PACK_LOC, "soc_missing_persons");

    if( loc < 0 )
        return 0;
    return ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc, -1, -1) ==
           TORIRSSERVER_TRIGGER_RAN;
}

static int
soc_run_npc(
    struct ToriRSServer* srv,
    const char* npc_name)
{
    int type = soc_sym(TORIRSSERVER_PACK_NPC, npc_name);
    int slot;

    if( type < 0 )
        return -1;
    slot = ToriRSServer_WorldNpcSpawn(srv, type, srv->active_player->x + 1,
                                      srv->active_player->z, srv->active_player->level);
    if( slot < 0 )
        return -1;
    if( ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, type, -1, slot) !=
        TORIRSSERVER_TRIGGER_RAN )
        return -1;
    return slot;
}

static int
soc_run_loc(
    struct ToriRSServer* srv,
    const char* loc_name)
{
    int loc = soc_sym(TORIRSSERVER_PACK_LOC, loc_name);

    if( loc < 0 )
        return 0;
    return ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc, -1, -1) ==
           TORIRSSERVER_TRIGGER_RAN;
}

static void
selftest_soc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int chatmenu;
    int slayer;
    int fishing;
    int construction;
    int hunter;
    int board;
    int fishing_rod;
    int maple_logs;
    int hammer;
    int willow_longbow;
    int soc_cloth;

    fprintf(stderr, "ToriRSServer selftest: Shadows of Custodia\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "SOC walk loads a compiled script pack");
    if( !loaded )
        return;

    chatmenu = soc_sym(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    slayer = soc_sym(TORIRSSERVER_PACK_STAT, "slayer");
    fishing = soc_sym(TORIRSSERVER_PACK_STAT, "fishing");
    construction = soc_sym(TORIRSSERVER_PACK_STAT, "construction");
    hunter = soc_sym(TORIRSSERVER_PACK_STAT, "hunter");
    board = soc_sym(TORIRSSERVER_PACK_LOC, "soc_missing_persons");
    fishing_rod = soc_sym(TORIRSSERVER_PACK_OBJ, "fishing_rod");
    maple_logs = soc_sym(TORIRSSERVER_PACK_OBJ, "maple_logs");
    hammer = soc_sym(TORIRSSERVER_PACK_OBJ, "hammer");
    willow_longbow = soc_sym(TORIRSSERVER_PACK_OBJ, "willow_longbow");
    soc_cloth = soc_sym(TORIRSSERVER_PACK_OBJ, "soc_cloth");

    SELFTEST_CHECK(chatmenu > 0, "chatmenu:options should resolve");
    SELFTEST_CHECK(board > 0, "soc_missing_persons should resolve");
    SELFTEST_CHECK(slayer >= 0 && fishing >= 0 && construction >= 0 && hunter >= 0,
                   "SOC skill symbols should resolve");
    SELFTEST_CHECK(soc_sym(TORIRSSERVER_PACK_VARBIT, "soc") >= 0, "%soc varbit should resolve");
    SELFTEST_CHECK(soc_sym(TORIRSSERVER_PACK_VARBIT, "vmq1") >= 0, "%vmq1 varbit should resolve");

    player->godmode = 1;

    /* Qualify fail: Children of the Sun. Must not write %soc. */
    {
        soc_reset_quest(srv, player);
        soc_set_stat(player, "slayer", 54);
        soc_set_stat(player, "fishing", 45);
        soc_set_stat(player, "construction", 41);
        soc_set_stat(player, "hunter", 36);
        soc_set_vb(srv, "vmq1", 0);
        SELFTEST_CHECK(soc_run_board(srv), "COTS fail should run the board");
        soc_click_to_menu_or_end(srv, chatmenu, 8);
        SELFTEST_CHECK(!soc_on_chatmenu(srv, chatmenu),
                       "COTS fail must not reach the Yes/No offer");
        SELFTEST_CHECK(soc_vb(srv, "soc") == 0, "COTS fail must not write %%soc, got %d",
                       soc_vb(srv, "soc"));
        selftest_click_through(srv, 8);
        player->active_script = NULL;
    }

    /* Qualify fail: Slayer 54. */
    {
        soc_reset_quest(srv, player);
        soc_qualify_ok(srv, player);
        player->stat_level[slayer] = 53;
        player->stat_boosted[slayer] = 99; /* boost must not bypass stat_base */
        SELFTEST_CHECK(soc_run_board(srv), "Slayer fail should run the board");
        soc_click_to_menu_or_end(srv, chatmenu, 8);
        SELFTEST_CHECK(!soc_on_chatmenu(srv, chatmenu),
                       "Slayer fail must not reach the Yes/No offer");
        SELFTEST_CHECK(soc_vb(srv, "soc") == 0, "Slayer fail must not write %%soc, got %d",
                       soc_vb(srv, "soc"));
        selftest_click_through(srv, 8);
        player->active_script = NULL;
    }

    /* Qualify fail: Fishing 45. */
    {
        soc_reset_quest(srv, player);
        soc_qualify_ok(srv, player);
        player->stat_level[fishing] = 44;
        player->stat_boosted[fishing] = 99;
        SELFTEST_CHECK(soc_run_board(srv), "Fishing fail should run the board");
        soc_click_to_menu_or_end(srv, chatmenu, 8);
        SELFTEST_CHECK(!soc_on_chatmenu(srv, chatmenu),
                       "Fishing fail must not reach the Yes/No offer");
        SELFTEST_CHECK(soc_vb(srv, "soc") == 0, "Fishing fail must not write %%soc, got %d",
                       soc_vb(srv, "soc"));
        selftest_click_through(srv, 8);
        player->active_script = NULL;
    }

    /* Qualify fail: Construction 41. */
    {
        soc_reset_quest(srv, player);
        soc_qualify_ok(srv, player);
        player->stat_level[construction] = 40;
        player->stat_boosted[construction] = 99;
        SELFTEST_CHECK(soc_run_board(srv), "Construction fail should run the board");
        soc_click_to_menu_or_end(srv, chatmenu, 8);
        SELFTEST_CHECK(!soc_on_chatmenu(srv, chatmenu),
                       "Construction fail must not reach the Yes/No offer");
        SELFTEST_CHECK(soc_vb(srv, "soc") == 0, "Construction fail must not write %%soc, got %d",
                       soc_vb(srv, "soc"));
        selftest_click_through(srv, 8);
        player->active_script = NULL;
    }

    /* Qualify fail: Hunter 36. */
    {
        soc_reset_quest(srv, player);
        soc_qualify_ok(srv, player);
        player->stat_level[hunter] = 35;
        player->stat_boosted[hunter] = 99;
        SELFTEST_CHECK(soc_run_board(srv), "Hunter fail should run the board");
        soc_click_to_menu_or_end(srv, chatmenu, 8);
        SELFTEST_CHECK(!soc_on_chatmenu(srv, chatmenu),
                       "Hunter fail must not reach the Yes/No offer");
        SELFTEST_CHECK(soc_vb(srv, "soc") == 0, "Hunter fail must not write %%soc, got %d",
                       soc_vb(srv, "soc"));
        selftest_click_through(srv, 8);
        player->active_script = NULL;
    }

    /* Offer parks on chatmenu:options. Refuse (row 2) must not write %soc. */
    {
        soc_reset_quest(srv, player);
        soc_qualify_ok(srv, player);
        SELFTEST_CHECK(soc_run_board(srv), "offer should run the board");
        soc_click_to_menu_or_end(srv, chatmenu, 8);
        SELFTEST_CHECK(soc_on_chatmenu(srv, chatmenu),
                       "offer must park on chatmenu:options so refuse can pick row 2");
        soc_choose_row(srv, chatmenu, 2);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 0, "refuse must not write %%soc, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
    }

    /* Accept (row 1) writes ^soc_citizens = 2. */
    {
        soc_reset_quest(srv, player);
        soc_qualify_ok(srv, player);
        SELFTEST_CHECK(soc_run_board(srv), "accept should run the board");
        soc_click_to_menu_or_end(srv, chatmenu, 8);
        SELFTEST_CHECK(soc_on_chatmenu(srv, chatmenu),
                       "accept path must park on chatmenu:options");
        soc_choose_row(srv, chatmenu, 1);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 2, "accept writes ^soc_citizens, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
    }

    /* Mid-quest: Marcus, then the other three citizens, then parents. */
    {
        int slot;

        soc_reset_quest(srv, player);
        soc_qualify_ok(srv, player);
        soc_set_vb(srv, "soc", 2);
        slot = soc_run_npc(srv, "soc_citizen");
        SELFTEST_CHECK(slot >= 0, "Marcus talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc_citizen") == 1, "Marcus should mark the citizen side");
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);

        slot = soc_run_npc(srv, "auburn_bartender");
        SELFTEST_CHECK(slot >= 0, "bartender talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc_barkeep") == 1, "bartender should mark the side");
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);

        slot = soc_run_npc(srv, "auburn_general_store");
        SELFTEST_CHECK(slot >= 0, "shopkeep talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc_shopkeep") == 1, "shopkeep should mark the side");
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);

        slot = soc_run_npc(srv, "soc_sillyman");
        SELFTEST_CHECK(slot >= 0, "Ictus talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc_sillyman") == 1, "Ictus should mark the side");
        SELFTEST_CHECK(soc_vb(srv, "soc") == 4, "all four citizens advance to parents, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);

        slot = soc_run_npc(srv, "soc_parent");
        SELFTEST_CHECK(slot >= 0, "parent talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 5, "parents send the player to the wall, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    /* Trail locs. */
    SELFTEST_CHECK(soc_run_loc(srv, "soc_wall_inspect_op"), "wall inspect should run");
    SELFTEST_CHECK(soc_vb(srv, "soc") == 6, "wall inspect writes puddle, got %d", soc_vb(srv, "soc"));
    player->active_script = NULL;

    SELFTEST_CHECK(soc_run_loc(srv, "soc_puddle"), "puddle inspect should run");
    SELFTEST_CHECK(soc_vb(srv, "soc") == 8, "puddle writes plank, got %d", soc_vb(srv, "soc"));
    player->active_script = NULL;

    if( fishing_rod >= 0 )
        selftest_give(player, fishing_rod, 1);
    SELFTEST_CHECK(soc_run_loc(srv, "soc_log_op"), "plank fish should run");
    SELFTEST_CHECK(soc_vb(srv, "soc") == 9, "plank writes cloth, got %d", soc_vb(srv, "soc"));
    if( soc_cloth >= 0 )
        SELFTEST_CHECK(selftest_count_obj(player, soc_cloth) >= 1, "plank should grant the cloth");
    player->active_script = NULL;

    {
        int slot = soc_run_npc(srv, "soc_parent");

        SELFTEST_CHECK(slot >= 0, "cloth hand-in should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 10, "cloth hand-in writes boys, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    SELFTEST_CHECK(soc_run_loc(srv, "soc_cave_entrance"), "cave enter should run");
    SELFTEST_CHECK(soc_vb(srv, "soc") == 12, "cave enter writes boys_cave, got %d",
                   soc_vb(srv, "soc"));
    player->active_script = NULL;

    {
        int slot = soc_run_npc(srv, "soc_injured_person");

        SELFTEST_CHECK(slot >= 0, "injured boy should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 14, "injured boy writes boys_found, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    {
        int slot = soc_run_npc(srv, "soc_parent");

        SELFTEST_CHECK(slot >= 0, "boys-found parent talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 15, "parents send the player to authorities, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    if( maple_logs >= 0 )
        selftest_give(player, maple_logs, 4);
    if( hammer >= 0 )
        selftest_give(player, hammer, 1);
    SELFTEST_CHECK(soc_run_loc(srv, "soc_wall_inspect_reinforce"), "wall reinforce should run");
    SELFTEST_CHECK(soc_vb(srv, "soc_wall_state") == 3, "reinforce writes wall_done, got %d",
                   soc_vb(srv, "soc_wall_state"));
    player->active_script = NULL;

    if( willow_longbow >= 0 )
        selftest_give(player, willow_longbow, 4);
    {
        int slot = soc_run_npc(srv, "auburnvale_guard_captain");

        SELFTEST_CHECK(slot >= 0, "captain bow hand-in should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 16, "captain writes etz, got %d", soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    {
        int slot = soc_run_npc(srv, "soc_etz");

        SELFTEST_CHECK(slot >= 0, "Etz talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 18, "Etz writes antos, got %d", soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    {
        int slot = soc_run_npc(srv, "soc_antos");

        SELFTEST_CHECK(slot >= 0, "Antos first talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 20, "Antos skip writes antos_saved, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    {
        int slot = soc_run_npc(srv, "soc_antos");

        SELFTEST_CHECK(slot >= 0, "Antos saved talk should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 22, "Antos saved writes finish, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    {
        int slot = soc_run_npc(srv, "auburnvale_guard_captain");

        SELFTEST_CHECK(slot >= 0, "captain finish should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(soc_vb(srv, "soc") == 24, "captain finish writes complete, got %d",
                       soc_vb(srv, "soc"));
        player->active_script = NULL;
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
    }

    /* Headless ::socrun still reaches complete=24. */
    {
        static struct ToriRSServerCapture cap;
        int said_ok = 0;

        soc_reset_quest(srv, player);
        soc_qualify_ok(srv, player);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "socrun") == TORIRSSERVER_TRIGGER_RAN,
                       "::socrun should reach content");
        ToriRSServer_CaptureEnd(srv);
        for( int i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
             i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const char* text = selftest_message_text(srv, &cap.packets[i]);

            if( text && strstr(text, "socrun OK") )
                said_ok = 1;
        }
        SELFTEST_CHECK(said_ok, "::socrun should print socrun OK");
        SELFTEST_CHECK(soc_vb(srv, "soc") == 24, "::socrun writes complete 24, got %d",
                       soc_vb(srv, "soc"));
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
    }

    ToriRSServer_WorldNpcReap(srv);
    ToriRSServer_ScriptsFree(srv);
}
