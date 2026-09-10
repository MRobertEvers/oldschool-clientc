/* Jungle Potion C walk. Included immediately before selftest_reset_world.
 * Required pointers abort; allocation failure is assert. Player stays
 * unkillable for the whole walk. Gate D named BMPs live in OSRS-Content
 * osrs239-content/server/scripts/selftest/quest_junglepotion/. */
assert(srv);
assert(player);
player->godmode = 1;
/*
 * Jungle Potion (2026-08-20 audit). Read in full first, this quest
 * was already well-ported -- trufitus.rs2's 5-herb switch, the
 * pick/decline/wrong-herb dialogue branches, the journal's 13
 * declared states, and the completion reward (1 QP + 775 Herblore
 * XP, matching the wiki verbatim) all matched. Confirmed for real
 * this pass, not assumed: every one of the 7 quest-authored loc
 * symbols (`snake_vine_full`, `ardrigal_palm_full`, `sito_soil_
 * full`, `volencia_moss_rock_full`, `rogues_purse_cave_full`,
 * `pothole_cave_entrance`, `jp_caverocksout`) IS genuinely placed
 * in this cache's map data -- a live C-side scan found real
 * coordinates for all 7 (unlike Sea Slug's three dead symbols the
 * same technique caught two iterations ago), so this quest did not
 * have that class of bug. No fix landed; this stanza exists to
 * prove the audit was real, not just a clean static read.
 *
 * Reloads its own pack rather than assuming one is already loaded --
 * the preceding ::seaslugrun stanza calls ToriRSServer_ScriptsFree at
 * its own end, so without this every dispatch below silently
 * returned TORIRSSERVER_TRIGGER_NONE (srv->scripts_ok false, checked
 * before the trigger table is even consulted) with no error to
 * explain why. Confirmed live: real loc AND real npc dispatch both
 * failed identically before this was added.
 */
{
        int loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());

        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(
                srv, selftest_scripts_dir_from_src());

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
        int loc_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "snake_vine_full");
        int loc_palm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ardrigal_palm_full");
        int loc_sito = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sito_soil_full");
        int loc_volencia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "volencia_moss_rock_full");
        int loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rogues_purse_cave_full");
        int loc_pothole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pothole_cave_entrance");
        int loc_climb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "jp_caverocksout");
        int varp_jp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "junglepotion");
        int obj_unid_snake =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_snake_weed");
        int obj_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "snake_weed");
        int obj_ardrigal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ardrigal");
        int obj_sito = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sito_foil");
        int obj_volencia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "volencia_moss");
        int obj_rogues = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rogues_purse");
        int npc_trufitus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trufitus");
        int stat_herblore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");

        SELFTEST_CHECK(loc_snake >= 0 && loc_palm >= 0 && loc_sito >= 0 && loc_volencia >= 0 &&
                           loc_wall >= 0 && loc_pothole >= 0 && loc_climb >= 0 && varp_jp >= 0 &&
                           obj_unid_snake >= 0 && obj_snake >= 0 && obj_ardrigal >= 0 &&
                           obj_sito >= 0 && obj_volencia >= 0 && obj_rogues >= 0 &&
                           npc_trufitus >= 0 && stat_herblore >= 0,
                       "the ::junglepotionrun C-side names should all resolve");
        (void)loc_palm;
        (void)loc_sito;
        (void)loc_volencia;
        (void)loc_wall;
        (void)loc_pothole;
        (void)loc_climb;

        if( loc_snake >= 0 && varp_jp >= 0 && obj_unid_snake >= 0 && obj_snake >= 0 &&
            obj_ardrigal >= 0 && obj_sito >= 0 && obj_volencia >= 0 && obj_rogues >= 0 &&
            npc_trufitus >= 0 && stat_herblore >= 0 )
        {
            int s;
            int npc_slot;
            int loc_slot;

            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, -1, 0);
            player->varps[varp_jp] = 0;
            player->godmode = 1;

            /* ---- the real snake_vine_full pickup: wrong state, then right state ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2763, 3044);
            selftest_tick(srv);
            loc_slot = ToriRSServer_SceneFindLocId(2763, 3044, 0, loc_snake);
            SELFTEST_CHECK(loc_slot >= 0, "snake_vine_full should resolve to a real scene slot");
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_snake, -1, loc_slot);
            SELFTEST_CHECK(player->inv[0].obj_id != obj_unid_snake,
                           "searching the vine before ^junglepotion_get_snake_weed should find "
                           "nothing, got obj_id=%d",
                           player->inv[0].obj_id);

            player->varps[varp_jp] = 1; /* junglepotion_get_snake_weed */
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_snake, -1, loc_slot);
            SELFTEST_CHECK(player->inv[0].obj_id == obj_unid_snake,
                           "searching the vine at ^junglepotion_get_snake_weed should grant "
                           "unidentified_snake_weed, got obj_id=%d",
                           player->inv[0].obj_id);
            SELFTEST_CHECK(player->varps[varp_jp] == 2 /* junglepotion_found_snake_weed */,
                           "picking the herb should advance junglepotion to "
                           "junglepotion_found_snake_weed, got %d",
                           player->varps[varp_jp]);

            /* ---- the real opnpcu hand-in: dirty herb declined, clean herb accepted ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2809, 3086);
            selftest_tick(srv);
            npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_trufitus, 2809, 3086, 0);
            SELFTEST_CHECK(npc_slot >= 0, "trufitus should spawn for the hand-in check");
            if( npc_slot >= 0 )
            {
                inv_set(player, 0, obj_unid_snake, 1);
                player->last_useitem = obj_unid_snake;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                /* Every accept branch ends in `~mesbox`, which parks the
                 * same way `~chatnpc_anim` does -- left alone, the NEXT
                 * dispatch below finds the player's one script slot still
                 * held and gets dropped instead of run ("dropping [proc,
                 * mesbox], which suspended while [proc,mesbox] waits",
                 * confirmed live). Close it out before moving on. */
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->inv[0].obj_id == obj_unid_snake,
                               "handing over unidentified_snake_weed should be declined as "
                               "dirty, item should stay, got obj_id=%d",
                               player->inv[0].obj_id);

                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                inv_set(player, 0, obj_snake, 1);
                player->last_useitem = obj_snake;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->inv[0].obj_id != obj_snake,
                               "handing over clean snake_weed should be accepted and consumed, "
                               "got obj_id=%d",
                               player->inv[0].obj_id);
                SELFTEST_CHECK(player->varps[varp_jp] == 3 /* junglepotion_get_ardrigal */,
                               "accepting snake_weed should advance junglepotion to "
                               "junglepotion_get_ardrigal, got %d",
                               player->varps[varp_jp]);

                /* Real hand-in through the remaining three per-herb switch
                 * branches -- each is its own hand-written case in
                 * trufitus.rs2's `trufitus_accept_herb`, worth proving each
                 * one individually fires rather than trusting the first
                 * proves the pattern. */
                player->varps[varp_jp] = 4; /* junglepotion_found_ardrigal */
                inv_set(player, 0, obj_ardrigal, 1);
                player->last_useitem = obj_ardrigal;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 5 /* junglepotion_get_sito_foil */,
                               "accepting ardrigal should advance junglepotion to "
                               "junglepotion_get_sito_foil, got %d",
                               player->varps[varp_jp]);

                player->varps[varp_jp] = 6; /* junglepotion_found_sito_foil */
                inv_set(player, 0, obj_sito, 1);
                player->last_useitem = obj_sito;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 7 /* junglepotion_get_volencia_moss */,
                               "accepting sito_foil should advance junglepotion to "
                               "junglepotion_get_volencia_moss, got %d",
                               player->varps[varp_jp]);

                player->varps[varp_jp] = 8; /* junglepotion_found_volencia_moss */
                inv_set(player, 0, obj_volencia, 1);
                player->last_useitem = obj_volencia;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 9 /* junglepotion_get_rogues_purse */,
                               "accepting volencia_moss should advance junglepotion to "
                               "junglepotion_get_rogues_purse, got %d",
                               player->varps[varp_jp]);

                player->varps[varp_jp] = 10; /* junglepotion_found_rogues_purse */
                inv_set(player, 0, obj_rogues, 1);
                player->last_useitem = obj_rogues;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 11 /* junglepotion_found_all_herbs */,
                               "accepting rogues_purse should advance junglepotion to "
                               "junglepotion_found_all_herbs, got %d",
                               player->varps[varp_jp]);

                /* ---- the real completion queue ----
                 * `trufitus_finish_quest` is reached from `[opnpc1,trufitus]`
                 * at junglepotion_found_all_herbs with no p_choice in the
                 * way -- but it OPENS with `~mesbox`, and `queue(junglepotion
                 * _quest_complete, 0, 0)` is the line right after it, so the
                 * queue is not even ARMED until that mesbox's own pause is
                 * released. `ToriRSServer_WorldCloseModal` ABORTS a parked
                 * script rather than resuming it (confirmed live: looping it
                 * alone left junglepotion stuck at 11 forever, the queue
                 * never armed) -- this clicks the real `messagebox:continue`
                 * resume button instead, same lesson as Sea Slug's Caroline
                 * dialogue, then falls back to the Hazeel Cult queue-drain
                 * fix (field guide S5) once the queue is actually armed. */
                {
                    int xp_before = player->stat_xp_tenths[stat_herblore];
                    int drain_tick;
                    int com_messagebox =
                        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT,
                                                    "messagebox:continue");

                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_trufitus, -1,
                                                   npc_slot);
                    if( com_messagebox > 0 )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    for( drain_tick = 0;
                         drain_tick < 40 && player->varps[varp_jp] != 12; drain_tick++ )
                    {
                        ToriRSServer_WorldCloseModal(srv);
                        selftest_tick(srv);
                    }
                    SELFTEST_CHECK(
                        player->varps[varp_jp] == 12 /* junglepotion_complete */,
                        "the real junglepotion_quest_complete queue should advance "
                        "junglepotion to junglepotion_complete, got %d",
                        player->varps[varp_jp]);
                    SELFTEST_CHECK(player->stat_xp_tenths[stat_herblore] > xp_before,
                                   "completion should award real Herblore xp, %d -> %d",
                                   xp_before, player->stat_xp_tenths[stat_herblore]);
                }

                ToriRSServer_WorldNpcFree(srv, npc_slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, -1, 0);
            player->varps[varp_jp] = 0;
        }
        ToriRSServer_ScriptsFree(srv);
        }
}
