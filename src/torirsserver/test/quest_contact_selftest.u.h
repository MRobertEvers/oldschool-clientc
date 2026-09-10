#ifndef TORIRSSERVER_TEST_QUEST_CONTACT_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_CONTACT_SELFTEST_U_H

/* Contact! Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned High
 * Priest / Jex / Maisa / Osman / Scarab / Kaleef locs cannot leak. Real
 * OPNPC1 on the authored [opnpc1,ics_little_hipriest_vis] dispatcher
 * (BCS splice → @contact_priest_talk). Jex / trapdoor / kaleef / maisa /
 * contact_osman_multi / desert / cave / scarab use their authored headers.
 * No second High Priest or osman trigger. player->godmode = 1 for the
 * whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_CONTACT_ONLY=1
 */

#define CONTACT_NOT_STARTED 0
#define CONTACT_TOLD_JEX 30
#define CONTACT_INVESTIGATING 40
#define CONTACT_READ_PARCHMENT 50
#define CONTACT_MET_MAISA 60
#define CONTACT_TOLD_OSMAN 70
#define CONTACT_OSMAN_OUTSIDE 80
#define CONTACT_SCARAB_KILLED 100
#define CONTACT_READY_TO_FINISH 120
#define CONTACT_COMPLETE 130

#define CONTACT_PRINCE_COMPLETE 110
#define CONTACT_ICS_COMPLETE 26
#define CONTACT_REWARD_THIEVING_TENTHS 70000
#define CONTACT_REWARD_LAMP_QTY 2
#define CONTACT_REWARD_QP 1

#define CONTACT_PRIEST_X 3281
#define CONTACT_PRIEST_Z 2772
#define CONTACT_JEX_X 3306
#define CONTACT_JEX_Z 2800
#define CONTACT_BANK_X 2766
#define CONTACT_BANK_Z 5130
#define CONTACT_CHASM_X 3218
#define CONTACT_CHASM_Z 9246
#define CONTACT_OSMAN_AK_X 3286
#define CONTACT_OSMAN_AK_Z 3180
#define CONTACT_OSMAN_DESERT_X 3289
#define CONTACT_OSMAN_DESERT_Z 2818

static void
contact_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "CONTACT PASS: %s\n", step);
}

static void
contact_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
contact_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
contact_finish(struct ToriRSServer* srv)
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
contact_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
contact_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = contact_chatmenu();
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
contact_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = contact_chatmenu();
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
contact_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    contact_god(player);
    selftest_tick(srv);
}

static int
contact_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    contact_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
contact_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
contact_free_type(struct ToriRSServer* srv, int npc_type)
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

static int
contact_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
contact_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( contact_inv_total(player, obj_id) >= count )
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
contact_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
contact_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
contact_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int id;

    assert(srv);
    assert(name);
    id = ToriRSServer_WorldVarp(name);
    if( id < 0 )
        id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( id >= 0 )
        ToriRSServer_WorldSetVarp(srv, id, value);
}

static int
contact_quest(struct ToriRSServerPlayer* player)
{
    return contact_get_vb(player, "contact");
}

static void
contact_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    contact_vb(srv, "contact", CONTACT_NOT_STARTED);
    contact_vb(srv, "contact_people_vis", 0);
    contact_vb(srv, "contact_discussed_menaphos", 0);
    contact_vb(srv, "contact_told_priest", 0);
    contact_vb(srv, "contact_found_kaleef", 0);
    contact_vb(srv, "contact_met_maisa", 0);
    contact_vb(srv, "contact_maisa_ans", 0);
    contact_vb(srv, "contact_osman_met", 0);
    contact_vb(srv, "contact_osman_told", 0);
    contact_vb(srv, "contact_been_downstairs", 0);
    contact_vb(srv, "contact_never_had_keris", 0);
    contact_vb(srv, "bcs", 0);
}

static void
contact_prereqs(struct ToriRSServer* srv, int prince, int ics)
{
    assert(srv);
    contact_varp(srv, "princequest", prince ? CONTACT_PRINCE_COMPLETE : 0);
    contact_vb(srv, "ics_little_var", ics ? CONTACT_ICS_COMPLETE : 0);
    contact_vb(srv, "bcs", 0);
}

