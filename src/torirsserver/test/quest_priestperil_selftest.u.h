#ifndef TORIRSSERVER_TEST_QUEST_PRIESTPERIL_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_PRIESTPERIL_SELFTEST_U_H

/* Priest in Peril Gate D C walk. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPLOC / OPLOCU / OPNPCU dispatch on the
 * authored path. Silent success is forbidden: each step prints PRIESTPERIL PASS.
 * player->godmode = 1 for the whole walk (not a death test).
 */

static void
pip_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "PRIESTPERIL PASS: %s\n", step);
}

static void
pip_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static void
pip_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
pip_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
pip_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
pip_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = pip_chatmenu();
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
pip_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = pip_chatmenu();
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

static int
pip_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    return n;
}

static void
pip_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( pip_inv_total(player, obj_id) >= count )
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

static int
pip_find_loc(int x, int z, int level, int loc_id, int radius, int* out_x, int* out_z)
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
pip_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
selftest_quest_priestperil(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int varp;
    int varp_maus;
    int npc_roald;
    int npc_drezel;
    int npc_drezel2;
    int npc_dog;
    int loc_door;
    int loc_cell;
    int loc_well;
    int loc_grave1;
    int loc_grave2;
    int loc_coffin;
    int loc_barrier;
    int obj_gold;
    int obj_iron;
    int obj_bucket;
    int obj_murky;
    int obj_blessed;
    int obj_water;
    int obj_hammer;
    int obj_essence;
    int obj_wolfbane;
    int stat_prayer;
    int roald_slot;
    int drezel_slot;
    int drezel2_slot;
    int dog_slot;
    int i;

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    pip_god(player);

    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "priestperil");
    varp_maus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "priestperil_mausoleum");
    npc_roald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "king_roald");
    npc_drezel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "priestperiltrappedmonk");
    npc_drezel2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "priestperiltrappedmonk2");
    npc_dog = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "priestperilguarddog");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "priestperiltempledoorl");
    loc_cell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pip_prisondoor");
    loc_well = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "priestperil_well");
    loc_grave1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "priestperil_grave_base1");
    loc_grave2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "priestperil_grave_base2");
    loc_coffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "priestperil_coffin_noanim");
    loc_barrier = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pip_underground_wall_side_withportal");
    obj_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pipkey_gold");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pipkey_iron");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
    obj_murky = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_murkywater");
    obj_blessed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_blessedwater");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_water");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_essence = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blankrune");
    obj_wolfbane = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dagger_wolfbane");
    stat_prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");

    SELFTEST_CHECK(varp >= 0 && npc_roald >= 0 && npc_drezel >= 0 &&
                       npc_drezel2 >= 0 && npc_dog >= 0 && loc_door >= 0 &&
                       loc_cell >= 0 && loc_well >= 0 && loc_grave1 >= 0 &&
                       loc_grave2 >= 0 && loc_coffin >= 0 && loc_barrier >= 0 &&
                       obj_gold >= 0 && obj_iron >= 0 && obj_bucket >= 0 &&
                       obj_murky >= 0 && obj_blessed >= 0 && obj_essence >= 0 &&
                       obj_wolfbane >= 0 && stat_prayer >= 0,
                   "priestperil C-side names should all resolve");
    if( varp < 0 || npc_roald < 0 || npc_drezel < 0 || npc_drezel2 < 0 )
    {
        ToriRSServer_ScriptsFree(srv);
        return;
    }

    pip_clear_inv(player);
    player->varps[varp] = 0;
    if( varp_maus >= 0 )
        player->varps[varp_maus] = 0;

    /* ---- King Roald offer / decline / accept ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3222, 3472);
    selftest_tick(srv);
    roald_slot = ToriRSServer_WorldNpcSpawn(srv, npc_roald, 3222, 3472, 0);
    SELFTEST_CHECK(roald_slot >= 0, "king_roald should spawn");
    if( roald_slot >= 0 )
    {
        player->varps[varp] = 0;
        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_roald, -1, roald_slot);
        pip_click_until_menu(srv, 8);
        pip_pick_row(srv, 2);
        selftest_click_through(srv, 8);
        pip_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 0,
                       "declining Roald must leave priestperil at 0, got %d",
                       player->varps[varp]);
        pip_pass("opnpc1_roald_decline");

        player->varps[varp] = 0;
        player->chatmodal_group = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_roald, -1, roald_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "opnpc1 Roald at not_started should open the offer tree");
        pip_click_until_menu(srv, 8);
        pip_pick_row(srv, 1);
        selftest_click_through(srv, 8);
        pip_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 1,
                       "accepting Roald should write priestperil=1, got %d",
                       player->varps[varp]);
        pip_pass("opnpc1_roald_accept");

        player->varps[varp] = 1;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_roald, -1, roald_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "opnpc1 Roald at started should open the temple-nudge");
        pip_close(srv);
        pip_pass("opnpc1_roald_started_nudge");

        player->varps[varp] = 3;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_roald, -1, roald_slot);
        selftest_click_through(srv, 12);
        pip_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 4,
                       "telling Roald the dog is dead should write priestperil=4, got %d",
                       player->varps[varp]);
        pip_pass("opnpc1_roald_angry_dog");
    }

    /* ---- Temple doors: locked / knock / Roald-sent / jokes / kill-dog ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3408, 3488);
    selftest_tick(srv);
    {
        int door_x = 3408;
        int door_z = 3488;
        int door_slot = pip_find_loc(3408, 3488, 0, loc_door, 16, &door_x, &door_z);

        SELFTEST_CHECK(door_slot >= 0, "priestperiltempledoorl should resolve near 3408,3488");
        if( door_slot >= 0 )
        {
            player->varps[varp] = 1;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, door_slot);
            pip_close(srv);
            pip_pass("oploc1_temple_door_locked");

            player->varps[varp] = 1;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_door, -1, door_slot);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "knocking the temple door at started should open the knock tree");
            pip_click_until_menu(srv, 8);
            pip_pick_row(srv, 2);
            selftest_click_through(srv, 8);
            pip_close(srv);
            pip_pass("oploc2_temple_door_joke_moved_in");

            player->varps[varp] = 1;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_door, -1, door_slot);
            pip_click_until_menu(srv, 8);
            pip_pick_row(srv, 3);
            selftest_click_through(srv, 8);
            pip_close(srv);
            pip_pass("oploc2_temple_door_joke_historic");

            player->varps[varp] = 1;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_door, -1, door_slot);
            pip_click_until_menu(srv, 8);
            pip_pick_row(srv, 4);
            selftest_click_through(srv, 8);
            pip_close(srv);
            pip_pass("oploc2_temple_door_joke_pipes");

            player->varps[varp] = 1;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_door, -1, door_slot);
            pip_click_until_menu(srv, 8);
            pip_pick_row(srv, 1);
            pip_click_until_menu(srv, 16);
            pip_pick_row(srv, 2);
            selftest_click_through(srv, 8);
            pip_close(srv);
            SELFTEST_CHECK(player->varps[varp] == 1,
                           "refusing the door-help must leave priestperil at 1, got %d",
                           player->varps[varp]);
            pip_pass("oploc2_temple_door_nope");

            player->varps[varp] = 1;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_door, -1, door_slot);
            pip_click_until_menu(srv, 8);
            pip_pick_row(srv, 1);
            pip_click_until_menu(srv, 16);
            pip_pick_row(srv, 1);
            selftest_click_through(srv, 12);
            pip_close(srv);
            SELFTEST_CHECK(player->varps[varp] == 2,
                           "agreeing to kill the dog should write priestperil=2, got %d",
                           player->varps[varp]);
            pip_pass("oploc2_temple_door_agree_kill_dog");

            player->varps[varp] = 2;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_door, -1, door_slot);
            selftest_click_through(srv, 12);
            pip_close(srv);
            pip_pass("oploc2_temple_door_kill_dog_ask");

            player->varps[varp] = 3;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_door, -1, door_slot);
            selftest_click_through(srv, 12);
            pip_close(srv);
            pip_pass("oploc2_temple_door_after_dog");
        }
    }

    /* ---- Temple guardian refuse / fight-allowed ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3405, 9902);
    selftest_tick(srv);
    dog_slot = ToriRSServer_WorldNpcSpawn(srv, npc_dog, 3405, 9902, 0);
    SELFTEST_CHECK(dog_slot >= 0, "priestperilguarddog should spawn");
    if( dog_slot >= 0 )
    {
        player->varps[varp] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_dog, -1, dog_slot);
        pip_close(srv);
        pip_pass("opnpc2_guardian_refuse_before_start");

        player->varps[varp] = 2;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_dog, -1, dog_slot);
        pip_close(srv);
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "attacking the guardian must leave the player alive");
        pip_pass("opnpc2_guardian_fight_allowed");
    }

    /* ---- Trapped Drezel + prison door ---- */
    ToriRSServer_WorldTeleport(srv, 2, 3417, 3489);
    selftest_tick(srv);
    drezel_slot = ToriRSServer_WorldNpcSpawn(srv, npc_drezel, 3417, 3489, 2);
    SELFTEST_CHECK(drezel_slot >= 0, "priestperiltrappedmonk should spawn");
    {
        int cell_x = 3417;
        int cell_z = 3489;
        int cell_slot = pip_find_loc(3417, 3489, 2, loc_cell, 12, &cell_x, &cell_z);

        SELFTEST_CHECK(cell_slot >= 0, "pip_prisondoor should resolve near 3417,3489,2");
        if( cell_slot >= 0 )
        {
            player->varps[varp] = 4;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cell, -1, cell_slot);
            pip_close(srv);
            pip_pass("oploc1_prisondoor_locked");

            pip_clear_inv(player);
            pip_give(player, obj_gold, 1);
            player->last_useitem = obj_gold;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cell, -1, cell_slot);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "gold key on the cell should open the fail mesbox");
            pip_close(srv);
            SELFTEST_CHECK(pip_inv_total(player, obj_gold) == 1,
                           "gold key must not unlock the cell");
            pip_pass("oplocu_prisondoor_gold_fail");

            if( drezel_slot >= 0 )
            {
                player->varps[varp] = 4;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_cell, -1,
                                                    cell_slot);
                SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                               "talk-through at return_to_drezel should open Drezel's first talk");
                pip_click_until_menu(srv, 16);
                pip_pick_row(srv, 2);
                selftest_click_through(srv, 8);
                pip_close(srv);
                SELFTEST_CHECK(player->varps[varp] == 4,
                               "skipping the Salve tale must leave priestperil at 4, got %d",
                               player->varps[varp]);
                pip_pass("oploc2_drezel_skip_tale");

                player->varps[varp] = 4;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_cell, -1,
                                                    cell_slot);
                pip_click_until_menu(srv, 16);
                pip_pick_row(srv, 1);
                pip_click_until_menu(srv, 40);
                pip_pick_row(srv, 2);
                selftest_click_through(srv, 8);
                pip_close(srv);
                SELFTEST_CHECK(player->varps[varp] == 4,
                               "refusing aid must leave priestperil at 4, got %d",
                               player->varps[varp]);
                pip_pass("oploc2_drezel_aid_no");

                player->varps[varp] = 4;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_cell, -1,
                                                    cell_slot);
                pip_click_until_menu(srv, 16);
                pip_pick_row(srv, 1);
                pip_click_until_menu(srv, 40);
                pip_pick_row(srv, 1);
                selftest_click_through(srv, 16);
                pip_close(srv);
                SELFTEST_CHECK(player->varps[varp] == 5,
                               "accepting Drezel's aid should write priestperil=5, got %d",
                               player->varps[varp]);
                pip_pass("oploc2_drezel_aid_yes");
            }

            pip_clear_inv(player);
            pip_give(player, obj_iron, 1);
            player->varps[varp] = 5;
            player->last_useitem = obj_iron;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cell, -1, cell_slot);
            selftest_click_through(srv, 8);
            pip_close(srv);
            SELFTEST_CHECK(player->varps[varp] == 6,
                           "iron key should unlock Drezel, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(pip_inv_total(player, obj_iron) == 0,
                           "iron key should be consumed on unlock");
            pip_pass("oplocu_prisondoor_iron_unlock");

            player->varps[varp] = 6;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cell, -1, cell_slot);
            selftest_click_through(srv, 4);
            pip_close(srv);
            pip_pass("oploc1_prisondoor_walkthrough");
        }
    }

    if( drezel_slot >= 0 )
    {
        pip_clear_inv(player);
        pip_give(player, obj_murky, 1);
        player->varps[varp] = 6;
        player->last_useitem = obj_murky;
        player->last_useslot = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_drezel, -1, drezel_slot);
        selftest_click_through(srv, 8);
        pip_close(srv);
        SELFTEST_CHECK(pip_inv_total(player, obj_blessed) == 1,
                       "Drezel should bless murky water");
        pip_pass("opnpcu_drezel_bless_water");
    }

    /* ---- Well fill / look ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3423, 9891);
    selftest_tick(srv);
    {
        int well_x = 3423;
        int well_z = 9891;
        int well_slot = pip_find_loc(3423, 9891, 0, loc_well, 16, &well_x, &well_z);

        SELFTEST_CHECK(well_slot >= 0, "priestperil_well should resolve near 3423,9891");
        if( well_slot >= 0 )
        {
            player->varps[varp] = 6;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_well, -1, well_slot);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "looking in the well before complete should open the murky mesbox");
            pip_close(srv);
            pip_pass("oploc1_well_look_murky");

            pip_clear_inv(player);
            pip_give(player, obj_bucket, 1);
            player->last_useitem = obj_bucket;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_well, -1, well_slot);
            pip_close(srv);
            SELFTEST_CHECK(pip_inv_total(player, obj_murky) == 1,
                           "filling a bucket at the well before complete should grant murky water");
            pip_pass("oplocu_well_fill_bucket");
        }
    }

    /* ---- Monuments: study / iron swap / other swap / steal ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3423, 9891);
    selftest_tick(srv);
    if( varp_maus >= 0 )
        player->varps[varp_maus] = (6 << 22) | (1 << 21);
    player->varps[varp] = 5;
    {
        int g1x = 3423;
        int g1z = 9891;
        int g1 = pip_find_loc(3423, 9891, 0, loc_grave1, 40, &g1x, &g1z);
        int g2x = 3423;
        int g2z = 9891;
        int g2 = pip_find_loc(3423, 9891, 0, loc_grave2, 40, &g2x, &g2z);

        SELFTEST_CHECK(g1 >= 0, "priestperil_grave_base1 should resolve near 3423,9891");
        if( g1 >= 0 )
            fprintf(stderr, "PRIESTPERIL loc grave1 at %d,%d\n", g1x, g1z);
        if( g1 >= 0 )
        {
            ToriRSServer_WorldTeleport(srv, 0, g1x, g1z);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_grave1, -1, g1);
            pip_close(srv);
            pip_pass("oploc1_monument_study");

            pip_clear_inv(player);
            pip_give(player, obj_gold, 1);
            player->last_useitem = obj_gold;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_grave1, -1, g1);
            pip_close(srv);
            SELFTEST_CHECK(pip_inv_total(player, obj_iron) == 1,
                           "gold key on the key monument should grant the iron key");
            pip_pass("oplocu_monument_swap_iron");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_grave1, -1, g1);
            pip_close(srv);
            SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                           "steal-prevented must leave the player alive");
            pip_pass("oploc2_monument_steal_prevented");
        }
        if( g2 >= 0 && obj_hammer >= 0 )
        {
            pip_clear_inv(player);
            pip_give(player, obj_hammer, 1);
            player->last_useitem = obj_hammer;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_grave2, -1, g2);
            pip_close(srv);
            pip_pass("oplocu_monument_swap_other");
        }
    }

    /* ---- Coffin ---- */
    ToriRSServer_WorldTeleport(srv, 2, 3413, 3486);
    selftest_tick(srv);
    {
        int cx = 3413;
        int cz = 3486;
        int cslot = pip_find_loc(3413, 3486, 2, loc_coffin, 12, &cx, &cz);

        SELFTEST_CHECK(cslot >= 0, "priestperil_coffin_noanim should resolve near 3413,3486,2");
        if( cslot >= 0 )
        {
            player->varps[varp] = 6;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_coffin, -1, cslot);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "looking at the coffin should open the alive-inside chat");
            pip_close(srv);
            pip_pass("oploc1_coffin_look");

            pip_clear_inv(player);
            pip_give(player, obj_murky, 1);
            player->last_useitem = obj_murky;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_coffin, -1, cslot);
            pip_close(srv);
            pip_pass("oplocu_coffin_murky_refuse");

            if( obj_water >= 0 )
            {
                pip_clear_inv(player);
                pip_give(player, obj_water, 1);
                player->last_useitem = obj_water;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_coffin, -1,
                                                    cslot);
                pip_close(srv);
                pip_pass("oplocu_coffin_plain_water");
            }

            pip_clear_inv(player);
            pip_give(player, obj_blessed, 1);
            player->last_useitem = obj_blessed;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_coffin, -1, cslot);
            pip_close(srv);
            SELFTEST_CHECK(player->varps[varp] == 7,
                           "pouring blessed water should write priestperil=7, got %d",
                           player->varps[varp]);
            pip_pass("oplocu_coffin_pour_blessed");
        }
    }

    if( drezel_slot >= 0 )
    {
        ToriRSServer_WorldTeleport(srv, 2, 3417, 3489);
        selftest_tick(srv);
        player->varps[varp] = 7;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_drezel, -1, drezel_slot);
        selftest_click_through(srv, 12);
        pip_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 8,
                       "telling trapped Drezel the coffin is sealed should write 8, got %d",
                       player->varps[varp]);
        pip_pass("opnpc1_drezel_after_pour");
    }

    /* ---- Mausoleum Drezel: Salve / essence / complete / barrier ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3440, 9895);
    selftest_tick(srv);
    drezel2_slot = ToriRSServer_WorldNpcSpawn(srv, npc_drezel2, 3440, 9895, 0);
    SELFTEST_CHECK(drezel2_slot >= 0, "priestperiltrappedmonk2 should spawn");
    if( drezel2_slot >= 0 )
    {
        player->varps[varp] = 8;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_drezel2, -1, drezel2_slot);
        selftest_click_through(srv, 16);
        pip_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 10,
                       "mausoleum Drezel should ask for essence and write 10, got %d",
                       player->varps[varp]);
        pip_pass("opnpc1_drezel_essence_ask");

        player->varps[varp] = 10;
        pip_clear_inv(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_drezel2, -1, drezel2_slot);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "talking without essence should open the still-need chat");
        pip_close(srv);
        pip_pass("opnpc1_drezel_essence_need");

        pip_clear_inv(player);
        pip_give(player, obj_essence, 10);
        player->last_useitem = obj_essence;
        player->last_useslot = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_drezel2, -1, drezel2_slot);
        selftest_click_through(srv, 8);
        pip_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 20,
                       "handing 10 essence at state 10 should write 20, got %d",
                       player->varps[varp]);
        pip_pass("opnpcu_drezel_essence_progress");

        {
            int xp_before = (stat_prayer >= 0) ? player->stat_xp_tenths[stat_prayer] : 0;

            pip_clear_inv(player);
            pip_give(player, obj_essence, 1);
            player->varps[varp] = 59;
            player->last_useitem = obj_essence;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_drezel2, -1,
                                           drezel2_slot);
            selftest_click_through(srv, 24);
            pip_close(srv);
            SELFTEST_CHECK(player->varps[varp] == 60,
                           "last essence should complete the quest, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(pip_inv_total(player, obj_wolfbane) >= 1,
                           "completion should grant dagger_wolfbane");
            if( stat_prayer >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_prayer] > xp_before,
                               "completion should award real Prayer xp, %d -> %d",
                               xp_before, player->stat_xp_tenths[stat_prayer]);
            pip_pass("opnpcu_drezel_last_essence_complete");
        }

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_drezel2, -1, drezel2_slot);
        selftest_click_through(srv, 12);
        pip_close(srv);
        SELFTEST_CHECK(player->varps[varp] == 61,
                       "holy-barrier blessing should write priestperil=61, got %d",
                       player->varps[varp]);
        pip_pass("opnpc1_drezel_holy_barrier");
    }

    /* ---- Holy barrier loc ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3440, 9895);
    selftest_tick(srv);
    {
        int bx = 3440;
        int bz = 9895;
        int bslot = pip_find_loc(3440, 9895, 0, loc_barrier, 40, &bx, &bz);

        if( bslot >= 0 )
        {
            player->varps[varp] = 60;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_barrier, -1, bslot);
            pip_close(srv);
            pip_pass("oploc1_holy_barrier_stop");

            player->varps[varp] = 61;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_barrier, -1, bslot);
            pip_close(srv);
            pip_pass("oploc1_holy_barrier_pass");
        }
    }

    /* ---- Journal pages ---- */
    player->varps[varp] = 0;
    ToriRSServer_ScriptsRunProc(srv, "[proc,priestperil_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal at not-started must leave the player alive");
    pip_close(srv);
    pip_pass("journal_not_started");

    player->varps[varp] = 5;
    ToriRSServer_ScriptsRunProc(srv, "[proc,priestperil_journal]", NULL, 0);
    pip_close(srv);
    pip_pass("journal_mid");

    player->varps[varp] = 60;
    ToriRSServer_ScriptsRunProc(srv, "[proc,priestperil_journal]", NULL, 0);
    pip_close(srv);
    pip_pass("journal_complete");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "the Priest in Peril walk must leave the player alive");
    for( i = 0; i < 2; i++ )
        selftest_tick(srv);
    pip_close(srv);
    pip_free_npc(srv, roald_slot);
    pip_free_npc(srv, drezel_slot);
    pip_free_npc(srv, drezel2_slot);
    pip_free_npc(srv, dog_slot);
    pip_clear_inv(player);
    player->varps[varp] = 0;
    pip_god(player);
}

#endif
