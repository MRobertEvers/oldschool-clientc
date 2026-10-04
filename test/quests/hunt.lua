-- Pirate's Treasure ("hunt" test id / quest_hunt content dir) -- client-driven
-- end to end through the real client. Redbeard Frank asks for a bottle of
-- Karamja Rum, and redbeard_frank.rs2's [label,redboard_progress]/
-- [label,redbeard_hand_rum] only check inv_total(inv, karamja_rum) >= 1 --
-- but actually GETTING that bottle onto the mainland is a six-NPC/loc
-- smuggling job this pack wires for real (2026-09-20 review): buy a bottle
-- from the Deadman's Chest bartender in Brimhaven (deadmans_bartender.rs2,
-- 27 coins), get employed by Luthas at the banana plantation
-- (luthas.rs2's luthas_employment), stash the rum AND pack 10 bananas into
-- his export crate (banana_crate.rs2's oplocu -- customs_officer.rs2's
-- customs_spot_inspection/customs_search confiscate karamja_rum carried
-- directly, the entire reason the crate trick exists), pass the Karamja
-- customs officer's boarding search, get employed at Wydin's grocery store
-- in Port Sarim (needs a white apron -- wydin.rs2's wydin_job), and dig the
-- smuggled rum back out of his grocery crate (food_store.rs2's oploc1
-- grocerycrate). Then Hector's chest in the Blue Moon Inn (use the key on
-- it -- [oplocu,piratechest]; op1 alone answers "The chest is locked."),
-- reading the pirate message it holds ([opheld1,piratemessage]), and
-- digging at the Falador Park cross ([opheld1,spade] recognises the tile
-- 0_46_52_55_55 = 2999,3383,0 and jumps to quest_hunt/scripts/dig.rs2's
-- [label,hunt_dig]).
--
-- dig.rs2's hunt_dig redirects to [label,pirate_irate_gardener_attack]
-- instead of completing the dig when falador_gardener (hitpoints=7,
-- attack=1, aggressive) is within 10 tiles of the dig spot -- its own spawn
-- (m46_52.spawn) is ~3.6 tiles from 2999,3383, so the FIRST dig is expected
-- to provoke it rather than complete. It was never a click problem: attack
-- 1 / strength 1 with a bronze dagger loses to defence 7 over thirty
-- swings from one click (measured, build/quest_gate/q3probe2). The setup
-- gives combat levels and a rune scimitar instead (trap 16: gear is a
-- prerequisite, not the quest's own deliverable) and this file fights back
-- with t.player.attack/t.npc.await_dead, then goes back to the dig tile
-- for the real dig (proof: build/quest_gate/q3proof, gardener dead in 9
-- ticks and "You dig a hole in the ground...").
--
-- DOORS AND FLOORS (fix_b59, the closed-space rule of docs/
-- QUEST_ORCHESTRATOR.md): no goto lands in or leaves a closed space. Both
-- ships are left by their own gangplank (general_use/scripts/gangplank.rs2
-- ~gangplank_disembark); the white apron is the guide's, in the Port Sarim
-- fishing shop (m47_50.spawn:53, 3016,3229 -- the other copy at 3009,3204
-- lies INSIDE Wydin's employees-only back room); Wydin's shop and the fishing
-- shop are walked into and out of past their map-open doors; the back room
-- is entered AND left through wydindoor's own click (food_store.rs2
-- [oploc1,wydindoor] -> [proc,hunt_walk_door], a walk-through with no
-- opened loc); the Blue Moon Inn is entered on foot, climbed by
-- fai_varrock_stairs (maplink 3226,3393,0 <-> 3230,3393,1), and Hector's
-- room is opened by its fai_varrock_door (3222,3395,1), in and out.
-- Karamja's members' gate (membergatel 2816,3182, gates.rs2's
-- member_fencegate_try walk-through) is the only way on foot between Musa
-- Point and Brimhaven, so it is clicked going to the rum and coming back
-- (fix_b59 round 2, sampler-findings.md "Sample matthew-mbp-m4-b59" (a):
-- the size of the regions on either side earns no exemption).

return {
    id = "hunt",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        -- Two: the driver's own held-item op ALSO drops the clicked item as
        -- a side effect (pointer.lua's `_inv_dispatch` banner -- "on
        -- rev-239's backpack op 1 is the shift-click-drop chain", measured
        -- here on both piratemessage and spade), so the gardener-interrupted
        -- first dig below spends the first spade before the real dig ever
        -- runs.
        "::give spade 2",
        -- Coins for the rum-smuggling chain the quest's own scripts wire
        -- (trap 16: money in the pocket is a prerequisite, never the
        -- deliverable) -- 27 to buy the bottle from Deadman's Chest
        -- (deadmans_bartender.rs2) and 30 for the Karamja customs
        -- officer's boarding charge (customs_officer.rs2's customs_pay).
        -- The rum itself is fetched for real below, never cheated in.
        "::give coins 100",
        -- Prerequisite combat gear for the gardener the dig provokes (see
        -- dig-treasure below) -- not the quest's own deliverable (trap 16).
        -- attack 1 / strength 1 with a bronze dagger loses to defence 7
        -- (measured, build/quest_gate/q3probe2), so level up and arm a
        -- rune scimitar instead.
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::give rune_scimitar 1",
        -- Food for the same fight: the margin row below asserts the lowest
        -- hitpoints AND food left, and await_dead eats it below EAT_BELOW.
        "::give lobster 2",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp71_hunt",
            constants = {
                not_started = 0,
                fetch_rum = 1,
                received_key = 2,
                read_note = 3,
                complete = 4,
                questpoints = 2,
            },
            row = "quest_piratestreasure",
            display = "Pirate's Treasure",
            points = 2,
        })

        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------------- helpers
        local function txt(v)
            local kind = type(v)
            if kind == "string" or kind == "number" or kind == "boolean" or kind == "nil" then
                return tostring(v)
            end
            return "<" .. kind .. ">"
        end
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        -- Wait for a teleport the click queued (a gangplank, a walk-through
        -- door, a climb) to land; the row after it reads the tile.
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end

        -- Walk to x,z on the current floor and grade the tile reached.
        local function walk_check(name, x, z, ok_fn, desc)
            local wr = t.player.walk_to(x, z, 40)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and ok_fn(tt),
                "walk_to " .. x .. "," .. z .. " -> " .. txt(wr) .. "; tile " .. tile_text(r, tt) .. " (want " .. desc .. ")")
        end

        -- Cross one ordinary door on foot (the b56/b57 pass_door pattern).
        -- Walk to the tile on this side; if the CLOSED leaf stands on the door
        -- tile on the player's own level, click THAT copy; otherwise the door
        -- stands open (the map plants it open, or an earlier press left it
        -- so), so assert the OPEN leaf stands within a tile of the door on
        -- this level -- a row that fails when neither leaf is there -- and do
        -- not press it. Then walk through and check the far tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 40)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and nt.x == near_x and nt.z == near_z,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z
                    .. " -> " .. tile_text(nr, nt))
            local here = (nr == "ok") and nt.level or 0
            local cr, cd = t.world.loc_near(closed_sym, 3)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == here then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z, here } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".doorStandsOpen",
                    orr == "ok" and od.level == here
                        and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. "," .. here .. ": "
                        .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z .. "," .. cd.level) or tostring(cr))
                        .. "; " .. open_sym .. ": "
                        .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                        .. " (want the open leaf within 1 of the door tile on this level: it stands open, so it is walked through, not pressed)")
            end
            t.player.walk_to(far_x, far_z, 40)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- A click that TELEPORTS the player across (wydindoor's
        -- hunt_walk_door, a gangplank, a staircase): it leaves no opened loc,
        -- so it is clicked on every crossing, and a short teleport can answer
        -- `timeout settle_after_click` on a crossing that landed. The row is
        -- graded on the tiles -- the tile before the click NOT on the far
        -- side, the tile after it on the far side -- with the click's answer
        -- in the detail.
        local function cross(name, sym, at_x, at_z, at_level, near_x, near_z, far_ok, far_desc)
            if near_x ~= nil then
                t.player.walk_to(near_x, near_z, 30)
            end
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = { at_x, at_z, at_level } })
            await_tile(far_ok, 12, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and br == "ok" and not far_ok(bt) and wr == "ok" and far_ok(wt),
                "from " .. tile_text(br, bt) .. " click_loc(" .. sym .. " at " .. at_x .. "," .. at_z .. "," .. at_level .. ") -> "
                    .. tostring(cr) .. " " .. txt(cd) .. "; world.tile -> " .. tile_text(wr, wt) .. " (want " .. far_desc .. ")")
        end

        local function hp_now()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" then
                return hp.level, hp.base_level
            end
            return nil, nil
        end

        -- ---------------------------------------------------- Redbeard Frank
        t.exec("goto-redbeard", t.player.goto_tile, 3051, 3253, 0)
        t.exec("talk-redbeard-start", t.player.talk_to, "redbeard_frank", 1)

        -- [opnpc1,redbeard_frank] with hunt=not_started -> [label,
        -- redbeard_options]'s 3-way p_choice3 menu.
        t.exec("redbeard-greeting", t.chat.play, {
            "npc:Arrrh Matey!",
            "options",
        })

        t.exec("choose-treasure", t.chat.choose, "I'm in search of treasure.")

        -- [label,redbeard_treasure]: sets %hunt = hunt_fetch_rum, ends on its
        -- own last npc line (no p_choice here).
        t.exec("redbeard-treasure-story", t.chat.play, {
            "player:I'm in search of treasure.",
            "npc:Arr, treasure you be after eh? Well I might be able to tell you where to find some... For a price...",
            "player:What sort of price?",
            "npc:Well for example if you can get me a bottle of rum... Not just any rum mind...",
            "npc:I'd like some rum made on Karamja Island. There's no rum like Karamja Rum!",
        })

        t.expect("quest.stage.fetch_rum", t.quest.expect_stage("fetch_rum"))

        -- ------------------------------------------------- sail to Karamja
        -- Talk to one of the Port Sarim seamen -- sailors.rs2's
        -- [opnpc1,seaman_lorris] -> karamja_sailor_talk, a plain p_choice2
        -- ("Yes please." / "No, thank you.") since %dragonquest is untouched
        -- by this fixture. Accepting falls into karamja_sailor_pay: takes
        -- the 30 coins, logs a mes() line, then p_delay(2) + p_telejump to
        -- Musa Point before its own ~mesbox reopens -- the identical
        -- if_close/p_delay/reopen shape as luthas-payout and customs-pay
        -- above (section 8's recipe): wait for chat.kind()=="mesbox" rather
        -- than chaining straight into it. Spawn tile from m47_50.spawn
        -- (3028,3221,0), one tile off the guide's own WorldPoint.
        t.exec("goto-seaman", t.player.goto_tile, 3028, 3221, 0)
        t.exec("talk-seaman", t.player.talk_to, "seaman_lorris", 1)
        t.exec("seaman-offer", t.chat.play, {
            "npc:Do you want to go on a trip to Karamja?",
            "npc:The trip will cost you 30 coins.",
            "options",
            "choose:Yes please.",
            "player:Yes please.",
        })
        t.expect("hunt.seaman_paid_msg", t.msg.expect("pay the 30 coins and board the ship"))

        local sail_reopen_result, sail_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "mesbox"
            end,
            note = "seaman.sail_reopen",
        }, 15)
        t.step("hunt.seaman_sail_reopen", sail_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == mesbox) after pay+p_delay(2)+telejump -> "
                .. tostring(sail_reopen_result) .. " " .. tostring(sail_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))

        t.exec("seaman-arrive", t.chat.play, {
            "mesbox:The ship arrives at Karamja.",
        })

        -- The telejump stands the player on the ship's deck (level 1,
        -- 2956,3143). Leave it by the gangplank, as a player does:
        -- sarimshipplank_off (2956,3144,1, angle north) -> ~gangplank_disembark
        -- drops one level and two tiles north onto the Musa Point pier.
        cross("hunt.disembarkMusa", "sarimshipplank_off", 2956, 3144, 1, nil, nil,
            function(tt) return tt.level == 0 and tt.z >= 3145 and math.abs(tt.x - 2956) <= 2 end,
            "on the Musa Point pier, level 0, z >= 3145")

        -- ------------------------------------------ fetch the rum, for real
        -- Musa Point -> Brimhaven is through Karamja's members' gate
        -- (membergatel 2816,3182, a west-wall leaf: the gate tile and
        -- everything east of it, x >= 2816, is the Musa side; x <= 2815 is
        -- Brimhaven). On foot it is the ONLY way between the two sides
        -- (reach.py with every door shut: NEEDS-DOOR via membergatel at
        -- margins 30, 80 and 160), so it is clicked on every crossing, in
        -- and out (sampler-findings.md, "Sample matthew-mbp-m4-b59" (a)).
        -- gates.rs2's [label,member_fencegate_try] is a walk-through: from
        -- the east it teleports onto the gate tile and then one west, from
        -- the west it teleports back onto the gate tile; it leaves no
        -- opened loc behind, so `cross` grades it on the tiles. The goto
        -- to the near side is overland from the pier (reach.py: REACH 177
        -- with every door shut).
        local function brimhaven_side(tt) return tt.level == 0 and tt.x <= 2815 end
        local function musa_side(tt) return tt.level == 0 and tt.x >= 2816 end
        t.exec("goto-karamjaGate", t.player.goto_tile, 2817, 3182, 0)
        cross("hunt.karamjaGate.cross", "membergatel", 2816, 3182, 0, 2817, 3182,
            brimhaven_side, "west of the gate in Brimhaven, level 0, x <= 2815")

        -- Deadman's Chest, Brimhaven -- buy a bottle of Karamja Rum. The
        -- pub has no door (comp.py floods 7,050 tiles from its floor); the
        -- goto from the gate's Brimhaven side (reach.py: REACH 43, every
        -- door shut) lands on the floor in front of the bar (2796,3158),
        -- not on the bartender's own spawn tile behind it (m43_49.spawn:47).
        t.exec("goto-bartender", t.player.goto_tile, 2796, 3158, 0)
        t.exec("talk-bartender", t.player.talk_to, "deadmans_bartender", 1)
        t.exec("buy-rum", t.chat.play, {
            "npc:Yohoho me hearty",
            "options",
            "choose:A bottle of rum please.",
            "player:A bottle of rum please.",
            "npc:27 coins",
        })
        local rum_bought_result, rum_bought_detail = t.inv.await("karamja_rum", 1, 10)
        t.step("hunt.rum_bought", rum_bought_result == "ok" and "PASS" or "FAIL",
            "inv.await(karamja_rum, 1) -> " .. tostring(rum_bought_result) .. " " .. tostring(rum_bought_detail))

        -- talk_to may have walked round behind the bar (open at 2793,3156):
        -- walk back out onto the pub floor before the next hop.
        walk_check("hunt.pubFloor", 2796, 3158,
            function(tt) return tt.level == 0 and tt.z >= 3157 and math.abs(tt.x - 2796) <= 1 end,
            "the pub floor in front of the bar, z >= 3157")

        -- Back to Musa Point the same way: overland to the gate's Brimhaven
        -- side (reach.py: REACH 43), click the gate, check the Musa side.
        t.exec("goto-karamjaGateBack", t.player.goto_tile, 2815, 3182, 0)
        cross("hunt.karamjaGate.crossBack", "membergatel", 2816, 3182, 0, 2815, 3182,
            musa_side, "on the gate's Musa Point side, level 0, x >= 2816")

        -- Luthas's banana plantation -- get employed so his export crate
        -- will take the rum (banana_crate.rs2's [oplocu,bananacrate]
        -- refuses otherwise: "Why would I want to do that?"). Overland from
        -- the gate's Musa side (reach.py: REACH 161, every door shut).
        t.exec("goto-luthas", t.player.goto_tile, 2939, 3154, 0)
        t.exec("talk-luthas", t.player.talk_to, "luthas", 1)
        t.exec("luthas-employ", t.chat.play, {
            "npc:Hello I'm Luthas",
            "options",
            "choose:Could you offer me employment on your plantation?",
            "player:Could you offer me employment on your plantation?",
            "npc:crate ready to be loaded onto the ship",
            "npc:fill it up with bananas",
        })

        -- Stash the rum in the crate FIRST -- banana_crate_pack_rum wants
        -- %hunt = hunt_fetch_rum, still true here -- so it is hidden in
        -- the crate's own state, never carried, and the customs search
        -- below finds nothing on the player (the entire point of the
        -- trick). use_on's own settle can fire on the walk-into-range
        -- map_flag before the resulting mesbox even lands (measured), so
        -- confirm by the karamja_rum count actually dropping, closing any
        -- stray mesbox and retrying if it has not.
        local bananacrate = t.player.by_symbol("loc", "bananacrate")
        local rum_stashed = false
        for stash_attempt = 1, 4 do
            t.player.use_on("karamja_rum", bananacrate)
            t.ticks(2)
            t.chat.close()
            local after_rum_result, after_rum = t.inv.count("karamja_rum")
            if after_rum_result == "ok" and after_rum == 0 then
                rum_stashed = true
                break
            end
        end
        t.step("hunt.stash_rum", rum_stashed and "PASS" or "FAIL",
            "use_on(karamja_rum, bananacrate) -- karamja_rum carried now "
                .. (rum_stashed and "0 (stashed)" or "still >0"))
        t.shot("stash-rum")

        -- Pack 10 bananas into the same crate, picked from the plantation's
        -- own trees (banana_tree.rs2's pick_karamja_banana; next_loc_stage
        -- cycles the tree through its own base symbol -- QUEST_AUTHORING.md
        -- trap 20 -- so the SAME symbol is clicked every time).
        -- A single click_loc does not always land a pick on the same tick
        -- (measured: a map_flag-only settle that just closes the gap, with
        -- the count unmoved) -- that is the walk, not a miss, so the retry
        -- loop itself carries no row of its own (QUEST_AUTHORING.md trap
        -- 15: a shot/row exists because a click changed something, and a
        -- walk-only attempt changed nothing); only the outcome is recorded.
        local bananas_picked = 0
        for attempt = 1, 25 do
            if bananas_picked >= 10 then
                break
            end
            t.player.click_loc("bananatreefull", 1)
            t.ticks(1)
            local after_result, after_banana = t.inv.count("banana")
            if after_result == "ok" then
                bananas_picked = after_banana
            end
        end
        t.step("hunt.bananas_gathered", bananas_picked >= 10 and "PASS" or "FAIL",
            "picked " .. tostring(bananas_picked) .. "/10 bananas from the plantation trees")

        -- Picking wanders between tree instances -- return to the crate
        -- before packing.
        t.exec("goto-luthas-crate", t.player.goto_tile, 2939, 3154, 0)

        -- Each successful pack opens a REAL mesbox ("You pack a banana
        -- into the crate.") that blocks the next use_on until dismissed
        -- (measured: three unconsumed clicks in a row timed out with a
        -- mesbox still open from the one before) -- close it every time
        -- and confirm by the banana count actually dropping.
        -- Same reasoning as the picking loop above -- an attempt whose own
        -- mesbox is still closing from the one before is a walk/settle,
        -- not a click that changed anything, so no per-attempt row.
        local bananas_packed = 0
        for pack_attempt = 1, 14 do
            if bananas_packed >= 10 then
                break
            end
            local before_result, before_banana = t.inv.count("banana")
            t.player.use_on("banana", bananacrate)
            t.ticks(2)
            t.chat.close()
            local after_result, after_banana = t.inv.count("banana")
            if before_result == "ok" and after_result == "ok" and after_banana < before_banana then
                bananas_packed = bananas_packed + 1
            end
        end
        t.step("hunt.bananas_packed", bananas_packed >= 10 and "PASS" or "FAIL",
            "packed " .. tostring(bananas_packed) .. "/10 bananas into the crate")

        -- Confirm the crate itself now reads full -- oploc1 is the plain
        -- read-state trigger, a mes() chat-log line, not a mesbox.
        t.exec("check-crate-full", t.player.click_loc, "bananacrate", 1)
        t.expect("hunt.crate_full_msg", t.msg.expect("crate is full of bananas"))
        -- ...and the rum is under them (banana_crate.rs2 [oploc1,bananacrate],
        -- %varp5737_crate_rum = 1).
        t.expect("hunt.crate_rum_msg", t.msg.expect("some rum stashed in here"))

        -- Hand the crate over -- Luthas pays 30 coins and ships it
        -- (crate_rum 1 -> 2), re-employing for another round; decline it.
        -- luthas.rs2's own payout branch calls if_close right after its
        -- npc page (a plain mes() coin-grant line follows, not a dialogue
        -- page), THEN reopens a fresh p_choice4 after a p_delay(3) --
        -- so one continuous chat.play list dies on its "options" entry
        -- with "the dialogue closed after 2 page(s)" (2026-09-20 review):
        -- the close is real, not a miss, and the reopen is a SEPARATE
        -- dialogue this driver has to wait for rather than continue past.
        t.exec("talk-luthas-payout", t.player.talk_to, "luthas", 1)
        t.exec("luthas-payout", t.chat.play, {
            "player:I've filled a crate with bananas.",
            "npc:here's your payment",
            "end",
        })
        t.expect("hunt.luthas_paid_msg", t.msg.expect("Luthas hands you 30 coins."))

        local payout_reopen_result, payout_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "options"
            end,
            note = "luthas.payout_reopen",
        }, 10)
        t.step("hunt.luthas_payout_reopen", payout_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == options) after if_close+p_delay(3) -> "
                .. tostring(payout_reopen_result) .. " " .. tostring(payout_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))

        t.exec("luthas-payout-decline", t.chat.play, {
            "options",
            "choose:Thank you, I'll be on my way",
            "player:Thank you, I'll be on my way",
        })

        -- Karamja customs officer -- the boarding search. Nothing is
        -- carried (the rum is stashed inside Luthas's shipped crate), so
        -- the spot check finds nothing and only the 30-coin boarding
        -- charge is owed (customs_officer.rs2's customs_search).
        t.exec("goto-customs", t.player.goto_tile, 2953, 3147, 0)
        t.exec("talk-customs", t.player.talk_to, "customs_officer", 1)
        t.exec("customs-pass", t.chat.play, {
            "npc:Can I help you?",
            "options",
            "choose:Can I journey on this ship?",
            "player:Can I journey on this ship?",
            "npc:You need to be searched",
            "options",
            "choose:Search away, I have nothing to hide.",
            "player:Search away, I have nothing to hide.",
            "npc:it's all legal",
            "options",
            "choose:Ok.",
            "player:Ok.",
        })
        t.expect("hunt.customs_paid_msg", t.msg.expect("pay 30 coins and board the ship"))

        -- customs_officer.rs2's customs_pay continues past that last
        -- "Ok." page into a plain mes() coin-charge line, p_delay(2), then
        -- a p_telejump to Port Sarim before its own ~mesbox reopens -- a
        -- second asynchronous reopen exactly like luthas-payout above, so
        -- the same split: wait for the page to actually remount rather
        -- than chaining straight into it (measured: reading immediately
        -- after "player:Ok." still shows that same page's own "Please
        -- wait..." transition frame, not yet closed).
        local customs_reopen_result, customs_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "mesbox"
            end,
            note = "customs.pay_reopen",
        }, 15)
        t.step("hunt.customs_pay_reopen", customs_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == mesbox) after pay+p_delay(2)+telejump -> "
                .. tostring(customs_reopen_result) .. " " .. tostring(customs_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))

        t.exec("customs-arrive", t.chat.play, {
            "mesbox:ship arrives at Port Sarim",
        })

        -- Off the ship by its gangplank: karamjashipplank_off (3031,3217,1,
        -- angle west) -> ~gangplank_disembark drops one level and two tiles
        -- west onto the Port Sarim pier.
        cross("hunt.disembarkSarim", "karamjashipplank_off", 3031, 3217, 1, nil, nil,
            function(tt) return tt.level == 0 and tt.x <= 3030 and math.abs(tt.z - 3217) <= 2 end,
            "on the Port Sarim pier, level 0, x <= 3030")

        -- A white apron is Wydin's own health-and-safety requirement for
        -- the job, not the quest's own deliverable (trap 16) -- pick up
        -- the guide's one in the Port Sarim fishing shop (getWhiteApron,
        -- 3016,3229; areas/world/configs/m47_50.spawn:53) rather than cheat
        -- it in. The shop (x 3011-3016, z 3221-3229, maps/m47_50) has one
        -- doorway, 3014,3220's south edge, whose poordooropen the map
        -- plants open. click_obj answers `ok` with a nil detail (trap 12's
        -- hollow shape), so called directly with the before/after count as
        -- the real detail.
        local function in_fish_shop(tt)
            return tt.level == 0 and tt.x >= 3011 and tt.x <= 3016 and tt.z >= 3221 and tt.z <= 3229
        end
        t.exec("goto-fishshop", t.player.goto_tile, 3014, 3218, 0)
        pass_door("hunt.fishShopIn", "poordoor", "poordooropen", 3014, 3220, 3014, 3219, 3014, 3222,
            in_fish_shop, "inside the fishing shop, x 3011-3016 z 3221-3229")
        local apron_before_result, apron_before = t.inv.count("white_apron")
        local apron_pick_result, apron_pick_detail = t.player.click_obj("white_apron")
        local apron_after_result, apron_after = t.inv.count("white_apron")
        t.step("hunt.take_apron",
            (apron_pick_result == "ok" and apron_before_result == "ok" and apron_after_result == "ok"
                and apron_after > apron_before) and "PASS" or "FAIL",
            "click_obj(white_apron) -> " .. tostring(apron_pick_result) .. " " .. txt(apron_pick_detail)
                .. " -- white_apron count " .. tostring(apron_before) .. " -> " .. tostring(apron_after))
        local apron_tile_result, apron_tile = t.world.tile()
        t.check("hunt.apron_in_fish_shop", apron_tile_result == "ok" and in_fish_shop(apron_tile),
            "picked up standing at " .. tile_text(apron_tile_result, apron_tile)
                .. " (want inside the fishing shop: the guide's copy at 3016,3229, not the back-room copy at 3009,3204)")

        pass_door("hunt.fishShopOut", "poordoor", "poordooropen", 3014, 3220, 3014, 3221, 3014, 3218,
            function(tt) return tt.level == 0 and tt.z <= 3219 end, "out on the street south of the shop, z <= 3219")

        -- Wydin's grocery store, Port Sarim (front room x 3012-3016,
        -- z 3204-3209; the back room x 3009-3011 behind wydindoor). Its
        -- front doorway is 3017,3206's west edge, whose poordooropen the map
        -- plants open: walk there from the fishing shop (reach.py: open
        -- street) and in. Get employed (the apron carried is enough for the
        -- job offer).
        local function in_wydin_front(tt)
            return tt.level == 0 and tt.x >= 3012 and tt.x <= 3016 and tt.z >= 3204 and tt.z <= 3209
        end
        pass_door("hunt.wydinShopIn", "poordoor", "poordooropen", 3017, 3206, 3018, 3206, 3015, 3206,
            in_wydin_front, "inside Wydin's shop, x 3012-3016 z 3204-3209")
        t.exec("talk-wydin", t.player.talk_to, "wydin", 1)
        t.exec("wydin-job", t.chat.play, {
            "npc:Welcome to my food store",
            "options",
            "choose:Can I get a job here?",
            "player:Can I get a job here?",
            "npc:Have you got your own white apron?",
            "player:Yes, I have one right here.",
            "npc:You're hired",
        })

        -- food_store.rs2's [oploc1,wydindoor] checks the apron is WORN,
        -- not just carried, before letting an employee through.
        t.exec("wear-apron", t.player.equip, "white_apron")

        -- wydindoor is a SILENT walk-through door: food_store.rs2's own
        -- [proc,hunt_walk_door] teleports the player across with no
        -- dialogue, no chat line and no opened loc, so click_loc's settle
        -- can time out on a crossing that landed. cross() grades it on the
        -- tiles: in, x <= 3011; out, x >= 3012.
        local function in_back_room(tt)
            return tt.level == 0 and tt.x >= 3009 and tt.x <= 3011 and tt.z >= 3204 and tt.z <= 3209
        end
        cross("hunt.enter_back_room", "wydindoor", 3012, 3204, 0, 3013, 3204,
            in_back_room, "in the back room, x 3009-3011 z 3204-3209")
        t.shot("enter-back-room")

        -- The grocery crate -- Wydin's half of the smuggle. crate_rum = 2
        -- from Luthas's shipment, so opening it hands the rum straight
        -- back; decline the free banana.
        t.exec("open-grocery-crate", t.player.click_loc, "grocerycrate", 1)
        -- The crate's own [oploc1,grocerycrate] logs its first mes() line
        -- immediately (that alone settles the click above) but the actual
        -- p_choice2_header is behind a p_delay(3) + p_delay(1) -- chat.play
        -- acts on a dialogue already open, never opening one, so give the
        -- delayed prompt time to land first.
        t.ticks(10)
        t.exec("decline-crate-banana", t.chat.play, { "options", "choose:No" })

        local rum_back_result, rum_back_detail = t.inv.await("karamja_rum", 1, 10)
        t.step("hunt.rum_recovered", rum_back_result == "ok" and "PASS" or "FAIL",
            "inv.await(karamja_rum, 1) -> " .. tostring(rum_back_result) .. " " .. tostring(rum_back_detail))

        -- Out the way we came: wydindoor again (the exit arm of
        -- hunt_walk_door teleports onto the door tile 3012,3204), then the
        -- front doorway.
        cross("hunt.leave_back_room", "wydindoor", 3012, 3204, 0, 3011, 3204,
            in_wydin_front, "back in the front room, x 3012-3016 z 3204-3209")
        pass_door("hunt.wydinShopOut", "poordoor", "poordooropen", 3017, 3206, 3016, 3206, 3018, 3206,
            function(tt) return tt.level == 0 and tt.x >= 3017 end, "out on the street east of the shop, x >= 3017")

        -- Talk to Redbeard again -- [label,redboard_progress] sees
        -- inv_total(inv, karamja_rum) >= 1 (the bottle smuggled in above)
        -- and falls straight through "Yes, I've got some." into [label,
        -- redbeard_hand_rum] in the SAME dialogue.
        t.exec("goto-redbeard-rum", t.player.goto_tile, 3051, 3253, 0)
        t.exec("talk-redbeard-rum", t.player.talk_to, "redbeard_frank", 1)
        t.exec("redbeard-hand-rum", t.chat.play, {
            "npc:Arrrh Matey!",
            "npc:Have ye brought some rum for yer ol' mate Frank?",
            "player:Yes, I've got some.",
            "npc:Now a deal's a deal, I'll tell ye about the treasure. I used to serve under a pirate captain called One-Eyed Hector.",
            "npc:Hector were very successful and became very rich. But about a year ago we were boarded by the Customs and Excise Agents.",
            "npc:Hector were killed along with many of the crew, I were one of the few to escape and I escaped with this.",
            "mesbox:Frank happily takes the rum... ... and hands you a key.",
            "npc:This be Hector's key. I believe it opens his chest in his old room in the Blue Moon Inn in Varrock.",
            "npc:With any luck his treasure will be in there.",
            "options",
        })

        t.exec("choose-go-get-it", t.chat.choose, "Ok thanks, I'll go and get it.")

        -- choose-go-get-it lands on its own trailing player echo with
        -- nothing left in the list to continue_ past it -- drain the rest
        -- shut so the next click is not fighting an open dialogue.
        local close_result, close_detail = t.chat.drain({ stop_at = "none" })
        t.expect("hunt.redbeard_dialogue_closed", close_result, close_detail)

        t.expect("quest.stage.received_key", t.quest.expect_stage("received_key"))

        -- ------------------------------------------- Hector's chest, upstairs
        -- The Blue Moon Inn, Varrock (maps/m50_53): the goto lands on the
        -- street west of the inn (3213,3395). The west doorway is 3216,3395's
        -- west edge, whose fai_varrock_door_open the map plants open; the
        -- stairs are fai_varrock_stairs 3227,3393 (maplink 3226,3393,0 ->
        -- 3230,3393,1, and fai_varrock_stairs_top back down); Hector's room
        -- (x 3218-3222, z 3393-3396, level 1) is closed by fai_varrock_door
        -- on 3222,3395's east edge. Every one is crossed in and out.
        local DOOR, DOOR_OPEN = "fai_varrock_door", "fai_varrock_door_open"
        local function in_hector_room(tt)
            return tt.level == 1 and tt.x >= 3218 and tt.x <= 3222 and tt.z >= 3393 and tt.z <= 3396
        end
        t.exec("goto-inn", t.player.goto_tile, 3213, 3395, 0)
        pass_door("hunt.innIn", DOOR, DOOR_OPEN, 3216, 3395, 3215, 3395, 3217, 3395,
            function(tt) return tt.level == 0 and tt.x >= 3216 end, "inside the inn, x >= 3216")
        cross("hunt.climbStairs", "fai_varrock_stairs", 3227, 3393, 0, 3226, 3393,
            function(tt) return tt.level == 1 and tt.x >= 3229 and tt.x <= 3231 and tt.z >= 3392 and tt.z <= 3395 end,
            "upstairs at the stairs head, level 1, x 3229-3231 z 3392-3395")
        pass_door("hunt.hectorDoorIn", DOOR, DOOR_OPEN, 3222, 3395, 3223, 3395, 3221, 3395,
            in_hector_room, "in Hector's room, level 1, x 3218-3222 z 3393-3396")

        -- [oplocu,piratechest]: op1 alone only answers "The chest is
        -- locked." -- it takes the key through use_on, and
        -- pirate_message.rs2 deletes the key as it hands over the message.
        local key_before_result, key_before = t.inv.count("chest_key")
        local piratechest = t.player.by_symbol("loc", "piratechest")
        t.exec("use-key-on-chest", t.player.use_on, "chest_key", piratechest)

        -- The chest's own two lines are plain mes() chat-log lines, not a
        -- mesbox -- wait for the message item itself rather than a dialogue.
        local msg_result, msg_detail = t.inv.await("piratemessage", 1, 10)
        t.step("hunt.message_taken", msg_result == "ok" and "PASS" or "FAIL",
            "inv.await(piratemessage, 1) -> " .. tostring(msg_result) .. " " .. tostring(msg_detail))
        local key_after_result, key_after = t.inv.count("chest_key")
        t.check("hunt.key_used_up", key_before_result == "ok" and key_before == 1 and key_after_result == "ok" and key_after == 0,
            "chest_key " .. tostring(key_before) .. " -> " .. tostring(key_after) .. " (want 1 -> 0: [oplocu,piratechest] inv_del)")

        -- ------------------------------------------------- read the message
        t.exec("read-message", t.player.inv_op, "piratemessage", 1)
        t.exec("read-message-dialog", t.chat.play, {
            "mesbox:Visit the city of the White Knights. In the park, Saradomin points to the X which marks the spot.",
        })

        t.expect("quest.stage.read_note", t.quest.expect_stage("read_note"))

        -- Out of the inn the same way: Hector's door, the stairs down, the
        -- west doorway.
        pass_door("hunt.hectorDoorOut", DOOR, DOOR_OPEN, 3222, 3395, 3222, 3395, 3224, 3395,
            function(tt) return tt.level == 1 and tt.x >= 3223 end, "on the landing outside Hector's room, level 1, x >= 3223")
        cross("hunt.descendStairs", "fai_varrock_stairs_top", 3228, 3393, 1, 3230, 3393,
            function(tt) return tt.level == 0 and tt.x >= 3225 and tt.x <= 3227 and tt.z >= 3392 and tt.z <= 3395 end,
            "downstairs at the stairs foot, level 0, x 3225-3227 z 3392-3395")
        pass_door("hunt.innOut", DOOR, DOOR_OPEN, 3216, 3395, 3217, 3395, 3214, 3395,
            function(tt) return tt.level == 0 and tt.x <= 3215 end, "out on the street west of the inn, x <= 3215")

        -- ---------------------------------------------------- Falador Park
        t.exec("goto-dig", t.player.goto_tile, 2999, 3383, 0)

        local before_gold_ring_result, before_gold_ring = t.inv.count("gold_ring")
        local before_emerald_result, before_emerald = t.inv.count("emerald")
        local before_coins_result, before_coins = t.inv.count("coins")

        -- dig.rs2's [label,hunt_dig] redirects to [label,
        -- pirate_irate_gardener_attack] instead of completing the dig
        -- whenever falador_gardener is within 10 tiles of 2999,3383 --
        -- npc_find(coord, falador_gardener, 10, 0), evaluated on the
        -- SERVER at the tick it processes the press. Its own spawn
        -- (m46_52.spawn: 2996,3381,0) is ~3.6 tiles away, so it almost
        -- always is in range, but it wanders -- a client-side npc.nearest
        -- read taken before the press is a snapshot from a different tick
        -- than the one the server's own npc_find runs on, so it is context,
        -- not a prediction (measured, run 1: the gardener drifted out of
        -- the 10-tile radius between the pre-check and the press).
        local gardener_before_result, gardener_before = t.npc.nearest("falador_gardener", 10)
        local hp_pre_dig = hp_now()
        -- A gardener that dies drops bones where it falls (shot 143 of r2
        -- account hunt_r2b): read the ground before the press so a pile the
        -- fight leaves is told from one that was already lying there.
        local bones_before_result, bones_before = t.world.obj_near("bones", 12)
        local function bones_dropped_since_press()
            local br, bd = t.world.obj_near("bones", 12)
            local fresh = br == "ok" and type(bd) == "table"
                and (bones_before_result ~= "ok"
                    or bd.tile_x ~= bones_before.tile_x or bd.tile_z ~= bones_before.tile_z
                    or bd.count > bones_before.count)
            local text = "bones within 12: before " .. (bones_before_result == "ok"
                    and (bones_before.tile_x .. "," .. bones_before.tile_z .. " x" .. tostring(bones_before.count)) or tostring(bones_before_result))
                .. ", now " .. ((br == "ok" and type(bd) == "table") and (bd.tile_x .. "," .. bd.tile_z .. " x" .. tostring(bd.count)) or tostring(br))
            return fresh, text
        end
        -- The chat-ring floor: msg.await only sees lines inserted after
        -- IT registers, and inv_op's settle resolves ON the dig line when the
        -- real dig runs, so the line is already in the ring by the time a
        -- msg.await after the press could register -- read the ring against
        -- this serial instead.
        local floor_result, floor_list = t.msg.last()
        local msg_floor = 0
        if floor_result == "ok" and type(floor_list) == "table" then
            for i = 1, #floor_list do
                if floor_list[i].serial > msg_floor then
                    msg_floor = floor_list[i].serial
                end
            end
        end
        local function dig_line_since_floor()
            local lr, list = t.msg.last()
            if lr ~= "ok" or type(list) ~= "table" then
                return nil
            end
            for i = 1, #list do
                if list[i].serial > msg_floor and string.find(list[i].text, "You dig a hole in the ground", 1, true) then
                    return list[i].text
                end
            end
            return nil
        end

        -- [opheld1,spade]: within 1 tile of 0_46_52_55_55 (2999,3383,0)
        -- jumps to dig.rs2's [label,hunt_dig]. inv_op's own "ok" answers
        -- the press being accepted and the driver's side-effect drop
        -- landing (setup's comment above) -- it reads "ok" identically on
        -- BOTH the real-dig branch and the silent gardener redirect
        -- (npc_say + npc_setmode, no mesbox, no chat line, nothing an
        -- inv_op settle can tell apart), so it is NOT the signal for which
        -- one happened (measured, runs 2-3: "ok" on a run that was in fact
        -- redirected, quest.varp_complete FAILing at hunt=3 the whole way
        -- down). Only the real-dig branch's own mes("You dig a hole in the
        -- ground...") -- absent from pirate_irate_gardener_attack entirely
        -- -- tells the two apart; read the chat ring for it.
        local dig1_result, dig1_detail = t.player.inv_op("spade", 1)
        local dig_line = dig_line_since_floor()
        if dig_line == nil then
            local late_result = t.msg.await("You dig a hole in the ground", 3)
            if late_result == "ok" then
                dig_line = "You dig a hole in the ground..."
            end
        end
        local dig_redirected = dig_line == nil
        -- The press is graded on what it CAUSED, not on inv_op's answer (the
        -- silent redirect settles `timeout settle_after_click`, measured
        -- run 1 of fix_b59), and not on "the gardener is within 10 tiles"
        -- (true before the press as well -- sampler-findings.md, "Sample
        -- matthew-mbp-m4-b59" (b)).
        --   * The real dig: its own mes line, newer than the press.
        --   * The redirect: [label,pirate_irate_gardener_attack] is
        --     npc_say("Hey, leave off my flowers!") + npc_setmode(opplayer2).
        --     The say is an overhead bubble that lives 150 client cycles
        --     (~5 server ticks, world.c world_entity_set_chat), and inv_op's
        --     silent settle runs 10+ ticks, so the bubble is read when it is
        --     still up and reported, but the row cannot depend on it. What
        --     the attack leaves behind can: the gardener fought the player.
        --     Before the press the gardener has never been hit (no health
        --     bar: health_ratio -1, the pool's own "nobody has hit it") and
        --     the player is at full hitpoints; after it, the gardener's bar
        --     has been sent / its newest hitsplat is newer (the player's
        --     auto-retaliate swung back at the npc that attacked him --
        --     measured, the reverted run: 4/30 before the first Attack press),
        --     or the player lost hitpoints to it, or the very gardener slot
        --     read before the press is gone from a 20-tile pool (a gardener
        --     wanders within a few tiles of its spawn 3.6 tiles from the spot,
        --     so it leaves that pool only by dying -- in that fight) AND a
        --     pile of bones that was not there before the press lies within
        --     12 tiles (what a dead gardener drops).
        local engaged, engaged_text = false, "no gardener read before the press"
        local gardener_slot = (gardener_before_result == "ok" and type(gardener_before) == "table") and gardener_before.slot or nil
        -- The provoked copy's live row (nil when it has left a 20-tile pool),
        -- and the pool read's own answer: `not_found` (no copy at all within
        -- 20) is a read; anything else but `ok` is not, and is never taken
        -- for "gone".
        local function provoked_row(slot)
            local pool_result, _, pool_rows = t.npc.tiles("falador_gardener", 20)
            if pool_result == "ok" and type(pool_rows) == "table" then
                for _, row in ipairs(pool_rows) do
                    if row.slot == slot then
                        return row, pool_result
                    end
                end
            end
            return nil, pool_result
        end
        local hp_post_dig = nil
        if gardener_slot ~= nil then
            local now_row, pool_result = provoked_row(gardener_slot)
            hp_post_dig = hp_now()
            local hp_fell = hp_pre_dig ~= nil and hp_post_dig ~= nil and hp_post_dig < hp_pre_dig
            local before_text = "before: slot " .. tostring(gardener_slot) .. " bar "
                .. tostring(gardener_before.health_ratio) .. "/" .. tostring(gardener_before.health_scale)
                .. " hit_cycle " .. tostring(gardener_before.hit_cycle) .. ", player hp " .. tostring(hp_pre_dig)
            if now_row ~= nil then
                local fought = (gardener_before.health_ratio == -1 and now_row.health_ratio >= 0)
                    or (now_row.health_ratio >= 0 and gardener_before.health_ratio >= 0 and now_row.health_ratio < gardener_before.health_ratio)
                    or (now_row.hit_cycle > gardener_before.hit_cycle)
                -- The exact line, dig.rs2:43 (OSRS-Content quest_hunt).
                local said = now_row.overhead == "Hey, leave off my flowers!"
                engaged = fought or hp_fell or said
                engaged_text = before_text .. "; after: bar " .. tostring(now_row.health_ratio) .. "/" .. tostring(now_row.health_scale)
                    .. " hit_cycle " .. tostring(now_row.hit_cycle) .. " at " .. tostring(now_row.x) .. "," .. tostring(now_row.z)
                    .. ", overhead '" .. tostring(now_row.overhead) .. "' timer " .. tostring(now_row.overhead_timer)
                    .. ", player hp " .. tostring(hp_post_dig)
                    .. (fought and " -- the gardener has been in a fight since the press" or "")
                    .. (hp_fell and " -- the player lost hitpoints since the press" or "")
                    .. (said and " -- the gardener's own redirect line is overhead" or "")
            else
                local bones_fresh, bones_text = bones_dropped_since_press()
                engaged = (pool_result == "ok" or pool_result == "not_found") and bones_fresh
                engaged_text = before_text .. "; after: slot " .. tostring(gardener_slot)
                    .. " gone from the 20-tile pool (" .. tostring(pool_result) .. "), " .. bones_text
                    .. (bones_fresh and " -- killed in the fight the redirect started, its bones on the ground" or " -- no fresh bones: not shown killed")
                    .. ", player hp " .. tostring(hp_post_dig)
            end
        end
        t.check("dig-treasure",
            (dig1_result == "ok" or dig1_result == "timeout")
                and ((not dig_redirected) or engaged),
            "inv_op(spade,1) -> " .. tostring(dig1_result) .. " " .. txt(dig1_detail)
                .. " -- dig line since the press: " .. tostring(dig_line)
                .. (dig_redirected
                    and (" -- redirected to hunt_dig's own gardener attack; engaged=" .. tostring(engaged) .. " (" .. engaged_text .. ")")
                    or " -- the real dig landed on the first press (the gardener was outside hunt_dig's own npc_find radius on the server's tick)"))

        if dig_redirected then
            -- all.npc: falador_gardener's op2 is Attack. hitpoints=7,
            -- attack=1 (combat_stats.generated.npc) -- trivial with real
            -- combat levels, but not automatic: it only goes hostile here
            -- (npc_setmode(opplayer2)), the player still has to fight
            -- back, so arm up first.
            local equip_result, equip_detail = t.player.equip("rune_scimitar")
            t.step("hunt.equip_scimitar", equip_result == "ok" and "PASS" or "FAIL",
                "player.equip(rune_scimitar) -> " .. tostring(equip_result)
                    .. " " .. tostring(equip_detail))

            -- The fight is with THE gardener the dig provoked -- the slot read
            -- before the press -- never "the nearest falador_gardener": a
            -- second copy stands at 3019,3370 (m47_52.spawn:22), and when the
            -- provoked one has already died to the player's auto-retaliate
            -- inside the dig press (measured, fix_b59 r2 accounts hunt_r2b and
            -- hunt_r2c: its slot gone 16 ticks after the press) a bare-symbol
            -- attack walked twenty tiles and killed that innocent one. So:
            -- still standing with a bar above 0 -> attack that slot and wait
            -- on it; already at 0 or gone from the pool -> it died in the
            -- fight the redirect started, and that is the kill.
            local hp_before = hp_now()
            local provoked_now, provoked_read = provoked_row(gardener_slot)
            local hp_at_attack, dead_result, dead_detail = nil, nil, nil
            if provoked_now ~= nil and provoked_now.health_ratio ~= 0 then
                t.exec("attack-gardener", t.player.attack, "falador_gardener", 2, 20, { slot = gardener_slot })
                hp_at_attack = hp_now()
                dead_result, dead_detail = t.npc.await_dead_engaged(60, 6, { eat = { item = "lobster", below = 6 } })
                t.expect("gardener-dead", dead_result, dead_detail)
            else
                local bones_fresh, bones_text = bones_dropped_since_press()
                t.check("gardener-deadInDigPress",
                    gardener_slot ~= nil
                        and ((provoked_now == nil and (provoked_read == "ok" or provoked_read == "not_found") and bones_fresh)
                            or (provoked_now ~= nil and provoked_now.health_ratio == 0)),
                    "the provoked gardener (slot " .. tostring(gardener_slot) .. ", no bar before the dig press) "
                        .. (provoked_now ~= nil
                            and ("reads bar " .. tostring(provoked_now.health_ratio) .. "/" .. tostring(provoked_now.health_scale) .. " at " .. tostring(provoked_now.x) .. "," .. tostring(provoked_now.z))
                            or ("has left the 20-tile npc pool (read " .. tostring(provoked_read) .. ")"))
                        .. "; " .. bones_text
                        .. " -- killed by the player's retaliation inside the dig press; no second attack is pressed")
            end
            -- Margin: the lowest stated hitpoints over the fight is at least
            -- a quarter of the maximum, AND food is left. The lowest is the
            -- minimum of await_dead's own eat sampler (absent when the
            -- gardener already died inside the dig press) and the reads
            -- before the dig press, after it, before and after the attack
            -- and after the wait.
            local hp_after, hp_base = hp_now()
            local low_text, base_text = string.match(tostring(dead_detail), "lowest hp (%d+)/(%d+)")
            local low, base = tonumber(low_text), tonumber(base_text) or hp_base
            for _, v in ipairs({ hp_pre_dig or false, hp_post_dig or false, hp_before or false, hp_at_attack or false, hp_after or false }) do
                if v and (low == nil or v < low) then
                    low = v
                end
            end
            local food_result, food_left = t.inv.count("lobster")
            t.check("killGardener.margin",
                low ~= nil and base ~= nil and low * 4 >= base and food_result == "ok" and (food_left or 0) >= 1,
                "lowest hp " .. tostring(low) .. "/" .. tostring(base) .. " (hp before the dig press " .. tostring(hp_pre_dig)
                    .. ", after it " .. tostring(hp_post_dig) .. ", before the attack " .. tostring(hp_before)
                    .. ", after the attack press " .. tostring(hp_at_attack) .. ", after the wait " .. tostring(hp_after)
                    .. ", sampler " .. tostring(low_text) .. "), lobsters left "
                    .. tostring(food_left) .. " (" .. tostring(food_result) .. ") of 2 staged -- margin: lowest hp >= a quarter of max AND food left")

            -- await_dead resolves on a corpse's bar reading 0 -- the slot
            -- is still IN the pool for a few ticks after that, and
            -- hunt_dig's own npc_find(coord, falador_gardener, 10, 0) does
            -- not check health, so a dig thrown right after await_dead
            -- would still redirect to pirate_irate_gardener_attack. Wait
            -- for the corpse to actually leave the pool. await_gone
            -- answers ok with a nil detail -- a hollow verb -- so call it
            -- directly and write the read-back count ourselves.
            local gone_result = t.npc.await_gone("falador_gardener", 10, 20)
            local gone_check_result, gone_check_row = t.npc.nearest("falador_gardener", 10)
            local gone_check_detail = gone_check_result
            if gone_check_result == "ok" and type(gone_check_row) == "table" then
                gone_check_detail = "slot " .. tostring(gone_check_row.slot)
            end
            t.step("hunt.gardener_gone", gone_result == "ok" and "PASS" or "FAIL",
                "npc.await_gone(falador_gardener, 10) -> " .. tostring(gone_result)
                    .. " -- npc.nearest now " .. tostring(gone_check_detail))

            -- The fight carries the player off the dig tile chasing the
            -- gardener -- spade.rs2:9 wants distance(coord, 2999,3383,0)
            -- <= 1 -- so goto_tile back before the second dig.
            t.exec("goto-dig-again", t.player.goto_tile, 2999, 3383, 0)

            -- The gardener is gone -- the second spade from setup covers
            -- the real dig (the first spade is now a ground item at our
            -- feet, the same op1-drops-it side effect read-message hit
            -- above).
            t.exec("dig-treasure-again", t.player.inv_op, "spade", 1)
        end

        -- dig.rs2's [label,hunt_dig] itself waits (p_arrivedelay + a
        -- p_delay(4)) before queue(hunt_quest_complete, 0, 0) fires -- a
        -- fixed t.ticks(3) right here covered it on the redirected path
        -- above (whose own attack/await_dead/await_gone/goto-dig-again
        -- chain already burns well over a dozen ticks after the real
        -- press), but the first-try path above (dig_redirected == false,
        -- the gardener out of hunt_dig's own npc_find radius on the
        -- server's tick, run 2 of this file) presses the real dig with
        -- NO fight in between, so a flat t.ticks(3) measured %hunt still
        -- at read_note (3) with quest.varp_complete FAILing: not enough
        -- of dig.rs2's own delay had elapsed. Poll the varp instead of
        -- guessing a tick count that has to cover two different shapes.
        t.exec("hunt-complete-await", t.var.await_server, "varp71_hunt", 4, 15)

        t.quest.expect_complete()

        -- dig.rs2's [queue,hunt_quest_complete] hands over a pirate_casket,
        -- not the loot directly -- confirm it landed before opening it.
        local casket_result, casket_count = t.inv.count("pirate_casket")
        t.check("reward.pirate_casket", casket_result == "ok" and casket_count == 1,
            string.format("pirate_casket count=%s (%s), want 1",
                tostring(casket_count), tostring(casket_result)))

        -- expect_complete's own journal_open/journal_close cycle (a modal
        -- interface) leaves the sidebar's own currently-DISPLAYED tab
        -- unsettled for a tick -- app_minimenu_ui_pick_live rejects an
        -- inv_op pick whose tab is not yet the one actually shown, a
        -- total no-op (no message, no page, no count change) rather than
        -- a refusal. Switch tabs and let it land before clicking.
        t.ui.tab("inventory")
        t.ticks(2)

        -- [opheld1,pirate_casket] is what actually grants the coins/ring/
        -- emerald.
        t.exec("open-casket", t.player.inv_op, "pirate_casket", 1)
        t.exec("open-casket-dialog", t.chat.play, {
            "mesbox:You open the casket and find 450 coins, a gold ring and an emerald!",
        })

        local after_gold_ring_result, after_gold_ring = t.inv.count("gold_ring")
        t.check("reward.gold_ring",
            before_gold_ring_result == "ok" and after_gold_ring_result == "ok" and
            after_gold_ring == before_gold_ring + 1,
            string.format("gold_ring %s -> %s (want +1), reads %s/%s",
                tostring(before_gold_ring), tostring(after_gold_ring),
                tostring(before_gold_ring_result), tostring(after_gold_ring_result)))

        local after_emerald_result, after_emerald = t.inv.count("emerald")
        t.check("reward.emerald",
            before_emerald_result == "ok" and after_emerald_result == "ok" and
            after_emerald == before_emerald + 1,
            string.format("emerald %s -> %s (want +1), reads %s/%s",
                tostring(before_emerald), tostring(after_emerald),
                tostring(before_emerald_result), tostring(after_emerald_result)))

        local after_coins_result, after_coins = t.inv.count("coins")
        t.check("reward.coins",
            before_coins_result == "ok" and after_coins_result == "ok" and
            after_coins == before_coins + 450,
            string.format("coins %s -> %s (want +450), reads %s/%s",
                tostring(before_coins), tostring(after_coins),
                tostring(before_coins_result), tostring(after_coins_result)))

        t.finish(0)
    end,
}
