#ifndef TORIRSSERVER_TEST_QUEST_GREATBRAINROBBERY_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_GREATBRAINROBBERY_SELFTEST_U_H

/* The Great Brain Robbery Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Tranquility / Fenk / Rufus / Mi-Gor /
 * crate / door locs cannot leak. Real OPNPC1 / OPLOC1 / OPLOCU / OPHELD1
 * / OPHELDU on the authored path. player->godmode = 1 for the whole
 * walk (not a death test).
 *
 * MERGE -- do not add second headers:
 *   [opnpc1,werewolfshopkeeper1]  areas/area_canifis/scripts/rufus.rs2
 *   @fenk_talk                    quests/quest_fenkenstrain/scripts/fenkenstrain.rs2
 *   [oploc1,bookcase]             general_use/scripts/bookcases.rs2
 *   [opheldu,knife]               skill_fletching/scripts/cut_logs.rs2
 *   ~poh_workshop_wooden_cat      skill_construction/scripts/poh_workshop_functions.rs2
 * Harmony / Mos travel is Tranquility teleport. Bill Teach / Cabin Fever
 * docks stay on their existing headers.
 *
 * Gate: TORIRSSERVER_SELFTEST_GBR_ONLY=1
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF /
 * date_runeday / flute 282 / telekinetic grab / POH workshop wooden cat):
 *   leftover_barrelchest_prayer_drain
 *   leftover_flood_peephole_extra
 *   leftover_windmill_flour
 *   leftover_anchor_pirate_smith
 */

#define GBR_NOT_STARTED 0
#define GBR_EXPLORING 10
#define GBR_PEEP 20
#define GBR_GETTING_BOOK 30
#define GBR_BOOK_READ 40
#define GBR_RECITED 50
#define GBR_CRATE_SCHEME 60
#define GBR_BASEMENT 70
#define GBR_DOOR 80
#define GBR_DOOR2 90
#define GBR_HELPED 100
#define GBR_CONFRONT 110
#define GBR_READY 120
#define GBR_COMPLETE 130

#define GBR_STATUE_OPEN 2
#define GBR_STAIRS_REPAIRED 1
#define GBR_PRAYER_READ 1
#define GBR_TALKED_FENK 2
#define GBR_TALKED_RUFUS 3
#define GBR_CRATE_GHOST 1
#define GBR_CRATE_WALLS 2
#define GBR_CRATE_BOTTOM 3
#define GBR_CRATE_CATS 4
#define GBR_CRATE_FENK 5
#define GBR_BARREL_KEG 2
#define GBR_BARREL_FUSE 3
#define GBR_BARREL_GONE 5
#define GBR_FENK_COMPLETE 9
#define GBR_FEVER_COMPLETE 140
#define GBR_RFD_PIRATE_COMPLETE 7
#define GBR_REQ_PRAYER 50
#define GBR_REQ_CON 30
#define GBR_REQ_CRAFT 16
#define GBR_REWARD_PRAYER_TENTHS 60000
#define GBR_REWARD_CRAFT_TENTHS 30000
#define GBR_REWARD_CON_TENTHS 20000
#define GBR_REWARD_QP 2
#define GBR_CATS_NEEDED 10
#define GBR_JARS_NEEDED 3
#define GBR_STAPLES_NEEDED 30

#define GBR_MOS_X 3681
#define GBR_MOS_Z 2963
#define GBR_HAR_X 3787
#define GBR_HAR_Z 2824
#define GBR_FENK_X 3548
#define GBR_FENK_Z 3550
#define GBR_RUFUS_X 3507
#define GBR_RUFUS_Z 3496
#define GBR_BASE_X 3784
#define GBR_BASE_Z 9225

static void
gbr_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "GBR PASS: %s\n", step);
}

static void
gbr_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
gbr_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
gbr_drain(struct ToriRSServer* srv)
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
gbr_finish(struct ToriRSServer* srv)
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
gbr_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
gbr_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : gbr_chatmenu();
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
gbr_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = gbr_chatmenu();
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
gbr_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    gbr_god(player);
    selftest_tick(srv);
}

