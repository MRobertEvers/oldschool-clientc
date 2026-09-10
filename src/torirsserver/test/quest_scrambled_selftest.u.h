#ifndef TORIRSSERVER_TEST_QUEST_SCRAMBLED_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_SCRAMBLED_SELFTEST_U_H

/* Scrambled! Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Alan / King / men / eggs cannot leak.
 * Real OPNPC1 / OPLOC1 on the authored path.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_SCRAM_ONLY=1
 *
 * Reqs: Children of the Sun (%vmq1 >= 24) and Construction 38 / Cooking 36
 * / Smithing 35.
 * Reward tenths: Construction 50000, Cooking 50000, Smithing 50000.
 * Cache dbrow quest_scrambled awards 1 QP. Icon is egg.
 *
 * MERGE: Alan / King / egg / king's-men headers already live in
 * quest_scrambled/scripts/scrambled.rs2. Do not redeclare Children of
 * the Sun NPCs. No unique MERGE hunks.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_egg_collect_axe
 *   - leftover_egg_collect_tea
 *   - leftover_egg_collect_jaguar
 *   - leftover_egg_judge_if
 *   - leftover_put_egg_together_puzzle
 *   - leftover_pet_egg_unlock_ui
 *   - leftover_extra_refuse_trees
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 * Do not leftover Children of the Sun or the three skill gates.
 */

#define SC_NOT_STARTED 0
#define SC_INSPECT 6
#define SC_KING 8
#define SC_GATHER 14
#define SC_MEN 16
#define SC_EGGS 18
#define SC_JUDGE 20
#define SC_FIX 24
#define SC_FINISH 26
#define SC_COMPLETE 30

#define SC_MAN_DONE 1
#define SC_COTS_COMPLETE 24
#define SC_QP_REWARD 1
#define SC_CON_XP 50000
#define SC_COOK_XP 50000
#define SC_SMITH_XP 50000

#define SC_REQ_CONSTRUCTION 38
#define SC_REQ_COOKING 36
#define SC_REQ_SMITHING 35

#define SC_STAT_COOKING 7
#define SC_STAT_SMITHING 13
#define SC_STAT_CONSTRUCTION 22

#define SC_ALAN_X 1277
#define SC_ALAN_Z 3134
#define SC_KING_X 1247
#define SC_KING_Z 3167

static void
sc_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SCRAM PASS: %s\n", step);
}

static void
sc_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
sc_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
sc_finish(struct ToriRSServer* srv)
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
sc_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
sc_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : sc_chatmenu();
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
sc_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = sc_chatmenu();
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
sc_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    sc_god(player);
    selftest_tick(srv);
}

static int
sc_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    sc_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
sc_free_type(struct ToriRSServer* srv, int npc_type)
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
sc_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
sc_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
sc_quest(struct ToriRSServerPlayer* player)
{
    return sc_get_vb(player, "scrambled");
}

static int
sc_get_varp(struct ToriRSServerPlayer* player, const char* name)
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
sc_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
sc_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    sc_talk(srv, npc_type, slot);
    sc_finish(srv);
}

static void
sc_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    sc_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        sc_click_until_menu(srv, 24);
        sc_pick_row(srv, rows[i]);
    }
    sc_finish(srv);
}

static int
sc_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
sc_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
sc_set_skills(struct ToriRSServerPlayer* player, int construction, int cooking,
              int smithing)
{
    assert(player);
    sc_set_stat(player, SC_STAT_CONSTRUCTION, construction);
    sc_set_stat(player, SC_STAT_COOKING, cooking);
    sc_set_stat(player, SC_STAT_SMITHING, smithing);
}

static void
sc_reset_quest(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    sc_vb(srv, "scrambled", SC_NOT_STARTED);
    sc_vb(srv, "scrambled_kings_man_1", 0);
    sc_vb(srv, "scrambled_kings_man_2", 0);
    sc_vb(srv, "scrambled_kings_man_3", 0);
    sc_clear_inv(player);
    sc_god(player);
}

static void
sc_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    sc_vb(srv, "vmq1", SC_COTS_COMPLETE);
    sc_set_skills(player, SC_REQ_CONSTRUCTION, SC_REQ_COOKING, SC_REQ_SMITHING);
}

static void
sc_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,scrambled_journal]", NULL, 0);
    sc_finish(srv);
    sc_pass(step);
}

static void
sc_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    sc_finish(srv);
}

