-- Vampyre Slayer. Rewritten from tools/quest_gate/new_quest.py's scaffold
-- against the quest's own .rs2 (OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_vampire/, areas/draynor/scripts/morgan.rs2,
-- areas/varrock/scripts/harlow.rs2, areas/varrock/scripts/bartender.rs2,
-- areas/draynor/scripts/garlic_cupboard.rs2, doors/scripts/doors.rs2).
-- Re-driven in b60 for the door rule (docs/QUEST_ORCHESTRATOR.md, owner
-- 2026-10-03): no goto_tile lands in or leaves a closed space; every door
-- and staircase on the route is pressed on every visit.
--
-- Route, walked:
--   * Morgan's house (3096-3102,3266-3270): poordoor 3098,3270 (north wall)
--     in and out with t.player.pass_door; its staircase by click both ways
--     -- `stairs` 3099,3266,0 from the maplink src 3098,3267 lands
--     3102,3266,1, `stairstop` 3100,3266,1 from 3102,3266 lands 3098,3266,0
--     (ladders_stairs/configs/maplink.dbrow maplink_0_48_51_26_3_up /
--     maplink_1_48_51_30_2_down). The garlic cupboard (3096,3269,1) has no
--     state guard, so the garlic is taken on the first visit.
--   * The Blue Moon Inn: entered and left through its west doorway, whose
--     fai_varrock_door stands open in the map (fai_varrock_door_open
--     3216,3395) -- pass_door grades the open leaf and never presses it.
--     The bartender is talked to across his bar from the customer side
--     (no goto behind the counter).
--   * Draynor Manor: the front doors (haunteddoorl, quest_haunted.rs2:29
--     [label,open_manor_entrance], a walk-through: the press carries the
--     player in and the doors "slam shut"), then the two draynor_panelled_
--     doors 3109,3358 and 3106,3368 to the east wing, then the crypt stairs
--     (cryptstairsdown 3115,3357) by click.
--
-- CONTENT GAP at the crypt stairs (b60, settled from LostCity): this pack's
-- cryptstairsdown has only `category=climb_down`
-- (ladders_stairs/configs/ladders.loc:1863-1864), no maplink.dbrow row and no
-- per-symbol script, so a press runs [oploc1,_climb_down] ~climb(-1)
-- (ladders.rs2:178) -> level 0 - 1 < 0 -> ~blocked_message "You can't go any
-- further." (ladders.rs2:73-76, player/messages.rs2:71-72). LostCity has the
-- real destination: LostCity_Server content/scripts/ladders+stairs/scripts/
-- stairs.rs2:408-417 [oploc1,cryptstairsdown] at 0_48_52_43_29 telejumps the
-- player to 0_48_152_5_43 (3077,9771,0), and cryptstairsup 0_48_152_5_40
-- back to 0_48_52_43_28 (stairs.rs2:419-424; this pack's cryptstairsup,
-- ladders.loc:197-198, is the same bare climb_up). The old goto into the
-- crypt was the cheat the door rule forbids, so the run stops at an honest
-- content_bug when the stairs do not land in the crypt; when content gives
-- them their destination the fight below runs unchanged.
--
-- Combat: count_draynor spawns off quest_vampire.npc's authored block
-- (hitpoints=35 attack=30 strength=25 defence=30, wiki level 34) and is
-- weakened by garlic AT SPAWN (npc_statsub -10/-10/-10/-40, clamped) since
-- garlic is already in the pack before the coffin is first opened. The
-- kill is finished by count_draynor.rs2's [ai_queue3] -- stake+hammer both
-- required in the pack; the stake is consumed, the hammer is not. The
-- completion is QUEUED three ticks after the "You hammer the stake..."
-- line, so this waits for that line and then polls %vampire server-side.

