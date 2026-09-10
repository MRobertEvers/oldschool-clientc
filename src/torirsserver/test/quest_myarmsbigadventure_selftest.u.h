#ifndef TORIRSSERVER_TEST_QUEST_MYARMSBIGADVENTURE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_MYARMSBIGADVENTURE_SELFTEST_U_H

/* My Arm's Big Adventure Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Burntmeat / My Arm / Barnaby / Murcaily /
 * Roc / Death Plateau pot locs cannot leak. Real OPNPC1 / OPLOCU / OPNPC2 /
 * AI_QUEUE3 on the authored path. player->godmode = 1 for the whole walk
 * (not a death test).
 *
 * Burntmeat is reached through the existing
 * `[opnpc1,eadgar_troll_chief_cook]` dispatcher (Eadgar's Ruse -> My Arm ->
 * Making Friends). My Arm is reached through the existing
 * `[opnpc1,myarm_fixed]` splice. Murcaily / Barnaby / pot use the authored
 * headers in maba_travel.rs2. No second trigger is added.
 *
 * Gate: TORIRSSERVER_SELFTEST_MABA_ONLY=1
 *       (or TORIRSSERVER_SELFTEST_MYARM / TORIRSSERVER_SELFTEST_MYARMSBIGADVENTURE)
 */

#define MABA_NOT_STARTED 0
#define MABA_STARTED 10
#define MABA_LUMP_NEEDED 60
#define MABA_LUMP_RETURNED 70
#define MABA_PATCH_READY 110
#define MABA_PATCH_FERTILISED 140
#define MABA_ARRIVED_BRIMHAVEN 160
#define MABA_ARRIVED_TAI 170
#define MABA_MURCAILY_FAVOUR 200
#define MABA_FIGHT_PREP 230
#define MABA_PATCH_GROWN 240
#define MABA_READY_FOR_BABY 250
#define MABA_BABY_DEFEATED 260
#define MABA_GIANT_DEFEATED 270
#define MABA_HARVESTED 280
#define MABA_RETURN_BURNTMEAT 300
#define MABA_FINISH_READY 310
#define MABA_COMPLETE 320

#define MABA_FAKE_RAKE 6
#define MABA_FAKE_COMPOST 7
#define MABA_FAKE_HARDY 8
#define MABA_FAKE_DIBBER 9

#define MABA_DUNG_NEEDED 3
#define MABA_COMPOST_NEEDED 7
#define MABA_REQ_WC 10
#define MABA_REQ_FARM 29
#define MABA_REQ_FAVOUR 60

#define MABA_EADGAR_COMPLETE 110
#define MABA_FEUD_COMPLETE 28
#define MABA_JUNGLE_COMPLETE 12

#define MABA_REWARD_HERB_TENTHS 100000
#define MABA_REWARD_FARM_TENTHS 50000
#define MABA_REWARD_BURNT_MEAT 29
#define MABA_REWARD_QP 1

#define MABA_BURNTMEAT_X 2845
#define MABA_BURNTMEAT_Z 10057
#define MABA_BURNTMEAT_LV 1
#define MABA_KITCHEN_X 2855
#define MABA_KITCHEN_Z 10053
#define MABA_KITCHEN_LV 1
#define MABA_CAULDRON_X 2864
#define MABA_CAULDRON_Z 3591
#define MABA_ROOF_X 2835
#define MABA_ROOF_Z 3694
#define MABA_FIGHT_X 2829
#define MABA_FIGHT_Z 3695
#define MABA_ROC_X 2831
#define MABA_ROC_Z 3696
#define MABA_BARNABY_X 2683
#define MABA_BARNABY_Z 3275
#define MABA_TAI_X 2781
#define MABA_TAI_Z 3123
#define MABA_MURCAILY_X 2815
#define MABA_MURCAILY_Z 3083

static void
maba_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "MABA PASS: %s\n", step);
}

static void
maba_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
maba_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
maba_finish(struct ToriRSServer* srv)
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

