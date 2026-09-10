#ifndef TORIRSSERVER_TEST_QUEST_RUMDEAL_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_RUMDEAL_SELFTEST_U_H

/* Rum Deal Gate D C walk. Included from torirs_server_world_selftest.c and
 * invoked immediately before a selftest_reset_world so spawned Pete /
 * Braindeath / Davey / Donnie / spirit / spider / squid cannot leak. Real
 * OPNPC / OPLOC / OPLOCU / OPHELD1 on the authored path. player->godmode = 1
 * for the whole walk (spirit / spider are not death-case tests). Completion
 * goes through Braindeath at return_to_finish -> ~deal_quest_complete.
 * Additive Rum Deal only -- do not rewrite Priest in Peril, Zogre Flesh
 * Eaters, Ghosts Ahoy, Cabin Fever, Great Brain Robbery, Construction, or
 * MTA.
 *
 * ::rumdealrun is a narrated soft-skip and is not used here.
 *
 * Gate: TORIRSSERVER_SELFTEST_RUMDEAL_ONLY=1
 *
 * Blindweed grow is authored at 500 ticks. This walk fires the real
 * [timer,deal_blindweed_grow] after plant (same compression forgettable
 * tale used for kelda). Name leftover_blindweed_grow_timer_compressed.bmp.
 */

#define RUMDEAL_NOT_STARTED 0
#define RUMDEAL_STARTED 1
#define RUMDEAL_GROWING 2
#define RUMDEAL_DELIVER 3
#define RUMDEAL_HOPPER 4
#define RUMDEAL_TOLD_WATER 5
#define RUMDEAL_GET_WATER 6
#define RUMDEAL_TOLD_SLUG 7
#define RUMDEAL_GET_SLUG 8
#define RUMDEAL_TOLD_SPIRIT 9
#define RUMDEAL_KILL_SPIRIT 10
#define RUMDEAL_TOLD_SPIDER 11
#define RUMDEAL_KILL_SPIDER 12
#define RUMDEAL_TOLD_SWILL 13
#define RUMDEAL_GET_SWILL 14
#define RUMDEAL_RETURN 15
#define RUMDEAL_COMPLETE 19

#define RUMDEAL_FARM_UNTOUCHED 0
#define RUMDEAL_FARM_RAKED 3
#define RUMDEAL_FARM_PLANTED 4
#define RUMDEAL_FARM_GROWN 5
#define RUMDEAL_BARREL_FULL 5
#define RUMDEAL_CONTROL_IDLE 0
#define RUMDEAL_CONTROL_SPIN 1
#define RUMDEAL_CONTROL_RUN 2

#define RUMDEAL_ZFE_COMPLETE 14
#define RUMDEAL_PIP_COMPLETE 60
#define RUMDEAL_REQ_FISHING 50
#define RUMDEAL_REQ_PRAYER 47
#define RUMDEAL_REQ_CRAFTING 42
#define RUMDEAL_REQ_SLAYER 42
#define RUMDEAL_REQ_FARMING 40
#define RUMDEAL_REWARD_XP 70000

#define RUMDEAL_PETE_X 3680
#define RUMDEAL_PETE_Z 3537
#define RUMDEAL_BRAIN_X 2144
#define RUMDEAL_BRAIN_Z 5109
#define RUMDEAL_DAVEY_X 2132
#define RUMDEAL_DAVEY_Z 5100
#define RUMDEAL_DONNIE_X 2150
#define RUMDEAL_DONNIE_Z 5078
#define RUMDEAL_CONTROL_X 2144
#define RUMDEAL_CONTROL_Z 5101
#define RUMDEAL_PATCH_X 2144
#define RUMDEAL_PATCH_Z 5090
#define RUMDEAL_HOPPER_X 2145
#define RUMDEAL_HOPPER_Z 5105
#define RUMDEAL_PRESSURE_X 2146
#define RUMDEAL_PRESSURE_Z 5105
#define RUMDEAL_LEVER_X 2148
#define RUMDEAL_LEVER_Z 5105
#define RUMDEAL_GATE_X 2144
#define RUMDEAL_GATE_Z 5120
#define RUMDEAL_LAKE_X 2144
#define RUMDEAL_LAKE_Z 5130
#define RUMDEAL_TAP_X 2136
#define RUMDEAL_TAP_Z 5100
#define RUMDEAL_SQUID_X 2113
#define RUMDEAL_SQUID_Z 5074

static void
rumdeal_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "RUMDEAL PASS: %s\n", step);
}

static void
rumdeal_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
rumdeal_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
rumdeal_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 160 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
rumdeal_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
rumdeal_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = rumdeal_chatmenu();
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
rumdeal_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = rumdeal_chatmenu();
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
rumdeal_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    rumdeal_god(player);
    selftest_tick(srv);
}

static int
rumdeal_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    rumdeal_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
rumdeal_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
rumdeal_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
rumdeal_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( rumdeal_inv_total(player, obj_id) >= count )
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
rumdeal_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, obj_id, 1);
}

static void
rumdeal_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
rumdeal_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
rumdeal_set_varp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        player->varps[varp] = value;
}

static int
rumdeal_get_varp(struct ToriRSServerPlayer* player, const char* name)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp < 0 )
        return 0;
    return player->varps[varp];
}

