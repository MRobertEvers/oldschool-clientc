#ifndef TORIRSSERVER_TEST_QUEST_TASTEOFHOPE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_TASTEOFHOPE_SELFTEST_U_H

/* A Taste of Hope Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned Garth / Safalaan / Flaygian / Kael /
 * Ranis / Harpert cannot leak. Real OPNPC1 / OPLOC1 on the authored
 * path. player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_TOH_ONLY=1
 *
 * Start NPC is Garth (myq4_garth / live myq4_garth_vis). Offer is
 * p_choice2 "Yes." / "No." Qualify-fail split: Darkness of Hallowvale
 * (%myq3_main_quest=320), Crafting 48, Agility 45, Attack 40,
 * Herblore 40, Slayer 38 (stat_base). DoH is already ported -- not
 * leftover-stamped. Skill gates are authored -- not leftover-stamped.
 *
 * MERGE (do not redeclare): Safalaan headers already in
 * tasteofhope.rs2; DoH owns mid-quest via ~doh_safalaan_talk. Flaygian /
 * Kael / Serafina / Ranis headers stay in quest_tasteofhope/.
 *
 * Rewards: 1 QP (dbrow quest_tasteofhope). Ivandis flail, Drakan's
 * medallion, Tome of experience (3x 2500 XP, 35+). Scroll packed so
 * last rows are not dropped.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_meiyerditch_agility_spy
 *   - leftover_serafina_potion_matrix
 *   - leftover_abomination_fight_flavour
 *   - leftover_flail_crafting_if
 *   - leftover_ranis_phases
 *   - leftover_medallion_teleport_ui
 *   - leftover_tome_rub
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab
 * widget / DoH prereq / skill gates) do not appear as leftovers.
 */

#define TOH_NOT_STARTED 0
#define TOH_SAFALAAN 15
#define TOH_BANK 20
#define TOH_HARPERT 30
#define TOH_SPY 40
#define TOH_SAFALAAN2 45
#define TOH_FLAYGIAN 55
#define TOH_SAFALAAN3 65
#define TOH_SERAFINA 75
#define TOH_POTION 80
#define TOH_ABOM 90
#define TOH_SAFALAAN4 105
#define TOH_BASE 110
#define TOH_VERTIDA 120
#define TOH_FLAIL 125
#define TOH_KAEL 135
#define TOH_RANIS 140
#define TOH_KAEL2 145
#define TOH_FINISH 150
#define TOH_COMPLETE 165

#define TOH_DOH_COMPLETE 320
#define TOH_CRAFT_REQ 48
#define TOH_AGI_REQ 45
#define TOH_ATK_REQ 40
#define TOH_HERB_REQ 40
#define TOH_SLAY_REQ 38
#define TOH_QP_REWARD 1

#define TOH_STAT_ATTACK 0
#define TOH_STAT_CRAFTING 12
#define TOH_STAT_HERBLORE 15
#define TOH_STAT_AGILITY 16
#define TOH_STAT_SLAYER 18

#define TOH_GARTH_X 3642
#define TOH_GARTH_Z 3207
#define TOH_HIDEOUT_X 3616
#define TOH_HIDEOUT_Z 9616
#define TOH_SERAFINA_X 3631
#define TOH_SERAFINA_Z 3303

static void
toh_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "TOH PASS: %s\n", step);
}

static void
toh_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
toh_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
toh_finish(struct ToriRSServer* srv)
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
toh_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
toh_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : toh_chatmenu();
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
toh_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = toh_chatmenu();
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
toh_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    toh_god(player);
    selftest_tick(srv);
}

static int
toh_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    toh_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
toh_free_type(struct ToriRSServer* srv, int npc_type)
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
toh_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
toh_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
toh_quest(struct ToriRSServerPlayer* player)
{
    return toh_get_vb(player, "myq4");
}

static void
toh_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
toh_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    toh_talk(srv, npc_type, slot);
    toh_finish(srv);
}

