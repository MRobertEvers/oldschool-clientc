#ifndef TORIRSSERVER_TEST_QUEST_LUNARDIPLOMACY_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_LUNARDIPLOMACY_SELFTEST_U_H

/* Lunar Diplomacy Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Lokar /
 * crew / Oneiromancer / monks / trial hosts / Me cannot leak. Real OPNPC1 /
 * OPNPC2 / AI_QUEUE3 / lunardip_visit_altar on the authored path.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_LUNAR_ONLY=1
 *
 * Real prereqs: Fremennik Trials (%viking >= 10), Lost City (%zanaris >= 6),
 * Rune Mysteries (%runemysteries >= 6), Shilo Village (%zombiequeen >= 15).
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - ship wall-chart IF interfaces/quest_lunar_galleon.if
 *   - Lunar spellbook switch (no engine spellbook system)
 *   - dream-trial RNG minigames condensed (except Mimic p_choice4)
 *   - suqah tooth/hide / lunar ore/bar/staff item chain
 *   - Pauline disguise riddle / blue-flower ring dig (narrated)
 *
 * Required systems (jewellery IF / date_runeday / flute widget / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 */

#define LD_NOT_STARTED 0
#define LD_ACCEPTED 10
#define LD_SHIP_BOARDED 20
#define LD_SYMBOLS 30
#define LD_MET_ONEIRO 40
#define LD_SUQAH 50
#define LD_POTION 60
#define LD_STAFF_START 70
#define LD_STAFF_AIR 71
#define LD_STAFF_FIRE 72
#define LD_STAFF_WATER 73
#define LD_STAFF_EARTH 74
#define LD_REGALIA 80
#define LD_DREAM 140
#define LD_GAMES 150
#define LD_MIRROR 160
#define LD_COMPLETE 190

#define LD_VIKING_DONE 10
#define LD_ZANARIS_DONE 6
#define LD_RUNEMY_DONE 6
#define LD_SHILO_DONE 15

#define LD_REWARD_TENTHS 50000
#define LD_QP_REWARD 2

#define LD_RELLEKKA_X 2620
#define LD_RELLEKKA_Z 3693
#define LD_HARBOUR_X 2151
#define LD_HARBOUR_Z 3868
#define LD_SHIP_X 2138
#define LD_SHIP_Z 3899
#define LD_SHIP_LEVEL 2
#define LD_DREAM_X 1750
#define LD_DREAM_Z 5090
#define LD_DREAM_LEVEL 2
#define LD_MIRROR_X 1823
#define LD_MIRROR_Z 5087
#define LD_MIRROR_LEVEL 2

#define LD_ALTAR_AIR 1
#define LD_ALTAR_WATER 3
#define LD_ALTAR_EARTH 4
#define LD_ALTAR_FIRE 5

static void
ld_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "LUNAR PASS: %s\n", step);
}

static void
ld_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ld_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ld_finish(struct ToriRSServer* srv)
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
ld_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ld_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : ld_chatmenu();
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
ld_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ld_chatmenu();
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
ld_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ld_god(player);
    selftest_tick(srv);
}

static int
ld_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    ld_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
ld_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
ld_free_type(struct ToriRSServer* srv, int npc_type)
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
ld_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ld_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
ld_quest(struct ToriRSServerPlayer* player)
{
    return ld_get_vb(player, "lunar_quest_main");
}

static void
ld_varp(struct ToriRSServer* srv, const char* name, int value)
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
ld_get_varp(struct ToriRSServerPlayer* player, const char* name)
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
ld_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
ld_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ld_talk(srv, npc_type, slot);
    ld_finish(srv);
}

static void
ld_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    ld_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        ld_click_until_menu(srv, 24);
        ld_pick_row(srv, rows[i]);
    }
    ld_finish(srv);
}

static void
ld_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
ld_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
ld_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
ld_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        ld_set_stat(player, stat, 99);
}

