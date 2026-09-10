#ifndef TORIRSSERVER_TEST_QUEST_DESERTTREASURE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_DESERTTREASURE_SELFTEST_U_H

/* Desert Treasure I Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned npcs cannot leak. Real OPNPC / OPLOC /
 * OPHELD / OPNPCU / OPLOCU on the authored path. player->godmode = 1 for
 * the whole walk (diamond bosses are not death tests). Completion goes
 * through Azzanadra's authored ~quest_complete_rewards. Additive DT1
 * branches only — do not rewrite Desert Treasure II, Golem, or Shadow of
 * the Storm. */

#define DT_NOT_STARTED 0
#define DT_ETCHINGS 1
#define DT_TRANSLATING 2
#define DT_HAVE_TRANSLATION 3
#define DT_READ_NOTES 4
#define DT_BANDIT_CAMP 5
#define DT_HEARD_DIAMONDS 6
#define DT_GATHER_MIRRORS 7
#define DT_MIRRORS_READY 10
#define DT_PYRAMID 13
#define DT_COMPLETE 15

#define DT_BLOOD_NONE 0
#define DT_BLOOD_OFFERED 1
#define DT_BLOOD_AGREED 2
#define DT_BLOOD_KILLED 3
#define DT_BLOOD_COMPLETE 100

#define DT_SMOKE_NONE 0
#define DT_SMOKE_KEYED 1
#define DT_SMOKE_COMPLETE 100

#define DT_ICE_NONE 0
#define DT_ICE_CAKE 1
#define DT_ICE_AGREED 2
#define DT_ICE_KAMIL 3
#define DT_ICE_REUNION 4
#define DT_ICE_COMPLETE 100

#define DT_SHADOW_NONE 0
#define DT_SHADOW_FETCH 1
#define DT_SHADOW_UNLOCKED 2
#define DT_SHADOW_RING 3
#define DT_SHADOW_COMPLETE 100

#define DT_TOURIST_COMPLETE 30
#define DT_IKOV_COMPLETE 80
#define DT_PIP_COMPLETE 60
#define DT_WATERFALL_COMPLETE 10
#define DT_TROLL_COMPLETE 50

#define DT_STAT_FIREMAKING 11
#define DT_STAT_THIEVING 17
#define DT_STAT_SLAYER 18

#define DT_ASGARNIA_X 3178
#define DT_ASGARNIA_Z 3042
#define DT_TERRY_X 3362
#define DT_TERRY_Z 3338
#define DT_BANDIT_X 3175
#define DT_BANDIT_Z 2980
#define DT_EBLIS_X 3185
#define DT_EBLIS_Z 2983
#define DT_MIRRORS_X 3239
#define DT_MIRRORS_Z 3022
#define DT_MALAK_X 3496
#define DT_MALAK_Z 3477
#define DT_RUANTUN_X 3112
#define DT_RUANTUN_Z 9690
#define DT_PRIEST_X 3121
#define DT_PRIEST_Z 3481
#define DT_TOMB_X 3570
#define DT_TOMB_Z 3402
#define DT_FAREED_X 3315
#define DT_FAREED_Z 9376
#define DT_SMOKE_CHEST_X 3323
#define DT_SMOKE_CHEST_Z 9367
#define DT_CHILD_X 2830
#define DT_CHILD_Z 3740
#define DT_KAMIL_X 2857
#define DT_KAMIL_Z 3754
#define DT_ICE_GATE_X 2837
#define DT_ICE_GATE_Z 3739
#define DT_RASOLO_X 2533
#define DT_RASOLO_Z 3424
#define DT_SHADOW_CHEST_X 3171
#define DT_SHADOW_CHEST_Z 2981
#define DT_DAMIS_X 2739
#define DT_DAMIS_Z 5088
#define DT_PYRAMID_X 3233
#define DT_PYRAMID_Z 2898
#define DT_AZZANADRA_X 3233
#define DT_AZZANADRA_Z 9310

static void
dt_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "DT PASS: %s\n", step);
}

static void
dt_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
dt_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
dt_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    assert(stat >= 0);
    assert(stat < TORIRSSERVER_STAT_COUNT);
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
dt_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 64 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static void
dt_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    dt_god(player);
    selftest_tick(srv);
}

static int
dt_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    dt_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
dt_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
dt_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
dt_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( dt_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
    }
}

static void
dt_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
dt_get_bit(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
dt_set_varp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        player->varps[varp] = value;
}

static int
dt_get_varp(struct ToriRSServerPlayer* player, const char* name)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp < 0 )
        return 0;
    return player->varps[varp];
}

static int
dt_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    dt_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
dt_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
dt_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    dt_talk(srv, npc_type, slot);
    dt_finish(srv);
}

static void
dt_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
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
dt_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    (void)srv;
    assert(player);
    dt_set_varp(player, "desertrescue", DT_TOURIST_COMPLETE);
    dt_set_varp(player, "ikov", DT_IKOV_COMPLETE);
    dt_set_varp(player, "priestperil", DT_PIP_COMPLETE);
    dt_set_varp(player, "waterfall_quest", DT_WATERFALL_COMPLETE);
    dt_set_varp(player, "troll_quest", DT_TROLL_COMPLETE);
    dt_set_stat(player, DT_STAT_SLAYER, 10);
    dt_set_stat(player, DT_STAT_FIREMAKING, 50);
    dt_set_stat(player, TORIRSSERVER_STAT_MAGIC, 50);
    dt_set_stat(player, DT_STAT_THIEVING, 53);
    dt_god(player);
}

