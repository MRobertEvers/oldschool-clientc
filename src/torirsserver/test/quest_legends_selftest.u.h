/* Legends' Quest Gate D stanza. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak into
 * later RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD dispatch on the critical
 * path. Silent success is forbidden: each step prints an ASCII PASS line.
 *
 * Disclosed soft-skips (do not hide; not verified-modern):
 *  - gem/rune shrine is one gated shortcut, not the per-rock SMELL + gem row
 *  - jungle map is one anywhere-in-Kharazi action, not three Crafting-50 zones
 *  - Gujuo gold-bowl smithing is one gold_bar use, not anvil + two bars
 *  - fire wall is one douse, not the multi-segment reignite puzzle
 *  - magic gate / winch / crystal furnace / dragon eye / heart recess are
 *    still unwired; the boulder + lgwaterpool path owns states 19-25
 *  - Yommi growth is a compressed single-player loc sequence
 *  - dense-jungle cutting is still the shared service gap
 */
static void
lg_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "LEGENDS PASS: %s\n", step);
}

static void
lg_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static void
lg_drain(struct ToriRSServer* srv, int max_steps)
{
    int i;
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    /* CloseModal aborts a parked writer (p_delay, post-mesbox state). Drain
     * continue buttons and delay ticks until the script finishes on its own. */
    for( i = 0; i < max_steps && player->active_script; i++ )
    {
        if( player->resume_button_count > 0 )
            selftest_click_through(srv, 1);
        else
            selftest_tick(srv);
    }
}

static void
lg_click(struct ToriRSServer* srv, int max_pages)
{
    assert(srv);
    selftest_click_through(srv, max_pages);
    lg_drain(srv, 48);
}

static int
lg_place_loc(
    struct ToriRSServer* srv,
    int x,
    int z,
    int loc_id)
{
    int placed;
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    ToriRSServer_WorldTeleport(srv, 0, x, z);
    selftest_tick(srv);
    placed = ToriRSServer_WorldLocSet(srv, x + 1, z, 0, 10, loc_id, 0,
                                      TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x + 1, z, 0, loc_id);
    if( slot >= 0 )
        return slot;
    /* Without cache.osrs239 SceneAddLoc has no loc_config, so the ZoneMap
     * record cannot become a scene slot. The OPLOC type trigger still runs. */
    (void)placed;
    return -1;
}

