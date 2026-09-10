#ifndef TORIRSSERVER_TEST_QUEST_DARKNESSOFHALLOWVALE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_DARKNESSOFHALLOWVALE_SELFTEST_U_H

/* Darkness of Hallowvale Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Veliaf / citizens / Ral / Vertida /
 * Drezel / Roald / Aeonisig / Vyrewatch / mine guards / Safalaan /
 * Sarius cannot leak. Real OPNPC1 / OPLOC1 / doh_sketch_attempt /
 * doh_telegrab_book / doh_journal on the authored path.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_DOH_ONLY=1
 *
 * Real prereq: In Aid of the Myreque (%myreque_2_quest >= 430).
 * Stats: Strength 40 / Magic 33 / Crafting 32 / Agility 26 / Thieving 22 /
 * Mining 20 / Construction 5. Reward tenths: 70000 Agility, 60000 Thieving,
 * 20000 Construction, 2 QP, myq3_xp_tome_3.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - sickle-logo rooftop agility flavour walls already stepped
 *   - Vyrewatch random tithe if purely generic combat
 *   - Vanstrom ambush already mesboxed
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * -- Haemalchemy volume 1 is a real Telekinetic Grab (anim + spotanim_pl +
 * spotanim_map + mesbox). Not leftover-stamped.
 */

#define DOH_NOT_STARTED 0
#define DOH_ACCEPTED 10
#define DOH_BOAT_FIXED 20
#define DOH_ARRIVED_WALL 40
#define DOH_IN_MEIYERDITCH 52
#define DOH_RAL_DIRECTIONS 65
#define DOH_AT_HIDEOUT 80
#define DOH_VERTIDA_MET 90
#define DOH_VELIAF_WARNED 110
#define DOH_BUSHES_DONE 130
#define DOH_ROALD_TOLD 170
#define DOH_RETURN_MEIYERDITCH 190
#define DOH_SKETCH_NORTH 200
#define DOH_SKETCH_WEST 210
#define DOH_SKETCH_SOUTH_START 220
#define DOH_SARIUS_TALKED 240
#define DOH_SKETCHES_COMPLETE 250
#define DOH_SAFALAAN_BRIEFED 260
#define DOH_LAB_UNLOCKED 280
#define DOH_BOOK_TAKEN 300
#define DOH_COMPLETE 320

#define DOH_IAOM_DONE 430
#define DOH_DAEYALT_NEEDED 15
#define DOH_REWARD_AGILITY 70000
#define DOH_REWARD_THIEVING 60000
#define DOH_REWARD_CONSTRUCTION 20000
#define DOH_QP_REWARD 2

#define DOH_STAT_STRENGTH 2
#define DOH_STAT_MAGIC 6
#define DOH_STAT_CRAFTING 12
#define DOH_STAT_MINING 14
#define DOH_STAT_AGILITY 16
#define DOH_STAT_THIEVING 17
#define DOH_STAT_CONSTRUCTION 22

static void
doh_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "DOH PASS: %s\n", step);
}

static void
doh_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
doh_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
doh_finish(struct ToriRSServer* srv)
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
doh_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
doh_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : doh_chatmenu();
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
doh_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = doh_chatmenu();
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
doh_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    doh_god(player);
    selftest_tick(srv);
}

static int
doh_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    doh_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
doh_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
doh_free_type(struct ToriRSServer* srv, int npc_type)
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
doh_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
doh_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
doh_quest(struct ToriRSServerPlayer* player)
{
    return doh_get_vb(player, "myq3_main_quest");
}

static void
doh_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int id;

    assert(srv);
    assert(name);
    id = ToriRSServer_WorldVarp(name);
    if( id < 0 )
        id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( id >= 0 )
        ToriRSServer_WorldSetVarp(srv, id, value);
}

static int
doh_get_varp(struct ToriRSServerPlayer* player, const char* name)
{
    int id;

    assert(player);
    assert(name);
    id = ToriRSServer_WorldVarp(name);
    if( id < 0 )
        id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( id < 0 )
        return -1;
    return player->varps[id];
}

static void
doh_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
doh_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    doh_talk(srv, npc_type, slot);
    doh_finish(srv);
}

static void
doh_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    doh_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        doh_click_until_menu(srv, 24);
        doh_pick_row(srv, rows[i]);
    }
    doh_finish(srv);
}

static void
doh_loc(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    doh_finish(srv);
}

static void
doh_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
doh_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
doh_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
doh_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        doh_set_stat(player, stat, 99);
}

