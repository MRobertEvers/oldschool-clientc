#ifndef TORIRSSERVER_TEST_QUEST_THEFREMENNIKISLES_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_THEFREMENNIKISLES_SELFTEST_U_H

/* The Fremennik Isles Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Gjuki / Mord / Maria / Slug / Mawnis /
 * tax / cat / king locs cannot leak. Real OPNPC1 on the authored
 * [opnpc1,fris_r_king] (merged start refuse/accept), ferrymen, tax
 * merchants, Slug, Mawnis. No second Gjuki / Mawnis / Slug / ferryman /
 * tax header. player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_FRIS_ONLY=1
 * Also accepts TORIRSSERVER_SELFTEST_FREMENNIKISLES=1.
 */

#define FRIS_NOT_STARTED 0
#define FRIS_TOLD_OF_QUEST 1
#define FRIS_GJUKI_DIALOG1 2
#define FRIS_NEED_ORE 3
#define FRIS_ORE_DELIVERED 4
#define FRIS_GET_JESTER_OUTFIT 5
#define FRIS_SPY_ON_MAWNIS_1 6
#define FRIS_REPORT_SLUG_1 7
#define FRIS_HELP_MAWNIS 8
#define FRIS_BRING_BRIDGE_ITEMS 9
#define FRIS_BRIDGE_ITEMS_DELIVERED 10
#define FRIS_REPAIR_BRIDGES 11
#define FRIS_BRIDGES_REPAIRED 12
#define FRIS_REPORT_GJUKI_BRIDGES 13
#define FRIS_COLLECT_WINDOW_TAX 14
#define FRIS_COLLECT_BEARD_TAX 15
#define FRIS_RETURN_TO_SPY_AGAIN 16
#define FRIS_SPY_ON_MAWNIS_2 17
#define FRIS_REPORT_SLUG_2 18
#define FRIS_REPORT_GJUKI_2 19
#define FRIS_DELIVER_DECREE 20
#define FRIS_DECREE_DELIVERED 21
#define FRIS_MAKE_ARMOUR 22
#define FRIS_MAKE_SHIELD 23
#define FRIS_KILL_TROLLS 24
#define FRIS_DECAPITATE_KING 25
#define FRIS_FINISH_QUEST 26
#define FRIS_COMPLETE 340

#define FRIS_VIKING_COMPLETE 10
#define FRIS_REQ_AGILITY 40
#define FRIS_REQ_CONSTRUCTION 20
#define FRIS_TASK_NEEDED 10
#define FRIS_REWARD_CONSTRUCTION_TENTHS 50000
#define FRIS_REWARD_CRAFTING_TENTHS 50000
#define FRIS_REWARD_WOODCUTTING_TENTHS 100000
#define FRIS_REWARD_QP 1
#define FRIS_REWARD_LAMP_QTY 2

#define FRIS_MORD_X 2644
#define FRIS_MORD_Z 3709
#define FRIS_MARIA_X 2644
#define FRIS_MARIA_Z 3710
#define FRIS_GJUKI_X 2407
#define FRIS_GJUKI_Z 3804
#define FRIS_SLUG_X 2335
#define FRIS_SLUG_Z 3811
#define FRIS_MAWNIS_X 2335
#define FRIS_MAWNIS_Z 3800
#define FRIS_BRIDGE1_X 2314
#define FRIS_BRIDGE1_Z 3840
#define FRIS_BRIDGE2_X 2355
#define FRIS_BRIDGE2_Z 3840
#define FRIS_CHEST_X 2407
#define FRIS_CHEST_Z 3800
#define FRIS_KEEPA_X 2417
#define FRIS_KEEPA_Z 3816
#define FRIS_VANLIGGA_X 2405
#define FRIS_VANLIGGA_Z 3813
#define FRIS_SKULI_X 2395
#define FRIS_SKULI_Z 3804
#define FRIS_HRING_X 2397
#define FRIS_HRING_Z 3797
#define FRIS_RAUM_X 2395
#define FRIS_RAUM_Z 3797
#define FRIS_FLOSI_X 2418
#define FRIS_FLOSI_Z 3813
#define FRIS_TRAPDOOR_X 2402
#define FRIS_TRAPDOOR_Z 3890
#define FRIS_CAVE_X 2390
#define FRIS_CAVE_Z 10280
#define FRIS_KING_X 2386
#define FRIS_KING_Z 10249
#define FRIS_KING_BRIDGE_X 2385
#define FRIS_KING_BRIDGE_Z 10263

static void
fris_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "FRIS PASS: %s\n", step);
}

static void
fris_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
fris_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
fris_finish(struct ToriRSServer* srv)
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
fris_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
fris_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = fris_chatmenu();
    if( chatmenu <= 0 )
        return;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
fris_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = fris_chatmenu();
    clicks = 0;
    while( clicks < max_pages && player->active_script )
    {
        if( player->resume_button_count > 0 && chatmenu > 0 &&
            player->resume_buttons[0] == chatmenu )
            return;
        if( selftest_click_through(srv, 1) <= 0 )
            break;
        clicks++;
    }
}

static void
fris_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    fris_god(player);
    selftest_tick(srv);
}

static int
fris_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    fris_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
fris_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
fris_free_type(struct ToriRSServer* srv, int npc_type)
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

