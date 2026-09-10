#ifndef TORIRSSERVER_TEST_QUEST_DREAMMENTOR_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_DREAMMENTOR_SELFTEST_U_H

/* Dream Mentor Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Cyrisus / Jack / Oneiromancer / dream
 * bosses cannot leak. Real OPNPC1 / OPNPCU / OPLOC1 / OPLOCU / OPHELDU
 * on the authored path. player->godmode = 1 for the whole walk (not a
 * death test).
 *
 * MERGE -- do not add second headers:
 *   [opnpc1,lunar_oneiromancer]  quests/quest_dragonslayer2/scripts/dragonslayer2.rs2
 *   [opheldu,hammer]             general_use/scripts/hammer.rs2
 *   [opheldu,pestle_and_mortar]  skill_herblore/scripts/grind_ingredient.rs2
 * Birds-Eye Jack bank headers already live in dreammentor_cyrisus.rs2.
 *
 * Gate: TORIRSSERVER_SELFTEST_DREAM_ONLY=1
 *
 * Reqs: Combat 85, Lunar Diplomacy FINISHED (%lunar_quest_main = 190),
 * Eadgar's Ruse FINISHED (%eadgar_quest = 110). Reward tenths:
 * Hitpoints 150000 (15000 XP), Magic 100000 (10000 XP). Cache dbrow
 * quest_dreammentor awards 2 QP.
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF /
 * date_runeday / flute 282 / telekinetic grab):
 *   leftover_dream_rng
 *   leftover_boss_flavour
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 */

#define DREAM_NOT_STARTED 0
#define DREAM_FOUND 4
#define DREAM_STAGE2 6
#define DREAM_STAGE3 12
#define DREAM_NEED_GEAR 16
#define DREAM_GEAR_GIVEN 18
#define DREAM_MET_ONEIRO 20
#define DREAM_DREAM_READY 24
#define DREAM_BOSSES 26
#define DREAM_COMPLETE 28

#define DREAM_HEALTH_S1 40
#define DREAM_HEALTH_S2 70
#define DREAM_HEALTH_FULL 100
#define DREAM_ARMAMENT_FULL 100
#define DREAM_LUNARDIP_COMPLETE 190
#define DREAM_EADGAR_COMPLETE 110
#define DREAM_REQ_COMBAT 85
#define DREAM_HP_XP 150000
#define DREAM_MAGIC_XP 100000
#define DREAM_QP_REWARD 2

#define DREAM_CAVE_X 2346
#define DREAM_CAVE_Z 10360
#define DREAM_CAVE_LEVEL 2
#define DREAM_JACK_X 2099
#define DREAM_JACK_Z 3921
#define DREAM_ONEIRO_X 2151
#define DREAM_ONEIRO_Z 3867
#define DREAM_SINK_X 2091
#define DREAM_SINK_Z 3922
#define DREAM_BRAZIER_X 2075
#define DREAM_BRAZIER_Z 3912
#define DREAM_ARENA_X 1824
#define DREAM_ARENA_Z 5150
#define DREAM_ARENA_LEVEL 2
#define DREAM_MINE_X 2142
#define DREAM_MINE_Z 3944
#define DREAM_CAVE_EXIT_X 2335
#define DREAM_CAVE_EXIT_Z 10346

static void
dream_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "DREAM PASS: %s\n", step);
}

static void
dream_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
dream_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
dream_drain(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    int t;

    assert(srv);
    player = srv->active_player;
    assert(player);
    for( t = 0; t < 8 && player->active_script; t++ )
    {
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
    ToriRSServer_ScriptsProcessQueues(srv);
}

static void
dream_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 1);
        selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    for( t = 0; t < 4; t++ )
        selftest_tick(srv);
}

static int
dream_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
dream_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : dream_chatmenu();
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
dream_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = dream_chatmenu();
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
dream_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    dream_god(player);
    selftest_tick(srv);
}

static int
dream_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    dream_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
dream_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
dream_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
dream_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
dream_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
dream_prog(struct ToriRSServerPlayer* player)
{
    return dream_get_vb(player, "dream_prog");
}

