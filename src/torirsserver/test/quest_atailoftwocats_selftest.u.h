#ifndef TORIRSSERVER_TEST_QUEST_ATAILOFTWOCATS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ATAILOFTWOCATS_SELFTEST_U_H

/* A Tail of Two Cats Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Unferth / Hild / Bob / Gertrude / Reldo /
 * Sphinx / Apothecary cannot leak. Real OPNPC / OPLOC / OPHELD / OPLOCU /
 * OPNPC2 on the authored path. player->godmode = 1 for the whole walk
 * (no death test). Completion goes through Unferth's authored
 * ~quest_complete_rewards(quest_tailoftwocats, ...).
 *
 * Additive Two Cats only -- do not rewrite Gertrude's Cat, Ratcatchers,
 * Giant Dwarf Reldo, Knight's Sword Reldo, Dragon Slayer II (except the
 * Bob window hook), Construction, or MTA.
 *
 * Leftovers (not verified-modern): Icthlarin's Little Helper start-gate
 * is deferred in the script header and is not a hard refuse. Enchanted
 * amulet / Bob locator interface / travel cutscene / hair growth timer /
 * museum kudos / mouse-toy pounce are not implemented. Plant oplocu
 * requires inv_total(rake) >= 3 (authored quirk). */

#define T2C_UNFERTH_X 2918
#define T2C_UNFERTH_Z 3558
#define T2C_HILD_X 2930
#define T2C_HILD_Z 3568
#define T2C_PATCH_X 2919
#define T2C_PATCH_Z 3562
#define T2C_BED_X 2917
#define T2C_BED_Z 3557
#define T2C_FIRE_X 2919
#define T2C_FIRE_Z 3557
#define T2C_TABLE_X 2921
#define T2C_TABLE_Z 3556
#define T2C_GERTRUDE_X 3151
#define T2C_GERTRUDE_Z 3413
#define T2C_RELDO_X 3211
#define T2C_RELDO_Z 3494
#define T2C_SPHINX_X 3302
#define T2C_SPHINX_Z 2784
#define T2C_APOTH_X 3195
#define T2C_APOTH_Z 3405
#define T2C_BOB_X 2920
#define T2C_BOB_Z 3559

#define T2C_GDWARF_IMCANDO_ASKED 17
#define T2C_GDWARF_RELDO_TOLD 18
#define T2C_ICS_COMPLETE 26
#define T2C_RATCATCH_NOT_STARTED 0

static void
t2c_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "T2C PASS: %s\n", step);
}

static void
t2c_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
t2c_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
t2c_finish(struct ToriRSServer* srv)
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
t2c_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
t2c_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = t2c_chatmenu();
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
t2c_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = t2c_chatmenu();
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
t2c_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    t2c_god(player);
    selftest_tick(srv);
}

static int
t2c_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    t2c_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
t2c_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
t2c_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
t2c_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( t2c_inv_total(player, obj_id) >= count )
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
t2c_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
t2c_get_bit(struct ToriRSServer* srv, const char* name)
{
    int bit;

    assert(srv);
    assert(srv->active_player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(srv->active_player, bit);
}

static int
t2c_ensure_loc(struct ToriRSServer* srv, int x, int z, int loc_id)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    t2c_tele(srv, x, z, 0);
    slot = ToriRSServer_SceneFindLocId(x, z, 0, loc_id);
    if( slot >= 0 )
        return slot;
    ToriRSServer_WorldLocSet(srv, x, z, 0, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, 0, loc_id);
    return slot;
}

static void
t2c_journal(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,twocats_journal]", NULL, 0) != 0,
                   "twocats_journal should run at plateau %d", t2c_get_bit(srv, "twocats_quest"));
    t2c_finish(srv);
}

