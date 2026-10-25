-- Vampyre Slayer. Rewritten from tools/quest_gate/new_quest.py's scaffold
-- against the quest's own .rs2 (OSRS-Content/osrs-content/server/scripts/
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
--     (cryptstairsdown 3115,3357) by click, down to the crypt and, after
--     the kill, cryptstairsup (3077,9768) back up to the east wing.
--
-- The crypt (rebuilt from RSProx index 4373, docs/rsprox-analysis/quests/
-- vampyre_slayer.md): the stairs land in a private copy of square 48_152 at
-- offset (6,43), so the crypt tiles below are graded as offsets into the copy;
-- the stairs up are at (5,40) and land on 3115,3356,0. Both ends are plane 0,
-- and t.player.climb asserts a NEW level, so these two keep the file's own
-- climb().
--
-- Combat: the coffin raises the Count at O+10 and he swings at O+11 with the
-- garlic line (-10 hitpoints, attack/strength -10, defence -40). Zero
-- hitpoints is his script's (param=death_script): with stake and hammer the
-- red line at K+1, gone at K+4, the completion at K+5 with the stake removed.

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
        -- the wire audit against RSProx 4373 reads this (build/quest_gate/<run>/ticklog.tsv)
        t.ticklog.start()
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
            journal_title = "Vampyre Slayer", -- vampire_journal.rs2's ~quest_journal title (the 2020 rename)
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
            "choose:Alright, I'll see about paying him a visit.",
            "player:Alright, I'll see about paying him a visit.",
            "npc:Thank you, brave adventurer.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- --------------------------------------------------- cGetGarlic --
        -- goUpstairsMorgan: the staircase by click from its maplink src.
        t.exec("goUpstairsMorgan", t.player.climb, { loc = "stairs", op = 1, op_name = "Climb-up",
            at = { 3099, 3266, 0 }, src = { 3098, 3267 }, dest = { 3102, 3266, 1 } }) -- maplink_0_48_51_26_3_up
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
        t.exec("getGarlic-dialog", t.chat.play, { "objbox:The cupboard contains garlic. You take a clove." })
        t.exec("garlic.received", t.inv.await, "garlic", 1, 5)

        -- Back down and out of the house, by the same stairs and door.
        t.exec("goDownstairsMorgan", t.player.climb, { loc = "stairstop", op = 1, op_name = "Climb-down",
            at = { 3100, 3266, 1 }, src = { 3102, 3266 }, dest = { 3098, 3266, 0 } }) -- maplink_1_48_51_30_2_down
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
            "npc:Have yoush got me a beer?",
            "choose:Yes, here you go.",
            "player:Yes, here you go.",
            "objbox:You give a beer to Dr Harlow.",
            "npc:Cheersh, matey",
            "player:Now, about Count Draynor",
            "npc:Yesh, Count Draynor! The evil nashty vampyre",
            "npc:You want to havesh a go?",
            "player:So how do I make sure I'm prepared?",
            "npc:Most vampyres regenerate.",
            "objbox:Dr Harlow hands you a stake.",
            "npc:Takesh that to Draynor Manor",
            "npc:Oh, and yoush should take some garlic",
            "player:Garlic? Hmm",
            "choose:I'd best get going.",
            "player:I'd best get going.",
            "npc:Good luck, matey...",
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
            -- A private copy of square 48_152 (RSProx index 4373 [451]): the
            -- landing is the template's 3078,9771, offset (6,43) in the copy.
            function(tt) return tt.x % 64 == 6 and tt.z % 64 == 43 and tt.x >= 6400 end,
            "the crypt copy's landing, (6,43) of a copy of 48_152 (vampire_crypt_stairs.rs2 [label,vampire_crypt_enter])",
            "goDownToBasement.stairsGoNowhere")
        if crypt == nil then
            t.blocked("content_bug: Draynor Manor's crypt stairs did not land in the crypt. "
                .. "OSRS-Content/osrs-content/server/scripts/quests/quest_vampire/scripts/vampire_crypt_stairs.rs2:20-26 "
                .. "[oploc1,cryptstairsdown] at 0_48_52_43_29 should p_telejump(0_48_152_5_43) (3077,9771,0), "
                .. "LostCity_Server content/scripts/ladders+stairs/scripts/stairs.rs2:408-417; the press, its answer and "
                .. "the chat are in goDownToBasement.stairsGoNowhere. The basement holds the coffin and Count Draynor "
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
        local present_result = t.npc.await_present("count_draynor", 10, 25)
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
        -- The stake is the death (param=death_script, count_draynor.rs2): K+1
        -- the red line and the transformation, K+4 gone, K+5 the completion
        -- (RSProx index 4373 [674..679]). The line is awaited during the fight
        -- and the xp snapshot taken on it, after the last swing (p_stopaction)
        -- and before the queued reward.
        local sharks_before_result, sharks_before = t.inv.count("shark")
        t.exec("killDraynor", t.player.attack, "count_draynor", 2, 15)
        t.exec("draynor.staked", t.msg.await, "hammer the stake into the vampyre", 60)
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
        t.exec("vampire.complete_var", t.var.await_server, "varp178_vampire", 3, 15)
        local gone_result = t.npc.nearest("count_draynor", 10)
        t.check("killDraynor.dead", gone_result ~= "ok",
            "count_draynor within 10 tiles after the completion: " .. tostring(gone_result) .. " (want none)")
        local hp_result, hp_now = t.skill.read("hitpoints")
        local sharks_result, sharks_left = t.inv.count("shark")
        t.check("killDraynor.margin", hp_result == "ok" and type(hp_now) == "table" and hp_now.level >= 25
                and sharks_result == "ok" and sharks_left >= 1,
            "after the fight: hp " .. tostring(type(hp_now) == "table" and hp_now.level or hp_now) .. "/99, sharks "
                .. tostring(sharks_before) .. " (" .. tostring(sharks_before_result) .. ") -> " .. tostring(sharks_left)
                .. " (" .. tostring(sharks_result) .. ") (margin: hp >= 25 AND food left)")
        -- The stake leaves the pack with the scroll (RSProx index 4373 [679]).
        local stake_result, stake_left = t.inv.count("stake")
        local hammer_result, hammer_left = t.inv.count("hammer")
        t.check("draynor.stakeUsed", stake_result == "ok" and stake_left == 0 and hammer_result == "ok" and hammer_left == 1,
            "after the completion: stake " .. tostring(stake_left) .. " (want 0, [queue,quest_vampire_complete] inv_del), hammer "
                .. tostring(hammer_left) .. " (want 1, kept)")

        t.quest.expect_complete()
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 4825, snapshot))

        -- Out of the crypt the way in: cryptstairsup (3077,9768,0) by click,
        -- from the down landing, to the east wing's stair foot.
        walk_check("leaveCrypt.atStairs", crypt.x - 1, crypt.z, 0)
        climb("leaveCrypt", "cryptstairsup", { crypt.x - 1, crypt.z - 3, 0 }, 0,
            function(tt) return tt.x == 3115 and tt.z == 3356 end,
            "the east wing 3115,3356,0 (vampire_crypt_stairs.rs2 p_telejump(0_48_52_43_28))")

        t.finish(0)
    end,
}
