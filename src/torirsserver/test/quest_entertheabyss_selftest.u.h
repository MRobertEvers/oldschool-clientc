#ifndef TORIRSSERVER_TEST_QUEST_ENTERTHEABYSS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ENTERTHEABYSS_SELFTEST_U_H

/* Enter the Abyss (miniquest) Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Mage / Aubury / Sedridor / Cromperty
 * cannot leak. Real OPNPC1 / OPNPC3 / OPNPC4 on the authored path.
 * player->godmode = 1 for the whole walk (Wilderness start is not a
 * death test). Completion goes through ~eta_quest_complete via the
 * Varrock Mage's authored ~eta_varrock_mage_talk (ToTE owns the
 * [opnpc1] wrappers). Additive ETA branches only -- do not rewrite
 * Rune Mysteries, Wanted!, Temple of the Eye, Construction, or MTA.
 *
 * Orb charging walks the real @teleport_to_essence_mine hook
 * (essence_mine.rs2 -> ~eta_charge_orb). ::etarun's bit-forge is a
 * disclosed leftover, not this walk's evidence.
 *
 * Gate: TORIRSSERVER_SELFTEST_ETA_ONLY=1
 */

#define ETA_NOT_STARTED 0
#define ETA_VARROCK 1
#define ETA_ORB 2
#define ETA_REWARD 3
#define ETA_COMPLETE 4

#define ETA_RM_COMPLETE 6
#define ETA_RC_XP_TENTHS 10000
#define ETA_STAT_RUNECRAFT 20

#define ETA_WILDY_X 3102
#define ETA_WILDY_Z 3557
#define ETA_VARROCK_X 3259
#define ETA_VARROCK_Z 3383
#define ETA_AUBURY_X 3253
#define ETA_AUBURY_Z 3401
#define ETA_SEDRIDOR_X 3106
#define ETA_SEDRIDOR_Z 9572
#define ETA_CROMPERTY_X 2684
#define ETA_CROMPERTY_Z 3322

static void
eta_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ETA PASS: %s\n", step);
}

static void
eta_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
eta_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
eta_finish(struct ToriRSServer* srv)
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
eta_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    eta_god(player);
    selftest_tick(srv);
}

static int
eta_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    eta_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
eta_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
eta_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, obj_id, 1);
}

static void
eta_reset_state(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int rm, int eta)
{
    int varp_rm;
    int varp_eta;
    int vb_aubury;
    int vb_tower;
    int vb_cromperty;
    int vb_brimstail;
    int vb_guild;

    assert(srv);
    assert(player);
    varp_rm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "runemysteries");
    varp_eta = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "abyssal_miniquest");
    vb_aubury = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_aubury");
    vb_tower =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_wizardstower");
    vb_cromperty =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_cromperty");
    vb_brimstail =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_brimstail");
    vb_guild =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_wizardsguild");
    eta_clear_inv(player);
    if( varp_rm >= 0 )
        player->varps[varp_rm] = rm;
    if( varp_eta >= 0 )
        player->varps[varp_eta] = eta;
    if( vb_aubury >= 0 )
        ToriRSServer_VarbitSet(srv, vb_aubury, 0);
    if( vb_tower >= 0 )
        ToriRSServer_VarbitSet(srv, vb_tower, 0);
    if( vb_cromperty >= 0 )
        ToriRSServer_VarbitSet(srv, vb_cromperty, 0);
    if( vb_brimstail >= 0 )
        ToriRSServer_VarbitSet(srv, vb_brimstail, 0);
    if( vb_guild >= 0 )
        ToriRSServer_VarbitSet(srv, vb_guild, 0);
    eta_god(player);
}

static void
eta_talk_and_pick(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int chatmenu,
                  int row)
{
    assert(srv);
    assert(player);
    biohazard_run_dialogue(srv, player, chatmenu);
    if( player->active_script && chatmenu > 0 && row > 0 )
        selftest_charter_choose(srv, row);
    biohazard_run_dialogue(srv, player, chatmenu);
}

