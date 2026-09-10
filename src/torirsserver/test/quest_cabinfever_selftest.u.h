#ifndef TORIRSSERVER_TEST_QUEST_CABINFEVER_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_CABINFEVER_SELFTEST_U_H

/* Cabin Fever Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Bill /
 * lockers / holes / cannon / plunder locs cannot leak. Real OPNPC1 /
 * OPLOC1 / OPLOC2 / OPLOCU on the authored path. player->godmode = 1 for
 * the whole walk (not a death test). Completion goes through ship Bill
 * OPNPC1 -> ~cabinfever_quest_complete -> ~quest_complete_rewards and
 * teleports to Mos Le'Harmless.
 *
 * Gate: TORIRSSERVER_SELFTEST_CABIN_ONLY=1
 *
 * ::complete / ::cabinfever are not the walk. Prereqs are genuine varp
 * writes (Pirate's Treasure + Rum Deal + Priest in Peril holy-barrier).
 *
 * Disclosed leftovers (named leftover_*.bmp):
 *   - Locker one-shot full grant
 *   - Plunder fixed split (4+3+3)
 *   - Canister phase is load/fire cycle, not direct cannon damage
 *   - No jammed-cannon misfire state
 */

#define CF_NOT_STARTED 0
#define CF_ACCEPTED 10
#define CF_SET_SAIL 30
#define CF_SABOTAGE_DONE 40
#define CF_REPAIRS_DONE 70
#define CF_LOOT_DONE 90
#define CF_CANNON_REPAIRED 110
#define CF_KILLED_ONE 111
#define CF_KILLED_TWO 112
#define CF_KILLED_THREE 113
#define CF_BALLS_PHASE 130
#define CF_COMPLETE 140

#define CF_REQ_AGILITY 42
#define CF_REQ_CRAFTING 45
#define CF_REQ_SMITHING 50
#define CF_REQ_RANGED 40
#define CF_HUNT_COMPLETE 4
#define CF_DEAL_COMPLETE 19
#define CF_PIP_COMPLETE 60
#define CF_PIP_BARRIER 61
#define CF_REWARD_TENTHS 70000
#define CF_REWARD_QP 2
#define CF_REWARD_COINS 10000
#define CF_GP_FUSED 2
#define CF_GP_EXPLODED 1
#define CF_HOLE_PLANKED 1
#define CF_HOLE_PROOFED 2
#define CF_CANNON_BROKEN 1
#define CF_CANNON_READY 3
#define CF_PLUNDER_NEEDED 10
#define CF_HULL_NEEDED 3

#define CF_BILL_DOCK_X 3679
#define CF_BILL_DOCK_Z 3495
#define CF_BILL_PORT_X 3713
#define CF_BILL_PORT_Z 3497
#define CF_BILL_SHIP_X 1814
#define CF_BILL_SHIP_Z 4836
#define CF_GANG_X 3712
#define CF_GANG_Z 3496
#define CF_OWN_SAIL_X 1817
#define CF_OWN_SAIL_Z 4830
#define CF_ENEMY_SAIL_X 1822
#define CF_ENEMY_SAIL_Z 4835
#define CF_BARREL_X 1824
#define CF_BARREL_Z 4831
#define CF_HOLE1_X 1810
#define CF_HOLE1_Z 4830
#define CF_HOLE2_X 1811
#define CF_HOLE2_Z 4830
#define CF_HOLE3_X 1812
#define CF_HOLE3_Z 4830
#define CF_WEAPONS_X 1812
#define CF_WEAPONS_Z 4834
#define CF_REPAIR_X 1813
#define CF_REPAIR_Z 4834
#define CF_CANNON_X 1816
#define CF_CANNON_Z 4838
#define CF_POWDER_X 1815
#define CF_POWDER_Z 4837
#define CF_CRATE_X 1824
#define CF_CRATE_Z 4828
#define CF_CHEST_X 1825
#define CF_CHEST_Z 4828
#define CF_LOOTBARREL_X 1826
#define CF_LOOTBARREL_Z 4828
#define CF_DEPOSIT_X 1814
#define CF_DEPOSIT_Z 4832
#define CF_MOSLE_X 3683
#define CF_MOSLE_Z 2948

static void
cf_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "CF PASS: %s\n", step);
}

static void
cf_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
cf_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
cf_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 8) <= 0 )
            selftest_tick(srv);
    }
    for( t = 0; t < 16; t++ )
        selftest_tick(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    for( t = 0; t < 4; t++ )
        selftest_tick(srv);
}

static int
cf_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
cf_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : cf_chatmenu();
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
cf_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = cf_chatmenu();
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
cf_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    cf_god(player);
    selftest_tick(srv);
}

static int
cf_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    cf_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
cf_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
cf_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
cf_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
cf_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
cf_get_varp(struct ToriRSServerPlayer* player, const char* name)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp < 0 )
        return -1;
    return player->varps[varp];
}

static int
cf_quest(struct ToriRSServerPlayer* player)
{
    return cf_get_varp(player, "fever_quest");
}

static void
cf_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
cf_talk_drain(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    cf_talk(srv, npc_type, slot);
    cf_finish(srv);
}

