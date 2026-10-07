-- Royal Trouble (Quest Helper helpers/quests/royaltrouble/RoyalTrouble.java), authored as an
-- 8-leg relay (docs/quest_authoring/relay.md). Notes for the legs: docs/quests/ladders/royaltrouble.notes.md
--
-- RE-DRIVE (matthew-mbp-m4-b72, door rule): no goto into or out of a closed space, and no goto with
-- no on-foot route. The setup's ::royaltrouble (royal_debug.rs2:35) stages the quest's state and
-- teleports to the island, so the setup's last cheat stands the player back on the fixture's
-- Lumbridge tile; the voyage is the guide's travelToMisc: Camelot Teleport from Lumbridge (the
-- Taverley members' gate is the only way north on foot), overland Camelot -> Rellekka's docks, and
-- the Rellekka sailor's longship (quest_viking/scripts/viking_sailor.rs2 [opnpc1,viking_sailor],
-- which needs The Fremennik Trials; Throne of Miscellania, this quest's requirement, needs it too).
-- Every room is walked into and out of on every visit, each door pressed by tile and level (the
-- routes are misc.lua's, b59, proven there):
--  * Miscellania castle (maps/m39_60.jl2): east door castledoor 2510,3860, hall door 2505,3860,
--    south stair room castledoor 2506,3851 (levels 0 and 1), spiralstairs_wooden 2505,3848
--    (maplink 0_39_60_9_10 up -> 2504,3849,1; down 1_39_60_8_9 -> 2505,3850,0), the throne room's
--    walk-through misc_ulby_throneroomdoor 2506,3857,1; north stair room castledoor 2506,3869
--    (levels 0 and 1), spiralstairs_wooden 2505,3871 (up -> 2507,3871,1; down 1_39_60_11_31 ->
--    2506,3870,0), Princess Astrid's castledoor 2504,3867,1.
--  * Etceteria castle (maps/m40_60.jl2): front door castledoor 2608,3875, stair room castledoor
--    2611,3866, spiralstairs 2613,3867 (up -> 2615,3867,1), spiralstairstop 2614,3867,1 (down ->
--    2614,3866,0), Queen Sigrid's castledoor 2615,3870,1.
--  * Matilda's house (m40_60.spawn:26, 2603,3871): misc_viking_abode_door_low 2603,3874 (the door
--    tile is on the street side; the house is x 2601-2605 z 3870-3873).
--  * Donal's pub in the dungeon village: royal_village_door 2525,10256 (outside tile; pub x 2526-2530).
-- Miscellania and Etceteria are one walkable landmass on level 0 (reach.py closed-doors), so the
-- hops between the castles' front doors, the dock, the gardener and the dungeon ladder are overland.
-- No dialogue on the route branches on combat level or Magic (grep of quest_royaltrouble,
-- quest_misc, area_miscellania): the only stat reads are hitpoints in royal_cave.rs2:75/92/98 (a death
-- guard and hazard damage), and nothing branches on a staged stat.

local CD, CDO = "castledoor", "opencastledoor"

local function inside(tt, level, x0, x1, z0, z1)
    return type(tt) == "table" and tt.level == level and tt.x >= x0 and tt.x <= x1 and tt.z >= z0 and tt.z <= z1
end

