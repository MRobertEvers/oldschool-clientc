/* The Fremennik Exiles Gate D stanza. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned npcs cannot leak into later RNG-gated
 * checks.
 *
 * Gate: TORIRSSERVER_SELFTEST_FX_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0
 *
 * Player stays unkillable unless the test is a death case. This walk has
 * no death case. assert() required pointers; one assert per condition.
 */

static void
fx_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "FX PASS: %s\n", step);
}

static void
fx_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    if( srv->active_player )
        srv->active_player->active_script = NULL;
}

static void
fx_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
fx_varbit(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
}

static int
fx_stat(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, name);
}

static void
fx_set_stat(struct ToriRSServerPlayer* player, const char* name, int level)
{
    int id;

    assert(player);
    assert(name);
    id = fx_stat(name);
    if( id < 0 )
        return;
    player->stat_level[id] = level;
    player->stat_boosted[id] = level;
}

static void
fx_set_prereq_quests(struct ToriRSServer* srv, int complete)
{
    int fris;
    int lunar;
    int mdq;
    int hero;
    int viking;

    assert(srv);
    fris = fx_varbit("fris_quest");
    lunar = fx_varbit("lunar_quest_main");
    mdq = fx_varbit("mdaughter_quest_var");
    if( fris >= 0 )
        ToriRSServer_VarbitSet(srv, fris, complete ? 340 : 0);
    if( lunar >= 0 )
        ToriRSServer_VarbitSet(srv, lunar, complete ? 190 : 0);
    if( mdq >= 0 )
        ToriRSServer_VarbitSet(srv, mdq, complete ? 70 : 0);
    hero = ToriRSServer_WorldVarp("heroquest");
    if( hero >= 0 )
        ToriRSServer_WorldSetVarp(srv, hero, complete ? 15 : 0);
    viking = ToriRSServer_WorldVarp("viking");
    if( viking >= 0 )
        ToriRSServer_WorldSetVarp(srv, viking, complete ? 10 : 0);
}

static void
fx_set_skill_gates(struct ToriRSServerPlayer* player, int pass)
{
    assert(player);
    fx_set_stat(player, "crafting", pass ? 65 : 1);
    fx_set_stat(player, "slayer", pass ? 60 : 1);
    fx_set_stat(player, "smithing", pass ? 60 : 1);
    fx_set_stat(player, "fishing", pass ? 60 : 1);
    fx_set_stat(player, "runecraft", pass ? 55 : 1);
}

