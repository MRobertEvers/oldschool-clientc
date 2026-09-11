#ifndef QUEST_TWILIGHTSPROMISE_SELFTEST_U_H
#define QUEST_TWILIGHTSPROMISE_SELFTEST_U_H

/*
 * Twilight's Promise Gate D C-walk. Player stays unkillable. Qualify
 * (Children of the Sun) runs before %vmq2 is written. Yes/No offer is
 * real p_choice2. Refuse must not write %vmq2. Furia starts the same
 * quest as Ennius. Keep ::twilightspromise / ::tprun names.
 *
 * Run focused:
 *   TORIRSSERVER_SELFTEST_TWP_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0 \
 *       TORIRSSERVER_CACHE=cache.osrs239 ./src/<obj>_opt/torirsserver --selftest
 */

static void
twp_selftest_pick_choice(
    struct ToriRSServer* srv,
    int row)
{
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    assert(srv->active_player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    assert(chatmenu > 0);
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(srv->active_player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static int
twp_selftest_spawn(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type)
{
    int slot;

    assert(srv);
    assert(player);
    assert(npc_type > 0);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, player->x + 1, player->z, player->level);
    return slot;
}

static void
twp_selftest_cleanup(
    struct ToriRSServer* srv,
    int slot)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
    ToriRSServer_ScriptsFree(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
selftest_quest_twilightspromise(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int vb_vmq1;
    int vb_vmq2;
    int npc_ennius;
    int npc_furia;
    int npc_metzli;
    int npc_itzla;
    int npc_regulus;
    int npc_regulus_fortis;
    int npc_knight1;
    int npc_citizen;
    int npc_knight3;
    int npc_drunk;
    int npc_knight6;
    int npc_cultist;
    int loc_stairs;
    int loc_chest;
    int obj_letter;
    int slot;

    assert(srv);
    assert(player);
    player->godmode = 1;

    vb_vmq1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1");
    vb_vmq2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq2");
    npc_ennius = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_ennius_vis");
    npc_furia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_furia_outer_palace");
    npc_metzli = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_metzli_vis");
    npc_itzla = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_itzla_vis");
    npc_regulus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_quetzal_keeper_1op");
    npc_regulus_fortis =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_quetzal_keeper_fortis");
    npc_knight1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_knight_1_vis");
    npc_citizen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_citizen_vis");
    npc_knight3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_knight_3_vis");
    npc_drunk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_knight_5_drunk");
    npc_knight6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_knight_6_vis");
    npc_cultist = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_cultist_m_1");
    loc_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vmq2_temple_stairs_top");
    loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vmq2_knight_4_chest");
    obj_letter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vmq2_incriminating_letter");

    SELFTEST_CHECK(vb_vmq1 > 0, "varbit vmq1 should resolve");
    SELFTEST_CHECK(vb_vmq2 > 0, "varbit vmq2 should resolve");
    SELFTEST_CHECK(npc_ennius > 0, "npc vmq2_ennius_vis should resolve");
    SELFTEST_CHECK(npc_furia > 0, "npc vmq2_furia_outer_palace should resolve");
    SELFTEST_CHECK(npc_metzli > 0, "npc vmq2_metzli_vis should resolve");
    SELFTEST_CHECK(npc_itzla > 0, "npc vmq2_itzla_vis should resolve");
    SELFTEST_CHECK(npc_regulus > 0, "npc vmq2_quetzal_keeper_1op should resolve");
    SELFTEST_CHECK(npc_regulus_fortis > 0, "npc vmq2_quetzal_keeper_fortis should resolve");
    if( vb_vmq1 <= 0 || vb_vmq2 <= 0 || npc_ennius <= 0 || npc_furia <= 0 )
        return;

    /* ::twilightspromise / ::tprun names stay exactly these. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "twilightspromise") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::twilightspromise should reach content");
    ToriRSServer_ScriptsFree(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                   "::twilightspromise should reset %%vmq2 to not_started, got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq2));

    ToriRSServer_WorldTeleport(srv, 0, 26 * 64 + 23, 49 * 64 + 5);

    /* Qualify fail: CotS missing. Named mesbox, no %vmq2 write. */
    ToriRSServer_VarbitSet(srv, vb_vmq1, 0);
    ToriRSServer_VarbitSet(srv, vb_vmq2, 0);
    slot = twp_selftest_spawn(srv, player, npc_ennius);
    SELFTEST_CHECK(slot >= 0, "Ennius should spawn for the CotS qualify");
    if( slot >= 0 )
    {
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ennius, -1,
                                                      slot) == TORIRSSERVER_TRIGGER_RAN,
                       "[opnpc1,vmq2_ennius_vis] should run the qualify path");
        SELFTEST_CHECK(player->active_script != NULL,
                       "missing Children of the Sun should park on the named mesbox");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                       "CotS qualify fail must not write %%vmq2, got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
        selftest_click_through(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                       "clicking the CotS mesbox still must not write %%vmq2, got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Furia qualify fail is the same gate. */
    ToriRSServer_VarbitSet(srv, vb_vmq1, 0);
    ToriRSServer_VarbitSet(srv, vb_vmq2, 0);
    slot = twp_selftest_spawn(srv, player, npc_furia);
    SELFTEST_CHECK(slot >= 0, "Furia should spawn for the CotS qualify");
    if( slot >= 0 )
    {
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_furia, -1,
                                                      slot) == TORIRSSERVER_TRIGGER_RAN,
                       "[opnpc1,vmq2_furia_outer_palace] should run the qualify path");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                       "Furia CotS qualify fail must not write %%vmq2, got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Regulus travel must not skip CotS. */
    ToriRSServer_VarbitSet(srv, vb_vmq1, 0);
    ToriRSServer_VarbitSet(srv, vb_vmq2, 0);
    slot = twp_selftest_spawn(srv, player, npc_regulus);
    SELFTEST_CHECK(slot >= 0, "Regulus should spawn for the CotS qualify");
    if( slot >= 0 )
    {
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_regulus, -1,
                                                      slot) == TORIRSSERVER_TRIGGER_RAN,
                       "[opnpc1,vmq2_quetzal_keeper_1op] should run the qualify path");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                       "Regulus CotS qualify fail must not write %%vmq2, got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Ennius refuse: Yes/No, pick No. Must not write %vmq2. */
    ToriRSServer_VarbitSet(srv, vb_vmq1, 24); /* ^cots_complete */
    ToriRSServer_VarbitSet(srv, vb_vmq2, 0);
    slot = twp_selftest_spawn(srv, player, npc_ennius);
    SELFTEST_CHECK(slot >= 0, "Ennius should spawn for the refuse");
    if( slot >= 0 )
    {
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ennius, -1,
                                                      slot) == TORIRSSERVER_TRIGGER_RAN,
                       "Ennius start should run when CotS is complete");
        selftest_click_through(srv, 1); /* past "Doesn't look like much." */
        SELFTEST_CHECK(player->active_script != NULL, "Ennius should park on p_choice2 Yes/No");
        twp_selftest_pick_choice(srv, 2); /* No. */
        selftest_click_through(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 0,
                       "Ennius refuse must not write %%vmq2, got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Ennius accept: Yes writes ^tp_metzli = 4. */
    ToriRSServer_VarbitSet(srv, vb_vmq1, 24);
    ToriRSServer_VarbitSet(srv, vb_vmq2, 0);
    slot = twp_selftest_spawn(srv, player, npc_ennius);
    SELFTEST_CHECK(slot >= 0, "Ennius should spawn for the accept");
    if( slot >= 0 )
    {
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ennius, -1,
                                                      slot) == TORIRSSERVER_TRIGGER_RAN,
                       "Ennius accept should run");
        selftest_click_through(srv, 8); /* Yes. is row 1 */
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 4,
                       "Ennius Yes should write %%vmq2 = ^tp_metzli (4), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Furia accept is the same start. */
    ToriRSServer_VarbitSet(srv, vb_vmq1, 24);
    ToriRSServer_VarbitSet(srv, vb_vmq2, 0);
    slot = twp_selftest_spawn(srv, player, npc_furia);
    SELFTEST_CHECK(slot >= 0, "Furia should spawn for the accept");
    if( slot >= 0 )
    {
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_furia, -1,
                                                      slot) == TORIRSSERVER_TRIGGER_RAN,
                       "Furia accept should run");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 4,
                       "Furia Yes should write %%vmq2 = ^tp_metzli (4), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Metzli sends the player into the crypt. */
    ToriRSServer_VarbitSet(srv, vb_vmq1, 24);
    ToriRSServer_VarbitSet(srv, vb_vmq2, 4);
    slot = twp_selftest_spawn(srv, player, npc_metzli);
    if( slot >= 0 && npc_metzli > 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_metzli, -1, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 8,
                       "Metzli should write %%vmq2 = ^tp_prince (8), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Temple stairs into the crypt. */
    if( loc_stairs > 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_vmq2, 8);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_stairs, -1, 0);
        ToriRSServer_WorldCloseModal(srv);
        ToriRSServer_ScriptsFree(srv);
    }

    /* Prince Itzla. */
    ToriRSServer_VarbitSet(srv, vb_vmq2, 8);
    slot = twp_selftest_spawn(srv, player, npc_itzla);
    if( slot >= 0 && npc_itzla > 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_itzla, -1, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 12,
                       "Itzla should write %%vmq2 = ^tp_ennius2 (12), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Crest from Ennius. */
    ToriRSServer_VarbitSet(srv, vb_vmq2, 12);
    slot = twp_selftest_spawn(srv, player, npc_ennius);
    if( slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ennius, -1, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 14,
                       "crest hand-out should write %%vmq2 = ^tp_knights (14), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Soft-skip the four knight groups. */
    if( npc_knight1 > 0 )
    {
        int kslot = twp_selftest_spawn(srv, player, npc_knight1);
        ToriRSServer_VarbitSet(srv, vb_vmq2, 14);
        if( kslot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_knight1, -1, kslot);
            selftest_click_through(srv, 4);
        }
        twp_selftest_cleanup(srv, kslot);
    }
    if( npc_citizen > 0 )
    {
        int cslot = twp_selftest_spawn(srv, player, npc_citizen);
        if( cslot >= 0 )
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_citizen, -1, cslot);
        twp_selftest_cleanup(srv, cslot);
    }
    if( npc_knight3 > 0 )
    {
        int kslot = twp_selftest_spawn(srv, player, npc_knight3);
        if( kslot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_knight3, -1, kslot);
            selftest_click_through(srv, 4);
        }
        twp_selftest_cleanup(srv, kslot);
    }
    if( npc_drunk > 0 )
    {
        int kslot = twp_selftest_spawn(srv, player, npc_drunk);
        if( kslot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_drunk, -1, kslot);
            selftest_click_through(srv, 4);
        }
        twp_selftest_cleanup(srv, kslot);
    }
    if( npc_knight6 > 0 )
    {
        int kslot = twp_selftest_spawn(srv, player, npc_knight6);
        if( kslot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_knight6, -1, kslot);
            selftest_click_through(srv, 4);
        }
        twp_selftest_cleanup(srv, kslot);
    }

    /* Ennius after the knights. */
    ToriRSServer_VarbitSet(srv, vb_vmq2, 22); /* ^tp_ennius3 */
    slot = twp_selftest_spawn(srv, player, npc_ennius);
    if( slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ennius, -1, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 24,
                       "knight report should write %%vmq2 = ^tp_letter (24), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Incriminating letter. */
    if( loc_chest > 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_vmq2, 24);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_chest, -1, 0);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 28,
                       "the HQ chest should write %%vmq2 = ^tp_bring (28), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
        ToriRSServer_WorldCloseModal(srv);
        ToriRSServer_ScriptsFree(srv);
    }
    (void)obj_letter;

    /* Letter hand-in. */
    ToriRSServer_VarbitSet(srv, vb_vmq2, 28);
    slot = twp_selftest_spawn(srv, player, npc_ennius);
    if( slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ennius, -1, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 34,
                       "letter hand-in should write %%vmq2 = ^tp_regulus (34), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);

    /* Fortis Regulus feed, then finish via Metzli / Ennius. */
    if( npc_regulus_fortis > 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_vmq2, 34);
        slot = twp_selftest_spawn(srv, player, npc_regulus_fortis);
        if( slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_regulus_fortis, -1, slot);
            selftest_click_through(srv, 8);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 36,
                           "Fortis Regulus should write %%vmq2 = ^tp_feed (36), got %d",
                           ToriRSServer_VarbitGet(player, vb_vmq2));
        }
        twp_selftest_cleanup(srv, slot);
    }

    ToriRSServer_VarbitSet(srv, vb_vmq2, 44); /* ^tp_cultists */
    if( npc_cultist > 0 )
    {
        slot = twp_selftest_spawn(srv, player, npc_cultist);
        if( slot >= 0 )
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cultist, -1, slot);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 46,
                       "a cultist soft-skip should write %%vmq2 = ^tp_finish (46), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
        twp_selftest_cleanup(srv, slot);
    }

    ToriRSServer_VarbitSet(srv, vb_vmq2, 46);
    slot = twp_selftest_spawn(srv, player, npc_ennius);
    if( slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ennius, -1, slot);
        selftest_click_through(srv, 12);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 50,
                       "Ennius finish should write %%vmq2 = ^tp_complete (50), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
    }
    twp_selftest_cleanup(srv, slot);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    ToriRSServer_ScriptsFree(srv);

    /* Headless ::tprun still reaches complete. */
    {
        static struct ToriRSServerCapture cap;
        int said_ok = 0;

        ToriRSServer_VarbitSet(srv, vb_vmq1, 24);
        ToriRSServer_VarbitSet(srv, vb_vmq2, 0);
        ToriRSServer_CaptureBegin(srv, &cap);
        ToriRSServer_ScriptsRunDebugproc(srv, "tprun");
        ToriRSServer_CaptureEnd(srv);
        for( int i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
             i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const char* text = selftest_message_text(srv, &cap.packets[i]);

            if( !text )
                continue;
            if( strstr(text, "tprun OK") != NULL )
                said_ok = 1;
        }
        SELFTEST_CHECK(said_ok, "::tprun should reach its OK line");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq2) == 50,
                       "::tprun should leave %%vmq2 = ^tp_complete (50), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq2));
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        ToriRSServer_ScriptsFree(srv);
    }
}

#endif /* QUEST_TWILIGHTSPROMISE_SELFTEST_U_H */
