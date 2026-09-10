#ifndef TORIRSSERVER_TEST_QUEST_GOLEM_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_GOLEM_SELFTEST_U_H

/* The Golem Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned npcs
 * cannot leak. Real OPNPC / OPLOC / OPHELD / OPNPCU / OPLOCU / OPNPC3 on
 * the critical path. player->godmode = 1 for the whole walk (no death
 * test). Completion goes through the authored
 * ~quest_complete_rewards(quest_golem, ...). Additive Golem branches
 * only — do not rewrite Shadow of the Storm, Observatory, Construction,
 * or MTA. */

#define GOLEM_NOT_STARTED 0
#define GOLEM_OFFERED 1
#define GOLEM_REPAIRED 2
#define GOLEM_TASKED 3
#define GOLEM_PORTAL_OPEN 6
#define GOLEM_NEED_PROGRAM 7
#define GOLEM_HEAD_OPEN 8
#define GOLEM_COMPLETE 10
#define GOLEM_CLAY_NEEDED 4
#define GOLEM_CRAFT_REQ 20
#define GOLEM_THIEVE_REQ 25
#define GOLEM_REWARD_XP_TENTHS 10000

static void
golem_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "GOLEM PASS: %s\n", step);
}

static void
golem_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
golem_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
golem_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 48 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
golem_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
golem_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = golem_chatmenu();
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
golem_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = golem_chatmenu();
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
golem_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    golem_god(player);
    selftest_tick(srv);
}

static int
golem_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    golem_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
golem_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
golem_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
golem_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( golem_inv_total(player, obj_id) >= count )
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
golem_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
golem_get_bit(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static int
golem_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    golem_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
golem_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
golem_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    golem_talk(srv, npc_type, slot);
    golem_finish(srv);
}

static void
golem_use_on_npc(struct ToriRSServer* srv, int npc_type, int slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    golem_finish(srv);
    player->last_useitem = -1;
}

static void
golem_use_on_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    golem_finish(srv);
    player->last_useitem = -1;
}

static void
golem_use_held(struct ToriRSServer* srv, int held_id, int useitem_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = useitem_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, held_id, -1, -1);
    golem_finish(srv);
    player->last_useitem = -1;
}

static void
golem_reset_state(struct ToriRSServer* srv)
{
    assert(srv);
    golem_set_bit(srv, "golem_a", GOLEM_NOT_STARTED);
    golem_set_bit(srv, "golem_b", 0);
    golem_set_bit(srv, "golem_clay", 0);
    golem_set_bit(srv, "golem_retrieved_statuette", 0);
    golem_set_bit(srv, "golem_seen_underground", 0);
    golem_set_bit(srv, "golem_throne_gems", 0);
    golem_set_bit(srv, "golem_head_open", 0);
    golem_set_bit(srv, "agrith_quest", 0);
    golem_set_bit(srv, "agrith_convinced_golem", 0);
}