static void
rumdeal_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
rumdeal_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    rumdeal_talk(srv, npc_type, slot);
    rumdeal_finish(srv);
}

static void
rumdeal_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    rumdeal_talk(srv, npc_type, slot);
    rumdeal_click_until_menu(srv, 16);
    rumdeal_pick_row(srv, row);
    rumdeal_finish(srv);
}

static int
rumdeal_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    rumdeal_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
rumdeal_oploc_finish(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    rumdeal_finish(srv);
}

static void
rumdeal_oploc2_finish(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_id, -1, loc_slot);
    rumdeal_finish(srv);
}

static void
rumdeal_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    rumdeal_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
rumdeal_set_skills(struct ToriRSServerPlayer* player, int fishing, int prayer, int crafting, int slayer, int farming)
{
    int stat;

    assert(player);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fishing");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, fishing);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, prayer);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, crafting);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, slayer);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "farming");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, farming);
}

static void
rumdeal_clear_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    rumdeal_set_bit(srv, "zogre", 0);
    rumdeal_set_varp(player, "priestperil", 0);
    rumdeal_set_skills(player, 1, 1, 1, 1, 1);
}

static void
rumdeal_set_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    rumdeal_set_bit(srv, "zogre", RUMDEAL_ZFE_COMPLETE);
    rumdeal_set_varp(player, "priestperil", RUMDEAL_PIP_COMPLETE);
    rumdeal_set_skills(
        player,
        RUMDEAL_REQ_FISHING,
        RUMDEAL_REQ_PRAYER,
        RUMDEAL_REQ_CRAFTING,
        RUMDEAL_REQ_SLAYER,
        RUMDEAL_REQ_FARMING);
}

static void
rumdeal_reset_quest(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    rumdeal_set_varp(player, "deal_quest", RUMDEAL_NOT_STARTED);
    rumdeal_set_bit(srv, "deal_farming", RUMDEAL_FARM_UNTOUCHED);
    rumdeal_set_bit(srv, "deal_barrel", 0);
    rumdeal_set_bit(srv, "deal_multi_hopper", RUMDEAL_CONTROL_IDLE);
}

static void
rumdeal_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,deal_journal]", NULL, 0);
    rumdeal_finish(srv);
    rumdeal_pass(step);
}