static void
cf_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    cf_talk(srv, npc_type, slot);
    cf_click_until_menu(srv, 24);
    cf_pick_row(srv, row);
    cf_finish(srv);
}

static void
cf_talk_pick2(
    struct ToriRSServer* srv,
    int npc_type,
    int slot,
    int row1,
    int row2)
{
    assert(srv);
    cf_talk(srv, npc_type, slot);
    cf_click_until_menu(srv, 24);
    cf_pick_row(srv, row1);
    cf_click_until_menu(srv, 24);
    cf_pick_row(srv, row2);
    cf_finish(srv);
}

static void
cf_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
    }
}

static int
cf_find_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            return s;
    }
    return -1;
}

static void
cf_oploc1(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    cf_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    cf_finish(srv);
}

static void
cf_oploc2(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    cf_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC2, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC2, loc_id, -1, -1);
    cf_finish(srv);
}

static void
cf_oplocu(struct ToriRSServer* srv, int loc_id, int x, int z, int level, int use_obj)
{
    struct ToriRSServerPlayer* player;
    int slot;
    int placed;
    int use_slot;

    assert(srv);
    assert(loc_id > 0);
    assert(use_obj > 0);
    player = srv->active_player;
    assert(player);
    cf_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    use_slot = cf_find_inv_slot(player, use_obj);
    player->last_useitem = use_obj;
    player->last_useslot = use_slot;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    cf_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
cf_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,cabinfever_journal]", NULL, 0);
    cf_finish(srv);
    cf_pass(step);
}

static void
cf_set_skills(
    struct ToriRSServerPlayer* player,
    int agil,
    int craft,
    int smith,
    int ranged)
{
    int stat_agil;
    int stat_craft;
    int stat_smith;
    int stat_ranged;

    assert(player);
    stat_agil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
    stat_ranged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
    if( stat_agil >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_agil, agil);
    if( stat_craft >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_craft, craft);
    if( stat_smith >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_smith, smith);
    if( stat_ranged >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_ranged, ranged);
}

static void
cf_clear_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    cf_varp(srv, "hunt", 0);
    cf_varp(srv, "deal_quest", 0);
    cf_varp(srv, "priestperil", 0);
}

static void
cf_set_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    cf_varp(srv, "hunt", CF_HUNT_COMPLETE);
    cf_varp(srv, "deal_quest", CF_DEAL_COMPLETE);
    cf_varp(srv, "priestperil", CF_PIP_BARRIER);
}

static void
cf_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    cf_varp(srv, "fever_quest", CF_NOT_STARTED);
    cf_vb(srv, "fever_gunpowder_barrel", 0);
    cf_vb(srv, "fever_fuse_1", 0);
    cf_vb(srv, "fever_fuse_2", 0);
    cf_vb(srv, "fever_enemy_cannon", 0);
    cf_vb(srv, "fever_hole_1", 0);
    cf_vb(srv, "fever_hole_2", 0);
    cf_vb(srv, "fever_hole_3", 0);
    cf_vb(srv, "fever_holes_patched", 0);
    cf_vb(srv, "fever_holes_proofed", 0);
    cf_vb(srv, "fever_crate", 0);
    cf_vb(srv, "fever_chest", 0);
    cf_vb(srv, "fever_barrel", 0);
    cf_vb(srv, "fever_plunder_points", 0);
    cf_vb(srv, "fever_cannon", 0);
    cf_vb(srv, "fever_cannon_powder", 0);
    cf_vb(srv, "fever_cannon_tamp", 0);
    cf_vb(srv, "fever_cannon_ammo", 0);
    cf_vb(srv, "fever_cannon_fuse", 0);
    cf_vb(srv, "fever_cannon_clean", 0);
    cf_vb(srv, "fever_holes_in_the_hull", 0);
    cf_vb(srv, "fever_given_book", 0);
    cf_vb(srv, "fever_gold", 0);
}

static void
cf_fire_cycle(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_cannon,
    int obj_powder,
    int obj_prod,
    int obj_ammo,
    int obj_fuse,
    int obj_tinder)
{
    assert(srv);
    assert(player);
    if( obj_powder > 0 )
        cf_give(player, obj_powder, 1);
    if( obj_prod > 0 && selftest_count_obj(player, obj_prod) <= 0 )
        cf_give(player, obj_prod, 1);
    if( obj_ammo > 0 )
        cf_give(player, obj_ammo, 1);
    if( obj_fuse > 0 )
        cf_give(player, obj_fuse, 1);
    if( obj_tinder > 0 && selftest_count_obj(player, obj_tinder) <= 0 )
        cf_give(player, obj_tinder, 1);
    if( loc_cannon > 0 && obj_powder > 0 )
        cf_oplocu(srv, loc_cannon, CF_CANNON_X, CF_CANNON_Z, 1, obj_powder);
    if( loc_cannon > 0 && obj_prod > 0 )
        cf_oplocu(srv, loc_cannon, CF_CANNON_X, CF_CANNON_Z, 1, obj_prod);
    if( loc_cannon > 0 && obj_ammo > 0 )
        cf_oplocu(srv, loc_cannon, CF_CANNON_X, CF_CANNON_Z, 1, obj_ammo);
    if( loc_cannon > 0 && obj_fuse > 0 )
        cf_oplocu(srv, loc_cannon, CF_CANNON_X, CF_CANNON_Z, 1, obj_fuse);
    if( loc_cannon > 0 && obj_tinder > 0 )
        cf_oplocu(srv, loc_cannon, CF_CANNON_X, CF_CANNON_Z, 1, obj_tinder);
}

