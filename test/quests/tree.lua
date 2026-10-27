-- Tree Gnome Village -- hand-written against the quest's own scripts
-- (OSRS-Content/osrs-content/server/scripts/quests/quest_tree/,
-- areas/area_gnome/scripts/{king_bolren,commander_montai,elkoy}.rs2) and
-- Quest Helper's TreeGnomeVillage.java step ladder. Tier 2.
--
-- RE-AUTHOR after 08782520b [seam23]: King Bolren's PoG hub now falls
-- through for an unqualified player (OSRS-Content 33c8b2ac6a), so the
-- quest starts from Bolren normally -- no more content_bug block there.
--
-- Door rule (b67 re-drive). Every trip is walked or taken by what a player
-- uses; the static tools (test/quests/orchestrator/matthew-mbp-m4/reports/
-- sample_tools, --root this checkout) read every goto below REACH
-- closed-doors at margins 30/80/160:
-- * Bolren's village (2541,3170) is a 357-tile walled pocket whose only
--   way in is the Loose Railing treegnomelooserailing 2515,3161
--   (gnomevillage_fence.rs2: squeeze one tile north/south). South of it is
--   the maze (elkoy_maze_coord 2515,3159), walked from its entrance 2504,3190
--   (215 tiles, REACH closed-doors at margin 40).
-- * Lumbridge to Kandarin crosses a members' wall on foot, so the run starts
--   with a real Camelot Teleport (Magic is staged 99 for the warlord's
--   fire bolts anyway); Camelot -> the maze entrance is REACH len 541.
-- * Bolren's accept and first-orb pages p_telejump the player out to
--   2504,3191 (king_bolren.rs2:146-156); the later trips in are Elkoy's own
--   "Yes please." telejump to 2515,3159 (elkoy.rs2:89-106, 162-200: the
--   guide's elkoySkip / elkoySkip2), then the railing, then a walk.
-- * The jail door poordoor 2524,3254 is opened in and walked out; the
--   stronghold is entered over the crumbled wall (south side only), the
--   ladder 2503,3252 climbed up and down, and left by
--   khazard_stronghold_door 2502,3250 (a walk-through from inside only,
--   quest_tree_locs.rs2:36-58; fixed in OSRS-Content 973a687d72 to land
--   the player on the door tile outside), then walked to the maze entrance.
-- * Combat-level branches: none in quest_tree/ or area_gnome/ (grepped
--   combat_level), so the staged combat stats change no page.
--
-- Rebuilt 2026-10-27 against RSProx index 4394 (the whole quest, rev 241):
-- Bolren's Start confirm, Elkoy's guide frame and Follow op, the trackers'
-- boxes and the rolled x coordinate, the warlord's dropped orbs.

return {
    id = "tree",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        -- Attack is left at its fresh-character level on purpose: the
        -- quest's own reward is stat_advance(attack, 114500) --
        -- khazard_warlord.rs2:244 -- a FLOOR, not an add (measured run 1:
        -- an already-::setlevel'd 99 Attack reads delta=0, the same
        -- "before==after" a stat_advance no-op always would). Quest
        -- Helper's own combatGear hint is "magic is best" -- fire_bolt vs
        -- this npc's magic=1 (quest_tree.npc) is effectively unmissable,
        -- so the warlord is fought with magic and Attack XP stays
        -- observable for the reward row.
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel magic 99",
        "::give logs 6", -- brought-along prerequisite for Commander Montai (Quest Helper sixLogs)
        "::give rune_full_helm 1", -- combat prerequisite (Quest Helper combatGear)
        "::give rune_chainbody 1", -- rune_platebody needs Dragon Slayer complete (real OSRS mechanic, measured run 1) -- chainbody does not
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give chaosrune 60", -- fire_bolt: 1 chaos + 4 fire + 3 air per cast (magic_combat_spells.dbrow)
        "::give firerune 200",
        "::give airrune 160", -- 150 for fire bolts + one Camelot Teleport's 5 (magic_spells.dbrow [magic_spell_teleport_camelot])
        "::give lawrune 1", -- one Camelot Teleport (airrune 5 + lawrune 1, level 45; Magic is staged 99 above): the start
        "::give shark 6", -- recommended food
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp111_treequest",
            constants = {
                not_started = 0,
                started = 1,
                spoken_montai = 2,
                given_logs_montai = 3,
                finding_trackers = 4,
                ballista_fired = 5,
                retrieved_orb = 6,
                returned_first_orb = 7,
                defeated_warlord = 8,
                complete = 9,
            },
            display = "Tree Gnome Village",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- a setup cheat's effect is not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Gear up before travelling -- ::give is not equip (trap 177). No
        -- weapon: the warlord is fought with magic (see setup banner), and
        -- an equipped weapon's auto-retaliate would land unrelated melee
        -- hits at the untouched Attack level, muddying the fight's own
        -- evidence for no benefit -- the runes carry the damage.
        t.exec("equip.helm", t.player.equip, "rune_full_helm")
        t.exec("equip.body", t.player.equip, "rune_chainbody")
        t.exec("equip.legs", t.player.equip, "rune_platelegs")
        t.exec("equip.shield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- Shared travel pieces (see the banner for the static evidence).
        -- ---------------------------------------------------------------
        local function tile_is(name, want, slack, why)
            local r, v = t.world.tile()
            local ok = r == "ok" and type(v) == "table" and v.level == want[3]
                and math.abs(v.x - want[1]) <= slack and math.abs(v.z - want[2]) <= slack
            t.check(name, ok, why .. ": want " .. want[1] .. "," .. want[2] .. "," .. want[3]
                .. " (within " .. slack .. "), read " .. tostring(r) .. " "
                .. ((type(v) == "table" and v.x) and (v.x .. "," .. v.z .. "," .. v.level) or "?"))
        end
        -- A page a script MAY raise after a telejump (an npc_find-gated line):
        -- played when it comes, written as a note when it does not.
        local function maybe_page(name, line, why)
            local r = t.await({ level = function() return t.chat.kind() ~= "none" end }, 4)
            if r == "ok" then
                t.exec(name, t.chat.play, { line })
            else
                t.note(name .. ": no page in 4 ticks (" .. tostring(r) .. "; " .. why .. ")")
            end
        end
        -- Elkoy's guided walk (RSProx 4394 [300..304], [308..312]): a box with
        -- nothing to click and the fade, the teleport at +3, Elkoy's page at +4.
        local function guided(name, line)
            local r = t.await({ level = function() return t.chat.kind() == "npc" end }, 8)
            t.check(name .. ".page_up", r == "ok", "await Elkoy's page after the guide -> " .. tostring(r)
                .. ", kind now " .. tostring(t.chat.kind()))
            t.exec(name, t.chat.play, { line })
        end
        -- The maze, entrance 2504,3190 to the railing's outer tile 2515,3160:
        -- the 215-tile closed-doors path reach.py's flood finds at margin 40,
        -- one waypoint per corner (hops <= 9).
        local MAZE = {
            { 2504, 3190 }, { 2512, 3190 }, { 2512, 3188 }, { 2521, 3188 }, { 2530, 3188 },
            { 2532, 3188 }, { 2532, 3183 }, { 2529, 3183 }, { 2529, 3181 }, { 2523, 3181 },
            { 2523, 3184 }, { 2520, 3184 }, { 2520, 3179 }, { 2514, 3179 }, { 2514, 3177 },
            { 2523, 3177 }, { 2527, 3177 }, { 2527, 3179 }, { 2529, 3179 }, { 2529, 3177 },
            { 2531, 3177 }, { 2531, 3179 }, { 2533, 3179 }, { 2533, 3177 }, { 2542, 3177 },
            { 2544, 3177 }, { 2544, 3175 }, { 2549, 3175 }, { 2549, 3166 }, { 2549, 3165 },
            { 2545, 3165 }, { 2545, 3159 }, { 2550, 3159 }, { 2550, 3156 }, { 2548, 3156 },
            { 2548, 3147 }, { 2548, 3145 }, { 2539, 3145 }, { 2539, 3150 }, { 2542, 3150 },
            { 2542, 3148 }, { 2544, 3148 }, { 2544, 3150 }, { 2545, 3150 }, { 2545, 3152 },
            { 2544, 3152 }, { 2544, 3155 }, { 2535, 3155 }, { 2534, 3155 }, { 2534, 3156 },
            { 2525, 3156 }, { 2519, 3156 }, { 2519, 3158 }, { 2515, 3158 }, { 2515, 3160 },
        }
        -- Inside the railing to Bolren's clearing (REACH closed-doors len 36).
        local VILLAGE = {
            { 2515, 3161 }, { 2515, 3162 }, { 2516, 3162 }, { 2516, 3164 }, { 2517, 3164 },
            { 2517, 3171 }, { 2526, 3171 }, { 2535, 3171 },
        }
        local function railing_in(name)
            t.exec(name, t.player.cross_trap, { loc = "treegnomelooserailing", op_name = "Squeeze-through",
                at = { 2515, 3161, 0 }, src = { 2515, 3160 }, dest = { 2515, 3161 }, attempts = 2 })
        end
        local function village_walk(name)
            t.exec(name, t.player.walk_route, VILLAGE)
        end
        -- The maze entrance, outside, beside Elkoy: an overland hop between
        -- open tiles of one walking component.
        local ENTRANCE = { 2504, 3190, 0 }

        -- ================= talkToKingBolren (goThroughMaze) =================
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "talkToKingBolren.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        t.exec("goto-mazeEntrance", t.player.goto_tile, ENTRANCE[1], ENTRANCE[2], 0)
        t.exec("goThroughMaze", t.player.walk_route, MAZE)
        railing_in("goThroughMaze.railingIn")
        village_walk("goThroughMaze.toBolren")
        t.exec("bolren.greet", t.player.talk_to, "king_bolren", 1)
        -- king_bolren.rs2 ^tree_not_started: TGV's own opener (seam23: the
        -- PoG hub now falls through for an unqualified player).
        t.exec("bolren.accept", t.chat.play, {
            "player:Hello.",
            "npc:Well hello stranger.",
            "npc:I'm surprised you made it in",
            "player:Maybe.",
            "npc:I'm afraid I have more serious concerns",
            "choose:Can I help at all?",
            "player:Can I help at all?",
            "npc:I'm glad you asked.",
            "npc:The truth is my people are in grave danger.",
            "npc:We are not a violent race",
            "npc:We became desperate",
            "npc:Khazard troops seized the orb.",
            "player:How can I help?",
            "npc:You would be a huge benefit",
            "choose:Yes.",
            "player:I would be glad to help.",
            "npc:Thank you. The battlefield is to the north",
            "npc:That is if he's still alive.",
            "npc:My assistant shall guide you out.",
        })
        t.exec("stage.started", t.var.await_server, "varp111_treequest", 1, 10)
        -- @bolren_leavemaze_initial: p_telejump(entrance - 1 z) = 2504,3191.
        -- RSProx 4394 [300..304]: the guide box, the fade, the teleport, Elkoy.
        guided("bolren.leaveMaze.elkoy", "npc:We're out of the maze now.")
        tile_is("bolren.leaveMaze", { 2504, 3191, 0 }, 0, "Bolren's assistant guided the player out of the maze (king_bolren.rs2:147 p_telejump)")

        t.exec("goto-montai", t.player.goto_tile, 2523, 3207, 0)
        t.exec("montai.talk", t.player.talk_to, "commander_montai", 1)
        t.exec("montai.accept", t.chat.play, {
            "player:Hello.",
            "npc:Hello traveller, are you here to help",
            "player:I've been sent by King Bolren",
            "npc:Excellent we need all the help",
            "npc:I'm commander Montai.",
            "player:What can I do?",
            "npc:Firstly we need to strengthen",
            "choose:Ok, I'll gather some wood.",
            "player:Ok, I'll gather some wood.",
            "npc:Please be as quick as you can",
        })
        t.exec("stage.spoken_montai", t.var.await_server, "varp111_treequest", 2, 10)
        t.exec("montai.logs_talk", t.player.talk_to, "commander_montai", 1)
        t.exec("montai.logs", t.chat.play, {
            "player:Hello.",
            "npc:Hello again, we're still desperate for wood soldier.",
            "player:I have some here.",
            "npc:That's excellent",
        })
        t.exec("stage.given_logs", t.var.await_server, "varp111_treequest", 3, 10)
        t.exec("logs.gone", t.inv.expect_absent, "logs")
        t.exec("montai.trackers_talk", t.player.talk_to, "commander_montai", 1)
        t.exec("montai.trackers", t.chat.play, {
            "player:How are you doing Montai?",
            "npc:We're hanging in there soldier.",
            "npc:The ballista can break through",
            "player:So what's the problem?",
            "npc:From this distance",
            "player:Have they returned?",
            "npc:I'm afraid not",
            "npc:Do you think you can do it?",
            "choose:I'll try my best.",
            "player:I'll try my best.",
            "npc:Thank you, you're braver than most.",
            "npc:I don't know how long",
            "npc:If you can retrieve the orb",
        })
        t.exec("stage.finding_trackers", t.var.await_server, "varp111_treequest", 4, 10)

        -- Trackers (Quest Helper firstTracker/secondTracker/thirdTracker).
        t.exec("goto-tracker1", t.player.goto_tile, 2501, 3260, 0)
        t.exec("tracker1.talk", t.player.talk_to, "tracker1", 1)
        t.exec("tracker1.height", t.chat.play, {
            "player:Do you know the coordinates",
            "npc:I managed to get one",
            "mesbox:The gnome tells you the",
            "player:Well done.",
            "npc:The other two tracker gnomes",
            "player:OK, take care.",
        })
        -- secondTracker: "inside the jail". The jail is an 8-tile room
        -- (2522-2526 x 3255-3256) behind poordoor 2524,3254 (its wall on the
        -- tile's north edge): opened going in, walked out through it.
        t.exec("goto-tracker2", t.player.goto_tile, 2524, 3253, 0)
        t.exec("secondTracker.jailDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2524, 3254, 0 }, near = { 2524, 3253 }, far = { 2524, 3255 } })
        -- He wanders his barred cell (2522-2526 x 3257-3258, RSProx 4394 too)
        -- and is spoken to across the bars from two tiles ([1178]: the
        -- player on 2524,3255, he on 2526,3257); from 2524,3256 every cell
        -- tile is within two.
        t.exec("walk-tracker2", t.player.walk_to, 2524, 3256)
        t.exec("tracker2.talk", t.player.talk_to, "tracker2", 1)
        t.exec("tracker2.y", t.chat.play, {
            "player:Are you OK?",
            "npc:They caught me spying",
            "npc:But I didn't crack.",
            "player:I'm sorry little man.",
            "npc:Don't be. I have the position",
            "mesbox:The gnome tells you the",
            "player:Well done.",
            "npc:Now leave before they find you",
            "player:Hang in there.",
            "npc:Go!",
        })
        t.exec("secondTracker.jailDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2524, 3254, 0 }, near = { 2524, 3255 }, far = { 2524, 3253 } })
        t.exec("goto-tracker3", t.player.goto_tile, 2497, 3233, 0)
        -- The x coordinate was rolled with stage 4 (varbit ballista, x =
        -- ballista + 1); tracker 3's riddle names it (RSProx 4394 [1218]: 2,
        -- "More than we, less than our feet."; the others the wiki's).
        local _, roll = t.var.server("varb602_ballista")
        roll = tonumber(roll) or 0
        local RIDDLE = { [0] = "Less than my hands.", [1] = "More than my head, less than my fingers.",
            [2] = "More than we, less than our feet.", [3] = "My legs and your legs, ha ha ha!" }
        t.exec("tracker3.talk", t.player.talk_to, "tracker3", 1)
        t.exec("tracker3.riddle", t.chat.play, {
            "player:Are you OK?",
            "npc:OK? Who's OK?",
            "player:What's wrong?",
            "npc:You can't see me",
            "player:What do you mean?",
            "npc:They're dancing",
            "mesbox:He's clearly lost the plot.",
            "player:Do you have the coordinate",
            "npc:Who holds the stronghold?",
            "player:What?",
            "npc:" .. RIDDLE[roll],
            "player:You're mad.",
            "npc:Dance with me, and Khazard's men are beat.",
            "mesbox:The toll of war",
            "player:I'll pray for you little man.",
            "npc:All day we pray in the hay",
        })

        -- Ballista (RSProx 4394 [1249..1275]): the height and y are entered
        -- for you; a wrong x first (a miss and the player's line), then the
        -- rolled one.
        local wrong = (roll + 1) % 4 + 1
        t.exec("goto-ballista", t.player.goto_tile, 2509, 3209, 0)
        t.exec("ballista.miss", t.player.click_loc, "catabow", 2)
        t.exec("ballista.miss.coords", t.chat.play, {
            "player:That tracker gnome was a bit vague",
            "choose:000" .. wrong,
            "mesbox:You enter the height and y coordinates",
        })
        t.exec("ballista.miss.page", t.chat.play, {
            "mesbox:The huge spear completely misses",
            "player:Evidently that wasn't the right x coordinate.",
        })
        t.exec("ballista.fire", t.player.click_loc, "catabow", 2)
        t.exec("ballista.coords", t.chat.play, {
            "player:That tracker gnome was a bit vague",
            "choose:000" .. (roll + 1),
            "mesbox:You enter the height and y coordinates",
        })
        t.exec("ballista.hit", t.chat.play, { "mesbox:screams down directly" })
        t.exec("stage.ballista_fired", t.var.await_server, "varp111_treequest", 5, 10)

        -- cRetrieveOrb: "Enter the tower by the Crumbled wall and climb the
        -- ladder". quest_tree_locs.rs2's [oploc1,khazzacklowwall] accepts only
        -- the south side ("You can't get over the wall from this side." when
        -- coordz(coord) > loc's), forcewalks to 2509,3252, climbs 2 north and
        -- p_teleports one more.
        t.exec("goto-wall", t.player.goto_tile, 2509, 3251, 0)
        t.exec("wall.climb", t.player.click_loc, "khazzacklowwall", 1)
        t.exec("wall.mesbox", t.chat.play, { "mesbox:reduced to rubble" })
        t.ticks(3) -- the forced climb animation (forcewalk2 + agility_exactmove) runs before the landing teleport
        local wall_tile_r, wall_tile_v = t.world.tile()
        t.check("wall.crossed", wall_tile_r == "ok" and wall_tile_v and wall_tile_v.z > 3253,
            "tile after the climb: " .. tostring(wall_tile_r) .. " " ..
            (wall_tile_v and (wall_tile_v.x .. "," .. wall_tile_v.z .. "," .. wall_tile_v.level) or "?"))
        -- climbTheLadder (2503,3252,0): the plain `ladder` op1 is
        -- ~climb_ladder(1) (zogre_finish.rs2:354, ladders.rs2:137) -- one plane
        -- up on the tile the player stands on, so stand on 2503,3253 (inside,
        -- REACH closed-doors len 10 from the wall landing).
        t.exec("walk-climbTheLadder", t.player.walk_to, 2503, 3253)
        t.exec("climbTheLadder", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
            at = { 2503, 3252, 0 }, src = { 2503, 3253 }, dest = { 2503, 3253, 1 }, slack = 1 })
        t.exec("chest.open", t.player.click_loc, "chestclosed_khazard", 1)
        t.ticks(2)
        t.exec("chest.search", t.player.click_loc, "chestopen_khazard", 1)
        t.exec("chest.orb_page", t.chat.play, { "mesbox:Inside you find the gnomes' stolen orb" })
        t.exec("orb.held", t.inv.await, "orb_of_protection", 1, 10)
        t.exec("stage.retrieved_orb", t.var.await_server, "varp111_treequest", 6, 10)
        -- Out: the laddertop down, then the stronghold's front door.
        -- [oploc1,khazard_stronghold_door] (quest_tree_locs.rs2:36-58), pressed
        -- from inside (coordz > the door's), passes ~check_axis(coord,
        -- loc_coord, loc_angle) as $entering, as LostCity quest_tree.rs2:32-36
        -- does: from inside $entering is false, so $dest stays on the door tile
        -- 2502,3250, outside (LostCity open_and_close_doors.rs2:20-35; fixed in
        -- OSRS-Content 973a687d72 -- before it the press ran the entering half
        -- and put the player back on 2502,3251). From the south the door
        -- refuses ("The door is locked from the inside."), and the crumbled
        -- wall refuses from the north, so this door is the only way out.
        t.exec("retrieveOrb.ladderDown", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 2503, 3252, 1 }, src = { 2503, 3253 }, dest = { 2503, 3253, 0 }, slack = 1 })
        t.exec("retrieveOrb.strongholdDoorOut", t.player.cross_gate, { loc = "khazard_stronghold_door",
            at = { 2502, 3250, 0 }, near = { 2502, 3251 },
            far_ok = function(tile) return tile.z <= 3250 end,
            far_desc = "outside the stronghold, on or south of its door (z <= 3250)" })
        t.exec("walk-retrieveOrb.leaveStronghold", t.player.walk_to, 2502, 3249)
        -- The commanders' wire shape, as RSProx 4394 recorded it ([1294..1347];
        -- docs/rsprox-analysis/quests/tree_gnome_village.md, "The Khazard
        -- commanders"). Each takes the player by his own hunt, a held target
        -- and never a script's mode (quest_tree.npc, khazard_commander.rs2):
        -- on the first npc turn the player is within his huntrange of 2 --
        -- the ground one the turn after the crawl C ([1310]), the first-floor
        -- one before any chest ([1320]), at the chest's "Oi!" at the latest.
        -- Each swing is human_spear_spike (428) with staff_stab (2562) on its
        -- tick, 4+ ticks after the last (5/5). The ground one lets go on the
        -- first turn the player is past his maxrange-4 leash from 2509,3254
        -- ([1316], the player 6 west). At C+2 the player steps on to 2509,3255
        -- with "Don't even think of taking the orb!" ([1311]).
        do
            local why, ok = {}, true
            local function rows(o)
                local r, v = t.ticklog.rows(o)
                return (r == "ok" and v) or {}
            end
            local function fail(msg) ok = false; why[#why + 1] = msg end
            local slot_floor = {}
            for _, sp in ipairs(rows({ kind = "npc_spawn", type = 4972 })) do
                slot_floor[sp.slot] = sp.level
            end
            local C, U, D, X
            for _, row in ipairs(rows({ kind = "player_anim", seq = 840 })) do C = row.tick end
            local ptile = {}
            for _, row in ipairs(rows({ kind = "player_tile" })) do ptile[row.tick] = row end
            if C ~= nil then
                for tick = C + 1, C + 200 do
                    local p = ptile[tick]
                    if U == nil and p and p.level == 1 then U = tick end
                    if U ~= nil and D == nil and p and p.level == 0 and tick > U then D = tick end
                end
            end
            local say = rows({ kind = "npc_say", type = 4972, text = "Oi! You! Get out of there." })
            X = say[1] and say[1].tick or nil
            if C == nil or U == nil or D == nil or X == nil then
                fail(string.format("anchors: crawl %s, first floor %s, down %s, Oi %s",
                    tostring(C), tostring(U), tostring(D), tostring(X)))
            else
                -- Where each commander stood at the end of each tick: its spawn
                -- row, then every npc_tile row.
                local moves = {}
                for slot in pairs(slot_floor) do
                    moves[slot] = rows({ kind = { "npc_spawn", "npc_tile" }, slot = slot })
                end
                local function npc_at(slot, tick)
                    local at = nil
                    for _, row in ipairs(moves[slot]) do
                        if row.tick <= tick then at = row end
                    end
                    return at
                end
                local function near(slot, tick, range)
                    local n, p = npc_at(slot, tick), ptile[tick]
                    return n and p and n.level == p.level
                        and math.max(math.abs(n.x - p.x), math.abs(n.z - p.z)) <= range
                end
                local synth = {}
                for _, row in ipairs(rows({ kind = "sound", sound = 2562 })) do synth[row.tick] = true end
                for slot, floor in pairs(slot_floor) do
                    local name = floor == 0 and "ground" or "first floor"
                    local from, to = (floor == 0) and C or U, (floor == 0) and U or D
                    -- Taken: his face on the player (npc_face z = 32768 + pid);
                    -- let go: the face reset (z = -1). A held target keeps the
                    -- face through every swing; the one-swing opplayer2 errand
                    -- reset it after the swing.
                    local faces = rows({ kind = "npc_face", slot = slot,
                        where = function(r) return r.x == -1 and r.tick > from - 30 and r.tick <= to + 5 end })
                    local taken, gone, early = nil, nil, nil
                    for _, row in ipairs(faces) do
                        if row.z >= 32768 and row.tick <= from and early == nil then early = row.tick end
                        if row.z >= 32768 and row.tick > from and taken == nil then taken = row.tick end
                        if row.z == -1 and taken ~= nil and gone == nil then gone = row.tick end
                    end
                    if floor == 1 and gone ~= nil and gone < D then
                        fail(string.format("first floor: let go at %d with the player still upstairs (down at %d)", gone, D))
                    end
                    -- The first turn the player is in range (his tile and the
                    -- player's at the end of the turn before).
                    local due = nil
                    for tick = from + 1, (floor == 0) and to or X do
                        if due == nil and near(slot, tick - 1, 2) then due = tick end
                    end
                    if floor == 1 and due == nil then due = X end
                    -- Upstairs a cannon or a pile of cannonballs can stand in
                    -- the sight line of a 2-tile diagonal (the hunt's
                    -- lineofsight), so there it is "not before he could, and
                    -- by the Oi! at the latest"; the ground floor's landing
                    -- tile is in plain sight of his whole room.
                    local late_ok = floor == 1 and taken ~= nil and taken >= due and taken <= X
                    if not late_ok and (taken == nil or taken ~= due) then
                        fail(string.format("%s: taken at %s, due at %s (C %d, U %d, Oi %d)", name,
                            tostring(taken), tostring(due), C, U, X))
                    end
                    if floor == 0 then
                        -- Not through the outer wall ([938..950]): the player
                        -- outside within 2 tiles before the crawl is no hunt.
                        local outside = 0
                        for tick = C - 30, C - 1 do
                            if near(slot, tick, 2) then outside = outside + 1 end
                        end
                        if early ~= nil then
                            fail(string.format("ground: taken through the wall at %d", early))
                        end
                        why[#why + 1] = string.format("ground: %d tick(s) within 2 outside the wall, not taken", outside)
                    end
                    local swings = rows({ kind = "npc_anim", slot = slot, seq = 428,
                        where = function(r) return r.tick >= from and r.tick <= to + 2 end })
                    local silent, short = 0, 0
                    for i, row in ipairs(swings) do
                        if not synth[row.tick] then silent = silent + 1 end
                        if i > 1 and row.tick - swings[i - 1].tick < 4 then short = short + 1 end
                    end
                    if #swings == 0 or silent > 0 or short > 0 then
                        fail(string.format("%s: %d swing(s), %d without staff_stab, %d under 4 apart",
                            name, #swings, silent, short))
                    end
                    why[#why + 1] = string.format("%s: taken at +%s, swings at %s", name,
                        tostring(taken and taken - from), table.concat((function()
                            local out = {}
                            for _, row in ipairs(swings) do out[#out + 1] = "+" .. (row.tick - from) end
                            return out
                        end)(), " "))
                    if floor == 0 and taken ~= nil then
                        -- maxrange 4: past 5 on an axis from the spawn, or 5 on both.
                        local leash = nil
                        for tick = taken + 1, U do
                            local p = ptile[tick - 1]
                            if leash == nil and p then
                                local dx, dz = math.abs(p.x - 2509), math.abs(p.z - 3254)
                                if p.level ~= 0 or dx > 5 or dz > 5 or (dx == 5 and dz == 5) then leash = tick end
                            end
                        end
                        if gone == nil or gone ~= leash then
                            fail(string.format("ground: let go at %s, the leash at %s", tostring(gone), tostring(leash)))
                        end
                    end
                end
                local orb = rows({ kind = "npc_say", type = 4972, text = "Don't even think of taking the orb!" })
                local p2 = ptile[C + 2]
                if not (orb[1] and orb[1].tick == C + 2 and p2 and p2.x == 2509 and p2.z == 3255) then
                    fail(string.format("C+2: the line at %s, the player on %s", tostring(orb[1] and orb[1].tick - C),
                        p2 and (p2.x .. "," .. p2.z) or "?"))
                end
            end
            t.check("commander.wire", ok, table.concat(why, "; "))
        end

        -- returnFirstOrb via elkoySkip: Elkoy outside the maze, "Yes please."
        -- -> p_telejump(^elkoy_maze_coord) 2515,3159 (elkoy.rs2:89-106).
        t.exec("goto-elkoySkip", t.player.goto_tile, ENTRANCE[1], ENTRANCE[2], 0)
        -- Follow (op 3), as the recording does ([1375..1380]).
        t.exec("elkoySkip", t.player.talk_to, "elkoy", 3)
        guided("elkoySkip.dialog", "npc:Here we are. Take the orb to King Bolren")
        tile_is("elkoySkip.landed", { 2515, 3159, 0 }, 0, "Elkoy's telejump to the maze's inner end (^elkoy_maze_coord 0_39_49_19_23)")
        t.exec("walk-returnFirstOrb.railing", t.player.walk_to, 2515, 3160)
        railing_in("returnFirstOrb.railingIn")
        village_walk("returnFirstOrb.toBolren")
        t.exec("bolren.orb_talk", t.player.talk_to, "king_bolren", 1)
        t.exec("bolren.first_orb", t.chat.play, {
            "player:I have the orb.",
            "npc:Oh my... The misery",
            "player:King Bolren, are you OK?",
            "npc:Thank you traveller, but it's too late.",
            "player:What happened?",
            "npc:They came in the night.",
            "player:Who?",
            "npc:Khazard troops.",
            "player:I'm sorry.",
            "npc:They took the other orbs",
            "player:Where did they take them?",
            "npc:They headed north of the stronghold.",
            "choose:I will find the warlord and bring back the orbs.",
            "player:I will find the warlord",
            "npc:You are brave",
            "npc:I will safeguard this orb",
        })
        t.exec("stage.returned_first_orb", t.var.await_server, "varp111_treequest", 7, 10)
        t.exec("orb.handed_in", t.inv.expect_absent, "orb_of_protection")
        -- @bolren_leavemaze_second: p_telejump to 2504,3191 again.
        guided("bolren.leaveMaze2.elkoy", "npc:Good luck friend.")
        tile_is("bolren.leaveMaze2", { 2504, 3191, 0 }, 0, "Bolren's assistant guided the player out again (king_bolren.rs2:153 p_telejump)")

        -- The warlord, west of West Ardougne's wall (ardoungewall x 2459-2460):
        -- an open tile beside him, REACH closed-doors from the maze entrance.
        t.exec("goto-warlord", t.player.goto_tile, 2457, 3300, 0)
        t.exec("warlord.talk", t.player.talk_to, "khazard_warlord", 1)
        t.exec("warlord.dialog", t.chat.play, {
            "player:You there, stop!",
            "npc:Go back to your pesky little green friends.",
            "player:I've come for the orbs.",
            "npc:You're out of your depth traveller.",
            "player:They're stolen goods,",
            "npc:Ha, you really think you stand a chance?",
        })
        -- Magic (Quest Helper's combatGear hint: "magic is best"), not
        -- melee -- fire_bolt (level 35, magic_combat_spells.dbrow) against
        -- this npc's magic=1 (quest_tree.npc:50, 170 hp) lands almost every
        -- cast. The casts between the first and the kill wait are attempts
        -- (a note), the kill and the margin are the graded outcome. Hitpoints
        -- are sampled before every cast and a shark eaten under 60.
        local fight = { low = nil, casts = 1 }
        local function sample_hp()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if fight.low == nil or hp.level < fight.low then fight.low = hp.level end
                if hp.level < 60 then t.player.inv_op("shark", 1) end
            end
        end
        sample_hp()
        t.exec("fightTheWarlord", t.player.cast, "fire_bolt", "khazard_warlord_combat", 14)
        while fight.casts < 40 and t.npc.nearest("khazard_warlord_combat", 20) == "ok" do
            sample_hp()
            t.player.cast("fire_bolt", "khazard_warlord_combat", 14)
            fight.casts = fight.casts + 1
        end
        t.note("fightTheWarlord: cast fire_bolt " .. fight.casts .. " time(s) before the kill wait")
        local dead_r, dead_d = t.npc.await_dead_engaged(400, 6, { eat = { item = "shark", below = 60 } })
        t.step("warlord.dead", dead_r == "ok" and "PASS" or "FAIL", tostring(dead_r) .. " " .. tostring(dead_d))
        local wait_low = tonumber(string.match(tostring(dead_d), "lowest hp (%d+)/") or "")
        if wait_low ~= nil and (fight.low == nil or wait_low < fight.low) then fight.low = wait_low end
        sample_hp()
        local food_r, food = t.inv.count("shark")
        t.check("fightTheWarlord.margin", fight.low ~= nil and fight.low >= 25 and food_r == "ok" and (food or 0) >= 1,
            "Khazard warlord (level 112, 170 hp): " .. fight.casts .. " fire bolt(s), lowest hp " .. tostring(fight.low)
                .. "/99, sharks left " .. tostring(food) .. " of 6 staged (" .. tostring(food_r)
                .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        -- RSProx 4394 [1880]: the orbs land on his tile for the player alone,
        -- with an objbox; picked up by hand ([1887]).
        t.exec("warlord.orbs_page", t.chat.play, { "objbox:As the warlord falls to the ground" })
        t.exec("stage.defeated_warlord", t.var.await_server, "varp111_treequest", 8, 10)
        -- The fight's shape as RSProx 4394 recorded it ([1520..1880];
        -- docs/rsprox-analysis/quests/tree_gnome_village.md, "The Khazard
        -- warlord"): his first swing two ticks after his add, then every 4
        -- (88/88), each human_blunt_pound (401) with warhammer_crush (2567);
        -- human_death (836) on the killing hit and his del two ticks later,
        -- the ghostly vapour (smokepuff 86) on that tick.
        do
            local why = {}
            local ok = true
            local r, spawns = t.ticklog.rows({ kind = "npc_spawn", type = 7622 })
            local sp = (r == "ok") and spawns[#spawns] or nil
            if sp == nil then
                ok = false
                why[#why + 1] = "no npc_spawn row for khazard_warlord_combat (" .. tostring(r) .. ")"
            else
                local _, swings = t.ticklog.rows({ kind = "npc_anim", slot = sp.slot, seq = 401, since = sp.serial })
                local _, deaths = t.ticklog.rows({ kind = "npc_anim", slot = sp.slot, seq = 836, since = sp.serial })
                local _, blows = t.ticklog.rows({ kind = "npc_death", slot = sp.slot, type = 7622, since = sp.serial })
                local _, frees = t.ticklog.rows({ kind = "npc_free", slot = sp.slot, type = 7622, since = sp.serial })
                local _, crush = t.ticklog.rows({ kind = "sound", sound = 2567, since = sp.serial })
                local _, puffs = t.ticklog.rows({ kind = "map_spotanim", spotanim = 86, since = sp.serial })
                local crush_at = {}
                for _, row in ipairs(crush or {}) do crush_at[row.tick] = true end
                local bad_gap, silent = 0, 0
                for i, row in ipairs(swings or {}) do
                    if i > 1 and row.tick - swings[i - 1].tick ~= 4 then bad_gap = bad_gap + 1 end
                    if not crush_at[row.tick] then silent = silent + 1 end
                end
                local first = (swings and swings[1]) and (swings[1].tick - sp.tick) or nil
                local k = (deaths and deaths[1]) and deaths[1].tick or nil
                local blow = (blows and blows[1]) and blows[1].tick or nil
                local d = (frees and frees[1]) and frees[1].tick or nil
                local puff = false
                for _, row in ipairs(puffs or {}) do if row.tick == d then puff = true end end
                ok = first == 2 and bad_gap == 0 and silent == 0 and #(swings or {}) > 1
                    and k ~= nil and k == blow and d ~= nil and d - k == 2 and puff
                why[#why + 1] = string.format("add tick %d, %d swing(s) (401), first at add+%s, %d gap(s) not 4,"
                    .. " %d without warhammer_crush; killing blow at %s, death seq 836 at %s (want the blow's tick),"
                    .. " del at %s (want death+2), smokepuff on the del: %s",
                    sp.tick, #(swings or {}), tostring(first), bad_gap, silent, tostring(blow), tostring(k),
                    tostring(d), tostring(puff))
            end
            t.check("warlord.wire", ok, table.concat(why, "; "))
        end
        t.exec("pickupOrb", t.player.click_obj, "orbs_of_protection")
        t.exec("orbs.held", t.inv.await, "orbs_of_protection", 1, 10)

        -- The reward goes out on the last page's click (RSProx 4394 [2134]).
        local snap_r, snap_v = t.skill.snapshot()
        t.check("reward.snapshot", snap_r == "ok", "skill.snapshot before the hand-in dialogue -> " .. tostring(snap_r))
        local amulet_before_r, amulet_before = t.inv.count("gnome_amulet")

        -- returnOrbs via elkoySkip2: Elkoy's hero page offers the way in
        -- again (elkoy.rs2:162-200, ^elkoy_maze_coord).
        t.exec("goto-elkoySkip2", t.player.goto_tile, ENTRANCE[1], ENTRANCE[2], 0)
        t.exec("elkoySkip2", t.player.talk_to, "elkoy", 3)
        guided("elkoySkip2.dialog", "npc:Here we are. Feel free to have a look around.")
        tile_is("elkoySkip2.landed", { 2515, 3159, 0 }, 0, "Elkoy's telejump to the maze's inner end (^elkoy_maze_coord 0_39_49_19_23)")
        t.exec("walk-returnOrbs.railing", t.player.walk_to, 2515, 3160)
        railing_in("returnOrbs.railingIn")
        village_walk("returnOrbs.toBolren")
        t.exec("bolren.orbs_talk", t.player.talk_to, "king_bolren", 1)
        t.exec("bolren.orbs", t.chat.play, {
            "player:Bolren, I have returned.",
            "npc:You made it back!",
            "player:I have them here.",
            "npc:Hooray, you're amazing.",
            "npc:Once the orbs are replaced",
            "player:What does the ceremony involve?",
            "npc:The spirit tree has looked over us",
            "mesbox:The gnomes begin to chant.",
        })
        local rest_r = t.await({ level = function() return t.chat.kind() == "mesbox" end }, 40)
        t.check("ceremony.rest_page", rest_r == "ok", "await chat.kind()==mesbox across the chant cutscene -> " .. tostring(rest_r) .. ", kind now " .. tostring(t.chat.kind()))
        t.exec("ceremony.end", t.chat.play, {
            "mesbox:The orbs of protection come to rest",
            "npc:Now at last my people are safe",
            "player:I'm pleased I could help.",
            "npc:You are modest brave traveller.",
            "npc:Please, for your efforts take this amulet.",
            "player:Thank you King Bolren.",
            "npc:The tree has many other powers",
        })
        t.exec("stage.complete", t.var.await_server, "varp111_treequest", 9, 10)
        t.exec("orbs.handed_in", t.inv.expect_absent, "orbs_of_protection")
        t.exec("reward.amulet", t.inv.await, "gnome_amulet", 1, 10)
        local amulet_after_r, amulet_after = t.inv.count("gnome_amulet")
        t.check("reward.amulet_delta", amulet_before_r == "ok" and amulet_after_r == "ok"
            and amulet_before == 0 and amulet_after == 1,
            "gnome_amulet " .. tostring(amulet_before) .. " -> " .. tostring(amulet_after)
                .. " (literal reward: one Gnome amulet, king_bolren.rs2 [queue,tree_quest_complete])")
        t.ticks(3)

        t.quest.expect_complete()
        t.exec("reward.attack_xp", t.skill.expect_gain, "attack", 11450, snap_v)
        t.finish(0)
    end,
}
