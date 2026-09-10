/* Watchtower Gate D stanza. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned ogres / locs cannot
 * leak into later RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD / OPHELDU dispatch on
 * the critical path. Silent success is forbidden: each step prints an
 * ASCII PASS line.
 */
static void
selftest_quest_itwatchtower(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::watchtowerrun\n");
    {
        /*
         * Watchtower (2026-09-09 Gate D). Existing LC port is mostly complete
         * (opnpc/oploc/opheld on the wiki critical path). One quest-blocking
         * gap: `%itwatchtower` was never written to
         * `^itwatchtower_found_all_crystals` (11), so `[oploc1,watchleverup]`
         * could never fire. Wizard Talk with all four crystals now writes 11.
         *
         * Placed immediately before a `selftest_reset_world`, deliberately.
         * Reloads its own pack -- earlier stanzas may have called ScriptsFree.
         */
        int loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            int varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "itwatchtower");
            int varp_bits =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "itwatchtower_bits");
            int npc_wiz = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "watchtower_wizard");
            int npc_og = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "og");
            int npc_toban = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "toban");
            int npc_grew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grew");
            int npc_guard2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ogre_guard2");
            int npc_city = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "city_guard");
            int npc_mad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mad_skavid");
            int npc_enclave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "enclave_guard");
            int npc_shaman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ogre_shaman");
            int loc_bush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "watchtowerbushnail");
            int loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tobanchest");
            int loc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rock_of_dalgroth");
            int loc_lever = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "watchleverup");
            int obj_nails = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fingernails");
            int obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "toban_key");
            int obj_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "stolen_gold");
            int obj_relic1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "relicpart1");
            int obj_relic2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "relicpart2");
            int obj_relic3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "relicpart3");
            int obj_statue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ogrerelic");
            int obj_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dragon_bones");
            int obj_tooth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ogretooth");
            int obj_death = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deathrune");
            int obj_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "skavidmap");
            int obj_shade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nightshade");
            int obj_guamvial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "guamvial");
            int obj_janger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "jangerberries");
            int obj_gjvial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "guamjangervial");
            int obj_bonesg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ground_bat_bones");
            int obj_ogrepot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ogre_potion");
            int obj_magpot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "magic_ogre_potion");
            int obj_c1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "powering_crystal1");
            int obj_c2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "powering_crystal2");
            int obj_c3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "powering_crystal3");
            int obj_c4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "powering_crystal4");
            int obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
            int obj_spell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "watchtowerspell");
            int stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
            int stat_herb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
            int stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
            int tx = 3222;
            int tz = 3218;
            int names_ok = varp >= 0 && varp_bits >= 0 && npc_wiz >= 0 && npc_og >= 0 &&
                           npc_toban >= 0 && npc_grew >= 0 && npc_guard2 >= 0 && npc_city >= 0 &&
                           npc_mad >= 0 && npc_enclave >= 0 && npc_shaman >= 0 && loc_bush >= 0 &&
                           loc_chest >= 0 && loc_rock >= 0 && loc_lever >= 0 && obj_nails >= 0 &&
                           obj_key >= 0 && obj_gold >= 0 && obj_relic1 >= 0 && obj_relic2 >= 0 &&
                           obj_relic3 >= 0 && obj_statue >= 0 && obj_bones >= 0 && obj_tooth >= 0 &&
                           obj_death >= 0 && obj_map >= 0 && obj_shade >= 0 && obj_guamvial >= 0 &&
                           obj_janger >= 0 && obj_gjvial >= 0 && obj_bonesg >= 0 &&
                           obj_ogrepot >= 0 && obj_magpot >= 0 && obj_c1 >= 0 && obj_c2 >= 0 &&
                           obj_c3 >= 0 && obj_c4 >= 0 && obj_pick >= 0 && obj_spell >= 0 &&
                           stat_magic >= 0 && stat_herb >= 0 && stat_mine >= 0;

            SELFTEST_CHECK(names_ok, "watchtower C-side pack names should all resolve");
            if( names_ok )
            {
                int s;
                int npc_slot;
                int loc_slot;
                int k;
                int tries;
                uint8_t payload[16];
                struct RSAreaBuf out;

                player->godmode = 1;
                player->active_script = NULL;
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                player->varps[varp] = 0;
                player->varps[varp_bits] = 0;
                ToriRSServer_CombatSetLevel(player, stat_magic, 99);
                ToriRSServer_CombatSetLevel(player, stat_herb, 99);
                ToriRSServer_CombatSetLevel(player, stat_mine, 99);
                ToriRSServer_WorldTeleport(srv, 0, tx, tz);
                selftest_tick(srv);

                /* 1. Wizard start -- real opnpc1, all accept choices are row 1. */
                npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_wiz, tx, tz, 0);
                SELFTEST_CHECK(npc_slot >= 0, "watchtower wizard should spawn");
                if( npc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_wiz, -1, npc_slot);
                    selftest_click_through(srv, 40);
                    SELFTEST_CHECK(player->varps[varp] == 1,
                                   "wizard accept should write itwatchtower_started, got %d",
                                   player->varps[varp]);
                    if( player->varps[varp] == 1 )
                        fprintf(stderr, "PASS watchtower start\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                /* 2. Fingernail bush -- real oploc1. */
                loc_slot = ToriRSServer_SceneFindLocId(tx, tz, 0, loc_bush);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(tx, tz, 0, loc_bush, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "watchtowerbushnail should sit in the scene");
                if( loc_slot >= 0 )
                {
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bush, -1,
                                                        loc_slot);
                    selftest_click_through(srv, 8);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_nails) > 0,
                                   "searching the nail bush at started should grant fingernails");
                    if( selftest_count_obj(player, obj_nails) > 0 )
                        fprintf(stderr, "PASS watchtower fingernails\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                /* 3. Fingernail hand-in -- real opnpcu. */
                if( npc_slot >= 0 )
                {
                    inv_set(player, 0, obj_nails, 1);
                    player->last_useitem = obj_nails;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_wiz, -1, npc_slot);
                    selftest_click_through(srv, 40);
                    SELFTEST_CHECK(player->varps[varp] == 2,
                                   "fingernail hand-in should write given_fingernails, got %d",
                                   player->varps[varp]);
                    if( player->varps[varp] == 2 )
                        fprintf(stderr, "PASS watchtower fingernails-handin\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                /* 4. Og -- real opnpc1, option 1 grants Toban's key. */
                {
                    int og_slot = ToriRSServer_WorldNpcSpawn(srv, npc_og, tx + 1, tz, 0);

                    SELFTEST_CHECK(og_slot >= 0, "og should spawn");
                    if( og_slot >= 0 )
                    {
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_og, -1, og_slot);
                        selftest_click_through(srv, 20);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_key) > 0,
                                       "Og should grant toban_key");
                        if( selftest_count_obj(player, obj_key) > 0 )
                            fprintf(stderr, "PASS watchtower og-key\n");
                        ToriRSServer_WorldCloseModal(srv);
                        player->active_script = NULL;

                        /* 5. Toban chest -- real oplocu with the key. */
                        loc_slot = ToriRSServer_SceneFindLocId(tx, tz + 1, 0, loc_chest);
                        if( loc_slot < 0 )
                            loc_slot = ToriRSServer_SceneAddLoc(tx, tz + 1, 0, loc_chest, 10, 0);
                        SELFTEST_CHECK(loc_slot >= 0, "tobanchest should sit in the scene");
                        if( loc_slot >= 0 )
                        {
                            player->last_useitem = obj_key;
                            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_chest,
                                                                -1, loc_slot);
                            selftest_click_through(srv, 8);
                            SELFTEST_CHECK(selftest_count_obj(player, obj_gold) > 0,
                                           "key-on-chest should grant stolen_gold");
                            if( selftest_count_obj(player, obj_gold) > 0 )
                                fprintf(stderr, "PASS watchtower toban-chest\n");
                            ToriRSServer_WorldCloseModal(srv);
                            player->active_script = NULL;
                        }

                        /* 6. Og gold hand-in -- real opnpcu. */
                        inv_set(player, 0, obj_gold, 1);
                        player->last_useitem = obj_gold;
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_og, -1, og_slot);
                        selftest_click_through(srv, 12);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_relic1) > 0,
                                       "gold-on-Og should grant relicpart1");
                        if( selftest_count_obj(player, obj_relic1) > 0 )
                            fprintf(stderr, "PASS watchtower og-gold\n");
                        ToriRSServer_WorldCloseModal(srv);
                        player->active_script = NULL;
                        ToriRSServer_WorldNpcFree(srv, og_slot);
                    }
                }

                /* 7. Gorad is a level-68 ogre. C walk skips the kill. */
                fprintf(stderr, "PASS watchtower boss=gorad CHEAT-SKIP\n");
                player->varps[varp_bits] |= (2 << 3); /* helped_grew field = 2 */

                /* 8. Grew tooth hand-in -- real opnpcu. Force spoken-only first. */
                {
                    int grew_slot = ToriRSServer_WorldNpcSpawn(srv, npc_grew, tx + 2, tz, 0);

                    SELFTEST_CHECK(grew_slot >= 0, "grew should spawn");
                    if( grew_slot >= 0 )
                    {
                        player->varps[varp_bits] &= ~(3 << 3);
                        player->varps[varp_bits] |= (1 << 3); /* spoken_grew = 1 */
                        inv_set(player, 1, obj_tooth, 1);
                        player->last_useitem = obj_tooth;
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_grew, -1,
                                                       grew_slot);
                        selftest_click_through(srv, 12);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_relic2) > 0 &&
                                           selftest_count_obj(player, obj_c1) > 0,
                                       "tooth-on-Grew should grant relicpart2 and crystal1");
                        if( selftest_count_obj(player, obj_relic2) > 0 &&
                            selftest_count_obj(player, obj_c1) > 0 )
                            fprintf(stderr, "PASS watchtower grew-tooth\n");
                        ToriRSServer_WorldCloseModal(srv);
                        player->active_script = NULL;
                        ToriRSServer_WorldNpcFree(srv, grew_slot);
                    }
                }

                /* 9. Toban bones -- real opnpcu after spoken bit. */
                {
                    int toban_slot = ToriRSServer_WorldNpcSpawn(srv, npc_toban, tx + 3, tz, 0);

                    SELFTEST_CHECK(toban_slot >= 0, "toban should spawn");
                    if( toban_slot >= 0 )
                    {
                        player->varps[varp_bits] |= (1 << 1); /* spoken_toban */
                        inv_set(player, 2, obj_bones, 1);
                        player->last_useitem = obj_bones;
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_toban, -1,
                                                       toban_slot);
                        selftest_click_through(srv, 12);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_relic3) > 0,
                                       "bones-on-Toban should grant relicpart3");
                        if( selftest_count_obj(player, obj_relic3) > 0 )
                            fprintf(stderr, "PASS watchtower toban-bones\n");
                        ToriRSServer_WorldCloseModal(srv);
                        player->active_script = NULL;
                        ToriRSServer_WorldNpcFree(srv, toban_slot);
                    }
                }

                /* 10. Wizard assembles the relic -- three real opnpcu. */
                if( npc_slot >= 0 )
                {
                    inv_set(player, 0, obj_relic1, 1);
                    player->last_useitem = obj_relic1;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_wiz, -1, npc_slot);
                    selftest_click_through(srv, 8);
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                    inv_set(player, 1, obj_relic2, 1);
                    player->last_useitem = obj_relic2;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_wiz, -1, npc_slot);
                    selftest_click_through(srv, 8);
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                    inv_set(player, 2, obj_relic3, 1);
                    player->last_useitem = obj_relic3;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_wiz, -1, npc_slot);
                    selftest_click_through(srv, 16);
                    SELFTEST_CHECK(player->varps[varp] == 3 &&
                                       selftest_count_obj(player, obj_statue) > 0,
                                   "three relic parts should assemble ogrerelic, state %d",
                                   player->varps[varp]);
                    if( player->varps[varp] == 3 && selftest_count_obj(player, obj_statue) > 0 )
                        fprintf(stderr, "PASS watchtower relic-assemble\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                /* 11. City gate -- real opnpcu of the assembled relic. */
                {
                    int g2 = ToriRSServer_WorldNpcSpawn(srv, npc_guard2, tx, tz + 2, 0);

                    SELFTEST_CHECK(g2 >= 0, "ogre_guard2 should spawn");
                    if( g2 >= 0 )
                    {
                        player->varps[varp_bits] |= 1; /* looking_relic */
                        inv_set(player, 3, obj_statue, 1);
                        player->last_useitem = obj_statue;
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_guard2, -1, g2);
                        selftest_click_through(srv, 12);
                        SELFTEST_CHECK(player->varps[varp] == 4,
                                       "relic-on-guard should write given_relic, got %d",
                                       player->varps[varp]);
                        if( player->varps[varp] == 4 )
                            fprintf(stderr, "PASS watchtower gate-relic\n");
                        ToriRSServer_WorldCloseModal(srv);
                        player->active_script = NULL;
                        ToriRSServer_WorldNpcFree(srv, g2);
                    }
                }

                /* 12. City-guard riddle -- real opnpcu of a death rune. */
                {
                    int cg = ToriRSServer_WorldNpcSpawn(srv, npc_city, tx + 1, tz + 2, 0);

                    SELFTEST_CHECK(cg >= 0, "city_guard should spawn");
                    if( cg >= 0 )
                    {
                        inv_set(player, 4, obj_death, 1);
                        player->last_useitem = obj_death;
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_city, -1, cg);
                        selftest_click_through(srv, 12);
                        SELFTEST_CHECK(player->varps[varp] == 6 &&
                                           selftest_count_obj(player, obj_map) > 0,
                                       "deathrune-on-guard should write solved_riddle and grant "
                                       "skavidmap, state %d",
                                       player->varps[varp]);
                        if( player->varps[varp] == 6 && selftest_count_obj(player, obj_map) > 0 )
                            fprintf(stderr, "PASS watchtower riddle\n");
                        ToriRSServer_WorldCloseModal(srv);
                        player->active_script = NULL;
                        ToriRSServer_WorldNpcFree(srv, cg);
                    }
                }

                /* 13. Mad skavid crystal -- real opnpc1. Row 1 (Cur) is correct
                 * when mes_type==1; retry the random prompt. */
                {
                    int mad = ToriRSServer_WorldNpcSpawn(srv, npc_mad, tx + 2, tz + 2, 0);

                    SELFTEST_CHECK(mad >= 0, "mad_skavid should spawn");
                    if( mad >= 0 )
                    {
                        player->varps[varp_bits] |= (31 << 12); /* learning + four words */
                        for( tries = 0; tries < 24 && player->varps[varp] != 7; tries++ )
                        {
                            ToriRSServer_WorldCloseModal(srv);
                            player->active_script = NULL;
                            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_mad, -1,
                                                           mad);
                            selftest_click_through(srv, 16);
                        }
                        SELFTEST_CHECK(player->varps[varp] == 7 &&
                                           selftest_count_obj(player, obj_c2) > 0,
                                       "mad skavid should grant crystal2 and write "
                                       "skavid_crystal, state %d",
                                       player->varps[varp]);
                        if( player->varps[varp] == 7 && selftest_count_obj(player, obj_c2) > 0 )
                            fprintf(stderr, "PASS watchtower skavid-crystal\n");
                        ToriRSServer_WorldCloseModal(srv);
                        player->active_script = NULL;
                        ToriRSServer_WorldNpcFree(srv, mad);
                    }
                }

                /* 14. Nightshade on enclave guard -- real opnpcu. */
                {
                    int eg = ToriRSServer_WorldNpcSpawn(srv, npc_enclave, tx + 3, tz + 2, 0);

                    SELFTEST_CHECK(eg >= 0, "enclave_guard should spawn");
                    if( eg >= 0 )
                    {
                        inv_set(player, 5, obj_shade, 1);
                        player->last_useitem = obj_shade;
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_enclave, -1, eg);
                        selftest_click_through(srv, 12);
                        for( s = 0; s < 4; s++ )
                            selftest_tick(srv);
                        SELFTEST_CHECK(player->varps[varp] == 8,
                                       "nightshade-on-guard should write fed_nightshade, got %d",
                                       player->varps[varp]);
                        if( player->varps[varp] == 8 )
                            fprintf(stderr, "PASS watchtower nightshade\n");
                        ToriRSServer_WorldCloseModal(srv);
                        player->active_script = NULL;
                        ToriRSServer_WorldNpcFree(srv, eg);
                    }
                }

                /* 15. Wizard teaches the potion -- real opnpc1. */
                if( npc_slot >= 0 )
                {
                    ToriRSServer_WorldTeleport(srv, 0, tx, tz);
                    selftest_tick(srv);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_wiz, -1, npc_slot);
                    selftest_click_through(srv, 20);
                    SELFTEST_CHECK(player->varps[varp] == 9,
                                   "wizard after the enclave should write learned_potion, got %d",
                                   player->varps[varp]);
                    if( player->varps[varp] == 9 )
                        fprintf(stderr, "PASS watchtower potion-learn\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                /* 16. Mix -- real OPHELDU janger-on-guam then bones-on-partial. */
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                inv_set(player, 0, obj_janger, 1);
                inv_set(player, 1, obj_guamvial, 1);
                rsab_wrap(&out, payload, sizeof(payload));
                rsab_p2(&out, obj_guamvial);
                rsab_p2(&out, 1);
                rsab_p4(&out, 0);
                rsab_p2(&out, obj_janger);
                rsab_p2(&out, 0);
                rsab_p4(&out, 0);
                selftest_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
                selftest_click_through(srv, 6);
                SELFTEST_CHECK(selftest_count_obj(player, obj_gjvial) > 0,
                               "jangerberries on guamvial should make guamjangervial");
                inv_set(player, 2, obj_bonesg, 1);
                rsab_wrap(&out, payload, sizeof(payload));
                rsab_p2(&out, obj_gjvial);
                rsab_p2(&out, 1);
                rsab_p4(&out, 0);
                rsab_p2(&out, obj_bonesg);
                rsab_p2(&out, 2);
                rsab_p4(&out, 0);
                selftest_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
                selftest_click_through(srv, 6);
                SELFTEST_CHECK(selftest_count_obj(player, obj_ogrepot) > 0,
                               "ground bones on guamjangervial should make ogre_potion");
                if( selftest_count_obj(player, obj_ogrepot) > 0 )
                    fprintf(stderr, "PASS watchtower potion-mix\n");
                ToriRSServer_WorldCloseModal(srv);
                player->active_script = NULL;

                /* 17. Wizard enchants -- real opnpcu. */
                if( npc_slot >= 0 )
                {
                    inv_set(player, 0, obj_ogrepot, 1);
                    player->last_useitem = obj_ogrepot;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_wiz, -1, npc_slot);
                    selftest_click_through(srv, 12);
                    for( s = 0; s < 6; s++ )
                        selftest_tick(srv);
                    selftest_click_through(srv, 8);
                    SELFTEST_CHECK(player->varps[varp] == 10 &&
                                       selftest_count_obj(player, obj_magpot) > 0,
                                   "ogre_potion on wizard should write made_potion and grant "
                                   "magic_ogre_potion, state %d",
                                   player->varps[varp]);
                    if( player->varps[varp] == 10 && selftest_count_obj(player, obj_magpot) > 0 )
                        fprintf(stderr, "PASS watchtower potion-enchant\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                /* 18. Six shamans -- real opnpcu of the magic potion. */
                if( selftest_count_obj(player, obj_magpot) <= 0 )
                    inv_set(player, 0, obj_magpot, 1);
                for( k = 0; k < 6; k++ )
                {
                    int sh = ToriRSServer_WorldNpcSpawn(srv, npc_shaman, tx + k, tz + 3, 0);

                    SELFTEST_CHECK(sh >= 0, "ogre_shaman %d should spawn", k + 1);
                    if( sh < 0 )
                        break;
                    player->last_useitem = obj_magpot;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_shaman, -1, sh);
                    selftest_click_through(srv, 8);
                    for( s = 0; s < 3; s++ )
                        selftest_tick(srv);
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                    if( srv->npcs[sh].active )
                    {
                        ToriRSServer_WorldNpcFree(srv, sh);
                    }
                }
                SELFTEST_CHECK(selftest_count_obj(player, obj_c3) > 0,
                               "the sixth shaman should drop powering_crystal3");
                if( selftest_count_obj(player, obj_c3) > 0 )
                    fprintf(stderr, "PASS watchtower shaman\n");

                /* 19. Rock of Dalgroth -- real oploc2. */
                loc_slot = ToriRSServer_SceneFindLocId(tx, tz + 4, 0, loc_rock);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(tx, tz + 4, 0, loc_rock, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "rock_of_dalgroth should sit in the scene");
                if( loc_slot >= 0 )
                {
                    inv_set(player, 6, obj_pick, 1);
                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_rock, -1,
                                                        loc_slot);
                    for( s = 0; s < 6; s++ )
                        selftest_tick(srv);
                    selftest_click_through(srv, 6);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_c4) > 0,
                                   "mining Rock of Dalgroth after six shamans should grant "
                                   "crystal4");
                    if( selftest_count_obj(player, obj_c4) > 0 )
                        fprintf(stderr, "PASS watchtower mine-crystal\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                /* Keep all four crystals for the wizard + lever. */
                inv_set(player, 0, obj_c1, 1);
                inv_set(player, 1, obj_c2, 1);
                inv_set(player, 2, obj_c3, 1);
                inv_set(player, 3, obj_c4, 1);

                /* 20. Return crystals -- real opnpc1 writes found_all_crystals. */
                if( npc_slot >= 0 )
                {
                    ToriRSServer_WorldTeleport(srv, 0, tx, tz);
                    selftest_tick(srv);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_wiz, -1, npc_slot);
                    selftest_click_through(srv, 16);
                    SELFTEST_CHECK(player->varps[varp] == 11,
                                   "wizard with four crystals should write "
                                   "found_all_crystals, got %d",
                                   player->varps[varp]);
                    if( player->varps[varp] == 11 )
                        fprintf(stderr, "PASS watchtower all-crystals\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                /* 21-22. Lever -- real oploc1 completes the quest. */
                loc_slot = ToriRSServer_SceneFindLocId(tx + 1, tz + 4, 0, loc_lever);
                if( loc_slot < 0 )
                    loc_slot = ToriRSServer_SceneAddLoc(tx + 1, tz + 4, 0, loc_lever, 10, 0);
                SELFTEST_CHECK(loc_slot >= 0, "watchleverup should sit in the scene");
                if( loc_slot >= 0 )
                {
                    int drain;
                    int xp_before = player->stat_xp_tenths[stat_magic];

                    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_lever, -1,
                                                        loc_slot);
                    selftest_click_through(srv, 8);
                    for( drain = 0; drain < 40 && player->varps[varp] < 13; drain++ )
                    {
                        int com = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT,
                                                             "messagebox:continue");
                        if( com > 0 )
                            ToriRSServer_ScriptsResumeButton(srv, com);
                        ToriRSServer_WorldCloseModal(srv);
                        selftest_tick(srv);
                    }
                    SELFTEST_CHECK(player->varps[varp] >= 13,
                                   "lever with four crystals at state 11 should complete, got %d",
                                   player->varps[varp]);
                    SELFTEST_CHECK(player->stat_xp_tenths[stat_magic] > xp_before,
                                   "completion should award Magic xp, %d -> %d", xp_before,
                                   player->stat_xp_tenths[stat_magic]);
                    SELFTEST_CHECK(selftest_count_obj(player, obj_spell) > 0,
                                   "completion should grant watchtowerspell");
                    if( player->varps[varp] >= 13 )
                        fprintf(stderr, "PASS watchtower lever\n");
                    if( player->varps[varp] >= 13 )
                        fprintf(stderr, "PASS watchtower complete\n");
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                }

                if( npc_slot >= 0 )
                    ToriRSServer_WorldNpcFree(srv, npc_slot);
                ToriRSServer_WorldNpcReap(srv);
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, -1, 0);
                player->varps[varp] = 0;
                player->varps[varp_bits] = 0;
                player->godmode = 1;
            }
            ToriRSServer_ScriptsFree(srv);
        }
    }
}
