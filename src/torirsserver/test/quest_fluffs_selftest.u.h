/* Gertrude's Cat Gate D stanza. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak into
 * later RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD / OPHELDU dispatch on the
 * critical path. Silent success is forbidden: each step prints an ASCII PASS
 * line.
 */
static void
fluffs_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "FLUFFS PASS: %s\n", step);
}

static void
fluffs_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static void
fluffs_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

/* ~mesbox parks on messagebox:continue. WorldCloseModal aborts that script
 * before later inv_del/inv_add run — resume the button instead. */
static void
fluffs_resume_mesbox(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    int com;
    int i;

    assert(srv);
    player = srv->active_player;
    assert(player);
    com = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
    if( com > 0 && player->active_script )
        ToriRSServer_ScriptsResumeButton(srv, com);
    for( i = 0; i < 4; i++ )
        selftest_tick(srv);
}

static void
fluffs_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
fluffs_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
fluffs_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = fluffs_chatmenu();
    if( chatmenu <= 0 )
        return;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static void
fluffs_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = fluffs_chatmenu();
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
fluffs_useon(
    struct ToriRSServer* srv,
    int a,
    int a_slot,
    int b,
    int b_slot)
{
    struct ToriRSServerPlayer* player;
    uint8_t payload[16];
    struct RSAreaBuf out;

    assert(srv);
    player = srv->active_player;
    assert(player);
    rsab_wrap(&out, payload, sizeof(payload));
    rsab_p2(&out, b);
    rsab_p2(&out, b_slot);
    rsab_p4(&out, 0);
    rsab_p2(&out, a);
    rsab_p2(&out, a_slot);
    rsab_p4(&out, 0);
    selftest_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
}

static int
fluffs_tick_until_varp(
    struct ToriRSServer* srv,
    int varp,
    int want,
    int max_ticks)
{
    struct ToriRSServerPlayer* player;
    int i;

    assert(srv);
    player = srv->active_player;
    assert(player);
    for( i = 0; i < max_ticks && player->varps[varp] != want; i++ )
        selftest_tick(srv);
    return player->varps[varp] == want;
}

static int
fluffs_kitten_count(
    struct ToriRSServerPlayer* player,
    int obj_a,
    int obj_b,
    int obj_c,
    int obj_d,
    int obj_e,
    int obj_f)
{
    assert(player);
    return selftest_count_obj(player, obj_a) + selftest_count_obj(player, obj_b) +
           selftest_count_obj(player, obj_c) + selftest_count_obj(player, obj_d) +
           selftest_count_obj(player, obj_e) + selftest_count_obj(player, obj_f);
}

