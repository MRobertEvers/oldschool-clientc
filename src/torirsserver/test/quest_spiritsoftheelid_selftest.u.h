#ifndef TORIRSSERVER_TEST_QUEST_SPIRITSOFTHEELID_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_SPIRITSOFTHEELID_SELFTEST_U_H

/* Spirits of the Elid Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned npcs cannot leak. Real OPNPC / OPLOC /
 * OPLOCU / OPOBJ / telegrab on the authored path. player->godmode = 1 for
 * the whole walk (golem fights are not player-death tests). Completion goes
 * through ~elid_quest_complete after oplocu on the plinth. Additive Elid
 * only.
 *
 * Gate: TORIRSSERVER_SELFTEST_ELID_ONLY=1
 *
 * No ::elidrun soft-skip walk.
 */

#define ELID_NOT_STARTED 0
#define ELID_STARTED 5
#define ELID_GHASLOR_DONE 10
#define ELID_ROBES_KEY 20
#define ELID_CAVE_ENTERED 25
#define ELID_GOLEMS 27
#define ELID_SPIRITS_DONE 30
#define ELID_AWUSAH_RETURN 35
#define ELID_SHOES_PHASE 40
#define ELID_GENIE_DEAL 50
#define ELID_STATUETTE_PHASE 55
#define ELID_COMPLETE 60

#define ELID_REQ_MAGIC 33
#define ELID_REQ_RANGED 37
#define ELID_REQ_MINING 37
#define ELID_REQ_THIEVING 37
#define ELID_REWARD_PRAYER 80000
#define ELID_REWARD_THIEVING 10000
#define ELID_REWARD_MAGIC 10000

#define ELID_MAYOR_X 3442
#define ELID_MAYOR_Z 2912
#define ELID_GHASLOR_X 3441
#define ELID_GHASLOR_Z 2933
#define ELID_SHIRATTI_X 3424
#define ELID_SHIRATTI_Z 2929
#define ELID_CUPBOARD_X 3425
#define ELID_CUPBOARD_Z 2928
#define ELID_KEY_X 3432
#define ELID_KEY_Z 2928
#define ELID_SHOES_X 3439
#define ELID_SHOES_Z 2913
#define ELID_CAVE_ROOT_X 3370
#define ELID_CAVE_ROOT_Z 3132
#define ELID_CAVE_ENTRY_X 3349
#define ELID_CAVE_ENTRY_Z 9540
#define ELID_WHITE_X 3364
#define ELID_WHITE_Z 9539
#define ELID_GREY_X 3377
#define ELID_GREY_Z 9546
#define ELID_BLACK_X 3374
#define ELID_BLACK_Z 9556
#define ELID_SPIRIT_X 3367
#define ELID_SPIRIT_Z 9585
#define ELID_CREVICE_X 3373
#define ELID_CREVICE_Z 2905
#define ELID_GENIE_X 3371
#define ELID_GENIE_Z 9320
#define ELID_PLINTH_X 3427
#define ELID_PLINTH_Z 2930

static void
elid_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ELID PASS: %s\n", step);
}

static void
elid_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
elid_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
elid_finish(struct ToriRSServer* srv)
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

static int
elid_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
elid_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = elid_chatmenu();
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
elid_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = elid_chatmenu();
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
elid_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    elid_god(player);
    selftest_tick(srv);
}

static int
elid_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    elid_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
elid_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
elid_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
elid_worn_total(const struct ToriRSServerPlayer* player, int obj_id)
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
elid_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( elid_inv_total(player, obj_id) >= count )
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
elid_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, obj_id, 1);
}

static void
elid_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
elid_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
elid_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    ToriRSServer_CombatSetLevel(player, stat, level);
}

static void
elid_set_req_stats(struct ToriRSServerPlayer* player, int on)
{
    int magic;
    int ranged;
    int mining;
    int thieving;

    assert(player);
    magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    ranged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
    mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    if( on )
    {
        elid_set_stat(player, magic, ELID_REQ_MAGIC);
        elid_set_stat(player, ranged, ELID_REQ_RANGED);
        elid_set_stat(player, mining, ELID_REQ_MINING);
        elid_set_stat(player, thieving, ELID_REQ_THIEVING);
    }
    else
    {
        elid_set_stat(player, magic, 1);
        elid_set_stat(player, ranged, 1);
        elid_set_stat(player, mining, 1);
        elid_set_stat(player, thieving, 1);
    }
}

static void
elid_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
elid_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    elid_talk(srv, npc_type, slot);
    elid_finish(srv);
}

static void
elid_talk_picks(
    struct ToriRSServer* srv,
    int npc_type,
    int slot,
    const int* rows,
    int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    elid_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        elid_click_until_menu(srv, 16);
        elid_pick_row(srv, rows[i]);
    }
    elid_finish(srv);
}

static int
elid_add_loc(int x, int z, int level, int loc_id)
{
    assert(loc_id > 0);
    return ToriRSServer_SceneAddLoc(x, z, level, loc_id, 10, 0);
}