static void
dream_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;
    int rc;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    dream_drain(srv);
    assert(slot >= 0);
    assert(srv->npcs[slot].active);
    player->last_slot = slot;
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 type %d slot %d should run (rc=%d active=%d)",
                   npc_type, slot, rc, srv->npcs[slot].active);
}

static void
dream_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    dream_talk(srv, npc_type, slot);
    dream_finish(srv);
}

static void
dream_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    dream_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        dream_click_until_menu(srv, 24);
        dream_pick_row(srv, rows[i]);
    }
    dream_finish(srv);
}

static void
dream_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
dream_inv_count(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;
    int n;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    n = 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    }
    return n;
}

static int
dream_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    dream_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
dream_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    assert(srv->active_player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    dream_finish(srv);
}

static void
dream_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    dream_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
dream_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    dream_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
dream_opnpcu(struct ToriRSServer* srv, int npc_type, int slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    dream_drain(srv);
    assert(slot >= 0);
    player->last_slot = slot;
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    dream_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
dream_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
dream_combat85(struct ToriRSServerPlayer* player)
{
    assert(player);
    dream_set_stat(player, TORIRSSERVER_STAT_ATTACK, 80);
    dream_set_stat(player, TORIRSSERVER_STAT_STRENGTH, 80);
    dream_set_stat(player, TORIRSSERVER_STAT_DEFENCE, 80);
    dream_set_stat(player, TORIRSSERVER_STAT_HITPOINTS, 80);
    dream_set_stat(player, TORIRSSERVER_STAT_MAGIC, 80);
    dream_set_stat(player, TORIRSSERVER_STAT_RANGED, 80);
    player->max_hitpoints = 80;
    player->hitpoints = 80;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
dream_combat_low(struct ToriRSServerPlayer* player)
{
    assert(player);
    dream_set_stat(player, TORIRSSERVER_STAT_ATTACK, 1);
    dream_set_stat(player, TORIRSSERVER_STAT_STRENGTH, 1);
    dream_set_stat(player, TORIRSSERVER_STAT_DEFENCE, 1);
    dream_set_stat(player, TORIRSSERVER_STAT_HITPOINTS, 10);
    dream_set_stat(player, TORIRSSERVER_STAT_MAGIC, 1);
    dream_set_stat(player, TORIRSSERVER_STAT_RANGED, 1);
    dream_set_stat(player, TORIRSSERVER_STAT_PRAYER, 1);
    player->max_hitpoints = 10;
    player->hitpoints = 10;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
dream_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    dream_vb(srv, "dream_prog", DREAM_NOT_STARTED);
    dream_vb(srv, "dream_health", 0);
    dream_vb(srv, "dream_armament", 0);
    dream_vb(srv, "dream_combattype", 0);
    dream_vb(srv, "dream_cutscene_seen", 0);
    dream_vb(srv, "dream_spirit", 0);
    dream_vb(srv, "lunar_brazier_lit", 0);
    dream_vb(srv, "lunar_quest_main", 0);
    dream_varp(srv, "eadgar_quest", 0);
}

static void
dream_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    dream_vb(srv, "lunar_quest_main", DREAM_LUNARDIP_COMPLETE);
    dream_varp(srv, "eadgar_quest", DREAM_EADGAR_COMPLETE);
}

static void
dream_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,dreammentor_journal]", NULL, 0);
    dream_finish(srv);
    dream_pass(step);
}