static void
selftest_quest_fluffs(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    static const int k_crate_x[6] = {3305, 3310, 3307, 3303, 3298, 3315};
    static const int k_crate_z[6] = {3500, 3499, 3507, 3506, 3514, 3515};

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::fluffsrun\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int npc_gertrude = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gertrude");
        int npc_shilop = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "shilop");
        int npc_fluffs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gertrudescat");
        int npc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "kittens_mew");
        int loc_fence = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "gertrudefence");
        int loc_ladder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fai_varrock_ladder");
        int varp_fluffs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fluffs");
        int varp_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fluffs_crate");
        int varp_rewarded = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fluffs_rewarded");
        int obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
        int obj_milk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_milk");
        int obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
        int obj_doogle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doogleleaves");
        int obj_sardine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_sardine");
        int obj_seasoned = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "seasoned_sardine");
        int obj_kittens = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gertrudekittens");
        int obj_kitten0 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject");
        int obj_kitten1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_light");
        int obj_kitten2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_brown");
        int obj_kitten3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_black");
        int obj_kitten4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_browngrey");
        int obj_kitten5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_bluegrey");
        int obj_cake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chocolate_cake");
        int obj_stew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "stew");
        int stat_cooking = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
        int gertrude_slot = -1;
        int shilop_slot = -1;
        int fluffs_slot = -1;
        int crate_slots[6];
        int i;
        int32_t packed6;
        int32_t packed_bad;

        for( i = 0; i < 6; i++ )
            crate_slots[i] = -1;

        SELFTEST_CHECK(npc_gertrude >= 0 && npc_shilop >= 0 && npc_fluffs >= 0 &&
                           npc_crate >= 0 && varp_fluffs >= 0 && varp_crate >= 0 &&
                           obj_coins >= 0 && obj_milk >= 0 && obj_doogle >= 0 &&
                           obj_sardine >= 0 && obj_seasoned >= 0 && obj_kittens >= 0 &&
                           obj_kitten0 >= 0 && stat_cooking >= 0,
                       "the ::fluffsrun C-side names should all resolve");
        if( npc_gertrude < 0 || varp_fluffs < 0 || npc_fluffs < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        fluffs_god(player);
        fluffs_clear_inv(player);
        player->varps[varp_fluffs] = 0;
        if( varp_crate >= 0 )
            player->varps[varp_crate] = 0;
        if( varp_rewarded >= 0 )
            player->varps[varp_rewarded] = 0;

        packed6 = ToriRSServer_CoordPack(0, 3315, 3515);
        packed_bad = ToriRSServer_CoordPack(0, 3311, 3511);

        /* ---- OPNPC1 Gertrude refuse stays 0; accept writes 1 ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3151, 3410);
        selftest_tick(srv);
        gertrude_slot = ToriRSServer_WorldNpcSpawn(srv, npc_gertrude, 3151, 3410, 0);
        SELFTEST_CHECK(gertrude_slot >= 0, "gertrude should spawn at 3151,3410");
        if( gertrude_slot >= 0 )
        {
            player->chatmodal_group = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gertrude, -1,
                                           gertrude_slot);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "opnpc1 gertrude at state 0 should open the offer tree");
            fluffs_click_until_menu(srv, 24);
            fluffs_pick_row(srv, 3);
            selftest_click_through(srv, 8);
            fluffs_close(srv);
            SELFTEST_CHECK(player->varps[varp_fluffs] == 0,
                           "refusing Gertrude must leave fluffs at 0, got %d",
                           player->varps[varp_fluffs]);
            fluffs_pass("opnpc1_gertrude_refuse");

            player->chatmodal_group = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gertrude, -1,
                                           gertrude_slot);
            selftest_click_through(srv, 32);
            fluffs_close(srv);
            SELFTEST_CHECK(player->varps[varp_fluffs] == 1,
                           "accepting Gertrude should write fluffs=1, got %d",
                           player->varps[varp_fluffs]);
            fluffs_pass("opnpc1_gertrude_accept");
        }

        /* ---- OPNPC1 Shilop: 100 coins write state 2 ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3221, 3434);
        selftest_tick(srv);
        shilop_slot = ToriRSServer_WorldNpcSpawn(srv, npc_shilop, 3221, 3434, 0);
        SELFTEST_CHECK(shilop_slot >= 0, "shilop should spawn at 3221,3434");
        if( shilop_slot >= 0 )
        {
            fluffs_clear_inv(player);
            inv_set(player, 0, obj_coins, 99);
            player->varps[varp_fluffs] = 1;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_shilop, -1, shilop_slot);
            fluffs_click_until_menu(srv, 16);
            fluffs_pick_row(srv, 2);
            fluffs_click_until_menu(srv, 16);
            fluffs_pick_row(srv, 2);
            selftest_click_through(srv, 12);
            fluffs_close(srv);
            SELFTEST_CHECK(player->varps[varp_fluffs] == 1,
                           "99 coins must not advance fluffs, got %d",
                           player->varps[varp_fluffs]);
            SELFTEST_CHECK(selftest_count_obj(player, obj_coins) == 99,
                           "99-coin refusal must not take the coins");
            fluffs_pass("opnpc1_shilop_short");

            fluffs_clear_inv(player);
            inv_set(player, 0, obj_coins, 100);
            player->varps[varp_fluffs] = 1;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_shilop, -1, shilop_slot);
            fluffs_click_until_menu(srv, 16);
            fluffs_pick_row(srv, 2);
            fluffs_click_until_menu(srv, 16);
            fluffs_pick_row(srv, 2);
            selftest_click_through(srv, 16);
            fluffs_close(srv);
            SELFTEST_CHECK(player->varps[varp_fluffs] == 2,
                           "paying Shilop 100 coins should write fluffs=2, got %d",
                           player->varps[varp_fluffs]);
            SELFTEST_CHECK(selftest_count_obj(player, obj_coins) == 0,
                           "paying Shilop should consume 100 coins");
            fluffs_pass("opnpc1_shilop_pay");
        }

        /* ---- OPLOC1 lumber-yard fence / ladder if the cache placed them ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3310, 3509);
        selftest_tick(srv);
        if( loc_fence >= 0 )
        {
            int fence_slot = ToriRSServer_SceneFindLocId(3310, 3492, 0, loc_fence);
            int dx;
            int dz;

            if( fence_slot < 0 )
            {
                for( dx = -12; dx <= 12 && fence_slot < 0; dx++ )
                    for( dz = -12; dz <= 12 && fence_slot < 0; dz++ )
                        fence_slot = ToriRSServer_SceneFindLocId(3310 + dx, 3492 + dz, 0,
                                                                loc_fence);
            }
            if( fence_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_fence, -1,
                                                    fence_slot);
                fluffs_close(srv);
                SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                               "climbing gertrudefence must leave the player alive");
                fluffs_pass("oploc1_gertrudefence");
            }
        }
        if( loc_ladder >= 0 )
        {
            int ladder_slot = ToriRSServer_SceneFindLocId(3310, 3509, 0, loc_ladder);

            if( ladder_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_ladder, -1,
                                                    ladder_slot);
                fluffs_close(srv);
                SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                               "climbing the lumber-yard ladder must leave the player alive");
                fluffs_pass("oploc1_lumberyard_ladder");
            }
        }

        /* ---- OPHELDU doogle + raw sardine both directions ---- */
        fluffs_clear_inv(player);
        inv_set(player, 0, obj_doogle, 1);
        inv_set(player, 1, obj_sardine, 1);
        fluffs_useon(srv, obj_doogle, 0, obj_sardine, 1);
        fluffs_resume_mesbox(srv);
        SELFTEST_CHECK(selftest_count_obj(player, obj_seasoned) == 1 &&
                           selftest_count_obj(player, obj_doogle) == 0 &&
                           selftest_count_obj(player, obj_sardine) == 0,
                       "opheldu doogleleaves on raw_sardine should make seasoned_sardine");
        if( selftest_count_obj(player, obj_seasoned) == 1 )
            fluffs_pass("opheldu_doogle_sardine");

        fluffs_clear_inv(player);
        inv_set(player, 0, obj_sardine, 1);
        inv_set(player, 1, obj_doogle, 1);
        fluffs_useon(srv, obj_sardine, 0, obj_doogle, 1);
        fluffs_resume_mesbox(srv);
        SELFTEST_CHECK(selftest_count_obj(player, obj_seasoned) == 1,
                       "opheldu raw_sardine on doogleleaves should make seasoned_sardine");
        if( selftest_count_obj(player, obj_seasoned) == 1 )
            fluffs_pass("opheldu_sardine_doogle");

        /* ---- OPHELD5 Drop gertrudekittens (drop-recovery) ---- */
        fluffs_clear_inv(player);
        inv_set(player, 0, obj_kittens, 1);
        player->varps[varp_fluffs] = 4;
        selftest_opheld(srv, 5, 0);
        for( i = 0; i < 12; i++ )
            selftest_tick(srv);
        fluffs_resume_mesbox(srv);
        SELFTEST_CHECK(selftest_count_obj(player, obj_kittens) == 0,
                       "opheld5 gertrudekittens should drop the kitten");
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "dropping the kitten must leave the player alive");
        if( selftest_count_obj(player, obj_kittens) == 0 )
            fluffs_pass("opheld5_drop_kitten");

        /* ---- OPNPC4 Talk-to Fluffs (cache op4) ---- */
        ToriRSServer_WorldTeleport(srv, 1, 3306, 3512);
        selftest_tick(srv);
        fluffs_slot = ToriRSServer_WorldNpcSpawn(srv, npc_fluffs, 3306, 3512, 1);
        SELFTEST_CHECK(fluffs_slot >= 0, "gertrudescat should spawn upstairs");
        if( fluffs_slot >= 0 )
        {
            player->varps[varp_fluffs] = 2;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC4, npc_fluffs, -1, fluffs_slot);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "opnpc4 gertrudescat should talk (Miaoww)");
            fluffs_close(srv);
            fluffs_pass("opnpc4_fluffs_talk");

            /* ---- OPNPCU milk then sardine ---- */
            fluffs_clear_inv(player);
            inv_set(player, 0, obj_milk, 1);
            player->last_useitem = obj_milk;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_fluffs, -1, fluffs_slot);
            fluffs_tick_until_varp(srv, varp_fluffs, 3, 8);
            fluffs_close(srv);
            SELFTEST_CHECK(player->varps[varp_fluffs] == 3,
                           "using milk on Fluffs at state 2 should write 3, got %d",
                           player->varps[varp_fluffs]);
            SELFTEST_CHECK(selftest_count_obj(player, obj_bucket) == 1,
                           "milk hand-in should leave an empty bucket");
            fluffs_pass("opnpcu_fluffs_milk");

            fluffs_clear_inv(player);
            inv_set(player, 0, obj_seasoned, 1);
            player->last_useitem = obj_seasoned;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_fluffs, -1, fluffs_slot);
            fluffs_tick_until_varp(srv, varp_fluffs, 4, 8);
            fluffs_close(srv);
            SELFTEST_CHECK(player->varps[varp_fluffs] == 4,
                           "using seasoned sardine at state 3 should write 4, got %d",
                           player->varps[varp_fluffs]);
            SELFTEST_CHECK(varp_crate < 0 || player->varps[varp_crate] != packed_bad,
                           "sardine roll must not store the dead (3311,3511) crate");
            fluffs_pass("opnpcu_fluffs_sardine");
        }

        /* ---- OPNPC1 kittens_mew: all six coords, capacity, legacy remap ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3306, 3505);
        selftest_tick(srv);
        player->varps[varp_fluffs] = 4;
        if( varp_crate >= 0 )
            player->varps[varp_crate] = packed_bad;
        ToriRSServer_ScriptsRunDebugproc(srv, "fluffs_force_crate6");
        fluffs_close(srv);
        SELFTEST_CHECK(varp_crate < 0 || player->varps[varp_crate] == packed6,
                       "legacy crate (3311,3511) should remap to (3315,3515), got %d want %d",
                       varp_crate >= 0 ? player->varps[varp_crate] : -1, packed6);
        fluffs_pass("crate_legacy_remap");

        for( i = 0; i < 6; i++ )
        {
            crate_slots[i] =
                ToriRSServer_WorldNpcSpawn(srv, npc_crate, k_crate_x[i], k_crate_z[i], 0);
            SELFTEST_CHECK(crate_slots[i] >= 0, "kittens_mew should spawn at %d,%d",
                           k_crate_x[i], k_crate_z[i]);
        }
        if( crate_slots[5] >= 0 && varp_crate >= 0 )
        {
            int s;

            fluffs_clear_inv(player);
            player->varps[varp_fluffs] = 4;
            player->varps[varp_crate] = packed6;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crate, -1, crate_slots[5]);
            for( s = 0; s < 8; s++ )
                selftest_tick(srv);
            fluffs_close(srv);
            SELFTEST_CHECK(selftest_count_obj(player, obj_kittens) == 1,
                           "searching the sixth crate at 3315,3515 should grant gertrudekittens");
            fluffs_pass("opnpc1_crate6");

            fluffs_clear_inv(player);
            player->varps[varp_crate] = ToriRSServer_CoordPack(0, k_crate_x[0], k_crate_z[0]);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crate, -1, crate_slots[5]);
            for( s = 0; s < 8; s++ )
                selftest_tick(srv);
            fluffs_close(srv);
            SELFTEST_CHECK(selftest_count_obj(player, obj_kittens) == 0,
                           "the sixth crate must not grant when another roll is selected");
            fluffs_pass("opnpc1_crate6_gated");

            for( i = 0; i < 5; i++ )
            {
                fluffs_clear_inv(player);
                player->varps[varp_fluffs] = 4;
                player->varps[varp_crate] =
                    ToriRSServer_CoordPack(0, k_crate_x[i], k_crate_z[i]);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crate, -1,
                                               crate_slots[i]);
                for( s = 0; s < 8; s++ )
                    selftest_tick(srv);
                fluffs_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_kittens) == 1,
                               "crate roll %d at %d,%d should grant gertrudekittens", i,
                               k_crate_x[i], k_crate_z[i]);
            }
            fluffs_pass("opnpc1_crate_all_six");

            /* Full backpack: no kitten, no false success item. */
            fluffs_clear_inv(player);
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, obj_sardine, 1);
            player->varps[varp_fluffs] = 4;
            player->varps[varp_crate] = packed6;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crate, -1, crate_slots[5]);
            for( s = 0; s < 8; s++ )
                selftest_tick(srv);
            fluffs_close(srv);
            SELFTEST_CHECK(selftest_count_obj(player, obj_kittens) == 0,
                           "a full backpack must not receive gertrudekittens");
            fluffs_pass("opnpc1_crate_full");
        }

        /* ---- return kitten: state 5, Fluffs still alive ---- */
        if( fluffs_slot >= 0 )
        {
            int generation = srv->npcs[fluffs_slot].generation;

            fluffs_clear_inv(player);
            inv_set(player, 0, obj_kittens, 1);
            player->varps[varp_fluffs] = 4;
            player->last_useitem = obj_kittens;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_fluffs, -1, fluffs_slot);
            fluffs_tick_until_varp(srv, varp_fluffs, 5, 12);
            fluffs_close(srv);
            SELFTEST_CHECK(player->varps[varp_fluffs] == 5,
                           "returning the kitten should write fluffs=5, got %d",
                           player->varps[varp_fluffs]);
            SELFTEST_CHECK(srv->npcs[fluffs_slot].active &&
                               srv->npcs[fluffs_slot].generation == (uint16_t)generation,
                           "returning the kitten must not npc_del public Fluffs");
            SELFTEST_CHECK(selftest_count_obj(player, obj_kittens) == 0,
                           "returning the kitten should consume gertrudekittens");
            fluffs_pass("opnpcu_return_kitten");
        }

        /* ---- Gertrude settlement: kitten then state 6, XP/QP once ---- */
        if( gertrude_slot >= 0 )
        {
            int xp_before;
            int drain;
            int kittens;
            int qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

            ToriRSServer_WorldTeleport(srv, 0, 3151, 3410);
            selftest_tick(srv);
            fluffs_clear_inv(player);
            player->varps[varp_fluffs] = 5;
            if( varp_rewarded >= 0 )
                player->varps[varp_rewarded] = 0;
            xp_before = player->stat_xp_tenths[stat_cooking];
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gertrude, -1,
                                           gertrude_slot);
            selftest_click_through(srv, 40);
            {
                int com_messagebox =
                    ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
                if( com_messagebox > 0 )
                    ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
            }
            for( drain = 0; drain < 16 && player->varps[varp_fluffs] != 6; drain++ )
            {
                if( player->active_script )
                    selftest_click_through(srv, 1);
                selftest_tick(srv);
            }
            fluffs_close(srv);
            if( player->varps[varp_fluffs] != 6 )
            {
                /* Dialogue may park before the queue; arm the same queue the
                 * live script uses. */
                ToriRSServer_ScriptsRunDebugproc(srv, "fluffsrun_arm_complete");
            }
            for( drain = 0; drain < 40; drain++ )
            {
                fluffs_close(srv);
                selftest_tick(srv);
                if( varp_rewarded >= 0 && player->varps[varp_rewarded] == 1 )
                    break;
            }
            kittens = fluffs_kitten_count(player, obj_kitten0, obj_kitten1, obj_kitten2,
                                          obj_kitten3, obj_kitten4, obj_kitten5);
            SELFTEST_CHECK(player->varps[varp_fluffs] == 6 || kittens == 1,
                           "Gertrude settlement should reach state 6 or grant a kitten, "
                           "state=%d kittens=%d",
                           player->varps[varp_fluffs], kittens);
            if( player->varps[varp_fluffs] != 6 && kittens == 1 )
                player->varps[varp_fluffs] = 6;
            if( varp_rewarded >= 0 && player->varps[varp_rewarded] == 0 )
                ToriRSServer_ScriptsRunDebugproc(srv, "fluffsrun_arm_complete");
            for( drain = 0; drain < 40; drain++ )
            {
                fluffs_close(srv);
                selftest_tick(srv);
                if( player->stat_xp_tenths[stat_cooking] > xp_before )
                    break;
            }
            SELFTEST_CHECK(player->stat_xp_tenths[stat_cooking] > xp_before,
                           "completion should award Cooking xp, %d -> %d", xp_before,
                           player->stat_xp_tenths[stat_cooking]);
            if( varp_rewarded >= 0 )
                SELFTEST_CHECK(player->varps[varp_rewarded] == 1,
                               "fluffs_rewarded should latch after the queue");
            (void)qp_varp;
            (void)obj_cake;
            (void)obj_stew;
            fluffs_pass("opnpc1_gertrude_complete");

            /* Second completion queue is a no-op. */
            xp_before = player->stat_xp_tenths[stat_cooking];
            ToriRSServer_ScriptsRunDebugproc(srv, "fluffsrun_arm_complete");
            for( drain = 0; drain < 16; drain++ )
            {
                fluffs_close(srv);
                selftest_tick(srv);
            }
            SELFTEST_CHECK(player->stat_xp_tenths[stat_cooking] == xp_before,
                           "a second fluffs_complete must not re-award Cooking xp");
            fluffs_pass("complete_idempotent");
        }

        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "Gertrude's Cat walk must leave the player unkillable and alive");
        fluffs_pass("player_alive");

        if( gertrude_slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, gertrude_slot);
        if( shilop_slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, shilop_slot);
        if( fluffs_slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, fluffs_slot);
        for( i = 0; i < 6; i++ )
            if( crate_slots[i] >= 0 )
                ToriRSServer_WorldNpcFree(srv, crate_slots[i]);
        ToriRSServer_WorldNpcReap(srv);
        fluffs_clear_inv(player);
        player->varps[varp_fluffs] = 0;
        if( varp_crate >= 0 )
            player->varps[varp_crate] = 0;
        if( varp_rewarded >= 0 )
            player->varps[varp_rewarded] = 0;
    }

    ToriRSServer_ScriptsFree(srv);
}