-- The routes, one graded row per crossing (t.player.pass_door / climb / cross_gate).
local function route(t)
    local r = {}
    -- A swinging door, closed behind the player unless keep_open (pass_door close = true).
    function r.door(name, closed, open, x, z, level, nx, nz, fx, fz, keep_open)
        t.exec(name, t.player.pass_door, {
            closed = closed, open = open, at = { x, z, level },
            near = { nx, nz }, far = { fx, fz }, close = not keep_open, ticks = 30,
        })
    end
    function r.climb(name, loc, op, ax, az, al, sx, sz, dx, dz, dl)
        t.exec(name, t.player.climb, {
            loc = loc, op = op, at = { ax, az, al }, src = { sx, sz }, dest = { dx, dz, dl },
        })
        t.ticks(2)
    end
    -- Miscellania castle, level 0: outside the east door <-> the main hall.
    function r.castle_in(p)
        r.door(p .. ".eastDoor", CD, CDO, 2510, 3860, 0, 2511, 3860, 2509, 3860)
        r.door(p .. ".hallDoor", CD, CDO, 2505, 3860, 0, 2506, 3860, 2504, 3860)
    end
    function r.castle_out(p)
        r.door(p .. ".hallDoorOut", CD, CDO, 2505, 3860, 0, 2504, 3860, 2506, 3860)
        r.door(p .. ".eastDoorOut", CD, CDO, 2510, 3860, 0, 2510, 3860, 2512, 3860)
    end
    -- Main hall <-> the south stair room (level 0).
    function r.south_room_in(p)
        r.door(p .. ".stairRoomDoor", CD, CDO, 2506, 3851, 0, 2506, 3852, 2506, 3850)
    end
    function r.south_room_out(p)
        r.door(p .. ".stairRoomDoorOut", CD, CDO, 2506, 3851, 0, 2506, 3851, 2506, 3853)
    end
    -- The south staircase, the guide's spiralstairs_wooden 2506,3849 (op1 up, op3 down).
    function r.south_up(name)
        r.climb(name, "spiralstairs_wooden", 1, 2505, 3848, 0, 2505, 3850, 2504, 3849, 1)
    end
    function r.south_down(name)
        r.climb(name, "spiralstairsmiddle_wooden", 3, 2505, 3848, 1, 2504, 3849, 2505, 3850, 0)
    end
    -- South stair landing (level 1) <-> the south corridor, then the throne room's walk-through door.
    function r.landing_in(p)
        r.door(p .. ".landingDoor", CD, CDO, 2506, 3851, 1, 2506, 3851, 2506, 3853)
    end
    function r.landing_out(p)
        r.door(p .. ".landingDoorOut", CD, CDO, 2506, 3851, 1, 2506, 3852, 2506, 3850)
    end
    function r.throne_in(p)
        t.exec(p .. ".throneDoorIn", t.player.cross_gate, {
            loc = "misc_ulby_throneroomdoor", at = { 2506, 3857, 1 }, near = { 2506, 3856 },
            far_ok = function(tt) return inside(tt, 1, 2498, 2510, 3857, 3863) end,
            far_desc = "the throne room, z 3857-3863 level 1",
        })
    end
    function r.throne_out(p)
        t.exec(p .. ".throneDoorOut", t.player.cross_gate, {
            loc = "misc_ulby_throneroomdoor", at = { 2506, 3857, 1 }, near = { 2505, 3857 },
            far_ok = function(tt) return inside(tt, 1, 2505, 2507, 3852, 3856) end,
            far_desc = "the south corridor, z 3852-3856 level 1",
        })
    end
    -- Main hall -> throne room by the south stairs (the guide's goUpTo* step is the climb), and back.
    function r.to_throne_from_hall(p, climb_name)
        r.south_room_in(p)
        r.south_up(climb_name)
        r.landing_in(p)
        r.throne_in(p)
    end
    function r.throne_to_hall(p, climb_name)
        r.throne_out(p)
        r.landing_out(p)
        r.south_down(climb_name)
        r.south_room_out(p)
    end
    -- Etceteria castle: outside its front door <-> the stair room (level 0).
    function r.etc_in(p)
        r.door(p .. ".etcFrontDoor", CD, CDO, 2608, 3875, 0, 2607, 3875, 2609, 3874)
        r.door(p .. ".etcStairRoomDoor", CD, CDO, 2611, 3866, 0, 2611, 3866, 2613, 3866)
    end
    function r.etc_out(p)
        r.door(p .. ".etcStairRoomDoorOut", CD, CDO, 2611, 3866, 0, 2612, 3866, 2610, 3866)
        r.door(p .. ".etcFrontDoorOut", CD, CDO, 2608, 3875, 0, 2608, 3875, 2606, 3875)
    end
    function r.etc_up(name)
        r.climb(name, "spiralstairs", 1, 2613, 3867, 0, 2615, 3868, 2615, 3867, 1)
    end
    function r.etc_down(name)
        r.climb(name, "spiralstairstop", 1, 2614, 3867, 1, 2615, 3867, 2614, 3866, 0)
    end
    function r.sigrid_in(p)
        r.door(p .. ".sigridDoor", CD, CDO, 2615, 3870, 1, 2615, 3870, 2615, 3872)
    end
    function r.sigrid_out(p)
        r.door(p .. ".sigridDoorOut", CD, CDO, 2615, 3870, 1, 2615, 3871, 2615, 3869)
    end
    -- Overland hops between open level-0 tiles of the island.
    function r.to_castle(name)
        t.exec(name, t.player.goto_tile, 2512, 3860, 0)
    end
    function r.to_etceteria(name)
        t.exec(name, t.player.goto_tile, 2606, 3875, 0)
    end
    function r.level_is(name, want)
        local lv = select(2, t.world.level())
        t.check(name, lv == want, "level " .. tostring(lv) .. " (want " .. tostring(want) .. ")")
    end
    function r.stage_is(name, var, want)
        local v = select(2, t.var.server(var))
        t.check(name, v == want, var .. " = " .. tostring(v) .. " (want " .. tostring(want) .. ")")
    end
    function r.tile_str()
        local _, tl = t.world.tile()
        return tostring(tl and (tl.x .. "," .. tl.z)) .. " level " .. tostring(select(2, t.world.level()))
    end
    return r
end

return {
    id = "royaltrouble",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- every castle visit is walked, door by door, both castles
    setup = {
        "::clearinv",
        "::setlevel agility 40", -- guide requirement: 40 Agility (royal_royals.rs2:79 ~royaltrouble_meets_requirements)
        "::setlevel slayer 40",  -- guide requirement: 40 Slayer (same check; the snake needs it to attack)
        "::complete quest_heroes", -- guide requirement chain (Throne of Miscellania needs Heroes' Quest): misc_door_guard.rs2:10 gates the throne-room door on %varp188_heroquest = ^hero_complete
        "::complete quest_fremenniktrials", -- guide requirement chain (Throne of Miscellania needs The Fremennik Trials): the Rellekka longship's gate (viking_sailor.rs2 %varp347_viking = ^viking_complete)
        "::give coal 5",         -- guide requirement: Coal x5 brought along (RoyalTrouble.java coal ItemRequirement; fuels the lift engine, leg 5)
        "::setlevel hitpoints 85", -- guide: the Giant Sea Snake fight (RoyalTrouble.java killBoss; wiki 100 hp, def 160, poison) is fought for real with a combat-capable character, brought along for leg 8
        "::setlevel attack 99",   -- same: attack to wield the rune scimitar
        "::setlevel strength 99", -- same
        "::setlevel defence 80",  -- same
        "::give rune_scimitar 1", -- guide: a melee weapon brought along for the Giant Sea Snake (leg 8, killBoss)
        "::give lobster 16",      -- guide: food brought along for the Giant Sea Snake (leg 8, killBoss)
        "::give 3doseantipoison 2", -- guide: antipoison brought along, the snake poisons (leg 8, killBoss)
        "::setlevel magic 45",    -- travel: one Camelot Teleport (magic_spells.dbrow [magic_spell_teleport_camelot]: level 45, 5 air + 1 law), Lumbridge -> Camelot on the way to Rellekka
        "::give airrune 5",
        "::give lawrune 1",
        "::royaltrouble",        -- royal_debug.rs2:35 stages Throne of Miscellania done + resets every Royal Trouble var (it also teleports to the island ...)
        "::goto 3206 3233 0",    -- ... so the player is stood back on the fixture's Lumbridge tile: the voyage is driven (leg 1)
    },
    bind = {
        varp = "varb2140_royal_quest",
        constants = { not_started = 0, chose_partner = 10, investigating = 20, complete = 30 },
        row = "quest_royaltrouble",
        display = "Royal Trouble",
        points = 1,
    },

    legs = {
        {
            name = "castle",
            run = function(t)
                local r = route(t)
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

                -- LEG 1 BEGIN: travelToMisc
                -- The voyage: Camelot Teleport cast from Lumbridge (past the Taverley members' gate),
                -- overland Camelot -> Rellekka's docks, then the guide's travelToMisc, the sailor's longship.
                t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "camelotTeleport",
                    runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
                t.exec("goto-rellekkaDocks", t.player.goto_tile, 2629, 3691, 0)
                -- The ride if_closes, waits two ticks, telejumps, then opens its arrival mesbox
                -- (seam-facts b59-seam1 (i)), so the dialogue is played in two lists around the landing.
                t.exec("travelToMisc", t.player.talk_to, "viking_sailor", 1)
                t.exec("travelToMisc-dialog", t.chat.play, {
                    "player:Hello. Can I get a ride on your ship?",
                    "npc:If you're ready to jump aboard",
                    "choose:Let's go!",
                    "player:Let's go!",
                })
                t.exec("travelToMisc.sail", t.await, {
                    level = function()
                        local wr, tt = t.world.tile()
                        return wr == "ok" and tt.x == 2581 and tt.z == 3845 and tt.level == 0
                    end,
                    note = "the longship lands on the Miscellania dock 2581,3845,0",
                }, 15)
                t.exec("travelToMisc.arrive", t.chat.play, { "mesbox:The ship arrives at Miscellania.", "end" })
                local _, dock = t.world.tile()
                t.check("travelToMisc.landed", dock ~= nil and dock.x == 2581 and dock.z == 3845 and dock.level == 0,
                    "after the ride: " .. r.tile_str() .. " (want the Miscellania dock 2581,3845,0)")

                -- goUpToGhrim / talkToGhrim
                r.to_castle("goto-misc-castle")
                r.castle_in("ghrim1")
                r.to_throne_from_hall("ghrim1", "goUpToGhrim")
                r.level_is("goUpToGhrim-level", 1)
                t.exec("talkToGhrim", t.player.talk_to, "misc_advisor_ghrim", 1)
                t.exec("talkToGhrim-dialog", t.chat.play, {
                    "npc:Greetings, Your Royal Highness",
                    "choose:Has anything been happening in the kingdom recently?",
                    "player:Has anything been happening",
                    "npc:Interesting that you should ask",
                    "npc:the people are worried",
                    "npc:King Vargas has found himself",
                    "npc:Unfortunately, we still have not",
                    "player:So, what has he been doing",
                    "npc:He has been trying to rule",
                    "npc:The two of them have been writing",
                    "player:I see.",
                    "npc:Your Royal Highness, I suggest",
                    "choose:Yes.",
                    "player:Very well",
                    "npc:Thank you, Your Royal Highness",
                    "npc:I suggest you talk to the prince",
                })
                t.ticks(2)
                t.expect("quest.stage.chose_partner", t.quest.expect_stage("chose_partner"))

                -- goUpToPartner / talkToPartner: the guide sends the player up the NORTH stairs to the Princess
                r.throne_to_hall("astrid", "goDown-afterGhrim")
                r.door("astrid.northStairRoomDoor", CD, CDO, 2506, 3869, 0, 2506, 3868, 2506, 3870)
                r.climb("goUpToPartner", "spiralstairs_wooden", 1, 2505, 3871, 0, 2506, 3870, 2507, 3871, 1)
                r.door("astrid.northLandingDoor", CD, CDO, 2506, 3869, 1, 2506, 3869, 2506, 3867)
                -- Astrid's door stays OPEN while the player is inside: she wanders (m39_60.spawn 2502,3868,1)
                -- and a close can shut her out in the corridor (misc_astrid.lua r3 run 1); it is closed on the way out
                r.door("astrid.astridDoor", CD, CDO, 2504, 3867, 1, 2505, 3867, 2503, 3868, true)
                t.exec("talkToPartner", t.player.talk_to, "misc_princess_astrid", 1)
                t.exec("talkToPartner-dialog", t.chat.play, {
                    "player:Good day, your Highness",
                    "npc:Good day",
                    "player:Yes, Advisor Ghrim said",
                    "npc:I'll ask Father",
                    "player:Advisor Ghrim told me",
                    "player:What has happened while",
                    "npc:It's terrible, my dear",
                    "npc:It's awful that your work",
                    "player:What do you mean, 'an accident'",
                    "npc:Well, if my father",
                    "player:What would you suggest",
                    "npc:We can't have him declaring",
                    "npc:And I could finally show",
                    "npc:Astrid, I have an even better",
                    "npc:Better than archery",
                    "npc:I think we should persuade",
                    "npc:...What are you talking about",
                    "npc:Think of the poetic irony",
                    "npc:The best part is",
                    "npc:'Today is a happy day",
                    "npc:Our national anthems",
                    "player:I'm not sure that's such a good",
                    "npc:I quite agree",
                    "npc:Perhaps it's an odd idea",
                    "player:Are you sure it'll work",
                    "npc:Of course it will",
                    "npc:Two people who start out",
                    "player:That doesn't always work",
                    "npc:But we don't have an army",
                    "npc:So, will you talk to our father",
                    "npc:I'm sure you can manage",
                    "player:I'll give it a try",
                    "npc:I'm sure you'll manage",
                })
                t.ticks(2)
                t.expect("quest.stage.investigating", t.quest.expect_stage("investigating"))

                -- goUpToVargas / talkToVargas: back down the north stairs, along the hall, up the south stairs
                r.door("vargas1.astridDoorOut", CD, CDO, 2504, 3867, 1, 2504, 3867, 2506, 3866)
                r.door("vargas1.northLandingDoorOut", CD, CDO, 2506, 3869, 1, 2506, 3868, 2506, 3870)
                r.climb("goDown-afterAstrid", "spiralstairsmiddle_wooden", 3, 2505, 3871, 1, 2507, 3871, 2506, 3870, 0)
                r.door("vargas1.northStairRoomDoorOut", CD, CDO, 2506, 3869, 0, 2506, 3869, 2506, 3867)
                r.to_throne_from_hall("vargas1", "goUpToVargas")
                t.exec("talkToVargas", t.player.talk_to, "misc_king_vargas", 1)
                t.exec("talkToVargas-dialog", t.chat.play, {
                    "player:Your Majesty.",
                    "npc:What is it? You'll have to excuse me",
                    "player:What are you busy with",
                    "npc:Affairs of state",
                    "player:I thought I was supposed",
                    "npc:Well, there's not all that much",
                    "npc:And though you do make",
                    "player:So you're still helping",
                    "npc:In a manner of speaking",
                    "player:These 'affairs of state'",
                    "player:Would these involve",
                    "npc:Not exactly",
                    "npc:Well, yes",
                    "npc:And that's the least",
                    "npc:Some of Frodi's barrels",
                    "npc:Since you're the regent",
                    "choose:Right away, your Majesty.",
                    "player:Right away",
                    "npc:I'm glad to see",
                    "npc:All you need to do",
                    "npc:It might also be useful",
                })
                t.ticks(2)
                r.stage_is("quest.stage.misc_set_task", "varb2141_royal_misc", 10)

                -- goDownFromVargas / talkToGunnhild: out of the castle to the gardener
                r.throne_to_hall("gunnhild", "goDownFromVargas")
                r.level_is("goDownFromVargas-level", 0)
                r.castle_out("gunnhild")
                t.exec("goto-talkToGunnhild", t.player.goto_tile, 2525, 3855, 0)
                t.exec("talkToGunnhild", t.player.talk_to, "misc_gardener", 1)
                t.exec("talkToGunnhild-dialog", t.chat.play, {
                    "npc:Good day, Your Royal Highness",
                    "player:King Vargas tells me",
                    "npc:You mean you hadn't heard",
                    "npc:You're the only person",
                })
                t.ticks(2)
                r.stage_is("quest.stage.misc_aboutthefts", "varb2143_royal_misc_villagers_aboutthefts", 1)

                -- goDownFromVargas2: the guide's route to Queen Sigrid starts down the same stairs; go back up and down once
                r.to_castle("goto-goDownFromVargas2")
                r.castle_in("vargas2")
                r.south_room_in("vargas2")
                r.south_up("goUp-forVargas2")
                r.level_is("goUp-forVargas2-level", 1)
                r.south_down("goDownFromVargas2")
                r.level_is("goDownFromVargas2-level", 0)
                r.south_room_out("vargas2")
                r.castle_out("vargas2")
                local _, e_stage = t.var.server("varb2140_royal_quest")
                local _, e_misc = t.var.server("varb2141_royal_misc")
                t.check("leg.1.state", t.chat.kind() == "none", "tile " .. r.tile_str() .. " (outside the castle's east door), varb2140_royal_quest=" .. tostring(e_stage) .. " varb2141_royal_misc=" .. tostring(e_misc))
                -- LEG 1 END
            end,
        },
        {
            name = "etceteria",
            run = function(t)
                -- LEG 2 BEGIN: goUpToSigrid
                local r = route(t)

                -- goUpToSigrid / talkToSigrid
                r.to_etceteria("goto-goUpToSigrid")
                r.etc_in("sigrid1")
                r.etc_up("goUpToSigrid")
                r.level_is("goUpToSigrid-level", 1)
                r.sigrid_in("sigrid1")
                t.exec("talkToSigrid", t.player.talk_to, "misc_queen_sigrid", 1)
                t.exec("talkToSigrid-dialog", t.chat.play, {
                    "player:Your Majesty, do you have a moment?",
                    "npc:Incompetent!",
                    "npc:Yes, what is it?",
                    "player:I hear from King Vargas",
                    "npc:Of course not!",
                    "npc:That idiot Vargas",
                    "npc:I suppose all those insults",
                    "npc:Even so, that doesn't excuse",
                    "player:What happened, your Majesty?",
                    "npc:My citizens have told me",
                    "npc:Since you're the one",
                    "choose:Of course, it's my duty.",
                    "player:Of course, it's my duty.",
                    "npc:I'm glad you understand.",
                    "npc:If you talk to the citizens",
                    "npc:I would find out for myself",
                })
                t.ticks(2)
                r.stage_is("quest.stage.etc_sigrid_talked", "varb2142_royal_etc", 10)

                -- goDownFromSigridToMatilda / talkToMatilda: down, out of the castle, into Matilda's house
                r.sigrid_out("matilda")
                r.etc_down("goDownFromSigridToMatilda")
                r.level_is("goDownFromSigridToMatilda-level", 0)
                r.etc_out("matilda")
                r.door("matilda.houseDoor", "misc_viking_abode_door_low", "misc_viking_abode_door_low_open", 2603, 3874, 0, 2603, 3874, 2603, 3872)
                t.player.walk_near(t.player.by_symbol("npc", "misc_etc_woman_2"), 20)
                t.ticks(2)
                t.exec("talkToMatilda", t.player.talk_to, "misc_etc_woman_2", 1)
                t.exec("talkToMatilda-dialog", t.chat.play, {
                    "player:Excuse me...",
                    "npc:Yes?",
                    "player:Queen Sigrid tells me",
                    "npc:You haven't heard about that",
                    "npc:I'm surprised.",
                    "npc:Well, Miscellanian soldiers came",
                    "npc:When I told them to leave",
                    "npc:It was a wonderful hat",
                    "npc:Could you please get it back",
                    "player:I'll see what I can do.",
                })
                t.ticks(2)
                r.stage_is("quest.stage.etc_aboutthefts", "varb2144_royal_etc_villagers_aboutthefts", 1)
                r.door("matilda.houseDoorOut", "misc_viking_abode_door_low", "misc_viking_abode_door_low_open", 2603, 3874, 0, 2603, 3873, 2603, 3875)

                -- getCoalOrPickaxe: the guide's bank visit. Etceteria's cache has no dwarf_keldagrim_bankbooth at the
                -- guide's 2612,3900 (that tile is a house wall); the town's bank is the bank table
                -- banktable_breakroute_bankable 2619,3894 (bank_booths.rs2:72 oploc2, the same ~openbank), in the open
                -- street. Nothing is banked on a fresh account and the pickaxe is picked up in the dungeon
                -- (royal_dungeon.rs2:195), so it is opened and closed
                t.exec("getCoalOrPickaxe", t.player.click_loc, "banktable_breakroute_bankable", 2, { at = { 2619, 3894, 0 } })
                t.ticks(4)
                local _, bank_open = t.ui.is_modal()
                t.check("getCoalOrPickaxe-open", bank_open == true, "bank table op2 -> modal open=" .. tostring(bank_open))
                t.key("escape")
                t.ticks(2)

                -- goDownFromSigridToVargas: the guide's second descent of Sigrid's stairs (back in and up once, then down)
                r.to_etceteria("goto-goDownFromSigridToVargas")
                r.etc_in("sigrid2")
                r.etc_up("goUp-forSigrid2")
                r.level_is("goUp-forSigrid2-level", 1)
                r.etc_down("goDownFromSigridToVargas")
                r.level_is("goDownFromSigridToVargas-level", 0)
                r.etc_out("sigrid2")

                -- goBackUpToVargasFromSigrid / talkToVargasAfterSigrid
                r.to_castle("goto-goBackUpToVargasFromSigrid")
                r.castle_in("vargas3")
                r.to_throne_from_hall("vargas3", "goBackUpToVargasFromSigrid")
                r.level_is("goBackUpToVargasFromSigrid-level", 1)
                t.exec("talkToVargasAfterSigrid", t.player.talk_to, "misc_king_vargas", 1)
                t.exec("talkToVargasAfterSigrid-dialog", t.chat.play, {
                    "player:Your Majesty.",
                    "npc:Have you spoken to the citizens yet",
                    "player:I've spoken to the Miscellanian citizens, and also",
                    "npc:What did they say?",
                    "player:The Miscellanian citizens said",
                    "npc:What?",
                    "npc:I see. Sigrid thinks",
                    "player:...and the Etceterian citizens",
                    "npc:And she's even started",
                    "player:Your Majesty...",
                    "npc:Lying to her own people",
                    "npc:Which makes it all the worse",
                    "player:I'm not sure that would work",
                    "npc:Perhaps not",
                    "npc:In any case",
                    "npc:Ask Advisor Ghrim",
                    "npc:He's a capable advisor",
                    "player:I'll do that, your Majesty.",
                })
                t.ticks(2)
                r.stage_is("quest.stage.misc_reported", "varb2141_royal_misc", 20)

                -- goUpToGhrim2 / talkToGhrim2: down the stairs and up again, as the guide routes it
                r.throne_out("ghrim2")
                r.landing_out("ghrim2")
                r.south_down("goDown-beforeGhrim2")
                r.level_is("goDown-beforeGhrim2-level", 0)
                r.south_up("goUpToGhrim2")
                r.level_is("goUpToGhrim2-level", 1)
                r.landing_in("ghrim2")
                r.throne_in("ghrim2")
                t.exec("talkToGhrim2", t.player.talk_to, "misc_advisor_ghrim", 1)
                t.exec("talkToGhrim2-dialog", t.chat.play, {
                    "npc:Greetings, Your Royal Highness",
                    "choose:King Vargas asked me to talk to you.",
                    "player:King Vargas asked me to talk to you.",
                    "player:He said you might know",
                    "npc:I'm not sure what you're implying",
                    "player:Er... that you could advise",
                    "npc:Ah, of course.",
                    "npc:Assuming that nobody",
                    "npc:it is of course likely",
                    "npc:The sailor who brought you",
                    "npc:I suggest you ask him",
                })
                t.ticks(2)
                r.stage_is("quest.stage.misc_ghrim_talked", "varb2141_royal_misc", 30)
                local _, e_stage = t.var.server("varb2140_royal_quest")
                local _, e_misc = t.var.server("varb2141_royal_misc")
                t.check("leg.2.end", t.chat.kind() == "none", "tile " .. r.tile_str() .. ", varb2140_royal_quest=" .. tostring(e_stage) .. " varb2141_royal_misc=" .. tostring(e_misc) .. " (Ghrim sent the player to the sailor)")
                -- LEG 2 END
            end,
        },
        {
            name = "dungeon",
            run = function(t)
                -- LEG 3 BEGIN: goDownToSailor
                local r = route(t)
                t.ticks(2)

                -- goDownToSailor: out of the throne room, down the south stairs, out of the castle
                r.throne_to_hall("sailor", "goDownToSailor")
                r.level_is("goDownToSailor-level", 0)
                r.castle_out("sailor")

                -- talkToSailor: the docks (the sailor stands at 2581,3847, m40_60.spawn:12)
                t.exec("goto-talkToSailor", t.player.goto_tile, 2579, 3847, 0)
                t.exec("talkToSailor", t.player.talk_to, "misc_sailor", 1)
                t.exec("talkToSailor-dialog", t.chat.play, {
                    "choose:I'm looking for a sailor...",
                    "player:I'm looking for a sailor",
                    "npc:You've found one here",
                    "player:The King's advisor said",
                    "npc:Well, I don't know of any suspicious",
                    "player:Oh. Has anyone visited",
                    "npc:Nobody apart from you",
                    "player:What kids from Rellekka",
                    "npc:Five of them came over",
                    "player:I didn't realise the King",
                    "player:I didn't know Miscellania",
                    "npc:They've not been used",
                    "npc:Some of them decided",
                    "npc:Only the weather",
                    "player:So you decided to live",
                    "npc:Well, of course, they had to be enlarged",
                    "npc:If you're looking for those kids",
                    "npc:Why are you surprised",
                    "player:I'm still an adventurer",
                })
                t.ticks(2)
                r.stage_is("quest.stage.misc_sailor_talked", "varb2141_royal_misc", 40)

                -- goUpToVargasAfterSailor / talkToVargasAfterSailor
                r.to_castle("goto-goUpToVargasAfterSailor")
                r.castle_in("vargas4")
                r.to_throne_from_hall("vargas4", "goUpToVargasAfterSailor")
                r.level_is("goUpToVargasAfterSailor-level", 1)
                t.exec("talkToVargasAfterSailor", t.player.talk_to, "misc_king_vargas", 1)
                t.exec("talkToVargasAfterSailor-dialog", t.chat.play, {
                    "player:Your Majesty.",
                    "player:Your Majesty, how do I get into the dungeons?",
                    "npc:The dungeons?",
                    "npc:Of course, how could I have forgotten?",
                    "npc:I'm surprised I didn't tell you",
                    "player:It's okay, the sailor",
                    "player:He said that the only new arrivals",
                    "player:Those relatives",
                    "npc:Ah, so you wish",
                    "npc:I doubt that anyone",
                    "npc:There is something I need you",
                    "npc:They are still being excavated",
                    "npc:Perhaps they are monsters",
                    "npc:Whatever they are",
                    "npc:You are a member of the royal family",
                    "player:Of course, your Majesty.",
                    "npc:The guard by the dungeon entrance",
                    "npc:You'll find the entrance",
                })
                t.ticks(2)
                t.exec("talkToVargasAfterSailor-mesbox", t.chat.continue_, true)
                t.ticks(2)
                r.stage_is("quest.stage.misc_scroll_given", "varb2141_royal_misc", 50)
                t.check("talkToVargasAfterSailor-scroll", select(2, t.inv.count("royal_official_scroll")) == 1, "official scroll in backpack: " .. tostring(select(2, t.inv.count("royal_official_scroll"))))

                -- goDownStairsToDungeon / goDownToDungeonNoScroll / goDownLadderToDungeon: the ladder is south of the castle
                r.throne_to_hall("ladder", "goDownStairsToDungeon")
                r.level_is("goDownStairsToDungeon-level", 0)
                r.castle_out("ladder")
                t.exec("goto-goDownLadderToDungeon", t.player.goto_tile, 2509, 3847, 0)
                t.exec("goDownLadderToDungeon", t.player.click_loc, "royal_ladder_down", 1)
                t.exec("goDownToDungeonNoScroll", t.chat.play, {
                    "npc:Sorry, but nobody's allowed",
                    "player:But I'm the Regent!",
                    "npc:You're the Regent?",
                    "npc:Sorry, 'Regent'",
                    "player:Look, the King gave me a scroll",
                    "npc:Let's have a look",
                    "player:Here you go.",
                    "npc:Hmm... all seems",
                    "npc:All right, your Highness",
                    "npc:Try not to be eaten",
                })
                t.ticks(4)
                r.stage_is("quest.stage.misc_in_dungeon", "varb2141_royal_misc", 60)
                local _, dz = t.world.tile()
                t.check("goDownLadderToDungeon-arrived", dz ~= nil and dz.z > 10000, "tile " .. tostring(dz and (dz.x .. "," .. dz.z)) .. " (dungeon village)")

                -- talkToDonal: in the pub, through its door royal_village_door 2525,10256 (gaps-world: Miscellania and Etceteria)
                t.exec("goto-talkToDonal", t.player.goto_tile, 2524, 10256, 0)
                r.door("donal.pubDoor", "royal_village_door", "royal_village_door_open", 2525, 10256, 0, 2525, 10256, 2527, 10257) -- 2527,10256 is the pub table
                t.exec("talkToDonal", t.player.talk_to, "royal_dwarf_drunk", 1)
                t.exec("talkToDonal-dialog", t.chat.play, {
                    "npc:What do you want?",
                    "player:What are you doing down in these caves?",
                    "npc:I'm waiting to work",
                    "npc:...though I don't really want",
                    "player:Why not?",
                    "npc:Not with monsters like that one",
                    "npc:It had huge teeth",
                    "npc:If it attacks the town",
                    "npc:Are you an adventurer",
                    "player:Adventurer and Regent",
                    "npc:In that case",
                    "npc:I don't want to have to face",
                    "choose:Of course. Dealing with monsters is what I do best!",
                    "player:Of course. Dealing with monsters",
                    "npc:You'll need to get into the other caves",
                    "npc:There's a crack in the wall",
                    "npc:I can give you a mining prop",
                    "player:Okay, so all I have to do",
                    "npc:No, after that",
                    "npc:But the walls are slippery",
                    "npc:...and I think I might",
                    "npc:You'll probably need to fix the lift",
                    "player:And the monster's behind that tunnel?",
                    "npc:Somewhere behind it",
                    "npc:There's some rope and some planks",
                    "npc:Ah, here's that mining prop",
                    "npc:Here you go.",
                })
                t.ticks(2)
                t.exec("talkToDonal-mesbox", t.chat.continue_, true)
                t.ticks(2)
                r.stage_is("quest.stage.misc_donal_talked", "varb2141_royal_misc", 80)
                t.check("talkToDonal-prop", select(2, t.inv.count("royal_mining_prop")) == 1, "mining prop in backpack: " .. tostring(select(2, t.inv.count("royal_mining_prop"))))
                r.door("donal.pubDoorOut", "royal_village_door", "royal_village_door_open", 2525, 10256, 0, 2526, 10256, 2524, 10256)

                -- usePropOnCrevice / enterCrevice: the crevice in the north-west corner
                t.exec("goto-usePropOnCrevice", t.player.goto_tile, 2505, 10279, 0)
                local crevice = t.player.by_symbol("loc", "royal_cavewall_crack")
                t.check("crevice.found", crevice ~= nil, "royal_cavewall_crack resolved: " .. tostring(crevice and crevice.id))
                local u_r, u_d = t.player.use_on("royal_mining_prop", crevice)
                t.expect("usePropOnCrevice", t.var.await_server("varb2145_royal_misc_usedminingprop", 1, 40))
                t.check("usePropOnCrevice.press", u_r ~= nil, "use_on -> " .. tostring(u_r) .. " " .. tostring(u_d) .. "; varb2145_royal_misc_usedminingprop read back = 1")
                t.exec("enterCrevice", t.player.click_loc, "royal_cavewall_crack", 1)
                t.ticks(4)
                local _, ez = t.world.tile()
                t.check("enterCrevice-arrived", ez ~= nil and ez.z >= 10282, "tile " .. tostring(ez and (ez.x .. "," .. ez.z)) .. " (lift room side of the crevice)")

                local _, e_tile = t.world.tile()
                local _, e_stage = t.var.server("varb2140_royal_quest")
                local _, e_misc = t.var.server("varb2141_royal_misc")
                t.check("leg.3.end", t.chat.kind() == "none", "tile " .. tostring(e_tile and (e_tile.x .. "," .. e_tile.z)) .. " level " .. tostring(select(2, t.world.level())) .. ", varb2140_royal_quest=" .. tostring(e_stage) .. " varb2141_royal_misc=" .. tostring(e_misc) .. "; scroll spent, mining prop used on the crevice; backpack empty")
                -- LEG 3 END
            end,
        },
        {
            name = "lift",
            run = function(t)
                -- LEG 4 BEGIN: takePulley
                local function stage_now()
                    return select(2, t.var.server("varb2146_royal_liftstage"))
                end
                -- crate: op1 pulls one item out; wait for the backpack count to rise (trap 24)
                local function take(name, sym, item, want)
                    local r, d = t.player.click_loc(sym, 1)
                    local a = t.inv.await(item, want, 12)
                    t.check(name, a == "ok", "click_loc " .. sym .. " -> " .. tostring(r) .. " " .. tostring(d):sub(1, 60) .. "; " .. item .. " now x" .. tostring(select(2, t.inv.count(item))))
                end
                local function on_scaffold(name, item, want_stage)
                    local sc = t.player.by_symbol("loc", "royal_side_scaffold_multiloc")
                    local r, d = t.player.use_on(item, sc)
                    local a = t.var.await_server("varb2146_royal_liftstage", want_stage, 20)
                    t.check(name, a == "ok", "use_on " .. item .. " scaffold -> " .. tostring(r) .. " " .. tostring(d):sub(1, 60) .. "; varb2146_royal_liftstage = " .. tostring(stage_now()))
                end
                local function on_item(name, a, b, gone, made)
                    local r, d = t.player.use_item_on_item(a, b)
                    local m = t.inv.await(made, 1, 12)
                    t.check(name, m == "ok", "use " .. a .. " on " .. b .. " -> " .. tostring(r) .. " " .. tostring(d):sub(1, 60) .. "; " .. made .. " x" .. tostring(select(2, t.inv.count(made))) .. ", " .. gone .. " x" .. tostring(select(2, t.inv.count(gone))))
                end
                t.ticks(2)
                local st0 = stage_now()
                t.check("leg.4.start", st0 == 0, "varb2146_royal_liftstage = " .. tostring(st0) .. " at the lift room")

                take("takePulley", "royal_crate_planks+pulleys", "royal_plank_pulley", 1)
                on_scaffold("usePulleyOnScaffold", "royal_plank_pulley", 1)
                take("takePulley2", "royal_crate_planks+pulleys", "royal_plank_pulley", 1)
                take("takeBeam", "royal_crate_planks", "royal_beam", 1)
                on_item("useBeamOnPulley", "royal_beam", "royal_plank_pulley", "royal_beam", "royal_plank_pulley_long")
                take("takeBeam2", "royal_crate_planks", "royal_beam", 1)
                on_item("useBeamOnLongPulley", "royal_beam", "royal_plank_pulley_long", "royal_beam", "royal_plank_pulley_longer")
                on_scaffold("useLongerPulleyOnScaffold", "royal_plank_pulley_longer", 2)
                take("takePulley3", "royal_crate_planks+pulleys", "royal_plank_pulley", 1)
                on_scaffold("usePulleyOnScaffold2", "royal_plank_pulley", 3)

                local _, e_tile = t.world.tile()
                t.check("leg.4.end", t.chat.kind() == "none" and stage_now() == 3, "tile " .. tostring(e_tile and (e_tile.x .. "," .. e_tile.z)) .. " level " .. tostring(select(2, t.world.level())) .. ", varb2140_royal_quest=" .. tostring(select(2, t.var.server("varb2140_royal_quest"))) .. ", varb2146_royal_liftstage=3 (two pulley beams and the longer top beam on the scaffold); backpack empty")
                -- LEG 4 END
            end,
        },
        {
            name = "engine",
            run = function(t)
                -- LEG 5 BEGIN: takeRope
                local function stage_now()
                    return select(2, t.var.server("varb2146_royal_liftstage"))
                end
                local function coal_in()
                    return select(2, t.var.server("varb2156_royal_coalinengine"))
                end
                local function take(name, sym, item, want)
                    local r, d = t.player.click_loc(sym, 1)
                    local a = t.inv.await(item, want, 12)
                    t.check(name, a == "ok", "click_loc " .. sym .. " -> " .. tostring(r) .. " " .. tostring(d):sub(1, 60) .. "; " .. item .. " now x" .. tostring(select(2, t.inv.count(item))))
                end
                local function on_loc(name, item, sym, want_stage)
                    local tg = t.player.by_symbol("loc", sym)
                    local r, d = t.player.use_on(item, tg)
                    local a = t.var.await_server("varb2146_royal_liftstage", want_stage, 20)
                    t.check(name, a == "ok", "use_on " .. item .. " " .. sym .. " -> " .. tostring(r) .. " " .. tostring(d):sub(1, 60) .. "; varb2146_royal_liftstage = " .. tostring(stage_now()))
                end
                local function coal_on_platform(name, want_coal)
                    local tg = t.player.by_symbol("loc", "royal_engine_platform_multiloc")
                    local r, d = t.player.use_on("coal", tg)
                    t.ticks(2)
                    t.check(name, coal_in() == want_coal, "use_on coal engine platform -> " .. tostring(r) .. " " .. tostring(d):sub(1, 60) .. "; varb2156_royal_coalinengine = " .. tostring(coal_in()) .. ", varb2146_royal_liftstage = " .. tostring(stage_now()) .. ", coal x" .. tostring(select(2, t.inv.count("coal"))))
                end
                t.ticks(2)
                t.check("leg.5.start", stage_now() == 3 and select(2, t.inv.count("coal")) == 5, "varb2146_royal_liftstage = " .. tostring(stage_now()) .. ", coal x" .. tostring(select(2, t.inv.count("coal"))))

                take("takeRope", "royal_crate_rope", "rope", 1)
                on_loc("useRopeOnScaffold", "rope", "royal_side_scaffold_multiloc", 4)
                take("takeBeam3", "royal_crate_planks", "royal_beam", 1)
                on_loc("useBeamOnPlatform", "royal_beam", "royal_lift_platform_multiloc", 5)

                local r1 = t.player.click_obj("royal_coal_engine", 3)
                local a1 = t.inv.await("royal_coal_engine", 1, 12)
                t.check("pickUpEngine", a1 == "ok", "click_obj royal_coal_engine -> " .. tostring(r1) .. "; engine x" .. tostring(select(2, t.inv.count("royal_coal_engine"))))

                -- the guide's manual and the first lump go into the engine while it is still in the pack (royal_dungeon.rs2:325)
                local r2 = t.player.click_obj("royal_lift_manual", 3)
                local a2 = t.inv.await("royal_lift_manual", 1, 12)
                t.exec("putCoalIntoEngine-manual", t.player.inv_op, "royal_lift_manual", 1)
                t.ticks(2)
                local k = 0
                while t.chat.kind() ~= "none" and k < 10 do
                    t.chat.continue_(true)
                    t.ticks(1)
                    k = k + 1
                end
                local r3, d3 = t.player.use_item_on_item("coal", "royal_coal_engine")
                t.ticks(2)
                t.check("putCoalIntoEngine", coal_in() == 1, "manual " .. tostring(a2) .. " (" .. tostring(r2) .. ") read in " .. k .. " pages; use coal on engine -> " .. tostring(r3) .. " " .. tostring(d3):sub(1, 60) .. "; varb2156_royal_coalinengine = " .. tostring(coal_in()))

                on_loc("useEngineOnPlatform", "royal_coal_engine", "royal_engine_platform_multiloc", 6)
                coal_on_platform("putCoalIntoEnginePlaced", 2)
                coal_on_platform("putCoalIntoEnginePlaced2", 3)
                coal_on_platform("putCoalIntoEnginePlaced3", 4)
                coal_on_platform("putCoalIntoEnginePlaced4", 5)
                t.var.await_server("varb2146_royal_liftstage", 7, 10)

                local _, e_tile = t.world.tile()
                t.check("leg.5.end", t.chat.kind() == "none" and stage_now() == 7, "tile " .. tostring(e_tile and (e_tile.x .. "," .. e_tile.z)) .. " level " .. tostring(select(2, t.world.level())) .. ", varb2140_royal_quest=" .. tostring(select(2, t.var.server("varb2140_royal_quest"))) .. ", varb2146_royal_liftstage=" .. tostring(stage_now()) .. " (lift repaired, five coal in the engine); backpack holds the lift manual only")
                -- LEG 5 END
            end,
        },
        {
            name = "caves",
            run = function(t)
                -- LEG 6 BEGIN: putCoalIntoEnginePlaced5
                local function stage_now()
                    return select(2, t.var.server("varb2146_royal_liftstage"))
                end
                local function tile_str()
                    local _, tl = t.world.tile()
                    return tostring(tl and (tl.x .. "," .. tl.z)) .. " level " .. tostring(select(2, t.world.level()))
                end
                t.ticks(2)
                t.check("leg.6.start", stage_now() == 7, "varb2146_royal_liftstage = " .. tostring(stage_now()) .. " at " .. tile_str())

                -- the fifth lump: the stage-7 transition happens on the platform (royal_dungeon.rs2 coal checks)
                t.check("putCoalIntoEnginePlaced5", stage_now() == 7 and select(2, t.var.server("varb2156_royal_coalinengine")) == 5, "five coal in the engine: varb2156_royal_coalinengine = " .. tostring(select(2, t.var.server("varb2156_royal_coalinengine"))) .. ", varb2146_royal_liftstage = " .. tostring(stage_now()))

                local r1, d1 = t.player.click_loc("royal_crate_rope", 1)
                local a1 = t.inv.await("rope", 1, 12)
                t.check("takeRope2", a1 == "ok", "click_loc royal_crate_rope -> " .. tostring(r1) .. " " .. tostring(d1):sub(1, 60) .. "; rope x" .. tostring(select(2, t.inv.count("rope"))))

                local r2, d2 = t.player.click_loc("royal_coal_lift_platform_useable", 1)
                t.ticks(6)
                t.check("useLift", select(2, t.world.level()) == 1, "click_loc royal_coal_lift_platform_useable -> " .. tostring(r2) .. " " .. tostring(d2):sub(1, 60) .. "; now " .. tile_str() .. ", varb2146_royal_liftstage = " .. tostring(stage_now()))

                -- the guide's goBackToPlank: the tunnel is entered first (no plank in hand), then left again for the plank
                local r4, d4 = t.player.click_loc("royal_small_cave", 1)
                t.ticks(6)
                t.check("enterTunnelFromPlankRoom-first", select(2, t.world.level()) == 0, "click_loc royal_small_cave -> " .. tostring(r4) .. " " .. tostring(d4):sub(1, 60) .. "; now " .. tile_str())
                local r5, d5 = t.player.click_loc("royal_small_cave_exit", 1)
                t.ticks(6)
                t.check("goBackToPlank", select(2, t.world.level()) == 1, "click_loc royal_small_cave_exit -> " .. tostring(r5) .. " " .. tostring(d5):sub(1, 60) .. "; now " .. tile_str())

                local r3 = t.player.click_obj("woodplank", 3)
                local a3 = t.inv.await("woodplank", 1, 12)
                t.check("takePlank", a3 == "ok", "click_obj woodplank -> " .. tostring(r3) .. "; plank x" .. tostring(select(2, t.inv.count("woodplank"))) .. " at " .. tile_str())

                local r6, d6 = t.player.click_loc("royal_small_cave", 1)
                t.ticks(6)
                t.check("enterTunnelFromPlankRoom", select(2, t.world.level()) == 0 and select(2, t.inv.count("woodplank")) == 1, "click_loc royal_small_cave -> " .. tostring(r6) .. " " .. tostring(d6):sub(1, 60) .. "; now " .. tile_str() .. ", plank x" .. tostring(select(2, t.inv.count("woodplank"))) .. ", rope x" .. tostring(select(2, t.inv.count("rope"))))

                -- the rope goes on the rock over the water (royal_cave.rs2:11), then the rope swing (skill_agility/rope_swings.rs2)
                t.exec("goto-attachRope", t.player.goto_tile, 2536, 10299, 0)
                local tg = t.player.by_symbol("loc", "royal_obstical_rockswing_mid_no_rope")
                local r7, d7 = t.player.use_on("rope", tg)
                local a7 = t.var.await_server("varb2147_royal_misc_ropeloc", 1, 20)
                t.check("attachRope", a7 == "ok", "use_on rope royal_obstical_rockswing_mid_no_rope -> " .. tostring(r7) .. " " .. tostring(d7):sub(1, 60) .. "; varb2147_royal_misc_ropeloc = " .. tostring(select(2, t.var.server("varb2147_royal_misc_ropeloc"))) .. ", rope x" .. tostring(select(2, t.inv.count("rope"))))
                local r8, d8 = t.player.click_loc("royal_ropeswing_multiloc", 1)
                t.ticks(10)
                local _, sw = t.world.tile()
                t.check("swingOverRope", sw ~= nil and sw.x >= 2541, "click_loc royal_ropeswing_multiloc -> " .. tostring(r8) .. " " .. tostring(d8):sub(1, 60) .. "; now " .. tile_str())

                t.exec("goto-searchFire1", t.player.goto_tile, 2553, 10295, 0)
                local r9, d9 = t.player.click_loc("royal_fire_remains1", 1)
                local a9 = t.inv.await("royal_diary1", 1, 12)
                t.check("searchFire1", a9 == "ok", "click_loc royal_fire_remains1 -> " .. tostring(r9) .. " " .. tostring(d9):sub(1, 60) .. "; varb2148_royal_misc_numberofchapters = " .. tostring(select(2, t.var.server("varb2148_royal_misc_numberofchapters"))))

                local k = 0
                while t.chat.kind() ~= "none" and k < 10 do
                    t.chat.continue_(true)
                    t.ticks(1)
                    k = k + 1
                end

                -- plankRock1: the slippery rock at 2548,10288, plain travel to the tile beside it from the fire, then the plank (royal_cave.rs2:28)
                t.exec("goto-plankRock1", t.player.goto_tile, 2549, 10288, 0)
                local x0 = select(2, t.world.tile()).x
                local tgr = t.player.by_symbol("loc", "royal_invisible_puddletrap")
                local r10, d10 = t.player.use_on("woodplank", tgr)
                t.ticks(6)
                local _, rt = t.world.tile()
                t.check("plankRock1", rt ~= nil and rt.x == 2547, "use_on woodplank royal_invisible_puddletrap -> " .. tostring(r10) .. " " .. tostring(d10):sub(1, 60) .. "; from x " .. tostring(x0) .. " to " .. tile_str() .. ", plank x" .. tostring(select(2, t.inv.count("woodplank"))))
                t.ticks(2)
                -- plankRock2: walk the one tile to the east side of the rock at 2545,10287 (no teleport onto the island)
                t.player.walk_to(2546, 10287, 8)
                t.ticks(4)
                local _, wt = t.world.tile()
                t.check("walk-plankRock2", wt ~= nil and wt.x == 2546, "walked from 2547 to " .. tile_str())
                x0 = wt.x
                local tgr2 = t.player.by_symbol("loc", "royal_invisible_puddletrap")
                local r11, d11 = t.player.use_on("woodplank", tgr2)
                t.ticks(6)
                local _, rt2 = t.world.tile()
                t.check("plankRock2", rt2 ~= nil and rt2.x == 2544, "use_on woodplank royal_invisible_puddletrap -> " .. tostring(r11) .. " " .. tostring(d11):sub(1, 60) .. "; from x " .. tostring(x0) .. " to " .. tile_str() .. ", plank x" .. tostring(select(2, t.inv.count("woodplank"))))
                t.ticks(2)
                t.check("leg.6.end", t.chat.kind() == "none", "tile " .. tile_str() .. ", varb2140_royal_quest=" .. tostring(select(2, t.var.server("varb2140_royal_quest"))) .. ", varb2148_royal_misc_numberofchapters=" .. tostring(select(2, t.var.server("varb2148_royal_misc_numberofchapters"))) .. "; backpack: royal_diary1, woodplank (kept), royal_lift_manual")
                -- LEG 6 END
            end,
        },
        {
            name = "diary",
            run = function(t)
                -- LEG 7 BEGIN: plankRock3
                local function tile_str()
                    local _, tl = t.world.tile()
                    return tostring(tl and (tl.x .. "," .. tl.z)) .. " level " .. tostring(select(2, t.world.level()))
                end
                local function chapters()
                    return select(2, t.var.server("varb2148_royal_misc_numberofchapters"))
                end
                local function pump(max)
                    local k = 0
                    while t.chat.kind() ~= "none" and k < max do
                        t.chat.continue_(true)
                        t.ticks(1)
                        k = k + 1
                    end
                    return k
                end
                t.ticks(2)
                t.check("leg.7.start", chapters() == 1 and select(2, t.inv.count("woodplank")) == 1, "varb2148 chapters = " .. tostring(chapters()) .. ", plank x" .. tostring(select(2, t.inv.count("woodplank"))) .. " at " .. tile_str())

                -- plankRock3 then plankRock4: walk beside each rock (no teleport onto the islands), lay the plank
                local function walk_plank(name, stand_x, stand_z, land_x)
                    t.player.walk_to(stand_x, stand_z, 8)
                    t.ticks(4)
                    local x0 = select(2, t.world.tile()).x
                    local tg = t.player.by_symbol("loc", "royal_invisible_puddletrap")
                    local r, d = t.player.use_on("woodplank", tg)
                    t.ticks(6)
                    local _, rt = t.world.tile()
                    t.check(name, rt ~= nil and rt.x == land_x, "use_on woodplank royal_invisible_puddletrap -> " .. tostring(r) .. " " .. tostring(d):sub(1, 60) .. "; from x " .. tostring(x0) .. " to " .. tile_str() .. ", plank x" .. tostring(select(2, t.inv.count("woodplank"))))
                end
                walk_plank("plankRock3", 2543, 10287, 2541)
                walk_plank("plankRock4", 2540, 10286, 2538)

                t.exec("goto-searchFire2", t.player.goto_tile, 2535, 10281, 0)
                local r1, d1 = t.player.click_loc("royal_fire_remains2", 1)
                t.ticks(4)
                pump(10)
                t.check("searchFire2", chapters() == 2, "click_loc royal_fire_remains2 -> " .. tostring(r1) .. " " .. tostring(d1):sub(1, 60) .. "; chapters = " .. tostring(chapters()) .. ", diary2 x" .. tostring(select(2, t.inv.count("royal_diary2"))))

                t.exec("goto-searchFire3", t.player.goto_tile, 2554, 10279, 0)
                local r2, d2 = t.player.click_loc("royal_fire_remains3", 1)
                t.ticks(4)
                pump(10)
                t.check("searchFire3", chapters() == 3, "click_loc royal_fire_remains3 -> " .. tostring(r2) .. " " .. tostring(d2):sub(1, 60) .. "; chapters = " .. tostring(chapters()) .. " at " .. tile_str())

                t.exec("goto-searchFire4", t.player.goto_tile, 2548, 10260, 0)
                local r3, d3 = t.player.click_loc("royal_fire_remains4", 1)
                t.ticks(4)
                pump(10)
                t.check("searchFire4", chapters() == 4, "click_loc royal_fire_remains4 -> " .. tostring(r3) .. " " .. tostring(d3):sub(1, 60) .. "; chapters = " .. tostring(chapters()) .. " at " .. tile_str())

                t.exec("goto-searchFire5", t.player.goto_tile, 2572, 10246, 0)
                local r4, d4 = t.player.click_loc("royal_fire_remains5", 1)
                t.ticks(4)
                pump(10)
                t.check("searchFire5", chapters() == 5, "click_loc royal_fire_remains5 -> " .. tostring(r4) .. " " .. tostring(d4):sub(1, 60) .. "; chapters = " .. tostring(chapters()) .. ", diary5 x" .. tostring(select(2, t.inv.count("royal_diary5"))))

                -- readDiary: Read the diary (royal_diary.rs2 opheld1), five pages of mesboxes
                local r5 = t.player.inv_op("royal_diary5", 1)
                t.ticks(3)
                local n5 = pump(30)
                t.ticks(2)
                t.check("readDiary", select(2, t.var.server("varb2153_royal_misc_diarychapter5read")) == 1, "inv_op royal_diary5 op1 -> " .. tostring(r5) .. "; " .. tostring(n5) .. " pages continued; varb2153_royal_misc_diarychapter5read = " .. tostring(select(2, t.var.server("varb2153_royal_misc_diarychapter5read"))))

                t.exec("goto-enterSnakesRoom", t.player.goto_tile, 2585, 10259, 0)
                local r6, d6 = t.player.click_loc("royal_cavewall_crack_fremenniks_in", 1)
                t.ticks(6)
                local _, kt = t.world.tile()
                t.check("enterSnakesRoom", select(2, t.var.server("varb2157_royal_meddlingkids_cutscene")) == 1, "click_loc royal_cavewall_crack_fremenniks_in -> " .. tostring(r6) .. " " .. tostring(d6):sub(1, 60) .. "; now " .. tile_str() .. ", varb2157_royal_meddlingkids_cutscene = " .. tostring(select(2, t.var.server("varb2157_royal_meddlingkids_cutscene"))))

                local rw, dw = t.player.walk_near(t.player.by_symbol("npc", "royal_fremennik_teen3"), 20)
                t.ticks(2)
                local r7, d7 = t.player.talk_to("royal_fremennik_teen3", 1)
                t.ticks(2)
                local n7 = pump(40)
                t.ticks(3)
                local misc = select(2, t.var.server("varb2141_royal_misc"))
                t.check("talkToArmod", misc == 110, "talk_to royal_fremennik_teen3 -> " .. tostring(r7) .. " " .. tostring(d7):sub(1, 60) .. "; " .. tostring(n7) .. " pages continued; varb2141_royal_misc = " .. tostring(misc))

                t.ticks(2)
                t.check("leg.7.end", t.chat.kind() == "none", "tile " .. tile_str() .. ", varb2140_royal_quest=" .. tostring(select(2, t.var.server("varb2140_royal_quest"))) .. ", varb2141_royal_misc=" .. tostring(misc) .. " (kids talked); backpack: royal_diary5, woodplank, royal_lift_manual")
                -- LEG 7 END
            end,
        },
        {
            name = "snake",
            run = function(t)
                -- LEG 8 BEGIN: enterBossRoom
                local function tile_str()
                    local _, tl = t.world.tile()
                    return tostring(tl and (tl.x .. "," .. tl.z)) .. " level " .. tostring(select(2, t.world.level()))
                end
                local function pump(max)
                    local k = 0
                    while t.chat.kind() ~= "none" and k < max do
                        t.chat.continue_(true)
                        t.ticks(1)
                        k = k + 1
                    end
                    return k
                end
                local function level_is(name, want)
                    local lv = select(2, t.world.level())
                    t.check(name, lv == want, "level " .. tostring(lv) .. " (want " .. tostring(want) .. ") at " .. tile_str())
                end
                local function var_now(name)
                    return select(2, t.var.server(name))
                end
                local function hp_now()
                    local _, v = t.skill.read("hitpoints")
                    return tostring(type(v) == "table" and (v.current or v.level) or v)
                end

                t.exec("wield-rune_scimitar", t.player.equip, "rune_scimitar")
                t.ticks(2)
                t.exec("drink-antipoison", t.player.inv_op, "3doseantipoison", 1)
                t.ticks(3)

                -- enterBossRoom: the crevice 2617,10272 (Agility 40, royal_cave.rs2:174)
                t.exec("goto-enterBossRoom", t.player.goto_tile, 2617, 10270, 0)
                local er, ed = t.player.click_loc("royal_cavewall_crack_seasnake_in", 1)
                t.ticks(6)
                local _, bt = t.world.tile()
                t.check("enterBossRoom", bt ~= nil and bt.z >= 10273, "click_loc royal_cavewall_crack_seasnake_in -> " .. tostring(er) .. " " .. tostring(ed):sub(1, 60) .. "; now " .. tile_str())

                -- killBoss: fought for real, melee from the shingle in front of its body (royal_kids_boss.rs2:94)
                t.exec("killBoss", t.player.attack, "royal_sea_snake_mother_smaller", 2, 40)
                t.exec("killBoss.dead", t.npc.await_dead_engaged, 600, 6, { eat = { item = "lobster", below = 35 } })
                t.ticks(2)
                local misc = var_now("varb2141_royal_misc")
                t.check("killBoss.stage", misc == 120, "varb2141_royal_misc = " .. tostring(misc) .. " (snake dead), hitpoints " .. hp_now() .. ", lobsters left " .. tostring(select(2, t.inv.count("lobster"))))

                -- pickUpBox: the heavy box tumbles to the floor (royal_kids_boss.rs2:144)
                t.exec("pickUpBox-click", t.player.click_obj, "royal_box", 3)
                t.inv.await("royal_box", 1, 10)
                t.check("pickUpBox", select(2, t.inv.count("royal_box")) == 1, "click_obj royal_box; royal_box x" .. tostring(select(2, t.inv.count("royal_box"))))

                -- leaveBossRoom / goUpRope
                local lr, ld = t.player.click_loc("royal_cavewall_crack_seasnake_out", 1)
                t.ticks(6)
                local _, lt = t.world.tile()
                t.check("leaveBossRoom", lt ~= nil and lt.z <= 10273, "click_loc royal_cavewall_crack_seasnake_out -> " .. tostring(lr) .. " " .. tostring(ld):sub(1, 60) .. "; now " .. tile_str())
                local ur, ud = t.player.click_loc("royal_light_exit_with_rope", 1)
                t.ticks(3)
                local up = pump(20)
                t.ticks(3)
                local _, st = t.world.tile()
                t.check("goUpRope", st ~= nil and st.z < 4000, "click_loc royal_light_exit_with_rope -> " .. tostring(ur) .. " " .. tostring(ud):sub(1, 60) .. "; " .. tostring(up) .. " guard pages continued; now " .. tile_str())

                -- goUpToSigridToFinish / talkToSigridToFinish: overland from the rope's landing to Etceteria's front door
                local r = route(t)
                r.to_etceteria("goto-goUpToSigridToFinish")
                r.etc_in("finish")
                r.etc_up("goUpToSigridToFinish")
                level_is("goUpToSigridToFinish-level", 1)
                r.sigrid_in("finish")
                -- etc is still 10 here (the guide has no step for the report), so the first talk runs royal_sigrid_reported
                -- (option page royal_royals.rs2:381, answered "I suppose so...") and ends at etc 20; the second talk is the reward (royal_sigrid_reward)
                local function sigrid_talk(label)
                    local r, d = t.player.talk_to("misc_queen_sigrid", 1)
                    t.ticks(2)
                    local pages = 0
                    for _ = 1, 80 do
                        local kind = t.chat.kind()
                        if kind == "none" then
                            break
                        elseif kind == "options" then
                            t.exec(label .. ".choose", t.chat.choose, "I suppose so...")
                            t.ticks(1)
                        else
                            t.chat.continue_(true)
                            t.ticks(1)
                            pages = pages + 1
                        end
                    end
                    t.ticks(3)
                    return tostring(r) .. " " .. tostring(d):sub(1, 60) .. "; " .. tostring(pages) .. " pages continued; varb2142_royal_etc = " .. tostring(var_now("varb2142_royal_etc"))
                end
                local rep = sigrid_talk("talkToSigridToFinish-report")
                t.check("talkToSigridToFinish-report", var_now("varb2142_royal_etc") == 20, "talk_to misc_queen_sigrid (report) -> " .. rep)
                local rew = sigrid_talk("talkToSigridToFinish")
                t.check("talkToSigridToFinish", var_now("varb2142_royal_etc") == 40, "talk_to misc_queen_sigrid (reward) -> " .. rew)
                t.check("sigrid.items", select(2, t.inv.count("royal_letter")) == 1 and select(2, t.inv.count("royal_box_afterquest")) == 1 and select(2, t.inv.count("coins")) >= 20000, "royal_letter x" .. tostring(select(2, t.inv.count("royal_letter"))) .. ", royal_box_afterquest x" .. tostring(select(2, t.inv.count("royal_box_afterquest"))) .. ", coins x" .. tostring(select(2, t.inv.count("coins"))) .. " (Sigrid: 20,000 coins and a letter)")

                -- goDownFromSigridToFinish
                r.sigrid_out("finish")
                r.etc_down("goDownFromSigridToFinish")
                level_is("goDownFromSigridToFinish-level", 0)
                r.etc_out("finish")

                -- goUpToVargasToFinish: Etceteria to Miscellania castle is overland, then the castle's doors
                r.to_castle("goto-goUpToVargasToFinish")
                r.castle_in("vargasFinish")
                r.to_throne_from_hall("vargasFinish", "goUpToVargasToFinish")
                level_is("goUpToVargasToFinish-level", 1)

                -- talkToVargasToFinish: hand the letter in; the rewards are read against a snapshot taken first
                local _, snap = t.skill.snapshot()
                local tv, tvd = t.player.talk_to("misc_king_vargas", 1)
                t.ticks(2)
                local vp = pump(80)
                t.ticks(4)
                t.check("talkToVargasToFinish", var_now("varb2140_royal_quest") == 30 and (select(2, t.inv.count("royal_letter")) or 0) == 0, "talk_to misc_king_vargas -> " .. tostring(tv) .. " " .. tostring(tvd):sub(1, 60) .. "; " .. tostring(vp) .. " pages continued; varb2140_royal_quest = " .. tostring(var_now("varb2140_royal_quest")) .. ", royal_letter x" .. tostring(select(2, t.inv.count("royal_letter"))))
                t.exec("xp.agility", t.skill.expect_gain, "agility", 5000, snap)
                t.exec("xp.slayer", t.skill.expect_gain, "slayer", 5000, snap)
                t.exec("xp.hitpoints", t.skill.expect_gain, "hitpoints", 5000, snap)
                t.quest.expect_complete()
                -- LEG 8 END
            end,
        },
    },
}
