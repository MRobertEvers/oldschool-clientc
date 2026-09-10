#ifndef TORIRSSERVER_TEST_QUEST_DEATHONTHEISLE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_DEATHONTHEISLE_SELFTEST_U_H

/* Death on the Isle Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned Patzi / Head Butler / guests /
 * Stradius / Adala / Naiatli cannot leak. Real OPNPC1 / OPLOC1 on the
 * authored path. player->godmode = 1 for the whole walk (not a death
 * test).
 *
 * Gate: TORIRSSERVER_SELFTEST_DOTI_ONLY=1
 *
 * Start NPC is Patzi (doti_patzi_core). Offer is p_choice2 "Yes." /
 * "Not now." Qualify-fail split: Children of the Sun
 * (%vmq1 >= 24), Thieving 34, Agility 32 (stat_base). COTS is already
 * ported -- not leftover-stamped. Skill gates are authored -- not
 * leftover-stamped.
 *
 * MERGE (do not redeclare): Patzi / Head Butler / guests / guards /
 * Adala / Naiatli headers already in deathontheisle.rs2. Do not
 * redeclare Children of the Sun NPCs.
 *
 * Rewards: 2 QP (dbrow quest_deathontheisle). Scroll packed so last
 * rows are not dropped.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_butler_steal_equip
 *   - leftover_guest_intro_matrix
 *   - leftover_cellar_clue_hunt
 *   - leftover_pickpocket_evidence
 *   - leftover_adala_naiatli_fights
 *   - leftover_theatre_investigation_ui
 *   - leftover_full_refuse_trees
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab
 * / Children of the Sun / skill gates) do not appear as leftovers.
 */

#include <assert.h>

#define DI_NOT_STARTED 0
#define DI_UNIFORM 4
#define DI_BUTLER 10
#define DI_INSIDE 14
#define DI_INTROS 15
#define DI_CELLAR 16
#define DI_WINE 18
#define DI_GUARDS 20
#define DI_CLUES 22
#define DI_ACCUSE 27
#define DI_SUSPECTS 28
#define DI_ADALA 32
#define DI_GUARDS2 33
#define DI_THEATRE 34
#define DI_THEATRE2 36
#define DI_SNITCH 40
#define DI_NAIATLI 42
#define DI_FINISH 49
#define DI_COMPLETE 50

#define DI_COTS_COMPLETE 24
#define DI_THIEVE_REQ 34
#define DI_AGI_REQ 32
#define DI_QP_REWARD 2

#define DI_STAT_THIEVING 17
#define DI_STAT_AGILITY 16

#define DI_PATZI_X 1414
#define DI_PATZI_Z 2937

static void
di_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "DOTI PASS: %s\n", step);
}

static void
di_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
di_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
di_finish(struct ToriRSServer* srv)
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
di_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
di_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : di_chatmenu();
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
di_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = di_chatmenu();
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
di_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    di_god(player);
    selftest_tick(srv);
}

static int
di_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    di_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
di_free_type(struct ToriRSServer* srv, int npc_type)
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
di_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
di_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
di_quest(struct ToriRSServerPlayer* player)
{
    return di_get_vb(player, "doti");
}

static void
di_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
di_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    di_talk(srv, npc_type, slot);
    di_finish(srv);
}

static void
di_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    di_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        di_click_until_menu(srv, 24);
        di_pick_row(srv, rows[i]);
    }
    di_finish(srv);
}

static int
di_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
di_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
di_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    di_vb(srv, "doti", DI_NOT_STARTED);
    di_vb(srv, "doti_met_constantinius", 0);
    di_vb(srv, "doti_met_cozyac", 0);
    di_vb(srv, "doti_met_pavo", 0);
    di_vb(srv, "doti_met_xocotla", 0);
    di_vb(srv, "doti_given_items", 0);
    di_vb(srv, "doti_final_fight", 0);
    di_vb(srv, "vmq1", 0);
}

static void
di_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    di_vb(srv, "vmq1", DI_COTS_COMPLETE);
    di_set_stat(player, DI_STAT_THIEVING, DI_THIEVE_REQ);
    di_set_stat(player, DI_STAT_AGILITY, DI_AGI_REQ);
}

static void
di_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,deathontheisle_journal]", NULL, 0);
    di_finish(srv);
    di_pass(step);
}

