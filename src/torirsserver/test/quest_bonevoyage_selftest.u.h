#ifndef TORIRSSERVER_TEST_QUEST_BONEVOYAGE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_BONEVOYAGE_SELFTEST_U_H

/* Bone Voyage Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Haig / Foreman / navigators cannot leak.
 * Real OPNPC1 on the authored path (foreman, sawmills, barge guard,
 * navigators, Jack, Odd Old Man, Apothecary). Start qualify / refuse /
 * accept go through ~bv_haig_talk, the MERGE splice from
 * [opnpc1,curator]. player->godmode = 1 for the whole walk (no death
 * case). Completion goes through ~bv_quest_complete via the Lead
 * Navigator sail branch.
 *
 * Qualify-fail is split before the offer: Dig Site
 * (`~itexam_progress() >= 9`), `%vm_kudos >= 100`, Woodcutting 60.
 * Offer is p_choice2 Yes / Not now. Do not auto-start.
 *
 * Gate: TORIRSSERVER_SELFTEST_BV_ONLY=1
 */

#define BV_NOT_STARTED 0
#define BV_FOREMAN 5
#define BV_SAWMILL 10
#define BV_GUILD 11
#define BV_RETURN_AGREE 15
#define BV_FOREMAN2 20
#define BV_LEAD 21
#define BV_JACK 22
#define BV_LEAD2 23
#define BV_ITEMS 25
#define BV_SAIL 30
#define BV_COMPLETE 50

#define BV_CHARM_GOT 1
#define BV_CHARM_GIVEN 2
#define BV_POT_TALKED 1
#define BV_POT_GOT 2
#define BV_POT_GIVEN 3

#define BV_ITEXAM_COMPLETE 9
#define BV_KUDOS_REQ 100
#define BV_WC_REQ 60

#define BV_HAIG_X 3257
#define BV_HAIG_Z 3448
#define BV_FOREMAN_X 3364
#define BV_FOREMAN_Z 3445
#define BV_SAWMILL_X 3302
#define BV_SAWMILL_Z 3492
#define BV_GUILD_X 1620
#define BV_GUILD_Z 3499
#define BV_BARGE_X 3362
#define BV_BARGE_Z 3446
#define BV_LEAD_X 3363
#define BV_LEAD_Z 3453
#define BV_JACK_X 3050
#define BV_JACK_Z 3257
#define BV_ODD_X 3360
#define BV_ODD_Z 3505
#define BV_APOTH_X 3195
#define BV_APOTH_Z 3405

static void
bv_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "BV PASS: %s\n", step);
}

static void
bv_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
bv_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
bv_finish(struct ToriRSServer* srv)
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
bv_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    bv_god(player);
    selftest_tick(srv);
}

static int
bv_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    bv_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
bv_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
bv_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, obj_id, 1);
}

static void
bv_set_wc(struct ToriRSServerPlayer* player, int woodcutting, int level)
{
    assert(player);
    if( woodcutting >= 0 )
    {
        player->stat_level[woodcutting] = level;
        player->stat_boosted[woodcutting] = level;
    }
}

static void
bv_reset_state(struct ToriRSServer* srv, struct ToriRSServerPlayer* player,
               int progress, int itexam, int kudos, int wc_level, int woodcutting,
               int vb_progress, int vb_charm, int vb_potion, int vb_kudos,
               int vp_itexam)
{
    assert(srv);
    assert(player);
    bv_clear_inv(player);
    if( vb_progress >= 0 )
        ToriRSServer_VarbitSet(srv, vb_progress, progress);
    if( vb_charm >= 0 )
        ToriRSServer_VarbitSet(srv, vb_charm, 0);
    if( vb_potion >= 0 )
        ToriRSServer_VarbitSet(srv, vb_potion, 0);
    if( vb_kudos >= 0 )
        ToriRSServer_VarbitSet(srv, vb_kudos, kudos);
    if( vp_itexam >= 0 )
        ToriRSServer_WorldSetVarp(srv, vp_itexam, itexam);
    bv_set_wc(player, woodcutting, wc_level);
    bv_god(player);
}