return {
    id = "vampire",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99", -- combatGear is brought along, not gathered -- a fresh level-3 character cannot wear rune or safely trade hits with a level-34 aggressive npc
        "::give hammer 1", -- getItemRequirements(): brought along, no obtain step in the guide
        "::give coins 10", -- beerOrTwoCoins: brought-along currency for the live buyBeer step (price 2)
        "::give rune_scimitar 1", -- combatGear: brought along, worn in run()
        "::give adamant_platebody 1", -- NOT rune platebody: F2P rune platebody is gated on Dragon Slayer being complete first (measured run 3)
        "::give adamant_platelegs 1",
        "::give shark 5", -- food, not in the guide's item list but cheap insurance against a level-34 aggressive npc
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp178_vampire",
            constants = {
                not_started = 0,
                started = 1,
                spoke_to_harlow = 2,
                complete = 3,
            },
            row = "quest_vampyreslayer",
            display = "Vampyre Slayer", -- the quest-list dbrow text (configs/all.dbrow:8572 "values=1:0:Vampyre Slayer")
            journal_title = "Vampire Slayer", -- vampire_journal.rs2's own ~quest_journal("Vampire Slayer", ...) title, spelled differently in this port
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- ===============================================================
        -- Route helpers.
        -- ===============================================================
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        -- A walk graded on the exact tile and level it reached.
        local function walk_check(name, x, z, level)
            local wr, wd = t.player.walk_to(x, z, 30)
            local r, tt = t.world.tile()
            if not (r == "ok" and tt.x == x and tt.z == z) then
                t.ticks(2)
                wr, wd = t.player.walk_to(x, z, 30)
                r, tt = t.world.tile()
            end
            t.check(name, r == "ok" and tt.x == x and tt.z == z and tt.level == level,
                "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. "); at "
                    .. tile_text(r, tt) .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end

        -- A staircase pressed by tile and level, graded on the level change
        -- and the landing (never on the press's answer, which is in the
        -- detail). Returns the landing tile when it matched.
        local function climb(name, sym, at, to_level, landed_ok, landed_desc, refused_name)
            local br, bt = t.world.tile()
            local function arrived()
                local r, tt = t.world.tile()
                return r == "ok" and tt.level == to_level and landed_ok(tt)
            end
            -- At most two presses: a press the camera could not land
            -- (`covered`, run 2's stairstop) leaves the player on the stair
            -- tile and is pressed once more from there; a press the server
            -- answered is never repeated.
            local cr, cd, ar
            local presses = 0
            repeat
                presses = presses + 1
                cr, cd = t.player.click_loc(sym, 1, { at = at })
                ar = t.await({ level = arrived, note = sym .. ": waiting for the landing" }, 10)
            until ar == "ok" or presses >= 2 or (cr ~= "covered" and cr ~= "not_visible")
            local tr, tt = t.world.tile()
            local landed = br == "ok" and bt.level == at[3] and tr == "ok" and tt.level == to_level and landed_ok(tt)
            local _, lines = t.msg.expect("further")
            -- refused_name: a staircase the content may not link yet. When it
            -- did not land, the row is a RECORDING row under its own name
            -- (gaps-combat: the last row before t.blocked() records, it
            -- does not FAIL) and the caller stops at t.blocked.
            local row_name, row_grade = name, landed
            if refused_name ~= nil and not landed then
                row_name, row_grade = refused_name, true
            end
            t.check(row_name, row_grade,
                "from " .. tile_text(br, bt) .. "; click_loc(" .. sym .. ", 1, at " .. at[1] .. "," .. at[2] .. ","
                    .. at[3] .. ") x" .. presses .. " -> " .. tostring(cr) .. " " .. tostring(cd) .. "; landing await -> " .. tostring(ar)
                    .. "; landed " .. tile_text(tr, tt) .. " (want level " .. to_level .. ", " .. landed_desc .. ")"
                    .. "; 'further' line: " .. tostring(lines))
            if landed then
                return tt
            end
            return nil
        end

        local function in_morgan_house(tt)
            return tt.x >= 3096 and tt.x <= 3102 and tt.z >= 3266 and tt.z <= 3270
        end

        -- ---------------------------------------------------- talkToMorgan
        -- The first goto of the run: open ground north of Morgan's house,
        -- outside its door (reach.py 3206,3233 -> 3098,3271: REACH 176).
        t.exec("goto-talkToMorgan", t.player.goto_tile, 3098, 3271, 0)
        t.exec("talkToMorgan.houseDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3098, 3270, 0 }, near = { 3098, 3271 }, far = { 3098, 3269 } })
        t.exec("talkToMorgan", t.player.talk_to, "morgan", 1)
        t.exec("talkToMorgan-dialog", t.chat.play, {
            "npc:Could it be? A bold adventurer! Please, you must help us!",
            "player:What is it? What's the problem?",
            "npc:It's the evil vampyre, Count Draynor!",
            "choose:Yes.",
            "player:Sounds like a job for me. Where should I start?",
            "npc:Oh, thank goodness! I've been hoping this day would come",
            "npc:If you speak to him, I'm sure he'll be able to help.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- --------------------------------------------------- cGetGarlic --
        -- goUpstairsMorgan: the staircase by click from its maplink src.
        walk_check("goUpstairsMorgan.atFoot", 3098, 3267, 0)
        climb("goUpstairsMorgan", "stairs", { 3099, 3266, 0 }, 1,
            function(tt) return tt.x == 3102 and tt.z == 3266 end,
            "the maplink dest 3102,3266,1 (maplink_0_48_51_26_3_up)")
        -- Across the bedroom to the cupboard's front (3096,3269 faces east):
        -- from the stair landing the press had to hunt round the wall
        -- (run 1: a hunted pose answered a chat line and nothing opened).
        walk_check("getGarlic.atCupboard", 3097, 3269, 1)
        t.exec("cupboard.open", t.player.click_loc, "garliccupboardshut", 1)
        t.check("cupboard.opened", t.msg.expect("You open the cupboard"))
        -- loc_change(garliccupboardopen, 500) reaches the client's pool a
        -- tick or two after the line (run 2 pressed before it: no loc 2613).
        local open_result = t.await({
            level = function()
                return t.world.loc_near("garliccupboardopen", 4) == "ok"
            end,
            note = "garliccupboardopen in the client's pool",
        }, 6)
        t.check("cupboard.openLoc", open_result, "garliccupboardopen within 4 tiles: " .. tostring(open_result))
        t.exec("getGarlic", t.player.click_loc, "garliccupboardopen", 1)
        t.exec("garlic.received", t.inv.await, "garlic", 1, 5)

        -- Back down and out of the house, by the same stairs and door.
        walk_check("goDownstairsMorgan.atTop", 3102, 3266, 1)
        climb("goDownstairsMorgan", "stairstop", { 3100, 3266, 1 }, 0,
            function(tt) return tt.x == 3098 and tt.z == 3266 end,
            "the maplink dest 3098,3266,0 (maplink_1_48_51_30_2_down)")
        t.exec("morganHouse.doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3098, 3270, 0 }, near = { 3098, 3269 }, far = { 3098, 3271 } })

        -- ----------------------------------------------------- talkToHarlow
        -- Draynor to Varrock on foot is open ground (reach.py 3098,3271 ->
        -- 3213,3410: REACH 334, no door or gate): travel. Land in the street
        -- west of the Blue Moon Inn and walk in through its doorway.
        t.exec("goto-talkToHarlow", t.player.goto_tile, 3212, 3398, 0)
        t.exec("talkToHarlow.innDoorIn", t.player.pass_door, { closed = "fai_varrock_door",
            open = "fai_varrock_door_open", at = { 3215, 3395, 0 }, near = { 3215, 3395 }, far = { 3217, 3396 } })
        t.exec("talkToHarlow", t.player.talk_to, "dr_harlow", 1)
        t.exec("talkToHarlow-dialog", t.chat.play, {
            "npc:Buy me a drink pleassh",
            "choose:I need your help dealing with a vampyre.",
            "player:I need your help dealing with a vampyre.",
            "npc:A vampyre you shhay",
            "player:Not just any vampyre. Count Draynor.",
            "npc:Draynor? Well, buy me a beer firsht",
            "player:Are you sure you've not had enough?",
            "npc:Huh? No, I don't think ssho. Now, buy ush a beer.",
        })
        t.expect("quest.stage.spoke_to_harlow", t.quest.expect_stage("spoke_to_harlow"))

        -- --------------------------------------------------------- buyBeer
        -- From the customer side of the bar (3224,3398), never behind it.
        walk_check("buyBeer.atBar", 3224, 3398, 0)
        local coins_before_result, coins_before = t.inv.count("coins")
        t.exec("buyBeer", t.player.talk_to, "bluemoon_bartender", 1)
        t.exec("buyBeer-dialog", t.chat.play, {
            "npc:What can I do yer for?",
            "choose:A glass of your finest ale please.",
            "player:A glass of your finest ale please.",
            "npc:No problemo. That'll be 2 coins.",
        })
        t.exec("beer.received", t.inv.await, "beer", 1, 5)
        local coins_after_result, coins_after = t.inv.count("coins")
        t.check("beer.paid", coins_before_result == "ok" and coins_after_result == "ok"
                and coins_before - coins_after == 2,
            "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (want exactly 2 paid)")

        -- ------------------------------------------------ talkToHarlowAgain
        t.exec("talkToHarlowAgain", t.player.talk_to, "dr_harlow", 1)
        t.exec("talkToHarlowAgain-dialog", t.chat.play, {
            "npc:Buy me a drink pleassh",
            "player:Yes, here you go.",
            "mesbox:You give a beer to Dr Harlow.",
            "npc:Cheersh, matey",
            "player:Now, about Count Draynor",
            "npc:Yesh, Count Draynor! The evil nashty vampyre",
            "npc:You want to havesh a go?",
            "player:So how do I make sure I'm prepared?",
            "npc:Most vampyres regenerate.",
            "mesbox:Dr Harlow hands you a stake.",
            "npc:Takesh that to Draynor Manor",
            "npc:Oh, and yoush should take some garlic",
            "player:Garlic? Hmm",
        })
        t.exec("stake.received", t.inv.await, "stake", 1, 5)
        local beer_left_result, beer_left = t.inv.count("beer")
        t.check("beer.given", beer_left_result == "ok" and beer_left == 0,
            "beer in the pack after the hand-over: " .. tostring(beer_left) .. " (" .. tostring(beer_left_result) .. ", want 0)")
        t.exec("innDoorOut", t.player.pass_door, { closed = "fai_varrock_door",
            open = "fai_varrock_door_open", at = { 3215, 3395, 0 }, near = { 3216, 3395 }, far = { 3214, 3396 } })

        -- --------------------------------------------------- prepareAndKillDraynor
        -- Varrock to the manor's front path is open ground (reach.py
        -- 3213,3410 -> 3108,3351: REACH 312).
        t.exec("goto-enterDraynorManor", t.player.goto_tile, 3108, 3351, 0)
        -- enterDraynorManor: the front doors are a walk-through
        -- (open_manor_entrance refuses from inside: coordz(coord) >
        -- coordz(loc_coord)), pressed from the south.
        t.exec("enterDraynorManor", t.player.pass_door, { closed = "haunteddoorl", open = "haunteddoorl_inactive",
            at = { 3108, 3353, 0 }, near = { 3108, 3352 }, far = { 3108, 3354 } })
        -- goDownToBasement: through the entrance hall's north door, the great
        -- hall's north-east door, round the east wing to the crypt stairs.
        t.exec("goDownToBasement.hallDoor", t.player.pass_door, { closed = "draynor_panelled_door",
            open = "draynor_panelled_door_open", at = { 3109, 3358, 0 }, near = { 3109, 3357 }, far = { 3109, 3359 } })
        t.exec("goDownToBasement.wingDoor", t.player.pass_door, { closed = "draynor_panelled_door",
            open = "draynor_panelled_door_open", at = { 3106, 3368, 0 }, near = { 3106, 3368 }, far = { 3106, 3369 } })
        t.exec("goDownToBasement.toStairs", t.player.walk_route,
            { { 3106, 3369 }, { 3113, 3370 }, { 3114, 3362 }, { 3113, 3356 }, { 3115, 3356 } }, { level = 0 })
        local crypt = climb("goDownToBasement", "cryptstairsdown", { 3115, 3357, 0 }, 0,
            function(tt) return tt.z > 6400 and math.abs(tt.x - 3077) <= 2 and math.abs(tt.z - 9771) <= 2 end,
            "the crypt under the manor within 2 of 3077,9771 (LostCity stairs.rs2:408-417 telejump 0_48_152_5_43)",
            "goDownToBasement.stairsGoNowhere")
        if crypt == nil then
            t.blocked("content_bug: Draynor Manor's crypt stairs go nowhere. cryptstairsdown (3115,3357,0) carries only "
                .. "category=climb_down (OSRS-Content/osrs239-content/server/scripts/ladders_stairs/configs/ladders.loc:1863-1864), "
                .. "has no ladders_stairs/configs/maplink.dbrow row and no [oploc1,cryptstairsdown] script, so the press runs "
                .. "[oploc1,_climb_down] ~climb(-1) (ladders_stairs/scripts/ladders.rs2:178) and level 0-1 < 0 answers "
                .. "~blocked_message 'You can't go any further.' (ladders.rs2:73-76). LostCity_Server "
                .. "content/scripts/ladders+stairs/scripts/stairs.rs2:408-417 telejumps 0_48_52_43_29 -> 0_48_152_5_43 "
                .. "(3077,9771,0) and :419-424 cryptstairsup 0_48_152_5_40 -> 0_48_52_43_28; needed: those two "
                .. "destinations (a maplink row pair or the two scripts). The basement holds the coffin and Count Draynor "
                .. "(quest_vampire.rs2 npc_add 0_48_152_6_46); a goto into it is the cheat the door rule forbids.")
            return
        end

        -- ------------------------------------------------------- openCoffin
        -- The guide's kit for the fight, asserted in the pack before the
        -- coffin: the stake (Dr Harlow's), the hammer (to drive it) and the
        -- garlic (the weaken at spawn).
        for _, need in ipairs({ "stake", "hammer", "garlic" }) do
            local cr, cn = t.inv.count(need)
            t.check("killDraynor.carry." .. need, cr == "ok" and cn == 1,
                need .. " in the pack before the coffin: " .. tostring(cn) .. " (" .. tostring(cr) .. ", want 1)")
        end
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")
        t.exec("equip.body", t.player.equip, "adamant_platebody")
        t.exec("equip.legs", t.player.equip, "adamant_platelegs")

        t.exec("openCoffin", t.player.click_loc, "vampcoffin", 1)
        -- quest_vampire_coffin_open runs p_delay(4) before npc_add + the
        -- garlic-weaken message, so give it the room before reading either.
        local present_result = t.npc.await_present("count_draynor", 10, 15)
        local nearest_result, nearest_row = t.npc.nearest("count_draynor", 10)
        local present_detail
        if nearest_result == "ok" then
            present_detail = string.format(
                "count_draynor slot %s (element %s) at %s,%s,%s",
                tostring(nearest_row.slot), tostring(nearest_row.element_id),
                tostring(nearest_row.x), tostring(nearest_row.z), tostring(nearest_row.level))
        else
            present_detail = "count_draynor await_present=" .. tostring(present_result)
                .. ", nearest lookup=" .. tostring(nearest_result)
        end
        t.check("draynor.present", present_result == "ok" and nearest_result == "ok", present_detail)
        -- The weaken mes() lands in the same tick as npc_add: read it back
        -- from the ring (msg.expect), not msg.await.
        t.check("draynor.weakened", t.msg.expect("weakened by the garlic"))

        -- --------------------------------------------------------- killDraynor
        local sharks_before_result, sharks_before = t.inv.count("shark")
        t.exec("killDraynor", t.player.attack, "count_draynor", 2, 15)
        local dead_result, dead_detail = t.npc.await_dead_engaged(60, 6, { eat = { item = "shark", below = 50 } })
        t.check("killDraynor.dead", dead_result, dead_detail)
        -- Margin: the lowest hitpoints the eater sampled every tick of the
        -- fight at least a quarter of 99 (25) AND food left.
        local lowest = tonumber(string.match(tostring(dead_detail), "lowest hp (%d+)/"))
        local hp_result, hp_now = t.skill.read("hitpoints")
        if hp_result == "ok" and type(hp_now) == "table" and hp_now.level and (lowest == nil or hp_now.level < lowest) then
            lowest = hp_now.level
        end
        local sharks_result, sharks_left = t.inv.count("shark")
        t.check("killDraynor.margin", lowest ~= nil and lowest >= 25 and sharks_result == "ok" and sharks_left >= 1,
            "Count Draynor: lowest hp " .. tostring(lowest) .. "/99 (sampled every tick by the kill wait), sharks "
                .. tostring(sharks_before) .. " (" .. tostring(sharks_before_result) .. ") -> " .. tostring(sharks_left)
                .. " (" .. tostring(sharks_result) .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")

        -- Snapshot AFTER the fight: ordinary combat damage grants its own
        -- Attack xp on top of the quest's flat reward (run 2 of the first
        -- author measured 100 xp of it). Only the queued completion's
        -- stat_advance(attack, 48250) happens after this point.
        local snapshot_result, snapshot = t.skill.snapshot()
        local attack_before = type(snapshot) == "table" and snapshot.attack or nil
        local snapshot_detail
        if type(attack_before) == "table" then
            snapshot_detail = string.format(
                "attack level=%s experience=%s",
                tostring(attack_before.level), tostring(attack_before.experience))
        else
            snapshot_detail = "attack reading unavailable: " .. tostring(attack_before)
        end
        t.check("attack.snapshot", snapshot_result == "ok" and type(attack_before) == "table", snapshot_detail)

        -- The stake finish is a queued proc (count_draynor.rs2's
        -- [ai_queue3]): wait for its own mes() line, then poll the
        -- completion varp server-side.
        t.exec("draynor.staked", t.msg.await, "hammer the stake into the vampyre", 20)
        local stake_result, stake_left = t.inv.count("stake")
        local hammer_result, hammer_left = t.inv.count("hammer")
        t.check("draynor.stakeUsed", stake_result == "ok" and stake_left == 0 and hammer_result == "ok" and hammer_left == 1,
            "after the finish: stake " .. tostring(stake_left) .. " (want 0, count_draynor.rs2 inv_del), hammer "
                .. tostring(hammer_left) .. " (want 1, kept)")
        t.exec("vampire.complete_var", t.var.await_server, "varp178_vampire", 3, 15)

        t.quest.expect_complete()
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 4825, snapshot))

        t.finish(0)
    end,
}
