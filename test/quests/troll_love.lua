-- Troll Romance (quest_troll_love), driven from Quest Helper's TrollRomance.java ladder.
-- Prerequisites staged by ::complete (Troll Stronghold, Death Plateau). The sled materials, the
-- tar/wax/tin, the climbing boots and the combat kit are the guide's brought-along items
-- (TrollRomance.java getItemRequirements: iron bar, maple logs, rope, cake tin, swamp tar, bucket of
-- wax, climbing boots, combat gear).
--
-- Door rule (b69 re-drive): no goto_tile lands in or leaves a closed space. The Trollheim summit is a
-- pocket on foot (eadgar.lua, sampler b58 round 2), so EVERY trip up walks the whole way: Tenzing's
-- fence gate and his front and back doors, the stile, both troll_climbingrocks pairs, the secret
-- door, the prison stairs and the prison door. Ug's room (troll_stronghold_interior_door
-- 2833,10070,1) and Aga's room (2828,10091,1) are entered and left by their doors on every visit.
-- Off the stronghold to Tenzing is a Camelot Teleport cast from the spellbook; after Arrg the arena
-- exit and the troll pass lead back onto the summit and in at the stronghold's front door. The
-- gotos left are overland hops between open tiles (Lumbridge -> the Taverley members' gate, which
-- is pressed; -> Tenzing's lane; -> Dunstan's street; -> the rocks), or hops inside one walkable
-- component (the summit, the Trollweiss cave), each REACH on reach.py.
return {
    id = "troll_love",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000, -- three trips up the mountain, each walked floor by floor
    setup = {
        "::clearinv",
        "::complete quest_trollstronghold", -- quest_troll.rs2: the climbing rocks, the secret door and
        -- the prison door gate on %troll_quest
        "::complete quest_deathplateau", -- Tenzing's front and back doors walk you through
        -- (death_doors_mechanism.rs2) and Tenzing/Dunstan reach their Trollweiss arms
        "::give cake_tin 1",
        "::give swamp_tar 1",
        "::give bucket_wax 1",
        "::give iron_bar 1", -- Dunstan's sled materials (death_dunstan.rs2:273-289), brought along
        "::give maple_logs 1",
        "::give rope 1",
        "::give death_climbingboots 1", -- troll_climbingrocks needs them worn from its south side
        -- (quest_troll.rs2:16, coordz 3611)
        "::setlevel agility 28", -- the quest's own requirement (trollromance_sled.rs2 slide check);
        -- the rocks roll stat_random(agility, 160, 300) (upass_obstacles.rs2:50), cross_trap re-presses a slip
        "::setlevel magic 45", -- Camelot Teleport off the stronghold to Tenzing (the summit is a pocket on foot);
        -- no Troll Romance dialogue reads magic
        "::give airrune 5",
        "::give lawrune 1",
        "::setlevel attack 85",
        "::setlevel strength 85",
        "::setlevel defence 85",
        "::setlevel hitpoints 90",
        "::give dragon_scimitar 1",
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 12",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp385_troll_love",
            constants = {
                not_started = 0,
                started = 5,
                aga_wants_trollweiss = 10,
                learnt_about_trollweiss = 15,
                bring_dunstan_materials = 20,
                dunstan_made_sled = 22,
                waxed_sled = 25,
                picked_trollweiss = 30,
                dispose_of_arrg = 35,
                defeated_arrg = 40,
                complete = 45,
            },
            row = "quest_trollromance",
            display = "Troll Romance",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        -- seam troll_love_arrg_and_sleds: Arrg is the OSRS wiki block (max 38 melee / 30 ranged);
        -- the armour is worn from the start (the guide's "Combat gear").
        for _, item in ipairs({ "rune_full_helm", "rune_chainbody", "rune_platelegs", "rune_kiteshield", "dragon_scimitar" }) do
            t.exec("wear-" .. item, t.player.equip, item)
        end

        ------------------------------------------------------------------ helpers
        local MAX_HP = 90
        local EAT_BELOW = 50
        local hp_low = nil
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
            end
            return tostring(r)
        end
        -- Sample hitpoints after every walk and crossing; eat below EAT_BELOW (the stronghold's trolls).
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    t.player.inv_op("shark", 1)
                    t.ticks(1)
                end
            end
        end
        local function trip_margin(name, trip)
            vitals()
            local fr, food = t.inv.count("shark")
            t.check(name, hp_low ~= nil and hp_low * 4 >= MAX_HP and fr == "ok" and food >= 1,
                trip .. ": lowest hp " .. tostring(hp_low) .. "/" .. MAX_HP .. " (sampled after every walk and crossing), sharks left "
                    .. tostring(food) .. " (" .. tostring(fr) .. ") (margin: lowest hp >= a quarter of max AND food left)")
            hp_low = nil
        end
        local function walk(name, x, z, level, ticks, tol)
            tol = tol or 1
            local wr, wd = t.player.walk_to(x, z, ticks)
            vitals()
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and tt.level == level and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol,
                "walk_to(" .. x .. "," .. z .. ") -> " .. tostring(wr) .. " " .. tostring(wd) .. "; tile " .. tile_text(r, tt)
                    .. " (want within " .. tol .. " of " .. x .. "," .. z .. "," .. level .. ")")
        end
        local ROCK_VITALS = { eat = "shark", below = EAT_BELOW }

        -- Tenzing's yard and house from the lane east of his gate.
        local function tenzing_in(pfx)
            t.exec("goto-" .. pfx .. ".tenzingLane", t.player.goto_tile, 2826, 3555, 0)
            t.exec(pfx .. ".tenzingGateIn", t.player.pass_door, { closed = "death_fencegate_l", open = "death_openfencegate_l",
                at = { 2824, 3555, 0 }, near = { 2825, 3555 }, far = { 2823, 3555 } })
            t.exec(pfx .. ".tenzingDoorIn", t.player.cross_gate, { loc = "death_sherpa_door", at = { 2822, 3555, 0 }, near = { 2823, 3555 },
                far_ok = function(tile) return tile.x >= 2819 and tile.x <= 2822 and tile.z >= 3554 and tile.z <= 3557 end,
                far_desc = "inside Tenzing's house, x 2819-2822 z 3554-3557" })
        end
        local function tenzing_out(pfx)
            t.exec(pfx .. ".tenzingDoorOut", t.player.cross_gate, { loc = "death_sherpa_door", at = { 2822, 3555, 0 }, near = { 2822, 3555 },
                far_ok = function(tile) return tile.x >= 2823 end, far_desc = "in Tenzing's yard, x >= 2823" })
            t.exec(pfx .. ".tenzingGateOut", t.player.pass_door, { closed = "death_fencegate_l", open = "death_openfencegate_l",
                at = { 2824, 3555, 0 }, near = { 2824, 3555 }, far = { 2826, 3555 } })
        end
        -- Tenzing's house -> back door -> stile -> both rock pairs -> the secret door -> the prison ->
        -- the prison stairs (row `stairs_name`) -> west through the prison door, level 1.
        local function walk_up(pfx, stairs_name, first)
            tenzing_in(pfx)
            if first then
                t.exec("equipClimbingBoots", t.player.equip, "death_climbingboots")
            end
            t.exec(pfx .. ".tenzingBackDoor", t.player.cross_gate, { loc = "death_sherpa_backdoor", at = { 2820, 3557, 0 }, near = { 2820, 3557 },
                far_ok = function(tile) return tile.z >= 3558 end, far_desc = "north of Tenzing's back door, z >= 3558" })
            walk("walk-" .. pfx .. ".toStile", 2817, 3561, 0, 20, 0)
            t.exec(pfx .. ".stile", t.player.cross_trap, { loc = "death_fullstyle", op_name = "Climb-over",
                at = { 2817, 3562, 0 }, src = { 2817, 3561 }, dest = { 2817, 3564 }, attempts = 1 })
            t.exec("goto-" .. pfx .. ".rocks", t.player.goto_tile, 2856, 3611, 0)
            t.exec(pfx .. ".rocksSouth", t.player.cross_trap, { loc = "troll_climbingrocks", op_name = "Climb",
                at = { 2856, 3612, 0 }, src = { 2856, 3611 }, dest = { 2856, 3613 }, attempts = 4, vitals = ROCK_VITALS })
            walk("walk-" .. pfx .. ".toRocksNorth", 2834, 3627, 0, 80, 0)
            t.exec(pfx .. ".rocksNorth", t.player.cross_trap, { loc = "troll_climbingrocks", op_name = "Climb",
                at = { 2834, 3628, 0 }, src = { 2834, 3627 }, dest = { 2834, 3629 }, attempts = 4, vitals = ROCK_VITALS })
            walk("walk-" .. pfx .. ".toSecretDoor", 2827, 3646, 0, 60, 2)
            -- The secret door's disguised rock face never renders a hittable pixel (eadgar.lua RUN 4, b55):
            -- drive.op sends the op and the server runs [oploc1,troll_stronghold_entrance]
            -- (quest_troll.rs2:114-120); graded on its exact landing p_teleport(0_44_157_7_2) = 2823,10050,0.
            local secret = t.player.by_symbol("loc", "troll_stronghold_entrance")
            local sop, sdet = t.drive.op(secret, 1)
            t.ticks(3)
            local sr, st = t.world.tile()
            t.check(pfx .. ".secretDoor", sr == "ok" and st.x == 2823 and st.z == 10050 and st.level == 0,
                "drive.op(troll_stronghold_entrance) -> " .. tostring(sop) .. " " .. tostring(sdet) .. "; tile " .. tile_text(sr, st)
                    .. " (want 2823,10050,0)")
            walk("walk-" .. pfx .. ".prisonCorridor", 2837, 10090, 0, 90)
            walk("walk-" .. pfx .. ".toPrisonStairs", 2851, 10106, 0, 140)
            t.exec(stairs_name, t.player.climb, { loc = "troll_stronghold_stairs", at = { 2852, 10106, 0 }, dest = { 2852, 10109, 1 }, slack = 1 })
            vitals()
            -- selfstage (open 500 ticks): the open leaf is the same symbol one tile west (2847,10107,1),
            -- so a door still open from the last trip is walked through, not pressed shut (run 1 up2).
            t.exec(pfx .. ".prisonDoor", t.player.pass_door, { closed = "troll_stronghold_prison_door_closed",
                open = "troll_stronghold_prison_door_closed", at = { 2848, 10107, 1 }, near = { 2848, 10107 }, far = { 2845, 10107 },
                far_ok = function(tile) return tile.x <= 2847 end, far_desc = "west of the prison door, x <= 2847" })
            vitals()
        end
        -- Ug's room (level 1, x 2823-2833 z 10064-10090) by its east door, and Aga's room north of it.
        local function ug_in(pfx)
            walk("walk-" .. pfx .. ".toUgDoor", 2834, 10070, 1, 60)
            t.exec(pfx .. ".ugDoorIn", t.player.pass_door, { closed = "troll_stronghold_interior_door", open = "troll_stronghold_interior_door_open",
                at = { 2833, 10070, 1 }, near = { 2834, 10070 }, far = { 2832, 10070 } })
        end
        local function ug_out(pfx)
            t.exec(pfx .. ".ugDoorOut", t.player.pass_door, { closed = "troll_stronghold_interior_door", open = "troll_stronghold_interior_door_open",
                at = { 2833, 10070, 1 }, near = { 2832, 10070 }, far = { 2835, 10070 } })
        end
        local function aga_in(pfx)
            t.exec(pfx .. ".agaDoorIn", t.player.pass_door, { closed = "troll_stronghold_interior_door", open = "troll_stronghold_interior_door_open",
                at = { 2828, 10091, 1 }, near = { 2828, 10090 }, far = { 2828, 10093 } })
        end
        local function aga_out(pfx)
            t.exec(pfx .. ".agaDoorOut", t.player.pass_door, { closed = "troll_stronghold_interior_door", open = "troll_stronghold_interior_door_open",
                at = { 2828, 10091, 1 }, near = { 2828, 10092 }, far = { 2828, 10089 } })
        end

        ---------------------------------------------------------------- 0: Ug
        -- The run's first goto obeys the door rule: from Lumbridge the only way on foot to Burthorpe is
        -- the members' gate south of Taverley (reach.py 3206,3233 -> 2826,3555: NEEDS-DOOR via
        -- membergater@2935,3450; 2934,3322 -> 2826,3555 REACH closed-doors 451).
        t.exec("goto-talkToUg.memberGate", t.player.goto_tile, 2934, 3318, 0)
        t.exec("talkToUg.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320" })
        walk_up("up1", "goUpToUg", true)
        ug_in("talkToUg")
        t.exec("talkToUg", t.player.talk_to, "trollromance_ug", 1)
        t.exec("talkToUg-dialog", t.chat.play, {
            "npc:Arrrghhh, die man-thing!",
            "npc:Ahhh, it no use, I too sad!",
            "choose:Awww, you poor troll. What seems to be the problem?",
            "player:Awww, you poor troll. What see",
            "npc:I love Aga, she so beautiful, ",
            "npc:But Arrg that... arrrrrg! He t",
            "choose:Don't worry now, I'll see what I can do.",
            "player:Don't worry now, I'll see what",
            "npc:You help Ug? You nice, maybe U",
            "player:Errrr... thanks... I think?",
            "player:I will go and talk to Aga.",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        ---------------------------------------------------------------- 5: Aga
        walk("walk-talkToAga.toAgaDoor", 2828, 10090, 1, 40, 0)
        aga_in("talkToAga")
        t.exec("talkToAga", t.player.talk_to, "trollromance_aga", 1)
        t.exec("talkToAga-dialog", t.chat.play, {
            "npc:What you want, man-thing?",
            "choose:So... how's your... um... love life?",
            "player:(I can't believe I am asking a",
            "player:So... how's your... um... love",
            "npc:It ok, I with Arrg, he very st",
            "npc:I not know if he love Aga,",
            "npc:It a very rare, beautiful flow",
            "npc:It grow somewhere in these mou",
            "player:(Maybe trolls DO have a romant",
            "player:And Arrg said he would get you",
            "npc:He very strong, if he love Aga",
            "choose:Errr... I've got to go.",
            "player:Errr... I've got to go.",
        })
        t.ticks(2)
        t.expect("quest.stage.aga_wants_trollweiss", t.quest.expect_stage("aga_wants_trollweiss"))
        aga_out("leaveAga")
        walk("walk-leaveAga.toUgDoor", 2832, 10070, 1, 40, 0)
        ug_out("leaveAga")
        trip_margin("up1.margin", "Lumbridge up through the stronghold to Ug and Aga")

        ---------------------------------------------------------------- 10: Tenzing
        -- Off the stronghold: Camelot Teleport (magic_spells.dbrow [magic_spell_teleport_camelot]: 5 air +
        -- 1 law, landing 2757,3478,0), then overland to Tenzing's lane (reach.py REACH closed-doors 686).
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "teleportCamelotToTenzing",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        tenzing_in("talkToTenzing")
        t.exec("talkToTenzing", t.player.talk_to, "death_sherpa", 1)
        t.exec("talkToTenzing-dialog", t.chat.play, {
            "player:Hello Tenzing!",
            "npc:Hello again traveller. What can I do for you?",
            "player:Do you know where I can find Trollweiss?",
            "npc:Trollweiss used to grow all over",
            "player:What would I need to get there?",
            "npc:You'd need to head up into the domain",
            "npc:The plateau used to be easy enough",
            "npc:If you could make some sort of sled",
            "npc:Just remember, once you slide down",
            "mesbox:You should go and speak to Dunstan",
        })
        t.ticks(2)
        t.expect("quest.stage.learnt_about_trollweiss", t.quest.expect_stage("learnt_about_trollweiss"))
        tenzing_out("leaveTenzing")

        ---------------------------------------------------------------- 15: Dunstan
        -- The materials are in the pack (the guide's bring-alongs), so ONE conversation asks for the sled
        -- (stage 20) and hands it over (stage 22): death_dunstan.rs2:273-289 writes
        -- ^troll_love_bring_dunstan_materials and then checks the pack in the same label.
        -- ANY-OF: talkToDunstanAgain talkToDunstan death_dunstan.rs2:273-289 asks for the materials and builds the sled in one talk when they are carried
        t.exec("goto-talkToDunstan.street", t.player.goto_tile, 2921, 3569, 0)
        t.exec("talkToDunstan.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2921, 3571, 0 }, near = { 2921, 3571 }, far = { 2921, 3572 } })
        t.exec("talkToDunstan", t.player.talk_to, "death_smithy", 1)
        t.exec("talkToDunstan-dialog", t.chat.play, {
            "player:Hi!",
            "npc:Hi! Did you want something?",
            "choose:I wanted to ask about something else.",
            "choose:Can you build me a sled to get to Trollweiss?",
            "player:Can you build me a sled to get to Trollweiss?",
            "npc:A sled, eh? Should be no trouble",
            "player:I've got everything you need right here.",
            "mesbox:A short while later, Dunstan hands you a sled.",
            "npc:There you go, one sled!",
            "player:Where would I find some wax?",
            "npc:Maybe you should look for some bees.",
            "npc:Anything else before I get on with my work?",
            "choose:I wanted to ask about something else.",
            "choose:Nothing, thanks.",
            "player:Nothing, thanks.",
        })
        t.ticks(2)
        t.expect("quest.stage.dunstan_made_sled", t.quest.expect_stage("dunstan_made_sled"))
        t.exec("sled.have", t.inv.await, "trollromance_toboggon", 1, 6)
        do
            local br, bar = t.inv.count("iron_bar")
            local lr, logs = t.inv.count("maple_logs")
            local rr, rope = t.inv.count("rope")
            t.check("talkToDunstan.materialsTaken", br == "ok" and bar == 0 and lr == "ok" and logs == 0 and rr == "ok" and rope == 0,
                "iron_bar " .. tostring(bar) .. ", maple_logs " .. tostring(logs) .. ", rope " .. tostring(rope) .. " (want all 0: Dunstan took them)")
        end

        ---------------------------------------------------------------- 22: wax
        t.exec("useTarOnWax", t.player.use_item_on_item, "swamp_tar", "bucket_wax")
        t.exec("useTarOnWax.made", t.inv.await, "trollromance_wax", 1, 8)
        t.exec("useWaxOnSled", t.player.use_item_on_item, "trollromance_wax", "trollromance_toboggon")
        t.exec("useWaxOnSled.made", t.inv.await, "trollromance_toboggon_waxed", 1, 8)
        t.ticks(2)
        t.expect("quest.stage.waxed_sled", t.quest.expect_stage("waxed_sled"))
        t.exec("leaveDunstan.doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2921, 3571, 0 }, near = { 2921, 3572 }, far = { 2921, 3569 } })

        ---------------------------------------------------------------- 25: the mountain
        -- Up through the stronghold again, out at its top exit onto the summit, then the summit hop to the
        -- cave mouth (reach.py 2840,3690 -> 2822,3744 REACH closed-doors 208).
        walk_up("up2", "toCave.prisonStairsUp", false)
        walk("walk-toCave.northStairs", 2841, 10108, 1, 20)
        t.exec("toCave.northStairsUp", t.player.climb, { loc = "troll_stronghold_stairs", at = { 2842, 10108, 1 }, dest = { 2845, 10108, 2 }, slack = 1 })
        vitals()
        walk("walk-toCave.topExit", 2837, 10090, 2, 80)
        t.exec("toCave.topExit", t.player.climb, { loc = "troll_stronghold_top_exit_mid", at = { 2838, 10090, 2 }, dest = { 2840, 3690, 0 }, slack = 1 })
        trip_margin("up2.margin", "Dunstan up through the stronghold to the summit")
        t.exec("goto-enterTrollCave", t.player.goto_tile, 2822, 3744, 0)
        t.exec("enterTrollCave", t.player.climb, { loc = "trollromance_caveentrance", at = { 2820, 3743, 0 }, dest = { 2803, 10187, 0 }, slack = 1 })
        -- inside the cave: reach.py 2803,10187 -> 2772,10232 REACH closed-doors 112
        t.exec("goto-leaveTrollCave", t.player.goto_tile, 2772, 10232, 0)
        t.exec("leaveTrollCave", t.player.climb, { loc = "trollromance_snow_cavewall_crevis", at = { 2772, 10233, 0 }, dest = { 2778, 3869, 0 }, slack = 1 })
        t.ticks(2)

        -- The first slope: the barrier 2772-2773,3835 is pressed from its top side (reach.py 2778,3869 ->
        -- 2772,3836 REACH closed-doors 39); the ride lands at 0_43_59_38_18 = 2790,3794 (trollromance_sled.rs2).
        walk("walk-sledSouth.toBarrier", 2772, 3836, 0, 40, 0)
        t.exec("equipSled", t.player.inv_op, "trollromance_toboggon_waxed", 2)
        t.ticks(3)
        t.exec("sledSouth", t.player.click_loc, "trollromance_piste_walk_barrier_down", 1, { at = { 2772, 3835 } })
        t.exec("sledSouth.cutscene", t.cutscene.await, "slide1", { timeout = 60, expect = {
            { op = "moveto", coord = "0_43_59_18_46" },
            { op = "lookat", coord = "0_43_59_21_33" },
            { op = "moveto", coord = "0_43_59_35_36" },
            { op = "lookat", coord = "0_43_59_28_31" },
            { op = "reset" },
        } })
        t.ticks(6)
        do
            local r, p = t.world.tile()
            t.check("sledSouth.landed", r == "ok" and p.x == 2790 and p.z == 3794 and p.level == 0,
                "landed at " .. tile_text(r, p) .. " (want 2790,3794,0: the first ride's p_teleport(0_43_59_38_18))")
        end

        -- the 1-in-250 rock crash (trollromance_sled.rs2:272-300): the harness hook ::trollromance_crash runs it
        -- from the first ride's tile; it lands on the same ledge (0_43_59_47_18 = 2799,3794) as the ride.
        do local r = t.cheat("::trollromance_crash"); t.check("sledCrash.hook", r, "::trollromance_crash answered " .. tostring(r)) end
        t.exec("sledCrash.cutscene", t.cutscene.await, "sledCrash", { timeout = 60, expect = {
            { op = "moveto", coord = "0_43_59_26_39" },
            { op = "lookat", coord = "0_43_59_22_44" },
            { op = "reset" },
        } })
        t.ticks(10)
        t.exec("sledCrash.chat", t.chat.play, { "player:And I thought snow was soft", "player:Although it was a little softer", "end" })
        t.exec("sledCrash.pickup", t.player.click_obj, "trollromance_toboggon_waxed", 3)
        t.exec("sledCrash.have", t.inv.await, "trollromance_toboggon_waxed", 1, 8)
        t.exec("equipSledAfterCrash", t.player.inv_op, "trollromance_toboggon_waxed", 2)
        t.ticks(3)

        -- reach.py 2790,3794 -> 2778,3784 REACH closed-doors 22
        walk("walk-pickFlowers", 2778, 3784, 0, 30, 1)
        -- The rider is anim-protected (trollromance_sled.rs2 [oploc2,trollromance_rareflowers]), so the pick
        -- shows nothing the click's settle can see (run 1: `settle_after_click` with the flower in the
        -- pack): graded on the flower arriving.
        do
            local pr, pd = t.player.click_loc("trollromance_rareflowers", 2, { at = { 2777, 3784 } })
            local fr, fd = t.inv.await("trollromance_rare_flower", 1, 8)
            t.check("pickFlowers", (pr == "ok" or pr == "timeout") and fr == "ok",
                "click_loc trollromance_rareflowers op2 at 2777,3784 -> " .. tostring(pr) .. " " .. tostring(pd)
                    .. "; inv.await trollromance_rare_flower -> " .. tostring(fr) .. " " .. tostring(fd))
        end
        t.expect("quest.stage.picked_trollweiss", t.quest.expect_stage("picked_trollweiss"))

        -- The second slope: the barrier 2785-2786,3771 from its top side; lands at 0_43_58_42_7 = 2794,3719.
        walk("walk-sledSouthAgain.toBarrier", 2785, 3772, 0, 40, 0)
        t.exec("sledSouthAgain", t.player.click_loc, "trollromance_piste_walk_barrier_down", 1, { at = { 2785, 3771 } })
        t.exec("sledSouthAgain.cutscene", t.cutscene.await, "slide2", { timeout = 80, expect = {
            { op = "moveto", coord = "0_43_58_38_11" },
            { op = "lookat", coord = "0_43_58_30_17" },
            { op = "moveto", coord = "0_43_58_31_17" },
            { op = "lookat", coord = "0_43_58_40_9" },
            { op = "reset" },
        } })
        t.ticks(6)
        do
            local r, p = t.world.tile()
            t.check("sledSouthAgain.landed", r == "ok" and p.x == 2794 and p.z == 3719 and p.level == 0,
                "landed at " .. tile_text(r, p) .. " (want 2794,3719,0: the second ride's p_teleport(0_43_58_42_7))")
        end

        -- Out of the piste pocket by its exit tunnel (maplink 2795-2797,3719 -> 2799,10134), through the
        -- tunnel (reach.py 2799,10134 -> 2773,10162 REACH 66) and out at its bottom (-> 2730,3713).
        t.exec("leavePiste.exitTunnel", t.player.climb, { loc = "trollromance_piste_exit_tunnel_exit", at = { 2795, 3717, 0 },
            src = { 2796, 3719 }, dest = { 2799, 10134, 0 }, slack = 1 })
        walk("walk-leavePiste.tunnel", 2773, 10162, 0, 60, 1)
        t.exec("leavePiste.tunnelBottom", t.player.climb, { loc = "trollromance_piste_exit_tunnel_bottom", at = { 2771, 10161, 0 },
            dest = { 2730, 3713, 0 }, slack = 1 })
        t.exec("sled.stowed", t.inv.await, "trollromance_toboggon_waxed", 1, 10)

        ---------------------------------------------------------------- 30: back to Ug
        -- reach.py 2730,3713 -> 2826,3555 REACH closed-doors 1100: overland to Tenzing's lane, then up again.
        walk_up("up3", "goUpToUgAgain", false)
        ug_in("talkToUgWithFlowers")
        trip_margin("up3.margin", "the piste exit up through the stronghold to Ug")
        t.exec("talkToUgWithFlowers", t.player.talk_to, "trollromance_ug", 1)
        t.exec("talkToUgWithFlowers-dialog", t.chat.play, {
            "npc:Have you got flower yet?",
            "player:Yes, I've got it right here.",
            "npc:Thanks man-thing. Ug so happy!",
            "npc:But me too scared to give Trollweiss",
            "player:What? So I have to get rid of Arrg",
            "player:Then again maybe not.",
            "npc:You no touch Aga. Ug kill you.",
            "player:Ok, I'll tell Arrg you said that.",
            "npc:No, no, no, wait!",
            "player:I suppose I am a bit of a legend.",
        })
        t.ticks(2)
        t.expect("quest.stage.dispose_of_arrg", t.quest.expect_stage("dispose_of_arrg"))

        ---------------------------------------------------------------- 35: Arrg
        t.exec("equipScimitar", t.player.equip, "dragon_scimitar")
        t.ticks(2)
        walk("walk-challengeArrg.toAgaDoor", 2828, 10090, 1, 40, 0)
        aga_in("challengeArrg")
        t.exec("challengeArrg", t.player.talk_to, "trollromance_arrg", 1)
        t.exec("challengeArrg-dialog", t.chat.play, {
            "npc:Whaaaaaaaat?",
            "player:Ehh, Excuse me... Mr. Troll, sir,",
            "choose:I am here to kill you!",
            "player:I am here to kill you!",
            "npc:Very good, Arrg was getting hungry.",
            "player:But not in front of the lady",
            "choose:Enter the arena. This is not a safe death.",
        })
        t.ticks(4)
        t.exec("killArrg", t.player.attack, "trollromance_arrg_attackable", 2, 20)
        local arrg_r, arrg_d = t.npc.await_dead_engaged(400, 40, { eat = { item = "shark", below = 65 } })
        t.step("killArrg.dead", arrg_r == "ok" and "PASS" or "FAIL", tostring(arrg_r) .. " " .. tostring(arrg_d))
        do
            local low = tonumber(string.match(tostring(arrg_d), "lowest hp (%d+)/") or "")
            local fr, food = t.inv.count("shark")
            t.check("killArrg.margin", low ~= nil and low * 4 >= MAX_HP and fr == "ok" and food >= 1,
                "Arrg (hp 140, max 38 melee / 30 ranged) in rune with a dragon scimitar: lowest hp " .. tostring(low) .. "/" .. MAX_HP
                    .. ", sharks left " .. tostring(food) .. " (" .. tostring(fr) .. ") (margin: lowest hp >= a quarter of max AND food left)")
        end
        t.ticks(10)
        t.expect("quest.stage.defeated_arrg", t.quest.expect_stage("defeated_arrg"))

        ---------------------------------------------------------------- 40: report to Ug
        -- Out of the arena by its north exit (selfstage, quest_troll.rs2:100-112), through the troll pass
        -- (maplink 2904,3643 -> 2907,10019; 2907,10035 -> 2908,3654) onto the summit, the summit hop to the
        -- stronghold's front door (reach.py 2908,3654 -> 2840,3690 REACH 208), and down the north stairs.
        walk("walk-leaveArena.toExit", 2916, 3628, 0, 40, 1)
        t.exec("leaveArena.exit", t.player.pass_door, { closed = "troll_stronghold_arena_exit_left",
            at = { 2916, 3629, 0 }, near = { 2916, 3628 }, far = { 2916, 3630 },
            far_ok = function(tile) return tile.z >= 3629 end, far_desc = "north of the arena exit, z >= 3629" })
        walk("walk-trollPass.toEntrance", 2904, 3643, 0, 40, 0)
        t.exec("trollPass.enter", t.player.climb, { loc = "troll_pass_entrance", at = { 2903, 3644, 0 }, src = { 2904, 3643 },
            dest = { 2907, 10019, 0 }, slack = 1 })
        walk("walk-trollPass.toExit", 2907, 10035, 0, 60, 0)
        t.exec("trollPass.exit", t.player.climb, { loc = "troll_pass_exit", at = { 2906, 10036, 0 }, src = { 2907, 10035 },
            dest = { 2908, 3654, 0 }, slack = 1 })
        t.exec("goto-enterStrongholdForEnd", t.player.goto_tile, 2840, 3690, 0)
        t.exec("enterStrongholdForEnd", t.player.climb, { loc = "troll_stronghold_door", at = { 2839, 3689, 0 }, src = { 2840, 3690 },
            dest = { 2837, 10090, 2 }, slack = 1 })
        t.drive.camera(0, 383, 600) -- a flatter pitch: the default one framed unrendered void (eadgar.lua runs 6/7)
        vitals()
        walk("walk-goDownToUgForEnd", 2843, 10106, 2, 60)
        t.exec("goDownToUgForEnd", t.player.climb, { loc = "troll_stronghold_stairstop", at = { 2843, 10108, 2 }, dest = { 2841, 10108, 1 }, slack = 1 })
        vitals()
        ug_in("returnToUg")
        trip_margin("up4.margin", "the arena through the troll pass and the stronghold to Ug")

        local xp_result, xp_snapshot = t.skill.snapshot()
        t.step("returnToUg.xp_before", xp_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(xp_result))
        t.exec("returnToUg", t.player.talk_to, "trollromance_ug", 1)
        t.exec("returnToUg-dialog", t.chat.play, {
            "npc:You defeat Arrg yet?",
            "player:Yes, he has been defeated.",
            "npc:You very strong and nice.",
            "player:Thanks, Ug. So, now you can go and speak to Aga!",
            "npc:I too scared.",
            "player:Has anyone ever told you that you are a useless troll?",
            "npc:Whaaat? Man-thing want to die?",
        })
        t.ticks(4)

        t.quest.expect_complete()
        t.check("reward.agility", t.skill.expect_gain("agility", 8000, xp_snapshot), "8000 Agility XP")
        t.check("reward.strength", t.skill.expect_gain("strength", 4000, xp_snapshot), "4000 Strength XP")
        t.check("reward.diamond", t.inv.expect_has("uncut_diamond", 1), "1 uncut diamond")
        t.check("reward.ruby", t.inv.expect_has("uncut_ruby", 2), "2 uncut rubies")
        t.check("reward.emerald", t.inv.expect_has("uncut_emerald", 4), "4 uncut emeralds")
        t.finish(0)
    end,
}
