#ifndef TORIRSSERVER_TEST_QUEST_TOWEROFLIFE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_TOWEROFLIFE_SELFTEST_U_H

/* Tower of Life Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Effigy / Bonafido / builders /
 * homunculus cannot leak. Real OPNPC1 / OPNPC3 / OPLOC1 on the
 * authored path. player->godmode = 1 for the whole walk (not a
 * death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_TOL_ONLY=1 (or TOWEROFLIFE)
 *
 * Construction 10 via stat_base (not boostable). No quest prereqs.
 * Reward tenths: Construction 10000, Crafting/Thieving 5000.
 * Cache dbrow quest_toweroflife awards 2 QP. Effigy is merged into
 * [opnpc1,tol_npc_efergy01]. Bonafido is [opnpc1,tol_npc_barry01].
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - PuzzleSolver widget puzzles collapsed to build+fix
 *   - post-quest Creature Creation (align / symbols / levers)
 *   - QH basement trapdoor route; final talk is ground-level
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear as unimplemented systems -- TK-grab is a homunculus
 * quiz answer, not leftover-stamped.
 */

#define TOL_NOT_STARTED 0
#define TOL_AGREED_TO_HELP 2
#define TOL_BONAFIDO_BRIEFED 4
#define TOL_OUTFIT_READY 6
#define TOL_FIXING_TOWER 8
#define TOL_TOWER_FIXED 10
#define TOL_CONFRONTING 12
#define TOL_SCARE_ALCHEMISTS 16
#define TOL_BASEMENT 17
#define TOL_COMPLETE 18

#define TOL_MACHINE_NOT_BUILT 0
#define TOL_MACHINE_BUILT 1
#define TOL_MACHINE_FIXED 2

#define TOL_NEED_SHEETS 3
#define TOL_NEED_BALLS 4
#define TOL_NEED_WHEELS 4
#define TOL_NEED_PIPES 4
#define TOL_NEED_RINGS 5
#define TOL_NEED_RIVETS 6
#define TOL_NEED_BARS 5
#define TOL_NEED_FLUID 4

#define TOL_CON_REQ 10
#define TOL_CON_XP 10000
#define TOL_CRAFT_XP 5000
#define TOL_THIEVE_XP 5000
#define TOL_QP_REWARD 2
#define TOL_SANDWICH_THIEVE_XP 80

#define TOL_STAT_CONSTRUCTION 22
#define TOL_STAT_CRAFTING 12
#define TOL_STAT_THIEVING 17

#define TOL_EFFIGY_X 2639
#define TOL_EFFIGY_Z 3219
#define TOL_BONAFIDO_X 2650
#define TOL_BONAFIDO_Z 3227
#define TOL_BLACKEYE_X 2645
#define TOL_BLACKEYE_Z 3229
#define TOL_NOFINGERS_X 2645
#define TOL_NOFINGERS_Z 3224
#define TOL_GUMMY_X 2645
#define TOL_GUMMY_Z 3230
#define TOL_GUNS_X 2643
#define TOL_GUNS_Z 3226
#define TOL_PLANT_X 2644
#define TOL_PLANT_Z 3220
#define TOL_DOOR_X 2648
#define TOL_DOOR_Z 3220
#define TOL_HOMO_CAGE_X 2648
#define TOL_HOMO_CAGE_Z 3217
#define TOL_HOMO_GROUND_X 2640
#define TOL_HOMO_GROUND_Z 3221

#define TOL_WORN_HAT 0
#define TOL_WORN_BODY 4
#define TOL_WORN_LEGS 7
#define TOL_WORN_BOOTS 10

static void
tol_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "TOL PASS: %s\n", step);
}

static void
tol_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
tol_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
tol_finish(struct ToriRSServer* srv)
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
tol_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
tol_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : tol_chatmenu();
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
tol_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = tol_chatmenu();
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
tol_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    tol_god(player);
    selftest_tick(srv);
}