static void
selftest_quest_atailoftwocats(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_unferth;
    int npc_hild;
    int npc_bob;
    int npc_gertrude;
    int npc_reldo;
    int npc_sphinx;
    int npc_apoth;
    int loc_patch;
    int loc_bed;
    int loc_fire;
    int loc_table;
    int obj_amulet;
    int obj_death;
    int obj_rake;
    int obj_seed;
    int obj_logs;
    int obj_tinder;
    int obj_cake;
    int obj_milk;
    int obj_shears;
    int obj_doc;
    int obj_nurse;
    int obj_toy;
    int obj_lamp;
    int slot_unferth;
    int slot_hild;
    int slot_bob;
    int slot_gertrude;
    int slot_reldo;
    int slot_sphinx;
    int slot_apoth;
    int slot_patch;
    int slot_bed;
    int slot_fire;
    int slot_table;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: A Tail of Two Cats\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    t2c_god(player);
    t2c_clear_inv(player);

    npc_unferth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "twocats_unferth_bald");
    npc_hild = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_woman_indoors1");
    npc_bob = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "death_growncat_black_vis");
    npc_gertrude = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gertrude_post");
    npc_reldo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "reldo_normal");
    npc_sphinx = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_sphinx");
    npc_apoth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "apothecary");
    loc_patch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "twocats_patch");
    loc_bed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "twocats_bed");
    loc_fire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "twocats_fireplace");
    loc_table = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "twocats_table");
    obj_amulet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "twocats_amuletofcatspeak");
    obj_death = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deathrune");
    obj_rake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rake");
    obj_seed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "potato_seed");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_cake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chocolate_cake");
    obj_milk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_milk");
    obj_shears = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "shears");
    obj_doc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "twocats_doctors_hat");
    obj_nurse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "twocats_nurses_hat");
    obj_toy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "twocats_mouse_toy");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");

    SELFTEST_CHECK(npc_unferth > 0, "twocats_unferth_bald should resolve");
    SELFTEST_CHECK(npc_hild > 0, "death_woman_indoors1 should resolve");
    SELFTEST_CHECK(npc_bob > 0, "death_growncat_black_vis should resolve");
    SELFTEST_CHECK(npc_gertrude > 0, "gertrude_post should resolve");
    SELFTEST_CHECK(npc_reldo > 0, "reldo_normal should resolve");
    SELFTEST_CHECK(npc_sphinx > 0, "ics_little_sphinx should resolve");
    SELFTEST_CHECK(npc_apoth > 0, "apothecary should resolve");
    SELFTEST_CHECK(loc_patch > 0, "twocats_patch should resolve");
    SELFTEST_CHECK(loc_bed > 0, "twocats_bed should resolve");
    SELFTEST_CHECK(loc_fire > 0, "twocats_fireplace should resolve");
    SELFTEST_CHECK(loc_table > 0, "twocats_table should resolve");
    SELFTEST_CHECK(obj_amulet > 0, "twocats_amuletofcatspeak should resolve");
    SELFTEST_CHECK(obj_death > 0, "deathrune should resolve");
    SELFTEST_CHECK(obj_rake > 0, "rake should resolve");
    SELFTEST_CHECK(obj_seed > 0, "potato_seed should resolve");
    SELFTEST_CHECK(obj_logs > 0, "logs should resolve");
    SELFTEST_CHECK(obj_tinder > 0, "tinderbox should resolve");
    SELFTEST_CHECK(obj_cake > 0, "chocolate_cake should resolve");
    SELFTEST_CHECK(obj_milk > 0, "bucket_milk should resolve");
    SELFTEST_CHECK(obj_shears > 0, "shears should resolve");
    SELFTEST_CHECK(obj_doc > 0, "twocats_doctors_hat should resolve");
    SELFTEST_CHECK(obj_nurse > 0, "twocats_nurses_hat should resolve");
    SELFTEST_CHECK(obj_toy > 0, "twocats_mouse_toy should resolve");
    SELFTEST_CHECK(obj_lamp > 0, "thosf_reward_lamp should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "twocats_quest") >= 0,
                   "twocats_quest varbit should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_tailoftwocats") >= 0,
                   "dbrow is quest_tailoftwocats");

    if( npc_unferth <= 0 || obj_amulet <= 0 )
        return;

    t2c_set_bit(srv, "twocats_quest", 0);
    t2c_set_bit(srv, "twocats_chores_tidygarden", 0);
    t2c_set_bit(srv, "twocats_chores_tidyhouse", 0);
    t2c_set_bit(srv, "twocats_chores_warmhuman", 0);
    t2c_set_bit(srv, "twocats_chores_feedhuman", 0);
    t2c_set_bit(srv, "twocats_chores_tidyhuman", 0);
    t2c_set_bit(srv, "twocats_locator_direction", 0);
    t2c_set_bit(srv, "twocats_gotreward", 0);
    t2c_set_bit(srv, "twocats_reldo", 0);
    t2c_journal(srv);
    t2c_pass("journal not started");

    /* Hild too-early citizen talk. */
    slot_hild = t2c_spawn(srv, npc_hild, T2C_HILD_X, T2C_HILD_Z, 0);
    SELFTEST_CHECK(slot_hild >= 0, "Hild should spawn");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(
                       srv, SS_TRIGGER_OPNPC1, npc_hild, -1, slot_hild) == TORIRSSERVER_TRIGGER_RAN,
                   "Hild at state 0 should run citizen talk");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 0, "citizen Hild must not start the quest");
    t2c_pass("hild too-early citizen");

    /* Unferth refuse: no amulet, pick busy. */
    slot_unferth = t2c_spawn(srv, npc_unferth, T2C_UNFERTH_X, T2C_UNFERTH_Z, 0);
    SELFTEST_CHECK(slot_unferth >= 0, "Unferth should spawn");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "Unferth start should run");
    t2c_click_until_menu(srv, 8);
    t2c_pick_row(srv, 2);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 0, "refuse busy must leave state 0");
    t2c_pass("unferth refuse busy");

    /* Unferth no-amulet refuse after Yes. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "Unferth start (no amulet) should run");
    t2c_click_until_menu(srv, 8);
    t2c_pick_row(srv, 1);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 0, "no-amulet refuse must leave state 0");
    t2c_pass("unferth no-amulet refuse");

    /* Accept with amulet. */
    t2c_give(player, obj_amulet, 1);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "Unferth accept should run");
    t2c_click_until_menu(srv, 8);
    t2c_pick_row(srv, 1);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 5, "accept should write state 5, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    t2c_pass("unferth start accept");
    t2c_journal(srv);
    t2c_pass("journal state 5");

    /* Hild death-rune refuse, then success. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_hild, -1, slot_hild) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "Hild without runes should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 5, "Hild refuse must stay at 5");
    t2c_pass("hild death-rune refuse");

    t2c_give(player, obj_death, 5);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_hild, -1, slot_hild) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "Hild with runes should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 10, "Hild success should write 10, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    SELFTEST_CHECK(t2c_inv_total(player, obj_death) == 0, "Hild should consume 5 death runes");
    t2c_pass("hild success");
    t2c_journal(srv);
    t2c_pass("journal state 10");

    /* Unferth mid: find Bob reminder (must not skip via amulet label). */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "Unferth find-Bob reminder should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 10, "find-Bob reminder must stay at 10");
    t2c_pass("unferth find bob reminder");

    /* Amulet Open find Bob 1. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD3, obj_amulet, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "amulet Open at 10 should run find Bob 1");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 20, "amulet find Bob 1 should write 20, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_locator_direction") == 1, "locator direction should be 1");
    t2c_pass("amulet find bob 1");

    /* Bob talk 1 via DS2-owned opnpc. */
    slot_bob = t2c_spawn(srv, npc_bob, T2C_BOB_X, T2C_BOB_Z, 0);
    SELFTEST_CHECK(slot_bob >= 0, "Bob should spawn");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_bob, -1, slot_bob) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "Bob talk 1 should run through DS2 hook");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 20, "Bob talk 1 at 20 stays 20");
    t2c_pass("bob talk 1");
    t2c_journal(srv);
    t2c_pass("journal state 20");

    /* Unferth after Bob. */
    t2c_tele(srv, T2C_UNFERTH_X, T2C_UNFERTH_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "Unferth after Bob should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 20, "after-Bob Unferth must not skip Gertrude");
    t2c_pass("unferth after bob");

    /* Gertrude after-Bob. */
    slot_gertrude = t2c_spawn(srv, npc_gertrude, T2C_GERTRUDE_X, T2C_GERTRUDE_Z, 0);
    SELFTEST_CHECK(slot_gertrude >= 0, "Gertrude should spawn");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gertrude, -1,
                                                 slot_gertrude) == TORIRSSERVER_TRIGGER_RAN,
                   "Gertrude after Bob should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 25, "Gertrude should write 25, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    t2c_pass("gertrude after bob");
    t2c_journal(srv);
    t2c_pass("journal state 25");

    /* Unferth mid Reldo. */
    t2c_tele(srv, T2C_UNFERTH_X, T2C_UNFERTH_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "Unferth Reldo mid should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 25, "Reldo mid must stay 25");
    t2c_pass("unferth mid reldo");

    /* Reldo no-amulet, then lore. */
    t2c_clear_inv(player);
    slot_reldo = t2c_spawn(srv, npc_reldo, T2C_RELDO_X, T2C_RELDO_Z, 0);
    SELFTEST_CHECK(slot_reldo >= 0, "Reldo should spawn");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_reldo, -1, slot_reldo) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "Reldo without amulet should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 25, "Reldo no-amulet stays 25");
    t2c_pass("reldo no amulet");

    t2c_give(player, obj_amulet, 1);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_reldo, -1, slot_reldo) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "Reldo catspeak lore should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 30, "Reldo should write 30, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_reldo") == 1, "twocats_reldo should be 1");
    t2c_pass("reldo bob lore");
    t2c_journal(srv);
    t2c_pass("journal state 30");

    /* Amulet find Bob 2, then Bob talk 2. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD3, obj_amulet, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "amulet Open at 30 should run find Bob 2");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 35, "amulet find Bob 2 should write 35, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    t2c_pass("amulet find bob 2");
    t2c_journal(srv);
    t2c_pass("journal state 35");

    t2c_tele(srv, T2C_BOB_X, T2C_BOB_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_bob, -1, slot_bob) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "Bob talk 2 should run");
    t2c_finish(srv);
    t2c_pass("bob talk 2");

    /* Unferth mid Sphinx. */
    t2c_tele(srv, T2C_UNFERTH_X, T2C_UNFERTH_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "Unferth Sphinx mid should run");
    t2c_finish(srv);
    t2c_pass("unferth mid sphinx");

    /* Sphinx via DS2-owned opnpc. */
    slot_sphinx = t2c_spawn(srv, npc_sphinx, T2C_SPHINX_X, T2C_SPHINX_Z, 0);
    SELFTEST_CHECK(slot_sphinx >= 0, "Sphinx should spawn");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sphinx, -1,
                                                 slot_sphinx) == TORIRSSERVER_TRIGGER_RAN,
                   "Sphinx should run twocats label via DS2 hook");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 40, "Sphinx should write 40, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    t2c_pass("sphinx help bob");
    t2c_journal(srv);
    t2c_pass("journal state 40");

    /* Chores. Plant oplocu requires inv_total(rake) >= 3. */
    t2c_give(player, obj_rake, 3);
    t2c_give(player, obj_seed, 4);
    t2c_give(player, obj_logs, 1);
    t2c_give(player, obj_tinder, 1);
    t2c_give(player, obj_cake, 1);
    t2c_give(player, obj_milk, 1);
    t2c_give(player, obj_shears, 1);

    slot_patch = t2c_ensure_loc(srv, T2C_PATCH_X, T2C_PATCH_Z, loc_patch);
    SELFTEST_CHECK(slot_patch >= 0, "twocats_patch should be in the scene");
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_patch, -1, slot_patch);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_tidygarden") == 3, "rake should set garden 3, got %d",
                   t2c_get_bit(srv, "twocats_chores_tidygarden"));
    t2c_pass("chore rake");

    player->last_useitem = obj_seed;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_patch, -1, slot_patch);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_tidygarden") == 4, "plant should set garden 4, got %d",
                   t2c_get_bit(srv, "twocats_chores_tidygarden"));
    t2c_pass("chore plant potatoes");

    slot_bed = t2c_ensure_loc(srv, T2C_BED_X, T2C_BED_Z, loc_bed);
    SELFTEST_CHECK(slot_bed >= 0, "twocats_bed should be in the scene");
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_bed, -1, slot_bed);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_tidyhouse") == 1, "make bed should set house 1");
    t2c_pass("chore make bed");

    slot_fire = t2c_ensure_loc(srv, T2C_FIRE_X, T2C_FIRE_Z, loc_fire);
    SELFTEST_CHECK(slot_fire >= 0, "twocats_fireplace should be in the scene");
    player->last_useitem = obj_logs;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_fire, -1, slot_fire);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_warmhuman") == 1, "logs should set fire 1");
    t2c_pass("chore logs");

    player->last_useitem = obj_tinder;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_fire, -1, slot_fire);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_warmhuman") == 2, "tinderbox should set fire 2");
    t2c_pass("chore light fire");

    slot_table = t2c_ensure_loc(srv, T2C_TABLE_X, T2C_TABLE_Z, loc_table);
    SELFTEST_CHECK(slot_table >= 0, "twocats_table should be in the scene");
    player->last_useitem = obj_cake;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_table, -1, slot_table);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_feedhuman") == 3, "cake should set feed 3");
    t2c_pass("chore cake");

    player->last_useitem = obj_milk;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_table, -1, slot_table);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_feedhuman") == 4, "milk should set feed 4");
    t2c_pass("chore milk");

    t2c_tele(srv, T2C_UNFERTH_X, T2C_UNFERTH_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "shear Unferth should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_tidyhuman") == 8, "shear should set tidyhuman 8");
    t2c_pass("chore shear unferth");

    /* Wait potatoes: planted + other chores -> grown mes, then 45. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "wait potatoes should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_chores_tidygarden") == 8, "potatoes should grow to 8");
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 45, "grown potatoes should write 45, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    t2c_pass("chore potato grown");
    t2c_journal(srv);
    t2c_pass("journal state 45");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "report to Unferth should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 50, "report should write 50, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    t2c_pass("unferth report");
    t2c_journal(srv);
    t2c_pass("journal state 50");

    /* Apothecary hat + potion. */
    slot_apoth = t2c_spawn(srv, npc_apoth, T2C_APOTH_X, T2C_APOTH_Z, 0);
    SELFTEST_CHECK(slot_apoth >= 0, "Apothecary should spawn");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_apoth, -1, slot_apoth) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "Apothecary Two Cats branch should run");
    t2c_click_until_menu(srv, 8);
    t2c_pick_row(srv, 1);
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 55, "Apothecary should write 55, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    SELFTEST_CHECK(t2c_inv_total(player, obj_doc) > 0 || t2c_inv_total(player, obj_nurse) > 0,
                   "Apothecary should give a hat");
    t2c_pass("apothecary hat potion");
    t2c_journal(srv);
    t2c_pass("journal state 55");

    /* Cure Unferth. */
    t2c_tele(srv, T2C_UNFERTH_X, T2C_UNFERTH_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "cure Unferth should run");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 60, "cure should write 60, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    t2c_pass("cure unferth");
    t2c_journal(srv);
    t2c_pass("journal state 60");

    /* Amulet find Bob 3, Bob talk 3. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD3, obj_amulet, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "amulet Open at 60 should run find Bob 3");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 65, "amulet find Bob 3 should write 65, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    t2c_pass("amulet find bob 3");

    t2c_tele(srv, T2C_BOB_X, T2C_BOB_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_bob, -1, slot_bob) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "Bob talk 3 should run");
    t2c_finish(srv);
    t2c_pass("bob talk 3");
    t2c_journal(srv);
    t2c_pass("journal state 65");

    /* Complete. */
    t2c_tele(srv, T2C_UNFERTH_X, T2C_UNFERTH_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_unferth, -1,
                                                 slot_unferth) == TORIRSSERVER_TRIGGER_RAN,
                   "Unferth done should run");
    t2c_finish(srv);
    {
        int q;

        for( q = 0; q < 24; q++ )
            selftest_tick(srv);
    }
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 70, "done should write 70, got %d",
                   t2c_get_bit(srv, "twocats_quest"));
    SELFTEST_CHECK(t2c_inv_total(player, obj_lamp) >= 2, "complete should grant 2 lamps, got %d",
                   t2c_inv_total(player, obj_lamp));
    SELFTEST_CHECK(t2c_inv_total(player, obj_toy) >= 1, "complete should grant mouse toy");
    t2c_pass("authored complete scroll");
    t2c_journal(srv);
    t2c_pass("journal QUEST COMPLETE");

    /* Out-of-window Ratcatchers Gertrude still works. */
    t2c_set_bit(srv, "twocats_quest", 0);
    t2c_set_bit(srv, "ics_little_var", T2C_ICS_COMPLETE);
    t2c_set_bit(srv, "giantdwarf_quest", 1);
    t2c_set_bit(srv, "ratcatch_var", T2C_RATCATCH_NOT_STARTED);
    t2c_tele(srv, T2C_GERTRUDE_X, T2C_GERTRUDE_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gertrude, -1,
                                                 slot_gertrude) == TORIRSSERVER_TRIGGER_RAN,
                   "out-of-window Gertrude should still reach Ratcatchers");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 0, "Ratcatchers Gertrude must not write twocats");
    t2c_pass("gertrude ratcatchers still works");

    /* Out-of-window GD Reldo still works. */
    t2c_set_bit(srv, "twocats_quest", 25);
    t2c_set_bit(srv, "giantdwarf_quest", T2C_GDWARF_IMCANDO_ASKED);
    t2c_tele(srv, T2C_RELDO_X, T2C_RELDO_Z, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_reldo, -1, slot_reldo) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "GD Reldo should steal the turn at imcando_asked");
    t2c_finish(srv);
    SELFTEST_CHECK(t2c_get_bit(srv, "giantdwarf_quest") == T2C_GDWARF_RELDO_TOLD,
                   "GD Reldo should advance Giant Dwarf, got %d",
                   t2c_get_bit(srv, "giantdwarf_quest"));
    SELFTEST_CHECK(t2c_get_bit(srv, "twocats_quest") == 25, "GD Reldo must not steal Two Cats state");
    t2c_pass("reldo gdwarf still works");

    t2c_free_npc(srv, slot_unferth);
    t2c_free_npc(srv, slot_hild);
    t2c_free_npc(srv, slot_bob);
    t2c_free_npc(srv, slot_gertrude);
    t2c_free_npc(srv, slot_reldo);
    t2c_free_npc(srv, slot_sphinx);
    t2c_free_npc(srv, slot_apoth);
    t2c_clear_inv(player);
    t2c_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_ATAILOFTWOCATS_SELFTEST_U_H */
