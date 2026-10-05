-- The Ides of Milk -- driven for real: Cassius (giver, hand-placed spawn row
-- landed with seam21) -> Gillie -> Seth -> search the shelves for the book ->
-- return the book -> drink the first milk sample -> Cassius sends the second
-- sample to Duke Horacio -> Duke -> back to Gillie, drink the second sample
-- -> Gillie sends the player to open the bull pen -> a REAL fight against
-- Brutus (level 30, quest_inventory.tsv boss_fight=yes, a plain op2 Attack
-- and a stat block -- QUEST_AUTHORING.md section 6's "fought for real" rule,
-- not the skipboss stub the scaffold guessed) -> Gillie -> Cassius to finish
-- -> back to Gillie for the cowbell amulet + magic lamp (the ONLY other
-- reward Quest Helper's getUnlockRewards() lists beyond the quest point).
--
-- Every t.chat.play list below was rebuilt from idesofmilk.rs2/
-- idesofmilk_locs.rs2 branch by branch (trap 17/18): the scaffold's guessed
-- lists all copied [opnpc1,cowboss_farmer]'s FIRST (not-yet-started) branch
-- for every later talk_to on Cassius/Gillie regardless of %cowquest's actual
-- stage, which is wrong for every stage but the first -- each is replaced
-- here with the branch the read stage actually reaches. Duke's page is
-- [proc,iom_duke_quest] (duke_horacio.rs2:18-19 checks %cowquest = ^iom_duke
-- BEFORE the generic Rune Mysteries/dragon-shield tree the scaffold guessed
-- from), so no p_choice is ever reached there.
--
-- Gear/food/stats are the quest's own bring-along prerequisites (trap 16,
-- hero.lua's Ice Queen idiom): Brutus's ordinary swing cannot kill the
-- player during the quest (~cowquest_try_safe_death caps it at 1 hp,
-- idesofmilk.rs2), but his two specials (docs/quests/the_ides_of_milk.md,
-- wiki-sourced: "ignore protection prayers" and "can hit up to 19") are
-- exempt and roll after every 1-6 swings regardless of the player's own
-- attack speed -- max combat stats + a fast weapon minimise the swing count
-- needed to drop his 58 hp (all three melee defences -7) and shark food
-- covers whatever specials land before he does.
return {
    id = "idesofmilk",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::idesofmilk", -- resets %cowquest and teleports to Cassius's hand-placed spawn tile
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give rune_scimitar 1", -- Brutus's defences are all -7 (stab/slash/crush) -- any weapon lands; scimitar's speed minimises swings before he dies
        "::give mithril_platebody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 10", -- food for whatever Snort/Growl specials land before Brutus's 58 hp runs out
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb20106_cowquest",
            constants = {
                not_started = 0,
                investigate = 3,
                book = 4,
                return_book = 5,
                drink1 = 6,
                cassius_after = 8,
                duke = 10,
                gillie2 = 12,
                drink2 = 14,
                gillie_after = 16,
                fight = 18,
                gillie_end = 20,
                finish = 21,
                complete = 22,
            },
            row = "quest_idesofmilk",
            display = "The Ides of Milk",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- the setup cheats' effects are not client-side yet (trap 23)
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        t.exec("equipScimitar", t.player.equip, "rune_scimitar")
        t.exec("equipPlatebody", t.player.equip, "mithril_platebody")
        t.exec("equipPlatelegs", t.player.equip, "rune_platelegs")
        t.exec("equipFullHelm", t.player.equip, "rune_full_helm")
        t.exec("equipKiteshield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- Crossings (docs/QUEST_ORCHESTRATOR.md door rule, b60): every
        -- gate, door and staircase between the player and a target is
        -- clicked on every visit, in and out; a goto only hops between
        -- open tiles outside.
        --   * Gillie's cow field (comp.py: 743 tiles, its only way in on
        --     foot is fencegate_l/_r 3253,3266-3267, field side x >= 3253).
        --     gates.rs2 [proc,open_gate]: the MAIN leaf (fencegate_l,
        --     gate_main_closed) is pressed; the outer leaf swings two
        --     tiles, so the open leaf graded is openfencegate_l.
        --   * Seth's farmyard (113 tiles: fencegate_l/_r 3236,3295-3296,
        --     yard side x <= 3236), his farmhouse (poordoor 3230,3291,
        --     house side x <= 3230) and his own room (poordoor 3225,3293,
        --     room x 3222-3224).
        --   * Lumbridge Castle: the keep's front doorway is open map
        --     (inaccastledoubledoor*open, no op); the north spiral
        --     staircase spiralstairsbottom_3 3204,3229 has no maplink row,
        --     so ladders.rs2 [proc,climb] moves the player +-1 level on the
        --     tile they stand on; the Duke's room is behind elfdoor
        --     3207,3222,1 (room x >= 3208).
        -- ---------------------------------------------------------------
        local function tile_text(r, tt)
            if r ~= "ok" or type(tt) ~= "table" then
                return tostring(r)
            end
            return tt.x .. "," .. tt.z .. "," .. tt.level
        end

        local function field_in(tag)
            t.exec("goto-" .. tag .. ".fieldGate", t.player.goto_tile, 3251, 3266, 0)
            t.exec(tag .. ".fieldGateIn", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
                at = { 3253, 3266, 0 }, near = { 3252, 3266 }, far = { 3254, 3267 },
                far_ok = function(tt) return tt.x >= 3253 end, far_desc = "in the cow field, x >= 3253" })
        end
        local function field_out(tag)
            t.exec(tag .. ".fieldGateOut", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
                at = { 3253, 3266, 0 }, near = { 3254, 3266 }, far = { 3250, 3266 },
                far_ok = function(tt) return tt.x <= 3252 end, far_desc = "outside the cow field, x <= 3252" })
        end

        -- A staircase climb by click, graded on the level change and the
        -- landing within 2 of the stairs.
        local function climb(name, sym, op, from_level, to_level)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, op, { at = { 3204, 3229, from_level } })
            local ar = t.await({
                level = function()
                    local lr, lt = t.world.tile()
                    return lr == "ok" and type(lt) == "table" and lt.level == to_level
                end,
                note = sym .. ": waiting for level " .. to_level,
            }, 10)
            local tr, tt = t.world.tile()
            t.check(name,
                br == "ok" and bt.level == from_level and ar == "ok" and tr == "ok" and tt.level == to_level
                    and math.abs(tt.x - 3204) <= 2 and math.abs(tt.z - 3229) <= 2,
                "from " .. tile_text(br, bt) .. "; click_loc(" .. sym .. ", " .. op .. ", at 3204,3229," .. from_level
                    .. ") -> " .. tostring(cr) .. " " .. tostring(cd) .. "; level await -> " .. tostring(ar)
                    .. "; landed " .. tile_text(tr, tt) .. " (want level " .. to_level
                    .. ", within 2 of the staircase 3204,3229)")
        end

        -- Hitpoints: the driver's eater samples every tick of the fight
        -- (await_dead_engaged's opts.eat), vitals() before and after; below
        -- EAT_BELOW a shark is eaten. The margin row reads the lowest.
        local EAT_BELOW = 60
        local hp_low, hp_eaten = nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    local er = t.player.inv_op("shark", 1)
                    if er == "ok" then
                        hp_eaten = hp_eaten + 1
                    end
                    t.ticks(1)
                end
            end
        end

        -- ---------------------------------------------------------------
        -- Starting off: Cassius, by the Lumbridge pond (open ground; the
        -- setup cheat stands the player here).
        -- ---------------------------------------------------------------
        t.exec("goto-talkToCassius", t.player.goto_tile, 3171, 3277, 0)
        t.exec("talkToCassius", t.player.talk_to, "cowboss_farmer", 1)
        t.exec("talkToCassius-dialog", t.chat.play, {
            "npc:Something's wrong with the cow",
            "choose:Yes.",
            "player:Yes.",
            "npc:Speak to Gillie Groats at the ",
        })
        t.exec("quest.stage.investigate", t.quest.expect_stage, "investigate")

        -- ---------------------------------------------------------------
        -- Investigation: Gillie, then Seth, then the shelves.
        -- ---------------------------------------------------------------
        field_in("talkToGillie")
        t.exec("talkToGillie", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("talkToGillie-dialog", t.chat.play, {
            "player:Can you share what makes your cows so productive?",
            "npc:Hard work and family secrets!",
        })
        t.exec("inv.gillieInformation", t.var.await, "varb20107_cowquest_gillie_information", 1, 5)
        field_out("talkToSeth")

        t.exec("goto-talkToSeth.yardGate", t.player.goto_tile, 3239, 3296, 0)
        t.exec("talkToSeth.yardGateIn", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
            at = { 3236, 3296, 0 }, near = { 3237, 3296 }, far = { 3234, 3294 },
            far_ok = function(tt) return tt.x <= 3236 end, far_desc = "in Seth's farmyard, x <= 3236" })
        t.exec("talkToSeth.houseDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3230, 3291, 0 }, near = { 3231, 3291 }, far = { 3229, 3291 },
            far_ok = function(tt) return tt.x <= 3230 end, far_desc = "in the farmhouse, x <= 3230" })
        t.exec("talkToSeth.roomDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3225, 3293, 0 }, near = { 3225, 3293 }, far = { 3224, 3292 },
            far_ok = function(tt) return tt.x <= 3224 end, far_desc = "in Seth's room, x <= 3224" })
        t.exec("talkToSeth", t.player.talk_to, "favour_seth_groats", 1)
        t.exec("talkToSeth-dialog", t.chat.play, {
            "npc:Looking for Groats' book?",
        })
        t.exec("quest.stage.book", t.quest.expect_stage, "book")

        t.exec("searchShelves.roomDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3225, 3293, 0 }, near = { 3224, 3293 }, far = { 3227, 3288 },
            far_ok = function(tt) return tt.x >= 3225 end, far_desc = "in the farmhouse's main room, x >= 3225" })
        t.exec("searchShelves", t.player.click_loc, "cowquest_seth_shelf", 1)
        t.exec("inv.husbandryBook", t.inv.await, "cowquest_husbandry_book", 1, 5)
        t.exec("quest.stage.return_book", t.quest.expect_stage, "return_book")

        -- ---------------------------------------------------------------
        -- Milk tasting: return the book, drink sample 1, talk to Cassius.
        -- ---------------------------------------------------------------
        t.exec("returnToCassiusWithBook.houseDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3230, 3291, 0 }, near = { 3229, 3291 }, far = { 3232, 3291 },
            far_ok = function(tt) return tt.x >= 3231 end, far_desc = "out in the farmyard, x >= 3231" })
        t.exec("returnToCassiusWithBook.yardGateOut", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
            at = { 3236, 3296, 0 }, near = { 3236, 3296 }, far = { 3239, 3296 },
            far_ok = function(tt) return tt.x >= 3237 end, far_desc = "outside Seth's farmyard, x >= 3237" })
        t.exec("goto-returnToCassiusWithBook", t.player.goto_tile, 3171, 3277, 0)
        t.exec("returnToCassiusWithBook", t.player.talk_to, "cowboss_farmer", 1)
        t.exec("returnToCassiusWithBook-dialog", t.chat.play, {
            "player:I found this book.",
            "npc:Fascinating! Here",
        })
        t.exec("quest.stage.drink1", t.quest.expect_stage, "drink1")
        t.exec("inv.milkSample1", t.inv.expect_has, "cowquest_milk_sample_1", 1)
        t.exec("drinkMilkSample1", t.player.inv_op, "cowquest_milk_sample_1", 1)
        t.exec("quest.stage.cassius_after", t.quest.expect_stage, "cassius_after")
        local s1r, s1n = t.inv.count("cowquest_milk_sample_1")
        t.check("drinkMilkSample1.gone", s1r == "ok" and s1n == 0,
            "cowquest_milk_sample_1 after the drink: " .. tostring(s1n) .. " (" .. tostring(s1r) .. ", want 0)")

        t.exec("talkToCassiusAfterDrink", t.player.talk_to, "cowboss_farmer", 1)
        t.exec("talkToCassiusAfterDrink-dialog", t.chat.play, {
            "npc:Well? How was my milk?",
            "player:Fine... but maybe get a second opinion.",
            "npc:Take this sample to Duke Horacio.",
        })
        t.exec("quest.stage.duke", t.quest.expect_stage, "duke")
        t.exec("inv.milkSample2", t.inv.expect_has, "cowquest_milk_sample_2", 1)

        -- ---------------------------------------------------------------
        -- The Duke's opinion: into the castle on foot, up the north
        -- staircase by click, through his door; and back the same way.
        -- ---------------------------------------------------------------
        t.exec("goto-talkToDuke.castleCourtyard", t.player.goto_tile, 3222, 3218, 0)
        t.exec("talkToDuke.toStairs", t.player.walk_route,
            { { 3215, 3219 }, { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } }, { level = 0 })
        climb("talkToDuke.stairsUp", "spiralstairsbottom_3", 1, 0, 1)
        t.exec("talkToDuke.dukeDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3222, 1 }, near = { 3207, 3222 }, far = { 3209, 3221 },
            far_ok = function(tt) return tt.x >= 3208 end, far_desc = "in the Duke's room, x >= 3208" })
        t.exec("talkToDuke", t.player.talk_to, "duke_of_lumbridge", 1)
        t.exec("talkToDuke-dialog", t.chat.play, {
            "player:Cassius asked me to bring you this milk sample.",
            "npc:Hmm. Acceptable, but not extraordinary.",
        })
        t.exec("quest.stage.gillie2", t.quest.expect_stage, "gillie2")

        t.exec("talkToGillieAgain.dukeDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3222, 1 }, near = { 3208, 3222 }, far = { 3206, 3224 },
            far_ok = function(tt) return tt.x <= 3207 end, far_desc = "out of the Duke's room, x <= 3207" })
        t.exec("talkToGillieAgain.toStairs", t.player.walk_route, { { 3205, 3228 } }, { level = 1 })
        climb("talkToGillieAgain.stairsDown", "spiralstairsmiddle", 3, 1, 0)
        t.exec("talkToGillieAgain.outOfCastle", t.player.walk_route,
            { { 3207, 3227 }, { 3214, 3226 }, { 3215, 3219 }, { 3222, 3218 } }, { level = 0 })
        field_in("talkToGillieAgain")
        t.exec("talkToGillieAgain", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("talkToGillieAgain-dialog", t.chat.play, {
            "npc:The Duke sent you?",
        })
        t.exec("quest.stage.drink2", t.quest.expect_stage, "drink2")

        t.exec("drinkMilkSample2", t.player.inv_op, "cowquest_milk_sample_2", 1)
        t.exec("quest.stage.gillie_after", t.quest.expect_stage, "gillie_after")
        local s2r, s2n = t.inv.count("cowquest_milk_sample_2")
        t.check("drinkMilkSample2.gone", s2r == "ok" and s2n == 0,
            "cowquest_milk_sample_2 after the drink: " .. tostring(s2n) .. " (" .. tostring(s2r) .. ", want 0)")

        t.exec("talkToGillieAfterDrink", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("talkToGillieAfterDrink-dialog", t.chat.play, {
            "npc:That milk... something's off.",
        })
        t.exec("quest.stage.fight", t.quest.expect_stage, "fight")

        -- ---------------------------------------------------------------
        -- The bull fight: the pen gate in the field's north-east corner
        -- (inside the field: walked, never a goto), then a real fight
        -- against Brutus. The gate never opens: idesofmilk_locs.rs2
        -- [oploc1,fencegate_l_cowboss_start] npc_adds Brutus at
        -- ^iom_bull_coord 3260,3292, in the field beside it.
        -- ---------------------------------------------------------------
        t.exec("openBullPen.walk", t.player.walk_route, { { 3256, 3278 }, { 3259, 3286 }, { 3262, 3293 } }, { level = 0 })
        t.exec("openBullPen", t.player.click_loc, "fencegate_l_cowboss_start", 1)
        t.exec("openBullPen-dialog", t.chat.play, { "choose:Yes." })
        -- t.npc.await_present is hollow (trap 12: ok with a nil detail) --
        -- call it directly and read the live copy back with t.npc.nearest
        -- so the row carries its own evidence.
        local present_result = t.npc.await_present("cowboss", 12, 10)
        local nearest_result, nearest_row = t.npc.nearest("cowboss", 12)
        local nearest_text = type(nearest_row) == "table"
            and (tostring(nearest_row.x) .. "," .. tostring(nearest_row.z)) or tostring(nearest_row)
        t.check("npc.brutusPresent", present_result == "ok" and nearest_result == "ok",
            "await_present=" .. tostring(present_result) .. " nearest=" .. tostring(nearest_result)
                .. " at " .. nearest_text)

        hp_low = nil
        vitals()
        t.exec("killBrutus.engage", t.player.attack, "cowboss", 2, 15)
        local rounds = 0
        local brutus_dead = false
        while rounds < 20 and not brutus_dead do
            rounds = rounds + 1
            -- the driver's eater samples hitpoints every tick of the wait
            -- and eats a shark below EAT_BELOW; its lowest reading and its
            -- eat count are in the detail.
            local dead_result, dead_detail = t.npc.await_dead_engaged(60, 10,
                { eat = { item = "shark", below = EAT_BELOW } })
            t.note("round " .. tostring(rounds) .. " await_dead_engaged: " .. tostring(dead_result)
                .. " " .. tostring(dead_detail))
            local lowest = tonumber(string.match(tostring(dead_detail), "lowest hp (%d+)/"))
            if lowest ~= nil and (hp_low == nil or lowest < hp_low) then
                hp_low = lowest
            end
            hp_eaten = hp_eaten + (tonumber(string.match(tostring(dead_detail), "ate shark (%d+) time")) or 0)
            vitals()
            if dead_result == "ok" then
                brutus_dead = true
            elseif dead_result == "no_row" then
                -- the stamped engagement is gone -- re-press and keep going.
                t.player.attack("cowboss", 2, 15)
            end
        end
        t.check("killBrutus", brutus_dead, "killed Brutus (58 hp) after " .. tostring(rounds) .. " round(s)")
        vitals()
        local food_result, food_left = t.inv.count("shark")
        t.check("killBrutus.margin", hp_low ~= nil and hp_low >= 25 and food_result == "ok" and food_left >= 1,
            "Brutus (level 30): lowest hp " .. tostring(hp_low) .. "/99 (sampled every tick of the fight), sharks staged 10, eaten "
                .. hp_eaten .. ", left " .. tostring(food_left) .. " (" .. tostring(food_result)
                .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        t.expect("player.aliveAfterBrutus", t.player.alive())
        t.exec("quest.stage.gillie_end", t.quest.expect_stage, "gillie_end")

        -- ---------------------------------------------------------------
        -- Finishing up: Gillie, then Cassius to complete the quest.
        -- ---------------------------------------------------------------
        t.exec("talkToGillieAfterFight.walk", t.player.walk_route, { { 3258, 3284 }, { 3255, 3275 } }, { level = 0 })
        t.exec("talkToGillieAfterFight", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("talkToGillieAfterFight-dialog", t.chat.play, {
            "player:Brutus is down.",
            "npc:Thank goodness!",
        })
        t.exec("quest.stage.finish", t.quest.expect_stage, "finish")

        local snapshot_result = t.skill.snapshot()
        t.step("skill.snapshot", snapshot_result == "ok" and "PASS" or "FAIL",
            "snapshot taken before hand-in: " .. tostring(snapshot_result))

        field_out("finishQuest")
        t.exec("goto-finishQuest", t.player.goto_tile, 3171, 3277, 0)
        t.exec("finishQuest", t.player.talk_to, "cowboss_farmer", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:The bull is dealt with.",
            "npc:Splendid work!",
        })

        t.ticks(3) -- completion is asynchronous (section 8) -- let ~iom_quest_complete's scroll mount before reading it
        t.quest.expect_complete()

        -- Quest Helper's getUnlockRewards(): "Access to the cow boss" (no
        -- separate item/varp to assert -- Brutus's own pen is what was just
        -- fought through) and "Cow bell amulet and magic lamp ... from
        -- Gillie Groats", granted by ONE more dialogue with her
        -- (idesofmilk.rs2's gillie_talk, %cowquest >= ^iom_complete &
        -- %cowquest_reward = 0 branch) -- driven for real, not ::given.
        field_in("collectRewardFromGillie")
        t.exec("collectRewardFromGillie", t.player.talk_to, "gillie_the_milkmaid", 1)
        t.exec("collectRewardFromGillie-dialog", t.chat.play, {
            "npc:For your help",
        })
        t.exec("reward.cowbellAmulet", t.inv.expect_has, "cowbell_amulet", 1)
        t.exec("reward.cowbossRewardLamp", t.inv.expect_has, "cowboss_reward_lamp", 1)

        t.finish(0)
        return
    end,
}
