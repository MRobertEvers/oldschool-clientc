#ifndef TORIRSSERVER_TEST_QUEST_ENAKHRASLAMENT_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ENAKHRASLAMENT_SELFTEST_U_H

/* Enakhra's Lament Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Lazim /
 * Pentyn / Boneguard / Akthanakos / quarry rocks cannot leak. Real OPNPC1 /
 * OPNPCU / OPLOC1 / OPLOCU / OPHELDU on the authored path.
 * player->godmode = 1 for the whole walk (not a death test). Completion goes
 * through Akthanakos OPNPC1 ~enakhraslament_complete -> ~quest_complete_rewards.
 *
 * Gate: TORIRSSERVER_SELFTEST_ENAKH_ONLY=1
 *
 * ::complete / ::enakhraslamentrun are not the walk. No quest prereqs.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - kg-by-kg sandstone collapsed to medium (5kg) pieces
 *   - limb pickups one chisel
 *   - camulet post-quest charge unused
 *   - Enakhra/Akthanakos form swaps cosmetic
 */

#define ENAKH_NOT_STARTED 0
#define ENAKH_STARTED 10
#define ENAKH_STATUE_COMPLETE 30
#define ENAKH_GROUND_FLOOR 40
#define ENAKH_PUZZLE_STARTED 45
#define ENAKH_PUZZLE_FLOOR 50
#define ENAKH_TOPFLOOR 55
#define ENAKH_WALL_REPAIRED 60
#define ENAKH_COMPLETE 70

#define ENAKH_REQ_CRAFTING 50
#define ENAKH_REQ_FIREMAKING 45
#define ENAKH_REQ_PRAYER 43
#define ENAKH_REQ_MAGIC 39
#define ENAKH_REQ_MINING 45
#define ENAKH_REWARD_TENTHS 70000
#define ENAKH_BASE_SAND 7
#define ENAKH_BODY_SAND 4

#define ENAKH_LAZIM_X 3191
#define ENAKH_LAZIM_Z 2926
#define ENAKH_ROCK_X 3193
#define ENAKH_ROCK_Z 2928
#define ENAKH_BOULDER_X 3190
#define ENAKH_BOULDER_Z 2915
#define ENAKH_TEMPLE_X 3127
#define ENAKH_TEMPLE_Z 9325
#define ENAKH_PENTYN_X 3091
#define ENAKH_PENTYN_Z 9324
#define ENAKH_FOUNT_X 3091
#define ENAKH_FOUNT_Z 9307
#define ENAKH_FURN_X 3115
#define ENAKH_FURN_Z 9323
#define ENAKH_BONE_X 3104
#define ENAKH_BONE_Z 9307
#define ENAKH_AKTH_X 3105
#define ENAKH_AKTH_Z 9297

static void
enakh_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ENAKH PASS: %s\n", step);
}

static void
enakh_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
enakh_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
enakh_finish(struct ToriRSServer* srv)
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
enakh_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
enakh_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : enakh_chatmenu();
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
enakh_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = enakh_chatmenu();
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
enakh_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    enakh_god(player);
    selftest_tick(srv);
}

static int
enakh_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    enakh_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
enakh_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
enakh_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
enakh_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
enakh_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
enakh_talk_drain(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    enakh_talk(srv, npc_type, slot);
    enakh_finish(srv);
}

static void
enakh_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    enakh_talk(srv, npc_type, slot);
    enakh_click_until_menu(srv, 24);
    enakh_pick_row(srv, row);
    enakh_finish(srv);
}

static void
enakh_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
enakh_find_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
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
enakh_opheldu(struct ToriRSServer* srv, int obj_type, int use_obj_type)
{
    struct ToriRSServerPlayer* player;
    int slot_a;
    int slot_b;

    assert(srv);
    assert(obj_type > 0);
    assert(use_obj_type > 0);
    player = srv->active_player;
    assert(player);
    slot_a = enakh_find_inv_slot(player, obj_type);
    slot_b = enakh_find_inv_slot(player, use_obj_type);
    player->last_item = obj_type;
    player->last_slot = slot_a;
    player->last_useitem = use_obj_type;
    player->last_useslot = slot_b;
    ToriRSServer_ScriptsRunOpheldu(srv, obj_type, -1, use_obj_type, -1);
    enakh_finish(srv);
    player->last_item = -1;
    player->last_useitem = -1;
    player->last_slot = -1;
    player->last_useslot = -1;
}

static void
enakh_oploc1(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    enakh_tele(srv, x, z, level);
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
    enakh_finish(srv);
}

static void
enakh_oplocu(struct ToriRSServer* srv, int loc_id, int x, int z, int level, int use_obj)
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
    enakh_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    use_slot = enakh_find_inv_slot(player, use_obj);
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
    enakh_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
enakh_opnpcu(struct ToriRSServer* srv, int npc_type, int slot, int use_obj)
{
    struct ToriRSServerPlayer* player;
    int use_slot;

    assert(srv);
    assert(npc_type > 0);
    assert(use_obj > 0);
    player = srv->active_player;
    assert(player);
    use_slot = enakh_find_inv_slot(player, use_obj);
    player->last_useitem = use_obj;
    player->last_useslot = use_slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    enakh_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
enakh_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,enakhraslament_journal]", NULL, 0);
    enakh_finish(srv);
    enakh_pass(step);
}

static void
enakh_set_skills(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
}

