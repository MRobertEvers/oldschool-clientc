#ifndef TORIRSSERVER_TEST_QUEST_EAGLEPEAK_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_EAGLEPEAK_SELFTEST_U_H

/* Eagles' Peak Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Charlie / Nickolaus / Asyff / kebbit /
 * guard / travel eagles cannot leak. Real OPNPC1 / OPNPC3 / OPLOC1 /
 * OPLOC2 / OPLOCU / OPHELD1 on the authored path. player->godmode = 1
 * for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_EAGLE_ONLY=1 (or EAGLESPEAK / EAGLEPEAK)
 *
 * Hunter 27 to start. Reward tenths: 25000 Hunter. Cache dbrow
 * quest_eaglespeak awards 2 QP. Asyff is merged into [opnpc1,tailorp].
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - tracking-tile / trail-segment visual multilocs collapsed to one
 *     %eaglepeak_puzzle2_tracking insert/advance
 *   - post-quest eyrie flavour beyond desert/jungle/polar travel eagles
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 */

#define EAGLE_NOT_STARTED 0
#define EAGLE_FIND_BOOK 5
#define EAGLE_USE_FEATHER 10
#define EAGLE_ENTERED 15
#define EAGLE_FREED_PATH 20
#define EAGLE_MEET_CAMP 25
#define EAGLE_GOT_FERRET 35
#define EAGLE_COMPLETE 40

#define EAGLE_NICK_SHOUT_NEED 3
#define EAGLE_NICK_ASKED_ASYFF 4
#define EAGLE_NICK_GOT_DISGUISE 5
#define EAGLE_FEATHERS_NEED 10
#define EAGLE_ASYFF_COINS 50
#define EAGLE_DOOR_OPEN 3
#define EAGLE_SILVER_PEDESTAL 1
#define EAGLE_SILVER_ROCKS1 2
#define EAGLE_SILVER_ROCKS2 3
#define EAGLE_SILVER_OPENING 4
#define EAGLE_SILVER_THREATENED 5
#define EAGLE_SEED_MAX 6

#define EAGLE_HUNTER_REQ 27
#define EAGLE_REWARD_TENTHS 25000
#define EAGLE_QP_REWARD 2
#define EAGLE_STAT_HUNTER 21

#define EAGLE_CHARLIE_X 2607
#define EAGLE_CHARLIE_Z 3264
#define EAGLE_CAMP_X 2317
#define EAGLE_CAMP_Z 3504
#define EAGLE_OUTCROP_X 2329
#define EAGLE_OUTCROP_Z 3495
#define EAGLE_CAVERN_X 1993
#define EAGLE_CAVERN_Z 4983
#define EAGLE_NEST_X 2006
#define EAGLE_NEST_Z 4960
#define EAGLE_ASYFF_X 3281
#define EAGLE_ASYFF_Z 3398
#define EAGLE_BRONZE_X 1974
#define EAGLE_BRONZE_Z 4915
#define EAGLE_SILVER_X 1947
#define EAGLE_SILVER_Z 4873
#define EAGLE_GOLD_X 1958
#define EAGLE_GOLD_Z 4906
#define EAGLE_PAST_DOOR_X 2002
#define EAGLE_PAST_DOOR_Z 4958
#define EAGLE_EYRIE_DESERT_X 3422
#define EAGLE_EYRIE_DESERT_Z 9568
#define EAGLE_EYRIE_RETURN_X 2019
#define EAGLE_EYRIE_RETURN_Z 4957

static void
eagle_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "EAGLE PASS: %s\n", step);
}

static void
eagle_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
eagle_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
eagle_finish(struct ToriRSServer* srv)
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
eagle_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
eagle_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : eagle_chatmenu();
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
eagle_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = eagle_chatmenu();
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
eagle_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    eagle_god(player);
    selftest_tick(srv);
}

static int
eagle_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    eagle_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
eagle_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
eagle_free_type(struct ToriRSServer* srv, int npc_type)
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
eagle_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
eagle_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
eagle_quest(struct ToriRSServerPlayer* player)
{
    return eagle_get_vb(player, "eaglepeak_quest");
}

static void
eagle_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
eagle_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    eagle_talk(srv, npc_type, slot);
    eagle_finish(srv);
}

static void
eagle_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    eagle_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        eagle_click_until_menu(srv, 24);
        eagle_pick_row(srv, rows[i]);
    }
    eagle_finish(srv);
}

