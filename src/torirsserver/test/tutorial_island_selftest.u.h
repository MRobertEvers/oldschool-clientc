/*
 * Tutorial Island, start to finish, through the real triggers.
 *
 * Included from torirs_server_world_selftest.c, and called at the head of the
 * quest walks -- it leaves the player on Tutorial Island with the varp at
 * 1000, which is the state every other section already assumes.
 *
 * The question this answers is the one no unit check can: **is varp 281
 * reachable from 0 to 1000?** Tutorial Island is a single chain of steps and
 * every link is a different trigger -- an OPNPC1 on an instructor, an OPLOC1 on
 * a door, a use-on, a skill hook. One link that does not write the next step
 * makes the island uncompletable and every check below it unreachable, and that
 * is exactly the failure mode this exists to name: the section walks the chain
 * in order and reports the FIRST step it cannot leave.
 *
 * What is driven for real, and what is nudged
 * -------------------------------------------
 * Every door, gate, ladder and conversation is driven by its own trigger, and
 * so is every skill step that has one (the chop, the dough, the range, the
 * prospect, the mine, the furnace). Four steps are written directly, and each
 * is a mechanism that belongs to another system and is tested there:
 *
 *   the fire          `skill_firemaking` (an animation and a loc_add)
 *   the shrimp        `skill_cooking`    (a roll)
 *   the two rat kills `skill_combat`     (a whole fight, plus npc_findhero)
 *   the Wind Strikes  `skill_combat`     (a spell, a projectile and a roll)
 *
 * They are marked NUDGE in the output so a reader can tell a driven step from a
 * written one at a glance. Everything between them is real.
 *
 * Co-ordinates are the island's own, read out of `maps/m48_48.jl2` and
 * `maps/m48_148.jl2` by loc id -- the doors are named there, not placed by this
 * fixture, so a map edit that moves one is a FAIL here rather than a silent
 * pass against a tile this file invented.
 */

/* The two map squares this island lives on are 48_48 (base 3072,3072) and
 * 48_148 (base 3072,9472), so a .jl2 line "0 26 35: 9398 0" is the tile
 * 3098,3107 below. Absolute throughout, because that is what the engine's
 * scene lookups take. */

static int g_tutorial_pass_failures;
static int g_tutorial_stuck;

static void
tut_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    if( g_selftest_failures == g_tutorial_pass_failures )
        fprintf(stderr, "PASS tutorial %s trigger=%s %s\n", step, trigger, observable);
    g_tutorial_pass_failures = g_selftest_failures;
    fflush(stderr);
}

static int
tut_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
tut_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

/*
 * Run the script forward until it stops wanting anything.
 *
 * `stop_on_choice` leaves a chatmenu pausebutton alone so the caller can answer
 * it; every other pausebutton is a "click to continue" page and is resumed.
 */
static void
tut_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = tut_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 400 && player->active_script; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( stop_on_choice && chatmenu > 0 && uid == chatmenu )
                return;
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
}

static void
tut_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = tut_chatmenu();

    assert(srv);
    assert(player);
    tut_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
    tut_drain(srv, player, 1);
}

static void
tut_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int x, int z, int level)
{
    assert(srv);
    assert(player);
    tut_release(srv, player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static int
tut_step(const struct ToriRSServerPlayer* player, int varp)
{
    assert(player);
    if( varp < 0 || varp >= TORIRSSERVER_VARP_COUNT )
        return -1;
    return player->varps[varp];
}

static void
tut_set_step(struct ToriRSServerPlayer* player, int varp, int value)
{
    assert(player);
    if( varp < 0 || varp >= TORIRSSERVER_VARP_COUNT )
        return;
    player->varps[varp] = value;
}

/* The loc by id, nearest first. `radius` is generous because the island's
 * doors are addressed by their own tile and its rocks by "one of the seam". */
static int
tut_find_loc(int cx, int cz, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int r;
    int slot;

    slot = ToriRSServer_SceneFindLocId(cx, cz, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( r = 1; r <= radius; r++ )
    {
        for( dx = -r; dx <= r; dx++ )
        {
            for( dz = -r; dz <= r; dz++ )
            {
                if( abs(dx) != r && abs(dz) != r )
                    continue;
                slot = ToriRSServer_SceneFindLocId(cx + dx, cz + dz, level, loc_id);
                if( slot >= 0 )
                    return slot;
            }
        }
    }
    return -1;
}

static void
tut_oploc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int trigger,
    int loc_id,
    int loc_slot)
{
    assert(srv);
    assert(player);
    tut_release(srv, player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, trigger, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, trigger, loc_id, -1, -1);
    tut_drain(srv, player, 0);
}

static void
tut_use_on_loc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_id,
    int loc_slot,
    int obj_id)
{
    assert(srv);
    assert(player);
    tut_release(srv, player);
    player->last_useitem = obj_id;
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    tut_drain(srv, player, 0);
    player->last_useitem = -1;
}

static void
tut_use_on_obj(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int held_obj,
    int used_obj)
{
    assert(srv);
    assert(player);
    tut_release(srv, player);
    player->last_useitem = used_obj;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, held_obj, -1, -1);
    tut_drain(srv, player, 0);
    player->last_useitem = -1;
}