static int
bv_haig(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    assert(slot >= 0);
    return ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,bv_haig_talk]", slot);
}

static void
bv_talk_and_pick(struct ToriRSServer* srv, struct ToriRSServerPlayer* player,
                 int chatmenu, int row)
{
    assert(srv);
    assert(player);
    biohazard_run_dialogue(srv, player, chatmenu);
    if( player->active_script && chatmenu > 0 && row > 0 )
        selftest_charter_choose(srv, row);
    biohazard_run_dialogue(srv, player, chatmenu);
}

static void
selftest_quest_bonevoyage(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int vb_progress;
    int vb_charm;
    int vb_potion;
    int vb_kudos;
    int vp_itexam;
    int woodcutting;
    int npc_curator;
    int npc_foreman;
    int npc_sawmill;
    int npc_guild;
    int npc_guard;
    int npc_lead;
    int npc_junior;
    int npc_jack;
    int npc_odd;
    int npc_apoth;
    int obj_proposal;
    int obj_agreement;
    int obj_charm;
    int obj_potion;
    int obj_marr;
    int obj_vodka;
    int obj_pot;
    int chatmenu;
    int slot_curator;
    int slot_foreman;
    int slot_sawmill;
    int slot_guild;
    int slot_guard;
    int slot_lead;
    int slot_junior;
    int slot_jack;
    int slot_odd;
    int slot_apoth;
    int rc;
    int progress;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: ::bonevoyage / Bone Voyage\n");

    loaded = srv->scripts_ok;
    if( !loaded )
    {
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    player->godmode = 1;
    srv->members_world = 1;

    vb_progress = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fossilquest_progress");
    vb_charm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fossilquest_lucky_charm");
    vb_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "fossilquest_potion");
    vb_kudos = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vm_kudos");
    vp_itexam = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "itexamlevel");
    woodcutting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    npc_curator = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "curator");
    npc_foreman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vm_canal_barge_foremen_talking");
    npc_sawmill = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "poh_sawmill_opp");
    npc_guild = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "prif_sawmill_operator");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fossilquest_barge_guard_port");
    npc_lead = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fossilquest_lead_navigator");
    npc_junior = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fossilquest_jr_navigator");
    npc_jack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sarim_pub_drinker_1");
    npc_odd = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rag_odd_old_man");
    npc_apoth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "apothecary");
    obj_proposal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fossilquest_sawmill_proposal");
    obj_agreement = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fossilquest_sawmill_agreement");
    obj_charm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fossilquest_bone_charm");
    obj_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fossilquest_potion");
    obj_marr = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "marrentillvial");
    obj_vodka = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vodka");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(vb_progress >= 0, "fossilquest_progress varbit should resolve");
    SELFTEST_CHECK(vb_kudos >= 0, "vm_kudos varbit should resolve");
    SELFTEST_CHECK(vp_itexam >= 0, "itexamlevel varp should resolve");
    SELFTEST_CHECK(npc_curator >= 0, "curator (Haig) should resolve");
    SELFTEST_CHECK(npc_foreman >= 0, "vm_canal_barge_foremen_talking should resolve");
    SELFTEST_CHECK(npc_sawmill >= 0, "poh_sawmill_opp should resolve");
    SELFTEST_CHECK(npc_guild >= 0, "prif_sawmill_operator should resolve");
    SELFTEST_CHECK(npc_lead >= 0, "fossilquest_lead_navigator should resolve");
    SELFTEST_CHECK(obj_proposal >= 0, "fossilquest_sawmill_proposal should resolve");
    if( vb_progress < 0 || npc_curator < 0 || npc_foreman < 0 || npc_sawmill < 0 ||
        npc_guild < 0 || npc_lead < 0 || obj_proposal < 0 )
    {
        fprintf(stderr, "  SKIP  missing Bone Voyage symbols\n");
        return;
    }

    slot_curator = bv_spawn(srv, npc_curator, BV_HAIG_X, BV_HAIG_Z, 0);
    slot_foreman = bv_spawn(srv, npc_foreman, BV_FOREMAN_X, BV_FOREMAN_Z, 0);
    slot_sawmill = bv_spawn(srv, npc_sawmill, BV_SAWMILL_X, BV_SAWMILL_Z, 0);
    slot_guild = bv_spawn(srv, npc_guild, BV_GUILD_X, BV_GUILD_Z, 0);
    slot_guard = npc_guard >= 0 ? bv_spawn(srv, npc_guard, BV_BARGE_X, BV_BARGE_Z, 0) : -1;
    slot_lead = bv_spawn(srv, npc_lead, BV_LEAD_X, BV_LEAD_Z, 1);
    slot_junior = npc_junior >= 0 ? bv_spawn(srv, npc_junior, BV_LEAD_X, BV_LEAD_Z + 1, 1) : -1;
    slot_jack = npc_jack >= 0 ? bv_spawn(srv, npc_jack, BV_JACK_X, BV_JACK_Z, 0) : -1;
    slot_odd = npc_odd >= 0 ? bv_spawn(srv, npc_odd, BV_ODD_X, BV_ODD_Z, 0) : -1;
    slot_apoth = npc_apoth >= 0 ? bv_spawn(srv, npc_apoth, BV_APOTH_X, BV_APOTH_Z, 0) : -1;
    SELFTEST_CHECK(slot_curator >= 0, "Curator Haig should spawn");
    SELFTEST_CHECK(slot_foreman >= 0, "Barge Foreman should spawn");
    SELFTEST_CHECK(slot_sawmill >= 0, "Varrock sawmill should spawn");
    SELFTEST_CHECK(slot_guild >= 0, "Guild sawmill should spawn");
    SELFTEST_CHECK(slot_lead >= 0, "Lead Navigator should spawn");

    /* ---- Qualify-fail: Dig Site ---- */
    bv_reset_state(srv, player, BV_NOT_STARTED, 0, BV_KUDOS_REQ, BV_WC_REQ, woodcutting,
                   vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    bv_tele(srv, BV_HAIG_X, BV_HAIG_Z, 0);
    rc = bv_haig(srv, slot_curator);
    bv_finish(srv);
    SELFTEST_CHECK(rc != 0, "bv_haig_talk Dig Site fail should run");
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_NOT_STARTED,
                   "missing Dig Site must not start Bone Voyage, got %d", progress);
    bv_pass("qualify_fail_digsite");

    /* ---- Qualify-fail: 100 kudos ---- */
    bv_reset_state(srv, player, BV_NOT_STARTED, BV_ITEXAM_COMPLETE, 0, BV_WC_REQ, woodcutting,
                   vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    bv_tele(srv, BV_HAIG_X, BV_HAIG_Z, 0);
    rc = bv_haig(srv, slot_curator);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_NOT_STARTED,
                   "missing 100 kudos must not start Bone Voyage, got %d", progress);
    bv_pass("qualify_fail_kudos");

    /* ---- Qualify-fail: Woodcutting 60 ---- */
    bv_reset_state(srv, player, BV_NOT_STARTED, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, 1,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    bv_tele(srv, BV_HAIG_X, BV_HAIG_Z, 0);
    rc = bv_haig(srv, slot_curator);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_NOT_STARTED,
                   "Woodcutting under 60 must not start Bone Voyage, got %d", progress);
    bv_pass("qualify_fail_woodcutting");

    /* ---- Refuse: Not now. ---- */
    bv_reset_state(srv, player, BV_NOT_STARTED, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, BV_WC_REQ,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    bv_tele(srv, BV_HAIG_X, BV_HAIG_Z, 0);
    rc = bv_haig(srv, slot_curator);
    bv_talk_and_pick(srv, player, chatmenu, 2);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_NOT_STARTED, "Not now must leave progress=0, got %d",
                   progress);
    bv_pass("haig_refuse");

    /* ---- Accept: Yes. ---- */
    bv_reset_state(srv, player, BV_NOT_STARTED, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, BV_WC_REQ,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    bv_tele(srv, BV_HAIG_X, BV_HAIG_Z, 0);
    rc = bv_haig(srv, slot_curator);
    bv_talk_and_pick(srv, player, chatmenu, 1);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_FOREMAN, "Yes should write progress=5, got %d", progress);
    bv_pass("haig_accept");

    /* ---- Foreman first talk ---- */
    bv_tele(srv, BV_FOREMAN_X, BV_FOREMAN_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_foreman, -1, slot_foreman);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_SAWMILL, "Foreman should write progress=10, got %d",
                   progress);
    bv_pass("foreman_first");

    /* ---- Varrock sawmill proposal ---- */
    bv_tele(srv, BV_SAWMILL_X, BV_SAWMILL_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sawmill, -1, slot_sawmill);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_GUILD, "Varrock sawmill should write progress=11, got %d",
                   progress);
    SELFTEST_CHECK(selftest_count_obj(player, obj_proposal) == 1,
                   "Varrock sawmill should grant the proposal, got %d",
                   selftest_count_obj(player, obj_proposal));
    bv_pass("sawmill_proposal");

    /* ---- Guild sawmill agreement ---- */
    bv_tele(srv, BV_GUILD_X, BV_GUILD_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guild, -1, slot_guild);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_RETURN_AGREE,
                   "Guild sawmill should write progress=15, got %d", progress);
    SELFTEST_CHECK(selftest_count_obj(player, obj_agreement) == 1,
                   "Guild sawmill should grant the agreement, got %d",
                   selftest_count_obj(player, obj_agreement));
    SELFTEST_CHECK(selftest_count_obj(player, obj_proposal) == 0,
                   "Guild sawmill should consume the proposal");
    bv_pass("guild_agreement");

    /* ---- Guild WC60 fail (in-dir gate label) ---- */
    bv_reset_state(srv, player, BV_GUILD, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, 1, woodcutting,
                   vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    if( obj_proposal > 0 )
        inv_set(player, 0, obj_proposal, 1);
    bv_tele(srv, BV_GUILD_X, BV_GUILD_Z, 0);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bv_try_guild_gate]", NULL, 0);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_GUILD, "WC under 60 must leave guild gate at 11, got %d",
                   progress);
    bv_pass("guild_gate_wc60");

    /* ---- Varrock sawmill hand-in ---- */
    bv_reset_state(srv, player, BV_RETURN_AGREE, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, BV_WC_REQ,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    if( obj_agreement > 0 )
        inv_set(player, 0, obj_agreement, 1);
    bv_tele(srv, BV_SAWMILL_X, BV_SAWMILL_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sawmill, -1, slot_sawmill);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_FOREMAN2, "agreement hand-in should write 20, got %d",
                   progress);
    SELFTEST_CHECK(selftest_count_obj(player, obj_agreement) == 0,
                   "agreement hand-in should consume the agreement");
    bv_pass("sawmill_handin");

    /* ---- Foreman return ---- */
    bv_tele(srv, BV_FOREMAN_X, BV_FOREMAN_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_foreman, -1, slot_foreman);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_LEAD, "Foreman return should write progress=21, got %d",
                   progress);
    bv_pass("foreman_return");

    /* ---- Barge guard not ready ---- */
    bv_reset_state(srv, player, BV_FOREMAN, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, BV_WC_REQ,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    if( npc_guard >= 0 && slot_guard >= 0 )
    {
        bv_tele(srv, BV_BARGE_X, BV_BARGE_Z, 0);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, slot_guard);
        bv_finish(srv);
        progress = ToriRSServer_VarbitGet(player, vb_progress);
        SELFTEST_CHECK(progress == BV_FOREMAN, "unready barge must leave progress=5, got %d",
                       progress);
        bv_pass("barge_guard_not_ready");
    }

    /* ---- Barge guard board ---- */
    bv_reset_state(srv, player, BV_LEAD, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, BV_WC_REQ,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    if( npc_guard >= 0 && slot_guard >= 0 )
    {
        bv_tele(srv, BV_BARGE_X, BV_BARGE_Z, 0);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, slot_guard);
        bv_finish(srv);
        bv_pass("barge_guard_board");
    }

    /* ---- Lead first talk → Jack ---- */
    bv_reset_state(srv, player, BV_LEAD, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, BV_WC_REQ,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    bv_tele(srv, BV_LEAD_X, BV_LEAD_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lead, -1, slot_lead);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_JACK, "Lead first talk should write progress=22, got %d",
                   progress);
    bv_pass("lead_first");

    /* ---- Jack advice ---- */
    if( npc_jack >= 0 && slot_jack >= 0 )
    {
        bv_tele(srv, BV_JACK_X, BV_JACK_Z, 0);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_jack, -1, slot_jack);
        bv_finish(srv);
        progress = ToriRSServer_VarbitGet(player, vb_progress);
        SELFTEST_CHECK(progress == BV_LEAD2, "Jack should write progress=23, got %d",
                       progress);
        bv_pass("jack_advice");
    }
    else
    {
        ToriRSServer_VarbitSet(srv, vb_progress, BV_LEAD2);
    }

    /* ---- Lead after Jack ---- */
    bv_tele(srv, BV_LEAD_X, BV_LEAD_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lead, -1, slot_lead);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_ITEMS, "Lead after Jack should write progress=25, got %d",
                   progress);
    bv_pass("lead_after_jack");

    /* ---- Odd Old Man bone charm ---- */
    if( npc_odd >= 0 && slot_odd >= 0 && obj_charm > 0 )
    {
        bv_tele(srv, BV_ODD_X, BV_ODD_Z, 0);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_odd, -1, slot_odd);
        bv_finish(srv);
        SELFTEST_CHECK(selftest_count_obj(player, obj_charm) == 1,
                       "Odd Old Man should grant a bone charm, got %d",
                       selftest_count_obj(player, obj_charm));
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_charm) == BV_CHARM_GOT,
                       "Odd Old Man should write charm=1, got %d",
                       ToriRSServer_VarbitGet(player, vb_charm));
        bv_pass("odd_charm");
    }
    else if( obj_charm > 0 )
    {
        inv_set(player, 0, obj_charm, 1);
        if( vb_charm >= 0 )
            ToriRSServer_VarbitSet(srv, vb_charm, BV_CHARM_GOT);
    }

    /* ---- Junior charm hand-in ---- */
    if( npc_junior >= 0 && slot_junior >= 0 && obj_charm > 0 )
    {
        bv_tele(srv, BV_LEAD_X, BV_LEAD_Z, 1);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_junior, -1,
                                           slot_junior);
        bv_finish(srv);
        SELFTEST_CHECK(selftest_count_obj(player, obj_charm) == 0,
                       "Junior should consume the bone charm");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_charm) == BV_CHARM_GIVEN,
                       "Junior should write charm=2, got %d",
                       ToriRSServer_VarbitGet(player, vb_charm));
        bv_pass("junior_charm_handin");
    }
    else if( vb_charm >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_charm, BV_CHARM_GIVEN);
        bv_clear_inv(player);
    }

    /* ---- Apothecary ask + brew ---- */
    if( npc_apoth >= 0 && slot_apoth >= 0 )
    {
        bv_tele(srv, BV_APOTH_X, BV_APOTH_Z, 0);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_apoth, -1, slot_apoth);
        bv_finish(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_potion) == BV_POT_TALKED,
                       "Apothecary ask should write potion=1, got %d",
                       ToriRSServer_VarbitGet(player, vb_potion));
        bv_pass("apoth_ask");

        if( obj_marr > 0 )
            inv_set(player, 0, obj_marr, 1);
        if( obj_vodka > 0 )
            inv_set(player, 1, obj_vodka, 2);
        bv_tele(srv, BV_APOTH_X, BV_APOTH_Z, 0);
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_apoth, -1, slot_apoth);
        bv_finish(srv);
        if( obj_potion > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, obj_potion) == 1,
                           "Apothecary should brew a potion of sealegs, got %d",
                           selftest_count_obj(player, obj_potion));
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_potion) == BV_POT_GOT,
                       "Apothecary brew should write potion=2, got %d",
                       ToriRSServer_VarbitGet(player, vb_potion));
        bv_pass("apoth_brew");
    }
    else if( obj_potion > 0 )
    {
        inv_set(player, 0, obj_potion, 1);
        if( vb_potion >= 0 )
            ToriRSServer_VarbitSet(srv, vb_potion, BV_POT_GOT);
        if( vb_charm >= 0 )
            ToriRSServer_VarbitSet(srv, vb_charm, BV_CHARM_GIVEN);
    }

    /* ---- Lead potion hand-in ---- */
    bv_tele(srv, BV_LEAD_X, BV_LEAD_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lead, -1, slot_lead);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_SAIL, "potion hand-in should write progress=30, got %d",
                   progress);
    bv_pass("lead_potion_handin");

    /* ---- Sail + authored complete scroll ---- */
    bv_tele(srv, BV_LEAD_X, BV_LEAD_Z, 1);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lead, -1, slot_lead);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_COMPLETE, "sail should complete progress=50, got %d",
                   progress);
    bv_pass("lead_sail_complete_scroll");

    /* ---- Post-complete Haig ---- */
    bv_tele(srv, BV_HAIG_X, BV_HAIG_Z, 0);
    bv_haig(srv, slot_curator);
    bv_finish(srv);
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_COMPLETE, "post-complete Haig must leave 50, got %d",
                   progress);
    bv_pass("haig_complete");

    /* ---- Full-inv proposal ---- */
    bv_reset_state(srv, player, BV_SAWMILL, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, BV_WC_REQ,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    if( obj_pot > 0 )
        bv_fill_inv(player, obj_pot);
    bv_tele(srv, BV_SAWMILL_X, BV_SAWMILL_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sawmill, -1, slot_sawmill);
    bv_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_proposal) == 0,
                   "full inv must not receive a sawmill proposal");
    progress = ToriRSServer_VarbitGet(player, vb_progress);
    SELFTEST_CHECK(progress == BV_SAWMILL, "full-inv proposal must leave progress=10, got %d",
                   progress);
    bv_pass("sawmill_proposal_full_inv");

    /* ---- Journal at authored states ---- */
    bv_reset_state(srv, player, BV_NOT_STARTED, BV_ITEXAM_COMPLETE, BV_KUDOS_REQ, BV_WC_REQ,
                   woodcutting, vb_progress, vb_charm, vb_potion, vb_kudos, vp_itexam);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bonevoyage_journal]", NULL, 0);
    bv_finish(srv);
    bv_pass("journal_not_started");

    ToriRSServer_VarbitSet(srv, vb_progress, BV_FOREMAN);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bonevoyage_journal]", NULL, 0);
    bv_finish(srv);
    bv_pass("journal_foreman");

    ToriRSServer_VarbitSet(srv, vb_progress, BV_ITEMS);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bonevoyage_journal]", NULL, 0);
    bv_finish(srv);
    bv_pass("journal_items");

    ToriRSServer_VarbitSet(srv, vb_progress, BV_COMPLETE);
    ToriRSServer_ScriptsRunProc(srv, "[proc,bonevoyage_journal]", NULL, 0);
    bv_finish(srv);
    bv_pass("journal_complete");

    SELFTEST_CHECK(player->hitpoints > 0 && player->godmode == 1,
                   "player must stay alive (godmode) through the walk");

    bv_free_npc(srv, slot_curator);
    bv_free_npc(srv, slot_foreman);
    bv_free_npc(srv, slot_sawmill);
    bv_free_npc(srv, slot_guild);
    bv_free_npc(srv, slot_guard);
    bv_free_npc(srv, slot_lead);
    bv_free_npc(srv, slot_junior);
    bv_free_npc(srv, slot_jack);
    bv_free_npc(srv, slot_odd);
    bv_free_npc(srv, slot_apoth);
    bv_reset_state(srv, player, BV_NOT_STARTED, 0, 0, 1, woodcutting, vb_progress, vb_charm,
                   vb_potion, vb_kudos, vp_itexam);
    bv_god(player);

    fprintf(stderr, "ToriRSServer bv selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before, g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_BONEVOYAGE_SELFTEST_U_H */
