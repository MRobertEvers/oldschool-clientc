/* Queen of Thieves C-walk. Not MM2 / MISTMYST / COK / BV.
 *
 * Gate: TORIRSSERVER_SELFTEST_QOT_ONLY=1
 * Placed immediately before the shop selftest_reset_world.
 * Godmode on for the whole walk. Player is unkillable (not a death case).
 */

static int
qot_capture_has(
    struct ToriRSServer* srv,
    const struct ToriRSServerCapture* cap,
    const char* needle)
{
    int i;

    assert(srv);
    assert(cap);
    assert(needle);
    for( i = 0; i < cap->count; i++ )
    {
        const struct ToriRSServerCapturedPacket* pk = &cap->packets[i];
        const char* text;

        if( pk->name == PKT_NAME_IF_SETTEXT )
        {
            text = selftest_settext_text(srv, pk);
            if( text && strstr(text, needle) )
                return 1;
        }
        if( pk->name == PKT_NAME_MESSAGE_GAME )
        {
            text = selftest_message_text(srv, pk);
            if( text && strstr(text, needle) )
                return 1;
        }
    }
    return 0;
}

/* Advance chatnpc / mesbox pages only. Stop on p_choice so the caller picks. */
static int
qot_click_pages(
    struct ToriRSServer* srv,
    int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    clicks = 0;
    while( clicks < max_pages && player->active_script )
    {
        int uid;
        uint8_t resume[4];

        if( player->resume_button_count <= 0 )
            break;
        uid = player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            break;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        clicks++;
    }
    return clicks;
}

static void
qot_close(
    struct ToriRSServer* srv)
{
    assert(srv);
    assert(srv->active_player);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    srv->active_player->active_script = NULL;
}