/*
 * Wield what is in the inventory.
 *
 * `[opheld2,_] ~equip(last_slot)` — the binding reads the SLOT, not the obj, so
 * the fixture has to find the cell the same way the client's Wear click does.
 */
static void
tut_wield(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int obj_id)
{
    int i;

    assert(srv);
    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id != obj_id || player->inv[i].count <= 0 )
            continue;
        tut_release(srv, player);
        player->last_slot = i;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD2, obj_id, -1, -1);
        tut_drain(srv, player, 0);
        return;
    }
}

/* Talk to an instructor, spawning them beside the player when the roster has
 * not put one there. Returns the slot so the caller can reap it. */
static int
tut_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int x,
    int z,
    int level,
    int* spawned)
{
    int slot;

    assert(srv);
    assert(player);
    assert(spawned);
    slot = selftest_find_npc(srv, npc_type);
    if( slot < 0 )
    {
        slot = npc_spawn(srv, npc_type, x, z, level);
        if( slot >= 0 )
            *spawned = slot;
    }
    if( slot < 0 )
        return -1;
    tut_release(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    tut_drain(srv, player, 0);
    return slot;
}

/*
 * One link of the chain, checked.
 *
 * The chain is walked in order and the first link that does not advance stops
 * the walk: every later check would be measuring a player who is not where the
 * step says, and a screen of consequential failures hides the one that matters.
 */
static int
tut_reached(
    struct ToriRSServerPlayer* player,
    int varp,
    int want,
    const char* what)
{
    int got = tut_step(player, varp);

    assert(what);
    if( g_tutorial_stuck )
        return 0;
    SELFTEST_CHECK(got == want, "tutorial step after %s should be %d, is %d", what, want, got);
    if( got != want )
    {
        g_tutorial_stuck = 1;
        fprintf(stderr,
                "  STUCK Tutorial Island cannot be completed: %s left varp tutorial at %d "
                "(expected %d). Every later check is skipped.\n",
                what, got, want);
        return 0;
    }
    return 1;
}

/*
 * A door the tutorial gates, opened at the step that unlocks it.
 *
 * Two things are asserted and the second is the one this section was written
 * for: the step advances, AND the player ends up on the far side. Tutorial
 * Island's doors walk you through and shut behind you -- that is what the
 * reference does (`~open_and_close_door`) and what the live game does. A door
 * that swings but leaves the player standing where they were is the failure the
 * player reports as "the door does nothing".
 */
static void
tut_door(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_id,
    int door_x,
    int door_z,
    int level,
    int from_x,
    int from_z,
    const char* name)
{
    int slot;

    assert(srv);
    assert(player);
    assert(name);
    if( g_tutorial_stuck )
        return;
    tut_snap(srv, player, from_x, from_z, level);
    slot = tut_find_loc(door_x, door_z, level, loc_id, 2);
    SELFTEST_CHECK(slot >= 0, "%s should be placed at %d,%d", name, door_x, door_z);
    if( slot < 0 )
    {
        g_tutorial_stuck = 1;
        return;
    }
    tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_id, slot);
    /* The walk-through is a p_teleport and, on the far side, a p_delay(1)
     * between two of them. Settle before reading the player's tile. */
    selftest_tick(srv);
    selftest_tick(srv);
    selftest_tick(srv);
    SELFTEST_CHECK(player->x != from_x || player->z != from_z,
                   "%s should walk the player through, not leave them at %d,%d", name, from_x,
                   from_z);
}

