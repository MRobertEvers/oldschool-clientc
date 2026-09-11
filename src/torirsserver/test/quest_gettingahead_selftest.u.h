/* Getting Ahead Gate D C-walk. Worker branch only — do not merge onto parent v3.
 *
 * Gate: TORIRSSERVER_SELFTEST_GA_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0
 * player->godmode = 1. Player is unkillable (no death case in this quest).
 *
 * Included immediately before the shop fprintf in
 * torirs_server_world_selftest.c. Also included from the GA_ONLY early gate.
 */
{
    int loaded;
    int ga_bit;
    int crafting;
    int construction;
    int coins;
    int gold_if;
    int silver_if;
    int flute_if;
    int telegrab;
    int32_t why;
    int32_t day;
    int craft_xp_before;
    int con_xp_before;
    int coins_before;
    static struct ToriRSServerCapture ga_capture;
    int garun_ok;
    int i;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Getting Ahead C-walk\n");

    player->godmode = 1;
    SELFTEST_CHECK(player->godmode == 1, "Getting Ahead C-walk leaves the player unkillable");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Getting Ahead C-walk loads a compiled script pack");
    if( loaded )
    {

    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    ToriRSServer_WorldSetActive(srv, player);

    ga_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ga");
    crafting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    construction = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    gold_if = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "crafting_gold");
    silver_if = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "silver_crafting");
    flute_if = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "ratcatcher_flute");
    telegrab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "magic_spellbook:telegrab");

    SELFTEST_CHECK(ga_bit >= 0, "varbit ga resolves");
    SELFTEST_CHECK(crafting >= 0, "stat crafting resolves");
    SELFTEST_CHECK(construction >= 0, "stat construction resolves");
    SELFTEST_CHECK(coins >= 0, "obj coins resolves");

    /* Required systems exist in-tree. Do not leftover-stamp them. */
    SELFTEST_CHECK(gold_if >= 0, "jewellery furnace IF crafting_gold resolves");
    SELFTEST_CHECK(silver_if >= 0, "jewellery furnace IF silver_crafting resolves");
    SELFTEST_CHECK(flute_if >= 0, "full flute widget ratcatcher_flute (interface 282) resolves");
    SELFTEST_CHECK(telegrab >= 0, "Telekinetic Grab magic_spellbook:telegrab resolves");

    day = 0;
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunProcInt(srv, "[proc,selftest_date_runeday]", NULL, 0, &day),
        "date_runeday() primitive answers");
    SELFTEST_CHECK(day > 0, "date_runeday() returns a positive runeday, got %d", day);

    /* Qualify uses stat_base, not boosted. Crafting is the first missing gate. */
    if( crafting >= 0 )
    {
        player->stat_level[crafting] = 1;
        player->stat_boosted[crafting] = 99;
    }
    if( construction >= 0 )
    {
        player->stat_level[construction] = 1;
        player->stat_boosted[construction] = 99;
    }
    if( ga_bit >= 0 )
        ToriRSServer_VarbitSet(srv, ga_bit, 0);

    why = -1;
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunProcInt(srv, "[proc,ga_qualify_fail_reason]", NULL, 0, &why),
        "ga_qualify_fail_reason runs");
    SELFTEST_CHECK(why == 1, "boosted Crafting 99 with base 1 fails the Crafting 30 gate, got %d", why);
    if( ga_bit >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, ga_bit) == 0,
                       "qualify fail does not write %%ga");

    /* Construction gate after Crafting base is met. */
    if( crafting >= 0 )
    {
        player->stat_level[crafting] = 30;
        player->stat_boosted[crafting] = 30;
    }
    why = -1;
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunProcInt(srv, "[proc,ga_qualify_fail_reason]", NULL, 0, &why),
        "ga_qualify_fail_reason runs for Construction");
    SELFTEST_CHECK(why == 2, "base Construction 1 fails the Construction 26 gate, got %d", why);

    /* Both bases met: qualify passes. Refuse must not start. */
    if( construction >= 0 )
    {
        player->stat_level[construction] = 26;
        player->stat_boosted[construction] = 26;
    }
    why = -1;
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunProcInt(srv, "[proc,ga_qualify_fail_reason]", NULL, 0, &why),
        "ga_qualify_fail_reason runs when both bases pass");
    SELFTEST_CHECK(why == 0, "Crafting 30 + Construction 26 qualifies, got %d", why);

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gabmp_05_gordon_refuse"),
                   "::gabmp_05_gordon_refuse parks the refuse chat");
    ToriRSServer_WorldCloseModal(srv);
    if( ga_bit >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, ga_bit) == 0,
                       "refuse leaves %%ga at 0");

    /* Named leftover debugprocs park on the disclosed mesboxes. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gabmp_leftover_beast_combat"),
                   "leftover_beast_combat is authored");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gabmp_leftover_flour_gate_lure"),
                   "leftover_flour_gate_lure is authored");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gabmp_leftover_tannery_unlock_ui"),
                   "leftover_tannery_unlock_ui is authored");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gabmp_leftover_full_gordon_mary_trees"),
                   "leftover_full_gordon_mary_trees is authored");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gabmp_leftover_construction_mount_hotspot"),
                   "leftover_construction_mount_hotspot is authored");
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gabmp_leftover_full_refuse_trees"),
                   "leftover_full_refuse_trees is authored");
    ToriRSServer_WorldCloseModal(srv);

    /* Headless walk: ::garun awards XP, coins, QP scroll, endstate 34. */
    craft_xp_before = (crafting >= 0) ? player->stat_xp_tenths[crafting] : 0;
    con_xp_before = (construction >= 0) ? player->stat_xp_tenths[construction] : 0;
    coins_before = (coins >= 0) ? selftest_count_obj(player, coins) : 0;

    garun_ok = 0;
    ToriRSServer_CaptureBegin(srv, &ga_capture);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "garun"), "::garun runs");
    ToriRSServer_CaptureEnd(srv);
    for( i = ToriRSServer_CaptureFindNamed(&ga_capture, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(&ga_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const struct ToriRSServerCapturedPacket* packet = &ga_capture.packets[i];
        const char* text = selftest_message_text(srv, packet);

        if( !text )
            continue;
        if( strstr(text, "garun OK") != NULL )
            garun_ok = 1;
    }
    SELFTEST_CHECK(garun_ok, "::garun should reach its OK line");
    if( ga_bit >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, ga_bit) == 34,
                       "::garun writes %%ga = 34, got %d",
                       ToriRSServer_VarbitGet(player, ga_bit));
    if( crafting >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[crafting] >= craft_xp_before + 40000,
                       "garun awards 40000 Crafting tenths (4000 XP)");
    if( construction >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[construction] >= con_xp_before + 32000,
                       "garun awards 32000 Construction tenths (3200 XP)");
    if( coins >= 0 )
        SELFTEST_CHECK(selftest_count_obj(player, coins) >= coins_before + 3000,
                       "garun grants 3000 coins via ~quest_complete_rewards(..., coins)");

    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gettingahead"),
                   "::gettingahead resets and parks at Gordon");
    if( ga_bit >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, ga_bit) == 0,
                       "::gettingahead writes %%ga = 0");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "gabmp_journal_34_complete"),
                   "journal complete debugproc runs");
    ToriRSServer_WorldCloseModal(srv);

    SELFTEST_CHECK(player->godmode == 1, "Getting Ahead C-walk never clears godmode");
    SELFTEST_CHECK(player->hitpoints > 0, "Getting Ahead C-walk does not kill the player");

    }
    if( getenv("TORIRSSERVER_SELFTEST_GA_ONLY") )
    {
        fprintf(stderr, "ToriRSServer Getting Ahead selftest: %lu checks, %d failures\n",
                g_selftest_checks, g_selftest_failures);
        selftest_evidence_end("gettingahead");
        return g_selftest_failures;
    }
}
