-- Creature of Fenkenstrain -- full route, sign through completion.
-- Guide: Quest Helper CreatureOfFenkenstrain.java (5ea99d5e), 35 leaf steps
-- across 7 panels. Content: OSRS-Content quests/quest_fenkenstrain/scripts/
-- {fenkenstrain,fenkenstrain_parts,fenkenstrain_lightning,fenkenstrain_finish}.rs2.
--
-- Every action row that drives a guide leaf step is named EXACTLY after that
-- step's own Java variable (helper_coverage.py's driven() matches a PASS row
-- whose name equals or starts with the step's normalised name -- see
-- docs/QUEST_AUTHORING.md trap 32 and helper_coverage.py's driven()).
-- talkToFrenkenstrain keeps Quest Helper's own misspelling ("Frenkenstrain")
-- on purpose: that is the guide's literal step name.
--
-- Body parts, brain, amulets, shed key, canes and the conductor mould are all
-- obtained by real gathering (bought/dug/searched/crafted) -- none are
-- brought along by setup (trap 16). setup only carries the items Quest
-- Helper's own getItemRequirements() lists as brought along: ghostspeak
-- amulet, spade, needle, 5 thread, a silver bar, 3 bronze wire, coins, and a
-- weapon (armor is listed generically as "Armour and weapons defeat a level
-- 51 monster" -- a rune scimitar plus setlevel'd combat stats stand in for
-- gear the fixture does not hand out), and food for that fight. From Quest
-- Helper's getItemRecommended(): a Fenkenstrain's Castle Teleport tablet
-- (fenkenstrainTeleports) and a Falador teleport (teleportToFurnace's
-- alternate) -- the spell's runes plus Magic 48, which is also the level
-- teleport_tablet.rs2 [label,use_teleport_tablet] demands for the castle
-- tablet (magic_spell_teleport_fenkenstrain_castle levelrequired=48).
-- No ectotokens: neither the guide nor the wiki quick guide has you obtain
-- any, so the furnace is Falador's, not Port Phasmatys's behind its toll.
--
-- WALLS (door rule, owner 2026-10-03; re-driven in b60). Every goto departs
-- from and lands on open ground outside; every door, stair and ladder of the
-- castle is pressed on every visit, in and out. The castle, from the static
-- map (maps/m55_55.jl2, flooded with every door closed):
--   level 0: the front double door fenk_door 3548,3535 -> entry hall ->
--     fenk_door 3548,3543 -> the main hall (Fenkenstrain) -> fenk_door
--     3548,3551 -> the north room -> fenk_door 3549,3558 -> the garden (the
--     gardener, the cane pile; the shed behind fenk_shed_door 3548,3565).
--     North room -> poordoor 3545,3555 -> poordoor 3540,3555 -> the west
--     stair hall (fenk_stairs_lv1 3537,3551; maplink 3537,3550,0 ->
--     3537,3554,1). North room -> poordoor 3552,3555 -> poordoor 3557,3555
--     -> the east stair hall (fenk_stairs_lv1 3559,3551; 3559,3550,0 ->
--     3559,3554,1). Stair tops come down to 3537,3549,0 / 3559,3549,0
--     (ladders_stairs/configs/maplink.dbrow 13014-13126).
--   level 1: the corridor joining both stair tops; poordoor 3558,3554 -> the
--     east bookcase room; poordoor 3539,3554 -> the west room (bookcase,
--     fireplace); fenk_door 3549,3543,1 -> the ladder 3548,3539 up to the
--     conductor; fenk_tower_door 3548,3551,1 -> the ladder 3548,3554 up to
--     the creature.
--   The graveyard of the three lords (graves 3502-3506,3576-3577) is walled:
--     it is reached only by the cave ladder 3504,9970 (maplink -> 3504,3569)
--     and left by pushing its memorial fenk_coffin 3505,3571 (p_teleport
--     ^fenk_experiment_cave 3577,9927) and climbing the cave ladder 3578,9927
--     (-> 3578,3526): Quest Helper's "run back through the caves".
--   The cavern's fenk_mausoleum_door 3510,9957 opens with the key and is
--     walked through on foot.
--   The furnace is Falador's (fai_falador_furnace 2976,3368), reached by the
--     guide's own recommended Falador Teleport (Falador square 2965,3378,
--     then a walk: reach.py 2965,3378 -> 2974,3369 REACH closed-doors len=18,
--     no door) and left by the Fenkenstrain's Castle Teleport tablet
--     (tele_coord 0_55_55_30_10 = 3550,3530, outside the front door).

