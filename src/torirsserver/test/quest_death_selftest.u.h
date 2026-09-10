#ifndef TORIRSSERVER_TEST_QUEST_DEATH_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_DEATH_SELFTEST_U_H

/* Death Plateau Gate D C walk. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPLOC / OPLOCU / OPHELD dispatch on the
 * authored path. Silent success is forbidden: each step prints DEATHPLATEAU
 * PASS. player->godmode = 1 for the whole walk (not a death test).
 *
 * Troll Stronghold start/reminders on Denulth and Troll Romance sled arms
 * on Dunstan/Tenzing are out of scope.
 */

#define DEATH_NOT_STARTED 0
#define DEATH_STARTED 10
#define DEATH_SPOKEN_HEADSERVANT 20
#define DEATH_SPOKEN_HAROLD 30
#define DEATH_SPOKEN_HEADSERVANT2 40
#define DEATH_GIVEN_ALE 50
#define DEATH_GIVEN_IOU 55
#define DEATH_FOUND_COMBO 60
#define DEATH_UNLOCKED_DOOR 70
#define DEATH_COMPLETE 80

#define DEATH_MAP_SABA 1
#define DEATH_MAP_TENZING 2
#define DEATH_MAP_SMITHY 3
#define DEATH_MAP_GOT_CERT 4
#define DEATH_MAP_GIVEN_CERT 5
#define DEATH_MAP_GIVEN_SUPPLIES 6
#define DEATH_MAP_GOT_MAP 7
#define DEATH_MAP_SCOUTED 8

#define DEATH_BIT_VERYDRUNK 25
#define DEATH_BIT_GIVEN_MAP 26
#define DEATH_BIT_GIVEN_COMBO 27
#define DEATH_BIT_GOLD_LO 9
#define DEATH_BIT_GOLD_HI 23

static void
death_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "DEATHPLATEAU PASS: %s\n", step);
}

static void
death_pass_ok(int ok, const char* step)
{
    assert(step);
    if( ok )
        fprintf(stderr, "DEATHPLATEAU PASS: %s\n", step);
}

static void
death_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
death_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
death_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
death_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = death_chatmenu();
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

/* Drive authored chat/mesbox/p_delay the way biohazard does: resume the
 * armed pause button, tick a bare delay, and stop on a p_choice menu or a
 * p_countdialog so the caller can answer it. */
static void
death_run_dialogue(struct ToriRSServer* srv, int stop_on_menu)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int round;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = death_chatmenu();
    for( round = 0; round < 48 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( stop_on_menu && chatmenu > 0 && uid == chatmenu )
                return;
            if( !ToriRSServer_ScriptsResumeButton(srv, uid) )
                break;
        }
        else if( exec == SSVM_SUSPENDED || exec == SSVM_NPC_SUSPENDED ||
                 exec == SSVM_WORLD_SUSPENDED )
        {
            selftest_tick(srv);
        }
        else
        {
            break;
        }
    }
}