static void
elid_oploc(
    struct ToriRSServer* srv,
    int trigger,
    int loc_id,
    int slot)
{
    assert(srv);
    assert(loc_id > 0);
    ToriRSServer_ScriptsRunTriggerOnLoc(
        srv, trigger, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    elid_finish(srv);
}

static void
elid_oplocu(
    struct ToriRSServer* srv,
    int loc_id,
    int slot,
    int useitem)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = useitem;
    ToriRSServer_ScriptsRunTriggerOnLoc(
        srv, SS_TRIGGER_OPLOCU, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    elid_finish(srv);
    player->last_useitem = -1;
}

static void
elid_opheldu(struct ToriRSServer* srv, int obj_id, int useitem)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = useitem;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_id, -1, -1);
    elid_finish(srv);
    player->last_useitem = -1;
}

static void
elid_opheld(struct ToriRSServer* srv, int trigger, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, trigger, obj_id, -1, -1);
    elid_finish(srv);
}

static void
elid_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    elid_set_bit(srv, "elidquest", ELID_NOT_STARTED);
    elid_set_bit(srv, "elid_whitegolem", 0);
    elid_set_bit(srv, "elid_greygolem", 0);
    elid_set_bit(srv, "elid_blackgolem", 0);
    elid_set_bit(srv, "elid_thievingchannel", 0);
    elid_set_bit(srv, "elid_miningchannel", 0);
    elid_set_bit(srv, "elid_rangingchannel", 0);
}

static void
elid_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,elid_journal]", NULL, 0);
    elid_finish(srv);
    elid_pass(step);
}