static void
maba_ticks(struct ToriRSServer* srv, int n)
{
    int i;

    assert(srv);
    for( i = 0; i < n; i++ )
        selftest_tick(srv);
}

static int
maba_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
maba_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = maba_chatmenu();
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
maba_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = maba_chatmenu();
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
maba_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    maba_god(player);
    selftest_tick(srv);
}

static int
maba_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    maba_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
maba_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
maba_free_type(struct ToriRSServer* srv, int npc_type)
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
maba_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
maba_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( maba_inv_total(player, obj_id) >= count )
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
maba_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
maba_skills(struct ToriRSServerPlayer* player)
{
    int stat_wc;
    int stat_farm;
    int stat_herb;
    int stat_hp;

    assert(player);
    stat_wc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    stat_farm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "farming");
    stat_herb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
    stat_hp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hitpoints");
    maba_set_stat(player, stat_wc, 99);
    maba_set_stat(player, stat_farm, 99);
    maba_set_stat(player, stat_herb, 99);
    maba_set_stat(player, stat_hp, 99);
    if( stat_hp >= 0 )
    {
        player->max_hitpoints = 99;
        player->hitpoints = 99;
        ToriRSServer_CombatSyncHitpoints(player);
    }
}

static void
maba_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
maba_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
maba_varp(struct ToriRSServer* srv, const char* name, int value)
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
maba_get_varp(struct ToriRSServerPlayer* player, const char* name)
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

static int
maba_quest(struct ToriRSServerPlayer* player)
{
    return maba_get_vb(player, "myarm");
}

static void
maba_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    maba_vb(srv, "myarm", MABA_NOT_STARTED);
    maba_vb(srv, "myarm_dung", 0);
    maba_vb(srv, "myarm_supercompost", 0);
    maba_vb(srv, "myarm_shipchat", 0);
    maba_vb(srv, "myarm_tubers", 0);
    maba_vb(srv, "myarm_rakejoke", 0);
    maba_vb(srv, "myarm_dwarfjoke", 0);
    maba_vb(srv, "myarm_firstgiantroc", 0);
    maba_vb(srv, "myarm_barnabyswap", 0);
    maba_vb(srv, "myarm_fakepatch", 0);
}

static void
maba_prereqs(struct ToriRSServer* srv, int eadgar, int feud, int jungle, int favour)
{
    assert(srv);
    maba_varp(srv, "eadgar_quest", eadgar ? MABA_EADGAR_COMPLETE : 0);
    maba_vb(srv, "feud_var", feud ? MABA_FEUD_COMPLETE : 0);
    maba_varp(srv, "junglepotion", jungle ? MABA_JUNGLE_COMPLETE : 0);
    maba_vb(srv, "favour_percentage", favour);
}

static void
maba_ready(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    maba_reset_quest(srv);
    maba_clear_inv(player);
    maba_skills(player);
    maba_prereqs(srv, 1, 1, 1, 100);
    maba_god(player);
}

static void
maba_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
maba_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    maba_talk(srv, npc_type, slot);
    maba_finish(srv);
}

static void
maba_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    maba_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        maba_click_until_menu(srv, 24);
        maba_pick_row(srv, rows[i]);
    }
    maba_finish(srv);
}

static int
maba_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    maba_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
maba_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    maba_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
maba_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    npc->combat_target = srv->active_player->pid;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    maba_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    maba_finish(srv);
}

static void
maba_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,maba_journal]", NULL, 0);
    maba_finish(srv);
    maba_pass(step);
}