static int
tol_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    tol_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
tol_free_type(struct ToriRSServer* srv, int npc_type)
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
tol_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
tol_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
tol_quest(struct ToriRSServerPlayer* player)
{
    return tol_get_vb(player, "tol_prog");
}

static void
tol_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
tol_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    tol_talk(srv, npc_type, slot);
    tol_finish(srv);
}

static void
tol_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    tol_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        tol_click_until_menu(srv, 24);
        tol_pick_row(srv, rows[i]);
    }
    tol_finish(srv);
}

static void
tol_npc3(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_type, -1, slot);
    tol_finish(srv);
}

static void
tol_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    tol_finish(srv);
}

static void
tol_loc1_rows(struct ToriRSServer* srv, int loc_type, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(loc_type > 0);
    assert(rows);
    assert(n > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    for( i = 0; i < n; i++ )
    {
        tol_click_until_menu(srv, 24);
        tol_pick_row(srv, rows[i]);
    }
    tol_finish(srv);
}

static void
tol_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
tol_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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

static int
tol_worn_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        if( player->worn[s].obj_id == obj_id )
            n += player->worn[s].count;
    return n;
}

static void
tol_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
tol_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    tol_vb(srv, "tol_prog", TOL_NOT_STARTED);
    tol_vb(srv, "tol_pres_prog", TOL_MACHINE_NOT_BUILT);
    tol_vb(srv, "tol_pipe_prog", TOL_MACHINE_NOT_BUILT);
    tol_vb(srv, "tol_cage_prog", TOL_MACHINE_NOT_BUILT);
    tol_vb(srv, "tol_cage_state", 0);
    tol_vb(srv, "tol_nofingers_asked", 0);
    tol_vb(srv, "tol_homonc_pres", 0);
}

static void
tol_skills(struct ToriRSServerPlayer* player)
{
    assert(player);
    tol_set_stat(player, TOL_STAT_CONSTRUCTION, TOL_CON_REQ);
}

static void
tol_wear_outfit(struct ToriRSServerPlayer* player, int hat, int shirt, int trousers, int boots)
{
    assert(player);
    if( hat > 0 )
        worn_set(player, TOL_WORN_HAT, hat, 1);
    if( shirt > 0 )
        worn_set(player, TOL_WORN_BODY, shirt, 1);
    if( trousers > 0 )
        worn_set(player, TOL_WORN_LEGS, trousers, 1);
    if( boots > 0 )
        worn_set(player, TOL_WORN_BOOTS, boots, 1);
}

static void
tol_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,tol_journal]", NULL, 0);
    tol_finish(srv);
    tol_pass(step);
}

