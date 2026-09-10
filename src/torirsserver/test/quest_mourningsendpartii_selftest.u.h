#ifndef TORIRSSERVER_TEST_QUEST_MOURNINGSENDPARTII_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_MOURNINGSENDPARTII_SELFTEST_U_H

/* Mourning's End Part II Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned npcs cannot leak. Real OPNPC1 on the
 * authored path -- not ::mend2run. player->godmode = 1 for the whole walk
 * (no death test). Completion goes through Arianwyn's authored
 * ~mend2_quest_complete / ~quest_complete_rewards(quest_mourningsendpart2).
 * Additive MEP2 only -- do not rewrite MEP1 sheep/toad/naphtha/disguise,
 * Sheep Herder, Roving Elves, Plague City Elena, SoTE, Wanted!, or POH. */

#define MEP2_NOT_STARTED 0
#define MEP2_BRIEFED 10
#define MEP2_ESSYLLT_TASK 20
#define MEP2_CRYSTAL_GIVEN 30
#define MEP2_PUZZLE_DONE 40
#define MEP2_REPORT 50
#define MEP2_COMPLETE 60

#define MEP1_COMPLETE 9

#define MEP2_REWARD_AGILITY 600000
#define MEP2_REWARD_QP 2

#define MEP2_ARIANWYN_X 2353
#define MEP2_ARIANWYN_Z 3172
#define MEP2_ESSYLLT_X 2044
#define MEP2_ESSYLLT_Z 4628

static void
mep2_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "MEP2 PASS: %s\n", step);
}

static void
mep2_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
mep2_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
mep2_finish(struct ToriRSServer* srv)
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
mep2_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
mep2_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mep2_chatmenu();
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
mep2_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mep2_chatmenu();
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
mep2_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    mep2_god(player);
    selftest_tick(srv);
}

static int
mep2_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    mep2_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
mep2_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
mep2_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
mep2_worn_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        if( player->worn[s].obj_id == obj_id )
            n += player->worn[s].count;
    return n;
}

static void
mep2_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( mep2_inv_total(player, obj_id) >= count )
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
mep2_set_var(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;
    int bit;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 && srv->active_player )
    {
        srv->active_player->varps[varp] = value;
        return;
    }
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mep2_get_var(struct ToriRSServerPlayer* player, const char* name)
{
    int varp;
    int bit;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        return player->varps[varp];
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
mep2_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mep2_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
mep2_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
mep2_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    mep2_talk(srv, npc_type, slot);
    mep2_finish(srv);
}

static void
mep2_talk_refuse(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    mep2_talk(srv, npc_type, slot);
    mep2_click_until_menu(srv, 16);
    mep2_pick_row(srv, 2);
    mep2_finish(srv);
}

static void
mep2_reset_state(struct ToriRSServer* srv)
{
    assert(srv);
    mep2_set_var(srv, "mourning_quest", 0);
    mep2_set_bit(srv, "mourning_quest_main", MEP2_NOT_STARTED);
    mep2_set_var(srv, "sote", 0);
}

static void
mep2_force_part1_complete(struct ToriRSServer* srv)
{
    assert(srv);
    mep2_set_var(srv, "mourning_quest", MEP1_COMPLETE);
    mep2_set_var(srv, "sote", 0);
}

static void
mep2_wear_disguise(
    struct ToriRSServerPlayer* player,
    int obj_mask,
    int obj_top,
    int obj_legs,
    int obj_cloak,
    int obj_boots,
    int obj_gloves)
{
    assert(player);
    if( obj_mask > 0 )
        worn_set(player, TORIRSSERVER_WEAR_HEAD, obj_mask, 1);
    if( obj_cloak > 0 )
        worn_set(player, TORIRSSERVER_WEAR_CAPE, obj_cloak, 1);
    if( obj_top > 0 )
        worn_set(player, TORIRSSERVER_WEAR_BODY, obj_top, 1);
    if( obj_legs > 0 )
        worn_set(player, TORIRSSERVER_WEAR_LEGS, obj_legs, 1);
    if( obj_gloves > 0 )
        worn_set(player, TORIRSSERVER_WEAR_HANDS, obj_gloves, 1);
    if( obj_boots > 0 )
        worn_set(player, TORIRSSERVER_WEAR_FEET, obj_boots, 1);
}

