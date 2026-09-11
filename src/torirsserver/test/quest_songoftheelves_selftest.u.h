/* Song of the Elves Gate D stanza. Included from
 * torirs_server_world_selftest.c immediately before the shop stanza.
 *
 * Focused gate:
 *   TORIRSSERVER_SELFTEST_SOTE_ONLY=1 ./src/build_opt/torirsserver --selftest
 *
 * Player is unkillable for the whole walk (god 1 / TORIRSSERVER_GOD=1 /
 * player->godmode = 1). TORIRS_PLUGINS=0. Allocation failure is assert.
 * Required pointers are assert(), not silent return.
 */
static void
sote_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SOTE PASS: %s\n", step);
}

static void
sote_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
sote_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
sote_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    assert(stat >= 0);
    assert(stat < TORIRSSERVER_STAT_COUNT);
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static int
sote_stat(const char* name)
{
    int id;

    assert(name);
    id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, name);
    return id;
}

static void
selftest_quest_songoftheelves(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int vb_sote;
    int vb_mep2;
    int vb_mh;
    int vp_elena;
    int npc_edmond;
    int npc_elena;
    int npc_arianwyn;
    int npc_baxtorian;
    int npc_amlodd;
    int npc_ysgawyn;
    int npc_essyllt;
    int st_agi;
    int st_con;
    int st_farm;
    int st_herb;
    int st_hunt;
    int st_mine;
    int st_smith;
    int st_wc;
    int32_t why;
    int slot;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Song of the Elves (::soterun)\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "SOTE C-walk loads a compiled script pack");
    if( !loaded )
        return;

    vb_sote = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "sote");
    vb_mep2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mourning_quest_main");
    vb_mh = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "makinghistory_prog");
    vp_elena = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "elenaquest");
    npc_edmond = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "edmond_top");
    npc_elena = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sote_elena");
    npc_arianwyn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mourning_arianwyn_vis");
    npc_baxtorian = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sote_baxtorian_vis");
    npc_amlodd = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sote_lord_amlodd_vis");
    npc_ysgawyn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sote_ysgawyn_vis");
    npc_essyllt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sote_essyllt_combat");
    st_agi = sote_stat("agility");
    st_con = sote_stat("construction");
    st_farm = sote_stat("farming");
    st_herb = sote_stat("herblore");
    st_hunt = sote_stat("hunter");
    st_mine = sote_stat("mining");
    st_smith = sote_stat("smithing");
    st_wc = sote_stat("woodcutting");

    SELFTEST_CHECK(vb_sote >= 0 && vb_mep2 >= 0 && vb_mh >= 0 && vp_elena >= 0 &&
                       npc_edmond >= 0 && npc_elena >= 0 && npc_arianwyn >= 0 &&
                       npc_baxtorian >= 0 && npc_amlodd >= 0 && npc_ysgawyn >= 0 &&
                       npc_essyllt >= 0 && st_agi >= 0 && st_con >= 0 &&
                       st_farm >= 0 && st_herb >= 0 && st_hunt >= 0 &&
                       st_mine >= 0 && st_smith >= 0 && st_wc >= 0,
                   "SOTE C-walk names should all resolve");
    if( vb_sote < 0 || npc_edmond < 0 )
    {
        ToriRSServer_ScriptsFree(srv);
        return;
    }

    sote_god(player);
    player->varps[vp_elena] = 29; /* ^elena_complete */
    ToriRSServer_VarbitSet(srv, vb_sote, 0);
    ToriRSServer_VarbitSet(srv, vb_mep2, 0);
    ToriRSServer_VarbitSet(srv, vb_mh, 0);
    sote_set_stat(player, st_agi, 1);
    sote_set_stat(player, st_con, 1);
    sote_set_stat(player, st_farm, 1);
    sote_set_stat(player, st_herb, 1);
    sote_set_stat(player, st_hunt, 1);
    sote_set_stat(player, st_mine, 1);
    sote_set_stat(player, st_smith, 1);
    sote_set_stat(player, st_wc, 1);

    ToriRSServer_WorldTeleport(srv, 0, 2566, 3337);
    selftest_tick(srv);

    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 1,
                   "MEP2 is the first named qualify fail, got %d", (int)why);
    sote_pass("qualify_fail_mep2");
    sote_close(srv);

    ToriRSServer_VarbitSet(srv, vb_mep2, 60);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 2,
                   "Making History is the second named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_makinghistory");
    sote_close(srv);

    ToriRSServer_VarbitSet(srv, vb_mh, 4);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 3,
                   "Agility 70 is the third named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_agility");
    sote_set_stat(player, st_agi, 70);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 4,
                   "Construction 70 is the fourth named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_construction");
    sote_set_stat(player, st_con, 70);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 5,
                   "Farming 70 is the fifth named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_farming");
    sote_set_stat(player, st_farm, 70);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 6,
                   "Herblore 70 is the sixth named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_herblore");
    sote_set_stat(player, st_herb, 70);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 7,
                   "Hunter 70 is the seventh named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_hunter");
    sote_set_stat(player, st_hunt, 70);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 8,
                   "Mining 70 is the eighth named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_mining");
    sote_set_stat(player, st_mine, 70);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 9,
                   "Smithing 70 is the ninth named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_smithing");
    sote_set_stat(player, st_smith, 70);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 10,
                   "Woodcutting 70 is the tenth named qualify fail, got %d",
                   (int)why);
    sote_pass("qualify_fail_woodcutting");
    sote_set_stat(player, st_wc, 70);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,sote_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 0,
                   "all ten gates open, got %d", (int)why);
    sote_pass("qualify_ok");

    ToriRSServer_VarbitSet(srv, vb_sote, 0);
    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,sote_refuse]", NULL, 0);
    sote_close(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 0,
                   "refuse must not start the quest, got %d",
                   ToriRSServer_VarbitGet(player, vb_sote));
    sote_pass("edmond_refuse");

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,sote_accept]", NULL, 0);
    sote_close(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 2,
                   "accept writes ^sote_king=2, got %d",
                   ToriRSServer_VarbitGet(player, vb_sote));
    sote_pass("edmond_accept");

    slot = ToriRSServer_WorldNpcSpawn(srv, npc_edmond, 2566, 3337, 0);
    SELFTEST_CHECK(slot >= 0, "edmond_top should spawn");
    if( slot >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_sote, 2);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_edmond, -1, slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 4,
                       "opnpc1 edmond at king writes edmond2=4, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("edmond_king_to_edmond2");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_edmond, -1, slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 10,
                       "opnpc1 edmond at edmond2 writes alrena=10, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("edmond_alrena");
    }

    slot = ToriRSServer_WorldNpcSpawn(srv, npc_elena, 2566, 3337, 0);
    SELFTEST_CHECK(slot >= 0, "sote_elena should spawn");
    if( slot >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_sote, 20);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_elena, -1, slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 22,
                       "opnpc1 elena at cell writes acid=22, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("elena_acid");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_elena, -1, slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 24,
                       "opnpc1 elena at acid writes free=24, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("elena_free");
    }

    slot = ToriRSServer_WorldNpcSpawn(srv, npc_arianwyn, 2566, 3337, 0);
    SELFTEST_CHECK(slot >= 0, "mourning_arianwyn_vis should spawn");
    if( slot >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_sote, 62);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_arianwyn, -1,
                                       slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 66,
                       "opnpc1 arianwyn at arianwyn2 writes bax=66, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("arianwyn_bax");
    }

    slot = ToriRSServer_WorldNpcSpawn(srv, npc_baxtorian, 2566, 3337, 0);
    SELFTEST_CHECK(slot >= 0, "sote_baxtorian_vis should spawn");
    if( slot >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_sote, 66);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_baxtorian, -1,
                                       slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 74,
                       "opnpc1 baxtorian at bax writes elena_bax=74, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("baxtorian_puzzle");
    }

    slot = ToriRSServer_WorldNpcSpawn(srv, npc_amlodd, 2566, 3337, 0);
    SELFTEST_CHECK(slot >= 0, "sote_lord_amlodd_vis should spawn");
    if( slot >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_sote, 90);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_amlodd, -1, slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 110,
                       "opnpc1 amlodd at amlodd writes library_done=110, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("amlodd_seals");
    }

    slot = ToriRSServer_WorldNpcSpawn(srv, npc_ysgawyn, 2566, 3337, 0);
    SELFTEST_CHECK(slot >= 0, "sote_ysgawyn_vis should spawn");
    if( slot >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_sote, 58);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ysgawyn, -1,
                                       slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 59,
                       "opnpc1 ysgawyn at ys writes elena_lletya=59, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("ysgawyn_elena");
    }

    slot = ToriRSServer_WorldNpcSpawn(srv, npc_essyllt, 2566, 3337, 0);
    SELFTEST_CHECK(slot >= 0, "sote_essyllt_combat should spawn");
    if( slot >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_sote, 176);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_essyllt, -1,
                                       slot);
        selftest_click_through(srv, 16);
        sote_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 186,
                       "opnpc1 essyllt writes final=186, got %d",
                       ToriRSServer_VarbitGet(player, vb_sote));
        sote_pass("essyllt_final");
    }

    (void)ToriRSServer_ScriptsRunProc(
        srv, "[proc,sote_leftover_ardougne_revolt_combat]", NULL, 0);
    sote_close(srv);
    sote_pass("leftover_ardougne_revolt_combat");
    (void)ToriRSServer_ScriptsRunProc(
        srv, "[proc,sote_leftover_baxtorian_statue_puzzle]", NULL, 0);
    sote_close(srv);
    sote_pass("leftover_baxtorian_statue_puzzle");
    (void)ToriRSServer_ScriptsRunProc(
        srv, "[proc,sote_leftover_clan_light_puzzles]", NULL, 0);
    sote_close(srv);
    sote_pass("leftover_clan_light_puzzles");
    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,sote_leftover_orb_guards]",
                                      NULL, 0);
    sote_close(srv);
    sote_pass("leftover_orb_guards");
    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,sote_leftover_lletya_siege]",
                                      NULL, 0);
    sote_close(srv);
    sote_pass("leftover_lletya_siege");
    (void)ToriRSServer_ScriptsRunProc(
        srv, "[proc,sote_leftover_dwarf_pass_defence]", NULL, 0);
    sote_close(srv);
    sote_pass("leftover_dwarf_pass_defence");
    (void)ToriRSServer_ScriptsRunProc(
        srv, "[proc,sote_leftover_essyllt_seren_final]", NULL, 0);
    sote_close(srv);
    sote_pass("leftover_essyllt_seren_final");
    (void)ToriRSServer_ScriptsRunProc(
        srv, "[proc,sote_leftover_full_refuse_trees]", NULL, 0);
    sote_close(srv);
    sote_pass("leftover_full_refuse_trees");

    ToriRSServer_VarbitSet(srv, vb_sote, 0);
    ToriRSServer_ScriptsRunDebugproc(srv, "soterun");
    selftest_click_through(srv, 32);
    sote_close(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_sote) == 200,
                   "soterun writes ^sote_complete=200, got %d",
                   ToriRSServer_VarbitGet(player, vb_sote));
    sote_pass("soterun_complete");

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,songoftheelves_journal]", NULL,
                                      0);
    sote_close(srv);
    sote_pass("journal_complete");

    ToriRSServer_ScriptsFree(srv);
}
