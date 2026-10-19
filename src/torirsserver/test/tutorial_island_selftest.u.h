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
 * The chain is the October 2025 Learning the Ropes (tutorial.constant's
 * `^tut_*` steps; test/quests/tutorialisland.lua plays the same island through
 * a client, graded on the wiki transcript).
 *
 * What is driven for real, and what is nudged
 * -------------------------------------------
 * Every door, gate, ladder and conversation is driven by its own trigger, and
 * so is every flashing side-tab stone (`[if_button1,toplevel_osrs_stretch:
 * stoneN]`), the run orb, the equipment-stats button, and every skill step
 * that has one of the island's own (the net, the chop, the dough, the range,
 * the mine, the furnace, the bank booth, the poll booth, each wield). Six
 * steps are written directly, and each is a mechanism that belongs to another
 * system and is tested there:
 *
 *   the familiarity   interface 929's stone buttons (a pausebutton resume)
 *   the fire          `skill_firemaking` (a roll, an animation and a loc_add)
 *   the shrimp        `skill_cooking`    (a roll)
 *   the dagger        `skill_smithing`   (the make-menu; the dagger is handed
 *                                          over, the island's next steps wield it)
 *   the two rat kills `skill_combat`     (a whole fight, plus npc_findhero)
 *   the Wind Strike   `skill_combat`     (a spell, a projectile and a roll)
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
 * Click a flashing side-tab stone: `[if_button1,toplevel_osrs_stretch:stoneN]`,
 * which `~tutorial_tab_opened` answers when the step is waiting on that tab.
 * The client sends IF_BUTTON1 on the stone; this runs the same trigger.
 */
static void
tut_stone(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stone)
{
    char name[48];
    int uid;

    assert(srv);
    assert(player);
    if( g_tutorial_stuck )
        return;
    snprintf(name, sizeof(name), "toplevel_osrs_stretch:stone%d", stone);
    uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, name);
    SELFTEST_CHECK(uid > 0, "%s should resolve", name);
    if( uid <= 0 )
        return;
    tut_release(srv, player);
    ToriRSServer_ScriptsRunIfButton(srv, uid, 1);
    tut_drain(srv, player, 0);
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
    int npc_account;
    int npc_brace;
    int npc_magic;
    int npc_fishspot;
    int loc_door1;
    int loc_gate_l;
    int loc_door2;
    int loc_door3;
    int loc_door4;
    int loc_ladder_down;
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
    int loc_poll;
    int obj_axe;
    int obj_tinderbox;
    int obj_logs;
    int obj_net;
    int obj_raw_shrimp;
    int obj_flour;
    int obj_water;
    int obj_dough;
    int obj_bread;
    int obj_pickaxe;
    int obj_bar;
    int obj_hammer;
    int obj_dagger;
    int obj_sword;
    int obj_shield;
    int obj_bow;
    int obj_arrows;
    int obj_air;
    int obj_mind;
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

    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "varp281_tutorial");
    varp_design = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "varp7136_newplayer_design_done");
    varp_run = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "varp173_option_run");
    npc_basics = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_basics_instructor");
    npc_survival = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_survival_instructor");
    npc_chef = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_cook_instructor");
    npc_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_quest_instructor");
    npc_mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_mining_instructor");
    npc_combat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_combat_instructor");
    npc_account = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_account_instructor");
    npc_brace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brother_noob");
    npc_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "newbie_magic_instructor");
    npc_fishspot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "0_48_48_newbiefishing");
    loc_door1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door1");
    loc_gate_l = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbiegateclosedl2");
    loc_door2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door2");
    loc_door3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door3");
    loc_door4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbie_door4");
    loc_ladder_down = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "newbieladdertop1");
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
    loc_poll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pollbooth_green");
    obj_axe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_axe");
    obj_tinderbox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_net = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "net");
    obj_raw_shrimp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_shrimp");
    obj_flour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "newbie_pot_flour");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_water");
    obj_dough = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread_dough");
    obj_bread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread");
    obj_pickaxe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_bar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_bar");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_dagger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_dagger");
    obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    obj_shield = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "wooden_shield");
    obj_bow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "shortbow");
    obj_arrows = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_arrow");
    obj_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "airrune");
    obj_mind = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mindrune");

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
    /* NUDGE: the design panel and the familiarity question ("How familiar are
     * you with Old School RuneScape?", interface 929) are the two things in the
     * house that are not instructors: one is closed, the other answered on a
     * stone button. Answered, the step is ^tut_talk_to_guide. */
    tut_set_step(player, varp_design, 1);
    tut_set_step(player, varp, 2);
    fprintf(stderr, "NUDGE tutorial familiarity tutorial=0->2 (interface 929)\n");

    /* ---- The Gielinor Guide's house ---- */
    tut_snap(srv, player, 3095, 3107, 0);
    tut_talk(srv, player, npc_basics, 3093, 3107, 0, &spawned[0]);
    if( !tut_reached(player, varp, 3, "OPNPC1 Gielinor Guide") )
        goto tut_done;
    tut_stone(srv, player, 11);
    if( !tut_reached(player, varp, 7, "IF_BUTTON1 stone11 (settings)") )
        goto tut_done;
    tut_talk(srv, player, npc_basics, 3093, 3107, 0, &spawned[0]);
    if( !tut_reached(player, varp, 10, "OPNPC1 Gielinor Guide (settings opened)") )
        goto tut_done;
    tut_pass("guide", "OPNPC1 newbie_basics_instructor + stone11", "tutorial=10");

    tut_door(srv, player, loc_door1, 3098, 3107, 0, 3097, 3107, "newbie_door1");
    if( !tut_reached(player, varp, 20, "OPLOC1 newbie_door1") )
        goto tut_done;
    tut_pass("guide-door", "OPLOC1 newbie_door1", "tutorial=20 walked-through");

    /* ---- The Survival Expert ---- */
    tut_snap(srv, player, 3102, 3095, 0);
    tut_talk(srv, player, npc_survival, 3102, 3096, 0, &spawned[1]);
    if( !tut_reached(player, varp, 30, "OPNPC1 Survival Expert") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_net) == 1,
                   "the Survival Expert should hand over a net");
    tut_stone(srv, player, 3);
    if( !tut_reached(player, varp, 40, "IF_BUTTON1 stone3 (inventory)") )
        goto tut_done;
    tut_pass("survival", "OPNPC1 newbie_survival_instructor + stone3", "tutorial=40 net");

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
    if( !tut_reached(player, varp, 50, "OPNPC1 fishing spot") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_raw_shrimp) == 1, "the net should catch a shrimp");
    tut_stone(srv, player, 1);
    if( !tut_reached(player, varp, 60, "IF_BUTTON1 stone1 (skills)") )
        goto tut_done;
    tut_pass("fish", "OPNPC1 0_48_48_newbiefishing + stone1", "tutorial=60 raw shrimp");

    tut_talk(srv, player, npc_survival, 3102, 3096, 0, &spawned[1]);
    if( !tut_reached(player, varp, 70, "OPNPC1 Survival Expert (woodcutting)") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_axe) == 1 &&
                       selftest_count(player, obj_tinderbox) == 1,
                   "the Survival Expert should hand over an axe and a tinderbox");
    tut_pass("survival-tools", "OPNPC1 newbie_survival_instructor", "tutorial=70 axe+tinderbox");

    tut_snap(srv, player, 3097, 3096, 0);
    slot = tut_find_loc(player->x, player->z, 0, loc_tree, 8);
    SELFTEST_CHECK(slot >= 0, "a newbietree should be near the Survival Expert");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_tree, slot);
    if( !tut_reached(player, varp, 80, "OPLOC1 newbietree") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_logs) >= 1, "the chop should yield logs");
    tut_pass("chop", "OPLOC1 newbietree", "tutorial=80 logs");

    /* NUDGE: the fire is skill_firemaking's (a roll every four ticks, a free
     * tile, an animation and a loc_add); the shrimp on it is skill_cooking's
     * roll. */
    tut_set_step(player, varp, 90);
    fprintf(stderr, "NUDGE tutorial fire tutorial=80->90 (skill_firemaking)\n");
    tut_set_step(player, varp, 120);
    fprintf(stderr, "NUDGE tutorial shrimp tutorial=90->120 (skill_cooking)\n");

    /* ---- The gate, and the Master Chef ---- */
    tut_door(srv, player, loc_gate_l, 3089, 3092, 0, 3090, 3092, "newbiegateclosedl2");
    if( !tut_reached(player, varp, 130, "OPLOC1 newbiegateclosedl2") )
        goto tut_done;
    tut_pass("survival-gate", "OPLOC1 newbiegateclosedl2", "tutorial=130 walked-through");

    tut_door(srv, player, loc_door2, 3079, 3084, 0, 3080, 3084, "newbie_door2");
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

    /* "To do so, just click on the indicated range." */
    slot = tut_find_loc(3075, 3081, 0, loc_range, 4);
    SELFTEST_CHECK(slot >= 0, "the tutorial range should be placed at 3075,3081");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_range, slot);
    if( !tut_reached(player, varp, 170, "OPLOC1 newbierange") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_bread) == 1, "the range should bake the bread");
    tut_pass("bread", "OPLOC1 newbierange", "tutorial=170 bread");

    tut_door(srv, player, loc_door3, 3072, 3090, 0, 3073, 3090, "newbie_door3");
    if( !tut_reached(player, varp, 200, "OPLOC1 newbie_door3") )
        goto tut_done;
    tut_pass("chef-exit", "OPLOC1 newbie_door3", "tutorial=200 walked-through");

    /* ---- Running ---- */
    {
        int runbutton = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "orbs:runbutton");

        SELFTEST_CHECK(runbutton > 0 && varp_run > 0,
                       "orbs:runbutton and option_run should resolve (button=%d varp=%d)",
                       runbutton, varp_run);
        /* Twice, because the step is "turn running ON" and the orb is a
         * toggle: a character who already has it on has to click it off first.
         * The second click is a no-op when the first already turned it on. */
        for( i = 0; i < 2 && tut_step(player, varp) < 210; i++ )
        {
            tut_release(srv, player);
            ToriRSServer_ScriptsRunIfButton(srv, runbutton, 1);
            tut_drain(srv, player, 0);
        }
    }
    if( !tut_reached(player, varp, 210, "IF_BUTTON orbs:runbutton") )
        goto tut_done;
    tut_pass("run", "IF_BUTTON orbs:runbutton", "tutorial=210 run on");

    /* ---- The Quest Guide ---- */
    tut_door(srv, player, loc_door4, 3086, 3126, 0, 3086, 3127, "newbie_door4");
    if( !tut_reached(player, varp, 220, "OPLOC1 newbie_door4") )
        goto tut_done;
    tut_pass("quest-door", "OPLOC1 newbie_door4", "tutorial=220 walked-through");

    tut_snap(srv, player, 3085, 3122, 0);
    tut_talk(srv, player, npc_quest, 3085, 3122, 0, &spawned[4]);
    if( !tut_reached(player, varp, 230, "OPNPC1 Quest Guide") )
        goto tut_done;
    tut_stone(srv, player, 2);
    if( !tut_reached(player, varp, 240, "IF_BUTTON1 stone2 (quest list)") )
        goto tut_done;
    tut_talk(srv, player, npc_quest, 3085, 3122, 0, &spawned[4]);
    if( !tut_reached(player, varp, 250, "OPNPC1 Quest Guide (quest list open)") )
        goto tut_done;
    tut_pass("quest-guide", "OPNPC1 newbie_quest_instructor + stone2", "tutorial=250");

    /* ---- The mine ---- */
    tut_snap(srv, player, 3088, 3119, 0);
    slot = tut_find_loc(3088, 3119, 0, loc_ladder_down, 2);
    SELFTEST_CHECK(slot >= 0, "newbieladdertop1 should be placed at 3088,3119");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_ladder_down, slot);
    if( !tut_reached(player, varp, 260, "OPLOC1 newbieladdertop1") )
        goto tut_done;
    tut_pass("mine-ladder", "OPLOC1 newbieladdertop1", "tutorial=260");

    tut_snap(srv, player, 3080, 9505, 0);
    tut_talk(srv, player, npc_mining, 3080, 9504, 0, &spawned[5]);
    if( !tut_reached(player, varp, 270, "OPNPC1 Mining Instructor") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_pickaxe) >= 1, "Dezzick should hand over a pickaxe");
    tut_pass("dezzick", "OPNPC1 newbie_mining_instructor", "tutorial=270 bronze_pickaxe");

    slot = tut_find_loc(3073, 9505, 0, loc_tin, 4);
    SELFTEST_CHECK(slot >= 0, "a newbietinrock should be in the mine");
    if( slot >= 0 )
    {
        tut_snap(srv, player, 3074, 9505, 0);
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_tin, slot);
    }
    if( !tut_reached(player, varp, 280, "OPLOC1 newbietinrock") )
        goto tut_done;
    slot = tut_find_loc(3083, 9501, 0, loc_copper, 4);
    SELFTEST_CHECK(slot >= 0, "a newbiecopperrock should be in the mine");
    if( slot >= 0 )
    {
        tut_snap(srv, player, 3083, 9502, 0);
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_copper, slot);
    }
    if( !tut_reached(player, varp, 290, "OPLOC1 newbiecopperrock") )
        goto tut_done;
    tut_pass("mine", "OPLOC1 newbie rocks", "tutorial=290 both ores");

    /* "To do so, just click on the indicated furnace." */
    slot = tut_find_loc(3078, 9495, 0, loc_furnace, 4);
    SELFTEST_CHECK(slot >= 0, "newbiefurnace should be placed at 3078,9495");
    if( slot >= 0 )
    {
        tut_snap(srv, player, 3078, 9496, 0);
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_furnace, slot);
    }
    if( !tut_reached(player, varp, 300, "OPLOC1 newbiefurnace") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_bar) == 1, "the furnace should make a bronze bar");
    tut_pass("smelt", "OPLOC1 newbiefurnace", "tutorial=300 bronze bar");

    tut_talk(srv, player, npc_mining, 3080, 9504, 0, &spawned[5]);
    if( !tut_reached(player, varp, 310, "OPNPC1 Mining Instructor (smith)") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_hammer) >= 1, "Dezzick should hand over a hammer");
    tut_pass("hammer", "OPNPC1 newbie_mining_instructor", "tutorial=310 hammer");

    /* NUDGE: the anvil opens skill_smithing's make-menu (two pages, a
     * resume per page), which this fixture does not drive. The dagger it
     * makes is handed over: the Combat Instructor's steps wield it. */
    tut_set_step(player, varp, 350);
    {
        int free_slot = inv_first_free(player);

        if( free_slot >= 0 )
            inv_set(player, free_slot, obj_dagger, 1);
    }
    fprintf(stderr, "NUDGE tutorial dagger tutorial=310->350 (skill_smithing)\n");

    /* ---- The Combat Instructor ---- */
    tut_door(srv, player, loc_mine_gate, 3094, 9503, 0, 3093, 9503, "newbiedoor4l");
    if( !tut_reached(player, varp, 360, "OPLOC1 newbiedoor4l") )
        goto tut_done;
    tut_pass("mine-gate", "OPLOC1 newbiedoor4l", "tutorial=360 walked-through");

    tut_snap(srv, player, 3105, 9508, 0);
    tut_talk(srv, player, npc_combat, 3106, 9509, 0, &spawned[6]);
    if( !tut_reached(player, varp, 370, "OPNPC1 Combat Instructor") )
        goto tut_done;
    tut_stone(srv, player, 4);
    if( !tut_reached(player, varp, 400, "IF_BUTTON1 stone4 (worn equipment)") )
        goto tut_done;
    {
        int stats_button = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT,
                                                      "wornitems:equipment");

        SELFTEST_CHECK(stats_button > 0, "wornitems:equipment should resolve");
        tut_release(srv, player);
        if( stats_button > 0 )
            ToriRSServer_ScriptsRunIfButton(srv, stats_button, 1);
        tut_drain(srv, player, 0);
        tut_release(srv, player);
    }
    if( !tut_reached(player, varp, 405, "IF_BUTTON wornitems:equipment") )
        goto tut_done;
    tut_wield(srv, player, obj_dagger);
    if( !tut_reached(player, varp, 410, "OPHELD2 bronze_dagger") )
        goto tut_done;
    tut_pass("vannaka", "OPNPC1 + stone4 + equipment stats + OPHELD2", "tutorial=410");

    tut_talk(srv, player, npc_combat, 3106, 9509, 0, &spawned[6]);
    if( !tut_reached(player, varp, 420, "OPNPC1 Combat Instructor (sword)") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_sword) == 1 && selftest_count(player, obj_shield) == 1,
                   "Vannaka should hand over a sword and a shield");
    tut_wield(srv, player, obj_sword);
    tut_wield(srv, player, obj_shield);
    if( !tut_reached(player, varp, 430, "OPHELD2 bronze_sword + wooden_shield") )
        goto tut_done;
    tut_stone(srv, player, 0);
    if( !tut_reached(player, varp, 440, "IF_BUTTON1 stone0 (combat)") )
        goto tut_done;
    tut_pass("sword-shield", "OPHELD2 + stone0", "tutorial=440");

    tut_door(srv, player, loc_pit_gate, 3111, 9518, 0, 3112, 9518, "newbiedoor5_l");
    if( !tut_reached(player, varp, 450, "OPLOC1 newbiedoor5_l") )
        goto tut_done;
    tut_pass("rat-pit", "OPLOC1 newbiedoor5_l", "tutorial=450 walked-through");

    /* NUDGE: two rat kills, which is skill_combat plus npc_findhero. */
    tut_set_step(player, varp, 460);
    fprintf(stderr, "NUDGE tutorial melee kill tutorial=450->460 (skill_combat)\n");
    tut_snap(srv, player, 3105, 9508, 0);
    tut_talk(srv, player, npc_combat, 3106, 9509, 0, &spawned[6]);
    if( !tut_reached(player, varp, 470, "OPNPC1 Combat Instructor (ranged)") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_bow) == 1 && selftest_count(player, obj_arrows) == 50,
                   "Vannaka should hand over a shortbow and 50 bronze arrows");
    tut_pass("ranged-brief", "OPNPC1 newbie_combat_instructor", "tutorial=470 bow+arrows");
    tut_set_step(player, varp, 480);
    fprintf(stderr, "NUDGE tutorial ranged kill tutorial=470->480 (skill_combat)\n");

    tut_snap(srv, player, 3111, 9525, 0);
    slot = tut_find_loc(3111, 9526, 0, loc_ladder_out, 2);
    SELFTEST_CHECK(slot >= 0, "newbieladder2 should be placed at 3111,9526");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_ladder_out, slot);
    if( !tut_reached(player, varp, 500, "OPLOC1 newbieladder2") )
        goto tut_done;
    tut_pass("leave-mine", "OPLOC1 newbieladder2", "tutorial=500");

    /* ---- The bank, the poll booth, the Account Guide ---- */
    tut_snap(srv, player, 3120, 3123, 0);
    slot = tut_find_loc(3120, 3124, 0, loc_booth, 3);
    SELFTEST_CHECK(slot >= 0, "newbiebankbooth should be placed near 3120,3124");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_booth, slot);
    tut_release(srv, player);
    if( !tut_reached(player, varp, 510, "OPLOC1 newbiebankbooth") )
        goto tut_done;
    tut_pass("bank", "OPLOC1 newbiebankbooth", "tutorial=510");

    slot = tut_find_loc(3120, 3124, 0, loc_poll, 10);
    SELFTEST_CHECK(slot >= 0, "pollbooth_green should be placed outside the bank");
    if( slot >= 0 )
        tut_oploc(srv, player, SS_TRIGGER_OPLOC1, loc_poll, slot);
    if( !tut_reached(player, varp, 520, "OPLOC1 pollbooth_green") )
        goto tut_done;
    tut_pass("poll-booth", "OPLOC1 pollbooth_green", "tutorial=520");

    tut_door(srv, player, loc_door6, 3125, 3124, 0, 3124, 3124, "newbie_door6");
    if( !tut_reached(player, varp, 525, "OPLOC1 newbie_door6") )
        goto tut_done;
    tut_pass("account-door", "OPLOC1 newbie_door6", "tutorial=525 walked-through");

    tut_snap(srv, player, 3127, 3124, 0);
    tut_talk(srv, player, npc_account, 3127, 3124, 0, &spawned[7]);
    if( !tut_reached(player, varp, 530, "OPNPC1 Account Guide") )
        goto tut_done;
    tut_stone(srv, player, 8);
    if( !tut_reached(player, varp, 535, "IF_BUTTON1 stone8 (account)") )
        goto tut_done;
    tut_talk(srv, player, npc_account, 3127, 3124, 0, &spawned[7]);
    if( !tut_reached(player, varp, 540, "OPNPC1 Account Guide (account open)") )
        goto tut_done;
    tut_pass("account", "OPNPC1 newbie_account_instructor + stone8", "tutorial=540");

    tut_door(srv, player, loc_door7, 3130, 3124, 0, 3129, 3124, "newbie_door7");
    if( !tut_reached(player, varp, 550, "OPLOC1 newbie_door7") )
        goto tut_done;
    tut_pass("account-exit", "OPLOC1 newbie_door7", "tutorial=550 walked-through");

    /* ---- Brother Brace ---- */
    tut_snap(srv, player, 3124, 3107, 0);
    tut_talk(srv, player, npc_brace, 3124, 3106, 0, &spawned[8]);
    if( !tut_reached(player, varp, 560, "OPNPC1 Brother Brace") )
        goto tut_done;
    tut_stone(srv, player, 5);
    if( !tut_reached(player, varp, 570, "IF_BUTTON1 stone5 (prayer)") )
        goto tut_done;
    tut_talk(srv, player, npc_brace, 3124, 3106, 0, &spawned[8]);
    if( !tut_reached(player, varp, 600, "OPNPC1 Brother Brace (prayer open)") )
        goto tut_done;
    tut_pass("brace", "OPNPC1 brother_noob + stone5", "tutorial=600");

    tut_door(srv, player, loc_door8, 3122, 3102, 0, 3122, 3103, "newbie_door8");
    if( !tut_reached(player, varp, 610, "OPLOC1 newbie_door8") )
        goto tut_done;
    tut_pass("chapel-exit", "OPLOC1 newbie_door8", "tutorial=610 walked-through");

    /* ---- The Magic Instructor ---- */
    tut_snap(srv, player, 3140, 3089, 0);
    tut_talk(srv, player, npc_magic, 3140, 3090, 0, &spawned[9]);
    if( !tut_reached(player, varp, 620, "OPNPC1 Magic Instructor") )
        goto tut_done;
    tut_stone(srv, player, 6);
    if( !tut_reached(player, varp, 630, "IF_BUTTON1 stone6 (magic)") )
        goto tut_done;
    tut_talk(srv, player, npc_magic, 3140, 3090, 0, &spawned[9]);
    if( !tut_reached(player, varp, 640, "OPNPC1 Magic Instructor (runes)") )
        goto tut_done;
    SELFTEST_CHECK(selftest_count(player, obj_air) == 5 && selftest_count(player, obj_mind) == 5,
                   "Terrova should hand over five air and five mind runes");
    tut_pass("terrova", "OPNPC1 newbie_magic_instructor + stone6", "tutorial=640 runes");

    /* NUDGE: the Wind Strike on a chicken, which is skill_combat's spell path;
     * hit or splash, it completes the quest (^tut_quest_complete). */
    tut_set_step(player, varp, 671);
    fprintf(stderr, "NUDGE tutorial wind strike tutorial=640->671 (skill_combat)\n");

    /* ---- Off the island ---- */
    /* The way off is the Home Teleport, and `~tutorial_home_teleport` is
     * reached through skill_magic's spell. What this section owns is that the
     * quest's endstate is reachable at all. */
    fprintf(stderr, "NUDGE tutorial home teleport tutorial=671->1000 (skill_magic)\n");
    SELFTEST_CHECK(tut_step(player, varp) >= 671,
                   "Tutorial Island should be walkable to its last step");
    tut_pass("island", "the whole chain", "tutorial=671 reachable from 0");

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
