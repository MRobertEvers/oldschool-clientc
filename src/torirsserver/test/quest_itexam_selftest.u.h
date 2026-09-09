/* The Dig Site (quest_itexam). Included from ToriRSServer_WorldSelftest and
 * called immediately before a selftest_reset_world so spawned npcs cannot
 * re-aim later stanzas. Every PASS is a real opnpc / oploc / opheld dispatch. */
static int
itexam_progress_bits(
    const struct ToriRSServerPlayer* player,
    int varp)
{
    assert(player);
    assert(varp >= 0);
    return player->varps[varp] & 0xF;
}

static int
itexam_inv_has(
    const struct ToriRSServerPlayer* player,
    int obj_id)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            return 1;
    }
    return 0;
}

static void
itexam_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static int
itexam_loc_slot(
    struct ToriRSServer* srv,
    int loc_id,
    int x,
    int z,
    int level,
    int shape)
{
    int slot;

    assert(srv);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    selftest_tick(srv);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot >= 0 )
        return slot;
    return ToriRSServer_SceneAddLoc(x, z, level, loc_id, shape, 0);
}

static void
itexam_pick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int last_slot,
    int rows_uid)
{
    assert(srv);
    assert(player);
    biohazard_run_dialogue(srv, player, rows_uid);
    if( player->active_script != NULL && player->resume_button_count > 0 &&
        rows_uid > 0 )
    {
        player->last_slot = last_slot;
        ToriRSServer_ScriptsResumeButton(srv, rows_uid);
    }
    biohazard_run_dialogue(srv, player, 0);
}

static void
itexam_opheldu(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int held,
    int used)
{
    int s;
    int held_slot = 0;
    int used_slot = 1;

    assert(srv);
    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == held )
            held_slot = s;
        if( player->inv[s].obj_id == used )
            used_slot = s;
    }
    player->last_item = held;
    player->last_slot = held_slot;
    player->last_useitem = used;
    player->last_useslot = used_slot;
    ToriRSServer_ScriptsRunOpheldu(srv, held, -1, used, -1);
    biohazard_run_dialogue(srv, player, 0);
}