static void
ld_prereqs(struct ToriRSServer* srv, int on)
{
    assert(srv);
    ld_varp(srv, "viking", on ? LD_VIKING_DONE : 0);
    ld_varp(srv, "zanaris", on ? LD_ZANARIS_DONE : 0);
    ld_varp(srv, "runemysteries", on ? LD_RUNEMY_DONE : 0);
    ld_varp(srv, "zombiequeen", on ? LD_SHILO_DONE : 0);
}

static void
ld_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    ld_vb(srv, "lunar_quest_main", LD_NOT_STARTED);
    ld_vb(srv, "lunar_quest_symbolpres1", 0);
    ld_vb(srv, "lunar_quest_symbolpres2", 0);
    ld_vb(srv, "lunar_quest_symbolpres3", 0);
    ld_vb(srv, "lunar_quest_symbolpres4", 0);
    ld_vb(srv, "lunar_quest_symbolpres5", 0);
    ld_vb(srv, "lunar_quest_springboard", 0);
    ld_vb(srv, "lunar_num_prog", 0);
    ld_vb(srv, "lunar_skill_prog", 0);
    ld_vb(srv, "lunar_tree_prog", 0);
    ld_vb(srv, "lunar_floor_prog", 0);
    ld_vb(srv, "lunar_dice_prog", 0);
    ld_vb(srv, "lunar_emote_prog", 0);
    ld_vb(srv, "lunar_monk_cape_intro", 0);
    ld_vb(srv, "lunar_monk_amulet_intro", 0);
    ld_vb(srv, "lunar_monk_ring_intro", 0);
    ld_vb(srv, "lunar_monk_tanclothes_intro", 0);
}

static void
ld_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,lunardip_journal]", NULL, 0);
    ld_finish(srv);
    ld_pass(step);
}

static void
ld_altar(struct ToriRSServer* srv, int element)
{
    int32_t arg;

    assert(srv);
    arg = element;
    ToriRSServer_ScriptsRunProc(srv, "[proc,lunardip_visit_altar]", &arg, 1);
    ld_finish(srv);
}

static void
ld_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    ld_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    ld_finish(srv);
}