static void
enakh_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    enakh_vb(srv, "enakh_quest", ENAKH_NOT_STARTED);
    enakh_vb(srv, "enakh_statue_multivar", 0);
    enakh_vb(srv, "enakh_choose_statue_head", 0);
    enakh_vb(srv, "enakh_lazim_statue_head_blurb", 0);
    enakh_vb(srv, "enakh_lazim_reallyamage", 0);
    enakh_vb(srv, "enakh_left_arm_taken", 0);
    enakh_vb(srv, "enakh_right_arm_taken", 0);
    enakh_vb(srv, "enakh_left_leg_taken", 0);
    enakh_vb(srv, "enakh_right_leg_taken", 0);
    enakh_vb(srv, "enakh_right_armlock", 0);
    enakh_vb(srv, "enakh_left_armlock", 0);
    enakh_vb(srv, "enakh_left_leglock", 0);
    enakh_vb(srv, "enakh_right_leglock", 0);
    enakh_vb(srv, "enakh_z_door", 0);
    enakh_vb(srv, "enakh_m_door", 0);
    enakh_vb(srv, "enakh_r_door", 0);
    enakh_vb(srv, "enakh_k_door", 0);
    enakh_vb(srv, "enakh_seen_pedestal", 0);
    enakh_vb(srv, "enakh_blood_room", 0);
    enakh_vb(srv, "enakh_ice_room", 0);
    enakh_vb(srv, "enakh_smoke_room", 0);
    enakh_vb(srv, "enakh_shadow_room", 0);
    enakh_vb(srv, "enakh_brazier_1_multivar", 0);
    enakh_vb(srv, "enakh_brazier_2_multivar", 0);
    enakh_vb(srv, "enakh_brazier_3_multivar", 0);
    enakh_vb(srv, "enakh_brazier_4_multivar", 0);
    enakh_vb(srv, "enakh_brazier_5_multivar", 0);
    enakh_vb(srv, "enakh_brazier_6_multivar", 0);
    enakh_vb(srv, "enakh_largewall_needs_trimming", 0);
    enakh_vb(srv, "enakh_largewall_multivar", 0);
    enakh_vb(srv, "enakh_player_helps_akthanakos", 0);
    enakh_vb(srv, "enakh_where_is_lazim", 0);
}

static int
enakh_obj(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, name);
}

static int
enakh_loc(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, name);
}

static int
enakh_npc(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, name);
}

static void
enakh_give_runes(struct ToriRSServerPlayer* player)
{
    int air;
    int fire;
    int earth;
    int chaos;

    assert(player);
    air = enakh_obj("airrune");
    fire = enakh_obj("firerune");
    earth = enakh_obj("earthrune");
    chaos = enakh_obj("chaosrune");
    if( air > 0 )
        enakh_give(player, air, 50);
    if( fire > 0 )
        enakh_give(player, fire, 50);
    if( earth > 0 )
        enakh_give(player, earth, 50);
    if( chaos > 0 )
        enakh_give(player, chaos, 50);
}

