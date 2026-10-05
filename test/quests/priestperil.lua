-- WALLS (door rule, owner 2026-10-03; re-driven in b63). Every goto departs
-- from and lands on open ground; every door, gate, stair, ladder, trapdoor
-- and barrier between the player and a target is pressed on every visit, in
-- and out. Checked with test/quests/orchestrator/matthew-mbp-m4/reports/
-- sample_tools/{reach,comp,locs_near}.py (doors closed):
--   * King Roald's room x 3219-3225 z 3470-3478: fai_varrock_castle_door on
--     the east edge of 3218,3472 from the great hall (reach Varrock square
--     3213,3424 -> 3217,3472 REACH len 52; -> 3222,3477 NEEDS-DOOR via it).
--   * Varrock -> Paterdomus: the members' gate fai_varrock_member_gatel
--     3319,3468 is the only way on foot (reach 3321,3468 -> 3318,3468
--     NEEDS-DOOR via it at margins 30/80/160); east of it 3321,3468 -> the
--     temple door 3407,3488 and the trapdoor 3405,3506 are open ground.
--   * The temple: priestperiltempledoorr on the east edge of 3408,3488
--     (walk-through, temple_doors.rs2 [proc,priestperil_temple_walk_door]);
--     paterdomus_spiralstairs 3417,3492 (2x2) up from 3416,3493 and
--     spiralstairstop 3417,3493,1 down (no maplink row: the climb up is
--     ladders.rs2 [proc,climb] +-1 plane on the tile; the way down lands on
--     3417,3494,0, measured in run 2); ladder / laddertop 3410,3485
--     from 3410,3486 on levels 1/2.
--   * Drezel's cell x 3416-3418 z 3483-3494 on level 2: pip_prisondoor on
--     the east edge of 3415,3489. Before Drezel is unlocked the gate's op1
--     is the talk-through (trapped_drezel.rs2:40-50), pressed from 3415,3489;
--     after, a walk-through (priestperil_cell_walk_door). The coffin
--     3413,3486,2 is OUTSIDE the cell.
--   * The underground: trapdoor 3405,3507 (trapdoors.rs2: telejump +6400 to
--     3405,9906), ladder_from_cellar 3405,9907 (maplink 0_53_154_13_50 ->
--     3405,3506), pip_underground_door1 3405,9895 and door2 3431,9897
--     (walk-through, area_mausoleum/scripts/gates.rs2), the holy barrier
--     3440,9886 (p_telejump to 3423,3485, mausoleum_interactions.rs2:26-30).
--     The old east trapdoor goto (3422,3484) landed on the MORYTANIA bank
--     (comp.py: that component never reaches the temple) and is gone.
--   * Long hauls are real Varrock Teleports (cast, runes, landing graded).
--   * The fifty essence come from the Varrock east bank in two trips
--     (staged there in SETUP by ::bankgive): 24 + 26, the pack holds no more.
return {
    id = "priestperil",
    fixture = "fresh_lumbridge.ini",
        setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel magic 25",
        "::give rune_scimitar 1",
        "::give shark 12",
        "::give bucket_empty 1",
        -- three Varrock Teleports (magic_spells.dbrow: 1 fire, 3 air, 1 law)
        "::give firerune 3",
        "::give airrune 9",
        "::give lawrune 3",
        -- noted essence for Drezel's refusal (mausoleum_drezel.rs2 [label,drezel_blankrune_cert])
        "::give cert_blankrune 5",
        -- the fifty essence, fetched from the bank in two trips
        "::bankgive blankrune 25",
        "::bankgive blankrune_high 25",
    },

    run = function(t)
        local r, d = t.quest.bind({
            varp = "varp302_priestperil",
            constants = {
                not_started = 0, started = 1, agree_to_kill_dog = 2, killed_dog = 3,
                return_to_drezel = 4, find_drezel_key = 5, unlocked_drezel = 6,
                poured_blessed_water = 7, meet_in_mausoleum = 8,
                begin_bring_essence = 10, end_bring_essence = 60, complete = 60,
                access_holy_barrier = 61,
            },
            row = "quest_priestinperil",
            display = "Priest in Peril",
            points = 1,
        })
        t.step("quest.bind", r == "ok" and "PASS" or "FAIL", d)
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")

        -- ------------------------------------------------------------ helpers
        local function varrock_teleport(name)
            t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = name,
                runes = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Varrock square" })
        end
        -- King Roald's room from the great hall, and back out.
        local CASTLE_DOOR, CASTLE_DOOR_OPEN = "fai_varrock_castle_door", "fai_varrock_castle_door_open"
        local function roald_in(prefix)
            t.exec("goto-" .. prefix .. ".hall", t.player.goto_tile, 3217, 3472, 0)
            t.exec(prefix .. ".roomDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3218, 3472, 0 }, near = { 3218, 3472 }, far = { 3220, 3472 } })
        end
        local function roald_out(prefix)
            t.exec(prefix .. ".roomDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3218, 3472, 0 }, near = { 3219, 3472 }, far = { 3217, 3472 } })
        end
        -- The Varrock members' gate, either way.
        local MGATE, MGATE_OPEN = "fai_varrock_member_gatel", "fai_varrock_member_gatel_open"
        local function gate_east(prefix)
            t.exec("goto-" .. prefix .. ".varrockGate", t.player.goto_tile, 3318, 3468, 0)
            t.exec(prefix .. ".varrockGate", t.player.pass_door, { closed = MGATE, open = MGATE_OPEN,
                at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        end
        local function gate_west(prefix)
            t.exec("goto-" .. prefix .. ".varrockGateBack", t.player.goto_tile, 3321, 3468, 0)
            t.exec(prefix .. ".varrockGateBack", t.player.pass_door, { closed = MGATE, open = MGATE_OPEN,
                at = { 3319, 3468, 0 }, near = { 3321, 3468 }, far = { 3318, 3468 } })
        end
        -- The temple's large door (walk-through once Drezel's story is known).
        local function in_temple(tile)
            return tile.x >= 3409 and tile.x <= 3420 and tile.z >= 3480 and tile.z <= 3496
        end
        local function temple_in(name)
            t.exec(name, t.player.cross_gate, { loc = "priestperiltempledoorr", at = { 3408, 3488, 0 },
                near = { 3407, 3488 }, far_ok = in_temple, far_desc = "inside the temple, x 3409-3420" })
        end
        local function temple_out(name)
            t.exec(name, t.player.cross_gate, { loc = "priestperiltempledoorr", at = { 3408, 3488, 0 },
                near = { 3409, 3488 }, far_ok = function(tile) return tile.x <= 3408 end,
                far_desc = "outside the temple, x <= 3408" })
        end
        -- Ground floor -> the cell gate on level 2, and back down.
        local function temple_up(stairs_row, ladder_row)
            t.exec(stairs_row, t.player.climb, { loc = "paterdomus_spiralstairs", op = 1, op_name = "Climb-up",
                at = { 3417, 3492, 0 }, src = { 3416, 3493 }, dest = { 3416, 3493, 1 } })
            t.exec(ladder_row, t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
                at = { 3410, 3485, 1 }, src = { 3410, 3486 }, dest = { 3410, 3486, 2 } })
            t.exec("walk-" .. ladder_row .. ".cellGate", t.player.walk_to, 3415, 3489)
        end
        local function temple_down(ladder_row, stairs_row)
            t.exec(ladder_row, t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
                at = { 3410, 3485, 2 }, src = { 3410, 3486 }, dest = { 3410, 3486, 1 } })
            t.exec(stairs_row, t.player.climb, { loc = "spiralstairstop", op = 1, op_name = "Climb-down",
                at = { 3417, 3493, 1 }, src = { 3416, 3493 }, dest = { 3417, 3494, 0 } })
        end
        -- Drezel's cell gate, walked through once he is unlocked.
        local function cell_in(name)
            t.exec(name, t.player.cross_gate, { loc = "pip_prisondoor", at = { 3415, 3489, 2 },
                near = { 3415, 3489 }, far_ok = function(tile) return tile.x >= 3416 and tile.x <= 3418 end,
                far_desc = "inside Drezel's cell, x 3416-3418" })
        end
        local function cell_out(name)
            t.exec(name, t.player.cross_gate, { loc = "pip_prisondoor", at = { 3415, 3489, 2 },
                near = { 3416, 3489 }, far_ok = function(tile) return tile.x <= 3415 end,
                far_desc = "outside Drezel's cell, x <= 3415" })
        end
        -- The trapdoor north of the temple: opened if shut (it shuts again after
        -- 500 ticks, trapdoors.rs2), then climbed down into the underground.
        local TRAP_AT = { 3405, 3507, 0 }
        local function trapdoor_down(prefix, climb_row)
            local closed_r = t.world.loc_near("trapdoor", 3, { at = TRAP_AT })
            if closed_r == "ok" then
                t.exec(prefix .. ".openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = TRAP_AT })
                t.await({
                    level = function()
                        return t.world.loc_near("trapdoor_open", 3, { at = TRAP_AT }) == "ok"
                    end,
                    note = prefix .. ": the trapdoor opens",
                }, 6)
            else
                t.note(prefix .. ": the trapdoor stands open (opened within its 500-tick revert), not pressed")
            end
            local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = TRAP_AT })
            local tdc_r = t.world.loc_near("trapdoor", 3, { at = TRAP_AT })
            t.check(prefix .. ".trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
                "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                    .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                    .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
            t.exec(climb_row, t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
                at = TRAP_AT, src = { 3405, 3506 }, dest = { 3405, 9906, 0 } })
        end
        local function ladder_up(row)
            t.exec(row, t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
                at = { 3405, 9907, 0 }, src = { 3405, 9906 }, dest = { 3405, 3506, 0 } })
        end
        local function gate1_south(name)
            t.exec(name, t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
                near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
                far_desc = "south of the golden-key gate, z <= 9894" })
        end
        local function gate1_north(name)
            t.exec(name, t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
                near = { 3405, 9894 }, far_ok = function(tile) return tile.z >= 9895 and tile.z < 9920 end,
                far_desc = "north of the golden-key gate, z >= 9895" })
        end
        local function gate2_east(name)
            t.exec(name, t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
                near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
                far_desc = "Drezel's side of the second gate, x >= 3432" })
        end
        -- Fight margin: lowest hp (the wait's eat log) >= 25, a quarter of 99, AND food left.
        local function margin_row(name, fight, dead_detail)
            local text = tostring(dead_detail)
            local low = tonumber(string.match(text, "lowest hp (%d+)/"))
            local fr, food = t.inv.count("shark")
            t.check(name, low ~= nil and low >= 25 and fr == "ok" and food >= 1,
                fight .. ": lowest hp " .. tostring(low) .. "/99 (await_dead_engaged's eat log), sharks left "
                    .. tostring(food) .. " (" .. tostring(fr) .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        end

        -- LEG 1: King Roald (LC text, king_roald.rs2)
        varrock_teleport("talkToRoald.varrockTeleport")
        roald_in("talkToRoald")
        t.exec("talkToRoald", t.player.talk_to, "king_roald", 1)
        t.exec("roald-1", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Well hello there. What do you want?",
            "player:I'm looking for a quest!",
            "npc:A quest you say?",
            "npc:Are you aware of the temple east of here.",
            "player:No, I don't think I know it...",
            "npc:Hmm, how strange that you don't.",
            "npc:Be a sport and go make sure",
            "choose:No, that sounds boring.",
            "player:No. That sounds boring.",
            "npc:Yes, I dare say it does.",
        })
        t.exec("expect_stage-not-started", t.quest.expect_stage, "not_started")
        t.exec("talkRoald2", t.player.talk_to, "king_roald", 1)
        t.exec("roald-2", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Well hello there. What do you want?",
            "player:I'm looking for a quest!",
            "npc:A quest you say?",
            "npc:Are you aware of the temple east of here.",
            "player:No, I don't think I know it...",
            "npc:Hmm, how strange that you don't.",
            "npc:Be a sport and go make sure",
            "choose:Sure.",
            "player:Sure. I don't have anything better to do right now.",
            "npc:Many thanks adventurer!",
        })
        t.exec("expect_stage-started", t.quest.expect_stage, "started")
        t.exec("talkRoald3", t.player.talk_to, "king_roald", 1)
        t.exec("roald-3", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Well hello there. What do you want?",
            "npc:You have news of Drezel for me?",
            "player:Where am I supposed to go again?",
            "npc:The temple east of here where Drezel lives.",
            "npc:Don't worry, you can't miss it.",
        })
        roald_out("talkToRoald")

        -- LEG 2: east through the members' gate, knock at the temple door
        gate_east("goToTemple")
        t.exec("goto-templedoor", t.player.goto_tile, 3407, 3488, 0)
        t.exec("goToTemple", t.player.click_loc, "priestperiltempledoorr", 1, { at = { 3408, 3488, 0 } })
        t.exec("knock1-open", t.chat.play, {
            "mesbox:You knock at the door",
            "player:Ummmm.....",
            "choose:Roald sent me to check on Drezel.",
            "player:Roald sent me to check on Drezel.",
            "mesbox:Psst",
            "player:Well, as I say, the King sent me",
            "mesbox:And, uh, what would you do",
            "player:I'm not sure.",
            "mesbox:Ah, good, well",
            "choose:Nope.",
            "player:Nope. Something about all this is very suspicious",
            "mesbox:Get lost then!",
        })
        t.exec("expect_stage-still-started", t.quest.expect_stage, "started")
        t.exec("knock2", t.player.click_loc, "priestperiltempledoorr", 1, { at = { 3408, 3488, 0 } })
        t.exec("knock2-open", t.chat.play, {
            "mesbox:You knock at the door",
            "player:Ummmm.....",
            "choose:Roald sent me to check on Drezel.",
            "player:Roald sent me to check on Drezel.",
            "mesbox:Psst",
            "player:Well, as I say, the King sent me",
            "mesbox:And, uh, what would you do",
            "player:I'm not sure.",
            "mesbox:Ah, good, well",
            "choose:Sure.",
            "player:Sure. I'm a helpful person!",
            "mesbox:HAHAHAHA! Really?",
            "mesbox:It's been really bugging me!",
            "player:Okey-dokey, one dead dog coming up.",
        })
        t.exec("expect_stage-agree", t.quest.expect_stage, "agree_to_kill_dog")
        local tile_r, tile = t.world.tile()
        t.check("knock.stillOutside", tile_r == "ok" and tile.x <= 3408,
            "after the knocks the player is at " .. tostring(tile_r == "ok" and (tile.x .. "," .. tile.z .. "," .. tile.level) or tile)
                .. " (want outside the temple, x <= 3408: a knock never opens the door before stage 4)")

        -- LEG 3: the Temple Guardian under the trapdoor (instanced, killed for real)
        t.exec("walk-goDownToDog", t.player.walk_to, 3405, 3506)
        trapdoor_down("goDownToDog", "goDownToDog")
        -- the guardian is added by the cellar's zone trigger on arrival (temple_guardian.rs2:10-22)
        t.ticks(4)
        t.exec("killTheDog", t.player.attack, "priestperilguarddog", 2, 30)
        local _, dog_detail = t.exec("attackDog.dead", t.npc.await_dead_engaged, 300, 40, { eat = { item = "shark", below = 50 } })
        margin_row("attackDog.margin", "Temple Guardian (level 30)", dog_detail)
        t.ticks(3)
        t.exec("expect_stage-killed_dog", t.quest.expect_stage, "killed_dog")
        ladder_up("climbUpAfterKillingDog")
        varrock_teleport("returnToKingRoald.varrockTeleport")
        roald_in("returnToKingRoald")
        t.exec("returnToKingRoald", t.player.talk_to, "king_roald", 1)
        t.exec("roald-4", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Well hello there. What do you want?",
            "npc:You have news of Drezel for me?",
            "player:Yeah, I spoke to the guys at the temple",
            "npc:YOU DID WHAT???",
            "npc:Are you mentally deficient???",
            "player:Did I make a mistake?",
            "npc:YES YOU DID!!!!!",
            "player:B-but Drezel TOLD me to...!",
            "npc:No, you absolute cretin!",
            "npc:You get back there",
            "player:Y-yes your highness.",
        })
        t.exec("expect_stage-return_to_drezel", t.quest.expect_stage, "return_to_drezel")
        roald_out("returnToKingRoald")

        -- LEG 4: back through the gate, into the temple, the gold key from a level-30 Monk of Zamorak
        gate_east("returnToTemple")
        t.exec("goto-templedoor2", t.player.goto_tile, 3407, 3488, 0)
        temple_in("returnToTemple")
        t.exec("killMonk", t.player.attack, "priestperilevilmonk3", 2, 20)
        local _, monk_detail = t.exec("attackMonk.dead", t.npc.await_dead_engaged, 400, 40, { eat = { item = "shark", below = 50 } })
        margin_row("attackMonk.margin", "Monk of Zamorak (level 30)", monk_detail)
        t.ticks(3)
        t.exec("goldKeyDropped", t.player.click_obj, "pipkey_gold", 3)
        t.exec("goldKeyHeld", t.inv.await, "pipkey_gold", 1)

        -- LEG 5: upstairs, Drezel behind the cell gate (talk-through from outside)
        temple_up("goUpToFloorOneTemple", "goUpToFloorTwoTemple")
        t.exec("talkToDrezel", t.player.click_loc, "pip_prisondoor", 1, { at = { 3415, 3489, 2 } })
        t.exec("talkThrough-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Oh! You do not appear to be one of those Zamorakians",
            "player:My name's",
            "npc:That is right! Oh, praise be to Saradomin!",
            "npc:me up here",
            "player:How is a river a good defence then?",
            "npc:Well, it is a long tale",
            "choose:You're right, we don't.",
            "player:You're right, we don't.",
            "npc:Well, let's just say",
        })
        t.exec("expect_stage-still-return", t.quest.expect_stage, "return_to_drezel")
        t.exec("talkThrough2", t.player.click_loc, "pip_prisondoor", 1, { at = { 3415, 3489, 2 } })
        t.exec("talkThrough2-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Oh! You do not appear to be one of those Zamorakians",
            "player:My name's",
            "npc:That is right! Oh, praise be to Saradomin!",
            "npc:me up here",
            "player:How is a river a good defence then?",
            "npc:Well, it is a long tale",
            "choose:Tell me anyway.",
        })
        t.exec("talkThrough2-tale", t.chat.drain, { stop_at = "options" })
        t.exec("talkThrough2-yes", t.chat.choose, "Yes.")
        t.exec("talkThrough2-tail", t.chat.drain, {})
        t.exec("expect_stage-find_drezel_key", t.quest.expect_stage, "find_drezel_key")
        local still_r, still = t.world.tile()
        t.check("talkThrough.outsideCell", still_r == "ok" and still.level == 2 and still.x <= 3415,
            "the talk-through leaves the player at " .. tostring(still_r == "ok" and (still.x .. "," .. still.z .. "," .. still.level) or still)
                .. " (want outside the cell on level 2, x <= 3415: the gate is still locked)")

        -- LEG 6: down through the temple, out, the trapdoor, the golden-key gate
        temple_down("goDownToFloorOneTemple", "goDownToGroundFloorTemple")
        temple_out("leaveTemple")
        t.exec("walk-enterUnderground", t.player.walk_to, 3405, 3506)
        trapdoor_down("enterUnderground", "enterUnderground")
        gate1_south("gate1-open")
        t.exec("gate1-msg", t.msg.expect, "The golden key unlocks the gate")

        -- LEG 7: the monuments -- study to find the key monument, swap the golden key
        t.exec("walk-monument1", t.player.walk_to, 3417, 9892)
        t.exec("study1", t.player.click_loc, "priestperil_grave_base1", 1)
        t.exec("study1-open", t.ui.await_open, "priestperil_gravemonument")
        local seed_r, mausoleum_bits = t.var.server("varp6733_priestperil_mausoleum")
        t.step("monument.seed-initialised", seed_r == "ok" and mausoleum_bits > 0 and "PASS" or "FAIL",
            "priestperil_mausoleum = " .. tostring(mausoleum_bits))
        local seed = math.floor(mausoleum_bits / 4194304) % 128
        local key_grave = 0
        for g = 1, 7 do
            if (seed + g * 17) % 7 + 1 == 3 then key_grave = g end
        end
        t.note("seed " .. seed .. " -> the key monument is grave " .. key_grave)
        t.key("escape")
        t.ticks(2)
        local key_target = t.player.by_symbol("loc", "priestperil_grave_base" .. key_grave)
        t.exec("useKeyForKey", t.player.use_on, "pipkey_gold", key_target)
        t.exec("swapKey-iron", t.inv.await, "pipkey_iron", 1, 10)
        local gold_r, gold_n = t.inv.count("pipkey_gold")
        t.check("swapKey-goldGone", gold_r == "ok" and gold_n == 0,
            "pipkey_gold in the pack after the swap: " .. tostring(gold_n) .. " (" .. tostring(gold_r) .. "; want 0)")

        -- LEG 8: the well -- fill the bucket with murky water
        local well = t.player.by_symbol("loc", "priestperil_well")
        t.exec("fillBucket", t.player.use_on, "bucket_empty", well)
        t.exec("fillBucket-murky", t.inv.await, "bucket_murkywater", 1, 10)

        -- back up: gate, ladder, temple door, stairs, ladder to the cell floor
        gate1_north("gate1-back")
        ladder_up("goUpWithWaterToSurface")
        t.exec("walk-enterTemple-back", t.player.walk_to, 3407, 3488)
        temple_in("enterTemple-back")
        temple_up("goUpWithWaterToFirstFloor", "goUpWithWaterToSecondFloor")

        -- LEG 9: the iron key on the cell gate (stage 6)
        local cell_door = t.player.by_symbol("loc", "pip_prisondoor")
        t.exec("openDoor", t.player.use_on, "pipkey_iron", cell_door)
        t.exec("unlockCell-dialog", t.chat.play, { "npc:Oh! Thank you! You have found the key!" })
        t.exec("expect_stage-unlocked_drezel", t.quest.expect_stage, "unlocked_drezel")
        local iron_r, iron_n = t.inv.count("pipkey_iron")
        t.check("openDoor-keyUsed", iron_r == "ok" and iron_n == 0,
            "pipkey_iron in the pack after the unlock: " .. tostring(iron_n) .. " (" .. tostring(iron_r) .. "; want 0)")

        -- LEG 10: into the cell, Drezel blesses the murky water, then talk
        cell_in("blessWater.cellIn")
        local drezel_cell = t.player.by_symbol("npc", "priestperiltrappedmonk")
        t.exec("blessWater", t.player.use_on, "bucket_murkywater", drezel_cell)
        t.exec("blessWater-dialog", t.chat.play, {
            "player:I have some water from the Salve. It seems to have been desecrated though.",
            "npc:Yes, good thinking adventurer! Give it to me, I will bless it!",
        })
        t.exec("blessWater-blessed", t.inv.await, "bucket_blessedwater", 1, 10)
        t.exec("talkFreed", t.player.talk_to, "priestperiltrappedmonk", 1)
        t.exec("talkFreed-dialog", t.chat.play, {
            "player:The key fitted the lock! You're free to leave now!",
            "npc:Well excellent work adventurer!",
            "player:I have some blessed water from the Salve in this bucket.",
            "npc:Yes! Great idea!",
        })
        cell_out("useBlessedWater.cellOut")

        -- LEG 11: pour it on the vampire's coffin (stage 7), tell Drezel in his cell (stage 8)
        local coffin = t.player.by_symbol("loc", "priestperil_coffin_noanim")
        t.exec("useBlessedWater", t.player.use_on, "bucket_blessedwater", coffin)
        t.exec("expect_stage-poured_blessed_water", t.quest.expect_stage, "poured_blessed_water")
        t.exec("bucket-returned", t.inv.await, "bucket_empty", 1, 10)
        cell_in("talkToDrezelAfterFreeing.cellIn")
        t.exec("talkToDrezelAfterFreeing", t.player.talk_to, "priestperiltrappedmonk", 1)
        t.exec("talkPoured-dialog", t.chat.play, {
            "player:I poured the blessed water over the vampires coffin.",
            "npc:Excellent work adventurer! I am free at last!",
            "npc:Look for me down there.",
        })
        t.exec("expect_stage-meet_in_mausoleum", t.quest.expect_stage, "meet_in_mausoleum")
        cell_out("talkToDrezelAfterFreeing.cellOut")

        -- LEG 12: down and out of the temple, west through the gate to the
        -- Varrock east bank for the first 24 essence (the pack's free slots).
        temple_down("goDownToFloorOneAfterFreeing", "goDownToGroundFloorAfterFreeing")
        temple_out("leaveTemple-2")
        gate_west("essenceTrip1")
        t.exec("goto-essenceTrip1.bank", t.player.goto_tile, 3253, 3420, 0)
        t.exec("essenceTrip1.bankOpen", t.bank.open, "fai_varrock_bankbooth", 2, { at = { 3253, 3419, 0 } })
        t.exec("essenceTrip1.depositShark", t.bank.deposit, "shark", "all")
        t.exec("essenceTrip1.depositBucket", t.bank.deposit, "bucket_empty", 1)
        t.exec("essenceTrip1.withdrawRune", t.bank.withdraw, "blankrune", 12)
        t.exec("essenceTrip1.withdrawPure", t.bank.withdraw, "blankrune_high", 12)
        t.check("essenceTrip1.bankClose", t.bank.close())
        gate_east("essenceTrip1")
        t.exec("goto-enterUndergroundAfterFreeing", t.player.goto_tile, 3405, 3506, 0)
        trapdoor_down("enterUndergroundAfterFreeing", "enterUndergroundAfterFreeing")
        gate1_south("talkToDrezelUnderground.gate1")
        gate2_east("talkToDrezelUnderground.gate2")
        t.exec("talkToDrezelUnderground", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("meetDrezel-dialog", t.chat.play, {
            "npc:Ah,",
            "player:Why, what's happened?",
            "npc:From what I can tell",
            "npc:and have used some kind",
            "npc:it will spread along",
            "player:What can we do",
            "npc:Well, as you can see",
            "npc:here focussing",
            "npc:find some kind of way",
            "player:Couldn't you bless",
            "npc:No, that would not work",
            "npc:I have only one idea",
            "player:What's that?",
            "npc:I have heard rumours",
            "npc:Should you be able",
            "player:Kind of like a filter",
            "npc:Well I have no knowledge",
        })
        t.exec("expect_stage-begin_bring_essence", t.quest.expect_stage, "begin_bring_essence")

        -- LEG 13: the holy barrier while the Salve is still polluted
        t.exec("barrierBlocked", t.player.click_loc, "pip_underground_wall_side_withportal", 1)
        t.exec("barrierBlocked-dialog", t.chat.play, {
            "npc:STOP!",
            "player:Can't I go through here?",
            "npc:No, you cannot!",
        })

        -- LEG 14: the essence -- noted essence is refused, then the two trips' hand-ins
        local drezel_m = t.player.by_symbol("npc", "priestperiltrappedmonk2")
        t.exec("certRefused", t.player.use_on, "cert_blankrune", drezel_m)
        t.exec("certRefused-dialog", t.chat.play, {
            "player:I brought you some Rune Essence.",
            "npc:You have brought me notes",
        })
        t.exec("expect_stage-still-10", t.quest.expect_stage, "begin_bring_essence")
        local cert_r, cert_n = t.inv.count("cert_blankrune")
        t.check("certRefused-notesKept", cert_r == "ok" and cert_n == 5,
            "cert_blankrune after the refusal: " .. tostring(cert_n) .. " (" .. tostring(cert_r) .. "; want all 5 kept)")
        t.exec("handIn1", t.player.use_on, "blankrune", drezel_m)
        t.ticks(2)
        t.exec("expect_stage-34", t.var.expect, "varp302_priestperil", 34)
        local h1r_r, h1r_n = t.inv.count("blankrune")
        local h1p_r, h1p_n = t.inv.count("blankrune_high")
        t.check("handIn1-allTaken", h1r_r == "ok" and h1r_n == 0 and h1p_r == "ok" and h1p_n == 0,
            "after handIn1: blankrune " .. tostring(h1r_n) .. ", blankrune_high " .. tostring(h1p_n)
                .. " (want 0 and 0: 12 + 12 given, 10 -> 34)")
        -- the notes go (Drezel answers "How many more" only to an empty-handed
        -- player: mausoleum_drezel.rs2 [label,priestperil_drezel_bring_more_essence]).
        -- The drop is an attempt (run 2: the pack fell 5 -> 0 but no ground row
        -- for cert_blankrune appeared on the player's tile); the outcome graded
        -- is the empty pack Drezel's branch reads.
        local dn_r, dn_d = t.player.drop("cert_blankrune")
        t.note("dropNotes: t.player.drop(cert_blankrune) -> " .. tostring(dn_r) .. " " .. tostring(dn_d))
        local dn_cr, dn_n = t.inv.count("cert_blankrune")
        t.check("dropNotes.packEmpty", dn_cr == "ok" and dn_n == 0,
            "cert_blankrune in the pack after the Drop press: " .. tostring(dn_n) .. " (" .. tostring(dn_cr) .. "; want 0, was 5)")
        t.exec("moreQuestion", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("moreQuestion-dialog", t.chat.play, {
            "player:How many more essences do I need to bring you?",
            "npc:I need 26 more",
        })

        -- Trip 2: Varrock Teleport (the last runes), the bank, back by the gate.
        varrock_teleport("essenceTrip2.varrockTeleport")
        t.exec("goto-essenceTrip2.bank", t.player.goto_tile, 3253, 3420, 0)
        t.exec("essenceTrip2.bankOpen", t.bank.open, "fai_varrock_bankbooth", 2, { at = { 3253, 3419, 0 } })
        t.exec("essenceTrip2.withdrawRune", t.bank.withdraw, "blankrune", 13)
        t.exec("essenceTrip2.withdrawPure", t.bank.withdraw, "blankrune_high", 13)
        t.check("essenceTrip2.bankClose", t.bank.close())
        gate_east("essenceTrip2")
        t.exec("goto-essenceTrip2.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        trapdoor_down("essenceTrip2", "essenceTrip2.descend")
        gate1_south("essenceTrip2.gate1")
        gate2_east("essenceTrip2.gate2")
        local snapshot_result, xp_before = t.skill.snapshot()
        t.check("reward.snapshot", snapshot_result, "skill snapshot before the last hand-in -> " .. tostring(snapshot_result))
        local dag0_r, dag0_n = t.inv.count("dagger_wolfbane")
        t.exec("bringDrezelEssence", t.player.use_on, "blankrune", drezel_m)
        t.exec("handIn3-dialog", t.chat.play, {
            "npc:Excellent! That should do it!",
            "npc:Please take this dagger",
            "npc:it has the power to prevent werewolves",
        })
        t.quest.expect_complete()
        local gain_result, gain_detail = t.skill.expect_gain("prayer", 1406, xp_before)
        t.check("reward.prayer_xp", gain_result, "prayer gain 1406 xp -> " .. tostring(gain_detail))
        local dag1_r, dag1_n = t.inv.count("dagger_wolfbane")
        t.check("reward.dagger", dag0_r == "ok" and dag1_r == "ok" and dag0_n == 0 and dag1_n == 1,
            "dagger_wolfbane in the pack " .. tostring(dag0_n) .. " -> " .. tostring(dag1_n)
                .. " (" .. tostring(dag0_r) .. "/" .. tostring(dag1_r) .. "; want 0 -> 1, the Wolfbane dagger)")

        -- LEG 15: the barrier at 60 says to speak to Drezel first; his advice grants passage (61)
        t.exec("barrierAdvice", t.player.click_loc, "pip_underground_wall_side_withportal", 1)
        t.exec("barrierAdvice-dialog", t.chat.play, {
            "npc:STOP!",
            "player:Can't I go through here?",
            "npc:Yes, now the Salve is restored you may, but speak to me first",
        })
        t.exec("talkAdvice", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("talkAdvice-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("expect_stage-access_holy_barrier", t.quest.expect_stage, "access_holy_barrier")

        -- the lost dagger (mausoleum_drezel.rs2 [label,reclaim_wolfbane_dagger]): dropped, Drezel returns it
        t.exec("dropDagger", t.player.drop, "dagger_wolfbane")
        t.exec("reclaimDagger", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("reclaimDagger-dialog", t.chat.play, {
            "player:I've lost my wolfbane dagger.",
            "npc:Yes, I know! Luckily for you it washed up",
            "npc:It's a family heirloom after all!",
            "player:Thanks for that!",
        })
        t.exec("reclaimDagger-held", t.inv.await, "dagger_wolfbane", 1, 10)

        -- LEG 16: through the barrier into Morytania
        t.exec("barrierPass", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
        t.exec("barrierPass-msg", t.msg.expect, "You pass through the holy barrier")
        t.finish(0)
    end,
}