static void
selftest_quest_dreammentor(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_uncon;
    int npc_barely;
    int npc_sitting;
    int npc_cyrisus;
    int npc_melee;
    int npc_jack;
    int npc_oneiro;
    int npc_inad;
    int npc_ever;
    int npc_unto;
    int npc_illus;
    int loc_cave;
    int loc_sink;
    int loc_brazier;
    int obj_bread;
    int obj_chest;
    int obj_seal;
    int obj_vial_empty;
    int obj_vial_water;
    int obj_vial_weed;
    int obj_vial_full;
    int obj_gout;
    int obj_astral;
    int obj_shards;
    int obj_ground;
    int obj_hammer;
    int obj_pestle;
    int obj_tinder;
    int obj_lamp;
    int stat_hp;
    int stat_magic;
    int varp_qp;
    int slot_uncon;
    int slot_barely;
    int slot_sitting;
    int slot_cyrisus;
    int slot_melee;
    int slot_jack;
    int slot_oneiro;
    int loc_slot;
    int hp_before;
    int magic_before;
    int qp_before;
    int feeds;
    int32_t boss_arg[1];
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };
    static const int k_bank_no[] = { 2 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: dream mentor critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer dream mentor selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    dream_god(player);
    dream_reset_quest(srv);
    dream_clear_inv(player);

    npc_uncon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_cyrisus_unconscious");
    npc_barely = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_cyrisus_barely_conscious");
    npc_sitting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_cyrisus_sitting");
    npc_cyrisus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_cyrisus");
    npc_melee = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_cyrisus_melee");
    npc_jack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_birds_eye_jack");
    npc_oneiro = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_oneiromancer");
    npc_inad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_inadequacy");
    npc_ever = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_everlasting");
    npc_unto = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_untouchable");
    npc_illus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dream_illusive");
    loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dream_cave_wall_entrance");
    loc_sink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lunar_moonclan_sink");
    loc_brazier = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lunar_moonclan_brazier_multi");
    obj_bread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread");
    obj_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dream_chest");
    obj_seal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_seal_of_passage");
    obj_vial_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dream_vial_empty");
    obj_vial_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dream_vial_water");
    obj_vial_weed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dream_vial_weed");
    obj_vial_full = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dream_vial_full");
    obj_gout = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_goutweed_herb");
    obj_astral = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "astralrune");
    obj_shards = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dream_astral_shards");
    obj_ground = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dream_groundastral");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_pestle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pestle_and_mortar");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");
    stat_hp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hitpoints");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_dreammentor") > 0,
                   "dbrow quest_dreammentor should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "dream_prog") >= 0,
                   "varbit dream_prog should resolve");
    SELFTEST_CHECK(npc_uncon > 0, "npc dream_cyrisus_unconscious should resolve");
    if( npc_uncon <= 0 )
        return;

    dream_journal(srv, "journal_00_not_started");

    slot_uncon = dream_spawn(srv, npc_uncon, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
    SELFTEST_CHECK(slot_uncon >= 0, "unconscious Cyrisus should spawn");
    if( slot_uncon < 0 )
        return;

    dream_combat_low(player);
    dream_talk_finish(srv, npc_uncon, slot_uncon);
    SELFTEST_CHECK(dream_prog(player) == DREAM_NOT_STARTED,
                   "Combat qualify-fail must stay not_started");
    dream_pass("opnpc1_qualify_fail_combat");

    dream_combat85(player);
    dream_talk_finish(srv, npc_uncon, slot_uncon);
    SELFTEST_CHECK(dream_prog(player) == DREAM_NOT_STARTED,
                   "Lunar Diplomacy qualify-fail must stay not_started");
    dream_pass("opnpc1_qualify_fail_lunar");

    dream_vb(srv, "lunar_quest_main", DREAM_LUNARDIP_COMPLETE);
    dream_talk_finish(srv, npc_uncon, slot_uncon);
    SELFTEST_CHECK(dream_prog(player) == DREAM_NOT_STARTED,
                   "Eadgar qualify-fail must stay not_started");
    dream_pass("opnpc1_qualify_fail_eadgar");

    dream_prereqs(srv);
    dream_talk_rows(srv, npc_uncon, slot_uncon, k_refuse, 1);
    SELFTEST_CHECK(dream_prog(player) == DREAM_NOT_STARTED,
                   "Refuse must stay not_started");
    dream_pass("opnpc1_start_refuse");

    dream_talk_rows(srv, npc_uncon, slot_uncon, k_accept, 1);
    SELFTEST_CHECK(dream_prog(player) == DREAM_FOUND,
                   "Accept must set found_cyrisus (got %d)", dream_prog(player));
    dream_pass("opnpc1_start_accept");

    if( loc_cave > 0 )
    {
        loc_slot = dream_place_loc(srv, loc_cave, DREAM_MINE_X, DREAM_MINE_Z, 0);
        dream_oploc(srv, loc_cave, loc_slot);
        dream_pass("oploc1_cave_enter");
        loc_slot = dream_place_loc(srv, loc_cave, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
        dream_oploc(srv, loc_cave, loc_slot);
        dream_pass("oploc1_cave_exit");
    }

    dream_journal(srv, "journal_01_feeding");

    if( obj_bread > 0 )
    {
        dream_give(player, obj_bread, 20);
        for( feeds = 0; feeds < 4; feeds++ )
            dream_opnpcu(srv, npc_uncon, slot_uncon, obj_bread);
        SELFTEST_CHECK(dream_get_vb(player, "dream_health") >= DREAM_HEALTH_S1,
                       "four feeds must reach health stage 1 (got %d)",
                       dream_get_vb(player, "dream_health"));
        dream_pass("opnpcu_unconscious_feed");
    }

    dream_talk_finish(srv, npc_uncon, slot_uncon);
    SELFTEST_CHECK(dream_prog(player) == DREAM_STAGE2,
                   "wake talk must set stage2 (got %d)", dream_prog(player));
    dream_pass("opnpc1_unconscious_wake");
    dream_free_npc(srv, slot_uncon);

    slot_barely = -1;
    if( npc_barely > 0 )
    {
        slot_barely = dream_spawn(srv, npc_barely, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
        SELFTEST_CHECK(slot_barely >= 0, "barely-conscious Cyrisus should spawn");
        if( slot_barely >= 0 )
        {
            dream_talk_finish(srv, npc_barely, slot_barely);
            SELFTEST_CHECK(dream_prog(player) == DREAM_STAGE2,
                           "hungry barely talk must stay stage2");
            dream_pass("opnpc1_barely_needs_food");
            if( obj_bread > 0 )
            {
                for( feeds = 0; feeds < 3; feeds++ )
                    dream_opnpcu(srv, npc_barely, slot_barely, obj_bread);
                SELFTEST_CHECK(dream_get_vb(player, "dream_health") >= DREAM_HEALTH_S2,
                               "three more feeds must reach health stage 2 (got %d)",
                               dream_get_vb(player, "dream_health"));
                dream_pass("opnpcu_barely_feed");
            }
            dream_talk_finish(srv, npc_barely, slot_barely);
            SELFTEST_CHECK(dream_prog(player) == DREAM_STAGE3,
                           "barely intro must set stage3 (got %d)", dream_prog(player));
            dream_pass("opnpc1_barely_intro");
            dream_free_npc(srv, slot_barely);
        }
    }

    slot_sitting = -1;
    if( npc_sitting > 0 )
    {
        slot_sitting = dream_spawn(srv, npc_sitting, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
        SELFTEST_CHECK(slot_sitting >= 0, "sitting Cyrisus should spawn");
        if( slot_sitting >= 0 )
        {
            dream_talk_finish(srv, npc_sitting, slot_sitting);
            SELFTEST_CHECK(dream_prog(player) == DREAM_STAGE3,
                           "hungry sitting talk must stay stage3");
            dream_pass("opnpc1_sitting_needs_food");
            if( obj_bread > 0 )
            {
                for( feeds = 0; feeds < 3; feeds++ )
                    dream_opnpcu(srv, npc_sitting, slot_sitting, obj_bread);
                SELFTEST_CHECK(dream_get_vb(player, "dream_health") >= DREAM_HEALTH_FULL,
                               "three more feeds must reach full health (got %d)",
                               dream_get_vb(player, "dream_health"));
                dream_pass("opnpcu_sitting_feed");
            }
            dream_talk_finish(srv, npc_sitting, slot_sitting);
            SELFTEST_CHECK(dream_prog(player) == DREAM_NEED_GEAR,
                           "sitting recovered must set need_gear (got %d)",
                           dream_prog(player));
            dream_pass("opnpc1_sitting_recovered");
            dream_free_npc(srv, slot_sitting);
        }
    }

    dream_journal(srv, "journal_02_need_gear");

    slot_cyrisus = -1;
    if( npc_cyrisus > 0 )
    {
        slot_cyrisus = dream_spawn(srv, npc_cyrisus, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
        SELFTEST_CHECK(slot_cyrisus >= 0, "recovered Cyrisus should spawn");
        if( slot_cyrisus >= 0 )
        {
            dream_talk_finish(srv, npc_cyrisus, slot_cyrisus);
            SELFTEST_CHECK(dream_prog(player) == DREAM_NEED_GEAR,
                           "need-chest talk must stay need_gear");
            dream_pass("opnpc1_cyrisus_need_chest");
        }
    }

    slot_jack = -1;
    if( npc_jack > 0 )
    {
        slot_jack = dream_spawn(srv, npc_jack, DREAM_JACK_X, DREAM_JACK_Z, 0);
        SELFTEST_CHECK(slot_jack >= 0, "Birds-Eye Jack should spawn");
        if( slot_jack >= 0 )
        {
            dream_talk_finish(srv, npc_jack, slot_jack);
            SELFTEST_CHECK(dream_inv_count(player, obj_chest) == 0,
                           "Jack without a seal must not grant the chest");
            dream_pass("opnpc1_jack_no_seal");

            if( obj_seal > 0 )
                dream_give(player, obj_seal, 1);
            dream_talk_finish(srv, npc_jack, slot_jack);
            SELFTEST_CHECK(obj_chest <= 0 || dream_inv_count(player, obj_chest) >= 1,
                           "Jack with a seal at need_gear must grant the chest");
            dream_pass("opnpc1_jack_need_gear_chest");
        }
    }

    if( slot_cyrisus >= 0 && npc_cyrisus > 0 )
    {
        dream_tele(srv, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
        dream_talk_finish(srv, npc_cyrisus, slot_cyrisus);
        SELFTEST_CHECK(dream_prog(player) == DREAM_GEAR_GIVEN,
                       "handing the chest must set gear_given (got %d)",
                       dream_prog(player));
        SELFTEST_CHECK(dream_get_vb(player, "dream_armament") == DREAM_ARMAMENT_FULL,
                       "handing the chest must set armament 100");
        dream_pass("opnpc1_cyrisus_give_chest");
        dream_free_npc(srv, slot_cyrisus);
    }

    dream_journal(srv, "journal_03_gear_given");

    slot_melee = -1;
    if( npc_melee > 0 )
    {
        slot_melee = dream_spawn(srv, npc_melee, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
        if( slot_melee >= 0 )
        {
            dream_talk_finish(srv, npc_melee, slot_melee);
            SELFTEST_CHECK(dream_prog(player) == DREAM_GEAR_GIVEN,
                           "go-oneiromancer talk must stay gear_given");
            dream_pass("opnpc1_cyrisus_go_oneiromancer");
        }
    }

    slot_oneiro = -1;
    if( npc_oneiro > 0 )
    {
        slot_oneiro = dream_spawn(srv, npc_oneiro, DREAM_ONEIRO_X, DREAM_ONEIRO_Z, 0);
        SELFTEST_CHECK(slot_oneiro >= 0, "Oneiromancer should spawn");
        if( slot_oneiro >= 0 )
        {
            dream_talk_finish(srv, npc_oneiro, slot_oneiro);
            SELFTEST_CHECK(dream_prog(player) == DREAM_MET_ONEIRO,
                           "ritual talk must set met_oneiromancer (got %d)",
                           dream_prog(player));
            dream_pass("opnpc1_oneiromancer_ritual");
            dream_talk_finish(srv, npc_oneiro, slot_oneiro);
            SELFTEST_CHECK(dream_prog(player) == DREAM_MET_ONEIRO,
                           "brew reminder must stay met_oneiromancer");
            dream_pass("opnpc1_oneiromancer_brew_reminder");
        }
    }

    dream_journal(srv, "journal_04_met_oneiromancer");

    if( loc_sink > 0 && obj_vial_empty > 0 )
    {
        dream_give(player, obj_vial_empty, 1);
        loc_slot = dream_place_loc(srv, loc_sink, DREAM_SINK_X, DREAM_SINK_Z, 0);
        dream_use_loc(srv, loc_sink, loc_slot, obj_vial_empty);
        SELFTEST_CHECK(obj_vial_water <= 0 || dream_inv_count(player, obj_vial_water) >= 1,
                       "sink must fill the empty dream vial");
        dream_pass("oplocu_vial_fill_water");
    }

    if( obj_vial_water > 0 && obj_gout > 0 )
    {
        if( dream_inv_count(player, obj_vial_water) < 1 )
            dream_give(player, obj_vial_water, 1);
        dream_give(player, obj_gout, 1);
        dream_opheldu(srv, obj_vial_water, obj_gout);
        SELFTEST_CHECK(obj_vial_weed <= 0 || dream_inv_count(player, obj_vial_weed) >= 1,
                       "goutweed on water vial must make the weed vial");
        dream_pass("opheldu_vial_add_goutweed");
    }

    if( obj_hammer > 0 && obj_astral > 0 )
    {
        dream_give(player, obj_hammer, 1);
        dream_give(player, obj_astral, 1);
        dream_opheldu(srv, obj_hammer, obj_astral);
        SELFTEST_CHECK(obj_shards <= 0 || dream_inv_count(player, obj_shards) >= 1,
                       "hammer on astral rune must make shards");
        dream_pass("opheldu_hammer_astral");
    }

    if( obj_pestle > 0 && obj_shards > 0 )
    {
        dream_give(player, obj_pestle, 1);
        if( dream_inv_count(player, obj_shards) < 1 )
            dream_give(player, obj_shards, 1);
        dream_opheldu(srv, obj_pestle, obj_shards);
        SELFTEST_CHECK(obj_ground <= 0 || dream_inv_count(player, obj_ground) >= 1,
                       "pestle on shards must make ground astral");
        dream_pass("opheldu_grind_shards");
    }

    if( obj_vial_weed > 0 && obj_ground > 0 )
    {
        if( dream_inv_count(player, obj_vial_weed) < 1 )
            dream_give(player, obj_vial_weed, 1);
        if( dream_inv_count(player, obj_ground) < 1 )
            dream_give(player, obj_ground, 1);
        dream_opheldu(srv, obj_vial_weed, obj_ground);
        SELFTEST_CHECK(obj_vial_full <= 0 || dream_inv_count(player, obj_vial_full) >= 1,
                       "ground astral on weed vial must finish the potion");
        dream_pass("opheldu_vial_add_ground_astral");
    }

    if( loc_brazier > 0 )
    {
        loc_slot = dream_place_loc(srv, loc_brazier, DREAM_BRAZIER_X, DREAM_BRAZIER_Z, 0);
        dream_oploc(srv, loc_brazier, loc_slot);
        SELFTEST_CHECK(dream_get_vb(player, "lunar_brazier_lit") == 0,
                       "brazier without tinderbox/potion must stay unlit");
        dream_pass("oploc1_brazier_no_tinderbox");
        if( obj_tinder > 0 )
            dream_give(player, obj_tinder, 1);
        if( obj_vial_full > 0 && dream_inv_count(player, obj_vial_full) < 1 )
            dream_give(player, obj_vial_full, 1);
        dream_oploc(srv, loc_brazier, loc_slot);
        SELFTEST_CHECK(dream_get_vb(player, "lunar_brazier_lit") == 1,
                       "lighting the brazier with potion and tinderbox must set the bit");
        dream_pass("oploc1_brazier_light");
    }

    if( slot_melee >= 0 && npc_melee > 0 )
    {
        dream_tele(srv, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
        dream_talk_finish(srv, npc_melee, slot_melee);
        SELFTEST_CHECK(dream_prog(player) == DREAM_DREAM_READY,
                       "lit-brazier talk must enter the dream (got %d)",
                       dream_prog(player));
        dream_pass("opnpc1_cyrisus_dream_entry");
    }

    dream_journal(srv, "journal_05_dream");

    if( npc_inad > 0 )
    {
        boss_arg[0] = npc_inad;
        ToriRSServer_ScriptsRunProc(srv, "[proc,dreammentor_boss_killed]", boss_arg, 1);
        dream_finish(srv);
        SELFTEST_CHECK(dream_get_vb(player, "dream_spirit") == 1,
                       "Inadequacy death must set spirit 1");
        dream_pass("boss_inadequacy");
    }
    if( npc_ever > 0 )
    {
        boss_arg[0] = npc_ever;
        ToriRSServer_ScriptsRunProc(srv, "[proc,dreammentor_boss_killed]", boss_arg, 1);
        dream_finish(srv);
        SELFTEST_CHECK(dream_get_vb(player, "dream_spirit") == 2,
                       "Everlasting death must set spirit 2");
        dream_pass("boss_everlasting");
    }
    if( npc_unto > 0 )
    {
        boss_arg[0] = npc_unto;
        ToriRSServer_ScriptsRunProc(srv, "[proc,dreammentor_boss_killed]", boss_arg, 1);
        dream_finish(srv);
        SELFTEST_CHECK(dream_get_vb(player, "dream_spirit") == 3,
                       "Untouchable death must set spirit 3");
        dream_pass("boss_untouchable");
    }
    if( npc_illus > 0 )
    {
        boss_arg[0] = npc_illus;
        ToriRSServer_ScriptsRunProc(srv, "[proc,dreammentor_boss_killed]", boss_arg, 1);
        dream_finish(srv);
        SELFTEST_CHECK(dream_prog(player) == DREAM_BOSSES,
                       "Illusive death must set bosses_defeated (got %d)",
                       dream_prog(player));
        dream_pass("boss_illusive_wake");
    }

    dream_journal(srv, "journal_06_bosses_defeated");

    hp_before = (stat_hp >= 0) ? player->stat_xp_tenths[stat_hp] : 0;
    magic_before = (stat_magic >= 0) ? player->stat_xp_tenths[stat_magic] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;

    if( slot_oneiro >= 0 && npc_oneiro > 0 )
    {
        dream_tele(srv, DREAM_ONEIRO_X, DREAM_ONEIRO_Z, 0);
        dream_talk_finish(srv, npc_oneiro, slot_oneiro);
        SELFTEST_CHECK(dream_prog(player) == DREAM_COMPLETE,
                       "Oneiromancer finish must set complete (got %d)",
                       dream_prog(player));
        if( stat_hp >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_hp] >= hp_before + DREAM_HP_XP,
                           "completion must award 15000 Hitpoints XP (tenths %d -> %d)",
                           hp_before, player->stat_xp_tenths[stat_hp]);
        if( stat_magic >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_magic] >= magic_before + DREAM_MAGIC_XP,
                           "completion must award 10000 Magic XP (tenths %d -> %d)",
                           magic_before, player->stat_xp_tenths[stat_magic]);
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + DREAM_QP_REWARD,
                           "completion must award 2 QP (%d -> %d)",
                           qp_before, player->varps[varp_qp]);
        if( obj_lamp > 0 )
            SELFTEST_CHECK(dream_inv_count(player, obj_lamp) >= 1,
                           "completion must grant the Dreamy lamp");
        dream_pass("opnpc1_oneiromancer_complete");
    }

    dream_journal(srv, "journal_07_complete");

    if( slot_jack >= 0 && npc_jack > 0 )
    {
        dream_tele(srv, DREAM_JACK_X, DREAM_JACK_Z, 0);
        dream_talk_rows(srv, npc_jack, slot_jack, k_bank_no, 1);
        SELFTEST_CHECK(dream_prog(player) == DREAM_COMPLETE,
                       "post-quest Jack must stay complete");
        dream_pass("opnpc1_jack_bank_post");
    }

    if( slot_melee >= 0 && npc_melee > 0 )
    {
        dream_tele(srv, DREAM_CAVE_X, DREAM_CAVE_Z, DREAM_CAVE_LEVEL);
        dream_talk_finish(srv, npc_melee, slot_melee);
        dream_pass("opnpc1_cyrisus_thanks");
        dream_free_npc(srv, slot_melee);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,dreammentor_leftover_dream_rng]", NULL, 0);
    dream_finish(srv);
    dream_pass("leftover_dream_rng");
    ToriRSServer_ScriptsRunProc(srv, "[proc,dreammentor_leftover_boss_flavour]", NULL, 0);
    dream_finish(srv);
    dream_pass("leftover_boss_flavour");

    dream_free_npc(srv, slot_jack);
    dream_free_npc(srv, slot_oneiro);
    dream_reset_quest(srv);
    dream_clear_inv(player);
}

#endif
