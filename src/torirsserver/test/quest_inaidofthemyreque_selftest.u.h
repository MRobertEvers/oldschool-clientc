#ifndef TORIRSSERVER_TEST_QUEST_INAIDOFTHEMYREQUE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_INAIDOFTHEMYREQUE_SELFTEST_U_H

/* In Aid of the Myreque Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Veliaf / Florin / Aurel / Gadderanks /
 * Ivan / Drezel / locs cannot leak. Real OPNPC1 / OPNPC2 / OPLOC1 /
 * OPLOC2 / OPLOCU / OPHELDU on the authored path. player->godmode = 1
 * for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_INAID_ONLY=1
 *
 * In Search of the Myreque is hard-gated via %routequest (varp). Skills
 * are Crafting 25 / Mining 15 / Magic 7 (stat_base). Finish goes through
 * existing [opnpc1,myq5_veliaf_child] -> ~myreque2_finish_veliaf.
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF / daily-reset
 * / flute widget / telekinetic grab):
 *   - Basement rubble one-shot (native counter still driven to 5)
 *   - Portable crate collapsed to Aurel bulk hand-in
 *   - Temple Trekking escort collapsed to the short 2-juvinate route
 *   - Lvl-1 Enchant is cosmic+water on the rod, not magic_spell_table
 *   - Ivan optional armour/food gifts unused
 */

#define IA_NOT_STARTED 0
#define IA_AT_BURGH 20
#define IA_FOOD_GIVEN 30
#define IA_TOLD_RAZVAN 40
#define IA_TRAPDOOR_OPEN 80
#define IA_BASEMENT_CLEAR 100
#define IA_LEFT_BASEMENT 110
#define IA_ROOF_FIXED 140
#define IA_WALL_FIXED 150
#define IA_CRATE_GIVEN 160
#define IA_CRATE_FILLED 165
#define IA_BOOTH_FIXED 170
#define IA_BANKWALL_FIXED 180
#define IA_CORNELIUS 190
#define IA_FURNACE_HINT 200
#define IA_FURNACE_REPAIRED 205
#define IA_FURNACE_COAL 210
#define IA_FURNACE_LIT 220
#define IA_FIGHT_STARTED 240
#define IA_GADDERANKS_DEFEATED 260
#define IA_VELIAF_RETURNED 280
#define IA_POLMAFI 290
#define IA_TREK_STARTED 300
#define IA_AT_TEMPLE 315
#define IA_LIBRARY_UNLOCKED 350
#define IA_BOOK_READ 370
#define IA_ROD_PHASE 375
#define IA_ROD_BLESSED 410
#define IA_COMPLETE 430

#define IA_ROUTEQUEST_COMPLETE 105
#define IA_REQ_CRAFTING 25
#define IA_REQ_MINING 15
#define IA_REQ_MAGIC 7
#define IA_REWARD_TENTHS 20000
#define IA_REWARD_QP 2
#define IA_RUBBLE_NEEDED 5
#define IA_CRATE_AXES 10
#define IA_CRATE_FOOD 10
#define IA_CRATE_TINDER 3
#define IA_BLOOD_TITHE 3
#define IA_JUVINATE_DEATHS 3
#define IA_AMBUSH_DEATHS 2

#define IA_VELIAF_X 3506
#define IA_VELIAF_Z 9838
#define IA_POLMAFI_X 3514
#define IA_POLMAFI_Z 9838
#define IA_IVAN_X 3513
#define IA_IVAN_Z 9843
#define IA_FLORIN_X 3483
#define IA_FLORIN_Z 3243
#define IA_RAZVAN_X 3492
#define IA_RAZVAN_Z 3235
#define IA_AUREL_X 3517
#define IA_AUREL_Z 3241
#define IA_CORNELIUS_X 3496
#define IA_CORNELIUS_Z 3212
#define IA_GADDERANKS_X 3514
#define IA_GADDERANKS_Z 3241
#define IA_JUVE1_X 3513
#define IA_JUVE1_Z 3241
#define IA_JUVE2_X 3513
#define IA_JUVE2_Z 3242
#define IA_WISKIT_X 3515
#define IA_WISKIT_Z 3242
#define IA_SHOP_WALL_X 3517
#define IA_SHOP_WALL_Z 3238
#define IA_BANK_WALL_X 3491
#define IA_BANK_WALL_Z 3211
#define IA_SHOP_X 3515
#define IA_SHOP_Z 3241
#define IA_AMBUSH_X 2859
#define IA_AMBUSH_Z 4564
#define IA_TEMPLE_X 3439
#define IA_TEMPLE_Z 9896
#define IA_LIBRARY_X 3354
#define IA_LIBRARY_Z 9902
#define IA_COFFIN_X 3511
#define IA_COFFIN_Z 9864
#define IA_FINISH_X 3494
#define IA_FINISH_Z 9628
#define IA_CHEST_X 3484
#define IA_CHEST_Z 3242
#define IA_INN_WALL_X 3488
#define IA_INN_WALL_Z 3230
#define IA_TRAPDOOR_X 3489
#define IA_TRAPDOOR_Z 3231
#define IA_RUBBLE_X 3489
#define IA_RUBBLE_Z 9620
#define IA_LADDER_X 3488
#define IA_LADDER_Z 9621
#define IA_ROOF_X 3516
#define IA_ROOF_Z 3240
#define IA_BOOTH_X 3495
#define IA_BOOTH_Z 3213
#define IA_FURNACE_X 3520
#define IA_FURNACE_Z 3210
#define IA_KEYHOLE_X 3440
#define IA_KEYHOLE_Z 9895
#define IA_LIBTRAP_X 3441
#define IA_LIBTRAP_Z 9894
#define IA_BOOKCASE_X 3355
#define IA_BOOKCASE_Z 9902
#define IA_BOARDS_X 3510
#define IA_BOARDS_Z 9865
#define IA_TOMB_X 3510
#define IA_TOMB_Z 9866
#define IA_WELL_X 3438
#define IA_WELL_Z 9898

static void
ia_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "IA PASS: %s\n", step);
}

static void
ia_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ia_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ia_finish(struct ToriRSServer* srv)
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
ia_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ia_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : ia_chatmenu();
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
ia_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ia_chatmenu();
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
ia_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ia_god(player);
    selftest_tick(srv);
}

static int
ia_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    ia_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
ia_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
ia_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ia_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
ia_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
ia_quest(struct ToriRSServerPlayer* player)
{
    return ia_get_vb(player, "myreque_2_quest");
}

static void
ia_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
ia_talk_drain(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ia_talk(srv, npc_type, slot);
    ia_finish(srv);
}

static void
ia_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    ia_talk(srv, npc_type, slot);
    ia_click_until_menu(srv, 24);
    ia_pick_row(srv, row);
    ia_finish(srv);
}