static void
eagle_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
eagle_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
eagle_inv_used(const struct ToriRSServerPlayer* player)
{
    int n = 0;
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id >= 0 && player->inv[s].count > 0 )
            n += player->inv[s].count;
    return n;
}

static void
eagle_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id < 0 )
            inv_set(player, s, obj_id, 1);
}

static void
eagle_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
eagle_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    eagle_vb(srv, "eaglepeak_quest", EAGLE_NOT_STARTED);
    eagle_vb(srv, "eaglepeak_nickolaus_chat", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_gate1", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_gate2", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_gate3", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_gate4", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird1", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird2", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird3", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird4", 0);
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird5", 0);
    eagle_vb(srv, "eaglepeak_puzzle2_tracking", 0);
    eagle_vb(srv, "eaglepeak_puzzle3_winch1", 0);
    eagle_vb(srv, "eaglepeak_puzzle3_winch2", 0);
    eagle_vb(srv, "eaglepeak_puzzle3_winch3", 0);
    eagle_vb(srv, "eaglepeak_puzzle3_winch4", 0);
    eagle_vb(srv, "eaglepeak_puzzle3_nettrap", 0);
    eagle_vb(srv, "eaglepeak_eagledoor_status", 0);
    eagle_vb(srv, "eaglepeak_entered_puzzle1", 0);
    eagle_vb(srv, "eaglepeak_entered_puzzle2", 0);
    eagle_vb(srv, "eaglepeak_entered_puzzle3", 0);
}

static void
eagle_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,eaglepeak_journal]", NULL, 0);
    eagle_finish(srv);
    eagle_pass(step);
}

static void
eagle_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    eagle_finish(srv);
}

static void
eagle_loc2(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC2, loc_type, -1, -1);
    eagle_finish(srv);
}

static void
eagle_locu(struct ToriRSServer* srv, int loc_type, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(loc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_type, -1, -1);
    eagle_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
eagle_held1(struct ToriRSServer* srv, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    eagle_finish(srv);
}

static void
eagle_npc3(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_type, -1, slot);
}