static void
selftest_quest_legends(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::legendsrun\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "legends_guild_guard1");
        int npc_radimus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "radimus_erkle");
        int npc_forester = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "jungleforester_m");
        int npc_gujuo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gujuo");
        int npc_ungadulu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ungadulu_bad");
        int loc_barrier =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "legendsquest_force_barrier");
        int loc_fire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lqfirewall_straight");
        int loc_pool = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sacred_water");
        int loc_lgpool = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lgwaterpool");
        int varp_lg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "legendsquest");
        int varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        int varp_hero = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "heroquest");
        int varp_crest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "crestquest");
        int varp_zq = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "zombiequeen");
        int varp_upass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "upass");
        int varp_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "waterfall_quest");
        int obj_notes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thkaramjamap");
        int obj_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thkaramjamapcomp");
        int obj_papyrus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "papyrus");
        int obj_charcoal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "charcoal");
        int obj_bull = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bullroarer");
        int obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "book_of_binding");
        int obj_soul = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "soulrune");
        int obj_mind = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mindrune");
        int obj_earth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "earthrune");
        int obj_law = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lawrune");
        int obj_opal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "opal");
        int obj_jade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "jade");
        int obj_topaz = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "red_topaz");
        int obj_sapp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sapphire");
        int obj_em = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "emerald");
        int obj_ruby = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ruby");
        int obj_dia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "diamond");
        int obj_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gold_bar");
        int obj_bowl_bless =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goldbowlbless_empty");
        int obj_bowl_pure =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goldbowlbless_pure");
        int obj_vial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vial_empty");
        int obj_vial_enc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vial_enchanted");
        int obj_totem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thtotempolegift");
        int stat_attack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
        int stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
        int stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
        int stat_prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
        int com_messagebox =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");

        SELFTEST_CHECK(npc_guard >= 0 && npc_radimus >= 0 && npc_forester >= 0 &&
                           npc_gujuo >= 0 && npc_ungadulu >= 0 && loc_barrier >= 0 &&
                           loc_fire >= 0 && loc_pool >= 0 && loc_lgpool >= 0 &&
                           varp_lg >= 0 && varp_qp >= 0 && varp_hero >= 0 &&
                           varp_crest >= 0 && varp_zq >= 0 && varp_upass >= 0 &&
                           varp_water >= 0 && obj_notes >= 0 && obj_map >= 0 &&
                           obj_papyrus >= 0 && obj_charcoal >= 0 && obj_bull >= 0 &&
                           obj_book >= 0 && obj_soul >= 0 && obj_mind >= 0 &&
                           obj_earth >= 0 && obj_law >= 0 && obj_opal >= 0 &&
                           obj_jade >= 0 && obj_topaz >= 0 && obj_sapp >= 0 &&
                           obj_em >= 0 && obj_ruby >= 0 && obj_dia >= 0 &&
                           obj_gold >= 0 && obj_bowl_bless >= 0 && obj_bowl_pure >= 0 &&
                           obj_vial >= 0 && obj_vial_enc >= 0 && obj_totem >= 0 &&
                           stat_attack >= 0 && stat_smith >= 0 && stat_magic >= 0 &&
                           stat_prayer >= 0,
                       "the ::legendsrun C-side names should all resolve");

        if( npc_guard >= 0 && npc_radimus >= 0 && npc_forester >= 0 && npc_gujuo >= 0 &&
            npc_ungadulu >= 0 && loc_barrier >= 0 && loc_fire >= 0 && loc_pool >= 0 &&
            varp_lg >= 0 && varp_qp >= 0 && varp_hero >= 0 && obj_notes >= 0 &&
            obj_map >= 0 && obj_papyrus >= 0 && obj_charcoal >= 0 && obj_bull >= 0 &&
            obj_book >= 0 && obj_gold >= 0 && obj_bowl_bless >= 0 && obj_bowl_pure >= 0 &&
            obj_totem >= 0 && stat_attack >= 0 )
        {
            int guard_slot;
            int radimus_slot;
            int forester_slot;
            int gujuo_slot;
            int ungadulu_slot;
            int loc_slot;
            int attack_before;
            int attack_after_four;

            player->godmode = 1;
            player->dying = 0;
            lg_clear_inv(player);
            player->varps[varp_lg] = 0;
            player->varps[varp_qp] = 0;
            player->varps[varp_hero] = 0;
            player->varps[varp_crest] = 0;
            player->varps[varp_zq] = 0;
            player->varps[varp_upass] = 0;
            player->varps[varp_water] = 0;

            /* ---- OPNPC1 guard: ineligible player is refused ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2726, 3349);
            selftest_tick(srv);
            guard_slot = ToriRSServer_WorldNpcSpawn(srv, npc_guard, 2726, 3349, 0);
            SELFTEST_CHECK(guard_slot >= 0, "legends_guild_guard1 should spawn");
            if( guard_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1,
                                               guard_slot);
                lg_click(srv, 16);
                SELFTEST_CHECK(player->varps[varp_lg] == 0,
                               "an ineligible guard talk must not start the quest, got %d",
                               player->varps[varp_lg]);
                SELFTEST_CHECK(player->z < 3360,
                               "an ineligible player must stay outside the guild, z=%d",
                               player->z);
                if( player->varps[varp_lg] == 0 && player->z < 3360 )
                    lg_pass("opnpc1_guard_ineligible");
            }

            /* ---- OPNPC1 guard: eligible admission walks through the gate ---- */
            player->varps[varp_hero] = 15;
            player->varps[varp_crest] = 11;
            player->varps[varp_zq] = 15;
            player->varps[varp_upass] = 10;
            player->varps[varp_water] = 10;
            player->varps[varp_qp] = 107;
            ToriRSServer_WorldTeleport(srv, 0, 2726, 3349);
            selftest_tick(srv);
            if( guard_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1,
                                               guard_slot);
                lg_click(srv, 20);
                SELFTEST_CHECK(player->z >= 3360,
                               "an eligible guard admit should walk inside the gates, z=%d",
                               player->z);
                SELFTEST_CHECK(player->varps[varp_lg] == 0,
                               "guard admission must not itself start the quest, got %d",
                               player->varps[varp_lg]);
                if( player->z >= 3360 && player->varps[varp_lg] == 0 )
                    lg_pass("opnpc1_guard_admit");
                ToriRSServer_WorldNpcFree(srv, guard_slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* ---- OPNPC1 Radimus start: notes + state 1 ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2724, 3368);
            selftest_tick(srv);
            radimus_slot = ToriRSServer_WorldNpcSpawn(srv, npc_radimus, 2724, 3368, 0);
            SELFTEST_CHECK(radimus_slot >= 0, "radimus_erkle should spawn");
            if( radimus_slot >= 0 )
            {
                lg_clear_inv(player);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1,
                                               radimus_slot);
                lg_click(srv, 24);
                SELFTEST_CHECK(player->varps[varp_lg] == 1,
                               "Radimus accept should write legends_started, got %d",
                               player->varps[varp_lg]);
                SELFTEST_CHECK(player->inv[0].obj_id == obj_notes,
                               "Radimus should hand thkaramjamap, got obj_id=%d",
                               player->inv[0].obj_id);
                if( player->varps[varp_lg] == 1 && player->inv[0].obj_id == obj_notes )
                    lg_pass("opnpc1_radimus_start");
            }

            /* ---- OPHELDU map: papyrus+charcoal on notes in Kharazi ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2780, 2900);
            selftest_tick(srv);
            lg_clear_inv(player);
            inv_set(player, 0, obj_notes, 1);
            inv_set(player, 1, obj_papyrus, 1);
            inv_set(player, 2, obj_charcoal, 1);
            player->last_item = obj_notes;
            player->last_slot = 0;
            player->last_useitem = obj_papyrus;
            player->last_useslot = 1;
            ToriRSServer_ScriptsRunOpheldu(srv, obj_notes, -1, obj_papyrus, -1);
            lg_click(srv, 8);
            SELFTEST_CHECK(player->varps[varp_lg] == 2,
                           "mapping the jungle should write legends_mapped_jungle, got %d",
                           player->varps[varp_lg]);
            SELFTEST_CHECK(player->inv[0].obj_id == obj_map,
                           "mapping should produce thkaramjamapcomp, got obj_id=%d",
                           player->inv[0].obj_id);
            if( player->varps[varp_lg] == 2 && player->inv[0].obj_id == obj_map )
                lg_pass("opheldu_map_kharazi");

            /* ---- OPNPCU forester: completed map grants bullroarer ---- */
            ToriRSServer_WorldTeleport(srv, 0, 2824, 2940);
            selftest_tick(srv);
            forester_slot = ToriRSServer_WorldNpcSpawn(srv, npc_forester, 2824, 2940, 0);
            SELFTEST_CHECK(forester_slot >= 0, "jungleforester_m should spawn");
            if( forester_slot >= 0 )
            {
                player->last_useitem = obj_map;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_forester, -1,
                                               forester_slot);
                lg_click(srv, 16);
                SELFTEST_CHECK(player->varps[varp_lg] == 3,
                               "forester copy should write legends_got_bullroarer, got %d",
                               player->varps[varp_lg]);
                SELFTEST_CHECK(player->inv[0].obj_id == obj_bull ||
                                   player->inv[1].obj_id == obj_bull ||
                                   player->inv[2].obj_id == obj_bull,
                               "forester should grant bullroarer");
                if( player->varps[varp_lg] == 3 &&
                    ( player->inv[0].obj_id == obj_bull || player->inv[1].obj_id == obj_bull ||
                      player->inv[2].obj_id == obj_bull ) )
                    lg_pass("opnpcu_forester_bullroarer");
                ToriRSServer_WorldNpcFree(srv, forester_slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* ---- OPLOC1 gem/rune shrine shortcut grants the book ---- */
            loc_slot = lg_place_loc(srv, 2780, 2900, loc_barrier);
            lg_clear_inv(player);
            inv_set(player, 0, obj_soul, 1);
            inv_set(player, 1, obj_mind, 1);
            inv_set(player, 2, obj_earth, 1);
            inv_set(player, 3, obj_law, 2);
            inv_set(player, 4, obj_opal, 1);
            inv_set(player, 5, obj_jade, 1);
            inv_set(player, 6, obj_topaz, 1);
            inv_set(player, 7, obj_sapp, 1);
            inv_set(player, 8, obj_em, 1);
            inv_set(player, 9, obj_ruby, 1);
            inv_set(player, 10, obj_dia, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_barrier, -1,
                                                loc_slot);
            lg_click(srv, 8);
            SELFTEST_CHECK(player->inv[0].obj_id == obj_book ||
                               player->inv[1].obj_id == obj_book,
                           "the shrine shortcut should grant book_of_binding");
            if( player->inv[0].obj_id == obj_book || player->inv[1].obj_id == obj_book )
                lg_pass("oploc1_gem_shrine_book");
            ToriRSServer_WorldLocSet(srv, 2781, 2900, 0, 10, -1, 0,
                                     TORIRSSERVER_LOC_SET_ADD);

            /* ---- OPNPCU Gujuo: gold bar -> blessed bowl (disclosed soft-skip) ---- */
            player->varps[varp_lg] = 7; /* legends_spoke_ungadulu */
            ToriRSServer_CombatSetLevel(player, stat_smith, 50);
            ToriRSServer_WorldTeleport(srv, 0, 2780, 2900);
            selftest_tick(srv);
            gujuo_slot = ToriRSServer_WorldNpcSpawn(srv, npc_gujuo, 2781, 2900, 0);
            SELFTEST_CHECK(gujuo_slot >= 0, "gujuo should spawn");
            if( gujuo_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gujuo, -1,
                                               gujuo_slot);
                lg_click(srv, 12);
                SELFTEST_CHECK(player->varps[varp_lg] == 8,
                               "asking Gujuo about water should write asked_gujuo_holy_water, "
                               "got %d",
                               player->varps[varp_lg]);
                if( player->varps[varp_lg] == 8 )
                    lg_pass("opnpc1_gujuo_ask_water");

                lg_clear_inv(player);
                inv_set(player, 0, obj_gold, 1);
                player->last_useitem = obj_gold;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_gujuo, -1,
                                               gujuo_slot);
                lg_click(srv, 8);
                SELFTEST_CHECK(player->inv[0].obj_id == obj_bowl_bless,
                               "Gujuo gold-bar soft-skip should grant goldbowlbless_empty, "
                               "got obj_id=%d",
                               player->inv[0].obj_id);
                if( player->inv[0].obj_id == obj_bowl_bless )
                    lg_pass("opnpcu_gujuo_gold_bowl");
            }

            /* ---- OPLOCU sacred pool fills the blessed bowl ---- */
            player->varps[varp_lg] = 5; /* below filled_bowl so the writer can fire */
            loc_slot = lg_place_loc(srv, 2780, 2910, loc_pool);
            lg_clear_inv(player);
            inv_set(player, 0, obj_bowl_bless, 1);
            player->last_useitem = obj_bowl_bless;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_pool, -1,
                                                loc_slot);
            lg_click(srv, 6);
            SELFTEST_CHECK(player->inv[0].obj_id == obj_bowl_pure,
                           "bowl on sacred_water should fill goldbowlbless_pure, "
                           "got obj_id=%d",
                           player->inv[0].obj_id);
            SELFTEST_CHECK(player->varps[varp_lg] == 10,
                           "filling the surface pool should write legends_filled_bowl, "
                           "got %d",
                           player->varps[varp_lg]);
            if( player->inv[0].obj_id == obj_bowl_pure && player->varps[varp_lg] == 10 )
                lg_pass("oplocu_sacred_water");
            ToriRSServer_WorldLocSet(srv, 2781, 2910, 0, 10, -1, 0,
                                     TORIRSSERVER_LOC_SET_ADD);

            /* ---- OPLOCU fire wall douse (state must be below found_entrance) ---- */
            player->varps[varp_lg] = 5;
            loc_slot = lg_place_loc(srv, 2780, 2920, loc_fire);
            lg_clear_inv(player);
            inv_set(player, 0, obj_bowl_pure, 1);
            player->last_useitem = obj_bowl_pure;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_fire, -1,
                                                loc_slot);
            lg_click(srv, 6);
            SELFTEST_CHECK(player->varps[varp_lg] == 6,
                           "dousing the fire wall should write legends_found_entrance, "
                           "got %d",
                           player->varps[varp_lg]);
            if( player->varps[varp_lg] == 6 )
                lg_pass("oplocu_firewall");
            ToriRSServer_WorldLocSet(srv, 2781, 2920, 0, 10, -1, 0,
                                     TORIRSSERVER_LOC_SET_ADD);

            /* ---- OPHELDU book enchants an empty vial ---- */
            ToriRSServer_CombatSetLevel(player, stat_magic, 56);
            ToriRSServer_CombatSetLevel(player, stat_prayer, 42);
            lg_clear_inv(player);
            inv_set(player, 0, obj_book, 1);
            inv_set(player, 1, obj_vial, 1);
            player->last_item = obj_book;
            player->last_slot = 0;
            player->last_useitem = obj_vial;
            player->last_useslot = 1;
            ToriRSServer_ScriptsRunOpheldu(srv, obj_book, -1, obj_vial, -1);
            lg_click(srv, 8);
            SELFTEST_CHECK(player->inv[1].obj_id == obj_vial_enc ||
                               player->inv[0].obj_id == obj_vial_enc,
                           "vial on book_of_binding should produce vial_enchanted");
            if( player->inv[1].obj_id == obj_vial_enc || player->inv[0].obj_id == obj_vial_enc )
                lg_pass("opheldu_book_enchant_vial");

            /* ---- OPNPCU Ungadulu: book summons first Nezikchened ---- */
            player->varps[varp_lg] = 6;
            ToriRSServer_WorldTeleport(srv, 0, 2780, 2930);
            selftest_tick(srv);
            ungadulu_slot = ToriRSServer_WorldNpcSpawn(srv, npc_ungadulu, 2781, 2930, 0);
            SELFTEST_CHECK(ungadulu_slot >= 0, "ungadulu_bad should spawn");
            if( ungadulu_slot >= 0 )
            {
                lg_clear_inv(player);
                inv_set(player, 0, obj_book, 1);
                player->last_useitem = obj_book;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_ungadulu, -1,
                                               ungadulu_slot);
                lg_click(srv, 16);
                SELFTEST_CHECK(player->varps[varp_lg] == 11,
                               "book on Ungadulu should write summoned_nezikchened_fire, "
                               "got %d",
                               player->varps[varp_lg]);
                if( player->varps[varp_lg] == 11 )
                    lg_pass("opnpcu_ungadulu_bind");
                ToriRSServer_WorldNpcFree(srv, ungadulu_slot);
                ToriRSServer_WorldNpcReap(srv);
            }
            if( gujuo_slot >= 0 )
            {
                ToriRSServer_WorldNpcFree(srv, gujuo_slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* ---- OPLOCU lgwaterpool: sacred water after the second fight ---- */
            player->varps[varp_lg] = 22; /* defeated_nezikchened_water */
            loc_slot = lg_place_loc(srv, 2780, 2940, loc_lgpool);
            lg_clear_inv(player);
            inv_set(player, 0, obj_bowl_bless, 1);
            player->last_useitem = obj_bowl_bless;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_lgpool, -1,
                                                loc_slot);
            lg_click(srv, 6);
            SELFTEST_CHECK(player->varps[varp_lg] == 25,
                           "bowl on lgwaterpool should write sacred_water_collected, "
                           "got %d",
                           player->varps[varp_lg]);
            if( player->varps[varp_lg] == 25 )
                lg_pass("oplocu_lgwaterpool");
            ToriRSServer_WorldLocSet(srv, 2781, 2940, 0, 10, -1, 0,
                                     TORIRSSERVER_LOC_SET_ADD);

            /* ---- OPNPC1 Radimus hand-in + four trainings + no fifth XP ---- */
            if( radimus_slot >= 0 )
            {
                ToriRSServer_WorldTeleport(srv, 0, 2724, 3368);
                selftest_tick(srv);
                player->varps[varp_lg] = 45; /* got_gilded_totem */
                lg_clear_inv(player);
                inv_set(player, 0, obj_map, 1);
                inv_set(player, 1, obj_totem, 1);
                ToriRSServer_CombatSetLevel(player, stat_attack, 50);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1,
                                               radimus_slot);
                lg_click(srv, 16);
                SELFTEST_CHECK(player->varps[varp_lg] == 50,
                               "hand-in should write returned_to_radimus, got %d",
                               player->varps[varp_lg]);
                if( player->varps[varp_lg] == 50 )
                    lg_pass("opnpc1_radimus_handin");

                attack_before = player->stat_xp_tenths[stat_attack];
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1,
                                               radimus_slot);
                lg_click(srv, 16);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1,
                                               radimus_slot);
                lg_click(srv, 16);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1,
                                               radimus_slot);
                lg_click(srv, 16);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1,
                                               radimus_slot);
                lg_click(srv, 16);
                SELFTEST_CHECK(player->varps[varp_lg] == 70,
                               "four training talks should reach radimus_training_4, got %d",
                               player->varps[varp_lg]);
                attack_after_four = player->stat_xp_tenths[stat_attack];
                SELFTEST_CHECK(attack_after_four > attack_before,
                               "four training sessions should award Attack xp, %d -> %d",
                               attack_before, attack_after_four);
                if( player->varps[varp_lg] == 70 && attack_after_four > attack_before )
                    lg_pass("opnpc1_radimus_four_trainings");

                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1,
                                               radimus_slot);
                lg_click(srv, 24);
                if( com_messagebox > 0 && player->active_script )
                    ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
                lg_drain(srv, 40);
                SELFTEST_CHECK(player->varps[varp_lg] == 75,
                               "the fifth Radimus talk should complete with no extra XP "
                               "session, got %d",
                               player->varps[varp_lg]);
                SELFTEST_CHECK(player->stat_xp_tenths[stat_attack] == attack_after_four,
                               "state 70 must not grant a fifth 30000 XP, %d -> %d",
                               attack_after_four, player->stat_xp_tenths[stat_attack]);
                if( player->varps[varp_lg] == 75 &&
                    player->stat_xp_tenths[stat_attack] == attack_after_four )
                    lg_pass("opnpc1_radimus_complete_no_fifth_xp");

                ToriRSServer_WorldNpcFree(srv, radimus_slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            lg_clear_inv(player);
            player->varps[varp_lg] = 0;
            player->godmode = 1;
            player->dying = 0;
        }
    }
    ToriRSServer_ScriptsFree(srv);
}
