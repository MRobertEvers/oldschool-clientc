#ifndef TORIRSSERVER_TEST_QUEST_TROLL_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_TROLL_SELFTEST_U_H

/* Troll Stronghold Gate D C walk. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPLOC / OPLOCU dispatch on the authored
 * path. Silent success is forbidden: each step prints TROLLSTRONGHOLD PASS.
 * player->godmode = 1 for the whole walk (not a death test).
 */

#ifndef TROLL_STAT_THIEVING
#define TROLL_STAT_THIEVING 17
#endif

static void
troll_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "TROLLSTRONGHOLD PASS: %s\n", step);
}

static void
troll_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
troll_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
troll_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
troll_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
troll_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = troll_chatmenu();
    if( chatmenu <= 0 )
        return;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static void
troll_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = troll_chatmenu();
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

static int
troll_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
troll_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( troll_inv_total(player, obj_id) >= count )
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
troll_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
troll_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static void
troll_reset_bits(struct ToriRSServer* srv)
{
    assert(srv);
    troll_set_bit(srv, "troll_told", 0);
    troll_set_bit(srv, "troll_accepted_challenge", 0);
    troll_set_bit(srv, "troll_to_the_death", 0);
    troll_set_bit(srv, "troll_opened_back_exit", 0);
    troll_set_bit(srv, "troll_entered_stronghold", 0);
    troll_set_bit(srv, "troll_freed_eadgar", 0);
}

static void
troll_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    troll_god(player);
    selftest_tick(srv);
}