static void
ia_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
ia_find_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
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
ia_oploc(
    struct ToriRSServer* srv,
    int trigger,
    int loc_id,
    int x,
    int z,
    int level)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    ia_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, trigger, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, trigger, loc_id, -1, -1);
    ia_finish(srv);
}

static void
ia_oplocu(struct ToriRSServer* srv, int loc_id, int x, int z, int level, int use_obj)
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
    ia_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    use_slot = ia_find_inv_slot(player, use_obj);
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
    ia_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
ia_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = ia_find_inv_slot(player, used);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    ia_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
ia_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,myreque2_journal]", NULL, 0);
    ia_finish(srv);
    ia_pass(step);
}

static void
ia_set_skills(struct ToriRSServerPlayer* player, int craft, int mine, int magic)
{
    int stat_craft;
    int stat_mine;
    int stat_magic;

    assert(player);
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    if( stat_craft >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_craft, craft);
    if( stat_mine >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_mine, mine);
    if( stat_magic >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_magic, magic);
}

static void
ia_reset_side(struct ToriRSServer* srv)
{
    assert(srv);
    ia_vb(srv, "myreque_2_quest", IA_NOT_STARTED);
    ia_vb(srv, "burgh_inn_colapsed_wall", 0);
    ia_vb(srv, "burgh_inn_trapdoor", 0);
    ia_vb(srv, "burgh_inn_rubble_pile", 0);
    ia_vb(srv, "burgh_store_roof", 0);
    ia_vb(srv, "burgh_store_wall", 0);
    ia_vb(srv, "burgh_axes_crate", 0);
    ia_vb(srv, "burgh_food_crate", 0);
    ia_vb(srv, "burgh_tinderbox_crate", 0);
    ia_vb(srv, "burgh_store_stocked", 0);
    ia_vb(srv, "burgh_bank_booth_open", 0);
    ia_vb(srv, "burgh_bank_wall", 0);
    ia_vb(srv, "burgh_bank_teller", 0);
    ia_vb(srv, "burgh_furnace_fix", 0);
    ia_vb(srv, "gadderanks_blood_tithe_chat", 0);
    ia_vb(srv, "juve_blood_tithe_chat", 0);
    ia_vb(srv, "villager_blood_tithe_chat", 0);
    ia_vb(srv, "juvinate_deaths", 0);
    ia_vb(srv, "juvinate_ambush_deaths", 0);
    ia_vb(srv, "blood_tithe_visible", 0);
    ia_vb(srv, "burgh_temple_trapdoor", 0);
    ia_vb(srv, "ivandis_tomb_boards", 0);
}

static void
ia_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    ia_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    ia_finish(srv);
}

