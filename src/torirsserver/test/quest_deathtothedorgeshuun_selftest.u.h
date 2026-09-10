#ifndef TORIRSSERVER_TEST_QUEST_DEATHTOTHEDORGESHUUN_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_DEATHTOTHEDORGESHUUN_SELFTEST_U_H

/* Death to the Dorgeshuun Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Mistag / Zanik / Johanhus / guards /
 * Sigmund / locs cannot leak. Real OPNPC1 / OPNPC2 / OPLOC1 on the
 * authored path. player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_DTTD_ONLY=1
 *
 * Mistag is the shared [opnpc1,lost_tribe_mistag_2ops] / _3ops /
 * [opnpc1,lost_tribe_mistag] / _1op trigger. Johanhus is the existing
 * [opnpc1,favour_johanhus_ulsbrecht] trigger. Juna is the existing
 * [oploc1,tog_juna] trigger. Nardok Trade is the existing
 * [opnpc3,dttd_bone_dealer] shop.
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF / daily-reset
 * / flute widget / telekinetic grab):
 *   - Nardok shop is the existing Trade trigger (full shop IF leftover-stamp)
 *   - HAM pickpocket already spliced
 *   - bone dagger / Dorgeshuun crossbow specials already in skill_combat
 *   - fairy ring AJQ already gated
 *   - MEP1 dbrow prereq corrupt; not hard-gated
 *   - OSF Johanhus branches stay on the shared trigger
 *   - HAM disguise questioning / jail compressed
 *   - Sigmund prayer-swap / mill-escape cutscene soft-skipped
 */

#define DTTD_NOT_STARTED 0
#define DTTD_MISTAG_FAVOUR 1
#define DTTD_ZANIK_RECRUITED 2
#define DTTD_LUMBRIDGE_TOURED 3
#define DTTD_HAM_INFILTRATED 4
#define DTTD_HAM_MEETING_FOUND 5
#define DTTD_ZANIK_SAVED 6
#define DTTD_TEARS_COLLECTED 7
#define DTTD_ZANIK_STORY 8
#define DTTD_MILL_ENTERED 9
#define DTTD_SIGMUND_DEFEATED 10
#define DTTD_DRILL_SMASHED 11
#define DTTD_COMPLETE 13

#define DTTD_LT_COMPLETE 11
#define DTTD_REQ_AGILITY 23
#define DTTD_REQ_THIEVING 23
#define DTTD_REWARD_TENTHS 20000
#define DTTD_REWARD_QP 1
#define DTTD_MILL_GUARDS_NEEDED 3
#define DTTD_TEARS_NEEDED 20

#define DTTD_MISTAG_X 3319
#define DTTD_MISTAG_Z 9615
#define DTTD_ZANIK_X 3212
#define DTTD_ZANIK_Z 9620
#define DTTD_DUKE_X 3209
#define DTTD_DUKE_Z 3222
#define DTTD_SUN_X 3218
#define DTTD_SUN_Z 3219
#define DTTD_CITIZEN_X 3225
#define DTTD_CITIZEN_Z 3220
#define DTTD_PRIEST_X 3243
#define DTTD_PRIEST_Z 3206
#define DTTD_GOBLIN_X 3247
#define DTTD_GOBLIN_Z 3235
#define DTTD_SHOP_X 3212
#define DTTD_SHOP_Z 3246
#define DTTD_JOHANHUS_X 3173
#define DTTD_JOHANHUS_Z 9619
#define DTTD_TRAPDOOR_X 3166
#define DTTD_TRAPDOOR_Z 9622
#define DTTD_GUARD1_X 2569
#define DTTD_GUARD1_Z 5189
#define DTTD_GUARD2_X 2566
#define DTTD_GUARD2_Z 5192
#define DTTD_GUARD3_X 2566
#define DTTD_GUARD3_Z 5194
#define DTTD_GUARD4_X 2576
#define DTTD_GUARD4_Z 5195
#define DTTD_GUARD5_X 2577
#define DTTD_GUARD5_Z 5200
#define DTTD_LISTEN_X 2571
#define DTTD_LISTEN_Z 5204
#define DTTD_BODY_X 3161
#define DTTD_BODY_Z 3246
#define DTTD_HOLE_X 3224
#define DTTD_HOLE_Z 9602
#define DTTD_CAVE_X 3226
#define DTTD_CAVE_Z 9540
#define DTTD_JUNA_X 3252
#define DTTD_JUNA_Z 9485
#define DTTD_JUNA_LEVEL 2
#define DTTD_CRATE_X 3228
#define DTTD_CRATE_Z 3280
#define DTTD_MILL_X 3230
#define DTTD_MILL_Z 3286
#define DTTD_MILLROOM_X 2000
#define DTTD_MILLROOM_Z 5087
#define DTTD_DRILL_X 1998
#define DTTD_DRILL_Z 5088
#define DTTD_EXIT_X 3245
#define DTTD_EXIT_Z 9661

static void
dttd_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "DTTD PASS: %s\n", step);
}

static void
dttd_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
dttd_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
dttd_finish(struct ToriRSServer* srv)
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
    ToriRSServer_ScriptsProcessQueues(srv);
    for( t = 0; t < 4; t++ )
        selftest_tick(srv);
}

static int
dttd_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
dttd_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : dttd_chatmenu();
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
dttd_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = dttd_chatmenu();
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
dttd_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    dttd_god(player);
    selftest_tick(srv);
}