static void
selftest_quest_golem(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_craft;
    int stat_thieve;
    int npc_golem;
    int npc_broken;
    int npc_phoenix;
    int npc_elissa;
    int npc_curator;
    int loc_bookcase;
    int loc_statuette;
    int loc_door;
    int loc_door_open;
    int loc_portal;
    int loc_mushrooms;
    int loc_throne;
    int loc_stairs_top;
    int loc_stairs_base;
    int obj_softclay;
    int obj_letter;
    int obj_notes;
    int obj_feather;
    int obj_key;
    int obj_statuette;
    int obj_mushroom;
    int obj_pestle;
    int obj_vial;
    int obj_ink;
    int obj_pen;
    int obj_papyrus;
    int obj_program;
    int obj_golemkey;
    int obj_hammer;
    int obj_chisel;
    int obj_ruby;
    int slot;
    int loc_slot;
    int craft_xp_before;
    int thieve_xp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: thegolem critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer golem selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    golem_god(player);

    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    npc_golem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "golem_golem");
    npc_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "golem_broken_golem");
    npc_phoenix = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "golem_phoenix");
    npc_elissa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "golem_elissa");
    npc_curator = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "curator");
    loc_bookcase = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_bookcase");
    loc_statuette = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_statuettea");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_demon_door");
    loc_door_open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_demon_door_always_open");
    loc_portal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_portal");
    loc_mushrooms = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_black_mushrooms");
    loc_throne = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_throne_withgems");
    loc_stairs_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_insidestairs_top");
    loc_stairs_base = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_insidestairs_base");
    obj_softclay = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "softclay");
    obj_letter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_letter");
    obj_notes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_notes");
    obj_feather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_phoenixfeather");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_statuettekey");
    obj_statuette = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_statuette");
    obj_mushroom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_mushroom");
    obj_pestle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pestle_and_mortar");
    obj_vial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vial_empty");
    obj_ink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_ink");
    obj_pen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_pen");
    obj_papyrus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "papyrus");
    obj_program = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_program");
    obj_golemkey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_golemkey");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_chisel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chisel");
    obj_ruby = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ruby");

    SELFTEST_CHECK(npc_golem > 0 || npc_broken > 0, "npc golem_golem/broken should resolve");
    SELFTEST_CHECK(npc_phoenix > 0, "npc golem_phoenix should resolve");
    SELFTEST_CHECK(npc_elissa > 0, "npc golem_elissa should resolve");
    SELFTEST_CHECK(npc_curator > 0, "npc curator should resolve");
    SELFTEST_CHECK(loc_bookcase >= 0, "loc golem_bookcase should resolve");
    SELFTEST_CHECK(loc_statuette >= 0, "loc golem_statuettea should resolve");
    SELFTEST_CHECK(loc_door >= 0, "loc golem_demon_door should resolve");
    SELFTEST_CHECK(loc_portal >= 0, "loc golem_portal should resolve");
    SELFTEST_CHECK(loc_mushrooms >= 0, "loc golem_black_mushrooms should resolve");
    SELFTEST_CHECK(loc_throne >= 0, "loc golem_throne_withgems should resolve");
    SELFTEST_CHECK(obj_softclay > 0, "obj softclay should resolve");
    SELFTEST_CHECK(obj_letter > 0, "obj golem_letter should resolve");
    SELFTEST_CHECK(obj_notes > 0, "obj golem_notes should resolve");
    SELFTEST_CHECK(obj_feather > 0, "obj golem_phoenixfeather should resolve");
    SELFTEST_CHECK(obj_key > 0, "obj golem_statuettekey should resolve");
    SELFTEST_CHECK(obj_statuette > 0, "obj golem_statuette should resolve");
    SELFTEST_CHECK(obj_program > 0, "obj golem_program should resolve");
    SELFTEST_CHECK(stat_craft >= 0, "stat crafting should resolve");
    SELFTEST_CHECK(stat_thieve >= 0, "stat thieving should resolve");
    if( npc_golem <= 0 && npc_broken > 0 )
        npc_golem = npc_broken;
    if( npc_golem <= 0 )
    {
        fprintf(stderr, "ToriRSServer golem selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    golem_clear_inv(player);
    golem_reset_state(srv);
    golem_god(player);

    /* ---- Offer / skill gate / decline ---- */
    slot = golem_spawn(srv, npc_golem, 3493, 3090, 0);
    SELFTEST_CHECK(slot >= 0, "clay golem should spawn");
    if( slot >= 0 )
    {
        if( stat_craft >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_craft, 1);
        if( stat_thieve >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_thieve, 1);
        golem_talk(srv, npc_golem, slot);
        golem_click_until_menu(srv, 8);
        golem_pick_row(srv, 1);
        golem_finish(srv);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NOT_STARTED,
                       "low-skill leave must not start The Golem, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_low_skill_leave");

        golem_talk(srv, npc_golem, slot);
        golem_click_until_menu(srv, 8);
        golem_pick_row(srv, 2);
        golem_finish(srv);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NOT_STARTED,
                       "low-skill never-mind must not start The Golem, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_low_skill_nevermind");

        if( stat_craft >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_craft, GOLEM_CRAFT_REQ);
        if( stat_thieve >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_thieve, GOLEM_THIEVE_REQ);
        golem_talk(srv, npc_golem, slot);
        golem_click_until_menu(srv, 8);
        golem_pick_row(srv, 2);
        golem_finish(srv);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NOT_STARTED,
                       "decline-conversation must not start The Golem, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_offer_decline");

        golem_talk(srv, npc_golem, slot);
        golem_click_until_menu(srv, 8);
        golem_pick_row(srv, 1);
        golem_finish(srv);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_OFFERED,
                       "repair offer should set golem_a=1, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_offer_repair");

        golem_talk_finish(srv, npc_golem, slot);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_OFFERED,
                       "offered talk must stay at repairs-needed, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_repairs_needed");

        /* Clay on golem at the wrong stage. */
        golem_set_bit(srv, "golem_a", GOLEM_NOT_STARTED);
        if( obj_softclay > 0 )
        {
            golem_clear_inv(player);
            golem_give(player, obj_softclay, 1);
            golem_use_on_npc(srv, npc_golem, slot, obj_softclay);
            SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NOT_STARTED,
                           "clay before offer must not repair");
            golem_pass("opnpcu_clay_no_reason");
        }

        golem_set_bit(srv, "golem_a", GOLEM_OFFERED);
        golem_set_bit(srv, "golem_clay", 0);
        golem_clear_inv(player);
        if( obj_softclay > 0 )
        {
            golem_give(player, obj_softclay, GOLEM_CLAY_NEEDED);
            golem_use_on_npc(srv, npc_golem, slot, obj_softclay);
            SELFTEST_CHECK(golem_get_bit(player, "golem_clay") == 1,
                           "first clay should set golem_clay=1, got %d",
                           golem_get_bit(player, "golem_clay"));
            golem_pass("opnpcu_clay_wounds");

            golem_use_on_npc(srv, npc_golem, slot, obj_softclay);
            SELFTEST_CHECK(golem_get_bit(player, "golem_clay") == 2,
                           "second clay should set golem_clay=2, got %d",
                           golem_get_bit(player, "golem_clay"));
            golem_pass("opnpcu_clay_legs");

            golem_use_on_npc(srv, npc_golem, slot, obj_softclay);
            SELFTEST_CHECK(golem_get_bit(player, "golem_clay") == 3,
                           "third clay should set golem_clay=3, got %d",
                           golem_get_bit(player, "golem_clay"));
            golem_pass("opnpcu_clay_nearly");

            golem_use_on_npc(srv, npc_golem, slot, obj_softclay);
            SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_REPAIRED,
                           "fourth clay should repair the golem, got %d",
                           golem_get_bit(player, "golem_a"));
            golem_pass("opnpcu_clay_final");
        }

        golem_talk_finish(srv, npc_golem, slot);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_TASKED,
                       "repaired briefing should task the player, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_task_briefing");

        golem_talk_finish(srv, npc_golem, slot);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_TASKED,
                       "tasked reminder must stay at 3, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_tasked_reminder");
    }

    /* ---- Letter / notes / Elissa / bookcase ---- */
    if( obj_letter > 0 )
    {
        golem_clear_inv(player);
        golem_give(player, obj_letter, 1);
        golem_set_bit(srv, "golem_b", 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_letter, -1, -1);
        golem_finish(srv);
        SELFTEST_CHECK(golem_get_bit(player, "golem_b") == 1,
                       "reading the letter should set golem_b=1, got %d",
                       golem_get_bit(player, "golem_b"));
        golem_pass("opheld1_golem_letter");
    }

    if( npc_elissa > 0 )
    {
        int elissa = golem_spawn(srv, npc_elissa, 3373, 3338, 0);

        SELFTEST_CHECK(elissa >= 0, "Elissa should spawn");
        if( elissa >= 0 )
        {
            golem_set_bit(srv, "golem_b", 1);
            golem_talk_finish(srv, npc_elissa, elissa);
            SELFTEST_CHECK(golem_get_bit(player, "golem_b") == 1,
                           "Elissa talk must stay at letter-read");
            golem_pass("opnpc1_elissa_varmen");
            golem_free_npc(srv, elissa);
        }
    }

    if( loc_bookcase >= 0 )
    {
        loc_slot = golem_place_loc(srv, loc_bookcase, 3378, 3334, 0);
        SELFTEST_CHECK(loc_slot >= 0, "golem_bookcase should place");
        if( loc_slot >= 0 )
        {
            golem_clear_inv(player);
            golem_set_bit(srv, "golem_b", 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bookcase, -1, loc_slot);
            golem_finish(srv);
            SELFTEST_CHECK(obj_notes <= 0 || golem_inv_total(player, obj_notes) > 0,
                           "bookcase search should grant golem_notes");
            golem_pass("oploc1_bookcase_find_notes");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bookcase, -1, loc_slot);
            golem_finish(srv);
            SELFTEST_CHECK(obj_notes <= 0 || golem_inv_total(player, obj_notes) == 1,
                           "second bookcase search must not duplicate notes");
            golem_pass("oploc1_bookcase_already");
        }
    }

    if( obj_notes > 0 )
    {
        if( golem_inv_total(player, obj_notes) < 1 )
            golem_give(player, obj_notes, 1);
        golem_set_bit(srv, "golem_b", 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_notes, -1, -1);
        golem_finish(srv);
        SELFTEST_CHECK(golem_get_bit(player, "golem_b") == 2,
                       "reading the notes should set golem_b=2, got %d",
                       golem_get_bit(player, "golem_b"));
        golem_pass("opheld1_golem_notes");
    }

    /* ---- Phoenix feather ---- */
    if( npc_phoenix > 0 )
    {
        int phoenix = golem_spawn(srv, npc_phoenix, 3417, 3153, 0);

        SELFTEST_CHECK(phoenix >= 0, "desert phoenix should spawn");
        if( phoenix >= 0 )
        {
            golem_clear_inv(player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_phoenix, -1, phoenix);
            golem_finish(srv);
            SELFTEST_CHECK(obj_feather <= 0 || golem_inv_total(player, obj_feather) > 0,
                           "Grab-feather should grant golem_phoenixfeather");
            golem_pass("opnpc3_phoenix_grab");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_phoenix, -1, phoenix);
            golem_finish(srv);
            SELFTEST_CHECK(obj_feather <= 0 || golem_inv_total(player, obj_feather) == 1,
                           "second Grab-feather must not duplicate");
            golem_pass("opnpc3_phoenix_already");
            golem_free_npc(srv, phoenix);
        }
    }

    /* ---- Museum key / statuette ---- */
    if( npc_curator > 0 && obj_key > 0 )
    {
        int curator = golem_spawn(srv, npc_curator, 3256, 3449, 0);

        SELFTEST_CHECK(curator >= 0, "curator should spawn");
        if( curator >= 0 )
        {
            golem_clear_inv(player);
            golem_set_bit(srv, "golem_a", GOLEM_NOT_STARTED);
            golem_set_bit(srv, "golem_retrieved_statuette", 0);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_curator, -1, curator);
            golem_finish(srv);
            SELFTEST_CHECK(golem_inv_total(player, obj_key) == 0,
                           "pickpocket before tasked must not grant the key");
            golem_pass("opnpc3_curator_too_early");

            golem_set_bit(srv, "golem_a", GOLEM_TASKED);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_curator, -1, curator);
            golem_finish(srv);
            SELFTEST_CHECK(golem_inv_total(player, obj_key) > 0,
                           "tasked pickpocket should grant golem_statuettekey");
            golem_pass("opnpc3_curator_steal_key");
            golem_free_npc(srv, curator);
        }
    }

    if( obj_key > 0 && obj_statuette > 0 )
    {
        golem_clear_inv(player);
        golem_give(player, obj_key, 1);
        golem_set_bit(srv, "golem_a", GOLEM_NOT_STARTED);
        golem_set_bit(srv, "golem_retrieved_statuette", 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_key, -1, -1);
        golem_finish(srv);
        SELFTEST_CHECK(golem_inv_total(player, obj_statuette) == 0,
                       "key before tasked must not grant the statuette");
        golem_pass("opheldu_key_no_reason");

        golem_set_bit(srv, "golem_a", GOLEM_TASKED);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_key, -1, -1);
        golem_finish(srv);
        SELFTEST_CHECK(golem_inv_total(player, obj_statuette) > 0,
                       "key should open the case and grant the statuette");
        SELFTEST_CHECK(golem_get_bit(player, "golem_retrieved_statuette") == 1,
                       "key should set golem_retrieved_statuette");
        golem_pass("opheldu_key_open_case");

        golem_give(player, obj_key, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_key, -1, -1);
        golem_finish(srv);
        SELFTEST_CHECK(golem_inv_total(player, obj_statuette) == 1,
                       "second key use must not duplicate the statuette");
        golem_pass("opheldu_key_already");
    }

    /* ---- Place statuette / portal ---- */
    if( loc_statuette >= 0 && obj_statuette > 0 )
    {
        loc_slot = golem_place_loc(srv, loc_statuette, 3493, 3088, 0);
        SELFTEST_CHECK(loc_slot >= 0, "golem_statuettea should place");
        if( loc_slot >= 0 )
        {
            golem_clear_inv(player);
            golem_give(player, obj_statuette, 1);
            golem_set_bit(srv, "golem_a", GOLEM_NOT_STARTED);
            golem_use_on_loc(srv, loc_statuette, loc_slot, obj_statuette);
            SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NOT_STARTED,
                           "placing the statuette before tasked must refuse");
            golem_pass("oplocu_place_no_reason");

            if( obj_softclay > 0 )
            {
                golem_set_bit(srv, "golem_a", GOLEM_TASKED);
                golem_use_on_loc(srv, loc_statuette, loc_slot, obj_softclay);
                SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_TASKED,
                               "wrong item on alcove must not open the portal");
                golem_pass("oplocu_place_wrong_item");
            }

            golem_set_bit(srv, "golem_a", GOLEM_TASKED);
            golem_use_on_loc(srv, loc_statuette, loc_slot, obj_statuette);
            SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_PORTAL_OPEN,
                           "placing the statuette should open the portal, got %d",
                           golem_get_bit(player, "golem_a"));
            golem_pass("oplocu_place_statuette");
        }
    }

    if( loc_door >= 0 )
    {
        loc_slot = golem_place_loc(srv, loc_door, 3490, 3088, 0);
        if( loc_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, loc_slot);
            golem_finish(srv);
            golem_pass("oploc1_demon_door_closed");
        }
    }

    if( loc_portal >= 0 )
    {
        loc_slot = golem_place_loc(srv, loc_portal, 3491, 3088, 0);
        if( loc_slot >= 0 )
        {
            golem_set_bit(srv, "golem_a", GOLEM_TASKED);
            golem_set_bit(srv, "golem_seen_underground", 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_portal, -1, loc_slot);
            golem_finish(srv);
            SELFTEST_CHECK(golem_get_bit(player, "golem_seen_underground") == 0,
                           "closed portal must not mark the skeleton seen");
            golem_pass("oploc1_portal_closed");

            golem_set_bit(srv, "golem_a", GOLEM_PORTAL_OPEN);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_portal, -1, loc_slot);
            golem_finish(srv);
            SELFTEST_CHECK(golem_get_bit(player, "golem_seen_underground") == 1,
                           "first portal enter should show the skeleton");
            golem_pass("oploc1_portal_skeleton");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_portal, -1, loc_slot);
            golem_finish(srv);
            SELFTEST_CHECK(golem_get_bit(player, "golem_seen_underground") == 1,
                           "second portal enter must keep seen=1");
            golem_pass("oploc1_portal_again");
        }
    }

    if( loc_door_open >= 0 )
    {
        loc_slot = golem_place_loc(srv, loc_door_open, 3492, 3088, 0);
        if( loc_slot >= 0 )
        {
            golem_set_bit(srv, "golem_seen_underground", 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door_open, -1, loc_slot);
            golem_finish(srv);
            SELFTEST_CHECK(golem_get_bit(player, "golem_seen_underground") == 1,
                           "always-open door should show the skeleton");
            golem_pass("oploc1_demon_door_open");
        }
    }

    if( loc_stairs_top >= 0 )
    {
        loc_slot = golem_place_loc(srv, loc_stairs_top, 3494, 3092, 0);
        if( loc_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_stairs_top, -1, loc_slot);
            golem_finish(srv);
            golem_pass("oploc1_stairs_down");
        }
    }
    if( loc_stairs_base >= 0 )
    {
        loc_slot = golem_place_loc(srv, loc_stairs_base, 3544, 4952, 0);
        if( loc_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_stairs_base, -1, loc_slot);
            golem_finish(srv);
            golem_pass("oploc1_stairs_up");
        }
    }

    /* ---- After seeing the skeleton, the golem will not believe it ---- */
    if( slot >= 0 )
    {
        golem_tele(srv, 3493, 3090, 0);
        golem_set_bit(srv, "golem_a", GOLEM_PORTAL_OPEN);
        golem_set_bit(srv, "golem_seen_underground", 1);
        golem_talk_finish(srv, npc_golem, slot);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NEED_PROGRAM,
                       "telling the golem the demon is dead should demand a rewrite, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_demon_dead");

        golem_talk_finish(srv, npc_golem, slot);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NEED_PROGRAM,
                       "need-program reminder must stay at 7, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpc1_golem_wont_believe");
    }

    /* ---- Mushrooms / ink / pen / program ---- */
    if( loc_mushrooms >= 0 && obj_mushroom > 0 )
    {
        loc_slot = golem_place_loc(srv, loc_mushrooms, 3490, 3094, 0);
        if( loc_slot >= 0 )
        {
            golem_clear_inv(player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_mushrooms, -1, loc_slot);
            golem_finish(srv);
            SELFTEST_CHECK(golem_inv_total(player, obj_mushroom) > 0,
                           "picking black mushrooms should grant golem_mushroom");
            golem_pass("oploc1_mushrooms_pick");
        }
    }

    if( obj_mushroom > 0 && obj_pestle > 0 )
    {
        golem_clear_inv(player);
        golem_give(player, obj_mushroom, 1);
        golem_give(player, obj_pestle, 1);
        golem_use_held(srv, obj_mushroom, obj_pestle);
        SELFTEST_CHECK(obj_ink <= 0 || golem_inv_total(player, obj_ink) == 0,
                       "crushing without a vial must not make ink");
        golem_pass("opheldu_crush_need_vial");

        if( obj_vial > 0 && obj_ink > 0 )
        {
            golem_give(player, obj_vial, 1);
            golem_use_held(srv, obj_mushroom, obj_pestle);
            SELFTEST_CHECK(golem_inv_total(player, obj_ink) > 0,
                           "crushing the mushroom should grant golem_ink");
            golem_pass("opheldu_crush_ink");
        }
    }

    if( obj_feather > 0 && obj_ink > 0 && obj_pen > 0 )
    {
        golem_clear_inv(player);
        golem_give(player, obj_feather, 1);
        golem_give(player, obj_ink, 1);
        golem_use_held(srv, obj_feather, obj_ink);
        SELFTEST_CHECK(golem_inv_total(player, obj_pen) > 0,
                       "dipping the feather should grant golem_pen");
        golem_pass("opheldu_dip_feather");

        golem_clear_inv(player);
        golem_give(player, obj_feather, 1);
        golem_give(player, obj_ink, 1);
        golem_use_held(srv, obj_ink, obj_feather);
        SELFTEST_CHECK(golem_inv_total(player, obj_pen) > 0,
                       "ink-on-feather should also grant golem_pen");
        golem_pass("opheldu_dip_ink_on_feather");
    }

    if( obj_pen > 0 && obj_papyrus > 0 && obj_program > 0 )
    {
        golem_clear_inv(player);
        golem_give(player, obj_pen, 1);
        golem_give(player, obj_papyrus, 1);
        golem_set_bit(srv, "golem_b", 1);
        golem_use_held(srv, obj_pen, obj_papyrus);
        SELFTEST_CHECK(golem_inv_total(player, obj_program) == 0,
                       "writing without notes must refuse");
        golem_pass("opheldu_write_no_notes");

        golem_set_bit(srv, "golem_b", 2);
        golem_use_held(srv, obj_pen, obj_papyrus);
        SELFTEST_CHECK(golem_inv_total(player, obj_program) > 0,
                       "writing with notes should grant golem_program");
        golem_pass("opheldu_write_program");

        golem_clear_inv(player);
        golem_give(player, obj_pen, 1);
        golem_give(player, obj_papyrus, 1);
        golem_use_held(srv, obj_papyrus, obj_pen);
        SELFTEST_CHECK(golem_inv_total(player, obj_program) > 0,
                       "papyrus-on-pen should also grant golem_program");
        golem_pass("opheldu_write_papyrus_on_pen");
    }

    /* ---- Throne gems / golem key ---- */
    if( loc_throne >= 0 && obj_hammer > 0 && obj_golemkey > 0 )
    {
        loc_slot = golem_place_loc(srv, loc_throne, 3544, 4954, 0);
        if( loc_slot >= 0 )
        {
            golem_clear_inv(player);
            golem_set_bit(srv, "golem_throne_gems", 0);
            if( obj_softclay > 0 )
            {
                golem_use_on_loc(srv, loc_throne, loc_slot, obj_softclay);
                SELFTEST_CHECK(golem_get_bit(player, "golem_throne_gems") == 0,
                               "wrong item on the throne must not take gems");
                golem_pass("oplocu_throne_wrong_item");
            }

            golem_use_on_loc(srv, loc_throne, loc_slot, obj_hammer);
            SELFTEST_CHECK(golem_get_bit(player, "golem_throne_gems") == 1,
                           "hammer on the throne should take the gems");
            SELFTEST_CHECK(golem_inv_total(player, obj_golemkey) > 0,
                           "throne gems should include golem_golemkey");
            SELFTEST_CHECK(obj_ruby <= 0 || golem_inv_total(player, obj_ruby) > 0,
                           "throne gems should include a ruby");
            golem_pass("oplocu_throne_gems");

            golem_use_on_loc(srv, loc_throne, loc_slot, obj_chisel > 0 ? obj_chisel : obj_hammer);
            SELFTEST_CHECK(golem_inv_total(player, obj_golemkey) == 1,
                           "second throne pry must not duplicate the key");
            golem_pass("oplocu_throne_already");
        }
    }

    /* ---- Reprogram / authored complete ---- */
    if( slot >= 0 && obj_golemkey > 0 && obj_program > 0 )
    {
        golem_tele(srv, 3493, 3090, 0);
        golem_clear_inv(player);
        golem_give(player, obj_golemkey, 1);
        golem_give(player, obj_program, 1);
        golem_set_bit(srv, "golem_a", GOLEM_TASKED);
        golem_set_bit(srv, "golem_b", 2);
        golem_set_bit(srv, "golem_head_open", 0);
        golem_use_on_npc(srv, npc_golem, slot, obj_golemkey);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_TASKED,
                       "key before need-program must refuse");
        golem_pass("opnpcu_key_too_early");

        golem_set_bit(srv, "golem_a", GOLEM_NEED_PROGRAM);
        golem_set_bit(srv, "golem_b", 1);
        golem_use_on_npc(srv, npc_golem, slot, obj_golemkey);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NEED_PROGRAM,
                       "key without notes must refuse");
        golem_pass("opnpcu_key_no_notes");

        golem_set_bit(srv, "golem_b", 2);
        golem_use_on_npc(srv, npc_golem, slot, obj_golemkey);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_HEAD_OPEN,
                       "key should hinge the skull open, got %d",
                       golem_get_bit(player, "golem_a"));
        golem_pass("opnpcu_key_open_head");

        golem_set_bit(srv, "golem_a", GOLEM_NEED_PROGRAM);
        golem_use_on_npc(srv, npc_golem, slot, obj_program);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_NEED_PROGRAM,
                       "program on a closed head must refuse");
        golem_pass("opnpcu_program_head_closed");

        golem_set_bit(srv, "golem_a", GOLEM_HEAD_OPEN);
        golem_set_bit(srv, "golem_head_open", 1);
        if( golem_inv_total(player, obj_program) < 1 )
            golem_give(player, obj_program, 1);
        if( stat_craft >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_craft, GOLEM_CRAFT_REQ);
        if( stat_thieve >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_thieve, GOLEM_THIEVE_REQ);
        craft_xp_before = stat_craft >= 0 ? player->stat_xp_tenths[stat_craft] : 0;
        thieve_xp_before = stat_thieve >= 0 ? player->stat_xp_tenths[stat_thieve] : 0;
        golem_use_on_npc(srv, npc_golem, slot, obj_program);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_COMPLETE,
                       "inserting the program should complete The Golem, got %d",
                       golem_get_bit(player, "golem_a"));
        SELFTEST_CHECK(stat_craft < 0 ||
                           player->stat_xp_tenths[stat_craft] >= craft_xp_before + GOLEM_REWARD_XP_TENTHS,
                       "complete should award 1000 Crafting XP");
        SELFTEST_CHECK(stat_thieve < 0 ||
                           player->stat_xp_tenths[stat_thieve] >= thieve_xp_before + GOLEM_REWARD_XP_TENTHS,
                       "complete should award 1000 Thieving XP");
        golem_pass("opnpcu_program_complete");

        golem_talk_finish(srv, npc_golem, slot);
        SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_COMPLETE,
                       "post-complete talk must stay at endstate 10");
        golem_pass("opnpc1_golem_post_complete");
    }

    /* ---- Journals at every authored arm ---- */
    golem_set_bit(srv, "golem_a", GOLEM_NOT_STARTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,golem_journal]", NULL, 0);
    golem_finish(srv);
    golem_pass("proc_golem_journal_not_started");

    golem_set_bit(srv, "golem_a", GOLEM_OFFERED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,golem_journal]", NULL, 0);
    golem_finish(srv);
    golem_pass("proc_golem_journal_offered");

    golem_set_bit(srv, "golem_a", GOLEM_REPAIRED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,golem_journal]", NULL, 0);
    golem_finish(srv);
    golem_pass("proc_golem_journal_repaired");

    golem_set_bit(srv, "golem_a", GOLEM_TASKED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,golem_journal]", NULL, 0);
    golem_finish(srv);
    golem_pass("proc_golem_journal_tasked");

    golem_set_bit(srv, "golem_a", GOLEM_PORTAL_OPEN);
    ToriRSServer_ScriptsRunProc(srv, "[proc,golem_journal]", NULL, 0);
    golem_finish(srv);
    golem_pass("proc_golem_journal_portal");

    golem_set_bit(srv, "golem_a", GOLEM_NEED_PROGRAM);
    ToriRSServer_ScriptsRunProc(srv, "[proc,golem_journal]", NULL, 0);
    golem_finish(srv);
    golem_pass("proc_golem_journal_need_program");

    golem_set_bit(srv, "golem_a", GOLEM_HEAD_OPEN);
    ToriRSServer_ScriptsRunProc(srv, "[proc,golem_journal]", NULL, 0);
    golem_finish(srv);
    golem_pass("proc_golem_journal_head_open");

    golem_set_bit(srv, "golem_a", GOLEM_COMPLETE);
    ToriRSServer_ScriptsRunProc(srv, "[proc,golem_journal]", NULL, 0);
    golem_finish(srv);
    golem_pass("proc_golem_journal_complete");

    /* ::complete twice: first sets endstate, second is a no-op. */
    golem_set_bit(srv, "golem_a", GOLEM_TASKED);
    {
        int32_t row = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_golem");
        int32_t first = 0;
        int32_t second = 0;

        if( row > 0 )
        {
            ToriRSServer_ScriptsRunProcInt(srv, "[proc,quest_cheat_complete]", &row, 1, &first);
            SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_COMPLETE,
                           "::complete quest_golem should set endstate 10, got %d",
                           golem_get_bit(player, "golem_a"));
            ToriRSServer_ScriptsRunProcInt(srv, "[proc,quest_cheat_complete]", &row, 1, &second);
            SELFTEST_CHECK(golem_get_bit(player, "golem_a") == GOLEM_COMPLETE,
                           "second ::complete must stay at endstate 10");
            golem_pass("quest_cheat_complete_idempotent");
        }
    }

    golem_free_npc(srv, slot);
    golem_clear_inv(player);
    golem_reset_state(srv);
    golem_god(player);
    ToriRSServer_WorldCloseModal(srv);

    fprintf(stderr, "ToriRSServer golem selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_GOLEM_SELFTEST_U_H */
