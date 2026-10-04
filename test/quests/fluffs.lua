-- Gertrude's Cat (quest_fluffs). Hand-authored from the scaffold, resumed
-- after t.player.use_item_on_item landed (was BLOCKED on the missing OPHELDU
-- verb) and again after a review rejection for two missing item-reward
-- rows; see QUEUE.tsv's last_failure history for both. Re-driven in
-- matthew-mbp-m4-b57 for the closed-space rule: no goto lands in or leaves
-- Gertrude's house or the fenced lumber yard, and every level change is the
-- ladder itself.
--
-- Flow (areas/varrock/scripts/gertrude.rs2, quests/quest_fluffs/scripts/
-- quest_fluffs.rs2): talk to Gertrude and accept -> talk to Shilop, pay 100
-- coins for the lumber-mill tip -> find Fluffs (gertrudescat, a public map
-- npc, always spawned) and give her the milk -> pick doogle leaves, rub them
-- on a raw sardine (item-on-item, OPHELDU, quest_fluffs.rs2:142-168) to
-- season it, feed Fluffs -> she sends kittens mewing from one of six crates
-- (%fluffs_crate = random(6), quest_fluffs.rs2:124/248, tiles decoded from
-- quest_fluffs.constant's ^fluffs_crate_0..5) -> give the found kitten back
-- to Fluffs -> hand in to Gertrude for the reward.
--
-- Driven for real throughout: Gertrude's front door (fai_varrock_door,
-- 3151,3412, maps/m49_53.jl2) opened on the way in and walked through on the
-- way out; Gertrude's accept dialogue; paying Shilop the 100 coins (a real
-- p_choice3 -> p_choice2 branch with the coins actually deducted); the lumber
-- yard's broken fence (gertrudefence 3308,3492, [oploc1,gertrudefence]
-- quest_fluffs.rs2:126-140, an exactmove between 3308,3491 and 3308,3492)
-- climbed in and out on every visit; the yard's ladder (fai_varrock_ladder /
-- fai_varrock_laddertop at 3310,3509, ~climb's same-tile plane change, no
-- maplink row) climbed up to the loft and down again every time; using the
-- milk on Fluffs (a real OPNPCU click, %fluffs actually advances); picking a
-- live doogleleaves obj; seasoning the sardine with a real use_item_on_item
-- (OPHELDU) press; feeding it to Fluffs; walking to each of the six live
-- kittens_mew crate npcs and searching that copy by its tile until the one
-- matching %fluffs_crate answers with the kitten mesbox (every other one must
-- answer "You find nothing."); giving the found kitten back to Fluffs
-- (opnpcu, gertrudekittens); and hand-in to Gertrude (opnpc1, %fluffs=rescued
-- branch) which settles the rewards synchronously (~fluffs_settle_rewards):
-- 1525 Cooking XP, a random one of six pet-kitten colours
-- (~gertrude_give_cat), a Chocolate Cake and a Stew (quest_fluffs.rs2:333-356).
-- No fight: Fluffs' scratch (~fluffs_attack) is only on opnpc1/opnpc3, never
-- pressed here.