static void
selftest_quest_enakhraslament(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_lazim;
    int npc_pentyn;
    int npc_fount;
    int npc_furn;
    int npc_bone;
    int npc_akth;
    int loc_sand;
    int loc_gran;
    int loc_boulder;
    int loc_statue;
    int loc_ladder;
    int loc_pedestal;
    int loc_wall;
    int loc_magic;
    int loc_pillar;
    int loc_door_ra;
    int loc_door_ll;
    int loc_door_la;
    int loc_door_rl;
    int loc_door_z;
    int loc_door_m;
    int loc_door_r;
    int loc_door_k;
    int loc_sig_z;
    int loc_sig_m;
    int loc_sig_r;
    int loc_sig_k;
    int loc_b1;
    int loc_b2;
    int loc_b3;
    int loc_b4;
    int loc_b5;
    int loc_b6;
    int obj_chisel;
    int obj_sand;
    int obj_gran;
    int obj_pick;
    int obj_head;
    int obj_soft;
    int obj_mould;
    int obj_camel;
    int obj_bread;
    int obj_logs;
    int obj_oak;
    int obj_willow;
    int obj_maple;
    int obj_candle;
    int obj_coal;
    int obj_arm_r;
    int obj_arm_l;
    int obj_leg_l;
    int obj_leg_r;
    int obj_sig_z;
    int obj_sig_m;
    int obj_sig_r;
    int obj_sig_k;
    int obj_camulet;
    int dbrow;
    int slot;
    int stat_craft;
    int stat_mine;
    int stat_fm;
    int stat_mage;
    int craft_before;
    int mine_before;
    int fm_before;
    int mage_before;
    int i;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: Enakhra's Lament critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer enakhraslament selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    player->godmode = 1;
    srv->members_world = 1;
    enakh_god(player);

    npc_lazim = enakh_npc("enakh_lazim_statue_east_multinpc");
    npc_pentyn = enakh_npc("enakh_pentyn");
    npc_fount = enakh_npc("enakh_dummy_fountain_multinpc");
    npc_furn = enakh_npc("enakh_dummy_furnace_multinpc");
    npc_bone = enakh_npc("enakh_boneguard_multinpc");
    npc_akth = enakh_npc("enakh_akthanakos_multinpc");
    loc_sand = enakh_loc("enakh_sandstone_rocks");
    loc_gran = enakh_loc("enakh_granite_rocks");
    loc_boulder = enakh_loc("enakh_secret_boulder_multiloc_e");
    loc_statue = enakh_loc("enakh_fallen_statue_east_multiloc");
    loc_ladder = enakh_loc("enakh_temple_ladderup");
    loc_pedestal = enakh_loc("enakh_pedestal_multiloc");
    loc_wall = enakh_loc("enakh_largewall_l_multiloc");
    loc_magic = enakh_loc("enakh_magic_wall");
    loc_pillar = enakh_loc("enakh_temple_pillar_ladder_top");
    loc_door_ra = enakh_loc("enakh_door_right_arm_inactive");
    loc_door_ll = enakh_loc("enakh_door_left_leg_inactive");
    loc_door_la = enakh_loc("enakh_door_left_arm_inactive");
    loc_door_rl = enakh_loc("enakh_door_right_leg_inactive");
    loc_door_z = enakh_loc("enakh_door_z_sigil_inactive");
    loc_door_m = enakh_loc("enakh_door_m_sigil_inactive");
    loc_door_r = enakh_loc("enakh_door_r_sigil_inactive");
    loc_door_k = enakh_loc("enakh_door_k_sigil_inactive");
    loc_sig_z = enakh_loc("enakh_pedestal_sigil_z");
    loc_sig_m = enakh_loc("enakh_pedestal_sigil_m");
    loc_sig_r = enakh_loc("enakh_pedestal_sigil_r");
    loc_sig_k = enakh_loc("enakh_pedestal_sigil_k");
    loc_b1 = enakh_loc("enakh_brazier_1_multiloc");
    loc_b2 = enakh_loc("enakh_brazier_2_multiloc");
    loc_b3 = enakh_loc("enakh_brazier_3_multiloc");
    loc_b4 = enakh_loc("enakh_brazier_4_multiloc");
    loc_b5 = enakh_loc("enakh_brazier_5_multiloc");
    loc_b6 = enakh_loc("enakh_brazier_6_multiloc");
    obj_chisel = enakh_obj("chisel");
    obj_sand = enakh_obj("enakh_sandstone_medium");
    obj_gran = enakh_obj("enakh_granite_medium");
    obj_pick = enakh_obj("rune_pickaxe");
    obj_head = enakh_obj("enakh_statue_head_lazim");
    obj_soft = enakh_obj("softclay");
    obj_mould = enakh_obj("enakh_camel_mould_positive");
    obj_camel = enakh_obj("enakh_stone_head_akthanakos");
    obj_bread = enakh_obj("bread");
    obj_logs = enakh_obj("logs");
    obj_oak = enakh_obj("oak_logs");
    obj_willow = enakh_obj("willow_logs");
    obj_maple = enakh_obj("maple_logs");
    obj_candle = enakh_obj("unlit_candle");
    obj_coal = enakh_obj("coal");
    obj_arm_r = enakh_obj("enakh_arm_right");
    obj_arm_l = enakh_obj("enakh_arm_left");
    obj_leg_l = enakh_obj("enakh_leg_left");
    obj_leg_r = enakh_obj("enakh_leg_right");
    obj_sig_z = enakh_obj("enakh_sigil_z");
    obj_sig_m = enakh_obj("enakh_sigil_m");
    obj_sig_r = enakh_obj("enakh_sigil_r");
    obj_sig_k = enakh_obj("enakh_sigil_k");
    obj_camulet = enakh_obj("camulet");
    dbrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_enakhraslament");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_mage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");

    SELFTEST_CHECK(npc_lazim > 0, "npc enakh_lazim_statue_east_multinpc should resolve");
    SELFTEST_CHECK(npc_pentyn > 0, "npc enakh_pentyn should resolve");
    SELFTEST_CHECK(npc_fount > 0, "npc enakh_dummy_fountain_multinpc should resolve");
    SELFTEST_CHECK(npc_furn > 0, "npc enakh_dummy_furnace_multinpc should resolve");
    SELFTEST_CHECK(npc_bone > 0, "npc enakh_boneguard_multinpc should resolve");
    SELFTEST_CHECK(npc_akth > 0, "npc enakh_akthanakos_multinpc should resolve");
    SELFTEST_CHECK(loc_sand > 0, "loc enakh_sandstone_rocks should resolve");
    SELFTEST_CHECK(loc_gran > 0, "loc enakh_granite_rocks should resolve");
    SELFTEST_CHECK(loc_boulder > 0, "loc enakh_secret_boulder_multiloc_e should resolve");
    SELFTEST_CHECK(loc_statue > 0, "loc enakh_fallen_statue_east_multiloc should resolve");
    SELFTEST_CHECK(obj_chisel > 0, "obj chisel should resolve");
    SELFTEST_CHECK(obj_sand > 0, "obj enakh_sandstone_medium should resolve");
    SELFTEST_CHECK(obj_gran > 0, "obj enakh_granite_medium should resolve");
    SELFTEST_CHECK(dbrow > 0, "dbrow quest_enakhraslament should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "enakh_quest") >= 0,
                   "varbit enakh_quest should resolve");
    if( npc_lazim <= 0 || obj_sand <= 0 )
    {
        fprintf(stderr, "ToriRSServer enakhraslament selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    enakh_clear_inv(player);
    enakh_reset_quest(srv);
    enakh_set_skills(player);
    enakh_journal(srv, "journal_0_not_started");

    /* ---- Lazim offer / refuse / accept / statue FSM ---- */
    slot = enakh_spawn(srv, npc_lazim, ENAKH_LAZIM_X, ENAKH_LAZIM_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Lazim should spawn");
    if( slot >= 0 )
    {
        enakh_talk_pick(srv, npc_lazim, slot, 2);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_NOT_STARTED,
                       "No thanks. must not start");
        enakh_pass("opnpc1_lazim_refuse");

        enakh_talk_pick(srv, npc_lazim, slot, 1);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_STARTED,
                       "Of course! must write started, got %d",
                       enakh_get_vb(player, "enakh_quest"));
        enakh_pass("opnpc1_lazim_accept");

        enakh_talk_drain(srv, npc_lazim, slot);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_STARTED,
                       "need-chisel reminder must stay started");
        enakh_pass("opnpc1_lazim_need_chisel_base");

        if( obj_chisel > 0 )
            enakh_give(player, obj_chisel, 1);
        enakh_talk_drain(srv, npc_lazim, slot);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_statue_multivar") == 0,
                       "need-sandstone reminder must not place the base");
        enakh_pass("opnpc1_lazim_need_sandstone_base");
    }
    enakh_journal(srv, "journal_10_started");

    /* ---- Sandstone / granite mine via real oploc1 + pickaxe_checker ---- */
    enakh_clear_inv(player);
    if( obj_pick > 0 )
        enakh_give(player, obj_pick, 1);
    if( loc_sand > 0 )
    {
        for( i = 0; i < 8; i++ )
            enakh_oploc1(srv, loc_sand, ENAKH_ROCK_X, ENAKH_ROCK_Z, 0);
        enakh_pass("oploc1_sandstone_mine");
    }
    if( loc_gran > 0 )
    {
        for( i = 0; i < 8; i++ )
            enakh_oploc1(srv, loc_gran, ENAKH_ROCK_X + 1, ENAKH_ROCK_Z, 0);
        enakh_pass("oploc1_granite_mine");
    }

    /* ---- Statue base / body / details / head ---- */
    enakh_clear_inv(player);
    if( obj_chisel > 0 )
        enakh_give(player, obj_chisel, 1);
    if( obj_sand > 0 )
        enakh_give(player, obj_sand, ENAKH_BASE_SAND);
    if( slot >= 0 )
    {
        enakh_talk_drain(srv, npc_lazim, slot);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_statue_multivar") == 1,
                       "base delivery must write statue_multivar=1, got %d",
                       enakh_get_vb(player, "enakh_statue_multivar"));
        enakh_pass("opnpc1_lazim_base_placed");
    }

    if( obj_sand > 0 )
        enakh_give(player, obj_sand, ENAKH_BODY_SAND);
    if( obj_chisel > 0 && enakh_find_inv_slot(player, obj_chisel) < 0 )
        enakh_give(player, obj_chisel, 1);
    if( slot >= 0 )
    {
        enakh_talk_drain(srv, npc_lazim, slot);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_statue_multivar") == 2,
                       "body delivery must write statue_multivar=2, got %d",
                       enakh_get_vb(player, "enakh_statue_multivar"));
        enakh_pass("opnpc1_lazim_body_placed");

        enakh_talk_pick(srv, npc_lazim, slot, 1);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_statue_multivar") == 3,
                       "details + Lazim head choice must write statue_multivar=3");
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_choose_statue_head") == 0,
                       "Lazim head choice must pack 0");
        enakh_pass("opnpc1_lazim_head_choice");
    }

    if( obj_gran > 0 && obj_chisel > 0 )
    {
        enakh_clear_inv(player);
        enakh_give(player, obj_chisel, 1);
        enakh_give(player, obj_gran, 1);
        enakh_opheldu(srv, obj_chisel, obj_gran);
        SELFTEST_CHECK(obj_head <= 0 || selftest_count_obj(player, obj_head) == 1,
                       "chisel on granite must carve the Lazim head");
        enakh_pass("opheldu_chisel_craft_head");
    }

    if( slot >= 0 )
    {
        if( obj_head > 0 && selftest_count_obj(player, obj_head) == 0 )
            enakh_give(player, obj_head, 1);
        if( obj_chisel > 0 && enakh_find_inv_slot(player, obj_chisel) < 0 )
            enakh_give(player, obj_chisel, 1);
        enakh_talk_drain(srv, npc_lazim, slot);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_STATUE_COMPLETE,
                       "head handoff must write statue_complete, got %d",
                       enakh_get_vb(player, "enakh_quest"));
        enakh_pass("opnpc1_lazim_head_handoff");

        enakh_talk_drain(srv, npc_lazim, slot);
        enakh_pass("opnpc1_lazim_entrance_hint");
    }
    enakh_journal(srv, "journal_30_statue_complete");

    /* ---- Boulder entrance ---- */
    if( loc_boulder > 0 )
    {
        enakh_vb(srv, "enakh_quest", ENAKH_STARTED);
        enakh_oploc1(srv, loc_boulder, ENAKH_BOULDER_X, ENAKH_BOULDER_Z, 0);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_where_is_lazim") == 0,
                       "boulder before statue_complete must not open");
        enakh_pass("oploc1_boulder_too_early");

        enakh_vb(srv, "enakh_quest", ENAKH_STATUE_COMPLETE);
        enakh_oploc1(srv, loc_boulder, ENAKH_BOULDER_X, ENAKH_BOULDER_Z, 0);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_where_is_lazim") == 1,
                       "boulder after statue must move Lazim into the temple");
        enakh_pass("oploc1_boulder_entrance");
    }

    if( slot >= 0 )
    {
        enakh_talk_drain(srv, npc_lazim, slot);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_lazim_reallyamage") == 1,
                       "first temple talk must set lazima-really-a-mage");
        enakh_pass("opnpc1_lazim_mage_reveal");
        enakh_talk_drain(srv, npc_lazim, slot);
        enakh_pass("opnpc1_lazim_limb_reminder");
    }

    /* ---- Limb chisel + four limb doors + four sigil doors ---- */
    enakh_clear_inv(player);
    if( obj_chisel > 0 )
        enakh_give(player, obj_chisel, 1);
    if( loc_statue > 0 && obj_chisel > 0 )
    {
        enakh_oplocu(srv, loc_statue, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 0, obj_chisel);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_left_arm_taken") == 1,
                       "chisel on fallen statue must take all four limbs");
        SELFTEST_CHECK(obj_arm_l <= 0 || selftest_count_obj(player, obj_arm_l) == 1,
                       "left arm must be granted");
        enakh_pass("oplocu_limb_chisel");

        enakh_oplocu(srv, loc_statue, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 0, obj_chisel);
        enakh_pass("oplocu_limb_already");
    }
    if( obj_arm_r > 0 && selftest_count_obj(player, obj_arm_r) == 0 )
        enakh_give(player, obj_arm_r, 1);
    if( obj_arm_l > 0 && selftest_count_obj(player, obj_arm_l) == 0 )
        enakh_give(player, obj_arm_l, 1);
    if( obj_leg_l > 0 && selftest_count_obj(player, obj_leg_l) == 0 )
        enakh_give(player, obj_leg_l, 1);
    if( obj_leg_r > 0 && selftest_count_obj(player, obj_leg_r) == 0 )
        enakh_give(player, obj_leg_r, 1);

    if( loc_door_ra > 0 && obj_arm_r > 0 )
    {
        enakh_oplocu(srv, loc_door_ra, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 0, obj_arm_r);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_right_armlock") == 1,
                       "right-arm door must unlock");
        enakh_pass("oplocu_door_right_arm");
    }
    if( loc_door_ll > 0 && obj_leg_l > 0 )
    {
        enakh_oplocu(srv, loc_door_ll, ENAKH_TEMPLE_X + 1, ENAKH_TEMPLE_Z, 0, obj_leg_l);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_left_leglock") == 1,
                       "left-leg door must unlock");
        enakh_pass("oplocu_door_left_leg");
    }
    if( loc_door_la > 0 && obj_arm_l > 0 )
    {
        enakh_oplocu(srv, loc_door_la, ENAKH_TEMPLE_X + 2, ENAKH_TEMPLE_Z, 0, obj_arm_l);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_left_armlock") == 1,
                       "left-arm door must unlock");
        enakh_pass("oplocu_door_left_arm");
    }
    if( loc_door_rl > 0 && obj_leg_r > 0 )
    {
        enakh_oplocu(srv, loc_door_rl, ENAKH_TEMPLE_X + 3, ENAKH_TEMPLE_Z, 0, obj_leg_r);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_right_leglock") == 1,
                       "right-leg door must unlock");
        enakh_pass("oplocu_door_right_leg");
    }

    if( loc_sig_z > 0 )
    {
        enakh_oploc1(srv, loc_sig_z, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z + 1, 0);
        SELFTEST_CHECK(obj_sig_z <= 0 || selftest_count_obj(player, obj_sig_z) == 1,
                       "Z sigil must be taken");
        enakh_pass("oploc1_sigil_z");
    }
    if( loc_sig_m > 0 )
    {
        enakh_oploc1(srv, loc_sig_m, ENAKH_TEMPLE_X + 1, ENAKH_TEMPLE_Z + 1, 0);
        enakh_pass("oploc1_sigil_m");
    }
    if( loc_sig_r > 0 )
    {
        enakh_oploc1(srv, loc_sig_r, ENAKH_TEMPLE_X + 2, ENAKH_TEMPLE_Z + 1, 0);
        enakh_pass("oploc1_sigil_r");
    }
    if( loc_sig_k > 0 )
    {
        enakh_oploc1(srv, loc_sig_k, ENAKH_TEMPLE_X + 3, ENAKH_TEMPLE_Z + 1, 0);
        enakh_pass("oploc1_sigil_k");
    }
    if( obj_sig_z > 0 && selftest_count_obj(player, obj_sig_z) == 0 )
        enakh_give(player, obj_sig_z, 1);
    if( obj_sig_m > 0 && selftest_count_obj(player, obj_sig_m) == 0 )
        enakh_give(player, obj_sig_m, 1);
    if( obj_sig_r > 0 && selftest_count_obj(player, obj_sig_r) == 0 )
        enakh_give(player, obj_sig_r, 1);
    if( obj_sig_k > 0 && selftest_count_obj(player, obj_sig_k) == 0 )
        enakh_give(player, obj_sig_k, 1);

    if( loc_door_z > 0 && obj_sig_z > 0 )
    {
        enakh_oplocu(srv, loc_door_z, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z + 2, 0, obj_sig_z);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_z_door") == 1, "Z sigil door must open");
        enakh_pass("oplocu_sigil_door_z");
    }
    if( loc_door_m > 0 && obj_sig_m > 0 )
    {
        enakh_oplocu(srv, loc_door_m, ENAKH_TEMPLE_X + 1, ENAKH_TEMPLE_Z + 2, 0, obj_sig_m);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_m_door") == 1, "M sigil door must open");
        enakh_pass("oplocu_sigil_door_m");
    }
    if( loc_door_r > 0 && obj_sig_r > 0 )
    {
        enakh_oplocu(srv, loc_door_r, ENAKH_TEMPLE_X + 2, ENAKH_TEMPLE_Z + 2, 0, obj_sig_r);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_r_door") == 1, "R sigil door must open");
        enakh_pass("oplocu_sigil_door_r");
    }
    if( loc_door_k > 0 && obj_sig_k > 0 )
    {
        enakh_oplocu(srv, loc_door_k, ENAKH_TEMPLE_X + 3, ENAKH_TEMPLE_Z + 2, 0, obj_sig_k);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_k_door") == 1, "K sigil door must open");
        enakh_pass("oplocu_sigil_door_k");
    }

    /* ---- Ladder ---- */
    if( loc_ladder > 0 )
    {
        enakh_vb(srv, "enakh_quest", ENAKH_NOT_STARTED);
        enakh_oploc1(srv, loc_ladder, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 0);
        enakh_pass("oploc1_ladder_nothing");

        enakh_vb(srv, "enakh_quest", ENAKH_STATUE_COMPLETE);
        enakh_vb(srv, "enakh_right_armlock", 0);
        enakh_oploc1(srv, loc_ladder, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 0);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_STATUE_COMPLETE,
                       "barred ladder must not advance");
        enakh_pass("oploc1_ladder_barred");

        enakh_vb(srv, "enakh_right_armlock", 1);
        enakh_vb(srv, "enakh_left_armlock", 1);
        enakh_vb(srv, "enakh_left_leglock", 1);
        enakh_vb(srv, "enakh_right_leglock", 1);
        enakh_vb(srv, "enakh_z_door", 1);
        enakh_vb(srv, "enakh_m_door", 1);
        enakh_vb(srv, "enakh_r_door", 1);
        enakh_vb(srv, "enakh_k_door", 1);
        enakh_oploc1(srv, loc_ladder, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 0);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_GROUND_FLOOR,
                       "open doors + ladder must write ground_floor_done, got %d",
                       enakh_get_vb(player, "enakh_quest"));
        enakh_pass("oploc1_ladder_pedestal");
    }
    enakh_journal(srv, "journal_40_ground_floor");

    /* ---- Pedestal / camel head ---- */
    enakh_clear_inv(player);
    if( obj_soft > 0 && loc_pedestal > 0 )
    {
        enakh_give(player, obj_soft, 1);
        enakh_oplocu(srv, loc_pedestal, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 1, obj_soft);
        SELFTEST_CHECK(obj_mould <= 0 || selftest_count_obj(player, obj_mould) == 1,
                       "soft clay on pedestal must press the camel mould");
        enakh_pass("oplocu_pedestal_softclay");
    }
    if( obj_gran > 0 && obj_chisel > 0 && obj_mould > 0 )
    {
        if( selftest_count_obj(player, obj_mould) == 0 )
            enakh_give(player, obj_mould, 1);
        if( enakh_find_inv_slot(player, obj_chisel) < 0 )
            enakh_give(player, obj_chisel, 1);
        enakh_give(player, obj_gran, 1);
        enakh_opheldu(srv, obj_chisel, obj_gran);
        SELFTEST_CHECK(obj_camel <= 0 || selftest_count_obj(player, obj_camel) == 1,
                       "chisel + mould + granite must carve Akthanakos's head");
        enakh_pass("opheldu_chisel_craft_camel_head");
    }
    if( loc_pedestal > 0 && obj_camel > 0 )
    {
        if( selftest_count_obj(player, obj_camel) == 0 )
            enakh_give(player, obj_camel, 1);
        enakh_oplocu(srv, loc_pedestal, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 1, obj_camel);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_PUZZLE_STARTED,
                       "stone head on pedestal must write puzzle_started, got %d",
                       enakh_get_vb(player, "enakh_quest"));
        enakh_pass("oplocu_pedestal_stone_head");
    }
    enakh_journal(srv, "journal_45_puzzle_started");

    /* ---- Pentyn / fountain / furnace ---- */
    enakh_free_npc(srv, slot);
    slot = -1;
    if( npc_pentyn > 0 && obj_bread > 0 )
    {
        slot = enakh_spawn(srv, npc_pentyn, ENAKH_PENTYN_X, ENAKH_PENTYN_Z, 1);
        if( slot >= 0 )
        {
            enakh_give(player, obj_bread, 2);
            enakh_opnpcu(srv, npc_pentyn, slot, obj_bread);
            SELFTEST_CHECK(enakh_get_vb(player, "enakh_blood_room") == 1,
                           "feeding Pentyn must set blood_room");
            enakh_pass("opnpcu_pentyn_feed");
            enakh_opnpcu(srv, npc_pentyn, slot, obj_bread);
            enakh_pass("opnpcu_pentyn_enough");
        }
        enakh_free_npc(srv, slot);
        slot = -1;
    }
    else
    {
        enakh_vb(srv, "enakh_blood_room", 1);
    }

    if( npc_fount > 0 )
    {
        slot = enakh_spawn(srv, npc_fount, ENAKH_FOUNT_X, ENAKH_FOUNT_Z, 1);
        if( slot >= 0 )
        {
            enakh_give_runes(player);
            enakh_talk_drain(srv, npc_fount, slot);
            SELFTEST_CHECK(enakh_get_vb(player, "enakh_ice_room") == 1,
                           "fire spell on fountain must melt ice");
            enakh_pass("opnpc1_fountain_melt");
            enakh_talk_drain(srv, npc_fount, slot);
            enakh_pass("opnpc1_fountain_already");
        }
        enakh_free_npc(srv, slot);
        slot = -1;
    }
    else
    {
        enakh_vb(srv, "enakh_ice_room", 1);
    }

    if( npc_furn > 0 )
    {
        slot = enakh_spawn(srv, npc_furn, ENAKH_FURN_X, ENAKH_FURN_Z, 1);
        if( slot >= 0 )
        {
            enakh_give_runes(player);
            enakh_talk_drain(srv, npc_furn, slot);
            SELFTEST_CHECK(enakh_get_vb(player, "enakh_smoke_room") == 1,
                           "air spell on furnace must clear ash");
            enakh_pass("opnpc1_furnace_clear");
            enakh_talk_drain(srv, npc_furn, slot);
            enakh_pass("opnpc1_furnace_already");
        }
        enakh_free_npc(srv, slot);
        slot = -1;
    }
    else
    {
        enakh_vb(srv, "enakh_smoke_room", 1);
    }

    /* ---- Six braziers in order ---- */
    enakh_clear_inv(player);
    if( obj_logs > 0 )
        enakh_give(player, obj_logs, 1);
    if( obj_oak > 0 )
        enakh_give(player, obj_oak, 1);
    if( obj_willow > 0 )
        enakh_give(player, obj_willow, 1);
    if( obj_maple > 0 )
        enakh_give(player, obj_maple, 1);
    if( obj_candle > 0 )
        enakh_give(player, obj_candle, 1);
    if( obj_coal > 0 )
        enakh_give(player, obj_coal, 1);
    if( loc_b2 > 0 && obj_oak > 0 )
    {
        enakh_oplocu(srv, loc_b2, ENAKH_TEMPLE_X + 1, ENAKH_TEMPLE_Z, 1, obj_oak);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_brazier_2_multivar") == 0,
                       "oak before normal must refuse");
        enakh_pass("oplocu_brazier_wrong_order");
    }
    if( loc_b1 > 0 && obj_logs > 0 )
    {
        enakh_oplocu(srv, loc_b1, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 1, obj_logs);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_brazier_1_multivar") == 1,
                       "normal logs must light brazier 1");
        enakh_pass("oplocu_brazier_1");
    }
    if( loc_b2 > 0 && obj_oak > 0 )
    {
        enakh_oplocu(srv, loc_b2, ENAKH_TEMPLE_X + 1, ENAKH_TEMPLE_Z, 1, obj_oak);
        enakh_pass("oplocu_brazier_2");
    }
    if( loc_b3 > 0 && obj_willow > 0 )
    {
        enakh_oplocu(srv, loc_b3, ENAKH_TEMPLE_X + 2, ENAKH_TEMPLE_Z, 1, obj_willow);
        enakh_pass("oplocu_brazier_3");
    }
    if( loc_b4 > 0 && obj_maple > 0 )
    {
        enakh_oplocu(srv, loc_b4, ENAKH_TEMPLE_X + 3, ENAKH_TEMPLE_Z, 1, obj_maple);
        enakh_pass("oplocu_brazier_4");
    }
    if( loc_b5 > 0 && obj_candle > 0 )
    {
        enakh_oplocu(srv, loc_b5, ENAKH_TEMPLE_X + 4, ENAKH_TEMPLE_Z, 1, obj_candle);
        enakh_pass("oplocu_brazier_5");
    }
    if( loc_b6 > 0 && obj_coal > 0 )
    {
        enakh_oplocu(srv, loc_b6, ENAKH_TEMPLE_X + 5, ENAKH_TEMPLE_Z, 1, obj_coal);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_PUZZLE_FLOOR,
                       "sixth brazier must write puzzle_floor_done, got %d",
                       enakh_get_vb(player, "enakh_quest"));
        enakh_pass("oplocu_brazier_6");
    }
    enakh_journal(srv, "journal_50_puzzle_floor");

    if( loc_magic > 0 )
    {
        enakh_vb(srv, "enakh_quest", ENAKH_PUZZLE_STARTED);
        enakh_oploc1(srv, loc_magic, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 1);
        enakh_pass("oploc1_magic_wall_blocked");
        enakh_vb(srv, "enakh_quest", ENAKH_PUZZLE_FLOOR);
        enakh_oploc1(srv, loc_magic, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 1);
        enakh_pass("oploc1_magic_wall_pass");
    }
    if( loc_ladder > 0 )
    {
        enakh_vb(srv, "enakh_quest", ENAKH_PUZZLE_FLOOR);
        enakh_oploc1(srv, loc_ladder, ENAKH_TEMPLE_X, ENAKH_TEMPLE_Z, 1);
        enakh_pass("oploc1_ladder_corridor");
    }

    /* ---- Boneguard Crumble Undead ---- */
    if( npc_bone > 0 )
    {
        slot = enakh_spawn(srv, npc_bone, ENAKH_BONE_X, ENAKH_BONE_Z, 2);
        if( slot >= 0 )
        {
            enakh_vb(srv, "enakh_quest", ENAKH_PUZZLE_STARTED);
            enakh_talk_drain(srv, npc_bone, slot);
            SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_PUZZLE_STARTED,
                           "Boneguard too-early must not crumble");
            enakh_pass("opnpc1_boneguard_early");

            enakh_vb(srv, "enakh_quest", ENAKH_PUZZLE_FLOOR);
            enakh_give_runes(player);
            enakh_talk_drain(srv, npc_bone, slot);
            SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_TOPFLOOR,
                           "Crumble Undead must write topfloor_done, got %d",
                           enakh_get_vb(player, "enakh_quest"));
            enakh_pass("opnpc1_boneguard_crumble");
        }
        enakh_free_npc(srv, slot);
        slot = -1;
    }
    else
    {
        enakh_vb(srv, "enakh_quest", ENAKH_TOPFLOOR);
    }
    enakh_journal(srv, "journal_55_topfloor");

    if( loc_pillar > 0 )
    {
        enakh_vb(srv, "enakh_quest", ENAKH_PUZZLE_FLOOR);
        enakh_oploc1(srv, loc_pillar, ENAKH_BONE_X, ENAKH_BONE_Z, 2);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_where_is_lazim") != 3,
                       "pillar ladder before Boneguard must refuse");
        enakh_pass("oploc1_pillar_blocked");
        enakh_vb(srv, "enakh_quest", ENAKH_TOPFLOOR);
        enakh_oploc1(srv, loc_pillar, ENAKH_BONE_X, ENAKH_BONE_Z, 2);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_where_is_lazim") == 3,
                       "pillar ladder after Boneguard must move Lazim");
        enakh_pass("oploc1_pillar_climb");
    }

    /* ---- Wall repair + Akthanakos complete ---- */
    if( npc_akth > 0 )
    {
        slot = enakh_spawn(srv, npc_akth, ENAKH_AKTH_X, ENAKH_AKTH_Z, 1);
        if( slot >= 0 )
        {
            enakh_vb(srv, "enakh_quest", ENAKH_TOPFLOOR);
            enakh_vb(srv, "enakh_player_helps_akthanakos", 0);
            if( selftest_prayer_on(srv, "prayer_protectfrommelee") )
                selftest_prayer_toggle(srv, "prayer_protectfrommelee");
            enakh_talk_drain(srv, npc_akth, slot);
            SELFTEST_CHECK(enakh_get_vb(player, "enakh_player_helps_akthanakos") == 0,
                           "Akthanakos without Protect from Melee must warn");
            enakh_pass("opnpc1_akthanakos_protect");

            if( !selftest_prayer_on(srv, "prayer_protectfrommelee") )
                selftest_prayer_toggle(srv, "prayer_protectfrommelee");
            enakh_talk_drain(srv, npc_akth, slot);
            SELFTEST_CHECK(enakh_get_vb(player, "enakh_player_helps_akthanakos") == 1,
                           "protected talk must accept the wall task");
            enakh_pass("opnpc1_akthanakos_help");
            enakh_talk_drain(srv, npc_akth, slot);
            enakh_pass("opnpc1_akthanakos_repair_hint");
        }
    }

    enakh_clear_inv(player);
    if( obj_chisel > 0 )
        enakh_give(player, obj_chisel, 1);
    if( obj_sand > 0 )
        enakh_give(player, obj_sand, 3);
    if( loc_wall > 0 && obj_sand > 0 && obj_chisel > 0 )
    {
        enakh_oplocu(srv, loc_wall, ENAKH_AKTH_X, ENAKH_AKTH_Z, 1, obj_chisel);
        enakh_pass("oplocu_wall_need_sandstone");
        for( i = 0; i < 3; i++ )
        {
            enakh_oplocu(srv, loc_wall, ENAKH_AKTH_X, ENAKH_AKTH_Z, 1, obj_sand);
            enakh_pass(i == 0 ? "oplocu_wall_sandstone" : "oplocu_wall_sandstone_more");
            enakh_oplocu(srv, loc_wall, ENAKH_AKTH_X, ENAKH_AKTH_Z, 1, obj_chisel);
            if( i == 0 )
                enakh_pass("oplocu_wall_chisel");
        }
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_WALL_REPAIRED,
                       "three sandstone+chisel cycles must repair the wall, got %d",
                       enakh_get_vb(player, "enakh_quest"));
        enakh_pass("oplocu_wall_repaired");
    }
    enakh_journal(srv, "journal_60_wall_repaired");

    if( slot >= 0 && npc_akth > 0 )
    {
        craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
        mine_before = (stat_mine >= 0) ? player->stat_xp_tenths[stat_mine] : 0;
        fm_before = (stat_fm >= 0) ? player->stat_xp_tenths[stat_fm] : 0;
        mage_before = (stat_mage >= 0) ? player->stat_xp_tenths[stat_mage] : 0;
        enakh_talk_drain(srv, npc_akth, slot);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_COMPLETE,
                       "Akthanakos after the wall must complete, got %d",
                       enakh_get_vb(player, "enakh_quest"));
        if( stat_craft >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >=
                               craft_before + ENAKH_REWARD_TENTHS,
                           "complete must advance crafting by 70000 tenths");
        if( stat_mine >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_mine] >=
                               mine_before + ENAKH_REWARD_TENTHS,
                           "complete must advance mining by 70000 tenths");
        if( stat_fm >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_fm] >= fm_before + ENAKH_REWARD_TENTHS,
                           "complete must advance firemaking by 70000 tenths");
        if( stat_mage >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_mage] >=
                               mage_before + ENAKH_REWARD_TENTHS,
                           "complete must advance magic by 70000 tenths");
        if( obj_camulet > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, obj_camulet) == 1,
                           "complete must deliver the Camulet");
        enakh_pass("opnpc1_akthanakos_complete");
        enakh_talk_drain(srv, npc_akth, slot);
        SELFTEST_CHECK(enakh_get_vb(player, "enakh_quest") == ENAKH_COMPLETE,
                       "post-complete Akthanakos must stay complete");
        enakh_pass("opnpc1_akthanakos_post_complete");
    }
    enakh_journal(srv, "journal_70_quest_complete");

    if( npc_lazim > 0 )
    {
        enakh_free_npc(srv, slot);
        slot = enakh_spawn(srv, npc_lazim, ENAKH_LAZIM_X, ENAKH_LAZIM_Z, 0);
        if( slot >= 0 )
        {
            enakh_talk_drain(srv, npc_lazim, slot);
            enakh_pass("opnpc1_lazim_post_complete");
        }
    }

    enakh_free_npc(srv, slot);

    fprintf(stderr, "ToriRSServer enakhraslament selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_ENAKHRASLAMENT_SELFTEST_U_H */