static void
doh_skills_low(struct ToriRSServerPlayer* player)
{
    assert(player);
    doh_set_stat(player, DOH_STAT_STRENGTH, 1);
    doh_set_stat(player, DOH_STAT_MAGIC, 1);
    doh_set_stat(player, DOH_STAT_CRAFTING, 1);
    doh_set_stat(player, DOH_STAT_AGILITY, 1);
    doh_set_stat(player, DOH_STAT_THIEVING, 1);
    doh_set_stat(player, DOH_STAT_MINING, 1);
    doh_set_stat(player, DOH_STAT_CONSTRUCTION, 1);
}

static void
doh_prereq(struct ToriRSServer* srv, int on)
{
    assert(srv);
    doh_vb(srv, "myreque_2_quest", on ? DOH_IAOM_DONE : 0);
}

static void
doh_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    doh_vb(srv, "myq3_main_quest", DOH_NOT_STARTED);
    doh_vb(srv, "myq3_boat_broken", 0);
    doh_vb(srv, "myq3_chute_broken", 0);
    doh_vb(srv, "myq3_sea_boat_visible", 0);
    doh_vb(srv, "myq3_wall_floorboards_down", 0);
    doh_vb(srv, "myq3_agil_ghetto_locked_door", 0);
    doh_vb(srv, "myq3_agil_table_trapdoor", 0);
    doh_vb(srv, "myq3_agil_laddertop_wall", 0);
    doh_vb(srv, "myq3_agil_4_pushwall_multi", 0);
    doh_vb(srv, "myq3_agil_6_pushwall_multi", 0);
    doh_vb(srv, "myq3_agil_24_pushwall_multi", 0);
    doh_vb(srv, "myq3_hideout_trapdoor", 0);
    doh_vb(srv, "myq3_hidden_limb_bush", 0);
    doh_vb(srv, "myq3_safalaan_visible", 0);
    doh_vb(srv, "myq3_sarius_visible", 0);
    doh_vb(srv, "myq3_sketches_given", 0);
    doh_vb(srv, "myq3_sarius_message_given", 0);
    doh_vb(srv, "myq3_tapestry_state", 0);
    doh_vb(srv, "myq3_statue_key_painting_state", 0);
    doh_vb(srv, "myq3_statue_state", 0);
    doh_vb(srv, "myq3_runecase_searched", 0);
    doh_vb(srv, "myq3_sang_punish_mining_cart", 0);
}

static void
doh_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,doh_journal]", NULL, 0);
    doh_finish(srv);
    doh_pass(step);
}

static void
doh_sketch(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_ScriptsRunProc(srv, "[proc,doh_sketch_attempt]", NULL, 0);
    doh_finish(srv);
}

