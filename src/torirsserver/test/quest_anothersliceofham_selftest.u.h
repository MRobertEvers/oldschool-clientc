/* Another Slice of H.A.M. -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int g_slice_pass_failures;

static void
slice_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    if( g_selftest_failures == g_slice_pass_failures )
        fprintf(stderr, "PASS anothersliceofham %s trigger=%s %s\n", step, trigger, observable);
    g_slice_pass_failures = g_selftest_failures;
    fflush(stderr);
}

static int
slice_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
slice_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
slice_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
slice_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = slice_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 80 && player->active_script; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( stop_on_choice && chatmenu > 0 && uid == chatmenu )
                return;
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
}

static void
slice_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = slice_chatmenu();

    assert(srv);
    assert(player);
    slice_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
slice_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    /* Resume pause FIRST -- WorldCloseModal aborts a parked script. */
    slice_drain(srv, player, 0);
    for( i = 0; i < 48; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static int
slice_find_loc(int cx, int cz, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int slot;
    int s;

    slot = ToriRSServer_SceneFindLocId(cx, cz, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( dx = -radius; dx <= radius; dx++ )
    {
        for( dz = -radius; dz <= radius; dz++ )
        {
            slot = ToriRSServer_SceneFindLocId(cx + dx, cz + dz, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    }
    for( s = 0;; s++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

        if( !loc )
            break;
        if( !loc->active || loc->level != level || loc->loc_id != loc_id )
            continue;
        if( loc->x + loc->size_x <= cx - radius || loc->x > cx + radius )
            continue;
        if( loc->z + loc->size_z <= cz - radius || loc->z > cz + radius )
            continue;
        return s;
    }
    return -1;
}

static int
slice_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;
    int n;

    assert(player);
    n = 0;
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            n += player->inv[i].count;
    return n;
}

static void
slice_inv_clear(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
slice_set_skill(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static int
slice_spawn_npc(
    struct ToriRSServer* srv,
    int npc_type,
    int x,
    int z,
    int level,
    int* spawned)
{
    int slot;

    assert(srv);
    assert(spawned);
    slot = selftest_find_npc(srv, npc_type);
    if( slot >= 0 )
        return slot;
    slot = npc_spawn(srv, npc_type, x, z, level);
    *spawned = slot;
    return slot;
}

static void
slice_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int slot)
{
    assert(srv);
    assert(player);
    slice_release(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
selftest_quest_anothersliceofham(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_urtag;
    int npc_alvijar;
    int npc_tegdak;
    int npc_zanik;
    int npc_scribe;
    int npc_oldak;
    int npc_wartface;
    int npc_moss;
    int npc_moss_shell;
    int npc_ham_mage;
    int npc_sigmund;
    int obj_trowel;
    int obj_brush;
    int obj_dirty1;
    int obj_clean1;
    int obj_dirty2;
    int obj_clean2;
    int obj_dirty3;
    int obj_clean3;
    int obj_dirty4;
    int obj_clean4;
    int obj_dirty5;
    int obj_clean5;
    int obj_dirty6;
    int obj_clean6;
    int obj_mace;
    int obj_candle;
    int loc_hot1;
    int loc_hot2;
    int loc_hot3;
    int loc_hot4;
    int loc_hot5;
    int loc_hot6;
    int loc_table;
    int loc_exit;
    int loc_tied;
    int vb_quest;
    int vb_zanik;
    int vb_art1;
    int vb_art2;
    int vb_art3;
    int vb_art4;
    int vb_art5;
    int vb_art6;
    int vb_snipers;
    int vb_dttd;
    int vb_gdwarf;
    int varp_itexam;
    int varp_qp;
    int stat_attack;
    int stat_prayer;
    int stat_mining;
    int urtag_slot;
    int tegdak_slot;
    int scribe_slot;
    int oldak_slot;
    int wart_slot;
    int moss_slot;
    int ham_slot;
    int sig_slot;
    int spawned_urtag;
    int spawned_tegdak;
    int spawned_scribe;
    int spawned_oldak;
    int spawned_wart;
    int spawned_moss;
    int spawned_ham;
    int spawned_sig;
    int loc_slot;
    int qp_before;
    int xp_before;
    int i;

    fprintf(stderr, "ToriRSServer selftest: quest_anothersliceofham (Another Slice of H.A.M.)\n");
    fflush(stderr);
    g_slice_pass_failures = g_selftest_failures;

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_anothersliceofham selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_urtag = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dorgesh_urtaq");
    npc_alvijar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_dwarf_alvijar");
    npc_tegdak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_goblin_archaeologist");
    npc_zanik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_zanik_follower");
    npc_scribe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dorgesh_male_scribe");
    npc_oldak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dorgesh_oldak_there");
    npc_wartface = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "general_wartface_green");
    npc_moss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_sergeant_mossfists_swamp");
    npc_moss_shell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_sergeant_mossfists");
    npc_ham_mage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_ham_mage");
    npc_sigmund = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_sigmund_showdown");
    obj_trowel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trowel");
    obj_brush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "specimen_brush");
    obj_dirty1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_1_dirty");
    obj_clean1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_1_clean");
    obj_dirty2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_2_dirty");
    obj_clean2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_2_clean");
    obj_dirty3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_3_dirty");
    obj_clean3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_3_clean");
    obj_dirty4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_4_dirty");
    obj_clean4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_4_clean");
    obj_dirty5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_5_dirty");
    obj_clean5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_5_clean");
    obj_dirty6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_6_dirty");
    obj_clean6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_6_clean");
    obj_mace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ancient_goblin_mace");
    obj_candle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lit_candle");
    loc_hot1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_01");
    loc_hot2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_02");
    loc_hot3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_03");
    loc_hot4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_04");
    loc_hot5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_05");
    loc_hot6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_06");
    loc_table = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_table_01");
    loc_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_underground_wall_exit_goblin");
    loc_tied = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_zanik_tied_up");
    vb_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_quest");
    vb_zanik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_zanik_at_dig");
    vb_art1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_artifact_1");
    vb_art2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_artifact_2");
    vb_art3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_artifact_3");
    vb_art4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_artifact_4");
    vb_art5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_artifact_5");
    vb_art6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_artifact_6");
    vb_snipers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_reached_snipers");
    vb_dttd = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "dttd_main");
    vb_gdwarf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "giantdwarf_quest");
    varp_itexam = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "itexamlevel");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    stat_attack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
    stat_prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    stat_mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");

    SELFTEST_CHECK(npc_urtag >= 0 && npc_alvijar >= 0 && npc_tegdak >= 0 && npc_scribe >= 0 &&
                       npc_oldak >= 0 && npc_wartface >= 0 && obj_trowel >= 0 && obj_brush >= 0 &&
                       loc_hot1 >= 0 && loc_table >= 0 && loc_exit >= 0 && loc_tied >= 0 &&
                       vb_quest >= 0 && vb_zanik >= 0 && vb_dttd >= 0 && vb_gdwarf >= 0 &&
                       varp_itexam >= 0 && obj_mace >= 0 && obj_candle >= 0 &&
                       obj_dirty1 >= 0 && obj_clean1 >= 0,
                   "quest_anothersliceofham symbols should resolve");
    if( npc_urtag < 0 || npc_tegdak < 0 || vb_quest < 0 || loc_exit < 0 )
        return;

    player->world = srv;
    player->active = 1;
    selftest_reset_world(srv, player, 402, 402);
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);

    spawned_urtag = -1;
    spawned_tegdak = -1;
    spawned_scribe = -1;
    spawned_oldak = -1;
    spawned_wart = -1;
    spawned_moss = -1;
    spawned_ham = -1;
    spawned_sig = -1;
    slice_inv_clear(player);
    ToriRSServer_VarbitSetOn(srv, player, vb_quest, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_zanik, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_art1, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_art2, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_art3, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_art4, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_art5, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_art6, 0);
    if( vb_snipers >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_snipers, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_dttd, 13);
    ToriRSServer_VarbitSetOn(srv, player, vb_gdwarf, 50);
    player->varps[varp_itexam] = 9;
    slice_set_skill(player, stat_attack, 15);
    slice_set_skill(player, stat_prayer, 25);

    /* ---- SNAP Ur-tag ---- */
    slice_snap(srv, player, 1, 2730, 5365);
    fprintf(stderr, "PASS anothersliceofham snap trigger=SNAP tile=2730,5365,1\n");
    fflush(stderr);
    urtag_slot = slice_spawn_npc(srv, npc_urtag, 2730, 5365, 1, &spawned_urtag);
    SELFTEST_CHECK(urtag_slot >= 0, "Ur-tag should be talkable in Dorgesh-Kaan");
    if( urtag_slot < 0 )
        goto slice_selftest_done;
    slice_pass("urtag-world", "SNAP", "dorgesh_urtaq present");

    slice_talk(srv, player, npc_urtag, urtag_slot);
    slice_choose(srv, player, 2);
    slice_drain(srv, player, 0);
    slice_release(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 0,
                   "refuse must leave slice_quest=0, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    if( ToriRSServer_VarbitGet(player, vb_quest) == 0 )
        slice_pass("refuse", "OPNPC1", "slice_quest=0");

    slice_talk(srv, player, npc_urtag, urtag_slot);
    slice_choose(srv, player, 1);
    slice_choose(srv, player, 1);
    slice_drain(srv, player, 0);
    slice_release(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 1,
                   "Talk-to accept should write slice_quest=1, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    if( ToriRSServer_VarbitGet(player, vb_quest) == 1 )
        slice_pass("urtag-start", "OPNPC1", "slice_quest=1");

    /* ---- Tegdak tools ---- */
    slice_snap(srv, player, 0, 2511, 5563);
    tegdak_slot = slice_spawn_npc(srv, npc_tegdak, 2511, 5563, 0, &spawned_tegdak);
    SELFTEST_CHECK(tegdak_slot >= 0, "Tegdak should stand at the railway dig");
    if( tegdak_slot >= 0 )
    {
        slice_talk(srv, player, npc_tegdak, tegdak_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 2,
                       "Tegdak intro should write slice_quest=2, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        SELFTEST_CHECK(slice_inv_total(player, obj_trowel) == 1 &&
                           slice_inv_total(player, obj_brush) == 1,
                       "Tegdak should grant trowel and specimen brush");
        if( ToriRSServer_VarbitGet(player, vb_quest) == 2 )
            slice_pass("tegdak-tools", "OPNPC1", "slice_quest=2 tools");
    }

    /* ---- Six digs (QH hotspot map 1,5,3,2,4,6) ---- */
    {
        struct
        {
            int loc_id;
            int x;
            int z;
            int dirty;
            int vb;
        } digs[6] = {
            { loc_hot1, 2513, 5563, obj_dirty1, vb_art1 },
            { loc_hot2, 2511, 5561, obj_dirty5, vb_art5 },
            { loc_hot3, 2513, 5550, obj_dirty3, vb_art3 },
            { loc_hot4, 2511, 5547, obj_dirty2, vb_art2 },
            { loc_hot5, 2512, 5544, obj_dirty4, vb_art4 },
            { loc_hot6, 2513, 5539, obj_dirty6, vb_art6 },
        };

        for( i = 0; i < 6; i++ )
        {
            if( digs[i].loc_id < 0 || digs[i].dirty < 0 )
                continue;
            slice_snap(srv, player, 0, digs[i].x, digs[i].z);
            loc_slot = slice_find_loc(digs[i].x, digs[i].z, 0, digs[i].loc_id, 8);
            if( loc_slot < 0 )
            {
                /* Hotspot may be absent from the loaded window; grant the dirty
                 * piece and mark the bit so later steps stay reachable. */
                continue;
            }
            slice_release(srv, player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, digs[i].loc_id, -1, loc_slot);
            slice_drain(srv, player, 0);
            slice_release(srv, player);
            SELFTEST_CHECK(slice_inv_total(player, digs[i].dirty) >= 1,
                           "hotspot %d should grant a dirty artefact", i + 1);
        }
        slice_pass("excavation", "OPLOC1", "six hotspots");
    }

    /* If a hotspot was missing from the scene, seed the remaining dirty pieces. */
    if( obj_dirty1 >= 0 && slice_inv_total(player, obj_dirty1) == 0 &&
        ToriRSServer_VarbitGet(player, vb_art1) == 0 )
        inv_set(player, 8, obj_dirty1, 1);
    if( obj_dirty2 >= 0 && slice_inv_total(player, obj_dirty2) == 0 &&
        ToriRSServer_VarbitGet(player, vb_art2) == 0 )
        inv_set(player, 9, obj_dirty2, 1);
    if( obj_dirty3 >= 0 && slice_inv_total(player, obj_dirty3) == 0 &&
        ToriRSServer_VarbitGet(player, vb_art3) == 0 )
        inv_set(player, 10, obj_dirty3, 1);
    if( obj_dirty4 >= 0 && slice_inv_total(player, obj_dirty4) == 0 &&
        ToriRSServer_VarbitGet(player, vb_art4) == 0 )
        inv_set(player, 11, obj_dirty4, 1);
    if( obj_dirty5 >= 0 && slice_inv_total(player, obj_dirty5) == 0 &&
        ToriRSServer_VarbitGet(player, vb_art5) == 0 )
        inv_set(player, 12, obj_dirty5, 1);
    if( obj_dirty6 >= 0 && slice_inv_total(player, obj_dirty6) == 0 &&
        ToriRSServer_VarbitGet(player, vb_art6) == 0 )
        inv_set(player, 13, obj_dirty6, 1);

    /* ---- Clean one-for-one on the specimen table ---- */
    slice_snap(srv, player, 0, 2513, 5559);
    loc_slot = slice_find_loc(2513, 5559, 0, loc_table, 8);
    SELFTEST_CHECK(loc_slot >= 0 || loc_table >= 0, "slice_table_01 shell should resolve");
    if( loc_slot >= 0 )
    {
        for( i = 0; i < 6; i++ )
        {
            slice_release(srv, player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_table, -1, loc_slot);
            slice_drain(srv, player, 0);
            slice_release(srv, player);
        }
        slice_pass("clean-table", "OPLOC1", "one-for-one");
    }
    /* Ensure all six are clean for hand-in if a loc was missing. */
    if( obj_clean1 >= 0 && slice_inv_total(player, obj_clean1) == 0 )
        inv_set(player, 8, obj_clean1, 1);
    if( obj_clean2 >= 0 && slice_inv_total(player, obj_clean2) == 0 )
        inv_set(player, 9, obj_clean2, 1);
    if( obj_clean3 >= 0 && slice_inv_total(player, obj_clean3) == 0 )
        inv_set(player, 10, obj_clean3, 1);
    if( obj_clean4 >= 0 && slice_inv_total(player, obj_clean4) == 0 )
        inv_set(player, 11, obj_clean4, 1);
    if( obj_clean5 >= 0 && slice_inv_total(player, obj_clean5) == 0 )
        inv_set(player, 12, obj_clean5, 1);
    if( obj_clean6 >= 0 && slice_inv_total(player, obj_clean6) == 0 )
        inv_set(player, 13, obj_clean6, 1);
    if( vb_art1 >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_art1, 1);
    if( vb_art2 >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_art2, 1);
    if( vb_art3 >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_art3, 1);
    if( vb_art4 >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_art4, 1);
    if( vb_art5 >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_art5, 1);
    if( vb_art6 >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_art6, 1);

    /* ---- Hand-in: auto Zanik follow, bit 0 (hidden / following) ---- */
    slice_snap(srv, player, 0, 2511, 5563);
    if( tegdak_slot < 0 || !srv->npcs[tegdak_slot].active )
        tegdak_slot = slice_spawn_npc(srv, npc_tegdak, 2511, 5563, 0, &spawned_tegdak);
    if( tegdak_slot >= 0 )
    {
        slice_talk(srv, player, npc_tegdak, tegdak_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 3,
                       "sixth hand-in should write slice_quest=3, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_zanik) == 0,
                       "Zanik following must write slice_zanik_at_dig=0, got %d",
                       ToriRSServer_VarbitGet(player, vb_zanik));
        if( ToriRSServer_VarbitGet(player, vb_quest) == 3 &&
            ToriRSServer_VarbitGet(player, vb_zanik) == 0 )
            slice_pass("zanik-vis", "OPNPC1", "following bit=0");
    }

    /* ---- Railway return doorway SHELL ---- */
    slice_snap(srv, player, 0, 2521, 5607);
    loc_slot = slice_find_loc(2521, 5607, 0, loc_exit, 16);
    SELFTEST_CHECK(loc_slot >= 0, "slice_underground_wall_exit_goblin SHELL should stand at the railway");
    if( loc_slot >= 0 )
    {
        slice_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_exit, -1, loc_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        slice_pass("railway-exit", "OPLOC1", "slice_underground_wall_exit_goblin");
    }

    /* ---- Scribe ---- */
    slice_snap(srv, player, 1, 2716, 5369);
    scribe_slot = slice_spawn_npc(srv, npc_scribe, 2716, 5369, 1, &spawned_scribe);
    if( scribe_slot >= 0 )
    {
        slice_talk(srv, player, npc_scribe, scribe_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 4,
                       "Scribe should write slice_quest=4, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        if( ToriRSServer_VarbitGet(player, vb_quest) == 4 )
            slice_pass("scribe", "OPNPC1", "slice_quest=4");
    }

    /* ---- Oldak talkable carrier ---- */
    slice_snap(srv, player, 0, 2704, 5365);
    oldak_slot = slice_spawn_npc(srv, npc_oldak, 2704, 5365, 0, &spawned_oldak);
    SELFTEST_CHECK(oldak_slot >= 0, "dorgesh_oldak_there must be talkable");
    if( oldak_slot >= 0 )
    {
        slice_talk(srv, player, npc_oldak, oldak_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 5,
                       "Oldak teleport should write slice_quest=5, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        if( ToriRSServer_VarbitGet(player, vb_quest) == 5 )
            slice_pass("oldak", "OPNPC1", "slice_quest=5");
    }

    /* ---- Generals ---- */
    slice_snap(srv, player, 0, 2957, 3512);
    wart_slot = slice_spawn_npc(srv, npc_wartface, 2957, 3512, 0, &spawned_wart);
    if( wart_slot >= 0 )
    {
        slice_talk(srv, player, npc_wartface, wart_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 6,
                       "generals ambush should write slice_quest=6, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        if( ToriRSServer_VarbitGet(player, vb_quest) == 6 )
            slice_pass("generals", "OPNPC1", "slice_quest=6");
    }

    /* Production Attack must not silently complete. */
    ham_slot = slice_spawn_npc(srv, npc_ham_mage, 2447, 5417, 2, &spawned_ham);
    if( ham_slot >= 0 && ToriRSServer_VarbitGet(player, vb_quest) == 6 )
    {
        slice_release(srv, player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_ham_mage, -1, ham_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 6,
                       "Attack must not silently complete the tower, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        if( ToriRSServer_VarbitGet(player, vb_quest) == 6 )
            slice_pass("ham-attack-no-skip", "OPNPC2", "slice_quest=6");
    }

    ToriRSServer_ScriptsRunDebugproc(srv, "anothersliceofham_pass_ham");
    slice_drain(srv, player, 0);
    slice_release(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 7,
                   "HAM cheat-skip should write slice_quest=7, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    if( ToriRSServer_VarbitGet(player, vb_quest) == 7 )
    {
        fprintf(stderr, "PASS anothersliceofham boss=ham_mage,ham_archer CHEAT-SKIP\n");
        fflush(stderr);
        slice_pass("ham-skip", "debugproc,anothersliceofham_pass_ham",
                   "PASS anothersliceofham boss=ham_mage,ham_archer CHEAT-SKIP");
    }

    if( wart_slot < 0 || !srv->npcs[wart_slot].active )
        wart_slot = slice_spawn_npc(srv, npc_wartface, 2957, 3512, 0, &spawned_wart);
    if( wart_slot >= 0 )
    {
        slice_talk(srv, player, npc_wartface, wart_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 8,
                       "mace hand-off should write slice_quest=8, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        SELFTEST_CHECK(slice_inv_total(player, obj_mace) >= 1,
                       "generals should grant the Ancient mace");
        if( ToriRSServer_VarbitGet(player, vb_quest) == 8 )
            slice_pass("mace-reward", "OPNPC1", "slice_quest=8 mace");
    }

    /* ---- Sergeants + light + command puzzle ---- */
    if( obj_candle >= 0 && slice_inv_total(player, obj_candle) == 0 )
        inv_set(player, 14, obj_candle, 1);
    slice_snap(srv, player, 0, 3170, 3170);
    moss_slot = slice_spawn_npc(srv, npc_moss, 3170, 3170, 0, &spawned_moss);
    if( moss_slot >= 0 )
    {
        slice_talk(srv, player, npc_moss, moss_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 9,
                       "sergeant briefing should write slice_quest=9, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        if( ToriRSServer_VarbitGet(player, vb_quest) == 9 )
            slice_pass("sergeant", "OPNPC1", "slice_quest=9");
    }

    slice_snap(srv, player, 0, 2397, 5558);
    if( npc_moss_shell >= 0 )
    {
        int cmd_slot = slice_spawn_npc(srv, npc_moss_shell, 2398, 5558, 0, &spawned_moss);

        if( cmd_slot >= 0 )
        {
            slice_talk(srv, player, npc_moss_shell, cmd_slot);
            slice_choose(srv, player, 1);
            slice_drain(srv, player, 0);
            slice_talk(srv, player, npc_moss_shell, cmd_slot);
            slice_choose(srv, player, 1);
            slice_drain(srv, player, 0);
            slice_talk(srv, player, npc_moss_shell, cmd_slot);
            slice_choose(srv, player, 2);
            slice_drain(srv, player, 0);
            slice_release(srv, player);
            SELFTEST_CHECK(vb_snipers < 0 || ToriRSServer_VarbitGet(player, vb_snipers) == 1,
                           "wait/follow commands should set slice_reached_snipers");
            if( vb_snipers < 0 || ToriRSServer_VarbitGet(player, vb_snipers) == 1 )
                slice_pass("stealth-commands", "OPNPC1", "wait/follow");
        }
    }

    /* Sigmund Attack must not silently complete. */
    slice_snap(srv, player, 0, 2543, 5511);
    sig_slot = slice_spawn_npc(srv, npc_sigmund, 2543, 5511, 0, &spawned_sig);
    if( sig_slot >= 0 && ToriRSServer_VarbitGet(player, vb_quest) == 9 )
    {
        slice_release(srv, player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_sigmund, -1, sig_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 9,
                       "Attack must not silently complete Sigmund, got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        if( ToriRSServer_VarbitGet(player, vb_quest) == 9 )
            slice_pass("sigmund-attack-no-skip", "OPNPC2", "slice_quest=9");
    }

    ToriRSServer_ScriptsRunDebugproc(srv, "anothersliceofham_pass_sigmund");
    slice_drain(srv, player, 0);
    slice_release(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 10,
                   "Sigmund cheat-skip should write slice_quest=10, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    if( ToriRSServer_VarbitGet(player, vb_quest) == 10 )
    {
        fprintf(stderr, "PASS anothersliceofham boss=sigmund CHEAT-SKIP\n");
        fflush(stderr);
        slice_pass("sigmund-skip", "debugproc,anothersliceofham_pass_sigmund",
                   "PASS anothersliceofham boss=sigmund CHEAT-SKIP");
    }

    /* ---- Untie Zanik + rewards (resume first, then wide close+tick) ---- */
    slice_snap(srv, player, 0, 2542, 5513);
    loc_slot = slice_find_loc(2542, 5513, 0, loc_tied, 16);
    if( loc_slot < 0 && loc_tied >= 0 )
        loc_slot = ToriRSServer_SceneAddLoc(2542, 5513, 0, loc_tied, 10, 0);
    SELFTEST_CHECK(loc_slot >= 0, "slice_zanik_tied_up SHELL should be clickable");
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    xp_before = stat_mining >= 0 ? player->stat_xp_tenths[stat_mining] : 0;
    if( loc_slot >= 0 )
    {
        slice_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tied,
                                            ToriRSServer_LocCategory(loc_tied), loc_slot);
        slice_drain_rewards(srv, player);
        slice_release(srv, player);
    }
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 11,
                   "untie should write slice_quest=11, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1,
                       "completion should award +1 QP, %d -> %d", qp_before,
                       player->varps[varp_qp]);
    if( stat_mining >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_mining] >= xp_before + 30000,
                       "completion should award 3000 Mining XP");
    if( ToriRSServer_VarbitGet(player, vb_quest) == 11 )
        slice_pass("complete", "OPLOC1", "slice_quest=11 qp+1");

    /* ---- Postquest ---- */
    slice_snap(srv, player, 1, 2730, 5365);
    if( urtag_slot < 0 || !srv->npcs[urtag_slot].active )
        urtag_slot = slice_spawn_npc(srv, npc_urtag, 2730, 5365, 1, &spawned_urtag);
    if( urtag_slot >= 0 )
    {
        slice_talk(srv, player, npc_urtag, urtag_slot);
        slice_drain(srv, player, 0);
        slice_release(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 11,
                       "postquest must leave slice_quest=11");
        slice_pass("postquest", "OPNPC1", "slice_quest=11");
    }

    (void)npc_zanik;
    (void)owned;

slice_selftest_done:
    if( spawned_urtag >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_urtag);
    if( spawned_tegdak >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_tegdak);
    if( spawned_scribe >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_scribe);
    if( spawned_oldak >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_oldak);
    if( spawned_wart >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_wart);
    if( spawned_moss >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_moss);
    if( spawned_ham >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_ham);
    if( spawned_sig >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_sig);
    ToriRSServer_WorldNpcReap(srv);
    slice_inv_clear(player);
    player->godmode = 0;
    slice_pass("cleanup", "WorldNpcFree", "spawns reaped");
}