static int
gbr_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    gbr_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
gbr_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
gbr_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
gbr_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
gbr_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
gbr_get_varp(struct ToriRSServerPlayer* player, const char* name)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp < 0 )
        return -1;
    return player->varps[varp];
}

static int
gbr_quest(struct ToriRSServerPlayer* player)
{
    return gbr_get_varp(player, "brain_quest_var");
}

static void
gbr_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;
    int rc;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    gbr_drain(srv);
    assert(slot >= 0);
    assert(srv->npcs[slot].active);
    player->last_slot = slot;
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 type %d slot %d should run (rc=%d active=%d)",
                   npc_type, slot, rc, srv->npcs[slot].active);
}

static void
gbr_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    gbr_talk(srv, npc_type, slot);
    gbr_finish(srv);
}

static void
gbr_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    gbr_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        gbr_click_until_menu(srv, 24);
        gbr_pick_row(srv, rows[i]);
    }
    gbr_finish(srv);
}

static void
gbr_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
gbr_inv_count(struct ToriRSServerPlayer* player, int obj_id)
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
gbr_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    gbr_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
gbr_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    gbr_finish(srv);
}

static void
gbr_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
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
    gbr_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
gbr_opheld(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    gbr_finish(srv);
}

static void
gbr_opheld2(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD2, obj_id, -1, -1);
    gbr_finish(srv);
}

static void
gbr_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    gbr_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
gbr_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
gbr_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        gbr_set_stat(player, stat, 99);
}

static void
gbr_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    gbr_varp(srv, "brain_quest_var", GBR_NOT_STARTED);
    gbr_vb(srv, "brain_tranquility_intro_chat", 0);
    gbr_vb(srv, "brain_broken_steps", 0);
    gbr_vb(srv, "brain_read_prayers", 0);
    gbr_vb(srv, "brain_words", 0);
    gbr_vb(srv, "brain_fenk_puzzle", 0);
    gbr_vb(srv, "brain_door_message", 0);
    gbr_vb(srv, "brain_crate", 0);
    gbr_vb(srv, "brain_barrel_setup", 0);
    gbr_vb(srv, "brain_clamp_given", 0);
    gbr_vb(srv, "brain_tongs_given", 0);
    gbr_vb(srv, "brain_hammer_given", 0);
    gbr_vb(srv, "brain_jars_given", 0);
    gbr_vb(srv, "brain_staples_given", 0);
    gbr_vb(srv, "brain_statue_pushed", 0);
    gbr_vb(srv, "brain_seen_wallbreaker", 0);
    gbr_vb(srv, "brain_multi_monk", 0);
    gbr_vb(srv, "creatureoffenkenstrain", 0);
    gbr_varp(srv, "fever_quest", 0);
    gbr_varp(srv, "rfd_pirate", 0);
}

static void
gbr_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    gbr_vb(srv, "creatureoffenkenstrain", GBR_FENK_COMPLETE);
    gbr_varp(srv, "fever_quest", GBR_FEVER_COMPLETE);
    gbr_varp(srv, "rfd_pirate", GBR_RFD_PIRATE_COMPLETE);
}

static void
gbr_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,brain_journal]", NULL, 0);
    gbr_finish(srv);
    gbr_pass(step);
}