static void
selftest_quest_myarmsbigadventure(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_burnt;
    int npc_myarm;
    int npc_barnaby;
    int npc_murcaily;
    int npc_baby;
    int npc_giant;
    int loc_pot;
    int obj_lump;
    int obj_tubers;
    int obj_dung;
    int obj_compost;
    int obj_bucket;
    int obj_rake;
    int obj_dibber;
    int obj_spade;
    int obj_meat;
    int obj_pot;
    int stat_herb;
    int stat_farm;
    int varp_qp;
    int slot;
    int loc_slot;
    int herb_before;
    int farm_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: my arm's big adventure critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer maba selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    maba_god(player);
    maba_reset_quest(srv);
    maba_clear_inv(player);

    npc_burnt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eadgar_troll_chief_cook");
    npc_myarm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myarm_fixed");
    npc_barnaby = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myarm_barnaby");
    npc_murcaily = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tbwcu_murcaily");
    npc_baby = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myarm_baby_roc");
    npc_giant = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myarm_giant_roc");
    loc_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_troll_cauldron");
    obj_lump = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myarm_lump");
    obj_tubers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "myarm_hardytubers");
    obj_dung = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feud_camel_pooh_bucket");
    obj_compost = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_supercompost");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
    obj_rake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rake");
    obj_dibber = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dibber");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
    obj_meat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "burnt_meat");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    stat_herb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
    stat_farm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "farming");
    varp_qp = ToriRSServer_WorldVarp("qp");
    if( varp_qp < 0 )
        varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_myarmsbigadventure") > 0,
                   "dbrow quest_myarmsbigadventure should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "myarm") >= 0,
                   "varbit myarm should resolve");
    SELFTEST_CHECK(npc_burnt > 0, "npc eadgar_troll_chief_cook should resolve");
    SELFTEST_CHECK(npc_myarm > 0, "npc myarm_fixed should resolve");
    SELFTEST_CHECK(npc_barnaby > 0, "npc myarm_barnaby should resolve");
    SELFTEST_CHECK(npc_murcaily > 0, "npc tbwcu_murcaily should resolve");
    SELFTEST_CHECK(npc_baby > 0, "npc myarm_baby_roc should resolve");
    SELFTEST_CHECK(npc_giant > 0, "npc myarm_giant_roc should resolve");
    SELFTEST_CHECK(loc_pot > 0, "loc death_troll_cauldron should resolve");
    SELFTEST_CHECK(obj_lump > 0, "obj myarm_lump should resolve");
    SELFTEST_CHECK(obj_tubers > 0, "obj myarm_hardytubers should resolve");
    SELFTEST_CHECK(obj_dung > 0, "obj feud_camel_pooh_bucket should resolve");
    SELFTEST_CHECK(obj_compost > 0, "obj bucket_supercompost should resolve");
    SELFTEST_CHECK(obj_bucket > 0, "obj bucket_empty should resolve");
    SELFTEST_CHECK(obj_rake > 0, "obj rake should resolve");
    SELFTEST_CHECK(obj_dibber > 0, "obj dibber should resolve");
    SELFTEST_CHECK(obj_spade > 0, "obj spade should resolve");

    maba_journal(srv, "journal_0_not_started");

    /* Qualify-fail: Eadgar done so the dispatcher reaches MABA, other reqs off. */
    slot = maba_spawn(srv, npc_burnt, MABA_BURNTMEAT_X, MABA_BURNTMEAT_Z, MABA_BURNTMEAT_LV);
    SELFTEST_CHECK(slot >= 0, "Burntmeat should spawn");
    if( slot >= 0 )
    {
        maba_skills(player);
        maba_prereqs(srv, 1, 0, 0, 0);
        maba_talk_finish(srv, npc_burnt, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_NOT_STARTED,
                       "qualify-fail must stay not_started, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_burntmeat_qualify_fail");

        maba_ready(srv, player);
        maba_talk_rows(srv, npc_burnt, slot, k_refuse, 1);
        SELFTEST_CHECK(maba_quest(player) == MABA_NOT_STARTED,
                       "Burntmeat refuse must stay not_started");
        maba_pass("opnpc1_burntmeat_refuse");

        maba_talk_rows(srv, npc_burnt, slot, k_accept, 1);
        SELFTEST_CHECK(maba_quest(player) == MABA_STARTED,
                       "Burntmeat accept must write started, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_burntmeat_accept");

        maba_talk_finish(srv, npc_burnt, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_STARTED,
                       "mid-quest Burntmeat must stay started");
        maba_pass("opnpc1_burntmeat_mid_quest");
    }
    maba_journal(srv, "journal_10_started");

    maba_free_npc(srv, slot);
    slot = maba_spawn(srv, npc_myarm, MABA_KITCHEN_X, MABA_KITCHEN_Z, MABA_KITCHEN_LV);
    SELFTEST_CHECK(slot >= 0, "kitchen My Arm should spawn");
    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_STARTED);
        maba_talk_rows(srv, npc_myarm, slot, k_refuse, 1);
        SELFTEST_CHECK(maba_quest(player) == MABA_STARTED,
                       "My Arm kitchen refuse must stay started");
        maba_pass("opnpc1_myarm_kitchen_refuse");

        maba_talk_rows(srv, npc_myarm, slot, k_accept, 1);
        SELFTEST_CHECK(maba_quest(player) == MABA_LUMP_NEEDED,
                       "My Arm kitchen accept must write lump_needed, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_myarm_kitchen_accept");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_LUMP_NEEDED,
                       "lump reminder must stay lump_needed");
        maba_pass("opnpc1_myarm_lump_reminder");
    }
    maba_journal(srv, "journal_60_lump_needed");

    loc_slot = maba_place_loc(srv, loc_pot, MABA_CAULDRON_X, MABA_CAULDRON_Z, 0);
    SELFTEST_CHECK(loc_slot >= 0, "Death Plateau pot should place");
    if( loc_slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_NOT_STARTED);
        maba_clear_inv(player);
        maba_give(player, obj_bucket, 1);
        maba_use_loc(srv, loc_pot, loc_slot, obj_bucket);
        SELFTEST_CHECK(maba_inv_total(player, obj_lump) == 0,
                       "pot must not scoop before lump_needed");
        maba_pass("oplocu_cauldron_not_quest");

        maba_vb(srv, "myarm", MABA_LUMP_NEEDED);
        maba_use_loc(srv, loc_pot, loc_slot, obj_rake);
        SELFTEST_CHECK(maba_inv_total(player, obj_lump) == 0,
                       "wrong item on pot must not scoop");
        maba_pass("oplocu_cauldron_wrong_item");

        maba_clear_inv(player);
        maba_give(player, obj_bucket, 1);
        maba_use_loc(srv, loc_pot, loc_slot, obj_bucket);
        SELFTEST_CHECK(maba_inv_total(player, obj_lump) > 0,
                       "bucket on pot must scoop myarm_lump");
        SELFTEST_CHECK(maba_inv_total(player, obj_bucket) > 0,
                       "scoop must not consume the bucket");
        maba_pass("oplocu_cauldron_scoop");

        maba_use_loc(srv, loc_pot, loc_slot, obj_bucket);
        SELFTEST_CHECK(maba_inv_total(player, obj_lump) == 1,
                       "second scoop must stay at one lump");
        maba_pass("oplocu_cauldron_already");
    }

    maba_tele(srv, MABA_KITCHEN_X, MABA_KITCHEN_Z, MABA_KITCHEN_LV);
    if( slot >= 0 )
    {
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_LUMP_RETURNED,
                       "lump hand-in must write lump_returned, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_myarm_lump_handin");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_PATCH_READY,
                       "follow-to-roof must write patch_ready, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_myarm_follow_roof");
    }
    maba_journal(srv, "journal_110_patch_ready");

    maba_free_npc(srv, slot);
    slot = maba_spawn(srv, npc_myarm, MABA_ROOF_X, MABA_ROOF_Z, 0);
    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_PATCH_READY);
        maba_vb(srv, "myarm_dung", 0);
        maba_vb(srv, "myarm_supercompost", 0);
        maba_clear_inv(player);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_dung") == 0,
                       "need-dung talk must not write dung without buckets");
        maba_pass("opnpc1_myarm_need_dung");

        maba_give(player, obj_dung, MABA_DUNG_NEEDED);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_dung") == MABA_DUNG_NEEDED,
                       "dung hand-in must write dung=3, got %d",
                       maba_get_vb(player, "myarm_dung"));
        maba_pass("opnpc1_myarm_give_dung");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_supercompost") == 0,
                       "need-compost talk must not write compost without buckets");
        maba_pass("opnpc1_myarm_need_compost");

        maba_give(player, obj_compost, MABA_COMPOST_NEEDED);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_PATCH_FERTILISED,
                       "compost hand-in must write fertilised, got %d",
                       maba_quest(player));
        SELFTEST_CHECK(maba_get_vb(player, "myarm_supercompost") == MABA_COMPOST_NEEDED,
                       "compost hand-in must write supercompost=7");
        maba_pass("opnpc1_myarm_give_compost");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_PATCH_FERTILISED,
                       "Barnaby tip must stay fertilised");
        maba_pass("opnpc1_myarm_barnaby_tip");
    }
    maba_journal(srv, "journal_140_fertilised");

    maba_free_npc(srv, slot);
    slot = maba_spawn(srv, npc_barnaby, MABA_BARNABY_X, MABA_BARNABY_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Barnaby should spawn");
    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_NOT_STARTED);
        maba_talk_finish(srv, npc_barnaby, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_NOT_STARTED,
                       "early Barnaby must stay not_started");
        maba_pass("opnpc1_barnaby_ahoy");

        maba_vb(srv, "myarm", MABA_PATCH_FERTILISED);
        maba_talk_finish(srv, npc_barnaby, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_ARRIVED_BRIMHAVEN,
                       "Barnaby sail must write arrived_brimhaven, got %d",
                       maba_quest(player));
        SELFTEST_CHECK(maba_get_vb(player, "myarm_barnabyswap") == 1,
                       "sail must set cosmetic barnabyswap");
        maba_pass("opnpc1_barnaby_sail");

        maba_talk_finish(srv, npc_barnaby, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_ARRIVED_BRIMHAVEN,
                       "already-sailed talk must stay brimhaven");
        maba_pass("opnpc1_barnaby_already");
    }
    maba_journal(srv, "journal_160_brimhaven");

    maba_free_npc(srv, slot);
    slot = maba_spawn(srv, npc_myarm, MABA_TAI_X, MABA_TAI_Z, 0);
    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_ARRIVED_BRIMHAVEN);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_ARRIVED_TAI,
                       "Brimhaven My Arm must write arrived_tai, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_myarm_brimhaven_walk");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_ARRIVED_TAI,
                       "Tai tip must stay arrived_tai");
        maba_pass("opnpc1_myarm_tai_tip");
    }
    maba_journal(srv, "journal_170_tai");

    maba_free_npc(srv, slot);
    slot = maba_spawn(srv, npc_murcaily, MABA_MURCAILY_X, MABA_MURCAILY_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Murcaily should spawn");
    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_NOT_STARTED);
        maba_talk_finish(srv, npc_murcaily, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_NOT_STARTED,
                       "early Murcaily must stay not_started");
        maba_pass("opnpc1_murcaily_early");

        maba_vb(srv, "myarm", MABA_ARRIVED_TAI);
        maba_vb(srv, "favour_percentage", 10);
        maba_clear_inv(player);
        maba_talk_finish(srv, npc_murcaily, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_ARRIVED_TAI,
                       "favour-fail must stay arrived_tai");
        SELFTEST_CHECK(maba_inv_total(player, obj_tubers) == 0,
                       "favour-fail must not grant tubers");
        maba_pass("opnpc1_murcaily_favour_fail");

        maba_vb(srv, "favour_percentage", 100);
        maba_talk_finish(srv, npc_murcaily, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_MURCAILY_FAVOUR,
                       "tuber grant must write murcaily_favour, got %d",
                       maba_quest(player));
        SELFTEST_CHECK(maba_inv_total(player, obj_tubers) > 0,
                       "Murcaily must grant myarm_hardytubers");
        maba_pass("opnpc1_murcaily_tuber_grant");

        maba_talk_finish(srv, npc_murcaily, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_MURCAILY_FAVOUR,
                       "already-gave talk must stay murcaily_favour");
        maba_pass("opnpc1_murcaily_already");
    }
    maba_journal(srv, "journal_200_tubers");

    maba_free_npc(srv, slot);
    slot = maba_spawn(srv, npc_myarm, MABA_FIGHT_X, MABA_FIGHT_Z, 0);
    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_MURCAILY_FAVOUR);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_FIGHT_PREP,
                       "head-back talk must write fight_prep, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_myarm_head_back");
    }
    maba_journal(srv, "journal_230_fight_prep");

    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_FIGHT_PREP);
        maba_vb(srv, "myarm_fakepatch", 0);
        maba_vb(srv, "myarm_tubers", 0);
        maba_clear_inv(player);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_fakepatch") == 0,
                       "need-rake talk must not write fakepatch");
        maba_pass("opnpc1_myarm_need_rake");

        maba_give(player, obj_rake, 1);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_fakepatch") == MABA_FAKE_RAKE,
                       "rake hand-in must write fakepatch=6, got %d",
                       maba_get_vb(player, "myarm_fakepatch"));
        maba_pass("opnpc1_myarm_give_rake");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_fakepatch") == MABA_FAKE_RAKE,
                       "need-plant-compost must stay rake-given");
        maba_pass("opnpc1_myarm_need_plant_compost");

        maba_give(player, obj_compost, MABA_COMPOST_NEEDED);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_fakepatch") == MABA_FAKE_COMPOST,
                       "plant compost must write fakepatch=7, got %d",
                       maba_get_vb(player, "myarm_fakepatch"));
        maba_pass("opnpc1_myarm_give_plant_compost");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_tubers") == 0,
                       "need-tubers talk must not write tubers");
        maba_pass("opnpc1_myarm_need_tubers");

        maba_give(player, obj_tubers, 1);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_get_vb(player, "myarm_tubers") == 1,
                       "tuber hand-in must write tubers=1");
        SELFTEST_CHECK(maba_get_vb(player, "myarm_fakepatch") == MABA_FAKE_HARDY,
                       "tuber hand-in must write fakepatch=8");
        maba_pass("opnpc1_myarm_give_tubers");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_FIGHT_PREP,
                       "need-dibber must stay fight_prep");
        maba_pass("opnpc1_myarm_need_dibber");

        maba_give(player, obj_dibber, 1);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_PATCH_GROWN,
                       "dibber hand-in must write patch_grown, got %d",
                       maba_quest(player));
        SELFTEST_CHECK(maba_get_vb(player, "myarm_fakepatch") == MABA_FAKE_DIBBER,
                       "dibber hand-in must write fakepatch=9");
        maba_pass("opnpc1_myarm_give_dibber");
    }
    maba_journal(srv, "journal_240_grown");

    if( slot >= 0 )
    {
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_READY_FOR_BABY,
                       "hear-something talk must write ready_for_baby, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_myarm_hear_something");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_READY_FOR_BABY,
                       "roc-attacking talk must stay ready_for_baby");
        maba_pass("opnpc1_myarm_roc_attacking");
    }

    {
        int baby_slot;

        maba_free_type(srv, npc_baby);
        maba_free_type(srv, npc_giant);
        baby_slot = maba_spawn(srv, npc_baby, MABA_ROC_X, MABA_ROC_Z, 0);
        SELFTEST_CHECK(baby_slot >= 0, "Baby Roc should spawn");
        if( baby_slot >= 0 )
        {
            maba_kill(srv, npc_baby, baby_slot);
            SELFTEST_CHECK(maba_quest(player) == MABA_BABY_DEFEATED,
                           "Baby Roc death must write baby_defeated, got %d",
                           maba_quest(player));
            maba_pass("ai_queue3_baby_roc");
        }
        maba_free_npc(srv, baby_slot);
    }
    maba_journal(srv, "journal_260_baby");

    if( slot >= 0 )
    {
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_BABY_DEFEATED,
                       "giant-warning talk must stay baby_defeated");
        maba_pass("opnpc1_myarm_giant_warning");
    }

    {
        int giant_slot;

        maba_free_type(srv, npc_giant);
        giant_slot = maba_spawn(srv, npc_giant, MABA_ROC_X, MABA_ROC_Z, 0);
        SELFTEST_CHECK(giant_slot >= 0, "Giant Roc should spawn");
        if( giant_slot >= 0 )
        {
            maba_vb(srv, "myarm", MABA_BABY_DEFEATED);
            maba_kill(srv, npc_giant, giant_slot);
            SELFTEST_CHECK(maba_quest(player) == MABA_GIANT_DEFEATED,
                           "Giant Roc death must write giant_defeated, got %d",
                           maba_quest(player));
            maba_pass("ai_queue3_giant_roc");
        }
        maba_free_npc(srv, giant_slot);
    }
    maba_journal(srv, "journal_270_giant");

    if( slot >= 0 )
    {
        maba_clear_inv(player);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_GIANT_DEFEATED,
                       "need-spade talk must stay giant_defeated");
        maba_pass("opnpc1_myarm_need_spade");

        maba_give(player, obj_spade, 1);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_HARVESTED,
                       "spade hand-in must write harvested, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_myarm_harvest");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_RETURN_BURNTMEAT,
                       "harvested talk must write return_burntmeat, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_myarm_tell_burntmeat");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_RETURN_BURNTMEAT,
                       "waiting talk must stay return_burntmeat");
        maba_pass("opnpc1_myarm_waiting_burntmeat");
    }
    maba_journal(srv, "journal_280_harvested");

    maba_free_npc(srv, slot);
    slot = maba_spawn(srv, npc_burnt, MABA_BURNTMEAT_X, MABA_BURNTMEAT_Z, MABA_BURNTMEAT_LV);
    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_RETURN_BURNTMEAT);
        maba_prereqs(srv, 1, 1, 1, 100);
        maba_talk_finish(srv, npc_burnt, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_FINISH_READY,
                       "return Burntmeat must write finish_ready, got %d",
                       maba_quest(player));
        maba_pass("opnpc1_burntmeat_return");
    }
    maba_journal(srv, "journal_310_finish_ready");

    maba_free_npc(srv, slot);
    slot = maba_spawn(srv, npc_myarm, MABA_FIGHT_X, MABA_FIGHT_Z, 0);
    herb_before = (stat_herb >= 0) ? player->stat_xp_tenths[stat_herb] : 0;
    farm_before = (stat_farm >= 0) ? player->stat_xp_tenths[stat_farm] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
    if( slot >= 0 )
    {
        maba_vb(srv, "myarm", MABA_FINISH_READY);
        maba_clear_inv(player);
        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_COMPLETE,
                       "finish talk must write complete, got %d",
                       maba_quest(player));
        if( stat_herb >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_herb] >=
                               herb_before + MABA_REWARD_HERB_TENTHS,
                           "complete must advance Herblore by 100000 tenths");
        if( stat_farm >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_farm] >=
                               farm_before + MABA_REWARD_FARM_TENTHS,
                           "complete must advance Farming by 50000 tenths");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + MABA_REWARD_QP,
                           "complete must award 1 QP");
        if( obj_meat > 0 )
            SELFTEST_CHECK(maba_inv_total(player, obj_meat) >= MABA_REWARD_BURNT_MEAT,
                           "complete must grant 29 burnt meat");
        maba_pass("opnpc1_myarm_finish_complete");

        maba_talk_finish(srv, npc_myarm, slot);
        SELFTEST_CHECK(maba_quest(player) == MABA_COMPLETE,
                       "already-complete talk must stay complete");
        maba_pass("opnpc1_myarm_already_complete");
    }
    maba_journal(srv, "journal_320_complete");

    (void)obj_pot;
    maba_free_npc(srv, slot);
    maba_free_type(srv, npc_baby);
    maba_free_type(srv, npc_giant);
    maba_clear_inv(player);
    maba_reset_quest(srv);
    fprintf(stderr, "ToriRSServer maba selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_MYARMSBIGADVENTURE_SELFTEST_U_H */