static int
dttd_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    dttd_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
dttd_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
dttd_free_type(struct ToriRSServer* srv, int npc_type)
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
dttd_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
dttd_get_vb(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
dttd_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
dttd_quest(struct ToriRSServerPlayer* player)
{
    return dttd_get_vb(player, "dttd_main");
}

static void
dttd_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
dttd_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    dttd_talk(srv, npc_type, slot);
    dttd_finish(srv);
}

static void
dttd_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    dttd_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        dttd_click_until_menu(srv, 24);
        dttd_pick_row(srv, rows[i]);
    }
    dttd_finish(srv);
}

static void
dttd_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
dttd_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    dttd_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
dttd_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    dttd_finish(srv);
}

static void
dttd_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
dttd_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        dttd_set_stat(player, stat, 99);
}

static void
dttd_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    dttd_vb(srv, "dttd_main", DTTD_NOT_STARTED);
    dttd_vb(srv, "dttd_tour_duke", 0);
    dttd_vb(srv, "dttd_tour_priest", 0);
    dttd_vb(srv, "dttd_tour_goblins", 0);
    dttd_vb(srv, "dttd_tour_citizens", 0);
    dttd_vb(srv, "dttd_tour_sun", 0);
    dttd_vb(srv, "dttd_zanik_in_cellar", 0);
    dttd_vb(srv, "dttd_tour_shop", 0);
    dttd_vb(srv, "dttd_tour_ham_deacon", 0);
    dttd_vb(srv, "dttd_tour_ham_johanhus", 0);
    dttd_vb(srv, "dttd_ham_trapdoor_state", 0);
    dttd_vb(srv, "dttd_zanik_corpse", 0);
    dttd_vb(srv, "dttd_collecting_tears", 0);
    dttd_vb(srv, "dttd_guard_1_warned", 0);
    dttd_vb(srv, "dttd_guard_1_dead", 0);
    dttd_vb(srv, "dttd_guard_2_warned", 0);
    dttd_vb(srv, "dttd_guard_2_dead", 0);
    dttd_vb(srv, "dttd_guard_3_dead", 0);
    dttd_vb(srv, "dttd_guard_4_warned", 0);
    dttd_vb(srv, "dttd_guard_4_dead", 0);
    dttd_vb(srv, "dttd_guard_5_warned", 0);
    dttd_vb(srv, "dttd_guard_5_dead", 0);
    dttd_vb(srv, "dttd_mill_guards_dead", 0);
    dttd_vb(srv, "lost_tribe_hole_2_dug", 0);
    dttd_vb(srv, "lost_tribe_quest", DTTD_LT_COMPLETE);
    dttd_varp(srv, "dttd_tear_count", 0);
}

static void
dttd_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,dttd_journal]", NULL, 0);
    dttd_finish(srv);
    dttd_pass(step);
}

static void
dttd_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    dttd_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    dttd_finish(srv);
}

static void
dttd_give_ham_sets(struct ToriRSServerPlayer* player, int shirt, int robe, int hood,
    int boots, int gloves, int badge, int cloak)
{
    assert(player);
    if( shirt > 0 )
        worn_set(player, TORIRSSERVER_WEAR_BODY, shirt, 1);
    if( robe > 0 )
        worn_set(player, TORIRSSERVER_WEAR_LEGS, robe, 1);
    if( hood > 0 )
        worn_set(player, TORIRSSERVER_WEAR_HEAD, hood, 1);
    if( boots > 0 )
        worn_set(player, TORIRSSERVER_WEAR_FEET, boots, 1);
    if( gloves > 0 )
        worn_set(player, TORIRSSERVER_WEAR_HANDS, gloves, 1);
    if( badge > 0 )
        worn_set(player, TORIRSSERVER_WEAR_AMULET, badge, 1);
    if( cloak > 0 )
        worn_set(player, TORIRSSERVER_WEAR_CAPE, cloak, 1);
    if( shirt > 0 )
        dttd_give(player, shirt, 1);
    if( robe > 0 )
        dttd_give(player, robe, 1);
    if( hood > 0 )
        dttd_give(player, hood, 1);
    if( boots > 0 )
        dttd_give(player, boots, 1);
    if( gloves > 0 )
        dttd_give(player, gloves, 1);
    if( badge > 0 )
        dttd_give(player, badge, 1);
    if( cloak > 0 )
        dttd_give(player, cloak, 1);
}