static void
selftest_quest_rumdeal(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_pete;
    int npc_island;
    int npc_brain;
    int npc_davey;
    int npc_donnie;
    int npc_spirit;
    int npc_spider;
    int npc_squid;
    int loc_weeds3;
    int loc_weeds2;
    int loc_weeds1;
    int loc_empty;
    int loc_seed;
    int loc_grown;
    int loc_hopper;
    int loc_pressure;
    int loc_lever;
    int loc_gate;
    int loc_lake;
    int loc_tap;
    int loc_control;
    int obj_seed;
    int obj_weed;
    int obj_rake;
    int obj_dibber;
    int obj_bucket;
    int obj_stagnant;
    int obj_net;
    int obj_slug;
    int obj_wrench;
    int obj_holy;
    int obj_body;
    int obj_swill;
    int obj_coins;
    int obj_pot;
    int stat_fishing;
    int stat_prayer;
    int stat_farming;
    int slot;
    int loc_slot;
    int xp_fish;
    int xp_pray;
    int xp_farm;
    int qp_before;
    int varp_qp;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: rumdeal critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer rumdeal selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    rumdeal_god(player);

    npc_pete = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "deal_pete");
    npc_island = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "deal_island_pete");
    npc_brain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "deal_captian_braindeath");
    npc_davey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "deal_davey");
    npc_donnie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "deal_captian_donnie");
    npc_spirit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "deal_evil_spirit");
    npc_spider = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "deal_fever_spiders1");
    npc_squid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "deal_squid");
    loc_weeds3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_blindweed_weeds3");
    loc_weeds2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_blindweed_weeds2");
    loc_weeds1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_blindweed_weeds1");
    loc_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_blindweed_empty");
    loc_seed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_blindweed_seed");
    loc_grown = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_blindweed_fullygrown");
    loc_hopper = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_hopper");
    loc_pressure = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_pressure");
    loc_lever = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_multi_lever");
    loc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_gate_closed");
    loc_lake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_stagnant");
    loc_tap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_brewvat_tap");
    loc_control = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deal_multicontrol");
    obj_seed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deal_blindweed_seed");
    obj_weed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deal_blindweed");
    obj_rake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rake");
    obj_dibber = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dibber");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
    obj_stagnant = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deal_stagnant_bucket");
    obj_net = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fishbowl_net");
    obj_slug = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deal_slugling");
    obj_wrench = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deal_wrench");
    obj_holy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deal_wrench_blessed");
    obj_body = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deal_spider_body");
    obj_swill = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deal_bucket_swill");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    stat_fishing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fishing");
    stat_prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    stat_farming = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "farming");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "deal_quest") >= 0,
                   "varp deal_quest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "deal_farming") >= 0,
                   "varbit deal_farming should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "deal_barrel") >= 0,
                   "varbit deal_barrel should resolve");
    SELFTEST_CHECK(npc_pete > 0, "npc deal_pete should resolve");
    SELFTEST_CHECK(npc_brain > 0, "npc deal_captian_braindeath should resolve");
    SELFTEST_CHECK(npc_davey > 0, "npc deal_davey should resolve");
    SELFTEST_CHECK(npc_donnie > 0, "npc deal_captian_donnie should resolve");
    SELFTEST_CHECK(obj_seed > 0, "obj deal_blindweed_seed should resolve");
    if( npc_pete <= 0 || npc_brain <= 0 || obj_seed <= 0 )
    {
        fprintf(stderr, "ToriRSServer rumdeal selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    rumdeal_clear_inv(player);
    rumdeal_reset_quest(srv, player);
    rumdeal_clear_prereqs(srv, player);
    rumdeal_god(player);

    rumdeal_journal(srv, "journal_not_started");

    /* ---- Pete refuse-reqs / offer / choice refuse / accept / mid ---- */
    slot = rumdeal_spawn(srv, npc_pete, RUMDEAL_PETE_X, RUMDEAL_PETE_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Pete should spawn");
    if( slot >= 0 )
    {
        rumdeal_talk_pick(srv, npc_pete, slot, 1);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_NOT_STARTED,
                       "missing real gates must not start Rum Deal (take-money)");
        rumdeal_pass("opnpc1_pete_refuse_reqs_take");

        rumdeal_talk_pick(srv, npc_pete, slot, 2);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_NOT_STARTED,
                       "missing real gates must not start Rum Deal (help-free)");
        rumdeal_pass("opnpc1_pete_refuse_reqs_free");

        rumdeal_set_prereqs(srv, player);
        rumdeal_talk_pick(srv, npc_pete, slot, 2);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_STARTED,
                       "accepting Pete (help-free) must write started, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        rumdeal_pass("opnpc1_pete_choice_accept_free");

        rumdeal_set_varp(player, "deal_quest", RUMDEAL_NOT_STARTED);
        rumdeal_clear_inv(player);
        rumdeal_talk_pick(srv, npc_pete, slot, 1);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_STARTED,
                       "accepting Pete (take-money) must write started, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        rumdeal_pass("opnpc1_pete_choice_accept_take");

        rumdeal_talk_finish(srv, npc_pete, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_STARTED,
                       "mid Pete at started must stay started");
        rumdeal_pass("opnpc1_pete_mid_started");

        rumdeal_set_varp(player, "deal_quest", RUMDEAL_GROWING);
        rumdeal_talk_finish(srv, npc_pete, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GROWING,
                       "mid Pete during quest must stay on current state");
        rumdeal_pass("opnpc1_pete_mid_in_progress");
    }

    rumdeal_journal(srv, "journal_started");

    if( npc_island > 0 )
    {
        rumdeal_free_npc(srv, slot);
        slot = rumdeal_spawn(srv, npc_island, RUMDEAL_PETE_X, RUMDEAL_PETE_Z, 0);
        if( slot >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_NOT_STARTED);
            rumdeal_talk_finish(srv, npc_island, slot);
            rumdeal_pass("opnpc1_island_pete_not_started");
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_STARTED);
            rumdeal_talk_finish(srv, npc_island, slot);
            rumdeal_pass("opnpc1_island_pete_in_progress");
        }
    }

    /* ---- Braindeath start / seed grant / inv-full / growing reminders ---- */
    rumdeal_free_npc(srv, slot);
    slot = rumdeal_spawn(srv, npc_brain, RUMDEAL_BRAIN_X, RUMDEAL_BRAIN_Z, 1);
    SELFTEST_CHECK(slot >= 0, "Braindeath should spawn");
    if( slot >= 0 )
    {
        rumdeal_set_prereqs(srv, player);
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_NOT_STARTED);
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_NOT_STARTED,
                       "Braindeath before start must bounce to Pete");
        rumdeal_pass("opnpc1_braindeath_not_started");

        rumdeal_set_varp(player, "deal_quest", RUMDEAL_STARTED);
        if( obj_pot > 0 )
            rumdeal_fill_inv(player, obj_pot);
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_STARTED,
                       "full inventory must not receive the Blindweed seed");
        rumdeal_pass("opnpc1_braindeath_started_inv_full");

        rumdeal_clear_inv(player);
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GROWING,
                       "Braindeath seed grant must write growing, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        SELFTEST_CHECK(obj_seed <= 0 || rumdeal_inv_total(player, obj_seed) >= 1,
                       "Braindeath must hand a Blindweed seed");
        rumdeal_pass("opnpc1_braindeath_started_grant_seed");

        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_growing_not_grown");

        rumdeal_set_bit(srv, "deal_farming", RUMDEAL_FARM_GROWN);
        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_growing_ready_pick");
    }

    rumdeal_journal(srv, "journal_growing_blindweed");

    /* ---- Rake / plant / growing / pick / inv-full ---- */
    rumdeal_set_varp(player, "deal_quest", RUMDEAL_GROWING);
    rumdeal_set_bit(srv, "deal_farming", RUMDEAL_FARM_UNTOUCHED);
    rumdeal_clear_inv(player);
    if( obj_seed > 0 )
        rumdeal_give(player, obj_seed, 1);
    if( loc_weeds3 >= 0 )
    {
        loc_slot = rumdeal_place_loc(srv, loc_weeds3, RUMDEAL_PATCH_X, RUMDEAL_PATCH_Z, 0);
        SELFTEST_CHECK(loc_slot >= 0, "weeds3 patch should place");
        if( loc_slot >= 0 && obj_rake > 0 )
        {
            rumdeal_use_loc(srv, loc_weeds3, loc_slot, obj_rake);
            SELFTEST_CHECK(rumdeal_get_bit(player, "deal_farming") == 1,
                           "first rake must write farming 1, got %d",
                           rumdeal_get_bit(player, "deal_farming"));
            rumdeal_pass("oplocu_rake_weeds3");
        }
    }
    if( loc_weeds2 >= 0 && obj_rake > 0 )
    {
        rumdeal_set_bit(srv, "deal_farming", 1);
        loc_slot = rumdeal_place_loc(srv, loc_weeds2, RUMDEAL_PATCH_X, RUMDEAL_PATCH_Z, 0);
        if( loc_slot >= 0 )
        {
            rumdeal_use_loc(srv, loc_weeds2, loc_slot, obj_rake);
            SELFTEST_CHECK(rumdeal_get_bit(player, "deal_farming") == 2,
                           "second rake must write farming 2, got %d",
                           rumdeal_get_bit(player, "deal_farming"));
            rumdeal_pass("oplocu_rake_weeds2");
        }
    }
    if( loc_weeds1 >= 0 && obj_rake > 0 )
    {
        rumdeal_set_bit(srv, "deal_farming", 2);
        loc_slot = rumdeal_place_loc(srv, loc_weeds1, RUMDEAL_PATCH_X, RUMDEAL_PATCH_Z, 0);
        if( loc_slot >= 0 )
        {
            rumdeal_use_loc(srv, loc_weeds1, loc_slot, obj_rake);
            SELFTEST_CHECK(rumdeal_get_bit(player, "deal_farming") == RUMDEAL_FARM_RAKED,
                           "third rake must write raked, got %d",
                           rumdeal_get_bit(player, "deal_farming"));
            rumdeal_pass("oplocu_rake_weeds1");
        }
    }
    if( loc_empty >= 0 && obj_seed > 0 )
    {
        rumdeal_set_bit(srv, "deal_farming", RUMDEAL_FARM_RAKED);
        rumdeal_clear_inv(player);
        rumdeal_give(player, obj_seed, 1);
        loc_slot = rumdeal_place_loc(srv, loc_empty, RUMDEAL_PATCH_X, RUMDEAL_PATCH_Z, 0);
        if( loc_slot >= 0 )
        {
            rumdeal_use_loc(srv, loc_empty, loc_slot, obj_seed);
            SELFTEST_CHECK(rumdeal_get_bit(player, "deal_farming") == RUMDEAL_FARM_RAKED,
                           "plant without dibber must stay raked");
            rumdeal_pass("oplocu_plant_no_dibber");

            if( obj_dibber > 0 )
                rumdeal_give(player, obj_dibber, 1);
            rumdeal_use_loc(srv, loc_empty, loc_slot, obj_seed);
            SELFTEST_CHECK(rumdeal_get_bit(player, "deal_farming") == RUMDEAL_FARM_PLANTED,
                           "plant with dibber must write planted, got %d",
                           rumdeal_get_bit(player, "deal_farming"));
            rumdeal_pass("oplocu_plant_seed");
        }
    }
    if( loc_seed >= 0 )
    {
        rumdeal_set_bit(srv, "deal_farming", RUMDEAL_FARM_PLANTED);
        loc_slot = rumdeal_place_loc(srv, loc_seed, RUMDEAL_PATCH_X, RUMDEAL_PATCH_Z, 0);
        if( loc_slot >= 0 )
        {
            rumdeal_oploc2_finish(srv, loc_seed, loc_slot);
            SELFTEST_CHECK(rumdeal_get_bit(player, "deal_farming") == RUMDEAL_FARM_PLANTED,
                           "inspect while growing must stay planted");
            rumdeal_pass("oploc2_growing_not_sprouted");
        }
    }
    rumdeal_set_bit(srv, "deal_farming", RUMDEAL_FARM_PLANTED);
    ToriRSServer_ScriptsRunProc(srv, "[timer,deal_blindweed_grow]", NULL, 0);
    rumdeal_finish(srv);
    SELFTEST_CHECK(rumdeal_get_bit(player, "deal_farming") == RUMDEAL_FARM_GROWN,
                   "grow timer must write fullygrown, got %d",
                   rumdeal_get_bit(player, "deal_farming"));
    rumdeal_pass("timer_blindweed_grow_compressed");

    if( loc_grown >= 0 && obj_weed > 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_GROWING);
        rumdeal_set_bit(srv, "deal_farming", RUMDEAL_FARM_GROWN);
        if( obj_pot > 0 )
            rumdeal_fill_inv(player, obj_pot);
        loc_slot = rumdeal_place_loc(srv, loc_grown, RUMDEAL_PATCH_X, RUMDEAL_PATCH_Z, 0);
        if( loc_slot >= 0 )
        {
            rumdeal_oploc_finish(srv, loc_grown, loc_slot);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GROWING,
                           "full inventory must not pick Blindweed");
            rumdeal_pass("oploc1_pick_inv_full");

            rumdeal_clear_inv(player);
            rumdeal_oploc_finish(srv, loc_grown, loc_slot);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_DELIVER,
                           "pick must write deliver, got %d",
                           rumdeal_get_varp(player, "deal_quest"));
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_weed) >= 1,
                           "pick must add Blindweed");
            rumdeal_pass("oploc1_pick_blindweed");
        }
    }

    rumdeal_journal(srv, "journal_deliver_blindweed");

    /* ---- Deliver / hopper add / too-early ---- */
    if( slot >= 0 )
    {
        rumdeal_set_prereqs(srv, player);
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_DELIVER);
        if( obj_weed > 0 )
        {
            rumdeal_clear_inv(player);
            rumdeal_give(player, obj_weed, 1);
        }
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_HOPPER,
                       "deliver talk must write hopper, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        rumdeal_pass("opnpc1_braindeath_deliver");

        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_hopper_reminder");
    }
    rumdeal_journal(srv, "journal_hopper_blindweed");

    if( loc_hopper >= 0 && obj_weed > 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_GROWING);
        rumdeal_clear_inv(player);
        rumdeal_give(player, obj_weed, 1);
        loc_slot = rumdeal_place_loc(srv, loc_hopper, RUMDEAL_HOPPER_X, RUMDEAL_HOPPER_Z, 1);
        if( loc_slot >= 0 )
        {
            rumdeal_use_loc(srv, loc_hopper, loc_slot, obj_weed);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GROWING,
                           "hopper too-early must not eat Blindweed");
            rumdeal_pass("oplocu_hopper_too_early");

            rumdeal_set_varp(player, "deal_quest", RUMDEAL_HOPPER);
            rumdeal_use_loc(srv, loc_hopper, loc_slot, obj_weed);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_TOLD_WATER,
                           "hopper add Blindweed must write told_water, got %d",
                           rumdeal_get_varp(player, "deal_quest"));
            rumdeal_pass("oplocu_hopper_add_blindweed");
        }
    }

    /* ---- Water briefing / gate / fetch / pour ---- */
    if( slot >= 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_TOLD_WATER);
        rumdeal_clear_inv(player);
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GET_WATER,
                       "water briefing must write get_water, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        rumdeal_pass("opnpc1_braindeath_told_water");

        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_water_reminder");
    }
    rumdeal_journal(srv, "journal_get_water");

    if( loc_gate >= 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_HOPPER);
        loc_slot = rumdeal_place_loc(srv, loc_gate, RUMDEAL_GATE_X, RUMDEAL_GATE_Z, 0);
        if( loc_slot >= 0 )
        {
            rumdeal_oploc_finish(srv, loc_gate, loc_slot);
            rumdeal_pass("oploc1_gate_locked");
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_WATER);
            rumdeal_oploc_finish(srv, loc_gate, loc_slot);
            rumdeal_pass("oploc1_gate_open");
        }
    }
    if( loc_lake >= 0 && obj_bucket > 0 && obj_stagnant > 0 )
    {
        rumdeal_clear_inv(player);
        rumdeal_give(player, obj_bucket, 1);
        loc_slot = rumdeal_place_loc(srv, loc_lake, RUMDEAL_LAKE_X, RUMDEAL_LAKE_Z, 0);
        if( loc_slot >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_HOPPER);
            rumdeal_use_loc(srv, loc_lake, loc_slot, obj_bucket);
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_stagnant) == 0,
                           "stagnant too-early must not fill");
            rumdeal_pass("oplocu_stagnant_too_early");

            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_WATER);
            rumdeal_use_loc(srv, loc_lake, loc_slot, obj_bucket);
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_stagnant) >= 1,
                           "stagnant fetch must add deal_stagnant_bucket");
            rumdeal_pass("oplocu_stagnant_fetch");
        }
    }
    if( loc_hopper >= 0 && obj_stagnant > 0 )
    {
        rumdeal_clear_inv(player);
        rumdeal_give(player, obj_stagnant, 1);
        loc_slot = rumdeal_place_loc(srv, loc_hopper, RUMDEAL_HOPPER_X, RUMDEAL_HOPPER_Z, 1);
        if( loc_slot >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_HOPPER);
            rumdeal_use_loc(srv, loc_hopper, loc_slot, obj_stagnant);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_HOPPER,
                           "pour too-early must not advance");
            rumdeal_pass("oplocu_hopper_pour_fail");

            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_WATER);
            rumdeal_use_loc(srv, loc_hopper, loc_slot, obj_stagnant);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_TOLD_SLUG,
                           "pour water must write told_sluglings, got %d",
                           rumdeal_get_varp(player, "deal_quest"));
            rumdeal_pass("oplocu_hopper_pour_water");
        }
    }

    /* ---- Sluglings: catch / each deposit / lever too-early / pull ---- */
    if( slot >= 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_TOLD_SLUG);
        rumdeal_clear_inv(player);
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GET_SLUG,
                       "slugling briefing must write get_sluglings, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        rumdeal_pass("opnpc1_braindeath_told_sluglings");

        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_sluglings_reminder");
    }
    rumdeal_journal(srv, "journal_get_sluglings");

    if( npc_squid > 0 && obj_slug > 0 )
    {
        int squid;

        rumdeal_free_npc(srv, slot);
        squid = rumdeal_spawn(srv, npc_squid, RUMDEAL_SQUID_X, RUMDEAL_SQUID_Z, 0);
        SELFTEST_CHECK(squid >= 0, "slugling spot should spawn");
        if( squid >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_WATER);
            rumdeal_clear_inv(player);
            rumdeal_talk_finish(srv, npc_squid, squid);
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_slug) == 0,
                           "wrong-state fishing must not catch");
            rumdeal_pass("opnpc1_squid_wrong_state");

            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_SLUG);
            rumdeal_talk_finish(srv, npc_squid, squid);
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_slug) == 0,
                           "no-net fishing must not catch");
            rumdeal_pass("opnpc1_squid_no_net");

            if( obj_net > 0 )
                rumdeal_give(player, obj_net, 1);
            rumdeal_talk_finish(srv, npc_squid, squid);
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_slug) >= 1,
                           "netted catch must add a slugling");
            rumdeal_pass("opnpc1_squid_catch");
        }
        rumdeal_free_npc(srv, squid);
        slot = rumdeal_spawn(srv, npc_brain, RUMDEAL_BRAIN_X, RUMDEAL_BRAIN_Z, 1);
    }

    if( loc_pressure >= 0 && obj_slug > 0 )
    {
        int i;

        rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_SLUG);
        rumdeal_set_bit(srv, "deal_barrel", 0);
        rumdeal_clear_inv(player);
        rumdeal_give(player, obj_slug, 6);
        if( obj_net > 0 )
            rumdeal_give(player, obj_net, 1);
        loc_slot = rumdeal_place_loc(srv, loc_pressure, RUMDEAL_PRESSURE_X, RUMDEAL_PRESSURE_Z, 1);
        if( loc_slot >= 0 )
        {
            for( i = 1; i <= RUMDEAL_BARREL_FULL; i++ )
            {
                rumdeal_use_loc(srv, loc_pressure, loc_slot, obj_slug);
                SELFTEST_CHECK(rumdeal_get_bit(player, "deal_barrel") == i,
                               "slugling deposit %d must write barrel %d, got %d",
                               i, i, rumdeal_get_bit(player, "deal_barrel"));
                rumdeal_pass(i == 1   ? "oplocu_barrel_deposit_1"
                             : i == 2 ? "oplocu_barrel_deposit_2"
                             : i == 3 ? "oplocu_barrel_deposit_3"
                             : i == 4 ? "oplocu_barrel_deposit_4"
                                      : "oplocu_barrel_deposit_5");
            }
            rumdeal_use_loc(srv, loc_pressure, loc_slot, obj_slug);
            SELFTEST_CHECK(rumdeal_get_bit(player, "deal_barrel") == RUMDEAL_BARREL_FULL,
                           "already-full barrel must stay at 5");
            rumdeal_pass("oplocu_barrel_already_full");
        }
    }
    if( loc_lever >= 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_SLUG);
        rumdeal_set_bit(srv, "deal_barrel", 4);
        loc_slot = rumdeal_place_loc(srv, loc_lever, RUMDEAL_LEVER_X, RUMDEAL_LEVER_Z, 1);
        if( loc_slot >= 0 )
        {
            rumdeal_oploc_finish(srv, loc_lever, loc_slot);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GET_SLUG,
                           "lever too-early must not advance");
            rumdeal_pass("oploc1_lever_too_early");

            rumdeal_set_bit(srv, "deal_barrel", RUMDEAL_BARREL_FULL);
            rumdeal_oploc_finish(srv, loc_lever, loc_slot);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_TOLD_SPIRIT,
                           "lever pull must write told_spirit, got %d",
                           rumdeal_get_varp(player, "deal_quest"));
            rumdeal_pass("oploc1_lever_pull");
        }
    }

    /* ---- Spirit: briefing / Davey / control / fight / defeat ---- */
    if( slot >= 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_TOLD_SPIRIT);
        rumdeal_clear_inv(player);
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_KILL_SPIRIT,
                       "spirit briefing must write kill_spirit, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        rumdeal_pass("opnpc1_braindeath_told_spirit");

        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_spirit_reminder");
    }
    rumdeal_journal(srv, "journal_kill_spirit");

    if( npc_davey > 0 )
    {
        int davey;

        rumdeal_free_npc(srv, slot);
        davey = rumdeal_spawn(srv, npc_davey, RUMDEAL_DAVEY_X, RUMDEAL_DAVEY_Z, 1);
        SELFTEST_CHECK(davey >= 0, "Davey should spawn");
        if( davey >= 0 )
        {
            rumdeal_set_prereqs(srv, player);
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_SLUG);
            rumdeal_clear_inv(player);
            rumdeal_talk_finish(srv, npc_davey, davey);
            rumdeal_pass("opnpc1_davey_wrong_state");

            rumdeal_set_varp(player, "deal_quest", RUMDEAL_KILL_SPIRIT);
            rumdeal_talk_finish(srv, npc_davey, davey);
            rumdeal_pass("opnpc1_davey_no_wrench");

            if( obj_wrench > 0 )
                rumdeal_give(player, obj_wrench, 1);
            rumdeal_set_skills(player, RUMDEAL_REQ_FISHING, 1, RUMDEAL_REQ_CRAFTING,
                               RUMDEAL_REQ_SLAYER, RUMDEAL_REQ_FARMING);
            rumdeal_talk_finish(srv, npc_davey, davey);
            SELFTEST_CHECK(obj_holy <= 0 || rumdeal_inv_total(player, obj_holy) == 0,
                           "low prayer must not bless the wrench");
            rumdeal_pass("opnpc1_davey_low_prayer");

            rumdeal_set_prereqs(srv, player);
            rumdeal_talk_finish(srv, npc_davey, davey);
            SELFTEST_CHECK(obj_holy <= 0 || rumdeal_inv_total(player, obj_holy) >= 1,
                           "Davey must bless the wrench");
            rumdeal_pass("opnpc1_davey_bless");

            rumdeal_talk_finish(srv, npc_davey, davey);
            rumdeal_pass("opnpc1_davey_already_blessed");
        }
        rumdeal_free_npc(srv, davey);
        slot = rumdeal_spawn(srv, npc_brain, RUMDEAL_BRAIN_X, RUMDEAL_BRAIN_Z, 1);
    }

    if( loc_control >= 0 && obj_holy > 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_KILL_SPIRIT);
        rumdeal_set_bit(srv, "deal_multi_hopper", RUMDEAL_CONTROL_IDLE);
        rumdeal_clear_inv(player);
        rumdeal_give(player, obj_holy, 1);
        loc_slot = rumdeal_place_loc(srv, loc_control, RUMDEAL_CONTROL_X, RUMDEAL_CONTROL_Z, 1);
        if( loc_slot >= 0 )
        {
            rumdeal_use_loc(srv, loc_control, loc_slot, obj_holy);
            SELFTEST_CHECK(rumdeal_get_bit(player, "deal_multi_hopper") == RUMDEAL_CONTROL_SPIN,
                           "wrench on control must write spinning, got %d",
                           rumdeal_get_bit(player, "deal_multi_hopper"));
            rumdeal_pass("oplocu_control_apply");

            rumdeal_use_loc(srv, loc_control, loc_slot, obj_holy);
            rumdeal_pass("oplocu_control_already_spinning");
        }
    }
    if( npc_spirit > 0 )
    {
        int spirit;

        spirit = rumdeal_spawn(srv, npc_spirit, RUMDEAL_CONTROL_X, RUMDEAL_CONTROL_Z, 1);
        if( spirit >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_KILL_SPIRIT);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_spirit, -1, spirit);
            rumdeal_finish(srv);
            rumdeal_pass("opnpc2_spirit_attack");
            ToriRSServer_ScriptsRunProcOnNpc(srv, "[ai_queue3,deal_evil_spirit]", spirit);
            rumdeal_finish(srv);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_TOLD_SPIDER,
                           "spirit death must write told_spider, got %d",
                           rumdeal_get_varp(player, "deal_quest"));
            rumdeal_pass("ai_queue3_spirit_defeat");
            rumdeal_free_npc(srv, spirit);
        }
    }

    /* ---- Spider: briefing / kill / hopper ---- */
    if( slot >= 0 )
    {
        rumdeal_set_prereqs(srv, player);
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_TOLD_SPIDER);
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_KILL_SPIDER,
                       "spider briefing must write kill_spider, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        rumdeal_pass("opnpc1_braindeath_told_spider");

        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_spider_reminder");
    }
    rumdeal_journal(srv, "journal_kill_spider");

    if( npc_spider > 0 )
    {
        int spider;

        spider = rumdeal_spawn(srv, npc_spider, RUMDEAL_BRAIN_X, RUMDEAL_BRAIN_Z - 4, 0);
        if( spider >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_KILL_SPIDER);
            ToriRSServer_ScriptsRunProcOnNpc(srv, "[ai_queue3,deal_fever_spiders1]", spider);
            rumdeal_finish(srv);
            rumdeal_pass("ai_queue3_fever_spider_kill");
            rumdeal_free_npc(srv, spider);
        }
    }
    if( obj_body > 0 )
    {
        rumdeal_clear_inv(player);
        rumdeal_give(player, obj_body, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_body, -1, -1);
        rumdeal_finish(srv);
        rumdeal_pass("opheld1_spider_body");
    }
    if( loc_hopper >= 0 && obj_body > 0 )
    {
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_KILL_SPIDER);
        rumdeal_clear_inv(player);
        rumdeal_give(player, obj_body, 1);
        loc_slot = rumdeal_place_loc(srv, loc_hopper, RUMDEAL_HOPPER_X, RUMDEAL_HOPPER_Z, 1);
        if( loc_slot >= 0 )
        {
            rumdeal_use_loc(srv, loc_hopper, loc_slot, obj_body);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_TOLD_SWILL,
                           "hopper spider must write told_swill, got %d",
                           rumdeal_get_varp(player, "deal_quest"));
            rumdeal_pass("oplocu_hopper_add_spider");
        }
    }

    /* ---- Swill / Donnie / finish / complete ---- */
    if( slot >= 0 )
    {
        rumdeal_set_prereqs(srv, player);
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_TOLD_SWILL);
        rumdeal_clear_inv(player);
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GET_SWILL,
                       "swill briefing must write get_swill, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        rumdeal_pass("opnpc1_braindeath_told_swill");

        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_swill_reminder");
    }
    rumdeal_journal(srv, "journal_get_swill");

    if( loc_tap >= 0 && obj_bucket > 0 && obj_swill > 0 )
    {
        rumdeal_clear_inv(player);
        loc_slot = rumdeal_place_loc(srv, loc_tap, RUMDEAL_TAP_X, RUMDEAL_TAP_Z, 0);
        if( loc_slot >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_SWILL);
            rumdeal_oploc_finish(srv, loc_tap, loc_slot);
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_swill) == 0,
                           "tap without bucket must not fill");
            rumdeal_pass("oploc1_tap_no_bucket");

            rumdeal_set_varp(player, "deal_quest", RUMDEAL_KILL_SPIDER);
            rumdeal_give(player, obj_bucket, 1);
            rumdeal_oploc_finish(srv, loc_tap, loc_slot);
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_swill) == 0,
                           "tap too-early must not fill");
            rumdeal_pass("oploc1_tap_wrong_state");

            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_SWILL);
            rumdeal_oploc_finish(srv, loc_tap, loc_slot);
            SELFTEST_CHECK(rumdeal_inv_total(player, obj_swill) >= 1,
                           "tap must fill unsanitary swill");
            rumdeal_pass("oploc1_tap_fill_swill");
        }
    }

    if( npc_donnie > 0 )
    {
        int donnie;

        rumdeal_free_npc(srv, slot);
        donnie = rumdeal_spawn(srv, npc_donnie, RUMDEAL_DONNIE_X, RUMDEAL_DONNIE_Z, 0);
        SELFTEST_CHECK(donnie >= 0, "Donnie should spawn");
        if( donnie >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_KILL_SPIDER);
            rumdeal_talk_finish(srv, npc_donnie, donnie);
            rumdeal_pass("opnpc1_donnie_too_early");

            rumdeal_set_varp(player, "deal_quest", RUMDEAL_GET_SWILL);
            rumdeal_clear_inv(player);
            rumdeal_talk_finish(srv, npc_donnie, donnie);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_GET_SWILL,
                           "Donnie without swill must not finish");
            rumdeal_pass("opnpc1_donnie_no_swill");

            if( obj_swill > 0 )
                rumdeal_give(player, obj_swill, 1);
            rumdeal_talk_finish(srv, npc_donnie, donnie);
            SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_RETURN,
                           "Donnie hand-in must write return_to_finish, got %d",
                           rumdeal_get_varp(player, "deal_quest"));
            rumdeal_pass("opnpc1_donnie_handin");
        }
        rumdeal_free_npc(srv, donnie);
        slot = rumdeal_spawn(srv, npc_brain, RUMDEAL_BRAIN_X, RUMDEAL_BRAIN_Z, 1);
    }
    rumdeal_journal(srv, "journal_return_to_finish");

    if( slot >= 0 )
    {
        rumdeal_set_prereqs(srv, player);
        rumdeal_set_varp(player, "deal_quest", RUMDEAL_RETURN);
        rumdeal_clear_inv(player);
        if( obj_holy > 0 )
            rumdeal_give(player, obj_holy, 1);
        xp_fish = (stat_fishing >= 0) ? player->stat_xp_tenths[stat_fishing] : 0;
        xp_pray = (stat_prayer >= 0) ? player->stat_xp_tenths[stat_prayer] : 0;
        xp_farm = (stat_farming >= 0) ? player->stat_xp_tenths[stat_farming] : 0;
        varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        rumdeal_talk_finish(srv, npc_brain, slot);
        SELFTEST_CHECK(rumdeal_get_varp(player, "deal_quest") == RUMDEAL_COMPLETE,
                       "finish talk must complete Rum Deal, got %d",
                       rumdeal_get_varp(player, "deal_quest"));
        if( stat_fishing >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_fishing] >= xp_fish + RUMDEAL_REWARD_XP,
                           "complete must advance fishing by 70000 tenths");
        if( stat_prayer >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_prayer] >= xp_pray + RUMDEAL_REWARD_XP,
                           "complete must advance prayer by 70000 tenths");
        if( stat_farming >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_farming] >= xp_farm + RUMDEAL_REWARD_XP,
                           "complete must advance farming by 70000 tenths");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + 2,
                           "complete must award 2 QP");
        rumdeal_pass("opnpc1_braindeath_finish_complete");

        rumdeal_talk_finish(srv, npc_brain, slot);
        rumdeal_pass("opnpc1_braindeath_post_complete");
    }

    rumdeal_journal(srv, "journal_complete");

    if( npc_pete > 0 )
    {
        rumdeal_free_npc(srv, slot);
        slot = rumdeal_spawn(srv, npc_pete, RUMDEAL_PETE_X, RUMDEAL_PETE_Z, 0);
        if( slot >= 0 )
        {
            rumdeal_set_varp(player, "deal_quest", RUMDEAL_COMPLETE);
            rumdeal_talk_finish(srv, npc_pete, slot);
            rumdeal_pass("opnpc1_pete_post_complete");
        }
    }

    /* Disclosed leftovers stay unused. */
    SELFTEST_CHECK(rumdeal_get_bit(player, "deal_squidfish") == 0,
                   "unused deal_squidfish must stay unused");
    SELFTEST_CHECK(rumdeal_get_bit(player, "deal_slug") == 0,
                   "unused deal_slug must stay unused");
    SELFTEST_CHECK(rumdeal_get_bit(player, "deal_thulhu") == 0,
                   "unused deal_thulhu must stay unused");
    rumdeal_pass("leftover_unused_deal_var_bits");

    rumdeal_free_npc(srv, slot);
    (void)obj_coins;

    fprintf(stderr, "ToriRSServer rumdeal selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_RUMDEAL_SELFTEST_U_H */