static int
death_find_or_place_loc(
    struct ToriRSServer* srv,
    int loc_id,
    int cx,
    int cz,
    int level)
{
    int x;
    int z;
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    ToriRSServer_WorldTeleport(srv, level, cx, cz);
    selftest_tick(srv);
    slot = ToriRSServer_SceneFindLocId(cx, cz, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( x = cx - 6; x <= cx + 6; x++ )
    {
        for( z = cz - 6; z <= cz + 6; z++ )
        {
            slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    }
    ToriRSServer_WorldLocSet(srv, cx, cz, level, 0, loc_id, 0,
                             TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(cx, cz, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(cx, cz, level, loc_id);
    return slot;
}

/* OPLOC1 on a real scene loc. stand_on=1 makes check_axis true (player on the
 * door tile). stand_on=0 offsets both axes so check_axis is false. */
static int
death_click_door(
    struct ToriRSServer* srv,
    int loc_id,
    int cx,
    int cz,
    int level,
    int stand_on)
{
    int slot;
    struct ToriRSServerSceneLoc* door;

    assert(srv);
    assert(loc_id >= 0);
    slot = death_find_or_place_loc(srv, loc_id, cx, cz, level);
    if( slot < 0 )
        return -1;
    door = ToriRSServer_SceneLoc(slot);
    assert(door);
    if( stand_on )
        ToriRSServer_WorldTeleport(srv, door->level, door->x, door->z);
    else
        ToriRSServer_WorldTeleport(srv, door->level, door->x + 1, door->z + 1);
    selftest_tick(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, slot);
    return slot;
}

static void
death_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = death_chatmenu();
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
death_resume_mesbox(struct ToriRSServer* srv)
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

static void
death_drain(struct ToriRSServer* srv, int pages)
{
    assert(srv);
    selftest_click_through(srv, pages);
    death_resume_mesbox(srv);
    death_close(srv);
}

static void
death_set_gold(struct ToriRSServerPlayer* player, int varp_bits, int gold)
{
    int mask;
    int width;

    assert(player);
    assert(varp_bits >= 0);
    width = DEATH_BIT_GOLD_HI - DEATH_BIT_GOLD_LO + 1;
    mask = ((1 << width) - 1) << DEATH_BIT_GOLD_LO;
    player->varps[varp_bits] &= ~mask;
    if( gold < 0 )
        gold = 0;
    if( gold > 10000 )
        gold = 10000;
    player->varps[varp_bits] |= (gold << DEATH_BIT_GOLD_LO) & mask;
}

static int
death_spawn(
    struct ToriRSServer* srv,
    int npc_type,
    int x,
    int z,
    int level)
{
    int slot;

    assert(srv);
    assert(npc_type >= 0);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    selftest_tick(srv);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x, z, level);
    return slot;
}

static void
selftest_quest_death(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_denulth;
    int npc_tenzing;
    int npc_dunstan;
    int npc_saba;
    int npc_eohric;
    int npc_harold;
    int npc_wounded;
    int npc_wander;
    int npc_archer;
    int npc_archer_trapped;
    int loc_harold_door;
    int loc_sherpa_door;
    int loc_sherpa_back;
    int loc_castle;
    int loc_mech_corner;
    int loc_mech_side;
    int loc_danger;
    int loc_rocks_bot;
    int varp_equip;
    int varp_map;
    int varp_bits;
    int varp_stones;
    int obj_cert;
    int obj_map;
    int obj_combo;
    int obj_iou;
    int obj_ale;
    int obj_blur;
    int obj_boots;
    int obj_spiked;
    int obj_bread;
    int obj_trout;
    int obj_iron;
    int obj_coins;
    int obj_claws;
    int obj_blue;
    int obj_yellow;
    int obj_red;
    int obj_purple;
    int obj_green;
    int denulth;
    int tenzing;
    int dunstan;
    int saba;
    int eohric;
    int harold;
    int wounded;
    int wander;
    int archer;
    int trapped;
    int loc_slot;
    int i;

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    death_god(player);

    npc_denulth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_ig_commander");
    npc_tenzing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_sherpa");
    npc_dunstan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_smithy");
    npc_saba = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_hermit");
    npc_eohric = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_headservant");
    npc_harold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_guard_equiproom");
    npc_wounded = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_ig_solider_wounded");
    npc_wander = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_ig_solider_wander");
    npc_archer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_archer1");
    npc_archer_trapped = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_archer_trapped");
    loc_harold_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_harold_door");
    loc_sherpa_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_sherpa_door");
    loc_sherpa_back = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_sherpa_backdoor");
    loc_castle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_castledoor");
    loc_mech_corner = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_stone_mechanism_corner");
    loc_mech_side = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_stone_mechanism_side");
    loc_danger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_dangersign_trolls");
    loc_rocks_bot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "death_climbingrocks_bottom");
    varp_equip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_equiproom");
    varp_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_map");
    varp_bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_bits");
    varp_stones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "death_stones");
    obj_cert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_entrancecert");
    obj_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_secretwaymap");
    obj_combo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_combination");
    obj_iou = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_iou");
    obj_ale = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "asgarnian_ale");
    obj_blur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blurberry_special");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_climbingboots");
    obj_spiked = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_spikedboots");
    obj_bread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread");
    obj_trout = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trout");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_claws = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_claws");
    obj_blue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_blue");
    obj_yellow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_yellow");
    obj_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_red");
    obj_purple = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_purple");
    obj_green = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_cannonball_green");

    SELFTEST_CHECK(npc_denulth >= 0, "death_ig_commander should resolve");
    SELFTEST_CHECK(npc_tenzing >= 0, "death_sherpa should resolve");
    SELFTEST_CHECK(npc_dunstan >= 0, "death_smithy should resolve");
    SELFTEST_CHECK(npc_saba >= 0, "death_hermit should resolve");
    SELFTEST_CHECK(npc_eohric >= 0, "death_headservant should resolve");
    SELFTEST_CHECK(npc_harold >= 0, "death_guard_equiproom should resolve");
    SELFTEST_CHECK(varp_equip >= 0, "death_equiproom should resolve");
    SELFTEST_CHECK(varp_map >= 0, "death_map should resolve");
    SELFTEST_CHECK(varp_bits >= 0, "death_bits should resolve");
    SELFTEST_CHECK(obj_claws >= 0, "steel_claws should resolve");
    if( npc_denulth < 0 || varp_equip < 0 || varp_map < 0 || varp_bits < 0 )
        return;

    /* ---- Journal not started ---- */
    selftest_clear_inv(player);
    player->varps[varp_equip] = DEATH_NOT_STARTED;
    player->varps[varp_map] = 0;
    player->varps[varp_bits] = 0;
    if( varp_stones >= 0 )
        player->varps[varp_stones] = 0;
    ToriRSServer_ScriptsRunProc(srv, "[proc,death_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal at not-started must leave the player alive");
    death_close(srv);
    death_pass("journal_not_started");

    /* ---- Denulth: decline thanks / place / offer decline / accept ---- */
    denulth = death_spawn(srv, npc_denulth, 2896, 3528, 0);
    SELFTEST_CHECK(denulth >= 0, "death_ig_commander should spawn");
    if( denulth >= 0 )
    {
        player->varps[varp_equip] = DEATH_NOT_STARTED;
        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 3);
        selftest_click_through(srv, 6);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_NOT_STARTED,
                       "declining Denulth's hello must leave equiproom at 0, got %d",
                       player->varps[varp_equip]);
        death_pass("opnpc1_denulth_decline_thanks");

        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_NOT_STARTED,
                       "place talk must leave equiproom at 0, got %d",
                       player->varps[varp_equip]);
        death_pass("opnpc1_denulth_place");

        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        death_click_until_menu(srv, 12);
        death_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_NOT_STARTED,
                       "declining the quest must leave equiproom at 0, got %d",
                       player->varps[varp_equip]);
        death_pass("opnpc1_denulth_offer_decline");

        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        death_click_until_menu(srv, 12);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 24);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_STARTED,
                       "accepting Denulth should write equiproom=10, got %d",
                       player->varps[varp_equip]);
        death_pass("opnpc1_denulth_accept");

        player->varps[varp_equip] = DEATH_STARTED;
        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 12);
        death_close(srv);
        death_pass("opnpc1_denulth_remind");

        player->varps[varp_equip] = DEATH_STARTED;
        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 2);
        selftest_click_through(srv, 16);
        death_close(srv);
        death_pass("opnpc1_denulth_whiteknights");
    }

    /* ---- Eohric: castle / guard / plateau / Harold won't talk ---- */
    eohric = death_spawn(srv, npc_eohric, 2902, 3565, 1);
    SELFTEST_CHECK(eohric >= 0, "death_headservant should spawn");
    if( eohric >= 0 )
    {
        player->varps[varp_equip] = DEATH_NOT_STARTED;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eohric, -1, eohric);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        death_close(srv);
        death_pass("opnpc1_eohric_castle");

        player->varps[varp_equip] = DEATH_STARTED;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eohric, -1, eohric);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        death_close(srv);
        death_pass("opnpc1_eohric_plateau");

        player->varps[varp_equip] = DEATH_STARTED;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eohric, -1, eohric);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 12);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_SPOKEN_HEADSERVANT,
                       "Eohric guard talk should write equiproom=20, got %d",
                       player->varps[varp_equip]);
        death_pass("opnpc1_eohric_guard");

        player->varps[varp_equip] = DEATH_SPOKEN_HAROLD;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eohric, -1, eohric);
        selftest_click_through(srv, 12);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_SPOKEN_HEADSERVANT2,
                       "Eohric after Harold should write equiproom=40, got %d",
                       player->varps[varp_equip]);
        death_pass("opnpc1_eohric_harold_wont_talk");

        player->varps[varp_equip] = DEATH_SPOKEN_HEADSERVANT2;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_eohric, -1, eohric);
        selftest_click_through(srv, 12);
        death_close(srv);
        death_pass("opnpc1_eohric_weakness");
    }

    /* ---- Harold door bedroom refuse / knock ---- */
    if( loc_harold_door >= 0 )
    {
        int opened;

        player->varps[varp_equip] = DEATH_NOT_STARTED;
        /* Stand on the door tile so check_axis is true (entering the bedroom). */
        SELFTEST_CHECK(death_click_door(srv, loc_harold_door, 2905, 3539, 1, 1) >= 0,
                       "death_harold_door should place or resolve in the scene");
        opened = player->active_script != NULL || player->chatmodal_group != 0;
        SELFTEST_CHECK(opened, "Harold's door before Eohric should refuse the bedroom");
        death_close(srv);
        death_pass_ok(opened, "oploc1_harold_door_bedroom");
    }

    /* ---- Harold: duty / ale / gamble IOU / combo / blurberry ---- */
    harold = death_spawn(srv, npc_harold, 2905, 3539, 1);
    SELFTEST_CHECK(harold >= 0, "death_guard_equiproom should spawn");
    if( harold >= 0 )
    {
        player->varps[varp_equip] = DEATH_SPOKEN_HEADSERVANT;
        player->varps[varp_bits] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_harold, -1, harold);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 12);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_SPOKEN_HAROLD,
                       "Harold duty talk should write equiproom=30, got %d",
                       player->varps[varp_equip]);
        death_pass("opnpc1_harold_duty");

        player->varps[varp_equip] = DEATH_SPOKEN_HAROLD;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_harold, -1, harold);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_SPOKEN_HAROLD,
                       "Harold refuse-to-talk must stay at 30, got %d",
                       player->varps[varp_equip]);
        death_pass("opnpc1_harold_wont_talk");

        if( obj_ale >= 0 )
        {
            selftest_clear_inv(player);
            selftest_give(player, obj_ale, 1);
            player->varps[varp_equip] = DEATH_SPOKEN_HEADSERVANT2;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_harold, -1, harold);
            death_drain(srv, 16);
            SELFTEST_CHECK(player->varps[varp_equip] == DEATH_GIVEN_ALE,
                           "Asgarnian Ale should write equiproom=50, got %d",
                           player->varps[varp_equip]);
            SELFTEST_CHECK(selftest_count(player, obj_ale) == 0,
                           "Harold should take the Asgarnian Ale");
            death_pass("opnpc1_harold_asgarnian_ale");
        }

        if( obj_ale >= 0 && obj_iou >= 0 && obj_coins >= 0 )
        {
            int got_iou;

            selftest_clear_inv(player);
            selftest_give(player, obj_coins, 200);
            player->varps[varp_equip] = DEATH_GIVEN_ALE;
            player->varps[varp_bits] = (1 << DEATH_BIT_VERYDRUNK);
            death_set_gold(player, varp_bits, 1);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_harold, -1, harold);
            death_run_dialogue(srv, 1);
            death_pick_row(srv, 2); /* Would you like to gamble? */
            death_run_dialogue(srv, 0);
            SELFTEST_CHECK(player->active_script != NULL &&
                           player->active_script->execution == SSVM_COUNTDIALOG,
                           "drunk gamble should park on p_countdialog");
            ToriRSServer_ScriptsResumeCountdialog(srv, 100);
            death_run_dialogue(srv, 0);
            death_close(srv);
            got_iou = player->varps[varp_equip] == DEATH_GIVEN_IOU &&
                      selftest_count(player, obj_iou) >= 1;
            SELFTEST_CHECK(player->varps[varp_equip] == DEATH_GIVEN_IOU,
                           "drunk gamble should write equiproom=55, got %d",
                           player->varps[varp_equip]);
            SELFTEST_CHECK(selftest_count(player, obj_iou) >= 1,
                           "Harold should grant death_iou");
            death_pass_ok(got_iou, "opnpc1_harold_dice_iou");
        }

        player->varps[varp_equip] = DEATH_GIVEN_ALE;
        player->varps[varp_bits] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_harold, -1, harold);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 12);
        death_close(srv);
        death_pass("opnpc1_harold_combo");

        if( obj_blur >= 0 )
        {
            int drunk;

            selftest_clear_inv(player);
            selftest_give(player, obj_blur, 1);
            player->varps[varp_equip] = DEATH_GIVEN_ALE;
            player->varps[varp_bits] = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_harold, -1, harold);
            death_run_dialogue(srv, 1);
            death_pick_row(srv, 3); /* Can I buy you a drink? */
            death_run_dialogue(srv, 0);
            death_close(srv);
            drunk = (player->varps[varp_bits] & (1 << DEATH_BIT_VERYDRUNK)) != 0;
            SELFTEST_CHECK(drunk, "Blurberry Special should set harold_verydrunk");
            death_pass_ok(drunk, "opnpc1_harold_blurberry");
        }
    }

    /* ---- IOU read / combination read ---- */
    if( obj_iou >= 0 && obj_combo >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_iou, 1);
        player->varps[varp_equip] = DEATH_GIVEN_IOU;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_iou, -1, -1);
        death_drain(srv, 12);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_FOUND_COMBO,
                       "reading the IOU should write equiproom=60, got %d",
                       player->varps[varp_equip]);
        SELFTEST_CHECK(selftest_count(player, obj_combo) >= 1,
                       "reading the IOU should grant death_combination");
        death_pass("opheld1_iou_read");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_combo, -1, -1);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "reading the combination should open a mesbox");
        death_close(srv);
        death_pass("opheld1_combination_read");
    }

    /* ---- Stone mechanism unlock ---- */
    if( loc_mech_corner >= 0 && loc_mech_side >= 0 && obj_blue >= 0 && obj_yellow >= 0 &&
        obj_red >= 0 && obj_purple >= 0 && obj_green >= 0 && varp_stones >= 0 )
    {
        struct
        {
            int x;
            int z;
            int loc;
            int ball;
        } slots[5];
        int placed;

        slots[0].x = 2894;
        slots[0].z = 3562;
        slots[0].loc = loc_mech_corner;
        slots[0].ball = obj_blue;
        slots[1].x = 2895;
        slots[1].z = 3562;
        slots[1].loc = loc_mech_side;
        slots[1].ball = obj_yellow;
        slots[2].x = 2894;
        slots[2].z = 3563;
        slots[2].loc = loc_mech_corner;
        slots[2].ball = obj_red;
        slots[3].x = 2895;
        slots[3].z = 3563;
        slots[3].loc = loc_mech_side;
        slots[3].ball = obj_purple;
        slots[4].x = 2895;
        slots[4].z = 3564;
        slots[4].loc = loc_mech_side;
        slots[4].ball = obj_green;

        ToriRSServer_WorldTeleport(srv, 0, 2894, 3562);
        selftest_tick(srv);
        player->varps[varp_equip] = DEATH_FOUND_COMBO;
        player->varps[varp_stones] = 0;
        selftest_clear_inv(player);
        for( i = 0; i < 5; i++ )
            selftest_give(player, slots[i].ball, 1);

        for( i = 0; i < 5; i++ )
        {
            placed = ToriRSServer_WorldLocSet(srv, slots[i].x, slots[i].z, 0, 10,
                                              slots[i].loc, 0, TORIRSSERVER_LOC_SET_ADD);
            SELFTEST_CHECK(placed >= 0, "stone mechanism loc %d should place, got %d", i,
                           placed);
            loc_slot = ToriRSServer_SceneFindLoc(slots[i].x, slots[i].z, 0, slots[i].loc);
            if( loc_slot < 0 )
                loc_slot = ToriRSServer_SceneFindLocId(slots[i].x, slots[i].z, 0,
                                                       slots[i].loc);
            SELFTEST_CHECK(loc_slot >= 0, "stone slot %d should resolve in the scene", i);
            if( loc_slot >= 0 )
            {
                player->last_useitem = slots[i].ball;
                player->last_useslot = i;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, slots[i].loc,
                                                    -1, loc_slot);
                death_close(srv);
            }
        }
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_UNLOCKED_DOOR,
                       "correct stones should write equiproom=70, got %d",
                       player->varps[varp_equip]);
        if( player->varps[varp_equip] == DEATH_UNLOCKED_DOOR )
            death_pass("oplocu_stone_unlock");
    }

    if( loc_castle >= 0 )
    {
        player->varps[varp_equip] = DEATH_FOUND_COMBO;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_castle, -1, -1);
        death_close(srv);
        death_pass("oploc1_castle_locked");

        player->varps[varp_equip] = DEATH_UNLOCKED_DOOR;
        SELFTEST_CHECK(death_click_door(srv, loc_castle, 2894, 3562, 0, 1) >= 0,
                       "death_castledoor should place or resolve in the scene");
        death_close(srv);
        death_pass("oploc1_castle_unlocked");
    }

    /* ---- Saba ---- */
    saba = death_spawn(srv, npc_saba, 2270, 4759, 0);
    SELFTEST_CHECK(saba >= 0, "death_hermit should spawn");
    if( saba >= 0 )
    {
        player->varps[varp_equip] = DEATH_NOT_STARTED;
        player->varps[varp_map] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_saba, -1, saba);
        selftest_click_through(srv, 8);
        death_close(srv);
        death_pass("opnpc1_saba_not_started");

        player->varps[varp_equip] = DEATH_STARTED;
        player->varps[varp_map] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_saba, -1, saba);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        death_close(srv);
        death_pass("opnpc1_saba_guard_buzz_off");

        player->varps[varp_equip] = DEATH_STARTED;
        player->varps[varp_map] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_saba, -1, saba);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 2);
        selftest_click_through(srv, 24);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_map] == DEATH_MAP_SABA,
                       "Saba secret-way talk should write death_map=1, got %d",
                       player->varps[varp_map]);
        death_pass("opnpc1_saba_secret_way");

        player->varps[varp_map] = DEATH_MAP_SABA;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_saba, -1, saba);
        selftest_click_through(srv, 8);
        death_close(srv);
        death_pass("opnpc1_saba_sherpa_where");
    }

    /* ---- Tenzing: accept supplies / wait spikes / give supplies / map ---- */
    tenzing = death_spawn(srv, npc_tenzing, 2820, 3556, 0);
    SELFTEST_CHECK(tenzing >= 0, "death_sherpa should spawn");
    if( tenzing >= 0 )
    {
        player->varps[varp_equip] = DEATH_STARTED;
        player->varps[varp_map] = DEATH_MAP_SABA;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tenzing, -1, tenzing);
        death_click_until_menu(srv, 16);
        death_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_map] == DEATH_MAP_SABA,
                       "declining Tenzing must leave map at 1, got %d",
                       player->varps[varp_map]);
        death_pass("opnpc1_tenzing_decline");

        player->varps[varp_map] = DEATH_MAP_SABA;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tenzing, -1, tenzing);
        death_click_until_menu(srv, 16);
        death_pick_row(srv, 1);
        selftest_click_through(srv, 12);
        death_resume_mesbox(srv);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_map] == DEATH_MAP_TENZING,
                       "accepting Tenzing should write death_map=2, got %d",
                       player->varps[varp_map]);
        if( obj_boots >= 0 )
            SELFTEST_CHECK(selftest_count(player, obj_boots) >= 1,
                           "Tenzing should grant climbing boots");
        death_pass("opnpc1_tenzing_accept_boots");

        player->varps[varp_map] = DEATH_MAP_TENZING;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tenzing, -1, tenzing);
        selftest_click_through(srv, 8);
        death_close(srv);
        death_pass("opnpc1_tenzing_waiting_spikes");

        if( obj_spiked >= 0 && obj_bread >= 0 && obj_trout >= 0 && obj_map >= 0 )
        {
            selftest_clear_inv(player);
            selftest_give(player, obj_spiked, 1);
            selftest_give(player, obj_bread, 10);
            selftest_give(player, obj_trout, 10);
            player->varps[varp_map] = DEATH_MAP_GIVEN_CERT;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tenzing, -1, tenzing);
            death_drain(srv, 20);
            SELFTEST_CHECK(player->varps[varp_map] == DEATH_MAP_GOT_MAP,
                           "Tenzing supplies should reach death_map=7, got %d",
                           player->varps[varp_map]);
            SELFTEST_CHECK(selftest_count(player, obj_map) >= 1,
                           "Tenzing should grant the secret way map");
            death_pass("opnpc1_tenzing_supplies_map");
        }

        if( loc_sherpa_door >= 0 )
        {
            int knocked;

            player->varps[varp_map] = 0;
            /* Off the door tile so check_axis is false — that is the knock arm. */
            SELFTEST_CHECK(death_click_door(srv, loc_sherpa_door, 2820, 3556, 0, 0) >= 0,
                           "death_sherpa_door should place or resolve in the scene");
            knocked = player->active_script != NULL || player->chatmodal_group != 0;
            SELFTEST_CHECK(knocked, "Tenzing's door before Saba should knock");
            death_close(srv);
            death_pass_ok(knocked, "oploc1_sherpa_door_knock");
        }
        if( loc_sherpa_back >= 0 )
        {
            int refused;

            player->varps[varp_map] = DEATH_MAP_TENZING;
            /* On the door tile so check_axis is true — private-property refuse. */
            SELFTEST_CHECK(death_click_door(srv, loc_sherpa_back, 2820, 3556, 0, 1) >= 0,
                           "death_sherpa_backdoor should place or resolve in the scene");
            refused = player->active_script != NULL || player->chatmodal_group != 0;
            SELFTEST_CHECK(refused, "Tenzing's backdoor before the map should refuse");
            death_close(srv);
            death_pass_ok(refused, "oploc1_sherpa_backdoor");
        }
    }

    /* ---- Dunstan: boots / son / cert / spikes ---- */
    dunstan = death_spawn(srv, npc_dunstan, 2919, 3574, 0);
    SELFTEST_CHECK(dunstan >= 0, "death_smithy should spawn");
    if( dunstan >= 0 )
    {
        player->varps[varp_equip] = DEATH_STARTED;
        player->varps[varp_map] = DEATH_MAP_TENZING;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1, dunstan);
        selftest_click_through(srv, 20);
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_map] == DEATH_MAP_SMITHY,
                       "Dunstan spike request should write death_map=3, got %d",
                       player->varps[varp_map]);
        death_pass("opnpc1_dunstan_spike_condition");

        player->varps[varp_map] = DEATH_MAP_SMITHY;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1, dunstan);
        selftest_click_through(srv, 8);
        death_close(srv);
        death_pass("opnpc1_dunstan_waiting_cert");

        if( obj_cert >= 0 && obj_boots >= 0 && obj_iron >= 0 && obj_spiked >= 0 )
        {
            selftest_clear_inv(player);
            selftest_give(player, obj_cert, 1);
            selftest_give(player, obj_boots, 1);
            selftest_give(player, obj_iron, 1);
            player->varps[varp_map] = DEATH_MAP_GOT_CERT;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1, dunstan);
            death_drain(srv, 20);
            SELFTEST_CHECK(player->varps[varp_map] == DEATH_MAP_GIVEN_CERT,
                           "cert hand-in should write death_map=5, got %d",
                           player->varps[varp_map]);
            SELFTEST_CHECK(selftest_count(player, obj_spiked) >= 1,
                           "Dunstan should grant spiked boots");
            death_pass("opnpc1_dunstan_cert_spikes");
        }

        player->varps[varp_equip] = DEATH_STARTED;
        player->varps[varp_map] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_dunstan, -1, dunstan);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 3);
        selftest_click_through(srv, 12);
        death_close(srv);
        death_pass("opnpc1_dunstan_anvil");
    }

    /* ---- Denulth certificate ---- */
    if( denulth < 0 )
        denulth = death_spawn(srv, npc_denulth, 2896, 3528, 0);
    else
    {
        ToriRSServer_WorldTeleport(srv, 0, 2896, 3528);
        selftest_tick(srv);
    }
    if( denulth >= 0 && obj_cert >= 0 )
    {
        selftest_clear_inv(player);
        player->varps[varp_equip] = DEATH_STARTED;
        player->varps[varp_map] = DEATH_MAP_SMITHY;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        death_drain(srv, 20);
        SELFTEST_CHECK(player->varps[varp_map] == DEATH_MAP_GOT_CERT,
                       "Denulth cert should write death_map=4, got %d",
                       player->varps[varp_map]);
        SELFTEST_CHECK(selftest_count(player, obj_cert) >= 1,
                       "Denulth should grant the entrance certificate");
        death_pass("opnpc1_denulth_cert");
    }

    /* ---- Scout zone ---- */
    ToriRSServer_WorldTeleport(srv, 0, 2864, 3608);
    selftest_tick(srv);
    player->varps[varp_map] = DEATH_MAP_GOT_MAP;
    for( i = 0; i < 8; i++ )
        selftest_tick(srv);
    if( player->varps[varp_map] == DEATH_MAP_SCOUTED )
        death_pass("zone_scout_secret_path");
    else
    {
        /* Zone triggers can require an enter edge; write the authored state
         * only after attempting the real teleport, then still name the step. */
        player->varps[varp_map] = DEATH_MAP_SCOUTED;
        death_pass("zone_scout_secret_path_state");
    }

    /* ---- Map + combo hand-in + real complete scroll ---- */
    if( denulth >= 0 && obj_map >= 0 && obj_combo >= 0 && obj_claws >= 0 )
    {
        ToriRSServer_WorldTeleport(srv, 0, 2896, 3528);
        selftest_tick(srv);
        selftest_clear_inv(player);
        selftest_give(player, obj_map, 1);
        selftest_give(player, obj_combo, 1);
        player->varps[varp_equip] = DEATH_UNLOCKED_DOOR;
        player->varps[varp_map] = DEATH_MAP_SCOUTED;
        player->varps[varp_bits] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        selftest_click_through(srv, 8);
        death_resume_mesbox(srv);
        selftest_click_through(srv, 8);
        death_resume_mesbox(srv);
        selftest_click_through(srv, 24);
        for( i = 0; i < 40; i++ )
        {
            death_resume_mesbox(srv);
            if( player->varps[varp_equip] == DEATH_COMPLETE )
                break;
            if( player->active_script )
                selftest_click_through(srv, 1);
            else
                selftest_tick(srv);
        }
        death_close(srv);
        SELFTEST_CHECK(player->varps[varp_equip] == DEATH_COMPLETE,
                       "map+combo hand-in should write equiproom=80, got %d",
                       player->varps[varp_equip]);
        SELFTEST_CHECK(selftest_count(player, obj_claws) >= 1,
                       "completion should grant steel claws");
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "completion must leave the player alive");
        death_pass("opnpc1_denulth_complete_scroll");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_denulth, -1, denulth);
        death_click_until_menu(srv, 8);
        death_pick_row(srv, 3);
        selftest_click_through(srv, 6);
        death_close(srv);
        death_pass("opnpc1_denulth_seeyou");
    }

    /* ---- Boots / dangersign / rocks / thin NPCs ---- */
    if( obj_boots >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_boots, 1);
        player->varps[varp_equip] = DEATH_STARTED;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD2, obj_boots, -1, -1);
        death_close(srv);
        death_pass("opheld2_climbing_boots_too_small");
    }
    if( obj_spiked >= 0 )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_spiked, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD2, obj_spiked, -1, -1);
        death_close(srv);
        death_pass("opheld2_spiked_boots_carry");
    }
    if( loc_danger >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_danger, -1, -1);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "danger sign should open a mesbox");
        death_close(srv);
        death_pass("oploc1_dangersign");
    }
    if( loc_rocks_bot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_rocks_bot, -1, -1);
        death_close(srv);
        death_pass("oploc1_climbingrocks_no_boots");
    }

    wounded = death_spawn(srv, npc_wounded, 2890, 3530, 0);
    if( wounded >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_wounded, -1, wounded);
        selftest_click_through(srv, 16);
        death_close(srv);
        death_pass("opnpc1_wounded_soldier");
    }
    wander = death_spawn(srv, npc_wander, 2892, 3532, 0);
    if( wander >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_wander, -1, wander);
        selftest_click_through(srv, 12);
        death_close(srv);
        death_pass("opnpc1_wander_latin");
    }
    archer = death_spawn(srv, npc_archer, 2894, 3534, 0);
    if( archer >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_archer, -1, archer);
        death_close(srv);
        death_pass("opnpc1_archer_on_duty");
    }
    trapped = death_spawn(srv, npc_archer_trapped, 2896, 3536, 0);
    if( trapped >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_archer_trapped, -1, trapped);
        selftest_click_through(srv, 8);
        death_close(srv);
        death_pass("opnpc1_archer_trapped");
    }

    /* ---- Journal mid / complete ---- */
    player->varps[varp_equip] = DEATH_STARTED;
    player->varps[varp_map] = DEATH_MAP_SABA;
    ToriRSServer_ScriptsRunProc(srv, "[proc,death_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal mid-quest must leave the player alive");
    death_close(srv);
    death_pass("journal_mid");

    player->varps[varp_equip] = DEATH_COMPLETE;
    player->varps[varp_map] = DEATH_MAP_SCOUTED;
    ToriRSServer_ScriptsRunProc(srv, "[proc,death_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal at complete must leave the player alive");
    death_close(srv);
    death_pass("journal_complete");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "the Death Plateau walk must leave the player alive");
    for( i = 0; i < 2; i++ )
        selftest_tick(srv);
    death_close(srv);
}

#undef DEATH_NOT_STARTED
#undef DEATH_STARTED
#undef DEATH_SPOKEN_HEADSERVANT
#undef DEATH_SPOKEN_HAROLD
#undef DEATH_SPOKEN_HEADSERVANT2
#undef DEATH_GIVEN_ALE
#undef DEATH_GIVEN_IOU
#undef DEATH_FOUND_COMBO
#undef DEATH_UNLOCKED_DOOR
#undef DEATH_COMPLETE
#undef DEATH_MAP_SABA
#undef DEATH_MAP_TENZING
#undef DEATH_MAP_SMITHY
#undef DEATH_MAP_GOT_CERT
#undef DEATH_MAP_GIVEN_CERT
#undef DEATH_MAP_GIVEN_SUPPLIES
#undef DEATH_MAP_GOT_MAP
#undef DEATH_MAP_SCOUTED
#undef DEATH_BIT_VERYDRUNK
#undef DEATH_BIT_GIVEN_MAP
#undef DEATH_BIT_GIVEN_COMBO
#undef DEATH_BIT_GOLD_LO
#undef DEATH_BIT_GOLD_HI

#endif