static void
selftest_quest_gbr(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_mos;
    int npc_island;
    int npc_fenk;
    int npc_rufus;
    int npc_island_fenk;
    int npc_migor;
    int loc_statue;
    int loc_stairs;
    int loc_peep;
    int loc_book;
    int loc_crate_ghost;
    int loc_crate_walls;
    int loc_crate_bottom;
    int loc_crate_fenk;
    int loc_locker;
    int loc_door;
    int loc_clock;
    int obj_book;
    int obj_oak;
    int obj_hammer;
    int obj_knife;
    int obj_cat;
    int obj_plank;
    int obj_fur;
    int obj_whistle;
    int obj_order;
    int obj_fuse;
    int obj_keg;
    int obj_tinder;
    int obj_clamp;
    int obj_tongs;
    int obj_jar;
    int obj_staple;
    int obj_anchor;
    int obj_lamp;
    int stat_prayer;
    int stat_con;
    int stat_craft;
    int varp_qp;
    int slot_mos;
    int slot_island;
    int slot_fenk;
    int slot_rufus;
    int slot_ifenk;
    int slot_migor;
    int loc_slot;
    int prayer_before;
    int craft_before;
    int con_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };
    static const int k_travel_no[] = { 2 };
    static const int k_workshop_cat[] = { 2 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: great brain robbery critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer gbr selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    gbr_god(player);
    gbr_reset_quest(srv);
    gbr_clear_inv(player);

    npc_mos = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brain_tranquility");
    npc_island = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brain_island_tranquility");
    npc_fenk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fenk_fenkenstrain");
    npc_rufus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "werewolfshopkeeper1");
    npc_island_fenk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brain_island_fenkenstrain");
    npc_migor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brain_mi_gor");
    loc_statue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_statue_saradomin_closed");
    loc_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_underwater_stairs_broken");
    loc_peep = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_secret_room_spyhole");
    loc_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bookcase");
    loc_crate_ghost = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_ghost_crate");
    loc_crate_walls = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_crate_no_top");
    loc_crate_bottom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_crate_no_top_false_bottom");
    loc_crate_fenk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_crate_withfenkenstrain");
    loc_locker = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_boat_locker");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brain_mon_entrance_door");
    loc_clock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "poh_clockmaking_1");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_saradomin_book");
    obj_oak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "plank_oak");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
    obj_cat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_wooden_cat");
    obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "woodplank");
    obj_fur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fur");
    obj_whistle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_silver_whistle");
    obj_order = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_shipping_order");
    obj_fuse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_fuse");
    obj_keg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_gun_powder_barrel");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_clamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_cranial_clamp");
    obj_tongs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_tongs");
    obj_jar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_bell_jar");
    obj_staple = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_inv_skull_staple");
    obj_anchor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_broken_anchor");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brain_blessed_lamp");
    stat_prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_greatbrainrobbery") > 0,
                   "dbrow quest_greatbrainrobbery should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "brain_quest_var") >= 0,
                   "varp brain_quest_var should resolve");
    SELFTEST_CHECK(npc_mos > 0, "npc brain_tranquility should resolve");
    if( npc_mos <= 0 )
        return;

    gbr_journal(srv, "journal_00_not_started");

    slot_mos = gbr_spawn(srv, npc_mos, GBR_MOS_X, GBR_MOS_Z, 0);
    SELFTEST_CHECK(slot_mos >= 0, "Mos Tranquility should spawn");
    if( slot_mos < 0 )
        return;

    gbr_set_stat(player, stat_prayer, 1);
    gbr_set_stat(player, stat_con, 1);
    gbr_set_stat(player, stat_craft, 1);
    gbr_talk_finish(srv, npc_mos, slot_mos);
    SELFTEST_CHECK(gbr_quest(player) == GBR_NOT_STARTED,
                   "Prayer qualify-fail must stay not_started");
    gbr_pass("opnpc1_tranq_qualify_fail_prayer");

    gbr_set_stat(player, stat_prayer, 99);
    gbr_talk_finish(srv, npc_mos, slot_mos);
    SELFTEST_CHECK(gbr_quest(player) == GBR_NOT_STARTED,
                   "Construction qualify-fail must stay not_started");
    gbr_pass("opnpc1_tranq_qualify_fail_construction");

    gbr_set_stat(player, stat_con, 99);
    gbr_talk_finish(srv, npc_mos, slot_mos);
    SELFTEST_CHECK(gbr_quest(player) == GBR_NOT_STARTED,
                   "Crafting qualify-fail must stay not_started");
    gbr_pass("opnpc1_tranq_qualify_fail_crafting");

    gbr_skills99(player);
    gbr_talk_finish(srv, npc_mos, slot_mos);
    SELFTEST_CHECK(gbr_quest(player) == GBR_NOT_STARTED,
                   "Fenkenstrain qualify-fail must stay not_started");
    gbr_pass("opnpc1_tranq_qualify_fail_fenkenstrain");

    gbr_vb(srv, "creatureoffenkenstrain", GBR_FENK_COMPLETE);
    gbr_talk_finish(srv, npc_mos, slot_mos);
    SELFTEST_CHECK(gbr_quest(player) == GBR_NOT_STARTED,
                   "Cabin Fever qualify-fail must stay not_started");
    gbr_pass("opnpc1_tranq_qualify_fail_cabin_fever");

    gbr_varp(srv, "fever_quest", GBR_FEVER_COMPLETE);
    gbr_talk_finish(srv, npc_mos, slot_mos);
    SELFTEST_CHECK(gbr_quest(player) == GBR_NOT_STARTED,
                   "Pirate Pete qualify-fail must stay not_started");
    gbr_pass("opnpc1_tranq_qualify_fail_pirate_pete");

    gbr_prereqs(srv);
    gbr_talk_rows(srv, npc_mos, slot_mos, k_refuse, 1);
    SELFTEST_CHECK(gbr_quest(player) == GBR_NOT_STARTED,
                   "Tranquility refuse must stay not_started");
    gbr_pass("opnpc1_tranq_refuse");

    gbr_talk_rows(srv, npc_mos, slot_mos, k_accept, 1);
    SELFTEST_CHECK(gbr_quest(player) == GBR_EXPLORING,
                   "Tranquility accept must set exploring (got %d)",
                   gbr_quest(player));
    gbr_pass("opnpc1_tranq_accept");

    if( npc_island > 0 )
    {
        slot_island = gbr_spawn(srv, npc_island, GBR_HAR_X, GBR_HAR_Z, 0);
        SELFTEST_CHECK(slot_island >= 0, "Island Tranquility should spawn");
        if( slot_island >= 0 )
        {
            gbr_talk_finish(srv, npc_island, slot_island);
            SELFTEST_CHECK(gbr_quest(player) == GBR_EXPLORING,
                           "statue hint must stay exploring");
            gbr_pass("opnpc1_island_statue_hint");
        }
    }
    else
        slot_island = -1;

    if( loc_statue > 0 )
    {
        loc_slot = gbr_place_loc(srv, loc_statue, GBR_HAR_X, GBR_HAR_Z + 2, 0);
        gbr_oploc(srv, loc_statue, loc_slot);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_statue_pushed") == GBR_STATUE_OPEN,
                       "statue pull must open the passage");
        gbr_pass("oploc1_statue_pull");
    }

    if( loc_stairs > 0 )
    {
        loc_slot = gbr_place_loc(srv, loc_stairs, GBR_HAR_X + 2, GBR_HAR_Z, 0);
        gbr_oploc(srv, loc_stairs, loc_slot);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_broken_steps") == GBR_STAIRS_REPAIRED,
                       "stairs repair must set brain_broken_steps");
        gbr_pass("oploc1_stairs_repair");
    }

    if( loc_peep > 0 )
    {
        loc_slot = gbr_place_loc(srv, loc_peep, GBR_HAR_X + 3, GBR_HAR_Z, 0);
        gbr_oploc(srv, loc_peep, loc_slot);
        SELFTEST_CHECK(gbr_quest(player) == GBR_PEEP,
                       "peephole must advance to returned_from_peep (got %d)",
                       gbr_quest(player));
        gbr_pass("oploc1_peephole_first");
    }

    if( slot_island >= 0 )
    {
        gbr_talk_finish(srv, npc_island, slot_island);
        SELFTEST_CHECK(gbr_quest(player) == GBR_GETTING_BOOK,
                       "peep report must send the player for the book (got %d)",
                       gbr_quest(player));
        gbr_pass("opnpc1_island_book_fetch");
    }

    if( loc_book > 0 && obj_book > 0 )
    {
        loc_slot = gbr_place_loc(srv, loc_book, GBR_HAR_X + 4, GBR_HAR_Z, 0);
        gbr_oploc(srv, loc_book, loc_slot);
        SELFTEST_CHECK(gbr_inv_count(player, obj_book) > 0,
                       "bookcase search must give the Book of Saradomin");
        gbr_pass("oploc1_bookcase_find");

        gbr_opheld(srv, obj_book);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_read_prayers") == GBR_PRAYER_READ,
                       "reading the book must set brain_read_prayers");
        SELFTEST_CHECK(gbr_quest(player) == GBR_BOOK_READ,
                       "reading the book must advance to book_read (got %d)",
                       gbr_quest(player));
        gbr_pass("opheld1_book_read");

        if( slot_island >= 0 )
        {
            gbr_tele(srv, GBR_HAR_X, GBR_HAR_Z, 0);
            gbr_opheld2(srv, obj_book);
            SELFTEST_CHECK(gbr_quest(player) == GBR_RECITED,
                           "recite must advance to recited_prayer (got %d)",
                           gbr_quest(player));
            gbr_pass("opheld2_recite_success");
        }
    }
    else
    {
        gbr_varp(srv, "brain_quest_var", GBR_RECITED);
        gbr_vb(srv, "brain_read_prayers", GBR_PRAYER_READ);
        gbr_vb(srv, "brain_words", 1);
    }

    if( slot_island >= 0 )
    {
        gbr_talk_finish(srv, npc_island, slot_island);
        SELFTEST_CHECK(gbr_quest(player) == GBR_CRATE_SCHEME,
                       "recited talk must send the player to Fenkenstrain (got %d)",
                       gbr_quest(player));
        gbr_pass("opnpc1_island_fenk_send");
    }
    else
        gbr_varp(srv, "brain_quest_var", GBR_CRATE_SCHEME);

    if( npc_fenk > 0 )
    {
        slot_fenk = gbr_spawn(srv, npc_fenk, GBR_FENK_X, GBR_FENK_Z, 0);
        SELFTEST_CHECK(slot_fenk >= 0, "Fenkenstrain should spawn");
        if( slot_fenk >= 0 )
        {
            gbr_talk_finish(srv, npc_fenk, slot_fenk);
            SELFTEST_CHECK(gbr_get_vb(player, "brain_fenk_puzzle") == GBR_TALKED_FENK,
                           "first Fenk talk must set brain_fenk_puzzle");
            gbr_pass("opnpc1_fenk_first");
        }
    }
    else
        slot_fenk = -1;

    if( npc_rufus > 0 )
    {
        slot_rufus = gbr_spawn(srv, npc_rufus, GBR_RUFUS_X, GBR_RUFUS_Z, 0);
        SELFTEST_CHECK(slot_rufus >= 0, "Rufus should spawn");
        if( slot_rufus >= 0 )
        {
            gbr_talk_finish(srv, npc_rufus, slot_rufus);
            SELFTEST_CHECK(gbr_get_vb(player, "brain_fenk_puzzle") == GBR_TALKED_RUFUS,
                           "Rufus scheme talk must set brain_fenk_puzzle");
            if( obj_whistle > 0 )
                SELFTEST_CHECK(gbr_inv_count(player, obj_whistle) > 0,
                               "Rufus must give the wolf whistle");
            if( obj_order > 0 )
                SELFTEST_CHECK(gbr_inv_count(player, obj_order) > 0,
                               "Rufus must give the shipping order");
            gbr_pass("opnpc1_rufus_scheme");
        }
    }
    else
        slot_rufus = -1;

    if( slot_fenk >= 0 )
    {
        gbr_talk_finish(srv, npc_fenk, slot_fenk);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_crate") == GBR_CRATE_GHOST,
                       "Fenk build-ask must set crate ghost");
        gbr_pass("opnpc1_fenk_build_ask");
    }

    if( obj_oak > 0 && obj_knife > 0 && obj_cat > 0 )
    {
        gbr_clear_inv(player);
        gbr_give(player, obj_oak, 1);
        gbr_give(player, obj_knife, 1);
        gbr_opheldu(srv, obj_knife, obj_oak);
        SELFTEST_CHECK(gbr_inv_count(player, obj_cat) > 0,
                       "knife-on-oak-plank must carve a wooden cat");
        gbr_pass("opheldu_wooden_cat_knife");
    }

    if( loc_clock > 0 && obj_plank > 0 && obj_fur > 0 && obj_cat > 0 )
    {
        gbr_clear_inv(player);
        gbr_give(player, obj_plank, 1);
        gbr_give(player, obj_fur, 1);
        loc_slot = gbr_place_loc(srv, loc_clock, GBR_FENK_X + 6, GBR_FENK_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_clock, -1, loc_slot);
        gbr_click_until_menu(srv, 8);
        gbr_pick_row(srv, k_workshop_cat[0]);
        gbr_finish(srv);
        SELFTEST_CHECK(gbr_inv_count(player, obj_cat) > 0,
                       "POH workshop wooden cat must craft a cat");
        gbr_pass("oploc1_poh_workshop_wooden_cat");
    }

    if( loc_crate_ghost > 0 && obj_oak > 0 && obj_hammer > 0 )
    {
        gbr_clear_inv(player);
        gbr_give(player, obj_oak, 4);
        gbr_give(player, obj_hammer, 1);
        loc_slot = gbr_place_loc(srv, loc_crate_ghost, GBR_FENK_X + 2, GBR_FENK_Z, 0);
        gbr_oploc(srv, loc_crate_ghost, loc_slot);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_crate") == GBR_CRATE_WALLS,
                       "crate build must set walls");
        gbr_pass("oploc1_crate_build_walls");
    }

    if( loc_crate_walls > 0 )
    {
        loc_slot = gbr_place_loc(srv, loc_crate_walls, GBR_FENK_X + 2, GBR_FENK_Z, 0);
        gbr_oploc(srv, loc_crate_walls, loc_slot);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_crate") == GBR_CRATE_BOTTOM,
                       "false bottom must set crate bottom");
        gbr_pass("oploc1_crate_add_bottom");
    }

    if( loc_crate_bottom > 0 && obj_cat > 0 )
    {
        gbr_give(player, obj_cat, GBR_CATS_NEEDED);
        loc_slot = gbr_place_loc(srv, loc_crate_bottom, GBR_FENK_X + 2, GBR_FENK_Z, 0);
        gbr_oploc(srv, loc_crate_bottom, loc_slot);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_crate") == GBR_CRATE_CATS,
                       "filling cats must set crate cats");
        gbr_pass("oploc1_crate_fill_cats");
    }

    if( obj_whistle > 0 && slot_fenk >= 0 )
    {
        gbr_give(player, obj_whistle, 1);
        gbr_tele(srv, GBR_FENK_X, GBR_FENK_Z, 0);
        gbr_opheld(srv, obj_whistle);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_crate") == GBR_CRATE_FENK,
                       "whistle must hide Fenkenstrain in the crate");
        gbr_pass("opheld1_whistle_success");
    }

    if( loc_crate_fenk > 0 && obj_order > 0 )
    {
        gbr_give(player, obj_order, 1);
        loc_slot = gbr_place_loc(srv, loc_crate_fenk, GBR_FENK_X + 2, GBR_FENK_Z, 0);
        gbr_use_loc(srv, loc_crate_fenk, loc_slot, obj_order);
        SELFTEST_CHECK(gbr_quest(player) == GBR_BASEMENT,
                       "shipping order must send Fenk to Harmony (got %d)",
                       gbr_quest(player));
        gbr_pass("oplocu_shipping_attach");
    }
    else
        gbr_varp(srv, "brain_quest_var", GBR_BASEMENT);

    if( npc_island_fenk > 0 )
    {
        slot_ifenk = gbr_spawn(srv, npc_island_fenk, GBR_BASE_X, GBR_BASE_Z, 0);
        SELFTEST_CHECK(slot_ifenk >= 0, "Island Fenkenstrain should spawn");
        if( slot_ifenk >= 0 )
        {
            gbr_talk_finish(srv, npc_island_fenk, slot_ifenk);
            SELFTEST_CHECK(gbr_quest(player) == GBR_DOOR,
                           "basement Fenk first talk must start the door sequence");
            gbr_pass("opnpc1_fenk_basement_first");
        }
    }
    else
        slot_ifenk = -1;

    if( loc_locker > 0 && obj_fuse > 0 )
    {
        loc_slot = gbr_place_loc(srv, loc_locker, GBR_HAR_X + 5, GBR_HAR_Z, 0);
        gbr_oploc(srv, loc_locker, loc_slot);
        SELFTEST_CHECK(gbr_inv_count(player, obj_fuse) > 0,
                       "boat locker must give a fuse");
        gbr_pass("oploc1_locker_find");
    }

    if( loc_door > 0 && obj_keg > 0 )
    {
        gbr_give(player, obj_keg, 1);
        loc_slot = gbr_place_loc(srv, loc_door, GBR_HAR_X + 6, GBR_HAR_Z, 0);
        gbr_use_loc(srv, loc_door, loc_slot, obj_keg);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_barrel_setup") == GBR_BARREL_KEG,
                       "keg must be placed on the door");
        gbr_pass("oplocu_keg_place");
    }

    if( loc_door > 0 && obj_fuse > 0 )
    {
        gbr_give(player, obj_fuse, 1);
        loc_slot = gbr_place_loc(srv, loc_door, GBR_HAR_X + 6, GBR_HAR_Z, 0);
        gbr_use_loc(srv, loc_door, loc_slot, obj_fuse);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_barrel_setup") == GBR_BARREL_FUSE,
                       "fuse must be added to the keg");
        gbr_pass("oplocu_fuse_add");
    }

    if( loc_door > 0 && obj_tinder > 0 )
    {
        gbr_give(player, obj_tinder, 1);
        loc_slot = gbr_place_loc(srv, loc_door, GBR_HAR_X + 6, GBR_HAR_Z, 0);
        gbr_use_loc(srv, loc_door, loc_slot, obj_tinder);
        SELFTEST_CHECK(gbr_get_vb(player, "brain_barrel_setup") == GBR_BARREL_GONE,
                       "lighting the fuse must destroy the door");
        gbr_pass("oplocu_fuse_light");
    }

    if( slot_ifenk >= 0 && obj_clamp > 0 && obj_tongs > 0 && obj_jar > 0 &&
        obj_staple > 0 )
    {
        gbr_give(player, obj_clamp, 1);
        gbr_give(player, obj_tongs, 1);
        gbr_give(player, obj_jar, GBR_JARS_NEEDED);
        gbr_give(player, obj_staple, GBR_STAPLES_NEEDED);
        gbr_talk_finish(srv, npc_island_fenk, slot_ifenk);
        SELFTEST_CHECK(gbr_quest(player) == GBR_HELPED,
                       "instrument hand-in must set helped_fenk (got %d)",
                       gbr_quest(player));
        gbr_pass("opnpc1_fenk_handin");
    }
    else
        gbr_varp(srv, "brain_quest_var", GBR_HELPED);

    if( slot_island >= 0 )
    {
        gbr_talk_finish(srv, npc_island, slot_island);
        SELFTEST_CHECK(gbr_quest(player) == GBR_CONFRONT,
                       "helped-fenk talk must send the player to Mi-Gor");
        gbr_pass("opnpc1_island_confront_send");
    }
    else
        gbr_varp(srv, "brain_quest_var", GBR_CONFRONT);

    if( npc_migor > 0 )
    {
        slot_migor = gbr_spawn(srv, npc_migor, GBR_HAR_X, GBR_HAR_Z, 0);
        SELFTEST_CHECK(slot_migor >= 0, "Mi-Gor should spawn");
        if( slot_migor >= 0 )
        {
            gbr_talk_finish(srv, npc_migor, slot_migor);
            SELFTEST_CHECK(gbr_get_vb(player, "brain_seen_wallbreaker") == 1,
                           "Mi-Gor confront must reveal Barrelchest");
            gbr_pass("opnpc1_migor_confront");
            gbr_free_npc(srv, slot_migor);
        }
    }

    gbr_varp(srv, "brain_quest_var", GBR_READY);
    if( obj_anchor > 0 )
    {
        gbr_give(player, obj_anchor, 1);
        gbr_opheld(srv, obj_anchor);
        gbr_pass("opheld1_anchor_examine");
    }

    prayer_before = player->stat_xp_tenths[stat_prayer >= 0 ? stat_prayer : 0];
    craft_before = player->stat_xp_tenths[stat_craft >= 0 ? stat_craft : 0];
    con_before = player->stat_xp_tenths[stat_con >= 0 ? stat_con : 0];
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;

    if( slot_island >= 0 )
    {
        gbr_talk_finish(srv, npc_island, slot_island);
        SELFTEST_CHECK(gbr_quest(player) == GBR_COMPLETE,
                       "ready-to-finish talk must complete the quest (got %d)",
                       gbr_quest(player));
        if( stat_prayer >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_prayer] >=
                               prayer_before + GBR_REWARD_PRAYER_TENTHS,
                           "complete must award 60000 Prayer tenths");
        if( stat_craft >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >=
                               craft_before + GBR_REWARD_CRAFT_TENTHS,
                           "complete must award 30000 Crafting tenths");
        if( stat_con >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_con] >=
                               con_before + GBR_REWARD_CON_TENTHS,
                           "complete must award 20000 Construction tenths");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + GBR_REWARD_QP,
                           "complete must award 2 quest points");
        if( obj_lamp > 0 )
            SELFTEST_CHECK(gbr_inv_count(player, obj_lamp) > 0,
                           "complete must give the blessed lamp");
        gbr_pass("opnpc1_island_complete");
    }

    gbr_journal(srv, "journal_130_complete");

    ToriRSServer_ScriptsRunProc(srv, "[proc,brain_leftover_barrelchest_prayer_drain]", NULL, 0);
    gbr_finish(srv);
    gbr_pass("leftover_barrelchest_prayer_drain");
    ToriRSServer_ScriptsRunProc(srv, "[proc,brain_leftover_flood_peephole_extra]", NULL, 0);
    gbr_finish(srv);
    gbr_pass("leftover_flood_peephole_extra");
    ToriRSServer_ScriptsRunProc(srv, "[proc,brain_leftover_windmill_flour]", NULL, 0);
    gbr_finish(srv);
    gbr_pass("leftover_windmill_flour");
    ToriRSServer_ScriptsRunProc(srv, "[proc,brain_leftover_anchor_pirate_smith]", NULL, 0);
    gbr_finish(srv);
    gbr_pass("leftover_anchor_pirate_smith");

    if( slot_mos >= 0 && gbr_quest(player) == GBR_COMPLETE )
    {
        gbr_tele(srv, GBR_MOS_X, GBR_MOS_Z, 0);
        gbr_talk_rows(srv, npc_mos, slot_mos, k_travel_no, 1);
        gbr_pass("opnpc1_tranq_post_travel_no");
    }

    gbr_free_npc(srv, slot_mos);
    gbr_free_npc(srv, slot_island);
    gbr_free_npc(srv, slot_fenk);
    gbr_free_npc(srv, slot_rufus);
    gbr_free_npc(srv, slot_ifenk);
    gbr_reset_quest(srv);
    gbr_clear_inv(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_GREATBRAINROBBERY_SELFTEST_U_H */