static void
selftest_quest_mourningsendpartii(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_agility;
    int npc_arianwyn;
    int npc_essyllt;
    int obj_journal;
    int obj_sample;
    int obj_new_sample;
    int obj_trinket;
    int obj_talisman;
    int obj_chisel;
    int obj_rope;
    int obj_mask;
    int obj_top;
    int obj_legs;
    int obj_cloak;
    int obj_boots;
    int obj_gloves;
    int varp_qp;
    int slot;
    int agility_xp_before;
    int qp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: mourningsendpartii critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer mep2 selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    mep2_god(player);

    stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    npc_arianwyn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mourning_arianwyn");
    npc_essyllt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mourner_hideout_head_mourner");
    obj_journal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_ederns_journal");
    obj_sample = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_crystal_sample");
    obj_new_sample = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_crystal_new_sample");
    obj_trinket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_crystal_trinket");
    obj_talisman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "death_talisman");
    obj_chisel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chisel");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_mask = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gasmask");
    obj_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_top");
    obj_legs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_legs");
    obj_cloak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_cloak");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_boots");
    obj_gloves = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_gloves");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "mourning_quest") >= 0,
                   "varp mourning_quest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mourning_quest_main") >= 0,
                   "varbit mourning_quest_main should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "mourning_quest_part2") >= 0,
                   "varp mourning_quest_part2 should resolve");
    SELFTEST_CHECK(npc_arianwyn > 0, "npc mourning_arianwyn should resolve");
    SELFTEST_CHECK(npc_essyllt > 0, "npc mourner_hideout_head_mourner should resolve");
    SELFTEST_CHECK(obj_trinket > 0, "obj mourning_crystal_trinket should resolve");
    SELFTEST_CHECK(obj_talisman > 0, "obj death_talisman should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_mourningsendpart2") > 0,
                   "dbrow quest_mourningsendpart2 should resolve");
    if( npc_arianwyn <= 0 || npc_essyllt <= 0 )
    {
        fprintf(stderr, "ToriRSServer mep2 selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    mep2_clear_inv(player);
    mep2_reset_state(srv);
    mep2_god(player);

    /* ---- Arianwyn too-early: Part I incomplete (owned trigger, not MEP2) ---- */
    slot = mep2_spawn(srv, npc_arianwyn, MEP2_ARIANWYN_X, MEP2_ARIANWYN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Arianwyn should spawn");
    if( slot >= 0 )
    {
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_NOT_STARTED,
                       "Arianwyn with Part I incomplete must not start MEP2");
        SELFTEST_CHECK(mep2_get_var(player, "mourning_quest") == 0,
                       "Arianwyn too-early must leave Part I unstarted");
        mep2_pass("opnpc1_arianwyn_too_early_part1_incomplete");

        /* ---- Offer + refuse + accept once Part I is genuinely complete ---- */
        mep2_force_part1_complete(srv);
        mep2_set_bit(srv, "mourning_quest_main", MEP2_NOT_STARTED);
        mep2_talk_refuse(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_NOT_STARTED,
                       "Arianwyn refuse must leave MEP2 unstarted, got %d",
                       mep2_get_bit(player, "mourning_quest_main"));
        mep2_pass("opnpc1_arianwyn_offer_refuse");

        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_BRIEFED,
                       "Arianwyn accept must write briefed, got %d",
                       mep2_get_bit(player, "mourning_quest_main"));
        mep2_pass("opnpc1_arianwyn_offer_accept");

        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_BRIEFED,
                       "Arianwyn briefed reminder must stay briefed");
        mep2_pass("opnpc1_arianwyn_briefed_reminder");
        mep2_free_npc(srv, slot);
    }

    /* ---- Essyllt too-early / assignment / after ---- */
    slot = mep2_spawn(srv, npc_essyllt, MEP2_ESSYLLT_X, MEP2_ESSYLLT_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Essyllt should spawn");
    if( slot >= 0 )
    {
        mep2_reset_state(srv);
        mep2_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_NOT_STARTED,
                       "Essyllt with Part I incomplete must not start MEP2");
        mep2_pass("opnpc1_essyllt_too_early_part1_incomplete");

        mep2_force_part1_complete(srv);
        mep2_set_bit(srv, "mourning_quest_main", MEP2_NOT_STARTED);
        mep2_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_NOT_STARTED,
                       "Essyllt before Arianwyn briefing must send the player back");
        mep2_pass("opnpc1_essyllt_too_early_not_briefed");

        mep2_set_bit(srv, "mourning_quest_main", MEP2_BRIEFED);
        mep2_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_ESSYLLT_TASK,
                       "Essyllt assignment must write essyllt_task, got %d",
                       mep2_get_bit(player, "mourning_quest_main"));
        mep2_pass("opnpc1_essyllt_assignment");

        mep2_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_ESSYLLT_TASK,
                       "Essyllt after assignment must stay at essyllt_task");
        mep2_pass("opnpc1_essyllt_after");
        mep2_free_npc(srv, slot);
    }

    /* ---- Crystal recovery + Eluned-folded hand-off (state 20 -> 30) ---- */
    mep2_force_part1_complete(srv);
    mep2_set_bit(srv, "mourning_quest_main", MEP2_ESSYLLT_TASK);
    mep2_clear_inv(player);
    slot = mep2_spawn(srv, npc_arianwyn, MEP2_ARIANWYN_X, MEP2_ARIANWYN_Z, 0);
    if( slot >= 0 )
    {
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_CRYSTAL_GIVEN,
                       "Arianwyn crystal hand-in must write crystal_given, got %d",
                       mep2_get_bit(player, "mourning_quest_main"));
        SELFTEST_CHECK(obj_journal <= 0 || mep2_inv_total(player, obj_journal) == 1,
                       "crystal hand-in must grant Edern's journal");
        SELFTEST_CHECK(obj_sample <= 0 || mep2_inv_total(player, obj_sample) == 0,
                       "blackened sample must be handed in");
        SELFTEST_CHECK(obj_new_sample <= 0 || mep2_inv_total(player, obj_new_sample) == 1,
                       "Arianwyn must shape a fresh crystal sample");
        mep2_pass("opnpc1_arianwyn_crystal_handin");

        /* ---- Missing-item gates at the collapsed temple interaction ---- */
        mep2_clear_inv(player);
        SELFTEST_CHECK(mep2_worn_total(player, obj_mask) == 0,
                       "disguise gate starts with no gasmask worn");
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_CRYSTAL_GIVEN,
                       "missing disguise must not advance the temple collapse");
        mep2_pass("opnpc1_arianwyn_missing_disguise");

        mep2_wear_disguise(player, obj_mask, obj_top, obj_legs, obj_cloak, obj_boots, obj_gloves);
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_CRYSTAL_GIVEN,
                       "missing chisel must not advance the temple collapse");
        mep2_pass("opnpc1_arianwyn_missing_chisel");

        if( obj_chisel > 0 )
            mep2_give(player, obj_chisel, 1);
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_CRYSTAL_GIVEN,
                       "missing rope must not advance the temple collapse");
        mep2_pass("opnpc1_arianwyn_missing_rope");

        if( obj_rope > 0 )
            mep2_give(player, obj_rope, 1);
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_CRYSTAL_GIVEN,
                       "missing crystal must not advance the temple collapse");
        mep2_pass("opnpc1_arianwyn_missing_crystal");

        if( obj_new_sample > 0 )
            mep2_give(player, obj_new_sample, 1);
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_CRYSTAL_GIVEN,
                       "missing death talisman must not advance the temple collapse");
        mep2_pass("opnpc1_arianwyn_missing_talisman");

        /* Full kit: authored Temple of Light + Death Altar collapse (30 -> 40). */
        if( obj_talisman > 0 )
            mep2_give(player, obj_talisman, 1);
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_PUZZLE_DONE,
                       "full-kit Arianwyn must narrate the maze collapse to puzzle_done, got %d",
                       mep2_get_bit(player, "mourning_quest_main"));
        SELFTEST_CHECK(obj_rope <= 0 || mep2_inv_total(player, obj_rope) == 0,
                       "collapsed temple must consume the rope");
        SELFTEST_CHECK(obj_new_sample <= 0 || mep2_inv_total(player, obj_new_sample) == 0,
                       "collapsed temple must consume the uncharged crystal");
        SELFTEST_CHECK(obj_chisel <= 0 || mep2_inv_total(player, obj_chisel) == 1,
                       "chisel must not be consumed");
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_temple_parts_1") == 0,
                       "unused mourning_temple_parts_1 must stay untouched");
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_light_temple_1_1_east") == 0,
                       "unused mourning_light_temple_1_1_east must stay untouched");
        mep2_pass("opnpc1_arianwyn_puzzle_narration");

        /* Report (40 -> 50). */
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_REPORT,
                       "Arianwyn report must write report, got %d",
                       mep2_get_bit(player, "mourning_quest_main"));
        mep2_pass("opnpc1_arianwyn_report");

        /* Authored complete scroll (50 -> 60). */
        agility_xp_before = (stat_agility >= 0) ? player->stat_xp_tenths[stat_agility] : 0;
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        mep2_clear_inv(player);
        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_COMPLETE,
                       "Arianwyn complete must write endstate 60, got %d",
                       mep2_get_bit(player, "mourning_quest_main"));
        SELFTEST_CHECK(obj_trinket <= 0 || mep2_inv_total(player, obj_trinket) == 1,
                       "complete must grant the crystal trinket");
        SELFTEST_CHECK(obj_talisman <= 0 || mep2_inv_total(player, obj_talisman) == 1,
                       "complete must grant a death talisman");
        if( stat_agility >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_agility] >= agility_xp_before + MEP2_REWARD_AGILITY,
                           "complete must award 60000 Agility XP");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + MEP2_REWARD_QP,
                           "complete must award 2 quest points");
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_temple_parts_1") == 0,
                       "complete must still leave mourning_temple_parts_1 unused");
        mep2_pass("opnpc1_arianwyn_complete_scroll");

        mep2_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep2_get_bit(player, "mourning_quest_main") == MEP2_COMPLETE,
                       "post-complete Arianwyn must stay complete");
        mep2_pass("opnpc1_arianwyn_post_complete");
        mep2_free_npc(srv, slot);
    }

    /* Fence spawned npcs before returning to the host stanza. */
    selftest_reset_world(srv, player, 402, 402);

    fprintf(stderr, "ToriRSServer mep2 selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_MOURNINGSENDPARTII_SELFTEST_U_H */