static void
selftest_quest_eaglepeak(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_charlie;
    int npc_nick;
    int npc_nick_shout;
    int npc_nick_camp;
    int npc_nick_normal;
    int npc_asyff;
    int npc_guard;
    int npc_kebbit;
    int npc_desert;
    int npc_jungle;
    int npc_polar;
    int loc_books;
    int loc_outcrop;
    int loc_outcrop_open;
    int loc_exit;
    int loc_pile;
    int loc_winch1;
    int loc_pedestal3;
    int loc_net_active;
    int loc_bronze_in;
    int loc_bronze_out;
    int loc_pedestal2;
    int loc_rock1;
    int loc_rock2;
    int loc_dummy;
    int loc_opening;
    int loc_pedestal2c;
    int loc_silver_in;
    int loc_dispenser;
    int loc_feeder1;
    int loc_feeder1a;
    int loc_feeder2;
    int loc_feeder3;
    int loc_lever1;
    int loc_reset;
    int loc_pedestal1;
    int loc_gold_in;
    int loc_door;
    int loc_eyrie_exit;
    int obj_book;
    int obj_metal;
    int obj_feather;
    int obj_dye;
    int obj_tar;
    int obj_coins;
    int obj_beak;
    int obj_cape;
    int obj_ferret;
    int obj_bronze;
    int obj_silver;
    int obj_gold;
    int obj_seed;
    int obj_logs;
    int obj_box;
    int slot_charlie;
    int slot_nick;
    int slot_asyff;
    int slot_guard;
    int slot_kebbit;
    int slot_desert;
    int slot_jungle;
    int slot_polar;
    int qp_id;
    int qp_before;
    int xp_before;
    int accept_rows[2];
    int refuse1[1];
    int refuse2[2];
    int nick_accept[3];
    int nick_leave[3];
    int one[1];
    int two[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "EAGLE SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    eagle_god(player);
    eagle_clear_inv(player);
    eagle_reset_quest(srv);

    npc_charlie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_zookeeper_charlie");
    npc_nick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_nickolaus");
    npc_nick_shout = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_nickolaus_shout");
    npc_nick_camp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_nickolaus_campsite");
    npc_nick_normal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_nickolaus_normal");
    npc_asyff = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tailorp");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_eagle_guard");
    npc_kebbit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_uber_kebbit");
    npc_desert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_eagle_todesert");
    npc_jungle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_eagle_tojungle");
    npc_polar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eaglepeak_eagle_topolar");
    loc_books = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_books_tidy");
    loc_outcrop = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_new_cave_entrance");
    loc_outcrop_open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_new_cave_entrance_open");
    loc_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_human_exitmid");
    loc_pile = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_feather_pile");
    loc_winch1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_winch1");
    loc_pedestal3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle3");
    loc_net_active = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_net_trap_active");
    loc_bronze_in = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle3_entrancemid");
    loc_bronze_out = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle3_exitmid");
    loc_pedestal2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle2");
    loc_rock1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_hunting_trail_spawn1");
    loc_rock2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_hunting_trail_spawn2");
    loc_dummy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_hunting_trail_spawn_dummy");
    loc_opening = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_kebbit_cavemid");
    loc_pedestal2c = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle2_complete");
    loc_silver_in = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle2_entrancemid");
    loc_dispenser = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_birdseed_dispenser");
    loc_feeder1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder1");
    loc_feeder1a = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder1a");
    loc_feeder2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder2");
    loc_feeder3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_bird_feeder3");
    loc_lever1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle1_lever1");
    loc_reset = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_reset_lever");
    loc_pedestal1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_dungeon_pedestal_puzzle1");
    loc_gold_in = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_puzzle1_entrancemid");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_gate_0_feather_mirror");
    loc_eyrie_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eaglepeak_desert_cave_exit");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_book_of_birds");
    obj_metal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_metal_feather");
    obj_feather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_eagle_feather");
    obj_dye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yellowdye");
    obj_tar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamp_tar");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_beak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_fake_beak");
    obj_cape = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_eagle_cape");
    obj_ferret = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_ferret");
    obj_bronze = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_crystal_feather3");
    obj_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_crystal_feather2");
    obj_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_crystal_feather1");
    obj_seed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eaglepeak_bird_seed");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_box = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hunting_box_trap");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_eaglespeak") > 0,
                   "dbrow quest_eaglespeak should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_quest") >= 0,
                   "varbit eaglepeak_quest should resolve");
    SELFTEST_CHECK(npc_charlie > 0, "npc eaglepeak_zookeeper_charlie should resolve");
    SELFTEST_CHECK(npc_nick > 0 && npc_nick_shout > 0 && npc_nick_camp > 0 && npc_nick_normal > 0,
                   "Nickolaus variants should resolve");
    SELFTEST_CHECK(npc_asyff > 0, "npc tailorp should resolve");
    SELFTEST_CHECK(npc_guard > 0 && npc_kebbit > 0, "guard eagle + kebbit should resolve");
    SELFTEST_CHECK(npc_desert > 0 && npc_jungle > 0 && npc_polar > 0,
                   "travel eagles should resolve");
    SELFTEST_CHECK(loc_books > 0 && loc_outcrop > 0 && loc_pile > 0,
                   "books / outcrop / feather pile should resolve");
    SELFTEST_CHECK(obj_book > 0 && obj_metal > 0 && obj_feather > 0 && obj_ferret > 0,
                   "book / metal feather / eagle feather / ferret should resolve");
    SELFTEST_CHECK(obj_beak > 0 && obj_cape > 0 && obj_bronze > 0 && obj_silver > 0 && obj_gold > 0,
                   "disguise + crystal feathers should resolve");
    SELFTEST_CHECK(obj_box > 0, "hunting_box_trap should resolve");

    accept_rows[0] = 1;
    accept_rows[1] = 1;
    refuse1[0] = 2;
    refuse2[0] = 1;
    refuse2[1] = 2;
    nick_accept[0] = 1;
    nick_accept[1] = 1;
    nick_accept[2] = 1;
    nick_leave[0] = 1;
    nick_leave[1] = 1;
    nick_leave[2] = 2;
    one[0] = 1;
    two[0] = 2;

    slot_charlie = eagle_spawn(srv, npc_charlie, EAGLE_CHARLIE_X, EAGLE_CHARLIE_Z, 0);
    SELFTEST_CHECK(slot_charlie >= 0, "Charlie should spawn");

    eagle_set_stat(player, EAGLE_STAT_HUNTER, 1);
    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_NOT_STARTED, "low Hunter must not start");
    eagle_pass("01_charlie_qualify_fail");

    eagle_journal(srv, "journal_00_not_started");

    eagle_set_stat(player, EAGLE_STAT_HUNTER, EAGLE_HUNTER_REQ);
    eagle_talk_rows(srv, npc_charlie, slot_charlie, refuse1, 1);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_NOT_STARTED, "first refuse must leave unstarted");
    eagle_pass("04_charlie_refuse_first");

    eagle_talk_rows(srv, npc_charlie, slot_charlie, refuse2, 2);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_NOT_STARTED, "second refuse must leave unstarted");
    eagle_pass("08_charlie_refuse_second");

    eagle_talk_rows(srv, npc_charlie, slot_charlie, accept_rows, 2);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_FIND_BOOK, "accept should set find_book=5");
    eagle_pass("11_charlie_start_mesbox");
    eagle_journal(srv, "journal_01_find_book");

    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_FIND_BOOK, "mid find_book talk must not rewind");
    eagle_pass("12_charlie_mid_find_book");

    eagle_tele(srv, EAGLE_OUTCROP_X, EAGLE_OUTCROP_Z, 0);
    eagle_clear_inv(player);
    eagle_loc1(srv, loc_books);
    SELFTEST_CHECK(eagle_inv_total(player, obj_book) >= 1, "search should grant the bird book");
    eagle_pass("22_books_find");

    eagle_loc1(srv, loc_books);
    SELFTEST_CHECK(eagle_inv_total(player, obj_book) == 1, "already-searched must not duplicate");
    eagle_pass("23_books_already");

    eagle_clear_inv(player);
    eagle_fill_inv(player, obj_logs);
    eagle_loc1(srv, loc_books);
    SELFTEST_CHECK(eagle_inv_total(player, obj_book) == 0, "full inv must not take the book");
    eagle_pass("24_books_inv_full");

    eagle_clear_inv(player);
    eagle_give(player, obj_book, 1);
    eagle_held1(srv, obj_book);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_USE_FEATHER, "read book should set use_feather=10");
    SELFTEST_CHECK(eagle_inv_total(player, obj_metal) >= 1, "read book should grant metal feather");
    eagle_pass("27_book_read");
    eagle_journal(srv, "journal_02_use_feather");

    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    eagle_pass("13_charlie_mid_use_feather");

    eagle_tele(srv, EAGLE_OUTCROP_X, EAGLE_OUTCROP_Z, 0);
    eagle_loc2(srv, loc_outcrop);
    eagle_pass("32_outcrop_inspect_closed");

    eagle_locu(srv, loc_outcrop, obj_logs > 0 ? obj_logs : obj_book);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_USE_FEATHER, "wrong item must not open the outcrop");
    eagle_pass("28_outcrop_wrong_item");

    eagle_locu(srv, loc_outcrop, obj_metal);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_ENTERED, "metal feather should open the outcrop");
    eagle_pass("31_outcrop_open");
    eagle_journal(srv, "journal_03_entered");

    eagle_loc2(srv, loc_outcrop_open > 0 ? loc_outcrop_open : loc_outcrop);
    eagle_pass("33_outcrop_inspect_open");

    eagle_loc1(srv, loc_outcrop_open > 0 ? loc_outcrop_open : loc_outcrop);
    SELFTEST_CHECK(player->x == EAGLE_CAVERN_X && player->z == EAGLE_CAVERN_Z,
                   "enter should land in the cavern");
    eagle_pass("35_cavern_enter");

    eagle_loc1(srv, loc_exit);
    SELFTEST_CHECK(player->x == EAGLE_OUTCROP_X && player->z == EAGLE_OUTCROP_Z,
                   "exit should return to the outcrop");
    eagle_pass("36_cavern_leave");

    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    eagle_pass("14_charlie_mid_entered");

    eagle_tele(srv, EAGLE_CAVERN_X, EAGLE_CAVERN_Z, 3);
    eagle_loc1(srv, loc_pile);
    SELFTEST_CHECK(eagle_inv_total(player, obj_feather) >= 1, "feather pile should grant a feather");
    eagle_pass("49_feather_take");

    eagle_clear_inv(player);
    eagle_give(player, obj_feather, EAGLE_FEATHERS_NEED);
    eagle_loc1(srv, loc_pile);
    SELFTEST_CHECK(eagle_inv_total(player, obj_feather) == EAGLE_FEATHERS_NEED,
                   "enough feathers must not add more");
    eagle_pass("50_feather_enough");

    slot_nick = eagle_spawn(srv, npc_nick_shout, EAGLE_CAVERN_X, EAGLE_CAVERN_Z - 6, 3);
    SELFTEST_CHECK(slot_nick >= 0, "shout Nickolaus should spawn");
    eagle_talk_rows(srv, npc_nick_shout, slot_nick, two, 1);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_nickolaus_chat") == 0,
                   "never-mind must not set shout_need");
    eagle_pass("39_nick_shout_refuse");

    eagle_talk_rows(srv, npc_nick_shout, slot_nick, nick_leave, 3);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_nickolaus_chat") == 0,
                   "leave-you-to-it must not set shout_need");
    eagle_pass("44_nick_shout_leave");

    eagle_talk_rows(srv, npc_nick_shout, slot_nick, nick_accept, 3);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_nickolaus_chat") == EAGLE_NICK_SHOUT_NEED,
                   "accept help should set shout_need=3");
    eagle_pass("46_nick_shout_mesbox");
    eagle_journal(srv, "journal_04_shout");

    eagle_talk_finish(srv, npc_nick_shout, slot_nick);
    eagle_pass("47_nick_shout_wait");
    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    eagle_pass("15_charlie_mid_shout");

    slot_asyff = eagle_spawn(srv, npc_asyff, EAGLE_ASYFF_X, EAGLE_ASYFF_Z, 0);
    SELFTEST_CHECK(slot_asyff >= 0, "Asyff/tailorp should spawn");
    eagle_talk_rows(srv, npc_asyff, slot_asyff, one, 1);
    eagle_pass("54_asyff_leave_perusing");

    eagle_talk_finish(srv, npc_asyff, slot_asyff);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_nickolaus_chat") == EAGLE_NICK_ASKED_ASYFF,
                   "bird request should set asked_asyff=4");
    eagle_pass("57_asyff_bird_request");
    eagle_journal(srv, "journal_05_asyff");

    eagle_clear_inv(player);
    eagle_talk_finish(srv, npc_asyff, slot_asyff);
    SELFTEST_CHECK(eagle_inv_total(player, obj_beak) == 0, "short materials must not craft");
    eagle_pass("58_asyff_short");

    eagle_give(player, obj_feather, EAGLE_FEATHERS_NEED);
    eagle_give(player, obj_dye, 1);
    eagle_give(player, obj_tar, 1);
    eagle_give(player, obj_coins, EAGLE_ASYFF_COINS);
    eagle_talk_rows(srv, npc_asyff, slot_asyff, two, 1);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_nickolaus_chat") == EAGLE_NICK_ASKED_ASYFF,
                   "come-back-later must not consume materials");
    eagle_pass("60_asyff_later");

    eagle_talk_rows(srv, npc_asyff, slot_asyff, one, 1);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_nickolaus_chat") == EAGLE_NICK_GOT_DISGUISE,
                   "craft should set got_disguise=5");
    SELFTEST_CHECK(eagle_inv_total(player, obj_beak) >= 2, "craft should grant two beaks");
    SELFTEST_CHECK(eagle_inv_total(player, obj_cape) >= 2, "craft should grant two capes");
    eagle_pass("63_asyff_make_mesbox");
    eagle_journal(srv, "journal_06_disguise");

    eagle_talk_rows(srv, npc_asyff, slot_asyff, one, 1);
    eagle_pass("65_asyff_browsing");

    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_asyff, -1, slot_asyff);
    eagle_finish(srv);
    eagle_pass("56_asyff_trade");

    slot_guard = eagle_spawn(srv, npc_guard, EAGLE_CAVERN_X + 3, EAGLE_CAVERN_Z - 3, 3);
    SELFTEST_CHECK(slot_guard >= 0, "guard eagle should spawn");
    eagle_talk_finish(srv, npc_guard, slot_guard);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_ENTERED, "unworn disguise must not sneak");
    eagle_pass("68_guard_wear");

    worn_set(player, TORIRSSERVER_WEAR_HEAD, obj_beak, 1);
    worn_set(player, TORIRSSERVER_WEAR_CAPE, obj_cape, 1);
    eagle_talk_finish(srv, npc_guard, slot_guard);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_FREED_PATH, "worn disguise should sneak to nest");
    SELFTEST_CHECK(player->x == EAGLE_NEST_X && player->z == EAGLE_NEST_Z,
                   "sneak should teleport to the nest");
    eagle_pass("69_guard_sneak");
    eagle_journal(srv, "journal_07_freed");

    eagle_free_npc(srv, slot_nick);
    slot_nick = eagle_spawn(srv, npc_nick_normal, EAGLE_NEST_X, EAGLE_NEST_Z, 3);
    SELFTEST_CHECK(slot_nick >= 0, "nest Nickolaus should spawn");
    eagle_talk_finish(srv, npc_nick_normal, slot_nick);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_MEET_CAMP, "nest talk should set meet_camp=25");
    eagle_pass("71_nick_nest_mesbox");
    eagle_journal(srv, "journal_08_camp");

    eagle_talk_finish(srv, npc_nick_normal, slot_nick);
    eagle_pass("72_nick_nest_reminder");
    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    eagle_pass("16_charlie_mid_camp");

    eagle_free_npc(srv, slot_nick);
    slot_nick = eagle_spawn(srv, npc_nick_camp, EAGLE_CAMP_X, EAGLE_CAMP_Z, 0);
    SELFTEST_CHECK(slot_nick >= 0, "camp Nickolaus should spawn");
    eagle_talk_rows(srv, npc_nick_camp, slot_nick, two, 1);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_MEET_CAMP, "maybe-later must not grant a ferret");
    eagle_pass("74_camp_later");

    eagle_clear_inv(player);
    eagle_fill_inv(player, obj_logs);
    eagle_talk_rows(srv, npc_nick_camp, slot_nick, one, 1);
    SELFTEST_CHECK(eagle_inv_total(player, obj_ferret) == 0, "full inv must not take a ferret");
    eagle_pass("77_camp_inv_full");

    eagle_clear_inv(player);
    eagle_talk_rows(srv, npc_nick_camp, slot_nick, one, 1);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_GOT_FERRET, "lesson should set got_ferret=35");
    SELFTEST_CHECK(eagle_inv_used(player) >= 1, "lesson should grant a ferret");
    eagle_pass("76_camp_ferret");
    eagle_journal(srv, "journal_09_ferret");

    eagle_talk_finish(srv, npc_nick_camp, slot_nick);
    eagle_pass("78_camp_take_to_charlie");

    eagle_clear_inv(player);
    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_GOT_FERRET, "missing ferret must not complete");
    eagle_pass("17_charlie_ferret_missing");

    eagle_talk_finish(srv, npc_nick_camp, slot_nick);
    SELFTEST_CHECK(eagle_inv_used(player) >= 1, "camp should replace a lost ferret");
    eagle_pass("79_camp_replace");

    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;
    xp_before = player->stat_xp_tenths[EAGLE_STAT_HUNTER];
    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    SELFTEST_CHECK(eagle_quest(player) == EAGLE_COMPLETE, "hand-in should complete at 40");
    SELFTEST_CHECK(player->stat_xp_tenths[EAGLE_STAT_HUNTER] >= xp_before + EAGLE_REWARD_TENTHS,
                   "complete should award 25000 Hunter tenths");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + EAGLE_QP_REWARD,
                       "complete should award dbrow quest points");
    eagle_pass("19_complete_scroll");
    eagle_journal(srv, "journal_10_complete");

    eagle_talk_finish(srv, npc_charlie, slot_charlie);
    eagle_pass("20_charlie_already_complete");
    eagle_talk_finish(srv, npc_nick_camp, slot_nick);
    eagle_pass("81_camp_complete");

    /* Puzzle rooms -- authored loc ops at entered state. */
    eagle_vb(srv, "eaglepeak_quest", EAGLE_ENTERED);
    eagle_tele(srv, EAGLE_BRONZE_X, EAGLE_BRONZE_Z, 2);
    eagle_loc1(srv, loc_bronze_in);
    eagle_pass("82_bronze_tunnel_in");
    eagle_loc1(srv, loc_pedestal3);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle3_nettrap") == 1,
                   "first take should spring the net");
    eagle_pass("83_bronze_net");
    eagle_loc1(srv, loc_net_active);
    eagle_pass("84_bronze_net_covers");
    eagle_loc1(srv, loc_winch1);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle3_winch1") == 1, "winch1 should latch");
    eagle_pass("85_bronze_winch");
    eagle_loc1(srv, loc_winch1);
    eagle_pass("86_bronze_winch_already");
    eagle_vb(srv, "eaglepeak_puzzle3_winch2", 1);
    eagle_vb(srv, "eaglepeak_puzzle3_winch3", 1);
    eagle_vb(srv, "eaglepeak_puzzle3_winch4", 1);
    eagle_clear_inv(player);
    eagle_loc1(srv, loc_pedestal3);
    SELFTEST_CHECK(eagle_inv_total(player, obj_bronze) >= 1, "raised net should grant bronze feather");
    eagle_pass("88_bronze_take");
    eagle_loc1(srv, loc_bronze_out);
    eagle_pass("91_bronze_tunnel_out");

    eagle_tele(srv, EAGLE_SILVER_X, EAGLE_SILVER_Z, 2);
    eagle_loc1(srv, loc_silver_in);
    eagle_pass("92_silver_tunnel_in");
    eagle_loc1(srv, loc_pedestal2);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle2_tracking") == EAGLE_SILVER_PEDESTAL,
                   "pedestal inspect should set tracking=1");
    eagle_pass("93_silver_pedestal");
    eagle_loc1(srv, loc_dummy);
    eagle_pass("95_silver_dummy");
    eagle_loc1(srv, loc_rock1);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle2_tracking") == EAGLE_SILVER_ROCKS1,
                   "east rocks should set tracking=2");
    eagle_pass("97_silver_rock_east");
    eagle_loc1(srv, loc_rock2);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle2_tracking") == EAGLE_SILVER_ROCKS2,
                   "NE rocks should set tracking=3");
    eagle_pass("98_silver_rock_ne");
    eagle_loc1(srv, loc_opening);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle2_tracking") == EAGLE_SILVER_OPENING,
                   "opening should set tracking=4");
    eagle_pass("101_silver_kebbit_mesbox");

    eagle_free_type(srv, npc_kebbit);
    slot_kebbit = eagle_spawn(srv, npc_kebbit, EAGLE_SILVER_X, EAGLE_SILVER_Z - 1, 2);
    SELFTEST_CHECK(slot_kebbit >= 0, "kebbit should spawn");
    eagle_npc3(srv, npc_kebbit, slot_kebbit);
    eagle_click_until_menu(srv, 8);
    eagle_pick_row(srv, 2);
    eagle_finish(srv);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle2_tracking") == EAGLE_SILVER_OPENING,
                   "leave kebbit must not advance");
    eagle_pass("105_silver_kebbit_leave");

    eagle_npc3(srv, npc_kebbit, slot_kebbit);
    eagle_click_until_menu(srv, 8);
    eagle_pick_row(srv, 1);
    eagle_finish(srv);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle2_tracking") == EAGLE_SILVER_THREATENED,
                   "taunt should set tracking=5");
    eagle_pass("107_silver_kebbit_fled");
    eagle_loc1(srv, loc_pedestal2c);
    SELFTEST_CHECK(eagle_inv_total(player, obj_silver) >= 1, "threatened pedestal should grant silver");
    eagle_pass("108_silver_take");

    eagle_tele(srv, EAGLE_GOLD_X, EAGLE_GOLD_Z, 2);
    eagle_loc1(srv, loc_gold_in);
    eagle_pass("111_gold_tunnel_in");
    eagle_clear_inv(player);
    eagle_loc1(srv, loc_dispenser);
    SELFTEST_CHECK(eagle_inv_total(player, obj_seed) >= 1, "dispenser should grant bird seed");
    eagle_pass("112_gold_seed");
    eagle_locu(srv, loc_feeder1, obj_seed);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle1_mechbird1") == 1, "feeder1 sets bird1");
    eagle_pass("115_gold_feed");
    eagle_give(player, obj_seed, 1);
    eagle_locu(srv, loc_feeder1a, obj_seed);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle1_mechbird3") == 1, "feeder1a sets bird3");
    eagle_give(player, obj_seed, 1);
    eagle_locu(srv, loc_feeder2, obj_seed);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle1_mechbird2") == 1, "feeder2 sets bird2");
    eagle_give(player, obj_seed, 1);
    eagle_locu(srv, loc_feeder3, obj_seed);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_puzzle1_mechbird4") == 1, "feeder3 sets bird4");
    eagle_pass("117_gold_feeders_done");
    eagle_loc1(srv, loc_lever1);
    eagle_pass("118_gold_lever_down");
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC2, loc_lever1, -1, -1);
    eagle_finish(srv);
    eagle_pass("119_gold_lever_up");
    eagle_loc1(srv, loc_reset);
    eagle_pass("120_gold_reset");
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird1", 1);
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird2", 1);
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird3", 1);
    eagle_vb(srv, "eaglepeak_puzzle1_mechbird4", 1);
    eagle_loc1(srv, loc_pedestal1);
    SELFTEST_CHECK(eagle_inv_total(player, obj_gold) >= 1, "fed birds should grant golden feather");
    eagle_pass("122_gold_take");

    eagle_tele(srv, EAGLE_CAVERN_X, EAGLE_CAVERN_Z, 3);
    eagle_loc1(srv, loc_door);
    eagle_pass("128_door_locked");
    eagle_give(player, obj_gold, 1);
    eagle_locu(srv, loc_door, obj_gold);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_eagledoor_status") == 1, "first feather seats");
    eagle_pass("124_door_first");
    eagle_give(player, obj_silver, 1);
    eagle_locu(srv, loc_door, obj_silver);
    eagle_give(player, obj_bronze, 1);
    eagle_locu(srv, loc_door, obj_bronze);
    SELFTEST_CHECK(eagle_get_vb(player, "eaglepeak_eagledoor_status") >= EAGLE_DOOR_OPEN,
                   "third feather should unlock the door");
    eagle_pass("125_door_last");
    eagle_loc1(srv, loc_door);
    SELFTEST_CHECK(player->x == EAGLE_PAST_DOOR_X && player->z == EAGLE_PAST_DOOR_Z,
                   "open door should slip through");
    eagle_pass("129_door_open");

    eagle_vb(srv, "eaglepeak_quest", EAGLE_ENTERED);
    slot_desert = eagle_spawn(srv, npc_desert, EAGLE_EYRIE_RETURN_X, EAGLE_EYRIE_RETURN_Z, 3);
    eagle_talk_finish(srv, npc_desert, slot_desert);
    SELFTEST_CHECK(player->x == EAGLE_EYRIE_RETURN_X, "incomplete must not ride");
    eagle_pass("130_eyrie_early");

    eagle_vb(srv, "eaglepeak_quest", EAGLE_COMPLETE);
    eagle_tele(srv, EAGLE_EYRIE_RETURN_X, EAGLE_EYRIE_RETURN_Z, 3);
    eagle_free_npc(srv, slot_desert);
    slot_desert = eagle_spawn(srv, npc_desert, EAGLE_EYRIE_RETURN_X, EAGLE_EYRIE_RETURN_Z, 3);
    slot_jungle = eagle_spawn(srv, npc_jungle, EAGLE_EYRIE_RETURN_X + 3, EAGLE_EYRIE_RETURN_Z, 3);
    slot_polar = eagle_spawn(srv, npc_polar, EAGLE_EYRIE_RETURN_X + 6, EAGLE_EYRIE_RETURN_Z, 3);
    eagle_talk_finish(srv, npc_desert, slot_desert);
    SELFTEST_CHECK(player->x == EAGLE_EYRIE_DESERT_X && player->z == EAGLE_EYRIE_DESERT_Z,
                   "desert eagle should land in the desert eyrie");
    eagle_pass("131_eyrie_desert");
    eagle_loc1(srv, loc_eyrie_exit);
    SELFTEST_CHECK(player->x == EAGLE_EYRIE_RETURN_X && player->z == EAGLE_EYRIE_RETURN_Z,
                   "eyrie exit should return to the Peak");
    eagle_pass("135_eyrie_exit");
    eagle_talk_finish(srv, npc_jungle, slot_jungle);
    eagle_pass("132_eyrie_jungle");
    eagle_tele(srv, EAGLE_EYRIE_RETURN_X, EAGLE_EYRIE_RETURN_Z, 3);
    eagle_talk_finish(srv, npc_polar, slot_polar);
    eagle_pass("133_eyrie_polar");

    eagle_pass("leftover_tracking_tile_trail");
    eagle_pass("leftover_postquest_eyrie_flavour");

    eagle_free_npc(srv, slot_charlie);
    eagle_free_npc(srv, slot_nick);
    eagle_free_npc(srv, slot_asyff);
    eagle_free_npc(srv, slot_guard);
    eagle_free_npc(srv, slot_kebbit);
    eagle_free_npc(srv, slot_desert);
    eagle_free_npc(srv, slot_jungle);
    eagle_free_npc(srv, slot_polar);
    eagle_free_type(srv, npc_charlie);
    eagle_free_type(srv, npc_nick);
    eagle_free_type(srv, npc_nick_shout);
    eagle_free_type(srv, npc_nick_camp);
    eagle_free_type(srv, npc_nick_normal);
    eagle_free_type(srv, npc_asyff);
    eagle_free_type(srv, npc_guard);
    eagle_free_type(srv, npc_kebbit);
    eagle_free_type(srv, npc_desert);
    eagle_free_type(srv, npc_jungle);
    eagle_free_type(srv, npc_polar);
    eagle_clear_inv(player);
    eagle_reset_quest(srv);
    eagle_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_EAGLEPEAK_SELFTEST_U_H */
