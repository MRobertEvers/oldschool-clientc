-- My Arm's Big Adventure (quest_myarmsbigadventure), driven from Quest Helper's
-- MyArmsBigAdventure.java ladder against the quest's own scripts
-- (OSRS-Content/osrs239-content/server/scripts/quests/quest_myarmsbigadventure/,
-- Burntmeat's dispatcher in quest_eadgar/scripts/eadgar_troll_chief_cook.rs2,
-- My Arm's Talk-to in quest_makingfriendswithmyarm/scripts/makingfriendswithmyarm.rs2).
--
-- Setup: the prerequisites Quest Helper names (Eadgar's Ruse, The Feud,
-- Jungle Potion, Woodcutting 10, Farming 29, Tai Bwo Wannai favour 60%) and
-- the travel prerequisites under them (Troll Stronghold and Death Plateau,
-- whose rocks, secret door and Tenzing's doors gate on them). The items are
-- Quest Helper's getItemRequirements(): climbing boots, 3 Ugthanki dung, 8
-- supercompost, rake, seed dibber, spade, bucket; and its recommended combat
-- gear and food. Nothing the quest makes is given: the goutweedy lump, the
-- farming manual and the hardy gout tubers are all fetched for real.
--
-- Door rule: no goto_tile lands in or leaves a closed space. The Trollheim
-- summit and the stronghold roof are pockets on foot (reach.py: 2840,3690 ->
-- 2829,3695 UNREACHABLE; the roof is reached only by myarm_ladder), so every
-- trip up walks Tenzing's gate and doors, the stile, both rock pairs, the
-- secret door, the prison, the prison door and the stairs; the roof is
-- climbed to and from by the troll ladder. Off the stronghold to Death
-- Plateau is a Camelot Teleport cast from the spellbook. The quest itself
-- moves the player to Ardougne, Brimhaven and back (p_telejump in My Arm's
-- and Barnaby's scripts). The gotos left are overland hops between open
-- tiles, each REACH on reach.py.
return {
    id = "myarmsbigadventure",
    fixture = "fresh_lumbridge.ini",
    max_frames = 360000, -- three walks up the mountain floor by floor, Karamja, and two roc fights
    setup = {
        "::clearinv",
        "::complete quest_eadgarsruse", -- Quest Helper: Eadgar's Ruse FINISHED (quest_cheat.rs2 dbrow name)
        "::complete quest_feud", -- The Feud FINISHED
        "::complete quest_junglepotion", -- Jungle Potion FINISHED
        "::complete quest_trollstronghold", -- quest_troll.rs2: the climbing rocks, the secret door and the prison door gate on %troll_quest
        "::complete quest_deathplateau", -- Tenzing's front and back doors walk you through (death_doors_mechanism.rs2)
        "::setvar varb907_favour_percentage 60", -- Quest Helper: >= 60% Tai Bwo Wannai Cleanup favour (that minigame is not ported)
        "::setvar varb900_chat_murc 1", -- Murcaily's multinpc shows her from 1 (0 is the village before Jungle Potion); nothing in this tree sets it
        "::setlevel woodcutting 10", -- quest requirement
        "::setlevel farming 29", -- quest requirement
        "::setlevel agility 15", -- troll_climbingrocks' own minimum (quest_troll.rs2:11); the roll is re-pressed by cross_trap
        "::setlevel magic 45", -- Camelot Teleport off the stronghold to Death Plateau; no quest script reads magic
        "::setlevel prayer 43", -- Protect from Missiles, the guide's prayer for the Giant Roc
        -- Margin for the stronghold's trolls and the rocs (Giant Roc: 250 hp, max 14 melee / 20 ranged);
        -- no quest script branches on a combat stat (grep: no stat( or combat level read in quest_myarmsbigadventure)
        "::setlevel attack 75",
        "::setlevel strength 75",
        "::setlevel defence 75",
        "::setlevel hitpoints 80",
        "::give dragon_scimitar 1",
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give death_climbingboots 1", -- Quest Helper: climbing boots
        "::give bucket_empty 1", -- Quest Helper: bucket (for the pot)
        "::give feud_camel_pooh_bucket 3", -- Quest Helper: 3 Ugthanki dung
        "::give bucket_supercompost 8", -- Quest Helper: 8 supercompost (7 for the patch, 1 for planting)
        "::give rake 1",
        "::give dibber 1",
        "::give spade 1",
        "::give airrune 5", -- one Camelot Teleport (magic_spells.dbrow: 5 air + 1 law)
        "::give lawrune 1",
        "::give shark 8",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb2790_myarm",
            constants = {
                not_started = 0,
                burntmeat_asked = 10,
                burntmeat_story = 20,
                burntmeat_introduced = 30,
                talk_to_myarm = 40,
                name_explained = 50,
                lump_needed = 60,
                lump_eaten = 70,
                on_roof = 80,
                manual_given = 90,
                manual_read = 100,
                treat_patch = 110,
                patch_treated = 120,
                karamja_planned = 130,
                at_ardougne = 150,
                at_brimhaven = 160,
                at_tai = 170,
                see_murcaily = 180,
                murcaily_asked = 190,
                got_tubers = 210,
                back_at_ardougne = 220,
                dulce_domum = 230,
                growing = 240,
                grown = 250,
                baby_dead = 260,
                giant_dead = 270,
                harvested = 280,
                see_burntmeat = 290,
                burntmeat_rewarding = 300,
                burntmeat_rewarded = 310,
                complete = 320,
            },
            row = "quest_myarmsbigadventure",
            display = "My Arm's Big Adventure",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        for _, item in ipairs({ "rune_full_helm", "rune_chainbody", "rune_platelegs", "rune_kiteshield", "dragon_scimitar" }) do
            t.exec("wear-" .. item, t.player.equip, item)
        end

        ------------------------------------------------------------------ helpers
        local MAX_HP = 80
        local EAT_BELOW = 45
        local hp_low = nil
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
            end
            return tostring(r)
        end
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
        local function stage(name)
            t.ticks(2)
            t.expect("quest.stage." .. name, t.quest.expect_stage(name))
        end
        local ROCK_VITALS = { eat = "shark", below = EAT_BELOW }

        -- Lane east of Tenzing's gate -> his yard -> his house -> back door -> stile -> both rock pairs ->
        -- the secret door -> the prison -> the prison stairs -> west through the prison door, level 1
        -- (troll_love.lua's walk; the rocks roll stat_random(agility, 160, 300), upass_obstacles.rs2:50,
        -- and cross_trap re-presses a slip from where it fell).
        local function walk_up(pfx, first)
            t.exec("goto-" .. pfx .. ".tenzingLane", t.player.goto_tile, 2826, 3555, 0)
            t.exec(pfx .. ".tenzingGateIn", t.player.pass_door, { closed = "death_fencegate_l", open = "death_openfencegate_l",
                at = { 2824, 3555, 0 }, near = { 2825, 3555 }, far = { 2823, 3555 } })
            t.exec(pfx .. ".tenzingDoorIn", t.player.cross_gate, { loc = "death_sherpa_door", at = { 2822, 3555, 0 }, near = { 2823, 3555 },
                far_ok = function(tile) return tile.x >= 2819 and tile.x <= 2822 and tile.z >= 3554 and tile.z <= 3557 end,
                far_desc = "inside Tenzing's house, x 2819-2822 z 3554-3557" })
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
                at = { 2856, 3612, 0 }, src = { 2856, 3611 }, dest = { 2856, 3613 }, attempts = 6, vitals = ROCK_VITALS })
            walk("walk-" .. pfx .. ".toRocksNorth", 2834, 3627, 0, 80, 0)
            t.exec(pfx .. ".rocksNorth", t.player.cross_trap, { loc = "troll_climbingrocks", op_name = "Climb",
                at = { 2834, 3628, 0 }, src = { 2834, 3627 }, dest = { 2834, 3629 }, attempts = 6, vitals = ROCK_VITALS })
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
            t.exec(pfx .. ".prisonStairs", t.player.climb, { loc = "troll_stronghold_stairs", at = { 2852, 10106, 0 }, dest = { 2852, 10109, 1 }, slack = 1 })
            vitals()
            t.exec(pfx .. ".prisonDoor", t.player.pass_door, { closed = "troll_stronghold_prison_door_closed",
                open = "troll_stronghold_prison_door_closed", at = { 2848, 10107, 1 }, near = { 2848, 10107 }, far = { 2845, 10107 },
                far_ok = function(tile) return tile.x <= 2847 end, far_desc = "west of the prison door, x <= 2847" })
            vitals()
            -- Up the north stairs to the top floor (Quest Helper's goUpToChef is the prison stairs above).
            walk("walk-" .. pfx .. ".toNorthStairs", 2841, 10108, 1, 20)
            t.exec(pfx .. ".northStairsUp", t.player.climb, { loc = "troll_stronghold_stairs", at = { 2842, 10108, 1 }, dest = { 2845, 10108, 2 }, slack = 1 })
            vitals()
        end
        -- Top floor -> the south staircase down to the kitchen floor (Quest Helper goDownToChef, 2844,10052,2).
        local function down_to_kitchen(name)
            walk("walk-" .. name, 2843, 10053, 2, 80)
            t.exec(name, t.player.climb, { loc = "troll_stronghold_stairstop", at = { 2843, 10051, 2 }, dest = { 2841, 10051, 1 }, slack = 1 })
            vitals()
        end
        -- Kitchen floor -> the south staircase up (Quest Helper goUpFromF1ToMyArm, 2843,10052,1).
        local function up_from_kitchen(name)
            walk("walk-" .. name, 2841, 10051, 1, 40)
            t.exec(name, t.player.climb, { loc = "troll_stronghold_stairs", at = { 2842, 10051, 1 }, dest = { 2845, 10051, 2 }, slack = 1 })
            vitals()
        end
        -- Top floor -> My Arm's troll ladder (2831,10077,2; maplink -> 2831,3676,0) onto the roof.
        local function up_to_roof(name)
            walk("walk-" .. name, 2831, 10076, 2, 80, 0)
            t.exec(name, t.player.climb, { loc = "myarm_ladder", at = { 2831, 10077, 2 }, dest = { 2831, 3676, 0 }, slack = 1 })
            t.ticks(2)
            vitals()
        end
        -- Roof -> myarm_exit (2831,3677; maplink -> 2831,10076,2).
        local function down_from_roof(name)
            walk("walk-" .. name, 2831, 3676, 0, 40, 0)
            t.exec(name, t.player.climb, { loc = "myarm_exit", at = { 2831, 3677, 0 }, dest = { 2831, 10076, 2 }, slack = 1 })
            t.ticks(2)
            vitals()
        end
        local function inv_row(name, item, want, why)
            local r, n = t.inv.count(item)
            t.check(name, r == "ok" and n == want, item .. " " .. tostring(n) .. " (" .. tostring(r) .. ", want " .. want .. ") -- " .. why)
        end
        local function var_row(name, var, want, why)
            local r, v = t.var.varbit(var)
            t.check(name, r == "ok" and v == want, var .. " = " .. tostring(v) .. " (" .. tostring(r) .. ", want " .. want .. ") -- " .. why)
        end

        ------------------------------------------------------------ 0: Burntmeat
        -- The run's first goto obeys the door rule: from Lumbridge the only way on foot to Burthorpe is
        -- the members' gate south of Taverley (reach.py 3206,3233 -> 2826,3555: NEEDS-DOOR via
        -- membergater@2935,3450; 2934,3322 -> 2826,3555 REACH closed-doors 451).
        t.exec("goto-enterStronghold.memberGate", t.player.goto_tile, 2934, 3318, 0)
        t.exec("enterStronghold.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320" })
        walk_up("enterStronghold", true)
        down_to_kitchen("goDownToChef")
        trip_margin("up1.margin", "Lumbridge up through the stronghold to the kitchen")

        t.exec("talkToBurntmeat", t.player.talk_to, "eadgar_troll_chief_cook", 1)
        t.exec("talkToBurntmeat-dialog", t.chat.play, {
            "npc:Oh, it you again.",
            "player:I probably won't feel happy",
            "npc:Well, Burntmeat need big important job",
            "choose:Yes.",
            "player:What do you want now?",
            "npc:You remember you come in here",
            "player:Yes, you told me trolls had picked",
            "npc:Yah. But Burntmeat is hearing",
            "player:Oh? How did you hear that?",
            "npc:Well, I was cooking dis adventurer",
            "npc:You like my armour?",
            "npc:Nah, silly red metal",
            "npc:Huh? What dis?",
            "npc:You can have that too!",
            "npc:Only place find goutweed",
            "npc:No! I-I- I grew that myself!",
            "npc:What say? Goutweed not grow now.",
            "npc:No, no; I grew it.",
            "npc:Humans grow goutweed?",
            "npc:You can get the gout tuber from Tai Bwo Wannai",
            "npc:Mmmm. Burntmeat think that sound good.",
            "npc:Oh b...",
            "player:You killed that poor chap!",
            "npc:Well, he give me mighty sore tummy",
            "player:Anyway, why did you tell me",
            "npc:Ah! Der man I cooked",
            "player:You want to become a farmer?",
            "npc:No, not me. Burntmeat stick to",
            "player:Your assistant's called WHAT?",
            "npc:He called My Arm.",
            "player:My...?",
            "npc:My Arm.",
            "npc:Yep, My Arm.",
            "player:But why is he called...",
            "npc:It a perfectly good troll name.",
            "player:Alright, enough!",
            "npc:You gonna help My Arm grow goutweed.",
            "player:Look, you're a vicious monster",
            "npc:if My Arm learns to grow goutweed",
            "player:Never?",
            "npc:Never. Burntmeat promises.",
            "choose:Alright, I'll lend him a hand.",
            "player:Alright, I'll lend him a hand.",
        })
        stage("talk_to_myarm")

        ------------------------------------------------------------ 40: My Arm in the kitchen
        t.exec("talkToMyArm", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArm-dialog", t.chat.play, {
            "player:Before I start helping you",
            "npc:Dat's easy!",
            "player:Okay, so what did you try to eat?",
            "npc:It were my dad's arm.",
            "player:And no one thinks it's a silly name?",
            "npc:Well, I seen worse.",
            "player:Okay, okay, I'm sorry!",
            "npc:If you happy 'bout my name now",
            "player:Alright, My Arm",
            "npc:Okieday. Now, I heard 'bout dese gouty",
            "npc:My Arm is thinking this is da thingy",
            "player:So what do we do about it?",
            "npc:You go to Death Plateau and search",
            "player:Why do I need to go there?",
            "npc:Never mind tubers.",
            "player:Hmph.",
            "npc:Go see Burntmeat",
        })
        stage("lump_needed")

        ------------------------------------------------------------ 60: the goutweedy lump
        -- Off the stronghold: Camelot Teleport (magic_spells.dbrow [magic_spell_teleport_camelot]: 5 air +
        -- 1 law, landing 2757,3478,0), then overland to the pot on Death Plateau (reach.py 2757,3478 ->
        -- 2864,3593 REACH closed-doors 706).
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "teleportToDeathPlateau",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        t.exec("goto-useBucketOnPot", t.player.goto_tile, 2864, 3593, 0)
        local pot = t.player.by_symbol("loc", "death_troll_cauldron")
        t.exec("useBucketOnPot", t.player.use_on, "bucket_empty", pot)
        t.check("useBucketOnPot.lump", t.inv.await("myarm_lump", 1, 10) == "ok",
            "maba_travel.rs2 [label,maba_search_pot]: '... And find something lumpy.' adds the goutweedy lump")
        inv_row("useBucketOnPot.bucketKept", "bucket_empty", 1, "the bucket is not consumed (Quest Helper isNotConsumed)")
        trip_margin("deathPlateau.margin", "Camelot to the Death Plateau pot")

        walk_up("enterStrongholdWithLump", false)
        down_to_kitchen("goDownToArmWithLump")
        trip_margin("up2.margin", "Tenzing up through the stronghold to the kitchen with the lump")
        t.exec("talkToArmWithLump", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToArmWithLump-dialog", t.chat.play, {
            "npc:You got da goutweedy lump yet?",
            "player:Yes, I have it. Here, take the thing.",
            "npc:Huh. It looking a bit tatty...",
            "player:So are you.",
            "npc:... but it smelling like goutweed.",
            "mesbox:My Arm has eaten the goutweedy lump.",
            "player:You ate it!",
            "npc:Yep, it tasting like goutweed too.",
            "player:So what are you going to do now?",
            "npc:Well, now My Arm know dese goutweedy",
            "player:Alright. Where do you need me to start?",
            "npc:My Arm done tried farming before.",
            "player:What did you plant?",
            "npc:I bin planting everythin'",
            "player:And none of those things grew?",
            "npc:Nope, not even a smidgen.",
            "player:So did you try getting help",
            "npc:Yah, My Arm found a nice farmer",
            "player:You're not very clever, are you?",
            "npc:My ol' mum say I clever enough.",
            "player:Oh, nothing. Maybe I should see",
            "npc:Sure t'ing",
            "mesbox:My Arm leads you through the Troll Stronghold",
        })
        stage("on_roof")
        inv_row("talkToArmWithLump.eaten", "myarm_lump", 0, "My Arm ate it")

        ------------------------------------------------------------ 80: the roof
        up_from_kitchen("goUpFromF1ToMyArm")
        up_to_roof("goUpToMyArm")
        t.exec("talkToMyArmUpstairs", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmUpstairs-dialog", t.chat.play, {
            "npc:Dis is my quiet place.",
            "player:It's a bit windy up here.",
            "npc:Dat is good, cos you smellin' distinctly whiffy.",
            "player:I wash more than enough!",
            "npc:Okieday, maybe My Arm imagining da smell.",
            "player:So you think you could grow things here?",
            "npc:I t'ink so, yah. I got book",
            "player:Where in the world did YOU get a book?",
            "npc:Dat farmer I ate had a book with him.",
            "player:Farmer Gricoller's Farming Manual?",
            "npc:My Arm not know what in book",
            "player:Alright, I'll have a look inside.",
        })
        stage("manual_given")
        inv_row("talkToMyArmUpstairs.manual", "myarm_book", 1, "My Arm hands over Farmer Gricoller's Farming Manual")

        t.exec("readBook", t.player.inv_op, "myarm_book", 1)
        t.exec("readBook.open", t.ui.await_open, "book", 10)
        t.exec("readBook.mountain", t.ui.expect_text, "book:title", "Farmer Gricoller's Farming Manual")
        t.key("escape")
        t.ticks(2)
        stage("manual_read")

        t.exec("talkToMyArmAfterReading", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAfterReading-dialog", t.chat.play, {
            "npc:You know how to read book I got off farmer?",
            "player:Of course; I've been reading it.",
            "npc:Dat's good. Is it any use?",
            "player:There's a useful section about preparing soil",
            "npc:Urgh. Dat's a lotta camel dung.",
            "player:Fortunately, I've got enough dung with me.",
            "npc:My Arm not like to say so earlier",
            "player:Thanks a lot!",
        })
        stage("treat_patch")

        -- AddDung / AddCompost: one bucket per use on My Arm's soil patch (2830,3695), spade carried.
        local patch = t.player.by_symbol("loc", "myarm_fakefarmingpatch")
        for i = 1, 3 do
            t.exec("useUgthankiDung-" .. i, t.player.use_on, "feud_camel_pooh_bucket", patch)
            if i == 3 then
                t.exec("useUgthankiDung-" .. i .. "-dialog", t.chat.play, { "player:Phew - that's enough dung." })
            end
            t.ticks(1)
        end
        var_row("useUgthankiDung.count", "varb2791_myarm_dung", 3, "three buckets dug in (Quest Helper AddDung)")
        for i = 1, 7 do
            t.exec("useCompost-" .. i, t.player.use_on, "bucket_supercompost", patch)
            if i == 7 then
                t.exec("useCompost-" .. i .. "-dialog", t.chat.play, { "player:Great, that's enough supercompost." })
            end
            t.ticks(1)
        end
        var_row("useCompost.count", "varb2792_myarm_supercompost", 7, "seven buckets dug in (Quest Helper AddCompost)")
        var_row("useCompost.patch", "varb2799_myarm_fakepatch", 1, "treated soil, myarm_fakepatch_soil2 (multiloc 1)")
        stage("patch_treated")

        t.exec("talkToMyArmAfterFertilising", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAfterFertilising-dialog", t.chat.play, {
            "player:I've finished treating that soil patch for you.",
            "npc:Good, good. Always I bin wanting",
            "player:So what do we need to do now?",
            "npc:Now we do like dat man say to Burntmeat.",
            "player:It's a long way to Tai Bwo Wannai Village.",
        })
        t.ticks(3)
        stage("at_ardougne")
        do
            local r, tt = t.world.tile()
            t.check("talkToMyArmAfterFertilising.ardougne", r == "ok" and tt.level == 0 and math.abs(tt.x - 2682) <= 1 and math.abs(tt.z - 3275) <= 1,
                "the fade to the Ardougne docks (p_telejump ^maba_ardougne_dock_coord 2682,3275,0) -> " .. tile_text(r, tt))
        end
        trip_margin("roof1.margin", "the kitchen up to the roof and the patch")

        ------------------------------------------------------------ 150: Karamja
        t.exec("talkToBarnaby", t.player.talk_to, "myarm_barnaby_ship", 1)
        t.exec("talkToBarnaby-dialog", t.chat.play, {
            "npc:What in the world is that THING?",
            "choose:This is My Arm. We'd like to go to Karamja.",
            "player:This is My Arm. We'd like to go to Karamja.",
            "npc:This is your what?",
            "player:He's a troll. His name is My Arm.",
            "npc:Yup, My Arm.",
            "npc:Whose arm?",
            "player:Never mind that now",
            "npc:It was my dad's arm.",
            "npc:What about your dad's arm?",
            "npc:My Arm tried to eat it.",
            "npc:Your arm did what?",
            "player:Will you take us to Karamja?",
            "npc:I don't think I want the mad troll.",
            "npc:Or his arm.",
            "player:Either you can take us to Karamja",
            "npc:Oh no, not your arm too...",
            "player:So you'll take us?",
            "npc:Alright, just get the arm thing onto the ship.",
            "mesbox:My Arm is thinking...",
            "npc:Urrrgh!",
            "player:What?",
            "npc:My Arm not liking dis.",
            "player:What's wrong with your ar...",
            "npc:You ever hear stories of troll sailors",
            "player:Um... can't think of any just now.",
            "npc:Dat 'cos trolls don't like da sea.",
            "player:Aren't there... river trolls",
            "npc:Them not proper trolls.",
            "player:But they look just like land trolls!",
            "npc:We trolls. Them not trolls.",
            "player:Oh, please yourself!",
            "mesbox:My Arm is thinking...",
            "npc:Are we dere yet?",
            "player:No! Look - no land.",
            "mesbox:My Arm is thinking...",
            "npc:How 'bout now?",
            "player:Still. No. Land.",
            "mesbox:My Arm is thinking...",
            "npc:You gettin' angry with My Arm, huh?",
            "player:No, no.",
            "npc:Is it 'cos I is thick?",
            "player:I'm not angry with you.",
            "npc:Oh, right.",
            "mesbox:My Arm is thinking...",
            "npc:Are we dere now?",
        })
        t.ticks(3)
        stage("at_brimhaven")
        do
            local r, tt = t.world.tile()
            t.check("talkToBarnaby.brimhaven", r == "ok" and tt.level == 0 and math.abs(tt.x - 2772) <= 1 and math.abs(tt.z - 3224) <= 1,
                "off the ship on the Brimhaven dock (p_telejump ^maba_brimhaven_dock_coord 2772,3224,0) -> " .. tile_text(r, tt))
        end

        t.exec("talkAfterBoat", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkAfterBoat-dialog", t.chat.play, {
            "npc:Urrrgh! My Arm feeling sick...",
            "npc:We will meet at dat village, yah?",
            "player:What?",
            "player:Oh, he's gone.",
        })
        stage("at_tai")

        -- Brimhaven dock -> east of the Tai Bwo Wannai general store (reach.py 2772,3224 -> 2781,3123 REACH 138).
        t.exec("goto-talkToMyArmAtTai", t.player.goto_tile, 2781, 3123, 0)
        t.exec("talkToMyArmAtTai", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAtTai-dialog", t.chat.play, {
            "npc:So dis is Tai Bwo Wannai Village, huh?",
            "player:Yes, that's right.",
            "npc:You doin' a lot of favours for people.",
            "player:I usually get something.",
            "npc:You not like Burntmeat's cooking?",
            "player:Believe it or not",
            "npc:Dat a pity.",
            "player:Murcaily, right.",
        })
        stage("see_murcaily")

        -- -> Murcaily, east Tai Bwo Wannai (reach.py 2781,3123 -> 2815,3083 REACH 74).
        t.exec("goto-talkToMurcaily", t.player.goto_tile, 2815, 3083, 0)
        t.exec("talkToMurcaily", t.player.talk_to, "tbwcu_murcaily", 1)
        t.exec("talkToMurcaily-dialog", t.chat.play, {
            "npc:Is there anything I can do for you, Bwana?",
            "choose:A troll called My Arm wants a favour...",
            "player:A troll called My Arm wants a favour from you.",
            "npc:What's a troll?",
            "player:I really don't want to discuss it.",
            "npc:Goutweed? People grow it in farming patches",
            "player:No, the silly troll won't listen.",
            "npc:You want to grow it in the mountains, Bwana?",
            "player:You mean we aren't going to be able",
            "npc:No, not with a normal gout tuber.",
            "player:Oh, wonderful.",
            "npc:Oh no, Bwana. Those are far too valuable",
            "player:You owe me a bit of favour",
            "npc:Oh, I suppose I could let you have one.",
            "player:So this hardy tuber will grow",
            "npc:It grows much faster than any other",
            "*",
            "npc:My Arm bored...",
            "mesbox:Da Rumble in da Jungle!",
            "npc:What is this noise?",
            "npc:Where's da goutweed?",
            "npc:Agh - a troll!",
            "npc:Want goutweed!",
            "npc:Eek!",
            "npc:Hey!",
            "npc:What are you doing?",
            "npc:My Arm want goutweed!",
            "npc:Be careful!",
            "npc:A broodoo man!",
            "npc:Has you got goutweed?",
            "npc:Ow!",
            "npc:Grrr!",
            "npc:Still want goutweed!",
            "npc:Jagdakobo - here is the monster!",
            "npc:Prepare to die!",
            "npc:Where you keep goutweed?",
            "npc:Ow!",
            "npc:You got issues!",
            "npc:Humans all mental.",
            "npc:Make it go away!",
            "npc:I'll give you anything!",
            "npc:My Arm want goutweed!",
            "npc:Okay, here you go.",
            "*",
        })
        stage("got_tubers")
        inv_row("talkToMurcaily.tubers", "myarm_hardytubers", 1, "Murcaily's hardy gout tubers")
        var_row("talkToMurcaily.favour", "varb907_favour_percentage", 0, "the walkthrough: Murcaily takes the 60% favour the tubers were given for")

        t.exec("goto-talkToMyArmAfterMurcaily", t.player.goto_tile, 2781, 3123, 0)
        t.exec("talkToMyArmAfterMurcaily", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAfterMurcaily-dialog", t.chat.play, {
            "player:What the heck was all THAT about?",
            "npc:You take far too long chattin'",
            "player:You could have really damaged their village!",
            "npc:Silly humans got such flimsy homes.",
            "player:If you ever come to",
            "npc:Anyway, you got da goutweedy stuff, right?",
            "player:Yes, I've got plenty of hardy gout tubers.",
            "npc:Right. Let's get back to da ship",
            "player:What do you think I need from the bank?",
            "npc:My Arm done tried planting stuff before",
            "player:The birds got interested",
            "npc:Yuh, an' dere was a great BIG one",
            "player:Exactly how BIG was this bird?",
            "npc:It pretty BIG.",
            "player:Are we talking bigger than... a pigeon?",
            "npc:How 'bout we just go back to Ardougne",
            "mesbox:My Arm is thinking...",
            "npc:Dat captain didn't look very happy",
            "player:No, he wasn't.",
            "mesbox:My Arm is thinking...",
            "npc:What was dat you said to him?",
            "player:I told him he'd better take us back",
            "npc:Or else what?",
            "player:Or else I'd let my arm loose on him.",
            "mesbox:My Arm is thinking...",
            "npc:So... are we dere yet?",
            "mesbox:Ardougne has never seemed so far away...",
            "npc:You probably wanna go to bank now",
            "player:How are you going to get back",
            "npc:Dat not your problem.",
            "player:Alright, I'll see you there.",
        })
        stage("back_at_ardougne")
        do
            local r, tt = t.world.tile()
            t.check("talkToMyArmAfterMurcaily.ardougne", r == "ok" and tt.level == 0 and math.abs(tt.x - 2682) <= 1 and math.abs(tt.z - 3275) <= 1,
                "the ship back to the Ardougne docks (p_telejump 2682,3275,0) -> " .. tile_text(r, tt))
        end

        ------------------------------------------------------------ 220: back to the roof
        -- Ardougne docks -> Tenzing's lane overland (reach.py 2682,3275 -> 2826,3555 REACH closed-doors 880).
        walk_up("enterStrongholdForFight", false)
        walk("walk-goUpToRoofForFight", 2836, 10090, 2, 60, 6)
        up_to_roof("goUpToRoofForFight")
        trip_margin("up3.margin", "Ardougne, Tenzing and the stronghold up to the roof")

        t.exec("talkToMyArmForFight", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmForFight-dialog", t.chat.play, {
            "npc:Ah, dulce domum.",
            "player:What did you say?",
            "npc:It mean somefing like 'Home sweet home'",
            "player:Riiight. Shall we get on with growing",
            "npc:Yup, dat sound good.",
            "player:Ok, I'll hand you what you need",
        })
        stage("growing")
        var_row("talkToMyArmForFight.weeds", "varb2799_myarm_fakepatch", 2, "the patch has grown over: myarm_fakepatch_weeds3 (multiloc 2)")

        ------------------------------------------------------------ 240: planting
        local arm = t.player.by_symbol("npc", "myarm_fixed")
        t.exec("giveRake", t.player.use_on, "rake", arm)
        t.ticks(5)
        -- "this may break" (wiki): the rake's head can fly off once (myarm_rakejoke). The outcome is
        -- read from the world (the handle in the pack), never from a hidden roll.
        local hr, handles = t.inv.count("rake_handle")
        if hr == "ok" and handles == 1 then
            t.exec("pickUpRakeHead", t.player.click_obj, "rake_head", 3)
            inv_row("pickUpRakeHead.held", "rake_head", 1, "the rake head picked up off the roof")
            t.exec("repairRake", t.player.use_item_on_item, "rake_head", "rake_handle")
            t.check("repairRake.rake", t.inv.await("rake", 1, 5) == "ok", "maba_travel.rs2 [label,maba_rake_repair]: 'You reattach the rake head to the handle.'")
            t.exec("giveRake-again", t.player.use_on, "rake", arm)
            t.ticks(5)
        else
            t.note("giveRake: the rake held together (rake_handle " .. tostring(handles) .. ")")
        end
        var_row("giveRake.raked", "varb2799_myarm_fakepatch", 6, "My Arm raked the weeds: myarm_fakepatch_empty (multiloc 6, Quest Helper usedRake)")

        t.exec("giveSupercompost", t.player.use_on, "bucket_supercompost", arm)
        t.ticks(2)
        var_row("giveSupercompost.patch", "varb2799_myarm_fakepatch", 7, "composted (Quest Helper givenCompost = 7)")

        t.exec("giveHardyGout", t.player.use_on, "myarm_hardytubers", arm)
        t.exec("giveHardyGout-dialog", t.chat.play, { "npc:T'anks," })
        var_row("giveHardyGout.tubers", "varb2794_myarm_tubers", 1, "Quest Helper givenHardy")

        t.exec("giveDibber", t.player.use_on, "dibber", arm)
        t.exec("giveDibber-dialog", t.chat.play, {
            "npc:'Ello, matey!",
            "npc:You a dwarf.",
            "npc:You not my matey!",
            "npc:Aaaargh - matey!",
            "npc:Ooh. Food for me?",
            "player:Aww, poor dwarfie.",
        })
        t.ticks(6)
        stage("grown")
        var_row("giveDibber.grown", "varb2799_myarm_fakepatch", 14, "the hardy goutweed fully grown (multiloc 14)")

        ------------------------------------------------------------ 250: the Baby Roc
        t.exec("talkToMyArmAfterGrow", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAfterGrow-dialog", t.chat.play, {
            "npc:Uh-oh - I fink da birds noticed us farming...",
            "npc:Akka akka!",
            "npc:Save da goutweed!",
            "npc:Heheh - ickle birdie!",
            "npc:Akka!",
            "player:You're worried about that?",
            "npc:It gonna eat da goutweed - kill it, quick!",
        })
        t.exec("killBabyRoc.present", t.npc.await_present, "myarm_baby_roc", 15, 10)
        t.exec("killBabyRoc", t.player.attack, "myarm_baby_roc", 2, 20)
        local baby_r, baby_d = t.npc.await_dead_engaged(300, 30, { eat = { item = "shark", below = EAT_BELOW } })
        t.step("killBabyRoc.dead", baby_r == "ok" and "PASS" or "FAIL", tostring(baby_r) .. " " .. tostring(baby_d))
        t.ticks(4)
        stage("baby_dead")

        ------------------------------------------------------------ 260: the Giant Roc
        -- Protect from Missiles (the guide's prayer): the prayer tab, then prayerbook:prayer14.
        t.ui.tab("prayer")
        t.ticks(1)
        local _, missiles = t.ui.widget("prayerbook:prayer14")
        t.ui.invoke(missiles, 1)
        t.ticks(2)
        var_row("killGiantRoc.protectFromMissiles", "varb4117_prayer_protectfrommissiles", 1, "Protect from Missiles on")
        t.ui.tab("inventory")
        t.ticks(1)
        t.exec("talkToMyArmAfterBaby", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAfterBaby-dialog", t.chat.play, {
            "npc:T'anks for saving da goutweed",
            "player:What other birdie?",
            "npc:Dat birdie behind you.",
        })
        t.exec("killGiantRoc.present", t.npc.await_present, "myarm_giant_roc", 20, 10)
        t.exec("killGiantRoc", t.player.attack, "myarm_giant_roc", 2, 20)
        local giant_r, giant_d = t.npc.await_dead_engaged(900, 90, { eat = { item = "shark", below = EAT_BELOW } })
        t.step("killGiantRoc.dead", giant_r == "ok" and "PASS" or "FAIL", tostring(giant_r) .. " " .. tostring(giant_d))
        do
            local low = tonumber(string.match(tostring(giant_d), "lowest hp (%d+)/") or "")
            local fr, food = t.inv.count("shark")
            t.check("killGiantRoc.margin", low ~= nil and low * 4 >= MAX_HP and fr == "ok" and food >= 1,
                "Giant Roc (250 hp, max 14 melee / 20 ranged) in rune with a dragon scimitar under Protect from Missiles: lowest hp "
                    .. tostring(low) .. "/" .. MAX_HP .. ", sharks left " .. tostring(food) .. " (" .. tostring(fr) .. ")"
                    .. " (margin: lowest hp >= a quarter of max AND food left)")
        end
        t.ticks(4)
        stage("giant_dead")

        ------------------------------------------------------------ 270: harvest
        t.exec("giveSpade", t.player.use_on, "spade", arm)
        t.ticks(3)
        stage("harvested")
        t.exec("talkToMyArmAfterHarvest", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAfterHarvest-dialog", t.chat.play, {
            "npc:T'ank you so much for all da help!",
            "player:I'm just glad the goutweed grew up okay.",
            "npc:My Arm just check dis da right stuff...",
            "npc:Yup, dis is goutweed. It lovverley.",
            "player:Don't eat it all!",
            "npc:Aww, too late.",
            "player:...",
            "npc:You gotta go back to kitchen",
            "player:Thanks, I'll remember that.",
        })
        stage("see_burntmeat")
        inv_row("talkToMyArmAfterHarvest.rake", "rake", 1, "My Arm returns the tools he was lent")

        ------------------------------------------------------------ 290: Burntmeat's reward
        down_from_roof("goDownFromMyArmToBurntmeat")
        down_to_kitchen("goDownToBurntmeat")
        t.exec("talkToBurntmeatAgain", t.player.talk_to, "eadgar_troll_chief_cook", 1)
        t.exec("talkToBurntmeatAgain-dialog", t.chat.play, {
            "player:We've done it!",
            "npc:So now we can grow goutweed whenever we want it?",
            "player:Yes, he's got plenty of hardy gout tubers",
            "npc:T'ank you so much!",
            "player:So... you mentioned a reward?",
            "npc:Oh yup, Burntmeat cooked you SPECIAL reward.",
            "npc:LOTS of burnt meat!",
            "player:...",
            "npc:See, Burntmeat promised you a reward!",
        })
        stage("burntmeat_rewarded")
        do
            local r, n = t.inv.count("burnt_meat")
            t.check("talkToBurntmeatAgain.burntMeat", r == "ok" and n ~= nil and n >= 1,
                "burnt meat " .. tostring(n) .. " (" .. tostring(r) .. ") -- 'receives=full inventory of burnt meat'")
        end

        ------------------------------------------------------------ 310: My Arm's better reward
        up_from_kitchen("goUpFromBurntmeatFinish")
        up_to_roof("goUpToMyArmFinish")
        trip_margin("roof2.margin", "the kitchen back up to the roof")
        local xp_result, xp_snapshot = t.skill.snapshot()
        t.step("talkToMyArmFinish.xp_before", xp_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(xp_result))
        t.exec("talkToMyArmFinish", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmFinish-dialog", t.chat.play, {
            "npc:Did Burntmeat give you nice reward?",
            "player:No, he jolly well didn't.",
            "npc:Aww, dat a shame.",
            "player:You said you might be able to give me",
            "npc:Yup, an' I will.",
            "mesbox:My Arm tells you a secret about herbs.",
            "npc:Now you know more 'bout herbs.",
        })
        t.ticks(4)
        t.quest.expect_complete()
        t.check("reward.herblore", t.skill.expect_gain("herblore", 10000, xp_snapshot), "10,000 Herblore XP")
        t.check("reward.farming", t.skill.expect_gain("farming", 5000, xp_snapshot), "5,000 Farming XP")
        t.finish(0)
    end,
}
