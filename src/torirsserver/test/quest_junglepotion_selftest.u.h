/* Jungle Potion Gate D stanza. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak into
 * later RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD dispatch on the critical
 * path. Silent success is forbidden: each step prints an ASCII PASS line.
 */
static void
jp_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "JUNGLEPOTION PASS: %s\n", step);
}

static int
jp_find_loc(
    int x,
    int z,
    int level,
    int loc_id,
    int radius,
    int* out_x,
    int* out_z)
{
    int dx;
    int dz;
    int slot;

    assert(radius >= 0);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot >= 0 )
    {
        if( out_x )
            *out_x = x;
        if( out_z )
            *out_z = z;
        return slot;
    }
    for( dx = -radius; dx <= radius; dx++ )
    {
        for( dz = -radius; dz <= radius; dz++ )
        {
            if( dx == 0 && dz == 0 )
                continue;
            slot = ToriRSServer_SceneFindLocId(x + dx, z + dz, level, loc_id);
            if( slot >= 0 )
            {
                if( out_x )
                    *out_x = x + dx;
                if( out_z )
                    *out_z = z + dz;
                return slot;
            }
        }
    }
    return -1;
}

static void
jp_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static void
jp_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
selftest_quest_junglepotion(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::junglepotionrun\n");

    /*
     * Reloads its own pack — preceding quest stanzas may have called
     * ToriRSServer_ScriptsFree (field guide S1 / Sea Slug).
     */
    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int loc_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "snake_vine_full");
        int loc_palm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ardrigal_palm_full");
        int loc_sito = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sito_soil_full");
        int loc_moss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "volencia_moss_rock_full");
        int loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rogues_purse_cave_full");
        int loc_wall_empty =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rogues_purse_cave_empty");
        int loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pothole_cave_entrance");
        int loc_climb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "jp_caverocksout");
        int varp_jp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "junglepotion");
        int varp_druid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "druidquest");
        int obj_unid_snake =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_snake_weed");
        int obj_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "snake_weed");
        int obj_unid_ardrigal =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_ardrigal");
        int obj_ardrigal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ardrigal");
        int obj_unid_sito =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_sito_foil");
        int obj_sito = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sito_foil");
        int obj_unid_volencia =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_volencia_moss");
        int obj_volencia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "volencia_moss");
        int obj_unid_rogues =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_rogues_purse");
        int obj_rogues = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rogues_purse");
        int npc_trufitus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "trufitus");
        int stat_herblore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
        int com_messagebox =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");

        SELFTEST_CHECK(loc_snake >= 0 && loc_palm >= 0 && loc_sito >= 0 && loc_moss >= 0 &&
                           loc_wall >= 0 && loc_wall_empty >= 0 && loc_cave >= 0 &&
                           loc_climb >= 0 && varp_jp >= 0 && varp_druid >= 0 &&
                           obj_unid_snake >= 0 && obj_snake >= 0 && obj_unid_ardrigal >= 0 &&
                           obj_ardrigal >= 0 && obj_unid_sito >= 0 && obj_sito >= 0 &&
                           obj_unid_volencia >= 0 && obj_volencia >= 0 && obj_unid_rogues >= 0 &&
                           obj_rogues >= 0 && npc_trufitus >= 0 && stat_herblore >= 0,
                       "the ::junglepotionrun C-side names should all resolve");
        if( loc_snake < 0 || varp_jp < 0 || npc_trufitus < 0 || stat_herblore < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        /* Player unkillable unless this step is a death test. It is not. */
        player->godmode = 1;
        player->dying = 0;
        if( player->max_hitpoints > 0 )
            player->hitpoints = player->max_hitpoints;
        ToriRSServer_CombatSyncHitpoints(player);
        ToriRSServer_CombatSetLevel(player, stat_herblore, 3);
        player->varps[varp_druid] = 4; /* ^druid_complete */
        jp_clear_inv(player);
        player->varps[varp_jp] = 0;

        /* ---- OPNPC1 start trigger fires (state 0) ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2809, 3086);
        selftest_tick(srv);
        {
            int npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_trufitus, 2809, 3086, 0);
            int if_chat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "chat_left");
            int if_messagebox =
                ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "messagebox");

            SELFTEST_CHECK(npc_slot >= 0, "trufitus should spawn for the start talk");
            if( npc_slot >= 0 )
            {
                player->chatmodal_group = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_trufitus, -1, npc_slot);
                SELFTEST_CHECK(player->chatmodal_group != 0 || player->active_script != NULL,
                               "opnpc1 trufitus at state 0 should open the offer tree");
                jp_close(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 0,
                               "declining / aborting the offer must not write junglepotion, got %d",
                               player->varps[varp_jp]);
                jp_pass("opnpc1_trufitus_offer");
                (void)if_chat;
                (void)if_messagebox;

                /* ---- OPLOC2 snake vine: wrong state then right state ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2763, 3044);
                selftest_tick(srv);
                {
                    int vine_x = 2763;
                    int vine_z = 3044;
                    int loc_slot = jp_find_loc(2763, 3044, 0, loc_snake, 4, &vine_x, &vine_z);

                    SELFTEST_CHECK(loc_slot >= 0,
                                   "snake_vine_full should resolve near 2763,3044");
                    if( loc_slot >= 0 )
                    {
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_snake, -1,
                                                            loc_slot);
                        jp_close(srv);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_unid_snake) == 0,
                                       "searching the vine before get_snake_weed should find "
                                       "nothing, got count=%d",
                                       selftest_count_obj(player, obj_unid_snake));
                        jp_pass("oploc2_snake_vine_gated");

                        player->varps[varp_jp] = 1; /* get_snake_weed */
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_snake, -1,
                                                            loc_slot);
                        jp_close(srv);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_unid_snake) == 1,
                                       "searching the vine at get_snake_weed should grant "
                                       "unidentified_snake_weed, got count=%d",
                                       selftest_count_obj(player, obj_unid_snake));
                        SELFTEST_CHECK(player->varps[varp_jp] == 2,
                                       "picking should advance to found_snake_weed, got %d",
                                       player->varps[varp_jp]);
                        jp_pass("oploc2_snake_vine");
                    }
                }

                /* ---- OPHELD1 clean grimy snake weed ---- */
                jp_clear_inv(player);
                inv_set(player, 0, obj_unid_snake, 1);
                player->last_item = obj_unid_snake;
                player->last_slot = 0;
                player->last_verb = 1;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_unid_snake, -1, -1);
                jp_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_snake) == 1 &&
                                   selftest_count_obj(player, obj_unid_snake) == 0,
                               "opheld1 unidentified_snake_weed should clean to snake_weed");
                jp_pass("opheld1_clean_snake_weed");

                /* ---- OPNPCU dirty decline, then clean accept ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2809, 3086);
                selftest_tick(srv);
                player->varps[varp_jp] = 2;
                inv_set(player, 0, obj_unid_snake, 1);
                player->last_useitem = obj_unid_snake;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                jp_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_unid_snake) == 1,
                               "handing grimy snake weed should be declined");
                jp_pass("opnpcu_decline_dirty_snake");

                jp_clear_inv(player);
                inv_set(player, 0, obj_snake, 1);
                player->last_useitem = obj_snake;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                jp_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_snake) == 0,
                               "handing clean snake_weed should consume it");
                SELFTEST_CHECK(player->varps[varp_jp] == 3,
                               "accepting snake_weed should advance to get_ardrigal, got %d",
                               player->varps[varp_jp]);
                jp_pass("opnpcu_handin_snake_weed");

                /* ---- OPLOC2 ardrigal palm (QH 2871,3116) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2871, 3116);
                selftest_tick(srv);
                {
                    int palm_x = 2871;
                    int palm_z = 3116;
                    int loc_slot = jp_find_loc(2871, 3116, 0, loc_palm, 6, &palm_x, &palm_z);

                    SELFTEST_CHECK(loc_slot >= 0,
                                   "ardrigal_palm_full should resolve near 2871,3116");
                    SELFTEST_CHECK(player->godmode == 1 && player->dying == 0,
                                   "player must stay unkillable at the harpie peninsula");
                    if( loc_slot >= 0 )
                    {
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_palm, -1,
                                                            loc_slot);
                        jp_close(srv);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_unid_ardrigal) == 1,
                                       "searching the palm should grant unidentified_ardrigal");
                        SELFTEST_CHECK(player->varps[varp_jp] == 4,
                                       "picking should advance to found_ardrigal, got %d",
                                       player->varps[varp_jp]);
                        jp_pass("oploc2_ardrigal_palm");
                    }
                }

                jp_clear_inv(player);
                inv_set(player, 0, obj_unid_ardrigal, 1);
                player->last_item = obj_unid_ardrigal;
                player->last_slot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_unid_ardrigal, -1, -1);
                jp_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_ardrigal) == 1,
                               "opheld1 should clean unidentified_ardrigal");
                jp_pass("opheld1_clean_ardrigal");

                player->varps[varp_jp] = 4;
                player->last_useitem = obj_ardrigal;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                jp_close(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 5,
                               "accepting ardrigal should advance to get_sito_foil, got %d",
                               player->varps[varp_jp]);
                jp_pass("opnpcu_handin_ardrigal");

                /* ---- OPLOC2 sito foil (QH 2791,3047) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2791, 3047);
                selftest_tick(srv);
                {
                    int sito_x = 2791;
                    int sito_z = 3047;
                    int loc_slot = jp_find_loc(2791, 3047, 0, loc_sito, 6, &sito_x, &sito_z);

                    SELFTEST_CHECK(loc_slot >= 0, "sito_soil_full should resolve near 2791,3047");
                    if( loc_slot >= 0 )
                    {
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_sito, -1,
                                                            loc_slot);
                        jp_close(srv);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_unid_sito) == 1,
                                       "searching scorched earth should grant unidentified_sito_foil");
                        SELFTEST_CHECK(player->varps[varp_jp] == 6,
                                       "picking should advance to found_sito_foil, got %d",
                                       player->varps[varp_jp]);
                        jp_pass("oploc2_sito_foil");
                    }
                }

                jp_clear_inv(player);
                inv_set(player, 0, obj_sito, 1);
                player->last_useitem = obj_sito;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                jp_close(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 7,
                               "accepting sito_foil should advance to get_volencia_moss, got %d",
                               player->varps[varp_jp]);
                jp_pass("opnpcu_handin_sito_foil");

                /* ---- OPLOC2 volencia moss (QH 2851,3036) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2851, 3036);
                selftest_tick(srv);
                {
                    int moss_x = 2851;
                    int moss_z = 3036;
                    int loc_slot = jp_find_loc(2851, 3036, 0, loc_moss, 6, &moss_x, &moss_z);

                    SELFTEST_CHECK(loc_slot >= 0,
                                   "volencia_moss_rock_full should resolve near 2851,3036");
                    if( loc_slot >= 0 )
                    {
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_moss, -1,
                                                            loc_slot);
                        jp_close(srv);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_unid_volencia) == 1,
                                       "searching the rock should grant unidentified_volencia_moss");
                        SELFTEST_CHECK(player->varps[varp_jp] == 8,
                                       "picking should advance to found_volencia_moss, got %d",
                                       player->varps[varp_jp]);
                        jp_pass("oploc2_volencia_moss");
                    }
                }

                jp_clear_inv(player);
                inv_set(player, 0, obj_volencia, 1);
                player->last_useitem = obj_volencia;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                jp_close(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 9,
                               "accepting volencia_moss should advance to get_rogues_purse, got %d",
                               player->varps[varp_jp]);
                jp_pass("opnpcu_handin_volencia_moss");

                /* ---- OPLOC2 pothole cave enter (QH 2825,3119) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2825, 3119);
                selftest_tick(srv);
                {
                    int cave_x = 2825;
                    int cave_z = 3119;
                    int loc_slot = jp_find_loc(2825, 3119, 0, loc_cave, 6, &cave_x, &cave_z);

                    SELFTEST_CHECK(loc_slot >= 0,
                                   "pothole_cave_entrance should resolve near 2825,3119");
                    if( loc_slot >= 0 )
                    {
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_cave, -1,
                                                            loc_slot);
                        selftest_click_through(srv, 8);
                        SELFTEST_CHECK(player->x == 2830 && player->z == 9520,
                                       "accepting cave entry should telejump to 2830,9520 "
                                       "(0_44_148_14_48), got %d,%d",
                                       player->x, player->z);
                        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                                       "cave entry must not kill the player");
                        jp_pass("oploc2_enter_pothole");
                    }
                }

                /* Scene after the telejump is rebuilt on the next tick. */
                selftest_tick(srv);

                /* ---- OPLOC2 rogue's purse wall inside the cave ---- */
                {
                    int wall_x = 2830;
                    int wall_z = 9520;
                    int loc_slot = jp_find_loc(2830, 9520, 0, loc_wall, 24, &wall_x, &wall_z);
                    int empty_x = 0;
                    int empty_z = 0;
                    int empty_slot;

                    SELFTEST_CHECK(loc_slot >= 0,
                                   "rogues_purse_cave_full should resolve near 2830,9520 "
                                   "(not the void at 0_44_147_8_54)");
                    if( loc_slot >= 0 )
                    {
                        fprintf(stderr,
                                "JUNGLEPOTION loc rogues_purse_cave_full at %d,%d\n",
                                wall_x, wall_z);
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_wall, -1,
                                                            loc_slot);
                        jp_close(srv);
                        SELFTEST_CHECK(selftest_count_obj(player, obj_unid_rogues) == 1,
                                       "searching the fungus wall should grant "
                                       "unidentified_rogues_purse");
                        SELFTEST_CHECK(player->varps[varp_jp] == 10,
                                       "picking should advance to found_rogues_purse, got %d",
                                       player->varps[varp_jp]);
                        empty_slot =
                            jp_find_loc(wall_x, wall_z, 0, loc_wall_empty, 2, &empty_x, &empty_z);
                        SELFTEST_CHECK(empty_slot >= 0,
                                       "the wall should loc_change to rogues_purse_cave_empty");
                        jp_pass("oploc2_rogues_purse");
                    }
                }

                jp_clear_inv(player);
                inv_set(player, 0, obj_unid_rogues, 1);
                player->last_item = obj_unid_rogues;
                player->last_slot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_unid_rogues, -1, -1);
                jp_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_rogues) == 1,
                               "opheld1 should clean unidentified_rogues_purse");
                jp_pass("opheld1_clean_rogues_purse");

                /* ---- OPLOC1 climb out (QH 2830,9522) ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2830, 9522);
                selftest_tick(srv);
                {
                    int climb_x = 2830;
                    int climb_z = 9522;
                    int loc_slot = jp_find_loc(2830, 9522, 0, loc_climb, 8, &climb_x, &climb_z);

                    SELFTEST_CHECK(loc_slot >= 0,
                                   "jp_caverocksout should resolve near 2830,9522");
                    if( loc_slot >= 0 )
                    {
                        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_climb, -1,
                                                            loc_slot);
                        selftest_click_through(srv, 4);
                        SELFTEST_CHECK(player->z < 6400,
                                       "climbing out should return to the surface, z=%d",
                                       player->z);
                        jp_pass("oploc1_climb_pothole");
                    }
                }

                /* ---- final hand-in + completion queue ---- */
                ToriRSServer_WorldTeleport(srv, 0, 2809, 3086);
                selftest_tick(srv);
                player->varps[varp_jp] = 10;
                jp_clear_inv(player);
                inv_set(player, 0, obj_rogues, 1);
                player->last_useitem = obj_rogues;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_trufitus, -1, npc_slot);
                jp_close(srv);
                SELFTEST_CHECK(player->varps[varp_jp] == 11,
                               "accepting rogues_purse should advance to found_all_herbs, got %d",
                               player->varps[varp_jp]);
                jp_pass("opnpcu_handin_rogues_purse");

                {
                    int xp_before = player->stat_xp_tenths[stat_herblore];
                    int drain_tick;

                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_trufitus, -1,
                                                   npc_slot);
                    if( com_messagebox > 0 )
                        ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                    for( drain_tick = 0; drain_tick < 40 && player->varps[varp_jp] != 12;
                         drain_tick++ )
                    {
                        ToriRSServer_WorldCloseModal(srv);
                        selftest_tick(srv);
                    }
                    SELFTEST_CHECK(player->varps[varp_jp] == 12,
                                   "the real junglepotion_quest_complete queue should advance "
                                   "to complete, got %d",
                                   player->varps[varp_jp]);
                    SELFTEST_CHECK(player->stat_xp_tenths[stat_herblore] > xp_before,
                                   "completion should award real Herblore xp, %d -> %d",
                                   xp_before, player->stat_xp_tenths[stat_herblore]);
                    jp_pass("opnpc1_complete");
                }

                /* Post-quest talk writes state 13. */
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_trufitus, -1, npc_slot);
                selftest_click_through(srv, 12);
                SELFTEST_CHECK(player->varps[varp_jp] == 13,
                               "post-quest talk should advance to complete_after_spoken, got %d",
                               player->varps[varp_jp]);
                jp_pass("opnpc1_postquest");

                ToriRSServer_WorldNpcFree(srv, npc_slot);
                ToriRSServer_WorldNpcReap(srv);
            }
        }

        jp_clear_inv(player);
        player->varps[varp_jp] = 0;
        player->godmode = 1;
        player->dying = 0;
    }
    ToriRSServer_ScriptsFree(srv);
}
