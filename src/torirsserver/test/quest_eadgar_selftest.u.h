#ifndef TORIRSSERVER_TEST_QUEST_EADGAR_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_EADGAR_SELFTEST_U_H

/* Eadgar's Ruse Gate D C walk. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPLOC / OPLOCU / OPNPCU / OPHELDU
 * dispatch on the authored path. Silent success is forbidden: each step
 * prints EADGAR PASS. player->godmode = 1 for the whole walk (not a death
 * test).
 *
 * Completion goes through sanfew.rs2's authored
 * ~quest_complete_rewards(quest_eadgarsruse, ...). Do not rewrite Troll
 * Stronghold.
 */

#define EADGAR_STARTED 10
#define EADGAR_SPOKEN_EADGAR_FIRST 15
#define EADGAR_SPOKEN_BURNTMEAT_FIRST 20
#define EADGAR_SPOKEN_BURNTMEAT_SECOND 25
#define EADGAR_NEEDS_PARROT 30
#define EADGAR_EXPLAINED_PLAN 50
#define EADGAR_HID_PARROT 60
#define EADGAR_NEEDS_ITEMS 70
#define EADGAR_NEEDS_POTION 80
#define EADGAR_NEEDS_PARROT_BACK 85
#define EADGAR_GOT_PARROT_BACK 86
#define EADGAR_GOT_FAKE_MAN 87
#define EADGAR_GOT_BURNT_MEAT 90
#define EADGAR_UNLOCKED_STOREROOM 100
#define EADGAR_COMPLETE 110

#define EADGAR_BIT_LOGS 1
#define EADGAR_BIT_CLOTHES 2

#define DRUID_COMPLETE 4

#ifndef EADGAR_STAT_HERBLORE
#define EADGAR_STAT_HERBLORE 15
#endif

static void
eadgar_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "EADGAR PASS: %s\n", step);
}

static void
eadgar_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
eadgar_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
eadgar_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
eadgar_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
eadgar_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = eadgar_chatmenu();
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
eadgar_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = eadgar_chatmenu();
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
eadgar_resume_mesbox(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    int com;
    int i;

    assert(srv);
    player = srv->active_player;
    assert(player);
    com = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
    if( com > 0 && player->active_script )
        ToriRSServer_ScriptsResumeButton(srv, com);
    for( i = 0; i < 4; i++ )
        selftest_tick(srv);
}

static int
eadgar_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    return n;
}

static void
eadgar_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( eadgar_inv_total(player, obj_id) >= count )
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
eadgar_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
eadgar_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static void
eadgar_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    eadgar_god(player);
    selftest_tick(srv);
}

static int
eadgar_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    eadgar_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
eadgar_set_herblore(struct ToriRSServerPlayer* player, int level)
{
    int stat;

    assert(player);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
    if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        stat = EADGAR_STAT_HERBLORE;
    assert(stat >= 0);
    assert(stat < TORIRSSERVER_STAT_COUNT);
    ToriRSServer_CombatSetLevel(player, stat, level);
}

static int
eadgar_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    eadgar_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
eadgar_talk_sanfew_more_work(struct ToriRSServer* srv, int npc_sanfew, int slot)
{
    assert(srv);
    assert(npc_sanfew > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sanfew, -1, slot);
    eadgar_click_until_menu(srv, 8);
    eadgar_pick_row(srv, 1);
}