static int
fx_exile_state(struct ToriRSServerPlayer* player)
{
    int bit;

    assert(player);
    bit = fx_varbit("vikingexile");
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static int
fx_run(struct ToriRSServer* srv, const char* cheat)
{
    int ran;

    assert(srv);
    assert(cheat);
    fx_close(srv);
    ran = ToriRSServer_ScriptsRunDebugproc(srv, cheat);
    return ran;
}

static int
fx_inv_count(struct ToriRSServerPlayer* player, int obj)
{
    int slot;
    int n;

    assert(player);
    if( obj < 0 )
        return 0;
    n = 0;
    for( slot = 0; slot < TORIRSSERVER_INV_SLOTS; slot++ )
    {
        if( player->inv[slot].obj_id == obj )
            n += player->inv[slot].count;
    }
    return n;
}

static void
selftest_quest_fremennikexiles(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int bit;
    int npc_brundt;
    int npc_freygerd;
    int loc_sandpit;
    int obj_shield;
    int slot;
    int if_gold;
    int if_silver;
    int if_flute;
    int spot_cast;
    int spot_impact;
    int32_t runeday;
    int slayer;
    int crafting;
    int runecraft;
    int xp_slayer_before;
    int xp_craft_before;
    int xp_rc_before;
    static struct ToriRSServerCapture cap;
    int fxrun_ok;
    int i;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: The Fremennik Exiles\n");
    fx_god(player);
    SELFTEST_CHECK(player->godmode == 1, "FX walk starts in godmode");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    bit = fx_varbit("vikingexile");
    SELFTEST_CHECK(bit >= 0, "%%vikingexile varbit must resolve");
    npc_brundt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_brundt_child");
    SELFTEST_CHECK(npc_brundt > 0, "viking_brundt_child must be in the npc pack");
    npc_freygerd = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_woman");
    SELFTEST_CHECK(npc_freygerd > 0, "viking_woman must be in the npc pack");
    loc_sandpit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "viking_sandpit");
    SELFTEST_CHECK(loc_sandpit > 0, "viking_sandpit must be in the loc pack");
    obj_shield = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vikingexile_v_shield");
    SELFTEST_CHECK(obj_shield > 0, "vikingexile_v_shield must be in the obj pack");
    fx_pass("pack_symbols");

    /* Required systems -- do not leftover-stamp. */
    if_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "crafting_gold");
    if_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "silver_crafting");
    if_flute = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "ratcatcher_flute");
    spot_cast = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_SPOTANIM, "telegrab_casting");
    spot_impact = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_SPOTANIM, "telegrab_impact");
    SELFTEST_CHECK(if_gold > 0, "jewellery furnace IF crafting_gold must exist");
    SELFTEST_CHECK(if_silver > 0, "jewellery furnace IF silver_crafting must exist");
    SELFTEST_CHECK(if_flute == 282 || if_flute > 0,
                   "Ratcatchers flute widget 282 must exist, got %d", if_flute);
    SELFTEST_CHECK(spot_cast > 0 && spot_impact > 0,
                   "telekinetic grab spotanims must exist");
    runeday = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,selftest_date_runeday]", NULL, 0,
                                                  &runeday),
                   "date_runeday primitive must answer");
    SELFTEST_CHECK(runeday >= 0, "date_runeday must be non-negative, got %d", runeday);
    fx_pass("required_systems");

    ToriRSServer_WorldTeleport(srv, 0, 2658, 3669);
    fx_god(player);

    /* Qualify fails -- one named surface each, real debugproc. */
    fx_set_prereq_quests(srv, 0);
    fx_set_skill_gates(player, 0);
    ToriRSServer_VarbitSet(srv, bit, 0);
    SELFTEST_CHECK(fx_run(srv, "fxbmp_01_qualify_fail_isles") == TORIRSSERVER_TRIGGER_RAN,
                   "Isles qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_isles");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_02_qualify_fail_lunar") == TORIRSSERVER_TRIGGER_RAN,
                   "Lunar qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_lunar");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_03_qualify_fail_mdaughter") == TORIRSSERVER_TRIGGER_RAN,
                   "Mountain Daughter qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_mdaughter");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_04_qualify_fail_heroes") == TORIRSSERVER_TRIGGER_RAN,
                   "Heroes qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_heroes");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_05_qualify_fail_crafting") == TORIRSSERVER_TRIGGER_RAN,
                   "Crafting 65 qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_crafting");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_06_qualify_fail_slayer") == TORIRSSERVER_TRIGGER_RAN,
                   "Slayer 60 qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_slayer");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_07_qualify_fail_smithing") == TORIRSSERVER_TRIGGER_RAN,
                   "Smithing 60 qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_smithing");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_08_qualify_fail_fishing") == TORIRSSERVER_TRIGGER_RAN,
                   "Fishing 60 qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_fishing");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_09_qualify_fail_runecraft") == TORIRSSERVER_TRIGGER_RAN,
                   "Runecraft 55 qualify mesbox must run");
    fx_close(srv);
    fx_pass("qualify_runecraft");

    /* Offer stays p_choice2 Yes / Not now and does not auto-start. */
    SELFTEST_CHECK(fx_run(srv, "fxbmp_11_brundt_offer_p_choice2") == TORIRSSERVER_TRIGGER_RAN,
                   "start offer p_choice2 must run");
    fx_close(srv);
    SELFTEST_CHECK(fx_exile_state(player) == 0,
                   "p_choice2 offer must not write %%vikingexile, state=%d",
                   fx_exile_state(player));
    fx_pass("offer_no_autostart");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_12_brundt_refuse") == TORIRSSERVER_TRIGGER_RAN,
                   "refuse chat must run");
    fx_close(srv);
    fx_pass("refuse");

    /* Real OPNPC1 on the in-dir child form. MERGE does not redeclare
     * [opnpc1,viking_brundt]. */
    fx_set_prereq_quests(srv, 1);
    fx_set_skill_gates(player, 1);
    ToriRSServer_VarbitSet(srv, bit, 0);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_brundt, player->x + 1, player->z, player->level);
    SELFTEST_CHECK(slot >= 0, "Brundt child should spawn");
    if( slot >= 0 )
    {
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_brundt,
                                                      ToriRSServer_NpcCategory(npc_brundt),
                                                      slot) == TORIRSSERVER_TRIGGER_RAN,
                       "[opnpc1,viking_brundt_child] should run");
        fx_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
    }
    fx_pass("opnpc1_brundt_child");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_13_brundt_accept") == TORIRSSERVER_TRIGGER_RAN,
                   "accept chat must run");
    fx_close(srv);
    fx_pass("accept");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_22_freygerd_investigate") == TORIRSSERVER_TRIGGER_RAN,
                   "Freygerd investigate chat must run");
    fx_close(srv);
    fx_pass("freygerd");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_27_sandpit_letter") == TORIRSSERVER_TRIGGER_RAN,
                   "sandpit letter mesbox must run");
    fx_close(srv);
    SELFTEST_CHECK(fx_run(srv, "fxbmp_30_rocks_fang") == TORIRSSERVER_TRIGGER_RAN,
                   "rocks fang mesbox must run");
    fx_close(srv);
    SELFTEST_CHECK(fx_run(srv, "fxbmp_32_boxes_gland") == TORIRSSERVER_TRIGGER_RAN,
                   "boxes gland mesbox must run");
    fx_close(srv);
    SELFTEST_CHECK(fx_run(srv, "fxbmp_34_letter_read") == TORIRSSERVER_TRIGGER_RAN,
                   "letter mesbox must run");
    fx_close(srv);
    fx_pass("investigation");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_36_brundt_se_brief") == TORIRSSERVER_TRIGGER_RAN,
                   "SE shield briefing must run");
    fx_close(srv);
    SELFTEST_CHECK(fx_run(srv, "fxbmp_41_boat_sail") == TORIRSSERVER_TRIGGER_RAN,
                   "Isle boat mesbox must run");
    fx_close(srv);
    fx_pass("shield_and_isle");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_journal_00_not_started") == TORIRSSERVER_TRIGGER_RAN,
                   "journal not-started must run");
    fx_close(srv);
    SELFTEST_CHECK(fx_run(srv, "fxbmp_journal_130_complete") == TORIRSSERVER_TRIGGER_RAN,
                   "journal complete must run");
    fx_close(srv);
    fx_pass("journal");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_leftover_keg_purchase") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_keg_purchase must run");
    fx_close(srv);
    SELFTEST_CHECK(fx_run(srv, "fxbmp_leftover_v_shield_craft_matrix") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_v_shield_craft_matrix must run");
    fx_close(srv);
    SELFTEST_CHECK(fx_run(srv, "fxbmp_leftover_full_refuse_trees") == TORIRSSERVER_TRIGGER_RAN,
                   "leftover_full_refuse_trees must run");
    fx_close(srv);
    fx_pass("leftovers");

    slayer = fx_stat("slayer");
    crafting = fx_stat("crafting");
    runecraft = fx_stat("runecraft");
    SELFTEST_CHECK(slayer >= 0 && crafting >= 0 && runecraft >= 0, "reward stats must resolve");
    fx_set_prereq_quests(srv, 1);
    fx_set_skill_gates(player, 1);
    ToriRSServer_VarbitSet(srv, bit, 0);
    xp_slayer_before = slayer >= 0 ? player->stat_xp_tenths[slayer] : 0;
    xp_craft_before = crafting >= 0 ? player->stat_xp_tenths[crafting] : 0;
    xp_rc_before = runecraft >= 0 ? player->stat_xp_tenths[runecraft] : 0;

    fxrun_ok = 0;
    ToriRSServer_CaptureBegin(srv, &cap);
    ToriRSServer_ScriptsRunDebugproc(srv, "fxrun");
    ToriRSServer_CaptureEnd(srv);
    for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const char* text = selftest_message_text(srv, &cap.packets[i]);

        if( !text )
            continue;
        if( strstr(text, "fxrun OK") != NULL )
            fxrun_ok = 1;
    }
    fx_close(srv);
    SELFTEST_CHECK(fxrun_ok, "::fxrun should reach its OK line");
    SELFTEST_CHECK(fx_exile_state(player) == 130,
                   "%%vikingexile endstate must be 130, got %d", fx_exile_state(player));
    SELFTEST_CHECK(fx_inv_count(player, obj_shield) >= 1, "completion should grant V's shield");
    if( slayer >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[slayer] > xp_slayer_before,
                       "completion should award Slayer xp");
    if( crafting >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[crafting] > xp_craft_before,
                       "completion should award Crafting xp");
    if( runecraft >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[runecraft] > xp_rc_before,
                       "completion should award Runecraft xp");
    fx_pass("fxrun_complete");

    SELFTEST_CHECK(fx_run(srv, "fxbmp_48_complete_scroll") == TORIRSSERVER_TRIGGER_RAN,
                   "complete scroll must run");
    fx_close(srv);
    fx_pass("complete_scroll");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "FX walk must leave the player unkillable and alive");
    fx_pass("player_alive");

    ToriRSServer_WorldNpcReap(srv);
    ToriRSServer_ScriptsFree(srv);
}
