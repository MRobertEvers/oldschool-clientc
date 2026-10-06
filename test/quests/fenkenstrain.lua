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
-- gear the fixture does not hand out), food for that fight, and the two
-- ectotokens of the Port Phasmatys toll (the nearest furnace).
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
--   The furnace is Port Phasmatys's (fai_falador_furnace 3688,3478), behind
--     the west Energy Barrier 3652,3485: op4 Pay-toll(2-Ecto) going in, op1
--     Pass (free, ahoy_hub.rs2 [label,ahoy_barrier_pass]) coming out.

return {
    id = "fenkenstrain",
    fixture = "fresh_lumbridge.ini",
    max_frames = 360000, -- the Paterdomus route in, every castle door, stair and ladder walked, Port Phasmatys and the caves both ways
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
        "::give ectotoken 2", -- the Port Phasmatys barrier toll (ahoy_hub.rs2, ^ahoy_barrier_toll = 2), carried like coins
    },

    run = function(t)
        t.quest.bind({
            varp = "varp399_fenk_quest",
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
        t.exec("enterMorytania.talkToDrezel-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
        t.exec("enterMorytania.holyBarrier-msg", t.msg.expect, "You pass through the holy barrier")
        t.exec("goto-readSign", t.player.goto_tile, 3496, 3489, 0)

        -- ================= Panel: Starting off =================
        t.exec("readSign", t.player.click_loc, "fenk_signpost", 1)
        t.exec("readSign-dialog", t.chat.play, {
            "mesbox:The signpost has a note pinned",
        })
        t.expect("quest.stage.sign_read", t.quest.expect_stage("sign_read"))

        enter_castle("talkToFrenkenstrain")
        t.exec("talkToFrenkenstrain", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("talkToFrenkenstrain-dialog", t.chat.play, {
            "npc:Have you come to apply for the",
            "options",
            "choose:Yes, if it pays well.",
            "player:Yes, if it pays well.",
            "npc:I'll have to ask you some quest",
            "player:Okay...",
            "npc:How would you describe yoursel",
            "options",
            "choose:Braindead.",
            "player:Braindead.",
            "npc:Mmmm, I see.",
            "npc:Just one more question. What w",
            "options",
            "choose:Grave-digging.",
            "player:Grave-digging.",
            "npc:Mmmm, I see.",
            "npc:Looks like you're just the",
            "player:Is there anything you'd like",
            "npc:Yes, there is. You're highly sk",
            "player:Err...yes, that's what I said",
            "npc:Excellent. Now listen carefully",
            "player:Stuff?",
            "npc:That's what I said...stuff.",
            "player:What kind of stuff?",
            "npc:Well...dead stuff.",
            "player:Go on...",
            "npc:I need you to get me enough dea",
            "player:Right...okay...if you insist.",
        })
        t.expect("quest.stage.hired", t.quest.expect_stage("hired"))

        -- Pickled brain: only sellable from Roavar once hired
        -- (werewolfinnkeeper.rs2's beer-menu gate reads
        -- %creatureoffenkenstrain >= fenk_hired), so this leg runs after the
        -- interview even though Quest Helper lists it as its own earlier
        -- panel. The Canifis bar has open doorways (no door loc; a static
        -- flood from the castle front reaches the bar with doors closed).
        leave_castle("getPickledBrain")
        t.exec("goto-bar", t.player.goto_tile, 3493, 3471, 0)
        t.exec("getPickledBrain", t.player.talk_to, "werewolfinnkeeper", 1)
        t.exec("getPickledBrain-dialog", t.chat.play, {
            "player:Hello there!",
            "npc:Greetings traveller",
            "options",
            "choose:Do you sell pickled brains?",
            "player:Do you sell pickled brains?",
            "npc:Pickled brain, my friend",
            "options",
            "choose:I'll buy one, please.",
            "player:I'll buy one, please.",
            "npc:Pleasure doing business.",
        })
        t.exec("getPickledBrain.held", t.inv.await, "fenk_brain", 1, 10)

        -- ================= Panel: Graverobbing =================
        -- Up the EAST staircase (the guide's goUpstairsForStar, 3560,3552).
        enter_castle("goUpstairsForStar")
        doors("goUpstairsForStar", { "northIn", "midEastIn", "eastHallIn" })
        stairs("goUpstairsForStar", EAST_UP)

        door("getBook1", "eastRoomIn")
        t.exec("getBook1", t.player.click_loc, "fenk_bookcase", 1, { at = { 3555, 3558, 1 } })
        t.exec("getBook1-dialog", t.chat.play, {
            "options",
            "choose:Handy Maggot Avoidance Techniques.",
            "mesbox:As you pull the book a hidden",
        })
        t.exec("getBook1.held", t.inv.await, "fenk_obsidian_amulet", 1, 10)
        door("getBook1", "eastRoomOut")

        door("getBook2", "westRoomIn")
        t.exec("getBook2", t.player.click_loc, "fenk_bookcase", 1, { at = { 3542, 3558, 1 } })
        t.exec("getBook2-dialog", t.chat.play, {
            "options",
            "choose:The Joy of Grave Digging.",
            "mesbox:As you pull the book a hidden",
        })
        t.exec("getBook2.held", t.inv.await, "fenk_marble_amulet", 1, 10)

        t.exec("combineAmulet", t.player.use_item_on_item, "fenk_marble_amulet", "fenk_obsidian_amulet")
        t.exec("combineAmulet.held", t.inv.await, "fenk_star_amulet", 1, 10)
        t.check("combineAmulet.consumed", t.inv.expect_absent("fenk_marble_amulet"))

        door("goDownstairsForStar", "westRoomOut")
        stairs("goDownstairsForStar", WEST_DOWN)

        doors("talkToGardenerForHead", { "westHallOut", "midWestOut", "gardenIn" })
        t.exec("talkToGardenerForHead", t.player.talk_to, "fenk_gardener_multi_2", 1)
        t.exec("talkToGardenerForHead-dialog", t.chat.play, {
            "player:What happened to your head?",
            "npc:It got chopped off while I was",
            "npc:Dig at my grave southeast of the",
        })
        t.exec("talkToGardenerForHead.flag", t.var.await_server, "varb193_fenk_spoken_to_gardener", 1, 10)

        doors("goToHeadGrave", { "gardenOut", "northOut" })
        leave_castle("goToHeadGrave")
        t.exec("goToHeadGrave.goto", t.player.goto_tile, 3608, 3489, 0)
        -- Ed Lestwit's grave, ^fenk_ed_grave 0_56_54_24_35 (fenkenstrain.constant:34).
        t.exec("goToHeadGrave", t.player.click_loc, "fenk_grave_poor", 2, { at = { 3608, 3491, 0 } })
        -- The dig's mesbox opens after anim(human_dig,0); p_delay(2), and a
        -- click_loc that first has to step off the grave's own tile (trap 8
        -- of section 8) adds a further tick -- click_loc's settle can win
        -- the race on the weaker map_flag arm instead of waiting for the
        -- mesbox (trap 24). Poll for the page instead of guessing a tick
        -- count.
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "goToHeadGrave dig mesbox" }, 8)
        t.exec("goToHeadGrave-dialog", t.chat.play, {
            "mesbox:...and you unearth a decapitated",
        })
        t.exec("goToHeadGrave.held", t.inv.await, "fenk_head_empty", 1, 10)

        t.exec("combinedHead", t.player.use_item_on_item, "fenk_head_empty", "fenk_brain")
        t.exec("combinedHead.held", t.inv.await, "fenk_head_full", 1, 10)
        t.check("combinedHead.brainUsed", t.inv.expect_absent("fenk_brain"))

        t.exec("goto-coffin", t.player.goto_tile, 3578, 3525, 0)
        local coffin_target = t.player.by_symbol("loc", "fenk_coffin")
        t.exec("useStarOnGrave", t.player.use_on, "fenk_star_amulet", coffin_target, { at = { 3578, 3527, 0 } })
        t.exec("useStarOnGrave-dialog", t.chat.play, {
            "mesbox:The star amulet fits exactly",
        })
        t.exec("useStarOnGrave.flag", t.var.await_server, "varb192_fenk_coffin", 1, 10)
        t.check("useStarOnGrave.amuletUsed", t.inv.expect_absent("fenk_star_amulet"))

        t.exec("enterExperimentCave", t.player.click_loc, "fenk_coffin", 1, { at = { 3578, 3527, 0 } })
        await_tile(function(tt) return tt.z > 6400 end, 8, "enterExperimentCave")
        local cave_r, cave_tile = t.world.tile()
        t.check("enterExperimentCave.tile", cave_r == "ok" and cave_tile.x == 3577 and cave_tile.z == 9927 and cave_tile.level == 0,
            "tile " .. tile_text(cave_r, cave_tile) .. " (want ^fenk_experiment_cave 3577,9927,0: fenkenstrain_parts.rs2 [oploc1,fenk_coffin])")

        -- One cavern passage: the landing to the Experiment is open floor
        -- (reach.py REACH closed-doors len=44), so the hop is travel.
        t.exec("goto-experiment", t.player.goto_tile, 3554, 9948, 0)
        t.exec("killExperiment", t.player.attack, "fenk_experiment_1", 2, 25)
        -- Fight it to the end, however long the rolls take: every swing is
        -- on the player's own stream (seam28), and on it the level-51
        -- Experiment sat at a sliver of health past the old 40-tick budget.
        local _, kill_detail = t.exec("killExperiment.dead", t.npc.await_dead_engaged, 200, 6,
            { eat = { item = "lobster", below = 40 } })
        local kill_low = tonumber(tostring(kill_detail):match("lowest hp (%d+)/"))
        local food_r, food_left = t.inv.count("lobster")
        t.check("killExperiment.margin", kill_low ~= nil and kill_low >= 20 and food_r == "ok" and food_left >= 1,
            "Experiment (level 51): lowest hp " .. tostring(kill_low) .. "/80, lobsters staged 4, left "
                .. tostring(food_left) .. " (" .. tostring(food_r) .. ") (margin: lowest hp >= 20, a quarter of 80, AND food left)")

        local key_result, key_row = t.world.obj_near("fenk_mausoleum_key", 15)
        t.check("pickupKey.locate", key_result == "ok",
            "world.obj_near(fenk_mausoleum_key,15) -> " .. tostring(key_result) .. " "
                .. (type(key_row) == "table" and (tostring(key_row.tile_x) .. "," .. tostring(key_row.tile_z)) or tostring(key_row)))
        if key_result == "ok" then
            t.exec("goto-key", t.player.goto_tile, key_row.tile_x, key_row.tile_z, key_row.level)
        end
        -- click_obj is hollow on success (ok, nil detail) -- section 8.
        local pickup_result = t.player.click_obj("fenk_mausoleum_key", 3)
        t.check("pickupKey", pickup_result == "ok", "click_obj fenk_mausoleum_key -> " .. tostring(pickup_result))
        t.exec("pickupKey.held", t.inv.await, "fenk_mausoleum_key", 1, 10)

        -- The same passage west to the mausoleum door (REACH closed-doors len=72).
        t.exec("goto-mausoleumDoor", t.player.goto_tile, 3511, 9957, 0)
        key_door("openMausoleumDoor", "fenk_mausoleum_key", "fenk_mausoleum_door", "fenk_mausoleum_door_open",
            { 3510, 9957, 0 }, "varb199_fenk_unlocked_cavern")
        t.check("openMausoleumDoor.keyUsed", t.inv.expect_absent("fenk_mausoleum_key"))
        t.exec("leaveExperimentCave.mausoleumDoor", t.player.pass_door, { closed = "fenk_mausoleum_door",
            open = "fenk_mausoleum_door_open", at = { 3510, 9957, 0 }, near = { 3511, 9957 }, far = { 3509, 9957 } })
        -- Up the cave ladder into the walled graveyard (maplink 0_54_155_48_49 -> 0_54_55_48_49).
        climb("leaveExperimentCave", "ladder_from_cellar_directional", { 3504, 9970, 0 }, { 3504, 9969 }, { 3504, 3569, 0, 1 })

        t.exec("getTorso", t.player.click_loc, "fenk_grave", 2, { at = { 3502, 3576, 0 } })
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "getTorso dig mesbox" }, 8)
        t.exec("getTorso-dialog", t.chat.play, {
            "mesbox:...and you unearth a torso.",
        })
        t.exec("getTorso.held", t.inv.await, "fenk_torso", 1, 10)

        t.exec("getArm", t.player.click_loc, "fenk_grave", 2, { at = { 3504, 3577, 0 } })
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "getArm dig mesbox" }, 8)
        t.exec("getArm-dialog", t.chat.play, {
            "mesbox:...and you unearth a pair of arms.",
        })
        t.exec("getArm.held", t.inv.await, "fenk_arms", 1, 10)

        t.exec("getLeg", t.player.click_loc, "fenk_grave", 2, { at = { 3506, 3576, 0 } })
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "getLeg dig mesbox" }, 8)
        t.exec("getLeg-dialog", t.chat.play, {
            "mesbox:...and you unearth a pair of legs.",
        })
        t.exec("getLeg.held", t.inv.await, "fenk_legs", 1, 10)

        -- Back through the caves: the graveyard's own memorial drops into
        -- the cavern by the east ladder, which climbs out beside the coffin.
        t.exec("deliverBodyParts.pushMemorial", t.player.click_loc, "fenk_coffin", 1, { at = { 3505, 3571, 0 } })
        await_tile(function(tt) return tt.z > 6400 end, 8, "deliverBodyParts.pushMemorial")
        local mem_r, mem_tile = t.world.tile()
        t.check("deliverBodyParts.inCavern", mem_r == "ok" and mem_tile.x == 3577 and mem_tile.z == 9927 and mem_tile.level == 0,
            "after the push " .. tile_text(mem_r, mem_tile) .. " (want ^fenk_experiment_cave 3577,9927,0)")
        climb("deliverBodyParts.caveLadder", "ladder_from_cellar_directional", { 3578, 9927, 0 }, { 3577, 9927 }, { 3578, 3526, 0, 1 })
        enter_castle("deliverBodyParts")
        t.exec("deliverBodyParts", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("deliverBodyParts-dialog", t.chat.play, {
            "options",
            "choose:I have some body parts for you.",
            "player:I have some body parts for you",
            "npc:Excellent! Exactly what I needed",
            "npc:Now I need a needle and five s",
        })
        t.expect("quest.stage.parts", t.quest.expect_stage("parts"))

        -- ================= Panel: Getting tools =================
        t.exec("gatherNeedleAndThread", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("gatherNeedleAndThread-dialog", t.chat.play, {
            "npc:Where are my needle and thread",
            "npc:Ah, a needle. Wonderful.",
            "npc:Some thread. Excellent.",
            "mesbox:Fenkenstrain uses the needle a",
            "npc:Perfect. But I need one more t",
            "player:Really?",
            "npc:I have honed to perfection an a",
            "player:And what power is this?",
            "npc:The power of lightning.",
            "npc:The storm that brews overhead w",
            "npc:Repair the conductor and BEGONE",
        })
        t.expect("quest.stage.lightning", t.quest.expect_stage("lightning"))

        -- ================= Panel: Attracting lightning =================
        doors("talkToGardenerForKey", { "northIn", "gardenIn" })
        t.exec("talkToGardenerForKey", t.player.talk_to, "fenk_gardener_multi_2", 1)
        t.exec("talkToGardenerForKey-dialog", t.chat.play, {
            "options",
            "choose:Do you know where the key to the shed is?",
            "player:Do you know where the key to",
            "npc:Got it right 'ere in my pocket",
        })
        t.exec("talkToGardenerForKey.held", t.inv.await, "fenk_shed_key", 1, 10)

        walk("searchForBrush.atShedDoor", 3548, 3565, 0)
        key_door("openShedDoor", "fenk_shed_key", "fenk_shed_door", "fenk_shed_door_open", { 3548, 3565, 0 },
            "varb200_fenk_unlocked_shed")
        t.exec("searchForBrush.shedIn", t.player.pass_door, { closed = "fenk_shed_door", open = "fenk_shed_door_open",
            at = { 3548, 3565, 0 }, near = { 3548, 3565 }, far = { 3547, 3564 } })
        t.exec("searchForBrush.open", t.player.click_loc, "fenk_broomcupboard", 1)
        -- [oploc1,fenk_broomcupboard] loc_change(fenk_broomcupboard_open, 500):
        -- wait for the opened cupboard to reach the client before searching it.
        local cup_r = t.await({ level = function()
            local r, row = t.world.loc_near("fenk_broomcupboard_open", 4)
            return r == "ok" and row.tile_x == 3546 and row.tile_z == 3563
        end, note = "the opened cupboard at 3546,3563" }, 8)
        t.check("searchForBrush.cupboardOpen", cup_r == "ok",
            "fenk_broomcupboard_open at 3546,3563,0 after op1 Open -> " .. tostring(cup_r))
        t.exec("searchForBrush", t.player.click_loc, "fenk_broomcupboard_open", 2)
        t.exec("searchForBrush.held", t.inv.await, "fenk_brush0", 1, 10)
        t.exec("searchForBrush.shedOut", t.player.pass_door, { closed = "fenk_shed_door", open = "fenk_shed_door_open",
            at = { 3548, 3565, 0 }, near = { 3547, 3565 }, far = { 3549, 3565 } })

        t.exec("grabCanes-1", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes-2", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes-3", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes.held", t.inv.await, "fenk_cane", 3, 10)

        t.exec("extendBrush-1", t.player.use_item_on_item, "fenk_cane", "fenk_brush0")
        t.exec("extendBrush-2", t.player.use_item_on_item, "fenk_cane", "fenk_brush1")
        t.exec("extendBrush-3", t.player.use_item_on_item, "fenk_cane", "fenk_brush2")
        t.exec("extendBrush.held", t.inv.await, "fenk_brush3", 1, 10)
        t.check("extendBrush.canesUsed", t.inv.expect_absent("fenk_cane"))

        doors("goUpWestStairs", { "gardenOut", "midWestIn", "westHallIn" })
        stairs("goUpWestStairs", WEST_UP)
        door("searchFirePlace", "westRoomIn")
        local fireplace_target = t.player.by_symbol("loc", "fenk_fireplace")
        -- Only the west upstairs fireplace (1_55_55_24_35) holds the mould.
        t.exec("searchFirePlace", t.player.use_on, "fenk_brush3", fireplace_target, { at = { 3544, 3555, 1 } })
        t.exec("searchFirePlace-dialog", t.chat.play, {
            "mesbox:A lightning conductor mould falls",
        })
        t.exec("searchFirePlace.held", t.inv.await, "fenk_lightning_mould", 1, 10)

        -- Any furnace works (smelting.rs2's silver_bar case calls
        -- fenk_try_cast_conductor before the ordinary jewellery menu). The
        -- nearest is Port Phasmatys's, east across the swamp, behind the
        -- town's west Energy Barrier.
        door("makeLightningRod", "westRoomOut")
        stairs("makeLightningRod.downstairs", WEST_DOWN)
        west_stairs_to_hall("makeLightningRod")
        leave_castle("makeLightningRod")
        t.exec("makeLightningRod.gotoBarrier", t.player.goto_tile, 3651, 3485, 0)
        local tok_before_r, tok_before = t.inv.count("ectotoken")
        t.exec("makeLightningRod.enterPhasmatys", t.player.cross_gate, { loc = "ahoy_town_barrier_multi", op = 4,
            at = { 3652, 3485, 0 }, near = { 3651, 3485 }, far_ok = function(tile) return tile.x >= 3653 end,
            far_desc = "inside Port Phasmatys, x >= 3653" })
        local tok_after_r, tok_after = t.inv.count("ectotoken")
        t.check("makeLightningRod.toll", tok_before_r == "ok" and tok_after_r == "ok" and tok_before == 2 and tok_after == 0,
            "ectotoken " .. tostring(tok_before) .. " -> " .. tostring(tok_after) .. " (want 2 -> 0, ^ahoy_barrier_toll)")
        local furnace_target = t.player.by_symbol("loc", "fai_falador_furnace")
        t.exec("makeLightningRod", t.player.use_on, "silver_bar", furnace_target, { at = { 3688, 3478, 0 } })
        t.exec("makeLightningRod.held", t.inv.await, "fenk_conductor", 1, 10)
        t.check("makeLightningRod.barUsed", t.inv.expect_absent("silver_bar"))
        walk("makeLightningRod.backToBarrier", 3653, 3485, 0)
        t.exec("makeLightningRod.leavePhasmatys", t.player.cross_gate, { loc = "ahoy_town_barrier_multi", op = 1,
            at = { 3652, 3485, 0 }, near = { 3653, 3485 }, far_ok = function(tile) return tile.x <= 3651 end,
            far_desc = "outside Port Phasmatys, x <= 3651" })

        enter_castle("goUpWestStairsWithRod")
        hall_to_west_stairs("goUpWestStairsWithRod")
        stairs("goUpWestStairsWithRod", WEST_UP)
        door("goUpTowerLadder", "conductorIn")
        climb("goUpTowerLadder", "ladder", { 3548, 3539, 1 }, { 3549, 3539 }, { 3549, 3539, 2, 1 })
        t.exec("repairConductor", t.player.click_loc, "fenk_conductor_broken", 1)
        t.exec("repairConductor-dialog", t.chat.play, {
            "mesbox:You repair the lightning conductor",
        })
        t.expect("quest.stage.alive", t.quest.expect_stage("alive"))

        climb("goBackToFirstFloor.ladderDown", "laddertop", { 3548, 3539, 2 }, { 3549, 3539 }, { 3549, 3539, 1, 1 })
        door("goBackToFirstFloor", "conductorOut")
        stairs("goBackToFirstFloor", WEST_DOWN)
        west_stairs_to_hall("goBackToFirstFloor")
        t.exec("talkToFenkenstrainAfterFixingRod", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("talkToFenkenstrainAfterFixingRod-dialog", t.chat.play, {
            "player:So did it work, then?",
            "npc:Yes, I'm afraid it did",
            "npc:I tricked it into going up to",
            "npc:I have no control over it!",
            "npc:Destroy it!!!",
        })
        t.exec("talkToFenkenstrainAfterFixingRod.held", t.inv.await, "fenk_tower_key", 1, 10)
        t.expect("quest.stage.tower", t.quest.expect_stage("tower"))

        -- ================= Panel: Facing the monster =================
        hall_to_west_stairs("goToMonsterFloor1")
        stairs("goToMonsterFloor1", WEST_UP)
        walk("openLockedDoor.atDoor", 3548, 3551, 1)
        key_door("openLockedDoor", "fenk_tower_key", "fenk_tower_door", "fenk_tower_door_open", { 3548, 3551, 1 },
            "varb198_fenk_unlocked_tower")
        t.check("openLockedDoor.keyKept", t.inv.expect_has("fenk_tower_key", 1))
        t.exec("goToMonsterFloor2.towerDoor", t.player.pass_door, { closed = "fenk_tower_door", open = "fenk_tower_door_open",
            at = { 3548, 3551, 1 }, near = { 3548, 3551 }, far = { 3548, 3553 } })
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
            "npc:Oh that's not very nice",
            "player:You don't look very dangerous.",
            "npc:How do I look?",
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
            "player:Is there anything I can do for you",
            "npc:Only one - please stop Fenkenstrain",
        })
        t.expect("quest.stage.spoke_creature", t.quest.expect_stage("spoke_creature"))

        -- ================= Panel: Finishing off =================
        climb("pickPocketFenkenstrain.ladderDown", "laddertop", { 3548, 3554, 2 }, { 3548, 3553 }, { 3548, 3553, 1, 1 })
        t.exec("pickPocketFenkenstrain.towerDoor", t.player.pass_door, { closed = "fenk_tower_door", open = "fenk_tower_door_open",
            at = { 3548, 3551, 1 }, near = { 3548, 3552 }, far = { 3548, 3550 } })
        stairs("pickPocketFenkenstrain.downstairs", WEST_DOWN)
        west_stairs_to_hall("pickPocketFenkenstrain")
        local snap_result, snap = t.skill.snapshot()
        t.check("reward.snapshot", snap_result == "ok", "skill.snapshot -> " .. tostring(snap_result))

        t.exec("pickPocketFenkenstrain", t.player.talk_to, "fenk_fenkenstrain", 3)
        t.exec("pickPocketFenkenstrain.held", t.inv.await, "ring_of_charos", 1, 10)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))

        t.ticks(3)
        t.settle()
        t.quest.expect_complete()

        t.check("reward.thieving_xp", t.skill.expect_gain("thieving", 1000, snap))
        t.check("reward.ring_of_charos", t.inv.expect_has("ring_of_charos", 1))

        t.finish(0)
    end,
}