static void
selftest_quest_cabinfever(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_dock;
    int npc_port;
    int npc_ship;
    int loc_gang;
    int loc_gang_exit;
    int loc_sail;
    int loc_barrel;
    int loc_fuse2;
    int loc_hole1;
    int loc_hole2;
    int loc_hole3;
    int loc_weapons;
    int loc_repair;
    int loc_cannon;
    int loc_powder;
    int loc_crate;
    int loc_chest;
    int loc_lootbarrel;
    int loc_deposit;
    int obj_fuse;
    int obj_rope;
    int obj_tinder;
    int obj_hammer;
    int obj_plank;
    int obj_tack;
    int obj_paste;
    int obj_plunder;
    int obj_cannon;
    int obj_powder;
    int obj_prod;
    int obj_canister;
    int obj_ball;
    int obj_book;
    int obj_coins;
    int dbrow;
    int slot;
    int slot_port;
    int slot_ship;
    int stat_agil;
    int stat_craft;
    int stat_smith;
    int stat_ranged;
    int varp_qp;
    int agil_before;
    int craft_before;
    int smith_before;
    int qp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: Cabin Fever critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer cabinfever selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    player->godmode = 1;
    srv->members_world = 1;
    cf_god(player);

    npc_dock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fever_teach");
    npc_port = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fever_port_ship_teach");
    npc_ship = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fever_quest_ship_teach");
    loc_gang = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_gangplank");
    loc_gang_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_gangplank_exit");
    loc_sail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_sail1_hoistedl_climb");
    loc_barrel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_gunpowder_barrel");
    loc_fuse2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_fuse_2");
    loc_hole1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_hole_1");
    loc_hole2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_hole_2");
    loc_hole3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_hole_3");
    loc_weapons = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_weapons_locker");
    loc_repair = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_repair_locker");
    loc_cannon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_cannon");
    loc_powder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_your_gunpowder_barrel");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_crate");
    loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_chest");
    loc_lootbarrel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_multi_barrel");
    loc_deposit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fever_plunder_deposit");
    obj_fuse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_fuse");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_repair_plank");
    obj_tack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_tack");
    obj_paste = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamppaste");
    obj_plunder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_plunder");
    obj_cannon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_cannon");
    obj_powder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_gunpowder");
    obj_prod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_cannon_prod");
    obj_canister = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_cannister");
    obj_ball = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_cannon_ball");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fever_piracy_book");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    dbrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_cabinfever");
    stat_agil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
    stat_ranged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(npc_dock > 0, "npc fever_teach should resolve");
    SELFTEST_CHECK(npc_port > 0, "npc fever_port_ship_teach should resolve");
    SELFTEST_CHECK(npc_ship > 0, "npc fever_quest_ship_teach should resolve");
    SELFTEST_CHECK(loc_gang > 0, "loc fever_gangplank should resolve");
    SELFTEST_CHECK(loc_sail > 0, "loc fever_sail1_hoistedl_climb should resolve");
    SELFTEST_CHECK(loc_barrel > 0, "loc fever_multi_gunpowder_barrel should resolve");
    SELFTEST_CHECK(loc_fuse2 > 0, "loc fever_multi_fuse_2 should resolve");
    SELFTEST_CHECK(loc_hole1 > 0 && loc_hole2 > 0 && loc_hole3 > 0,
                   "hull hole locs should resolve");
    SELFTEST_CHECK(loc_weapons > 0 && loc_repair > 0, "locker locs should resolve");
    SELFTEST_CHECK(loc_cannon > 0, "loc fever_multi_cannon should resolve");
    SELFTEST_CHECK(loc_crate > 0 && loc_chest > 0 && loc_lootbarrel > 0,
                   "plunder locs should resolve");
    SELFTEST_CHECK(dbrow > 0, "dbrow quest_cabinfever should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fever_quest") >= 0,
                   "varp fever_quest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hunt") >= 0,
                   "varp hunt should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "deal_quest") >= 0,
                   "varp deal_quest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "priestperil") >= 0,
                   "varp priestperil should resolve");
    if( npc_dock <= 0 || loc_cannon <= 0 )
    {
        fprintf(stderr, "ToriRSServer cabinfever selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    cf_clear_inv(player);
    cf_reset_quest(srv);
    cf_clear_prereqs(srv);
    cf_set_skills(player, 1, 1, 1, 1);
    cf_journal(srv, "journal_not_started");

    /* ---- Dock Bill: refuse / qualify-fail / accept ---- */
    slot = cf_spawn(srv, npc_dock, CF_BILL_DOCK_X, CF_BILL_DOCK_Z, 0);
    SELFTEST_CHECK(slot >= 0, "dock Bill should spawn");
    if( slot >= 0 )
    {
        cf_talk_pick(srv, npc_dock, slot, 2);
        SELFTEST_CHECK(cf_quest(player) == CF_NOT_STARTED,
                       "No thanks, I'm busy. must not start");
        cf_pass("opnpc1_bill_dock_refuse_busy");

        cf_set_skills(player, 1, 1, 1, 1);
        cf_set_prereqs(srv);
        cf_talk_pick(srv, npc_dock, slot, 1);
        SELFTEST_CHECK(cf_quest(player) == CF_NOT_STARTED,
                       "low stats must fail cabinfever_meets_requirements");
        cf_pass("opnpc1_bill_qualify_fail_stats");

        cf_set_skills(player, CF_REQ_AGILITY, CF_REQ_CRAFTING, CF_REQ_SMITHING, CF_REQ_RANGED);
        cf_clear_prereqs(srv);
        cf_talk_pick(srv, npc_dock, slot, 1);
        SELFTEST_CHECK(cf_quest(player) == CF_NOT_STARTED,
                       "missing Pirate's Treasure / Rum Deal / PiP must fail");
        cf_pass("opnpc1_bill_qualify_fail_prereqs");

        cf_varp(srv, "hunt", CF_HUNT_COMPLETE);
        cf_varp(srv, "deal_quest", CF_DEAL_COMPLETE);
        cf_varp(srv, "priestperil", 0);
        cf_talk_pick(srv, npc_dock, slot, 1);
        SELFTEST_CHECK(cf_quest(player) == CF_NOT_STARTED,
                       "Priest in Peril holy-barrier gate must stay");
        cf_pass("opnpc1_bill_qualify_fail_priestperil");

        cf_set_prereqs(srv);
        cf_talk_pick2(srv, npc_dock, slot, 1, 2);
        SELFTEST_CHECK(cf_quest(player) == CF_NOT_STARTED,
                       "No thanks. on accept must not start");
        cf_pass("opnpc1_bill_dock_refuse_accept");

        cf_talk_pick2(srv, npc_dock, slot, 1, 1);
        SELFTEST_CHECK(cf_quest(player) == CF_ACCEPTED,
                       "Yes, I've always wanted to be a pirate! must write 10, got %d",
                       cf_quest(player));
        cf_pass("opnpc1_bill_dock_accept_pirate");

        cf_talk_drain(srv, npc_dock, slot);
        SELFTEST_CHECK(cf_quest(player) == CF_ACCEPTED,
                       "board reminder must stay accepted");
        cf_pass("opnpc1_bill_dock_board_reminder");
    }
    cf_journal(srv, "journal_accepted");

    /* ---- Gangplank too-early / board / exit ---- */
    cf_reset_quest(srv);
    cf_set_prereqs(srv);
    cf_set_skills(player, CF_REQ_AGILITY, CF_REQ_CRAFTING, CF_REQ_SMITHING, CF_REQ_RANGED);
    if( loc_gang > 0 )
    {
        cf_oploc1(srv, loc_gang, CF_GANG_X, CF_GANG_Z, 1);
        SELFTEST_CHECK(cf_quest(player) == CF_NOT_STARTED,
                       "gangplank before accept must not board");
        cf_pass("oploc1_gangplank_too_early");
    }
    cf_varp(srv, "fever_quest", CF_ACCEPTED);
    if( loc_gang > 0 )
    {
        cf_oploc1(srv, loc_gang, CF_GANG_X, CF_GANG_Z, 1);
        SELFTEST_CHECK(player->x == (58 * 64 + 2) && player->z == (54 * 64 + 40),
                       "gangplank must teleport onto Bill's dock ship, at %d,%d",
                       player->x, player->z);
        cf_pass("oploc1_gangplank_board");
    }
    if( loc_gang_exit > 0 )
    {
        cf_oploc1(srv, loc_gang_exit, CF_GANG_X, CF_GANG_Z, 1);
        cf_pass("oploc1_gangplank_exit");
    }

    /* ---- Ship Bill: refuse sail / accept sail ---- */
    slot_port = -1;
    if( npc_port > 0 )
    {
        slot_port = cf_spawn(srv, npc_port, CF_BILL_PORT_X, CF_BILL_PORT_Z, 1);
        SELFTEST_CHECK(slot_port >= 0, "dock-ship Bill should spawn");
        if( slot_port >= 0 )
        {
            cf_talk_pick(srv, npc_port, slot_port, 2);
            SELFTEST_CHECK(cf_quest(player) == CF_ACCEPTED,
                           "Not yet. must not set sail");
            cf_pass("opnpc1_bill_ship_refuse_sail");

            cf_talk_pick(srv, npc_port, slot_port, 1);
            SELFTEST_CHECK(cf_quest(player) == CF_SET_SAIL,
                           "Let's go Cap'n! must write 30, got %d",
                           cf_quest(player));
            SELFTEST_CHECK(player->x == (28 * 64 + 23) && player->z == (75 * 64 + 34),
                           "set sail must teleport to the battle deck, at %d,%d",
                           player->x, player->z);
            cf_pass("opnpc1_bill_ship_accept_sail");
        }
    }
    cf_journal(srv, "journal_set_sail");
    cf_free_npc(srv, slot_port);

    /* ---- Lockers: fuse + rope ---- */
    cf_clear_inv(player);
    if( loc_weapons > 0 )
    {
        cf_oploc1(srv, loc_weapons, CF_WEAPONS_X, CF_WEAPONS_Z, 1);
        SELFTEST_CHECK(obj_fuse <= 0 || selftest_count_obj(player, obj_fuse) == 1,
                       "weapons locker at set-sail must grant a fuse");
        cf_pass("oploc1_weapons_locker_fuse");
    }
    if( loc_repair > 0 )
    {
        cf_oploc1(srv, loc_repair, CF_REPAIR_X, CF_REPAIR_Z, 1);
        SELFTEST_CHECK(obj_rope <= 0 || selftest_count_obj(player, obj_rope) >= 1,
                       "repair locker at set-sail must grant rope");
        cf_pass("oploc1_repair_locker_rope");
    }

    /* ---- Rope swing both ways ---- */
    if( loc_sail > 0 && obj_rope > 0 )
    {
        if( selftest_count_obj(player, obj_rope) <= 0 )
            cf_give(player, obj_rope, 2);
        cf_oplocu(srv, loc_sail, CF_OWN_SAIL_X, CF_OWN_SAIL_Z, 2, obj_rope);
        SELFTEST_CHECK(player->x == CF_ENEMY_SAIL_X && player->z == CF_ENEMY_SAIL_Z,
                       "rope on own sail must swing to the enemy ship, at %d,%d",
                       player->x, player->z);
        cf_pass("oplocu_rope_swing_to_enemy");

        cf_oplocu(srv, loc_sail, CF_ENEMY_SAIL_X, CF_ENEMY_SAIL_Z, 2, obj_rope);
        SELFTEST_CHECK(player->x == CF_OWN_SAIL_X && player->z == CF_OWN_SAIL_Z,
                       "rope on enemy sail must swing back, at %d,%d",
                       player->x, player->z);
        cf_pass("oplocu_rope_swing_back");
    }

    /* ---- Sabotage: attach fuse / already / light fuse_2 ---- */
    if( loc_barrel > 0 && obj_fuse > 0 )
    {
        cf_clear_inv(player);
        cf_give(player, obj_fuse, 1);
        if( obj_tinder > 0 )
            cf_give(player, obj_tinder, 1);
        cf_oplocu(srv, loc_barrel, CF_BARREL_X, CF_BARREL_Z, 1, obj_fuse);
        SELFTEST_CHECK(cf_get_vb(player, "fever_gunpowder_barrel") == CF_GP_FUSED,
                       "fuse on barrel must write fused=2, got %d",
                       cf_get_vb(player, "fever_gunpowder_barrel"));
        cf_pass("oplocu_sabotage_fuse_attach");

        if( obj_fuse > 0 )
            cf_give(player, obj_fuse, 1);
        cf_oplocu(srv, loc_barrel, CF_BARREL_X, CF_BARREL_Z, 1, obj_fuse);
        SELFTEST_CHECK(cf_get_vb(player, "fever_gunpowder_barrel") == CF_GP_FUSED,
                       "second fuse must stay fused");
        cf_pass("oplocu_sabotage_already_rigged");
    }
    if( loc_fuse2 > 0 && obj_tinder > 0 )
    {
        cf_clear_inv(player);
        cf_give(player, obj_tinder, 1);
        cf_vb(srv, "fever_gunpowder_barrel", 0);
        cf_oplocu(srv, loc_fuse2, CF_BARREL_X, CF_BARREL_Z, 1, obj_tinder);
        SELFTEST_CHECK(cf_get_vb(player, "fever_gunpowder_barrel") == 0,
                       "tinderbox on fuse before attach must refuse");
        cf_pass("oplocu_sabotage_light_need_fuse");

        cf_vb(srv, "fever_gunpowder_barrel", CF_GP_FUSED);
        cf_oplocu(srv, loc_fuse2, CF_BARREL_X, CF_BARREL_Z, 1, obj_tinder);
        SELFTEST_CHECK(cf_get_vb(player, "fever_gunpowder_barrel") == CF_GP_EXPLODED,
                       "tinderbox on fever_multi_fuse_2 must explode the barrel");
        cf_pass("oplocu_sabotage_light_fuse2");
    }

    slot_ship = -1;
    if( npc_ship > 0 )
    {
        slot_ship = cf_spawn(srv, npc_ship, CF_BILL_SHIP_X, CF_BILL_SHIP_Z, 1);
        SELFTEST_CHECK(slot_ship >= 0, "ship Bill should spawn");
        if( slot_ship >= 0 )
        {
            cf_vb(srv, "fever_gunpowder_barrel", 0);
            cf_talk_drain(srv, npc_ship, slot_ship);
            SELFTEST_CHECK(cf_quest(player) == CF_SET_SAIL,
                           "sabotage reminder must stay set-sail");
            cf_pass("opnpc1_bill_sabotage_reminder");

            cf_vb(srv, "fever_gunpowder_barrel", CF_GP_EXPLODED);
            cf_talk_drain(srv, npc_ship, slot_ship);
            SELFTEST_CHECK(cf_quest(player) == CF_SABOTAGE_DONE,
                           "sabotage report must write 40, got %d",
                           cf_quest(player));
            cf_pass("opnpc1_bill_sabotage_report");
        }
    }
    cf_journal(srv, "journal_sabotage_done");

    /* ---- Repair locker + three hull plank/proof ---- */
    cf_clear_inv(player);
    if( loc_repair > 0 )
    {
        cf_oploc1(srv, loc_repair, CF_REPAIR_X, CF_REPAIR_Z, 1);
        SELFTEST_CHECK(obj_plank <= 0 || selftest_count_obj(player, obj_plank) >= 6,
                       "repair locker must one-shot grant planks/tacks/paste/hammer");
        cf_pass("oploc1_repair_locker_kit");
    }
    if( loc_hole1 > 0 )
    {
        cf_clear_inv(player);
        cf_oploc1(srv, loc_hole1, CF_HOLE1_X, CF_HOLE1_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_1") == 0,
                       "hole 1 without materials must refuse");
        cf_pass("oploc1_hull1_need_materials");

        if( obj_hammer > 0 )
            cf_give(player, obj_hammer, 1);
        if( obj_plank > 0 )
            cf_give(player, obj_plank, 6);
        if( obj_tack > 0 )
            cf_give(player, obj_tack, 30);
        cf_oploc1(srv, loc_hole1, CF_HOLE1_X, CF_HOLE1_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_1") == CF_HOLE_PLANKED,
                       "hole 1 plank must write 1");
        cf_pass("oploc1_hull1_plank");

        cf_oploc1(srv, loc_hole1, CF_HOLE1_X, CF_HOLE1_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_1") == CF_HOLE_PLANKED,
                       "hole 1 without paste must stay planked");
        cf_pass("oploc1_hull1_need_paste");

        if( obj_paste > 0 )
            cf_give(player, obj_paste, 3);
        cf_oploc1(srv, loc_hole1, CF_HOLE1_X, CF_HOLE1_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_1") == CF_HOLE_PROOFED,
                       "hole 1 proof must write 2");
        cf_pass("oploc1_hull1_proof");
    }
    if( loc_hole2 > 0 )
    {
        if( obj_hammer > 0 && selftest_count_obj(player, obj_hammer) <= 0 )
            cf_give(player, obj_hammer, 1);
        if( obj_plank > 0 && selftest_count_obj(player, obj_plank) < 2 )
            cf_give(player, obj_plank, 4);
        if( obj_tack > 0 && selftest_count_obj(player, obj_tack) < 10 )
            cf_give(player, obj_tack, 20);
        cf_oploc1(srv, loc_hole2, CF_HOLE2_X, CF_HOLE2_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_2") == CF_HOLE_PLANKED,
                       "hole 2 plank must write 1");
        cf_pass("oploc1_hull2_plank");
        if( obj_paste > 0 && selftest_count_obj(player, obj_paste) < 1 )
            cf_give(player, obj_paste, 2);
        cf_oploc1(srv, loc_hole2, CF_HOLE2_X, CF_HOLE2_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_2") == CF_HOLE_PROOFED,
                       "hole 2 proof must write 2");
        cf_pass("oploc1_hull2_proof");
    }
    if( loc_hole3 > 0 )
    {
        if( obj_hammer > 0 && selftest_count_obj(player, obj_hammer) <= 0 )
            cf_give(player, obj_hammer, 1);
        if( obj_plank > 0 && selftest_count_obj(player, obj_plank) < 2 )
            cf_give(player, obj_plank, 2);
        if( obj_tack > 0 && selftest_count_obj(player, obj_tack) < 10 )
            cf_give(player, obj_tack, 10);
        cf_oploc1(srv, loc_hole3, CF_HOLE3_X, CF_HOLE3_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_3") == CF_HOLE_PLANKED,
                       "hole 3 plank must write 1");
        cf_pass("oploc1_hull3_plank");
        if( obj_paste > 0 && selftest_count_obj(player, obj_paste) < 1 )
            cf_give(player, obj_paste, 1);
        cf_oploc1(srv, loc_hole3, CF_HOLE3_X, CF_HOLE3_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_3") == CF_HOLE_PROOFED,
                       "hole 3 proof must write 2");
        SELFTEST_CHECK(cf_get_vb(player, "fever_holes_proofed") >= 3,
                       "three proofed holes must increment fever_holes_proofed");
        cf_pass("oploc1_hull3_proof");

        cf_oploc1(srv, loc_hole3, CF_HOLE3_X, CF_HOLE3_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_hole_3") == CF_HOLE_PROOFED,
                       "already-fixed hole must stay proofed");
        cf_pass("oploc1_hull_already_fixed");
    }

    if( slot_ship >= 0 )
    {
        cf_vb(srv, "fever_holes_proofed", 0);
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_SABOTAGE_DONE,
                       "repair reminder must stay sabotage_done");
        cf_pass("opnpc1_bill_repair_reminder");

        cf_vb(srv, "fever_holes_proofed", 3);
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_REPAIRS_DONE,
                       "repair report must write 70, got %d",
                       cf_quest(player));
        cf_pass("opnpc1_bill_repair_report");
    }
    cf_journal(srv, "journal_repairs_done");

    /* ---- Plunder crate / chest / barrel + store ---- */
    cf_clear_inv(player);
    if( loc_crate > 0 )
    {
        cf_oploc1(srv, loc_crate, CF_CRATE_X, CF_CRATE_Z, 0);
        SELFTEST_CHECK(obj_plunder <= 0 || selftest_count_obj(player, obj_plunder) == 4,
                       "crate must grant 4 plunder");
        cf_pass("oploc1_plunder_crate");
        cf_oploc1(srv, loc_crate, CF_CRATE_X, CF_CRATE_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_crate") == 1,
                       "crate must stay looted");
        cf_pass("oploc1_plunder_crate_already");
    }
    if( loc_chest > 0 )
    {
        cf_oploc1(srv, loc_chest, CF_CHEST_X, CF_CHEST_Z, 0);
        SELFTEST_CHECK(obj_plunder <= 0 || selftest_count_obj(player, obj_plunder) == 7,
                       "chest must grant 3 plunder (running total 7)");
        cf_pass("oploc1_plunder_chest");
    }
    if( loc_lootbarrel > 0 )
    {
        cf_oploc1(srv, loc_lootbarrel, CF_LOOTBARREL_X, CF_LOOTBARREL_Z, 0);
        SELFTEST_CHECK(obj_plunder <= 0 || selftest_count_obj(player, obj_plunder) == 10,
                       "barrel must grant 3 plunder (running total 10)");
        cf_pass("oploc1_plunder_barrel");
    }
    if( loc_deposit > 0 )
    {
        cf_oploc2(srv, loc_deposit, CF_DEPOSIT_X, CF_DEPOSIT_Z, 0);
        SELFTEST_CHECK(cf_get_vb(player, "fever_plunder_points") >= CF_PLUNDER_NEEDED,
                       "storing plunder must reach 10, got %d",
                       cf_get_vb(player, "fever_plunder_points"));
        cf_pass("oploc2_plunder_deposit");
    }

    if( slot_ship >= 0 )
    {
        cf_vb(srv, "fever_plunder_points", 0);
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_REPAIRS_DONE,
                       "loot reminder must stay repairs_done");
        cf_pass("opnpc1_bill_loot_reminder");

        cf_vb(srv, "fever_plunder_points", CF_PLUNDER_NEEDED);
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_LOOT_DONE,
                       "loot report must write 90, got %d",
                       cf_quest(player));
        SELFTEST_CHECK(cf_get_vb(player, "fever_cannon") == CF_CANNON_BROKEN,
                       "loot report must break the cannon");
        cf_pass("opnpc1_bill_loot_report_cannon_break");
    }
    cf_journal(srv, "journal_loot_done");

    /* ---- Cannon barrel + repair ---- */
    cf_clear_inv(player);
    if( loc_weapons > 0 )
    {
        cf_oploc1(srv, loc_weapons, CF_WEAPONS_X, CF_WEAPONS_Z, 1);
        SELFTEST_CHECK(obj_cannon <= 0 || selftest_count_obj(player, obj_cannon) == 1,
                       "weapons locker at loot_done must grant a cannon barrel");
        cf_pass("oploc1_weapons_locker_barrel");
    }
    if( loc_cannon > 0 && obj_cannon > 0 )
    {
        if( selftest_count_obj(player, obj_cannon) <= 0 )
            cf_give(player, obj_cannon, 1);
        cf_oplocu(srv, loc_cannon, CF_CANNON_X, CF_CANNON_Z, 1, obj_cannon);
        SELFTEST_CHECK(cf_get_vb(player, "fever_cannon") == CF_CANNON_READY,
                       "repair must write cannon ready=3");
        cf_pass("oplocu_cannon_repair");
    }

    if( slot_ship >= 0 )
    {
        cf_vb(srv, "fever_cannon", CF_CANNON_BROKEN);
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_LOOT_DONE,
                       "cannon reminder must stay loot_done");
        cf_pass("opnpc1_bill_cannon_reminder");

        cf_vb(srv, "fever_cannon", CF_CANNON_READY);
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_CANNON_REPAIRED,
                       "cannon report must write 110, got %d",
                       cf_quest(player));
        cf_pass("opnpc1_bill_cannon_report");
    }
    cf_journal(srv, "journal_cannon_repaired");

    /* ---- Canister load/fire cycle x3 ---- */
    cf_clear_inv(player);
    if( loc_weapons > 0 )
    {
        cf_oploc1(srv, loc_weapons, CF_WEAPONS_X, CF_WEAPONS_Z, 1);
        cf_pass("oploc1_weapons_locker_canister");
    }
    if( loc_powder > 0 )
    {
        cf_oploc1(srv, loc_powder, CF_POWDER_X, CF_POWDER_Z, 1);
        SELFTEST_CHECK(obj_powder <= 0 || selftest_count_obj(player, obj_powder) >= 1,
                       "powder barrel must grant gunpowder");
        cf_pass("oploc1_powder_take");
    }
    cf_fire_cycle(srv, player, loc_cannon, obj_powder, obj_prod, obj_canister, obj_fuse,
                  obj_tinder);
    SELFTEST_CHECK(cf_quest(player) == CF_KILLED_ONE,
                   "first canister fire must write 111, got %d",
                   cf_quest(player));
    cf_pass("oplocu_cannon_fire_canister_1");
    cf_fire_cycle(srv, player, loc_cannon, obj_powder, obj_prod, obj_canister, obj_fuse,
                  obj_tinder);
    SELFTEST_CHECK(cf_quest(player) == CF_KILLED_TWO,
                   "second canister fire must write 112, got %d",
                   cf_quest(player));
    cf_pass("oplocu_cannon_fire_canister_2");
    cf_fire_cycle(srv, player, loc_cannon, obj_powder, obj_prod, obj_canister, obj_fuse,
                  obj_tinder);
    SELFTEST_CHECK(cf_quest(player) == CF_KILLED_THREE,
                   "third canister fire must write 113, got %d",
                   cf_quest(player));
    cf_pass("oplocu_cannon_fire_canister_3");

    if( slot_ship >= 0 )
    {
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_BALLS_PHASE,
                       "killed-three report must write 130, got %d",
                       cf_quest(player));
        cf_pass("opnpc1_bill_killed_three");
    }
    cf_journal(srv, "journal_balls_phase");

    /* ---- Ball fire driving fever_holes_in_the_hull ---- */
    cf_clear_inv(player);
    if( loc_weapons > 0 )
    {
        cf_oploc1(srv, loc_weapons, CF_WEAPONS_X, CF_WEAPONS_Z, 1);
        cf_pass("oploc1_weapons_locker_balls");
    }
    cf_fire_cycle(srv, player, loc_cannon, obj_powder, obj_prod, obj_ball, obj_fuse, obj_tinder);
    SELFTEST_CHECK(cf_get_vb(player, "fever_holes_in_the_hull") == 1,
                   "first ball must write holes_in_the_hull=1");
    cf_pass("oplocu_cannon_fire_ball_1");
    cf_fire_cycle(srv, player, loc_cannon, obj_powder, obj_prod, obj_ball, obj_fuse, obj_tinder);
    SELFTEST_CHECK(cf_get_vb(player, "fever_holes_in_the_hull") == 2,
                   "second ball must write holes_in_the_hull=2");
    cf_pass("oplocu_cannon_fire_ball_2");
    cf_fire_cycle(srv, player, loc_cannon, obj_powder, obj_prod, obj_ball, obj_fuse, obj_tinder);
    SELFTEST_CHECK(cf_get_vb(player, "fever_holes_in_the_hull") == CF_HULL_NEEDED,
                   "third ball must write holes_in_the_hull=3");
    cf_pass("oplocu_cannon_fire_ball_3");

    if( slot_ship >= 0 )
    {
        cf_vb(srv, "fever_holes_in_the_hull", 0);
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_BALLS_PHASE,
                       "balls reminder must stay 130");
        cf_pass("opnpc1_bill_balls_reminder");

        agil_before = (stat_agil >= 0) ? player->stat_xp_tenths[stat_agil] : 0;
        craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
        smith_before = (stat_smith >= 0) ? player->stat_xp_tenths[stat_smith] : 0;
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        cf_vb(srv, "fever_holes_in_the_hull", CF_HULL_NEEDED);
        cf_talk_drain(srv, npc_ship, slot_ship);
        SELFTEST_CHECK(cf_quest(player) == CF_COMPLETE,
                       "three hull holes must complete, got %d",
                       cf_quest(player));
        if( stat_agil >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_agil] >= agil_before + CF_REWARD_TENTHS,
                           "complete must advance agility by 70000 tenths");
        if( stat_craft >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >=
                               craft_before + CF_REWARD_TENTHS,
                           "complete must advance crafting by 70000 tenths");
        if( stat_smith >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_smith] >=
                               smith_before + CF_REWARD_TENTHS,
                           "complete must advance smithing by 70000 tenths");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + CF_REWARD_QP,
                           "complete must award 2 quest points");
        SELFTEST_CHECK(player->x == CF_MOSLE_X && player->z == CF_MOSLE_Z,
                       "complete must teleport to Mos Le'Harmless, at %d,%d",
                       player->x, player->z);
        if( obj_book > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, obj_book) == 1,
                           "complete must grant Book o' piracy");
        (void)stat_ranged;
        (void)obj_coins;
        cf_pass("opnpc1_bill_complete_scroll_mosle");
    }
    cf_journal(srv, "journal_complete");

    /* Post-complete coin claim at dock Bill. */
    if( slot >= 0 )
    {
        cf_tele(srv, CF_BILL_DOCK_X, CF_BILL_DOCK_Z, 0);
        cf_talk_drain(srv, npc_dock, slot);
        SELFTEST_CHECK(cf_get_vb(player, "fever_gold") == 1,
                       "post-complete Bill must pay 10000 coins once");
        cf_pass("opnpc1_bill_post_complete_coins");
        cf_talk_drain(srv, npc_dock, slot);
        SELFTEST_CHECK(cf_quest(player) == CF_COMPLETE,
                       "second post-complete talk must stay complete");
        cf_pass("opnpc1_bill_post_complete");
    }

    cf_free_npc(srv, slot);
    cf_free_npc(srv, slot_ship);

    fprintf(stderr, "ToriRSServer cabinfever selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_CABINFEVER_SELFTEST_U_H */