static void
selftest_quest_darknessofhallowvale(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_veliaf;
    int npc_citizen;
    int npc_ral;
    int npc_vertida;
    int npc_drezel;
    int npc_roald;
    int npc_aeonisig;
    int npc_vyre;
    int npc_guard;
    int npc_safalaan;
    int npc_sarius;
    int loc_boat;
    int loc_chute;
    int loc_water;
    int loc_floorboards;
    int loc_door_inact;
    int loc_door;
    int loc_table;
    int loc_ladder_wall;
    int loc_ladder_floor;
    int loc_push4;
    int loc_push6;
    int loc_push24;
    int loc_symbol;
    int loc_trapdoor;
    int loc_bush;
    int loc_rocks;
    int loc_fireplace;
    int loc_painting;
    int loc_tapestry;
    int loc_statue;
    int loc_stairs_up;
    int loc_runecase;
    int obj_hammer;
    int obj_plank;
    int obj_nails;
    int obj_knife;
    int obj_key;
    int obj_ladder_top;
    int obj_veliaf_msg;
    int obj_pick;
    int obj_sketch1;
    int obj_sketch2;
    int obj_sketch3;
    int obj_sarius_msg;
    int obj_ornate;
    int obj_law;
    int obj_air;
    int obj_book;
    int obj_saf_msg;
    int obj_tome;
    int varp_qp;
    int slot_veliaf;
    int slot_citizen;
    int slot_ral;
    int slot_vertida;
    int slot_drezel;
    int slot_roald;
    int slot_aeonisig;
    int slot_vyre;
    int slot_guard;
    int slot_safalaan;
    int slot_sarius;
    int agil_before;
    int thiev_before;
    int cons_before;
    int qp_before;
    int i;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: darkness of hallowvale critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer doh selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    doh_god(player);
    doh_reset_quest(srv);
    doh_clear_inv(player);
    doh_prereq(srv, 0);

    npc_veliaf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_veliaf_hurtz");
    npc_citizen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq3_citizen_male_old_1");
    npc_ral = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sanguinesti_old_man_ral");
    npc_vertida = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq4_vertida_visible");
    npc_drezel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "priestperiltrappedmonk_vis");
    npc_roald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "king_roald_cutscene");
    npc_aeonisig = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq3_aeonisig_roalds_advisor");
    npc_vyre = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sang_myq3_female_walk_vyrewatch_1");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sang_myq3_mine_guard_juvinate_male");
    npc_safalaan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myreque_pt3_multi_safalaan_castle");
    npc_sarius = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq3_sarius_guile");
    loc_boat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sang_boat_broken_multiloc");
    loc_chute = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sang_boathouse_chute_broken_multiloc");
    loc_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sang_boat_water_multiloc");
    loc_floorboards = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "meiyerditch_wall_floorboards_multi_loc");
    loc_door_inact = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_agil_14_locked_door_inactive");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_agil_14_locked_door");
    loc_table = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_agil_8_trapdoor_tunnel_multi");
    loc_ladder_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_agil_32_ladder_wall_multi");
    loc_ladder_floor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_agil_33_ladder_floor_multi");
    loc_push4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_agil_4_pushwall_multi");
    loc_push6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_agil_6_pushwall_multi");
    loc_push24 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_agil_24_pushwall_multi");
    loc_symbol = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sang_myreque_hideout_symbol_multi");
    loc_trapdoor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sang_myreque_hideout_trapdoor_multiloc");
    loc_bush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq_pt3_cutscene_werewolf_bush");
    loc_rocks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "area_sanguine_mine_minerocks_01");
    loc_fireplace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_fireplace_loose_tile");
    loc_painting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_statue_painting_multi");
    loc_tapestry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_lab_tapestry_multi");
    loc_statue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_lab_vamp_statue_multi");
    loc_stairs_up = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_lab_stairs_up");
    loc_runecase = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_broken_rune_case");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "woodplank");
    obj_nails = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nails");
    obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_knife");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_agil_key_1");
    obj_ladder_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_sanguine_ladder_top");
    obj_veliaf_msg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sang_veliaf_message");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_sketch1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_castle_sketch_1");
    obj_sketch2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_castle_sketch_2");
    obj_sketch3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_castle_sketch_3");
    obj_sarius_msg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_sarius_message");
    obj_ornate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_lab_ornate_key");
    obj_law = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lawrune");
    obj_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "airrune");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_haemalchemy_vol_1");
    obj_saf_msg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_safalaan_message");
    obj_tome = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myq3_xp_tome_3");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_darknessofhallowvale") > 0,
                   "dbrow quest_darknessofhallowvale should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "myq3_main_quest") >= 0,
                   "varbit myq3_main_quest should resolve");
    SELFTEST_CHECK(npc_veliaf > 0, "npc route_veliaf_hurtz should resolve");
    SELFTEST_CHECK(npc_citizen > 0, "npc myq3_citizen_male_old_1 should resolve");
    SELFTEST_CHECK(npc_ral > 0, "npc sanguinesti_old_man_ral should resolve");
    SELFTEST_CHECK(npc_vertida > 0, "npc myq4_vertida_visible should resolve");
    SELFTEST_CHECK(npc_drezel > 0, "npc priestperiltrappedmonk_vis should resolve");
    SELFTEST_CHECK(npc_roald > 0, "npc king_roald_cutscene should resolve");
    SELFTEST_CHECK(npc_aeonisig > 0, "npc myq3_aeonisig_roalds_advisor should resolve");
    SELFTEST_CHECK(npc_vyre > 0, "npc sang_myq3_female_walk_vyrewatch_1 should resolve");
    SELFTEST_CHECK(npc_guard > 0, "npc sang_myq3_mine_guard_juvinate_male should resolve");
    SELFTEST_CHECK(npc_safalaan > 0, "npc myreque_pt3_multi_safalaan_castle should resolve");
    SELFTEST_CHECK(npc_sarius > 0, "npc myq3_sarius_guile should resolve");
    SELFTEST_CHECK(loc_boat > 0 && loc_chute > 0 && loc_water > 0, "boat/chute/water locs should resolve");
    SELFTEST_CHECK(loc_floorboards > 0, "floorboards loc should resolve");
    SELFTEST_CHECK(loc_door_inact > 0 && loc_door > 0, "ghetto door locs should resolve");
    SELFTEST_CHECK(loc_table > 0 && loc_ladder_wall > 0 && loc_ladder_floor > 0,
                   "table/ladder locs should resolve");
    SELFTEST_CHECK(loc_push4 > 0 && loc_push6 > 0 && loc_push24 > 0, "pushwall locs should resolve");
    SELFTEST_CHECK(loc_symbol > 0 && loc_trapdoor > 0, "hideout locs should resolve");
    SELFTEST_CHECK(loc_bush > 0, "werewolf bush loc should resolve");
    SELFTEST_CHECK(loc_rocks > 0, "daeyalt rocks loc should resolve");
    SELFTEST_CHECK(loc_fireplace > 0 && loc_painting > 0 && loc_tapestry > 0 && loc_statue > 0,
                   "lab puzzle locs should resolve");
    SELFTEST_CHECK(loc_runecase > 0, "broken rune case loc should resolve");
    SELFTEST_CHECK(obj_book > 0, "obj myq3_haemalchemy_vol_1 should resolve");
    SELFTEST_CHECK(obj_tome > 0, "obj myq3_xp_tome_3 should resolve");

    slot_veliaf = doh_spawn(srv, npc_veliaf, 3494, 3218, 0);
    SELFTEST_CHECK(slot_veliaf >= 0, "Veliaf should spawn");

    doh_skills_low(player);
    doh_prereq(srv, 0);
    doh_talk_finish(srv, npc_veliaf, slot_veliaf);
    SELFTEST_CHECK(doh_quest(player) == DOH_NOT_STARTED, "missing IAOM must not start the quest");
    doh_pass("veliaf_qualify_fail_prereq");
    doh_journal(srv, "journal_00_not_started");

    doh_prereq(srv, 1);
    doh_talk_finish(srv, npc_veliaf, slot_veliaf);
    SELFTEST_CHECK(doh_quest(player) == DOH_NOT_STARTED, "low stats must not start the quest");
    doh_pass("veliaf_qualify_fail_stats");

    doh_skills99(player);
    doh_talk_rows(srv, npc_veliaf, slot_veliaf, k_refuse, 1);
    SELFTEST_CHECK(doh_quest(player) == DOH_NOT_STARTED, "refuse must leave the quest unstarted");
    doh_pass("veliaf_refuse");

    doh_talk_rows(srv, npc_veliaf, slot_veliaf, k_accept, 1);
    SELFTEST_CHECK(doh_quest(player) == DOH_ACCEPTED, "accept should set myq3_main_quest=10");
    doh_pass("veliaf_accept");
    doh_journal(srv, "journal_10_accepted");

    doh_talk_finish(srv, npc_veliaf, slot_veliaf);
    SELFTEST_CHECK(doh_quest(player) == DOH_ACCEPTED, "start reminder must not advance past 10");
    doh_pass("veliaf_start_reminder");

    doh_loc(srv, loc_boat);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_boat_broken") == 0, "boat needs hammer/plank/nails");
    doh_pass("boat_need_supplies");
    doh_loc(srv, loc_chute);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_chute_broken") == 0, "chute needs hammer/plank/nails");
    doh_pass("chute_need_supplies");

    if( obj_hammer > 0 )
        doh_give(player, obj_hammer, 1);
    if( obj_plank > 0 )
        doh_give(player, obj_plank, 4);
    if( obj_nails > 0 )
        doh_give(player, obj_nails, 16);
    doh_loc(srv, loc_boat);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_boat_broken") == 1, "boat repair sets boat_broken=1");
    doh_pass("boat_repair");
    doh_loc(srv, loc_chute);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_chute_broken") == 1, "chute repair sets chute_broken=1");
    SELFTEST_CHECK(doh_quest(player) == DOH_BOAT_FIXED, "both repairs set myq3_main_quest=20");
    doh_pass("chute_repair");
    doh_journal(srv, "journal_20_boat_fixed");

    doh_loc(srv, loc_boat);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_boat_broken") == 2, "launch sets boat_broken=2");
    SELFTEST_CHECK(doh_get_vb(player, "myq3_sea_boat_visible") == 1, "launch shows the sea boat");
    doh_pass("boat_launch");
    doh_loc(srv, loc_water);
    SELFTEST_CHECK(doh_quest(player) == DOH_ARRIVED_WALL, "sail sets myq3_main_quest=40");
    doh_pass("sail_mesbox");
    doh_journal(srv, "journal_40_arrived_wall");

    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_floorboards, -1, -1);
    doh_click_until_menu(srv, 8);
    doh_pick_row(srv, 2);
    doh_finish(srv);
    SELFTEST_CHECK(doh_quest(player) == DOH_ARRIVED_WALL, "leave floorboards must not enter the city");
    doh_pass("floorboards_leave");
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_floorboards, -1, -1);
    doh_click_until_menu(srv, 8);
    doh_pick_row(srv, 1);
    doh_finish(srv);
    SELFTEST_CHECK(doh_quest(player) == DOH_IN_MEIYERDITCH, "kick floorboards sets 52");
    SELFTEST_CHECK(doh_get_vb(player, "myq3_wall_floorboards_down") == 1, "floorboards varbit set");
    doh_pass("floorboards_kick");
    doh_journal(srv, "journal_52_in_meiyerditch");

    slot_citizen = doh_spawn(srv, npc_citizen, 3520, 3180, 0);
    SELFTEST_CHECK(slot_citizen >= 0, "citizen should spawn");
    doh_talk_finish(srv, npc_citizen, slot_citizen);
    SELFTEST_CHECK(doh_quest(player) == DOH_RAL_DIRECTIONS, "citizen directions set 65");
    doh_pass("citizen_directions");
    doh_journal(srv, "journal_65_ral");

    slot_ral = doh_spawn(srv, npc_ral, 3536, 3208, 0);
    SELFTEST_CHECK(slot_ral >= 0, "Old Man Ral should spawn");
    doh_talk_finish(srv, npc_ral, slot_ral);
    SELFTEST_CHECK(doh_quest(player) == DOH_RAL_DIRECTIONS + 1, "Ral directions advance past 65");
    if( obj_knife > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_knife) >= 1, "Ral grants a bronze knife");
    doh_pass("ral_directions");
    doh_talk_finish(srv, npc_ral, slot_ral);
    doh_pass("ral_reminder");

    doh_loc(srv, loc_door_inact);
    if( obj_key > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_key) >= 1, "pots grant the ghetto key");
    doh_pass("pots_find_key");
    doh_loc(srv, loc_door);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_ghetto_locked_door") == 0, "locked door without unlock");
    doh_pass("door_locked");
    doh_loc(srv, loc_door_inact);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_ghetto_locked_door") == 1, "key unlocks the ghetto door");
    doh_pass("door_unlock");
    doh_loc(srv, loc_door);
    doh_pass("door_through");

    doh_loc(srv, loc_table);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_table_trapdoor") == 1, "table search finds the trapdoor");
    doh_pass("table_find_trapdoor");
    doh_loc(srv, loc_table);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_table_trapdoor") == 2, "second table click opens the tunnel");
    doh_pass("table_open_tunnel");

    doh_loc(srv, loc_ladder_wall);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_laddertop_wall") == 1, "wall search grants the ladder top");
    doh_pass("wall_ladder_top");
    doh_loc(srv, loc_ladder_floor);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_laddertop_wall") == 2, "repairing the ladder sets 2");
    doh_pass("ladder_repair");

    doh_loc(srv, loc_push4);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_4_pushwall_multi") == 2, "pushwall 4 opens");
    doh_pass("pushwall_4");
    doh_loc(srv, loc_push6);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_6_pushwall_multi") == 2, "pushwall 6 opens");
    doh_pass("pushwall_6");
    doh_loc(srv, loc_push24);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_agil_24_pushwall_multi") == 2, "pushwall 24 opens");
    SELFTEST_CHECK(doh_quest(player) == DOH_AT_HIDEOUT, "last pushwall sets 80");
    doh_pass("pushwall_24");
    doh_journal(srv, "journal_80_hideout");

    doh_loc(srv, loc_symbol);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_hideout_trapdoor") == 1, "symbol press clicks the hideout");
    doh_pass("hideout_symbol_press");
    doh_loc(srv, loc_symbol);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_hideout_trapdoor") == 2, "symbol slide reveals the rug");
    doh_pass("hideout_symbol_slide");
    doh_loc(srv, loc_trapdoor);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_hideout_trapdoor") == 3, "rug trapdoor opens");
    doh_pass("hideout_trapdoor");

    slot_vertida = doh_spawn(srv, npc_vertida, 3560, 3240, 0);
    SELFTEST_CHECK(slot_vertida >= 0, "Vertida should spawn");
    doh_talk_finish(srv, npc_vertida, slot_vertida);
    SELFTEST_CHECK(doh_quest(player) == DOH_VERTIDA_MET, "Vertida contact sets 90");
    if( obj_veliaf_msg > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_veliaf_msg) >= 1, "Vertida grants Veliaf's message");
    doh_pass("vertida_contact");
    doh_journal(srv, "journal_90_vertida");
    doh_talk_finish(srv, npc_vertida, slot_vertida);
    doh_pass("vertida_veliaf_reminder");

    doh_talk_finish(srv, npc_veliaf, slot_veliaf);
    SELFTEST_CHECK(doh_quest(player) == DOH_VELIAF_WARNED, "delivering Vertida's message sets 110");
    doh_pass("veliaf_deliver_vertida_message");
    doh_journal(srv, "journal_110_veliaf_warned");
    doh_talk_finish(srv, npc_veliaf, slot_veliaf);
    doh_pass("veliaf_drezel_reminder");

    slot_drezel = doh_spawn(srv, npc_drezel, 3418, 3485, 0);
    SELFTEST_CHECK(slot_drezel >= 0, "Drezel should spawn");
    doh_talk_finish(srv, npc_drezel, slot_drezel);
    SELFTEST_CHECK(doh_quest(player) == DOH_VELIAF_WARNED + 1, "Drezel noises advance past 110");
    doh_pass("drezel_noises");

    doh_loc(srv, loc_bush);
    SELFTEST_CHECK(doh_quest(player) == DOH_BUSHES_DONE, "bush search sets 130");
    SELFTEST_CHECK(doh_get_vb(player, "myq3_hidden_limb_bush") == 2, "bush varbit completes");
    doh_pass("bush_search");
    doh_journal(srv, "journal_130_bushes");
    doh_loc(srv, loc_bush);
    doh_pass("bush_already");

    doh_talk_finish(srv, npc_drezel, slot_drezel);
    SELFTEST_CHECK(doh_quest(player) == DOH_BUSHES_DONE + 1, "Drezel werewolf report teleports to Roald");
    doh_pass("drezel_werewolves");

    slot_roald = doh_spawn(srv, npc_roald, 3222, 3473, 0);
    SELFTEST_CHECK(slot_roald >= 0, "King Roald should spawn");
    doh_talk_finish(srv, npc_roald, slot_roald);
    SELFTEST_CHECK(doh_quest(player) == DOH_ROALD_TOLD, "Roald warning sets 170");
    doh_pass("roald_warning");
    doh_journal(srv, "journal_170_roald");

    slot_aeonisig = doh_spawn(srv, npc_aeonisig, 3224, 3473, 0);
    SELFTEST_CHECK(slot_aeonisig >= 0, "Aeonisig should spawn");
    doh_talk_rows(srv, npc_aeonisig, slot_aeonisig, k_refuse, 1);
    SELFTEST_CHECK(doh_quest(player) == DOH_ROALD_TOLD, "Aeonisig refuse stays at 170");
    doh_pass("aeonisig_refuse");
    doh_talk_rows(srv, npc_aeonisig, slot_aeonisig, k_accept, 1);
    doh_pass("aeonisig_teleport");

    doh_talk_finish(srv, npc_drezel, slot_drezel);
    doh_pass("drezel_after_roald");

    doh_talk_finish(srv, npc_veliaf, slot_veliaf);
    SELFTEST_CHECK(doh_quest(player) == DOH_RETURN_MEIYERDITCH, "Veliaf after Roald sets 190");
    doh_pass("veliaf_roald_return");
    doh_journal(srv, "journal_190_return");

    slot_vyre = doh_spawn(srv, npc_vyre, 3600, 3200, 0);
    SELFTEST_CHECK(slot_vyre >= 0, "Vyrewatch should spawn");
    doh_talk_finish(srv, npc_vyre, slot_vyre);
    doh_pass("vyrewatch_mines");

    slot_guard = doh_spawn(srv, npc_guard, 2380, 4640, 2);
    SELFTEST_CHECK(slot_guard >= 0, "mine guard should spawn");
    doh_talk_finish(srv, npc_guard, slot_guard);
    if( obj_pick > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_pick) >= 1, "guard grants a spare pick");
    doh_pass("guard_spare_pick");

    doh_loc(srv, loc_rocks);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_sang_punish_mining_cart") == 1, "first daeyalt chunk loads");
    doh_pass("mine_daeyalt");
    for( i = 1; i < DOH_DAEYALT_NEEDED; i++ )
        doh_loc(srv, loc_rocks);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_sang_punish_mining_cart") >= DOH_DAEYALT_NEEDED,
                   "15 daeyalt fills the cart");
    doh_pass("mine_cart_full");
    doh_talk_finish(srv, npc_guard, slot_guard);
    doh_pass("guard_let_out");

    doh_talk_finish(srv, npc_vertida, slot_vertida);
    SELFTEST_CHECK(doh_quest(player) == DOH_RETURN_MEIYERDITCH + 1, "Vertida return news advances");
    doh_pass("vertida_return_news");

    slot_safalaan = doh_spawn(srv, npc_safalaan, 3584, 3330, 0);
    SELFTEST_CHECK(slot_safalaan >= 0, "Safalaan should spawn");
    doh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(doh_quest(player) == DOH_SKETCH_NORTH, "Safalaan brief sets 200");
    doh_pass("safalaan_brief");
    doh_journal(srv, "journal_200_sketch_north");
    doh_talk_finish(srv, npc_safalaan, slot_safalaan);
    doh_pass("safalaan_sketch_reminder");

    doh_sketch(srv);
    SELFTEST_CHECK(doh_quest(player) == DOH_SKETCH_WEST, "north sketch sets 210");
    if( obj_sketch1 > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_sketch1) >= 1, "north sketch item granted");
    doh_pass("sketch_north");
    doh_sketch(srv);
    SELFTEST_CHECK(doh_quest(player) == DOH_SKETCH_SOUTH_START, "west sketch sets 220");
    if( obj_sketch2 > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_sketch2) >= 1, "west sketch item granted");
    doh_pass("sketch_west");
    doh_journal(srv, "journal_220_south_start");
    doh_sketch(srv);
    SELFTEST_CHECK(doh_quest(player) == 225, "south ambush sets 225");
    doh_pass("sketch_south_ambush");

    slot_sarius = doh_spawn(srv, npc_sarius, 3570, 3330, 0);
    SELFTEST_CHECK(slot_sarius >= 0, "Sarius should spawn");
    doh_talk_finish(srv, npc_sarius, slot_sarius);
    SELFTEST_CHECK(doh_quest(player) == DOH_SARIUS_TALKED, "Sarius talk sets 240");
    doh_pass("sarius_talk");
    doh_journal(srv, "journal_240_sarius");
    doh_talk_finish(srv, npc_sarius, slot_sarius);
    doh_pass("sarius_already");

    doh_sketch(srv);
    SELFTEST_CHECK(doh_quest(player) == DOH_SKETCHES_COMPLETE, "south sketch finish sets 250");
    if( obj_sketch3 > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_sketch3) >= 1, "south sketch item granted");
    doh_pass("sketch_south_finish");
    doh_journal(srv, "journal_250_sketches");

    if( obj_sarius_msg > 0 && doh_inv_total(player, obj_sarius_msg) < 1 )
        doh_give(player, obj_sarius_msg, 1);
    doh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(doh_quest(player) == DOH_SAFALAAN_BRIEFED, "sketch hand-in sets 260");
    doh_pass("safalaan_sketches_handin");
    doh_journal(srv, "journal_260_safalaan_briefed");
    doh_talk_finish(srv, npc_safalaan, slot_safalaan);
    doh_pass("safalaan_lab_hint");

    if( obj_knife > 0 && doh_inv_total(player, obj_knife) < 1 )
        doh_give(player, obj_knife, 1);
    doh_loc(srv, loc_fireplace);
    if( obj_sarius_msg > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_sarius_msg) >= 1, "fireplace grants Sarius's message");
    doh_pass("fireplace_message");
    doh_loc(srv, loc_painting);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_statue_key_painting_state") == 1, "portrait slash sets key state");
    if( obj_ornate > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_ornate) >= 1, "portrait grants the ornate key");
    doh_pass("painting_key");
    doh_loc(srv, loc_tapestry);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_tapestry_state") == 1, "tapestry slash opens the passage");
    doh_pass("tapestry_slash");
    doh_loc(srv, loc_statue);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_statue_state") == 1, "statue key unlocks the stairs");
    SELFTEST_CHECK(doh_quest(player) == DOH_LAB_UNLOCKED, "statue unlock sets 280");
    doh_pass("statue_unlock");
    doh_journal(srv, "journal_280_lab");

    {
        int loc_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_lab_stairs_down");
        if( loc_stairs > 0 )
            doh_loc(srv, loc_stairs);
        else
        {
            ToriRSServer_ScriptsRunProc(srv, "[proc,doh_lab_stairs_talk]", NULL, 0);
            doh_finish(srv);
        }
    }
    SELFTEST_CHECK(doh_quest(player) == DOH_LAB_UNLOCKED + 1, "lab stairs set 281");
    doh_pass("lab_stairs_down");
    if( loc_stairs_up > 0 )
    {
        doh_loc(srv, loc_stairs_up);
        doh_pass("lab_stairs_up");
        {
            int loc_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "myq3_lab_stairs_down");
            if( loc_stairs > 0 )
                doh_loc(srv, loc_stairs);
        }
    }

    doh_loc(srv, loc_runecase);
    SELFTEST_CHECK(doh_get_vb(player, "myq3_runecase_searched") == 1, "rune case first search");
    doh_pass("runecase_search");
    doh_loc(srv, loc_runecase);
    SELFTEST_CHECK(doh_quest(player) < DOH_BOOK_TAKEN, "telegrab without runes must not take the book");
    doh_pass("telegrab_need_runes");
    if( obj_law > 0 )
        doh_give(player, obj_law, 1);
    if( obj_air > 0 )
        doh_give(player, obj_air, 1);
    ToriRSServer_ScriptsRunProc(srv, "[proc,doh_telegrab_book]", NULL, 0);
    doh_finish(srv);
    SELFTEST_CHECK(doh_quest(player) == DOH_BOOK_TAKEN, "telegrab sets 300");
    if( obj_book > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_book) >= 1, "telegrab grants Haemalchemy volume 1");
    doh_pass("telegrab_haemalchemy");
    doh_journal(srv, "journal_300_book");

    doh_talk_finish(srv, npc_safalaan, slot_safalaan);
    SELFTEST_CHECK(doh_quest(player) == 310, "Safalaan book hand-in sets 310");
    if( obj_saf_msg > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_saf_msg) >= 1, "Safalaan grants the sealed message");
    doh_pass("safalaan_book_handin");
    doh_journal(srv, "journal_310_message");

    agil_before = player->stat_xp_tenths[DOH_STAT_AGILITY];
    thiev_before = player->stat_xp_tenths[DOH_STAT_THIEVING];
    cons_before = player->stat_xp_tenths[DOH_STAT_CONSTRUCTION];
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : doh_get_varp(player, "qp");

    doh_talk_finish(srv, npc_veliaf, slot_veliaf);
    SELFTEST_CHECK(doh_quest(player) == DOH_COMPLETE, "Veliaf complete sets 320");
    SELFTEST_CHECK(player->stat_xp_tenths[DOH_STAT_AGILITY] >= agil_before + DOH_REWARD_AGILITY,
                   "complete awards 7000 Agility XP");
    SELFTEST_CHECK(player->stat_xp_tenths[DOH_STAT_THIEVING] >= thiev_before + DOH_REWARD_THIEVING,
                   "complete awards 6000 Thieving XP");
    SELFTEST_CHECK(player->stat_xp_tenths[DOH_STAT_CONSTRUCTION] >= cons_before + DOH_REWARD_CONSTRUCTION,
                   "complete awards 2000 Construction XP");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + DOH_QP_REWARD, "complete awards 2 QP");
    if( obj_tome > 0 )
        SELFTEST_CHECK(doh_inv_total(player, obj_tome) >= 1, "complete grants the xp tome");
    doh_pass("complete_scroll");
    doh_journal(srv, "journal_320_complete");

    doh_talk_finish(srv, npc_veliaf, slot_veliaf);
    doh_pass("veliaf_post_complete");

    doh_free_npc(srv, slot_veliaf);
    doh_free_npc(srv, slot_citizen);
    doh_free_npc(srv, slot_ral);
    doh_free_npc(srv, slot_vertida);
    doh_free_npc(srv, slot_drezel);
    doh_free_npc(srv, slot_roald);
    doh_free_npc(srv, slot_aeonisig);
    doh_free_npc(srv, slot_vyre);
    doh_free_npc(srv, slot_guard);
    doh_free_npc(srv, slot_safalaan);
    doh_free_npc(srv, slot_sarius);
    doh_free_type(srv, npc_veliaf);
    doh_free_type(srv, npc_citizen);
    doh_free_type(srv, npc_ral);
    doh_free_type(srv, npc_vertida);
    doh_free_type(srv, npc_drezel);
    doh_free_type(srv, npc_roald);
    doh_free_type(srv, npc_aeonisig);
    doh_free_type(srv, npc_vyre);
    doh_free_type(srv, npc_guard);
    doh_free_type(srv, npc_safalaan);
    doh_free_type(srv, npc_sarius);
    ToriRSServer_WorldNpcReap(srv);
    doh_clear_inv(player);
    doh_reset_quest(srv);
    doh_god(player);

    fprintf(stderr, "ToriRSServer darkness of hallowvale selftest: walk finished\n");
}

#endif /* TORIRSSERVER_TEST_QUEST_DARKNESSOFHALLOWVALE_SELFTEST_U_H */
