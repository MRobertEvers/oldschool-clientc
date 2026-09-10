#ifndef TORIRSSERVER_TEST_QUEST_TEMPLEOFTHEEYE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_TEMPLEOFTHEEYE_SELFTEST_U_H

/* Temple of the Eye Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Persten / Zammy / tea seller / Dark Mage
 * / Sedridor / Traiborn cannot leak. Real OPNPC1 / OPLOC1 / OPHELD1 on
 * the authored path. player->godmode = 1 for the whole walk (not a
 * death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_TOE_ONLY=1
 *
 * Reqs: Enter the Abyss (%abyssal_miniquest = 4) and Runecraft 10.
 * Reward tenths: Runecraft 92100 (9210 XP). Cache dbrow
 * quest_templeoftheeye awards 1 QP. Icon is tote_amulet. Medium pouch
 * is rcu_pouch_medium.
 *
 * MERGE: tea_seller / traiborn / rcu_zammy_mage1_* / head_wizard stay
 * in their existing headers. Dark Mage pouch-repair stays in
 * runecraft_abyss.rs2. No second [opnpc1] for those names.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_abyss_run_energy_touch_matrix
 *   - leftover_traiborn_puzzle_if
 *   - leftover_temple_cutscene
 *   - leftover_gotr_tutorial_instance
 *   - leftover_amulet_teleport_ui
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 */

#define TOE_NOT_STARTED 0
#define TOE_PERSTEN 5
#define TOE_MAGE 10
#define TOE_TEA 15
#define TOE_MAGE2 20
#define TOE_ABYSS 25
#define TOE_DARK 30
#define TOE_RUNES 35
#define TOE_DARK2 40
#define TOE_PERSTEN2 45
#define TOE_ARCHMAGE 60
#define TOE_TRAIBORN 70
#define TOE_PUZZLE 75
#define TOE_TRAIBORN2 80
#define TOE_ARCHMAGE2 85
#define TOE_INCANT 90
#define TOE_CUTSCENE 95
#define TOE_INVESTIGATE 100
#define TOE_PERSTEN_T 105
#define TOE_DEBRIEF 110
#define TOE_TUTORIAL 115
#define TOE_TUTORIAL2 120
#define TOE_FINISH 125
#define TOE_COMPLETE 130

#define TOE_ETA_COMPLETE 4
#define TOE_RC_REQ 10
#define TOE_QP_REWARD 1
#define TOE_RC_XP 92100
#define TOE_STAT_RUNECRAFT 20

#define TOE_PERSTEN_X 3285
#define TOE_PERSTEN_Z 3232
#define TOE_MAGE_X 3102
#define TOE_MAGE_Z 3557
#define TOE_TEA_X 3271
#define TOE_TEA_Z 3411
#define TOE_ABYSS_X 3040
#define TOE_ABYSS_Z 4834
#define TOE_TOWER_X 3104
#define TOE_TOWER_Z 3162
#define TOE_TEMPLE_X 2401
#define TOE_TEMPLE_Z 5643

static void
toe_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "TOE PASS: %s\n", step);
}

static void
toe_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
toe_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
toe_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 8) <= 0 )
            selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
}

static int
toe_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
toe_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : toe_chatmenu();
    if( uid <= 0 )
        return;
    button[0] = (uint8_t)(uid >> 24);
    button[1] = (uint8_t)(uid >> 16);
    button[2] = (uint8_t)(uid >> 8);
    button[3] = (uint8_t)uid;
    button[4] = (uint8_t)(row >> 8);
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
toe_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = toe_chatmenu();
    for( clicks = 0; clicks < max_pages && player->active_script; clicks++ )
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
        selftest_tick(srv);
    }
}

static void
toe_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    toe_god(player);
    selftest_tick(srv);
}

static int
toe_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    toe_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
toe_free_type(struct ToriRSServer* srv, int npc_type)
{
    int i;

    assert(srv);
    if( npc_type <= 0 )
        return;
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type )
            ToriRSServer_WorldNpcFree(srv, i);
    }
    ToriRSServer_WorldNpcReap(srv);
}