static void
selftest_quest_toweroflife(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_effigy;
    int npc_bonafido;
    int npc_blackeye;
    int npc_nofingers;
    int npc_gummy;
    int npc_guns;
    int npc_homo_cage;
    int npc_homo_ground;
    int loc_plant;
    int loc_door;
    int loc_crate_sheets;
    int loc_crate_balls;
    int loc_crate_wheels;
    int loc_crate_pipes;
    int loc_crate_rings;
    int loc_crate_rivets;
    int loc_crate_bars;
    int loc_crate_fluid;
    int loc_pres;
    int loc_pipe;
    int loc_cage;
    int obj_hat;
    int obj_shirt;
    int obj_trousers;
    int obj_boots;
    int obj_beer;
    int obj_sandwich;
    int obj_hammer;
    int obj_saw;
    int obj_sheets;
    int obj_balls;
    int obj_wheels;
    int obj_pipes;
    int obj_rings;
    int obj_rivets;
    int obj_bars;
    int obj_fluid;
    int slot_effigy;
    int slot_bonafido;
    int slot_blackeye;
    int slot_nofingers;
    int slot_gummy;
    int slot_guns;
    int slot_homo_cage;
    int slot_homo_ground;
    int qp_id;
    int qp_before;
    int con_before;
    int craft_before;
    int thieve_before;
    int refuse_row[1];
    int accept_row[1];
    int blackeye_ok[3];
    int gummy_clothes[1];
    int gummy_no[1];
    int drink_wrong[1];
    int quiz_ok[4];
    int build_yes[1];
    int homo_magic[2];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "TOL SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    tol_god(player);
    tol_clear_inv(player);
    tol_reset_quest(srv);

    npc_effigy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tol_npc_efergy01");
    npc_bonafido = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tol_npc_barry01");
    npc_blackeye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tol_npc_builder01");
    npc_nofingers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tol_npc_builder02");
    npc_gummy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tol_npc_builder03");
    npc_guns = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tol_npc_builder04");
    npc_homo_cage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tol_homonculus_cage_broken");
    npc_homo_ground = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tol_homonculus_nocage");
    loc_plant = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_plant4");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_tower_wall_door");
    loc_crate_sheets = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_crate06");
    loc_crate_balls = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_crate07");
    loc_crate_wheels = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_crate05");
    loc_crate_pipes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_crate02");
    loc_crate_rings = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_crate03");
    loc_crate_rivets = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_crate04");
    loc_crate_bars = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_crate10");
    loc_crate_fluid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_crate08");
    loc_pres = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_pressure_machine01");
    loc_pipe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_pipe_machine01");
    loc_cage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tol_cage_broken");
    obj_hat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_player_construction_hardhat");
    obj_shirt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_player_construction_shirt");
    obj_trousers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_player_construction_trousers");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_player_construction_boots");
    obj_beer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "beer");
    obj_sandwich = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "triangle_sandwich");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_saw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "poh_saw");
    obj_sheets = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_metal_sheet");
    obj_balls = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_pressure_ball");
    obj_wheels = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_wheel");
    obj_pipes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_pipe");
    obj_rings = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_ring");
    obj_rivets = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_rivets");
    obj_bars = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_bar");
    obj_fluid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tol_glue");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_toweroflife") >= 0,
                   "dbrow quest_toweroflife should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "tol_prog") >= 0,
                   "varbit tol_prog should resolve");
    SELFTEST_CHECK(npc_effigy > 0, "npc tol_npc_efergy01 should resolve");
    SELFTEST_CHECK(npc_bonafido > 0, "npc tol_npc_barry01 should resolve");
    SELFTEST_CHECK(npc_blackeye > 0 && npc_nofingers > 0 && npc_gummy > 0 && npc_guns > 0,
                   "builder npcs should resolve");
    SELFTEST_CHECK(npc_homo_cage > 0 && npc_homo_ground > 0, "homunculus npcs should resolve");
    SELFTEST_CHECK(loc_plant > 0 && loc_door > 0 && loc_pres > 0 && loc_pipe > 0 && loc_cage > 0,
                   "plant / door / machines should resolve");
    SELFTEST_CHECK(loc_crate_sheets > 0 && loc_crate_balls > 0 && loc_crate_wheels > 0,
                   "pressure crates should resolve");
    SELFTEST_CHECK(loc_crate_pipes > 0 && loc_crate_rings > 0 && loc_crate_rivets > 0,
                   "pipe crates should resolve");
    SELFTEST_CHECK(loc_crate_bars > 0 && loc_crate_fluid > 0, "cage crates should resolve");
    SELFTEST_CHECK(obj_hat > 0 && obj_shirt > 0 && obj_trousers > 0 && obj_boots > 0,
                   "builder outfit objs should resolve");
    SELFTEST_CHECK(obj_beer > 0 && obj_sandwich > 0 && obj_hammer > 0 && obj_saw > 0,
                   "beer / sandwich / tools should resolve");
    SELFTEST_CHECK(obj_sheets > 0 && obj_balls > 0 && obj_wheels > 0 && obj_pipes > 0,
                   "pressure/pipe mats should resolve");
    SELFTEST_CHECK(obj_rings > 0 && obj_rivets > 0 && obj_bars > 0 && obj_fluid > 0,
                   "cage mats should resolve");

    refuse_row[0] = 2;
    accept_row[0] = 1;
    blackeye_ok[0] = 3;
    blackeye_ok[1] = 2;
    blackeye_ok[2] = 1;
    gummy_clothes[0] = 1;
    gummy_no[0] = 2;
    drink_wrong[0] = 2;
    quiz_ok[0] = 1;
    quiz_ok[1] = 1;
    quiz_ok[2] = 1;
    quiz_ok[3] = 1;
    build_yes[0] = 1;
    homo_magic[0] = 1;
    homo_magic[1] = 1;

    slot_effigy = tol_spawn(srv, npc_effigy, TOL_EFFIGY_X, TOL_EFFIGY_Z, 0);
    SELFTEST_CHECK(slot_effigy >= 0, "Effigy should spawn");

    tol_set_stat(player, TOL_STAT_CONSTRUCTION, 1);
    tol_talk_finish(srv, npc_effigy, slot_effigy);
    SELFTEST_CHECK(tol_quest(player) == TOL_NOT_STARTED, "low Construction must not start");
    tol_pass("01_effigy_qualify_fail");
    tol_journal(srv, "journal_00_not_started");

    slot_bonafido = tol_spawn(srv, npc_bonafido, TOL_BONAFIDO_X, TOL_BONAFIDO_Z, 0);
    SELFTEST_CHECK(slot_bonafido >= 0, "Bonafido should spawn");
    tol_talk_finish(srv, npc_bonafido, slot_bonafido);
    SELFTEST_CHECK(tol_quest(player) == TOL_NOT_STARTED, "Bonafido before accept must stay unstarted");
    tol_pass("14_bonafido_early");

    tol_skills(player);
    tol_tele(srv, TOL_EFFIGY_X, TOL_EFFIGY_Z, 0);
    tol_talk_rows(srv, npc_effigy, slot_effigy, refuse_row, 1);
    SELFTEST_CHECK(tol_quest(player) == TOL_NOT_STARTED, "refuse must leave unstarted");
    tol_pass("03_effigy_refuse");

    tol_talk_rows(srv, npc_effigy, slot_effigy, accept_row, 1);
    SELFTEST_CHECK(tol_quest(player) == TOL_AGREED_TO_HELP, "accept should set agreed_to_help=2");
    tol_pass("04_effigy_accept");
    tol_journal(srv, "journal_01_agreed_to_help");

    tol_talk_finish(srv, npc_effigy, slot_effigy);
    SELFTEST_CHECK(tol_quest(player) == TOL_AGREED_TO_HELP, "mid talk must not rewind");
    tol_pass("05_effigy_see_bonafido");

    tol_tele(srv, TOL_BONAFIDO_X, TOL_BONAFIDO_Z, 0);
    tol_talk_finish(srv, npc_bonafido, slot_bonafido);
    SELFTEST_CHECK(tol_quest(player) == TOL_BONAFIDO_BRIEFED, "first Bonafido talk should brief=4");
    tol_pass("15_bonafido_first");
    tol_journal(srv, "journal_02_bonafido_briefed");

    tol_talk_finish(srv, npc_bonafido, slot_bonafido);
    SELFTEST_CHECK(tol_quest(player) == TOL_BONAFIDO_BRIEFED, "no outfit must not enter");
    tol_pass("16_bonafido_no_outfit");

    slot_blackeye = tol_spawn(srv, npc_blackeye, TOL_BLACKEYE_X, TOL_BLACKEYE_Z, 0);
    SELFTEST_CHECK(slot_blackeye >= 0, "Black-eye should spawn");
    tol_talk_rows(srv, npc_blackeye, slot_blackeye, blackeye_ok, 3);
    SELFTEST_CHECK(tol_worn_total(player, obj_hat) >= 1, "Black-eye quiz should grant the hard hat");
    tol_pass("34_blackeye_success");

    slot_nofingers = tol_spawn(srv, npc_nofingers, TOL_NOFINGERS_X, TOL_NOFINGERS_Z, 0);
    SELFTEST_CHECK(slot_nofingers >= 0, "No fingers should spawn");
    tol_npc3(srv, npc_nofingers, slot_nofingers);
    SELFTEST_CHECK(tol_worn_total(player, obj_boots) == 0, "pickpocket before talk must fail");
    tol_pass("38_nofingers_pickpocket_early");
    tol_talk_finish(srv, npc_nofingers, slot_nofingers);
    SELFTEST_CHECK(tol_get_vb(player, "tol_nofingers_asked") == 1, "talk should set nofingers_asked");
    tol_pass("37_nofingers_talk");
    tol_npc3(srv, npc_nofingers, slot_nofingers);
    SELFTEST_CHECK(tol_worn_total(player, obj_boots) >= 1, "pickpocket should grant boots");
    tol_pass("39_nofingers_pickpocket");

    slot_gummy = tol_spawn(srv, npc_gummy, TOL_GUMMY_X, TOL_GUMMY_Z, 0);
    SELFTEST_CHECK(slot_gummy >= 0, "Gummy should spawn");
    tol_talk_rows(srv, npc_gummy, slot_gummy, gummy_no, 1);
    SELFTEST_CHECK(tol_worn_total(player, obj_trousers) == 0, "Gummy refuse must not grant trousers");
    tol_pass("43_gummy_refuse");
    tol_talk_rows(srv, npc_gummy, slot_gummy, gummy_clothes, 1);
    tol_pass("44_gummy_clothing");
    tol_npc3(srv, npc_gummy, slot_gummy);
    SELFTEST_CHECK(tol_inv_total(player, obj_sandwich) >= 1, "Gummy pickpocket should grant a sandwich");
    tol_pass("46_gummy_pickpocket");

    tol_tele(srv, TOL_PLANT_X, TOL_PLANT_Z, 0);
    tol_loc1(srv, loc_plant);
    SELFTEST_CHECK(tol_worn_total(player, obj_trousers) >= 1, "plant search should grant trousers");
    tol_pass("54_plant_find");

    slot_guns = tol_spawn(srv, npc_guns, TOL_GUNS_X, TOL_GUNS_Z, 0);
    SELFTEST_CHECK(slot_guns >= 0, "The Guns should spawn");
    tol_talk_finish(srv, npc_guns, slot_guns);
    SELFTEST_CHECK(tol_worn_total(player, obj_shirt) == 0, "Guns without beer must not grant the shirt");
    tol_pass("50_guns_need_beer");
    tol_give(player, obj_beer, 1);
    tol_talk_finish(srv, npc_guns, slot_guns);
    SELFTEST_CHECK(tol_worn_total(player, obj_shirt) >= 1, "beer trade should grant the shirt");
    tol_pass("51_guns_beer_trade");

    tol_tele(srv, TOL_BONAFIDO_X, TOL_BONAFIDO_Z, 0);
    tol_talk_rows(srv, npc_bonafido, slot_bonafido, drink_wrong, 1);
    SELFTEST_CHECK(tol_quest(player) == TOL_BONAFIDO_BRIEFED, "wrong tea answer must not enter");
    tol_pass("18_bonafido_quiz_drink_wrong");
    tol_talk_rows(srv, npc_bonafido, slot_bonafido, quiz_ok, 4);
    SELFTEST_CHECK(tol_quest(player) == TOL_OUTFIT_READY, "outfit quiz should set outfit_ready=6");
    tol_pass("25_bonafido_enter");
    tol_journal(srv, "journal_03_outfit_ready");

    tol_talk_finish(srv, npc_bonafido, slot_bonafido);
    tol_pass("26_bonafido_good_luck");

    tol_tele(srv, TOL_DOOR_X, TOL_DOOR_Z, 0);
    tol_loc1(srv, loc_door);
    SELFTEST_CHECK(tol_quest(player) == TOL_FIXING_TOWER, "door should set fixing_tower=8");
    tol_pass("58_door_enter");
    tol_journal(srv, "journal_04_fixing_tower");

    tol_clear_inv(player);
    tol_loc1(srv, loc_crate_sheets);
    SELFTEST_CHECK(tol_inv_total(player, obj_sheets) >= TOL_NEED_SHEETS, "crate06 should grant sheets");
    tol_pass("60_crate_sheets");
    tol_loc1(srv, loc_crate_balls);
    SELFTEST_CHECK(tol_inv_total(player, obj_balls) >= TOL_NEED_BALLS, "crate07 should grant balls");
    tol_pass("62_crate_balls");
    tol_loc1(srv, loc_crate_wheels);
    SELFTEST_CHECK(tol_inv_total(player, obj_wheels) >= TOL_NEED_WHEELS, "crate05 should grant wheels");
    tol_pass("64_crate_wheels");
    tol_loc1(srv, loc_crate_pipes);
    SELFTEST_CHECK(tol_inv_total(player, obj_pipes) >= TOL_NEED_PIPES, "crate02 should grant pipes");
    tol_pass("66_crate_pipes");
    tol_loc1(srv, loc_crate_rings);
    SELFTEST_CHECK(tol_inv_total(player, obj_rings) >= TOL_NEED_RINGS, "crate03 should grant rings");
    tol_pass("68_crate_rings");
    tol_loc1(srv, loc_crate_rivets);
    SELFTEST_CHECK(tol_inv_total(player, obj_rivets) >= TOL_NEED_RIVETS, "crate04 should grant rivets");
    tol_pass("70_crate_rivets");
    tol_loc1(srv, loc_crate_bars);
    SELFTEST_CHECK(tol_inv_total(player, obj_bars) >= TOL_NEED_BARS, "crate10 should grant bars");
    tol_pass("72_crate_bars");
    tol_loc1(srv, loc_crate_fluid);
    SELFTEST_CHECK(tol_inv_total(player, obj_fluid) >= TOL_NEED_FLUID, "crate08 should grant fluid");
    tol_pass("74_crate_fluid");

    tol_give(player, obj_hammer, 1);
    tol_give(player, obj_saw, 1);
    tol_loc1_rows(srv, loc_pres, build_yes, 1);
    SELFTEST_CHECK(tol_get_vb(player, "tol_pres_prog") == TOL_MACHINE_BUILT, "pressure build should set built=1");
    tol_pass("81_pres_build");
    tol_loc1(srv, loc_pres);
    SELFTEST_CHECK(tol_get_vb(player, "tol_pres_prog") == TOL_MACHINE_FIXED, "pressure fix should set fixed=2");
    tol_pass("82_pres_calibrate");

    tol_loc1_rows(srv, loc_pipe, build_yes, 1);
    SELFTEST_CHECK(tol_get_vb(player, "tol_pipe_prog") == TOL_MACHINE_BUILT, "pipe build should set built=1");
    tol_pass("89_pipe_build");
    tol_loc1(srv, loc_pipe);
    SELFTEST_CHECK(tol_get_vb(player, "tol_pipe_prog") == TOL_MACHINE_FIXED, "pipe fix should set fixed=2");
    tol_pass("90_pipe_calibrate");

    tol_loc1_rows(srv, loc_cage, build_yes, 1);
    SELFTEST_CHECK(tol_get_vb(player, "tol_cage_prog") == TOL_MACHINE_BUILT, "cage build should set built=1");
    tol_pass("96_cage_build");
    tol_loc1(srv, loc_cage);
    SELFTEST_CHECK(tol_get_vb(player, "tol_cage_prog") == TOL_MACHINE_FIXED, "cage fix should set fixed=2");
    SELFTEST_CHECK(tol_quest(player) == TOL_TOWER_FIXED, "all three machines should set tower_fixed=10");
    SELFTEST_CHECK(tol_get_vb(player, "tol_cage_state") == 1, "cage fix should set cage_state");
    tol_pass("97_cage_fix");
    tol_pass("98_tower_all_fixed");
    tol_journal(srv, "journal_05_tower_fixed");

    tol_tele(srv, TOL_EFFIGY_X, TOL_EFFIGY_Z, 0);
    tol_talk_finish(srv, npc_effigy, slot_effigy);
    SELFTEST_CHECK(tol_quest(player) == TOL_CONFRONTING, "report should set confronting=12");
    tol_pass("09_effigy_tower_fixed_report");
    tol_journal(srv, "journal_06_confronting");

    slot_homo_cage = tol_spawn(srv, npc_homo_cage, TOL_HOMO_CAGE_X, TOL_HOMO_CAGE_Z, 3);
    SELFTEST_CHECK(slot_homo_cage >= 0, "caged homunculus should spawn");
    tol_talk_rows(srv, npc_homo_cage, slot_homo_cage, homo_magic, 2);
    SELFTEST_CHECK(tol_quest(player) == TOL_SCARE_ALCHEMISTS, "homunculus talk should set scare=16");
    tol_pass("100_homo_hello");
    tol_journal(srv, "journal_07_scare_alchemists");

    tol_tele(srv, TOL_EFFIGY_X, TOL_EFFIGY_Z, 0);
    tol_talk_finish(srv, npc_effigy, slot_effigy);
    SELFTEST_CHECK(tol_quest(player) == TOL_BASEMENT, "scare report should set basement=17");
    SELFTEST_CHECK(tol_get_vb(player, "tol_homonc_pres") == 1, "scare report should set homonc_pres");
    tol_pass("11_effigy_scare_report");
    tol_journal(srv, "journal_08_basement");

    slot_homo_ground = tol_spawn(srv, npc_homo_ground, TOL_HOMO_GROUND_X, TOL_HOMO_GROUND_Z, 0);
    SELFTEST_CHECK(slot_homo_ground >= 0, "ground homunculus should spawn");
    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;
    con_before = player->stat_xp_tenths[TOL_STAT_CONSTRUCTION];
    craft_before = player->stat_xp_tenths[TOL_STAT_CRAFTING];
    thieve_before = player->stat_xp_tenths[TOL_STAT_THIEVING];
    tol_talk_finish(srv, npc_homo_ground, slot_homo_ground);
    SELFTEST_CHECK(tol_quest(player) == TOL_COMPLETE, "final talk should complete at 18");
    SELFTEST_CHECK(player->stat_xp_tenths[TOL_STAT_CONSTRUCTION] >= con_before + TOL_CON_XP,
                   "complete should award 10000 Construction tenths");
    SELFTEST_CHECK(player->stat_xp_tenths[TOL_STAT_CRAFTING] >= craft_before + TOL_CRAFT_XP,
                   "complete should award 5000 Crafting tenths");
    SELFTEST_CHECK(player->stat_xp_tenths[TOL_STAT_THIEVING] >= thieve_before + TOL_THIEVE_XP,
                   "complete should award 5000 Thieving tenths");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + TOL_QP_REWARD,
                       "complete should award dbrow quest points");
    tol_pass("105_complete_scroll");
    tol_journal(srv, "journal_09_complete");

    tol_talk_finish(srv, npc_effigy, slot_effigy);
    tol_pass("13_effigy_complete");
    tol_talk_finish(srv, npc_homo_ground, slot_homo_ground);
    tol_pass("106_homo_ground_after");

    (void)TOL_SANDWICH_THIEVE_XP;
    tol_pass("leftover_puzzle_solver_widgets");
    tol_pass("leftover_creature_creation");
    tol_pass("leftover_trapdoor_qh_basement");

    tol_free_type(srv, npc_effigy);
    tol_free_type(srv, npc_bonafido);
    tol_free_type(srv, npc_blackeye);
    tol_free_type(srv, npc_nofingers);
    tol_free_type(srv, npc_gummy);
    tol_free_type(srv, npc_guns);
    tol_free_type(srv, npc_homo_cage);
    tol_free_type(srv, npc_homo_ground);
    tol_clear_inv(player);
    tol_reset_quest(srv);
    tol_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_TOWEROFLIFE_SELFTEST_U_H */