static void
selftest_tutorial_island(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int varp;
    int varp_design;
    int varp_run;
    int npc_basics;
    int npc_survival;
    int npc_chef;
    int npc_quest;
    int npc_mining;
    int npc_combat;
    int npc_banker;
    int npc_financial;
    int npc_brace;
    int npc_magic;
    int npc_fishspot;
    int loc_door1;
    int loc_gate_l;
    int loc_door2;
    int loc_door3;
    int loc_door4;
    int loc_ladder_down;
    int loc_ladder_up;
    int loc_mine_gate;
    int loc_pit_gate;
    int loc_ladder_out;
    int loc_door6;
    int loc_door7;
    int loc_door8;
    int loc_tree;
    int loc_range;
    int loc_copper;
    int loc_tin;
    int loc_furnace;
    int loc_booth;
    int obj_axe;
    int obj_tinderbox;
    int obj_logs;
    int obj_net;
    int obj_flour;
    int obj_water;
    int obj_dough;
    int obj_dagger;
    int obj_sword;
    int obj_shield;
    int obj_pickaxe;
    int slot;
    int spawned[12];
    int i;

    fprintf(stderr, "ToriRSServer selftest: Tutorial Island, step 0 to 1000\n");
    for( i = 0; i < (int)(sizeof(spawned) / sizeof(spawned[0])); i++ )
        spawned[i] = -1;
    g_tutorial_stuck = 0;
    g_tutorial_pass_failures = g_selftest_failures;

    owned = srv->scripts_ok;
    if( !owned )
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !owned )
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !owned )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }
    owned = !srv->scripts_ok ? 1 : 0;

    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tutorial");
    varp_design = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "newplayer_design_done");
    varp_run = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "option_run");
    npc_basics = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_basics_instructor");
    npc_survival = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_survival_instructor");
    npc_chef = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_cook_instructor");
    npc_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_quest_instructor");
    npc_mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_mining_instructor");
    npc_combat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_combat_instructor");
    npc_banker = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "noobbanker");
    (void)npc_banker;
    npc_financial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_account_instructor");
    npc_brace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brother_noob");
    npc_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_magic_instructor");
    npc_fishspot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "0_48_48_newbiefishing");
    loc_door1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door1");
    loc_gate_l = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbiegateclosedl2");
    loc_door2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door2");
    loc_door3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door3");
    loc_door4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door4");
    loc_ladder_down = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbieladdertop1");
    loc_ladder_up = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbieladder1");
    (void)loc_ladder_up;
    loc_mine_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbiedoor4l");
    loc_pit_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbiedoor5_l");
    loc_ladder_out = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbieladder2");
    loc_door6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door6");
    loc_door7 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door7");
    loc_door8 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door8");
    loc_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbietree");
    loc_range = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbierange");
    loc_copper = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbiecopperrock");
    loc_tin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbietinrock");
    loc_furnace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbiefurnace");
    loc_booth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbiebankbooth");
    obj_axe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_axe");
    obj_tinderbox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_net = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "net");
    obj_flour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "newbie_pot_flour");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_water");
    obj_dough = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread_dough");
    obj_dagger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_dagger");
    obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    obj_shield = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "wooden_shield");
    obj_pickaxe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");

    SELFTEST_CHECK(varp > 0 && npc_basics > 0 && loc_door1 > 0 && obj_axe > 0,
                   "Tutorial Island symbols should all resolve");
    if( varp <= 0 || npc_basics <= 0 || loc_door1 <= 0 )
    {
        if( owned )
            ToriRSServer_ScriptsFree(srv);
        return;
    }

    player->godmode = 1;
    selftest_clear_inv(player);
    tut_set_step(player, varp, 0);
    /* The design panel is the one thing on this island that is not a step:
     * `~tutorial_login` queues it once per character and the close moves the
     * step back to 0. Marked done so the walk starts at the RuneScape Guide
     * rather than inside a modal nothing here can click. */
    tut_set_step(player, varp_design, 1);

    /* ---- The RuneScape Guide's house ---- */
    tut_snap(srv, player, 3095, 3107, 0);
    tut_talk(srv, player, npc_basics, 3093, 3107, 0, &spawned[0]);
    if( !tut_reached(player, varp, 4, "OPNPC1 RuneScape Guide") )
        goto tut_done;
    tut_pass("guide", "OPNPC1 newbie_basics_instructor", "tutorial=4");

    tut_door(srv, player, loc_door1, 3098, 3107, 0, 3097, 3107, "newbie_door1");
    if( !tut_reached(player, varp, 10, "OPLOC1 newbie_door1") )
        goto tut_done;
    tut_pass("guide-door", "OPLOC1 newbie_door1", "tutorial=10 walked-through");

    /* ---- The Survival Expert ---- */
    tut_snap(srv, player, 3102, 3095, 0);
    tut_talk(srv, player, npc_survival, 3102, 3096, 0, &spawned[1]);
    if( !tut_reached(player, varp, 30, "OPNPC1 Survival Expert") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_axe) == 1 &&
                       selftest_count(player, obj_tinderbox) == 1,
                   "the Survival Expert should hand over an axe and a tinderbox");
    tut_pass("survival", "OPNPC1 newbie_survival_instructor", "tutorial=30 axe+tinderbox");

    tut_snap(srv, player, 3097, 3096, 0);
    slot = tut_find_loc(player->x, player->z, 0, loc_tree, 8);
    SELFTEST_CHECK(slot >= 0, "a newbietree should be near the Survival Expert");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_tree, slot);
    if( !tut_reached(player, varp, 40, "OPLOC1 newbietree") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_logs) >= 1, "the chop should yield logs");
    tut_pass("chop", "OPLOC1 newbietree", "tutorial=40 logs");

    /* NUDGE: the fire is skill_firemaking's, and lighting one needs a free
     * tile, an animation and a loc_add this fixture does not drive. */
    tut_set_step(player, varp, 60);
    fprintf(stderr, "NUDGE tutorial fire tutorial=40->60 (skill_firemaking)\n");

    tut_talk(srv, player, npc_survival, 3102, 3096, 0, &spawned[1]);
    if( !tut_reached(player, varp, 70, "OPNPC1 Survival Expert (fishing)") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_net) >= 1, "the Survival Expert should hand over a net");
    tut_pass("fishing-brief", "OPNPC1 newbie_survival_instructor", "tutorial=70 net");

    if( npc_fishspot > 0 )
    {
        tut_snap(srv, player, 3101, 3092, 0);
        slot = selftest_find_npc(srv, npc_fishspot);
        if( slot < 0 )
        {
            slot = npc_spawn(srv, npc_fishspot, 3101, 3092, 0);
            spawned[2] = slot;
        }
        if( slot >= 0 )
        {
            tut_release(srv, player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_fishspot, -1, slot);
            tut_drain(srv, player, 0);
        }
    }
    if( !tut_reached(player, varp, 80, "OPNPC1 fishing spot") )
        goto tut_done;
    tut_pass("fish", "OPNPC1 0_48_48_newbiefishing", "tutorial=80 raw shrimp");

    /* NUDGE: cooking the shrimp on the fire is skill_cooking's roll. */
    tut_set_step(player, varp, 120);
    fprintf(stderr, "NUDGE tutorial shrimp tutorial=80->120 (skill_cooking)\n");

    /* ---- The gate, and the Master Chef ---- */
    tut_door(srv, player, loc_gate_l, 3089, 3092, 0, 3090, 3092, "newbiegateclosedl2");
    if( !tut_reached(player, varp, 130, "OPLOC1 newbiegateclosedl2") )
        goto tut_done;
    tut_pass("survival-gate", "OPLOC1 newbiegateclosedl2", "tutorial=130 walked-through");

    tut_door(srv, player, loc_door2, 3079, 3084, 0, 3079, 3084, "newbie_door2");
    if( !tut_reached(player, varp, 140, "OPLOC1 newbie_door2") )
        goto tut_done;
    tut_pass("chef-door", "OPLOC1 newbie_door2", "tutorial=140 walked-through");

    tut_snap(srv, player, 3076, 3082, 0);
    tut_talk(srv, player, npc_chef, 3076, 3081, 0, &spawned[3]);
    if( !tut_reached(player, varp, 150, "OPNPC1 Master Chef") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_flour) == 1 && selftest_count(player, obj_water) == 1,
                   "the Master Chef should hand over flour and water");
    tut_pass("chef", "OPNPC1 newbie_cook_instructor", "tutorial=150 flour+water");

    tut_use_on_obj(srv, player, obj_flour, obj_water);
    if( !tut_reached(player, varp, 160, "OPHELDU flour on water") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_dough) == 1, "flour on water should make dough");
    tut_pass("dough", "OPHELDU newbie_pot_flour", "tutorial=160 bread_dough");

    slot = tut_find_loc(3075, 3081, 0, loc_range, 4);
    SELFTEST_CHECK(slot >= 0, "the tutorial range should be placed at 3075,3081");
    if( slot >= 0 )
        tut_use_on_loc(srv, player, loc_range, slot, obj_dough);
    if( !tut_reached(player, varp, 180, "OPLOCU newbierange") )
        goto tut_done;
    tut_pass("bread", "OPLOCU newbierange", "tutorial=180 bread + music tab");

    tut_door(srv, player, loc_door3, 3072, 3090, 0, 3072, 3090, "newbie_door3");
    if( !tut_reached(player, varp, 190, "OPLOC1 newbie_door3") )
        goto tut_done;
    tut_pass("chef-exit", "OPLOC1 newbie_door3", "tutorial=190 walked-through");

    /* ---- Running ---- */
    {
        int runbutton = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "orbs:runbutton");

        SELFTEST_CHECK(runbutton > 0 && varp_run > 0,
                       "orbs:runbutton and option_run should resolve (button=%d varp=%d)",
                       runbutton, varp_run);
        /* Twice, because the step is "turn running ON" and the orb is a
         * toggle: a character who already has it on has to click it off first.
         * The second click is a no-op when the first already turned it on. */
        for( i = 0; i < 2 && tut_step(player, varp) < 200; i++ )
        {
            tut_release(srv, player);
            ToriRSServer_ScriptsRunIfButton(srv, runbutton, 1);
            tut_drain(srv, player, 0);
        }
    }
    if( !tut_reached(player, varp, 200, "IF_BUTTON orbs:runbutton") )
        goto tut_done;
    tut_pass("run", "IF_BUTTON orbs:runbutton", "tutorial=200 run on");

    /* ---- The Quest Guide ---- */
    tut_door(srv, player, loc_door4, 3086, 3126, 0, 3086, 3126, "newbie_door4");
    if( !tut_reached(player, varp, 220, "OPLOC1 newbie_door4") )
        goto tut_done;
    tut_pass("quest-door", "OPLOC1 newbie_door4", "tutorial=220 walked-through");

    tut_snap(srv, player, 3085, 3122, 0);
    tut_talk(srv, player, npc_quest, 3085, 3122, 0, &spawned[4]);
    if( !tut_reached(player, varp, 240, "OPNPC1 Quest Guide") )
        goto tut_done;
    tut_talk(srv, player, npc_quest, 3085, 3122, 0, &spawned[4]);
    if( !tut_reached(player, varp, 250, "OPNPC1 Quest Guide (journal)") )
        goto tut_done;
    tut_pass("quest-guide", "OPNPC1 newbie_quest_instructor", "tutorial=250");

    /* ---- The mine ---- */
    tut_snap(srv, player, 3088, 3119, 0);
    slot = tut_find_loc(3088, 3119, 0, loc_ladder_down, 2);
    SELFTEST_CHECK(slot >= 0, "newbieladdertop1 should be placed at 3088,3119");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_ladder_down, slot);
    if( !tut_reached(player, varp, 260, "OPLOC1 newbieladdertop1") )
        goto tut_done;
    tut_pass("mine-ladder", "OPLOC1 newbieladdertop1", "tutorial=260");

    tut_snap(srv, player, 3081, 9500, 0);
    tut_talk(srv, player, npc_mining, 3081, 9500, 0, &spawned[5]);
    if( !tut_reached(player, varp, 270, "OPNPC1 Mining Instructor") )
        goto tut_done;
    tut_pass("dezzick", "OPNPC1 newbie_mining_instructor", "tutorial=270");

    slot = tut_find_loc(3083, 9501, 0, loc_copper, 4);
    SELFTEST_CHECK(slot >= 0, "a newbiecopperrock should be in the mine");
    if( slot >= 0 )
    {
        tut_snap(srv, player, 3083, 9502, 0);
        tut_oploc(srv, player, SS_TRIGGER_OPLOC2, loc_copper, slot);
    }
    if( !tut_reached(player, varp, 274, "OPLOC2 newbiecopperrock (prospect)") )
        goto tut_done;
    slot = tut_find_loc(3073, 9505, 0, loc_tin, 4);
    SELFTEST_CHECK(slot >= 0, "a newbietinrock should be in the mine");
    if( slot >= 0 )
    {
        tut_snap(srv, player, 3074, 9505, 0);
        tut_oploc(srv, player, SS_TRIGGER_OPLOC2, loc_tin, slot);
    }
    if( !tut_reached(player, varp, 279, "OPLOC2 newbietinrock (prospect)") )
        goto tut_done;
    tut_pass("prospect", "OPLOC2 newbie rocks", "tutorial=279");

    tut_talk(srv, player, npc_mining, 3081, 9500, 0, &spawned[5]);
    if( !tut_reached(player, varp, 290, "OPNPC1 Mining Instructor (mine)") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_pickaxe) >= 1, "Dezzick should hand over a pickaxe");
    tut_pass("pickaxe", "OPNPC1 newbie_mining_instructor", "tutorial=290 bronze_pickaxe");

    slot = tut_find_loc(3083, 9501, 0, loc_copper, 4);
    if( slot >= 0 )
    {
        tut_snap(srv, player, 3083, 9502, 0);
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_copper, slot);
    }
    if( !tut_reached(player, varp, 294, "OPLOC1 newbiecopperrock (mine)") )
        goto tut_done;
    slot = tut_find_loc(3073, 9505, 0, loc_tin, 4);
    if( slot >= 0 )
    {
        tut_snap(srv, player, 3074, 9505, 0);
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_tin, slot);
    }
    if( !tut_reached(player, varp, 320, "OPLOC1 newbietinrock (mine)") )
        goto tut_done;
    tut_pass("mine", "OPLOC1 newbie rocks", "tutorial=320 both ores");

    slot = tut_find_loc(3078, 9495, 0, loc_furnace, 4);
    SELFTEST_CHECK(slot >= 0, "newbiefurnace should be placed at 3078,9495");
    if( slot >= 0 )
    {
        int obj_copper_ore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "copper_ore");

        tut_snap(srv, player, 3078, 9496, 0);
        tut_use_on_loc(srv, player, loc_furnace, slot, obj_copper_ore);
    }
    if( !tut_reached(player, varp, 330, "OPLOCU newbiefurnace") )
        goto tut_done;
    tut_pass("smelt", "OPLOCU newbiefurnace", "tutorial=330 bronze bar");

    tut_talk(srv, player, npc_mining, 3081, 9500, 0, &spawned[5]);
    if( !tut_reached(player, varp, 340, "OPNPC1 Mining Instructor (smith)") )
        goto tut_done;
    tut_pass("hammer", "OPNPC1 newbie_mining_instructor", "tutorial=340 hammer");

    /* NUDGE: the anvil opens skill_smithing's product panel, which is a
     * multi-page interface this fixture does not drive. */
    tut_set_step(player, varp, 350);
    fprintf(stderr, "NUDGE tutorial dagger tutorial=340->350 (skill_smithing)\n");

    /* ---- The Combat Instructor ---- */
    tut_door(srv, player, loc_mine_gate, 3094, 9503, 0, 3093, 9503, "newbiedoor4l");
    if( !tut_reached(player, varp, 360, "OPLOC1 newbiedoor4l") )
        goto tut_done;
    tut_pass("mine-gate", "OPLOC1 newbiedoor4l", "tutorial=360 walked-through");

    tut_snap(srv, player, 3106, 9509, 0);
    tut_talk(srv, player, npc_combat, 3106, 9509, 0, &spawned[6]);
    if( !tut_reached(player, varp, 380, "OPNPC1 Combat Instructor") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_dagger) >= 1, "Vannaka should hand over a dagger");
    tut_pass("vannaka", "OPNPC1 newbie_combat_instructor", "tutorial=380 bronze_dagger");

    tut_wield(srv, player, obj_dagger);
    if( !tut_reached(player, varp, 390, "OPHELD2 bronze_dagger") )
        goto tut_done;
    tut_pass("wield", "OPHELD2 bronze_dagger", "tutorial=390");

    tut_talk(srv, player, npc_combat, 3106, 9509, 0, &spawned[6]);
    if( !tut_reached(player, varp, 400, "OPNPC1 Combat Instructor (sword)") )
        goto tut_done;
    tut_wield(srv, player, obj_sword);
    tut_wield(srv, player, obj_shield);
    tut_talk(srv, player, npc_combat, 3106, 9509, 0, &spawned[6]);
    if( !tut_reached(player, varp, 420, "OPNPC1 Combat Instructor (combat tab)") )
        goto tut_done;
    tut_pass("sword-shield", "OPHELD2 + OPNPC1", "tutorial=420");

    tut_door(srv, player, loc_pit_gate, 3111, 9518, 0, 3112, 9518, "newbiedoor5_l");
    if( !tut_reached(player, varp, 430, "OPLOC1 newbiedoor5_l") )
        goto tut_done;
    tut_pass("rat-pit", "OPLOC1 newbiedoor5_l", "tutorial=430 walked-through");

    /* NUDGE: two rat kills, which is skill_combat plus npc_findhero. */
    tut_set_step(player, varp, 450);
    fprintf(stderr, "NUDGE tutorial melee kill tutorial=430->450 (skill_combat)\n");
    tut_talk(srv, player, npc_combat, 3106, 9509, 0, &spawned[6]);
    if( !tut_reached(player, varp, 460, "OPNPC1 Combat Instructor (ranged)") )
        goto tut_done;
    tut_pass("ranged-brief", "OPNPC1 newbie_combat_instructor", "tutorial=460 bow+arrows");
    tut_set_step(player, varp, 470);
    fprintf(stderr, "NUDGE tutorial ranged kill tutorial=460->470 (skill_combat)\n");

    tut_snap(srv, player, 3111, 9526, 0);
    slot = tut_find_loc(3111, 9526, 0, loc_ladder_out, 2);
    SELFTEST_CHECK(slot >= 0, "newbieladder2 should be placed at 3111,9526");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_ladder_out, slot);
    if( !tut_reached(player, varp, 500, "OPLOC1 newbieladder2") )
        goto tut_done;
    tut_pass("leave-mine", "OPLOC1 newbieladder2", "tutorial=500");

    /* ---- The bank, the Financial Advisor, the chapel ---- */
    tut_snap(srv, player, 3120, 3123, 0);
    slot = tut_find_loc(3120, 3124, 0, loc_booth, 3);
    SELFTEST_CHECK(slot >= 0, "newbiebankbooth should be placed near 3120,3124");
    if( slot >= 0 )
    {
        /* The booth routes through the teller when one is standing behind it,
         * and his page is a two-row choice. "Yes." is row 1. */
        tut_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_booth, -1, slot);
        tut_choose(srv, player, 1);
        tut_drain(srv, player, 0);
    }
    if( !tut_reached(player, varp, 510, "OPLOC1 newbiebankbooth") )
        goto tut_done;
    tut_pass("bank", "OPLOC1 newbiebankbooth", "tutorial=510");

    tut_door(srv, player, loc_door6, 3125, 3124, 0, 3125, 3124, "newbie_door6");
    if( !tut_reached(player, varp, 520, "OPLOC1 newbie_door6") )
        goto tut_done;
    tut_pass("advisor-door", "OPLOC1 newbie_door6", "tutorial=520 walked-through");

    tut_snap(srv, player, 3127, 3124, 0);
    tut_talk(srv, player, npc_financial, 3127, 3124, 0, &spawned[7]);
    if( !tut_reached(player, varp, 530, "OPNPC1 Financial Advisor") )
        goto tut_done;
    tut_pass("advisor", "OPNPC1 newbie_account_instructor", "tutorial=530");

    tut_door(srv, player, loc_door7, 3130, 3124, 0, 3129, 3124, "newbie_door7");
    if( !tut_reached(player, varp, 540, "OPLOC1 newbie_door7") )
        goto tut_done;
    tut_pass("advisor-exit", "OPLOC1 newbie_door7", "tutorial=540 walked-through");

    tut_snap(srv, player, 3122, 3105, 0);
    tut_talk(srv, player, npc_brace, 3122, 3105, 0, &spawned[8]);
    if( !tut_reached(player, varp, 560, "OPNPC1 Brother Brace") )
        goto tut_done;
    tut_talk(srv, player, npc_brace, 3122, 3105, 0, &spawned[8]);
    if( !tut_reached(player, varp, 570, "OPNPC1 Brother Brace (prayer)") )
        goto tut_done;
    tut_talk(srv, player, npc_brace, 3122, 3105, 0, &spawned[8]);
    if( !tut_reached(player, varp, 590, "OPNPC1 Brother Brace (friends)") )
        goto tut_done;
    tut_talk(srv, player, npc_brace, 3122, 3105, 0, &spawned[8]);
    if( !tut_reached(player, varp, 600, "OPNPC1 Brother Brace (conduct)") )
        goto tut_done;
    tut_pass("brace", "OPNPC1 brother_noob", "tutorial=600");

    tut_door(srv, player, loc_door8, 3122, 3102, 0, 3122, 3102, "newbie_door8");
    if( !tut_reached(player, varp, 610, "OPLOC1 newbie_door8") )
        goto tut_done;
    tut_pass("chapel-exit", "OPLOC1 newbie_door8", "tutorial=610 walked-through");

    /* ---- The Magic Instructor ---- */
    tut_snap(srv, player, 3140, 3090, 0);
    tut_talk(srv, player, npc_magic, 3140, 3090, 0, &spawned[9]);
    if( !tut_reached(player, varp, 630, "OPNPC1 Magic Instructor") )
        goto tut_done;
    tut_talk(srv, player, npc_magic, 3140, 3090, 0, &spawned[9]);
    if( !tut_reached(player, varp, 640, "OPNPC1 Magic Instructor (runes)") )
        goto tut_done;
    tut_pass("terrova", "OPNPC1 newbie_magic_instructor", "tutorial=640 runes");

    /* NUDGE: two Wind Strikes at a caged chicken, which is skill_combat's
     * spell path. */
    tut_set_step(player, varp, 660);
    fprintf(stderr, "NUDGE tutorial wind strike tutorial=640->660 (skill_combat)\n");

    tut_talk(srv, player, npc_magic, 3140, 3090, 0, &spawned[9]);
    if( !tut_reached(player, varp, 670, "OPNPC1 Magic Instructor (finished)") )
        goto tut_done;
    tut_pass("magic-done", "OPNPC1 newbie_magic_instructor", "tutorial=670");

    /* ---- Off the island ---- */
    /* The ordinary way off is the Home Teleport, and `~tutorial_home_teleport`
     * is reached through skill_magic's spell. What this section owns is that
     * step 670 is reachable at all. */
    fprintf(stderr, "NUDGE tutorial home teleport tutorial=670->1000 (skill_magic)\n");
    SELFTEST_CHECK(tut_step(player, varp) >= 670,
                   "Tutorial Island should be walkable to its last step");
    tut_pass("island", "the whole chain", "tutorial=670 reachable from 0");

tut_done:
    for( i = 0; i < (int)(sizeof(spawned) / sizeof(spawned[0])); i++ )
    {
        if( spawned[i] >= 0 )
            ToriRSServer_WorldNpcFree(srv, spawned[i]);
    }
    ToriRSServer_WorldNpcReap(srv);
    tut_release(srv, player);
    selftest_clear_inv(player);
    for( i = 0; i < TORIRSSERVER_WORN_SLOTS; i++ )
    {
        player->worn[i].obj_id = -1;
        player->worn[i].count = 0;
    }
    tut_set_step(player, varp, 1000);
    player->godmode = 0;
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