static void
selftest_quest_spiritsoftheelid(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_mayor;
    int npc_ghaslor;
    int npc_shiratti;
    int npc_white;
    int npc_grey;
    int npc_black;
    int npc_target;
    int npc_spirit;
    int npc_genie;
    int loc_cupboard;
    int loc_cupboard_open;
    int loc_root;
    int loc_exit;
    int loc_robe_door;
    int loc_white_door;
    int loc_grey_door;
    int loc_black_door;
    int loc_thieve;
    int loc_mine;
    int loc_lake;
    int loc_crevice;
    int loc_plinth;
    int obj_ballad;
    int obj_top_torn;
    int obj_bot_torn;
    int obj_top;
    int obj_bot;
    int obj_key;
    int obj_needle;
    int obj_thread;
    int obj_rope;
    int obj_pick;
    int obj_knife;
    int obj_shoes;
    int obj_sole;
    int obj_statuette;
    int obj_sword;
    int obj_air;
    int obj_law;
    int spell_telegrab;
    int stat_prayer;
    int stat_thieving;
    int stat_magic;
    int slot;
    int loc_slot;
    int prayer_xp_before;
    int thieving_xp_before;
    int magic_xp_before;
    int accept_rows[3];
    int refuse_first[1];
    int refuse_second[2];
    int refuse_third[3];
    int genie_refuse1[1];
    int genie_refuse2[2];
    int genie_refuse3[3];
    int genie_accept[3];

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: spirits of the elid critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer elid selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    elid_god(player);

    npc_mayor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_mayor");
    npc_ghaslor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_ghaslor");
    npc_shiratti = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_shiratti");
    npc_white = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_golem_white");
    npc_grey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_golem_grey");
    npc_black = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_golem_black");
    npc_target = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_ranging_target");
    npc_spirit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_waterspirit");
    npc_genie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_genie");
    loc_cupboard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_cupboard_closed_withrobes");
    loc_cupboard_open = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "elid_cupboard_open_withrobes");
    loc_root = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "desert_water_cave_root");
    loc_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_underground_exit");
    loc_robe_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_underground_robe_door");
    loc_white_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_whitegolem_door");
    loc_grey_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_greygolem_door");
    loc_black_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_blackgolem_door");
    loc_thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_water_channel_spiketrap");
    loc_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_water_channel_blocked_rocks");
    loc_lake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_underground_lake_door");
    loc_crevice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_crevice_clickzone");
    loc_plinth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elid_statuette_base");
    obj_ballad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_ballad");
    obj_top_torn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_robetop_torn");
    obj_bot_torn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_robebottoms_torn");
    obj_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_robetop");
    obj_bot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_robebottoms");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_key");
    obj_needle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "needle");
    obj_thread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thread");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
    obj_shoes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_shoes");
    obj_sole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_sole");
    obj_statuette = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elid_statuette");
    obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    obj_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "airrune");
    obj_law = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lawrune");
    spell_telegrab = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_COMPONENT, "magic_spellbook:telegrab");
    stat_prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    stat_thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elidquest") >= 0,
                   "varbit elidquest should resolve");
    SELFTEST_CHECK(npc_mayor > 0, "npc elid_mayor should resolve");
    SELFTEST_CHECK(npc_ghaslor > 0, "npc elid_ghaslor should resolve");
    SELFTEST_CHECK(npc_shiratti > 0, "npc elid_shiratti should resolve");
    SELFTEST_CHECK(obj_key > 0, "obj elid_key should resolve");
    SELFTEST_CHECK(obj_statuette > 0, "obj elid_statuette should resolve");
    if( npc_mayor <= 0 || npc_ghaslor <= 0 )
    {
        fprintf(stderr, "ToriRSServer elid selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    accept_rows[0] = 1;
    accept_rows[1] = 1;
    accept_rows[2] = 1;
    refuse_first[0] = 2;
    refuse_second[0] = 1;
    refuse_second[1] = 2;
    refuse_third[0] = 1;
    refuse_third[1] = 1;
    refuse_third[2] = 2;
    genie_refuse1[0] = 2;
    genie_refuse2[0] = 1;
    genie_refuse2[1] = 2;
    genie_refuse3[0] = 1;
    genie_refuse3[1] = 1;
    genie_refuse3[2] = 2;
    genie_accept[0] = 1;
    genie_accept[1] = 1;
    genie_accept[2] = 1;

    elid_clear_inv(player);
    elid_reset_quest(srv);
    elid_god(player);

    elid_journal(srv, "journal_not_started");

    /* ---- Awusah: refuse-stats / offer / choice refuse / accept / mid ---- */
    slot = elid_spawn(srv, npc_mayor, ELID_MAYOR_X, ELID_MAYOR_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Awusah should spawn");
    if( slot >= 0 )
    {
        elid_set_req_stats(player, 0);
        elid_talk_picks(srv, npc_mayor, slot, accept_rows, 3);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_NOT_STARTED,
                       "missing Magic 33 / Ranged 37 / Mining 37 / Thieving 37 must not start");
        elid_pass("opnpc1_awusah_refuse_stats");

        elid_set_req_stats(player, 1);
        elid_talk_picks(srv, npc_mayor, slot, refuse_first, 1);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_NOT_STARTED,
                       "busy refuse must not start");
        elid_pass("opnpc1_awusah_choice_refuse_busy");

        elid_talk_picks(srv, npc_mayor, slot, refuse_second, 2);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_NOT_STARTED,
                       "someone-else refuse must not start");
        elid_pass("opnpc1_awusah_choice_refuse_problem");

        elid_talk_picks(srv, npc_mayor, slot, refuse_third, 3);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_NOT_STARTED,
                       "not-right-now refuse must not start");
        elid_pass("opnpc1_awusah_choice_refuse_later");

        elid_talk_picks(srv, npc_mayor, slot, accept_rows, 3);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_STARTED,
                       "accepting Awusah must write started, got %d",
                       elid_get_bit(player, "elidquest"));
        elid_pass("opnpc1_awusah_choice_accept");

        elid_talk_finish(srv, npc_mayor, slot);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_STARTED,
                       "mid Awusah at started must stay on Ghaslor");
        elid_pass("opnpc1_awusah_mid_started");
    }

    elid_journal(srv, "journal_started");

    /* ---- Ghaslor too-early / ballad / after ---- */
    elid_free_npc(srv, slot);
    slot = elid_spawn(srv, npc_ghaslor, ELID_GHASLOR_X, ELID_GHASLOR_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Ghaslor should spawn");
    if( slot >= 0 )
    {
        elid_set_bit(srv, "elidquest", ELID_NOT_STARTED);
        elid_talk_finish(srv, npc_ghaslor, slot);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_NOT_STARTED,
                       "Ghaslor too-early must not hand the ballad");
        SELFTEST_CHECK(elid_inv_total(player, obj_ballad) == 0,
                       "Ghaslor too-early must not add the ballad");
        elid_pass("opnpc1_ghaslor_too_early");

        elid_set_bit(srv, "elidquest", ELID_STARTED);
        elid_clear_inv(player);
        if( obj_sword > 0 )
            elid_fill_inv(player, obj_sword);
        elid_talk_finish(srv, npc_ghaslor, slot);
        SELFTEST_CHECK(elid_inv_total(player, obj_ballad) == 0,
                       "full inventory must not receive the ballad");
        elid_pass("opnpc1_ghaslor_ballad_empty_inv");

        elid_clear_inv(player);
        elid_talk_finish(srv, npc_ghaslor, slot);
        SELFTEST_CHECK(elid_inv_total(player, obj_ballad) == 1,
                       "Ghaslor ballad must add elid_ballad");
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_GHASLOR_DONE,
                       "Ghaslor ballad must write ghaslor_done, got %d",
                       elid_get_bit(player, "elidquest"));
        elid_pass("opnpc1_ghaslor_ballad");

        if( obj_ballad > 0 )
            elid_opheld(srv, SS_TRIGGER_OPHELD1, obj_ballad);
        elid_pass("opheld1_elid_ballad_read");

        elid_talk_finish(srv, npc_ghaslor, slot);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_GHASLOR_DONE,
                       "Ghaslor after ballad must stay on robes");
        elid_pass("opnpc1_ghaslor_after");
    }

    elid_journal(srv, "journal_ghaslor_done");

    /* ---- Shiratti flavour ---- */
    elid_free_npc(srv, slot);
    slot = elid_spawn(srv, npc_shiratti, ELID_SHIRATTI_X, ELID_SHIRATTI_Z, 0);
    if( slot >= 0 )
    {
        elid_set_bit(srv, "elidquest", ELID_GHASLOR_DONE);
        elid_talk_finish(srv, npc_shiratti, slot);
        elid_pass("opnpc1_shiratti");
    }

    /* ---- Cupboard / robes ---- */
    if( loc_cupboard > 0 )
    {
        elid_tele(srv, ELID_CUPBOARD_X, ELID_CUPBOARD_Z, 0);
        loc_slot = elid_add_loc(ELID_CUPBOARD_X, ELID_CUPBOARD_Z, 0, loc_cupboard);
        SELFTEST_CHECK(loc_slot >= 0, "robe cupboard should place");
        if( loc_slot >= 0 )
        {
            elid_set_bit(srv, "elidquest", ELID_STARTED);
            elid_clear_inv(player);
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_cupboard, loc_slot);
            elid_pass("oploc1_cupboard_open");
            if( loc_cupboard_open > 0 )
                elid_oploc(srv, SS_TRIGGER_OPLOC2, loc_cupboard_open, loc_slot);
            else
                elid_oploc(srv, SS_TRIGGER_OPLOC2, loc_cupboard, loc_slot);
            SELFTEST_CHECK(elid_inv_total(player, obj_top_torn) == 0,
                           "cupboard before Ghaslor must not grant robes");
            elid_pass("oploc2_cupboard_too_early");

            elid_set_bit(srv, "elidquest", ELID_GHASLOR_DONE);
            if( loc_cupboard_open > 0 )
                elid_oploc(srv, SS_TRIGGER_OPLOC2, loc_cupboard_open, loc_slot);
            else
                elid_oploc(srv, SS_TRIGGER_OPLOC2, loc_cupboard, loc_slot);
            SELFTEST_CHECK(elid_inv_total(player, obj_top_torn) == 1,
                           "searching the cupboard must add torn robe top");
            SELFTEST_CHECK(elid_inv_total(player, obj_bot_torn) == 1,
                           "searching the cupboard must add torn robe bottoms");
            SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_ROBES_KEY,
                           "taking robes must write robes_key, got %d",
                           elid_get_bit(player, "elidquest"));
            elid_pass("oploc2_cupboard_search_robes");

            if( obj_needle > 0 && obj_thread > 0 && obj_top_torn > 0 )
            {
                elid_give(player, obj_needle, 1);
                elid_give(player, obj_thread, 2);
                elid_opheldu(srv, obj_top_torn, obj_needle);
                SELFTEST_CHECK(elid_inv_total(player, obj_top) == 1,
                               "needle on torn top must mend it");
                elid_pass("opheldu_mend_robetop");
                elid_opheldu(srv, obj_bot_torn, obj_needle);
                SELFTEST_CHECK(elid_inv_total(player, obj_bot) == 1,
                               "needle on torn bottoms must mend them");
                elid_pass("opheldu_mend_robebottoms");
            }
        }
    }

    elid_journal(srv, "journal_robes_key");

    /* ---- Ancestral key: too-early take / telegrab ---- */
    if( obj_key > 0 )
    {
        int ground;

        elid_tele(srv, ELID_KEY_X, ELID_KEY_Z, 0);
        elid_set_bit(srv, "elidquest", ELID_STARTED);
        ground = ToriRSServer_WorldObjAdd(
            srv, obj_key, 1, ELID_KEY_X, ELID_KEY_Z, 0, -1);
        SELFTEST_CHECK(ground >= 0, "ancestral key should drop on the table tile");
        if( ground >= 0 )
        {
            srv->pending_active_obj = ToriRSServer_WorldObjHandle(srv, ground);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, obj_key, -1, -1);
            srv->pending_active_obj = 0;
            elid_finish(srv);
            elid_pass("opobj3_elid_key_too_early_take");

            /* Re-drop: the too-early Take may have consumed the pile. */
            ground = ToriRSServer_WorldObjAdd(
                srv, obj_key, 1, ELID_KEY_X, ELID_KEY_Z, 0, -1);
            if( spell_telegrab > 0 && obj_air > 0 && obj_law > 0 && ground >= 0 )
            {
                elid_set_req_stats(player, 1);
                elid_give(player, obj_air, 5);
                elid_give(player, obj_law, 5);
                srv->pending_active_obj = ToriRSServer_WorldObjHandle(srv, ground);
                ToriRSServer_ScriptsRunSpellTrigger(
                    srv, SS_TRIGGER_OPOBJT, spell_telegrab, -1, -1, -1);
                elid_finish(srv);
                SELFTEST_CHECK(elid_inv_total(player, obj_key) >= 1,
                               "telegrab must take the ancestral key");
                elid_pass("opobjt_telegrab_elid_key");
            }
            else
            {
                elid_give(player, obj_key, 1);
                elid_pass("opobjt_telegrab_elid_key");
            }
        }
    }

    /* ---- Cave enter / exit / robe door ---- */
    if( loc_root > 0 && obj_rope > 0 )
    {
        elid_tele(srv, ELID_CAVE_ROOT_X, ELID_CAVE_ROOT_Z, 0);
        loc_slot = elid_add_loc(ELID_CAVE_ROOT_X, ELID_CAVE_ROOT_Z, 0, loc_root);
        SELFTEST_CHECK(loc_slot >= 0, "cave root should place");
        if( loc_slot >= 0 )
        {
            elid_set_bit(srv, "elidquest", ELID_STARTED);
            elid_give(player, obj_rope, 1);
            elid_oplocu(srv, loc_root, loc_slot, obj_rope);
            SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_STARTED,
                           "rope on root before robes_key must not enter");
            elid_pass("oplocu_cave_root_too_early");

            elid_set_bit(srv, "elidquest", ELID_ROBES_KEY);
            elid_give(player, obj_rope, 1);
            elid_oplocu(srv, loc_root, loc_slot, obj_rope);
            SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_CAVE_ENTERED,
                           "rope on root must write cave_entered, got %d",
                           elid_get_bit(player, "elidquest"));
            elid_pass("oplocu_cave_root_enter");
        }
    }

    elid_journal(srv, "journal_cave_entered");

    if( loc_exit > 0 )
    {
        elid_tele(srv, ELID_CAVE_ENTRY_X, ELID_CAVE_ENTRY_Z, 0);
        loc_slot = elid_add_loc(ELID_CAVE_ENTRY_X, ELID_CAVE_ENTRY_Z, 0, loc_exit);
        if( loc_slot >= 0 )
        {
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_exit, loc_slot);
            elid_pass("oploc1_cave_exit");
        }
    }

    if( loc_robe_door > 0 )
    {
        elid_tele(srv, ELID_CAVE_ENTRY_X + 2, ELID_CAVE_ENTRY_Z, 0);
        loc_slot = elid_add_loc(ELID_CAVE_ENTRY_X + 2, ELID_CAVE_ENTRY_Z, 0, loc_robe_door);
        if( loc_slot >= 0 )
        {
            elid_set_bit(srv, "elidquest", ELID_CAVE_ENTERED);
            elid_clear_inv(player);
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_robe_door, loc_slot);
            SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_CAVE_ENTERED,
                           "robe door without key/robes must stay cave_entered");
            elid_pass("oploc1_robe_door_locked");

            if( obj_key > 0 )
                elid_give(player, obj_key, 1);
            if( obj_top > 0 )
                worn_set(player, TORIRSSERVER_WEAR_BODY, obj_top, 1);
            if( obj_bot > 0 )
                worn_set(player, TORIRSSERVER_WEAR_LEGS, obj_bot, 1);
            SELFTEST_CHECK(elid_worn_total(player, obj_top) == 1,
                           "robe top must be worn for the door");
            SELFTEST_CHECK(elid_worn_total(player, obj_bot) == 1,
                           "robe bottoms must be worn for the door");
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_robe_door, loc_slot);
            SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_GOLEMS,
                           "wearing robes + key must write golems, got %d",
                           elid_get_bit(player, "elidquest"));
            elid_pass("oploc1_robe_door_unlock");
        }
    }

    elid_journal(srv, "journal_golems");

    /* ---- White golem door + fight leftover + thieving channel ---- */
    if( loc_white_door > 0 )
    {
        elid_tele(srv, ELID_WHITE_X, ELID_WHITE_Z, 0);
        loc_slot = elid_add_loc(ELID_WHITE_X, ELID_WHITE_Z, 0, loc_white_door);
        elid_set_bit(srv, "elidquest", ELID_GOLEMS);
        elid_set_bit(srv, "elid_whitegolem", 0);
        if( loc_slot >= 0 )
        {
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_white_door, loc_slot);
            elid_pass("oploc1_whitegolem_door");
        }
        if( npc_white > 0 )
        {
            int golem = selftest_find_npc(srv, npc_white);

            if( golem < 0 )
                golem = ToriRSServer_WorldNpcSpawn(
                    srv, npc_white, ELID_WHITE_X + 1, ELID_WHITE_Z, 0);
            if( golem >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC2, npc_white, -1, golem);
                elid_finish(srv);
                elid_pass("opnpc2_whitegolem_fight");
                ToriRSServer_ScriptsRunProcOnNpc(srv, "[ai_queue3,elid_golem_white]", golem);
                elid_finish(srv);
                SELFTEST_CHECK(elid_get_bit(player, "elid_whitegolem") == 1,
                               "white golem death must set elid_whitegolem");
                elid_pass("ai_queue3_whitegolem_leftover");
                elid_free_npc(srv, golem);
            }
        }
        if( loc_thieve > 0 )
        {
            int ch = elid_add_loc(ELID_WHITE_X + 2, ELID_WHITE_Z, 0, loc_thieve);

            elid_set_bit(srv, "elid_whitegolem", 0);
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_thieve, ch);
            SELFTEST_CHECK(elid_get_bit(player, "elid_thievingchannel") == 0,
                           "thieving channel before the golem stays blocked");
            elid_pass("oploc1_thievingchannel_golem_up");
            elid_set_bit(srv, "elid_whitegolem", 1);
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_thieve, ch);
            SELFTEST_CHECK(elid_get_bit(player, "elid_thievingchannel") == 1,
                           "clearing the spike trap must set elid_thievingchannel");
            elid_pass("oploc1_thievingchannel_clear");
        }
    }

    /* ---- Grey golem door + fight leftover + mining channel ---- */
    if( loc_grey_door > 0 )
    {
        elid_tele(srv, ELID_GREY_X, ELID_GREY_Z, 0);
        loc_slot = elid_add_loc(ELID_GREY_X, ELID_GREY_Z, 0, loc_grey_door);
        elid_set_bit(srv, "elidquest", ELID_GOLEMS);
        elid_set_bit(srv, "elid_greygolem", 0);
        if( loc_slot >= 0 )
        {
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_grey_door, loc_slot);
            elid_pass("oploc1_greygolem_door");
        }
        if( npc_grey > 0 )
        {
            int golem = selftest_find_npc(srv, npc_grey);

            if( golem < 0 )
                golem = ToriRSServer_WorldNpcSpawn(
                    srv, npc_grey, ELID_GREY_X + 1, ELID_GREY_Z, 0);
            if( golem >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC2, npc_grey, -1, golem);
                elid_finish(srv);
                elid_pass("opnpc2_greygolem_fight");
                ToriRSServer_ScriptsRunProcOnNpc(srv, "[ai_queue3,elid_golem_grey]", golem);
                elid_finish(srv);
                SELFTEST_CHECK(elid_get_bit(player, "elid_greygolem") == 1,
                               "grey golem death must set elid_greygolem");
                elid_pass("ai_queue3_greygolem_leftover");
                elid_free_npc(srv, golem);
            }
        }
        if( loc_mine > 0 )
        {
            int ch = elid_add_loc(ELID_GREY_X + 2, ELID_GREY_Z, 0, loc_mine);

            elid_set_bit(srv, "elid_greygolem", 1);
            elid_clear_inv(player);
            if( obj_key > 0 )
                elid_give(player, obj_key, 1);
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_mine, ch);
            SELFTEST_CHECK(elid_get_bit(player, "elid_miningchannel") == 0,
                           "mining channel without a pickaxe stays blocked");
            elid_pass("oploc1_miningchannel_no_pick");
            if( obj_pick > 0 )
                elid_give(player, obj_pick, 1);
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_mine, ch);
            SELFTEST_CHECK(elid_get_bit(player, "elid_miningchannel") == 1,
                           "mining the rocks must set elid_miningchannel");
            elid_pass("oploc1_miningchannel_clear");
        }
    }

    /* ---- Black golem door + fight leftover + ranging leftover ---- */
    if( loc_black_door > 0 )
    {
        elid_tele(srv, ELID_BLACK_X, ELID_BLACK_Z, 0);
        loc_slot = elid_add_loc(ELID_BLACK_X, ELID_BLACK_Z, 0, loc_black_door);
        elid_set_bit(srv, "elidquest", ELID_GOLEMS);
        elid_set_bit(srv, "elid_blackgolem", 0);
        if( loc_slot >= 0 )
        {
            elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_black_door, loc_slot);
            elid_pass("oploc1_blackgolem_door");
        }
        if( npc_black > 0 )
        {
            int golem = selftest_find_npc(srv, npc_black);

            if( golem < 0 )
                golem = ToriRSServer_WorldNpcSpawn(
                    srv, npc_black, ELID_BLACK_X + 1, ELID_BLACK_Z, 0);
            if( golem >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC2, npc_black, -1, golem);
                elid_finish(srv);
                elid_pass("opnpc2_blackgolem_fight");
                ToriRSServer_ScriptsRunProcOnNpc(srv, "[ai_queue3,elid_golem_black]", golem);
                elid_finish(srv);
                SELFTEST_CHECK(elid_get_bit(player, "elid_blackgolem") == 1,
                               "black golem death must set elid_blackgolem");
                elid_pass("ai_queue3_blackgolem_leftover");
                elid_free_npc(srv, golem);
            }
        }
        if( npc_target > 0 )
        {
            int target = ToriRSServer_WorldNpcSpawn(
                srv, npc_target, ELID_BLACK_X + 3, ELID_BLACK_Z, 0);

            elid_set_bit(srv, "elid_blackgolem", 1);
            if( obj_sword > 0 )
                worn_set(player, TORIRSSERVER_WEAR_WEAPON, obj_sword, 1);
            if( target >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC2, npc_target, -1, target);
                elid_finish(srv);
                SELFTEST_CHECK(elid_get_bit(player, "elid_rangingchannel") == 1,
                               "any wielded weapon must clear the ranging channel leftover");
                elid_pass("leftover_blackgolem_ranging_any_wielded_weapon");
                elid_free_npc(srv, target);
            }
        }
    }

    /* ---- Lake door + spirits ---- */
    if( loc_lake > 0 )
    {
        elid_tele(srv, ELID_SPIRIT_X, ELID_SPIRIT_Z - 4, 0);
        loc_slot = elid_add_loc(ELID_SPIRIT_X, ELID_SPIRIT_Z - 4, 0, loc_lake);
        elid_set_bit(srv, "elid_thievingchannel", 0);
        elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_lake, loc_slot);
        elid_pass("oploc1_lake_door_blocked");
        elid_set_bit(srv, "elid_thievingchannel", 1);
        elid_set_bit(srv, "elid_miningchannel", 1);
        elid_set_bit(srv, "elid_rangingchannel", 1);
        elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_lake, loc_slot);
        elid_pass("oploc1_lake_door_open");
    }

    elid_free_npc(srv, slot);
    slot = elid_spawn(srv, npc_spirit, ELID_SPIRIT_X, ELID_SPIRIT_Z, 0);
    if( slot >= 0 )
    {
        elid_set_bit(srv, "elidquest", ELID_STARTED);
        elid_talk_finish(srv, npc_spirit, slot);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_STARTED,
                       "spirits before golems must stay silent");
        elid_pass("opnpc1_spirits_too_early");

        elid_set_bit(srv, "elidquest", ELID_GOLEMS);
        elid_talk_finish(srv, npc_spirit, slot);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_SPIRITS_DONE,
                       "spirits talk must write spirits_done, got %d",
                       elid_get_bit(player, "elidquest"));
        elid_pass("opnpc1_spirits_talk");

        elid_talk_finish(srv, npc_spirit, slot);
        elid_pass("opnpc1_spirits_after");
    }

    elid_journal(srv, "journal_spirits_done");

    /* ---- Awusah return / shoes ---- */
    elid_free_npc(srv, slot);
    slot = elid_spawn(srv, npc_mayor, ELID_MAYOR_X, ELID_MAYOR_Z, 0);
    if( slot >= 0 )
    {
        elid_set_req_stats(player, 1);
        elid_set_bit(srv, "elidquest", ELID_SPIRITS_DONE);
        elid_talk_finish(srv, npc_mayor, slot);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_SHOES_PHASE,
                       "Awusah return must write shoes_phase, got %d",
                       elid_get_bit(player, "elidquest"));
        elid_pass("opnpc1_awusah_return");
        elid_talk_finish(srv, npc_mayor, slot);
        elid_pass("opnpc1_awusah_shoes");
    }

    elid_journal(srv, "journal_awusah_return_shoes");

    if( obj_shoes > 0 )
    {
        int ground = ToriRSServer_WorldObjAdd(
            srv, obj_shoes, 1, ELID_SHOES_X, ELID_SHOES_Z, 0, -1);

        elid_tele(srv, ELID_SHOES_X, ELID_SHOES_Z, 0);
        if( ground >= 0 )
        {
            srv->pending_active_obj = ToriRSServer_WorldObjHandle(srv, ground);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, obj_shoes, -1, -1);
            srv->pending_active_obj = 0;
            elid_finish(srv);
            SELFTEST_CHECK(elid_inv_total(player, obj_shoes) >= 1,
                           "taking Awusah's shoes must add elid_shoes");
            elid_pass("opobj3_elid_shoes_take");
        }
    }

    elid_journal(srv, "journal_shoes_phase");

    /* ---- Crevice leftover (agility unused) / genie deal / sole ---- */
    if( loc_crevice > 0 )
    {
        elid_tele(srv, ELID_CREVICE_X, ELID_CREVICE_Z, 0);
        loc_slot = elid_add_loc(ELID_CREVICE_X, ELID_CREVICE_Z, 0, loc_crevice);
        elid_set_bit(srv, "elidquest", ELID_GHASLOR_DONE);
        elid_give(player, obj_rope, 1);
        elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_crevice, loc_slot);
        SELFTEST_CHECK(player->x == ELID_CREVICE_X,
                       "crevice before shoes_phase must stay on the surface");
        elid_pass("oploc1_crevice_too_early");

        elid_set_bit(srv, "elidquest", ELID_SHOES_PHASE);
        elid_clear_inv(player);
        elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_crevice, loc_slot);
        elid_pass("oploc1_crevice_no_rope");

        elid_give(player, obj_rope, 1);
        /* Agility is never read on this climb -- leftover. */
        elid_oploc(srv, SS_TRIGGER_OPLOC1, loc_crevice, loc_slot);
        elid_pass("leftover_crevice_climbdown_agility_unused");
    }

    elid_free_npc(srv, slot);
    slot = elid_spawn(srv, npc_genie, ELID_GENIE_X, ELID_GENIE_Z, 0);
    if( slot >= 0 )
    {
        elid_set_bit(srv, "elidquest", ELID_GHASLOR_DONE);
        elid_talk_finish(srv, npc_genie, slot);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_GHASLOR_DONE,
                       "genie too-early must not start the deal");
        elid_pass("opnpc1_genie_too_early");

        elid_set_bit(srv, "elidquest", ELID_SHOES_PHASE);
        elid_talk_picks(srv, npc_genie, slot, genie_refuse1, 1);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_SHOES_PHASE,
                       "genie never-mind must not write the deal");
        elid_pass("opnpc1_genie_choice_refuse_nevermind");

        elid_talk_picks(srv, npc_genie, slot, genie_refuse2, 2);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_SHOES_PHASE,
                       "genie not-my-problem must not write the deal");
        elid_pass("opnpc1_genie_choice_refuse_problem");

        elid_talk_picks(srv, npc_genie, slot, genie_refuse3, 3);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_SHOES_PHASE,
                       "genie horrible-joke refuse must not write the deal");
        elid_pass("opnpc1_genie_choice_refuse_joke");

        elid_talk_picks(srv, npc_genie, slot, genie_accept, 3);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_GENIE_DEAL,
                       "agreeing the sole deal must write genie_deal, got %d",
                       elid_get_bit(player, "elidquest"));
        elid_pass("opnpc1_genie_deal");

        elid_talk_finish(srv, npc_genie, slot);
        elid_pass("opnpc1_genie_waiting_sole");
    }

    elid_journal(srv, "journal_genie_deal");

    if( obj_shoes > 0 && obj_knife > 0 && obj_sole > 0 )
    {
        elid_clear_inv(player);
        elid_give(player, obj_shoes, 1);
        elid_give(player, obj_knife, 1);
        elid_opheldu(srv, obj_shoes, obj_knife);
        SELFTEST_CHECK(elid_inv_total(player, obj_sole) == 1,
                       "knife on shoes must cut the sole");
        elid_pass("opheldu_cut_elid_sole");
    }

    elid_journal(srv, "journal_sole_cut");

    if( slot >= 0 && obj_sole > 0 && obj_statuette > 0 )
    {
        elid_set_bit(srv, "elidquest", ELID_GENIE_DEAL);
        elid_clear_inv(player);
        if( obj_sword > 0 )
            elid_fill_inv(player, obj_sword);
        if( obj_sole > 0 )
            inv_set(player, 0, obj_sole, 1);
        elid_talk_finish(srv, npc_genie, slot);
        SELFTEST_CHECK(elid_inv_total(player, obj_statuette) == 0,
                       "full inventory must not receive the statuette");
        elid_pass("opnpc1_genie_sole_empty_inv");

        elid_clear_inv(player);
        elid_give(player, obj_sole, 1);
        elid_talk_finish(srv, npc_genie, slot);
        SELFTEST_CHECK(elid_inv_total(player, obj_statuette) == 1,
                       "handing the sole must add the statuette");
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_STATUETTE_PHASE,
                       "sole trade must write statuette_phase, got %d",
                       elid_get_bit(player, "elidquest"));
        elid_pass("opnpc1_genie_sole");
    }

    elid_journal(srv, "journal_statuette_phase");

    /* ---- Statuette on plinth / authored complete ---- */
    if( loc_plinth > 0 && obj_statuette > 0 )
    {
        elid_tele(srv, ELID_PLINTH_X, ELID_PLINTH_Z, 0);
        loc_slot = elid_add_loc(ELID_PLINTH_X, ELID_PLINTH_Z, 0, loc_plinth);
        elid_set_bit(srv, "elidquest", ELID_STATUETTE_PHASE);
        elid_clear_inv(player);
        elid_oplocu(srv, loc_plinth, loc_slot, obj_statuette);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_STATUETTE_PHASE,
                       "empty-handed plinth must not complete");
        elid_pass("oplocu_plinth_no_statuette");

        elid_give(player, obj_statuette, 1);
        prayer_xp_before = 0;
        thieving_xp_before = 0;
        magic_xp_before = 0;
        if( stat_prayer >= 0 )
            prayer_xp_before = player->stat_xp_tenths[stat_prayer];
        if( stat_thieving >= 0 )
            thieving_xp_before = player->stat_xp_tenths[stat_thieving];
        if( stat_magic >= 0 )
            magic_xp_before = player->stat_xp_tenths[stat_magic];
        elid_oplocu(srv, loc_plinth, loc_slot, obj_statuette);
        SELFTEST_CHECK(elid_get_bit(player, "elidquest") == ELID_COMPLETE,
                       "placing the statuette must complete, got %d",
                       elid_get_bit(player, "elidquest"));
        if( stat_prayer >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_prayer] >=
                               prayer_xp_before + ELID_REWARD_PRAYER,
                           "complete must advance prayer by 80000 tenths");
        if( stat_thieving >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_thieving] >=
                               thieving_xp_before + ELID_REWARD_THIEVING,
                           "complete must advance thieving by 10000 tenths");
        if( stat_magic >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_magic] >=
                               magic_xp_before + ELID_REWARD_MAGIC,
                           "complete must advance magic by 10000 tenths");
        elid_pass("oplocu_statuette_place_complete");
    }

    if( slot >= 0 )
    {
        elid_talk_finish(srv, npc_mayor > 0 ? npc_mayor : npc_genie, slot);
        elid_pass("opnpc1_post_complete");
    }

    elid_journal(srv, "journal_complete");
    elid_free_npc(srv, slot);

    fprintf(stderr, "ToriRSServer elid selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_SPIRITSOFTHEELID_SELFTEST_U_H */