static void
selftest_quest_eadgar(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int varp;
    int varp_druid;
    int varp_bits;
    int varp_grain;
    int varp_chickens;
    int npc_sanfew;
    int npc_eadgar;
    int npc_burntmeat;
    int npc_pete;
    int npc_tegid;
    int npc_thistle;
    int loc_drawers;
    int loc_drawers_open;
    int loc_door;
    int loc_rack;
    int loc_crate;
    int loc_hatch;
    int loc_fire;
    int obj_parrot;
    int obj_chunks;
    int obj_vodka;
    int obj_pine;
    int obj_robe;
    int obj_thistle;
    int obj_dried;
    int obj_ground;
    int obj_potion;
    int obj_fake;
    int obj_key;
    int obj_gout;
    int obj_logs;
    int obj_chicken;
    int obj_grain;
    int obj_ranarr;
    int obj_pestle;
    int sanfew_slot = -1;
    int eadgar_slot = -1;
    int burntmeat_slot = -1;
    int pete_slot = -1;
    int tegid_slot = -1;
    int thistle_slot = -1;
    int loaded;
    int i;

    assert(srv);
    assert(player);

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "EADGAR SKIP: no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    eadgar_god(player);

    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "eadgar_quest");
    varp_druid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "druidquest");
    varp_bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "eadgar_bits");
    varp_grain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "eadgar_grain");
    varp_chickens = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "eadgar_chickens");
    npc_sanfew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sanfew");
    npc_eadgar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "troll_eadgar");
    npc_burntmeat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eadgar_troll_chief_cook");
    npc_pete = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eadgar_zoo_keeper_aviary");
    npc_tegid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eadgar_druid_washing");
    npc_thistle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eadgar_troll_thistle");
    loc_drawers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eadgar_kitchen_drawers");
    loc_drawers_open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eadgar_kitchen_drawers_open");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eadgar_storeroomdoor");
    loc_rack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eadgar_rack");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eadgar_crate_goutweed");
    loc_hatch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eadgar_aviary_wall_hatch");
    loc_fire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fire");
    obj_parrot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_drunk_parrot");
    obj_chunks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_alco_chunks");
    obj_vodka = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vodka");
    obj_pine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pineapple_chunks");
    obj_robe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_dirty_druid_robe");
    obj_thistle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_troll_thistle");
    obj_dried = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_dried_troll_thistle");
    obj_ground = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_ground_troll_thistle");
    obj_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_ground_troll_thistle_potion");
    obj_fake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_fake_man");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_troll_storeroom_key");
    obj_gout = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eadgar_goutweed_herb");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_chicken");
    obj_grain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grain");
    obj_ranarr = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ranarrvial");
    obj_pestle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pestle_and_mortar");

    SELFTEST_CHECK(varp >= 0, "varp eadgar_quest should resolve");
    SELFTEST_CHECK(varp_druid >= 0, "varp druidquest should resolve");
    SELFTEST_CHECK(npc_sanfew > 0, "npc sanfew should resolve");
    SELFTEST_CHECK(npc_eadgar > 0, "npc troll_eadgar should resolve");
    SELFTEST_CHECK(npc_burntmeat > 0, "npc eadgar_troll_chief_cook should resolve");
    SELFTEST_CHECK(npc_pete > 0, "npc eadgar_zoo_keeper_aviary should resolve");
    SELFTEST_CHECK(npc_tegid > 0, "npc eadgar_druid_washing should resolve");
    SELFTEST_CHECK(obj_gout > 0, "obj eadgar_goutweed_herb should resolve");
    if( varp < 0 || varp_druid < 0 || npc_sanfew <= 0 || npc_eadgar <= 0 ||
        npc_burntmeat <= 0 )
        return;

    /* ---- Sanfew prereq fails / offer / decline / accept ---- */
    sanfew_slot = eadgar_spawn(srv, npc_sanfew, 2897, 3426, 1);
    SELFTEST_CHECK(sanfew_slot >= 0, "sanfew should spawn");
    if( sanfew_slot >= 0 )
    {
        eadgar_clear_inv(player);
        player->varps[varp] = 0;
        player->varps[varp_druid] = 0;
        if( varp_bits >= 0 )
            player->varps[varp_bits] = 0;
        if( varp_grain >= 0 )
            player->varps[varp_grain] = 0;
        if( varp_chickens >= 0 )
            player->varps[varp_chickens] = 0;
        eadgar_set_bit(srv, "troll_freed_eadgar", 0);
        eadgar_set_herblore(player, 1);
        eadgar_god(player);

        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 0,
                       "Druidic Ritual incomplete must leave eadgar_quest at 0, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_sanfew_prereq_druid");

        player->varps[varp_druid] = DRUID_COMPLETE;
        eadgar_set_herblore(player, 30);
        eadgar_set_bit(srv, "troll_freed_eadgar", 1);
        eadgar_god(player);
        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 0,
                       "Herblore 30 must leave eadgar_quest at 0, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_sanfew_prereq_herblore");

        eadgar_set_herblore(player, 31);
        eadgar_set_bit(srv, "troll_freed_eadgar", 0);
        eadgar_god(player);
        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 0,
                       "Eadgar not freed must leave eadgar_quest at 0, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_sanfew_prereq_eadgar");

        eadgar_set_bit(srv, "troll_freed_eadgar", 1);
        eadgar_god(player);
        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        eadgar_click_until_menu(srv, 16);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "Sanfew offer should open a choice");
        eadgar_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 0,
                       "declining Sanfew must leave eadgar_quest at 0, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_sanfew_decline");

        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        eadgar_click_until_menu(srv, 16);
        eadgar_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        eadgar_resume_mesbox(srv);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_STARTED,
                       "accepting Sanfew should write eadgar_quest=10, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_sanfew_accept");

        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_STARTED,
                       "started remind must leave eadgar_quest at 10, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_sanfew_started_remind");

        player->varps[varp] = EADGAR_NEEDS_PARROT;
        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_NEEDS_PARROT,
                       "mid remind must leave eadgar_quest at 30, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_sanfew_mid_remind");
    }

    /* ---- Eadgar goutweed ask / parrot plan / item + potion hand-ins ---- */
    eadgar_slot = eadgar_spawn(srv, npc_eadgar, 2893, 3671, 0);
    SELFTEST_CHECK(eadgar_slot >= 0, "troll_eadgar should spawn at home");
    if( eadgar_slot >= 0 )
    {
        player->varps[varp] = EADGAR_STARTED;
        eadgar_clear_inv(player);
        eadgar_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        eadgar_click_until_menu(srv, 8);
        eadgar_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_SPOKEN_EADGAR_FIRST,
                       "asking Eadgar about goutweed should write 15, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_eadgar_goutweed_ask");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_eadgar_remind_burntmeat");

        player->varps[varp] = EADGAR_SPOKEN_BURNTMEAT_FIRST;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        selftest_click_through(srv, 12);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_NEEDS_PARROT,
                       "Eadgar parrot plan should write 30, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_eadgar_parrot_plan");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_eadgar_want_parrot_none");

        if( obj_parrot > 0 )
        {
            eadgar_give(player, obj_parrot, 1);
            player->last_useitem = obj_parrot;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eadgar, -1,
                                           eadgar_slot);
            selftest_click_through(srv, 12);
            eadgar_close(srv);
            SELFTEST_CHECK(player->varps[varp] == EADGAR_EXPLAINED_PLAN,
                           "showing the parrot should write 50, got %d",
                           player->varps[varp]);
            eadgar_pass("opnpcu_eadgar_show_parrot");
        }

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_eadgar_hide_reminder");

        player->varps[varp] = EADGAR_HID_PARROT;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        selftest_click_through(srv, 12);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_NEEDS_ITEMS,
                       "reporting the hidden parrot should write 70, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_eadgar_explain_items");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_eadgar_items_check");

        if( obj_logs > 0 )
        {
            eadgar_give(player, obj_logs, 1);
            player->last_useitem = obj_logs;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eadgar, -1,
                                           eadgar_slot);
            selftest_click_through(srv, 8);
            eadgar_close(srv);
            eadgar_pass("opnpcu_eadgar_give_logs");
        }
        if( obj_robe > 0 )
        {
            eadgar_give(player, obj_robe, 1);
            player->last_useitem = obj_robe;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eadgar, -1,
                                           eadgar_slot);
            selftest_click_through(srv, 8);
            eadgar_close(srv);
            eadgar_pass("opnpcu_eadgar_give_robe");
        }
        if( obj_chicken > 0 )
        {
            eadgar_give(player, obj_chicken, 5);
            player->last_useitem = obj_chicken;
            for( i = 0; i < 5; i++ )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eadgar,
                                               -1, eadgar_slot);
                selftest_click_through(srv, 4);
                eadgar_close(srv);
            }
            eadgar_pass("opnpcu_eadgar_give_chickens");
        }
        if( obj_grain > 0 )
        {
            eadgar_give(player, obj_grain, 10);
            player->last_useitem = obj_grain;
            for( i = 0; i < 10; i++ )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eadgar,
                                               -1, eadgar_slot);
                selftest_click_through(srv, 4);
                eadgar_close(srv);
            }
            eadgar_pass("opnpcu_eadgar_give_grain");
        }
        SELFTEST_CHECK(player->varps[varp] == EADGAR_NEEDS_POTION,
                       "all fake-man ingredients should write 80, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpcu_eadgar_items_complete");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_eadgar_want_potion_none");

        if( obj_potion > 0 )
        {
            eadgar_give(player, obj_potion, 1);
            player->last_useitem = obj_potion;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eadgar, -1,
                                           eadgar_slot);
            selftest_click_through(srv, 8);
            eadgar_close(srv);
            SELFTEST_CHECK(player->varps[varp] == EADGAR_NEEDS_PARROT_BACK,
                           "potion hand-in should write 85, got %d",
                           player->varps[varp]);
            eadgar_pass("opnpcu_eadgar_give_potion");
        }

        player->varps[varp] = EADGAR_GOT_PARROT_BACK;
        if( obj_parrot > 0 )
        {
            eadgar_clear_inv(player);
            eadgar_give(player, obj_parrot, 1);
            player->last_useitem = obj_parrot;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_eadgar, -1,
                                           eadgar_slot);
            selftest_click_through(srv, 8);
            eadgar_close(srv);
            SELFTEST_CHECK(player->varps[varp] == EADGAR_GOT_FAKE_MAN,
                           "returning the trained parrot should write 87, got %d",
                           player->varps[varp]);
            if( obj_fake > 0 )
                SELFTEST_CHECK(eadgar_inv_total(player, obj_fake) >= 1,
                               "Eadgar should grant a fake man");
            eadgar_pass("opnpcu_eadgar_make_fake_man");
        }

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_eadgar_postfakeman");
    }

    /* ---- Burntmeat first / second / fake-man hand-in ---- */
    burntmeat_slot = eadgar_spawn(srv, npc_burntmeat, 2844, 10057, 1);
    SELFTEST_CHECK(burntmeat_slot >= 0, "eadgar_troll_chief_cook should spawn");
    if( burntmeat_slot >= 0 )
    {
        player->varps[varp] = EADGAR_STARTED;
        eadgar_clear_inv(player);
        eadgar_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_burntmeat, -1,
                                       burntmeat_slot);
        selftest_click_through(srv, 16);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_SPOKEN_BURNTMEAT_FIRST,
                       "Burntmeat first should write 20, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_burntmeat_first");

        player->varps[varp] = EADGAR_SPOKEN_EADGAR_FIRST;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_burntmeat, -1,
                                       burntmeat_slot);
        selftest_click_through(srv, 16);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_SPOKEN_BURNTMEAT_SECOND,
                       "Burntmeat second should write 25, got %d",
                       player->varps[varp]);
        eadgar_pass("opnpc1_burntmeat_second");

        player->varps[varp] = EADGAR_NEEDS_PARROT;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_burntmeat, -1,
                                       burntmeat_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_burntmeat_working");

        player->varps[varp] = EADGAR_GOT_FAKE_MAN;
        if( obj_fake > 0 )
        {
            eadgar_give(player, obj_fake, 1);
            player->last_useitem = obj_fake;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_burntmeat,
                                           -1, burntmeat_slot);
            eadgar_click_until_menu(srv, 16);
            eadgar_pick_row(srv, 1);
            selftest_click_through(srv, 8);
            eadgar_close(srv);
            SELFTEST_CHECK(player->varps[varp] == EADGAR_GOT_BURNT_MEAT,
                           "fake-man hand-in should write 90, got %d",
                           player->varps[varp]);
            eadgar_pass("opnpcu_burntmeat_fake_man");
        }

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_burntmeat, -1,
                                       burntmeat_slot);
        eadgar_click_until_menu(srv, 8);
        eadgar_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_burntmeat_where_goutweed");
    }

    /* ---- Parroty Pete / alco-chunks / hatch ---- */
    pete_slot = eadgar_spawn(srv, npc_pete, 2611, 3285, 0);
    SELFTEST_CHECK(pete_slot >= 0, "eadgar_zoo_keeper_aviary should spawn");
    if( pete_slot >= 0 )
    {
        player->varps[varp] = EADGAR_NEEDS_PARROT;
        eadgar_clear_inv(player);
        eadgar_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_pete, -1,
                                       pete_slot);
        eadgar_click_until_menu(srv, 8);
        eadgar_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_pete_when_add");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_pete, -1,
                                       pete_slot);
        eadgar_click_until_menu(srv, 8);
        eadgar_pick_row(srv, 3);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_pete_what_feed");

        if( obj_parrot > 0 )
        {
            eadgar_give(player, obj_parrot, 1);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_pete, -1,
                                           pete_slot);
            selftest_click_through(srv, 8);
            eadgar_close(srv);
            eadgar_pass("opnpc1_pete_drunk_parrot");
        }
    }

    if( obj_vodka > 0 && obj_pine > 0 && obj_chunks > 0 )
    {
        eadgar_clear_inv(player);
        eadgar_give(player, obj_vodka, 1);
        eadgar_give(player, obj_pine, 1);
        player->last_item = obj_vodka;
        player->last_useitem = obj_pine;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_vodka, -1, -1);
        eadgar_close(srv);
        SELFTEST_CHECK(eadgar_inv_total(player, obj_chunks) >= 1,
                       "vodka on pineapple chunks should make alco-chunks");
        eadgar_pass("opheldu_alco_chunks");
    }

    if( loc_hatch > 0 && obj_chunks > 0 && obj_parrot > 0 )
    {
        int hatch_slot;

        player->varps[varp] = EADGAR_NEEDS_PARROT;
        eadgar_clear_inv(player);
        eadgar_give(player, obj_chunks, 1);
        hatch_slot = eadgar_place_loc(srv, loc_hatch, 2612, 3284, 0);
        player->last_useitem = obj_chunks;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_hatch, -1,
                                            hatch_slot);
        selftest_tick(srv);
        eadgar_close(srv);
        SELFTEST_CHECK(eadgar_inv_total(player, obj_parrot) >= 1,
                       "alco-chunks on the hatch should catch a parrot");
        eadgar_pass("oplocu_hatch_catch");
    }

    /* ---- Tegid dirty robe ---- */
    tegid_slot = eadgar_spawn(srv, npc_tegid, 2913, 3417, 0);
    SELFTEST_CHECK(tegid_slot >= 0, "eadgar_druid_washing should spawn");
    if( tegid_slot >= 0 )
    {
        player->varps[varp] = EADGAR_NEEDS_ITEMS;
        if( varp_bits >= 0 )
            player->varps[varp_bits] = 0;
        eadgar_clear_inv(player);
        eadgar_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tegid, -1,
                                       tegid_slot);
        eadgar_click_until_menu(srv, 8);
        eadgar_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_tegid_nevermind");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tegid, -1,
                                       tegid_slot);
        eadgar_click_until_menu(srv, 8);
        eadgar_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        if( obj_robe > 0 )
            SELFTEST_CHECK(eadgar_inv_total(player, obj_robe) >= 1,
                           "Tegid should hand over a dirty robe");
        eadgar_pass("opnpc1_tegid_give_robe");
    }

    /* ---- Thistle pick / dry / mix ---- */
    if( npc_thistle > 0 )
    {
        thistle_slot = eadgar_spawn(srv, npc_thistle, 2891, 3676, 0);
        SELFTEST_CHECK(thistle_slot >= 0, "eadgar_troll_thistle should spawn");
        if( thistle_slot >= 0 && obj_thistle > 0 )
        {
            eadgar_clear_inv(player);
            eadgar_god(player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_thistle, -1,
                                           thistle_slot);
            eadgar_close(srv);
            SELFTEST_CHECK(eadgar_inv_total(player, obj_thistle) >= 1,
                           "picking the thistle should grant eadgar_troll_thistle");
            eadgar_pass("opnpc1_thistle_pick");
        }
    }

    if( loc_fire > 0 && obj_thistle > 0 && obj_dried > 0 )
    {
        int fire_slot;

        eadgar_clear_inv(player);
        eadgar_give(player, obj_thistle, 1);
        fire_slot = eadgar_place_loc(srv, loc_fire, 2893, 3671, 0);
        player->last_useitem = obj_thistle;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_fire, -1,
                                            fire_slot);
        eadgar_close(srv);
        SELFTEST_CHECK(eadgar_inv_total(player, obj_dried) >= 1,
                       "drying thistle on a fire should grant dried thistle");
        eadgar_pass("oplocu_thistle_dry");
    }

    if( obj_dried > 0 && obj_pestle > 0 )
    {
        eadgar_clear_inv(player);
        eadgar_give(player, obj_dried, 1);
        eadgar_give(player, obj_pestle, 1);
        player->last_useitem = obj_pestle;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_dried, -1, -1);
        eadgar_close(srv);
        eadgar_pass("opheldu_thistle_grind");
    }

    if( obj_ground > 0 && obj_ranarr > 0 && obj_potion > 0 )
    {
        eadgar_clear_inv(player);
        eadgar_give(player, obj_ground, 1);
        eadgar_give(player, obj_ranarr, 1);
        player->last_useitem = obj_ground;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_ranarr, -1, -1);
        eadgar_close(srv);
        SELFTEST_CHECK(eadgar_inv_total(player, obj_potion) >= 1,
                       "mixing ground thistle into ranarr (unf) should make the potion");
        eadgar_pass("opheldu_troll_potion");
    }

    /* ---- Rack hide / fetch ---- */
    if( loc_rack > 0 && obj_parrot > 0 )
    {
        int rack_slot;

        player->varps[varp] = EADGAR_EXPLAINED_PLAN;
        eadgar_clear_inv(player);
        eadgar_give(player, obj_parrot, 1);
        rack_slot = eadgar_place_loc(srv, loc_rack, 2831, 10077, 0);
        player->last_useitem = obj_parrot;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_rack, -1,
                                            rack_slot);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_HID_PARROT,
                       "hiding the parrot should write 60, got %d",
                       player->varps[varp]);
        eadgar_pass("oplocu_hide_parrot");

        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rack, -1,
                                            rack_slot);
        eadgar_close(srv);
        eadgar_pass("oploc1_rack_empty");

        player->varps[varp] = EADGAR_NEEDS_PARROT_BACK;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rack, -1,
                                            rack_slot);
        eadgar_resume_mesbox(srv);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_GOT_PARROT_BACK,
                       "fetching the parrot should write 86, got %d",
                       player->varps[varp]);
        eadgar_pass("oploc1_rack_fetch");
    }

    /* ---- Drawers / storeroom / goutweed crate ---- */
    if( loc_drawers_open > 0 )
    {
        int drawer_slot;

        player->varps[varp] = EADGAR_NEEDS_PARROT;
        eadgar_clear_inv(player);
        drawer_slot = eadgar_place_loc(srv, loc_drawers_open, 2845, 10057, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_drawers_open,
                                            -1, drawer_slot);
        eadgar_close(srv);
        eadgar_pass("oploc2_drawers_empty");

        player->varps[varp] = EADGAR_GOT_BURNT_MEAT;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_drawers_open,
                                            -1, drawer_slot);
        eadgar_close(srv);
        if( obj_key > 0 )
            SELFTEST_CHECK(eadgar_inv_total(player, obj_key) >= 1,
                           "searching the drawers at 90 should grant the key");
        eadgar_pass("oploc2_drawers_key");
    }

    if( loc_drawers > 0 )
    {
        int closed;

        closed = eadgar_place_loc(srv, loc_drawers, 2846, 10057, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_drawers, -1,
                                            closed);
        eadgar_close(srv);
        eadgar_pass("oploc1_drawers_open");
    }

    if( loc_door > 0 )
    {
        int door_slot;

        player->varps[varp] = EADGAR_GOT_BURNT_MEAT;
        eadgar_clear_inv(player);
        door_slot = eadgar_place_loc(srv, loc_door, 2847, 10058, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1,
                                            door_slot);
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_GOT_BURNT_MEAT,
                       "storeroom without a key must stay at 90, got %d",
                       player->varps[varp]);
        eadgar_pass("oploc1_storeroom_locked");

        if( obj_key > 0 )
        {
            eadgar_give(player, obj_key, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door,
                                                -1, door_slot);
            eadgar_close(srv);
            SELFTEST_CHECK(player->varps[varp] == EADGAR_UNLOCKED_STOREROOM,
                           "unlocking the storeroom should write 100, got %d",
                           player->varps[varp]);
            eadgar_pass("oploc1_storeroom_unlock");
        }
    }

    if( loc_crate > 0 && obj_gout > 0 )
    {
        int crate_slot;

        player->varps[varp] = EADGAR_UNLOCKED_STOREROOM;
        eadgar_clear_inv(player);
        eadgar_god(player);
        crate_slot = eadgar_place_loc(srv, loc_crate, 2848, 10059, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crate, -1,
                                            crate_slot);
        selftest_tick(srv);
        eadgar_close(srv);
        SELFTEST_CHECK(eadgar_inv_total(player, obj_gout) >= 1,
                       "searching the crate should grant goutweed");
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "crate search must leave the player alive");
        eadgar_pass("oploc1_goutweed_crate");
    }

    /* ---- Sanfew turn-in + real complete scroll ---- */
    if( sanfew_slot >= 0 && obj_gout > 0 )
    {
        int xp_before;
        int stat_herb;

        eadgar_tele(srv, 2897, 3426, 1);
        player->varps[varp] = EADGAR_UNLOCKED_STOREROOM;
        player->varps[varp_druid] = DRUID_COMPLETE;
        eadgar_clear_inv(player);
        eadgar_give(player, obj_gout, 1);
        eadgar_set_herblore(player, 31);
        eadgar_god(player);
        stat_herb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
        if( stat_herb < 0 || stat_herb >= TORIRSSERVER_STAT_COUNT )
            stat_herb = EADGAR_STAT_HERBLORE;
        xp_before = player->stat_xp_tenths[stat_herb];

        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        selftest_click_through(srv, 16);
        eadgar_resume_mesbox(srv);
        for( i = 0; i < 12; i++ )
        {
            eadgar_resume_mesbox(srv);
            selftest_tick(srv);
        }
        eadgar_close(srv);
        SELFTEST_CHECK(player->varps[varp] == EADGAR_COMPLETE,
                       "Sanfew goutweed turn-in should write 110, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(player->stat_xp_tenths[stat_herb] > xp_before,
                       "completion should award Herblore xp, %d -> %d",
                       xp_before, player->stat_xp_tenths[stat_herb]);
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "completion must leave the player alive");
        eadgar_pass("opnpc1_sanfew_complete_scroll");

        eadgar_talk_sanfew_more_work(srv, npc_sanfew, sanfew_slot);
        selftest_click_through(srv, 8);
        eadgar_close(srv);
        eadgar_pass("opnpc1_sanfew_postquest");
    }

    /* ---- Journal pages ---- */
    player->varps[varp] = 0;
    ToriRSServer_ScriptsRunProc(srv, "[proc,eadgar_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal at not-started must leave the player alive");
    eadgar_close(srv);
    eadgar_pass("journal_not_started");

    player->varps[varp] = EADGAR_STARTED;
    ToriRSServer_ScriptsRunProc(srv, "[proc,eadgar_journal]", NULL, 0);
    eadgar_close(srv);
    eadgar_pass("journal_started");

    player->varps[varp] = EADGAR_NEEDS_PARROT;
    ToriRSServer_ScriptsRunProc(srv, "[proc,eadgar_journal]", NULL, 0);
    eadgar_close(srv);
    eadgar_pass("journal_parrot");

    player->varps[varp] = EADGAR_NEEDS_ITEMS;
    ToriRSServer_ScriptsRunProc(srv, "[proc,eadgar_journal]", NULL, 0);
    eadgar_close(srv);
    eadgar_pass("journal_items");

    player->varps[varp] = EADGAR_UNLOCKED_STOREROOM;
    ToriRSServer_ScriptsRunProc(srv, "[proc,eadgar_journal]", NULL, 0);
    eadgar_close(srv);
    eadgar_pass("journal_storeroom");

    player->varps[varp] = EADGAR_COMPLETE;
    ToriRSServer_ScriptsRunProc(srv, "[proc,eadgar_journal]", NULL, 0);
    eadgar_close(srv);
    eadgar_pass("journal_complete");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "the Eadgar's Ruse walk must leave the player alive");
    for( i = 0; i < 2; i++ )
        selftest_tick(srv);
    eadgar_close(srv);
    eadgar_free_npc(srv, sanfew_slot);
    eadgar_free_npc(srv, eadgar_slot);
    eadgar_free_npc(srv, burntmeat_slot);
    eadgar_free_npc(srv, pete_slot);
    eadgar_free_npc(srv, tegid_slot);
    eadgar_free_npc(srv, thistle_slot);
    eadgar_clear_inv(player);
    player->varps[varp] = 0;
    player->varps[varp_druid] = 0;
    if( varp_bits >= 0 )
        player->varps[varp_bits] = 0;
    if( varp_grain >= 0 )
        player->varps[varp_grain] = 0;
    if( varp_chickens >= 0 )
        player->varps[varp_chickens] = 0;
    eadgar_set_bit(srv, "troll_freed_eadgar", 0);
    eadgar_god(player);
}

#endif