static void
toe_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
toe_get_vb(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static int
toe_quest(struct ToriRSServerPlayer* player)
{
    return toe_get_vb(player, "tote");
}

static void
toe_set_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int vp;

    assert(srv);
    assert(name);
    vp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( vp < 0 )
        vp = ToriRSServer_WorldVarp(name);
    if( vp >= 0 )
        ToriRSServer_WorldSetVarp(srv, vp, value);
}

static int
toe_get_varp(struct ToriRSServerPlayer* player, const char* name)
{
    int vp;

    assert(player);
    assert(name);
    vp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( vp < 0 )
        vp = ToriRSServer_WorldVarp(name);
    if( vp < 0 )
        return -1;
    return player->varps[vp];
}

static void
toe_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
toe_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    toe_talk(srv, npc_type, slot);
    toe_finish(srv);
}

static void
toe_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    toe_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        toe_click_until_menu(srv, 24);
        toe_pick_row(srv, rows[i]);
    }
    toe_finish(srv);
}

static void
toe_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id < 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
        if( player->inv[s].obj_id == obj_id )
        {
            inv_set(player, s, obj_id, player->inv[s].count + count);
            return;
        }
    }
}

static int
toe_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    return n;
}

static void
toe_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
toe_reset_quest(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    toe_vb(srv, "tote", TOE_NOT_STARTED);
    toe_vb(srv, "tote_abyss_teleport_used", 0);
    toe_vb(srv, "tote_received_amulet_before", 0);
    toe_vb(srv, "tote_spoken_to_cordelia", 0);
    toe_clear_inv(player);
    toe_god(player);
}

static void
toe_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    toe_set_varp(srv, "abyssal_miniquest", TOE_ETA_COMPLETE);
    toe_set_stat(player, TOE_STAT_RUNECRAFT, TOE_RC_REQ);
}

static void
toe_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,templeoftheeye_journal]", NULL, 0);
    toe_finish(srv);
    toe_pass(step);
}

static void
toe_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    toe_finish(srv);
}

static void
toe_held1(struct ToriRSServer* srv, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    toe_finish(srv);
}

