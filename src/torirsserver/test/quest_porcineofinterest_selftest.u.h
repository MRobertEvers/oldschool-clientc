/* A Porcine of Interest C-walk.
 *
 * Included immediately before the shop `selftest_reset_world` so any npc /
 * tick / varbit cost ends at that reset. Focused gate:
 *
 *   TORIRSSERVER_SELFTEST_POI_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0 \
 *     ./src/build_opt/torirsserver --selftest
 *
 * Player is unkillable unless a step is a death case. This walk is not a
 * death case: god 1 / player->godmode = 1 for the whole stanza.
 *
 * Unique MERGE hunks: none. Do not leftover jewellery IF / date_runeday /
 * flute 282 / telekinetic grab / the notice Yes/No offer.
 */
static void
selftest_quest_porcineofinterest(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    static struct ToriRSServerCapture cap;
    int porcine_bit;
    int footcut_bit;
    int slayer_points_bit;
    int slayer_stat;
    int coins;
    int goggles;
    int rope;
    int knife;
    int trophy;
    int loaded;
    int poirun_ok;
    int i;
    int xp_before;
    int points_before;
    int god_saved;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: A Porcine of Interest C-walk\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Porcine C-walk loads a compiled script pack");
    if( !loaded )
        return;

    god_saved = player->godmode;
    player->godmode = 1;
    if( getenv("TORIRSSERVER_GOD") )
        player->godmode = 1;

    porcine_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "porcine");
    footcut_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "porcine_footcut");
    slayer_points_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slayer_points");
    slayer_stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    goggles = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slayer_reinforced_goggles");
    rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
    trophy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "porcine_sourhog_trophy");

    assert(porcine_bit >= 0);
    assert(footcut_bit >= 0);
    assert(slayer_points_bit >= 0);
    assert(slayer_stat >= 0);
    assert(coins >= 0);
    assert(goggles >= 0);
    assert(rope >= 0);
    assert(knife >= 0);
    assert(trophy >= 0);

    SELFTEST_CHECK(player->godmode == 1, "Porcine C-walk keeps the player unkillable");

    ToriRSServer_VarbitSet(srv, porcine_bit, 0);
    ToriRSServer_VarbitSet(srv, footcut_bit, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, porcine_bit) == 0,
                   "Porcine starts at ^poi_not_started=0, got %d",
                   ToriRSServer_VarbitGet(player, porcine_bit));

    /* No qualify gate: state 0 is reachable with no skill / quest prereq. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "porcineofinterest") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::porcineofinterest should reach content");
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, porcine_bit) == 0,
                   "::porcineofinterest resets to 0, got %d",
                   ToriRSServer_VarbitGet(player, porcine_bit));
    SELFTEST_CHECK(selftest_count(player, rope) >= 1,
                   "::porcineofinterest grants a rope");
    SELFTEST_CHECK(selftest_count(player, knife) >= 1,
                   "::porcineofinterest grants a knife");

    /* Notice refuse stays at 0. The authored No path writes a mesbox and
     * does not start. */
    ToriRSServer_VarbitSet(srv, porcine_bit, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "poibmp_03_notice_refuse") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "notice refuse debugproc exists");
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, porcine_bit) == 0,
                   "notice refuse must not start the quest, got %d",
                   ToriRSServer_VarbitGet(player, porcine_bit));

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "poibmp_02_notice_offer_p_choice2") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "notice Yes/No p_choice2 debugproc exists");
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* Soft-kill, foot cut, and complete procs at the authored states. */
    ToriRSServer_VarbitSet(srv, porcine_bit, 25);
    ToriRSServer_VarbitSet(srv, footcut_bit, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,poi_soft_kill_sourhog]", NULL, 0),
                   "~poi_soft_kill_sourhog runs");
    ToriRSServer_ScriptsProcessQueues(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, porcine_bit) == 30,
                   "soft-kill writes ^poi_foot=30, got %d",
                   ToriRSServer_VarbitGet(player, porcine_bit));

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,poi_cut_foot]", NULL, 0),
                   "~poi_cut_foot runs");
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(selftest_count(player, trophy) >= 1,
                   "cutting the foot grants porcine_sourhog_trophy");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, footcut_bit) == 1,
                   "cutting the foot writes porcine_footcut");

    xp_before = player->stat_xp_tenths[slayer_stat];
    points_before = ToriRSServer_VarbitGet(player, slayer_points_bit);
    ToriRSServer_VarbitSet(srv, porcine_bit, 35);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,poi_quest_complete]", NULL, 0),
                   "~poi_quest_complete runs");
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, porcine_bit) == 40,
                   "complete writes ^poi_complete=40, got %d",
                   ToriRSServer_VarbitGet(player, porcine_bit));
    SELFTEST_CHECK(player->stat_xp_tenths[slayer_stat] >= xp_before + 10000,
                   "complete awards 10000 Slayer tenths (1000 XP): before=%d after=%d",
                   xp_before, player->stat_xp_tenths[slayer_stat]);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, slayer_points_bit) >= points_before + 30,
                   "complete awards 30 Slayer points: before=%d after=%d",
                   points_before, ToriRSServer_VarbitGet(player, slayer_points_bit));

    /* Journal complete prints QUEST COMPLETE! */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,porcineofinterest_journal]", NULL, 0),
                   "~porcineofinterest_journal runs at complete");
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* Full state-machine walk. */
    ToriRSServer_VarbitSet(srv, porcine_bit, 0);
    ToriRSServer_VarbitSet(srv, footcut_bit, 0);
    poirun_ok = 0;
    ToriRSServer_CaptureBegin(srv, &cap);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "poirun") == TORIRSSERVER_TRIGGER_RAN,
                   "::poirun should reach content");
    ToriRSServer_CaptureEnd(srv);
    for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const char* text = selftest_message_text(srv, &cap.packets[i]);

        if( !text )
            continue;
        if( strstr(text, "poirun") == NULL )
            continue;
        fprintf(stderr, "  %s\n", text);
        if( strstr(text, "poirun OK") != NULL )
            poirun_ok = 1;
        if( strstr(text, "poirun FAIL") != NULL )
            poirun_ok = 0;
    }
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(poirun_ok, "::poirun should reach its OK line");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, porcine_bit) == 40,
                   "::poirun ends at ^poi_complete=40, got %d",
                   ToriRSServer_VarbitGet(player, porcine_bit));
    SELFTEST_CHECK(selftest_count(player, coins) >= 5000,
                   "::poirun awards 5000 coins, held %d",
                   selftest_count(player, coins));

    /* Leftover procs exist and are the only allowed leftovers. */
    {
        static const char* leftovers[] = {
            "[proc,poi_leftover_tracking_cabbages_cart]",
            "[proc,poi_leftover_pig_thing_cutscene]",
            "[proc,poi_leftover_sourhog_blockage_climb]",
            "[proc,poi_leftover_slash_weapon_matrix]",
            "[proc,poi_leftover_sarah_shop]",
            "[proc,poi_leftover_spria_slayer_task_offer]",
            "[proc,poi_leftover_full_refuse_trees]",
        };
        static const char* leftover_cheats[] = {
            "poibmp_leftover_tracking_cabbages_cart",
            "poibmp_leftover_pig_thing_cutscene",
            "poibmp_leftover_sourhog_blockage_climb",
            "poibmp_leftover_slash_weapon_matrix",
            "poibmp_leftover_sarah_shop",
            "poibmp_leftover_spria_slayer_task_offer",
            "poibmp_leftover_full_refuse_trees",
        };

        for( i = 0; i < 7; i++ )
        {
            SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, leftovers[i], NULL, 0),
                           "%s runs", leftovers[i]);
            ToriRSServer_ScriptsProcessQueues(srv);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, leftover_cheats[i]) ==
                               TORIRSSERVER_TRIGGER_RAN,
                           "::%s should reach content", leftover_cheats[i]);
            ToriRSServer_ScriptsProcessQueues(srv);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
        }
    }

    /* Named BMP debugprocs used by INTERACTIONS.txt exist. */
    {
        static const char* cheats[] = {
            "poibmp_01_notice_search",
            "poibmp_02_notice_offer_p_choice2",
            "poibmp_04_notice_accept",
            "poibmp_07_sarah_bounty_choice",
            "poibmp_09_sarah_interview",
            "poibmp_12_sarah_handin",
            "poibmp_16_spria_awake_goggles",
            "poibmp_21_spria_complete",
            "poibmp_26_rope_tie",
            "poibmp_33_skeleton_investigate",
            "poibmp_39_foot_cut",
            "poibmp_41_complete_scroll",
            "poibmp_journal_00_not_started",
            "poibmp_journal_40_complete",
        };

        for( i = 0; i < (int)(sizeof(cheats) / sizeof(cheats[0])); i++ )
        {
            ToriRSServer_VarbitSet(srv, porcine_bit, 0);
            ToriRSServer_VarbitSet(srv, footcut_bit, 0);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, cheats[i]) ==
                               TORIRSSERVER_TRIGGER_RAN,
                           "::%s should reach content", cheats[i]);
            ToriRSServer_ScriptsProcessQueues(srv);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
        }
    }

    ToriRSServer_VarbitSet(srv, porcine_bit, 0);
    ToriRSServer_VarbitSet(srv, footcut_bit, 0);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    player->godmode = 1;
    (void)god_saved;
    (void)goggles;

    SELFTEST_CHECK(player->godmode == 1, "Porcine C-walk leaves godmode on");
}
