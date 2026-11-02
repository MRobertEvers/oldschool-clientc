-- The flow is RSProx index 4459's (rev 241, 2026-11-02): docs/rsprox-analysis/
-- quests/priest_in_peril.md. At stage 2 the trapdoor leads into the player's
-- own copy of the cellar (temple_guardian.rs2), so the dog leg checks an
-- instance tile, not 3405,9906.
--
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
--     (a knock until stage 4, then a walk-through: temple_doors.rs2);
--     paterdomus_spiralstairs 3417,3492 (2x2) up from 3416,3493 and
--     spiralstairstop 3417,3493,1 down (no maplink row: the climb up is
--     ladders.rs2 [proc,climb] +-1 plane on the tile; the way down lands on
--     3417,3494,0, measured in run 2); ladder / laddertop 3410,3485
--     from 3410,3486 on levels 1/2.
--   * Drezel's cell x 3416-3418 z 3483-3494 on level 2: pip_prisondoor on
--     the east edge of 3415,3489. Drezel is talked to through the bars from
--     3415,3489 ([apnpc1] at range 2, RSProx 4459 [1193]); after the iron key
--     the gate is a walk-through. The coffin 3413,3486,2 is OUTSIDE the cell.
--   * The underground: trapdoor 3405,3507 (trapdoors.rs2: telejump +6400 to
--     3405,9906), ladder_from_cellar 3405,9907 (maplink 0_53_154_13_50 ->
--     3405,3506), pip_underground_door1 3405,9895 and door2 3431,9897
--     (walk-throughs, area_mausoleum/scripts/gates.rs2), the holy barrier
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
        -- the recorder's Prayer: 1,406 xp brings no level-up box over the talk
        -- that follows the scroll (4459 [2044..2047])
        "::setlevel prayer 43",
        "::give rune_scimitar 1",
        "::give shark 12",
        "::give bucket_empty 1",
        -- five Varrock Teleports (magic_spells.dbrow: 1 fire, 3 air, 1 law)
        "::give firerune 5",
        "::give airrune 15",
        "::give lawrune 5",
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

                -- The flow is RSProx index 4459's (rev 241, 2026-11-02): pages, menus,
        -- stages and varbits as recorded; docs/rsprox-analysis/quests/
        -- priest_in_peril.md. Lines the recording never reaches are the wiki's.
        local EAT = { item = "shark", below = 50 }
        local function expect_varbit(name, sym, want)
            local vr, v = t.var.server(sym)
            t.check(name, vr == "ok" and v == want, sym .. " = " .. tostring(v) .. " (" .. tostring(vr) .. "; want " .. want .. ")")
        end
        local function knock(name)
            t.exec("goto-" .. name, t.player.goto_tile, 3407, 3488, 0)
            t.exec(name, t.player.click_loc, "priestperiltempledoorr", 1, { at = { 3408, 3488, 0 } })
        end
        local function await_tile(name, ok, desc, ticks)
            t.await({ level = function()
                local r, tile = t.world.tile()
                return r == "ok" and ok(tile)
            end, note = name .. ": " .. desc }, ticks or 8)
            local r, tile = t.world.tile()
            t.check(name, r == "ok" and ok(tile), "player at " .. tostring(r == "ok" and (tile.x .. "," .. tile.z .. "," .. tile.level) or tile) .. " (want " .. desc .. ")")
        end
        local function cell_front(name)
            t.exec("walk-" .. name, t.player.walk_to, 3415, 3489)
        end
        -- Drezel through the bars, from the gate's tile (an ap at the default
        -- range, RSProx 4459 [1160]). Asked again if a click is refused.
        local function talk_drezel(name)
            local r, d
            for try = 1, 4 do
                r, d = t.player.talk_to("priestperiltrappedmonk", 1)
                if r == "ok" then break end
                t.note(name .. ": try " .. try .. " -> " .. tostring(r) .. " " .. tostring(d))
                t.exec("walk-" .. name .. ".retry" .. try, t.player.walk_to, 3415, 3489)
                t.ticks(4)
            end
            t.check(name, r == "ok", "talk_to priestperiltrappedmonk -> " .. tostring(r) .. " " .. tostring(d))
        end

        -- LEG 1: King Roald [91..165]. "No." to the start first (the wiki's
        -- pages), then the recorded start and both of its questions.
        varrock_teleport("talkToRoald.varrockTeleport")
        roald_in("talkToRoald")
        t.exec("talkToRoald", t.player.talk_to, "king_roald", 1)
        t.exec("roald-refuse", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Yes, citizen. Do you need something?",
            "choose:I'm looking for a quest!",
            "player:I'm looking for a quest!",
            "npc:A quest you say? Hmm... What an odd request",
            "npc:Are you aware of Paterdomus?",
            "choose:No, I don't think I know it...",
            "player:No, I don't think I know it...",
            "npc:Hmm... How strange that you don't.",
            "choose:No.",
            "player:No. That sounds boring.",
            "npc:Yes, I dare say it does.",
        })
        t.exec("expect_stage-not-started", t.quest.expect_stage, "not_started")
        t.exec("talkRoald2", t.player.talk_to, "king_roald", 1)
        t.exec("roald-start", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Yes, citizen. Do you need something?",
            "choose:I'm looking for a quest!",
            "player:I'm looking for a quest!",
            "npc:A quest you say?",
            "npc:Are you aware of Paterdomus?",
            "choose:No, I don't think I know it...",
            "player:No, I don't think I know it...",
            "npc:Hmm... How strange that you don't.",
            "choose:Yes.",
            "player:Sure. I don't have anything better to do right now.",
            "npc:Many thanks, adventurer! I would have sent one of the squires",
            "choose:Why do you care about Drezel anyway?",
            "player:Why do you care about Drezel anyway?",
            "npc:Well, that is a slightly impertinent question",
            "npc:As you are no doubt aware, this kingdom worships Saradomin",
            "npc:Paterdomus, the temple where Drezel lives",
            "player:Evil?",
            "npc:Oh yes. Morytania is a fearful place",
            "npc:Thankfully, the sacred River Salve",
            "npc:Drezel is descended from one of the original",
            "npc:This is the reason why the lack of communication",
            "choose:Do I get a reward for this?",
            "player:Do I get a reward for this?",
            "npc:You will be rewarded with the knowledge",
            "player:So... that would be a 'no' then?",
            "npc:That is correct.",
            "choose:I'll get going.",
            "player:I'll get going.",
        })
        t.exec("expect_stage-started", t.quest.expect_stage, "started")
        t.exec("startLine", t.msg.expect, "You've started a new quest")
        t.exec("talkRoald3", t.player.talk_to, "king_roald", 1)
        t.exec("roald-stage1", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Yes, citizen. Do you need something?",
            "choose:About that job I'm doing...",
            "player:About that job I'm doing...",
            "npc:You have news of Drezel for me?",
            "choose:Where am I supposed to go again?",
            "player:Where am I supposed to go again?",
            "npc:The temple of Paterdomus where Drezel lives.",
            "choose:I have to go.",
            "player:I have to go.",
        })
        roald_out("talkToRoald")

        -- LEG 2: the knock [518..558], a joke first (the wiki's), then the
        -- fake Drezel and the errand; the knock at stage 2 [572..577].
        gate_east("goToTemple")
        knock("knock1")
        t.exec("knock1-joke", t.chat.play, {
            "mesbox:You knock at the door...",
            "npc:Who are you and what do you want?",
            "player:Er...",
            "choose:Hi, I just moved in next door.",
            "player:Hi, I just moved in next door.",
            "npc:What?",
            "player:Yeah, and I was just wondering if I could borrow a cup of coffee?",
            "npc:Next door? Coffee?",
            "choose:Actually, never mind.",
            "player:Actually, never mind.",
        })
        t.exec("expect_stage-still-started", t.quest.expect_stage, "started")
        knock("knock2")
        t.exec("knock2-errand", t.chat.play, {
            "mesbox:You knock at the door...",
            "npc:Who are you and what do you want?",
            "player:Er...",
            "choose:King Roald sent me to check on Drezel.",
            "player:King Roald sent me to check on Drezel.",
            "npc:Drezel? Right... I, er... Just give me a moment.",
            "mesbox:You hear someone moving around",
            "npc:Okay, he's just coming! Wait a second!",
            "npc:Hello, my name is Drevil.",
            "npc:Drezel!",
            "npc:I mean Drezel. How can I help?",
            "player:Er... Is everything okay in there?",
            "npc:Yes, totally fine.",
            "player:Well, the King hadn't heard from you for a few days.",
            "npc:I see... And, uh, what would you do",
            "player:I'm not sure. Ask you what help you need I suppose.",
            "npc:Ah, good, well, I don't think...",
            "npc:Hey... The dog!",
            "npc:Ah, actually there is something I could use some help with.",
            "player:What kind of favour?",
            "npc:Well, there's this horrible big... dog-like thing",
            "player:I don't know... Something about all this seems a bit suspicious",
            "npc:No, no, there's no need for that.",
            "player:Right...",
            "npc:Then please, deal with that dog for me.",
            "player:Hmm...",
        })
        t.exec("expect_stage-agree", t.quest.expect_stage, "agree_to_kill_dog")
        knock("knock3")
        t.exec("knock3-notYet", t.chat.play, {
            "mesbox:You knock at the door...",
            "npc:Hello? Is that you again, adventurer? Have you dealt with that dog yet?",
            "player:Not yet...",
            "npc:But you're going to, right? For good old Delzig?",
            "npc:Drezel!",
            "npc:Sorry, for good old Drezel, right?",
            "player:Hmm...",
        })
        local tile_r, tile = t.world.tile()
        t.check("knock.stillOutside", tile_r == "ok" and tile.x <= 3408,
            "after the knocks the player is at " .. tostring(tile_r == "ok" and (tile.x .. "," .. tile.z .. "," .. tile.level) or tile)
                .. " (want outside the temple, x <= 3408: a knock never opens the door before stage 4)")

        -- LEG 3: back to King Roald at stage 2 [622..648].
        varrock_teleport("reportRoald2.varrockTeleport")
        roald_in("reportRoald2")
        t.exec("reportRoald2", t.player.talk_to, "king_roald", 1)
        t.exec("roald-stage2", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Yes, citizen. Do you need something?",
            "choose:About that job I'm doing...",
            "player:About that job I'm doing...",
            "npc:You have news of Drezel for me?",
            "player:Well... I went to the temple like you asked me to",
            "npc:Ah, well that must have been Drezel then.",
            "player:Well... he seemed to be having some kind of trouble.",
            "npc:Well, I expect you to offer him your full assistance",
            "player:But-",
            "npc:Now, run along.",
            "player:Well... okay then.",
        })
        expect_varbit("roald2.varbit", "varb15612_priestperil_returned_to_roald", 1)
        roald_out("reportRoald2")

        -- LEG 4: the Temple Guardian, in the player's own copy of the cellar
        -- [728..776]; the ladder leaves the copy for 3405,3506 [791..796].
        gate_east("goDownToDog")
        t.exec("goto-goDownToDog", t.player.goto_tile, 3405, 3506, 0)
        local TRAP_AT = { 3405, 3507, 0 }
        if t.world.loc_near("trapdoor", 3, { at = TRAP_AT }) == "ok" then
            t.exec("goDownToDog.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = TRAP_AT })
            t.ticks(2)
        end
        t.exec("goDownToDog.climb", t.player.click_loc, "trapdoor_open", 1, { at = TRAP_AT })
        await_tile("goDownToDog.inCopy", function(p) return p.x > 6400 end,
            "in the cellar copy (an instance tile, x > 6400)", 10)
        t.ticks(2)
        t.exec("killTheDog", t.player.attack, "priestperilguarddog", 2, 30, { eat = EAT })
        local _, dog_detail = t.exec("attackDog.dead", t.npc.await_dead_engaged, 300, 40, { eat = EAT })
        margin_row("attackDog.margin", "Temple Guardian (level 30)", dog_detail)
        t.ticks(4)
        t.exec("expect_stage-killed_dog", t.quest.expect_stage, "killed_dog")
        t.exec("climbUpAfterKillingDog", t.player.click_loc, "ladder_from_cellar", 1)
        await_tile("climbUpAfterKillingDog.landed", function(p) return p.x == 3405 and p.z == 3506 and p.level == 0 end,
            "3405,3506,0 above the trapdoor", 10)

        -- LEG 5: the fake Drezel's thanks [808..823] and its short form [829..831].
        knock("knock4")
        t.exec("knock4-thanks", t.chat.play, {
            "mesbox:You knock at the door...",
            "npc:Is that you, adventurer? Have you dealt with that dog?",
            "player:Yes, it's done.",
            "npc:Really? Well... thank you!",
            "npc:Ha! Brilliant! Good work, adventurer.",
            "player:Don't mention it... Is there anything else you need?",
            "npc:I think it's safe to say that you've done more than enough",
            "player:Okay... I guess I'd better go and let the King know.",
        })
        expect_varbit("knock4.varbit", "varb15613_priestperil_returned_to_fake_drezel", 1)
        knock("knock5")
        t.exec("knock5-again", t.chat.play, {
            "mesbox:You knock at the door...",
            "npc:Is that you, adventurer? Once again, thank you so much",
            "player:Don't mention it... Now, I guess I'd better report back to the King.",
        })

        -- LEG 6: King Roald's anger [874..902]: stage 4 and the monks' hunt.
        varrock_teleport("returnToKingRoald.varrockTeleport")
        roald_in("returnToKingRoald")
        t.exec("returnToKingRoald", t.player.talk_to, "king_roald", 1)
        t.exec("roald-stage3", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Yes, citizen. Do you need something?",
            "choose:About that job I'm doing...",
            "player:About that job I'm doing...",
            "npc:You have news of Drezel for me?",
            "player:Well, I went to the temple and spoke to... Drezel.",
            "npc:A dog? Tell me you didn't do it?!",
            "player:Well...",
            "npc:Are you a complete imbecile?",
            "player:I tried to tell you it was a bit suspicious",
            "npc:Do you even realise what you've done?",
            "npc:That 'dog' was responsible for guarding the entrance",
            "player:But Drezel...",
            "npc:Do you not see what's happened here",
            "npc:Now you get back there and get this mess sorted out.",
            "player:Y-yes, your highness.",
        })
        t.exec("expect_stage-return_to_drezel", t.quest.expect_stage, "return_to_drezel")
        expect_varbit("roald3.hunt", "varb15614_priestperilevilmonk_hunt", 1)
        roald_out("returnToKingRoald")

        -- LEG 7: through the doors (walk-through from stage 4) among monks that
        -- now attack; the hooded leader drops the golden key [1086..1140].
        gate_east("returnToTemple")
        t.exec("goto-templedoor2", t.player.goto_tile, 3407, 3488, 0)
        temple_in("returnToTemple")
        local leader_dead = false
        for round = 1, 6 do
            local ar, ad = t.player.attack("priestperilevilmonk3", 2, 20, { eat = EAT })
            if ar == "ok" then
                local dr, dd = t.npc.await_dead_engaged(400, 40, { eat = EAT })
                t.check("killLeader.dead", dr == "ok", "the hooded leader: " .. tostring(dr) .. " " .. tostring(dd))
                leader_dead = dr == "ok"
                break
            end
            t.note("round " .. round .. ": the leader was not engaged (" .. tostring(ad) .. "); fighting the monk that hunts us")
            local mr = t.player.attack("priestperilevilmonk1", 2, 20, { eat = EAT })
            if mr ~= "ok" then
                mr = t.player.attack("priestperilevilmonk2", 2, 20, { eat = EAT })
            end
            if mr == "ok" then
                t.npc.await_dead_engaged(300, 40, { eat = EAT })
            end
            t.ticks(2)
        end
        t.check("killLeader", leader_dead, "the hooded leader killed within six rounds")
        t.exec("leaderDrop.line", t.msg.expect, "Untradeable drop: Golden key")
        expect_varbit("leaderDrop.varbit", "varb15615_priestperilevilmonk_dead", 1)
        t.exec("goldKeyDropped", t.player.click_obj, "pipkey_gold", 3)
        t.exec("goldKeyHeld", t.inv.await, "pipkey_gold", 1)

        -- LEG 8: Drezel through the bars [1160..1193], the history from the
        -- menu, the stage and the hunt with the last page; again with the key
        -- [1196..1198]; the coffin [1206].
        temple_up("goUpToFloorOneTemple", "goUpToFloorTwoTemple")
        cell_front("talkToDrezel")
        talk_drezel("talkToDrezel")
        t.exec("drezel-meet", t.chat.play, {
            "player:Hello.",
            "npc:Oh! You do not appear to be one of those Zamorakians",
            "player:My name's",
            "npc:That's right! Oh, praise be to Saradomin!",
            "player:Why? What are they planning to do?",
            "npc:I believe they aim to desecrate the blessings",
            "choose:Seven Priestly Warriors?",
            "player:Seven Priestly Warriors?",
            "npc:Indeed. While this isn't the... ideal time for a history lesson",
            "npc:It is said that many years ago",
            "npc:Morytania wasn't enough for them though.",
            "npc:They led the armies of Misthalin",
            "npc:For ten days and nights they fought",
            "npc:On the eleventh day, reinforcements arrived",
            "npc:However, their sacrifice was not in vain.",
            "npc:Those blessings remain to this day",
            "player:And what about the mausoleum below?",
            "npc:That is the most important part of the temple",
            "npc:Their tombs are built around the well",
            "player:I see. And you think those Zamorakians",
            "npc:I fear that is almost certainly their goal.",
            "player:So what do we do?",
            "npc:First, we need to get me out of this cell.",
            "player:Yes, I think I might have it here.",
            "npc:Excellent work! However, that isn't our only problem.",
            "player:What do you mean?",
            "npc:When the Salve was first blessed",
            "npc:Even with them weakened",
            "npc:This coffin here contains one of those vampyres.",
            "player:But why?",
            "npc:Well, as those fiends pointed out to me with delight",
            "npc:If I get too close, it will probably wake up",
            "player:Maybe I could kill it somehow?",
            "npc:I would not risk waking it.",
            "player:Right. So, I need to find the key to your cell",
            "npc:Good luck.",
        })
        t.exec("expect_stage-find_drezel_key", t.quest.expect_stage, "find_drezel_key")
        expect_varbit("drezel.hunt", "varb15614_priestperilevilmonk_hunt", 2)
        talk_drezel("talkToDrezel2")
        t.exec("drezel-goldKey", t.chat.play, {
            "npc:How goes it, adventurer?",
            "player:I have this key I took from the leader of the Zamorakians!",
            "npc:Excellent work! Quickly, try it on the gate",
        })
        t.exec("coffin1", t.player.click_loc, "priestperil_coffin_noanim", 1)
        t.exec("coffin1-line", t.chat.play, { "player:It sounds like there's something alive inside it." })

        -- LEG 9: the mausoleum: the north gate (the golden key, no line) [1250],
        -- Study and Take-from [1265..1374], the key swap [1381], the well [1396..1406].
        temple_down("goDownToFloorOneTemple", "goDownToGroundFloorTemple")
        temple_out("leaveTemple")
        t.exec("walk-enterUnderground", t.player.walk_to, 3405, 3506)
        trapdoor_down("enterUnderground", "enterUnderground")
        gate1_south("gate1-open")
        t.exec("walk-monument3", t.player.walk_to, 3427, 9890)
        t.exec("study3", t.player.click_loc, "priestperil_grave_base3", 1)
        t.exec("study3-open", t.ui.await_open, "priestperil_gravemonument")
        t.key("escape")
        t.ticks(2)
        local hp0_r, hp0 = t.skill.read("hitpoints")
        t.exec("take6", t.player.click_loc, "priestperil_grave_base6", 2)
        t.exec("take6-line", t.msg.expect, "You try and take the pot from the monument but a holy power stops you!")
        t.ticks(2)
        local hp1_r, hp1 = t.skill.read("hitpoints")
        t.check("take6.damage", hp0_r == "ok" and hp1_r == "ok" and hp0.level - hp1.level == 1,
            "hitpoints " .. tostring(hp0_r == "ok" and hp0.level) .. " -> " .. tostring(hp1_r == "ok" and hp1.level)
                .. " (want a 1: 8 of 8 recorded)")
        local key_monument = t.player.by_symbol("loc", "priestperil_grave_base3")
        t.exec("useKeyForKey", t.player.use_on, "pipkey_gold", key_monument)
        t.exec("swapKey-box", t.chat.play, { "*" })
        t.exec("swapKey-iron", t.inv.await, "pipkey_iron", 1, 10)
        local gold_r, gold_n = t.inv.count("pipkey_gold")
        t.check("swapKey-goldGone", gold_r == "ok" and gold_n == 0,
            "pipkey_gold in the pack after the swap: " .. tostring(gold_n) .. " (" .. tostring(gold_r) .. "; want 0)")
        local well = t.player.by_symbol("loc", "priestperil_well")
        t.exec("inspectWell", t.player.click_loc, "priestperil_well", 1)
        t.exec("inspectWell-box", t.chat.play, { "mesbox:You look down the well and see the water of the River Salve moving slowly along." })
        t.exec("fillBucket", t.player.use_on, "bucket_empty", well)
        t.exec("fillBucket-box", t.chat.play, { "*" })
        t.exec("fillBucket-murky", t.inv.await, "bucket_murkywater", 1, 10)

        -- LEG 10: back up; Drezel with the water [1533..1558] and the history menu.
        gate1_north("gate1-back")
        ladder_up("goUpWithWaterToSurface")
        t.exec("walk-enterTemple-back", t.player.walk_to, 3407, 3488)
        temple_in("enterTemple-back")
        temple_up("goUpWithWaterToFirstFloor", "goUpWithWaterToSecondFloor")
        cell_front("drezelWater")
        talk_drezel("drezelWater")
        t.exec("drezelWater-dialog", t.chat.play, {
            "npc:How goes it, adventurer?",
            "player:I have some water from the Salve.",
            "npc:Hmm... Yes, that could work.",
            "player:Which means I'm going to need to get this gate open first.",
            "npc:Indeed.",
            "choose:I'll get going.",
            "player:I'll get going.",
        })

        -- LEG 11: the iron key on the gate [1570] (stage 6), then the blessing
        -- with the water in hand [1591..1613].
        local cell_door = t.player.by_symbol("loc", "pip_prisondoor")
        t.exec("openDoor", t.player.use_on, "pipkey_iron", cell_door)
        t.exec("unlockCell-dialog", t.chat.play, {
            "*",
            "player:The key fit the lock!",
            "npc:Excellent work! Now we just need the vampyre dealing with.",
            "player:Well, I have some water from the Salve here.",
            "npc:Very good. Let me have a look at it.",
            "*",
            "npc:It's as I feared.",
            "player:I'll do that right away.",
        })
        t.exec("expect_stage-unlocked_drezel", t.quest.expect_stage, "unlocked_drezel")
        expect_varbit("unlock.hunt", "varb15614_priestperilevilmonk_hunt", 1)
        local iron_r, iron_n = t.inv.count("pipkey_iron")
        t.check("openDoor-keyUsed", iron_r == "ok" and iron_n == 0,
            "pipkey_iron in the pack after the unlock: " .. tostring(iron_n) .. " (" .. tostring(iron_r) .. "; want 0)")
        t.exec("blessWater-blessed", t.inv.await, "bucket_blessedwater", 1, 10)

        -- LEG 12: Open on the coffin, "Yes." [1617..1624] (stage 7); Drezel [1645] (stage 8).
        t.exec("pour", t.player.click_loc, "priestperil_coffin_noanim", 1)
        t.exec("pour-dialog", t.chat.play, {
            "player:It sounds like there's something alive inside it.",
            "choose:Yes.",
            "*",
        })
        t.exec("expect_stage-poured_blessed_water", t.quest.expect_stage, "poured_blessed_water")
        t.exec("bucket-returned", t.inv.await, "bucket_empty", 1, 10)
        cell_front("talkToDrezelAfterFreeing")
        talk_drezel("talkToDrezelAfterFreeing")
        t.exec("talkPoured-dialog", t.chat.play, {
            "player:I poured the blessed water over the coffin.",
            "npc:Excellent work, adventurer! Now, meet me down in the mausoleum.",
        })
        t.exec("expect_stage-meet_in_mausoleum", t.quest.expect_stage, "meet_in_mausoleum")

        -- LEG 13: the first essence trip (west on foot to the Varrock east bank).
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

        -- LEG 14: the barrier's "Stop!" [1749..1752], the meeting [1763..1782] (stage 10).
        t.exec("barrierBlocked", t.player.click_loc, "pip_underground_wall_side_withportal", 1)
        t.exec("barrierBlocked-dialog", t.chat.play, {
            "npc:Stop!",
            "player:Can't I go through there?",
            "npc:No, you cannot!",
        })
        t.exec("talkToDrezelUnderground", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("meetDrezel-dialog", t.chat.play, {
            "npc:Glad you made it.",
            "player:How bad is it?",
            "npc:From what I can tell, those Zamorakians used some kind of potion",
            "npc:Before long, the blessings placed upon the river",
            "player:Could you not bless the river",
            "npc:I'm afraid not.",
            "npc:However, I believe there might still be a way",
            "npc:I have managed to reinforce the barrier",
            "player:Soak up the magic? How?",
            "npc:I'm sure you know of runes",
            "player:Ah, I see where this is going.",
            "npc:Exactly. If you could bring me fifty essence",
        })
        t.exec("expect_stage-begin_bring_essence", t.quest.expect_stage, "begin_bring_essence")

        -- LEG 15: the essence: notes refused, 24 given [1789..1793], "How much more".
        local drezel_m = t.player.by_symbol("npc", "priestperiltrappedmonk2")
        t.exec("certRefused", t.player.use_on, "cert_blankrune", drezel_m)
        t.exec("certRefused-dialog", t.chat.play, {
            "player:I brought you some rune essence.",
            "npc:You have brought me notes",
        })
        t.exec("handIn1", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("handIn1-dialog", t.chat.play, {
            "player:I brought you some rune essence.",
            "*",
            "npc:Thank you. I need 26 more.",
        })
        t.exec("expect_stage-34", t.var.expect, "varp302_priestperil", 34)
        local dn_r, dn_d = t.player.drop("cert_blankrune")
        t.check("dropNotes", dn_r == "ok", "t.player.drop(cert_blankrune) -> " .. tostring(dn_r) .. " " .. tostring(dn_d))
        t.exec("moreQuestion", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("moreQuestion-dialog", t.chat.play, {
            "player:How much more essence do I need to bring you?",
            "npc:I need 26 more.",
        })

        -- LEG 16: the second trip and the end [2033..2085]: the box, the pages,
        -- the dagger page's click completes it, the talk after the scroll,
        -- Drezel's blessing (61), "And you. See you later."
        varrock_teleport("essenceTrip2.varrockTeleport")
        t.exec("goto-essenceTrip2.bank", t.player.goto_tile, 3253, 3420, 0)
        t.exec("essenceTrip2.bankOpen", t.bank.open, "fai_varrock_bankbooth", 2, { at = { 3253, 3419, 0 } })
        -- No teleport is cast after this one: the runes go in, 26 essence fit.
        t.exec("essenceTrip2.depositFire", t.bank.deposit, "firerune", "all")
        t.exec("essenceTrip2.depositAir", t.bank.deposit, "airrune", "all")
        t.exec("essenceTrip2.depositLaw", t.bank.deposit, "lawrune", "all")
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
        t.exec("bringDrezelEssence", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("handIn2-dialog", t.chat.play, {
            "player:I brought you some more essence.",
            "*",
            "npc:Excellent! That should do it!",
            "player:What about the Zamorakians upstairs?",
            "npc:They hopefully shouldn't be too much trouble",
            "npc:For now though, please take this dagger.",
        })
        t.quest.expect_complete()
        expect_varbit("end.varbit", "varb15617_priestperil_ending_dialogue", 1)
        local gain_result, gain_detail = t.skill.expect_gain("prayer", 1406, xp_before)
        t.check("reward.prayer_xp", gain_result, "prayer gain 1406 xp -> " .. tostring(gain_detail))
        t.exec("reward.dagger", t.inv.await, "dagger_wolfbane", 1, 5)
        -- expect_complete shuts the scroll and opens the journal, which cuts
        -- off the talk the scroll's close queued (4459 [2047]); at stage 60
        -- Drezel's own Talk-to gives the same pages.
        t.exec("afterScroll", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("afterScroll-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Into Morytania? Yes, you can.",
            "npc:I must warn you though, Morytania is an evil land.",
            "npc:Worst of all are the vampyres",
            "player:I'll be careful. Anything else I should know?",
            "npc:The first settlement you'll likely come across is Canifis",
            "npc:You should keep that dagger I gave you to hand.",
            "player:I see. Thanks.",
            "npc:One last thing.",
            "player:The Myreque?",
            "npc:Yes. They're a band of freedom fighters.",
            "npc:Veliaf, the leader of their Mort Myre group",
            "player:I'll keep an eye out for them.",
            "npc:Right, I think that's everything.",
            "mesbox:Drezel blesses you, allowing you to pass through the barrier into Morytania.",
            "npc:Good luck out there. Stay safe.",
            "choose:And you. See you later.",
            "player:And you. See you later.",
        })
        t.exec("expect_stage-access_holy_barrier", t.quest.expect_stage, "access_holy_barrier")

        -- LEG 17: the lost dagger (LostCity's, unverified) and the barrier.
        t.exec("dropDagger", t.player.drop, "dagger_wolfbane")
        t.exec("reclaimDagger", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("reclaimDagger-dialog", t.chat.play, {
            "player:I've lost my wolfbane dagger.",
            "npc:Yes, I know! Luckily for you it washed up",
            "npc:It's a family heirloom after all!",
            "player:Thanks for that!",
        })
        t.exec("reclaimDagger-held", t.inv.await, "dagger_wolfbane", 1, 10)
        t.exec("barrierPass", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
        t.exec("barrierPass-msg", t.msg.expect, "You pass through the holy barrier")
        t.finish(0)
    end,
}