static int
fris_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
fris_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( fris_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
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

static void
fris_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
fris_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
fris_varp(struct ToriRSServer* srv, const char* name, int value)
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
fris_quest(struct ToriRSServerPlayer* player)
{
    return fris_get_vb(player, "fris_quest");
}

static void
fris_reset_tax(struct ToriRSServer* srv)
{
    assert(srv);
    fris_vb(srv, "frisd_weaponmerchant_taxcollected", 0);
    fris_vb(srv, "frisd_oremerchant_taxcollected", 0);
    fris_vb(srv, "frisd_fishmonger_taxcollected", 0);
    fris_vb(srv, "frisd_armourmerchant_taxcollected", 0);
    fris_vb(srv, "frisd_pub_taxcollected", 0);
    fris_vb(srv, "frisd_cook_taxcollected", 0);
}

static void
fris_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    fris_vb(srv, "fris_quest", FRIS_NOT_STARTED);
    fris_vb(srv, "fris_m_b3", 0);
    fris_vb(srv, "fris_m_b4", 0);
    fris_vb(srv, "fris_m_b5", 0);
    fris_vb(srv, "fris_king", 0);
    fris_vb(srv, "fris_task", FRIS_TASK_NEEDED);
    fris_reset_tax(srv);
}

static void
fris_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int ok)
{
    int agility;
    int construction;

    assert(srv);
    assert(player);
    fris_varp(srv, "viking", ok ? FRIS_VIKING_COMPLETE : 0);
    agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    construction = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    if( agility >= 0 )
        ToriRSServer_CombatSetLevel(player, agility, ok ? FRIS_REQ_AGILITY : 1);
    if( construction >= 0 )
        ToriRSServer_CombatSetLevel(player, construction, ok ? FRIS_REQ_CONSTRUCTION : 1);
}

static void
fris_ready(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    fris_reset_quest(srv);
    fris_clear_inv(player);
    fris_prereqs(srv, player, 1);
    fris_god(player);
}

static void
fris_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
fris_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    fris_talk(srv, npc_type, slot);
    fris_finish(srv);
}

static void
fris_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    fris_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        fris_click_until_menu(srv, 24);
        fris_pick_row(srv, rows[i]);
    }
    fris_finish(srv);
}

static int
fris_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    fris_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
fris_op_loc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    fris_finish(srv);
}

static void
fris_op_held(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    fris_finish(srv);
}

static void
fris_use_npc(struct ToriRSServer* srv, int npc_type, int slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_slot = slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    fris_finish(srv);
}

static void
fris_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    npc->combat_target = srv->active_player->pid;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    fris_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    fris_finish(srv);
}

static void
fris_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,fris_journal]", NULL, 0);
    fris_finish(srv);
    fris_pass(step);
}

static void
fris_give_jester(struct ToriRSServerPlayer* player, int hat, int top, int legs, int boots)
{
    assert(player);
    fris_clear_inv(player);
    if( hat > 0 )
        fris_give(player, hat, 1);
    if( top > 0 )
        fris_give(player, top, 1);
    if( legs > 0 )
        fris_give(player, legs, 1);
    if( boots > 0 )
        fris_give(player, boots, 1);
}