static void
selftest_quest_lunardiplomacy(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_lokar;
    int npc_lookout;
    int npc_cabin;
    int npc_cook;
    int npc_mate;
    int npc_nav;
    int npc_captain;
    int npc_oneiro;
    int npc_baba;
    int npc_pauline;
    int npc_meteora;
    int npc_melana;
    int npc_selene;
    int npc_rimae;
    int npc_guard;
    int npc_suqah;
    int npc_num;
    int npc_expert;
    int npc_tree;
    int npc_guide;
    int npc_fluke;
    int npc_mimic;
    int npc_me;
    int obj_seal;
    int obj_tiara;
    int obj_dramen;
    int obj_helm;
    int obj_cape;
    int obj_amulet;
    int obj_torso;
    int obj_legs;
    int obj_gloves;
    int obj_boots;
    int obj_ring;
    int obj_astral;
    int stat_magic;
    int stat_craft;
    int stat_mine;
    int stat_wc;
    int stat_fm;
    int stat_def;
    int stat_herb;
    int stat_rc;
    int varp_qp;
    int slot_lokar;
    int slot_lookout;
    int slot_cabin;
    int slot_cook;
    int slot_mate;
    int slot_nav;
    int slot_captain;
    int slot_oneiro;
    int slot_baba;
    int slot_pauline;
    int slot_meteora;
    int slot_melana;
    int slot_selene;
    int slot_rimae;
    int slot_guard;
    int slot_suqah;
    int slot_num;
    int slot_expert;
    int slot_tree;
    int slot_guide;
    int slot_fluke;
    int slot_mimic;
    int slot_me;
    int magic_before;
    int rc_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };
    static const int k_mimic[] = { 1, 2 };
    static const int k_hub_nowhere[] = { 3 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: lunar diplomacy critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer lunar selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    ld_god(player);
    ld_reset_quest(srv);
    ld_clear_inv(player);
    ld_prereqs(srv, 0);

    npc_lokar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_fremennik_pirate_1op");
    if( npc_lokar <= 0 )
        npc_lokar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_fremennik_pirate");
    npc_lookout = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_pirate_lookout");
    npc_cabin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_pirate_cabin_boy");
    npc_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_pirate_cook");
    npc_mate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_pirate_first_mate");
    npc_nav = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_pirate_navigator");
    npc_captain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_pirate_captain");
    npc_oneiro = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_oneiromancer");
    npc_baba = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moonclan_baba_yaga");
    npc_pauline = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moonclan_monk1");
    npc_meteora = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moonclan_monk2");
    npc_melana = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moonclan_monk3");
    npc_selene = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moonclan_monk4");
    npc_rimae = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moonclan_monk5");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moonclan_guard");
    npc_suqah = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_suqka");
    npc_num = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moon_dream_numbers_game_man");
    npc_expert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moon_dream_power_game_man");
    npc_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moon_dream_trees_game_man");
    npc_guide = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moon_dream_jumping_game_man");
    npc_fluke = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moon_dream_dice_game_man");
    npc_mimic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lunar_moon_dream_music_game_man");
    npc_me = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "quest_lunar_mirror_of_player");
    obj_seal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_seal_of_passage");
    obj_tiara = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_tiara");
    obj_dramen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dramen_staff");
    obj_helm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_helmet");
    obj_cape = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_cape");
    obj_amulet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_amulet");
    obj_torso = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_torso");
    obj_legs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_legs");
    obj_gloves = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_gloves");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_boots");
    obj_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lunar_ring");
    obj_astral = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "astralrune");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_wc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_def = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "defence");
    stat_herb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_lunardiplomacy") > 0,
                   "dbrow quest_lunardiplomacy should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lunar_quest_main") >= 0,
                   "varbit lunar_quest_main should resolve");
    SELFTEST_CHECK(npc_lokar > 0, "npc lunar_fremennik_pirate_1op should resolve");
    SELFTEST_CHECK(npc_lookout > 0, "npc lunar_pirate_lookout should resolve");
    SELFTEST_CHECK(npc_cabin > 0, "npc lunar_pirate_cabin_boy should resolve");
    SELFTEST_CHECK(npc_cook > 0, "npc lunar_pirate_cook should resolve");
    SELFTEST_CHECK(npc_mate > 0, "npc lunar_pirate_first_mate should resolve");
    SELFTEST_CHECK(npc_nav > 0, "npc lunar_pirate_navigator should resolve");
    SELFTEST_CHECK(npc_captain > 0, "npc lunar_pirate_captain should resolve");
    SELFTEST_CHECK(npc_oneiro > 0, "npc lunar_oneiromancer should resolve");
    SELFTEST_CHECK(npc_baba > 0, "npc lunar_moonclan_baba_yaga should resolve");
    SELFTEST_CHECK(npc_pauline > 0, "npc lunar_moonclan_monk1 should resolve");
    SELFTEST_CHECK(npc_meteora > 0, "npc lunar_moonclan_monk2 should resolve");
    SELFTEST_CHECK(npc_melana > 0, "npc lunar_moonclan_monk3 should resolve");
    SELFTEST_CHECK(npc_selene > 0, "npc lunar_moonclan_monk4 should resolve");
    SELFTEST_CHECK(npc_rimae > 0, "npc lunar_moonclan_monk5 should resolve");
    SELFTEST_CHECK(npc_guard > 0, "npc lunar_moonclan_guard should resolve");
    SELFTEST_CHECK(npc_suqah > 0, "npc lunar_suqka should resolve");
    SELFTEST_CHECK(npc_num > 0, "npc ethereal numerator should resolve");
    SELFTEST_CHECK(npc_expert > 0, "npc ethereal expert should resolve");
    SELFTEST_CHECK(npc_tree > 0, "npc ethereal perceptive should resolve");
    SELFTEST_CHECK(npc_guide > 0, "npc ethereal guide should resolve");
    SELFTEST_CHECK(npc_fluke > 0, "npc ethereal fluke should resolve");
    SELFTEST_CHECK(npc_mimic > 0, "npc ethereal mimic should resolve");
    SELFTEST_CHECK(npc_me > 0, "npc quest_lunar_mirror_of_player should resolve");
    SELFTEST_CHECK(obj_seal > 0, "obj lunar_seal_of_passage should resolve");
    SELFTEST_CHECK(obj_tiara > 0, "obj lunar_tiara should resolve");
    SELFTEST_CHECK(obj_dramen > 0, "obj dramen_staff should resolve");
    SELFTEST_CHECK(obj_helm > 0 && obj_cape > 0 && obj_amulet > 0, "lunar helm/cape/amulet should resolve");
    SELFTEST_CHECK(obj_torso > 0 && obj_legs > 0 && obj_gloves > 0 && obj_boots > 0 && obj_ring > 0,
                   "lunar garments/ring should resolve");

    slot_lokar = ld_spawn(srv, npc_lokar, LD_RELLEKKA_X, LD_RELLEKKA_Z, 0);
    SELFTEST_CHECK(slot_lokar >= 0, "Lokar should spawn");

    ld_journal(srv, "journal_0_not_started");

    /* Qualify-fail: stats too low, prereqs already done. */
    ld_prereqs(srv, 1);
    ld_set_stat(player, stat_magic, 1);
    ld_set_stat(player, stat_craft, 1);
    ld_set_stat(player, stat_mine, 1);
    ld_set_stat(player, stat_wc, 1);
    ld_set_stat(player, stat_fm, 1);
    ld_set_stat(player, stat_def, 1);
    ld_set_stat(player, stat_herb, 1);
    ld_talk_finish(srv, npc_lokar, slot_lokar);
    SELFTEST_CHECK(ld_quest(player) == LD_NOT_STARTED, "low stats must not start the quest");
    ld_pass("lokar_qualify_fail_stats");

    /* Qualify-fail: stats ok, missing Fremennik Trials. */
    ld_skills99(player);
    ld_prereqs(srv, 0);
    ld_talk_finish(srv, npc_lokar, slot_lokar);
    SELFTEST_CHECK(ld_quest(player) == LD_NOT_STARTED, "missing prereqs must not start the quest");
    ld_pass("lokar_qualify_fail_prereqs");

    /* Refuse. */
    ld_prereqs(srv, 1);
    ld_talk_rows(srv, npc_lokar, slot_lokar, k_refuse, 1);
    SELFTEST_CHECK(ld_quest(player) == LD_NOT_STARTED, "refuse must leave the quest unstarted");
    SELFTEST_CHECK(ld_inv_total(player, obj_seal) == 0, "refuse must not grant the seal");
    ld_pass("lokar_refuse");

    /* Accept + seal. */
    ld_talk_rows(srv, npc_lokar, slot_lokar, k_accept, 1);
    SELFTEST_CHECK(ld_quest(player) == LD_ACCEPTED, "accept should set lunar_quest_main=10");
    SELFTEST_CHECK(ld_inv_total(player, obj_seal) >= 1, "accept should grant seal of passage");
    ld_pass("lokar_accept_seal");
    ld_journal(srv, "journal_10_accepted");

    /* Board ship. */
    ld_talk_finish(srv, npc_lokar, slot_lokar);
    SELFTEST_CHECK(ld_quest(player) == LD_ACCEPTED, "re-talk boards without advancing past accepted");
    ld_pass("lokar_board_ship");

    slot_lookout = ld_spawn(srv, npc_lookout, LD_SHIP_X, LD_SHIP_Z, LD_SHIP_LEVEL);
    slot_cabin = ld_spawn(srv, npc_cabin, LD_SHIP_X + 2, LD_SHIP_Z, LD_SHIP_LEVEL);
    slot_cook = ld_spawn(srv, npc_cook, LD_SHIP_X + 3, LD_SHIP_Z, LD_SHIP_LEVEL);
    slot_mate = ld_spawn(srv, npc_mate, LD_SHIP_X + 4, LD_SHIP_Z, LD_SHIP_LEVEL);
    slot_nav = ld_spawn(srv, npc_nav, LD_SHIP_X + 5, LD_SHIP_Z, LD_SHIP_LEVEL);
    slot_captain = ld_spawn(srv, npc_captain, LD_SHIP_X + 6, LD_SHIP_Z, LD_SHIP_LEVEL);
    SELFTEST_CHECK(slot_lookout >= 0 && slot_cabin >= 0 && slot_cook >= 0 &&
                       slot_mate >= 0 && slot_nav >= 0 && slot_captain >= 0,
                   "ship crew should spawn");

    ld_talk_finish(srv, npc_captain, slot_captain);
    ld_pass("captain_bentley");

    ld_talk_finish(srv, npc_lookout, slot_lookout);
    SELFTEST_CHECK(ld_quest(player) == LD_SHIP_BOARDED, "first crew talk boards the ship");
    SELFTEST_CHECK(ld_get_vb(player, "lunar_quest_symbolpres1") >= 1, "lookout fills symbol 1");
    ld_pass("lookout_symbol");
    ld_journal(srv, "journal_20_ship");

    ld_talk_finish(srv, npc_cabin, slot_cabin);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_quest_symbolpres2") >= 1, "cabin boy fills symbol 2");
    ld_pass("cabin_boy_symbol");

    ld_talk_finish(srv, npc_cook, slot_cook);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_quest_symbolpres3") >= 1, "cook fills symbol 3");
    ld_pass("cook_symbol");

    ld_talk_finish(srv, npc_mate, slot_mate);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_quest_symbolpres4") >= 1, "first mate fills symbol 4");
    ld_pass("first_mate_symbol");

    ld_talk_finish(srv, npc_nav, slot_nav);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_quest_symbolpres5") >= 1, "navigator fills symbol 5");
    SELFTEST_CHECK(ld_quest(player) == LD_SYMBOLS, "all five symbols advance to 30");
    ld_pass("navigator_symbol");
    ld_journal(srv, "journal_30_symbols");

    ld_free_npc(srv, slot_lookout);
    ld_free_npc(srv, slot_cabin);
    ld_free_npc(srv, slot_cook);
    ld_free_npc(srv, slot_mate);
    ld_free_npc(srv, slot_nav);
    ld_free_npc(srv, slot_captain);

    slot_oneiro = ld_spawn(srv, npc_oneiro, LD_HARBOUR_X, LD_HARBOUR_Z, 0);
    slot_baba = ld_spawn(srv, npc_baba, LD_HARBOUR_X + 2, LD_HARBOUR_Z, 0);
    slot_guard = ld_spawn(srv, npc_guard, LD_HARBOUR_X + 3, LD_HARBOUR_Z, 0);
    SELFTEST_CHECK(slot_oneiro >= 0 && slot_baba >= 0 && slot_guard >= 0,
                   "isle NPCs should spawn");

    ld_talk_finish(srv, npc_guard, slot_guard);
    ld_pass("clan_guard_halt");

    ld_talk_finish(srv, npc_oneiro, slot_oneiro);
    SELFTEST_CHECK(ld_quest(player) == LD_MET_ONEIRO, "Oneiromancer first talk sets 40");
    ld_pass("oneiro_first_task");
    ld_journal(srv, "journal_40_oneiro");

    ld_talk_finish(srv, npc_guard, slot_guard);
    ld_pass("clan_guard_vouches");

    ld_talk_finish(srv, npc_baba, slot_baba);
    SELFTEST_CHECK(ld_quest(player) == LD_MET_ONEIRO, "Baba Yaga refuses before the Suqah");
    ld_pass("baba_too_early");

    slot_suqah = ld_spawn(srv, npc_suqah, LD_HARBOUR_X + 4, LD_HARBOUR_Z, 0);
    SELFTEST_CHECK(slot_suqah >= 0, "Suqah should spawn");
    ld_kill(srv, npc_suqah, slot_suqah);
    SELFTEST_CHECK(ld_quest(player) == LD_SUQAH, "Suqah death sets 50");
    SELFTEST_CHECK(ld_inv_total(player, obj_tiara) >= 1, "Suqah grants lunar tiara");
    ld_pass("suqah_defeat");
    ld_journal(srv, "journal_50_suqah");
    ld_free_npc(srv, slot_suqah);

    ld_talk_finish(srv, npc_baba, slot_baba);
    SELFTEST_CHECK(ld_quest(player) == LD_POTION, "Baba Yaga potion sets 60");
    ld_pass("baba_potion");
    ld_journal(srv, "journal_60_potion");

    ld_give(player, obj_dramen, 1);
    ld_talk_finish(srv, npc_oneiro, slot_oneiro);
    SELFTEST_CHECK(ld_quest(player) == LD_STAFF_START, "Oneiromancer staff task sets 70");
    ld_pass("oneiro_staff_task");
    ld_journal(srv, "journal_70_staff");

    ld_altar(srv, LD_ALTAR_FIRE);
    SELFTEST_CHECK(ld_quest(player) == LD_STAFF_START, "wrong altar order must not advance");
    ld_pass("altar_wrong_order");

    ld_altar(srv, LD_ALTAR_AIR);
    SELFTEST_CHECK(ld_quest(player) == LD_STAFF_AIR, "Air altar sets 71");
    ld_pass("altar_air");

    ld_altar(srv, LD_ALTAR_FIRE);
    SELFTEST_CHECK(ld_quest(player) == LD_STAFF_FIRE, "Fire altar sets 72");
    ld_pass("altar_fire");

    ld_altar(srv, LD_ALTAR_WATER);
    SELFTEST_CHECK(ld_quest(player) == LD_STAFF_WATER, "Water altar sets 73");
    ld_pass("altar_water");

    ld_altar(srv, LD_ALTAR_EARTH);
    SELFTEST_CHECK(ld_quest(player) == LD_STAFF_EARTH, "Earth altar sets 74");
    ld_pass("altar_earth");
    ld_journal(srv, "journal_74_staff_done");

    ld_talk_finish(srv, npc_oneiro, slot_oneiro);
    SELFTEST_CHECK(ld_quest(player) == LD_REGALIA, "Oneiromancer regalia task sets 80");
    ld_pass("oneiro_regalia_task");
    ld_journal(srv, "journal_80_regalia");

    slot_pauline = ld_spawn(srv, npc_pauline, LD_HARBOUR_X + 5, LD_HARBOUR_Z, 0);
    slot_meteora = ld_spawn(srv, npc_meteora, LD_HARBOUR_X + 6, LD_HARBOUR_Z, 0);
    slot_melana = ld_spawn(srv, npc_melana, LD_HARBOUR_X + 7, LD_HARBOUR_Z, 0);
    slot_selene = ld_spawn(srv, npc_selene, LD_HARBOUR_X + 8, LD_HARBOUR_Z, 0);
    slot_rimae = ld_spawn(srv, npc_rimae, LD_HARBOUR_X + 9, LD_HARBOUR_Z, 0);
    SELFTEST_CHECK(slot_pauline >= 0 && slot_meteora >= 0 && slot_melana >= 0 &&
                       slot_selene >= 0 && slot_rimae >= 0,
                   "monks should spawn");

    ld_talk_finish(srv, npc_pauline, slot_pauline);
    SELFTEST_CHECK(ld_inv_total(player, obj_cape) >= 1, "Pauline grants lunar cape");
    ld_pass("pauline_cape");

    ld_talk_finish(srv, npc_meteora, slot_meteora);
    SELFTEST_CHECK(ld_inv_total(player, obj_amulet) >= 1, "Meteora grants lunar amulet");
    SELFTEST_CHECK(ld_inv_total(player, obj_tiara) == 0, "Meteora takes the Suqah tiara");
    ld_pass("meteora_amulet");

    ld_talk_finish(srv, npc_melana, slot_melana);
    SELFTEST_CHECK(ld_inv_total(player, obj_helm) >= 1, "Melana grants lunar helm");
    ld_pass("melana_helm");

    ld_talk_finish(srv, npc_selene, slot_selene);
    SELFTEST_CHECK(ld_inv_total(player, obj_ring) >= 1, "Selene grants lunar ring");
    ld_pass("selene_ring");

    ld_talk_finish(srv, npc_rimae, slot_rimae);
    SELFTEST_CHECK(ld_inv_total(player, obj_torso) >= 1 && ld_inv_total(player, obj_legs) >= 1 &&
                       ld_inv_total(player, obj_gloves) >= 1 && ld_inv_total(player, obj_boots) >= 1,
                   "Rimae grants tanned garments");
    ld_pass("rimae_garments");

    ld_talk_finish(srv, npc_oneiro, slot_oneiro);
    SELFTEST_CHECK(ld_quest(player) == LD_DREAM, "full regalia enters the dream");
    ld_pass("oneiro_dream_entry");
    ld_journal(srv, "journal_140_dream");

    ld_free_npc(srv, slot_pauline);
    ld_free_npc(srv, slot_meteora);
    ld_free_npc(srv, slot_melana);
    ld_free_npc(srv, slot_selene);
    ld_free_npc(srv, slot_rimae);
    ld_free_npc(srv, slot_baba);
    ld_free_npc(srv, slot_guard);

    slot_num = ld_spawn(srv, npc_num, LD_DREAM_X, LD_DREAM_Z, LD_DREAM_LEVEL);
    slot_expert = ld_spawn(srv, npc_expert, LD_DREAM_X + 1, LD_DREAM_Z, LD_DREAM_LEVEL);
    slot_tree = ld_spawn(srv, npc_tree, LD_DREAM_X + 2, LD_DREAM_Z, LD_DREAM_LEVEL);
    slot_guide = ld_spawn(srv, npc_guide, LD_DREAM_X + 3, LD_DREAM_Z, LD_DREAM_LEVEL);
    slot_fluke = ld_spawn(srv, npc_fluke, LD_DREAM_X + 4, LD_DREAM_Z, LD_DREAM_LEVEL);
    slot_mimic = ld_spawn(srv, npc_mimic, LD_DREAM_X + 5, LD_DREAM_Z, LD_DREAM_LEVEL);
    SELFTEST_CHECK(slot_num >= 0 && slot_expert >= 0 && slot_tree >= 0 &&
                       slot_guide >= 0 && slot_fluke >= 0 && slot_mimic >= 0,
                   "trial hosts should spawn");

    ld_talk_finish(srv, npc_num, slot_num);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_num_prog") >= 1, "Numerator trial");
    ld_pass("numerator_trial");

    ld_talk_finish(srv, npc_expert, slot_expert);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_skill_prog") >= 1, "Expert trial");
    ld_pass("expert_trial");

    ld_talk_finish(srv, npc_tree, slot_tree);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_tree_prog") >= 1, "Perceptive trial");
    ld_pass("perceptive_trial");

    ld_talk_finish(srv, npc_guide, slot_guide);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_floor_prog") >= 1, "Guide trial");
    ld_pass("guide_trial");

    ld_talk_finish(srv, npc_fluke, slot_fluke);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_dice_prog") >= 1, "Fluke trial");
    ld_pass("fluke_trial");

    ld_talk_rows(srv, npc_mimic, slot_mimic, k_mimic, 2);
    SELFTEST_CHECK(ld_get_vb(player, "lunar_emote_prog") >= 1, "Mimic p_choice4 wave then bow");
    SELFTEST_CHECK(ld_quest(player) == LD_GAMES, "all six trials set 150");
    ld_pass("mimic_p_choice4");
    ld_journal(srv, "journal_150_games");

    ld_free_npc(srv, slot_num);
    ld_free_npc(srv, slot_expert);
    ld_free_npc(srv, slot_tree);
    ld_free_npc(srv, slot_guide);
    ld_free_npc(srv, slot_fluke);
    ld_free_npc(srv, slot_mimic);

    slot_me = ld_spawn(srv, npc_me, LD_MIRROR_X, LD_MIRROR_Z, LD_MIRROR_LEVEL);
    SELFTEST_CHECK(slot_me >= 0, "Me should spawn");
    ld_kill(srv, npc_me, slot_me);
    SELFTEST_CHECK(ld_quest(player) == LD_MIRROR, "Me death sets 160");
    ld_pass("me_kill");
    ld_journal(srv, "journal_160_mirror");
    ld_free_npc(srv, slot_me);

    magic_before = player->stat_xp_tenths[stat_magic >= 0 ? stat_magic : TORIRSSERVER_STAT_MAGIC];
    rc_before = (stat_rc >= 0) ? player->stat_xp_tenths[stat_rc] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : ld_get_varp(player, "qp");

    ld_talk_finish(srv, npc_oneiro, slot_oneiro);
    SELFTEST_CHECK(ld_quest(player) == LD_COMPLETE, "Oneiromancer complete sets 190");
    SELFTEST_CHECK(player->stat_xp_tenths[stat_magic >= 0 ? stat_magic : TORIRSSERVER_STAT_MAGIC] >=
                       magic_before + LD_REWARD_TENTHS,
                   "complete awards 5000 Magic XP");
    if( stat_rc >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_rc] >= rc_before + LD_REWARD_TENTHS,
                       "complete awards 5000 Runecraft XP");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + LD_QP_REWARD, "complete awards 2 QP");
    SELFTEST_CHECK(ld_inv_total(player, obj_seal) >= 1, "complete grants seal of passage");
    if( obj_astral > 0 )
        SELFTEST_CHECK(ld_inv_total(player, obj_astral) >= 50, "complete grants 50 astral runes");
    ld_pass("complete_scroll");
    ld_journal(srv, "journal_190_complete");

    /* Post-quest Lokar hub. */
    ld_free_npc(srv, slot_oneiro);
    ld_free_npc(srv, slot_lokar);
    {
        int npc_hub = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "lunar_fremennik_pirate_by_pirateship");
        int slot_hub;

        if( npc_hub <= 0 )
            npc_hub = npc_lokar;
        slot_hub = ld_spawn(srv, npc_hub, LD_SHIP_X, LD_SHIP_Z, LD_SHIP_LEVEL);
        SELFTEST_CHECK(slot_hub >= 0, "post-quest Lokar should spawn");
        ld_talk_rows(srv, npc_hub, slot_hub, k_hub_nowhere, 1);
        ld_pass("lokar_post_p_choice3");
        ld_free_npc(srv, slot_hub);
    }

    ld_free_type(srv, npc_lokar);
    ld_free_type(srv, npc_lookout);
    ld_free_type(srv, npc_cabin);
    ld_free_type(srv, npc_cook);
    ld_free_type(srv, npc_mate);
    ld_free_type(srv, npc_nav);
    ld_free_type(srv, npc_captain);
    ld_free_type(srv, npc_oneiro);
    ld_free_type(srv, npc_baba);
    ld_free_type(srv, npc_pauline);
    ld_free_type(srv, npc_meteora);
    ld_free_type(srv, npc_melana);
    ld_free_type(srv, npc_selene);
    ld_free_type(srv, npc_rimae);
    ld_free_type(srv, npc_guard);
    ld_free_type(srv, npc_suqah);
    ld_free_type(srv, npc_num);
    ld_free_type(srv, npc_expert);
    ld_free_type(srv, npc_tree);
    ld_free_type(srv, npc_guide);
    ld_free_type(srv, npc_fluke);
    ld_free_type(srv, npc_mimic);
    ld_free_type(srv, npc_me);
    ToriRSServer_WorldNpcReap(srv);
    ld_clear_inv(player);
    ld_reset_quest(srv);
    ld_god(player);

    fprintf(stderr, "ToriRSServer lunar diplomacy selftest: walk finished\n");
}

#endif /* TORIRSSERVER_TEST_QUEST_LUNARDIPLOMACY_SELFTEST_U_H */
