#ifndef TORIRSSERVER_TEST_QUEST_LOSTTRIBE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_LOSTTRIBE_SELFTEST_U_H

/* The Lost Tribe Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Sigmund / Duke / Bob / Reldo / generals /
 * Mistag cannot leak. Real OPNPC / OPLOC / OPHELD / OPNPC3 / OPNPCU /
 * OPLOCU on the authored path. player->godmode = 1 for the whole walk
 * (HAM / cellar are not death tests). Completion goes through Mistag's
 * authored ~quest_complete_rewards(quest_losttribe, ...). Additive LT
 * branches only — do not rewrite Rune Mysteries, Goblin Diplomacy,
 * Cook's Assistant (red herring already present), Observatory,
 * Construction, or MTA. */

#define LT_NOT_STARTED 0
#define LT_STARTED 1
#define LT_TUNNEL 4
#define LT_GENERALS 7
#define LT_MISTAG 8
#define LT_HAM_HUNT 9
#define LT_TREATY 10
#define LT_COMPLETE 11

#define LT_CONTACT_NONE 0
#define LT_CONTACT_COOK 1
#define LT_CONTACT_DUKE 2
#define LT_CONTACT_BROOCH 3

#define LT_BOOK_NONE 0
#define LT_BOOK_RELDO 1
#define LT_BOOK_IDENTIFIED 2
#define LT_BOOK_WAR 3

#define LT_HAM_NONE 0
#define LT_HAM_ROBES 1
#define LT_HAM_SILVER 3

#define LT_RM_COMPLETE 6
#define LT_GOBDIP_COMPLETE 6

#define LT_MINING_REQ 17
#define LT_AGILITY_REQ 13
#define LT_THIEVING_REQ 13
#define LT_MINING_XP_TENTHS 30000

#define LT_STAT_MINING 14
#define LT_STAT_THIEVING 17

#define LT_SIGMUND_X 3215
#define LT_SIGMUND_Z 3221
#define LT_DUKE_X 3209
#define LT_DUKE_Z 3222
#define LT_BOB_X 3231
#define LT_BOB_Z 3203
#define LT_COOK_X 3207
#define LT_COOK_Z 3217
#define LT_RELDO_X 3207
#define LT_RELDO_Z 3496
#define LT_WARTFACE_X 2957
#define LT_WARTFACE_Z 3512
#define LT_MISTAG_X 3323
#define LT_MISTAG_Z 9615
#define LT_RUBBLE_X 3219
#define LT_RUBBLE_Z 9618
#define LT_HOLE_X 3221
#define LT_HOLE_Z 9618
#define LT_BOOKCASE_X 3207
#define LT_BOOKCASE_Z 3496
#define LT_CHEST_X 3209
#define LT_CHEST_Z 3217
#define LT_CRATE_X 3152
#define LT_CRATE_Z 9645
#define LT_TRAP_X 3166
#define LT_TRAP_Z 3252

static void
lt_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "LT PASS: %s\n", step);
}

static void
lt_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
lt_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
lt_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    assert(stat >= 0);
    assert(stat < TORIRSSERVER_STAT_COUNT);
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
lt_finish(struct ToriRSServer* srv)
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

static void
lt_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
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
lt_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    lt_god(player);
    selftest_tick(srv);
}

static int
lt_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    lt_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
lt_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
lt_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
lt_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( lt_inv_total(player, obj_id) >= count )
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
lt_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
lt_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
lt_set_varp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        player->varps[varp] = value;
}

static int
lt_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    lt_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
lt_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
lt_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    lt_talk(srv, npc_type, slot);
    lt_finish(srv);
}

static void
lt_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    lt_talk(srv, npc_type, slot);
    lt_click_until_menu(srv, 16);
    selftest_charter_choose(srv, row);
    lt_finish(srv);
}

static void
lt_skills_ok(struct ToriRSServerPlayer* player)
{
    assert(player);
    lt_set_stat(player, LT_STAT_MINING, LT_MINING_REQ);
    lt_set_stat(player, TORIRSSERVER_STAT_AGILITY, LT_AGILITY_REQ);
    lt_set_stat(player, LT_STAT_THIEVING, LT_THIEVING_REQ);
}