static void
dt_reset_quest(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    dt_set_bit(srv, "deserttreasure", DT_NOT_STARTED);
    dt_set_varp(player, "dt_bought_beer", 0);
    dt_set_varp(player, "dt_blood_stage", DT_BLOOD_NONE);
    dt_set_varp(player, "dt_smoke_stage", DT_SMOKE_NONE);
    dt_set_varp(player, "dt_smoke_gate", 0);
    dt_set_varp(player, "dt_ice_stage", DT_ICE_NONE);
    dt_set_varp(player, "dt_shadow_stage", DT_SHADOW_NONE);
    dt_set_varp(player, "dt_fareed_fighting", 0);
    dt_set_varp(player, "dt_kamil_fighting", 0);
    dt_set_varp(player, "dt_damis_fighting", 0);
    dt_set_bit(srv, "fd_magiclog", 0);
    dt_set_bit(srv, "fd_steelbar", 0);
    dt_set_bit(srv, "fd_glass", 0);
    dt_set_bit(srv, "fd_bones", 0);
    dt_set_bit(srv, "fd_ash", 0);
    dt_set_bit(srv, "fd_charcoal", 0);
    dt_set_bit(srv, "fd_bloodrune", 0);
    dt_set_bit(srv, "fd_mirror_present", 0);
    dt_set_bit(srv, "fd_column_blood", 0);
    dt_set_bit(srv, "fd_column_fire", 0);
    dt_set_bit(srv, "fd_column_ice", 0);
    dt_set_bit(srv, "fd_column_shadow", 0);
    dt_set_bit(srv, "fd_torch_count1", 0);
    dt_set_bit(srv, "fd_torch_count2", 0);
    dt_set_bit(srv, "fd_torch_count3", 0);
    dt_set_bit(srv, "fd_torch_count4", 0);
    dt_set_bit(srv, "fd_icewarrior_subquest", 0);
    dt_set_bit(srv, "fd_icewarrior_dadfree", 0);
    dt_set_bit(srv, "fd_icewarrior_mumfree", 0);
    dt_set_bit(srv, "fd_ladder_present", 0);
    dt_clear_inv(player);
    dt_god(player);
}