static int
qot_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type)
{
    int slot;

    assert(srv);
    assert(player);
    slot = selftest_require_npc(srv, npc_type, player->x + 1, player->z, player->level);
    if( slot < 0 )
        return TORIRSSERVER_TRIGGER_FAILED;
    player->x = srv->npcs[slot].x + 1;
    player->z = srv->npcs[slot].z;
    player->level = srv->npcs[slot].level;
    player->active_script = NULL;
    return ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
qot_ready(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int veos_bit,
    int clue_bit,
    int pisc_bit,
    int thieving,
    int thieving_level)
{
    assert(srv);
    assert(player);
    assert(veos_bit >= 0);
    assert(clue_bit >= 0);
    assert(pisc_bit >= 0);
    assert(thieving >= 0);
    ToriRSServer_VarbitSet(srv, veos_bit, 7);
    ToriRSServer_VarbitSet(srv, clue_bit, 8);
    ToriRSServer_VarbitSet(srv, pisc_bit, 0);
    ToriRSServer_CombatSetLevel(player, thieving, thieving_level);
}

static void
selftest_quest_queenofthieves(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int veos_bit;
    int clue_bit;
    int pisc_bit;
    int thieving;
    int lawry;
    int poor;
    int oreilly;
    int devan;
    int conrad;
    int queen;
    int stew;
    int docs;
    int coins;
    int page;
    int qot_complete;
    int qot_thieve_xp;
    int qot_coin_reward;
    static struct ToriRSServerCapture cap;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Queen of Thieves\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Queen of Thieves loads a compiled script pack");
    if( !loaded )
        return;

    player->godmode = 1;

    veos_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "veos_progress");
    clue_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "cluequest");
    pisc_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "piscquest");
    thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    lawry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "piscquest_official");
    poor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "piscarilius_poor_citizen_female_3");
    oreilly = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "piscquest_citizen");
    devan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "piscquest_thug");
    conrad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "piscquest_target_alive");
    queen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "piscquest_queen");
    stew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "stew");
    docs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "piscquest_documents");
    coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "veos_memoirs_pisc_page");
    qot_complete = ToriRSServer_ContentConstantInt("qot_complete", -1);
    qot_thieve_xp = ToriRSServer_ContentConstantInt("qot_thieve_xp", -1);
    qot_coin_reward = ToriRSServer_ContentConstantInt("qot_coin_reward", -1);

    SELFTEST_CHECK(veos_bit >= 0, "veos_progress varbit resolves");
    SELFTEST_CHECK(clue_bit >= 0, "cluequest varbit resolves");
    SELFTEST_CHECK(pisc_bit >= 0, "piscquest varbit resolves");
    SELFTEST_CHECK(thieving == 17, "thieving should be stat 17, got %d", thieving);
    SELFTEST_CHECK(lawry > 0, "piscquest_official resolves");
    SELFTEST_CHECK(poor > 0, "piscarilius_poor_citizen_female_3 resolves");
    SELFTEST_CHECK(oreilly > 0, "piscquest_citizen resolves");
    SELFTEST_CHECK(devan > 0, "piscquest_thug resolves");
    SELFTEST_CHECK(conrad > 0, "piscquest_target_alive resolves");
    SELFTEST_CHECK(queen > 0, "piscquest_queen resolves");
    SELFTEST_CHECK(stew > 0, "stew resolves");
    SELFTEST_CHECK(docs > 0, "piscquest_documents resolves");
    SELFTEST_CHECK(coins > 0, "coins resolves");
    SELFTEST_CHECK(page > 0, "veos_memoirs_pisc_page resolves");
    SELFTEST_CHECK(qot_complete == 13, "^qot_complete is 13, got %d", qot_complete);
    SELFTEST_CHECK(qot_thieve_xp == 20000, "^qot_thieve_xp is 20000 tenths, got %d",
                   qot_thieve_xp);
    SELFTEST_CHECK(qot_coin_reward == 2000, "^qot_coin_reward is 2000, got %d",
                   qot_coin_reward);
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,qot_show_qualify_fail]") != NULL,
                   "split qualify proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,qot_leftover_full_refuse_trees]") !=
                       NULL,
                   "leftover_full_refuse_trees proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,qot_leftover_favour_system]") != NULL,
                   "leftover_favour_system proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,qot_leftover_graceful_recolour_ui]") !=
                       NULL,
                   "leftover_graceful_recolour_ui proc is in the pack");
    if( veos_bit < 0 || clue_bit < 0 || pisc_bit < 0 || thieving < 0 || lawry <= 0 )
        return;

    /* --- Qualify fail: Client of Kourend --- */
    {
        ToriRSServer_VarbitSet(srv, veos_bit, 0);
        ToriRSServer_VarbitSet(srv, clue_bit, 8);
        ToriRSServer_VarbitSet(srv, pisc_bit, 0);
        ToriRSServer_CombatSetLevel(player, thieving, 20);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(qot_talk(srv, player, lawry) == TORIRSSERVER_TRIGGER_RAN,
                       "Tomas OPNPC1 runs for CoK fail");
        qot_click_pages(srv, 2);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(qot_capture_has(srv, &cap, "Client of Kourend"),
                       "CoK qualify fail names Client of Kourend");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 0,
                       "CoK fail does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Qualify fail: X Marks the Spot --- */
    {
        ToriRSServer_VarbitSet(srv, veos_bit, 7);
        ToriRSServer_VarbitSet(srv, clue_bit, 0);
        ToriRSServer_VarbitSet(srv, pisc_bit, 0);
        ToriRSServer_CombatSetLevel(player, thieving, 20);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(qot_talk(srv, player, lawry) == TORIRSSERVER_TRIGGER_RAN,
                       "Tomas OPNPC1 runs for X Marks fail");
        qot_click_pages(srv, 2);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(qot_capture_has(srv, &cap, "X Marks the Spot"),
                       "X Marks qualify fail names X Marks the Spot");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 0,
                       "X Marks fail does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Qualify fail: Thieving 20 --- */
    {
        ToriRSServer_VarbitSet(srv, veos_bit, 7);
        ToriRSServer_VarbitSet(srv, clue_bit, 8);
        ToriRSServer_VarbitSet(srv, pisc_bit, 0);
        ToriRSServer_CombatSetLevel(player, thieving, 1);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(qot_talk(srv, player, lawry) == TORIRSSERVER_TRIGGER_RAN,
                       "Tomas OPNPC1 runs for Thieving fail");
        qot_click_pages(srv, 2);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(qot_capture_has(srv, &cap, "Thieving level of 20"),
                       "Thieving qualify fail names Thieving 20");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 0,
                       "Thieving fail does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Offer refuse: Yes / Not now, does not auto-start --- */
    {
        qot_ready(srv, player, veos_bit, clue_bit, pisc_bit, thieving, 20);
        SELFTEST_CHECK(qot_talk(srv, player, lawry) == TORIRSSERVER_TRIGGER_RAN,
                       "Tomas OPNPC1 runs for offer");
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(player->active_script != NULL, "offer parks on p_choice2");
        selftest_charter_choose(srv, 2);
        qot_click_pages(srv, 2);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 0,
                       "Not now does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Offer accept --- */
    {
        qot_ready(srv, player, veos_bit, clue_bit, pisc_bit, thieving, 20);
        SELFTEST_CHECK(qot_talk(srv, player, lawry) == TORIRSSERVER_TRIGGER_RAN,
                       "Tomas OPNPC1 runs for accept");
        qot_click_pages(srv, 4);
        selftest_charter_choose(srv, 1);
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 1,
                       "Yes starts the quest at ^qot_poor=1, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Poor looking woman --- */
    if( poor > 0 )
    {
        ToriRSServer_VarbitSet(srv, pisc_bit, 1);
        SELFTEST_CHECK(qot_talk(srv, player, poor) == TORIRSSERVER_TRIGGER_RAN,
                       "poor citizen OPNPC1 runs");
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 2,
                       "poor clue writes ^qot_oreilly=2, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Robert O'Reilly: want stew / no stew / give stew --- */
    if( oreilly > 0 && stew > 0 )
    {
        ToriRSServer_VarbitSet(srv, pisc_bit, 2);
        SELFTEST_CHECK(qot_talk(srv, player, oreilly) == TORIRSSERVER_TRIGGER_RAN,
                       "O'Reilly OPNPC1 runs at oreilly");
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 3,
                       "want-stew writes ^qot_stew=3, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);

        SELFTEST_CHECK(qot_talk(srv, player, oreilly) == TORIRSSERVER_TRIGGER_RAN,
                       "O'Reilly OPNPC1 runs with no stew");
        qot_click_pages(srv, 2);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 3,
                       "no stew stays at ^qot_stew=3, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);

        selftest_give(player, stew, 1);
        SELFTEST_CHECK(qot_talk(srv, player, oreilly) == TORIRSSERVER_TRIGGER_RAN,
                       "O'Reilly OPNPC1 runs with stew");
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 4,
                       "give stew writes ^qot_devan=4, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        SELFTEST_CHECK(selftest_count(player, stew) == 0, "stew is consumed");
        qot_close(srv);
    }

    /* --- Devan refuse then accept --- */
    if( devan > 0 )
    {
        ToriRSServer_VarbitSet(srv, pisc_bit, 4);
        SELFTEST_CHECK(qot_talk(srv, player, devan) == TORIRSSERVER_TRIGGER_RAN,
                       "Devan OPNPC1 runs for offer");
        qot_click_pages(srv, 6);
        selftest_charter_choose(srv, 2);
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 4,
                       "Devan refuse stays at ^qot_devan=4, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);

        SELFTEST_CHECK(qot_talk(srv, player, devan) == TORIRSSERVER_TRIGGER_RAN,
                       "Devan OPNPC1 runs for accept");
        qot_click_pages(srv, 6);
        selftest_charter_choose(srv, 1);
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 6,
                       "Devan accept writes ^qot_conrad=6, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Conrad murder --- */
    if( conrad > 0 )
    {
        ToriRSServer_VarbitSet(srv, pisc_bit, 6);
        SELFTEST_CHECK(qot_talk(srv, player, conrad) == TORIRSSERVER_TRIGGER_RAN,
                       "Conrad OPNPC1 runs");
        qot_click_pages(srv, 4);
        selftest_charter_choose(srv, 1);
        qot_click_pages(srv, 2);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 7,
                       "Conrad murder writes ^qot_tell_devan=7, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Devan report --- */
    if( devan > 0 )
    {
        ToriRSServer_VarbitSet(srv, pisc_bit, 7);
        SELFTEST_CHECK(qot_talk(srv, player, devan) == TORIRSSERVER_TRIGGER_RAN,
                       "Devan OPNPC1 runs for report");
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 8,
                       "Devan report writes ^qot_queen=8, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Queen briefing --- */
    if( queen > 0 )
    {
        ToriRSServer_VarbitSet(srv, pisc_bit, 8);
        SELFTEST_CHECK(qot_talk(srv, player, queen) == TORIRSSERVER_TRIGGER_RAN,
                       "Queen OPNPC1 runs for briefing");
        qot_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 10,
                       "Queen briefing writes ^qot_chest=10, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Hughes chest --- */
    if( docs > 0 )
    {
        int32_t opened = 0;

        ToriRSServer_VarbitSet(srv, pisc_bit, 8);
        opened = 0;
        ToriRSServer_ScriptsRunProcInt(srv, "[proc,qot_open_chest]", NULL, 0, &opened);
        SELFTEST_CHECK(opened == 0, "chest refuses before ^qot_chest");
        SELFTEST_CHECK(selftest_count(player, docs) == 0, "too-early chest grants no letter");
        qot_close(srv);

        ToriRSServer_VarbitSet(srv, pisc_bit, 10);
        {
            int slot;

            for( slot = 0; slot < TORIRSSERVER_INV_SLOTS; slot++ )
                if( player->inv[slot].obj_id < 0 )
                    selftest_give(player, stew > 0 ? stew : coins, 1);
        }
        opened = 0;
        ToriRSServer_ScriptsRunProcInt(srv, "[proc,qot_open_chest]", NULL, 0, &opened);
        SELFTEST_CHECK(opened == 0, "full inventory refuses the letter");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 10,
                       "full-inv chest stays at ^qot_chest=10, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);

        selftest_clear_inv(player);
        opened = 0;
        ToriRSServer_ScriptsRunProcInt(srv, "[proc,qot_open_chest]", NULL, 0, &opened);
        SELFTEST_CHECK(opened == 1, "chest opens and returns 1");
        SELFTEST_CHECK(selftest_count(player, docs) == 1, "chest grants piscquest_documents");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 11,
                       "found letter writes ^qot_lawry2=11, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);

        opened = 0;
        ToriRSServer_ScriptsRunProcInt(srv, "[proc,qot_open_chest]", NULL, 0, &opened);
        SELFTEST_CHECK(opened == 1, "already-have chest returns 1");
        SELFTEST_CHECK(selftest_count(player, docs) == 1, "already-have does not duplicate the letter");
        qot_close(srv);
    }

    /* --- Lawry takes the letter --- */
    {
        ToriRSServer_VarbitSet(srv, pisc_bit, 11);
        if( docs > 0 && selftest_count(player, docs) < 1 )
            selftest_give(player, docs, 1);
        SELFTEST_CHECK(qot_talk(srv, player, lawry) == TORIRSSERVER_TRIGGER_RAN,
                       "Tomas OPNPC1 runs with documents");
        qot_click_pages(srv, 6);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 12,
                       "Lawry hand-in writes ^qot_shauna=12, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    /* --- Queen complete + authored rewards --- */
    if( queen > 0 && page > 0 && coins > 0 )
    {
        int xp_before;
        int coins_before;

        ToriRSServer_VarbitSet(srv, pisc_bit, 12);
        if( docs > 0 && selftest_count(player, docs) < 1 )
            selftest_give(player, docs, 1);
        xp_before = player->stat_xp_tenths[thieving];
        coins_before = selftest_count(player, coins);
        SELFTEST_CHECK(qot_talk(srv, player, queen) == TORIRSSERVER_TRIGGER_RAN,
                       "Queen OPNPC1 runs for hand-in");
        qot_click_pages(srv, 8);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) == 13,
                       "hand-in writes ^qot_complete=13, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        SELFTEST_CHECK(player->stat_xp_tenths[thieving] >= xp_before + 20000,
                       "complete awards 20000 Thieving tenths (%d -> %d)",
                       xp_before, player->stat_xp_tenths[thieving]);
        SELFTEST_CHECK(selftest_count(player, coins) >= coins_before + 2000,
                       "complete awards 2000 coins (%d -> %d)",
                       coins_before, selftest_count(player, coins));
        SELFTEST_CHECK(selftest_count(player, page) >= 1,
                       "complete awards veos_memoirs_pisc_page");
        SELFTEST_CHECK(selftest_count(player, docs) == 0, "hand-in consumes the letter");
        qot_close(srv);
    }

    /* --- Journal QUEST COMPLETE! --- */
    {
        ToriRSServer_VarbitSet(srv, pisc_bit, 13);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,queenofthieves_journal]", NULL, 0),
                       "queenofthieves_journal runs");
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(qot_capture_has(srv, &cap, "QUEST COMPLETE!"),
                       "complete journal paints QUEST COMPLETE!");
        qot_close(srv);
    }

    /* --- ::qotrun headless walk --- */
    {
        int qotrun_ok = 0;

        ToriRSServer_VarbitSet(srv, veos_bit, 7);
        ToriRSServer_VarbitSet(srv, clue_bit, 8);
        ToriRSServer_VarbitSet(srv, pisc_bit, 0);
        ToriRSServer_CombatSetLevel(player, thieving, 20);
        if( page > 0 && selftest_count(player, page) > 0 )
        {
            int i;

            for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
                if( player->inv[i].obj_id == page )
                {
                    player->inv[i].obj_id = -1;
                    player->inv[i].count = 0;
                }
        }
        ToriRSServer_CaptureBegin(srv, &cap);
        ToriRSServer_ScriptsRunDebugproc(srv, "qotrun");
        ToriRSServer_CaptureEnd(srv);
        {
            int i;

            for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
                 i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
            {
                const char* text = selftest_message_text(srv, &cap.packets[i]);

                if( text && strstr(text, "qotrun OK") )
                    qotrun_ok = 1;
            }
        }
        SELFTEST_CHECK(qotrun_ok, "::qotrun should reach its OK line");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, pisc_bit) >= 13,
                       "::qotrun leaves ^qot_complete, got %d",
                       ToriRSServer_VarbitGet(player, pisc_bit));
        qot_close(srv);
    }

    player->godmode = 1;
}