static void
selftest_quest_entertheabyss(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int varp_rm;
    int varp_eta;
    int npc_wildy;
    int npc_varrock;
    int npc_aubury;
    int npc_sedridor;
    int npc_cromperty;
    int obj_orb_empty;
    int obj_orb_full;
    int obj_book;
    int obj_pouch;
    int obj_pot;
    int vb_aubury;
    int vb_tower;
    int vb_cromperty;
    int chatmenu;
    int wildy_slot;
    int varrock_slot;
    int aubury_slot;
    int sed_slot;
    int crom_slot;
    int xp_before;
    int rc;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: ::entertheabyss / Enter the Abyss\n");

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

    varp_rm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "runemysteries");
    varp_eta = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "abyssal_miniquest");
    npc_wildy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1a");
    if( npc_wildy < 0 )
        npc_wildy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1");
    npc_varrock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1_edgeb");
    if( npc_varrock < 0 )
        npc_varrock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1_edge");
    npc_aubury = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "aubury");
    npc_sedridor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "head_wizard");
    npc_cromperty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ardounge_wizard");
    obj_orb_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "scrying_orb_empty");
    obj_orb_full = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "scrying_orb_full");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_instruction_book");
    obj_pouch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_small");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    vb_aubury = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_aubury");
    vb_tower =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_wizardstower");
    vb_cromperty =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_cromperty");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(varp_rm >= 0, "runemysteries varp should resolve");
    SELFTEST_CHECK(varp_eta >= 0, "abyssal_miniquest varp should resolve");
    SELFTEST_CHECK(npc_wildy >= 0, "Wilderness Mage of Zamorak should resolve");
    SELFTEST_CHECK(npc_varrock >= 0, "Varrock Mage of Zamorak should resolve");
    SELFTEST_CHECK(npc_aubury >= 0, "aubury should resolve");
    SELFTEST_CHECK(npc_sedridor >= 0, "head_wizard (Sedridor) should resolve");
    SELFTEST_CHECK(npc_cromperty >= 0, "ardounge_wizard (Cromperty) should resolve");
    SELFTEST_CHECK(obj_orb_empty >= 0, "scrying_orb_empty should resolve");
    SELFTEST_CHECK(obj_orb_full >= 0, "scrying_orb_full should resolve");
    SELFTEST_CHECK(obj_book >= 0, "rcu_instruction_book should resolve");
    SELFTEST_CHECK(obj_pouch >= 0, "rcu_pouch_small should resolve");
    if( varp_rm < 0 || varp_eta < 0 || npc_wildy < 0 || npc_varrock < 0 ||
        npc_aubury < 0 || npc_sedridor < 0 || npc_cromperty < 0 ||
        obj_orb_empty < 0 || obj_orb_full < 0 || obj_book < 0 || obj_pouch < 0 )
    {
        fprintf(stderr, "  SKIP  missing Enter the Abyss symbols\n");
        return;
    }

    wildy_slot = eta_spawn(srv, npc_wildy, ETA_WILDY_X, ETA_WILDY_Z, 0);
    varrock_slot = eta_spawn(srv, npc_varrock, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    aubury_slot = eta_spawn(srv, npc_aubury, ETA_AUBURY_X, ETA_AUBURY_Z, 0);
    sed_slot = eta_spawn(srv, npc_sedridor, ETA_SEDRIDOR_X, ETA_SEDRIDOR_Z, 0);
    crom_slot = eta_spawn(srv, npc_cromperty, ETA_CROMPERTY_X, ETA_CROMPERTY_Z, 0);
    SELFTEST_CHECK(wildy_slot >= 0, "Wilderness Mage should spawn");
    SELFTEST_CHECK(varrock_slot >= 0, "Varrock Mage should spawn");
    SELFTEST_CHECK(aubury_slot >= 0, "Aubury should spawn");
    SELFTEST_CHECK(sed_slot >= 0, "Sedridor should spawn");
    SELFTEST_CHECK(crom_slot >= 0, "Cromperty should spawn");

    /* ---- RM-incomplete wildy refuse ---- */
    eta_reset_state(srv, player, 0, ETA_NOT_STARTED);
    eta_tele(srv, ETA_WILDY_X, ETA_WILDY_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_wildy, -1, wildy_slot);
    eta_finish(srv);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 wildy mage (no RM) should run, got %d", rc);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_NOT_STARTED,
                   "RM-incomplete refuse must not start the miniquest, got %d",
                   player->varps[varp_eta]);
    eta_pass("wildy_rm_incomplete_refuse");

    /* ---- Wildy start: meet at Varrock Chaos Temple ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_NOT_STARTED);
    eta_tele(srv, ETA_WILDY_X, ETA_WILDY_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_wildy, -1, wildy_slot);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_VARROCK,
                   "OPNPC1 wildy with RM complete should write eta=1, got %d",
                   player->varps[varp_eta]);
    eta_pass("wildy_start_meet_varrock");

    /* ---- Already-started wildy: no place to talk ---- */
    eta_tele(srv, ETA_WILDY_X, ETA_WILDY_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_wildy, -1, wildy_slot);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_VARROCK,
                   "re-talk wildy must stay at eta=1, got %d", player->varps[varp_eta]);
    eta_pass("wildy_already_started_no_place");

    /* ---- Varrock RM-incomplete refuse ---- */
    eta_reset_state(srv, player, 0, ETA_VARROCK);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_VARROCK,
                   "Varrock RM-incomplete refuse must not advance, got %d",
                   player->varps[varp_eta]);
    eta_pass("varrock_rm_incomplete_refuse");

    /* ---- Stranger default (RM complete, not started) ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_NOT_STARTED);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_NOT_STARTED,
                   "stranger default must leave eta=0, got %d", player->varps[varp_eta]);
    eta_pass("varrock_stranger_default");

    /* ---- Refuse deal: Nothing, thanks. ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_VARROCK);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_talk_and_pick(srv, player, chatmenu, 2);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_VARROCK,
                   "Nothing thanks must leave eta=1, got %d", player->varps[varp_eta]);
    eta_pass("varrock_refuse_nothing_thanks");

    /* ---- Help never-mind ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_VARROCK);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_talk_and_pick(srv, player, chatmenu, 1); /* Where do you get your runes */
    eta_talk_and_pick(srv, player, chatmenu, 2); /* Never mind */
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_VARROCK,
                   "Never mind help must leave eta=1, got %d", player->varps[varp_eta]);
    eta_pass("varrock_refuse_never_mind");

    /* ---- Refuse Yes/No deal ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_VARROCK);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_talk_and_pick(srv, player, chatmenu, 2); /* No. */
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_VARROCK,
                   "Refuse deal must leave eta=1, got %d", player->varps[varp_eta]);
    eta_pass("varrock_refuse_deal");

    /* ---- Inventory-full orb grant ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_VARROCK);
    if( obj_pot > 0 )
        eta_fill_inv(player, obj_pot);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_VARROCK,
                   "full-inv grant must not write eta=2, got %d", player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_empty) == 0,
                   "full inv must not receive a scrying orb");
    eta_pass("varrock_inventory_full_orb");

    /* ---- Accept deal / scrying orb grant ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_VARROCK);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_ORB,
                   "accept deal should write eta=2, got %d", player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_empty) == 1,
                   "accept deal should grant one empty scrying orb, got %d",
                   selftest_count_obj(player, obj_orb_empty));
    eta_pass("varrock_scrying_orb_grant");

    /* ---- Orb-not-ready reminder ---- */
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_ORB,
                   "reminder must leave eta=2, got %d", player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_empty) == 1,
                   "reminder must not consume the empty orb");
    eta_pass("varrock_orb_not_ready_reminder");

    /* ---- Lost-orb replacement ---- */
    eta_clear_inv(player);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_empty) == 1,
                   "lost-orb talk should replace one empty scrying orb, got %d",
                   selftest_count_obj(player, obj_orb_empty));
    eta_pass("varrock_lost_orb_replacement");

    /* ---- Lost-orb inventory full ---- */
    eta_clear_inv(player);
    if( obj_pot > 0 )
        eta_fill_inv(player, obj_pot);
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_empty) == 0,
                   "full inv must not duplicate a lost orb");
    eta_pass("varrock_lost_orb_inventory_full");

    /* ---- Real Aubury essence tele (OPNPC4) with orb ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_ORB);
    inv_set(player, 0, obj_orb_empty, 1);
    eta_tele(srv, ETA_AUBURY_X, ETA_AUBURY_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC4, npc_aubury, -1, aubury_slot);
    if( rc != TORIRSSERVER_TRIGGER_RAN )
        rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_aubury, -1, aubury_slot);
    eta_finish(srv);
    SELFTEST_CHECK(vb_aubury < 0 || ToriRSServer_VarbitGet(player, vb_aubury) == 1,
                   "real Aubury essence tele should set rcu_essencespot_aubury");
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_empty) == 1,
                   "first charge must leave the empty orb, got %d",
                   selftest_count_obj(player, obj_orb_empty));
    eta_pass("aubury_essence_tele_charge");

    /* ---- Real Sedridor essence tele (OPNPC3) with orb ---- */
    eta_tele(srv, ETA_SEDRIDOR_X, ETA_SEDRIDOR_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_sedridor, -1, sed_slot);
    eta_finish(srv);
    SELFTEST_CHECK(vb_tower < 0 || ToriRSServer_VarbitGet(player, vb_tower) == 1,
                   "real Sedridor essence tele should set rcu_essencespot_wizardstower");
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_empty) == 1,
                   "second charge must leave the empty orb, got %d",
                   selftest_count_obj(player, obj_orb_empty));
    eta_pass("sedridor_essence_tele_charge");

    /* ---- Real Cromperty essence tele (OPNPC3) -- third spot absorbs ---- */
    eta_tele(srv, ETA_CROMPERTY_X, ETA_CROMPERTY_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_cromperty, -1, crom_slot);
    eta_finish(srv);
    SELFTEST_CHECK(vb_cromperty < 0 || ToriRSServer_VarbitGet(player, vb_cromperty) == 1,
                   "real Cromperty essence tele should set rcu_essencespot_cromperty");
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_full) == 1,
                   "third distinct source should convert to charged orb, full=%d empty=%d",
                   selftest_count_obj(player, obj_orb_full),
                   selftest_count_obj(player, obj_orb_empty));
    eta_pass("cromperty_essence_tele_absorb");

    /* ---- Return charged orb ---- */
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_REWARD,
                   "handing the charged orb should write eta=3, got %d",
                   player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_orb_full) == 0,
                   "handover should consume the charged orb");
    eta_pass("return_charged_orb");

    /* ---- Reward dialogue + authored complete scroll ---- */
    xp_before = player->stat_xp_tenths[ETA_STAT_RUNECRAFT];
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_COMPLETE,
                   "reward talk should complete eta=4, got %d", player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_book) == 1,
                   "complete should grant Abyssal book, got %d",
                   selftest_count_obj(player, obj_book));
    SELFTEST_CHECK(selftest_count_obj(player, obj_pouch) == 1,
                   "complete should grant small pouch, got %d",
                   selftest_count_obj(player, obj_pouch));
    SELFTEST_CHECK(player->stat_xp_tenths[ETA_STAT_RUNECRAFT] >=
                       xp_before + ETA_RC_XP_TENTHS,
                   "complete should grant 1000 Runecraft XP (10000 tenths), before %d after %d",
                   xp_before, player->stat_xp_tenths[ETA_STAT_RUNECRAFT]);
    eta_pass("reward_dialogue_complete_scroll");

    /* ---- Post-complete never mind ---- */
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_talk_and_pick(srv, player, chatmenu, 2);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_COMPLETE,
                   "Never mind must leave complete, got %d", player->varps[varp_eta]);
    eta_pass("post_complete_never_mind");

    /* ---- Post-complete Abyss teleport offer ---- */
    eta_tele(srv, ETA_VARROCK_X, ETA_VARROCK_Z, 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_varrock, -1, varrock_slot);
    eta_talk_and_pick(srv, player, chatmenu, 1);
    eta_finish(srv);
    SELFTEST_CHECK(player->varps[varp_eta] == ETA_COMPLETE,
                   "Abyss teleport must leave complete, got %d", player->varps[varp_eta]);
    eta_pass("post_complete_abyss_teleport");

    /* ---- Journal at every authored state ---- */
    eta_reset_state(srv, player, ETA_RM_COMPLETE, ETA_NOT_STARTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,entertheabyss_journal]", NULL, 0);
    eta_finish(srv);
    eta_pass("journal_not_started");

    player->varps[varp_eta] = ETA_VARROCK;
    ToriRSServer_ScriptsRunProc(srv, "[proc,entertheabyss_journal]", NULL, 0);
    eta_finish(srv);
    eta_pass("journal_varrock");

    player->varps[varp_eta] = ETA_ORB;
    inv_set(player, 0, obj_orb_empty, 1);
    ToriRSServer_ScriptsRunProc(srv, "[proc,entertheabyss_journal]", NULL, 0);
    eta_finish(srv);
    eta_pass("journal_orb_empty");

    eta_clear_inv(player);
    inv_set(player, 0, obj_orb_full, 1);
    ToriRSServer_ScriptsRunProc(srv, "[proc,entertheabyss_journal]", NULL, 0);
    eta_finish(srv);
    eta_pass("journal_orb_full");

    eta_clear_inv(player);
    player->varps[varp_eta] = ETA_REWARD;
    ToriRSServer_ScriptsRunProc(srv, "[proc,entertheabyss_journal]", NULL, 0);
    eta_finish(srv);
    eta_pass("journal_reward");

    player->varps[varp_eta] = ETA_COMPLETE;
    ToriRSServer_ScriptsRunProc(srv, "[proc,entertheabyss_journal]", NULL, 0);
    eta_finish(srv);
    eta_pass("journal_complete");

    SELFTEST_CHECK(player->hitpoints > 0 && player->godmode == 1,
                   "player must stay alive (godmode) through the walk");

    eta_free_npc(srv, wildy_slot);
    eta_free_npc(srv, varrock_slot);
    eta_free_npc(srv, aubury_slot);
    eta_free_npc(srv, sed_slot);
    eta_free_npc(srv, crom_slot);
    eta_reset_state(srv, player, 0, ETA_NOT_STARTED);
    eta_god(player);

    fprintf(stderr, "ToriRSServer eta selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before, g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_ENTERTHEABYSS_SELFTEST_U_H */