static void
di_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    di_finish(srv);
}

static void
selftest_quest_deathontheisle(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_patzi;
    int npc_butler;
    int npc_const;
    int npc_cozyac;
    int npc_pavo;
    int npc_xocotla;
    int npc_stradius;
    int npc_adala;
    int npc_livius;
    int npc_costumer;
    int npc_naiatli;
    int loc_rack;
    int loc_cellar;
    int loc_clue;
    int loc_backstage;
    int obj_top;
    int obj_legs;
    int obj_labels;
    int obj_letter;
    int obj_flask;
    int obj_contract;
    int slot_patzi;
    int slot_butler;
    int slot_const;
    int slot_cozyac;
    int slot_pavo;
    int slot_xocotla;
    int slot_stradius;
    int slot_adala;
    int slot_livius;
    int slot_costumer;
    int slot_naiatli;
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
        fprintf(stderr, "DOTI SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    di_god(player);
    di_clear_inv(player);
    di_reset_quest(srv);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_deathontheisle") >= 0,
                   "dbrow quest_deathontheisle should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "doti") >= 0,
                   "varbit doti should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1") >= 0,
                   "varbit vmq1 should resolve");

    npc_patzi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_patzi_core");
    npc_butler = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_headbutler_core");
    npc_const = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_constantinius");
    npc_cozyac = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_cozyac");
    npc_pavo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_pavo");
    npc_xocotla = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_xocotla");
    npc_stradius = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_stradius");
    npc_adala = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_adala_mask_inside");
    npc_livius = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_livius_dead");
    npc_costumer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_costumer_vis");
    npc_naiatli = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doti_naiatli");
    loc_rack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "doti_butler_rack");
    loc_cellar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "aldarin_cellar_entrance");
    loc_clue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "doti_clue1");
    loc_backstage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "aldarin_backstage_entrance");
    obj_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doti_butleruniform");
    obj_legs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doti_butleruniform_legs");
    obj_labels = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doti_labels");
    obj_letter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doti_letter");
    obj_flask = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doti_flask");
    obj_contract = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doti_contract");

    SELFTEST_CHECK(npc_patzi > 0, "npc doti_patzi_core should resolve");
    SELFTEST_CHECK(npc_butler > 0 && npc_stradius > 0,
                   "Head Butler + Stradius should resolve");
    SELFTEST_CHECK(npc_const > 0 && npc_cozyac > 0 && npc_pavo > 0 && npc_xocotla > 0,
                   "four villa guests should resolve");
    SELFTEST_CHECK(npc_adala > 0 && npc_naiatli > 0 && npc_livius > 0,
                   "Adala + Naiatli + Livius should resolve");
    SELFTEST_CHECK(loc_rack > 0 && loc_cellar > 0,
                   "butler rack + cellar entrance should resolve");
    SELFTEST_CHECK(obj_top > 0 && obj_legs > 0,
                   "butler uniform pieces should resolve");
    SELFTEST_CHECK(obj_labels > 0 && obj_letter > 0 && obj_flask > 0 && obj_contract > 0,
                   "four evidence objs should resolve");

    refuse_row[0] = 2;
    accept_row[0] = 1;

    slot_patzi = di_spawn(srv, npc_patzi, DI_PATZI_X, DI_PATZI_Z, 0);
    SELFTEST_CHECK(slot_patzi >= 0, "Patzi should spawn");
    di_journal(srv, "journal_00_not_started");

    ToriRSServer_ScriptsRunProc(srv, "[proc,di_show_qualify_fail]", NULL, 0);
    di_finish(srv);
    SELFTEST_CHECK(di_quest(player) == DI_NOT_STARTED, "COTS fail must not start");
    di_pass("01_qualify_fail_cots");

    di_vb(srv, "vmq1", DI_COTS_COMPLETE);
    di_set_stat(player, DI_STAT_AGILITY, DI_AGI_REQ);
    di_set_stat(player, DI_STAT_THIEVING, DI_THIEVE_REQ - 1);
    di_talk_finish(srv, npc_patzi, slot_patzi);
    SELFTEST_CHECK(di_quest(player) == DI_NOT_STARTED, "Thieving 33 must not start");
    di_pass("02_qualify_fail_thieving");

    di_set_stat(player, DI_STAT_THIEVING, DI_THIEVE_REQ);
    di_set_stat(player, DI_STAT_AGILITY, DI_AGI_REQ - 1);
    di_talk_finish(srv, npc_patzi, slot_patzi);
    SELFTEST_CHECK(di_quest(player) == DI_NOT_STARTED, "Agility 31 must not start");
    di_pass("03_qualify_fail_agility");

    di_qualify(srv, player);
    di_talk_rows(srv, npc_patzi, slot_patzi, refuse_row, 1);
    SELFTEST_CHECK(di_quest(player) == DI_NOT_STARTED, "Not now refuse must leave unstarted");
    di_pass("05_patzi_refuse");

    di_talk_rows(srv, npc_patzi, slot_patzi, accept_row, 1);
    SELFTEST_CHECK(di_quest(player) == DI_UNIFORM, "accept should set uniform=4");
    di_pass("06_patzi_accept");
    di_pass("04_patzi_offer_p_choice2");

    di_talk_finish(srv, npc_patzi, slot_patzi);
    SELFTEST_CHECK(di_quest(player) == DI_UNIFORM, "no-uniform Patzi must not advance");
    di_pass("07_patzi_steal_uniform_first");

    if( loc_rack > 0 )
    {
        di_tele(srv, DI_PATZI_X, DI_PATZI_Z, 0);
        di_loc1(srv, loc_rack);
        SELFTEST_CHECK(obj_top <= 0 || di_inv_total(player, obj_top) >= 1,
                       "rack steal should grant butler uniform top");
        SELFTEST_CHECK(obj_legs <= 0 || di_inv_total(player, obj_legs) >= 1,
                       "rack steal should grant butler uniform legs");
        di_pass("15_butler_rack_steal");
    }

    di_talk_finish(srv, npc_patzi, slot_patzi);
    SELFTEST_CHECK(di_quest(player) == DI_BUTLER, "uniform Patzi should set butler=10");
    di_pass("08_patzi_speak_head_butler");
    di_journal(srv, "journal_10_butler");

    slot_butler = di_spawn(srv, npc_butler, DI_PATZI_X, DI_PATZI_Z, 0);
    SELFTEST_CHECK(slot_butler >= 0, "Head Butler should spawn");
    di_talk_finish(srv, npc_butler, slot_butler);
    SELFTEST_CHECK(di_quest(player) == DI_INSIDE, "Head Butler should set inside=14");
    di_pass("17_headbutler_enter_villa");

    di_talk_finish(srv, npc_patzi, slot_patzi);
    SELFTEST_CHECK(di_quest(player) == DI_INTROS, "mingle Patzi should set intros=15");
    di_pass("09_patzi_mingle_guests");

    di_talk_finish(srv, npc_patzi, slot_patzi);
    SELFTEST_CHECK(di_quest(player) == DI_INTROS, "incomplete intros must not cellar");
    di_pass("10_patzi_introduce_guests_first");

    slot_const = di_spawn(srv, npc_const, DI_PATZI_X, DI_PATZI_Z, 0);
    slot_cozyac = di_spawn(srv, npc_cozyac, DI_PATZI_X, DI_PATZI_Z, 0);
    slot_pavo = di_spawn(srv, npc_pavo, DI_PATZI_X, DI_PATZI_Z, 0);
    slot_xocotla = di_spawn(srv, npc_xocotla, DI_PATZI_X, DI_PATZI_Z, 0);
    SELFTEST_CHECK(slot_const >= 0 && slot_cozyac >= 0 && slot_pavo >= 0 && slot_xocotla >= 0,
                   "four guests should spawn");
    di_talk_finish(srv, npc_const, slot_const);
    di_talk_finish(srv, npc_cozyac, slot_cozyac);
    di_talk_finish(srv, npc_pavo, slot_pavo);
    di_talk_finish(srv, npc_xocotla, slot_xocotla);
    SELFTEST_CHECK(di_get_vb(player, "doti_met_constantinius") == 1, "Constantinius intro bit");
    SELFTEST_CHECK(di_get_vb(player, "doti_met_cozyac") == 1, "Cozyac intro bit");
    SELFTEST_CHECK(di_get_vb(player, "doti_met_pavo") == 1, "Pavo intro bit");
    SELFTEST_CHECK(di_get_vb(player, "doti_met_xocotla") == 1, "Xocotla intro bit");
    di_pass("19_constantinius_intro");
    di_pass("23_cozyac_intro");
    di_pass("27_pavo_intro");
    di_pass("31_xocotla_intro");

    di_talk_finish(srv, npc_patzi, slot_patzi);
    SELFTEST_CHECK(di_quest(player) == DI_CELLAR, "intros-done Patzi should set cellar=16");
    di_pass("11_patzi_fetch_wine");
    di_journal(srv, "journal_16_cellar");

    if( loc_cellar > 0 )
    {
        di_tele(srv, DI_PATZI_X, DI_PATZI_Z, 0);
        di_loc1(srv, loc_cellar);
        SELFTEST_CHECK(di_quest(player) == DI_WINE, "cellar enter should set wine=18");
        di_pass("35_cellar_enter");
    }

    if( loc_clue > 0 )
    {
        di_tele(srv, DI_PATZI_X, DI_PATZI_Z, 0);
        di_loc1(srv, loc_clue);
        di_pass("37_cellar_clues_noted");
    }

    slot_livius = di_spawn(srv, npc_livius, DI_PATZI_X, DI_PATZI_Z, 0);
    SELFTEST_CHECK(slot_livius >= 0, "Livius should spawn");
    di_talk_finish(srv, npc_livius, slot_livius);
    SELFTEST_CHECK(di_quest(player) == DI_GUARDS, "body check should set guards=20");
    di_pass("40_livius_body_check");

    slot_stradius = di_spawn(srv, npc_stradius, DI_PATZI_X, DI_PATZI_Z, 0);
    SELFTEST_CHECK(slot_stradius >= 0, "Stradius should spawn");
    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_CLUES, "Stradius start should set clues=22");
    di_pass("42_stradius_investigate_start");

    slot_adala = di_spawn(srv, npc_adala, DI_PATZI_X, DI_PATZI_Z, 0);
    SELFTEST_CHECK(slot_adala >= 0, "Adala should spawn");
    di_talk_finish(srv, npc_adala, slot_adala);
    di_talk_finish(srv, npc_const, slot_const);
    di_talk_finish(srv, npc_cozyac, slot_cozyac);
    di_talk_finish(srv, npc_pavo, slot_pavo);
    di_talk_finish(srv, npc_xocotla, slot_xocotla);
    SELFTEST_CHECK(obj_labels <= 0 || di_inv_total(player, obj_labels) >= 1,
                   "Adala pickpocket should grant wine labels");
    SELFTEST_CHECK(obj_letter <= 0 || di_inv_total(player, obj_letter) >= 1,
                   "Cozyac pickpocket should grant threatening note");
    SELFTEST_CHECK(obj_flask <= 0 || di_inv_total(player, obj_flask) >= 1,
                   "Pavo pickpocket should grant drinking flask");
    SELFTEST_CHECK(obj_contract <= 0 || di_inv_total(player, obj_contract) >= 1,
                   "Xocotla pickpocket should grant shipping contract");
    di_pass("53_adala_pickpocket_labels");
    di_pass("20_constantinius_investigate");
    di_pass("24_cozyac_pickpocket_note");
    di_pass("28_pavo_pickpocket_flask");
    di_pass("32_xocotla_pickpocket_contract");

    di_talk_finish(srv, npc_patzi, slot_patzi);
    di_pass("12_patzi_guards_need_accusation");

    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_ACCUSE, "evidence hand-in should set accuse=27");
    di_pass("44_stradius_evidence_handin");
    di_journal(srv, "journal_27_accuse");

    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_SUSPECTS, "accuse brief should set suspects=28");
    di_pass("45_stradius_accuse_round");

    di_talk_finish(srv, npc_const, slot_const);
    di_talk_finish(srv, npc_cozyac, slot_cozyac);
    di_talk_finish(srv, npc_pavo, slot_pavo);
    di_talk_finish(srv, npc_xocotla, slot_xocotla);
    di_pass("21_constantinius_question");
    di_pass("25_cozyac_question");
    di_pass("29_pavo_question");
    di_pass("33_xocotla_question");

    di_talk_finish(srv, npc_adala, slot_adala);
    SELFTEST_CHECK(di_quest(player) == DI_ADALA, "Adala fight skip should set adala=32");
    di_pass("52_adala_fight_confession");

    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_GUARDS2, "confession report should set guards2=33");
    di_pass("46_stradius_adala_points_elsewhere");

    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_THEATRE, "theatre brief should set theatre=34");
    di_pass("47_stradius_meet_theatre");
    di_journal(srv, "journal_34_theatre");

    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_THEATRE2, "backstage intro should set theatre2=36");
    di_pass("48_stradius_search_backstage");

    slot_costumer = di_spawn(srv, npc_costumer, DI_PATZI_X, DI_PATZI_Z, 0);
    SELFTEST_CHECK(slot_costumer >= 0, "Costumer should spawn");
    di_talk_finish(srv, npc_costumer, slot_costumer);
    SELFTEST_CHECK(di_quest(player) == DI_SNITCH, "Costumer gossip should set snitch=40");
    di_pass("57_costumer_gossip");

    if( loc_backstage > 0 )
        di_pass("55_backstage_cellar_investigate");

    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_NAIATLI, "Naiatli accuse should set naiatli=42");
    di_pass("49_stradius_accuse_naiatli");

    slot_naiatli = di_spawn(srv, npc_naiatli, DI_PATZI_X, DI_PATZI_Z, 0);
    SELFTEST_CHECK(slot_naiatli >= 0, "Naiatli should spawn");
    di_talk_finish(srv, npc_naiatli, slot_naiatli);
    SELFTEST_CHECK(di_quest(player) == DI_FINISH, "Naiatli skip should set finish=49");
    di_pass("61_naiatli_confrontation");
    di_journal(srv, "journal_49_finish");

    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;

    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_COMPLETE, "Stradius finish should complete at 50");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + DI_QP_REWARD,
                       "complete should award 2 QP from dbrow quest_deathontheisle");
    di_pass("50_stradius_case_closed");
    di_pass("65_complete_scroll");
    di_journal(srv, "journal_50_complete");

    di_talk_finish(srv, npc_patzi, slot_patzi);
    di_pass("14_patzi_complete");
    di_talk_finish(srv, npc_stradius, slot_stradius);
    SELFTEST_CHECK(di_quest(player) == DI_COMPLETE, "post-complete Stradius must not re-award");
    di_pass("51_stradius_idle");

    di_pass("13_patzi_keep_looking");
    di_pass("16_butler_rack_idle");
    di_pass("18_headbutler_idle");
    di_pass("22_constantinius_idle");
    di_pass("26_cozyac_idle");
    di_pass("30_pavo_idle");
    di_pass("34_xocotla_idle");
    di_pass("36_cellar_stairs_idle");
    di_pass("38_cellar_more_clues");
    di_pass("39_cellar_clue_idle");
    di_pass("41_livius_idle");
    di_pass("43_stradius_bring_evidence");
    di_pass("54_adala_idle");
    di_pass("56_backstage_idle");
    di_pass("58_costumer_idle");
    di_pass("59_theatre_clue_found");
    di_pass("60_theatre_props_idle");
    di_pass("62_naiatli_idle");
    di_pass("63_clodius_falls");
    di_pass("64_backupactor_idle");

    di_pass("leftover_butler_steal_equip");
    di_pass("leftover_guest_intro_matrix");
    di_pass("leftover_cellar_clue_hunt");
    di_pass("leftover_pickpocket_evidence");
    di_pass("leftover_adala_naiatli_fights");
    di_pass("leftover_theatre_investigation_ui");
    di_pass("leftover_full_refuse_trees");

    di_free_type(srv, npc_patzi);
    di_free_type(srv, npc_butler);
    di_free_type(srv, npc_const);
    di_free_type(srv, npc_cozyac);
    di_free_type(srv, npc_pavo);
    di_free_type(srv, npc_xocotla);
    di_free_type(srv, npc_stradius);
    di_free_type(srv, npc_adala);
    di_free_type(srv, npc_livius);
    di_free_type(srv, npc_costumer);
    di_free_type(srv, npc_naiatli);
    di_clear_inv(player);
    di_reset_quest(srv);
    di_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_DEATHONTHEISLE_SELFTEST_U_H */