static void
lt_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    lt_set_varp(player, "runemysteries", LT_RM_COMPLETE);
    lt_set_bit(srv, "gobdip_main", LT_GOBDIP_COMPLETE);
    lt_skills_ok(player);
    lt_god(player);
}

static void
lt_reset_quest(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    lt_set_bit(srv, "lost_tribe_quest", LT_NOT_STARTED);
    lt_set_bit(srv, "lost_tribe_contact", LT_CONTACT_NONE);
    lt_set_bit(srv, "lost_tribe_bookmark", LT_BOOK_NONE);
    lt_set_bit(srv, "lost_tribe_ham", LT_HAM_NONE);
    lt_set_bit(srv, "lost_tribe_sigmund_accused", 0);
    lt_set_bit(srv, "lost_tribe_sigmund_leaving", 0);
    lt_set_bit(srv, "lost_tribe_returned_brooch", 0);
    lt_set_varp(player, "runemysteries", 0);
    lt_set_bit(srv, "gobdip_main", 0);
    lt_set_varp(player, "squire", 0);
    lt_set_varp(player, "phoenixgang", 0);
    lt_set_bit(srv, "dttd_main", 0);
    lt_clear_inv(player);
    lt_god(player);
}

static void
selftest_quest_losttribe(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_sigmund;
    int npc_duke;
    int npc_bob;
    int npc_cook;
    int npc_reldo;
    int npc_wartface;
    int npc_mistag;
    int npc_mistag1;
    int loc_rubble;
    int loc_hole;
    int loc_bookcase;
    int loc_chest;
    int loc_crate;
    int loc_trap;
    int obj_brooch;
    int obj_book;
    int obj_pick;
    int obj_key;
    int obj_silver;
    int obj_treaty;
    int obj_ring;
    int obj_helmet;
    int slot;
    int loc_slot;
    int quest;
    int contact;
    int book;
    int ham;
    int mining_xp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;

    fprintf(stderr, "ToriRSServer selftest: The Lost Tribe Gate D\n");
    player->godmode = 1;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    npc_sigmund = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lost_tribe_sigmund");
    npc_duke = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "duke_of_lumbridge");
    npc_bob = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bob");
    npc_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "cook");
    npc_reldo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "reldo");
    npc_wartface = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "general_wartface");
    npc_mistag = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lost_tribe_mistag");
    npc_mistag1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lost_tribe_mistag_1op");
    loc_rubble = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lost_tribe_cellar_hole_blocking");
    loc_hole = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "lost_tribe_cavewall_hole_walldecor");
    loc_bookcase = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lost_tribe_bookcase");
    loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lost_tribe_chest");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lost_tribe_crate");
    loc_trap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "osf_trapdoor_closed");
    obj_brooch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lost_tribe_brooch");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lost_tribe_book");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lost_tribe_chest_key");
    obj_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lost_tribe_silverware");
    obj_treaty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lost_tribe_treaty");
    obj_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ring_of_life");
    obj_helmet = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_OBJ, "cave_goblin_mining_helmet_unlit");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lost_tribe_quest") >= 0,
                   "varbit lost_tribe_quest should resolve");
    SELFTEST_CHECK(npc_sigmund > 0, "npc lost_tribe_sigmund should resolve");
    SELFTEST_CHECK(npc_duke > 0, "npc duke_of_lumbridge should resolve");
    SELFTEST_CHECK(npc_bob > 0, "npc bob should resolve");
    SELFTEST_CHECK(npc_mistag > 0, "npc lost_tribe_mistag should resolve");
    if( npc_sigmund <= 0 )
    {
        fprintf(stderr, "ToriRSServer lt selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    lt_reset_quest(srv, player);

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_not_started");

    slot = lt_spawn(srv, npc_sigmund, LT_SIGMUND_X, LT_SIGMUND_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Sigmund should spawn");
    if( slot >= 0 )
    {
        lt_talk_pick(srv, npc_sigmund, slot, 2);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_NOT_STARTED,
                       "who-are-you must not start the quest, got %d",
                       lt_get_bit(player, "lost_tribe_quest"));
        lt_pass("opnpc1_sigmund_who_are_you");

        lt_talk_pick(srv, npc_sigmund, slot, 1);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_NOT_STARTED,
                       "Sigmund must refuse without RM+GD, got %d",
                       lt_get_bit(player, "lost_tribe_quest"));
        lt_pass("opnpc1_sigmund_no_prereqs");

        lt_set_varp(player, "runemysteries", LT_RM_COMPLETE);
        lt_set_bit(srv, "gobdip_main", LT_GOBDIP_COMPLETE);
        lt_set_stat(player, LT_STAT_MINING, 1);
        lt_set_stat(player, TORIRSSERVER_STAT_AGILITY, 1);
        lt_set_stat(player, LT_STAT_THIEVING, 1);
        lt_talk_pick(srv, npc_sigmund, slot, 1);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_NOT_STARTED,
                       "Sigmund must skill-gate Mining/Agility/Thieving, got %d",
                       lt_get_bit(player, "lost_tribe_quest"));
        lt_pass("opnpc1_sigmund_skill_gate");

        lt_prereqs(srv, player);
        lt_talk_pick(srv, npc_sigmund, slot, 1);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_STARTED,
                       "accepting Sigmund must write started, got %d",
                       lt_get_bit(player, "lost_tribe_quest"));
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_contact") == LT_CONTACT_NONE,
                       "start must leave contact none, got %d",
                       lt_get_bit(player, "lost_tribe_contact"));
        lt_pass("opnpc1_sigmund_start");

        lt_talk_finish(srv, npc_sigmund, slot);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_STARTED,
                       "mid Sigmund must stay started");
        lt_pass("opnpc1_sigmund_mid_not_found");
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_started");

    if( npc_cook > 0 )
    {
        int cook = lt_spawn(srv, npc_cook, LT_COOK_X, LT_COOK_Z, 0);

        SELFTEST_CHECK(cook >= 0, "Cook should spawn");
        if( cook >= 0 )
        {
            lt_talk_finish(srv, npc_cook, cook);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_contact") == LT_CONTACT_NONE,
                           "Cook is a red herring and must not write contact, got %d",
                           lt_get_bit(player, "lost_tribe_contact"));
            lt_pass("opnpc1_cook_red_herring");
            lt_free_npc(srv, cook);
        }
    }

    if( npc_bob > 0 )
    {
        int bob = lt_spawn(srv, npc_bob, LT_BOB_X, LT_BOB_Z, 0);

        SELFTEST_CHECK(bob >= 0, "Bob should spawn");
        if( bob >= 0 )
        {
            lt_talk_finish(srv, npc_bob, bob);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_contact") == LT_CONTACT_COOK,
                           "Bob witness must write contact cook, got %d",
                           lt_get_bit(player, "lost_tribe_contact"));
            lt_pass("opnpc1_bob_witness");
            lt_free_npc(srv, bob);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_bob_witness");

    if( npc_duke > 0 )
    {
        int duke = lt_spawn(srv, npc_duke, LT_DUKE_X, LT_DUKE_Z, 1);

        SELFTEST_CHECK(duke >= 0, "Duke should spawn");
        if( duke >= 0 )
        {
            lt_talk_finish(srv, npc_duke, duke);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_contact") == LT_CONTACT_DUKE,
                           "Duke permission must write contact duke, got %d",
                           lt_get_bit(player, "lost_tribe_contact"));
            lt_pass("opnpc1_duke_permission");
            lt_free_npc(srv, duke);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_duke_permission");

    if( loc_rubble >= 0 )
    {
        loc_slot = lt_place_loc(srv, loc_rubble, LT_RUBBLE_X, LT_RUBBLE_Z, 0);
        ToriRSServer_ScriptsRunProc(srv, "[proc,lost_tribe_mine_rubble]", NULL, 0);
        lt_finish(srv);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_STARTED,
                       "rubble without a pickaxe must not advance, got %d",
                       lt_get_bit(player, "lost_tribe_quest"));
        lt_pass("oploc_rubble_no_pickaxe");

        if( obj_pick > 0 )
            lt_give(player, obj_pick, 1);
        ToriRSServer_ScriptsRunProc(srv, "[proc,lost_tribe_mine_rubble]", NULL, 0);
        {
            int t;

            for( t = 0; t < 8; t++ )
                selftest_tick(srv);
        }
        lt_finish(srv);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_TUNNEL,
                       "mining rubble must write tunnel, got %d",
                       lt_get_bit(player, "lost_tribe_quest"));
        lt_pass("oploc_rubble_mine");
        (void)loc_slot;
    }

    if( loc_hole >= 0 )
    {
        loc_slot = lt_place_loc(srv, loc_hole, LT_HOLE_X, LT_HOLE_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_hole, -1, loc_slot);
        lt_finish(srv);
        lt_pass("oploc1_cellar_hole_squeeze");
    }

    if( obj_brooch > 0 )
    {
        if( lt_inv_total(player, obj_brooch) <= 0 )
            lt_give(player, obj_brooch, 1);
        player->last_item = obj_brooch;
        player->last_slot = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_brooch, -1, -1);
        lt_finish(srv);
        lt_pass("opheld1_brooch_closeup");
    }

    if( npc_duke > 0 && obj_brooch > 0 )
    {
        int duke = lt_spawn(srv, npc_duke, LT_DUKE_X, LT_DUKE_Z, 1);

        if( duke >= 0 )
        {
            if( lt_inv_total(player, obj_brooch) <= 0 )
                lt_give(player, obj_brooch, 1);
            lt_set_bit(srv, "lost_tribe_quest", LT_TUNNEL);
            lt_set_bit(srv, "lost_tribe_contact", LT_CONTACT_DUKE);
            lt_talk_pick(srv, npc_duke, duke, 1);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_contact") == LT_CONTACT_BROOCH,
                           "Duke brooch must write contact brooch, got %d",
                           lt_get_bit(player, "lost_tribe_contact"));
            lt_pass("opnpc1_duke_brooch");
            lt_free_npc(srv, duke);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_brooch");

    if( npc_reldo > 0 && obj_brooch > 0 )
    {
        int reldo = lt_spawn(srv, npc_reldo, LT_RELDO_X, LT_RELDO_Z, 0);

        SELFTEST_CHECK(reldo >= 0, "Reldo should spawn");
        if( reldo >= 0 )
        {
            /* OPNPC1 Reldo: phoenix=0 + squire=0 + brooch in inv → p_choice4,
             * brooch is row 4. ~chatnpc needs this active npc. */
            if( lt_inv_total(player, obj_brooch) <= 0 )
                lt_give(player, obj_brooch, 1);
            lt_set_bit(srv, "lost_tribe_contact", LT_CONTACT_BROOCH);
            lt_talk_pick(srv, npc_reldo, reldo, 4);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_bookmark") == LT_BOOK_RELDO,
                           "Reldo brooch must write bookmark reldo, got %d",
                           lt_get_bit(player, "lost_tribe_bookmark"));
            lt_pass("opnpc1_reldo_brooch");

            lt_talk_pick(srv, npc_reldo, reldo, 4);
            lt_pass("opnpc1_reldo_already_pointed");
            lt_free_npc(srv, reldo);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_reldo");

    if( loc_bookcase >= 0 && obj_book > 0 )
    {
        lt_set_bit(srv, "lost_tribe_bookmark", LT_BOOK_RELDO);
        lt_clear_inv(player);
        loc_slot = lt_place_loc(srv, loc_bookcase, LT_BOOKCASE_X, LT_BOOKCASE_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_bookcase, -1, loc_slot);
        lt_finish(srv);
        SELFTEST_CHECK(lt_inv_total(player, obj_book) > 0,
                       "bookcase must grant the goblin symbol book");
        lt_pass("oploc1_bookcase_find");

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_bookcase, -1, loc_slot);
        lt_finish(srv);
        lt_pass("oploc1_bookcase_already");
    }

    if( obj_book > 0 )
    {
        lt_set_bit(srv, "lost_tribe_bookmark", LT_BOOK_RELDO);
        if( lt_inv_total(player, obj_book) <= 0 )
            lt_give(player, obj_book, 1);
        player->last_item = obj_book;
        player->last_slot = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_book, -1, -1);
        lt_finish(srv);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_bookmark") == LT_BOOK_IDENTIFIED,
                       "reading the book must identify Dorgeshuun, got %d",
                       lt_get_bit(player, "lost_tribe_bookmark"));
        lt_pass("opheld1_book_identify");
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_identified");

    if( npc_wartface > 0 )
    {
        int wart = lt_spawn(srv, npc_wartface, LT_WARTFACE_X, LT_WARTFACE_Z, 0);

        SELFTEST_CHECK(wart >= 0, "Wartface should spawn");
        if( wart >= 0 )
        {
            lt_set_bit(srv, "lost_tribe_quest", LT_TUNNEL);
            lt_set_bit(srv, "lost_tribe_bookmark", LT_BOOK_IDENTIFIED);
            lt_talk(srv, npc_wartface, wart);
            lt_click_until_menu(srv, 16);
            selftest_charter_choose(srv, 3);
            lt_finish(srv);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_GENERALS,
                           "generals briefing must write generals, got %d",
                           lt_get_bit(player, "lost_tribe_quest"));
            lt_pass("opnpc1_generals_briefing");
            lt_free_npc(srv, wart);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_generals");

    if( npc_duke > 0 )
    {
        int duke = lt_spawn(srv, npc_duke, LT_DUKE_X, LT_DUKE_Z, 1);

        if( duke >= 0 )
        {
            lt_set_bit(srv, "lost_tribe_quest", LT_GENERALS);
            lt_set_bit(srv, "lost_tribe_bookmark", LT_BOOK_IDENTIFIED);
            lt_talk_pick(srv, npc_duke, duke, 1);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_bookmark") == LT_BOOK_WAR,
                           "Duke war briefing must write bookmark war, got %d",
                           lt_get_bit(player, "lost_tribe_bookmark"));
            lt_pass("opnpc1_duke_war");
            lt_free_npc(srv, duke);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_war");

    if( npc_mistag1 > 0 || npc_mistag > 0 )
    {
        int mist_type = npc_mistag1 > 0 ? npc_mistag1 : npc_mistag;
        int mist = lt_spawn(srv, mist_type, LT_MISTAG_X, LT_MISTAG_Z, 0);

        SELFTEST_CHECK(mist >= 0, "Mistag should spawn");
        if( mist >= 0 )
        {
            lt_set_bit(srv, "lost_tribe_quest", LT_GENERALS);
            lt_set_bit(srv, "lost_tribe_bookmark", LT_BOOK_WAR);
            lt_talk_finish(srv, mist_type, mist);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_GENERALS,
                           "talking before the bow must leave generals, got %d",
                           lt_get_bit(player, "lost_tribe_quest"));
            lt_pass("opnpc1_mistag_panic");

            /* npc_find(lost_tribe_mistag_1op, 3) — spawn that type in range. */
            if( npc_mistag1 > 0 && mist_type != npc_mistag1 )
            {
                lt_free_npc(srv, mist);
                mist = lt_spawn(srv, npc_mistag1, LT_MISTAG_X, LT_MISTAG_Z, 0);
                mist_type = npc_mistag1;
            }
            ToriRSServer_ScriptsRunProc(srv, "[proc,lost_tribe_mistag_emote_bow]", NULL, 0);
            lt_finish(srv);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_MISTAG,
                           "goblin bow at Mistag must write mistag, got %d",
                           lt_get_bit(player, "lost_tribe_quest"));
            lt_pass("emote_mistag_goblin_bow");

            lt_talk_finish(srv, mist_type, mist);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_MISTAG,
                           "post-contact Mistag must stay mistag");
            lt_pass("opnpc1_mistag_after_contact");
            lt_free_npc(srv, mist);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_mistag");

    if( npc_duke > 0 )
    {
        int duke = lt_spawn(srv, npc_duke, LT_DUKE_X, LT_DUKE_Z, 1);

        if( duke >= 0 )
        {
            lt_set_bit(srv, "lost_tribe_quest", LT_MISTAG);
            lt_set_bit(srv, "lost_tribe_bookmark", LT_BOOK_WAR);
            lt_talk_pick(srv, npc_duke, duke, 1);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_HAM_HUNT,
                           "Duke silverware accusation must write ham_hunt, got %d",
                           lt_get_bit(player, "lost_tribe_quest"));
            lt_pass("opnpc1_duke_silverware_missing");
            lt_free_npc(srv, duke);
        }
    }

    {
        int sig = lt_spawn(srv, npc_sigmund, LT_SIGMUND_X, LT_SIGMUND_Z, 0);

        if( sig >= 0 )
        {
            lt_set_bit(srv, "lost_tribe_quest", LT_HAM_HUNT);
            lt_set_bit(srv, "lost_tribe_ham", LT_HAM_NONE);
            lt_clear_inv(player);
            lt_skills_ok(player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_sigmund, -1, sig);
            {
                int t;

                for( t = 0; t < 8; t++ )
                    selftest_tick(srv);
            }
            lt_finish(srv);
            SELFTEST_CHECK(obj_key <= 0 || lt_inv_total(player, obj_key) > 0,
                           "pickpocket Sigmund must grant the chest key");
            lt_pass("opnpc3_sigmund_pickpocket");
            lt_free_npc(srv, sig);
        }
    }

    if( loc_chest >= 0 )
    {
        lt_set_bit(srv, "lost_tribe_quest", LT_HAM_HUNT);
        lt_set_bit(srv, "lost_tribe_ham", LT_HAM_NONE);
        if( obj_key > 0 && lt_inv_total(player, obj_key) <= 0 )
            lt_give(player, obj_key, 1);
        loc_slot = lt_place_loc(srv, loc_chest, LT_CHEST_X, LT_CHEST_Z, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_chest, -1, loc_slot);
        lt_finish(srv);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_ham") == LT_HAM_ROBES,
                       "Sigmund chest must write ham robes, got %d",
                       lt_get_bit(player, "lost_tribe_ham"));
        lt_pass("oploc1_sigmund_chest");
    }

    if( loc_trap >= 0 )
    {
        loc_slot = lt_place_loc(srv, loc_trap, LT_TRAP_X, LT_TRAP_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC5, loc_trap, -1, loc_slot);
        {
            int t;

            for( t = 0; t < 6; t++ )
                selftest_tick(srv);
        }
        lt_finish(srv);
        lt_pass("oploc5_ham_trapdoor_pick");
    }

    if( loc_crate >= 0 )
    {
        lt_set_bit(srv, "lost_tribe_quest", LT_HAM_HUNT);
        lt_set_bit(srv, "lost_tribe_ham", LT_HAM_ROBES);
        lt_clear_inv(player);
        loc_slot = lt_place_loc(srv, loc_crate, LT_CRATE_X, LT_CRATE_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crate, -1, loc_slot);
        lt_finish(srv);
        SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_ham") == LT_HAM_SILVER,
                       "HAM crate must write silver, got %d",
                       lt_get_bit(player, "lost_tribe_ham"));
        SELFTEST_CHECK(obj_silver <= 0 || lt_inv_total(player, obj_silver) > 0,
                       "HAM crate must grant the silverware");
        lt_pass("oploc1_ham_crate_silverware");
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_silverware");

    if( npc_duke > 0 && obj_silver > 0 )
    {
        int duke = lt_spawn(srv, npc_duke, LT_DUKE_X, LT_DUKE_Z, 1);

        if( duke >= 0 )
        {
            lt_set_bit(srv, "lost_tribe_quest", LT_HAM_HUNT);
            lt_set_bit(srv, "lost_tribe_ham", LT_HAM_SILVER);
            lt_clear_inv(player);
            lt_give(player, obj_silver, 1);
            lt_talk_finish(srv, npc_duke, duke);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_TREATY,
                           "handing in silverware must write treaty, got %d",
                           lt_get_bit(player, "lost_tribe_quest"));
            SELFTEST_CHECK(obj_treaty <= 0 || lt_inv_total(player, obj_treaty) > 0,
                           "Duke must grant the peace treaty");
            lt_pass("opnpc1_duke_treaty");
            lt_free_npc(srv, duke);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_treaty");

    if( npc_mistag > 0 )
    {
        int mist = lt_spawn(srv, npc_mistag, LT_MISTAG_X, LT_MISTAG_Z, 0);

        if( mist >= 0 )
        {
            lt_set_bit(srv, "lost_tribe_quest", LT_TREATY);
            lt_set_bit(srv, "lost_tribe_bookmark", LT_BOOK_WAR);
            lt_set_bit(srv, "lost_tribe_ham", LT_HAM_SILVER);
            lt_clear_inv(player);
            ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,lost_tribe_mistag_treaty]", mist);
            lt_finish(srv);
            SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_TREATY,
                           "Mistag without the treaty must stay treaty, got %d",
                           lt_get_bit(player, "lost_tribe_quest"));
            lt_pass("opnpc1_mistag_lost_treaty");

            if( obj_treaty > 0 )
                lt_give(player, obj_treaty, 1);
            mining_xp_before = player->stat_xp_tenths[LT_STAT_MINING];
            /* Do not WorldCloseModal mid-signing — that aborts before rewards. */
            ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,lost_tribe_mistag_treaty]", mist);
            {
                int t;

                for( t = 0; t < 160 && player->active_script; t++ )
                {
                    if( selftest_click_through(srv, 1) <= 0 )
                        selftest_tick(srv);
                    if( lt_get_bit(player, "lost_tribe_quest") == LT_COMPLETE )
                        break;
                }
            }
            lt_finish(srv);
            quest = lt_get_bit(player, "lost_tribe_quest");
            SELFTEST_CHECK(quest == LT_COMPLETE,
                           "treaty signing must complete the quest at 11, got %d", quest);
            SELFTEST_CHECK(
                player->stat_xp_tenths[LT_STAT_MINING] >= mining_xp_before + LT_MINING_XP_TENTHS,
                "complete must grant 3000 Mining XP (30000 tenths), before %d after %d",
                mining_xp_before, player->stat_xp_tenths[LT_STAT_MINING]);
            SELFTEST_CHECK(obj_ring <= 0 || lt_inv_total(player, obj_ring) > 0,
                           "complete must grant a ring of life");
            lt_pass("opnpc1_mistag_complete");
            lt_free_npc(srv, mist);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,losttribe_journal]", NULL, 0);
    lt_finish(srv);
    lt_pass("journal_complete");

    if( npc_mistag > 0 && obj_brooch > 0 && obj_helmet > 0 )
    {
        int mist = lt_spawn(srv, npc_mistag, LT_MISTAG_X, LT_MISTAG_Z, 0);

        if( mist >= 0 )
        {
            lt_set_bit(srv, "lost_tribe_quest", LT_COMPLETE);
            if( lt_inv_total(player, obj_brooch) <= 0 )
                lt_give(player, obj_brooch, 1);
            player->last_useitem = obj_brooch;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_mistag, -1, mist);
            lt_finish(srv);
            SELFTEST_CHECK(lt_inv_total(player, obj_helmet) > 0,
                           "returning the brooch must grant a mining helmet");
            lt_pass("opnpcu_mistag_brooch_helmet");
            lt_free_npc(srv, mist);
        }
    }

    {
        static const uint8_t complete_cmd[] = "complete quest_losttribe\n";

        handle_cheat(srv, complete_cmd, (int)sizeof(complete_cmd) - 1);
    }
    lt_finish(srv);
    SELFTEST_CHECK(lt_get_bit(player, "lost_tribe_quest") == LT_COMPLETE,
                   "::complete twice must leave endstate 11");
    lt_pass("complete_idempotent");

    quest = lt_get_bit(player, "lost_tribe_quest");
    contact = lt_get_bit(player, "lost_tribe_contact");
    book = lt_get_bit(player, "lost_tribe_bookmark");
    ham = lt_get_bit(player, "lost_tribe_ham");
    SELFTEST_CHECK(quest == LT_COMPLETE, "walk must end complete, quest=%d contact=%d book=%d ham=%d",
                   quest, contact, book, ham);

    if( slot >= 0 )
        lt_free_npc(srv, slot);
    lt_reset_quest(srv, player);
    lt_god(player);

    fprintf(stderr, "ToriRSServer lt selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_LOSTTRIBE_SELFTEST_U_H */