static void
selftest_quest_thefremennikisles(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_mord;
    int npc_maria;
    int npc_gjuki;
    int npc_cat;
    int npc_slug;
    int npc_mawnis;
    int npc_keepa;
    int npc_vanligga;
    int npc_skuli;
    int npc_hring;
    int npc_raum;
    int npc_flosi;
    int npc_thakkrad;
    int npc_troll;
    int npc_king;
    int loc_chest;
    int loc_rb3;
    int loc_rb4;
    int loc_trapdoor;
    int loc_king_bridge;
    int loc_dead_head;
    int obj_tuna;
    int obj_mithril;
    int obj_hat;
    int obj_top;
    int obj_legs;
    int obj_boots;
    int obj_rope;
    int obj_split;
    int obj_knife;
    int obj_decree;
    int obj_yak;
    int obj_cured;
    int obj_body;
    int obj_greaves;
    int obj_shield;
    int obj_head;
    int obj_helm;
    int obj_lamp;
    int obj_pot;
    int obj_log;
    int obj_nail;
    int stat_con;
    int stat_craft;
    int stat_wc;
    int varp_qp;
    int slot;
    int loc_slot;
    int con_before;
    int craft_before;
    int wc_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: the fremennik isles critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer fris selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    fris_god(player);
    fris_reset_quest(srv);
    fris_clear_inv(player);

    npc_mord = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_r_ferryman_rellekka");
    npc_maria = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_r_ferry_rellikka");
    npc_gjuki = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_r_king");
    npc_cat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_r_kingscat");
    npc_slug = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_spymaster");
    npc_mawnis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_r_burgher_crown");
    if( npc_mawnis <= 0 )
        npc_mawnis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_r_burgher");
    npc_keepa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "frisd_cook");
    npc_vanligga = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "frisd_izso_landlady");
    npc_skuli = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "frisd_weaponmerchant");
    npc_hring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "frisd_oremerchant");
    npc_raum = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "frisd_armourmerchant");
    npc_flosi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "frisd_fishmerchant");
    npc_thakkrad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "frisd_eng");
    npc_troll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_trollm_pc");
    npc_king = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fris_troll_king_true");
    loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fris_chest_closed");
    loc_rb3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "frisr_rb3");
    loc_rb4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "frisr_rb4");
    loc_trapdoor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fris_troll_trapdoor_r2");
    loc_king_bridge = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "frisb_bridge_6_n");
    loc_dead_head = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fris_troll_king_dead_head");
    obj_tuna = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_tuna");
    obj_mithril = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mithril_ore");
    obj_hat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "frisd_jester_hat");
    obj_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "frisd_jester_top");
    obj_legs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "frisd_jester_legs");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "frisd_jester_boots");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_split = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "arctic_pine_split");
    obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
    obj_decree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "frisd_reciept");
    obj_yak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yak_hide");
    obj_cured = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yak_hide_cured");
    obj_body = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yak_hide_armour_body");
    obj_greaves = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yak_hide_armour_greaves");
    obj_shield = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fremmenik_round_shield");
    obj_head = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "frisr_trollkinghead");
    obj_helm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fris_kingly_helm");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    obj_log = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "arctic_pine_log");
    obj_nail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nails_bronze");
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_wc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    varp_qp = ToriRSServer_WorldVarp("qp");
    if( varp_qp < 0 )
        varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_fremennikisles") > 0,
                   "dbrow quest_fremennikisles should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fris_quest") >= 0,
                   "varbit fris_quest should resolve");
    SELFTEST_CHECK(npc_mord > 0, "npc Mord should resolve");
    SELFTEST_CHECK(npc_maria > 0, "npc Maria should resolve");
    SELFTEST_CHECK(npc_gjuki > 0, "npc Gjuki should resolve");
    SELFTEST_CHECK(npc_cat > 0, "npc kingscat should resolve");
    SELFTEST_CHECK(npc_slug > 0, "npc Slug should resolve");
    SELFTEST_CHECK(npc_mawnis > 0, "npc Mawnis should resolve");
    SELFTEST_CHECK(npc_keepa > 0, "npc Keepa should resolve");
    SELFTEST_CHECK(npc_vanligga > 0, "npc Vanligga should resolve");
    SELFTEST_CHECK(npc_skuli > 0, "npc Skuli should resolve");
    SELFTEST_CHECK(npc_hring > 0, "npc Hring should resolve");
    SELFTEST_CHECK(npc_raum > 0, "npc Raum should resolve");
    SELFTEST_CHECK(npc_flosi > 0, "npc Flosi should resolve");
    SELFTEST_CHECK(npc_thakkrad > 0, "npc Thakkrad should resolve");
    SELFTEST_CHECK(npc_troll > 0, "npc cave troll should resolve");
    SELFTEST_CHECK(npc_king > 0, "npc Ice Troll King should resolve");
    SELFTEST_CHECK(loc_chest > 0, "loc jester chest should resolve");
    SELFTEST_CHECK(loc_rb3 > 0, "loc west bridge should resolve");
    SELFTEST_CHECK(loc_rb4 > 0, "loc east bridge should resolve");
    SELFTEST_CHECK(loc_trapdoor > 0, "loc troll trapdoor should resolve");
    SELFTEST_CHECK(obj_helm > 0, "obj Helm of Neitiznot should resolve");
    SELFTEST_CHECK(obj_lamp > 0, "obj thosf_reward_lamp should resolve");

    fris_journal(srv, "journal_0_not_started");

    /* Maria redirects before the quest starts. */
    slot = fris_spawn(srv, npc_maria, FRIS_MARIA_X, FRIS_MARIA_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Maria should spawn");
    if( slot >= 0 )
    {
        fris_ready(srv, player);
        fris_talk_finish(srv, npc_maria, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_NOT_STARTED,
                       "Maria pre-quest must stay not_started");
        SELFTEST_CHECK(player->x == FRIS_MARIA_X && player->z == FRIS_MARIA_Z,
                       "Maria pre-quest must not ferry");
        fris_pass("opnpc1_maria_redirect");
    }
    fris_free_npc(srv, slot);

    /* Mord qualify-fail / refuse / accept. */
    slot = fris_spawn(srv, npc_mord, FRIS_MORD_X, FRIS_MORD_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Mord should spawn");
    if( slot >= 0 )
    {
        fris_reset_quest(srv);
        fris_prereqs(srv, player, 0);
        fris_talk_finish(srv, npc_mord, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_NOT_STARTED,
                       "Mord qualify-fail must stay not_started");
        SELFTEST_CHECK(player->x == FRIS_MORD_X && player->z == FRIS_MORD_Z,
                       "Mord qualify-fail must not ferry");
        fris_pass("opnpc1_mord_qualify_fail");

        fris_ready(srv, player);
        fris_talk_rows(srv, npc_mord, slot, k_refuse, 1);
        SELFTEST_CHECK(fris_quest(player) == FRIS_NOT_STARTED,
                       "Mord refuse must stay not_started");
        SELFTEST_CHECK(player->x == FRIS_MORD_X && player->z == FRIS_MORD_Z,
                       "Mord refuse must not ferry");
        fris_pass("opnpc1_mord_refuse");

        fris_talk_rows(srv, npc_mord, slot, k_accept, 1);
        SELFTEST_CHECK(player->x == FRIS_GJUKI_X && player->z == FRIS_GJUKI_Z,
                       "Mord accept must ferry to Jatizso %d,%d got %d,%d",
                       FRIS_GJUKI_X, FRIS_GJUKI_Z, player->x, player->z);
        fris_pass("opnpc1_mord_accept");
    }
    fris_free_npc(srv, slot);

    /* Gjuki qualify-fail / refuse / accept / ore / jester / taxes / decree. */
    slot = fris_spawn(srv, npc_gjuki, FRIS_GJUKI_X, FRIS_GJUKI_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Gjuki should spawn");
    if( slot >= 0 )
    {
        fris_reset_quest(srv);
        fris_prereqs(srv, player, 0);
        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_NOT_STARTED,
                       "Gjuki qualify-fail must stay not_started");
        fris_pass("opnpc1_gjuki_qualify_fail");

        fris_ready(srv, player);
        fris_talk_rows(srv, npc_gjuki, slot, k_refuse, 1);
        SELFTEST_CHECK(fris_quest(player) == FRIS_NOT_STARTED,
                       "Gjuki refuse must stay not_started");
        fris_pass("opnpc1_gjuki_refuse");

        fris_talk_rows(srv, npc_gjuki, slot, k_accept, 1);
        SELFTEST_CHECK(fris_quest(player) == FRIS_TOLD_OF_QUEST,
                       "Gjuki accept must write told_of_quest, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_gjuki_accept");

        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_GJUKI_DIALOG1,
                       "Gjuki plan must write dialog1, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_gjuki_plan");

        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_NEED_ORE,
                       "Gjuki ore ask must write need_ore, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_gjuki_need_ore");

        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_NEED_ORE,
                       "ore missing must stay need_ore");
        fris_pass("opnpc1_gjuki_ore_missing");

        if( obj_mithril > 0 )
            fris_give(player, obj_mithril, 6);
        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_ORE_DELIVERED,
                       "ore hand-in must write ore_delivered, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_gjuki_ore_delivered");

        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_GET_JESTER_OUTFIT,
                       "jester brief must write get_jester, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_gjuki_jester_brief");
    }
    fris_journal(srv, "journal_5_jester");

    /* Cat tuna. */
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_cat, FRIS_GJUKI_X, FRIS_GJUKI_Z, 0);
    SELFTEST_CHECK(slot >= 0, "cat should spawn");
    if( slot >= 0 )
    {
        fris_ready(srv, player);
        fris_vb(srv, "fris_quest", FRIS_TOLD_OF_QUEST);
        fris_talk_finish(srv, npc_cat, slot);
        fris_pass("opnpc1_cat_miaow");

        if( obj_tuna > 0 )
        {
            fris_give(player, obj_tuna, 1);
            fris_use_npc(srv, npc_cat, slot, obj_tuna);
            SELFTEST_CHECK(fris_inv_total(player, obj_tuna) == 0,
                           "cat tuna must consume the fish");
            fris_pass("opnpcu_cat_tuna");
        }
    }

    /* Jester chest. */
    loc_slot = fris_place_loc(srv, loc_chest, FRIS_CHEST_X, FRIS_CHEST_Z, 0);
    SELFTEST_CHECK(loc_slot >= 0, "jester chest should place");
    if( loc_slot >= 0 )
    {
        fris_ready(srv, player);
        fris_op_loc(srv, loc_chest, loc_slot);
        SELFTEST_CHECK(fris_inv_total(player, obj_hat) == 0,
                       "early chest must not grant outfit");
        fris_pass("oploc1_chest_empty");

        fris_vb(srv, "fris_quest", FRIS_GET_JESTER_OUTFIT);
        if( obj_pot > 0 )
        {
            int s;
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, obj_pot, 1);
            fris_op_loc(srv, loc_chest, loc_slot);
            SELFTEST_CHECK(fris_inv_total(player, obj_hat) == 0,
                           "full inv must not grant jester outfit");
            fris_pass("oploc1_chest_inv_full");
        }

        fris_clear_inv(player);
        fris_op_loc(srv, loc_chest, loc_slot);
        SELFTEST_CHECK(fris_inv_total(player, obj_hat) > 0,
                       "chest must grant jester hat");
        SELFTEST_CHECK(fris_inv_total(player, obj_top) > 0,
                       "chest must grant jester top");
        SELFTEST_CHECK(fris_inv_total(player, obj_legs) > 0,
                       "chest must grant jester legs");
        SELFTEST_CHECK(fris_inv_total(player, obj_boots) > 0,
                       "chest must grant jester boots");
        fris_pass("oploc1_chest_find");

        fris_op_loc(srv, loc_chest, loc_slot);
        fris_pass("oploc1_chest_already");
    }

    /* Slug brief 1 + Mawnis spy 1 + Slug report 1. */
    fris_free_type(srv, npc_slug);
    slot = fris_spawn(srv, npc_slug, FRIS_SLUG_X, FRIS_SLUG_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Slug should spawn");
    if( slot >= 0 )
    {
        fris_ready(srv, player);
        fris_vb(srv, "fris_quest", FRIS_GET_JESTER_OUTFIT);
        fris_talk_finish(srv, npc_slug, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_GET_JESTER_OUTFIT,
                       "Slug without jester must stay get_jester");
        fris_pass("opnpc1_slug_need_jester");

        fris_give_jester(player, obj_hat, obj_top, obj_legs, obj_boots);
        fris_talk_finish(srv, npc_slug, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_SPY_ON_MAWNIS_1,
                       "Slug brief 1 must write spy_1, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_slug_brief_1");
    }
    fris_journal(srv, "journal_6_spy1");

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_mawnis, FRIS_MAWNIS_X, FRIS_MAWNIS_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Mawnis should spawn");
    if( slot >= 0 )
    {
        fris_ready(srv, player);
        fris_vb(srv, "fris_quest", FRIS_SPY_ON_MAWNIS_1);
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_SPY_ON_MAWNIS_1,
                       "Mawnis without jester must stay spy_1");
        fris_pass("opnpc1_mawnis_need_jester");

        fris_give_jester(player, obj_hat, obj_top, obj_legs, obj_boots);
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_REPORT_SLUG_1,
                       "Mawnis spy 1 must write report_slug_1, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_spy_1");
    }

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_slug, FRIS_SLUG_X, FRIS_SLUG_Z, 0);
    if( slot >= 0 )
    {
        fris_vb(srv, "fris_quest", FRIS_REPORT_SLUG_1);
        fris_talk_finish(srv, npc_slug, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_HELP_MAWNIS,
                       "Slug report 1 must write help_mawnis, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_slug_report_1");
    }
    fris_journal(srv, "journal_8_help");

    /* Mawnis help / items / repair howto. */
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_mawnis, FRIS_MAWNIS_X, FRIS_MAWNIS_Z, 0);
    if( slot >= 0 )
    {
        fris_clear_inv(player);
        fris_vb(srv, "fris_quest", FRIS_HELP_MAWNIS);
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_BRING_BRIDGE_ITEMS,
                       "Mawnis help must write bring_items, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_help");

        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_BRING_BRIDGE_ITEMS,
                       "missing items must stay bring_items");
        fris_pass("opnpc1_mawnis_need_items");

        if( obj_rope > 0 )
            fris_give(player, obj_rope, 8);
        if( obj_split > 0 )
            fris_give(player, obj_split, 8);
        if( obj_knife > 0 )
            fris_give(player, obj_knife, 1);
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_BRIDGE_ITEMS_DELIVERED,
                       "item hand-in must write items_delivered, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_items");

        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_REPAIR_BRIDGES,
                       "repair howto must write repair_bridges, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_repair_howto");
    }
    fris_journal(srv, "journal_11_repair");

    /* Two bridge repairs. */
    loc_slot = fris_place_loc(srv, loc_rb3, FRIS_BRIDGE1_X, FRIS_BRIDGE1_Z, 0);
    SELFTEST_CHECK(loc_slot >= 0, "west bridge should place");
    if( loc_slot >= 0 )
    {
        fris_ready(srv, player);
        fris_op_loc(srv, loc_rb3, loc_slot);
        SELFTEST_CHECK(fris_get_vb(player, "fris_m_b3") == 0,
                       "too-early west bridge must stay broken");
        fris_pass("oploc1_bridge_west_too_early");

        fris_vb(srv, "fris_quest", FRIS_REPAIR_BRIDGES);
        fris_op_loc(srv, loc_rb3, loc_slot);
        SELFTEST_CHECK(fris_get_vb(player, "fris_m_b3") == 0,
                       "west bridge without items must stay broken");
        fris_pass("oploc1_bridge_west_need_items");

        if( obj_rope > 0 )
            fris_give(player, obj_rope, 1);
        if( obj_split > 0 )
            fris_give(player, obj_split, 1);
        if( obj_knife > 0 )
            fris_give(player, obj_knife, 1);
        fris_vb(srv, "prayer_protectfrommissiles", 1);
        if( !selftest_prayer_on(srv, "prayer_protectfrommissiles") )
            selftest_prayer_toggle(srv, "prayer_protectfrommissiles");
        fris_op_loc(srv, loc_rb3, loc_slot);
        SELFTEST_CHECK(fris_get_vb(player, "fris_m_b3") == 1,
                       "west bridge repair must set fris_m_b3");
        fris_pass("oploc1_bridge_west");
    }

    loc_slot = fris_place_loc(srv, loc_rb4, FRIS_BRIDGE2_X, FRIS_BRIDGE2_Z, 0);
    SELFTEST_CHECK(loc_slot >= 0, "east bridge should place");
    if( loc_slot >= 0 )
    {
        fris_vb(srv, "fris_quest", FRIS_REPAIR_BRIDGES);
        if( obj_rope > 0 )
            fris_give(player, obj_rope, 1);
        if( obj_split > 0 )
            fris_give(player, obj_split, 1);
        if( obj_knife > 0 )
            fris_give(player, obj_knife, 1);
        fris_vb(srv, "prayer_protectfrommissiles", 1);
        if( !selftest_prayer_on(srv, "prayer_protectfrommissiles") )
            selftest_prayer_toggle(srv, "prayer_protectfrommissiles");
        fris_op_loc(srv, loc_rb4, loc_slot);
        SELFTEST_CHECK(fris_get_vb(player, "fris_m_b4") == 1,
                       "east bridge repair must set fris_m_b4");
        SELFTEST_CHECK(fris_get_vb(player, "fris_m_b5") == 1,
                       "east bridge must flip cosmetic fris_m_b5");
        fris_pass("oploc1_bridge_east");
    }

    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_BRIDGES_REPAIRED,
                       "both bridges must write bridges_repaired, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_bridges_thanks");

        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_REPORT_GJUKI_BRIDGES,
                       "tell Gjuki must write report_gjuki_bridges, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_tell_gjuki");
    }
    fris_journal(srv, "journal_13_report_gjuki");

    /* Window tax + beard tax. */
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_gjuki, FRIS_GJUKI_X, FRIS_GJUKI_Z, 0);
    if( slot >= 0 )
    {
        fris_vb(srv, "fris_quest", FRIS_REPORT_GJUKI_BRIDGES);
        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_COLLECT_WINDOW_TAX,
                       "Gjuki bridges report must write window_tax, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_gjuki_window_ask");
    }

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_keepa, FRIS_KEEPA_X, FRIS_KEEPA_Z, 0);
    if( slot >= 0 )
    {
        fris_vb(srv, "fris_quest", FRIS_COLLECT_WINDOW_TAX);
        fris_reset_tax(srv);
        fris_talk_finish(srv, npc_keepa, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_cook_taxcollected") == 1,
                       "Keepa window tax must set collected");
        fris_pass("opnpc1_keepa_window");
    }
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_vanligga, FRIS_VANLIGGA_X, FRIS_VANLIGGA_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_vanligga, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_pub_taxcollected") == 1,
                       "Vanligga window tax must set collected");
        fris_pass("opnpc1_vanligga_window");
    }
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_skuli, FRIS_SKULI_X, FRIS_SKULI_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_skuli, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_weaponmerchant_taxcollected") == 1,
                       "Skuli window tax must set collected");
        fris_pass("opnpc1_skuli_window");
    }
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_hring, FRIS_HRING_X, FRIS_HRING_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_hring, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_oremerchant_taxcollected") == 1,
                       "Hring window tax must set collected");
        fris_pass("opnpc1_hring_window");
    }

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_gjuki, FRIS_GJUKI_X, FRIS_GJUKI_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_COLLECT_BEARD_TAX,
                       "window complete must write beard_tax, got %d",
                       fris_quest(player));
        SELFTEST_CHECK(fris_get_vb(player, "frisd_cook_taxcollected") == 0,
                       "~fris_reset_tax must clear cook bit for beard round");
        fris_pass("opnpc1_gjuki_beard_ask");
    }
    fris_journal(srv, "journal_15_beard");

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_hring, FRIS_HRING_X, FRIS_HRING_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_hring, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_oremerchant_taxcollected") == 1,
                       "Hring beard tax must set collected");
        fris_pass("opnpc1_hring_beard");
    }
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_raum, FRIS_RAUM_X, FRIS_RAUM_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_raum, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_armourmerchant_taxcollected") == 1,
                       "Raum beard tax must set collected");
        fris_pass("opnpc1_raum_beard");
    }
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_skuli, FRIS_SKULI_X, FRIS_SKULI_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_skuli, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_weaponmerchant_taxcollected") == 1,
                       "Skuli beard tax must set collected");
        fris_pass("opnpc1_skuli_beard");
    }
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_keepa, FRIS_KEEPA_X, FRIS_KEEPA_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_keepa, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_cook_taxcollected") == 1,
                       "Keepa beard tax must set collected");
        fris_pass("opnpc1_keepa_beard");
    }
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_flosi, FRIS_FLOSI_X, FRIS_FLOSI_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_flosi, slot);
        SELFTEST_CHECK(fris_get_vb(player, "frisd_fishmonger_taxcollected") == 1,
                       "Flosi beard tax must set collected");
        fris_pass("opnpc1_flosi_beard");
    }

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_gjuki, FRIS_GJUKI_X, FRIS_GJUKI_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_RETURN_TO_SPY_AGAIN,
                       "beard complete must write spy_again, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_gjuki_spy_again");
    }
    fris_journal(srv, "journal_16_spy_again");

    /* Second spy + decree. */
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_slug, FRIS_SLUG_X, FRIS_SLUG_Z, 0);
    if( slot >= 0 )
    {
        fris_give_jester(player, obj_hat, obj_top, obj_legs, obj_boots);
        fris_vb(srv, "fris_quest", FRIS_RETURN_TO_SPY_AGAIN);
        fris_talk_finish(srv, npc_slug, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_SPY_ON_MAWNIS_2,
                       "Slug brief 2 must write spy_2, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_slug_brief_2");
    }

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_mawnis, FRIS_MAWNIS_X, FRIS_MAWNIS_Z, 0);
    if( slot >= 0 )
    {
        fris_give_jester(player, obj_hat, obj_top, obj_legs, obj_boots);
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_REPORT_SLUG_2,
                       "Mawnis spy 2 must write report_slug_2, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_spy_2");
    }

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_slug, FRIS_SLUG_X, FRIS_SLUG_Z, 0);
    if( slot >= 0 )
    {
        fris_talk_finish(srv, npc_slug, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_REPORT_GJUKI_2,
                       "Slug report 2 must write report_gjuki_2, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_slug_report_2");
    }

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_gjuki, FRIS_GJUKI_X, FRIS_GJUKI_Z, 0);
    if( slot >= 0 )
    {
        fris_clear_inv(player);
        fris_talk_finish(srv, npc_gjuki, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_DELIVER_DECREE,
                       "Gjuki decree must write deliver_decree, got %d",
                       fris_quest(player));
        if( obj_decree > 0 )
            SELFTEST_CHECK(fris_inv_total(player, obj_decree) > 0,
                           "Gjuki must grant the royal decree");
        fris_pass("opnpc1_gjuki_decree");
    }
    fris_journal(srv, "journal_20_decree");

    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_mawnis, FRIS_MAWNIS_X, FRIS_MAWNIS_Z, 0);
    if( slot >= 0 )
    {
        if( obj_decree > 0 && fris_inv_total(player, obj_decree) == 0 )
            fris_give(player, obj_decree, 1);
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_DECREE_DELIVERED,
                       "decree hand-in must write decree_delivered, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_decree");

        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_MAKE_ARMOUR,
                       "armour ask must write make_armour, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_need_armour");

        if( obj_body > 0 )
            fris_give(player, obj_body, 1);
        if( obj_greaves > 0 )
            fris_give(player, obj_greaves, 1);
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_MAKE_SHIELD,
                       "armour show must write make_shield, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_armour_ok");

        if( obj_shield > 0 )
            fris_give(player, obj_shield, 1);
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_KILL_TROLLS,
                       "shield show must write kill_trolls, got %d",
                       fris_quest(player));
        fris_pass("opnpc1_mawnis_ready_trolls");
    }
    fris_journal(srv, "journal_24_trolls");

    /* Thakkrad hide cure. */
    fris_free_npc(srv, slot);
    slot = fris_spawn(srv, npc_thakkrad, FRIS_MAWNIS_X, FRIS_MAWNIS_Z, 0);
    if( slot >= 0 && obj_yak > 0 && obj_cured > 0 )
    {
        fris_clear_inv(player);
        fris_give(player, obj_yak, 1);
        fris_use_npc(srv, npc_thakkrad, slot, obj_yak);
        SELFTEST_CHECK(fris_inv_total(player, obj_cured) > 0,
                       "Thakkrad must cure yak hide");
        fris_pass("opnpcu_thakkrad_cure");
    }

    /* Craft helpers (merged knife/hammer/needle). */
    if( obj_log > 0 && obj_split > 0 && obj_knife > 0 )
    {
        fris_clear_inv(player);
        fris_give(player, obj_log, 1);
        fris_give(player, obj_knife, 1);
        player->last_useitem = obj_log;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_knife, -1, -1);
        fris_finish(srv);
        SELFTEST_CHECK(fris_inv_total(player, obj_split) > 0,
                       "knife-on-log must split arctic pine");
        fris_pass("opheldu_split_arctic_pine");
    }
    if( obj_log > 0 && obj_nail > 0 && obj_rope > 0 && obj_shield > 0 )
    {
        int hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
        fris_clear_inv(player);
        fris_give(player, obj_log, 2);
        fris_give(player, obj_nail, 1);
        fris_give(player, obj_rope, 1);
        if( hammer > 0 )
        {
            fris_give(player, hammer, 1);
            player->last_useitem = obj_log;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, hammer, -1, -1);
            fris_finish(srv);
            SELFTEST_CHECK(fris_inv_total(player, obj_shield) > 0,
                           "hammer craft must make Neitiznot shield");
            fris_pass("opheldu_craft_shield");
        }
    }

    /* Trapdoor / trolls / king / decapitate / complete. */
    loc_slot = fris_place_loc(srv, loc_trapdoor, FRIS_TRAPDOOR_X, FRIS_TRAPDOOR_Z, 0);
    SELFTEST_CHECK(loc_slot >= 0, "troll trapdoor should place");
    if( loc_slot >= 0 )
    {
        fris_ready(srv, player);
        fris_op_loc(srv, loc_trapdoor, loc_slot);
        SELFTEST_CHECK(player->x == FRIS_TRAPDOOR_X && player->z == FRIS_TRAPDOOR_Z,
                       "too-early trapdoor must not teleport");
        fris_pass("oploc1_trapdoor_too_early");

        fris_vb(srv, "fris_quest", FRIS_KILL_TROLLS);
        fris_vb(srv, "fris_task", FRIS_TASK_NEEDED);
        fris_op_loc(srv, loc_trapdoor, loc_slot);
        SELFTEST_CHECK(player->x == FRIS_CAVE_X && player->z == FRIS_CAVE_Z,
                       "trapdoor must tele to cave %d,%d got %d,%d",
                       FRIS_CAVE_X, FRIS_CAVE_Z, player->x, player->z);
        fris_pass("oploc1_trapdoor_climb");
    }

    fris_free_type(srv, npc_troll);
    slot = fris_spawn(srv, npc_troll, FRIS_CAVE_X, FRIS_CAVE_Z, 1);
    if( slot >= 0 )
    {
        int task_before = fris_get_vb(player, "fris_task");
        fris_vb(srv, "fris_quest", FRIS_KILL_TROLLS);
        fris_kill(srv, npc_troll, slot);
        SELFTEST_CHECK(fris_get_vb(player, "fris_task") == task_before - 1 ||
                           fris_get_vb(player, "fris_task") < task_before,
                       "troll death must decrement fris_task, before %d after %d",
                       task_before, fris_get_vb(player, "fris_task"));
        fris_pass("ai_queue3_troll");
    }
    fris_free_npc(srv, slot);

    loc_slot = (loc_king_bridge > 0)
        ? fris_place_loc(srv, loc_king_bridge, FRIS_KING_BRIDGE_X, FRIS_KING_BRIDGE_Z, 1)
        : -1;
    if( loc_slot >= 0 )
    {
        fris_vb(srv, "fris_quest", FRIS_KILL_TROLLS);
        fris_vb(srv, "fris_task", 4);
        fris_op_loc(srv, loc_king_bridge, loc_slot);
        SELFTEST_CHECK(player->x == FRIS_KING_BRIDGE_X && player->z == FRIS_KING_BRIDGE_Z,
                       "king bridge with trolls remaining must not cross");
        fris_pass("oploc1_king_bridge_blocked");

        fris_vb(srv, "fris_task", 0);
        fris_op_loc(srv, loc_king_bridge, loc_slot);
        SELFTEST_CHECK(player->x == FRIS_KING_X && player->z == FRIS_KING_Z,
                       "cleared cave must tele to king chamber");
        fris_pass("oploc1_king_bridge_cross");
    }

    fris_free_type(srv, npc_king);
    slot = fris_spawn(srv, npc_king, FRIS_KING_X, FRIS_KING_Z, 1);
    SELFTEST_CHECK(slot >= 0, "Ice Troll King should spawn");
    if( slot >= 0 )
    {
        fris_vb(srv, "fris_quest", FRIS_KILL_TROLLS);
        fris_vb(srv, "fris_task", 0);
        fris_kill(srv, npc_king, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_DECAPITATE_KING,
                       "king death must write decapitate, got %d",
                       fris_quest(player));
        fris_pass("ai_queue3_king");
    }
    fris_free_npc(srv, slot);
    fris_journal(srv, "journal_25_decap");

    loc_slot = (loc_dead_head > 0)
        ? fris_place_loc(srv, loc_dead_head, FRIS_KING_X, FRIS_KING_Z, 1)
        : -1;
    if( loc_slot >= 0 )
    {
        fris_clear_inv(player);
        if( obj_pot > 0 )
        {
            int s;
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, obj_pot, 1);
            fris_op_loc(srv, loc_dead_head, loc_slot);
            SELFTEST_CHECK(fris_inv_total(player, obj_head) == 0,
                           "full inv must not take the head");
            fris_pass("oploc1_decapitate_inv_full");
        }

        fris_clear_inv(player);
        fris_vb(srv, "fris_quest", FRIS_DECAPITATE_KING);
        fris_op_loc(srv, loc_dead_head, loc_slot);
        SELFTEST_CHECK(fris_inv_total(player, obj_head) > 0,
                       "decapitate must grant the head");
        SELFTEST_CHECK(fris_quest(player) == FRIS_FINISH_QUEST,
                       "decapitate must write finish_quest, got %d",
                       fris_quest(player));
        fris_pass("oploc1_decapitate");
    }
    if( obj_head > 0 )
    {
        fris_op_held(srv, obj_head);
        fris_pass("opheld1_head_look");
    }
    fris_journal(srv, "journal_26_finish");

    fris_free_type(srv, npc_mawnis);
    slot = fris_spawn(srv, npc_mawnis, FRIS_MAWNIS_X, FRIS_MAWNIS_Z, 0);
    con_before = (stat_con >= 0) ? player->stat_xp_tenths[stat_con] : 0;
    craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
    wc_before = (stat_wc >= 0) ? player->stat_xp_tenths[stat_wc] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
    if( slot >= 0 )
    {
        fris_ready(srv, player);
        fris_vb(srv, "fris_quest", FRIS_FINISH_QUEST);
        fris_clear_inv(player);
        if( obj_head > 0 )
            fris_give(player, obj_head, 1);
        con_before = (stat_con >= 0) ? player->stat_xp_tenths[stat_con] : 0;
        craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
        wc_before = (stat_wc >= 0) ? player->stat_xp_tenths[stat_wc] : 0;
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        fris_talk_finish(srv, npc_mawnis, slot);
        SELFTEST_CHECK(fris_quest(player) == FRIS_COMPLETE,
                       "head hand-in must write complete, got %d",
                       fris_quest(player));
        if( stat_con >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_con] >=
                               con_before + FRIS_REWARD_CONSTRUCTION_TENTHS,
                           "complete must advance Construction by 50000 tenths");
        if( stat_craft >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >=
                               craft_before + FRIS_REWARD_CRAFTING_TENTHS,
                           "complete must advance Crafting by 50000 tenths");
        if( stat_wc >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_wc] >=
                               wc_before + FRIS_REWARD_WOODCUTTING_TENTHS,
                           "complete must advance Woodcutting by 100000 tenths");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + FRIS_REWARD_QP,
                           "complete must award 1 QP");
        if( obj_helm > 0 )
            SELFTEST_CHECK(fris_inv_total(player, obj_helm) > 0,
                           "complete must grant Helm of Neitiznot");
        if( obj_lamp > 0 )
            SELFTEST_CHECK(fris_inv_total(player, obj_lamp) >= FRIS_REWARD_LAMP_QTY,
                           "complete must grant 2 lamps, got %d",
                           fris_inv_total(player, obj_lamp));
        fris_pass("opnpc1_mawnis_complete");

        fris_talk_finish(srv, npc_mawnis, slot);
        fris_pass("opnpc1_mawnis_post_complete");
    }
    fris_journal(srv, "journal_340_complete");

    fris_free_npc(srv, slot);
    fris_free_type(srv, npc_mord);
    fris_free_type(srv, npc_maria);
    fris_free_type(srv, npc_gjuki);
    fris_free_type(srv, npc_cat);
    fris_free_type(srv, npc_slug);
    fris_free_type(srv, npc_mawnis);
    fris_free_type(srv, npc_keepa);
    fris_free_type(srv, npc_vanligga);
    fris_free_type(srv, npc_skuli);
    fris_free_type(srv, npc_hring);
    fris_free_type(srv, npc_raum);
    fris_free_type(srv, npc_flosi);
    fris_free_type(srv, npc_thakkrad);
    fris_free_type(srv, npc_troll);
    fris_free_type(srv, npc_king);
    fris_reset_quest(srv);
    fris_clear_inv(player);
    fris_god(player);

    fprintf(stderr, "ToriRSServer fris selftest: %d checks, %d failures this walk\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_THEFREMENNIKISLES_SELFTEST_U_H */
