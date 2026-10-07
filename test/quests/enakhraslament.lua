-- Enakhra's Lament. Stage values: OSRS-Content/osrs239-content/server/scripts/quests/
-- quest_enakhraslament/configs/enakhraslament.constant (enakh_quest 0/10/20/30/40/50/60/70, the
-- canonical values Quest Helper's steps.put ladder reads).
-- Guide: quest-helper helpers/quests/enakhraslament/EnakhrasLament.java; wiki Enakhra's_Lament
-- (oldid 15365540), /Quick_guide (15276622), Transcript (15356267).
--
-- Setup stages only the quest's requirements (dbrow quest_enakhraslament: Crafting 50,
-- Firemaking 45, Prayer 43, Magic 39) plus Mining 45, the level the quarry's granite rocks need
-- (mine.dbrow rock_level 45; the guide mines the sandstone and granite). No script or dialogue
-- branches on any of these except the rocks' own gate, the head carving's Crafting 50 and the
-- braziers' Investigate (not used here). The kit is Quest Helper's bring-along list.
--
-- Door rule (owner 2026-10-03): the run starts at the fixture's tile in Lumbridge (3206,3233).
--   * The Kharidian Desert is entered on foot only through the Shantay Pass doorway
--     shantay_pass_henge_doorway 3302,3116 (a pass from Shantay, 5 gp; cross_gate as golem.lua).
--   * The quarry south of the Bandit Camp is open ground (reach.py 3304,3110 -> 3190,2924 REACH).
--   * The temple is entered by the statue's fall (enakhraslament_quarry.rs2 @enakh_statue_collapse
--     -> 3127,9323,0). Inside, every quadrant of the bottom floor is closed by a limb door and the
--     centre by four sigil doors (comp.py: four rooms, each with its two limb doors and one sigil
--     door on its edge), the middle floor's north room by the magic barrier, the top floor's
--     corridor by the first Boneguard (blockwalk=all) and its rubble; each is pressed here.
--
-- Random rolls: the quarry's sandstone and granite tiers (mining.rs2 ~enakh_sandstone_tier /
-- ~enakh_granite_tier) and Crumble Undead's hit roll. The route reads only the backpack and the
-- dialogue (Lazim's own "I need N kg more"), never a roll's varbit.
return {
    id = "enakhraslament",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- three quarry sessions on a random tier roll, then the whole temple
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so the kit fits
        "::give rune_pickaxe 1", -- Quest Helper: any pickaxe (the quarry rocks, mining.rs2:49-51)
        "::give chisel 1", -- the statue, the heads, the limbs, the wall
        "::give softclay 1", -- the pedestal's camel mould
        "::give bread 1", -- Pentyn
        "::give tinderbox 1", -- the braziers
        "::give logs 1", -- the six braziers
        "::give oak_logs 1",
        "::give willow_logs 1",
        "::give maple_logs 1",
        "::give unlit_candle 1",
        "::give coal 1",
        "::give firerune 8", -- Fire Bolt (fountain): 4 fire, 3 air, 1 chaos
        "::give airrune 16", -- + Wind Bolt (furnace) 2 air 1 chaos, Crumble Undead 2 air 2 earth 1 chaos (x5)
        "::give earthrune 10",
        "::give chaosrune 8",
        "::give coins 10", -- two Shantay passes, 5 gp each (shantay.rs2)
        "::give water_skin4 1", -- desert heat (desert_heat.rs2: a drink per 150 ticks in desert_zones)
        "::setlevel crafting 50",
        "::setlevel firemaking 45",
        "::setlevel prayer 43",
        "::setlevel magic 39",
        "::setlevel mining 45", -- granite rocks' rock_level (mine.dbrow); sandstone needs 35
    },

    run = function(t)
        local function count(sym)
            local r, n = t.inv.count(sym)
            if r ~= "ok" or type(n) ~= "number" then
                return 0
            end
            return n
        end
        local function tile()
            local r, at = t.world.tile()
            if r ~= "ok" or type(at) ~= "table" then
                return nil
            end
            return at
        end
        local function reading()
            local at = tile()
            if not at then
                return "tile ?"
            end
            return string.format("%d,%d,%d", at.x, at.z, at.level)
        end
        local function var(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or -1
        end
        local function stage()
            return var("varb1560_enakh_quest")
        end
        local function statue()
            return var("varb1593_enakh_statue_multivar")
        end
        local function free_slots()
            local free = 0
            for i = 0, 27 do
                local r, slot = t.inv.slot(i)
                if r == "ok" and type(slot) == "table" and slot.count == 0 then
                    free = free + 1
                end
            end
            return free
        end

        local bind_result, bind_detail = t.quest.bind({
            varp = "varb1560_enakh_quest",
            constants = {
                not_started = 0,
                statue = 10,
                bottom_floor = 20,
                puzzles = 30,
                first_boneguard = 40,
                second_boneguard = 50,
                wall = 60,
                complete = 70,
            },
            row = "quest_enakhraslament",
            display = "Enakhra's Lament",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", tostring(bind_detail))
        t.ticks(3)
        t.check("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------ into the desert
        -- Buy a Shantay pass and go south through the doorway (shantay.rs2: 5 gp;
        -- shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway]); the first trip reads the poster.
        t.exec("goto-shantay", t.player.goto_tile, 3304, 3123, 0)
        local coins0, pass0 = count("coins"), count("shantay_pass")
        t.exec("shantay.buyPass", t.player.talk_to, "shantay", 1)
        t.exec("shantay.buyPass-dialog", t.chat.play, {
            "npc:Hello effendi, I am Shantay.", "npc:I see you're new.",
            "choose:I want to buy a shantay pass for 5 gold coins.",
            "player:I want to buy a shantay pass for",
            "mesbox:You purchase a Shantay Pass.",
        })
        local pass_await = t.inv.await("shantay_pass", 1, 5)
        t.check("shantay.buyPass-paid", pass_await == "ok" and count("shantay_pass") == pass0 + 1 and count("coins") == coins0 - 5,
            "shantay_pass " .. pass0 .. " -> " .. count("shantay_pass") .. ", coins " .. coins0 .. " -> " .. count("coins") .. " (want -5, shantay.rs2)")
        t.exec("shantay.doorway", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
            near = { 3304, 3118 }, far_ok = function(at) return at.z <= 3115 end,
            far_desc = "south of the Shantay Pass doorway, z <= 3115",
            chat = { "mesbox:There is a large poster on the wall", "mesbox:The Desert is a VERY Dangerous place",
                "mesbox:That seems pretty scary!", "choose:Yeah, that poster doesn't scare me!",
                "npc:Can I see your Shantay Desert Pass", "mesbox:You hand over a Shantay Pass.",
                "player:Sure, here you go!", "npc:Here, have a disclaimer" } })
        t.check("shantay.doorway.passHandedOver", count("shantay_pass") == pass0,
            "shantay_pass " .. count("shantay_pass") .. " after the doorway (handed over)")

        -- ------------------------------------------------ the quarry: Lazim's offer
        -- Lazim's quarry spawn is 3191,2926 (m49_45.spawn); stand west of him on open sand.
        local LAZIM = "enakh_lazim_statue_east_multinpc"
        t.exec("goto-talkToLazim", t.player.goto_tile, 3189, 2927, 0)
        t.exec("talkToLazim", t.player.talk_to, LAZIM, 1)
        t.exec("talkToLazim-dialog", t.chat.play, {
            "npc:Ah, an adventurer.", "player:Why, what's the matter?", "npc:It's a sorry tale.",
            "npc:I made statues", "npc:After that, I could never hold a chisel", "npc:But I have been struck",
            "npc:The desert's plain", "player:So, what would you need", "npc:As I said, I can't hold a chisel",
            "npc:I'd need you to quarry", "npc:Well, will you help me with my art?",
            "choose:Of course!",
            "player:Of course!", "player:You'll have to explain it", "npc:Excellent.",
            "player:What should I be doing to make your statue?", "npc:There is a stone quarry nearby",
            "npc:Each of these blocks should be around", "player:I can't mine a block that big!",
            "npc:Well, then, bring me enough smaller blocks",
            "choose:Okay, I'll get on with it.",
            "player:Okay, I'll get on with it.",
        })
        t.check("quest.stage.statue", t.quest.expect_stage("statue"))

        -- ------------------------------------------------ the quarry's rocks
        local SAND = { { "enakh_sandstone_large", 10 }, { "enakh_sandstone_medium", 5 },
            { "enakh_sandstone_small", 2 }, { "enakh_sandstone_tiny", 1 } }
        local GRANITE_JUNK = { "enakh_granite_tiny", "enakh_granite_small" }
        local SAND_ROCKS = {
            { 3166, 2906 }, { 3166, 2905 }, { 3164, 2906 }, { 3164, 2905 }, { 3169, 2906 }, { 3169, 2905 }, { 3163, 2906 },
        }
        local GRANITE_ROCKS = {
            { 3165, 2909 }, { 3165, 2910 }, { 3164, 2909 }, { 3164, 2910 }, { 3165, 2908 }, { 3167, 2904 }, { 3167, 2903 },
        }
        local function sand_kg()
            local s = 0
            for _, e in ipairs(SAND) do s = s + count(e[1]) * e[2] end
            return s
        end
        local function sand_pieces()
            local s = 0
            for _, e in ipairs(SAND) do s = s + count(e[1]) end
            return s
        end
        local function granite_pieces()
            return count("enakh_granite_tiny") + count("enakh_granite_small") + count("enakh_granite_medium")
        end
        local function mining_xp()
            local r, s = t.skill.read("mining")
            return (r == "ok" and type(s) == "table") and tostring(s.experience) or tostring(r)
        end
        -- Swing at `rocks` until `done()` or `cap` presses; each press waits for the piece.
        local function quarry(name, rock_sym, rocks, done, pieces, cap)
            t.exec("walk-" .. name, t.player.walk_to, 3167, 2907, 40)
            local presses = 0
            for att = 1, cap do
                if done() then break end
                local rock = rocks[(att - 1) % #rocks + 1]
                local n0 = pieces()
                presses = presses + 1
                t.player.click_loc(rock_sym, 1, { at = { rock[1], rock[2] } })
                t.await({ level = function() return pieces() > n0 end, note = name .. " quarried a piece" }, 15)
            end
            return presses
        end

        -- Lazim's count of the kilograms ("I need N kg more stone"), one block at a time. The
        -- menu offers only blocks that still fit (enakhraslament_quarry.rs2 ~enakh_choose_block),
        -- so the backpack is made to sum exactly first: the greedy pick, and a chisel split of the
        -- smallest block that is still too big (wiki Sandstone: 10 -> 5+5, 5 -> 2+2+1, 2 -> 1+1).
        local function greedy_left(need)
            local left = need
            for _, e in ipairs(SAND) do
                local n = count(e[1])
                while n > 0 and left >= e[2] do
                    left = left - e[2]
                    n = n - 1
                end
            end
            return left
        end
        local function make_exact(name, need)
            for i = 1, 8 do
                local left = greedy_left(need)
                if left == 0 then return true end
                local split = nil
                for k = #SAND, 1, -1 do
                    local e = SAND[k]
                    if e[2] > left and e[2] > 1 and count(e[1]) > 0 then
                        split = e[1]
                        break
                    end
                end
                if not split then return false end
                local before = sand_pieces()
                t.exec(name .. "-split" .. i, t.player.use_item_on_item, split, "chisel")
                t.await({ level = function() return sand_pieces() > before end, note = "the split landed" }, 6)
            end
            return greedy_left(need) == 0
        end
        local function fits(need)
            for _, e in ipairs(SAND) do
                if e[2] <= need and count(e[1]) > 0 then return true end
            end
            return false
        end
        -- One conversation: hand over blocks until Lazim has `target` or nothing more fits.
        -- Returns the kilograms he now carries. Lazim's block menu (~enakh_choose_block) lists
        -- only the sizes held that still fit, largest first, then "Why won't you take more than
        -- N kg of stone?"; the block is picked from the rows he offers, never from a count.
        -- A handed-over block leaves the backpack a tick after its page (trap 25), so each
        -- handover is awaited before the next Yes/No: a stale count once said "Yes" with the
        -- last fitting block already gone (enq2), and the menu then offered only the "Why"
        -- row. That row is a real branch: it is chosen, Lazim explains, and the conversation
        -- ends; build_block splits and comes back.
        local BLOCK_ROWS = {
            ["Here's a large 10 kg block."] = { "enakh_sandstone_large", 10 },
            ["Here's a medium 5 kg block."] = { "enakh_sandstone_medium", 5 },
            ["Here's a small 2 kg block."] = { "enakh_sandstone_small", 2 },
            ["Here's a tiny 1 kg block."] = { "enakh_sandstone_tiny", 1 },
        }
        local function deliver(name, target, carried)
            t.exec("goto-" .. name, t.player.goto_tile, 3189, 2927, 0)
            t.exec(name, t.player.talk_to, LAZIM, 1)
            local visit = 0
            while carried < target and visit < 40 do
                visit = visit + 1
                local dr, kind = t.chat.drain({ stop_at = "options" })
                if kind ~= "options" then break end
                local _, rows = t.chat.options()
                rows = rows or {}
                local has_yes, block_row, why_row = false, nil, nil
                for _, row in ipairs(rows) do
                    if row == "Yes, I have more stone." then has_yes = true end
                    if not block_row and BLOCK_ROWS[row] then block_row = row end
                    if row:find("^Why won't you take more than") then why_row = row end
                end
                local need = target - carried
                if has_yes then
                    if fits(need) then
                        t.exec(name .. "-more" .. visit, t.chat.choose, "Yes, I have more stone.")
                    else
                        t.exec(name .. "-enough" .. visit, t.chat.choose, "No, that's all for now.")
                        break
                    end
                elseif block_row then
                    local sym, kg = BLOCK_ROWS[block_row][1], BLOCK_ROWS[block_row][2]
                    local before = count(sym)
                    t.exec(name .. "-block" .. visit, t.chat.choose, block_row)
                    t.await({ level = function() return count(sym) < before end,
                        note = "Lazim took the " .. kg .. " kg block" }, 6)
                    carried = carried + kg
                elseif why_row then
                    t.exec(name .. "-why" .. visit, t.chat.choose, why_row)
                    break
                else
                    t.check(name .. "-menu" .. visit, false, "a menu Lazim's handover never shows: " .. table.concat(rows, " | "))
                    break
                end
            end
            t.exec(name .. "-end", t.chat.drain, {})
            return carried
        end
        -- Quarry and hand over until Lazim has `target` kg and gives his block back.
        local function build_block(name, target, block)
            local carried = 0
            for round = 1, 6 do
                if count(block) > 0 then break end
                local need = target - carried
                quarry("mine-" .. name .. round, "enakh_sandstone_rocks", SAND_ROCKS,
                    function() return sand_kg() >= need or free_slots() <= 1 end, sand_pieces, 120)
                if sand_kg() >= need then
                    make_exact(name .. round, need)
                end
                carried = deliver(name .. (round > 1 and ("-trip" .. round) or ""), target, carried)
            end
            t.inv.await(block, 1, 5)
            return count(block) > 0
        end

        -- ------------------------------------------------ the base: 32 kg
        local got_base = build_block("bringLazim32Sandstone", 32, "enakh_sandstone_huge_base+legs")
        t.check("bringLazim32Sandstone.block", got_base,
            "enakh_sandstone_huge_base+legs x" .. count("enakh_sandstone_huge_base+legs")
                .. " after the deliveries (enakhraslament_quarry.rs2 @enakh_lazim_take_stone); mining xp " .. mining_xp())
        t.exec("useChiselOn32Sandstone", t.player.use_item_on_item, "enakh_sandstone_huge_base+legs", "chisel")
        t.inv.await("enakh_sandstone_crafted_base+legs", 1, 5)
        t.check("useChiselOn32Sandstone.base", count("enakh_sandstone_crafted_base+legs") == 1,
            "enakh_sandstone_crafted_base+legs x" .. count("enakh_sandstone_crafted_base+legs"))
        local statue_loc = t.player.by_symbol("loc", "enakh_statue_east_multiloc")
        t.exec("goto-placeBase", t.player.goto_tile, 3188, 2927, 0)
        t.exec("placeBase", t.player.use_on, "enakh_sandstone_crafted_base+legs", statue_loc)
        t.await({ level = function() return statue() == 1 end, note = "statue multivar 1 (base placed)" }, 6)
        t.check("placeBase.placed", statue() == 1, "enakh_statue_multivar " .. statue() .. " (want 1: the base on the flat ground)")

        -- ------------------------------------------------ the body: 20 kg
        t.exec("talkToLazimAboutBody", t.player.talk_to, LAZIM, 1)
        t.exec("talkToLazimAboutBody-dialog", t.chat.play, {
            "player:What do you want me to do now?", "npc:Hmph. Well, the base of the statue is done",
            "choose:I'll do it right away!", "player:I'll do it right away!",
        })
        t.check("talkToLazimAboutBody.blurb", var("varb1562_enakh_lazim_statue_body_blurb") == 1,
            "enakh_lazim_statue_body_blurb " .. var("varb1562_enakh_lazim_statue_body_blurb") .. " (Quest Helper hasTalkedToLazimAfterBase)")
        local got_body = build_block("bringLazim20Sandstone", 20, "enakh_sandstone_huge_body")
        t.check("bringLazim20Sandstone.block", got_body, "enakh_sandstone_huge_body x" .. count("enakh_sandstone_huge_body"))
        t.exec("useChiselOn20Sandstone", t.player.use_item_on_item, "enakh_sandstone_huge_body", "chisel")
        t.inv.await("enakh_sandstone_crafted_body", 1, 5)
        t.check("useChiselOn20Sandstone.body", count("enakh_sandstone_crafted_body") == 1,
            "enakh_sandstone_crafted_body x" .. count("enakh_sandstone_crafted_body"))
        t.exec("goto-placeBody", t.player.goto_tile, 3188, 2927, 0)
        t.exec("placeBody", t.player.use_on, "enakh_sandstone_crafted_body", statue_loc)
        t.await({ level = function() return statue() == 2 end, note = "statue multivar 2 (body placed)" }, 6)
        t.check("placeBody.placed", statue() == 2, "enakh_statue_multivar " .. statue() .. " (want 2)")
        t.exec("chiselStatue", t.player.use_on, "chisel", statue_loc)
        t.await({ level = function() return statue() == 3 end, note = "statue multivar 3 (chiselled)" }, 6)
        t.check("chiselStatue.chiselled", statue() == 3, "enakh_statue_multivar " .. statue() .. " (want 3)")
        -- The rest of the quarried sandstone is not needed again (the wall's comes from the
        -- temple's rubble, Quest Helper repairWall).
        for _, e in ipairs(SAND) do
            for _ = 1, 12 do
                if count(e[1]) <= 0 then break end
                t.player.drop(e[1])
            end
        end

        -- ------------------------------------------------ the head
        t.exec("talkToLazimToChooseHead", t.player.talk_to, LAZIM, 1)
        t.exec("talkToLazimToChooseHead-dialog", t.chat.play, {
            "npc:It shouldn't have taken this long", "npc:Ah, adventurer. As you can see",
            "npc:The head should be made out of granite", "npc:As for whose head",
            "npc:Whose head do you think should be on the statue?",
            "choose:I think it should have your head.",
            "player:I think it should have your head.", "player:After all, it's your statue.",
            "npc:Why, that's very generous", "npc:Are you hoping for a bigger reward", "player:Er, maybe.",
            "npc:Wonderful.", "npc:Now, go and find a suitable block of granite", "npc:And do try to remember",
        })
        t.check("talkToLazimToChooseHead.chosen", var("varb1563_enakh_lazim_statue_head_blurb") == 1
                and var("varb1615_enakh_choose_statue_head") == 0,
            "head_blurb " .. var("varb1563_enakh_lazim_statue_head_blurb") .. ", choose_statue_head "
                .. var("varb1615_enakh_choose_statue_head") .. " (want 1, 0 = Lazim)")

        -- Two 5 kg granite: the statue's head and the pedestal's (quick guide); the other tiers
        -- are dropped as they land.
        local function drop_granite_junk()
            for _, sym in ipairs(GRANITE_JUNK) do
                for _ = 1, 4 do
                    if count(sym) <= 0 then break end
                    t.player.drop(sym)
                end
            end
        end
        for round = 1, 4 do
            if count("enakh_granite_medium") >= 2 then break end
            quarry("getGranite" .. (round > 1 and round or ""), "enakh_granite_rocks", GRANITE_ROCKS,
                function() return count("enakh_granite_medium") >= 2 or free_slots() <= 1 end, granite_pieces, 40)
            drop_granite_junk()
        end
        t.check("getGranite", count("enakh_granite_medium") >= 2,
            "enakh_granite_medium x" .. count("enakh_granite_medium") .. " (want 2); mining xp " .. mining_xp())
        local head0 = count("enakh_statue_head_lazim")
        t.exec("craftHead", t.player.use_item_on_item, "enakh_granite_medium", "chisel")
        t.exec("craftHead-dialog", t.chat.play, {
            "player:Hmm. Which head did I decide", "choose:The head of Lazim, the sculptor",
            "player:There, that looks good.",
        })
        t.inv.await("enakh_statue_head_lazim", head0 + 1, 5)
        t.check("craftHead.carved", count("enakh_statue_head_lazim") == head0 + 1 and count("enakh_granite_medium") == 1,
            "enakh_statue_head_lazim " .. head0 .. " -> " .. count("enakh_statue_head_lazim") .. ", enakh_granite_medium "
                .. count("enakh_granite_medium") .. " left for the pedestal")

        -- "Talk to him and you'll fall into a temple" (quick guide): Lazim checks the head, it goes
        -- on the statue, the statue breaks through and he pushes the player in.
        t.exec("goto-giveLazimHead", t.player.goto_tile, 3189, 2927, 0)
        t.exec("giveLazimHead", t.player.talk_to, LAZIM, 1)
        t.exec("giveLazimHead-dialog", t.chat.play, {
            "player:I have a head for the statue, but...", "player:I'm not sure it's the right one.",
            "npc:Let me see...", "npc:You decided to make the statue's head look like me",
            "npc:Hmm. I don't think much of your artistic skills",
            "player:What's going on?", "player:Ow!", "player:Ouch!", "player:Argh, my duodenum!",
            "player:Ow... Lazim has some explaining to do!",
        })
        t.check("quest.stage.bottom_floor", t.quest.expect_stage("bottom_floor"))
        local fell = tile()
        t.check("giveLazimHead.fell", fell ~= nil and fell.z > 9000 and fell.level == 0,
            "player at " .. reading() .. " (want the temple's bottom floor, 3127,9323,0); statue multivar " .. statue()
                .. " (28 = the hole), fallen statue multivar " .. var("varb1587_enakh_fallen_statue_multivar") .. " (want 3)")

        -- ------------------------------------------------ the bottom floor
        local TEMPLE_LAZIM = "enakh_lazim_fallen_statue_east_multinpc"
        t.ticks(3)
        t.exec("talkToLazimInTemple", t.player.talk_to, TEMPLE_LAZIM, 1)
        t.exec("talkToLazimInTemple-dialog", t.chat.play, {
            "npc:Ah, excellent, it's all gone as planned.", "player:Gone as planned?",
            "player:We just fell through the ground", "npc:Of course we did!", "player:I don't think you really are",
            "npc:Oh, nothing.", "player:I'll be leaving, then.", "npc:Wait! If you help me", "npc:I'll give you a share",
            "player:That sounds better.", "npc:At the top of the temple.", "player:That's good, but...",
            "player:How do I get further into the temple?", "npc:The stories I've heard", "player:Yuck!",
            "npc:You can use your chisel or pick", "npc:Be thankful",
        })
        t.check("talkToLazimInTemple.reallyamage", var("varb1566_enakh_lazim_reallyamage") == 1,
            "enakh_lazim_reallyamage " .. var("varb1566_enakh_lazim_reallyamage") .. " (Quest Helper startedTemple)")

        -- "If you climb up the ladder and up the sand pile next to the statue now, you'll be able to
        -- exit and enter the temple" (quick guide): out by the east room's ladder and sand pile, which
        -- opens the north-east secret entrance (wiki Secret_entrance), and back in through it --
        -- Quest Helper's enterTemple / enterTempleDownLadder.
        t.exec("exitTemple-ladderUp", t.player.climb, { loc = "enakh_temple_ladderup", at = { 3127, 9329, 0 },
            op_name = "Climb-up", dest = { 3126, 9329, 1 }, slack = 2 })
        t.exec("exitTemple-sandPile", t.player.climb, { loc = "enakh_temple_sand_pile", at = { 3124, 9329, 1 },
            op_name = "Climb", dest = { 3194, 2926, 0 }, slack = 1 })
        t.check("exitTemple.entranceOpen", var("varb1599_enakh_boulder_e_multivar") == 1,
            "enakh_boulder_e_multivar " .. var("varb1599_enakh_boulder_e_multivar") .. " (the north-east secret entrance)")
        t.exec("enterTemple", t.player.climb, { loc = "enakh_secret_boulder_multiloc_e", at = { 3194, 2925, 0 },
            op_name = "Climb-down", dest = { 3124, 9328, 1 }, slack = 1 })
        t.exec("enterTempleDownLadder", t.player.climb, { loc = "enakh_temple_ladderdown", at = { 3127, 9329, 1 },
            op_name = "Climb-down", dest = { 3127, 9330, 0 } })

        local fallen = t.player.by_symbol("loc", "enakh_fallen_statue_east_multiloc")
        local LIMBS = {
            { "LeftArm", "Remove the statue's left arm", "enakh_arm_left" },
            { "RightArm", "Remove the statue's right arm", "enakh_arm_right" },
            { "LeftLeg", "Remove the statue's left leg", "enakh_leg_left" },
            { "RightLeg", "Remove the statue's right leg", "enakh_leg_right" },
        }
        for _, limb in ipairs(LIMBS) do
            t.exec("cutOffLimb-" .. limb[1], t.player.use_on, "chisel", fallen)
            t.exec("cutOffLimb-" .. limb[1] .. "-choose", t.chat.play, { "choose:" .. limb[2] })
            t.inv.await(limb[3], 1, 5)
        end
        t.check("cutOffLimb.all", var("varb1587_enakh_fallen_statue_multivar") == 63
                and count("enakh_arm_left") == 1 and count("enakh_arm_right") == 1
                and count("enakh_leg_left") == 1 and count("enakh_leg_right") == 1,
            "fallen statue multivar " .. var("varb1587_enakh_fallen_statue_multivar") .. " (want 63, Quest Helper gottenLimbs); limbs "
                .. count("enakh_arm_left") .. count("enakh_arm_right") .. count("enakh_leg_left") .. count("enakh_leg_right"))

        -- M sigil: the pedestal south of Lazim, in the room the fall lands in.
        t.exec("takeM", t.player.click_loc, "enakh_pedestal_sigil_m", 1)
        t.inv.await("enakh_sigil_m", 1, 5)
        t.check("takeM.sigil", count("enakh_sigil_m") == 1, "enakh_sigil_m x" .. count("enakh_sigil_m"))

        -- Anticlockwise round the outer rooms (wiki: "You should travel anticlockwise for the cut
        -- scenes to be in the correct order"): each limb door takes its limb on the first press.
        t.exec("walk-enterDoor1", t.player.walk_to, 3128, 9336, 40)
        t.exec("enterDoor1", t.player.cross_gate, { loc = "enakh_door_right_arm", at = { 3126, 9337, 0 },
            near = { 3127, 9337 }, far_ok = function(at) return at.x <= 3126 end,
            far_desc = "the north room, x <= 3126",
            chat = { "player:Ow, my head...", "npc:At last, it's complete." } })
        t.check("enterDoor1.lock", var("varb1608_enakh_right_armlock") == 1 and count("enakh_arm_right") == 0,
            "enakh_right_armlock " .. var("varb1608_enakh_right_armlock") .. ", right arm x" .. count("enakh_arm_right"))
        t.exec("takeZ", t.player.click_loc, "enakh_pedestal_sigil_z", 1)
        t.inv.await("enakh_sigil_z", 1, 5)
        t.check("takeZ.sigil", count("enakh_sigil_z") == 1, "enakh_sigil_z x" .. count("enakh_sigil_z"))

        t.exec("walk-enterDoor2", t.player.walk_to, 3079, 9336, 40)
        t.exec("enterDoor2", t.player.cross_gate, { loc = "enakh_door_left_leg", at = { 3079, 9334, 0 },
            near = { 3079, 9335 }, far_ok = function(at) return at.z <= 9334 end,
            far_desc = "the west room, z <= 9334",
            chat = { "player:Ow, my head...", "npc:All right, men", "npc:You will never have this temple",
                "mesbox:Enakhra casts a spell that kills", "npc:F-for Avarrocka!", "npc:Ah ha ha ha ha!",
                "mesbox:Enakhra casts a spell that freezes", "npc:Aaaaaargh!" } })
        t.check("enterDoor2.lock", var("varb1609_enakh_left_leglock") == 1 and count("enakh_leg_left") == 0,
            "enakh_left_leglock " .. var("varb1609_enakh_left_leglock") .. ", left leg x" .. count("enakh_leg_left"))
        t.exec("takeK", t.player.click_loc, "enakh_pedestal_sigil_k", 1)
        t.inv.await("enakh_sigil_k", 1, 5)
        t.check("takeK.sigil", count("enakh_sigil_k") == 1, "enakh_sigil_k x" .. count("enakh_sigil_k"))

        t.exec("walk-enterDoor3", t.player.walk_to, 3080, 9288, 40)
        t.exec("enterDoor3", t.player.cross_gate, { loc = "enakh_door_left_arm", at = { 3082, 9287, 0 },
            near = { 3081, 9287 }, far_ok = function(at) return at.x >= 3082 end,
            far_desc = "the south room, x >= 3082",
            chat = { "player:Ow, my head...", "npc:Well, why would Zamorak want a temple?", "npc:I wish I'd never supported him.",
                "npc:So you've seen sense?", "npc:Of course.", "mesbox:Akthanakos walks away", "npc:Heh heh heh..." } })
        t.check("enterDoor3.lock", var("varb1607_enakh_left_armlock") == 1 and count("enakh_arm_left") == 0,
            "enakh_left_armlock " .. var("varb1607_enakh_left_armlock") .. ", left arm x" .. count("enakh_arm_left"))
        t.exec("takeR", t.player.click_loc, "enakh_pedestal_sigil_r", 1)
        t.inv.await("enakh_sigil_r", 1, 5)
        t.check("takeR.sigil", count("enakh_sigil_r") == 1, "enakh_sigil_r x" .. count("enakh_sigil_r"))

        t.exec("walk-enterDoor4", t.player.walk_to, 3128, 9288, 40)
        t.exec("enterDoor4", t.player.cross_gate, { loc = "enakh_door_right_leg", at = { 3129, 9290, 0 },
            near = { 3129, 9289 }, far_ok = function(at) return at.z >= 9290 end,
            far_desc = "back in the east room, z >= 9290",
            chat = { "player:Ow, my head...", "npc:Surely this would make a good weapon...",
                "mesbox:Enakhra casts a spell on some bones", "npc:No, no! Curse it" } })
        t.check("enterDoor4.lock", var("varb1610_enakh_right_leglock") == 1 and count("enakh_leg_right") == 0,
            "enakh_right_leglock " .. var("varb1610_enakh_right_leglock") .. ", right leg x" .. count("enakh_leg_right"))

        -- The centre: the K door from the east room, then the M door (Quest Helper's enterMDoor)
        -- out to the west room and back ("Try to open the doors to place the sigils").
        -- The east room is one open room from the leg door to the K door (comp.py: one component).
        t.exec("goto-enterKDoor", t.player.goto_tile, 3113, 9312, 0)
        t.exec("enterKDoor", t.player.cross_gate, { loc = "enakh_door_k_sigil", at = { 3111, 9312, 0 },
            near = { 3112, 9312 }, far_ok = function(at) return at.x <= 3110 end,
            far_desc = "the centre room, x <= 3110", chat = { "mesbox:You place the sigil in the lock." } })
        t.check("enterKDoor.placed", var("varb1614_enakh_k_door") == 1 and count("enakh_sigil_k") == 0,
            "enakh_k_door " .. var("varb1614_enakh_k_door") .. ", K sigil x" .. count("enakh_sigil_k"))
        t.exec("walk-enterMDoor", t.player.walk_to, 3099, 9312, 40)
        t.exec("enterMDoor", t.player.cross_gate, { loc = "enakh_door_m_sigil", at = { 3097, 9312, 0 },
            near = { 3098, 9312 }, far_ok = function(at) return at.x <= 3097 end,
            far_desc = "the west room, x <= 3097", chat = { "mesbox:You place the sigil in the lock." } })
        t.check("enterMDoor.placed", var("varb1612_enakh_m_door") == 1 and count("enakh_sigil_m") == 0,
            "enakh_m_door " .. var("varb1612_enakh_m_door") .. ", M sigil x" .. count("enakh_sigil_m"))
        t.exec("enterMDoor-back", t.player.cross_gate, { loc = "enakh_door_m_sigil", at = { 3097, 9312, 0 },
            near = { 3097, 9312 }, far_ok = function(at) return at.x >= 3098 end,
            far_desc = "the centre room, x >= 3098" })

        t.exec("goUpToPuzzles", t.player.climb, { loc = "enakh_temple_ladderup", at = { 3104, 9309, 0 },
            op_name = "Climb-up", dest = { 3104, 9310, 1 } })
        t.check("goUpToPuzzles.seenPedestal", var("varb1618_enakh_seen_pedestal") == 1,
            "enakh_seen_pedestal " .. var("varb1618_enakh_seen_pedestal") .. " (Quest Helper goneUpstairs)")

        -- ------------------------------------------------ the pedestal
        local pedestal = t.player.by_symbol("loc", "enakh_pedestal_multiloc")
        t.exec("useSoftClayOnPedestal", t.player.use_on, "softclay", pedestal)
        t.exec("useSoftClayOnPedestal-page", t.chat.play, { "mesbox:You press the soft clay into the hollow" })
        t.check("useSoftClayOnPedestal.mould", count("enakh_camel_mould_positive") == 1 and count("softclay") == 0,
            "camel mould x" .. count("enakh_camel_mould_positive") .. ", soft clay x" .. count("softclay"))
        t.exec("useChiselOnGranite", t.player.use_item_on_item, "enakh_granite_medium", "chisel")
        t.inv.await("enakh_stone_head_akthanakos", 1, 5)
        t.check("useChiselOnGranite.head", count("enakh_stone_head_akthanakos") == 1 and count("enakh_granite_medium") == 0,
            "enakh_stone_head_akthanakos x" .. count("enakh_stone_head_akthanakos") .. ", granite x" .. count("enakh_granite_medium"))
        t.exec("useStoneHeadOnPedestal", t.player.use_on, "enakh_stone_head_akthanakos", pedestal)
        t.exec("useStoneHeadOnPedestal-dialog", t.chat.play, {
            "mesbox:You place the stone head into the hollow. It fits exactly.", "player:Ow, not another headache...",
            "npc:But it's still a nice temple", "npc:Wait... What is this?", "npc:Ha ha ha. You believed me",
            "npc:I would never desert my lord!", "mesbox:Akthanakos is frozen.",
        })
        t.check("quest.stage.puzzles", t.quest.expect_stage("puzzles"))

        -- ------------------------------------------------ the four rooms
        -- Blood (north-west): Pentyn.
        t.exec("walk-useBread", t.player.walk_to, 3093, 9322, 40)
        local pentyn = t.player.by_symbol("npc", "enakh_pentyn")
        t.exec("useBread", t.player.use_on, "bread", pentyn)
        t.exec("useBread-dialog", t.chat.play, {
            "player:*", "npc:My daughter was learning to cook this", "npc:I haven't remembered her",
            "player:Are you feeling better now?", "npc:Yes, thank you.", "npc:The food you gave me",
            "npc:I'm afraid that I'm trapped here", "npc:If you need any hints",
            "choose:It's okay, I don't need any help.", "player:It's okay, I don't need any help.",
        })
        t.check("useBread.fed", var("varb1576_enakh_blood_room") == 1 and count("bread") == 0,
            "enakh_blood_room " .. var("varb1576_enakh_blood_room") .. ", bread x" .. count("bread"))

        -- Ice (south-west): Fire Bolt on the crust of ice.
        t.exec("walk-castFireSpell", t.player.walk_to, 3094, 9309, 40)
        t.exec("castFireSpell", t.player.cast, "fire_bolt", "enakh_dummy_fountain_multinpc", 12)
        t.var.await_server("varb1577_enakh_ice_room", 1, 10)
        t.check("castFireSpell.melted", var("varb1577_enakh_ice_room") == 1,
            "enakh_ice_room " .. var("varb1577_enakh_ice_room") .. "; fire runes x" .. count("firerune"))

        -- Smoke (north-east): Wind Bolt on the furnace grate.
        t.exec("walk-castAirSpell", t.player.walk_to, 3113, 9322, 40)
        t.exec("castAirSpell", t.player.cast, "wind_bolt", "enakh_dummy_furnace_multinpc", 12)
        t.var.await_server("varb1578_enakh_smoke_room", 1, 10)
        t.check("castAirSpell.cleared", var("varb1578_enakh_smoke_room") == 1,
            "enakh_smoke_room " .. var("varb1578_enakh_smoke_room"))

        -- Shadow (south-east): the six braziers, each with its own fuel, lit with the tinderbox.
        local BRAZIERS = {
            { "useLog", "logs", "enakh_brazier_1_multiloc", "varb1581_enakh_brazier_1_multivar" },
            { "useOakLog", "oak_logs", "enakh_brazier_2_multiloc", "varb1582_enakh_brazier_2_multivar" },
            { "useWillowLog", "willow_logs", "enakh_brazier_3_multiloc", "varb1583_enakh_brazier_3_multivar" },
            { "useMapleLog", "maple_logs", "enakh_brazier_4_multiloc", "varb1584_enakh_brazier_4_multivar" },
            { "useCandle", "unlit_candle", "enakh_brazier_5_multiloc", "varb1585_enakh_brazier_5_multivar" },
            { "useCoal", "coal", "enakh_brazier_6_multiloc", "varb1586_enakh_brazier_6_multivar" },
        }
        t.exec("walk-useLog", t.player.walk_to, 3116, 9307, 40)
        for _, b in ipairs(BRAZIERS) do
            local brazier = t.player.by_symbol("loc", b[3])
            t.exec(b[1], t.player.use_on, b[2], brazier)
            t.var.await_server(b[4], 1, 8)
            t.check(b[1] .. ".lit", var(b[4]) == 1 and count(b[2]) == 0, b[4] .. " " .. var(b[4]) .. ", " .. b[2] .. " x" .. count(b[2]))
        end
        t.check("useCoal.shadowRoom", var("varb1579_enakh_shadow_room") == 1,
            "enakh_shadow_room " .. var("varb1579_enakh_shadow_room") .. " (all six braziers)")
        t.check("quest.stage.first_boneguard", t.quest.expect_stage("first_boneguard"))

        -- ------------------------------------------------ the barrier and the top floor
        t.exec("walk-passBarrier", t.player.walk_to, 3104, 9317, 40)
        t.exec("passBarrier", t.player.cross_gate, { loc = "enakh_magic_wall", at = { 3104, 9319, 1 },
            near = { 3104, 9318 }, far_ok = function(at) return at.z >= 9320 end, far_desc = "north of the barrier, z >= 9320" })
        t.exec("goUpFromPuzzleRoom", t.player.climb, { loc = "enakh_temple_ladderup", at = { 3104, 9332, 1 },
            op_name = "Climb-up", dest = { 3105, 9332, 2 }, slack = 1 })

        -- Crumble Undead must LAND (wiki): cast again on a splash.
        t.exec("walk-castCrumbleUndead", t.player.walk_to, 3104, 9310, 40)
        for cast = 1, 5 do
            if stage() >= 50 then break end
            t.exec("castCrumbleUndead" .. (cast > 1 and ("-recast" .. cast) or ""), t.player.cast, "crumble_undead",
                "enakh_boneguard_multinpc", 14)
            t.var.await_server("varb1560_enakh_quest", 50, 8)
        end
        t.exec("castCrumbleUndead-spirit", t.chat.play, {
            "npc:Thank you, kind", "npc:I no longer need these bones",
        })
        t.check("quest.stage.second_boneguard", t.quest.expect_stage("second_boneguard"))
        -- Over the Boneguard's bones (enakh_bone_pile, Climb-over).
        local before_bones = reading()
        t.exec("goDownToFinalRoom-climbBones", t.player.talk_to, "enakh_boneguard_multinpc", 1)
        t.await({ level = function() local at = tile() return at ~= nil and at.z <= 9306 end, note = "over the bones" }, 8)
        local past = tile()
        t.check("goDownToFinalRoom-climbBones.over", past ~= nil and past.z <= 9306 and past.level == 2,
            "from " .. before_bones .. " to " .. reading() .. " (want south of the bones, z <= 9306)")
        t.exec("goDownToFinalRoom", t.player.climb, { loc = "enakh_temple_pillar_ladder_top", at = { 3105, 9300, 2 },
            op_name = "Climb-down", dest = { 3105, 9299, 1 }, slack = 1 })

        -- ------------------------------------------------ Akthanakos' Boneguard
        t.exec("walk-protectThenTalk", t.player.walk_to, 3105, 9299, 20)
        t.ui.tab("prayer")
        t.ticks(1)
        local widget_result, widget = t.ui.widget("prayerbook:prayer15")
        t.check("protectThenTalk-prayerwidget", widget_result == "ok", "prayerbook:prayer15 -> " .. tostring(widget_result))
        t.ui.invoke(widget, 1)
        t.ticks(2)
        t.check("protectThenTalk-prayerOn", var("varb4118_prayer_protectfrommelee") == 1,
            "varb4118_prayer_protectfrommelee " .. var("varb4118_prayer_protectfrommelee"))
        t.ui.tab("inventory")
        t.exec("protectThenTalk", t.player.talk_to, "enakh_akthanakos_boneguard_multinpc", 1)
        t.exec("protectThenTalk-dialog", t.chat.play, {
            "npc:Leave this place, before I am forced to attack!", "npc:If you value your life",
            "npc:You should not be here!", "npc:I am impressed", "npc:Perhaps, then, you could help me",
            "player:Why, what's wrong?", "npc:I am trapped in this form",
            "choose:Of course, I'll help you out.",
            "player:Of course, I'll help you out.", "player:I, er, was just wondering...", "npc:Certainly",
            "player:That's good enough for me.", "npc:Enakhra keeps me trapped", "npc:Her hold over me grew weaker",
            "npc:Unfortunately, she has strengthened", "npc:I want you to finish building this wall",
            "choose:Okay, I'll start building.", "player:Okay, I'll start building.", "npc:Thank you. Once you are finished",
        })
        t.check("quest.stage.wall", t.quest.expect_stage("wall"))
        t.check("protectThenTalk.dodged", var("varb1616_enakh_akthanakos_hits_dodged") == 3,
            "enakh_akthanakos_hits_dodged " .. var("varb1616_enakh_akthanakos_hits_dodged"))
        -- Prayer off again for the building.
        t.ui.tab("prayer")
        t.ticks(1)
        local off_result, off_widget = t.ui.widget("prayerbook:prayer15")
        if off_result == "ok" then t.ui.invoke(off_widget, 1) end
        t.ticks(2)
        t.ui.tab("inventory")

        -- ------------------------------------------------ the wall
        local wall = t.player.by_symbol("loc", "enakh_largewall_l_multiloc")
        for load = 1, 3 do
            t.exec("repairWall-takeRock" .. load, t.player.click_loc, "enakh_rubblepile_by_largewall", 1)
            t.inv.await("enakh_sandstone_medium", 1, 6)
            t.exec("repairWall" .. (load > 1 and ("-load" .. load) or ""), t.player.use_on, "enakh_sandstone_medium", wall)
            t.var.await_server("varb1620_enakh_largewall_needs_trimming", 1, 6)
            t.exec("useChiselOnWall" .. (load > 1 and ("-load" .. load) or ""), t.player.use_on, "chisel", wall)
            t.var.await_server("varb1602_enakh_largewall_multivar", load, 6)
            t.check("useChiselOnWall.load" .. load, var("varb1602_enakh_largewall_multivar") == load,
                "enakh_largewall_multivar " .. var("varb1602_enakh_largewall_multivar") .. " (want " .. load .. ")")
        end
        t.exec("useChiselOnWall-enakhra", t.chat.play, { "npc:No! What have you done?" })

        -- ------------------------------------------------ Akthanakos freed
        local skill_snapshot_result, skill_snapshot = t.skill.snapshot()
        t.step("skill.snapshot", skill_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot -> " .. tostring(skill_snapshot_result))
        t.exec("talkToAkthankos", t.player.talk_to, "enakh_akthanakos_boneguard_multinpc", 1)
        t.exec("talkToAkthankos-dialog", t.chat.play, {
            "player:The wall's finally built!", "npc:Thank you. Thank you very much", "npc:I can feel Enakhra's control",
            "npc:After thousands of years", "npc:As thanks for your help", "player:Wow. Er, what does it do?",
            "npc:It allows you to understand", "npc:It is also bound to the desert",
            "npc:After so many years", "npc:Now my lord will punish you", "npc:Ha ha... in that case",
            "mesbox:Akthanakos and Enakhra transform", "npc:I'll be waiting for you in the north",
            "npc:It'll be a pleasure to fight you once more",
        })
        t.quest.expect_complete()
        t.check("reward.camulet", count("camulet") == 1, "camulet x" .. count("camulet"))
        t.check("reward.crafting", t.skill.expect_gain("crafting", 7000, skill_snapshot))
        t.check("reward.mining", t.skill.expect_gain("mining", 7000, skill_snapshot))
        t.check("reward.firemaking", t.skill.expect_gain("firemaking", 7000, skill_snapshot))
        t.check("reward.magic", t.skill.expect_gain("magic", 7000, skill_snapshot))
    end,
}