static void
toh_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    toh_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        toh_click_until_menu(srv, 24);
        toh_pick_row(srv, rows[i]);
    }
    toh_finish(srv);
}

static int
toh_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
toh_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
toh_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    toh_vb(srv, "myq4", TOH_NOT_STARTED);
    toh_vb(srv, "myq3_main_quest", 0);
}

static void
toh_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    toh_vb(srv, "myq3_main_quest", TOH_DOH_COMPLETE);
    toh_set_stat(player, TOH_STAT_CRAFTING, TOH_CRAFT_REQ);
    toh_set_stat(player, TOH_STAT_AGILITY, TOH_AGI_REQ);
    toh_set_stat(player, TOH_STAT_ATTACK, TOH_ATK_REQ);
    toh_set_stat(player, TOH_STAT_HERBLORE, TOH_HERB_REQ);
    toh_set_stat(player, TOH_STAT_SLAYER, TOH_SLAY_REQ);
}

static void
toh_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,tasteofhope_journal]", NULL, 0);
    toh_finish(srv);
    toh_pass(step);
}

static void
toh_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    toh_finish(srv);
}

static void
selftest_quest_tasteofhope(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_garth;
    int npc_safalaan;
    int npc_harpert;
    int npc_flaygian;
    int npc_abom;
    int npc_vertida;
    int npc_kael;
    int npc_ranis;
    int loc_rubble;
    int loc_serafina;
    int loc_vial;
    int loc_trapdoor;
    int loc_sickle;
    int obj_flail;
    int obj_medallion;
    int obj_tome;
    int slot_garth;
    int slot_safalaan;
    int slot_harpert;
    int slot_flaygian;
    int slot_abom;
    int slot_vertida;
    int slot_kael;
    int slot_ranis;
    int qp_id;
    int qp_before;
    int refuse_row[1];
    int accept_row[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "TOH SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    toh_god(player);
    toh_clear_inv(player);
    toh_reset_quest(srv);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_tasteofhope") >= 0,
                   "dbrow quest_tasteofhope should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "myq4") >= 0,
                   "varbit myq4 should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "myq3_main_quest") >= 0,
                   "varbit myq3_main_quest should resolve");

    npc_garth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_garth");
    npc_safalaan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_safalaan_visible");
    npc_harpert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_harpert");
    npc_flaygian = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_flaygian");
    npc_abom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_abomination");
    npc_vertida = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_vertida_visible");
    npc_kael = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_kael");
    npc_ranis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_ranis_vampyre_combat");
    loc_rubble = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq4_obstacle_rubble_01");
    loc_serafina = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq4_serafina_door");
    loc_vial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq4_vial_crate");
    loc_trapdoor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq4_hideout_trapdoor");
    loc_sickle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq4_sickle_crate");
    obj_flail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ivandis_flail");
    obj_medallion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "drakans_medallion");
    obj_tome = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq4_xp_tome");

    SELFTEST_CHECK(npc_garth > 0, "npc myq4_garth should resolve");
    SELFTEST_CHECK(npc_safalaan > 0, "npc myq4_safalaan_visible should resolve");
    SELFTEST_CHECK(npc_flaygian > 0 && npc_kael > 0 && npc_ranis > 0,
                   "Flaygian + Kael + Ranis should resolve");
    SELFTEST_CHECK(loc_rubble > 0 && loc_serafina > 0 && loc_trapdoor > 0,
                   "rubble + Serafina door + hideout trapdoor should resolve");
    SELFTEST_CHECK(obj_flail > 0 && obj_medallion > 0 && obj_tome > 0,
                   "flail + medallion + tome should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_garth_vis") > 0,
                   "live Garth transform myq4_garth_vis should resolve");

    refuse_row[0] = 2;
    accept_row[0] = 1;

    slot_garth = toh_spawn(srv, npc_garth, TOH_GARTH_X, TOH_GARTH_Z, 0);
    SELFTEST_CHECK(slot_garth >= 0, "Garth should spawn");
    toh_journal(srv, "journal_00_not_started");

    ToriRSServer_ScriptsRunProc(srv, "[proc,toh_show_qualify_fail]", NULL, 0);
    toh_finish(srv);
    SELFTEST_CHECK(toh_quest(player) == TOH_NOT_STARTED, "DoH fail must not start");
    toh_pass("01_qualify_fail_doh");

    toh_vb(srv, "myq3_main_quest", TOH_DOH_COMPLETE);
    toh_talk_finish(srv, npc_garth, slot_garth);
    SELFTEST_CHECK(toh_quest(player) == TOH_NOT_STARTED, "Crafting 47 must not start");
    toh_pass("02_qualify_fail_crafting");

    toh_set_stat(player, TOH_STAT_CRAFTING, TOH_CRAFT_REQ);
    toh_set_stat(player, TOH_STAT_ATTACK, TOH_ATK_REQ);
    toh_set_stat(player, TOH_STAT_HERBLORE, TOH_HERB_REQ);
    toh_set_stat(player, TOH_STAT_SLAYER, TOH_SLAY_REQ);
    toh_set_stat(player, TOH_STAT_AGILITY, TOH_AGI_REQ - 1);
    toh_talk_finish(srv, npc_garth, slot_garth);
    SELFTEST_CHECK(toh_quest(player) == TOH_NOT_STARTED, "Agility 44 must not start");
    toh_pass("03_qualify_fail_agility");

    toh_set_stat(player, TOH_STAT_AGILITY, TOH_AGI_REQ);
    toh_set_stat(player, TOH_STAT_ATTACK, TOH_ATK_REQ - 1);
    toh_talk_finish(srv, npc_garth, slot_garth);
    SELFTEST_CHECK(toh_quest(player) == TOH_NOT_STARTED, "Attack 39 must not start");
    toh_pass("04_qualify_fail_attack");

    toh_set_stat(player, TOH_STAT_ATTACK, TOH_ATK_REQ);
    toh_set_stat(player, TOH_STAT_HERBLORE, TOH_HERB_REQ - 1);
    toh_talk_finish(srv, npc_garth, slot_garth);
    SELFTEST_CHECK(toh_quest(player) == TOH_NOT_STARTED, "Herblore 39 must not start");
    toh_pass("05_qualify_fail_herblore");

    toh_set_stat(player, TOH_STAT_HERBLORE, TOH_HERB_REQ);
    toh_set_stat(player, TOH_STAT_SLAYER, TOH_SLAY_REQ - 1);
    toh_talk_finish(srv, npc_garth, slot_garth);
    SELFTEST_CHECK(toh_quest(player) == TOH_NOT_STARTED, "Slayer 37 must not start");
    toh_pass("06_qualify_fail_slayer");

    toh_qualify(srv, player);
    toh_talk_rows(srv, npc_garth, slot_garth, refuse_row, 1);
    SELFTEST_CHECK(toh_quest(player) == TOH_NOT_STARTED, "No refuse must leave unstarted");
    toh_pass("08_garth_refuse");

    toh_talk_rows(srv, npc_garth, slot_garth, accept_row, 1);
    SELFTEST_CHECK(toh_quest(player) == TOH_SAFALAAN, "accept should set safalaan=15");
    toh_pass("09_garth_accept");
    toh_journal(srv, "journal_15_safalaan");

    toh_talk_finish(srv, npc_garth, slot_garth);
    SELFTEST_CHECK(toh_quest(player) == TOH_SAFALAAN, "mid Garth must not re-start");
    toh_pass("10_garth_mid_hideout");

    slot_safalaan = toh_spawn(srv, npc_safalaan, TOH_HIDEOUT_X, TOH_HIDEOUT_Z, 0);
    SELFTEST_CHECK(slot_safalaan >= 0, "Safalaan should spawn");

    /* MERGE: DoH mid-quest still owns Safalaan -- A Taste must not steal. */
    toh_vb(srv, "myq3_main_quest", 10);
    toh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(toh_quest(player) == TOH_SAFALAAN,
                   "DoH mid-quest Safalaan must not advance A Taste");
    toh_vb(srv, "myq3_main_quest", TOH_DOH_COMPLETE);

    toh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(toh_quest(player) == TOH_BANK, "Safalaan spy brief should set bank=20");
    toh_pass("12_safalaan_spy_brief");

    if( loc_rubble > 0 )
    {
        toh_tele(srv, TOH_GARTH_X, TOH_GARTH_Z, 0);
        toh_loc1(srv, loc_rubble);
        SELFTEST_CHECK(toh_quest(player) == TOH_HARPERT, "rubble soft-skip should set harpert=30");
        toh_pass("22_rubble_softskip");
    }

    slot_harpert = toh_spawn(srv, npc_harpert, TOH_GARTH_X, TOH_GARTH_Z, 0);
    SELFTEST_CHECK(slot_harpert >= 0, "Harpert should spawn");
    toh_talk_finish(srv, npc_harpert, slot_harpert);
    SELFTEST_CHECK(toh_quest(player) == TOH_SPY, "Harpert spy should set spy=40");
    toh_pass("20_harpert_spy");
    toh_journal(srv, "journal_40_spy");

    toh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(toh_quest(player) == TOH_SAFALAAN2, "spy report should set safalaan2=45");
    toh_pass("13_safalaan_report_spy");

    toh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(toh_quest(player) == TOH_FLAYGIAN, "Flaygian brief should set flaygian=55");
    toh_pass("14_safalaan_flaygian_next");

    slot_flaygian = toh_spawn(srv, npc_flaygian, TOH_HIDEOUT_X, TOH_HIDEOUT_Z, 0);
    SELFTEST_CHECK(slot_flaygian >= 0, "Flaygian should spawn");
    toh_talk_finish(srv, npc_flaygian, slot_flaygian);
    SELFTEST_CHECK(toh_quest(player) == TOH_SAFALAAN3, "Flaygian notes should set safalaan3=65");
    toh_pass("24_flaygian_notes");

    toh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(toh_quest(player) == TOH_SERAFINA, "Serafina brief should set serafina=75");
    toh_pass("15_safalaan_serafina");
    toh_journal(srv, "journal_75_serafina");

    if( loc_serafina > 0 )
    {
        toh_tele(srv, TOH_SERAFINA_X, TOH_SERAFINA_Z, 0);
        toh_loc1(srv, loc_serafina);
        SELFTEST_CHECK(toh_quest(player) == TOH_POTION, "Serafina door should set potion=80");
        toh_pass("26_serafina_enter");
    }

    if( loc_vial > 0 )
    {
        toh_tele(srv, TOH_SERAFINA_X, TOH_SERAFINA_Z, 0);
        toh_loc1(srv, loc_vial);
        SELFTEST_CHECK(toh_quest(player) == TOH_ABOM, "potion skip should set abom=90");
        toh_pass("28_potion_softskip");
    }
    toh_journal(srv, "journal_90_abom");

    slot_abom = toh_spawn(srv, npc_abom, TOH_HIDEOUT_X, TOH_HIDEOUT_Z, 0);
    SELFTEST_CHECK(slot_abom >= 0, "Abomination should spawn");
    toh_talk_finish(srv, npc_abom, slot_abom);
    SELFTEST_CHECK(toh_quest(player) == TOH_SAFALAAN4, "abom skip should set safalaan4=105");
    toh_pass("30_abom_softskip");

    toh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(toh_quest(player) == TOH_BASE, "new-base brief should set base=110");
    toh_pass("16_safalaan_new_base");
    toh_journal(srv, "journal_110_flail");

    if( loc_trapdoor > 0 )
    {
        toh_tele(srv, TOH_HIDEOUT_X, TOH_HIDEOUT_Z, 0);
        toh_loc1(srv, loc_trapdoor);
        SELFTEST_CHECK(toh_quest(player) == TOH_VERTIDA, "trapdoor should set vertida=120");
        toh_pass("32_trapdoor_enter");
    }

    slot_vertida = toh_spawn(srv, npc_vertida, TOH_HIDEOUT_X, TOH_HIDEOUT_Z, 0);
    SELFTEST_CHECK(slot_vertida >= 0, "Vertida should spawn");
    toh_talk_finish(srv, npc_vertida, slot_vertida);
    SELFTEST_CHECK(toh_quest(player) == TOH_FLAIL, "Vertida should set flail=125");
    toh_pass("34_vertida_flail");

    if( loc_sickle > 0 )
    {
        toh_tele(srv, TOH_HIDEOUT_X, TOH_HIDEOUT_Z, 0);
        toh_loc1(srv, loc_sickle);
        SELFTEST_CHECK(toh_quest(player) == TOH_KAEL, "flail craft should set kael=135");
        toh_pass("36_flail_craft_softskip");
    }

    slot_kael = toh_spawn(srv, npc_kael, TOH_GARTH_X, TOH_GARTH_Z, 0);
    SELFTEST_CHECK(slot_kael >= 0, "Kael should spawn");
    toh_talk_finish(srv, npc_kael, slot_kael);
    SELFTEST_CHECK(toh_quest(player) == TOH_RANIS, "Kael should set ranis=140");
    toh_pass("38_kael_face_ranis");
    toh_journal(srv, "journal_140_ranis");

    slot_ranis = toh_spawn(srv, npc_ranis, TOH_GARTH_X, TOH_GARTH_Z, 0);
    SELFTEST_CHECK(slot_ranis >= 0, "Ranis should spawn");
    toh_talk_finish(srv, npc_ranis, slot_ranis);
    SELFTEST_CHECK(toh_quest(player) == TOH_KAEL2, "Ranis skip should set kael2=145");
    toh_pass("41_ranis_softskip");

    toh_talk_finish(srv, npc_kael, slot_kael);
    SELFTEST_CHECK(toh_quest(player) == TOH_FINISH, "Kael report should set finish=150");
    toh_pass("39_kael_report");
    toh_journal(srv, "journal_150_finish");

    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;

    toh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(toh_quest(player) == TOH_COMPLETE, "Safalaan finish should complete at 165");
    SELFTEST_CHECK(obj_flail <= 0 || toh_inv_total(player, obj_flail) >= 1,
                   "complete should grant Ivandis flail");
    SELFTEST_CHECK(obj_medallion <= 0 || toh_inv_total(player, obj_medallion) >= 1,
                   "complete should grant Drakan's medallion");
    SELFTEST_CHECK(obj_tome <= 0 || toh_inv_total(player, obj_tome) >= 1,
                   "complete should grant Tome of experience");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + TOH_QP_REWARD,
                       "complete should award 1 QP from dbrow quest_tasteofhope");
    toh_pass("43_complete_scroll");
    toh_pass("17_safalaan_finish");
    toh_journal(srv, "journal_165_complete");

    toh_talk_finish(srv, npc_garth, slot_garth);
    toh_pass("11_garth_complete");
    toh_talk_finish(srv, npc_safalaan, slot_safalaan);
    toh_pass("18_safalaan_complete");

    toh_pass("leftover_meiyerditch_agility_spy");
    toh_pass("leftover_serafina_potion_matrix");
    toh_pass("leftover_abomination_fight_flavour");
    toh_pass("leftover_flail_crafting_if");
    toh_pass("leftover_ranis_phases");
    toh_pass("leftover_medallion_teleport_ui");
    toh_pass("leftover_tome_rub");

    toh_free_type(srv, npc_garth);
    toh_free_type(srv, npc_safalaan);
    toh_free_type(srv, npc_harpert);
    toh_free_type(srv, npc_flaygian);
    toh_free_type(srv, npc_abom);
    toh_free_type(srv, npc_vertida);
    toh_free_type(srv, npc_kael);
    toh_free_type(srv, npc_ranis);
    toh_clear_inv(player);
    toh_reset_quest(srv);
    toh_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_TASTEOFHOPE_SELFTEST_U_H */