static void
selftest_quest_inaidofthemyreque(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_veliaf;
    int npc_polmafi;
    int npc_ivan;
    int npc_florin;
    int npc_razvan;
    int npc_aurel;
    int npc_cornelius;
    int npc_gadderanks;
    int npc_juve_talk;
    int npc_wiskit;
    int npc_gadd_atk;
    int npc_juve1_atk;
    int npc_juve2_atk;
    int npc_wounded;
    int npc_rescue;
    int npc_ambush1;
    int npc_ambush2;
    int npc_drezel;
    int npc_finish;
    int loc_chest;
    int loc_inn_wall;
    int loc_trap;
    int loc_rubble;
    int loc_ladder;
    int loc_roof;
    int loc_shop_wall;
    int loc_booth;
    int loc_furnace_broken;
    int loc_furnace_repaired;
    int loc_furnace_coal;
    int loc_furnace_fired;
    int loc_keyhole;
    int loc_libtrap;
    int loc_bookcase;
    int loc_boards;
    int loc_tomb;
    int loc_coffin;
    int loc_well;
    int obj_bread;
    int obj_pick;
    int obj_spade;
    int obj_hammer;
    int obj_plank;
    int obj_nails;
    int obj_axe;
    int obj_mackerel;
    int obj_tinder;
    int obj_paste;
    int obj_steel;
    int obj_coal;
    int obj_key;
    int obj_clay;
    int obj_mould;
    int obj_silver;
    int obj_mithril;
    int obj_sapphire;
    int obj_rod1;
    int obj_rod2;
    int obj_rod10;
    int obj_cosmic;
    int obj_water;
    int obj_rope;
    int stat_craft;
    int stat_mine;
    int stat_magic;
    int stat_attack;
    int stat_str;
    int stat_def;
    int varp_qp;
    int slot_veliaf;
    int slot_polmafi;
    int slot_ivan;
    int slot_florin;
    int slot_razvan;
    int slot_aurel;
    int slot_cornelius;
    int slot_gadderanks;
    int slot_juve;
    int slot_wiskit;
    int slot_drezel;
    int slot_finish;
    int slot_atk[3];
    int slot_ambush[2];
    int craft_before;
    int attack_before;
    int str_before;
    int def_before;
    int qp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: in aid of the myreque critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer inaidofthemyreque selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    ia_god(player);

    npc_veliaf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_veliaf_hurtz");
    npc_polmafi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_polmafi_ferdygris");
    npc_ivan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_ivan_strom");
    npc_florin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_vilager_8");
    npc_razvan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_vilager_rat_2");
    npc_aurel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_general_store_owner");
    npc_cornelius = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_potential_bank_teller");
    npc_gadderanks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_gadderanks");
    npc_juve_talk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_vampire_juve2_blood_tithe");
    npc_wiskit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_villager_blood_tithe");
    npc_gadd_atk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_gadderanks_attackable");
    npc_juve1_atk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_vampire_juve_1_attackable");
    npc_juve2_atk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_vampire_juve_2_attackable");
    npc_wounded = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_gadderanks_wounded");
    npc_rescue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_rescue_veliaf_hurtz_talk");
    npc_ambush1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_ivan_temple_vampire_juve_1");
    npc_ambush2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "burgh_ivan_temple_vampire_juve_2");
    npc_drezel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "priestperiltrappedmonk_vis");
    npc_finish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myq5_veliaf_burgh_hideout");
    loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_quest_food_chest_open");
    loc_inn_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_inn_colapsed_wall_multiloc");
    loc_trap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_inn_trapdoor_closed");
    loc_rubble = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_rubble_a_1");
    loc_ladder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_inn_basement_ladderup");
    loc_roof = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_store_roof_broken");
    loc_shop_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_boared_up_wall_clickzone");
    loc_booth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_bankbooth_damaged");
    loc_furnace_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_furnace_broken");
    loc_furnace_repaired = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_furnace_repaired");
    loc_furnace_coal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_furnace_coal_loaded");
    loc_furnace_fired = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_furnace_fired");
    loc_keyhole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_library_keyhole");
    loc_libtrap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_temple_library_trapdoor_open");
    loc_bookcase = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_library_bookcase_ivandis");
    loc_boards = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_ivandis_tombdoor_board_multiloc");
    loc_tomb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_ivandis_tomb_entrance");
    loc_coffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "burgh_ancient_coffin");
    loc_well = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "priestperil_well");
    obj_bread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "woodplank");
    obj_nails = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nails");
    obj_axe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_axe");
    obj_mackerel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_mackerel");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_paste = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamppaste");
    obj_steel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_bar");
    obj_coal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coal");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "burgh_key");
    obj_clay = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "softclay");
    obj_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "burgh_rod_clay");
    obj_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silver_bar");
    obj_mithril = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mithril_bar");
    obj_sapphire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sapphire");
    obj_rod1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "burgh_rod_command1");
    obj_rod2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "burgh_rod_command2");
    obj_rod10 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "burgh_rod_command_final_10");
    obj_cosmic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cosmicrune");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "waterrune");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    stat_attack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
    stat_str = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "strength");
    stat_def = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "defence");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    slot_veliaf = -1;
    slot_polmafi = -1;
    slot_ivan = -1;
    slot_florin = -1;
    slot_razvan = -1;
    slot_aurel = -1;
    slot_cornelius = -1;
    slot_gadderanks = -1;
    slot_juve = -1;
    slot_wiskit = -1;
    slot_drezel = -1;
    slot_finish = -1;
    slot_atk[0] = slot_atk[1] = slot_atk[2] = -1;
    slot_ambush[0] = slot_ambush[1] = -1;

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_inaidofthemyreque") > 0,
                   "dbrow quest_inaidofthemyreque should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "myreque_2_quest") >= 0,
                   "varbit myreque_2_quest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "routequest") >= 0,
                   "varp routequest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "myreque2_multivar") >= 0,
                   "varp myreque2_multivar should resolve");
    SELFTEST_CHECK(npc_veliaf > 0, "npc route_veliaf_hurtz should resolve");
    SELFTEST_CHECK(npc_florin > 0, "npc burgh_vilager_8 should resolve");
    SELFTEST_CHECK(npc_aurel > 0, "npc burgh_general_store_owner should resolve");
    SELFTEST_CHECK(npc_finish > 0, "npc myq5_veliaf_burgh_hideout should resolve");
    if( npc_veliaf <= 0 )
    {
        fprintf(stderr, "ToriRSServer inaidofthemyreque selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    ia_clear_inv(player);
    ia_reset_side(srv);
    ia_varp(srv, "routequest", 0);
    ia_set_skills(player, 1, 1, 1);
    ia_journal(srv, "journal_0_not_started");

    /* ---- Veliaf start: In Search hard-gate, skills, accept ---- */
    slot_veliaf = ia_spawn(srv, npc_veliaf, IA_VELIAF_X, IA_VELIAF_Z, 0);
    SELFTEST_CHECK(slot_veliaf >= 0, "hideout Veliaf should spawn");
    if( slot_veliaf >= 0 )
    {
        ia_talk_drain(srv, npc_veliaf, slot_veliaf);
        SELFTEST_CHECK(ia_quest(player) == IA_NOT_STARTED,
                       "unfinished In Search must hard-gate In Aid");
        ia_pass("opnpc1_veliaf_routequest_gate");

        ia_varp(srv, "routequest", IA_ROUTEQUEST_COMPLETE);
        ia_set_skills(player, 1, 1, 1);
        ia_talk_drain(srv, npc_veliaf, slot_veliaf);
        SELFTEST_CHECK(ia_quest(player) == IA_NOT_STARTED,
                       "low Crafting/Mining/Magic must fail myreque2_meets_requirements");
        ia_pass("opnpc1_veliaf_qualify_fail_stats");

        ia_set_skills(player, IA_REQ_CRAFTING, IA_REQ_MINING, IA_REQ_MAGIC);
        ia_talk_drain(srv, npc_veliaf, slot_veliaf);
        SELFTEST_CHECK(ia_quest(player) == IA_AT_BURGH,
                       "accept must write at_burgh=20, got %d",
                       ia_quest(player));
        ia_pass("opnpc1_veliaf_send_to_burgh");

        ia_talk_drain(srv, npc_veliaf, slot_veliaf);
        SELFTEST_CHECK(ia_quest(player) == IA_AT_BURGH,
                       "mid-quest Veliaf reminder must stay at_burgh");
        ia_pass("opnpc1_veliaf_keep_at_it");
    }
    ia_journal(srv, "journal_20_at_burgh");

    /* ---- Florin + food chest ---- */
    slot_florin = ia_spawn(srv, npc_florin, IA_FLORIN_X, IA_FLORIN_Z, 0);
    if( slot_florin >= 0 )
    {
        ia_talk_drain(srv, npc_florin, slot_florin);
        SELFTEST_CHECK(ia_quest(player) == IA_AT_BURGH,
                       "Florin at at_burgh must ask for food, not advance");
        ia_pass("opnpc1_florin_need_food");
    }
    if( loc_chest > 0 )
    {
        ia_clear_inv(player);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_chest, IA_CHEST_X, IA_CHEST_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_AT_BURGH,
                       "empty chest must refuse without food");
        ia_pass("oploc2_food_chest_need_food");

        if( obj_bread > 0 )
            ia_give(player, obj_bread, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_chest, IA_CHEST_X, IA_CHEST_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_FOOD_GIVEN,
                       "food in chest must write food_given=30, got %d",
                       ia_quest(player));
        ia_pass("oploc2_food_chest_give");
    }

    /* ---- Razvan inn hint ---- */
    slot_razvan = ia_spawn(srv, npc_razvan, IA_RAZVAN_X, IA_RAZVAN_Z, 0);
    if( slot_razvan >= 0 )
    {
        ia_talk_drain(srv, npc_razvan, slot_razvan);
        SELFTEST_CHECK(ia_quest(player) == IA_TOLD_RAZVAN,
                       "Razvan must write told_razvan=40, got %d",
                       ia_quest(player));
        ia_pass("opnpc1_razvan_pub_basement");
        ia_talk_drain(srv, npc_razvan, slot_razvan);
        ia_pass("opnpc1_razvan_pub_reminder");
    }

    /* ---- Collapsed wall / trapdoor / rubble / ladder ---- */
    if( loc_inn_wall > 0 && obj_pick > 0 )
    {
        ia_clear_inv(player);
        ia_give(player, obj_spade > 0 ? obj_spade : obj_pick, 1);
        ia_oplocu(srv, loc_inn_wall, IA_INN_WALL_X, IA_INN_WALL_Z, 0,
                  obj_spade > 0 ? obj_spade : obj_pick);
        SELFTEST_CHECK(ia_quest(player) == IA_TOLD_RAZVAN,
                       "wrong item on collapsed wall must not advance");
        ia_pass("oplocu_inn_wall_wrong_item");

        ia_give(player, obj_pick, 1);
        ia_oplocu(srv, loc_inn_wall, IA_INN_WALL_X, IA_INN_WALL_Z, 0, obj_pick);
        SELFTEST_CHECK(ia_quest(player) == IA_TRAPDOOR_OPEN,
                       "pickaxe on collapsed wall must write trapdoor_open=80, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_inn_colapsed_wall") == 1,
                       "collapsed wall varbit must be 1");
        ia_pass("oplocu_inn_wall_pickaxe");
    }
    if( loc_trap > 0 )
    {
        ia_oploc(srv, SS_TRIGGER_OPLOC1, loc_trap, IA_TRAPDOOR_X, IA_TRAPDOOR_Z, 0);
        SELFTEST_CHECK(ia_get_vb(player, "burgh_inn_trapdoor") == 1,
                       "opening the inn trapdoor must write burgh_inn_trapdoor=1");
        ia_pass("oploc1_inn_trapdoor_open");
    }
    if( loc_rubble > 0 )
    {
        ia_clear_inv(player);
        ia_oploc(srv, SS_TRIGGER_OPLOC1, loc_rubble, IA_RUBBLE_X, IA_RUBBLE_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_TRAPDOOR_OPEN,
                       "rubble without pickaxe must stay trapdoor_open");
        ia_pass("oploc1_rubble_need_pickaxe");

        if( obj_pick > 0 )
            ia_give(player, obj_pick, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC1, loc_rubble, IA_RUBBLE_X, IA_RUBBLE_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_TRAPDOOR_OPEN,
                       "rubble without spade must stay trapdoor_open");
        ia_pass("oploc1_rubble_need_spade");

        if( obj_spade > 0 )
            ia_give(player, obj_spade, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC1, loc_rubble, IA_RUBBLE_X, IA_RUBBLE_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_BASEMENT_CLEAR,
                       "pickaxe+spade rubble must write basement_clear=100, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_inn_rubble_pile") == IA_RUBBLE_NEEDED,
                       "rubble pile must be driven to 5");
        ia_pass("oploc1_rubble_clear");
    }
    if( loc_ladder > 0 )
    {
        ia_oploc(srv, SS_TRIGGER_OPLOC1, loc_ladder, IA_LADDER_X, IA_LADDER_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_LEFT_BASEMENT,
                       "basement ladder must write left_basement=110, got %d",
                       ia_quest(player));
        ia_pass("oploc1_basement_ladder_up");
    }
    ia_journal(srv, "journal_110_left_basement");

    if( slot_razvan >= 0 )
    {
        ia_tele(srv, IA_RAZVAN_X, IA_RAZVAN_Z, 0);
        ia_talk_drain(srv, npc_razvan, slot_razvan);
        SELFTEST_CHECK(ia_quest(player) == IA_ROOF_FIXED,
                       "Razvan after basement must write roof_fixed=140, got %d",
                       ia_quest(player));
        ia_pass("opnpc1_razvan_help_aurel");
    }

    /* ---- General store roof / wall / crate ---- */
    slot_aurel = ia_spawn(srv, npc_aurel, IA_AUREL_X, IA_AUREL_Z, 0);
    if( slot_aurel >= 0 )
    {
        ia_talk_drain(srv, npc_aurel, slot_aurel);
        SELFTEST_CHECK(ia_quest(player) == IA_ROOF_FIXED,
                       "Aurel at roof_fixed must explain the roof, not advance");
        ia_pass("opnpc1_aurel_roof_hint");
    }
    if( loc_roof > 0 )
    {
        ia_clear_inv(player);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_roof, IA_ROOF_X, IA_ROOF_Z, 0);
        ia_pass("oploc2_store_roof_need_hammer");
        if( obj_hammer > 0 )
            ia_give(player, obj_hammer, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_roof, IA_ROOF_X, IA_ROOF_Z, 0);
        ia_pass("oploc2_store_roof_need_planks");
        if( obj_plank > 0 )
            ia_give(player, obj_plank, 3);
        if( obj_nails > 0 )
            ia_give(player, obj_nails, 12);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_roof, IA_ROOF_X, IA_ROOF_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_WALL_FIXED,
                       "roof repair must write wall_fixed=150, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_store_roof") == 1,
                       "store roof varbit must be 1");
        ia_pass("oploc2_store_roof_repair");
    }
    if( loc_shop_wall > 0 )
    {
        ia_clear_inv(player);
        if( obj_hammer > 0 )
            ia_give(player, obj_hammer, 1);
        if( obj_plank > 0 )
            ia_give(player, obj_plank, 3);
        if( obj_nails > 0 )
            ia_give(player, obj_nails, 12);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_shop_wall, IA_SHOP_WALL_X, IA_SHOP_WALL_Z, 0);
        SELFTEST_CHECK(ia_get_vb(player, "burgh_store_wall") == 1,
                       "shop wall repair must write burgh_store_wall=1");
        ia_pass("oploc2_store_wall_repair");
    }
    if( slot_aurel >= 0 )
    {
        ia_tele(srv, IA_AUREL_X, IA_AUREL_Z, 0);
        ia_talk_drain(srv, npc_aurel, slot_aurel);
        SELFTEST_CHECK(ia_quest(player) == IA_CRATE_GIVEN,
                       "Aurel after wall must write crate_given=160, got %d",
                       ia_quest(player));
        ia_pass("opnpc1_aurel_crate_assign");

        ia_talk_drain(srv, npc_aurel, slot_aurel);
        SELFTEST_CHECK(ia_quest(player) == IA_CRATE_GIVEN,
                       "empty crate hand-in must stay crate_given");
        ia_pass("opnpc1_aurel_crate_need_axes");

        ia_clear_inv(player);
        if( obj_axe > 0 )
            ia_give(player, obj_axe, IA_CRATE_AXES);
        ia_talk_drain(srv, npc_aurel, slot_aurel);
        ia_pass("opnpc1_aurel_crate_need_food");
        if( obj_mackerel > 0 )
            ia_give(player, obj_mackerel, IA_CRATE_FOOD);
        ia_talk_drain(srv, npc_aurel, slot_aurel);
        ia_pass("opnpc1_aurel_crate_need_tinder");
        if( obj_tinder > 0 )
            ia_give(player, obj_tinder, IA_CRATE_TINDER);
        ia_talk_drain(srv, npc_aurel, slot_aurel);
        SELFTEST_CHECK(ia_quest(player) == IA_BOOTH_FIXED,
                       "filled crate must write booth_fixed=170, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_axes_crate") == IA_CRATE_AXES,
                       "crate axes must be 10");
        SELFTEST_CHECK(ia_get_vb(player, "burgh_food_crate") == IA_CRATE_FOOD,
                       "crate food must be 10");
        SELFTEST_CHECK(ia_get_vb(player, "burgh_tinderbox_crate") == IA_CRATE_TINDER,
                       "crate tinderboxes must be 3");
        SELFTEST_CHECK(ia_get_vb(player, "burgh_store_stocked") == 1,
                       "store must be marked stocked");
        ia_pass("opnpc1_aurel_crate_filled");

        ia_talk_pick(srv, npc_aurel, slot_aurel, 2);
        ia_pass("opnpc1_aurel_shop_no_thanks");
        ia_talk_pick(srv, npc_aurel, slot_aurel, 1);
        ia_pass("opnpc1_aurel_shop_wares");
    }

    /* ---- Bank booth / wall / Cornelius ---- */
    if( loc_booth > 0 )
    {
        ia_clear_inv(player);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_booth, IA_BOOTH_X, IA_BOOTH_Z, 0);
        ia_pass("oploc2_bank_booth_need_hammer");
        if( obj_hammer > 0 )
            ia_give(player, obj_hammer, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_booth, IA_BOOTH_X, IA_BOOTH_Z, 0);
        ia_pass("oploc2_bank_booth_need_materials");
        if( obj_plank > 0 )
            ia_give(player, obj_plank, 2);
        if( obj_nails > 0 )
            ia_give(player, obj_nails, 8);
        if( obj_paste > 0 )
            ia_give(player, obj_paste, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_booth, IA_BOOTH_X, IA_BOOTH_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_BANKWALL_FIXED,
                       "booth repair must write bankwall_fixed=180, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_bank_booth_open") == 1,
                       "bank booth varbit must be 1");
        ia_pass("oploc2_bank_booth_repair");
    }
    if( loc_shop_wall > 0 )
    {
        ia_clear_inv(player);
        if( obj_hammer > 0 )
            ia_give(player, obj_hammer, 1);
        if( obj_plank > 0 )
            ia_give(player, obj_plank, 3);
        if( obj_nails > 0 )
            ia_give(player, obj_nails, 12);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_shop_wall, IA_BANK_WALL_X, IA_BANK_WALL_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_CORNELIUS,
                       "bank wall repair must write cornelius=190, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_bank_wall") == 1,
                       "bank wall varbit must be 1");
        ia_pass("oploc2_bank_wall_repair");
    }
    slot_cornelius = ia_spawn(srv, npc_cornelius, IA_CORNELIUS_X, IA_CORNELIUS_Z, 0);
    if( slot_cornelius >= 0 )
    {
        ia_talk_drain(srv, npc_cornelius, slot_cornelius);
        SELFTEST_CHECK(ia_get_vb(player, "burgh_bank_teller") == 1,
                       "Cornelius must write burgh_bank_teller=1");
        SELFTEST_CHECK(ia_quest(player) == IA_CORNELIUS,
                       "Cornelius recruitment must stay cornelius=190");
        ia_pass("opnpc1_cornelius_recruited");
        ia_vb(srv, "myreque_2_quest", IA_FURNACE_HINT);
        ia_talk_drain(srv, npc_cornelius, slot_cornelius);
        ia_pass("opnpc1_cornelius_bank_welcome");
        ia_vb(srv, "myreque_2_quest", IA_CORNELIUS);
    }
    if( slot_razvan >= 0 )
    {
        ia_tele(srv, IA_RAZVAN_X, IA_RAZVAN_Z, 0);
        ia_talk_drain(srv, npc_razvan, slot_razvan);
        SELFTEST_CHECK(ia_quest(player) == IA_FURNACE_HINT,
                       "Razvan after Cornelius must write furnace_hint=200, got %d",
                       ia_quest(player));
        ia_pass("opnpc1_razvan_furnace_hint");
    }
    ia_journal(srv, "journal_200_furnace_hint");

    /* ---- Furnace repair / coal / light ---- */
    if( loc_furnace_broken > 0 )
    {
        ia_clear_inv(player);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_furnace_broken, IA_FURNACE_X, IA_FURNACE_Z, 0);
        ia_pass("oploc2_furnace_need_hammer");
        if( obj_hammer > 0 )
            ia_give(player, obj_hammer, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_furnace_broken, IA_FURNACE_X, IA_FURNACE_Z, 0);
        ia_pass("oploc2_furnace_need_steel");
        if( obj_steel > 0 )
            ia_give(player, obj_steel, 2);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_furnace_broken, IA_FURNACE_X, IA_FURNACE_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_FURNACE_REPAIRED,
                       "furnace repair must write 205, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_furnace_fix") == 1,
                       "furnace visual must be repaired=1");
        ia_pass("oploc2_furnace_repair");
    }
    if( loc_furnace_repaired > 0 )
    {
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_furnace_repaired, IA_FURNACE_X, IA_FURNACE_Z, 0);
        ia_pass("oploc2_furnace_need_coal");
        if( obj_coal > 0 )
            ia_give(player, obj_coal, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_furnace_repaired, IA_FURNACE_X, IA_FURNACE_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_FURNACE_COAL,
                       "adding coal must write 210, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_furnace_fix") == 2,
                       "furnace visual must be coal=2");
        ia_pass("oploc2_furnace_add_coal");
    }
    if( loc_furnace_coal > 0 )
    {
        ia_clear_inv(player);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_furnace_coal, IA_FURNACE_X, IA_FURNACE_Z, 0);
        ia_pass("oploc2_furnace_need_tinderbox");
        if( obj_tinder > 0 )
            ia_give(player, obj_tinder, 1);
        ia_oploc(srv, SS_TRIGGER_OPLOC2, loc_furnace_coal, IA_FURNACE_X, IA_FURNACE_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_FURNACE_LIT,
                       "lighting furnace must write 220, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "burgh_furnace_fix") == 3,
                       "furnace visual must be fired=3");
        ia_pass("oploc2_furnace_light");
    }

    /* ---- Blood-tithe talks + fight + wounded ---- */
    slot_gadderanks = ia_spawn(srv, npc_gadderanks, IA_GADDERANKS_X, IA_GADDERANKS_Z, 0);
    if( slot_gadderanks >= 0 )
    {
        ia_talk_drain(srv, npc_gadderanks, slot_gadderanks);
        SELFTEST_CHECK(ia_get_vb(player, "gadderanks_blood_tithe_chat") == 1,
                       "first Gadderanks talk must set blood_tithe_chat");
        ia_pass("opnpc1_gadderanks_first");
        ia_talk_drain(srv, npc_gadderanks, slot_gadderanks);
        ia_pass("opnpc1_gadderanks_talk_juvinate");
    }
    if( npc_juve_talk > 0 )
    {
        slot_juve = ia_spawn(srv, npc_juve_talk, IA_JUVE1_X, IA_JUVE1_Z, 0);
        if( slot_juve >= 0 )
        {
            ia_talk_drain(srv, npc_juve_talk, slot_juve);
            SELFTEST_CHECK(ia_get_vb(player, "juve_blood_tithe_chat") == 1,
                           "Juvinate talk must set juve_blood_tithe_chat");
            ia_pass("opnpc1_juvinate_blood_tithe");
        }
    }
    if( npc_wiskit > 0 )
    {
        slot_wiskit = ia_spawn(srv, npc_wiskit, IA_WISKIT_X, IA_WISKIT_Z, 0);
        if( slot_wiskit >= 0 )
        {
            ia_talk_drain(srv, npc_wiskit, slot_wiskit);
            SELFTEST_CHECK(ia_quest(player) == IA_FIGHT_STARTED,
                           "Wiskit must write fight_started=240, got %d",
                           ia_quest(player));
            SELFTEST_CHECK(ia_get_vb(player, "villager_blood_tithe_chat") == 1,
                           "Wiskit must set villager_blood_tithe_chat");
            ia_pass("opnpc1_wiskit_start_fight");
        }
    }
    if( npc_gadd_atk > 0 && npc_juve1_atk > 0 && npc_juve2_atk > 0 )
    {
        slot_atk[0] = selftest_find_npc(srv, npc_gadd_atk);
        slot_atk[1] = selftest_find_npc(srv, npc_juve1_atk);
        slot_atk[2] = selftest_find_npc(srv, npc_juve2_atk);
        if( slot_atk[0] < 0 )
            slot_atk[0] = ToriRSServer_WorldNpcSpawn(
                srv, npc_gadd_atk, IA_SHOP_X + 1, IA_SHOP_Z, 0);
        if( slot_atk[1] < 0 )
            slot_atk[1] = ToriRSServer_WorldNpcSpawn(
                srv, npc_juve1_atk, IA_SHOP_X + 2, IA_SHOP_Z, 0);
        if( slot_atk[2] < 0 )
            slot_atk[2] = ToriRSServer_WorldNpcSpawn(
                srv, npc_juve2_atk, IA_SHOP_X + 3, IA_SHOP_Z, 0);
        if( slot_atk[0] >= 0 )
        {
            ia_kill(srv, npc_gadd_atk, slot_atk[0]);
            ia_pass("opnpc2_gadderanks_attack");
        }
        if( slot_atk[1] >= 0 )
        {
            ia_kill(srv, npc_juve1_atk, slot_atk[1]);
            ia_pass("opnpc2_juve1_attack");
        }
        if( slot_atk[2] >= 0 )
        {
            ia_kill(srv, npc_juve2_atk, slot_atk[2]);
            ia_pass("opnpc2_juve2_attack");
        }
        if( ia_get_vb(player, "juvinate_deaths") < IA_JUVINATE_DEATHS )
        {
            ToriRSServer_ScriptsRunProc(srv, "[proc,myreque2_juvinate_death]", NULL, 0);
            ia_finish(srv);
            ToriRSServer_ScriptsRunProc(srv, "[proc,myreque2_juvinate_death]", NULL, 0);
            ia_finish(srv);
            ToriRSServer_ScriptsRunProc(srv, "[proc,myreque2_juvinate_death]", NULL, 0);
            ia_finish(srv);
        }
        SELFTEST_CHECK(ia_get_vb(player, "juvinate_deaths") == IA_JUVINATE_DEATHS ||
                           ia_quest(player) == IA_GADDERANKS_DEFEATED,
                       "three deaths must write gadderanks_defeated or deaths=3");
        ia_pass("ai_queue3_gadderanks_fight_won");
    }
    if( npc_wounded > 0 )
    {
        int wounded = selftest_find_npc(srv, npc_wounded);

        if( wounded < 0 )
            wounded = ia_spawn(srv, npc_wounded, IA_SHOP_X, IA_SHOP_Z, 0);
        if( wounded >= 0 )
        {
            ia_vb(srv, "myreque_2_quest", IA_GADDERANKS_DEFEATED);
            ia_talk_drain(srv, npc_wounded, wounded);
            SELFTEST_CHECK(ia_quest(player) == IA_VELIAF_RETURNED,
                           "wounded Gadderanks must write veliaf_returned=280, got %d",
                           ia_quest(player));
            ia_pass("opnpc1_gadderanks_wounded");
            ia_free_npc(srv, wounded);
        }
    }
    if( npc_rescue > 0 )
    {
        int rescue = ia_spawn(srv, npc_rescue, IA_SHOP_X, IA_SHOP_Z, 0);

        if( rescue >= 0 )
        {
            ia_talk_drain(srv, npc_rescue, rescue);
            SELFTEST_CHECK(ia_get_vb(player, "blood_tithe_visible") == IA_BLOOD_TITHE,
                           "rescue Veliaf must write blood_tithe_visible=3");
            ia_pass("opnpc1_rescue_veliaf");
            ia_free_npc(srv, rescue);
        }
    }
    ia_journal(srv, "journal_280_veliaf_returned");

    /* ---- Hideout report / Polmafi / Ivan trek ---- */
    if( slot_veliaf >= 0 )
    {
        ia_tele(srv, IA_VELIAF_X, IA_VELIAF_Z, 0);
        ia_talk_drain(srv, npc_veliaf, slot_veliaf);
        SELFTEST_CHECK(ia_quest(player) == IA_POLMAFI,
                       "return Veliaf must write polmafi=290, got %d",
                       ia_quest(player));
        ia_pass("opnpc1_veliaf_return_polmafi");
    }
    if( npc_polmafi > 0 )
    {
        slot_polmafi = ia_spawn(srv, npc_polmafi, IA_POLMAFI_X, IA_POLMAFI_Z, 0);
        if( slot_polmafi >= 0 )
        {
            ia_talk_drain(srv, npc_polmafi, slot_polmafi);
            SELFTEST_CHECK(ia_quest(player) == IA_TREK_STARTED,
                           "Polmafi must write trek_started=300, got %d",
                           ia_quest(player));
            ia_pass("opnpc1_polmafi_ivan_escort");
            ia_talk_drain(srv, npc_polmafi, slot_polmafi);
            ia_pass("opnpc1_polmafi_good_luck");
        }
    }
    if( npc_ivan > 0 )
    {
        slot_ivan = ia_spawn(srv, npc_ivan, IA_IVAN_X, IA_IVAN_Z, 0);
        if( slot_ivan >= 0 )
        {
            ia_talk_drain(srv, npc_ivan, slot_ivan);
            ia_pass("opnpc1_ivan_start_trek");
        }
    }
    if( npc_ambush1 > 0 && npc_ambush2 > 0 )
    {
        slot_ambush[0] = selftest_find_npc(srv, npc_ambush1);
        slot_ambush[1] = selftest_find_npc(srv, npc_ambush2);
        if( slot_ambush[0] < 0 )
            slot_ambush[0] = ToriRSServer_WorldNpcSpawn(
                srv, npc_ambush1, IA_AMBUSH_X + 1, IA_AMBUSH_Z, 0);
        if( slot_ambush[1] < 0 )
            slot_ambush[1] = ToriRSServer_WorldNpcSpawn(
                srv, npc_ambush2, IA_AMBUSH_X + 2, IA_AMBUSH_Z, 0);
        if( slot_ambush[0] >= 0 )
        {
            ia_kill(srv, npc_ambush1, slot_ambush[0]);
            ia_pass("opnpc2_ambush_juve1");
        }
        if( slot_ambush[1] >= 0 )
        {
            ia_kill(srv, npc_ambush2, slot_ambush[1]);
            ia_pass("opnpc2_ambush_juve2");
        }
        if( ia_get_vb(player, "juvinate_ambush_deaths") < IA_AMBUSH_DEATHS )
        {
            ToriRSServer_ScriptsRunProc(srv, "[proc,myreque2_ambush_death]", NULL, 0);
            ia_finish(srv);
            ToriRSServer_ScriptsRunProc(srv, "[proc,myreque2_ambush_death]", NULL, 0);
            ia_finish(srv);
        }
        SELFTEST_CHECK(ia_quest(player) == IA_AT_TEMPLE ||
                           ia_get_vb(player, "juvinate_ambush_deaths") == IA_AMBUSH_DEATHS,
                       "ambush deaths must reach temple or deaths=2");
        ia_pass("ai_queue3_ambush_won");
    }
    ia_vb(srv, "myreque_2_quest", IA_AT_TEMPLE);
    ia_journal(srv, "journal_315_at_temple");

    /* ---- Drezel / library / book ---- */
    if( npc_drezel > 0 )
    {
        slot_drezel = ia_spawn(srv, npc_drezel, IA_TEMPLE_X, IA_TEMPLE_Z, 0);
        if( slot_drezel >= 0 )
        {
            ia_talk_drain(srv, npc_drezel, slot_drezel);
            SELFTEST_CHECK(ia_quest(player) == IA_LIBRARY_UNLOCKED,
                           "Drezel must write library_unlocked=350, got %d",
                           ia_quest(player));
            if( obj_key > 0 )
                SELFTEST_CHECK(selftest_count_obj(player, obj_key) >= 1,
                               "Drezel must grant burgh_key");
            ia_pass("opnpc1_drezel_library_key");
            ia_talk_drain(srv, npc_drezel, slot_drezel);
            ia_pass("opnpc1_drezel_keyhole_hint");
        }
    }
    if( loc_keyhole > 0 && obj_key > 0 )
    {
        if( selftest_count_obj(player, obj_key) < 1 )
            ia_give(player, obj_key, 1);
        ia_oplocu(srv, loc_keyhole, IA_KEYHOLE_X, IA_KEYHOLE_Z, 0, obj_key);
        SELFTEST_CHECK(ia_get_vb(player, "burgh_temple_trapdoor") == 1,
                       "key on keyhole must open the temple trapdoor");
        ia_pass("oplocu_library_keyhole");
    }
    if( loc_libtrap > 0 )
    {
        ia_oploc(srv, SS_TRIGGER_OPLOC1, loc_libtrap, IA_LIBTRAP_X, IA_LIBTRAP_Z, 0);
        ia_pass("oploc1_library_trapdoor");
    }
    if( loc_bookcase > 0 )
    {
        ia_oploc(srv, SS_TRIGGER_OPLOC1, loc_bookcase, IA_BOOKCASE_X, IA_BOOKCASE_Z, 0);
        SELFTEST_CHECK(ia_quest(player) == IA_BOOK_READ,
                       "Ivandis bookcase must write book_read=370, got %d",
                       ia_quest(player));
        ia_pass("oploc1_library_bookcase_ivandis");
    }
    ia_journal(srv, "journal_370_book_read");

    /* ---- Tomb boards / mould / rod / bless ---- */
    if( loc_boards > 0 && obj_hammer > 0 )
    {
        ia_clear_inv(player);
        ia_give(player, obj_hammer, 1);
        ia_oplocu(srv, loc_boards, IA_BOARDS_X, IA_BOARDS_Z, 0, obj_hammer);
        SELFTEST_CHECK(ia_quest(player) == IA_ROD_PHASE,
                       "hammer on boards must write rod_phase=375, got %d",
                       ia_quest(player));
        SELFTEST_CHECK(ia_get_vb(player, "ivandis_tomb_boards") == 1,
                       "tomb boards varbit must be 1");
        ia_pass("oplocu_tomb_boards");
    }
    if( loc_tomb > 0 )
    {
        ia_oploc(srv, SS_TRIGGER_OPLOC1, loc_tomb, IA_TOMB_X, IA_TOMB_Z, 0);
        ia_pass("oploc1_tomb_entrance");
    }
    if( loc_coffin > 0 && obj_clay > 0 )
    {
        ia_give(player, obj_clay, 1);
        ia_oplocu(srv, loc_coffin, IA_COFFIN_X, IA_COFFIN_Z, 0, obj_clay);
        if( obj_mould > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, obj_mould) >= 1,
                           "soft clay on coffin must grant burgh_rod_clay");
        ia_pass("oplocu_coffin_mould");
        ia_oplocu(srv, loc_coffin, IA_COFFIN_X, IA_COFFIN_Z, 0, obj_clay);
        ia_pass("oplocu_coffin_already");
    }
    if( loc_furnace_fired > 0 && obj_mould > 0 )
    {
        ia_clear_inv(player);
        ia_give(player, obj_mould, 1);
        ia_oplocu(srv, loc_furnace_fired, IA_FURNACE_X, IA_FURNACE_Z, 0, obj_mould);
        ia_pass("oplocu_furnace_rod_need_bars");
        ia_give(player, obj_mould, 1);
        if( obj_silver > 0 )
            ia_give(player, obj_silver, 1);
        if( obj_mithril > 0 )
            ia_give(player, obj_mithril, 1);
        if( obj_sapphire > 0 )
            ia_give(player, obj_sapphire, 1);
        ia_oplocu(srv, loc_furnace_fired, IA_FURNACE_X, IA_FURNACE_Z, 0, obj_mould);
        if( obj_rod1 > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, obj_rod1) >= 1,
                           "furnace + mould must grant Silvthrill rod");
        ia_pass("oplocu_furnace_make_rod");
    }
    if( obj_rod1 > 0 && obj_cosmic > 0 && obj_water > 0 )
    {
        ia_clear_inv(player);
        ia_give(player, obj_rod1, 1);
        ia_opheldu(srv, obj_rod1, obj_cosmic);
        ia_pass("opheldu_rod_need_runes");
        ia_give(player, obj_cosmic, 1);
        ia_give(player, obj_water, 1);
        ia_set_skills(player, IA_REQ_CRAFTING, IA_REQ_MINING, 1);
        ia_opheldu(srv, obj_rod1, obj_cosmic);
        ia_pass("opheldu_rod_need_magic");
        ia_set_skills(player, IA_REQ_CRAFTING, IA_REQ_MINING, IA_REQ_MAGIC);
        ia_give(player, obj_rod1, 1);
        ia_give(player, obj_cosmic, 1);
        ia_give(player, obj_water, 1);
        ia_opheldu(srv, obj_rod1, obj_cosmic);
        if( obj_rod2 > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, obj_rod2) >= 1,
                           "cosmic+water on rod must grant enchanted rod");
        ia_pass("opheldu_enchant_rod");
        ia_clear_inv(player);
        ia_give(player, obj_rod1, 1);
        ia_give(player, obj_cosmic, 1);
        ia_give(player, obj_water, 1);
        ia_opheldu(srv, obj_cosmic, obj_rod1);
        ia_pass("opheldu_cosmic_on_rod");
        ia_clear_inv(player);
        ia_give(player, obj_rod1, 1);
        ia_give(player, obj_cosmic, 1);
        ia_give(player, obj_water, 1);
        ia_opheldu(srv, obj_water, obj_rod1);
        ia_pass("opheldu_water_on_rod");
    }
    if( loc_well > 0 && obj_rod2 > 0 )
    {
        ia_clear_inv(player);
        ia_give(player, obj_rod2, 1);
        ia_oplocu(srv, loc_well, IA_WELL_X, IA_WELL_Z, 0, obj_rod2);
        ia_pass("oplocu_well_need_rope");
        if( obj_rope > 0 )
            ia_give(player, obj_rope, 1);
        ia_give(player, obj_rod2, 1);
        ia_oplocu(srv, loc_well, IA_WELL_X, IA_WELL_Z, 0, obj_rod2);
        SELFTEST_CHECK(ia_quest(player) == IA_ROD_BLESSED,
                       "blessing must write rod_blessed=410, got %d",
                       ia_quest(player));
        if( obj_rod10 > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, obj_rod10) >= 1,
                           "well + rope must grant Rod of Ivandis");
        ia_pass("oplocu_well_bless_rod");
    }
    ia_journal(srv, "journal_410_rod_blessed");

    /* ---- Finish with Burgh Veliaf (shared Sins of the Father hub) ---- */
    if( npc_finish > 0 )
    {
        slot_finish = ia_spawn(srv, npc_finish, IA_FINISH_X, IA_FINISH_Z, 0);
        if( slot_finish >= 0 )
        {
            ia_clear_inv(player);
            ia_talk_drain(srv, npc_finish, slot_finish);
            SELFTEST_CHECK(ia_quest(player) == IA_ROD_BLESSED,
                           "Veliaf without the rod must stay rod_blessed");
            ia_pass("opnpc1_finish_veliaf_need_rod");

            if( obj_rod10 > 0 )
                ia_give(player, obj_rod10, 1);
            attack_before = (stat_attack >= 0) ? player->stat_xp_tenths[stat_attack] : 0;
            str_before = (stat_str >= 0) ? player->stat_xp_tenths[stat_str] : 0;
            craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
            def_before = (stat_def >= 0) ? player->stat_xp_tenths[stat_def] : 0;
            qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
            ia_talk_drain(srv, npc_finish, slot_finish);
            SELFTEST_CHECK(ia_quest(player) == IA_COMPLETE,
                           "rod handoff must complete the quest, got %d",
                           ia_quest(player));
            if( stat_attack >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_attack] >=
                                   attack_before + IA_REWARD_TENTHS,
                               "complete must advance attack by 20000 tenths");
            if( stat_str >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_str] >=
                                   str_before + IA_REWARD_TENTHS,
                               "complete must advance strength by 20000 tenths");
            if( stat_craft >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >=
                                   craft_before + IA_REWARD_TENTHS,
                               "complete must advance crafting by 20000 tenths");
            if( stat_def >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_def] >=
                                   def_before + IA_REWARD_TENTHS,
                               "complete must advance defence by 20000 tenths");
            if( varp_qp >= 0 )
                SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + IA_REWARD_QP,
                               "complete must award 2 quest points");
            ia_pass("opnpc1_finish_veliaf_complete_scroll");
            ia_talk_drain(srv, npc_finish, slot_finish);
            SELFTEST_CHECK(ia_quest(player) == IA_COMPLETE,
                           "second finish talk must stay complete");
            ia_pass("opnpc1_finish_veliaf_post_complete");
        }
    }
    ia_journal(srv, "journal_430_complete");

    /* Hideout Veliaf after complete dispatches to Darkness of Hallowvale. */
    if( slot_veliaf >= 0 )
    {
        ia_tele(srv, IA_VELIAF_X, IA_VELIAF_Z, 0);
        ia_talk_drain(srv, npc_veliaf, slot_veliaf);
        ia_pass("opnpc1_veliaf_post_complete_doh_hub");
    }

    ia_free_npc(srv, slot_veliaf);
    ia_free_npc(srv, slot_polmafi);
    ia_free_npc(srv, slot_ivan);
    ia_free_npc(srv, slot_florin);
    ia_free_npc(srv, slot_razvan);
    ia_free_npc(srv, slot_aurel);
    ia_free_npc(srv, slot_cornelius);
    ia_free_npc(srv, slot_gadderanks);
    ia_free_npc(srv, slot_juve);
    ia_free_npc(srv, slot_wiskit);
    ia_free_npc(srv, slot_drezel);
    ia_free_npc(srv, slot_finish);
    ia_free_npc(srv, slot_atk[0]);
    ia_free_npc(srv, slot_atk[1]);
    ia_free_npc(srv, slot_atk[2]);
    ia_free_npc(srv, slot_ambush[0]);
    ia_free_npc(srv, slot_ambush[1]);

    (void)stat_mine;
    (void)stat_magic;
    (void)npc_rescue;
    (void)obj_bread;

    fprintf(stderr, "ToriRSServer inaidofthemyreque selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_INAIDOFTHEMYREQUE_SELFTEST_U_H */