static void
contact_ready(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    contact_reset_quest(srv);
    contact_clear_inv(player);
    contact_prereqs(srv, 1, 1);
    contact_god(player);
}

static void
contact_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
contact_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    contact_talk(srv, npc_type, slot);
    contact_finish(srv);
}

static void
contact_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    contact_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        contact_click_until_menu(srv, 24);
        contact_pick_row(srv, rows[i]);
    }
    contact_finish(srv);
}

static int
contact_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    contact_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
contact_op_loc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    contact_finish(srv);
}

static void
contact_op_held(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    contact_finish(srv);
}

static void
contact_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    npc->combat_target = srv->active_player->pid;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    contact_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    contact_finish(srv);
}

static void
contact_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,contact_journal]", NULL, 0);
    contact_finish(srv);
    contact_pass(step);
}

static void
selftest_quest_contact(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_priest;
    int npc_jex;
    int npc_maisa;
    int npc_osman_ak;
    int npc_osman_desert;
    int npc_osman_cave;
    int npc_scarab;
    int loc_trapdoor;
    int loc_ladder;
    int loc_kaleef;
    int obj_scroll;
    int obj_keris;
    int obj_lamp;
    int obj_pot;
    int stat_thieve;
    int varp_qp;
    int slot;
    int loc_slot;
    int thieve_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };
    static const int k_wrong_then_wedge[] = { 1, 3 };

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: contact! critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer contact selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    contact_god(player);
    contact_reset_quest(srv);
    contact_clear_inv(player);

    npc_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_hipriest_vis");
    if( npc_priest <= 0 )
        npc_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_hipriest_town");
    npc_jex = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "contact_jex");
    npc_maisa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "contact_maisa_multi");
    if( npc_maisa <= 0 )
        npc_maisa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "contact_maisa");
    npc_osman_ak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "contact_osman_multi");
    npc_osman_desert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "contact_osman_desert_multi");
    npc_osman_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "contact_osman_cave_instance");
    npc_scarab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "contact_scarab_boss");
    loc_trapdoor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "contact_temple_trapdoor_open");
    loc_ladder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "contact_ladder_barricaded");
    loc_kaleef = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "contact_dead_body_kaleef_vis");
    obj_scroll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "contact_kaleef_scroll");
    obj_keris = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "contact_keris");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    stat_thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    varp_qp = ToriRSServer_WorldVarp("qp");
    if( varp_qp < 0 )
        varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_contact") > 0,
                   "dbrow quest_contact should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "contact") >= 0,
                   "varbit contact should resolve");
    SELFTEST_CHECK(npc_priest > 0, "npc High Priest vis/town should resolve");
    SELFTEST_CHECK(npc_jex > 0, "npc contact_jex should resolve");
    SELFTEST_CHECK(npc_maisa > 0, "npc contact_maisa should resolve");
    SELFTEST_CHECK(npc_osman_ak > 0, "npc contact_osman_multi should resolve");
    SELFTEST_CHECK(npc_osman_desert > 0, "npc contact_osman_desert_multi should resolve");
    SELFTEST_CHECK(npc_osman_cave > 0, "npc contact_osman_cave_instance should resolve");
    SELFTEST_CHECK(npc_scarab > 0, "npc contact_scarab_boss should resolve");
    SELFTEST_CHECK(loc_trapdoor > 0, "loc contact_temple_trapdoor_open should resolve");
    SELFTEST_CHECK(loc_ladder > 0, "loc contact_ladder_barricaded should resolve");
    SELFTEST_CHECK(loc_kaleef > 0, "loc contact_dead_body_kaleef_vis should resolve");
    SELFTEST_CHECK(obj_scroll > 0, "obj contact_kaleef_scroll should resolve");
    SELFTEST_CHECK(obj_keris > 0, "obj contact_keris should resolve");
    SELFTEST_CHECK(obj_lamp > 0, "obj thosf_reward_lamp should resolve");

    contact_journal(srv, "journal_0_not_started");

    /* Qualify-fail: ICS complete so the BCS dispatcher reaches Contact;
     * Prince Ali Rescue off so ~contact_meets_requirements is false. */
    slot = contact_spawn(srv, npc_priest, CONTACT_PRIEST_X, CONTACT_PRIEST_Z, 0);
    SELFTEST_CHECK(slot >= 0, "High Priest should spawn");
    if( slot >= 0 )
    {
        contact_reset_quest(srv);
        contact_prereqs(srv, 0, 1);
        contact_talk_finish(srv, npc_priest, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_NOT_STARTED,
                       "qualify-fail must stay not_started, got %d",
                       contact_quest(player));
        contact_pass("opnpc1_priest_qualify_fail");

        contact_ready(srv, player);
        contact_talk_rows(srv, npc_priest, slot, k_refuse, 1);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_NOT_STARTED,
                       "priest refuse must stay not_started");
        contact_pass("opnpc1_priest_refuse");

        contact_talk_rows(srv, npc_priest, slot, k_accept, 1);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_TOLD_JEX,
                       "priest accept must write told_jex, got %d",
                       contact_quest(player));
        contact_pass("opnpc1_priest_accept");

        contact_talk_finish(srv, npc_priest, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_TOLD_JEX,
                       "seek-jex reminder must stay told_jex");
        contact_pass("opnpc1_priest_reminder_jex");
    }
    contact_journal(srv, "journal_30_told_jex");

    contact_free_npc(srv, slot);
    slot = contact_spawn(srv, npc_jex, CONTACT_JEX_X, CONTACT_JEX_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Jex should spawn");
    if( slot >= 0 )
    {
        contact_ready(srv, player);
        contact_talk_finish(srv, npc_jex, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_NOT_STARTED,
                       "early Jex must stay not_started");
        contact_pass("opnpc1_jex_early_busy");

        contact_vb(srv, "contact", CONTACT_TOLD_JEX);
        contact_talk_rows(srv, npc_jex, slot, k_refuse, 1);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_TOLD_JEX,
                       "Jex prepare-first must stay told_jex");
        contact_pass("opnpc1_jex_prepare");

        contact_talk_rows(srv, npc_jex, slot, k_accept, 1);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_INVESTIGATING,
                       "Jex go-down must write investigating, got %d",
                       contact_quest(player));
        SELFTEST_CHECK(contact_get_vb(player, "contact_people_vis") == 1,
                       "Jex go-down must open the trapdoor vis flag");
        contact_pass("opnpc1_jex_go_down");

        contact_talk_finish(srv, npc_jex, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_INVESTIGATING,
                       "watch-yourself must stay investigating");
        contact_pass("opnpc1_jex_watch_yourself");
    }
    contact_journal(srv, "journal_40_investigating");

    loc_slot = contact_place_loc(srv, loc_trapdoor, CONTACT_JEX_X + 2, CONTACT_JEX_Z, 0);
    SELFTEST_CHECK(loc_slot >= 0, "temple trapdoor should place");
    if( loc_slot >= 0 )
    {
        contact_vb(srv, "contact", CONTACT_NOT_STARTED);
        contact_op_loc(srv, loc_trapdoor, loc_slot);
        SELFTEST_CHECK(player->x == CONTACT_JEX_X + 2 && player->z == CONTACT_JEX_Z,
                       "barricaded trapdoor must not teleport");
        contact_pass("oploc1_trapdoor_barricaded");

        contact_vb(srv, "contact", CONTACT_INVESTIGATING);
        contact_op_loc(srv, loc_trapdoor, loc_slot);
        SELFTEST_CHECK(player->x == CONTACT_BANK_X && player->z == CONTACT_BANK_Z,
                       "open trapdoor must tele to bank cellar %d,%d got %d,%d",
                       CONTACT_BANK_X, CONTACT_BANK_Z, player->x, player->z);
        contact_pass("oploc1_trapdoor_climb");
    }

    loc_slot = contact_place_loc(srv, loc_ladder, CONTACT_BANK_X, CONTACT_BANK_Z, 0);
    SELFTEST_CHECK(loc_slot >= 0, "bank cellar ladder should place");
    if( loc_slot >= 0 )
    {
        contact_vb(srv, "contact", CONTACT_NOT_STARTED);
        contact_op_loc(srv, loc_ladder, loc_slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_NOT_STARTED,
                       "early ladder must stay not_started");
        contact_pass("oploc1_ladder_too_early");

        contact_vb(srv, "contact", CONTACT_INVESTIGATING);
        contact_op_loc(srv, loc_ladder, loc_slot);
        SELFTEST_CHECK(player->x == CONTACT_CHASM_X && player->z == CONTACT_CHASM_Z,
                       "maze ladder must tele to chasm %d,%d got %d,%d",
                       CONTACT_CHASM_X, CONTACT_CHASM_Z, player->x, player->z);
        SELFTEST_CHECK(contact_get_vb(player, "contact_been_downstairs") == 1,
                       "first chasm trip must set been_downstairs");
        contact_pass("oploc1_ladder_maze_tele");
    }

    loc_slot = contact_place_loc(srv, loc_kaleef, CONTACT_CHASM_X, CONTACT_CHASM_Z, 0);
    SELFTEST_CHECK(loc_slot >= 0, "Kaleef body should place");
    if( loc_slot >= 0 )
    {
        contact_ready(srv, player);
        contact_clear_inv(player);
        contact_op_loc(srv, loc_kaleef, loc_slot);
        SELFTEST_CHECK(contact_inv_total(player, obj_scroll) == 0,
                       "too-early Kaleef must not grant parchment");
        contact_pass("oploc1_kaleef_too_early");

        contact_vb(srv, "contact", CONTACT_INVESTIGATING);
        if( obj_pot > 0 )
        {
            int s;
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, obj_pot, 1);
            contact_op_loc(srv, loc_kaleef, loc_slot);
            SELFTEST_CHECK(contact_inv_total(player, obj_scroll) == 0,
                           "full inv must not grant parchment");
            contact_pass("oploc1_kaleef_inv_full");
        }

        contact_clear_inv(player);
        contact_op_loc(srv, loc_kaleef, loc_slot);
        SELFTEST_CHECK(contact_inv_total(player, obj_scroll) > 0,
                       "Kaleef search must grant parchment");
        SELFTEST_CHECK(contact_get_vb(player, "contact_found_kaleef") == 1,
                       "search must set found_kaleef");
        contact_pass("oploc1_kaleef_search");

        contact_op_loc(srv, loc_kaleef, loc_slot);
        SELFTEST_CHECK(contact_inv_total(player, obj_scroll) == 1,
                       "already-taken must stay at one parchment");
        contact_pass("oploc1_kaleef_already");

        contact_op_held(srv, obj_scroll);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_READ_PARCHMENT,
                       "read parchment must write read_parchment, got %d",
                       contact_quest(player));
        contact_pass("opheld1_parchment_read");
    }
    contact_journal(srv, "journal_50_read_parchment");

    contact_free_npc(srv, slot);
    slot = contact_spawn(srv, npc_maisa, CONTACT_CHASM_X, CONTACT_CHASM_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Maisa should spawn");
    if( slot >= 0 )
    {
        contact_ready(srv, player);
        contact_talk_finish(srv, npc_maisa, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_NOT_STARTED,
                       "early Maisa must stay not_started");
        contact_pass("opnpc1_maisa_early");

        contact_vb(srv, "contact", CONTACT_READ_PARCHMENT);
        contact_vb(srv, "contact_maisa_ans", 0);
        contact_talk_rows(srv, npc_maisa, slot, k_refuse, 1);
        SELFTEST_CHECK(contact_get_vb(player, "contact_maisa_ans") == 0,
                       "Maisa q1 wrong must stay ans=0");
        contact_pass("opnpc1_maisa_q1_wrong");

        contact_talk_rows(srv, npc_maisa, slot, k_accept, 1);
        SELFTEST_CHECK(contact_get_vb(player, "contact_maisa_ans") == 1,
                       "Maisa q1 correct must write ans=1, got %d",
                       contact_get_vb(player, "contact_maisa_ans"));
        contact_pass("opnpc1_maisa_q1_correct");

        contact_talk_rows(srv, npc_maisa, slot, k_refuse, 1);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_READ_PARCHMENT,
                       "Maisa q2 wrong must stay read_parchment");
        contact_pass("opnpc1_maisa_q2_wrong");

        contact_talk_rows(srv, npc_maisa, slot, k_accept, 1);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_MET_MAISA,
                       "Maisa q2 correct must write met_maisa, got %d",
                       contact_quest(player));
        contact_pass("opnpc1_maisa_q2_correct");

        contact_talk_finish(srv, npc_maisa, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_MET_MAISA,
                       "find-Osman reminder must stay met_maisa");
        contact_pass("opnpc1_maisa_find_osman");
    }
    contact_journal(srv, "journal_60_met_maisa");

    contact_free_npc(srv, slot);
    slot = contact_spawn(srv, npc_osman_ak, CONTACT_OSMAN_AK_X, CONTACT_OSMAN_AK_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Al Kharid Osman should spawn");
    if( slot >= 0 )
    {
        contact_ready(srv, player);
        contact_talk_finish(srv, npc_osman_ak, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_NOT_STARTED,
                       "early Osman must stay not_started");
        contact_pass("opnpc1_osman_ak_early");

        contact_vb(srv, "contact", CONTACT_MET_MAISA);
        contact_talk_rows(srv, npc_osman_ak, slot, k_wrong_then_wedge, 2);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_TOLD_OSMAN,
                       "Osman wedge must write told_osman, got %d",
                       contact_quest(player));
        SELFTEST_CHECK(contact_get_vb(player, "contact_osman_met") == 1,
                       "wedge must set osman_met");
        contact_pass("opnpc1_osman_ak_wedge");

        contact_talk_finish(srv, npc_osman_ak, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_TOLD_OSMAN,
                       "on-my-way talk must stay told_osman");
        contact_pass("opnpc1_osman_ak_on_way");
    }
    contact_journal(srv, "journal_70_told_osman");

    contact_free_npc(srv, slot);
    slot = contact_spawn(srv, npc_osman_desert, CONTACT_OSMAN_DESERT_X, CONTACT_OSMAN_DESERT_Z, 0);
    SELFTEST_CHECK(slot >= 0, "desert Osman should spawn");
    if( slot >= 0 )
    {
        contact_ready(srv, player);
        contact_talk_finish(srv, npc_osman_desert, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_NOT_STARTED,
                       "early desert Osman must stay not_started");
        contact_pass("opnpc1_osman_desert_early");

        contact_vb(srv, "contact", CONTACT_TOLD_OSMAN);
        contact_talk_rows(srv, npc_osman_desert, slot, k_refuse, 1);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_TOLD_OSMAN,
                       "not-sure must stay told_osman");
        contact_pass("opnpc1_osman_desert_not_sure");

        contact_talk_rows(srv, npc_osman_desert, slot, k_accept, 1);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_OSMAN_OUTSIDE,
                       "secret entrance must write osman_outside, got %d",
                       contact_quest(player));
        contact_pass("opnpc1_osman_desert_secret");

        contact_talk_finish(srv, npc_osman_desert, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_OSMAN_OUTSIDE,
                       "catch-up talk must stay osman_outside");
        contact_pass("opnpc1_osman_desert_catch_up");
    }
    contact_journal(srv, "journal_80_osman_outside");

    contact_free_npc(srv, slot);
    slot = contact_spawn(srv, npc_priest, CONTACT_PRIEST_X, CONTACT_PRIEST_Z, 0);
    if( slot >= 0 )
    {
        contact_vb(srv, "contact", CONTACT_OSMAN_OUTSIDE);
        contact_talk_finish(srv, npc_priest, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_OSMAN_OUTSIDE,
                       "help-Osman reminder must stay osman_outside");
        contact_pass("opnpc1_priest_reminder_help_osman");
    }
    contact_free_npc(srv, slot);

    loc_slot = contact_place_loc(srv, loc_ladder, CONTACT_BANK_X, CONTACT_BANK_Z, 0);
    if( loc_slot >= 0 )
    {
        contact_vb(srv, "contact", CONTACT_OSMAN_OUTSIDE);
        contact_op_loc(srv, loc_ladder, loc_slot);
        SELFTEST_CHECK(player->x == CONTACT_CHASM_X && player->z == CONTACT_CHASM_Z,
                       "scarab-trip ladder must tele to chasm");
        contact_pass("oploc1_ladder_scarab_trip");
    }

    contact_free_type(srv, npc_scarab);
    slot = contact_spawn(srv, npc_scarab, CONTACT_CHASM_X, CONTACT_CHASM_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Giant Scarab should spawn");
    if( slot >= 0 )
    {
        contact_vb(srv, "contact", CONTACT_OSMAN_OUTSIDE);
        contact_kill(srv, npc_scarab, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_SCARAB_KILLED,
                       "Scarab death must write scarab_killed, got %d",
                       contact_quest(player));
        contact_pass("ai_queue3_scarab");
    }
    contact_free_npc(srv, slot);
    contact_journal(srv, "journal_100_scarab_killed");

    slot = contact_spawn(srv, npc_osman_cave, CONTACT_CHASM_X, CONTACT_CHASM_Z, 0);
    SELFTEST_CHECK(slot >= 0, "cave Osman should spawn");
    if( slot >= 0 )
    {
        contact_vb(srv, "contact", CONTACT_OSMAN_OUTSIDE);
        contact_talk_finish(srv, npc_osman_cave, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_OSMAN_OUTSIDE,
                       "still-scarab talk must stay osman_outside");
        contact_pass("opnpc1_osman_cave_still_scarab");

        contact_vb(srv, "contact", CONTACT_SCARAB_KILLED);
        contact_clear_inv(player);
        contact_talk_finish(srv, npc_osman_cave, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_READY_TO_FINISH,
                       "cave Osman without keris must write ready_to_finish, got %d",
                       contact_quest(player));
        SELFTEST_CHECK(contact_get_vb(player, "contact_never_had_keris") == 1,
                       "no-keris talk must set never_had_keris");
        contact_pass("opnpc1_osman_cave_never_had_keris");
    }
    contact_journal(srv, "journal_120_ready_to_finish");

    contact_free_npc(srv, slot);
    slot = contact_spawn(srv, npc_osman_cave, CONTACT_CHASM_X, CONTACT_CHASM_Z, 0);
    if( slot >= 0 )
    {
        contact_vb(srv, "contact", CONTACT_SCARAB_KILLED);
        contact_vb(srv, "contact_never_had_keris", 0);
        contact_clear_inv(player);
        if( obj_keris > 0 )
            contact_give(player, obj_keris, 1);
        contact_talk_finish(srv, npc_osman_cave, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_READY_TO_FINISH,
                       "cave Osman with keris must write ready_to_finish, got %d",
                       contact_quest(player));
        contact_pass("opnpc1_osman_cave_keris");
    }

    contact_free_npc(srv, slot);
    slot = contact_spawn(srv, npc_priest, CONTACT_PRIEST_X, CONTACT_PRIEST_Z, 0);
    thieve_before = (stat_thieve >= 0) ? player->stat_xp_tenths[stat_thieve] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
    if( slot >= 0 )
    {
        contact_vb(srv, "contact", CONTACT_READY_TO_FINISH);
        contact_prereqs(srv, 1, 1);
        contact_clear_inv(player);
        contact_talk_finish(srv, npc_priest, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_COMPLETE,
                       "finale hand-in must write complete, got %d",
                       contact_quest(player));
        if( stat_thieve >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_thieve] >=
                               thieve_before + CONTACT_REWARD_THIEVING_TENTHS,
                           "complete must advance Thieving by 70000 tenths");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + CONTACT_REWARD_QP,
                           "complete must award 1 QP");
        if( obj_lamp > 0 )
            SELFTEST_CHECK(contact_inv_total(player, obj_lamp) >= CONTACT_REWARD_LAMP_QTY,
                           "complete must grant 2 combat lamps, got %d",
                           contact_inv_total(player, obj_lamp));
        contact_pass("opnpc1_priest_finale_complete");

        contact_talk_finish(srv, npc_priest, slot);
        SELFTEST_CHECK(contact_quest(player) == CONTACT_COMPLETE,
                       "already-complete talk must stay complete");
        contact_pass("opnpc1_priest_post_complete");
    }
    contact_journal(srv, "journal_130_complete");

    contact_free_npc(srv, slot);
    contact_free_type(srv, npc_scarab);
    contact_free_type(srv, npc_jex);
    contact_free_type(srv, npc_maisa);
    contact_free_type(srv, npc_osman_ak);
    contact_free_type(srv, npc_osman_desert);
    contact_free_type(srv, npc_osman_cave);
    contact_clear_inv(player);
    contact_reset_quest(srv);
    fprintf(stderr, "ToriRSServer contact selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_CONTACT_SELFTEST_U_H */