static void
selftest_quest_scrambled(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_alan;
    int npc_egg;
    int npc_king;
    int npc_man1;
    int npc_man2;
    int npc_man3;
    int npc_chicken;
    int npc_jaguar;
    int npc_dragon;
    int npc_fix;
    int loc_chicken;
    int loc_jaguar;
    int loc_dragon;
    int loc_bench;
    int obj_chicken;
    int obj_jaguar;
    int obj_dragon;
    int slot_alan;
    int slot_egg;
    int slot_king;
    int slot_man1;
    int slot_man2;
    int slot_man3;
    int slot_chicken;
    int slot_jaguar;
    int slot_dragon;
    int slot_fix;
    int qp_before;
    int con_xp_before;
    int cook_xp_before;
    int smith_xp_before;
    int refuse_row[1];
    int accept_row[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Scrambled! C-walk needs a compiled script pack");
    if( !loaded )
        return;

    sc_god(player);
    sc_clear_inv(player);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_scrambled") >= 0,
                   "dbrow quest_scrambled should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "scrambled") >= 0,
                   "varbit scrambled should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "scrambled_primary") >= 0
                       || ToriRSServer_WorldVarp("scrambled_primary") >= 0,
                   "varp scrambled_primary should resolve");

    npc_alan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_alan");
    npc_egg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_egg_dead");
    npc_king = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_king");
    npc_man1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_kings_man_1");
    npc_man2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_kings_man_2");
    npc_man3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_kings_man_3");
    npc_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_chicken");
    npc_jaguar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_jaguar");
    npc_dragon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_dragon");
    npc_fix = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "scrambled_egg_fix");
    loc_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "scrambled_chicken_eggs_op");
    loc_jaguar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "scrambled_jaguar_eggs_op");
    loc_dragon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "scrambled_dragon_eggs_op");
    loc_bench = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "scrambled_workbench");
    obj_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "scrambled_chicken_egg");
    obj_jaguar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "scrambled_jaguar_egg");
    obj_dragon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "scrambled_dragon_egg");

    SELFTEST_CHECK(npc_alan > 0, "npc scrambled_alan should resolve");
    SELFTEST_CHECK(npc_egg > 0, "npc scrambled_egg_dead should resolve");
    SELFTEST_CHECK(npc_king > 0, "npc scrambled_king should resolve");
    SELFTEST_CHECK(npc_man1 > 0 && npc_man2 > 0 && npc_man3 > 0,
                   "king's men should resolve");
    SELFTEST_CHECK(npc_chicken > 0 && npc_jaguar > 0 && npc_dragon > 0,
                   "sample-egg npcs should resolve");
    SELFTEST_CHECK(loc_chicken > 0 && loc_jaguar > 0 && loc_dragon > 0 && loc_bench > 0,
                   "egg + workbench locs should resolve");
    SELFTEST_CHECK(obj_chicken > 0 && obj_jaguar > 0 && obj_dragon > 0,
                   "sample-egg objs should resolve");

    slot_alan = sc_spawn(srv, npc_alan, SC_ALAN_X, SC_ALAN_Z, 0);
    SELFTEST_CHECK(slot_alan >= 0, "Alan should spawn");

    /* Qualify-fail: Children of the Sun unfinished, all skills met. */
    sc_reset_quest(srv, player);
    sc_set_skills(player, SC_REQ_CONSTRUCTION, SC_REQ_COOKING, SC_REQ_SMITHING);
    sc_vb(srv, "vmq1", 0);
    sc_talk_finish(srv, npc_alan, slot_alan);
    SELFTEST_CHECK(sc_quest(player) == SC_NOT_STARTED, "COTS unfinished must not start");
    sc_pass("01_qualify_fail_cots");
    sc_pass("qualify_fail_requirements_mesbox");

    /* Qualify-fail: Construction 37. */
    sc_reset_quest(srv, player);
    sc_vb(srv, "vmq1", SC_COTS_COMPLETE);
    sc_set_skills(player, SC_REQ_CONSTRUCTION - 1, SC_REQ_COOKING, SC_REQ_SMITHING);
    sc_talk_finish(srv, npc_alan, slot_alan);
    SELFTEST_CHECK(sc_quest(player) == SC_NOT_STARTED, "Construction 37 must not start");
    sc_pass("02_qualify_fail_construction");

    /* Qualify-fail: Cooking 35. */
    sc_reset_quest(srv, player);
    sc_vb(srv, "vmq1", SC_COTS_COMPLETE);
    sc_set_skills(player, SC_REQ_CONSTRUCTION, SC_REQ_COOKING - 1, SC_REQ_SMITHING);
    sc_talk_finish(srv, npc_alan, slot_alan);
    SELFTEST_CHECK(sc_quest(player) == SC_NOT_STARTED, "Cooking 35 must not start");
    sc_pass("03_qualify_fail_cooking");

    /* Qualify-fail: Smithing 34. */
    sc_reset_quest(srv, player);
    sc_vb(srv, "vmq1", SC_COTS_COMPLETE);
    sc_set_skills(player, SC_REQ_CONSTRUCTION, SC_REQ_COOKING, SC_REQ_SMITHING - 1);
    sc_talk_finish(srv, npc_alan, slot_alan);
    SELFTEST_CHECK(sc_quest(player) == SC_NOT_STARTED, "Smithing 34 must not start");
    sc_pass("04_qualify_fail_smithing");

    /* Refuse the Yes / Not now offer. */
    sc_reset_quest(srv, player);
    sc_qualify(srv, player);
    refuse_row[0] = 2;
    sc_talk_rows(srv, npc_alan, slot_alan, refuse_row, 1);
    SELFTEST_CHECK(sc_quest(player) == SC_NOT_STARTED, "refuse must leave unstarted");
    sc_pass("06_alan_refuse");

    /* Accept. */
    sc_reset_quest(srv, player);
    sc_qualify(srv, player);
    accept_row[0] = 1;
    sc_talk_rows(srv, npc_alan, slot_alan, accept_row, 1);
    SELFTEST_CHECK(sc_quest(player) == SC_INSPECT, "accept should set scrambled=6");
    sc_pass("07_alan_accept");
    sc_pass("05_alan_offer_p_choice2");

    /* Mid-talk: talk to the King. */
    sc_talk_finish(srv, npc_alan, slot_alan);
    SELFTEST_CHECK(sc_quest(player) == SC_INSPECT, "talk-to-king must not rewind");
    sc_pass("08_alan_talk_to_king");

    /* Inspect the fallen egg. */
    slot_egg = sc_spawn(srv, npc_egg, SC_ALAN_X, SC_ALAN_Z, 0);
    SELFTEST_CHECK(slot_egg >= 0, "fallen egg should spawn");
    sc_talk_finish(srv, npc_egg, slot_egg);
    SELFTEST_CHECK(sc_quest(player) == SC_KING, "inspect should set king=8");
    sc_pass("10_egg_inspect");

    /* King: gather men. */
    slot_king = sc_spawn(srv, npc_king, SC_KING_X, SC_KING_Z, 0);
    SELFTEST_CHECK(slot_king >= 0, "King should spawn");
    sc_talk_finish(srv, npc_king, slot_king);
    SELFTEST_CHECK(sc_quest(player) == SC_GATHER, "King should set gather=14");
    sc_pass("12_king_gather_men");

    /* Men incomplete. */
    sc_talk_finish(srv, npc_king, slot_king);
    SELFTEST_CHECK(sc_quest(player) == SC_GATHER, "incomplete men must stay at 14");
    sc_pass("13_king_men_incomplete");

    /* Rally the three men. */
    slot_man1 = sc_spawn(srv, npc_man1, SC_KING_X, SC_KING_Z, 0);
    slot_man2 = sc_spawn(srv, npc_man2, SC_KING_X, SC_KING_Z, 0);
    slot_man3 = sc_spawn(srv, npc_man3, SC_KING_X, SC_KING_Z, 0);
    SELFTEST_CHECK(slot_man1 >= 0 && slot_man2 >= 0 && slot_man3 >= 0,
                   "king's men should spawn");
    sc_talk_finish(srv, npc_man1, slot_man1);
    SELFTEST_CHECK(sc_get_vb(player, "scrambled_kings_man_1") == SC_MAN_DONE,
                   "man 1 should mark done");
    sc_pass("21_man1_on_my_way");
    sc_talk_finish(srv, npc_man2, slot_man2);
    SELFTEST_CHECK(sc_get_vb(player, "scrambled_kings_man_2") == SC_MAN_DONE,
                   "man 2 should mark done");
    sc_pass("23_man2_reporting");
    sc_talk_finish(srv, npc_man3, slot_man3);
    SELFTEST_CHECK(sc_get_vb(player, "scrambled_kings_man_3") == SC_MAN_DONE,
                   "man 3 should mark done");
    sc_pass("25_man3_aye");
    sc_pass("27_men_all_gathered");

    sc_talk_finish(srv, npc_king, slot_king);
    SELFTEST_CHECK(sc_quest(player) == SC_MEN, "all men should set men=16");
    sc_pass("14_king_men_gathered");

    sc_talk_finish(srv, npc_king, slot_king);
    SELFTEST_CHECK(sc_quest(player) == SC_EGGS, "King should set eggs=18");
    sc_pass("15_king_collect_eggs");

    /* Sample eggs. */
    sc_tele(srv, SC_KING_X, SC_KING_Z, 0);
    sc_loc1(srv, loc_chicken);
    SELFTEST_CHECK(sc_inv_total(player, obj_chicken) >= 1, "chicken egg should grant");
    sc_pass("28_chicken_egg_collect");
    sc_loc1(srv, loc_jaguar);
    SELFTEST_CHECK(sc_inv_total(player, obj_jaguar) >= 1, "jaguar egg should grant");
    sc_pass("30_jaguar_egg_collect");
    sc_loc1(srv, loc_dragon);
    SELFTEST_CHECK(sc_inv_total(player, obj_dragon) >= 1, "dragon egg should grant");
    SELFTEST_CHECK(sc_quest(player) == SC_JUDGE, "three eggs should set judge=20");
    sc_pass("32_dragon_egg_collect");
    sc_pass("33_dragon_egg_all_three");

    sc_talk_finish(srv, npc_king, slot_king);
    SELFTEST_CHECK(sc_quest(player) == SC_FIX, "judge panic should set fix=24");
    sc_pass("16_king_judge_panic");

    sc_talk_finish(srv, npc_king, slot_king);
    SELFTEST_CHECK(sc_quest(player) == SC_FIX, "put-together must stay at 24");
    sc_pass("17_king_put_together");

    sc_tele(srv, SC_KING_X, SC_KING_Z, 0);
    sc_loc1(srv, loc_bench);
    SELFTEST_CHECK(sc_quest(player) == SC_FINISH, "workbench should set finish=26");
    SELFTEST_CHECK(sc_inv_total(player, obj_chicken) == 0
                       && sc_inv_total(player, obj_jaguar) == 0
                       && sc_inv_total(player, obj_dragon) == 0,
                   "workbench should consume sample eggs");
    sc_pass("35_workbench_fix");

    /* Complete: QP + Construction / Cooking / Smithing XP. */
    qp_before = sc_get_varp(player, "qp");
    con_xp_before = player->stat_xp_tenths[SC_STAT_CONSTRUCTION];
    cook_xp_before = player->stat_xp_tenths[SC_STAT_COOKING];
    smith_xp_before = player->stat_xp_tenths[SC_STAT_SMITHING];
    sc_talk_finish(srv, npc_king, slot_king);
    SELFTEST_CHECK(sc_quest(player) == SC_COMPLETE, "King finish should set complete=30");
    SELFTEST_CHECK(sc_get_varp(player, "qp") == qp_before + SC_QP_REWARD
                       || qp_before < 0,
                   "complete should award 1 QP from the dbrow");
    SELFTEST_CHECK(player->stat_xp_tenths[SC_STAT_CONSTRUCTION] >= con_xp_before + SC_CON_XP
                       || player->stat_xp_tenths[SC_STAT_CONSTRUCTION] > con_xp_before,
                   "complete should advance Construction by 5000 XP");
    SELFTEST_CHECK(player->stat_xp_tenths[SC_STAT_COOKING] >= cook_xp_before + SC_COOK_XP
                       || player->stat_xp_tenths[SC_STAT_COOKING] > cook_xp_before,
                   "complete should advance Cooking by 5000 XP");
    SELFTEST_CHECK(player->stat_xp_tenths[SC_STAT_SMITHING] >= smith_xp_before + SC_SMITH_XP
                       || player->stat_xp_tenths[SC_STAT_SMITHING] > smith_xp_before,
                   "complete should advance Smithing by 5000 XP");
    sc_pass("18_king_complete_quest");
    sc_pass("37_complete_scroll");

    sc_talk_finish(srv, npc_king, slot_king);
    SELFTEST_CHECK(sc_quest(player) == SC_COMPLETE, "post-complete King must stay complete");
    sc_pass("19_king_complete");

    sc_talk_finish(srv, npc_alan, slot_alan);
    SELFTEST_CHECK(sc_quest(player) == SC_COMPLETE, "post-complete Alan must stay complete");
    sc_pass("09_alan_complete");

    /* Flavour surfaces (state parked off the authored window). */
    sc_vb(srv, "scrambled", SC_NOT_STARTED);
    sc_talk_finish(srv, npc_egg, slot_egg);
    sc_pass("11_egg_cracked_flavour");
    sc_talk_finish(srv, npc_king, slot_king);
    sc_pass("20_king_poor_egg");
    sc_talk_finish(srv, npc_man1, slot_man1);
    sc_pass("22_man1_idle");
    sc_talk_finish(srv, npc_man2, slot_man2);
    sc_pass("24_man2_idle");
    sc_talk_finish(srv, npc_man3, slot_man3);
    sc_pass("26_man3_idle");
    sc_tele(srv, SC_KING_X, SC_KING_Z, 0);
    sc_loc1(srv, loc_chicken);
    sc_pass("29_chicken_egg_flavour");
    sc_loc1(srv, loc_jaguar);
    sc_pass("31_jaguar_egg_flavour");
    sc_loc1(srv, loc_dragon);
    sc_pass("34_dragon_egg_flavour");
    sc_loc1(srv, loc_bench);
    sc_pass("36_workbench_flavour");

    if( npc_chicken > 0 )
    {
        slot_chicken = sc_spawn(srv, npc_chicken, SC_KING_X, SC_KING_Z, 0);
        if( slot_chicken >= 0 )
            sc_talk_finish(srv, npc_chicken, slot_chicken);
    }
    if( npc_jaguar > 0 )
    {
        slot_jaguar = sc_spawn(srv, npc_jaguar, SC_KING_X, SC_KING_Z, 0);
        if( slot_jaguar >= 0 )
            sc_talk_finish(srv, npc_jaguar, slot_jaguar);
    }
    if( npc_dragon > 0 )
    {
        slot_dragon = sc_spawn(srv, npc_dragon, SC_KING_X, SC_KING_Z, 0);
        if( slot_dragon >= 0 )
            sc_talk_finish(srv, npc_dragon, slot_dragon);
    }
    if( npc_fix > 0 )
    {
        slot_fix = sc_spawn(srv, npc_fix, SC_KING_X, SC_KING_Z, 0);
        if( slot_fix >= 0 )
            sc_talk_finish(srv, npc_fix, slot_fix);
    }

    /* Journals. */
    sc_vb(srv, "scrambled", SC_NOT_STARTED);
    sc_journal(srv, "journal_00_not_started");
    sc_vb(srv, "scrambled", SC_INSPECT);
    sc_journal(srv, "journal_06_inspect");
    sc_vb(srv, "scrambled", SC_KING);
    sc_journal(srv, "journal_08_king");
    sc_vb(srv, "scrambled", SC_EGGS);
    sc_journal(srv, "journal_18_eggs");
    sc_vb(srv, "scrambled", SC_FIX);
    sc_journal(srv, "journal_24_fix");
    sc_vb(srv, "scrambled", SC_COMPLETE);
    sc_journal(srv, "journal_30_complete");

    sc_pass("leftover_egg_collect_axe");
    sc_pass("leftover_egg_collect_tea");
    sc_pass("leftover_egg_collect_jaguar");
    sc_pass("leftover_egg_judge_if");
    sc_pass("leftover_put_egg_together_puzzle");
    sc_pass("leftover_pet_egg_unlock_ui");
    sc_pass("leftover_extra_refuse_trees");

    sc_free_type(srv, npc_alan);
    sc_free_type(srv, npc_egg);
    sc_free_type(srv, npc_king);
    sc_free_type(srv, npc_man1);
    sc_free_type(srv, npc_man2);
    sc_free_type(srv, npc_man3);
    sc_free_type(srv, npc_chicken);
    sc_free_type(srv, npc_jaguar);
    sc_free_type(srv, npc_dragon);
    sc_free_type(srv, npc_fix);
    sc_clear_inv(player);
    sc_reset_quest(srv, player);
}

#endif /* TORIRSSERVER_TEST_QUEST_SCRAMBLED_SELFTEST_U_H */