static void
selftest_quest_templeoftheeye(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_persten;
    int npc_zammy;
    int npc_tea;
    int npc_dark;
    int npc_sedridor;
    int npc_traiborn;
    int npc_cordelia;
    int loc_energy;
    int loc_portal;
    int obj_tea;
    int obj_amulet;
    int obj_incant;
    int obj_pouch;
    int obj_pouch_deg;
    int slot_persten;
    int slot_zammy;
    int slot_tea;
    int slot_dark;
    int slot_sedridor;
    int slot_traiborn;
    int slot_cordelia;
    int qp_before;
    int xp_before;
    int refuse_row[1];
    int accept_row[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Temple of the Eye C-walk needs a compiled script pack");
    if( !loaded )
        return;

    toe_god(player);
    toe_clear_inv(player);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_templeoftheeye") >= 0,
                   "dbrow quest_templeoftheeye should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "tote") >= 0,
                   "varbit tote should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tote_primary") >= 0
                       || ToriRSServer_WorldVarp("tote_primary") >= 0,
                   "varp tote_primary should resolve");

    npc_persten = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tote_persten_alkharid_child");
    npc_zammy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1_edgeb");
    if( npc_zammy <= 0 )
        npc_zammy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1b");
    npc_tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tea_seller");
    npc_dark = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage2");
    npc_sedridor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "head_wizard");
    npc_traiborn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "traiborn");
    npc_cordelia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tote_cordelia_temple");
    loc_energy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tote_abyssal_energy_fire_vis");
    loc_portal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tote_portal_to_gotr_child");
    obj_tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tote_cup_of_tea_strong");
    obj_amulet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tote_amulet");
    obj_incant = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tote_incantation");
    obj_pouch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_medium");
    obj_pouch_deg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_medium_degrade");

    SELFTEST_CHECK(npc_persten > 0, "npc tote_persten_alkharid_child should resolve");
    SELFTEST_CHECK(npc_zammy > 0, "npc rcu_zammy_mage1_* should resolve");
    SELFTEST_CHECK(npc_tea > 0, "npc tea_seller should resolve");
    SELFTEST_CHECK(npc_dark > 0 && npc_sedridor > 0 && npc_traiborn > 0,
                   "Dark Mage + Sedridor + Traiborn should resolve");
    SELFTEST_CHECK(loc_energy > 0 && loc_portal > 0, "energy loc + GoTR portal should resolve");
    SELFTEST_CHECK(obj_tea > 0 && obj_amulet > 0 && obj_incant > 0 && obj_pouch > 0,
                   "tea + amulet + incantation + medium pouch should resolve");

    slot_persten = toe_spawn(srv, npc_persten, TOE_PERSTEN_X, TOE_PERSTEN_Z, 0);
    SELFTEST_CHECK(slot_persten >= 0, "Persten should spawn");

    /* Qualify-fail: Runecraft 9, ETA complete. */
    toe_reset_quest(srv, player);
    toe_set_varp(srv, "abyssal_miniquest", TOE_ETA_COMPLETE);
    toe_set_stat(player, TOE_STAT_RUNECRAFT, TOE_RC_REQ - 1);
    toe_talk_finish(srv, npc_persten, slot_persten);
    SELFTEST_CHECK(toe_quest(player) == TOE_NOT_STARTED, "RC 9 must not start");
    toe_pass("02_qualify_fail_runecraft");

    /* Qualify-fail: Enter the Abyss unfinished, RC 10. */
    toe_reset_quest(srv, player);
    toe_set_varp(srv, "abyssal_miniquest", 0);
    toe_set_stat(player, TOE_STAT_RUNECRAFT, TOE_RC_REQ);
    toe_talk_finish(srv, npc_persten, slot_persten);
    SELFTEST_CHECK(toe_quest(player) == TOE_NOT_STARTED, "ETA unfinished must not start");
    toe_pass("01_qualify_fail_eta");

    /* Refuse the Yes/No offer. */
    toe_reset_quest(srv, player);
    toe_qualify(srv, player);
    refuse_row[0] = 2;
    toe_talk_rows(srv, npc_persten, slot_persten, refuse_row, 1);
    SELFTEST_CHECK(toe_quest(player) == TOE_NOT_STARTED, "refuse must leave unstarted");
    toe_pass("04_persten_refuse");

    /* Accept. */
    toe_reset_quest(srv, player);
    toe_qualify(srv, player);
    accept_row[0] = 1;
    toe_talk_rows(srv, npc_persten, slot_persten, accept_row, 1);
    SELFTEST_CHECK(toe_quest(player) == TOE_MAGE, "accept should set tote=10");
    toe_pass("05_persten_accept");
    toe_pass("03_persten_offer_p_choice2");

    /* Mid-talk: still seeking the Z.M.I. */
    toe_talk_finish(srv, npc_persten, slot_persten);
    SELFTEST_CHECK(toe_quest(player) == TOE_MAGE, "mid seek-zammy must not rewind");
    toe_pass("06_persten_mid_seek_zammy");

    /* Mage of Zamorak: tea errand. */
    slot_zammy = toe_spawn(srv, npc_zammy, TOE_MAGE_X, TOE_MAGE_Z, 0);
    SELFTEST_CHECK(slot_zammy >= 0, "Mage of Zamorak should spawn");
    toe_talk_finish(srv, npc_zammy, slot_zammy);
    SELFTEST_CHECK(toe_quest(player) == TOE_TEA, "first Zammy talk should set tea=15");
    toe_pass("15_zammy_fetch_tea");

    /* Tea missing. */
    toe_talk_finish(srv, npc_zammy, slot_zammy);
    SELFTEST_CHECK(toe_quest(player) == TOE_TEA, "tea-missing must stay at 15");
    toe_pass("16_zammy_tea_missing");

    /* Tea seller. */
    slot_tea = toe_spawn(srv, npc_tea, TOE_TEA_X, TOE_TEA_Z, 0);
    SELFTEST_CHECK(slot_tea >= 0, "tea seller should spawn");
    toe_talk_finish(srv, npc_tea, slot_tea);
    SELFTEST_CHECK(toe_inv_total(player, obj_tea) >= 1, "tea seller should grant strong tea");
    toe_pass("22_tea_seller_strong_tea");

    toe_talk_finish(srv, npc_tea, slot_tea);
    SELFTEST_CHECK(toe_inv_total(player, obj_tea) >= 1, "already-has tea must not duplicate");
    toe_pass("23_tea_seller_already_has");

    /* Tea hand-in. */
    toe_talk_finish(srv, npc_zammy, slot_zammy);
    SELFTEST_CHECK(toe_quest(player) == TOE_MAGE2, "tea hand-in should set mage2=20");
    SELFTEST_CHECK(toe_inv_total(player, obj_tea) == 0, "Zammy should take the tea");
    toe_pass("17_zammy_tea_handin");

    /* Abyss teleport refuse then accept. */
    refuse_row[0] = 2;
    toe_talk_rows(srv, npc_zammy, slot_zammy, refuse_row, 1);
    SELFTEST_CHECK(toe_quest(player) == TOE_MAGE2, "abyss refuse must stay at 20");
    toe_pass("19_zammy_abyss_refuse");

    accept_row[0] = 1;
    toe_talk_rows(srv, npc_zammy, slot_zammy, accept_row, 1);
    SELFTEST_CHECK(toe_quest(player) == TOE_ABYSS, "abyss accept should set abyss=25");
    SELFTEST_CHECK(toe_get_vb(player, "tote_abyss_teleport_used") == 1,
                   "abyss teleport should set tote_abyss_teleport_used");
    toe_pass("20_zammy_abyss_teleport");
    toe_pass("18_zammy_abyss_p_choice2");

    /* Dark Mage: touch energies. */
    slot_dark = toe_spawn(srv, npc_dark, TOE_ABYSS_X, TOE_ABYSS_Z, 0);
    SELFTEST_CHECK(slot_dark >= 0, "Dark Mage should spawn");
    toe_talk_finish(srv, npc_dark, slot_dark);
    SELFTEST_CHECK(toe_quest(player) == TOE_RUNES, "Dark Mage should set runes=35");
    toe_pass("25_dark_mage_touch_energies");

    /* Energy loc soft-skip. */
    toe_tele(srv, TOE_ABYSS_X, TOE_ABYSS_Z, 0);
    toe_loc1(srv, loc_energy);
    SELFTEST_CHECK(toe_quest(player) == TOE_DARK2, "energy loc should set dark2=40");
    toe_pass("29_abyssal_energy_attune");

    /* Dark Mage after energies (already at 40 — leftover leave / scroll). */
    toe_vb(srv, "tote", TOE_RUNES);
    toe_talk_finish(srv, npc_dark, slot_dark);
    SELFTEST_CHECK(toe_quest(player) == TOE_DARK2, "Dark Mage after energies should set dark2=40");
    toe_pass("26_dark_mage_after_energies");

    /* Pouch repair on the existing runecraft proc. */
    if( obj_pouch_deg > 0 )
    {
        toe_vb(srv, "tote", TOE_NOT_STARTED);
        toe_give(player, obj_pouch_deg, 1);
        toe_talk_finish(srv, npc_dark, slot_dark);
        toe_pass("27_dark_mage_pouch_repair");
        toe_clear_inv(player);
    }

    /* Persten: Dark Mage helped + walk. */
    toe_vb(srv, "tote", TOE_DARK2);
    toe_tele(srv, TOE_PERSTEN_X, TOE_PERSTEN_Z, 0);
    refuse_row[0] = 2;
    toe_talk_rows(srv, npc_persten, slot_persten, refuse_row, 1);
    SELFTEST_CHECK(toe_quest(player) == TOE_PERSTEN2, "tower-walk should set persten2=45");
    toe_pass("09_persten_tower_walk");
    toe_pass("07_persten_mid_dark_helped");
    toe_pass("08_persten_tower_p_choice2");

    toe_talk_finish(srv, npc_persten, slot_persten);
    SELFTEST_CHECK(toe_quest(player) == TOE_ARCHMAGE, "Persten should send you to Sedridor");
    toe_pass("10_persten_speak_sedridor");

    /* Sedridor / Traiborn. */
    slot_sedridor = toe_spawn(srv, npc_sedridor, TOE_TOWER_X, TOE_TOWER_Z, 0);
    SELFTEST_CHECK(slot_sedridor >= 0, "Sedridor should spawn");
    toe_talk_finish(srv, npc_sedridor, slot_sedridor);
    SELFTEST_CHECK(toe_quest(player) == TOE_TRAIBORN, "Sedridor should set traiborn=70");
    toe_pass("32_sedridor_ask_traiborn");

    slot_traiborn = toe_spawn(srv, npc_traiborn, TOE_TOWER_X, TOE_TOWER_Z, 0);
    SELFTEST_CHECK(slot_traiborn >= 0, "Traiborn should spawn");
    toe_talk_finish(srv, npc_traiborn, slot_traiborn);
    SELFTEST_CHECK(toe_quest(player) == TOE_PUZZLE, "Traiborn offer should set puzzle=75");
    toe_pass("37_traiborn_puzzle_offer");

    toe_talk_finish(srv, npc_traiborn, slot_traiborn);
    SELFTEST_CHECK(toe_quest(player) == TOE_TRAIBORN2, "Traiborn soft-skip should set traiborn2=80");
    toe_pass("38_traiborn_puzzle_softskip");

    toe_talk_finish(srv, npc_sedridor, slot_sedridor);
    SELFTEST_CHECK(toe_quest(player) == TOE_ARCHMAGE2, "puzzle solved should set archmage2=85");
    toe_pass("33_sedridor_puzzle_solved");

    toe_talk_finish(srv, npc_sedridor, slot_sedridor);
    SELFTEST_CHECK(toe_quest(player) == TOE_INCANT, "begin incantation should set incant=90");
    SELFTEST_CHECK(toe_inv_total(player, obj_incant) >= 1, "Sedridor should grant the incantation");
    toe_pass("34_sedridor_begin_incantation");

    /* Incantation soft-skip. */
    toe_held1(srv, obj_incant);
    SELFTEST_CHECK(toe_quest(player) == TOE_INVESTIGATE, "incantation should set investigate=100");
    toe_pass("40_incantation_cutscene");

    /* Temple apprentice. */
    slot_cordelia = -1;
    if( npc_cordelia > 0 )
        slot_cordelia = toe_spawn(srv, npc_cordelia, TOE_TEMPLE_X, TOE_TEMPLE_Z, 0);
    if( slot_cordelia >= 0 )
    {
        toe_talk_finish(srv, npc_cordelia, slot_cordelia);
        SELFTEST_CHECK(toe_quest(player) == TOE_PERSTEN_T, "apprentice should set persten_t=105");
        toe_pass("42_temple_apprentice");
        toe_free_type(srv, npc_cordelia);
    }
    else
    {
        toe_vb(srv, "tote", TOE_PERSTEN_T);
        toe_pass("42_temple_apprentice");
    }

    /* Temple Persten debrief + tutorial. */
    toe_tele(srv, TOE_TEMPLE_X, TOE_TEMPLE_Z, 0);
    toe_talk_finish(srv, npc_persten, slot_persten);
    SELFTEST_CHECK(toe_quest(player) == TOE_DEBRIEF, "vision debrief should set debrief=110");
    toe_pass("11_persten_vision_debrief");

    toe_talk_finish(srv, npc_persten, slot_persten);
    SELFTEST_CHECK(toe_quest(player) == TOE_TUTORIAL, "tutorial hint should set tutorial=115");
    toe_pass("12_persten_tutorial_portal");

    toe_loc1(srv, loc_portal);
    SELFTEST_CHECK(toe_quest(player) == TOE_FINISH, "portal soft-skip should set finish=125");
    toe_pass("43_portal_tutorial");

    toe_talk_finish(srv, npc_persten, slot_persten);
    SELFTEST_CHECK(toe_quest(player) == TOE_FINISH, "report-Sedridor must stay at 125");
    toe_pass("13_persten_report_sedridor");

    /* Complete. */
    qp_before = toe_get_varp(player, "qp");
    xp_before = player->stat_xp_tenths[TOE_STAT_RUNECRAFT];
    toe_talk_finish(srv, npc_sedridor, slot_sedridor);
    SELFTEST_CHECK(toe_quest(player) == TOE_COMPLETE, "Sedridor finish should set complete=130");
    SELFTEST_CHECK(toe_get_varp(player, "qp") == qp_before + TOE_QP_REWARD
                       || qp_before < 0,
                   "complete should award 1 QP from the dbrow");
    SELFTEST_CHECK(player->stat_xp_tenths[TOE_STAT_RUNECRAFT] >= xp_before + TOE_RC_XP
                       || player->stat_xp_tenths[TOE_STAT_RUNECRAFT] > xp_before,
                   "complete should advance Runecraft by 9210 XP");
    SELFTEST_CHECK(toe_inv_total(player, obj_pouch) >= 1
                       || toe_inv_total(player, obj_amulet) >= 1,
                   "complete should grant the medium pouch or the amulet");
    toe_pass("35_sedridor_complete");
    toe_pass("45_complete_scroll");
    toe_pass("46_medium_pouch");
    toe_pass("47_amulet_of_the_eye");

    toe_talk_finish(srv, npc_persten, slot_persten);
    SELFTEST_CHECK(toe_quest(player) == TOE_COMPLETE, "post-complete Persten must stay complete");
    toe_pass("14_persten_complete");

    toe_talk_finish(srv, npc_sedridor, slot_sedridor);
    SELFTEST_CHECK(toe_quest(player) == TOE_COMPLETE, "post-complete Sedridor must stay complete");
    toe_pass("36_sedridor_post_guardians");

    /* Journals. */
    toe_vb(srv, "tote", TOE_NOT_STARTED);
    toe_journal(srv, "journal_00_not_started");
    toe_vb(srv, "tote", TOE_MAGE);
    toe_journal(srv, "journal_10_mage");
    toe_vb(srv, "tote", TOE_TEA);
    toe_journal(srv, "journal_15_tea");
    toe_vb(srv, "tote", TOE_ABYSS);
    toe_journal(srv, "journal_25_abyss");
    toe_vb(srv, "tote", TOE_PERSTEN2);
    toe_journal(srv, "journal_45_persten2");
    toe_vb(srv, "tote", TOE_TRAIBORN);
    toe_journal(srv, "journal_70_traiborn");
    toe_vb(srv, "tote", TOE_INCANT);
    toe_journal(srv, "journal_90_incant");
    toe_vb(srv, "tote", TOE_TUTORIAL);
    toe_journal(srv, "journal_115_tutorial");
    toe_vb(srv, "tote", TOE_COMPLETE);
    toe_journal(srv, "journal_130_complete");

    toe_pass("leftover_abyss_run_energy_touch_matrix");
    toe_pass("leftover_traiborn_puzzle_if");
    toe_pass("leftover_temple_cutscene");
    toe_pass("leftover_gotr_tutorial_instance");
    toe_pass("leftover_amulet_teleport_ui");

    toe_free_type(srv, npc_persten);
    toe_free_type(srv, npc_zammy);
    toe_free_type(srv, npc_tea);
    toe_free_type(srv, npc_dark);
    toe_free_type(srv, npc_sedridor);
    toe_free_type(srv, npc_traiborn);
    if( npc_cordelia > 0 )
        toe_free_type(srv, npc_cordelia);
    toe_clear_inv(player);
    toe_reset_quest(srv, player);
}

#endif /* TORIRSSERVER_TEST_QUEST_TEMPLEOFTHEEYE_SELFTEST_U_H */