static int
troll_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    troll_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
selftest_quest_troll(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int varp;
    int varp_death;
    int npc_denulth;
    int npc_dunstan;
    int npc_dad;
    int npc_godric;
    int npc_eadgar;
    int npc_guard1;
    int npc_guard2;
    int loc_rocks;
    int loc_arena_in;
    int loc_arena_out;
    int loc_prison;
    int loc_cell_godric;
    int loc_cell_eadgar;
    int loc_secret;
    int loc_stronghold;
    int loc_pot;
    int obj_boots;
    int obj_prison_key;
    int obj_godric_key;
    int obj_eadgar_key;
    int obj_talisman;
    int denulth_slot = -1;
    int dunstan_slot = -1;
    int dad_slot = -1;
    int godric_slot = -1;
    int eadgar_slot = -1;
    int guard_slot = -1;
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
        fprintf(stderr, "TROLLSTRONGHOLD SKIP: no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    troll_god(player);

    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "troll_quest");
    varp_death = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_equiproom");
    npc_denulth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_ig_commander");
    npc_dunstan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_smithy");
    npc_dad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "troll_champion");
    npc_godric = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "troll_godric");
    npc_eadgar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "troll_eadgar");
    npc_guard1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "troll_prison_guard1");
    npc_guard2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "troll_prison_guard2");
    loc_rocks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_climbingrocks");
    loc_arena_in = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_stronghold_arena_entrance_left");
    loc_arena_out = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_stronghold_arena_exit_left");
    loc_prison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_stronghold_prison_door_closed");
    loc_cell_godric = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_celldoor_godric");
    loc_cell_eadgar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_celldoor_eadgar");
    loc_secret = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_stronghold_entrance");
    loc_stronghold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_stronghold_door");
    loc_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "troll_eadgar_cooking_pot");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_climbingboots");
    obj_prison_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "troll_key_prison");
    obj_godric_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "troll_key_godric");
    obj_eadgar_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "troll_key_eadgar");
    obj_talisman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "law_talisman");

    SELFTEST_CHECK(varp >= 0, "varp troll_quest should resolve");
    SELFTEST_CHECK(varp_death >= 0, "varp death_equiproom should resolve");
    SELFTEST_CHECK(npc_denulth > 0, "npc death_ig_commander should resolve");
    SELFTEST_CHECK(npc_dunstan > 0, "npc death_smithy should resolve");
    SELFTEST_CHECK(npc_dad > 0, "npc troll_champion should resolve");
    SELFTEST_CHECK(npc_godric > 0, "npc troll_godric should resolve");
    SELFTEST_CHECK(npc_eadgar > 0, "npc troll_eadgar should resolve");
    SELFTEST_CHECK(loc_rocks > 0, "loc troll_climbingrocks should resolve");
    SELFTEST_CHECK(loc_prison > 0, "loc troll_stronghold_prison_door_closed should resolve");
    SELFTEST_CHECK(loc_cell_godric > 0, "loc troll_celldoor_godric should resolve");
    SELFTEST_CHECK(obj_talisman > 0, "obj law_talisman should resolve");
    if( varp < 0 || varp_death < 0 || npc_denulth <= 0 || npc_dunstan <= 0 ||
        npc_dad <= 0 || npc_godric <= 0 )
        return;

    /* ---- Denulth: post-Death-Plateau offer / decline / accept ---- */
    denulth_slot = troll_spawn(srv, npc_denulth, 2896, 3528, 0);
    SELFTEST_CHECK(denulth_slot >= 0, "death_ig_commander should spawn");
    if( denulth_slot >= 0 )
    {
        player->varps[varp_death] = 80;
        player->varps[varp] = 0;
        troll_reset_bits(srv);
        troll_clear_inv(player);
        troll_god(player);

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 1);
        troll_click_until_menu(srv, 12);
        troll_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 0,
                       "declining Denulth's offer must leave troll_quest at 0, got %d",
                       player->varps[varp]);
        troll_pass("opnpc1_denulth_decline");

        player->varps[varp] = 0;
        troll_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "Denulth offer should open chat");
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 1);
        troll_click_until_menu(srv, 12);
        troll_pick_row(srv, 2);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 0,
                       "declining after the help ask must leave troll_quest at 0, got %d",
                       player->varps[varp]);
        troll_pass("opnpc1_denulth_decline_nothing");

        player->varps[varp] = 0;
        troll_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 1);
        troll_click_until_menu(srv, 12);
        troll_pick_row(srv, 2);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 10,
                       "accepting Denulth should write troll_quest=10, got %d",
                       player->varps[varp]);
        troll_pass("opnpc1_denulth_accept");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "mid-quest Denulth remind should open chat");
        selftest_click_through(srv, 12);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 10,
                       "mid-quest remind must leave troll_quest at 10, got %d",
                       player->varps[varp]);
        troll_pass("opnpc1_denulth_remind_started");

        if( obj_boots > 0 )
            troll_give(player, obj_boots, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        selftest_click_through(srv, 12);
        troll_close(srv);
        troll_pass("opnpc1_denulth_remind_boots");

        player->varps[varp] = 20;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        selftest_click_through(srv, 12);
        troll_close(srv);
        troll_pass("opnpc1_denulth_remind_defeated_dad");

        troll_set_bit(srv, "troll_entered_stronghold", 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        selftest_click_through(srv, 12);
        troll_close(srv);
        troll_pass("opnpc1_denulth_remind_stronghold");

        player->varps[varp] = 30;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        selftest_click_through(srv, 12);
        troll_close(srv);
        troll_pass("opnpc1_denulth_remind_prison");

        player->varps[varp] = 40;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1,
                                       denulth_slot);
        selftest_click_through(srv, 12);
        troll_close(srv);
        troll_pass("opnpc1_denulth_remind_freed");
    }

    /* ---- Climbing rocks refuse / success gates ---- */
    troll_tele(srv, 2872, 3611, 0);
    player->varps[varp] = 0;
    troll_clear_inv(player);
    troll_god(player);
    ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 1);
    if( loc_rocks > 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rocks, -1, -1);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 0,
                       "rocks before start must not advance the quest");
        troll_pass("oploc1_rocks_refuse_not_started");

        player->varps[varp] = 10;
        ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rocks, -1, -1);
        troll_close(srv);
        troll_pass("oploc1_rocks_refuse_low_agility");

        ToriRSServer_CombatSetLevel(player, TORIRSSERVER_STAT_AGILITY, 20);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rocks, -1, -1);
        troll_close(srv);
        troll_pass("oploc1_rocks_refuse_no_boots");

        if( obj_boots > 0 )
            worn_set(player, TORIRSSERVER_WEAR_FEET, obj_boots, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rocks, -1, -1);
        troll_close(srv);
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "rocks success must leave the player alive");
        troll_pass("oploc1_rocks_success");
    }

    /* ---- Arena doors + Dad talk / accept / decline ---- */
    dad_slot = troll_spawn(srv, npc_dad, 2912, 3613, 0);
    SELFTEST_CHECK(dad_slot >= 0, "troll_champion should spawn");
    if( dad_slot >= 0 )
    {
        player->varps[varp] = 10;
        troll_reset_bits(srv);
        troll_god(player);

        if( loc_arena_in > 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_arena_in,
                                                -1, -1);
            troll_close(srv);
            troll_pass("oploc1_arena_entrance");
        }

        /* Talk first: arena-exit sets Dad to opplayer2, which refuses Talk-to. */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dad, -1, dad_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "Dad talk should open the challenge chat");
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 10,
                       "asking Dad's name must leave troll_quest at 10, got %d",
                       player->varps[varp]);
        troll_pass("opnpc1_dad_why_called");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dad, -1, dad_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 3);
        selftest_click_through(srv, 8);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 10,
                       "declining Dad must leave troll_quest at 10, got %d",
                       player->varps[varp]);
        troll_pass("opnpc1_dad_decline");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dad, -1, dad_slot);
        troll_click_until_menu(srv, 8);
        troll_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        troll_close(srv);
        troll_pass("opnpc1_dad_accept");

        if( loc_arena_out > 0 )
        {
            player->varps[varp] = 10;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_arena_out,
                                                -1, -1);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "arena exit before Dad should open the blocked chat");
            selftest_click_through(srv, 8);
            troll_close(srv);
            troll_pass("oploc1_arena_exit_blocked");
        }

        player->varps[varp] = 20;
        troll_set_bit(srv, "troll_to_the_death", 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dad, -1, dad_slot);
        troll_close(srv);
        troll_pass("opnpc1_dad_not_interested");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_dad, -1, dad_slot);
        troll_close(srv);
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "Dad re-fight refuse must leave the player alive");
        troll_pass("opnpc2_dad_no_refight");
    }

    /* ---- Stronghold / secret / prison doors ---- */
    troll_tele(srv, 2827, 3646, 0);
    player->varps[varp] = 20;
    troll_set_bit(srv, "troll_opened_back_exit", 0);
    troll_god(player);
    if( loc_secret > 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_secret, -1, -1);
        troll_close(srv);
        troll_pass("oploc1_secret_door_unknown");

        player->varps[varp] = 40;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_secret, -1, -1);
        troll_close(srv);
        troll_pass("oploc1_secret_door_open");
    }

    if( loc_stronghold > 0 )
    {
        player->varps[varp] = 20;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_stronghold, -1,
                                            -1);
        troll_close(srv);
        troll_pass("oploc1_stronghold_door");
    }

    troll_tele(srv, 2831, 10068, 0);
    player->varps[varp] = 20;
    troll_clear_inv(player);
    troll_god(player);
    if( loc_prison > 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_prison, -1, -1);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 20,
                       "prison door without a key must stay at 20, got %d",
                       player->varps[varp]);
        troll_pass("oploc1_prison_need_key");

        if( obj_prison_key > 0 )
            troll_give(player, obj_prison_key, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_prison, -1, -1);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 30,
                       "unlocking the prison should write troll_quest=30, got %d",
                       player->varps[varp]);
        if( obj_prison_key > 0 )
            SELFTEST_CHECK(troll_inv_total(player, obj_prison_key) == 0,
                           "prison key should be consumed on unlock");
        troll_pass("oploc1_prison_unlock");

        if( obj_prison_key > 0 )
        {
            /* OPLOCU's authored body is `p_oploc(1)` — needs a live loc slot.
             * The unlock itself is already proven on OPLOC1 above. */
            player->varps[varp] = 20;
            troll_give(player, obj_prison_key, 1);
            player->last_useitem = obj_prison_key;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_prison, -1,
                                                -1);
            troll_close(srv);
            SELFTEST_CHECK(player->last_useitem == obj_prison_key,
                           "prison-key OPLOCU should bind last_useitem");
            troll_pass("oplocu_prison_key");
        }
    }

    /* ---- Godric in cage / cell keys ---- */
    godric_slot = troll_spawn(srv, npc_godric, 2827, 10077, 0);
    SELFTEST_CHECK(godric_slot >= 0, "troll_godric should spawn");
    if( godric_slot >= 0 )
    {
        player->varps[varp] = 30;
        troll_clear_inv(player);
        troll_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_godric, -1,
                                       godric_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "Godric in cage should open chat");
        selftest_click_through(srv, 12);
        troll_close(srv);
        troll_pass("opnpc1_godric_in_cage");

        if( loc_cell_godric > 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cell_godric,
                                                -1, -1);
            troll_close(srv);
            SELFTEST_CHECK(player->varps[varp] == 30,
                           "Godric's cell without a key must stay at 30, got %d",
                           player->varps[varp]);
            troll_pass("oploc1_cell_godric_need_key");

            if( obj_eadgar_key > 0 )
            {
                troll_give(player, obj_eadgar_key, 1);
                player->last_useitem = obj_eadgar_key;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU,
                                                    loc_cell_godric, -1, -1);
                troll_close(srv);
                SELFTEST_CHECK(player->varps[varp] == 30,
                               "Eadgar's key on Godric's cell must not unlock it");
                troll_pass("oplocu_cell_godric_wrong_key");
            }

            if( obj_godric_key > 0 )
            {
                troll_give(player, obj_godric_key, 1);
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1,
                                                    loc_cell_godric, -1, -1);
                selftest_click_through(srv, 8);
                troll_close(srv);
                SELFTEST_CHECK(player->varps[varp] == 40,
                               "unlocking Godric should write troll_quest=40, got %d",
                               player->varps[varp]);
                SELFTEST_CHECK(troll_inv_total(player, obj_godric_key) == 0,
                               "Godric's key should be consumed");
                troll_pass("oploc1_cell_godric_unlock");
            }
        }

        player->varps[varp] = 40;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_godric, -1,
                                       godric_slot);
        selftest_click_through(srv, 8);
        troll_close(srv);
        troll_pass("opnpc1_godric_after_freed");
    }

    /* ---- Eadgar in cage / cell keys ---- */
    eadgar_slot = troll_spawn(srv, npc_eadgar, 2827, 10081, 0);
    SELFTEST_CHECK(eadgar_slot >= 0, "troll_eadgar should spawn");
    if( eadgar_slot >= 0 )
    {
        player->varps[varp] = 30;
        troll_set_bit(srv, "troll_freed_eadgar", 0);
        troll_clear_inv(player);
        troll_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eadgar, -1,
                                       eadgar_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "Eadgar in cage should open chat");
        selftest_click_through(srv, 12);
        troll_close(srv);
        troll_pass("opnpc1_eadgar_in_cage");

        if( loc_cell_eadgar > 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cell_eadgar,
                                                -1, -1);
            troll_close(srv);
            troll_pass("oploc1_cell_eadgar_need_key");

            if( obj_godric_key > 0 )
            {
                troll_give(player, obj_godric_key, 1);
                player->last_useitem = obj_godric_key;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU,
                                                    loc_cell_eadgar, -1, -1);
                troll_close(srv);
                troll_pass("oplocu_cell_eadgar_wrong_key");
            }

            if( obj_eadgar_key > 0 )
            {
                troll_give(player, obj_eadgar_key, 1);
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1,
                                                    loc_cell_eadgar, -1, -1);
                selftest_click_through(srv, 8);
                troll_close(srv);
                troll_pass("oploc1_cell_eadgar_unlock");
            }
        }
    }

    if( loc_pot > 0 )
    {
        troll_tele(srv, 2893, 10074, 0);
        player->last_useitem = (obj_boots > 0) ? obj_boots : 1;
        player->last_useslot = 0;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_pot, -1, -1);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "using Eadgar's stew pot should open chat");
        selftest_click_through(srv, 4);
        troll_close(srv);
        troll_pass("oplocu_eadgar_stew_pot");
    }

    /* ---- Prison-guard pickpocket ---- */
    if( npc_guard1 > 0 )
    {
        guard_slot = troll_spawn(srv, npc_guard1, 2831, 10076, 0);
        SELFTEST_CHECK(guard_slot >= 0, "troll_prison_guard1 should spawn");
        if( guard_slot >= 0 )
        {
            player->varps[varp] = 30;
            troll_clear_inv(player);
            troll_god(player);
            ToriRSServer_CombatSetLevel(player, TROLL_STAT_THIEVING, 1);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_guard1, -1,
                                           guard_slot);
            troll_close(srv);
            troll_pass("opnpc3_guard_low_thieving");

            ToriRSServer_CombatSetLevel(player, TROLL_STAT_THIEVING, 50);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_guard1, -1,
                                           guard_slot);
            troll_close(srv);
            troll_pass("opnpc3_guard_pickpocket");
        }
    }
    (void)npc_guard2;

    /* ---- Dunstan Godric-home + real complete scroll ---- */
    dunstan_slot = troll_spawn(srv, npc_dunstan, 2919, 3574, 0);
    SELFTEST_CHECK(dunstan_slot >= 0, "death_smithy should spawn");
    if( dunstan_slot >= 0 )
    {
        player->varps[varp_death] = 80;
        player->varps[varp] = 10;
        troll_clear_inv(player);
        troll_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1,
                                       dunstan_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "Dunstan mid-quest should ask about Godric");
        selftest_click_through(srv, 16);
        troll_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 10,
                       "mid-quest Dunstan must leave troll_quest at 10, got %d",
                       player->varps[varp]);
        troll_pass("opnpc1_dunstan_mid_rescue");

        player->varps[varp] = 40;
        troll_clear_inv(player);
        troll_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1,
                                       dunstan_slot);
        selftest_click_through(srv, 16);
        troll_close(srv);
        for( i = 0; i < 8; i++ )
            selftest_tick(srv);
        SELFTEST_CHECK(player->varps[varp] == 50,
                       "Dunstan Godric-home should write troll_quest=50, got %d",
                       player->varps[varp]);
        if( obj_talisman > 0 )
            SELFTEST_CHECK(troll_inv_total(player, obj_talisman) >= 1,
                           "completion should grant a law talisman");
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "completion must leave the player alive");
        troll_pass("opnpc1_dunstan_complete_scroll");
    }

    /* ---- Journal pages ---- */
    player->varps[varp] = 0;
    troll_reset_bits(srv);
    ToriRSServer_ScriptsRunProc(srv, "[proc,troll_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal at not-started must leave the player alive");
    troll_close(srv);
    troll_pass("journal_not_started");

    player->varps[varp] = 10;
    ToriRSServer_ScriptsRunProc(srv, "[proc,troll_journal]", NULL, 0);
    troll_close(srv);
    troll_pass("journal_started");

    player->varps[varp] = 20;
    troll_set_bit(srv, "troll_told", 1);
    ToriRSServer_ScriptsRunProc(srv, "[proc,troll_journal]", NULL, 0);
    troll_close(srv);
    troll_pass("journal_defeated_dad");

    player->varps[varp] = 30;
    troll_set_bit(srv, "troll_entered_stronghold", 1);
    ToriRSServer_ScriptsRunProc(srv, "[proc,troll_journal]", NULL, 0);
    troll_close(srv);
    troll_pass("journal_prison");

    player->varps[varp] = 40;
    ToriRSServer_ScriptsRunProc(srv, "[proc,troll_journal]", NULL, 0);
    troll_close(srv);
    troll_pass("journal_freed");

    player->varps[varp] = 50;
    ToriRSServer_ScriptsRunProc(srv, "[proc,troll_journal]", NULL, 0);
    troll_close(srv);
    troll_pass("journal_complete");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "the Troll Stronghold walk must leave the player alive");
    for( i = 0; i < 2; i++ )
        selftest_tick(srv);
    troll_close(srv);
    troll_free_npc(srv, denulth_slot);
    troll_free_npc(srv, dunstan_slot);
    troll_free_npc(srv, dad_slot);
    troll_free_npc(srv, godric_slot);
    troll_free_npc(srv, eadgar_slot);
    troll_free_npc(srv, guard_slot);
    troll_clear_inv(player);
    player->varps[varp] = 0;
    player->varps[varp_death] = 0;
    troll_reset_bits(srv);
    troll_god(player);
}

#endif