return {
    id = "fenkenstrain",
    fixture = "fresh_lumbridge.ini",
    max_frames = 360000, -- the Paterdomus route in, every castle door, stair and ladder walked, the caves both ways
    setup = {
        "::clearinv",
        -- The quest's own gate (fenkenstrain.rs2:6-22 [proc,fenk_has_requirements]):
        -- %varp107_prieststart >= ^priest_started (Restless Ghost started) and
        -- %varp302_priestperil >= ^fenk_pip_gate = 61 (fenkenstrain.constant:22),
        -- the value Drezel's farewell advice sets AFTER Priest in Peril's
        -- completion (60 -> 61, mausoleum_drezel.rs2:145-154). The two
        -- `::complete` lines stage the prerequisite quests (as mortton.lua and
        -- makinghistory.lua do); the run itself walks into Morytania and takes
        -- Drezel's advice, so 61 is reached by a real conversation. The
        -- `::fenkenstrain` debugproc used to stage all this AND p_teleport the
        -- player to the Canifis signpost (fenkenstrain.rs2:238-255) -- a
        -- placement with no on-foot route across the Salve -- so it is gone.
        -- A fresh_lumbridge account holds every %fenk_* flag at 0 already
        -- (quest.stage.not_started below grades that).
        "::complete quest_priestinperil",
        "::complete quest_restlessghost",
        "::give dagger_wolfbane 1", -- Priest in Peril's own reward (::complete grants no items); Drezel's advice branch needs it held (mausoleum_drezel.rs2:28-33)
        "::setlevel hitpoints 80",
        "::setlevel attack 80",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel crafting 40",  -- boostable 20 req (conductor casting)
        "::setlevel thieving 40",  -- boostable 25 req (final pickpocket)
        "::give rune_scimitar 1",
        "::give amulet_of_ghostspeak 1",
        "::give spade 1",
        "::give needle 1",
        "::give thread 5",
        "::give silver_bar 1",
        "::give bronzecraftwire 3",
        "::give coins 200",
        "::give lobster 4", -- food for the level-51 Experiment
        -- Quest Helper getItemRecommended(): teleportToFurnace (its alternate
        -- "a Falador teleport": [magic_spell_teleport_falador] level 37,
        -- waterrune 1 + airrune 3 + lawrune 1) and fenkenstrainTeleports
        -- (teletab_fenk; teleport_tablet.rs2:34 refuses it below Magic 48).
        -- The OSRS wiki's tablet page says the opposite: no Magic level is
        -- needed to break it, only Priest in Peril with Drezel's warning
        -- (staged above, reached for real). The spell alone needs 37; the 48
        -- is the content's gate, an open owner question on staged levels.
        -- Magic 48 leaves the combat level where the 80 melee stats put it
        -- (melee 0.325*160 = 52 over magic 0.325*72 = 23.4), so no dialogue
        -- the run passes takes another branch.
        "::setlevel magic 48",
        "::give waterrune 1",
        "::give airrune 3",
        "::give lawrune 1",
        "::give teletab_fenk 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb13715_creatureoffenkenstrain",
            constants = {
                not_started = 0, sign_read = 1, hired = 2, parts = 3,
                lightning = 4, alive = 5, tower = 6, spoke_creature = 7,
                complete = 9,
            },
            display = "Creature of Fenkenstrain",
            points = 2,
        })

        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end

        -- A walk inside a room or yard, graded on the tile it reached.
        local function walk(name, x, z, level, tol)
            tol = tol or 0
            local wr, wd = t.player.walk_to(x, z, 40)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and tt.level == level and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol,
                "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " " .. tostring(wd) .. "; at " .. tile_text(r, tt)
                    .. " (want " .. x .. "," .. z .. "," .. level .. (tol > 0 and (" within " .. tol) or "") .. ")")
        end

        -- A stair or ladder climb by the driver's verb (t.player.climb,
        -- verbs-pointer.md): the player walked to src on at's level before
        -- the press, on dest's level within dest[4] (slack) of its x,z after
        -- it. A cave ladder lands on the same level in the surface frame
        -- (z // 6400 differs), which the verb accepts as a climb. Only the
        -- climb moves the player between floors.
        local function climb(name, sym, at, src, dest)
            t.exec(name, t.player.climb, { loc = sym, op = 1, at = at, src = src,
                dest = { dest[1], dest[2], dest[3] }, slack = dest[4] or 0 })
        end

        -- A key used on its locked door. The quest's label sets the unlock
        -- bit and opens the leaf with ~door_open_active and no message, so
        -- the press's own settle can time out although it worked (run 1,
        -- openShedDoor): graded on the bit and the leaves on the door's own
        -- level, the press's answer only in the detail.
        local function key_door(name, key, closed, open, at, varbit)
            local target = t.player.by_symbol("loc", closed)
            local ur, ud = t.player.use_on(key, target, { at = at })
            local fr, fd = t.var.await_server(varbit, 1, 10)
            local opened = t.await({ level = function()
                local r, row = t.world.loc_near(open, 3)
                return r == "ok" and row.level == at[3] and math.abs(row.tile_x - at[1]) <= 1 and math.abs(row.tile_z - at[2]) <= 1
            end, note = name .. ": the open leaf" }, 6)
            local or_, orow = t.world.loc_near(open, 3)
            local cr, crow = t.world.loc_near(closed, 3)
            local still_closed = cr == "ok" and crow.level == at[3] and crow.tile_x == at[1] and crow.tile_z == at[2]
            local function where(r, row)
                return r == "ok" and (row.tile_x .. "," .. row.tile_z .. "," .. row.level) or tostring(r)
            end
            t.check(name, fr == "ok" and opened == "ok" and not still_closed,
                "use_on " .. key .. " on " .. closed .. " at " .. at[1] .. "," .. at[2] .. "," .. at[3] .. " -> " .. tostring(ur) .. " "
                    .. tostring(ud) .. "; " .. varbit .. " " .. tostring(fr) .. " " .. tostring(fd) .. "; " .. open .. " "
                    .. where(or_, orow) .. "; " .. closed .. " " .. where(cr, crow)
                    .. " (want the unlock bit 1, the open leaf within 1 of the door tile on its level, no closed leaf on it)")
        end

        -- ---------- the castle's doors (t.player.pass_door, one row each) ----------
        local function fenk(at, near, far)
            return { closed = "fenk_door", open = "fenk_door_open", at = at, near = near, far = far }
        end
        local function poor(at, near, far)
            return { closed = "poordoor", open = "poordooropen", at = at, near = near, far = far }
        end
        local DOOR = {
            frontIn = fenk({ 3548, 3535, 0 }, { 3548, 3534 }, { 3548, 3537 }),
            frontOut = fenk({ 3548, 3535, 0 }, { 3548, 3537 }, { 3548, 3531 }),
            hallIn = fenk({ 3548, 3543, 0 }, { 3548, 3542 }, { 3548, 3545 }),
            hallOut = fenk({ 3548, 3543, 0 }, { 3548, 3545 }, { 3548, 3541 }),
            northIn = fenk({ 3548, 3551, 0 }, { 3547, 3551 }, { 3548, 3553 }),
            northOut = fenk({ 3548, 3551, 0 }, { 3548, 3553 }, { 3547, 3551 }),
            gardenIn = fenk({ 3549, 3558, 0 }, { 3549, 3557 }, { 3549, 3560 }),
            gardenOut = fenk({ 3549, 3558, 0 }, { 3549, 3559 }, { 3549, 3555 }),
            -- north room <-> west stair hall, through the middle room
            midWestIn = poor({ 3545, 3555, 0 }, { 3545, 3555 }, { 3543, 3555 }),
            midWestOut = poor({ 3545, 3555, 0 }, { 3544, 3555 }, { 3546, 3555 }),
            westHallIn = poor({ 3540, 3555, 0 }, { 3540, 3555 }, { 3538, 3555 }),
            westHallOut = poor({ 3540, 3555, 0 }, { 3539, 3555 }, { 3541, 3555 }),
            -- north room -> east stair hall
            midEastIn = poor({ 3552, 3555, 0 }, { 3552, 3555 }, { 3554, 3555 }),
            eastHallIn = poor({ 3557, 3555, 0 }, { 3557, 3555 }, { 3559, 3555 }),
            -- level 1
            eastRoomIn = poor({ 3558, 3554, 1 }, { 3558, 3554 }, { 3556, 3554 }),
            eastRoomOut = poor({ 3558, 3554, 1 }, { 3557, 3554 }, { 3559, 3555 }),
            westRoomIn = poor({ 3539, 3554, 1 }, { 3539, 3554 }, { 3541, 3554 }),
            westRoomOut = poor({ 3539, 3554, 1 }, { 3540, 3554 }, { 3538, 3555 }),
            conductorIn = fenk({ 3549, 3543, 1 }, { 3549, 3544 }, { 3549, 3541 }),
            conductorOut = fenk({ 3549, 3543, 1 }, { 3549, 3542 }, { 3549, 3545 }),
        }
        local function door(prefix, key)
            return t.exec(prefix .. "." .. key, t.player.pass_door, DOOR[key])
        end
        local function doors(prefix, keys)
            for _, key in ipairs(keys) do
                door(prefix, key)
            end
        end

        local WEST_UP = { "fenk_stairs_lv1", { 3537, 3551, 0 }, { 3537, 3550 }, { 3537, 3554, 1 } }
        local WEST_DOWN = { "fenk_stairs_lv1_top", { 3537, 3552, 1 }, { 3537, 3554 }, { 3537, 3549, 0 } }
        local EAST_UP = { "fenk_stairs_lv1", { 3559, 3551, 0 }, { 3559, 3550 }, { 3559, 3554, 1 } }
        local function stairs(name, s)
            climb(name, s[1], s[2], s[3], s[4])
        end

        -- Castle front (open ground on the walkway's south end) <-> main hall.
        local function enter_castle(prefix)
            t.exec(prefix .. ".gotoCastleFront", t.player.goto_tile, 3548, 3528, 0)
            doors(prefix, { "frontIn", "hallIn" })
        end
        local function leave_castle(prefix)
            doors(prefix, { "hallOut", "frontOut" })
        end
        -- Main hall <-> the west stair hall (k), through the north room.
        local function hall_to_west_stairs(prefix)
            doors(prefix, { "northIn", "midWestIn", "westHallIn" })
        end
        local function west_stairs_to_hall(prefix)
            doors(prefix, { "westHallOut", "midWestOut", "northOut" })
        end

        t.ticklog.start()
        t.ticks(2)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("equip.ghostspeak", t.player.equip, "amulet_of_ghostspeak")
        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")

        -- ================= Into Morytania =================
        -- The fixture stands in Lumbridge (3206,3233); no walk crosses the
        -- Salve (reach 3206,3233 -> 3496,3489 UNREACHABLE at margin 600), and no
        -- spell this pack implements lands in Morytania (skill_magic/scripts/
        -- spells/teleport.rs2). So the way in is the one Priest in Peril opens,
        -- walked as mortton.lua / makinghistory.lua do (sample_tools/reach.py
        -- --root <worktree>, doors closed):
        --   * 3206,3233 -> 3318,3468, west of the Varrock members' gate
        --     (REACH closed-doors len=389 at margins 30/80).
        --   * fai_varrock_member_gatel 3319,3468 by pass_door, then
        --     3321,3468 -> 3405,3506 beside the Paterdomus trapdoor (REACH len=122).
        --   * The trapdoor 3405,3507 (open, climb down), the two mausoleum gates,
        --     Drezel's advice (60 -> 61: this is also the quest's own
        --     ^fenk_pip_gate requirement) and the holy barrier (p_telejump out
        --     at 3423,3485, mausoleum_interactions.rs2:26).
        --   * 3423,3485 -> 3496,3489 beside the Canifis signpost (3488,3485)
        --     (REACH closed-doors len=95 at margin 30).
        t.exec("goto-enterMorytania.varrockGate", t.player.goto_tile, 3318, 3468, 0)
        t.exec("enterMorytania.varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-enterMorytania.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        t.exec("enterMorytania.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.await({
            level = function()
                return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
            end,
            note = "enterMorytania: the trapdoor opens",
        }, 6)
        local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
        local tdc_r = t.world.loc_near("trapdoor", 3, { at = { 3405, 3507, 0 } })
        t.check("enterMorytania.trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
            "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
        t.exec("enterMorytania.descend", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906, 0 } })
        t.exec("enterMorytania.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
            near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
            far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
        t.exec("enterMorytania.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
            near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
            far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
        -- Priest in Peril's farewell advice (mausoleum_drezel.rs2:145-154,
        -- LostCity drezel.rs2:138-147): 60 -> 61, the holy barrier opens. The
        -- advice has no combat-level branch, so the staged combat stats
        -- (80 attack/strength/defence/hitpoints) change nothing here; no
        -- dialogue this run passes reads ~player_combat_level.
        t.exec("enterMorytania.talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        -- mausoleum_drezel.rs2 [label,drezel_access_holy_barrier] (RSProx
        -- index 4459 [2047..2085]): the blessing writes 61 with its box.
        t.exec("enterMorytania.talkToDrezel-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Into Morytania? Yes, you can.",
            "npc:I must warn you though",
            "npc:Worst of all are the vampyres",
            "player:I'll be careful.",
            "npc:The first settlement",
            "npc:You should keep that dagger",
            "player:I see. Thanks.",
            "npc:One last thing.",
            "player:The Myreque?",
            "npc:Yes. They're a band of freedom fighters.",
            "npc:Veliaf, the leader",
            "player:I'll keep an eye out for them.",
            "npc:Right, I think that's everything.",
            "mesbox:Drezel blesses you",
            "npc:Good luck out there.",
            "options",
            "choose:And you. See you later.",
            "player:And you. See you later.",
        })
        t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
        t.exec("enterMorytania.holyBarrier-msg", t.msg.expect, "You pass through the holy barrier")
        t.exec("goto-readSign", t.player.goto_tile, 3496, 3489, 0)

        -- ================= Panel: Starting off =================
        -- RSProx index 4606 is the flow from here on (ticks cited in
        -- docs/rsprox-analysis/quests/creature_of_fenkenstrain.md).
        t.ticklog.mark("leg.start")
        -- [595..604]: the confirm on the op, the note's box on "Yes.".
        t.exec("readSign", t.player.click_loc, "fenk_signpost", 1)
        t.exec("readSign-dialog", t.chat.play, {
            "options",
            "choose:Yes.",
            "mesbox:The signpost has a note pinned",
        })
        t.expect("quest.stage.sign_read", t.quest.expect_stage("sign_read"))
        t.exec("readSign.startLine", t.msg.expect, "You've started a new quest")

        -- [534..575]: the brain is Roavar's sale when the jar is taken.
        t.exec("goto-bar", t.player.goto_tile, 3493, 3474, 0)
        -- The take opens Roavar's pages instead of picking the jar up, so
        -- the press's own settle (a pickup) times out; the pages are the row.
        local brain_take = t.player.click_obj("fenk_brain", 3)
        t.note("getPickledBrain: click_obj fenk_brain 3 -> " .. tostring(brain_take))
        t.exec("getPickledBrain-dialog", t.chat.play, {
            "npc:You're interested in our speciality",
            "player:What exactly is in the jar?",
            "npc:Pickled brain, my friend.",
            "player:Err...pickled brain from what animal?",
            "npc:Animal? Don't be disgusting",
            "options",
            "choose:I'll buy one.",
            "player:I'll buy one, please.",
            "npc:A very wise choice",
        })
        t.exec("getPickledBrain.held", t.inv.await, "fenk_brain", 1, 10)

        t.ticklog.mark("leg.interview")
        enter_castle("talkToFrenkenstrain")
        t.exec("talkToFrenkenstrain", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("talkToFrenkenstrain-dialog", t.chat.play, {
            "npc:Have you come to apply for the",
            "options",
            "choose:Yes.",
            "player:Yes, if it pays well.",
            "npc:I'll have to ask you some quest",
            "player:Okay...",
            "npc:How would you describe yoursel",
            "options",
            "choose:Braindead",
            "npc:Mmmm, I see.",
            "npc:Just one more question. What w",
            "options",
            "choose:Grave-digging",
            "npc:Mmmm, I see.",
            "npc:Looks like you're just the",
            "player:Is there anything you'd like",
            "npc:Yes, there is. You're highly sk",
            "player:Err...yes, that's what I said",
            "npc:Excellent. Now listen carefully.",
            "npc:I need you to find some...stuff",
            "player:Stuff?",
            "npc:That's what I said...stuff.",
            "player:What kind of stuff?",
            "npc:Well...dead stuff.",
            "player:Go on...",
            "npc:I need you to get me enough dea",
            "player:Right...okay...if you insist.",
        })
        t.expect("quest.stage.hired", t.quest.expect_stage("hired"))

        -- ================= Panel: Graverobbing =================
        t.ticklog.mark("leg.bookcases")
        doors("goUpstairsForStar", { "northIn", "midEastIn", "eastHallIn" })
        stairs("goUpstairsForStar", EAST_UP)

        door("getBook1", "eastRoomIn")
        t.exec("getBook1", t.player.click_loc, "fenk_bookcase", 1, { at = { 3555, 3558, 1 } })
        t.exec("getBook1-dialog", t.chat.play, {
            "options",
            "choose:Handy Maggot Avoidance Techniques",
            "objbox:As you pull the book a hidden",
            "objbox:You find an obsidian amulet",
        })
        t.exec("getBook1.held", t.inv.await, "fenk_obsidian_amulet", 1, 10)
        door("getBook1", "eastRoomOut")

        door("getBook2", "westRoomIn")
        t.exec("getBook2", t.player.click_loc, "fenk_bookcase", 1, { at = { 3542, 3558, 1 } })
        t.exec("getBook2-dialog", t.chat.play, {
            "options",
            "choose:The Joy of Gravedigging",
            "mesbox:As you pull the book a hidden",
            "objbox:You find a marble amulet",
        })
        t.exec("getBook2.held", t.inv.await, "fenk_marble_amulet", 1, 10)

        t.exec("combineAmulet", t.player.use_item_on_item, "fenk_marble_amulet", "fenk_obsidian_amulet")
        t.exec("combineAmulet-dialog", t.chat.play, { "objbox:The marble and obsidian amulets" })
        t.exec("combineAmulet.held", t.inv.await, "fenk_star_amulet", 1, 10)
        t.check("combineAmulet.consumed", t.inv.expect_absent("fenk_marble_amulet"))

        door("goDownstairsForStar", "westRoomOut")
        stairs("goDownstairsForStar", WEST_DOWN)

        -- [2144..2161]: the head's tale; "10 minutes to spare" sends the
        -- guide (the ghostspeak box first, the amulet's first use).
        t.ticklog.mark("leg.gardener")
        doors("talkToGardenerForHead", { "westHallOut", "midWestOut", "gardenIn" })
        t.exec("talkToGardenerForHead", t.player.talk_to, "fenk_gardener_multi_2", 1)
        t.exec("talkToGardenerForHead-dialog", t.chat.play, {
            "options",
            "choose:What happened to your head?",
            "player:What happened to your head?",
            "objbox:You feel power emanate",
            "npc:Oi was in the old 'aunted Forest",
            "player:Oh yes, dreadful bad luck.",
            "npc:So oi thinks to meself",
            "player:Would you show me where the place was?",
            "npc:Well, oi s'pose oi've got 10 minutes",
        })
        t.exec("talkToGardenerForHead.flag", t.var.await_server, "varb193_fenk_spoken_to_gardener", 1, 10)
        t.exec("talkToGardenerForHead.guide", t.var.await_server, "varb185_fenk_gardener_directions", 1, 10)
        -- [2165]: the guide, talked to in the castle's square, says a line
        -- overhead and opens no page.
        t.ticklog.mark("guide.say")
        local _, gmarks = t.ticklog.rows({ kind = "mark" })
        local gsince = gmarks[#gmarks].serial
        t.player.talk_to("fenk_gardener_multi", 1)
        local gsay = nil
        local gwait = 0
        while gsay == nil and gwait < 10 do
            t.ticks(1)
            gwait = gwait + 1
            local _, says = t.ticklog.rows({ kind = "npc_say", since = gsince })
            gsay = says[1]
        end
        t.check("talkToGardenerForHead.guideSays", gsay ~= nil and gsay.text == "You need to head off to them Haunted Woods, mate."
            and t.chat.kind() == "none",
            "npc_say after the talk -> " .. tostring(gsay and gsay.text) .. "; chat " .. tostring(t.chat.kind())
                .. " (want the castle's line overhead and no page, RSProx 4606 [2165])")

        doors("goToHeadGrave", { "gardenOut", "northOut" })
        leave_castle("goToHeadGrave")
        t.exec("goToHeadGrave.goto", t.player.goto_tile, 3608, 3490, 0)
        -- [2297]: the spade's Dig beside Ed Lestwit's grave.
        t.exec("goToHeadGrave", t.player.inv_op, "spade", 1)
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "goToHeadGrave dig box" }, 10)
        t.exec("goToHeadGrave-dialog", t.chat.play, {
            "objbox:... and you unearth a decapitated",
        })
        t.exec("goToHeadGrave.held", t.inv.await, "fenk_head_empty", 1, 10)
        t.exec("goToHeadGrave.guideGone", t.var.await_server, "varb185_fenk_gardener_directions", 0, 10)

        t.exec("combinedHead", t.player.use_item_on_item, "fenk_head_empty", "fenk_brain")
        t.exec("combinedHead-dialog", t.chat.play, { "objbox:You squeeze the pickled brain" })
        t.exec("combinedHead.held", t.inv.await, "fenk_head_full", 1, 10)
        t.check("combinedHead.brainUsed", t.inv.expect_absent("fenk_brain"))

        t.ticklog.mark("leg.caves")
        t.exec("goto-coffin", t.player.goto_tile, 3578, 3525, 0)
        local coffin_target = t.player.by_symbol("loc", "fenk_coffin")
        t.exec("useStarOnGrave", t.player.use_on, "fenk_star_amulet", coffin_target, { at = { 3578, 3527, 0 } })
        t.exec("useStarOnGrave-dialog", t.chat.play, {
            "objbox:The star amulet fits exactly",
        })
        t.exec("useStarOnGrave.flag", t.var.await_server, "varb192_fenk_coffin", 1, 10)
        t.check("useStarOnGrave.amuletUsed", t.inv.expect_absent("fenk_star_amulet"))

        t.exec("enterExperimentCave", t.player.click_loc, "fenk_coffin", 1, { at = { 3578, 3527, 0 } })
        await_tile(function(tt) return tt.z > 6400 end, 8, "enterExperimentCave")
        local cave_r, cave_tile = t.world.tile()
        t.check("enterExperimentCave.tile", cave_r == "ok" and cave_tile.x == 3577 and cave_tile.z == 9927 and cave_tile.level == 0,
            "tile " .. tile_text(cave_r, cave_tile) .. " (want 3577,9927,0: RSProx 4606 [1663])")

        t.exec("goto-experiment", t.player.goto_tile, 3554, 9948, 0)
        t.exec("killExperiment", t.player.attack, "fenk_experiment_1", 2, 25)
        local _, kill_detail = t.exec("killExperiment.dead", t.npc.await_dead_engaged, 200, 6,
            { eat = { item = "lobster", below = 40 } })
        local kill_low = tonumber(tostring(kill_detail):match("lowest hp (%d+)/"))
        local food_r, food_left = t.inv.count("lobster")
        t.check("killExperiment.margin", kill_low ~= nil and kill_low >= 20 and food_r == "ok" and food_left >= 1,
            "Experiment (level 51): lowest hp " .. tostring(kill_low) .. "/80, lobsters staged 4, left "
                .. tostring(food_left) .. " (" .. tostring(food_r) .. ") (margin: lowest hp >= 20, a quarter of 80, AND food left)")
        t.exec("killExperiment.keyLine", t.msg.expect, "Untradeable drop: Cavern key")

        local key_result, key_row = t.world.obj_near("fenk_mausoleum_key", 15)
        t.check("pickupKey.locate", key_result == "ok",
            "world.obj_near(fenk_mausoleum_key,15) -> " .. tostring(key_result) .. " "
                .. (type(key_row) == "table" and (tostring(key_row.tile_x) .. "," .. tostring(key_row.tile_z)) or tostring(key_row)))
        if key_result == "ok" then
            t.exec("goto-key", t.player.goto_tile, key_row.tile_x, key_row.tile_z, key_row.level)
        end
        local pickup_result = t.player.click_obj("fenk_mausoleum_key", 3)
        t.check("pickupKey", pickup_result == "ok", "click_obj fenk_mausoleum_key -> " .. tostring(pickup_result))
        t.exec("pickupKey.held", t.inv.await, "fenk_mausoleum_key", 1, 10)

        -- [1824..1827]: the key on the door spends it, unlocks it and steps
        -- the player through (an inviswall and the open leaf for 2 ticks).
        t.exec("goto-mausoleumDoor", t.player.goto_tile, 3511, 9957, 0)
        local door_target = t.player.by_symbol("loc", "fenk_mausoleum_door")
        t.exec("openMausoleumDoor", t.player.use_on, "fenk_mausoleum_key", door_target, { at = { 3510, 9957, 0 } })
        t.exec("openMausoleumDoor.flag", t.var.await_server, "varb199_fenk_unlocked_cavern", 1, 10)
        await_tile(function(tt) return tt.x == 3510 and tt.z == 9957 end, 6, "openMausoleumDoor.through")
        t.check("openMausoleumDoor.keyUsed", t.inv.expect_absent("fenk_mausoleum_key"))
        climb("leaveExperimentCave", "ladder_from_cellar_directional", { 3504, 9970, 0 }, { 3504, 9969 }, { 3504, 3569, 0, 1 })

        -- [1896..1967]: each lord's grave dug with the spade beside it.
        t.ticklog.mark("leg.lords")
        local function dig_grave(name, stand, part, line)
            walk(name .. ".stand", stand[1], stand[2], 0)
            t.exec(name, t.player.inv_op, "spade", 1)
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = name .. " dig box" }, 10)
            t.exec(name .. "-dialog", t.chat.play, { "objbox:" .. line })
            t.exec(name .. ".held", t.inv.await, part, 1, 10)
        end
        dig_grave("getArm", { 3504, 3576 }, "fenk_arms", "... and you unearth a pair of arms.")
        dig_grave("getTorso", { 3503, 3576 }, "fenk_torso", "... and you unearth a torso.")
        dig_grave("getLeg", { 3505, 3576 }, "fenk_legs", "... and you unearth a pair of legs.")

        -- [1858..1863]: the island's memorial back down; east through the
        -- unlocked entrance ([2047]) to the ladder beside the first memorial.
        t.exec("deliverBodyParts.pushMemorial", t.player.click_loc, "fenk_coffin", 1, { at = { 3505, 3571, 0 } })
        await_tile(function(tt) return tt.z > 6400 end, 8, "deliverBodyParts.pushMemorial")
        local mem_r, mem_tile = t.world.tile()
        t.check("deliverBodyParts.inCavern", mem_r == "ok" and mem_tile.x == 3504 and mem_tile.z == 9969 and mem_tile.level == 0,
            "after the push " .. tile_text(mem_r, mem_tile) .. " (want 3504,9969,0: RSProx 4606 [1863])")
        t.exec("goto-mausoleumDoorWest", t.player.goto_tile, 3510, 9957, 0)
        t.exec("deliverBodyParts.entrance", t.player.click_loc, "fenk_mausoleum_door", 1, { at = { 3510, 9957, 0 } })
        await_tile(function(tt) return tt.x == 3511 and tt.z == 9957 end, 6, "deliverBodyParts.entrance")
        t.exec("goto-caveLadder", t.player.goto_tile, 3577, 9927, 0)
        climb("deliverBodyParts.caveLadder", "ladder_from_cellar_directional", { 3578, 9927, 0 }, { 3577, 9927 }, { 3577, 3527, 0, 1 })

        -- [2083..2123]: arms, legs and torso used on him, each its own line;
        -- [2531..2539]: the head by the menu, "Superb!!" with the stage.
        t.ticklog.mark("leg.handin")
        enter_castle("deliverBodyParts")
        local fenk_target = t.player.by_symbol("npc", "fenk_fenkenstrain")
        local function use_part(part, line)
            t.exec("deliverBodyParts." .. part, t.player.use_on, part, fenk_target)
            t.exec("deliverBodyParts." .. part .. "-dialog", t.chat.play, { "npc:" .. line })
        end
        use_part("fenk_arms", "Great, you've brought me some arms.")
        use_part("fenk_legs", "Excellent, you've brought me some legs.")
        use_part("fenk_torso", "Splendid, you've brought me a torso.")
        t.exec("deliverBodyParts", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("deliverBodyParts-dialog", t.chat.play, {
            "options",
            "choose:I have some body parts for you.",
            "player:I have some body parts for you.",
            "npc:Fantastic, you've brought me a head.",
            "npc:Superb!! Those are all the parts",
            "npc:Oh bother! I haven't got a needle",
            "npc:Go and get me a needle",
        })
        t.expect("quest.stage.parts", t.quest.expect_stage("parts"))

        -- ================= Panel: Getting tools =================
        t.exec("gatherNeedleAndThread", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("gatherNeedleAndThread-dialog", t.chat.play, {
            "npc:Where are my needle and thread",
            "npc:Ah, a needle. Wonderful.",
            "npc:Some thread, excellent.",
            "mesbox:Fenkenstrain uses the needle a",
            "npc:Perfect. But I need one more t",
            "player:Really?",
            "npc:I have honed to perfection an a",
            "player:And what power is this?",
            "npc:The power of lightning.",
            "player:Sorry, can't make lightning",
            "npc:Silence your insolent tongue!",
            "player:Repair the lightning conductor, right.",
            "npc:Repair the conductor and BEGONE!!!",
        })
        t.expect("quest.stage.lightning", t.quest.expect_stage("lightning"))

        -- ================= Panel: Attracting lightning =================
        t.ticklog.mark("leg.shed")
        doors("talkToGardenerForKey", { "northIn", "gardenIn" })
        t.exec("talkToGardenerForKey", t.player.talk_to, "fenk_gardener_multi_2", 1)
        t.exec("talkToGardenerForKey-dialog", t.chat.play, {
            "options",
            "choose:Do you know where the key to the shed is?",
            "player:Do you know where the key to",
            "npc:Got it right 'ere in my pocket",
            "objbox:The headless gardener hands you a rusty key.",
        })
        t.exec("talkToGardenerForKey.held", t.inv.await, "fenk_shed_key", 1, 10)

        -- [2962..2965]: Open with the key held spends it and steps the
        -- player into the shed.
        walk("searchForBrush.atShedDoor", 3548, 3565, 0)
        t.exec("openShedDoor", t.player.click_loc, "fenk_shed_door", 1, { at = { 3548, 3565, 0 } })
        t.exec("openShedDoor.flag", t.var.await_server, "varb200_fenk_unlocked_shed", 1, 10)
        await_tile(function(tt) return tt.x == 3547 and tt.z == 3565 end, 6, "openShedDoor.through")
        t.check("openShedDoor.keyUsed", t.inv.expect_absent("fenk_shed_key"))
        t.exec("searchForBrush.open", t.player.click_loc, "fenk_broomcupboard", 1)
        local cup_r = t.await({ level = function()
            local r, row = t.world.loc_near("fenk_broomcupboard_open", 4)
            return r == "ok" and row.tile_x == 3546 and row.tile_z == 3563
        end, note = "the opened cupboard at 3546,3563" }, 8)
        t.check("searchForBrush.cupboardOpen", cup_r == "ok",
            "fenk_broomcupboard_open at 3546,3563,0 after op1 Open -> " .. tostring(cup_r))
        t.exec("searchForBrush", t.player.click_loc, "fenk_broomcupboard_open", 2)
        t.exec("searchForBrush-dialog", t.chat.play, { "objbox:You find a garden brush in the cupboard." })
        t.exec("searchForBrush.held", t.inv.await, "fenk_brush0", 1, 10)
        walk("searchForBrush.inside", 3547, 3565, 0)
        t.exec("searchForBrush.shedOut", t.player.click_loc, "fenk_shed_door", 1, { at = { 3548, 3565, 0 } })
        await_tile(function(tt) return tt.x == 3548 and tt.z == 3565 end, 6, "searchForBrush.shedOut")

        t.exec("grabCanes-1", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes-2", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes-3", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes.held", t.inv.await, "fenk_cane", 3, 10)

        t.exec("extendBrush-1", t.player.use_item_on_item, "fenk_cane", "fenk_brush0")
        t.exec("extendBrush-2", t.player.use_item_on_item, "fenk_cane", "fenk_brush1")
        t.exec("extendBrush-3", t.player.use_item_on_item, "fenk_cane", "fenk_brush2")
        t.exec("extendBrush.held", t.inv.await, "fenk_brush3", 1, 10)
        t.check("extendBrush.canesUsed", t.inv.expect_absent("fenk_cane"))

        t.ticklog.mark("leg.chimney")
        doors("goUpWestStairs", { "gardenOut", "midWestIn", "westHallIn" })
        stairs("goUpWestStairs", WEST_UP)
        door("searchFirePlace", "westRoomIn")
        local fireplace_target = t.player.by_symbol("loc", "fenk_fireplace")
        -- [3103..3108]: the west first-floor chimney holds the mould.
        t.exec("searchFirePlace", t.player.use_on, "fenk_brush3", fireplace_target, { at = { 3544, 3555, 1 } })
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "searchFirePlace box" }, 10)
        t.exec("searchFirePlace-dialog", t.chat.play, {
            "objbox:A lightning conductor mould falls",
        })
        t.exec("searchFirePlace.held", t.inv.await, "fenk_lightning_mould", 1, 10)

        -- [3170..3175]: the silver bar on the furnace opens the silver
        -- crafting panel; its lightning rod cell casts the conductor.
        t.ticklog.mark("leg.furnace")
        t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "makeLightningRod.faladorTeleport",
            runes = { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } },
            where = "Falador square, tele_coord 0_46_52_21_50" })
        t.exec("makeLightningRod.toFurnace", t.player.walk_to, 2974, 3369, 40)
        local furnace_target = t.player.by_symbol("loc", "fai_falador_furnace")
        t.exec("makeLightningRod.use", t.player.use_on, "silver_bar", furnace_target, { at = { 2976, 3368, 0 } })
        t.exec("makeLightningRod.panel", t.ui.await_open, "silver_crafting", 10)
        local rod_wr, rod_cell = t.ui.widget("silver_crafting:lightning_rod")
        local rod_ir = t.ui.invoke(rod_cell, 1)
        t.check("makeLightningRod", rod_wr == "ok" and rod_ir == "ok", "invoke silver_crafting:lightning_rod -> " .. tostring(rod_wr) .. " " .. tostring(rod_ir))
        t.exec("makeLightningRod.held", t.inv.await, "fenk_conductor", 1, 10)
        t.check("makeLightningRod.barUsed", t.inv.expect_absent("silver_bar"))

        local tab_before_r, tab_before = t.inv.count("teletab_fenk")
        local tab_r, tab_d = t.player.inv_op("teletab_fenk", 1)
        await_tile(function(tt) return tt.level == 0 and math.abs(tt.x - 3550) <= 2 and math.abs(tt.z - 3530) <= 2 end,
            12, "goUpWestStairsWithRod.castleTeleport")
        local land_r, land = t.world.tile()
        local tab_after_r, tab_after = t.inv.count("teletab_fenk")
        t.check("goUpWestStairsWithRod.castleTeleport",
            tab_r ~= "refused" and land_r == "ok" and land.level == 0 and math.abs(land.x - 3550) <= 2
                and math.abs(land.z - 3530) <= 2 and tab_before_r == "ok" and tab_after_r == "ok"
                and tab_before == 1 and tab_after == 0,
            "inv_op(teletab_fenk,1) -> " .. tostring(tab_r) .. " " .. tostring(tab_d) .. "; landed " .. tile_text(land_r, land)
                .. "; teletab_fenk " .. tostring(tab_before) .. " -> " .. tostring(tab_after)
                .. " (want within 2 of 3550,3530,0, tele_coord 0_55_55_30_10, and the tablet 1 -> 0)")

        t.ticklog.mark("leg.roof")
        walk("goUpWestStairsWithRod.toCastleFront", 3548, 3528, 0)
        doors("goUpWestStairsWithRod", { "frontIn", "hallIn" })
        hall_to_west_stairs("goUpWestStairsWithRod")
        stairs("goUpWestStairsWithRod", WEST_UP)
        -- [3308..3314]: the conductor's double door, walked through.
        t.exec("goUpTowerLadder.toDoor", t.player.goto_tile, 3548, 3544, 1)
        t.exec("goUpTowerLadder.door", t.player.click_loc, "fenk_door", 1, { at = { 3549, 3543, 1 } })
        await_tile(function(tt) return tt.level == 1 and tt.z == 3542 end, 8, "goUpTowerLadder.door")
        climb("goUpTowerLadder", "ladder", { 3548, 3539, 1 }, { 3548, 3540 }, { 3548, 3540, 2, 1 })
        local conductor_target = t.player.by_symbol("loc", "fenk_conductor_broken")
        t.exec("repairConductor", t.player.use_on, "fenk_conductor", conductor_target, { at = { 3548, 3536, 2 } })
        t.exec("repairConductor-dialog", t.chat.play, {
            "mesbox:You repair the lightning conductor",
        })
        t.expect("quest.stage.alive", t.quest.expect_stage("alive"))

        climb("goBackToFirstFloor.ladderDown", "laddertop", { 3548, 3539, 2 }, { 3548, 3538 }, { 3548, 3538, 1, 1 })
        t.exec("goBackToFirstFloor.toDoor", t.player.goto_tile, 3548, 3542, 1)
        t.exec("goBackToFirstFloor.door", t.player.click_loc, "fenk_door_mirror", 1, { at = { 3548, 3543, 1 } })
        await_tile(function(tt) return tt.level == 1 and tt.z == 3543 end, 8, "goBackToFirstFloor.door")
        stairs("goBackToFirstFloor", WEST_DOWN)
        west_stairs_to_hall("goBackToFirstFloor")
        t.ticklog.mark("leg.alive")
        t.exec("talkToFenkenstrainAfterFixingRod", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("talkToFenkenstrainAfterFixingRod-dialog", t.chat.play, {
            "player:So did it work, then?",
            "npc:Yes, I'm afraid it did",
            "player:I can't see it anywhere.",
            "npc:I tricked it into going up to",
            "player:So the creature wasn't all",
            "npc:...oh, what have I done...",
            "player:Oh, I see, we're developing",
            "player:Bit late for that, I'd say.",
            "npc:I have no control over it!",
            "player:What do you want me to do about it?",
            "npc:Destroy it!!!",
        })
        t.exec("talkToFenkenstrainAfterFixingRod.held", t.inv.await, "fenk_tower_key", 1, 10)
        t.expect("quest.stage.tower", t.quest.expect_stage("tower"))

        -- ================= Panel: Facing the monster =================
        t.ticklog.mark("leg.tower")
        hall_to_west_stairs("goToMonsterFloor1")
        stairs("goToMonsterFloor1", WEST_UP)
        walk("openLockedDoor.atDoor", 3548, 3551, 1)
        -- [3469..3470]: Open with the key held spends it, unlocks the door
        -- and steps the player into the Tower.
        t.exec("openLockedDoor", t.player.click_loc, "fenk_tower_door", 1, { at = { 3548, 3551, 1 } })
        t.exec("openLockedDoor.flag", t.var.await_server, "varb198_fenk_unlocked_tower", 1, 10)
        await_tile(function(tt) return tt.level == 1 and tt.x == 3548 and tt.z == 3552 end, 6, "openLockedDoor.through")
        t.check("openLockedDoor.keyUsed", t.inv.expect_absent("fenk_tower_key"))
        climb("goToMonsterFloor2", "ladder", { 3548, 3554, 1 }, { 3548, 3553 }, { 3548, 3553, 2, 1 })

        local present_result, present_creature = t.npc.await_present("fenk_creature", 6, 5)
        t.check("talkToMonster.present", present_result == "ok",
            "await_present fenk_creature -> " .. tostring(present_result) .. " "
                .. tostring(type(present_creature) == "table"
                    and (tostring(present_creature.tile_x) .. "," .. tostring(present_creature.tile_z))
                    or present_creature))
        t.exec("talkToMonster", t.player.talk_to, "fenk_creature", 1)
        t.exec("talkToMonster-dialog", t.chat.play, {
            "player:I am commanded to destroy you, creature!",
            "npc:Oh that's *hic* not very *hic* nice",
            "player:Are you feeling okay?",
            "npc:Abso *hic* lutely.",
            "player:You don't look very dangerous.",
            "npc:How *hic* do I look?",
            "player:You really don't know",
            "mesbox:The creature stumbles over towards the mirror",
            "npc:AAAAARRGGGGHHHH!",
            "mesbox:The creature becomes instantly sober",
            "player:I'm sorry.",
            "npc:No - it was him I wager",
            "player:Who are - were - you?",
            "npc:I was Rologarth",
            "player:So the castle wasn't really abandoned",
            "npc:Is that what he told you?",
            "player:I found your brain in a jar",
            "npc:Of that I will not speak.",
            "player:That's it - I'm leaving",
            "npc:Only one - please stop Fenkenstrain",
        })
        t.expect("quest.stage.spoke_creature", t.quest.expect_stage("spoke_creature"))

        -- ================= Panel: Finishing off =================
        t.ticklog.mark("leg.finish")
        climb("pickPocketFenkenstrain.ladderDown", "laddertop", { 3548, 3554, 2 }, { 3548, 3555 }, { 3548, 3555, 1, 1 })
        walk("pickPocketFenkenstrain.toTowerDoor", 3548, 3552, 1)
        t.exec("pickPocketFenkenstrain.towerDoor", t.player.click_loc, "fenk_tower_door", 1, { at = { 3548, 3551, 1 } })
        await_tile(function(tt) return tt.level == 1 and tt.x == 3548 and tt.z == 3551 end, 6, "pickPocketFenkenstrain.towerDoor")
        stairs("pickPocketFenkenstrain.downstairs", WEST_DOWN)
        west_stairs_to_hall("pickPocketFenkenstrain")
        t.exec("pickPocketFenkenstrain.refusal", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("pickPocketFenkenstrain.refusal-dialog", t.chat.play, {
            "npc:So have you destroyed it?!!?",
            "player:Never, now that he has told me the truth!",
            "npc:Oh my, oh my",
            "npc:Why did you have to pick Rologarth's brain",
            "player:I'm through working for you.",
            "npc:No!! I refuse to release you!!",
        })
        local snap_result, snap = t.skill.snapshot()
        t.check("reward.snapshot", snap_result == "ok", "skill.snapshot -> " .. tostring(snap_result))

        -- [3575]: the steal is the completion, on the op.
        t.exec("pickPocketFenkenstrain", t.player.talk_to, "fenk_fenkenstrain", 3)
        t.exec("pickPocketFenkenstrain.held", t.inv.await, "ring_of_charos", 1, 10)
        t.exec("pickPocketFenkenstrain.line", t.msg.expect, "You steal the Ring of Charos from Fenkenstrain.")
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.exec("pickPocketFenkenstrain.switch", t.var.await_server, "varp399_fenk_quest", 8, 10)

        t.ticks(3)
        t.settle()
        t.quest.expect_complete()

        t.check("reward.thieving_xp", t.skill.expect_gain("thieving", 1000, snap))
        t.check("reward.ring_of_charos", t.inv.expect_has("ring_of_charos", 1))

        t.finish(0)
    end,
}
