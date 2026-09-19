/* Goblin Diplomacy -- real-trigger walk, not ::gobdiprun.
 * Live generals (OPNPC1), three crate OPLOC1s, dye OPHELDU, hand-in, complete.
 */

static void
gobdip_pass(const char* line)
{
    assert(line);
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
}

static int
gobdip_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;
    int n;

    assert(player);
    n = 0;
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            n += player->inv[i].count;
    return n;
}

static int
gobdip_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            return i;
    return -1;
}

static void
gobdip_inv_clear(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
gobdip_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static int
gobdip_drain_to_choice(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int chatmenu)
{
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 80 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( chatmenu > 0 && uid == chatmenu )
                return 1;
            if( !ToriRSServer_ScriptsResumeButton(srv, uid) )
                break;
        }
        else if( exec == SSVM_SUSPENDED || exec == SSVM_NPC_SUSPENDED ||
                 exec == SSVM_WORLD_SUSPENDED )
        {
            selftest_tick(srv);
        }
        else
        {
            break;
        }
    }
    return 0;
}

static void
gobdip_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;
    int chatmenu;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    gobdip_drain_to_choice(srv, player, chatmenu);
    for( i = 0; i < 40; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
gobdip_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot,
    const int* choices,
    int choice_count)
{
    int chatmenu;
    int i;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    gobdip_release(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    for( i = 0; i < choice_count; i++ )
    {
        if( !gobdip_drain_to_choice(srv, player, chatmenu) )
            break;
        player->last_slot = choices[i];
        if( chatmenu > 0 )
            ToriRSServer_ScriptsResumeButton(srv, chatmenu);
    }
    gobdip_drain_to_choice(srv, player, chatmenu);
    gobdip_drain_rewards(srv, player);
}

static int
gobdip_find_loc(int x, int z, int level, int loc_id)
{
    int slot;
    int dx;
    int dz;

    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( dx = -4; dx <= 4; dx++ )
        for( dz = -4; dz <= 4; dz++ )
        {
            slot = ToriRSServer_SceneFindLocId(x + dx, z + dz, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    return -1;
}

static void
gobdip_click_crate(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_id,
    int x,
    int z,
    int level)
{
    int slot;

    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    selftest_tick(srv);
    slot = gobdip_find_loc(x, z, level, loc_id);
    SELFTEST_CHECK(slot >= 0, "crate loc %d should exist at %d,%d,%d", loc_id, x, z, level);
    if( slot < 0 )
        return;
    gobdip_release(srv, player);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, slot);
    gobdip_drain_rewards(srv, player);
}

static void
selftest_quest_gobdip(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_bent;
    int npc_wart;
    int npc_bar;
    int loc_c1;
    int loc_c2;
    int loc_c3;
    int obj_mail;
    int obj_orange;
    int obj_blue;
    int obj_odye;
    int obj_bdye;
    int obj_gold;
    int vb_main;
    int vb_c1;
    int vb_c2;
    int vb_c3;
    int vb_vis;
    int varp_qp;
    int stat_craft;
    int bent_slot;
    int wart_slot;
    int bar_slot;
    int spawned_bent;
    int spawned_wart;
    int spawned_bar;
    int qp_before;
    int xp_before;
    int accept_choices[4];
    int leave_choices[1];

    assert(srv);
    assert(player);
    fprintf(stderr, "ToriRSServer selftest: Goblin Diplomacy real-trigger walk\n");
    fflush(stderr);

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "quest_gobdip selftest needs a compiled script pack");
    if( !loaded )
        return;

    npc_bent = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "general_bentnoze_red");
    npc_wart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "general_wartface_green");
    npc_bar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rustyanchor_bartender");
    loc_c1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "goblin_outpost_large_crate_armour1");
    loc_c2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "goblin_outpost_large_crate_armour2");
    loc_c3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "goblin_outpost_large_crate_armour3");
    obj_mail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goblin_armour");
    obj_orange = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goblin_armour_orange");
    obj_blue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goblin_armour_darkblue");
    obj_odye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "orangedye");
    obj_bdye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bluedye");
    obj_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gold_bar");
    vb_main = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "gobdip_main");
    vb_c1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "gobdip_crate1_searched");
    vb_c2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "gobdip_crate2_searched");
    vb_c3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "gobdip_crate3_searched");
    vb_vis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "gobdip_grubfoot_vis");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");

    SELFTEST_CHECK(npc_bent > 0 && npc_wart > 0 && loc_c1 > 0 && loc_c2 > 0 && loc_c3 > 0 &&
                       obj_mail > 0 && obj_orange > 0 && obj_blue > 0 && obj_odye > 0 &&
                       obj_bdye > 0 && obj_gold > 0 && vb_main >= 0 && vb_c1 >= 0 &&
                       vb_c2 >= 0 && vb_c3 >= 0 && stat_craft >= 0,
                   "quest_gobdip symbols should resolve");
    if( npc_bent <= 0 || vb_main < 0 || loc_c1 <= 0 || obj_mail <= 0 )
        return;

    spawned_bent = -1;
    spawned_wart = -1;
    spawned_bar = -1;
    gobdip_inv_clear(player);
    ToriRSServer_VarbitSetOn(srv, player, vb_main, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_c1, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_c2, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_c3, 0);
    if( vb_vis >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_vis, 0);
    gobdip_release(srv, player);

    ToriRSServer_WorldTeleport(srv, 0, 2958, 3512);
    selftest_tick(srv);
    bent_slot = selftest_find_npc(srv, npc_bent);
    if( bent_slot < 0 )
    {
        bent_slot = ToriRSServer_WorldNpcSpawn(srv, npc_bent, 2958, 3511, 0);
        spawned_bent = bent_slot;
    }
    wart_slot = selftest_find_npc(srv, npc_wart);
    if( wart_slot < 0 )
    {
        wart_slot = ToriRSServer_WorldNpcSpawn(srv, npc_wart, 2957, 3511, 0);
        spawned_wart = wart_slot;
    }
    SELFTEST_CHECK(bent_slot >= 0, "live General Bentnoze (red) should exist");
    if( bent_slot < 0 )
        goto gobdip_done;

    /* Bartender must not start the quest. */
    if( npc_bar > 0 )
    {
        int rumour[1];

        bar_slot = ToriRSServer_WorldNpcSpawn(srv, npc_bar, player->x + 1, player->z, 0);
        spawned_bar = bar_slot;
        rumour[0] = 2;
        if( bar_slot >= 0 )
        {
            gobdip_talk(srv, player, npc_bar, bar_slot, rumour, 1);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 0,
                           "Rusty Anchor must not write gobdip_main, got %d",
                           ToriRSServer_VarbitGet(player, vb_main));
            if( ToriRSServer_VarbitGet(player, vb_main) == 0 )
                gobdip_pass("GOBDIP PASS: bartender rumour left state 0");
            ToriRSServer_WorldNpcFree(srv, bar_slot);
            ToriRSServer_WorldNpcReap(srv);
            spawned_bar = -1;
        }
    }

    /* Refuse / leave stays 0. */
    leave_choices[0] = 4;
    gobdip_talk(srv, player, npc_bent, bent_slot, leave_choices, 1);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 0,
                   "leave must leave gobdip_main=0, got %d", ToriRSServer_VarbitGet(player, vb_main));
    if( ToriRSServer_VarbitGet(player, vb_main) == 0 )
        gobdip_pass("GOBDIP PASS: start refuse via OPNPC1 left state 0");

    /* Accept: pick colour / different colour / Yes / okay. */
    accept_choices[0] = 3;
    accept_choices[1] = 3;
    accept_choices[2] = 1;
    accept_choices[3] = 3;
    gobdip_talk(srv, player, npc_bent, bent_slot, accept_choices, 4);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 3,
                   "accept via live general should write gobdip_main=3, got %d",
                   ToriRSServer_VarbitGet(player, vb_main));
    if( ToriRSServer_VarbitGet(player, vb_main) == 3 )
        gobdip_pass("GOBDIP PASS: start accept via OPNPC1 wrote state 3");

    /* Three crates. */
    gobdip_click_crate(srv, player, loc_c1, 2959, 3514, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_c1) == 1 && gobdip_inv_total(player, obj_mail) >= 1,
                   "crate 1 Search should grant mail and set bit");
    if( ToriRSServer_VarbitGet(player, vb_c1) == 1 )
        gobdip_pass("GOBDIP PASS: crate1 oploc1 granted mail");

    gobdip_click_crate(srv, player, loc_c1, 2959, 3514, 0);
    SELFTEST_CHECK(gobdip_inv_total(player, obj_mail) == 1, "repeat crate 1 must not grant a second mail");
    if( gobdip_inv_total(player, obj_mail) == 1 )
        gobdip_pass("GOBDIP PASS: crate1 repeat search empty");

    gobdip_click_crate(srv, player, loc_c2, 2951, 3508, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_c2) == 1 && gobdip_inv_total(player, obj_mail) >= 2,
                   "crate 2 Search should grant mail and set bit");
    if( ToriRSServer_VarbitGet(player, vb_c2) == 1 )
        gobdip_pass("GOBDIP PASS: crate2 oploc1 granted mail");

    gobdip_click_crate(srv, player, loc_c3, 2955, 3498, 2);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_c3) == 1 && gobdip_inv_total(player, obj_mail) >= 3,
                   "crate 3 Search should grant mail and set bit");
    if( ToriRSServer_VarbitGet(player, vb_c3) == 1 )
        gobdip_pass("GOBDIP PASS: crate3 oploc1 granted mail");

    /* Dye: use orange dye on one brown mail; brown copies in other slots stay. */
    {
        int mail_slot;
        int dye_slot;
        int brown_before;

        inv_set(player, 10, obj_odye, 1);
        inv_set(player, 11, obj_bdye, 1);
        mail_slot = gobdip_inv_slot(player, obj_mail);
        dye_slot = gobdip_inv_slot(player, obj_odye);
        SELFTEST_CHECK(mail_slot >= 0 && dye_slot >= 0, "dye and mail slots should exist");
        brown_before = gobdip_inv_total(player, obj_mail);
        player->last_item = obj_mail;
        player->last_slot = mail_slot;
        player->last_useitem = obj_odye;
        player->last_useslot = dye_slot;
        ToriRSServer_ScriptsRunOpheldu(srv, obj_mail, -1, obj_odye, -1);
        SELFTEST_CHECK(gobdip_inv_total(player, obj_orange) == 1,
                       "OPHELDU orange dye should produce orange mail");
        SELFTEST_CHECK(gobdip_inv_total(player, obj_mail) == brown_before - 1,
                       "OPHELDU must consume only the clicked brown mail");
        if( gobdip_inv_total(player, obj_orange) == 1 )
            gobdip_pass("GOBDIP PASS: opheldu orange dye is slot-safe");

        mail_slot = gobdip_inv_slot(player, obj_mail);
        dye_slot = gobdip_inv_slot(player, obj_bdye);
        if( mail_slot >= 0 && dye_slot >= 0 )
        {
            player->last_item = obj_mail;
            player->last_slot = mail_slot;
            player->last_useitem = obj_bdye;
            player->last_useslot = dye_slot;
            ToriRSServer_ScriptsRunOpheldu(srv, obj_mail, -1, obj_bdye, -1);
        }
        SELFTEST_CHECK(gobdip_inv_total(player, obj_blue) == 1,
                       "OPHELDU blue dye should produce blue mail");
        if( gobdip_inv_total(player, obj_blue) == 1 )
            gobdip_pass("GOBDIP PASS: opheldu blue dye converted one mail");
    }

    /* Return to generals for the 3-4-5-6 ladder. */
    ToriRSServer_WorldTeleport(srv, 0, 2958, 3512);
    selftest_tick(srv);
    bent_slot = selftest_find_npc(srv, npc_bent);
    if( bent_slot < 0 && spawned_bent < 0 )
    {
        bent_slot = ToriRSServer_WorldNpcSpawn(srv, npc_bent, 2958, 3511, 0);
        spawned_bent = bent_slot;
    }
    else if( bent_slot < 0 )
        bent_slot = spawned_bent;
    SELFTEST_CHECK(bent_slot >= 0, "generals should still be present for hand-in");
    if( bent_slot < 0 )
        goto gobdip_done;

    gobdip_talk(srv, player, npc_bent, bent_slot, NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 4,
                   "orange fitting should write gobdip_main=4, got %d",
                   ToriRSServer_VarbitGet(player, vb_main));
    SELFTEST_CHECK(gobdip_inv_total(player, obj_orange) == 0, "orange mail should be consumed");
    if( ToriRSServer_VarbitGet(player, vb_main) == 4 )
        gobdip_pass("GOBDIP PASS: orange hand-in wrote state 4");

    gobdip_talk(srv, player, npc_bent, bent_slot, NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 5,
                   "blue fitting should write gobdip_main=5, got %d",
                   ToriRSServer_VarbitGet(player, vb_main));
    if( ToriRSServer_VarbitGet(player, vb_main) == 5 )
        gobdip_pass("GOBDIP PASS: blue hand-in wrote state 5");

    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    xp_before = player->stat_xp_tenths[stat_craft];
    gobdip_talk(srv, player, npc_bent, bent_slot, NULL, 0);
    gobdip_drain_rewards(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 6,
                   "brown fitting should complete gobdip_main=6, got %d",
                   ToriRSServer_VarbitGet(player, vb_main));
    SELFTEST_CHECK(gobdip_inv_total(player, obj_gold) == 1, "completion should grant 1 gold bar");
    SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] == xp_before + 2000,
                   "completion should grant 200 Crafting XP, %d -> %d", xp_before,
                   player->stat_xp_tenths[stat_craft]);
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 5,
                       "completion should award 5 QP, %d -> %d", qp_before, player->varps[varp_qp]);
    if( ToriRSServer_VarbitGet(player, vb_main) == 6 )
        gobdip_pass("GOBDIP PASS: brown hand-in completed state 6 with XP and gold bar");

    gobdip_talk(srv, player, npc_bent, bent_slot, NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 6, "postquest talk must stay at 6");
    SELFTEST_CHECK(gobdip_inv_total(player, obj_gold) == 1, "postquest must not grant another gold bar");
    if( ToriRSServer_VarbitGet(player, vb_main) == 6 )
        gobdip_pass("GOBDIP PASS: postquest OPNPC1 stayed complete");

    gobdip_pass("GOBDIP PASS: start-to-complete via live generals");

gobdip_done:
    if( spawned_bent >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_bent);
    if( spawned_wart >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_wart);
    if( spawned_bar >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_bar);
    ToriRSServer_WorldNpcReap(srv);
    gobdip_inv_clear(player);
    ToriRSServer_VarbitSetOn(srv, player, vb_main, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_c1, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_c2, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_c3, 0);
}
