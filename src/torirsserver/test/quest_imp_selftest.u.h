/* Imp Catcher -- real-trigger walk, not a debugproc mirror.
 * Talks through SS_TRIGGER_OPNPC1 + ScriptsResumeButton. Choices are last_slot.
 */
static void
imp_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    fprintf(stderr, "PASS impcatcher %s trigger=%s %s\n", step, trigger, observable);
    fflush(stderr);
}

static void
imp_clear_inv(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
imp_give_beads(
    struct ToriRSServerPlayer* player,
    int black_bead,
    int white_bead,
    int yellow_bead,
    int red_bead)
{
    assert(player);
    /* Slots 0-3 may already hold the first amulet. */
    inv_set(player, 4, black_bead, 1);
    inv_set(player, 5, white_bead, 1);
    inv_set(player, 6, yellow_bead, 1);
    inv_set(player, 7, red_bead, 1);
}

static void
imp_release_park(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

/* Drain continue / p_delay until idle or parked on chatmenu:options. */
static int
imp_drain_to_choice_or_idle(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int chatmenu)
{
    int round;
    assert(srv);
    assert(player);
    for( round = 0; round < 80 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( chatmenu > 0 && uid == chatmenu )
                return 1;
            if( !ToriRSServer_ScriptsResumeButton(srv, uid) )
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
    return 0;
}

static void
imp_pick_slot(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int chatmenu,
    int slot)
{
    assert(srv);
    assert(player);
    player->last_slot = slot;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
imp_drain_scroll_queue(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int i;
    int chatmenu;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    /* Resume any leftover pause first -- WorldCloseModal aborts a parked script. */
    imp_drain_to_choice_or_idle(srv, player, chatmenu);
    for( i = 0; i < 40; i++ )
    {
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
imp_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot,
    const int* choices,
    int choice_count)
{
    int chatmenu;
    int i;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    imp_release_park(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    for( i = 0; i < choice_count; i++ )
    {
        if( !imp_drain_to_choice_or_idle(srv, player, chatmenu) )
            break;
        imp_pick_slot(srv, player, chatmenu, choices[i]);
    }
    imp_drain_to_choice_or_idle(srv, player, chatmenu);
    imp_drain_scroll_queue(srv, player);
}

static void
selftest_quest_imp(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_mizgog;
    int npc_grayzag;
    int npc_demon;
    int obj_black;
    int obj_white;
    int obj_yellow;
    int obj_red;
    int obj_amulet;
    int varp_imp;
    int varp_settle;
    int varp_qp;
    int miz_slot;
    int gray_slot;
    int demon_slot;
    int qp_before;
    int xp_before;
    int chatmenu;

    assert(srv);
    assert(player);
    fprintf(stderr, "ToriRSServer selftest: Imp Catcher real-trigger walk\n");
    fflush(stderr);

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Imp Catcher selftest needs a compiled script pack");
    if( !loaded )
        return;

    npc_mizgog = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wizard_mizgog");
    npc_grayzag = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wizard_grayzag");
    npc_demon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lesser_demon");
    obj_black = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "black_bead");
    obj_white = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "white_bead");
    obj_yellow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yellow_bead");
    obj_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "red_bead");
    obj_amulet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_accuracy");
    varp_imp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "imp");
    varp_settle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "imp_settle");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(npc_mizgog >= 0 && npc_grayzag >= 0 && obj_black >= 0 &&
                       obj_white >= 0 && obj_yellow >= 0 && obj_red >= 0 &&
                       obj_amulet >= 0 && varp_imp >= 0 && varp_settle >= 0 &&
                       varp_qp >= 0 && chatmenu > 0 &&
                       varp_imp < TORIRSSERVER_VARP_COUNT &&
                       varp_settle < TORIRSSERVER_VARP_COUNT &&
                       varp_qp < TORIRSSERVER_VARP_COUNT,
                   "Imp Catcher symbols should resolve inside the varp table");
    if( npc_mizgog < 0 || npc_grayzag < 0 || obj_black < 0 || obj_white < 0 ||
        obj_yellow < 0 || obj_red < 0 || obj_amulet < 0 || varp_imp < 0 ||
        varp_settle < 0 || varp_qp < 0 || chatmenu <= 0 ||
        varp_imp >= TORIRSSERVER_VARP_COUNT || varp_settle >= TORIRSSERVER_VARP_COUNT ||
        varp_qp >= TORIRSSERVER_VARP_COUNT )
        return;

    /* The helper stanza immediately above memsets players[0], including
     * `world`. WorldPlayerInit preserves that pointer, so restore it first. */
    player->world = srv;
    player->active = 1;
    selftest_reset_world(srv, player, 402, 402);
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);
    ToriRSServer_WorldTeleport(srv, 2, 3103, 3163);
    selftest_ack_scene(srv);
    /* Same-floor lesser_demon is not this quest. Free it before the SNAP tick
     * so generic combat AI cannot abort the walk. */
    if( npc_demon >= 0 )
    {
        demon_slot = selftest_find_npc(srv, npc_demon);
        if( demon_slot >= 0 )
        {
            ToriRSServer_WorldNpcFree(srv, demon_slot);
            ToriRSServer_WorldNpcReap(srv);
        }
    }
    selftest_tick(srv);
    fprintf(stderr, "PASS impcatcher snap trigger=SNAP tile=3103,3163,2\n");
    fflush(stderr);

    imp_clear_inv(player);
    player->varps[varp_imp] = 0;
    player->varps[varp_settle] = 0;
    player->last_slot = -1;

    miz_slot = npc_spawn(srv, npc_mizgog, player->x + 1, player->z, player->level);
    gray_slot = npc_spawn(srv, npc_grayzag, player->x + 2, player->z, player->level);
    fprintf(stderr, "PASS impcatcher setup trigger=SNAP miz=%d gray=%d imp=%d\n",
            miz_slot, gray_slot, player->varps[varp_imp]);
    fflush(stderr);
    SELFTEST_CHECK(miz_slot >= 0 && gray_slot >= 0,
                   "Imp Catcher should spawn Mizgog and Grayzag beside the player");
    if( miz_slot < 0 || gray_slot < 0 )
        return;

    /* ---- decline: Yes/No commit stays at state 0 ---- */
    {
        const int choices[] = {1, 2};

        imp_talk(srv, player, npc_mizgog, miz_slot, choices, 2);
        SELFTEST_CHECK(player->varps[varp_imp] == 0,
                       "decline must leave %%imp at 0, got %d",
                       player->varps[varp_imp]);
        SELFTEST_CHECK(player->varps[varp_settle] == 0,
                       "decline must not write a settlement receipt, got %d",
                       player->varps[varp_settle]);
        if( player->varps[varp_imp] == 0 )
            imp_pass("decline", "OPNPC1", "imp=0 beads untouched");
    }

    /* ---- accept without beads ---- */
    {
        const int choices[] = {1, 1};

        imp_talk(srv, player, npc_mizgog, miz_slot, choices, 2);
        SELFTEST_CHECK(player->varps[varp_imp] == 1,
                       "accept without beads must set %%imp=1, got %d",
                       player->varps[varp_imp]);
        if( player->varps[varp_imp] == 1 )
            imp_pass("accept", "OPNPC1", "imp=1 no beads");
    }

    /* ---- Grayzag started line ---- */
    imp_talk(srv, player, npc_grayzag, gray_slot, NULL, 0);
    SELFTEST_CHECK(player->varps[varp_imp] == 1,
                   "Grayzag talk must not change %%imp, got %d",
                   player->varps[varp_imp]);
    imp_pass("grayzag-started", "OPNPC1", "imp stays 1");

    /* ---- re-talk with no beads ---- */
    imp_talk(srv, player, npc_mizgog, miz_slot, NULL, 0);
    SELFTEST_CHECK(player->varps[varp_imp] == 1,
                   "empty-hand re-talk must keep %%imp=1, got %d",
                   player->varps[varp_imp]);
    imp_pass("retalk-none", "OPNPC1", "imp=1 still collecting");

    /* ---- hand-in of a gathered set ---- */
    qp_before = player->varps[varp_qp];
    xp_before = player->stat_xp_tenths[TORIRSSERVER_STAT_MAGIC];
    imp_give_beads(player, obj_black, obj_white, obj_yellow, obj_red);
    SELFTEST_CHECK(selftest_count_obj(player, obj_black) == 1 &&
                       selftest_count_obj(player, obj_white) == 1 &&
                       selftest_count_obj(player, obj_yellow) == 1 &&
                       selftest_count_obj(player, obj_red) == 1,
                   "hand-in setup should grant all four beads");
    imp_talk(srv, player, npc_mizgog, miz_slot, NULL, 0);
    SELFTEST_CHECK(player->varps[varp_imp] == 2,
                   "full-set hand-in must complete %%imp=2, got %d",
                   player->varps[varp_imp]);
    SELFTEST_CHECK(player->varps[varp_settle] == 5,
                   "hand-in must fully receipt settlement, got %d",
                   player->varps[varp_settle]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_black) == 0 &&
                       selftest_count_obj(player, obj_white) == 0 &&
                       selftest_count_obj(player, obj_yellow) == 0 &&
                       selftest_count_obj(player, obj_red) == 0,
                   "hand-in must consume exactly one of each bead");
    SELFTEST_CHECK(selftest_count_obj(player, obj_amulet) == 1,
                   "first completion must grant one amulet, got %d",
                   selftest_count_obj(player, obj_amulet));
    SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1,
                   "first completion must award +1 QP, %d -> %d",
                   qp_before, player->varps[varp_qp]);
    SELFTEST_CHECK(player->stat_xp_tenths[TORIRSSERVER_STAT_MAGIC] ==
                       xp_before + 8750,
                   "first completion must award 875 Magic XP, %d -> %d",
                   xp_before, player->stat_xp_tenths[TORIRSSERVER_STAT_MAGIC]);
    if( player->varps[varp_imp] == 2 && selftest_count_obj(player, obj_amulet) == 1 )
        imp_pass("handin", "OPNPC1", "imp=2 amulet=1 qp+1 xp+875");

    /* ---- exactly-once: re-talk must not replay first rewards ---- */
    {
        int amulets = selftest_count_obj(player, obj_amulet);
        int qp = player->varps[varp_qp];
        int xp = player->stat_xp_tenths[TORIRSSERVER_STAT_MAGIC];
        const int more_quests[] = {1};

        imp_talk(srv, player, npc_mizgog, miz_slot, more_quests, 1);
        SELFTEST_CHECK(selftest_count_obj(player, obj_amulet) == amulets,
                       "postquest re-talk must not grant another first amulet");
        SELFTEST_CHECK(player->varps[varp_qp] == qp,
                       "postquest re-talk must not award a second QP");
        SELFTEST_CHECK(player->stat_xp_tenths[TORIRSSERVER_STAT_MAGIC] == xp,
                       "postquest re-talk must not award Magic XP again");
        imp_pass("exactly-once", "OPNPC1", "no second amulet/QP/XP");
    }

    /* ---- postquest cancel must not consume beads ---- */
    {
        const int cancel[] = {3, 2};

        imp_give_beads(player, obj_black, obj_white, obj_yellow, obj_red);
        imp_talk(srv, player, npc_mizgog, miz_slot, cancel, 2);
        SELFTEST_CHECK(selftest_count_obj(player, obj_black) == 1 &&
                           selftest_count_obj(player, obj_white) == 1 &&
                           selftest_count_obj(player, obj_yellow) == 1 &&
                           selftest_count_obj(player, obj_red) == 1,
                       "Maybe later must leave the bead set");
        SELFTEST_CHECK(selftest_count_obj(player, obj_amulet) == 1,
                       "cancel must not add an amulet");
        if( selftest_count_obj(player, obj_black) == 1 &&
            selftest_count_obj(player, obj_amulet) == 1 )
            imp_pass("postquest-cancel", "OPNPC1 last_slot=2", "beads kept");
    }

    /* ---- postquest confirmed purchase via Talk-to ---- */
    {
        const int buy[] = {3, 1};

        imp_talk(srv, player, npc_mizgog, miz_slot, buy, 2);
        SELFTEST_CHECK(selftest_count_obj(player, obj_black) == 0 &&
                           selftest_count_obj(player, obj_red) == 0,
                       "confirmed Talk-to purchase must consume one set");
        SELFTEST_CHECK(selftest_count_obj(player, obj_amulet) == 2,
                       "confirmed Talk-to purchase must add one amulet, got %d",
                       selftest_count_obj(player, obj_amulet));
        if( selftest_count_obj(player, obj_amulet) == 2 )
            imp_pass("postquest-talk-buy", "OPNPC1 last_slot=1", "amulet=2");
    }

    /* ---- Purchase Amulet op3 + confirmation ---- */
    {
        const int buy[] = {1};

        imp_give_beads(player, obj_black, obj_white, obj_yellow, obj_red);
        imp_release_park(srv, player);
        player->last_slot = -1;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_mizgog, -1, miz_slot);
        if( imp_drain_to_choice_or_idle(srv, player, chatmenu) )
            imp_pick_slot(srv, player, chatmenu, buy[0]);
        imp_drain_to_choice_or_idle(srv, player, chatmenu);
        imp_drain_scroll_queue(srv, player);
        SELFTEST_CHECK(selftest_count_obj(player, obj_amulet) == 3,
                       "op3 confirmed purchase must add a third amulet, got %d",
                       selftest_count_obj(player, obj_amulet));
        if( selftest_count_obj(player, obj_amulet) == 3 )
            imp_pass("postquest-op3", "OPNPC3", "amulet=3 confirmed");
    }

    /* ---- already-holding-4-beads shortcut: one conversation ---- */
    player->varps[varp_imp] = 0;
    player->varps[varp_settle] = 0;
    imp_clear_inv(player);
    imp_give_beads(player, obj_black, obj_white, obj_yellow, obj_red);
    qp_before = player->varps[varp_qp];
    xp_before = player->stat_xp_tenths[TORIRSSERVER_STAT_MAGIC];
    {
        const int choices[] = {1, 1};

        imp_talk(srv, player, npc_mizgog, miz_slot, choices, 2);
        SELFTEST_CHECK(player->varps[varp_imp] == 2,
                       "accepting with all four beads must complete in the same conversation, %%imp=%d",
                       player->varps[varp_imp]);
        SELFTEST_CHECK(player->varps[varp_settle] == 5,
                       "shortcut settlement must be fully receipted, got %d",
                       player->varps[varp_settle]);
        SELFTEST_CHECK(selftest_count_obj(player, obj_amulet) == 1,
                       "shortcut must grant the first amulet, got %d",
                       selftest_count_obj(player, obj_amulet));
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1,
                       "shortcut must award +1 QP");
        SELFTEST_CHECK(player->stat_xp_tenths[TORIRSSERVER_STAT_MAGIC] ==
                           xp_before + 8750,
                       "shortcut must award 875 Magic XP");
        if( player->varps[varp_imp] == 2 )
            imp_pass("shortcut-4beads", "OPNPC1", "imp=2 same conversation");
    }

    /* ---- Grayzag completed line ---- */
    imp_talk(srv, player, npc_grayzag, gray_slot, NULL, 0);
    imp_pass("grayzag-complete", "OPNPC1", "completed threat line");

    ToriRSServer_WorldNpcFree(srv, miz_slot);
    ToriRSServer_WorldNpcFree(srv, gray_slot);
    ToriRSServer_WorldNpcReap(srv);
    imp_clear_inv(player);
    player->varps[varp_imp] = 0;
    player->varps[varp_settle] = 0;
    player->godmode = 0;
    imp_pass("cleanup", "WorldNpcFree", "spawns reaped");
}