return {
    id = "fluffs",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::give bucket_milk 1", -- Quest Helper prerequisite -- not obtainable during the quest
        "::give raw_sardine 1", -- Quest Helper prerequisite
        "::give coins 100", -- Quest Helper prerequisite -- Shilop's price, paid via dialogue below
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp180_fluffs",
            constants = {
                complete = 6,
                gave_milk = 3,
                gave_sardine = 4,
                not_started = 0,
                paid_boy = 2,
                questpoints = 1,
                rescued = 5,
                started = 1,
            },
            row = "quest_gertrudescat",
            display = "Gertrude's Cat",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effects are not client-side yet

        local function tile_text(r, tt)
            return r == "ok" and (tt.x .. "," .. tt.z .. "," .. tt.level) or tostring(r)
        end

        -- Cross one door on foot. Walk to the tile on this side of it; if the
        -- closed leaf stands at door_x,door_z, click THAT copy; otherwise an
        -- earlier press left it open (doors swing back after 500 ticks), so
        -- assert the open leaf is really standing on or beside the door tile
        -- -- a row that fails when neither leaf is there -- and do not press it
        -- again. Then walk to the far side and check the tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1 and nt.level == 0,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local cr, cd = t.world.loc_near(closed_sym, 3)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".doorStandsOpen", orr == "ok" and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": "
                        .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z) or tostring(cr))
                        .. "; " .. open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z) or tostring(orr))
                        .. " (want within 1 of the door tile: still open from the earlier press, so walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- Gertrude's house: the room x 3148-3153 z 3404-3411 (maps/m49_53.jl2),
        -- its front door fai_varrock_door on the south edge of 3151,3412.
        local function inside_house(ft)
            return ft.level == 0 and ft.x >= 3148 and ft.x <= 3153 and ft.z >= 3404 and ft.z <= 3411
        end
        local function enter_house(prefix)
            pass_door(prefix, "fai_varrock_door", "fai_varrock_door_open", 3151, 3412, 3151, 3413, 3151, 3411,
                inside_house, "inside Gertrude's house, x 3148-3153 z 3404-3411")
        end
        local function leave_house(prefix)
            pass_door(prefix, "fai_varrock_door", "fai_varrock_door_open", 3151, 3412, 3151, 3411, 3151, 3413,
                function(ft) return ft.level == 0 and ft.z >= 3412 end, "outside the front door, z >= 3412")
        end

        -- The lumber yard's broken fence. [oploc1,gertrudefence] is an
        -- exactmove between 3308,3491 (outside) and 3308,3492 (the fence
        -- tile, inside): a one-tile hop after a route, so click_loc can read
        -- `timeout` on a crossing that landed (start-and-travel: "A short hop
        -- does not trip it"). It is called directly and graded on the tile,
        -- which only the crossing reaches (3308,3491 -> 3308,3493 has no
        -- static route at margin 40).
        local function cross_fence(prefix, entering)
            local near_z = entering and 3491 or 3493
            t.player.walk_to(3308, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atFence", nr == "ok" and nt.x == 3308 and nt.z == near_z and nt.level == 0,
                "walked to 3308," .. near_z .. " beside the broken fence at 3308,3492 -> " .. tile_text(nr, nt))
            local cr, cd = t.player.click_loc("gertrudefence", 1, { at = { 3308, 3492 } })
            local fr, ft
            for _ = 1, 8 do
                t.ticks(1)
                fr, ft = t.world.tile()
                if fr == "ok" and ((entering and ft.z >= 3492) or ((not entering) and ft.z <= 3491)) then
                    break
                end
            end
            t.check(prefix .. ".crossed", fr == "ok" and ft.x == 3308 and ft.level == 0
                    and ((entering and ft.z == 3492) or ((not entering) and ft.z == 3491)),
                "click_loc(gertrudefence) -> " .. tostring(cr) .. " " .. tostring(cd) .. "; tile " .. tile_text(fr, ft)
                    .. " (want " .. (entering and "3308,3492,0 inside the yard" or "3308,3491,0 outside the yard") .. ")")
        end

        -- The loft ladder at 3310,3509: ~climb moves the player to the same
        -- tile one plane up or down (no maplink row names it).
        local function climb(name, sym, want_level)
            t.exec(name, t.player.click_loc, sym, 1, { at = { 3310, 3509 } })
            -- the click can settle on the walk's map_flag arm when the route
            -- ends beside the ladder, before ~climb_ladder's anim + p_delay
            -- moves the plane: poll the tile for the climb itself.
            local cr, ct
            for _ = 1, 8 do
                cr, ct = t.world.tile()
                if cr == "ok" and ct.level == want_level then
                    break
                end
                t.ticks(1)
            end
            t.check(name .. ".level", cr == "ok" and ct.level == want_level and math.abs(ct.x - 3310) <= 1 and math.abs(ct.z - 3509) <= 1,
                "tile after " .. sym .. " -> " .. tile_text(cr, ct) .. " (want level " .. want_level .. " beside 3310,3509)")
        end

        -- Outside the yard's south fence -> over the fence -> up the ladder.
        local function up_to_loft(goto_name, ladder_name)
            t.exec(goto_name, t.player.goto_tile, 3308, 3490, 0)
            cross_fence(ladder_name .. ".fenceIn", true)
            climb(ladder_name, "fai_varrock_ladder", 1)
        end

        -- Talk to Gertrude west of Varrock (gertrude.rs2:10) and accept. The
        -- goto lands on the street north of her front door; the door is opened
        -- and walked through.
        t.exec("goto-talkToGertrude", t.player.goto_tile, 3151, 3414, 0)
        enter_house("talkToGertrude.door")
        t.exec("talkToGertrude", t.player.talk_to, "gertrude", 1)
        t.exec("talkToGertrude-dialog", t.chat.play, {
            "player:Hello, are you okay?",
            "npc:Do I look ok? Those kids drive me crazy.",
            "npc:I'm sorry. It's just that I've lost her.",
            "player:Lost who?",
            "npc:Fluffs, poor Fluffs. She never hurt anyone.",
            "player:Who's Fluffs?",
            "npc:My beloved feline friend Fluffs. She's been purring by my side for almost a decade. Please, could you go search for her while I look over the kids?",
            "choose:Well, I suppose I could.",
            "player:Well, I suppose I could.",
            "npc:Really? Thank you so much! I really have no idea where she could be!",
            "npc:I think my sons, Shilop and Wilough, saw the cat last. They'll be out in the market place.",
            "player:Alright then, I'll see what I can do.",
        })
        t.exec("quest.stage.started", t.quest.expect_stage, "started")
        leave_house("talkToGertrude.leave")

        -- Talk to Shilop in Varrock Square (fluffs_boy_dialogue -> fluffs_boy
        -- -> fluffs_boy_secondary, "What will make you tell me?" -> fluffs_pay,
        -- "Okay then, I'll pay." -- pays the 100 coins for real).
        t.exec("goto-talkToShilop", t.player.goto_tile, 3221, 3434, 0)
        t.exec("talkToShilop", t.player.talk_to, "shilop", 1)
        t.exec("talkToShilop-dialog", t.chat.play, {
            "player:Hello there, I've been looking for you.",
            "npc:I didn't mean to take it! I just forgot to pay.",
            "player:What? I'm trying to help your mum find Fluffs.",
            "npc:I might be able to help. Fluffs followed me to our secret play area and I haven't seen her since.",
            "player:Where is this play area?",
            "npc:If I told you that, it wouldn't be a secret.",
            "choose:What will make you tell me?",
            "player:What will make you tell me?",
            "npc:Well...now you ask, I am a bit short on cash.",
            "player:How much?",
            "npc:10 coins.",
            "npc:10 coins?!",
            "npc:I'll handle this.",
            "npc:100 coins should cover it.",
            "player:100 coins! Why should I pay you?",
            "npc:You shouldn't, but we won't help otherwise. We never liked that cat anyway, so what do you say?",
            "choose:Okay then, I'll pay.",
            "player:Okay then, I'll pay.",
            "mesbox:You give the lad 100 coins.",
            "player:There you go, now where did you see Fluffs ?",
            "npc:We play at an abandoned lumber mill to the north east. Just beyond the Jolly Boar Inn. I saw Fluffs running around in there.",
            "player:Anything else?",
            "npc:Well, you'll have to find the broken fence to get in. I'm sure you can manage that.",
        })
        t.exec("quest.stage.paid_boy", t.quest.expect_stage, "paid_boy")
        local coins_result, coins_left = t.inv.count("coins")
        t.check("spentCoins", coins_result == "ok" and coins_left == 0,
            "coins after paying Shilop 100 = " .. tostring(coins_left) .. " (read " .. tostring(coins_result) .. ")")

        -- Fluffs (gertrudescat) is upstairs in the lumber yard loft,
        -- ^fluffs_cat_coord = 1_51_54_42_56 -> 3306,3512,1. Over the broken
        -- fence, up the ladder, and give her the milk (OPNPCU,
        -- quest_fluffs.rs2:216-234).
        up_to_loft("goto-fluffsCat", "climbLadder")
        local cat = t.player.by_symbol("npc", "gertrudescat")
        t.exec("giveMilkToFluffs", t.player.use_on, "bucket_milk", cat)
        -- OPNPCU's own p_delay (quest_fluffs.rs2:229-234) lands after
        -- use_on's settle resolves (measured 2026-09-19: the settle's
        -- `map_flag` arm can win the race before the server-side write) --
        -- not client-side yet without this.
        t.ticks(3)
        t.exec("quest.stage.gave_milk", t.quest.expect_stage, "gave_milk")
        local milk_result, milk_left = t.inv.count("bucket_milk")
        t.check("milkConsumed", milk_result == "ok" and milk_left == 0,
            "bucket_milk after feeding Fluffs = " .. tostring(milk_left) .. " (read " .. tostring(milk_result) .. ")")
        t.expect("milkBucketEmptied", t.inv.expect_has("bucket_empty", 1)) -- inv_add(inv, bucket_empty, 1), quest_fluffs.rs2:233

        -- Down the ladder and back out over the fence before travelling.
        climb("leaveLoft1", "fai_varrock_laddertop", 0)
        cross_fence("leaveYard1.fenceOut", false)

        -- Doogle leaves grow behind Gertrude's house, in the open (m49_53.spawn
        -- OBJ block, e.g. 3151,3399; a static route from the street north of
        -- the house reaches it with every door closed). Pick one up -- the
        -- seasoning step past this needs it and the raw sardine already carried.
        t.exec("goto-pickDoogleLeaves", t.player.goto_tile, 3151, 3399, 0)
        -- click_obj answers `ok` with a nil detail on this path (the await
        -- branch, pointer.lua:1431-1437) -- hollow through t.exec (measured
        -- 2026-09-19: "why=hollow -- ok with no detail"). Call it directly.
        local pick_result, pick_detail = t.player.click_obj("doogleleaves")
        t.check("pickDoogleLeaves", pick_result == "ok",
            "click_obj(doogleleaves) -> " .. tostring(pick_result) .. " " .. tostring(pick_detail))
        t.expect("haveDoogleLeaves", t.inv.expect_has("doogleleaves", 1))
        t.expect("haveRawSardine", t.inv.expect_has("raw_sardine", 1))

        -- Both ingredients for the seasoned sardine (quest_fluffs.rs2:142-168,
        -- [proc,fluffs_make_sardine] via [opheldu,doogleleaves]/[opheldu,
        -- raw_sardine]) are now in the backpack -- a real item-on-item OPHELDU
        -- press (arms doogleleaves, clicks raw_sardine's cell). The recipe
        -- pauses on its own ~mesbox ("You rub the doogle leaves over the
        -- sardine."), so the verb settles on that page and the seasoned
        -- sardine only lands once it is dismissed (section 8's gap note).
        t.exec("makeSeasonedSardine", t.player.use_item_on_item, "doogleleaves", "raw_sardine")
        local sardine_close_result, sardine_close_detail = t.chat.continue_()
        t.check("makeSeasonedSardine-dismiss", sardine_close_result == "ok",
            "chat.continue_ after makeSeasonedSardine -> " .. tostring(sardine_close_result)
                .. " " .. tostring(sardine_close_detail))
        local sardine_await_result = t.inv.await("seasoned_sardine", 1, 10)
        t.check("makeSeasonedSardine-sync", sardine_await_result == "ok",
            "inv.await seasoned_sardine 1 -> " .. tostring(sardine_await_result))
        t.expect("haveSeasonedSardine", t.inv.expect_has("seasoned_sardine", 1))

        -- Feed the seasoned sardine to Fluffs (OPNPCU, quest_fluffs.rs2:235-
        -- 248) -- back over the fence and up the ladder to the loft,
        -- gertrudescat re-resolved fresh. This is also the action that rolls
        -- %fluffs_crate = random(6) for the kitten hunt.
        up_to_loft("goto-fluffsCat2", "climbLadder2")
        local cat2 = t.player.by_symbol("npc", "gertrudescat")
        t.exec("giveSardineToFluffs", t.player.use_on, "seasoned_sardine", cat2)
        t.ticks(3) -- OPNPCU's own p_delay lands after the click's settle, same race as the milk step
        t.exec("quest.stage.gave_sardine", t.quest.expect_stage, "gave_sardine")
        local sardine_result, sardine_left = t.inv.count("seasoned_sardine")
        t.check("sardineConsumed", sardine_result == "ok" and sardine_left == 0,
            "seasoned_sardine after feeding Fluffs = " .. tostring(sardine_left) .. " (read " .. tostring(sardine_result) .. ")")

        -- The crates are in the yard below the loft: down the ladder.
        climb("climbDownLadderStep", "fai_varrock_laddertop", 0)

        -- Six mewing-crate tiles, decoded from quest_fluffs.constant's
        -- ^fluffs_crate_0..5 (level_regionX_regionY_localX_localY, section 8's
        -- formula: worldX = regionX*64+localX, worldZ = regionY*64+localY).
        -- %fluffs_crate was just rolled to one of these six by the feed above
        -- -- [opnpc1,kittens_mew] (quest_fluffs.rs2:277-290) grants the
        -- kitten only when npc_coord matches it and answers "You find
        -- nothing." everywhere else. Each crate npc stands on a solid crate
        -- loc, so the player walks (inside the yard, no goto) to an open tile
        -- beside it (a = the static-route neighbour) and presses THAT copy by
        -- its tile.
        local crate_tiles = {
            { x = 3305, z = 3500, ax = 3306, az = 3500 }, -- ^fluffs_crate_0 = 0_51_54_41_44
            { x = 3310, z = 3499, ax = 3309, az = 3499 }, -- ^fluffs_crate_1 = 0_51_54_46_43
            { x = 3307, z = 3507, ax = 3307, az = 3506 }, -- ^fluffs_crate_2 = 0_51_54_43_51
            { x = 3303, z = 3506, ax = 3304, az = 3506 }, -- ^fluffs_crate_3 = 0_51_54_39_50
            { x = 3298, z = 3514, ax = 3298, az = 3513 }, -- ^fluffs_crate_4 = 0_51_54_34_58
            { x = 3315, z = 3515, ax = 3315, az = 3514 }, -- ^fluffs_crate_5 = 0_51_54_51_59
        }
        -- Chat lines come back newest first, each with a serial: a crate's
        -- answer must be a line newer than the floor read before its press.
        local function newest_serial()
            local r, list = t.msg.last(1)
            if r == "ok" and type(list) == "table" and list[1] then
                return list[1].serial
            end
            return 0
        end
        local function line_since(floor, substring)
            local r, list = t.msg.last(20)
            if r ~= "ok" or type(list) ~= "table" then
                return nil
            end
            for i = 1, #list do
                if list[i].serial > floor and string.find(list[i].text, substring, 1, true) then
                    return list[i].text
                end
            end
            return nil
        end
        local kitten_found = false
        local kitten_crate_index = nil
        for i = 1, #crate_tiles do
            if not kitten_found then
                local tile = crate_tiles[i]
                t.player.walk_to(tile.ax, tile.az, 30)
                local wr, wt = t.world.tile()
                t.check("searchCrate" .. i .. ".beside", wr == "ok" and wt.level == 0
                        and math.abs(wt.x - tile.x) + math.abs(wt.z - tile.z) == 1,
                    "walked to " .. tile.ax .. "," .. tile.az .. " beside crate " .. i .. " at " .. tile.x .. "," .. tile.z
                        .. " -> " .. tile_text(wr, wt))
                local floor = newest_serial()
                local click_result, click_detail = t.player.talk_to("kittens_mew", 1, { at = { tile.x, tile.z } })
                -- The answer comes after [opnpc1,kittens_mew]'s p_delay(4):
                -- the kitten mesbox, or a "You find nothing." chat line. This
                -- cluster sits against the wilderness line, so an unrelated
                -- warning page can also come up: it is dismissed, never read
                -- as the answer.
                local outcome, notes = nil, {}
                for _ = 1, 12 do
                    if t.chat.kind() ~= "none" then
                        local play_result, play_detail = t.chat.play({ "mesbox:You find a kitten!" })
                        if play_result == "ok" then
                            outcome = "kitten"
                            break
                        end
                        notes[#notes + 1] = "dismissed an unrelated page: " .. tostring(play_result) .. " " .. tostring(play_detail)
                        t.chat.continue_()
                    end
                    if line_since(floor, "You find nothing.") then
                        outcome = "nothing"
                        break
                    end
                    t.ticks(1)
                end
                local searched = line_since(floor, "You search the crate.")
                if outcome == "kitten" then
                    kitten_found = true
                    kitten_crate_index = i
                end
                t.check("searchCrate" .. i, outcome ~= nil and searched ~= nil,
                    "crate " .. i .. " at " .. tile.x .. "," .. tile.z .. " -- talk_to(kittens_mew) -> "
                        .. tostring(click_result) .. " " .. tostring(click_detail)
                        .. "; answer: " .. tostring(outcome) .. "; 'You search the crate.' line: " .. tostring(searched)
                        .. (#notes > 0 and ("; " .. table.concat(notes, "; ")) or ""))
            end
        end
        t.check("kittenFound", kitten_found == true,
            "kittens_mew crate search: found at crate index " .. tostring(kitten_crate_index)
                .. " of 6 (%varp5749_fluffs_crate matched)")
        t.expect("haveKitten", t.inv.expect_has("gertrudekittens", 1))

        -- Give the found kitten back to Fluffs (OPNPCU, quest_fluffs.rs2:249-
        -- 259) -- back up the ladder from the yard; she runs off home with her
        -- offspring, advancing to rescued.
        climb("climbUpLadderStep", "fai_varrock_ladder", 1)
        local cat3 = t.player.by_symbol("npc", "gertrudescat")
        t.exec("giveKittenToFluffs", t.player.use_on, "gertrudekittens", cat3)
        t.ticks(3) -- same OPNPCU settle race as the milk and sardine steps
        t.exec("quest.stage.rescued", t.quest.expect_stage, "rescued")
        local kitten_left_result, kitten_left = t.inv.count("gertrudekittens")
        t.check("kittenGivenConsumed", kitten_left_result == "ok" and kitten_left == 0,
            "gertrudekittens after giving to Fluffs = " .. tostring(kitten_left) .. " (read " .. tostring(kitten_left_result) .. ")")

        -- Down the ladder and out over the fence before travelling back.
        climb("leaveLoft3", "fai_varrock_laddertop", 0)
        cross_fence("leaveYard3.fenceOut", false)

        -- Hand in to Gertrude (opnpc1,gertrude's %fluffs=^fluffs_rescued
        -- branch, gertrude.rs2:49-61) -- opens with the PLAYER's line (trap
        -- 18). ~fluffs_settle_rewards fires synchronously right after the
        -- last mesbox is dismissed: 1525 Cooking XP, a random pet-kitten
        -- colour, a Chocolate Cake and a Stew. Snapshot cooking XP before the
        -- hand-in, per the reward rule (a read: no row of its own; the
        -- expect_gain below fails on a bad snapshot).
        local _, xp_snapshot = t.skill.snapshot()
        t.exec("goto-handInGertrude", t.player.goto_tile, 3151, 3414, 0)
        enter_house("handInGertrude.door")
        t.exec("handInGertrude", t.player.talk_to, "gertrude", 1)
        t.exec("handInGertrude-dialog", t.chat.play, {
            "player:Hello Gertrude. Fluffs ran off with her kitten.",
            "npc:You're back! Thank you! Thank you! Fluffs just came back! I think she was just upset as she couldn't find her kitten.",
            "mesbox:Gertrude gives you a hug.",
            "npc:If you hadn't found her kitten it would have died out there!",
            "player:That's okay, I like to do my bit.",
            "npc:I don't know how to thank you. I have no real material possessions. I do have kittens! I can only really look after one.",
            "player:Well, if it needs a home.",
            "npc:I would sell it to my cousin in West Ardougne. I hear there's a rat epidemic there. But it's too far.",
            "npc:Here you go, look after her and thank you again!",
            "mesbox:Gertrude gives you a kitten. And some food!",
        })
        t.ticks(3) -- fluffs_settle_rewards's own writes land behind the chat ack, same race as trap in section 8

        t.quest.expect_complete()

        -- Reward rows -- the literal grant quest_fluffs.rs2:333-356 makes,
        -- not a number read back from the scroll. The pet kitten is one of
        -- six random colours (~gertrude_give_cat's switch_int(random(6))),
        -- so the six documented colours are tried and exactly one must be
        -- present, then asserted by its own resolved name.
        local kitten_names = {
            "kittenobject", "kittenobject_light", "kittenobject_brown",
            "kittenobject_black", "kittenobject_browngrey", "kittenobject_bluegrey",
        }
        local kitten_seen, kitten_seen_count = nil, 0
        for i = 1, #kitten_names do
            local has_result, has = t.inv.has(kitten_names[i])
            if has_result == "ok" and has then
                kitten_seen = kitten_names[i]
                kitten_seen_count = kitten_seen_count + 1
            end
        end
        t.check("reward.kittenColour", kitten_seen_count == 1,
            "pet kitten reward: " .. tostring(kitten_seen) .. " present, "
                .. kitten_seen_count .. " of the six documented colours found")
        if kitten_seen then
            t.expect("reward.kitten", t.inv.expect_has(kitten_seen, 1))
        else
            t.expect("reward.kitten", "refused", "no pet-kitten colour found in inventory after hand-in")
        end
        t.expect("reward.chocolateCake", t.inv.expect_has("chocolate_cake", 1))
        t.expect("reward.stew", t.inv.expect_has("stew", 1))
        t.exec("reward.cookingXp", t.skill.expect_gain, "cooking", 1525, xp_snapshot)

        t.finish(0)
    end,
}