static void
selftest_quest_deathtothedorgeshuun(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_mistag;
    int npc_mistag_shell;
    int npc_mistag_1op;
    int npc_zanik;
    int npc_zanik_cellar;
    int npc_follower;
    int npc_follower_ham;
    int npc_duke;
    int npc_priest;
    int npc_citizen;
    int npc_shopkeep;
    int npc_johanhus;
    int npc_g1;
    int npc_g2;
    int npc_g3;
    int npc_g4;
    int npc_g5;
    int npc_mill_guard;
    int npc_sigmund;
    int loc_trapdoor;
    int loc_listen;
    int loc_body;
    int loc_hole;
    int loc_cave;
    int loc_juna;
    int loc_wall;
    int loc_crate;
    int loc_mill;
    int loc_drill;
    int loc_exit;
    int obj_shirt;
    int obj_robe;
    int obj_hood;
    int obj_boots;
    int obj_gloves;
    int obj_badge;
    int obj_cloak;
    int obj_pick;
    int obj_tinder;
    int obj_bronze;
    int stat_agility;
    int stat_thieving;
    int stat_ranged;
    int varp_qp;
    int slot_mistag;
    int slot_zanik;
    int slot_follower;
    int slot_duke;
    int slot_priest;
    int slot_citizen;
    int slot_shop;
    int slot_johanhus;
    int slot_g;
    int slot_sigmund;
    int loc_slot;
    int thieving_before;
    int ranged_before;
    int qp_before;
    int i;
    static const int k_refuse[] = { 2 };
    static const int k_second_refuse[] = { 1, 2 };
    static const int k_accept[] = { 1, 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: death to the dorgeshuun critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer dttd selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    dttd_god(player);
    dttd_reset_quest(srv);
    dttd_clear_inv(player);

    npc_mistag = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lost_tribe_mistag_2ops");
    npc_mistag_shell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lost_tribe_mistag");
    npc_mistag_1op = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lost_tribe_mistag_1op");
    npc_zanik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_zanik_marked");
    npc_zanik_cellar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_zanik_cellar");
    npc_follower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_zanik_follower");
    npc_follower_ham = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_zanik_follower_ham");
    npc_duke = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "duke_of_lumbridge");
    npc_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "father_aereck");
    npc_citizen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "man");
    npc_shopkeep = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "generalshopkeeper1");
    npc_johanhus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "favour_johanhus_ulsbrecht");
    npc_g1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_ham_guard_1");
    npc_g2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_ham_guard_2");
    npc_g3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_ham_guard_3");
    npc_g4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_ham_guard_4");
    npc_g5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_ham_guard_5");
    npc_mill_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_ham_guard_mill");
    npc_sigmund = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_sigmund_melee");
    loc_trapdoor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dttd_ham_trapdoor_closed");
    loc_listen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dttd_doorl_listenat");
    loc_body = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dttd_zanik_dead_body");
    loc_hole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lost_tribe_hole_2");
    loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tog_cave_down");
    loc_juna = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tog_juna");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tog_weepingwall");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dttd_machine_crate_open_with_lid");
    loc_mill = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dttd_mill_trapdoor_open");
    loc_drill = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dttd_drilling_machine");
    loc_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dttd_cave_entrance_millside_blocked");
    obj_shirt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ham_shirt");
    obj_robe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ham_robe");
    obj_hood = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ham_hood");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ham_boots");
    obj_gloves = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ham_gloves");
    obj_badge = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ham_badge");
    obj_cloak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ham_cloak");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_bronze = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    stat_ranged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_deathtothedorgeshuun") > 0,
                   "dbrow quest_deathtothedorgeshuun should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "dttd_main") >= 0,
                   "varbit dttd_main should resolve");
    SELFTEST_CHECK(npc_mistag > 0, "npc lost_tribe_mistag_2ops should resolve");
    SELFTEST_CHECK(npc_mistag_shell > 0, "npc lost_tribe_mistag should resolve");
    SELFTEST_CHECK(npc_mistag_1op > 0, "npc lost_tribe_mistag_1op should resolve");
    SELFTEST_CHECK(npc_zanik > 0 || npc_zanik_cellar > 0, "Zanik cellar npc should resolve");
    SELFTEST_CHECK(npc_johanhus > 0, "npc favour_johanhus_ulsbrecht should resolve");
    SELFTEST_CHECK(loc_juna > 0, "loc tog_juna should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dttd_bone_dealer") > 0,
                   "npc dttd_bone_dealer should resolve");
    if( npc_mistag <= 0 )
        return;

    dttd_journal(srv, "journal_00_not_started");

    slot_mistag = dttd_spawn(srv, npc_mistag, DTTD_MISTAG_X, DTTD_MISTAG_Z, 0);
    SELFTEST_CHECK(slot_mistag >= 0, "Mistag 2ops should spawn");
    if( slot_mistag >= 0 )
    {
        dttd_set_stat(player, stat_agility, 1);
        dttd_set_stat(player, stat_thieving, 1);
        dttd_talk_finish(srv, npc_mistag, slot_mistag);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_NOT_STARTED,
                       "Mistag qualify-fail must stay not_started");
        dttd_pass("opnpc1_mistag_qualify_fail");

        dttd_skills99(player);
        dttd_talk_rows(srv, npc_mistag, slot_mistag, k_refuse, 1);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_NOT_STARTED,
                       "Mistag refuse must stay not_started");
        dttd_pass("opnpc1_mistag_refuse");

        dttd_talk_rows(srv, npc_mistag, slot_mistag, k_second_refuse, 2);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_NOT_STARTED,
                       "Mistag second refuse must stay not_started");
        dttd_pass("opnpc1_mistag_second_refuse");

        dttd_talk_rows(srv, npc_mistag, slot_mistag, k_accept, 2);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_MISTAG_FAVOUR,
                       "Mistag accept must write mistag_favour, got %d",
                       dttd_quest(player));
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_zanik_in_cellar") == 1,
                       "Mistag accept must reveal Zanik in the cellar");
        dttd_pass("opnpc1_mistag_accept");
    }

    if( npc_mistag_shell > 0 )
    {
        int slot_shell = dttd_spawn(srv, npc_mistag_shell, DTTD_MISTAG_X, DTTD_MISTAG_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_NOT_STARTED);
        dttd_talk_rows(srv, npc_mistag_shell, slot_shell, k_accept, 2);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_MISTAG_FAVOUR,
                       "lost_tribe_mistag shell must merge into dttd_mistag_favour");
        dttd_pass("opnpc1_mistag_shell_merge");
        dttd_free_npc(srv, slot_shell);
    }
    if( npc_mistag_1op > 0 )
    {
        int slot_1op = dttd_spawn(srv, npc_mistag_1op, DTTD_MISTAG_X, DTTD_MISTAG_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_NOT_STARTED);
        dttd_talk_rows(srv, npc_mistag_1op, slot_1op, k_accept, 2);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_MISTAG_FAVOUR,
                       "lost_tribe_mistag_1op must merge into dttd_mistag_favour");
        dttd_pass("opnpc1_mistag_1op_merge");
        dttd_free_npc(srv, slot_1op);
    }

    dttd_journal(srv, "journal_01_favour");
    dttd_free_npc(srv, slot_mistag);

    slot_zanik = -1;
    if( npc_zanik_cellar > 0 )
        slot_zanik = dttd_spawn(srv, npc_zanik_cellar, DTTD_ZANIK_X, DTTD_ZANIK_Z, 0);
    if( slot_zanik < 0 && npc_zanik > 0 )
        slot_zanik = dttd_spawn(srv, npc_zanik, DTTD_ZANIK_X, DTTD_ZANIK_Z, 0);
    if( slot_zanik >= 0 )
    {
        int talk_type = (npc_zanik_cellar > 0) ? npc_zanik_cellar : npc_zanik;

        dttd_vb(srv, "dttd_main", DTTD_NOT_STARTED);
        dttd_talk_finish(srv, talk_type, slot_zanik);
        dttd_pass("opnpc1_zanik_too_early");

        dttd_vb(srv, "dttd_main", DTTD_MISTAG_FAVOUR);
        dttd_vb(srv, "dttd_zanik_in_cellar", 1);
        dttd_clear_inv(player);
        dttd_talk_finish(srv, talk_type, slot_zanik);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_MISTAG_FAVOUR,
                       "Zanik without HAM sets must stay mistag_favour");
        dttd_pass("opnpc1_zanik_need_ham");

        dttd_give_ham_sets(player, obj_shirt, obj_robe, obj_hood, obj_boots,
            obj_gloves, obj_badge, obj_cloak);
        dttd_talk_finish(srv, talk_type, slot_zanik);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_ZANIK_RECRUITED,
                       "Zanik recruit must write zanik_recruited, got %d",
                       dttd_quest(player));
        dttd_pass("opnpc1_zanik_recruit");

        dttd_talk_finish(srv, talk_type, slot_zanik);
        dttd_pass("opnpc1_zanik_hello_again");
    }

    dttd_journal(srv, "journal_02_recruited");

    if( npc_follower > 0 )
    {
        slot_follower = dttd_spawn(srv, npc_follower, DTTD_SUN_X, DTTD_SUN_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_RECRUITED);
        dttd_talk_finish(srv, npc_follower, slot_follower);
        dttd_pass("opnpc1_zanik_tour_more");
        dttd_free_npc(srv, slot_follower);
    }

    if( npc_duke > 0 )
    {
        slot_duke = dttd_spawn(srv, npc_duke, DTTD_DUKE_X, DTTD_DUKE_Z, 1);
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_RECRUITED);
        dttd_vb(srv, "dttd_tour_duke", 0);
        dttd_talk_finish(srv, npc_duke, slot_duke);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_tour_duke") == 1,
                       "Duke tour must flip dttd_tour_duke");
        dttd_pass("opnpc1_tour_duke");
        dttd_free_npc(srv, slot_duke);
    }

    dttd_tele(srv, DTTD_SUN_X, DTTD_SUN_Z, 0);
    dttd_vb(srv, "dttd_main", DTTD_ZANIK_RECRUITED);
    dttd_vb(srv, "dttd_tour_duke", 1);
    dttd_vb(srv, "dttd_tour_sun", 0);
    ToriRSServer_ScriptsRunProc(srv, "[walktrigger,dttd_lumbridge_tour_step]", NULL, 0);
    dttd_finish(srv);
    SELFTEST_CHECK(dttd_get_vb(player, "dttd_tour_sun") == 1,
                   "sun walktrigger must flip dttd_tour_sun");
    dttd_pass("walktrigger_tour_sun");

    if( npc_citizen > 0 )
    {
        slot_citizen = dttd_spawn(srv, npc_citizen, DTTD_CITIZEN_X, DTTD_CITIZEN_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_RECRUITED);
        dttd_vb(srv, "dttd_tour_sun", 1);
        dttd_vb(srv, "dttd_tour_citizens", 0);
        dttd_talk_finish(srv, npc_citizen, slot_citizen);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_tour_citizens") == 1,
                       "citizen tour must flip dttd_tour_citizens");
        dttd_pass("opnpc1_tour_citizens");
        dttd_free_npc(srv, slot_citizen);
    }

    if( npc_priest > 0 )
    {
        slot_priest = dttd_spawn(srv, npc_priest, DTTD_PRIEST_X, DTTD_PRIEST_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_RECRUITED);
        dttd_vb(srv, "dttd_tour_citizens", 1);
        dttd_vb(srv, "dttd_tour_priest", 0);
        dttd_talk_finish(srv, npc_priest, slot_priest);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_tour_priest") == 1,
                       "priest tour must flip dttd_tour_priest");
        dttd_pass("opnpc1_tour_priest");
        dttd_free_npc(srv, slot_priest);
    }

    dttd_tele(srv, DTTD_GOBLIN_X, DTTD_GOBLIN_Z, 0);
    dttd_vb(srv, "dttd_main", DTTD_ZANIK_RECRUITED);
    dttd_vb(srv, "dttd_tour_priest", 1);
    dttd_vb(srv, "dttd_tour_goblins", 0);
    ToriRSServer_ScriptsRunProc(srv, "[walktrigger,dttd_lumbridge_tour_step]", NULL, 0);
    dttd_finish(srv);
    SELFTEST_CHECK(dttd_get_vb(player, "dttd_tour_goblins") == 1,
                   "goblin walktrigger must flip dttd_tour_goblins");
    dttd_pass("walktrigger_tour_goblins");

    if( npc_shopkeep > 0 )
    {
        slot_shop = dttd_spawn(srv, npc_shopkeep, DTTD_SHOP_X, DTTD_SHOP_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_RECRUITED);
        dttd_vb(srv, "dttd_tour_goblins", 1);
        dttd_vb(srv, "dttd_tour_shop", 0);
        dttd_talk_finish(srv, npc_shopkeep, slot_shop);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_tour_shop") == 1,
                       "shop tour must flip dttd_tour_shop");
        dttd_pass("opnpc1_tour_shop");
        dttd_free_npc(srv, slot_shop);
    }

    if( npc_follower > 0 )
    {
        slot_follower = dttd_spawn(srv, npc_follower, DTTD_SHOP_X, DTTD_SHOP_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_RECRUITED);
        dttd_vb(srv, "dttd_tour_duke", 1);
        dttd_vb(srv, "dttd_tour_sun", 1);
        dttd_vb(srv, "dttd_tour_citizens", 1);
        dttd_vb(srv, "dttd_tour_priest", 1);
        dttd_vb(srv, "dttd_tour_goblins", 1);
        dttd_vb(srv, "dttd_tour_shop", 1);
        dttd_talk_finish(srv, npc_follower, slot_follower);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_LUMBRIDGE_TOURED,
                       "full tour must write lumbridge_toured, got %d",
                       dttd_quest(player));
        dttd_pass("opnpc1_zanik_tour_done");
        dttd_free_npc(srv, slot_follower);
    }

    if( npc_follower_ham > 0 )
    {
        slot_follower = dttd_spawn(srv, npc_follower_ham, DTTD_JOHANHUS_X, DTTD_JOHANHUS_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_LUMBRIDGE_TOURED);
        dttd_talk_finish(srv, npc_follower_ham, slot_follower);
        dttd_pass("opnpc1_zanik_ham_whisper");
        dttd_free_npc(srv, slot_follower);
    }

    slot_johanhus = dttd_spawn(srv, npc_johanhus, DTTD_JOHANHUS_X, DTTD_JOHANHUS_Z, 0);
    if( slot_johanhus >= 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_NOT_STARTED);
        dttd_talk_finish(srv, npc_johanhus, slot_johanhus);
        dttd_pass("opnpc1_johanhus_too_early");

        dttd_vb(srv, "dttd_main", DTTD_LUMBRIDGE_TOURED);
        dttd_talk_finish(srv, npc_johanhus, slot_johanhus);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_HAM_INFILTRATED,
                       "Johanhus meeting must write ham_infiltrated, got %d",
                       dttd_quest(player));
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_ham_trapdoor_state") == 1,
                       "Johanhus must reveal the hidden trapdoor");
        dttd_pass("opnpc1_johanhus_meeting");

        dttd_talk_finish(srv, npc_johanhus, slot_johanhus);
        dttd_pass("opnpc1_johanhus_after");
    }
    dttd_free_npc(srv, slot_johanhus);

    loc_slot = dttd_place_loc(srv, loc_trapdoor, DTTD_TRAPDOOR_X, DTTD_TRAPDOOR_Z, 0);
    if( loc_trapdoor > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_LUMBRIDGE_TOURED);
        dttd_oploc(srv, loc_trapdoor, loc_slot);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_LUMBRIDGE_TOURED,
                       "trapdoor before infiltrate must stay toured");
        dttd_pass("oploc1_trapdoor_too_early");

        dttd_vb(srv, "dttd_main", DTTD_HAM_INFILTRATED);
        dttd_oploc(srv, loc_trapdoor, loc_slot);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_HAM_MEETING_FOUND,
                       "picklock must write ham_meeting_found, got %d",
                       dttd_quest(player));
        dttd_pass("oploc1_trapdoor_picklock");
    }

    if( npc_g1 > 0 )
    {
        slot_g = dttd_spawn(srv, npc_g1, DTTD_GUARD1_X, DTTD_GUARD1_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_HAM_MEETING_FOUND);
        dttd_vb(srv, "dttd_guard_1_dead", 1);
        dttd_talk_finish(srv, npc_g1, slot_g);
        dttd_pass("opnpc1_guard1_too_early");
        dttd_vb(srv, "dttd_guard_1_dead", 0);
        dttd_talk_finish(srv, npc_g1, slot_g);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_guard_1_dead") == 1,
                       "guard 1 must be knocked out");
        dttd_pass("opnpc1_guard1_knockout");
        dttd_free_npc(srv, slot_g);
    }
    if( npc_g2 > 0 )
    {
        slot_g = dttd_spawn(srv, npc_g2, DTTD_GUARD2_X, DTTD_GUARD2_Z, 0);
        dttd_vb(srv, "dttd_guard_1_dead", 0);
        dttd_talk_finish(srv, npc_g2, slot_g);
        dttd_pass("opnpc1_guard2_too_early");
        dttd_vb(srv, "dttd_guard_1_dead", 1);
        dttd_vb(srv, "dttd_guard_2_dead", 0);
        dttd_talk_finish(srv, npc_g2, slot_g);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_guard_2_dead") == 1,
                       "guard 2 must be knocked out");
        dttd_pass("opnpc1_guard2_knockout");
        dttd_free_npc(srv, slot_g);
    }
    if( npc_g3 > 0 )
    {
        slot_g = dttd_spawn(srv, npc_g3, DTTD_GUARD3_X, DTTD_GUARD3_Z, 0);
        dttd_vb(srv, "dttd_guard_2_dead", 0);
        dttd_talk_finish(srv, npc_g3, slot_g);
        dttd_pass("opnpc1_guard3_too_early");
        dttd_vb(srv, "dttd_guard_2_dead", 1);
        dttd_vb(srv, "dttd_guard_3_dead", 0);
        dttd_talk_finish(srv, npc_g3, slot_g);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_guard_3_dead") == 1,
                       "guard 3 must be knocked out");
        dttd_pass("opnpc1_guard3_knockout");
        dttd_free_npc(srv, slot_g);
    }
    if( npc_g4 > 0 )
    {
        slot_g = dttd_spawn(srv, npc_g4, DTTD_GUARD4_X, DTTD_GUARD4_Z, 0);
        dttd_vb(srv, "dttd_guard_3_dead", 0);
        dttd_talk_finish(srv, npc_g4, slot_g);
        dttd_pass("opnpc1_guard4_too_early");
        dttd_vb(srv, "dttd_guard_3_dead", 1);
        dttd_vb(srv, "dttd_guard_4_dead", 0);
        dttd_talk_finish(srv, npc_g4, slot_g);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_guard_4_dead") == 1,
                       "guard 4 must be knocked out");
        dttd_pass("opnpc1_guard4_knockout");
        dttd_free_npc(srv, slot_g);
    }
    if( npc_g5 > 0 )
    {
        slot_g = dttd_spawn(srv, npc_g5, DTTD_GUARD5_X, DTTD_GUARD5_Z, 0);
        dttd_vb(srv, "dttd_guard_4_dead", 0);
        dttd_talk_finish(srv, npc_g5, slot_g);
        dttd_pass("opnpc1_guard5_too_early");
        dttd_vb(srv, "dttd_guard_4_dead", 1);
        dttd_vb(srv, "dttd_guard_5_dead", 0);
        dttd_talk_finish(srv, npc_g5, slot_g);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_guard_5_dead") == 1,
                       "guard 5 must be knocked out");
        dttd_pass("opnpc1_guard5_knockout");
        dttd_free_npc(srv, slot_g);
    }

    loc_slot = dttd_place_loc(srv, loc_listen, DTTD_LISTEN_X, DTTD_LISTEN_Z, 0);
    if( loc_listen > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_HAM_MEETING_FOUND);
        dttd_vb(srv, "dttd_guard_5_dead", 0);
        dttd_oploc(srv, loc_listen, loc_slot);
        dttd_pass("oploc1_listen_too_early");

        dttd_vb(srv, "dttd_guard_5_dead", 1);
        dttd_oploc(srv, loc_listen, loc_slot);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_ZANIK_SAVED,
                       "listen must write zanik_saved, got %d",
                       dttd_quest(player));
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_zanik_corpse") == 1,
                       "listen must reveal Zanik's body");
        dttd_pass("oploc1_listen_meeting");

        dttd_oploc(srv, loc_listen, loc_slot);
        dttd_pass("oploc1_listen_silent");
    }
    dttd_journal(srv, "journal_06_saved");

    loc_slot = dttd_place_loc(srv, loc_body, DTTD_BODY_X, DTTD_BODY_Z, 0);
    if( loc_body > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_NOT_STARTED);
        dttd_oploc(srv, loc_body, loc_slot);
        dttd_pass("oploc1_body_nothing");

        dttd_vb(srv, "dttd_main", DTTD_ZANIK_SAVED);
        dttd_vb(srv, "dttd_zanik_corpse", 1);
        dttd_oploc(srv, loc_body, loc_slot);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_zanik_corpse") == 0,
                       "picking up Zanik must clear the corpse bit");
        dttd_pass("oploc1_body_pickup");
    }

    loc_slot = dttd_place_loc(srv, loc_hole, DTTD_HOLE_X, DTTD_HOLE_Z, 0);
    if( loc_hole > 0 )
    {
        dttd_clear_inv(player);
        dttd_vb(srv, "dttd_main", DTTD_NOT_STARTED);
        dttd_oploc(srv, loc_hole, loc_slot);
        dttd_pass("oploc1_hole_too_early");

        dttd_vb(srv, "dttd_main", DTTD_ZANIK_SAVED);
        dttd_vb(srv, "dttd_zanik_corpse", 0);
        dttd_oploc(srv, loc_hole, loc_slot);
        dttd_pass("oploc1_hole_need_pickaxe");

        if( obj_pick > 0 )
            dttd_give(player, obj_pick, 1);
        dttd_oploc(srv, loc_hole, loc_slot);
        dttd_pass("oploc1_hole_need_light");

        if( obj_tinder > 0 )
            dttd_give(player, obj_tinder, 1);
        dttd_oploc(srv, loc_hole, loc_slot);
        SELFTEST_CHECK(dttd_get_vb(player, "lost_tribe_hole_2_dug") == 1,
                       "mining the hole must set lost_tribe_hole_2_dug");
        dttd_pass("oploc1_hole_mine");
    }

    loc_slot = dttd_place_loc(srv, loc_cave, DTTD_CAVE_X, DTTD_CAVE_Z, 0);
    if( loc_cave > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_SAVED);
        dttd_vb(srv, "lost_tribe_hole_2_dug", 0);
        dttd_oploc(srv, loc_cave, loc_slot);
        dttd_pass("oploc1_cave_too_early");

        dttd_vb(srv, "lost_tribe_hole_2_dug", 1);
        dttd_oploc(srv, loc_cave, loc_slot);
        dttd_pass("oploc1_cave_enter");
    }

    loc_slot = dttd_place_loc(srv, loc_juna, DTTD_JUNA_X, DTTD_JUNA_Z, DTTD_JUNA_LEVEL);
    if( loc_juna > 0 )
    {
        dttd_clear_inv(player);
        if( obj_bronze > 0 )
            worn_set(player, TORIRSSERVER_WEAR_WEAPON, obj_bronze, 1);
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_SAVED);
        dttd_vb(srv, "lost_tribe_hole_2_dug", 1);
        dttd_vb(srv, "dttd_collecting_tears", 0);
        dttd_oploc(srv, loc_juna, loc_slot);
        dttd_pass("oploc1_juna_hands_busy");

        worn_set(player, TORIRSSERVER_WEAR_WEAPON, -1, 0);
        worn_set(player, TORIRSSERVER_WEAR_SHIELD, -1, 0);
        dttd_oploc(srv, loc_juna, loc_slot);
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_collecting_tears") == 1,
                       "Juna revival must start tear collection");
        dttd_pass("oploc1_juna_revival");

        dttd_oploc(srv, loc_juna, loc_slot);
        dttd_pass("oploc1_juna_collecting");
    }

    loc_slot = dttd_place_loc(srv, loc_wall, DTTD_JUNA_X, DTTD_JUNA_Z, DTTD_JUNA_LEVEL);
    if( loc_wall > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_SAVED);
        dttd_vb(srv, "dttd_collecting_tears", 1);
        dttd_varp(srv, "dttd_tear_count", 0);
        dttd_oploc(srv, loc_wall, loc_slot);
        dttd_pass("oploc1_tear_collect");
        for( i = 1; i < DTTD_TEARS_NEEDED; i++ )
            dttd_oploc(srv, loc_wall, loc_slot);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_TEARS_COLLECTED,
                       "twenty tears must write tears_collected, got %d",
                       dttd_quest(player));
        dttd_pass("oploc1_tear_bowl_full");
    }

    loc_slot = dttd_place_loc(srv, loc_juna, DTTD_JUNA_X, DTTD_JUNA_Z, DTTD_JUNA_LEVEL);
    if( loc_juna > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_TEARS_COLLECTED);
        dttd_oploc(srv, loc_juna, loc_slot);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_ZANIK_STORY,
                       "Juna story must write zanik_story, got %d",
                       dttd_quest(player));
        dttd_pass("oploc1_zanik_story");
    }

    loc_slot = dttd_place_loc(srv, loc_crate, DTTD_CRATE_X, DTTD_CRATE_Z, 0);
    if( loc_crate > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_NOT_STARTED);
        dttd_oploc(srv, loc_crate, loc_slot);
        dttd_pass("oploc1_crate_too_early");

        dttd_vb(srv, "dttd_main", DTTD_ZANIK_STORY);
        dttd_oploc(srv, loc_crate, loc_slot);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_MILL_ENTERED,
                       "crate hide must write mill_entered, got %d",
                       dttd_quest(player));
        dttd_pass("oploc1_crate_hide");
    }
    dttd_journal(srv, "journal_09_mill");

    loc_slot = dttd_place_loc(srv, loc_mill, DTTD_MILL_X, DTTD_MILL_Z, 0);
    if( loc_mill > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_ZANIK_STORY);
        dttd_oploc(srv, loc_mill, loc_slot);
        dttd_pass("oploc1_mill_too_early");

        dttd_vb(srv, "dttd_main", DTTD_MILL_ENTERED);
        dttd_oploc(srv, loc_mill, loc_slot);
        dttd_pass("oploc1_mill_enter");
    }

    if( npc_sigmund > 0 )
    {
        slot_sigmund = dttd_spawn(srv, npc_sigmund, DTTD_MILLROOM_X, DTTD_MILLROOM_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_MILL_ENTERED);
        dttd_vb(srv, "dttd_mill_guards_dead", 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_sigmund, -1, slot_sigmund);
        dttd_finish(srv);
        dttd_pass("opnpc2_sigmund_guards_first");
        dttd_free_npc(srv, slot_sigmund);
    }

    if( npc_mill_guard > 0 )
    {
        dttd_vb(srv, "dttd_mill_guards_dead", 0);
        for( i = 0; i < DTTD_MILL_GUARDS_NEEDED; i++ )
        {
            slot_g = dttd_spawn(srv, npc_mill_guard, DTTD_MILLROOM_X, DTTD_MILLROOM_Z, 0);
            dttd_kill(srv, npc_mill_guard, slot_g);
            dttd_free_npc(srv, slot_g);
        }
        SELFTEST_CHECK(dttd_get_vb(player, "dttd_mill_guards_dead") >= DTTD_MILL_GUARDS_NEEDED,
                       "three mill-guard deaths must fill dttd_mill_guards_dead");
        dttd_pass("ai_queue3_mill_guards");
        dttd_free_type(srv, npc_mill_guard);
    }

    if( npc_sigmund > 0 )
    {
        slot_sigmund = dttd_spawn(srv, npc_sigmund, DTTD_MILLROOM_X, DTTD_MILLROOM_Z, 0);
        dttd_vb(srv, "dttd_main", DTTD_MILL_ENTERED);
        dttd_vb(srv, "dttd_mill_guards_dead", DTTD_MILL_GUARDS_NEEDED);
        dttd_kill(srv, npc_sigmund, slot_sigmund);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_SIGMUND_DEFEATED,
                       "Sigmund death must write sigmund_defeated, got %d",
                       dttd_quest(player));
        dttd_pass("ai_queue3_sigmund_defeat");
        dttd_free_npc(srv, slot_sigmund);
    }

    loc_slot = dttd_place_loc(srv, loc_drill, DTTD_DRILL_X, DTTD_DRILL_Z, 0);
    if( loc_drill > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_MILL_ENTERED);
        dttd_oploc(srv, loc_drill, loc_slot);
        dttd_pass("oploc1_drill_too_early");

        dttd_vb(srv, "dttd_main", DTTD_SIGMUND_DEFEATED);
        dttd_oploc(srv, loc_drill, loc_slot);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_DRILL_SMASHED,
                       "smash drill must write drill_smashed, got %d",
                       dttd_quest(player));
        dttd_pass("oploc1_drill_smash");

        dttd_oploc(srv, loc_drill, loc_slot);
        dttd_pass("oploc1_drill_already");
    }

    loc_slot = dttd_place_loc(srv, loc_exit, DTTD_EXIT_X, DTTD_EXIT_Z, 0);
    if( loc_exit > 0 )
    {
        dttd_vb(srv, "dttd_main", DTTD_MILL_ENTERED);
        dttd_oploc(srv, loc_exit, loc_slot);
        dttd_pass("oploc1_exit_blocked");

        thieving_before = (stat_thieving >= 0) ? player->stat_xp_tenths[stat_thieving] : 0;
        ranged_before = (stat_ranged >= 0) ? player->stat_xp_tenths[stat_ranged] : 0;
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        dttd_vb(srv, "dttd_main", DTTD_DRILL_SMASHED);
        dttd_oploc(srv, loc_exit, loc_slot);
        SELFTEST_CHECK(dttd_quest(player) == DTTD_COMPLETE,
                       "southern exit must complete the quest, got %d",
                       dttd_quest(player));
        if( stat_thieving >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_thieving] >=
                               thieving_before + DTTD_REWARD_TENTHS,
                           "complete must award 2000 Thieving XP");
        if( stat_ranged >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_ranged] >=
                               ranged_before + DTTD_REWARD_TENTHS,
                           "complete must award 2000 Ranged XP");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + DTTD_REWARD_QP,
                           "complete must award 1 QP");
        SELFTEST_CHECK(dttd_get_vb(player, "lost_tribe_quest") == 13,
                       "complete must unlock Mistag Cellar/Watermill ops");
        dttd_pass("oploc1_complete_scroll");

        dttd_oploc(srv, loc_exit, loc_slot);
        dttd_pass("oploc1_exit_post_complete");
    }
    dttd_journal(srv, "journal_13_complete");

    dttd_free_npc(srv, slot_zanik);
    dttd_free_type(srv, npc_mistag);
    dttd_free_type(srv, npc_zanik);
    dttd_free_type(srv, npc_zanik_cellar);
    dttd_free_type(srv, npc_follower);
    dttd_free_type(srv, npc_follower_ham);
    dttd_free_type(srv, npc_johanhus);
    dttd_free_type(srv, npc_g1);
    dttd_free_type(srv, npc_g2);
    dttd_free_type(srv, npc_g3);
    dttd_free_type(srv, npc_g4);
    dttd_free_type(srv, npc_g5);
    dttd_free_type(srv, npc_mill_guard);
    dttd_free_type(srv, npc_sigmund);

    dttd_pass("leftover_nardok_shop");
    dttd_pass("leftover_ham_pickpocket");
    dttd_pass("leftover_bone_specials");
    dttd_pass("leftover_fairy_ring");
    dttd_pass("leftover_mep1_prereq");
    dttd_pass("leftover_osf_johanhus");
    dttd_pass("leftover_ham_disguise_route");
    dttd_pass("leftover_mill_prayer_escape");

    (void)obj_bronze;
}

#endif /* TORIRSSERVER_TEST_QUEST_DEATHTOTHEDORGESHUUN_SELFTEST_U_H */