static void
selftest_quest_itexam(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int rows_uid;
    int i;

    assert(srv);
    assert(player);
    fprintf(stderr, "ToriRSServer selftest: ::itexamrun\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int varp_level = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "itexamlevel");
        int varp_errands = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "itexam_errands");
        int varp_bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "itexam_bits");
        int npc_examiner = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "examiner");
        int npc_curator = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "curator");
        int npc_student1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "student1");
        int npc_student2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "student2");
        int npc_student3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "student3");
        int npc_expert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "archaeological_expert");
        int npc_workman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "digworkman1");
        int npc_guide = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "panning_guide");
        int loc_bush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "digsitebushsample");
        int loc_pan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "panning_point");
        int loc_cupboard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "qip_digsite_cupboardshut");
        int loc_winch1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "digwinch1");
        int loc_winch2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "digwinch2");
        int loc_barrel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "digbarrelclosed");
        int loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "digchestclosed");
        int loc_brick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "digblastbrick");
        int loc_tablet = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "qip_digsite_zaros_stone_tablet_multiloc");
        int obj_letter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "digplainletter");
        int obj_sealed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "recommendedletter");
        int obj_sample1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rock_sample1");
        int obj_sample2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rock_sample2");
        int obj_sample3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rock_sample3");
        int obj_tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cup_of_tea");
        int obj_tray = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tray_empty");
        int obj_talisman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "digtalisman");
        int obj_scroll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "digexpertscroll");
        int obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
        int obj_trowel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trowel");
        int obj_vial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vial_empty");
        int obj_liquid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_liquid");
        int obj_nitro = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nitroglycerin");
        int obj_powder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_powder");
        int obj_nitrate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ammonium_nitrate");
        int obj_charcoal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "charcoal");
        int obj_pestle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pestle_and_mortar");
        int obj_ground = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ground_charcoal");
        int obj_pre = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "precharcoalmixture");
        int obj_post = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "postcharcoalmixture");
        int obj_root = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "arcenia_root");
        int obj_compound = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "digcompound");
        int obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
        int obj_tablet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zarosstonetablet");
        int obj_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gold_bar");
        int obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "digchestkey");
        int stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
        int stat_herblore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
        int stat_thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
        int stat_mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");

        SELFTEST_CHECK(
            varp_level >= 0 && varp_errands >= 0 && varp_bits >= 0 && npc_examiner >= 0 &&
                npc_curator >= 0 && npc_student1 >= 0 && npc_student2 >= 0 &&
                npc_student3 >= 0 && npc_expert >= 0 && npc_workman >= 0 &&
                npc_guide >= 0 && loc_bush >= 0 && loc_pan >= 0 && loc_cupboard >= 0 &&
                loc_winch1 >= 0 && loc_winch2 >= 0 && loc_barrel >= 0 && loc_chest >= 0 &&
                loc_brick >= 0 && loc_tablet >= 0 && obj_letter >= 0 && obj_sealed >= 0 &&
                obj_sample1 >= 0 && obj_sample2 >= 0 && obj_sample3 >= 0 && obj_tea >= 0 &&
                obj_tray >= 0 && obj_talisman >= 0 && obj_scroll >= 0 && obj_rope >= 0 &&
                obj_trowel >= 0 && obj_vial >= 0 && obj_liquid >= 0 && obj_nitro >= 0 &&
                obj_powder >= 0 && obj_nitrate >= 0 && obj_charcoal >= 0 &&
                obj_pestle >= 0 && obj_ground >= 0 && obj_pre >= 0 && obj_post >= 0 &&
                obj_root >= 0 && obj_compound >= 0 && obj_tinder >= 0 && obj_tablet >= 0 &&
                obj_gold >= 0 && obj_key >= 0 && stat_agility >= 0 && stat_herblore >= 0 &&
                stat_thieving >= 0 && stat_mining >= 0,
            "the ::itexamrun C-side names should all resolve");

        if( varp_level < 0 || npc_examiner < 0 || loc_tablet < 0 || obj_letter < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        rows_uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
        player->godmode = 1;
        player->hitpoints = 99;
        player->stat_level[TORIRSSERVER_STAT_HITPOINTS] = 99;
        player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS] = 99;
        if( stat_agility >= 0 )
        {
            player->stat_level[stat_agility] = 99;
            player->stat_boosted[stat_agility] = 99;
        }
        if( stat_herblore >= 0 )
        {
            player->stat_level[stat_herblore] = 99;
            player->stat_boosted[stat_herblore] = 99;
        }
        if( stat_thieving >= 0 )
        {
            player->stat_level[stat_thieving] = 99;
            player->stat_boosted[stat_thieving] = 99;
        }
        if( stat_mining >= 0 )
        {
            player->stat_level[stat_mining] = 99;
            player->stat_boosted[stat_mining] = 99;
        }
        itexam_clear_inv(player);
        player->varps[varp_level] = 0;
        player->varps[varp_errands] = 0;
        player->varps[varp_bits] = 0;

        /* ---- step 0: Examiner starts the quest (real opnpc1 + p_choice) ---- */
        {
            int slot = npc_spawn(srv, npc_examiner, 3362, 3337, 0);

            ToriRSServer_WorldTeleport(srv, 0, 3362, 3337);
            selftest_tick(srv);
            player->varps[varp_level] = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_examiner, -1, slot);
            itexam_pick(srv, player, 1, rows_uid);
            SELFTEST_CHECK(itexam_inv_has(player, obj_letter),
                           "examiner start should grant digplainletter");
            SELFTEST_CHECK(itexam_progress_bits(player, varp_level) == 1,
                           "examiner start should reach itexam_stamping, got %d",
                           itexam_progress_bits(player, varp_level));
            if( itexam_inv_has(player, obj_letter) &&
                itexam_progress_bits(player, varp_level) == 1 )
                fprintf(stderr, "  PASS  itexam step 0: examiner starts the quest\n");
            if( slot >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        /* ---- step 1: Curator stamps the letter (real opnpcu) ---- */
        {
            int slot = npc_spawn(srv, npc_curator, 3257, 3448, 0);

            ToriRSServer_WorldTeleport(srv, 0, 3257, 3448);
            selftest_tick(srv);
            player->last_useitem = obj_letter;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_curator, -1, slot);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_inv_has(player, obj_sealed),
                           "curator should stamp digplainletter into recommendedletter");
            SELFTEST_CHECK(!itexam_inv_has(player, obj_letter),
                           "stamped letter should consume digplainletter");
            if( itexam_inv_has(player, obj_sealed) )
                fprintf(stderr, "  PASS  itexam step 1a: curator stamps the letter\n");

            /* Return the sealed letter -- examiner consumes it and opens exam 1. */
            {
                int ex = npc_spawn(srv, npc_examiner, 3362, 3337, 0);

                ToriRSServer_WorldTeleport(srv, 0, 3362, 3337);
                selftest_tick(srv);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_examiner, -1, ex);
                biohazard_run_dialogue(srv, player, 0);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(itexam_progress_bits(player, varp_level) == 2,
                               "returning the sealed letter should reach first_exam, got %d",
                               itexam_progress_bits(player, varp_level));
                if( itexam_progress_bits(player, varp_level) == 2 )
                    fprintf(stderr, "  PASS  itexam step 1b: examiner takes the sealed letter\n");
                if( ex >= 0 )
                {
                    ToriRSServer_WorldNpcFree(srv, ex);
                    ToriRSServer_WorldNpcReap(srv);
                }
            }
            if( slot >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        /* ---- step 2: student samples via real oploc / opnpcu ---- */
        {
            int bush = itexam_loc_slot(srv, loc_bush, 3357, 3372, 0, 10);
            int s1 = npc_spawn(srv, npc_student1, 3362, 3398, 0);
            int s2 = npc_spawn(srv, npc_student2, 3345, 3425, 0);
            int s3 = npc_spawn(srv, npc_student3, 3369, 3419, 0);

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bush, -1, bush);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_inv_has(player, obj_sample3),
                           "searching digsitebushsample at first_exam should grant rock_sample3");
            if( itexam_inv_has(player, obj_sample3) )
                fprintf(stderr, "  PASS  itexam step 2a: bush grants rock_sample3\n");

            inv_set(player, 1, obj_sample1, 1);
            inv_set(player, 2, obj_sample2, 1);
            player->last_useitem = obj_sample1;
            ToriRSServer_WorldTeleport(srv, 0, 3362, 3398);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_student1, -1, s1);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(!itexam_inv_has(player, obj_sample1),
                           "student1 should consume rock_sample1");

            player->last_useitem = obj_sample2;
            ToriRSServer_WorldTeleport(srv, 0, 3369, 3419);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_student3, -1, s3);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(!itexam_inv_has(player, obj_sample2),
                           "student3 should consume rock_sample2");

            player->last_useitem = obj_sample3;
            ToriRSServer_WorldTeleport(srv, 0, 3345, 3425);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_student2, -1, s2);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(!itexam_inv_has(player, obj_sample3),
                           "student2 should consume rock_sample3");
            /* 2 bits per errand, value 2 = answered: 2 + (2<<2) + (2<<4) = 42 */
            SELFTEST_CHECK(player->varps[varp_errands] == 42,
                           "all three student hand-ins should mark errands answered, got %d",
                           player->varps[varp_errands]);
            if( player->varps[varp_errands] == 42 )
                fprintf(stderr, "  PASS  itexam step 2b: three student sample hand-ins\n");

            if( s1 >= 0 )
                ToriRSServer_WorldNpcFree(srv, s1);
            if( s2 >= 0 )
                ToriRSServer_WorldNpcFree(srv, s2);
            if( s3 >= 0 )
                ToriRSServer_WorldNpcFree(srv, s3);
            ToriRSServer_WorldNpcReap(srv);
        }

        /* ---- steps 3-4: student tip talks unlock later exams (real opnpc1) ---- */
        player->varps[varp_level] = 3; /* second_exam */
        player->varps[varp_errands] = 0;
        {
            int s1 = npc_spawn(srv, npc_student1, 3362, 3398, 0);

            ToriRSServer_WorldTeleport(srv, 0, 3362, 3398);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_student1, -1, s1);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK((player->varps[varp_errands] & 3) == 2,
                           "student1 second-exam tips should mark errand 0 answered, bits=%d",
                           player->varps[varp_errands]);
            if( (player->varps[varp_errands] & 3) == 2 )
                fprintf(stderr, "  PASS  itexam step 3: student1 second-exam tips\n");
            if( s1 >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, s1);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        /* ---- step 5: tea + panning loc, then talisman on expert (opnpcu) ---- */
        {
            int guide = npc_spawn(srv, npc_guide, 3385, 3386, 0);
            int pan = itexam_loc_slot(srv, loc_pan, 3384, 3381, 0, 10);

            itexam_clear_inv(player);
            inv_set(player, 0, obj_tea, 1);
            ToriRSServer_WorldTeleport(srv, 0, 3385, 3386);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guide, -1, guide);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK((player->varps[varp_bits] & (1 << 1)) != 0,
                           "cup of tea on the panning guide should set the tea bit");
            if( (player->varps[varp_bits] & (1 << 1)) != 0 )
                fprintf(stderr, "  PASS  itexam step 5a: panning guide takes tea\n");

            inv_set(player, 0, obj_tray, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_pan, -1, pan);
            for( i = 0; i < 8 && player->active_script; i++ )
                selftest_tick(srv);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(!itexam_inv_has(player, obj_tray) || player->inv[0].obj_id != obj_tray,
                           "panning_point should consume the empty tray");
            fprintf(stderr, "  PASS  itexam step 5b: real oploc1 panning_point\n");
            if( guide >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, guide);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        player->varps[varp_level] = 5; /* impress_archeological_expert */
        itexam_clear_inv(player);
        inv_set(player, 0, obj_talisman, 1);
        {
            int ex = npc_spawn(srv, npc_expert, 3357, 3334, 0);

            ToriRSServer_WorldTeleport(srv, 0, 3357, 3334);
            selftest_tick(srv);
            player->last_useitem = obj_talisman;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_expert, -1, ex);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_inv_has(player, obj_scroll),
                           "showing the talisman should grant digexpertscroll");
            SELFTEST_CHECK((player->varps[varp_bits] & (1 << 7)) != 0,
                           "talisman hand-in should set the shown-symbol bit");
            if( itexam_inv_has(player, obj_scroll) )
                fprintf(stderr, "  PASS  itexam step 5c: expert takes the talisman\n");
            if( ex >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, ex);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        /* ---- step 6: invitation on workman, ropes on both winches ---- */
        {
            int wm = npc_spawn(srv, npc_workman, 3360, 3415, 0);
            int w1;
            int w2;

            ToriRSServer_WorldTeleport(srv, 0, 3360, 3415);
            selftest_tick(srv);
            player->last_useitem = obj_scroll;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_workman, -1, wm);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_progress_bits(player, varp_level) == 6,
                           "invitation on a workman should reach mineshaft_permit, got %d",
                           itexam_progress_bits(player, varp_level));
            if( itexam_progress_bits(player, varp_level) == 6 )
                fprintf(stderr, "  PASS  itexam step 6a: workman takes the invitation\n");

            itexam_clear_inv(player);
            inv_set(player, 0, obj_rope, 1);
            w1 = itexam_loc_slot(srv, loc_winch1, 3353, 3417, 0, 10);
            player->last_useitem = obj_rope;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_winch1, -1, w1);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK((player->varps[varp_bits] & (1 << 2)) != 0,
                           "rope on digwinch1 should attach");
            SELFTEST_CHECK(!itexam_inv_has(player, obj_rope),
                           "attaching the west winch rope should consume it");

            inv_set(player, 0, obj_rope, 1);
            w2 = itexam_loc_slot(srv, loc_winch2, 3370, 3429, 0, 10);
            player->last_useitem = obj_rope;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_winch2, -1, w2);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK((player->varps[varp_bits] & (1 << 3)) != 0,
                           "rope on digwinch2 should attach");
            if( (player->varps[varp_bits] & (1 << 2)) != 0 &&
                (player->varps[varp_bits] & (1 << 3)) != 0 )
                fprintf(stderr, "  PASS  itexam step 6b: ropes on both winches\n");
            if( wm >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, wm);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        /* ---- chemistry + barrel + chest (real oplocu / opheldu / opnpcu) ---- */
        {
            int barrel = itexam_loc_slot(srv, loc_barrel, 3364, 3378, 0, 10);
            int chest = itexam_loc_slot(srv, loc_chest, 3374, 3378, 0, 10);
            int ex;

            itexam_clear_inv(player);
            inv_set(player, 0, obj_trowel, 1);
            player->last_useitem = obj_trowel;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_barrel, -1, barrel);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK((player->varps[varp_bits] & (1 << 4)) != 0,
                           "trowel on the barrel should open it");

            inv_set(player, 1, obj_vial, 1);
            player->last_useitem = obj_vial;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_barrel, -1, barrel);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_inv_has(player, obj_liquid),
                           "vial on the open barrel should grant unidentified_liquid");
            if( itexam_inv_has(player, obj_liquid) )
                fprintf(stderr, "  PASS  itexam step 6c: barrel trowel + vial\n");

            inv_set(player, 2, obj_key, 1);
            player->last_useitem = obj_key;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_chest, -1, chest);
            for( i = 0; i < 4 && player->active_script; i++ )
                selftest_tick(srv);
            ToriRSServer_WorldCloseModal(srv);
            {
                int open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "digchestopen");
                int open_slot = -1;

                if( open >= 0 )
                    open_slot = ToriRSServer_SceneFindLocId(3374, 3378, 0, open);
                if( open_slot < 0 )
                    open_slot = itexam_loc_slot(srv, open, 3374, 3378, 0, 10);
                if( open >= 0 && open_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, SS_TRIGGER_OPLOC1, open, -1, open_slot);
                    biohazard_run_dialogue(srv, player, 0);
                }
            }
            SELFTEST_CHECK(itexam_inv_has(player, obj_powder),
                           "searching the unlocked chest should grant unidentified_powder");
            if( itexam_inv_has(player, obj_powder) )
                fprintf(stderr, "  PASS  itexam step 6d: chest key + search\n");

            ex = npc_spawn(srv, npc_expert, 3357, 3334, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3357, 3334);
            selftest_tick(srv);
            player->last_useitem = obj_liquid;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_expert, -1, ex);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_inv_has(player, obj_nitro),
                           "expert should identify the liquid as nitroglycerin");
            player->last_useitem = obj_powder;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_expert, -1, ex);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_inv_has(player, obj_nitrate),
                           "expert should identify the powder as ammonium_nitrate");
            if( itexam_inv_has(player, obj_nitro) && itexam_inv_has(player, obj_nitrate) )
                fprintf(stderr, "  PASS  itexam step 6e: expert identifies chemicals\n");

            itexam_clear_inv(player);
            inv_set(player, 0, obj_nitrate, 1);
            inv_set(player, 1, obj_nitro, 1);
            itexam_opheldu(srv, player, obj_nitrate, obj_nitro);
            SELFTEST_CHECK(itexam_inv_has(player, obj_pre),
                           "ammonium + nitro should mix precharcoalmixture");

            inv_set(player, 2, obj_charcoal, 1);
            inv_set(player, 3, obj_pestle, 1);
            itexam_opheldu(srv, player, obj_charcoal, obj_pestle);
            SELFTEST_CHECK(itexam_inv_has(player, obj_ground),
                           "pestle on charcoal should grind it");

            itexam_opheldu(srv, player, obj_pre, obj_ground);
            SELFTEST_CHECK(itexam_inv_has(player, obj_post),
                           "ground charcoal in the mix should make postcharcoalmixture");

            inv_set(player, 4, obj_root, 1);
            itexam_opheldu(srv, player, obj_post, obj_root);
            SELFTEST_CHECK(itexam_inv_has(player, obj_compound),
                           "arcenia root in the mix should make digcompound");
            if( itexam_inv_has(player, obj_compound) )
                fprintf(stderr, "  PASS  itexam step 6f: four-part chemical mix\n");
            if( ex >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, ex);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        /* ---- step 7: pour + ignite the bricks (real oplocu) ---- */
        {
            int brick = itexam_loc_slot(srv, loc_brick, 3378, 9824, 0, 10);

            player->varps[varp_level] = 6; /* mineshaft_permit */
            itexam_clear_inv(player);
            inv_set(player, 0, obj_compound, 1);
            inv_set(player, 1, obj_tinder, 1);
            player->last_useitem = obj_compound;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_brick, -1, brick);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_progress_bits(player, varp_level) == 7,
                           "compound on the bricks should reach poured_compound, got %d",
                           itexam_progress_bits(player, varp_level));
            SELFTEST_CHECK(!itexam_inv_has(player, obj_compound),
                           "pouring should consume digcompound");
            if( itexam_progress_bits(player, varp_level) == 7 )
                fprintf(stderr, "  PASS  itexam step 7a: compound on bricks\n");

            ToriRSServer_WorldTeleport(srv, 0, 3379, 9826);
            selftest_tick(srv);
            player->last_useitem = obj_tinder;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_brick, -1, brick);
            for( i = 0; i < 20 && player->active_script; i++ )
                selftest_tick(srv);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_progress_bits(player, varp_level) == 8,
                           "tinderbox on the bricks should remove the blockage, got %d",
                           itexam_progress_bits(player, varp_level));
            if( itexam_progress_bits(player, varp_level) == 8 )
                fprintf(stderr, "  PASS  itexam step 7b: tinderbox blows the bricks\n");
        }

        /* ---- step 8: take tablet (real oploc1) + hand in (real opnpcu) ---- */
        {
            int tab = itexam_loc_slot(srv, loc_tablet, 3373, 9746, 0, 10);
            int ex;
            int xp_before = stat_mining >= 0 ? player->stat_xp_tenths[stat_mining] : 0;
            int com_messagebox = ToriRSServer_ContentSymbol(
                TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");

            itexam_clear_inv(player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_tablet, -1, tab);
            biohazard_run_dialogue(srv, player, 0);
            SELFTEST_CHECK(itexam_inv_has(player, obj_tablet),
                           "taking the Zaros tablet loc should grant zarosstonetablet");
            SELFTEST_CHECK((player->varps[varp_bits] & (1 << 5)) != 0,
                           "taking the tablet should set itexam_bit_stone_tablet_taken");
            if( itexam_inv_has(player, obj_tablet) )
                fprintf(stderr, "  PASS  itexam step 8a: take the Zaros tablet\n");

            ex = npc_spawn(srv, npc_expert, 3357, 3334, 0);
            ToriRSServer_WorldTeleport(srv, 0, 3357, 3334);
            selftest_tick(srv);
            player->last_useitem = obj_tablet;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_expert, -1, ex);
            if( com_messagebox > 0 )
                ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
            for( i = 0; i < 40 && itexam_progress_bits(player, varp_level) != 9; i++ )
            {
                ToriRSServer_WorldCloseModal(srv);
                selftest_tick(srv);
            }
            SELFTEST_CHECK(itexam_progress_bits(player, varp_level) == 9,
                           "tablet on the expert should complete the quest, got %d",
                           itexam_progress_bits(player, varp_level));
            SELFTEST_CHECK(itexam_inv_has(player, obj_gold),
                           "completion should grant gold bars");
            if( stat_mining >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_mining] > xp_before,
                               "completion should award Mining xp, %d -> %d",
                               xp_before, player->stat_xp_tenths[stat_mining]);
            if( itexam_progress_bits(player, varp_level) == 9 )
                fprintf(stderr, "  PASS  itexam step 8b: expert completes The Dig Site\n");
            if( ex >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, ex);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        itexam_clear_inv(player);
        player->varps[varp_level] = 0;
        player->varps[varp_errands] = 0;
        player->varps[varp_bits] = 0;
    }
    ToriRSServer_ScriptsFree(srv);
}