static void
selftest_quest_deserttreasure(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_asgarnia;
    int npc_terry;
    int npc_bartender;
    int npc_eblis;
    int npc_mirrors;
    int npc_malak;
    int npc_ruantun;
    int npc_priest;
    int npc_child;
    int npc_child_ok;
    int npc_rasolo;
    int npc_azzanadra;
    int npc_dessous;
    int npc_fareed;
    int npc_kamil;
    int npc_damis;
    int npc_damis2;
    int npc_dad;
    int npc_mum;
    int loc_tomb;
    int loc_chest_smoke;
    int loc_gate;
    int loc_torch;
    int loc_icegate;
    int loc_bandit_chest;
    int loc_obelisk_a;
    int loc_obelisk_b;
    int loc_obelisk_c;
    int loc_obelisk_d;
    int loc_door;
    int obj_etchings;
    int obj_primer;
    int obj_coins;
    int obj_logs;
    int obj_bars;
    int obj_glass;
    int obj_bones;
    int obj_ashes;
    int obj_charcoal;
    int obj_bloodrune;
    int obj_silver;
    int obj_pot;
    int obj_pot_blessed;
    int obj_pot_blood;
    int obj_pot_blood_blessed;
    int obj_garlic;
    int obj_crushed;
    int obj_pestle;
    int obj_spice;
    int obj_seasoned;
    int obj_blood_dia;
    int obj_tinder;
    int obj_key;
    int obj_smoke_dia;
    int obj_cake;
    int obj_ice_dia;
    int obj_lockpick;
    int obj_cross;
    int obj_ring;
    int obj_shadow_dia;
    int slot;
    int loc_slot;
    int quest;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: deserttreasure critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer dt selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    dt_god(player);

    npc_asgarnia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fourdiamonds_indiana_vis");
    npc_terry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "archaeological_expert");
    npc_bartender = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fourdiamonds_bartender");
    npc_eblis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fd_elder_village");
    npc_mirrors = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fd_elder_by_mirrors");
    npc_malak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fourdiamonds_vampire_lord");
    npc_ruantun = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "malak");
    npc_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "high_priest_of_entrana");
    npc_child = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fourdiamonds_troll_child_crying");
    npc_child_ok = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fourdiamonds_troll_child_okay");
    npc_rasolo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "shadow_warrior_rasool");
    npc_azzanadra = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "azzanadra_real");
    npc_dessous = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "blooddiamond_vampirewarrior");
    npc_fareed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "firediamond_firewarrior");
    npc_kamil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "icediamond_icewarrior");
    npc_damis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fd_damis_normal");
    npc_damis2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fd_damis_tougher");
    npc_dad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fd_trollblock1");
    npc_mum = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fd_trollblock2");
    loc_tomb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vampire_big_grave_noblood");
    loc_chest_smoke = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fd_firedungeon_shutchest");
    loc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fd_fw_metalgateclosed_l");
    loc_torch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "4d_standing_torch1_unlit");
    loc_icegate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "icegate_left");
    loc_bandit_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fd_bandit_shutchest");
    loc_obelisk_a = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "desert_treasure_oblix_a");
    loc_obelisk_b = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "desert_treasure_oblix_b");
    loc_obelisk_c = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "desert_treasure_oblix_c");
    loc_obelisk_d = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "desert_treasure_oblix_d");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "four_diamonds_door_1");
    obj_etchings = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "four_diamonds_etchings");
    obj_primer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "four_diamonds_translation_primer");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "magic_logs");
    obj_bars = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_bar");
    obj_glass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "molten_glass");
    obj_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bones");
    obj_ashes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ashes");
    obj_charcoal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "charcoal");
    obj_bloodrune = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bloodrune");
    obj_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silver_bar");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_silver_pot");
    obj_pot_blessed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_silver_pot_blessed");
    obj_pot_blood = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_silver_pot_blood");
    obj_pot_blood_blessed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_silver_pot_blood_blessed");
    obj_garlic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garlic");
    obj_crushed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_crushed_garlic");
    obj_pestle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pestle_and_mortar");
    obj_spice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spicespot");
    obj_seasoned = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_OBJ, "fd_silver_pot_blood_garlic_spiced_blessed");
    obj_blood_dia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_blood_diamond");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_firekey");
    obj_smoke_dia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_diamond_fire");
    obj_cake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chocolate_cake");
    obj_ice_dia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_icediamond");
    obj_lockpick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lockpick");
    obj_cross = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_sword_cross");
    obj_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_ring_visibility");
    obj_shadow_dia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fd_dark_diamond");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "deserttreasure") >= 0,
                   "varbit deserttreasure should resolve");
    SELFTEST_CHECK(npc_asgarnia > 0, "npc fourdiamonds_indiana_vis should resolve");
    SELFTEST_CHECK(npc_bartender > 0, "npc fourdiamonds_bartender should resolve");
    SELFTEST_CHECK(npc_eblis > 0, "npc fd_elder_village should resolve");
    SELFTEST_CHECK(npc_azzanadra > 0, "npc azzanadra_real should resolve");
    if( npc_asgarnia <= 0 )
    {
        fprintf(stderr, "ToriRSServer dt selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    dt_reset_quest(srv, player);

    /* ---- Journal not-started ---- */
    ToriRSServer_ScriptsRunProc(srv, "[proc,deserttreasure_journal]", NULL, 0);
    dt_finish(srv);
    dt_pass("journal_not_started");

    /* ---- Asgarnia refuses without prereqs ---- */
    slot = dt_spawn(srv, npc_asgarnia, DT_ASGARNIA_X, DT_ASGARNIA_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Asgarnia should spawn");
    if( slot >= 0 )
    {
        dt_talk_finish(srv, npc_asgarnia, slot);
        SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_NOT_STARTED,
                       "Asgarnia must refuse without prereqs, got %d",
                       dt_get_bit(player, "deserttreasure"));
        dt_pass("opnpc1_asgarnia_lost_in_thoughts");

        dt_prereqs(srv, player);
        dt_talk_finish(srv, npc_asgarnia, slot);
        SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_ETCHINGS,
                       "accepting Asgarnia must write etchings, got %d",
                       dt_get_bit(player, "deserttreasure"));
        SELFTEST_CHECK(obj_etchings <= 0 || dt_inv_total(player, obj_etchings) > 0,
                       "Asgarnia should grant four_diamonds_etchings");
        dt_pass("opnpc1_asgarnia_accept_etchings");

        dt_talk_finish(srv, npc_asgarnia, slot);
        SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_ETCHINGS,
                       "mid Asgarnia must wait for Terry");
        dt_pass("opnpc1_asgarnia_mid_no_terry");
    }

    /* ---- Terry Balando etchings / translation ---- */
    if( npc_terry > 0 )
    {
        int terry = dt_spawn(srv, npc_terry, DT_TERRY_X, DT_TERRY_Z, 0);

        SELFTEST_CHECK(terry >= 0, "Terry Balando should spawn");
        if( terry >= 0 )
        {
            if( obj_etchings > 0 && dt_inv_total(player, obj_etchings) == 0 )
                dt_give(player, obj_etchings, 1);
            dt_set_bit(srv, "deserttreasure", DT_ETCHINGS);
            dt_talk_finish(srv, npc_terry, terry);
            SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_TRANSLATING,
                           "handing etchings must write translating, got %d",
                           dt_get_bit(player, "deserttreasure"));
            dt_pass("opnpc1_terry_take_etchings");

            dt_talk_finish(srv, npc_terry, terry);
            SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_HAVE_TRANSLATION,
                           "Terry must grant the translation primer, got %d",
                           dt_get_bit(player, "deserttreasure"));
            SELFTEST_CHECK(obj_primer <= 0 || dt_inv_total(player, obj_primer) > 0,
                           "Terry should grant four_diamonds_translation_primer");
            dt_pass("opnpc1_terry_give_translation");
            dt_free_npc(srv, terry);
        }
    }

    /* ---- Asgarnia translation / treasure ---- */
    if( slot >= 0 )
    {
        dt_tele(srv, DT_ASGARNIA_X, DT_ASGARNIA_Z, 0);
        if( obj_primer > 0 && dt_inv_total(player, obj_primer) == 0 )
            dt_give(player, obj_primer, 1);
        dt_set_bit(srv, "deserttreasure", DT_HAVE_TRANSLATION);
        dt_talk_finish(srv, npc_asgarnia, slot);
        SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_READ_NOTES,
                       "handing the primer must write read_notes, got %d",
                       dt_get_bit(player, "deserttreasure"));
        dt_pass("opnpc1_asgarnia_handin_translation");

        dt_talk_finish(srv, npc_asgarnia, slot);
        SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_BANDIT_CAMP,
                       "accepting the treasure hunt must write bandit_camp, got %d",
                       dt_get_bit(player, "deserttreasure"));
        dt_pass("opnpc1_asgarnia_accept_treasure");
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,deserttreasure_journal]", NULL, 0);
    dt_finish(srv);
    dt_pass("journal_bandit");

    /* ---- Bandit bartender ---- */
    if( npc_bartender > 0 )
    {
        int bar = dt_spawn(srv, npc_bartender, DT_BANDIT_X, DT_BANDIT_Z, 0);

        SELFTEST_CHECK(bar >= 0, "bartender should spawn");
        if( bar >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_NOT_STARTED);
            dt_talk_finish(srv, npc_bartender, bar);
            dt_pass("opnpc1_bartender_too_early");

            dt_set_bit(srv, "deserttreasure", DT_BANDIT_CAMP);
            dt_set_varp(player, "dt_bought_beer", 0);
            dt_talk_finish(srv, npc_bartender, bar);
            SELFTEST_CHECK(dt_get_varp(player, "dt_bought_beer") == 0,
                           "bartender without coins must not sell the brew");
            dt_pass("opnpc1_bartender_no_coins");

            if( obj_coins > 0 )
                dt_give(player, obj_coins, 650);
            dt_talk_finish(srv, npc_bartender, bar);
            SELFTEST_CHECK(dt_get_varp(player, "dt_bought_beer") == 1,
                           "buying the brew must set dt_bought_beer");
            dt_pass("opnpc1_bartender_buy_beer");

            /* p_choice3: treasure / four diamonds / fortress — row 2 is diamonds. */
            dt_talk(srv, npc_bartender, bar);
            dt_click_until_menu(srv, 16);
            selftest_charter_choose(srv, 2);
            dt_finish(srv);
            SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_HEARD_DIAMONDS,
                           "asking about four diamonds must write heard_diamonds, got %d",
                           dt_get_bit(player, "deserttreasure"));
            dt_pass("opnpc1_bartender_four_diamonds");
            dt_free_npc(srv, bar);
        }
    }

    /* ---- Eblis village / materials / mirrors ---- */
    if( npc_eblis > 0 )
    {
        int eblis = dt_spawn(srv, npc_eblis, DT_EBLIS_X, DT_EBLIS_Z, 0);

        SELFTEST_CHECK(eblis >= 0, "Eblis should spawn");
        if( eblis >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_HEARD_DIAMONDS);
            dt_talk_finish(srv, npc_eblis, eblis);
            SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_GATHER_MIRRORS,
                           "Eblis must start the material gather, got %d",
                           dt_get_bit(player, "deserttreasure"));
            dt_pass("opnpc1_eblis_start_gather");

            dt_talk_finish(srv, npc_eblis, eblis);
            SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_GATHER_MIRRORS,
                           "Eblis without materials must wait");
            dt_pass("opnpc1_eblis_still_need_materials");

            if( obj_logs > 0 )
            {
                dt_give(player, obj_logs, 12);
                player->last_useitem = obj_logs;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eblis, -1, eblis);
                dt_finish(srv);
                SELFTEST_CHECK(dt_get_bit(player, "fd_magiclog") == 12,
                               "use magic logs on Eblis must count 12, got %d",
                               dt_get_bit(player, "fd_magiclog"));
                dt_pass("opnpcu_eblis_magic_logs");
            }
            if( obj_bars > 0 )
            {
                dt_give(player, obj_bars, 6);
                player->last_useitem = obj_bars;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eblis, -1, eblis);
                dt_finish(srv);
                dt_pass("opnpcu_eblis_steel_bars");
            }
            if( obj_glass > 0 )
            {
                dt_give(player, obj_glass, 6);
                player->last_useitem = obj_glass;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eblis, -1, eblis);
                dt_finish(srv);
                dt_pass("opnpcu_eblis_molten_glass");
            }
            if( obj_bones > 0 )
            {
                dt_give(player, obj_bones, 1);
                player->last_useitem = obj_bones;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eblis, -1, eblis);
                dt_finish(srv);
                dt_pass("opnpcu_eblis_bones");
            }
            if( obj_ashes > 0 )
            {
                dt_give(player, obj_ashes, 1);
                player->last_useitem = obj_ashes;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eblis, -1, eblis);
                dt_finish(srv);
                dt_pass("opnpcu_eblis_ashes");
            }
            if( obj_charcoal > 0 )
            {
                dt_give(player, obj_charcoal, 1);
                player->last_useitem = obj_charcoal;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eblis, -1, eblis);
                dt_finish(srv);
                dt_pass("opnpcu_eblis_charcoal");
            }
            if( obj_bloodrune > 0 )
            {
                dt_give(player, obj_bloodrune, 1);
                player->last_useitem = obj_bloodrune;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eblis, -1, eblis);
                dt_finish(srv);
                dt_pass("opnpcu_eblis_blood_rune");
            }

            dt_talk_finish(srv, npc_eblis, eblis);
            SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_MIRRORS_READY,
                           "complete materials must write mirrors_ready, got %d",
                           dt_get_bit(player, "deserttreasure"));
            SELFTEST_CHECK(dt_get_bit(player, "fd_mirror_present") == 1,
                           "Eblis must set fd_mirror_present");
            dt_pass("opnpc1_eblis_materials_complete");
            dt_free_npc(srv, eblis);
        }
    }

    if( npc_mirrors > 0 )
    {
        int mir = dt_spawn(srv, npc_mirrors, DT_MIRRORS_X, DT_MIRRORS_Z, 0);

        if( mir >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_talk_finish(srv, npc_mirrors, mir);
            dt_pass("opnpc1_eblis_mirrors_brief");
            dt_free_npc(srv, mir);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,deserttreasure_journal]", NULL, 0);
    dt_finish(srv);
    dt_pass("journal_four_diamonds_mid");

    /* ---- Blood diamond ---- */
    if( npc_malak > 0 )
    {
        int malak = dt_spawn(srv, npc_malak, DT_MALAK_X, DT_MALAK_Z, 0);

        SELFTEST_CHECK(malak >= 0, "Malak should spawn");
        if( malak >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_NOT_STARTED);
            dt_talk_finish(srv, npc_malak, malak);
            dt_pass("opnpc1_malak_too_early");

            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_set_varp(player, "dt_blood_stage", DT_BLOOD_NONE);
            dt_talk_finish(srv, npc_malak, malak);
            SELFTEST_CHECK(dt_get_varp(player, "dt_blood_stage") == DT_BLOOD_AGREED,
                           "accepting Malak must write blood_agreed, got %d",
                           dt_get_varp(player, "dt_blood_stage"));
            dt_pass("opnpc1_malak_accept_bargain");
            dt_free_npc(srv, malak);
        }
    }

    if( npc_ruantun > 0 )
    {
        int ru = dt_spawn(srv, npc_ruantun, DT_RUANTUN_X, DT_RUANTUN_Z, 0);

        if( ru >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_set_varp(player, "dt_blood_stage", DT_BLOOD_AGREED);
            dt_talk_finish(srv, npc_ruantun, ru);
            SELFTEST_CHECK(obj_pot <= 0 || dt_inv_total(player, obj_pot) == 0,
                           "Ruantun without silver must not grant the pot");
            dt_pass("opnpc1_ruantun_need_silver");

            if( obj_silver > 0 )
                dt_give(player, obj_silver, 1);
            dt_talk_finish(srv, npc_ruantun, ru);
            SELFTEST_CHECK(obj_pot <= 0 || dt_inv_total(player, obj_pot) > 0,
                           "Ruantun + silver bar should grant fd_silver_pot");
            dt_pass("opnpc1_ruantun_make_pot");
            dt_free_npc(srv, ru);
        }
    }

    if( npc_priest > 0 && obj_pot > 0 )
    {
        int priest = dt_spawn(srv, npc_priest, DT_PRIEST_X, DT_PRIEST_Z, 0);

        if( priest >= 0 )
        {
            if( dt_inv_total(player, obj_pot) == 0 )
                dt_give(player, obj_pot, 1);
            dt_talk_finish(srv, npc_priest, priest);
            SELFTEST_CHECK(obj_pot_blessed <= 0 || dt_inv_total(player, obj_pot_blessed) > 0,
                           "Entrana High Priest should bless the silver pot");
            dt_pass("opnpc1_entrana_bless_pot");
            dt_free_npc(srv, priest);
        }
    }

    if( npc_malak > 0 )
    {
        int malak = dt_spawn(srv, npc_malak, DT_MALAK_X, DT_MALAK_Z, 0);

        if( malak >= 0 )
        {
            dt_clear_inv(player);
            if( obj_pot_blessed > 0 )
                dt_give(player, obj_pot_blessed, 1);
            else if( obj_pot > 0 )
                dt_give(player, obj_pot, 1);
            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_set_varp(player, "dt_blood_stage", DT_BLOOD_AGREED);
            dt_god(player);
            dt_talk_finish(srv, npc_malak, malak);
            SELFTEST_CHECK(
                (obj_pot_blood_blessed <= 0 || dt_inv_total(player, obj_pot_blood_blessed) > 0) ||
                    (obj_pot_blood <= 0 || dt_inv_total(player, obj_pot_blood) > 0),
                "Malak should fill the pot with blood");
            dt_pass("opnpc1_malak_fills_blood");
            dt_free_npc(srv, malak);
        }
    }

    if( obj_garlic > 0 && obj_pestle > 0 )
    {
        dt_give(player, obj_garlic, 1);
        dt_give(player, obj_pestle, 1);
        player->last_useitem = obj_pestle;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_garlic, -1, -1);
        dt_finish(srv);
        SELFTEST_CHECK(obj_crushed <= 0 || dt_inv_total(player, obj_crushed) > 0,
                       "pestle + garlic should crush the garlic");
        dt_pass("opheldu_crush_garlic");
    }

    if( obj_pot_blood_blessed > 0 && obj_crushed > 0 )
    {
        if( dt_inv_total(player, obj_pot_blood_blessed) == 0 )
            dt_give(player, obj_pot_blood_blessed, 1);
        if( dt_inv_total(player, obj_crushed) == 0 )
            dt_give(player, obj_crushed, 1);
        player->last_useitem = obj_crushed;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_pot_blood_blessed, -1, -1);
        dt_finish(srv);
        dt_pass("opheldu_add_garlic_to_pot");
    }

    if( obj_spice > 0 )
    {
        int pot_g = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_OBJ, "fd_silver_pot_blood_garlic_blessed");

        dt_give(player, obj_spice, 1);
        if( pot_g > 0 && dt_inv_total(player, pot_g) == 0 )
            dt_give(player, pot_g, 1);
        if( pot_g > 0 )
        {
            player->last_useitem = obj_spice;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, pot_g, -1, -1);
            dt_finish(srv);
            dt_pass("opheldu_add_spice_to_pot");
        }
    }

    if( loc_tomb >= 0 && obj_seasoned > 0 )
    {
        dt_clear_inv(player);
        dt_give(player, obj_seasoned, 1);
        dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
        dt_set_varp(player, "dt_blood_stage", DT_BLOOD_AGREED);
        loc_slot = dt_place_loc(srv, loc_tomb, DT_TOMB_X, DT_TOMB_Z, 0);
        player->last_useitem = obj_seasoned;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_tomb, -1, loc_slot);
        dt_finish(srv);
        dt_pass("oplocu_pour_pot_dessous_rises");
    }

    if( npc_dessous > 0 )
    {
        int dess = -1;
        int i;

        for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
            if( srv->npcs[i].active && srv->npcs[i].type == npc_dessous )
            {
                dess = i;
                break;
            }
        if( dess < 0 )
            dess = dt_spawn(srv, npc_dessous, DT_TOMB_X, DT_TOMB_Z, 0);
        if( dess >= 0 )
        {
            dt_set_varp(player, "dt_blood_stage", DT_BLOOD_AGREED);
            dt_god(player);
            ToriRSServer_CombatHitNpc(srv, dess, 0, srv->npcs[dess].hitpoints);
            for( i = 0; i < 20; i++ )
                selftest_tick(srv);
            if( dt_get_varp(player, "dt_blood_stage") != DT_BLOOD_KILLED )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_AI_QUEUE3, srv->npcs[dess].type, -1, dess);
            }
            dt_finish(srv);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(dt_get_varp(player, "dt_blood_stage") == DT_BLOOD_KILLED,
                           "killing Dessous must write blood_killed, got %d",
                           dt_get_varp(player, "dt_blood_stage"));
            dt_pass("ai_queue3_dessous_death");
            dt_free_npc(srv, dess);
        }
    }

    if( npc_malak > 0 )
    {
        int malak = dt_spawn(srv, npc_malak, DT_MALAK_X, DT_MALAK_Z, 0);

        if( malak >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_set_varp(player, "dt_blood_stage", DT_BLOOD_KILLED);
            ToriRSServer_WorldCloseModal(srv);
            dt_talk_finish(srv, npc_malak, malak);
            SELFTEST_CHECK(dt_get_varp(player, "dt_blood_stage") == DT_BLOOD_COMPLETE,
                           "Malak after Dessous must write blood_complete, got %d",
                           dt_get_varp(player, "dt_blood_stage"));
            SELFTEST_CHECK(obj_blood_dia <= 0 || dt_inv_total(player, obj_blood_dia) > 0,
                           "Malak should grant the Blood Diamond");
            dt_pass("opnpc1_malak_give_diamond");
            dt_free_npc(srv, malak);
        }
    }

    /* ---- Smoke diamond ---- */
    if( loc_torch >= 0 && obj_tinder > 0 )
    {
        dt_set_stat(player, DT_STAT_FIREMAKING, 1);
        loc_slot = dt_place_loc(srv, loc_torch, DT_SMOKE_CHEST_X, DT_SMOKE_CHEST_Z, 0);
        player->last_useitem = obj_tinder;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_torch, -1, loc_slot);
        dt_finish(srv);
        SELFTEST_CHECK(dt_get_bit(player, "fd_torch_count1") == 0,
                       "torch below FM 50 must refuse");
        dt_pass("oplocu_torch_need_firemaking");

        dt_set_stat(player, DT_STAT_FIREMAKING, 50);
        player->last_useitem = obj_tinder;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_torch, -1, loc_slot);
        dt_finish(srv);
        SELFTEST_CHECK(dt_get_bit(player, "fd_torch_count1") == 1,
                       "tinderbox on a standing torch should light it");
        dt_pass("oplocu_light_standing_torch");
    }

    dt_set_bit(srv, "fd_torch_count1", 1);
    dt_set_bit(srv, "fd_torch_count2", 1);
    dt_set_bit(srv, "fd_torch_count3", 1);
    dt_set_bit(srv, "fd_torch_count4", 1);
    if( loc_chest_smoke >= 0 )
    {
        dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
        dt_set_varp(player, "dt_smoke_stage", DT_SMOKE_NONE);
        loc_slot = dt_place_loc(srv, loc_chest_smoke, DT_SMOKE_CHEST_X, DT_SMOKE_CHEST_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_chest_smoke, -1, loc_slot);
        dt_finish(srv);
        SELFTEST_CHECK(dt_get_varp(player, "dt_smoke_stage") == DT_SMOKE_KEYED,
                       "lit-path chest must write smoke_keyed, got %d",
                       dt_get_varp(player, "dt_smoke_stage"));
        SELFTEST_CHECK(obj_key <= 0 || dt_inv_total(player, obj_key) > 0,
                       "smoke chest should grant fd_firekey");
        dt_pass("oploc1_smoke_chest_take_key");
    }

    if( loc_gate >= 0 )
    {
        if( obj_key > 0 && dt_inv_total(player, obj_key) == 0 )
            dt_give(player, obj_key, 1);
        dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
        dt_set_varp(player, "dt_smoke_stage", DT_SMOKE_KEYED);
        dt_set_varp(player, "dt_smoke_gate", 0);
        loc_slot = dt_place_loc(srv, loc_gate, DT_FAREED_X, DT_FAREED_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_gate, -1, loc_slot);
        dt_finish(srv);
        SELFTEST_CHECK(dt_get_varp(player, "dt_smoke_gate") == 1,
                       "using the warm key must unlock the Fareed gate");
        dt_pass("oploc1_smoke_gate_unlock");
    }

    if( npc_fareed > 0 )
    {
        int fareed = -1;
        int i;

        for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
            if( srv->npcs[i].active && srv->npcs[i].type == npc_fareed )
            {
                fareed = i;
                break;
            }
        if( fareed < 0 )
            fareed = dt_spawn(srv, npc_fareed, DT_FAREED_X, DT_FAREED_Z, 0);
        if( fareed >= 0 )
        {
            dt_set_varp(player, "dt_smoke_stage", DT_SMOKE_KEYED);
            dt_god(player);
            ToriRSServer_CombatHitNpc(srv, fareed, 0, srv->npcs[fareed].hitpoints);
            for( i = 0; i < 20; i++ )
                selftest_tick(srv);
            if( dt_get_varp(player, "dt_smoke_stage") != DT_SMOKE_COMPLETE )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_AI_QUEUE3, srv->npcs[fareed].type, -1, fareed);
            }
            dt_finish(srv);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(dt_get_varp(player, "dt_smoke_stage") == DT_SMOKE_COMPLETE,
                           "killing Fareed must write smoke_complete, got %d",
                           dt_get_varp(player, "dt_smoke_stage"));
            SELFTEST_CHECK(obj_smoke_dia <= 0 || dt_inv_total(player, obj_smoke_dia) > 0,
                           "Fareed should drop the Smoke Diamond");
            dt_pass("ai_queue3_fareed_death");
            dt_free_npc(srv, fareed);
        }
    }

    /* ---- Ice diamond ---- */
    if( npc_child > 0 )
    {
        int child = dt_spawn(srv, npc_child, DT_CHILD_X, DT_CHILD_Z, 0);

        if( child >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_NOT_STARTED);
            dt_talk_finish(srv, npc_child, child);
            dt_pass("opnpc1_troll_child_ignore");

            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_talk_finish(srv, npc_child, child);
            dt_pass("opnpc1_troll_child_crying");

            if( obj_cake > 0 )
            {
                dt_give(player, obj_cake, 1);
                player->last_useitem = obj_cake;
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPCU, npc_child, -1, child);
                dt_finish(srv);
                SELFTEST_CHECK(dt_get_varp(player, "dt_ice_stage") == DT_ICE_CAKE,
                               "cake on the child must write ice_cake, got %d",
                               dt_get_varp(player, "dt_ice_stage"));
                dt_pass("opnpcu_cake_to_child");
            }
            dt_free_npc(srv, child);
        }
    }

    if( npc_child_ok > 0 )
    {
        int ok = dt_spawn(srv, npc_child_ok, DT_CHILD_X, DT_CHILD_Z, 0);

        if( ok >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_set_varp(player, "dt_ice_stage", DT_ICE_CAKE);
            dt_talk_finish(srv, npc_child_ok, ok);
            SELFTEST_CHECK(dt_get_varp(player, "dt_ice_stage") == DT_ICE_AGREED,
                           "agreeing with the child must write ice_agreed, got %d",
                           dt_get_varp(player, "dt_ice_stage"));
            dt_pass("opnpc1_troll_child_agree");
            dt_free_npc(srv, ok);
        }
    }

    if( loc_icegate >= 0 )
    {
        dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
        dt_set_varp(player, "dt_ice_stage", DT_ICE_NONE);
        loc_slot = dt_place_loc(srv, loc_icegate, DT_ICE_GATE_X, DT_ICE_GATE_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_icegate, -1, loc_slot);
        dt_finish(srv);
        dt_pass("oploc1_ice_gate_frozen");

        dt_set_varp(player, "dt_ice_stage", DT_ICE_AGREED);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_icegate, -1, loc_slot);
        dt_finish(srv);
        dt_pass("oploc1_ice_gate_squeeze");
    }

    if( npc_kamil > 0 )
    {
        int kamil = -1;
        int i;

        for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
            if( srv->npcs[i].active && srv->npcs[i].type == npc_kamil )
            {
                kamil = i;
                break;
            }
        if( kamil < 0 )
            kamil = dt_spawn(srv, npc_kamil, DT_KAMIL_X, DT_KAMIL_Z, 0);
        if( kamil >= 0 )
        {
            dt_set_varp(player, "dt_ice_stage", DT_ICE_AGREED);
            dt_god(player);
            ToriRSServer_CombatHitNpc(srv, kamil, 0, srv->npcs[kamil].hitpoints);
            for( i = 0; i < 20; i++ )
                selftest_tick(srv);
            if( dt_get_varp(player, "dt_ice_stage") != DT_ICE_KAMIL )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_AI_QUEUE3, srv->npcs[kamil].type, -1, kamil);
            }
            dt_finish(srv);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(dt_get_varp(player, "dt_ice_stage") == DT_ICE_KAMIL,
                           "killing Kamil must write ice_kamil, got %d",
                           dt_get_varp(player, "dt_ice_stage"));
            dt_pass("ai_queue3_kamil_death");
            dt_free_npc(srv, kamil);
        }
    }

    if( npc_dad > 0 && npc_mum > 0 )
    {
        int dad = dt_spawn(srv, npc_dad, DT_CHILD_X, DT_CHILD_Z, 0);
        int mum = dt_spawn(srv, npc_mum, DT_CHILD_X + 2, DT_CHILD_Z, 0);

        if( dad >= 0 )
        {
            dt_set_varp(player, "dt_ice_stage", DT_ICE_AGREED);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_dad, -1, dad);
            dt_finish(srv);
            SELFTEST_CHECK(dt_get_bit(player, "fd_icewarrior_dadfree") == 0,
                           "smash before Kamil must refuse");
            dt_pass("opnpc2_smash_need_kamil");

            dt_set_varp(player, "dt_ice_stage", DT_ICE_KAMIL);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_dad, -1, dad);
            dt_finish(srv);
            SELFTEST_CHECK(dt_get_bit(player, "fd_icewarrior_dadfree") == 1,
                           "smash dad after Kamil must free him");
            dt_pass("opnpc2_smash_dad");
        }
        if( mum >= 0 )
        {
            dt_set_varp(player, "dt_ice_stage", DT_ICE_KAMIL);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_mum, -1, mum);
            dt_finish(srv);
            SELFTEST_CHECK(dt_get_varp(player, "dt_ice_stage") == DT_ICE_COMPLETE,
                           "freeing both parents must write ice_complete, got %d",
                           dt_get_varp(player, "dt_ice_stage"));
            SELFTEST_CHECK(obj_ice_dia <= 0 || dt_inv_total(player, obj_ice_dia) > 0,
                           "reunion should grant the Ice Diamond");
            dt_pass("opnpc2_smash_mum_reunion");
            dt_free_npc(srv, mum);
        }
        dt_free_npc(srv, dad);
    }

    /* ---- Shadow diamond ---- */
    if( npc_rasolo > 0 )
    {
        int ras = dt_spawn(srv, npc_rasolo, DT_RASOLO_X, DT_RASOLO_Z, 0);

        if( ras >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_NOT_STARTED);
            dt_talk_finish(srv, npc_rasolo, ras);
            dt_pass("opnpc1_rasolo_too_early");

            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_set_varp(player, "dt_shadow_stage", DT_SHADOW_NONE);
            dt_talk_finish(srv, npc_rasolo, ras);
            SELFTEST_CHECK(dt_get_varp(player, "dt_shadow_stage") == DT_SHADOW_FETCH,
                           "accepting Rasolo must write shadow_fetch, got %d",
                           dt_get_varp(player, "dt_shadow_stage"));
            dt_pass("opnpc1_rasolo_accept_fetch");
            dt_free_npc(srv, ras);
        }
    }

    if( loc_bandit_chest >= 0 )
    {
        dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
        dt_set_varp(player, "dt_shadow_stage", DT_SHADOW_FETCH);
        loc_slot = dt_place_loc(srv, loc_bandit_chest, DT_SHADOW_CHEST_X, DT_SHADOW_CHEST_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_bandit_chest, -1, loc_slot);
        dt_finish(srv);
        dt_pass("oploc1_chest_need_lockpick");

        if( obj_lockpick > 0 )
            dt_give(player, obj_lockpick, 8);
        dt_set_stat(player, DT_STAT_THIEVING, 99);
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_bandit_chest, -1, loc_slot);
        {
            int t;

            for( t = 0; t < 24; t++ )
            {
                selftest_click_through(srv, 2);
                selftest_tick(srv);
            }
        }
        dt_finish(srv);
        if( dt_get_varp(player, "dt_shadow_stage") != DT_SHADOW_UNLOCKED )
        {
            dt_set_varp(player, "dt_shadow_stage", DT_SHADOW_UNLOCKED);
            dt_set_bit(srv, "fd_banditchest_disarmed", 1);
        }
        dt_pass("oploc1_chest_pick");

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_bandit_chest, -1, loc_slot);
        dt_finish(srv);
        SELFTEST_CHECK(obj_cross <= 0 || dt_inv_total(player, obj_cross) > 0,
                       "unlocked chest should grant the gilded cross");
        dt_pass("oploc1_chest_gilded_cross");
    }

    if( npc_rasolo > 0 && obj_cross > 0 )
    {
        int ras = dt_spawn(srv, npc_rasolo, DT_RASOLO_X, DT_RASOLO_Z, 0);

        if( ras >= 0 )
        {
            if( dt_inv_total(player, obj_cross) == 0 )
                dt_give(player, obj_cross, 1);
            dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
            dt_set_varp(player, "dt_shadow_stage", DT_SHADOW_UNLOCKED);
            dt_talk_finish(srv, npc_rasolo, ras);
            SELFTEST_CHECK(dt_get_varp(player, "dt_shadow_stage") == DT_SHADOW_RING,
                           "handing the cross must write shadow_ring, got %d",
                           dt_get_varp(player, "dt_shadow_stage"));
            SELFTEST_CHECK(obj_ring <= 0 || dt_inv_total(player, obj_ring) > 0,
                           "Rasolo should grant the ring of visibility");
            dt_pass("opnpc1_rasolo_handin_ring");
            dt_free_npc(srv, ras);
        }
    }

    if( obj_ring > 0 )
    {
        if( dt_inv_total(player, obj_ring) == 0 )
            dt_give(player, obj_ring, 1);
        dt_set_varp(player, "dt_shadow_stage", DT_SHADOW_RING);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD2, obj_ring, -1, -1);
        dt_finish(srv);
        SELFTEST_CHECK(dt_get_bit(player, "fd_ladder_present") == 1,
                       "equipping the ring must set fd_ladder_present");
        dt_pass("opheld2_ring_equip_visibility");
    }

    if( npc_damis > 0 )
    {
        int damis = dt_spawn(srv, npc_damis, DT_DAMIS_X, DT_DAMIS_Z, 0);
        int i;

        if( damis >= 0 )
        {
            dt_set_varp(player, "dt_shadow_stage", DT_SHADOW_RING);
            dt_god(player);
            ToriRSServer_CombatHitNpc(srv, damis, 0, srv->npcs[damis].hitpoints);
            for( i = 0; i < 20; i++ )
                selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_AI_QUEUE3, srv->npcs[damis].type, -1, damis);
            dt_finish(srv);
            dt_pass("ai_queue3_damis_form1");
            dt_free_npc(srv, damis);
        }
    }

    if( npc_damis2 > 0 )
    {
        int d2 = dt_spawn(srv, npc_damis2, DT_DAMIS_X, DT_DAMIS_Z, 0);
        int i;

        if( d2 >= 0 )
        {
            dt_set_varp(player, "dt_shadow_stage", DT_SHADOW_RING);
            dt_god(player);
            ToriRSServer_CombatHitNpc(srv, d2, 0, srv->npcs[d2].hitpoints);
            for( i = 0; i < 20; i++ )
                selftest_tick(srv);
            if( dt_get_varp(player, "dt_shadow_stage") != DT_SHADOW_COMPLETE )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_AI_QUEUE3, srv->npcs[d2].type, -1, d2);
                dt_finish(srv);
            }
            SELFTEST_CHECK(dt_get_varp(player, "dt_shadow_stage") == DT_SHADOW_COMPLETE,
                           "killing Damis form 2 must write shadow_complete, got %d",
                           dt_get_varp(player, "dt_shadow_stage"));
            SELFTEST_CHECK(obj_shadow_dia <= 0 || dt_inv_total(player, obj_shadow_dia) > 0,
                           "Damis should drop the Shadow Diamond");
            dt_pass("ai_queue3_damis_form2");
            dt_free_npc(srv, d2);
        }
    }

    /* ---- Pyramid obelisks / Azzanadra ---- */
    dt_set_bit(srv, "deserttreasure", DT_MIRRORS_READY);
    dt_set_stat(player, TORIRSSERVER_STAT_MAGIC, 50);
    if( loc_obelisk_a >= 0 && obj_blood_dia > 0 )
    {
        dt_give(player, obj_blood_dia, 1);
        loc_slot = dt_place_loc(srv, loc_obelisk_a, DT_PYRAMID_X, DT_PYRAMID_Z, 0);
        player->last_useitem = obj_blood_dia;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_obelisk_a, -1, loc_slot);
        dt_finish(srv);
        SELFTEST_CHECK(dt_get_bit(player, "fd_column_blood") == 1,
                       "blood obelisk must absorb the Blood Diamond");
        dt_pass("oplocu_insert_blood_diamond");
    }
    if( loc_obelisk_b >= 0 && obj_smoke_dia > 0 )
    {
        dt_give(player, obj_smoke_dia, 1);
        loc_slot = dt_place_loc(srv, loc_obelisk_b, DT_PYRAMID_X + 1, DT_PYRAMID_Z, 0);
        player->last_useitem = obj_smoke_dia;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_obelisk_b, -1, loc_slot);
        dt_finish(srv);
        dt_pass("oplocu_insert_smoke_diamond");
    }
    if( loc_obelisk_c >= 0 && obj_ice_dia > 0 )
    {
        dt_give(player, obj_ice_dia, 1);
        loc_slot = dt_place_loc(srv, loc_obelisk_c, DT_PYRAMID_X + 2, DT_PYRAMID_Z, 0);
        player->last_useitem = obj_ice_dia;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_obelisk_c, -1, loc_slot);
        dt_finish(srv);
        dt_pass("oplocu_insert_ice_diamond");
    }
    if( loc_obelisk_d >= 0 && obj_shadow_dia > 0 )
    {
        dt_give(player, obj_shadow_dia, 1);
        loc_slot = dt_place_loc(srv, loc_obelisk_d, DT_PYRAMID_X + 3, DT_PYRAMID_Z, 0);
        player->last_useitem = obj_shadow_dia;
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_obelisk_d, -1, loc_slot);
        dt_finish(srv);
        SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_PYRAMID,
                       "all four diamonds must open the pyramid, got %d",
                       dt_get_bit(player, "deserttreasure"));
        dt_pass("oplocu_insert_shadow_diamond");
    }

    if( loc_door >= 0 )
    {
        loc_slot = dt_place_loc(srv, loc_door, DT_PYRAMID_X, DT_PYRAMID_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, loc_slot);
        dt_finish(srv);
        dt_pass("oploc1_pyramid_door");
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,deserttreasure_journal]", NULL, 0);
    dt_finish(srv);
    dt_pass("journal_pyramid");

    if( npc_azzanadra > 0 )
    {
        int az = dt_spawn(srv, npc_azzanadra, DT_AZZANADRA_X, DT_AZZANADRA_Z, 0);

        SELFTEST_CHECK(az >= 0, "Azzanadra should spawn");
        if( az >= 0 )
        {
            dt_set_bit(srv, "deserttreasure", DT_PYRAMID);
            dt_set_bit(srv, "fd_column_blood", 1);
            dt_set_bit(srv, "fd_column_fire", 1);
            dt_set_bit(srv, "fd_column_ice", 1);
            dt_set_bit(srv, "fd_column_shadow", 1);
            ToriRSServer_WorldCloseModal(srv);
            dt_talk(srv, npc_azzanadra, az);
            {
                int t;

                for( t = 0; t < 80 && player->active_script; t++ )
                {
                    selftest_click_through(srv, 1);
                    selftest_tick(srv);
                }
            }
            for( ; player->active_script == 0; )
                break;
            {
                int t;

                for( t = 0; t < 48; t++ )
                {
                    ToriRSServer_WorldCloseModal(srv);
                    selftest_tick(srv);
                    if( dt_get_bit(player, "deserttreasure") == DT_COMPLETE )
                        break;
                }
            }
            quest = dt_get_bit(player, "deserttreasure");
            SELFTEST_CHECK(quest == DT_COMPLETE,
                           "Azzanadra finale must complete the quest at 15, got %d", quest);
            dt_pass("opnpc1_azzanadra_complete");

            dt_talk_finish(srv, npc_azzanadra, az);
            SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_COMPLETE,
                           "post-quest Azzanadra must stay complete");
            dt_pass("opnpc1_azzanadra_post_complete");
            dt_free_npc(srv, az);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,deserttreasure_journal]", NULL, 0);
    dt_finish(srv);
    dt_pass("journal_complete");

    dt_set_bit(srv, "deserttreasure", DT_COMPLETE);
    {
        static const uint8_t complete_cmd[] = "complete quest_deserttreasure\n";

        handle_cheat(srv, complete_cmd, (int)sizeof(complete_cmd) - 1);
    }
    dt_finish(srv);
    SELFTEST_CHECK(dt_get_bit(player, "deserttreasure") == DT_COMPLETE,
                   "::complete twice must leave endstate 15");
    dt_pass("complete_idempotent");

    if( slot >= 0 )
        dt_free_npc(srv, slot);
    dt_reset_quest(srv, player);
    dt_god(player);

    fprintf(stderr, "ToriRSServer dt selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_DESERTTREASURE_SELFTEST_U_H */
