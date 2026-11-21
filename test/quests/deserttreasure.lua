-- Desert Treasure I. Stage values: OSRS-Content/osrs-content/server/scripts/quests/quest_deserttreasure/configs/deserttreasure.constant
-- Setup stages the prerequisites (quests, levels) and the brought-along kit; every diamond is obtained in play.
--
-- Door rule (fix_b61 re-drive, docs/QUEST_ORCHESTRATOR.md standing rules, owner 2026-10-03): no goto_tile
-- departs from or lands in a closed space; every door, gate, ladder, trapdoor, well, gangplank and barrier
-- between the player and the target is pressed, going in and coming out. Checked against the map with
-- test/quests/orchestrator/matthew-mbp-m4/reports/sample_tools/{reach,comp,locs_near}.py (doors closed):
--   * The Kharidian Desert is entered on foot ONLY through the Shantay Pass (reach.py 3304,3123 ->
--     3304,3108 UNREACHABLE at margins 30/80/160): every visit buys a pass from Shantay and presses
--     shantay_pass_henge_doorway (shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway] -> [queue,
--     shantay_pass_enter], 3304,3115). Every way OUT of the desert (and out of every dungeon) is a REAL
--     teleport cast from the spellbook (t.player.teleport_cast: TELEPORTED, exact runes, landing).
--   * The Exam Centre (Terry Balando) sits behind the Varrock members' gate fai_varrock_member_gatel/r
--     (3312,3331-3332, west edge; reach.py from Lumbridge NEEDS-DOOR at margins 60/160, from Varrock
--     NEEDS-DOOR via vm_fencegate at 80/250) and its own door qip_digsite_poshdoor 3352,3337 (north edge;
--     the room x 3348-3367 z 3332-3348, maps/m52_52.jl2). Both pressed both ways.
--   * Eblis's house in the Bandit Camp: desertdoorclosed 3182,2984 (east edge; room x 3183-3188
--     z 2980-2987, maps/m49_46.jl2), pressed in and out.
--   * Morytania: the Paterdomus route Priest in Peril opens -- the Varrock members' gate 3319,3467-3468
--     (east edge; reach.py Varrock -> 3405,3506 NEEDS-DOOR at 30/80/160), the temple trapdoor 3405,3507,
--     pip_underground_door1 3405,9895 and door2 3431,9897 (gates.rs2), Drezel's advice (mausoleum_drezel.rs2
--     [label,drezel_access_holy_barrier], 60 -> 61; the wolfbane dagger is counted in the bank by
--     ~obj_gettotal, inv_procs.rs2:221) and the holy barrier (mausoleum_interactions.rs2:26, out at 3423,3485).
--   * Draynor sewer: vampire_trap1 opened, vampire_trap2 climbed (deserttreasure.rs2:557-564, lands
--     3118,9644), vampire_ladder back up.
--   * Entrana: the Port Sarim monk searches for weapons and armour (port_sarim/scripts/monk_of_entrana.rs2
--     ~has_entrana_restricted_items, seam b59-seam1): the rune kit is banked at Draynor (open doorway,
--     bankdoor_*_inactive) before the boat and taken out after. The deck, the gangplanks and shipmonk2's
--     boat back are all pressed.
--   * The troll child (2830,3740) is in the Trollheim summit's walking component (comp.py from 2835,3738
--     reaches 2840,3690), a pocket on foot (sampler b58 round 2, eadgar.lua): it is walked up the whole way
--     -- Tenzing's gate and doors, the stile, both rock pairs, the secret door, the stronghold's stairs and
--     prison door, the top exit. Trollheim Teleport is not used: in OSRS it needs Eadgar's Ruse, which
--     this account has not done (teleport.rs2:25 does not check it; using it would ride that gap).
--   * The smoke well, the shadow ladder, the ice gate, the troll cave, the ice ledge, the path gate and
--     the pyramid ladders are pressed; the gotos left are overland hops between open tiles, or hops
--     inside one dungeon passage.
return {
    id = "deserttreasure",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000, -- four desert visits, two Morytania trips, Entrana and the Trollheim climb, all walked
    setup = {
        "::clearinv",
        "::give coins 1000", -- Shantay passes (5 each, shantay.rs2), the Bandit Camp beer, the boat is free
        -- Thieving 99, not 53: each of the chest's three locks is stat_random(thieving, 52, 128) (deserttreasure.rs2:1565-1581,
        -- deserttreasure.constant:56-57; value > random(256), torirs_server_scripts.c SS_OP_STAT_RANDOM): 36% a lock at 53
        -- (4.6% an attempt) and 50% at 99 (12.8%). The player's random stream is seeded from its name (torirs_server_save.c:268).
        "::setlevel thieving 99",
        -- Magic 99: Water Blast for Fareed, Damis and Dessous, Fire Blast for Kamil and the ice blocks, and the
        -- Lumbridge/Varrock/Camelot teleports; the Ice Path's cold drains Magic a level per ten ticks
        -- (dt_ice.rs2 [timer,dt_ice_cold]: every skill but Hitpoints, 4628) and Fire Blast needs 59.
        "::setlevel magic 99",
        "::setlevel firemaking 50",
        "::setlevel slayer 10",
        -- Prayer 99 for Protect from Melee at Damis and Kamil (prayer potions are on Quest Helper's list,
        -- DesertTreasure.java:302/:607). Since the eat-delay port (raid branch, OSRS-Content 7936c59bf9) an eat no longer
        -- holds his hits: unprayed, the true form killed the character in 2 of 2 runs (hp_dt_3, hp_dt_4).
        "::setlevel prayer 99",
        -- Melee for Fareed (with ice gloves), the ice trolls and the guide's "water spells or melee gear"
        -- (Quest Helper's combat requirement): the same 99s the committed test staged at leg 2.
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        -- Agility 99: troll_climbingrocks on the way up to Trollheim needs 15 and rolls stat_random(agility, ...) to
        -- cross without a fall (quest_troll.rs2 @rockslide_obstacle); Troll Stronghold (a prerequisite) already
        -- implies the 15. Same staging as eadgar.lua.
        "::setlevel agility 99",
        "::complete quest_digsite",
        "::complete quest_templeofikov",
        "::complete quest_touristtrap",
        "::complete quest_trollstronghold",
        -- Death Plateau is Troll Stronghold's own prerequisite; Tenzing's doors and the climbing boots read it
        -- (death_doors_mechanism.rs2:31-56, death_locs.rs2 [opheld2,death_climbingboots]).
        "::complete quest_deathplateau",
        "::complete quest_priestinperil",
        "::complete quest_waterfall",
        "::complete quest_plaguecity",
        -- Runes: Water Blast (3 water, 3 air, 1 death), Fire Blast (5 fire, 4 air, 1 death), Lumbridge Teleport
        -- (3 air, 1 earth, 1 law), Varrock Teleport (1 fire, 3 air, 1 law), Camelot Teleport (5 air, 1 law)
        -- (magic_spells.dbrow).
        "::give airrune 900",
        "::give waterrune 400",
        "::give deathrune 250",
        "::give firerune 400",
        "::give lawrune 15",
        "::give earthrune 8",
        -- Eblis's scrying ingredients, brought along; noted, which he takes ("Items can be noted",
        -- deserttreasure.rs2:385 [proc,dt_eblis_take]).
        "::give cert_magic_logs 12",
        "::give cert_steel_bar 6",
        "::give cert_molten_glass 6",
        "::give bones 1",
        "::give ashes 1",
        "::give charcoal 1",
        "::give bloodrune 1",
        -- The smoke dungeon kit (Quest Helper: tinderbox, face covering, ice gloves, water spells or melee gear).
        "::give tinderbox 1",
        "::give gasmask 1",
        "::give ice_gloves 1",
        "::give rune_scimitar 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give water_skin4 1", -- the desert heat ([timer,desert_heat], desert_heat.rs2:53) drinks from it
        "::give shark 10",
        -- The rest of the brought-along kit waits in the bank and is withdrawn at Al Kharid, Seers' Village
        -- and Draynor on the way: lockpicks for the Bandit Camp chest (one snaps on every miss,
        -- deserttreasure.rs2:1548), food, the silver bar for Ruantun, garlic powder, spice and a cake,
        -- climbing and spiked boots, the whip and restore potions for the Ice Path, more waterskins.
        "::bankgive lockpick 60",
        "::bankgive shark 90",
        "::bankgive silver_bar 1",
        "::bankgive fd_crushed_garlic 1",
        "::bankgive spicespot 1",
        "::bankgive cake 1",
        "::bankgive death_climbingboots 1",
        "::bankgive death_spikedboots 1",
        "::bankgive abyssal_whip 1",
        "::bankgive 4dose2restore 4",
        "::bankgive water_skin4 3",
        -- Priest in Peril's reward (::complete grants no items): Drezel's barrier advice needs it owned, and
        -- ~obj_gettotal counts the bank (mausoleum_drezel.rs2:29-34, inv_procs.rs2:221). Banked: it is a
        -- weapon the Entrana monk would refuse.
        "::bankgive dagger_wolfbane 1",
        "::bankgive 4doseprayerrestore 2", -- Quest Helper lists prayer potions (DesertTreasure.java:302/:607); prayer does not regenerate
    },
    bind = {
        varp = "varb358_deserttreasure",
        constants = {
            not_started = 0, etchings = 1, translating = 2, have_translation = 3,
            read_notes = 4, bandit_camp = 5, heard_diamonds = 6, gather_mirrors = 7,
            mirrors_ready = 10, pyramid = 13, complete = 15,
            -- The recorded stages 1..11 (RSProx 4621/4622; deserttreasure.constant ^dt_s_*); the names above keep the old values.
            s_started = 1, s_terry_has = 2, s_translating = 3, s_translation = 4, s_smith_read = 5, s_split = 6,
            s_beer = 7, s_elders = 8, s_list = 9, s_mirrors = 10, s_hunting = 11,
        },
        row = "quest_deserttreasure",
        display = "Desert Treasure I",
        points = 3,
    },
    legs = {
        {
            name = "bandit_camp",
            run = function(t)
        -- LEG 1 BEGIN: talkToArchaeologist
        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function landed(name, ok_fn, want, ticks)
            t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and ok_fn(tt) end, note = name }, ticks or 10)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and ok_fn(tt), "tile " .. reading() .. " (want " .. want .. ")")
        end
        -- The Shantay Pass, the only way into the desert on foot: buy a pass, press the doorway from the north.
        local function shantay(pfx, first)
            t.exec("goto-" .. pfx .. ".shantay", t.player.goto_tile, 3304, 3123, 0)
            local coins0, pass0 = count("coins"), count("shantay_pass")
            -- Buy-pass is Shantay's own op (4621 [504], 4629 [3903]): the item box, five coins out, the pass in.
            t.exec(pfx .. ".buyPass", t.player.talk_to, "shantay", 4)
            t.exec(pfx .. ".buyPass-dialog", t.chat.play, { "objbox:You purchase a Shantay Pass." })
            t.check(pfx .. ".buyPass-paid", count("shantay_pass") == pass0 + 1 and count("coins") == coins0 - 5,
                "shantay_pass " .. pass0 .. " -> " .. count("shantay_pass") .. ", coins " .. coins0 .. " -> " .. count("coins") .. " (5 coins, shantay.rs2)")
            -- North of the doorway (z 3116): the desert warning (cws_warning_10) opens, and its "warn1" takes the
            -- pass and walks the player through (shantay_pass.rs2, as desertrescue.lua passes it).
            t.exec("walk-" .. pfx .. ".toDoorway", t.player.walk_route, { { 3304, 3118 } })
            t.exec(pfx .. ".doorway", t.player.click_loc, "shantay_pass_henge_doorway", 1)
            local wo = t.ui.await_open("cws_warning_10", 15)
            local wr, ww = t.ui.widget("cws_warning_10:warn1")
            t.check(pfx .. ".doorway-dialog", wo == "ok" and wr == "ok" and select(1, t.ui.invoke(ww, 1)) == "ok",
                "desert warning " .. tostring(wo) .. ", warn1 " .. tostring(wr))
            t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and tt.z < 3116 end, note = pfx .. ".doorway" }, 10)
            local r, tt = t.world.tile()
            t.check(pfx .. ".doorway-landed", r == "ok" and tt.level == 0 and tt.x == 3304 and tt.z < 3116 and count("shantay_pass") == pass0,
                "after the doorway: " .. reading() .. " (the queued gate teleport lands 3304,3115), shantay_pass "
                    .. count("shantay_pass") .. " (handed over)")
        end
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        -- The brought-along melee kit, worn from the start (Quest Helper: water spells or melee gear).
        for _, w in ipairs({ "gasmask", "ice_gloves", "rune_chainbody", "rune_platelegs", "rune_kiteshield", "rune_scimitar" }) do
            t.exec("wear-" .. w, t.player.equip, w)
        end

        -- Leg 1 ------------------------------------------------------------
        shantay("talkToArchaeologist", true)
        t.exec("goto-talkToArchaeologist", t.player.goto_tile, 3177, 3043, 0)
        t.exec("talkToArchaeologist", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
        t.exec("talkToArchaeologist-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:Howdy stranger.",
            "choose:Do you have any quests?",
            "player:Do you have any quests?",
            "npc:Well, it's funny",
            "*",
            "npc:It's very old",
            "choose:Yes.",
            "player:Sure, I was heading",
            "npc:His name's Terry",
            "npc:a preliminary translation",
        })
        t.ticks(2)
        -- UNRUN: pages and menu rewritten from 4621 [598..682] (the offer menu "Start the Desert Treasure I quest?", Yes. / No.).
        t.expect("quest.stage.s_started", t.quest.expect_stage("s_started"))
        t.check("etchings-in-pack", count("four_diamonds_etchings") == 1, "etchings held: " .. count("four_diamonds_etchings"))

        -- Out of the desert by Lumbridge Teleport, then on foot to the Exam Centre through the members' gate.
        t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = "talkToExpert.lumbridgeTeleport",
            runes = { { "airrune", 3 }, { "earthrune", 1 }, { "lawrune", 1 } }, where = "Lumbridge" })
        t.exec("goto-talkToExpert.memberGate", t.player.goto_tile, 3310, 3332, 0)
        t.exec("talkToExpert.memberGateIn", t.player.pass_door, { closed = "fai_varrock_member_gater", open = "fai_varrock_member_gater_open",
            at = { 3312, 3332, 0 }, near = { 3311, 3332 }, far = { 3313, 3332 } })
        t.exec("goto-talkToExpert", t.player.goto_tile, 3352, 3340, 0)
        t.exec("talkToExpert.examDoorIn", t.player.pass_door, { closed = "qip_digsite_poshdoor", open = "qip_digsite_poshdoor_open",
            at = { 3352, 3337, 0 }, near = { 3352, 3338 }, far = { 3352, 3336 } })
        t.exec("talkToExpert", t.player.talk_to, "archaeological_expert", 1)
        t.exec("talkToExpert-dialog", t.chat.play, {
            "player:Hello, are you Terry Balando?",
            "npc:That's right, who wants",
            "npc:Ah yes, I recognise you!",
            "player:That's right.",
            "npc:You spoke to",
            "player:So what does the inscription",
            "npc:This... this is fascinating",
            "player:Can you translate",
            "npc:Well, I am not familiar",
            "player:Um... So, can you translate it for me or not?",
            "npc:Well, unfortunately",
            "npc:It might be slightly obscure",
            "npc:translate into a journal",
        })
        t.ticks(2)
        -- UNRUN: 4621 [951..1005]; the stage goes 1 -> 2 on "So what does the inscription say?" and 2 -> 3 on "Well, unfortunately".
        t.expect("quest.stage.s_translating", t.quest.expect_stage("s_translating"))

        t.exec("talkToExpertAgain", t.player.talk_to, "archaeological_expert", 1)
        t.exec("talkToExpertAgain-dialog", t.chat.play, {
            "npc:There you go",
            "npc:right track",
        })
        t.ticks(2)
        t.expect("quest.stage.s_translation", t.quest.expect_stage("s_translation"))
        t.exec("bringTranslationToArchaeologist.examDoorOut", t.player.pass_door, { closed = "qip_digsite_poshdoor", open = "qip_digsite_poshdoor_open",
            at = { 3352, 3337, 0 }, near = { 3352, 3336 }, far = { 3352, 3339 } })
        t.exec("goto-bringTranslationToArchaeologist.memberGate", t.player.goto_tile, 3314, 3332, 0)
        t.exec("bringTranslationToArchaeologist.memberGateOut", t.player.pass_door, { closed = "fai_varrock_member_gater", open = "fai_varrock_member_gater_open",
            at = { 3312, 3332, 0 }, near = { 3313, 3332 }, far = { 3311, 3332 } })
        shantay("bringTranslationToArchaeologist", false)
        t.exec("goto-bringTranslationToArchaeologist", t.player.goto_tile, 3177, 3043, 0)
        t.exec("bringTranslationToArchaeologist", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
        t.exec("bringTranslationToArchaeologist-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:So what did Terry Balando say",
            "player:Yeah, he did. I have it here",
            "npc:Did you take a read",
            "choose:Don't read book",
            "player:Yeah, I did.",
            "npc:Excellent.",
        })
        t.ticks(2)
        -- UNRUN: "Don't read book" (4621 [1338]) writes stage 5 and takes the primer; "Read book" opens the book instead.
        t.expect("quest.stage.s_smith_read", t.quest.expect_stage("s_smith_read"))

        t.exec("talkToArchaeologistAgainAfterTranslation", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
        t.exec("talkToArchaeologistAgainAfterTranslation-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:Hmmm. Interesting.",
            "npc:So what do you say?",
            "player:Uh... don't you mean",
            "npc:Yeeees",
            "npc:It's all this hot sun",
            "npc:Well anyway",
            "npc:all over to the Museum",
            "npc:Let's also say",
            "npc:argument thirty percent",
            "player:Fifty percent.",
            "npc:That's right!",
            "npc:A purely for the sake",
            "npc:Do you see where",
            "player:You want me",
            "player:discovers you're actually",
            "npc:Uh... yes",
            "npc:So what do you say?",
            "choose:Help him",
            "player:Well...",
            "player:Aw, go on then",
            "npc:That's what I like to hear!",
            "npc:I'll continue",
            "npc:You head due South",
        })
        t.ticks(2)
        -- UNRUN: 4621 [1352..1433]; stage 6 goes out with "That's what I like to hear!".
        t.expect("quest.stage.s_split", t.quest.expect_stage("s_split"))

        -- The pub's doorway is open on the map (reach.py from the camp: REACH with every door shut).
        t.exec("goto-buyDrink", t.player.goto_tile, 3159, 2981, 0)
        local coins_before_beer = count("coins")
        t.exec("buyDrink", t.player.talk_to, "fourdiamonds_bartender", 1)
        t.exec("buyDrink-dialog", t.chat.play, {
            "npc:If you're not buying",
            "choose:Buy a drink",
            "npc:What's that?",
            "choose:Buy a beer",
            "npc:There you go.",
        })
        t.ticks(2)
        t.check("buyDrink-paid", count("coins") < coins_before_beer, "coins " .. coins_before_beer .. " -> " .. count("coins") .. " for the beer")
        t.exec("talkToBartender", t.player.talk_to, "fourdiamonds_bartender", 1)
        -- UNRUN: the talk after the beer writes stage 7 on its first page (4621 [2654]) and stage 8 on "It's just a fairy tale"
        -- (4622 [38]); one talk here, the recorder's were two.
        t.exec("talkToBartender-dialog", t.chat.play, {
            "npc:You've had your drink",
            "player:No, Wait!",
            "player:I am only here",
            "npc:Oh really?",
            "choose:I heard about four diamonds...",
            "player:I heard a rumour",
            "npc:The four diamonds of Azzanadra",
            "player:You've heard of them then?",
            "npc:It's just a fairy tale",
            "npc:Now get out of here",
        })
        t.ticks(2)
        -- UNRUN: 4622 [35..39]; stage 8 goes out with "It's just a fairy tale".
        t.expect("quest.stage.s_elders", t.quest.expect_stage("s_elders"))

        -- Eblis's house: the curtain desertdoorclosed on the east edge of 3182,2984, pressed in and out.
        t.exec("goto-talkToEblis", t.player.goto_tile, 3180, 2984, 0)
        t.exec("talkToEblis.curtainIn", t.player.pass_door, { closed = "desertdoorclosed", open = "desertdooropen",
            at = { 3182, 2984, 0 }, near = { 3182, 2984 }, far = { 3184, 2984 } })
        t.exec("talkToEblis", t.player.talk_to, "fd_elder_village", 1)
        t.exec("talkToEblis-dialog", t.chat.play, {
            "player:Hello. I represent",
            "npc:Ah yes.",
            "npc:I have nothing to say",
            "player:Please, if I can just",
            "npc:(sigh)",
            "choose:Tell me of The four diamonds of Azzanadra",
            "player:So tell me",
            "npc:This is the treasure",
            "npc:Please accept my apologies!",
            "npc:looking to restore",
            "player:Uh... yeah...",
            "npc:Heard of them?",
            "player:So... do you have",
            "player:Uh, valuable as historical",
            "npc:They were stolen",
            "npc:I suspect they will not",
            "npc:Beware too",
            "player:How do you mean?",
            "npc:Each diamond",
            "npc:There is the Diamond of Blood",
            "npc:You should expect",
            "player:Do you have any idea",
            "npc:There is an ancient spell",
            "npc:Should you be willing",
            "npc:right track",
            "npc:Is your desire",
            "choose:Yes",
            "player:Sure, what do you need?",
            "npc:For this spell, I will need",
            "npc:When enchanted",
            "player:Okay, but what exactly",
            "npc:Well, six scrying glasses",
            "npc:glass, and 6 steel bars.",
            "npc:In addition",
            "npc:Do you understand me",
            "player:Quick question",
            "npc:Standard bones.",
            "choose:Yes, I will go get those for you.",
            "player:It's a slightly odd",
        })
        t.ticks(2)
        -- UNRUN: 4622 [93..273]; the menu at stage 8 has five options, stage 9 goes out with "Sure, what do you need?".
        t.expect("quest.stage.s_list", t.quest.expect_stage("s_list"))

        -- Eblis takes the scrying ingredients one item on him (opnpcu, deserttreasure.rs2:336), noted forms included.
        local eblis = t.player.by_symbol("npc", "fd_elder_village")
        local gifts = {
            -- UNRUN: the three bulk items give no page (4622 [1144..1149]: only "You hand over ..." lines); the four single
            -- items give Eblis's thanks (4622 [1161..1169]). The varbits fd_* count up per unit.
            { "eblis-magic_logs", "cert_magic_logs", nil },
            { "eblis-steel_bar", "cert_steel_bar", nil },
            { "eblis-molten_glass", "cert_molten_glass", nil },
            { "eblis-bones", "bones", "npc:Thank you, those are enough bones" },
            { "eblis-ashes", "ashes", "npc:Thank you, that is enough ash" },
            { "eblis-charcoal", "charcoal", "npc:Thank you, that is enough charcoal" },
            { "eblis-bloodrune", "bloodrune", "npc:Thank you, that blood rune" },
        }
        for _, g in ipairs(gifts) do
            local before = count(g[2])
            t.exec(g[1], t.player.use_on, g[2], eblis)
            if g[3] then t.exec(g[1] .. "-page", t.chat.play, { g[3] }) else t.ticks(2) end
            t.check(g[1] .. "-taken", count(g[2]) == 0 and before > 0, g[2] .. " " .. before .. " -> " .. count(g[2]) .. " (Eblis took them)")
        end
        t.exec("talkToEblis-complete", t.player.talk_to, "fd_elder_village", 1)
        t.exec("talkToEblis-complete-dialog", t.chat.play, {
            "npc:Excellent! Those are all",
            "npc:I will find a suitable spot",
            "npc:mirrors to find the diamonds.",
        })
        t.ticks(2)
        -- UNRUN: stage 10 and fd_mirror_present = 1 go out on the first page of this talk (4622 [1174]).
        t.expect("quest.stage.s_mirrors", t.quest.expect_stage("s_mirrors"))
        t.exec("talkToEblisAtMirrors.curtainOut", t.player.pass_door, { closed = "desertdoorclosed", open = "desertdooropen",
            at = { 3182, 2984, 0 }, near = { 3183, 2984 }, far = { 3180, 2984 } })

        t.exec("goto-talkToEblisAtMirrors", t.player.goto_tile, 3214, 2954, 0)
        t.exec("talkToEblisAtMirrors", t.player.talk_to, "fd_elder_by_mirrors", 1)
        t.exec("talkToEblisAtMirrors-dialog", t.chat.play, {
            "npc:Ah, so you got here",
            "npc:As you may have noticed",
            "npc:By simply looking",
            "npc:Unfortunately, I cannot narrow",
            "npc:the first place.",
            "player:So you can't be anymore",
            "npc:I'm afraid not",
            "npc:Make sure to come and speak",
        })

        t.ticks(2)
        -- UNRUN: stage 11 goes out on "Ah, so you got here at last." (4622 [1229]); the four hunts open at 11, not 10.
        local _, stage = t.var.server("varb358_deserttreasure")
        t.check("leg.1.state", stage == 11, "Eblis at the mirrors talked to; tile " .. reading()
            .. ", deserttreasure stage read from server = " .. tostring(stage) .. ", coins " .. count("coins"))
                -- LEG 1 END
            end,
        },
        {
            name = "smoke_dungeon",
            run = function(t)
        -- LEG 2 BEGIN: enterSmokeDungeon
        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function lowest(detail) return tonumber(tostring(detail):match("lowest hp (%d+)/")) end
        local function margin(name, fight, low, food_left)
            t.check(name, low ~= nil and low * 4 >= 99, -- food left is reported, not graded: the bosses now hit as recorded and the pack is full of kit
                fight .. ": lowest hp " .. tostring(low) .. "/99, sharks left " .. food_left .. " (margin: lowest hp >= a quarter of 99)")
        end
        local function free_slots()
            local n = 0
            for i = 0, 27 do
                local r, sl = t.inv.slot(i)
                if r == "ok" and type(sl) == "table" and sl.name == "" then n = n + 1 end
            end
            return n
        end
        local function protect_melee(name, want)
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            local tab_result, wr = "ok", "ok"
            if now ~= want then
                tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local pw
                wr, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(2)
            end
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, pr = t.skill.read("prayer")
            t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on)
                .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
        end
        local function shantay(pfx)
            t.exec("goto-" .. pfx .. ".shantay", t.player.goto_tile, 3304, 3123, 0)
            local coins0, pass0 = count("coins"), count("shantay_pass")
            t.exec(pfx .. ".buyPass", t.player.talk_to, "shantay", 4)
            t.exec(pfx .. ".buyPass-dialog", t.chat.play, { "objbox:You purchase a Shantay Pass." })
            t.check(pfx .. ".buyPass-paid", count("shantay_pass") == pass0 + 1 and count("coins") == coins0 - 5,
                "shantay_pass " .. pass0 .. " -> " .. count("shantay_pass") .. ", coins " .. coins0 .. " -> " .. count("coins"))
            t.exec("walk-" .. pfx .. ".toDoorway", t.player.walk_route, { { 3304, 3118 } })
            t.exec(pfx .. ".doorway", t.player.click_loc, "shantay_pass_henge_doorway", 1)
            local wo = t.ui.await_open("cws_warning_10", 15)
            local wr, ww = t.ui.widget("cws_warning_10:warn1")
            t.check(pfx .. ".doorway-dialog", wo == "ok" and wr == "ok" and select(1, t.ui.invoke(ww, 1)) == "ok",
                "desert warning " .. tostring(wo) .. ", warn1 " .. tostring(wr))
            t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and tt.z < 3116 end, note = pfx .. ".doorway" }, 10)
            local r, tt = t.world.tile()
            t.check(pfx .. ".doorway-landed", r == "ok" and tt.level == 0 and tt.x == 3304 and tt.z < 3116 and count("shantay_pass") == pass0,
                "after the doorway: " .. reading() .. " (lands 3304,3115), shantay_pass " .. count("shantay_pass") .. " (handed over)")
        end
        -- Al Kharid bank (the doorway at 3272,3166-3167 holds only inactive open leaves): walked in from the street.
        local function al_kharid_bank(pfx, withdraw, deposit)
            t.exec("goto-" .. pfx .. ".alKharidBank", t.player.goto_tile, 3276, 3167, 0)
            t.exec("walk-" .. pfx .. ".intoBank", t.player.walk_route, { { 3269, 3166 } })
            t.exec(pfx .. ".bankOpen", t.bank.open, "bankbooth", 2, { at = { 3268, 3166 } })
            for _, d in ipairs(deposit or {}) do
                if count(d[1]) > 0 then t.exec(pfx .. ".deposit." .. d[1], t.bank.deposit, d[1], d[2]) end
            end
            for _, w in ipairs(withdraw or {}) do t.exec(pfx .. ".withdraw." .. w[1], t.bank.withdraw, w[1], w[2]) end
            t.check(pfx .. ".bankClose", t.bank.close())
            t.exec("walk-" .. pfx .. ".outOfBank", t.player.walk_route, { { 3276, 3167 } })
        end

        t.exec("goto-enterSmokeDungeon", t.player.goto_tile, 3310, 2964, 0)
        -- The first click on the well only speaks (4623 [1253]: two lines of the player's, fd_fw_warningspam 0 -> 1);
        -- the climb is the next click.
        t.exec("enterSmokeDungeon.look", t.player.click_loc, "sword_haunted_well", 1, { at = { 3310, 2962, 0 } })
        t.exec("enterSmokeDungeon.look-dialog", t.chat.play, {
            "player:I can see a lot of foul looking smoke at the bottom of this well.",
            "player:I don't think breathing it in directly would be very good for my health.",
        })
        t.exec("enterSmokeDungeon", t.player.cross_gate, { loc = "sword_haunted_well", at = { 3310, 2962, 0 }, near = { 3310, 2963 },
            far_ok = function(tile) return tile.z > 6400 end, far_desc = "down the well in the smoke dungeon (z > 6400)" })
        -- Without the warm key the gate is locked (dt_smoke.rs2 dt_smoke_gate_open; recorded 4623 [1459] as a message box, not a game line): the guide's enterFareedRoom state.
        t.exec("goto-enterFareedRoom", t.player.goto_tile, 3303, 9376, 0)
        t.exec("enterFareedRoom", t.player.click_loc, "fd_fw_metalgateclosed_r", 1)
        -- UNRUN: the line is now a message box, "This gate is locked." (4623 [1459]); t.msg.expect may not read box text, then read it the way the other box rows do.
        t.exec("enterFareedRoom-locked", t.msg.expect, "This gate is locked")
        -- The four torches must burn at once (each lasts 501 ticks, 4623). The recorded lighting never failed and ours has no roll now; the retry is harmless.
        for _, torch in ipairs({
            { "lightTorch1", "4d_standing_torch1_unlit", 3323, 9398 },
            { "lightTorch2", "4d_standing_torch2_unlit", 3321, 9355 },
            { "lightTorch4", "4d_standing_torch3_unlit", 3204, 9350 },
            { "lightTorch3", "4d_standing_torch4_unlit", 3207, 9395 },
        }) do
            t.exec("goto-" .. torch[1], t.player.goto_tile, torch[3], torch[4] - 1, 0)
            -- The line comes with the use itself (4623 [1306]: the click's answer, no delay), so it is read from
            -- the chat ring afterwards, and the torch's own varbit says it burns.
            local n = ({ lightTorch1 = 360, lightTorch2 = 361, lightTorch4 = 362, lightTorch3 = 363 })[torch[1]]
            local lit_var = "varb" .. n .. "_fd_torch_count" .. (n - 359)
            t.player.use_on("tinderbox", t.player.by_symbol("loc", torch[2]), { at = { torch[3], torch[4] } })
            t.ticks(3)
            local mr = t.msg.expect("You light the torch.")
            t.check(torch[1], mr == "ok" and select(2, t.var.server(lit_var)) == 1, "tinderbox on " .. torch[2] .. " at " .. torch[3] .. "," .. torch[4]
                .. ": 'You light the torch.' " .. tostring(mr) .. ", " .. lit_var .. " = " .. tostring(select(2, t.var.server(lit_var))))
        end
        t.exec("goto-openChest", t.player.goto_tile, 3248, 9362, 0)
        t.exec("openChest", t.player.click_loc, "fd_firedungeon_shutchest", 1)
        t.exec("openChest-key", t.inv.await, "fd_firekey", 1, 10)
        t.check("openChest-key-held", count("fd_firekey") == 1, "warm keys held: " .. count("fd_firekey"))
        t.exec("goto-useWarmKey", t.player.goto_tile, 3303, 9376, 0)
        -- ANY-OF: useWarmKey useWarmKey.openGate the gate's own Open spends the warm key held in the pack and walks the player in (no [oplocu] trigger on fd_fw_metalgateclosed_r): OSRS-Content/osrs-content/server/scripts/quests/quest_deserttreasure/scripts/deserttreasure.rs2:986
        -- Fareed is a melee fighter (deserttreasure.rs2:1042 ~npc_meleeattack): Protect from Melee goes UP before the
        -- gate press that spawns him (dt_smoke_gate_open npc_add), so it is on for his first swing.
        local _, pr_f = t.skill.read("prayer")
        t.check("killFareed-prayerPoints", type(pr_f) == "table" and pr_f.level >= 90,
            "prayer " .. tostring(type(pr_f) == "table" and pr_f.level) .. "/99 before Fareed (want >= 90: prayer does not regenerate)")
        protect_melee("killFareed-protectMelee", 1)
        local _, before_gate = t.world.tile()
        t.exec("useWarmKey.openGate", t.player.click_loc, "fd_fw_metalgateclosed_r", 1)
        t.ticks(3)
        local _, after_gate = t.world.tile()
        t.check("useWarmKey-consumed", count("fd_firekey") == 0 and type(after_gate) == "table" and after_gate.x >= 3305,
            "the gate press spent the warm key (held now " .. count("fd_firekey") .. ") and carried the player from "
                .. tostring(before_gate and before_gate.x) .. " to x " .. tostring(after_gate and after_gate.x) .. " (Fareed's room, x >= 3305)")
        local sharks_at_fareed = count("shark")
        t.exec("killFareed-engage", t.player.attack, "firediamond_firewarrior", 2, 20)
        t.exec("killFareed-cast", t.player.cast, "water_blast", "firediamond_firewarrior", 14)
        local _, fareed_detail = t.exec("killFareed", t.npc.await_dead_engaged, 400, 60, { eat = { item = "shark", below = 55 } })
        margin("killFareed-margin", "Fareed (sharks at the start " .. sharks_at_fareed .. ")", lowest(fareed_detail), count("shark"))
        protect_melee("killFareed-prayerOff", 0)
        t.ticks(3)
        -- UNRUN: the diamond goes straight into the pack at death + 2 with "You take the Diamond of Smoke from the ashes of the warrior." (4623 [2120]), once dt_fareed.rs2 queues ~dt_smoke_fareed_slain; until the fight agent does, this row falls back to the ashes pickup.
        t.ticks(3)
        if count("fd_diamond_fire") < 1 then t.exec("pickUpFireDiamond", t.player.click_obj, "fd_diamond_fire", 3) end
        t.ticks(2)
        t.check("smoke-diamond-held", count("fd_diamond_fire") == 1, "smoke diamonds held: " .. count("fd_diamond_fire"))

        -- Out of the dungeon by Camelot Teleport; Rasolo is an overland walk south-west (reach.py 346 tiles).
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "talkToRasolo.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        t.exec("goto-talkToRasolo", t.player.goto_tile, 2535, 3430, 0)
        t.exec("talkToRasolo", t.player.talk_to, "shadow_warrior_rasool", 1)
        t.exec("talkToRasolo-dialog", t.chat.play, {
            -- UNRUN: the recorded pages of 4624 [292]..[504] (Rasolo rebuilt from them, dt_shadow.rs2)
            "npc:Greetings friend.",
            "choose:Ask about the Diamonds of Azzanadra",
            "player:No, actually",
            "npc:Hmmmm?",
            "player:I am looking for one",
            "npc:Ahhh",
            "npc:I know the object",
            "player:How can I find",
            "npc:Well now",
            "npc:I have in my possession",
            "player:How much do you want",
            "npc:Ah, but it is not for sale",
            "npc:I am offering to trade",
            "npc:The item in question",
            "player:Where can I find this Laheeb?",
            "npc:Well, as a travelling merchant",
            "npc:To the far east",
            "player:Al Kharid?",
            "npc:...Yes, Al Kharid.",
            "player:Shantay Pass.",
            "npc:...Yes. I didn't realise",
            "npc:Anyway, when you have gone",
            "npc:village...",
            "player:The Bedabin camp.",
            "npc:If you know where Laheeb lives",
            "player:I don't.",
            "npc:Well, okay then.",
            "npc:thieves...",
            "npc:He will have hidden",
            "npc:of visibility",
            "choose:Yes",
            "player:Not a problem",
        })
        t.ticks(2)
        t.check("talkToRasolo-shadow-fetch", select(2, t.var.server("varb383_fd_shadowwarrior_quest")) == 2,
            "fd_shadowwarrior_quest = " .. tostring(select(2, t.var.server("varb383_fd_shadowwarrior_quest"))) .. " (agreed, 4624 [504])")

        -- Back to the Bandit Camp: Lumbridge Teleport, the Al Kharid bank for lockpicks (Quest Helper: "as many
        -- lockpicks as you can"), the Shantay Pass.
        t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = "getCross.lumbridgeTeleport",
            runes = { { "airrune", 3 }, { "earthrune", 1 }, { "lawrune", 1 } }, where = "Lumbridge" })
        local function fill_lockpicks(pfx)
            -- every free slot but two (the gilded cross, the Shantay pass) takes a lockpick; the tinderbox is done
            local before = free_slots() + count("tinderbox")
            al_kharid_bank(pfx, { { "lockpick", math.max(1, before - 2) } }, { { "tinderbox", "all" } })
        end
        fill_lockpicks("getCross")
        shantay("getCross")
        t.exec("goto-getCross", t.player.goto_tile, 3169, 2965, 0)
        local picked = false
        local pick_tries = 0
        for trip = 1, 3 do
            for attempt = 1, 40 do
                if count("lockpick") == 0 then break end
                pick_tries = pick_tries + 1
                -- a miss costs 2 or 3 hitpoints (dt_shadow.rs2 dt_shadow_pick_fail, 4625): eat first
                local _, hp_pick = t.skill.read("hitpoints")
                if type(hp_pick) == "table" and (hp_pick.level or 99) < 40 and count("shark") > 0 then
                    t.player.inv_op("shark", 1)
                    t.ticks(3)
                end
                t.player.click_loc("fd_bandit_shutchest", 1)
                t.chat.play({ "mesbox:Your skill as a thief", "choose:Yes" })
                t.ticks(6)
                if select(2, t.var.server("varb384_fd_banditchest_disarmed")) == 1 then picked = true break end
            end
            if picked then break end
            -- Every lockpick snapped: out by the Shantay doorway (free from the south, shantay_pass.rs2:84), more from the bank.
            t.exec("goto-getCross.leaveDesert" .. trip, t.player.goto_tile, 3304, 3113, 0)
            t.exec("getCross.leaveDesert" .. trip, t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 }, near = { 3304, 3114 },
                far_ok = function(tile) return tile.z > 3116 end, far_desc = "north of the Shantay doorway" })
            fill_lockpicks("getCross.restock" .. trip)
            shantay("getCross.return" .. trip)
            t.exec("goto-getCross.return" .. trip, t.player.goto_tile, 3169, 2965, 0)
        end
        t.check("pickChestLocks", picked, "fd_banditchest_disarmed = " .. tostring(select(2, t.var.server("varb384_fd_banditchest_disarmed"))) .. " after " .. pick_tries .. " attempt(s)")
        -- UNRUN: the cross comes with the last lock, four ticks after it (4625 [1493]..[1497]); no second press.
        t.exec("getCross-held", t.inv.await, "fd_sword_cross", 1, 12)
        t.check("getCross-count", count("fd_sword_cross") == 1, "gilded crosses held: " .. count("fd_sword_cross"))

        t.ticks(2)
        local _, stage = t.var.server("varb358_deserttreasure")
        t.check("leg.2.end", stage == 11 and count("fd_sword_cross") == 1 and count("fd_diamond_fire") == 1,
            "tile " .. reading() .. ", deserttreasure stage read from server = " .. tostring(stage)
            .. " (11, Eblis at the mirrors), gilded cross " .. count("fd_sword_cross") .. " and smoke diamond " .. count("fd_diamond_fire") .. " held, sharks " .. count("shark")
            .. ", lockpicks " .. count("lockpick"))
                -- LEG 2 END
            end,
        },
        {
            name = "shadow_diamond",
            run = function(t)
        -- LEG 3 BEGIN: returnCross
        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function lowest(detail) return tonumber(tostring(detail):match("lowest hp (%d+)/")) end
        local function margin(name, fight, low, food_left)
            t.check(name, low ~= nil and low * 4 >= 99, -- food left is reported, not graded: the bosses now hit as recorded and the pack is full of kit
                fight .. ": lowest hp " .. tostring(low) .. "/99, sharks left " .. food_left .. " (margin: lowest hp >= a quarter of 99)")
        end
        local function free_slots()
            local n = 0
            for i = 0, 27 do
                local r, sl = t.inv.slot(i)
                if r == "ok" and type(sl) == "table" and sl.name == "" then n = n + 1 end
            end
            return n
        end
        -- Out of the desert: Camelot Teleport, the Seers' Village bank (open doorway, kr_bankdoor_*_inactive) for food.
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "returnCross.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        t.exec("goto-returnCross.seersBank", t.player.goto_tile, 2726, 3484, 0)
        t.exec("walk-returnCross.intoBank", t.player.walk_route, { { 2726, 3489 }, { 2724, 3493 } })
        t.exec("returnCross.bankOpen", t.bank.open, "kr_bankbooth", 2, { at = { 2724, 3494 } })
        -- the leftover lockpicks, the beer and the waterskin (no desert until the pyramid) make room for food
        for _, d in ipairs({ "lockpick", "bandit_brew", "water_skin4", "water_skin3", "water_skin2", "water_skin1", "water_skin0" }) do
            if count(d) > 0 then t.exec("returnCross.deposit." .. d, t.bank.deposit, d, "all") end
        end
        -- Prayer potions (Quest Helper's list, DesertTreasure.java:302): Fareed took some prayer and it does not regenerate.
        t.exec("returnCross.withdraw.4doseprayerrestore", t.bank.withdraw, "4doseprayerrestore", 2)
        local shark_room = free_slots() - 1 -- one slot for the ring of visibility
        if shark_room > 0 then t.exec("returnCross.withdraw.shark", t.bank.withdraw, "shark", shark_room) end
        t.check("returnCross.bankClose", t.bank.close())
        t.exec("walk-returnCross.outOfBank", t.player.walk_route, { { 2726, 3489 }, { 2726, 3484 } })
        t.exec("goto-returnCross", t.player.goto_tile, 2535, 3430, 0)
        t.exec("returnCross", t.player.talk_to, "shadow_warrior_rasool", 1)
        t.exec("returnCross-dialog", t.chat.play, {
            "npc:Have you retrieved",
            "player:Yes I have!",
        })
        t.ticks(2)
        -- UNRUN: 4626 [381] gives the ring on the player's page and keeps the cross in the pack.
        t.check("returnCross-ring", count("fd_ring_visibility") == 1 and count("fd_sword_cross") == 1, "rings of visibility held: " .. count("fd_ring_visibility")
            .. ", crosses " .. count("fd_sword_cross") .. ", fd_shadowwarrior_quest = " .. tostring(select(2, t.var.server("varb383_fd_shadowwarrior_quest"))) .. " (3)")

        t.exec("wear-ringOfVisibility", t.player.equip, "fd_ring_visibility")
        t.ticks(2)
        -- The shadow ladder (fd_shadowladder1, 2547,3421, shown by the ring) -> ^dt_shadow_dungeon 2630,5071
        -- (deserttreasure.rs2:1653 p_teleport): an overland hop to it (reach.py 20 tiles), then the press.
        t.exec("goto-enterShadowDungeon", t.player.goto_tile, 2547, 3423, 0)
        t.exec("enterShadowDungeon", t.player.cross_gate, { loc = "fd_shadowladder1", at = { 2547, 3421, 0 }, near = { 2547, 3422 },
            far_ok = function(tile) return tile.x == 2630 and tile.z == 5071 end, far_desc = "the shadow dungeon, 2630,5071 (^dt_shadow_dungeon, 4626 [433])" })
        -- Protect from Melee for both forms (recipe: verbs-combat.md "Turning on a protection prayer"): Damis is a crush
        -- fighter (fd_damis_normal / fd_damis_tougher damagetype 2, deserttreasure.npc) and a prayed npc melee hit is 0
        -- (combat_stats.rs2 playerhit_n_melee_apply). The prayer is UP before he spawns (his first swing). The true form's
        -- aura drains it (deserttreasure.rs2 [proc,dt_damis_prayer_drain]); then food.
        local function protect_melee(name, want)
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            local tab_result, wr = "ok", "ok"
            if now ~= want then
                tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local pw
                wr, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(2)
            end
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, pr = t.skill.read("prayer")
            t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on)
                .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
        end
        local drunk = {}
        for _ = 1, 6 do
            local _, pnow = t.skill.read("prayer")
            if type(pnow) ~= "table" or pnow.level >= 90 then break end
            local dose = nil
            for _, d in ipairs({ "1doseprayerrestore", "2doseprayerrestore", "3doseprayerrestore", "4doseprayerrestore" }) do
                if count(d) > 0 then dose = d break end
            end
            if dose == nil then break end
            drunk[#drunk + 1] = dose .. " " .. tostring(t.player.inv_op(dose, 1))
            t.ticks(2)
        end
        local _, pr0 = t.skill.read("prayer")
        t.check("killDamis-prayerPoints", type(pr0) == "table" and pr0.level >= 90,
            "prayer " .. tostring(type(pr0) == "table" and pr0.level) .. "/99 before Damis after drinking [" .. table.concat(drunk, ", ") .. "] (want >= 90: prayer does not regenerate)")
        protect_melee("killDamis-protectMelee", 1)
        t.exec("waitForDamis-goto", t.player.goto_tile, 2738, 5088, 0)
        t.exec("waitForDamis", t.npc.await_present, "fd_damis_normal", 15, 20)
        local sharks_at_damis = count("shark")
        t.exec("killDamis1-engage", t.player.attack, "fd_damis_normal", 2, 20)
        -- The first form does not die: at 0 the same npc becomes the true form on the next tick (4626 [875],
        -- fd_shadowwarrior_quest 3 -> 4), so its end is read from the varbit, not waited out as a death.
        local damis1_result, damis1_ticks = "timeout", 0
        for round = 1, 100 do
            if select(2, t.var.server("varb383_fd_shadowwarrior_quest")) == 4 then damis1_result = "ok" break end
            local _, hp1 = t.skill.read("hitpoints")
            if type(hp1) == "table" and hp1.level < 60 and count("shark") > 0 then t.player.inv_op("shark", 1) end
            -- One press is not the fight: on one roll the character stood 300 ticks beside Damis without felling
            -- him (the room is multi-combat; what took its swing was not traced). Attack is pressed again.
            if round % 8 == 0 then t.player.attack("fd_damis_normal", 2, 6) end
            t.ticks(3)
            damis1_ticks = damis1_ticks + 3
        end
        t.check("killDamis1", damis1_result == "ok", "the first form fell after about " .. damis1_ticks .. " tick(s): fd_shadowwarrior_quest = "
            .. tostring(select(2, t.var.server("varb383_fd_shadowwarrior_quest"))) .. " (4 = the true form)")
        local _, hp_after1 = t.skill.read("hitpoints")
        local damis_lowest = type(hp_after1) == "table" and hp_after1.level or nil
        t.exec("killDamis2-present", t.npc.await_present, "fd_damis_tougher", 15, 20)
        -- Single-way combat: the true form claims the player on spawn and every Attack answers "I'm already under attack."
        -- (docs/quest_authoring/gaps-combat.md ::passive); the type is held passive so the swing lands, Damis still dies for real.
        local passive_ok = true
        for _, sym in ipairs({ "sword_skeleton_3", "sword_skeleton_3b", "shadow_dog_wild", "small_bat" }) do
            if t.cheat("::passive " .. sym) ~= "ok" then passive_ok = false end
        end
        t.check("killDamis2-passive", passive_ok, "::passive on the dungeon's wanderers (skeletons, shadow dogs, bat) drops the single-way claim they hold on the player (test affordance, gaps-combat)")
        t.ticks(2)
        local engaged2, engage2_detail = "refused", ""
        for attempt = 1, 8 do
            engaged2, engage2_detail = t.player.attack("fd_damis_tougher", 2, 20)
            if engaged2 == "ok" then break end
            t.ticks(3)
        end
        t.check("killDamis2-engage", engaged2 == "ok", "attack fd_damis_tougher: " .. tostring(engaged2) .. " " .. tostring(engage2_detail))
        -- The true form's defence outlasts the scimitar: finish with water_blast casts as the guide allows magic.
        local damis2_result, damis2_detail = "timeout", ""
        local prayer_doses = {}
        for round = 1, 60 do
            -- his aura drains Prayer (deserttreasure.rs2 [proc,dt_damis_prayer_drain]): a potion before it runs dry
            local _, pnow = t.skill.read("prayer")
            if type(pnow) == "table" and pnow.level < 25 then
                for _, d in ipairs({ "1doseprayerrestore", "2doseprayerrestore", "3doseprayerrestore", "4doseprayerrestore" }) do
                    if count(d) > 0 then
                        prayer_doses[#prayer_doses + 1] = d .. " at " .. pnow.level .. " " .. tostring(t.player.inv_op(d, 1))
                        t.ticks(2)
                        break
                    end
                end
                local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                local _, pafter = t.skill.read("prayer")
                if on ~= 1 and type(pafter) == "table" and pafter.level > 0 then protect_melee("killDamis2-protectMeleeAgain" .. round, 1) end
            end
            t.player.cast("water_blast", "fd_damis_tougher", 8)
            damis2_result, damis2_detail = t.npc.await_dead_engaged(8, 1, { eat = { item = "shark", below = 65 } })
            local low = lowest(damis2_detail)
            if low and (damis_lowest == nil or low < damis_lowest) then damis_lowest = low end
            if damis2_result == "ok" then break end
        end
        t.check("killDamis2", damis2_result == "ok", "killed the true form of Damis with water_blast casts and melee: " .. tostring(damis2_result) .. " " .. tostring(damis2_detail))
        margin("killDamis-margin", "both forms of Damis (sharks at the start " .. tostring(sharks_at_damis) .. ", prayer doses mid-fight ["
            .. table.concat(prayer_doses, ", ") .. "])", damis_lowest, count("shark"))
        protect_melee("killDamis-prayerOff", 0)
        t.ticks(2)
        t.check("killDamis-stage", select(2, t.var.server("varb383_fd_shadowwarrior_quest")) == 5,
            "fd_shadowwarrior_quest = " .. tostring(select(2, t.var.server("varb383_fd_shadowwarrior_quest"))) .. " a tick after the corpse (5 = Damis dead, 4626 [1151])")
        t.exec("pickUpShadowDiamond", t.player.click_obj, "fd_dark_diamond", 3)
        t.exec("pickUpShadowDiamond-held", t.inv.await, "fd_dark_diamond", 1, 10)

        -- To Canifis the way Priest in Peril opened: Varrock Teleport, the members' gate east of Varrock, the
        -- Paterdomus trapdoor, both mausoleum gates, Drezel's advice and the holy barrier.
        t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = "talkToMalak.varrockTeleport",
            runes = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Varrock" })
        t.exec("goto-talkToMalak.memberGate", t.player.goto_tile, 3317, 3468, 0)
        t.exec("talkToMalak.memberGate", t.player.pass_door, { closed = "fai_varrock_member_gatel", open = "fai_varrock_member_gatel_open",
            at = { 3319, 3468, 0 }, near = { 3319, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-talkToMalak.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        t.exec("talkToMalak.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.ticks(2)
        local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3)
        t.check("talkToMalak.trapdoorOpen", tdo_r == "ok" and tdo.tile_x == 3405 and tdo.tile_z == 3507,
            "trapdoor_open after op1 Open -> " .. tostring(tdo_r) .. " " .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z) or tostring(tdo)) .. " (want 3405,3507)")
        t.exec("talkToMalak.descend", t.player.cross_gate, { loc = "trapdoor_open", at = { 3405, 3507, 0 }, near = { 3405, 3506 },
            far_ok = function(tile) return tile.z > 6400 end, far_desc = "under the temple (z > 6400)" })
        t.exec("talkToMalak.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 }, near = { 3405, 9896 },
            far_ok = function(tile) return tile.z < 9895 end, far_desc = "south of pip_underground_door1 (z < 9895)" })
        t.exec("walk-talkToMalak.toGate2", t.player.walk_route, { { 3405, 9890 }, { 3410, 9891 }, { 3418, 9893 }, { 3424, 9897 }, { 3431, 9897 } })
        t.exec("talkToMalak.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 }, near = { 3431, 9897 },
            far_ok = function(tile) return tile.x > 3431 end, far_desc = "east of pip_underground_door2 (x > 3431)" })
        t.exec("talkToMalak.talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        -- Drezel's talk after Priest in Peril (the pages priestperil.lua plays, RSProx index 4459 [2047..2085]):
        -- his blessing sets priestperil 61, and the barrier lets the player through.
        t.exec("talkToMalak.talkToDrezel-dialog", t.chat.play, {
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
        t.exec("talkToMalak.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("talkToMalak.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal", at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.z < 6400 and tile.x >= 3420 end, far_desc = "east of the Salve, out at 3423,3485" })
        -- Canifis's bar has open doorways (no door loc): reach.py 87 tiles from the barrier's landing.
        -- Into the Hair of the Dog by its north doorway, as the recorder did (4628 [3273]): the tithe scene takes
        -- the player two ticks on (dt_malak_scene.rs2; the pages past Malak's first are the wiki's), and at its end
        -- the server walks them to Malak and his talk opens with nothing pressed. test/quests/_dt_malak.lua is the
        -- same rows on their own.
        t.exec("goto-talkToMalak", t.player.goto_tile, 3494, 3482, 0)
        t.exec("walk-talkToMalak.in", t.player.walk_route, { { 3494, 3481 }, { 3494, 3479 } })
        t.ticks(4)
        t.exec("malakScene", t.chat.play, {
            "npc:M-my Lord Malak, what brings you here?",
            "npc:Hmm? You know very well that your tithe is due, Roavar. I am here to collect.",
            "npc:Y-yes of course, sire",
            "npc:Indeed it is Roavar",
            "npc:Of course, my lord, I meant no disrespect.",
            "npc:So you have the tithe ready for me?",
            "npc:Of course, my lord. I trust you will find it all there and in good order.",
            "npc:Hmm... Well, for your sake",
            "npc:Then again... perhaps this was not a total waste of my time after all.",
            "npc:You there!",
            "player:M-me?",
            "npc:Yes, you. Your bearing suggests you are not one of the usual peasants",
            "npc:Come here, I would like to know what you are doing here.",
        })
        t.check("malakScene-varbit", select(2, t.var.server("varb372_fdvw_cutscene")) == 1, "fdvw_cutscene = " .. tostring(select(2, t.var.server("varb372_fdvw_cutscene"))))
        t.ticks(8)
        t.exec("talkToMalak-dialog", t.chat.play, {
            -- Every page of 4628 [3800..3910] on this path, the one-line carry-over pages included.
            "npc:A human, eh?",
            "npc:You had better make it a good one",
            "choose:I am looking for a special Diamond...",
            "player:I am here looking for a special diamond",
            "player:I'm fairly sure it will have",
            "npc:Interesting",
            "npc:I may have information",
            "npc:agreement?",
            "player:Well, what kind of something?",
            "npc:Ah, you have a healthy sense of paranoia",
            "npc:All I ask",
            "npc:When he is dead",
            "npc:So what say you?",
            "npc:The current owner of this diamond is named Dessous.",
            "choose:Yes",
            "player:Well... I can't see any drawback",
            "npc:What I know?",
            "npc:I'm afraid you will need to be a little more specific.",
            "choose:How can I kill Dessous?",
            "player:So what advice can you give me",
            "npc:As ancient and weak as Dessous",
            "npc:That is, assuming",
            "npc:My proposal",
            "player:How would I go about doing that?",
            "npc:Well, my plan would be as follows.",
            "npc:more interesting experiments many years past.",
            "npc:Then take that pot to Entrana",
            "npc:If you then bring that silver pot",
            "npc:Use that pot of blood",
            "npc:The combination of garlic",
            "npc:This is just my suggestion",
            "npc:Was there anything else you wanted?",
            "choose:Actually, I don't need to know anything.",
            "player:Never mind, I will figure out",
            "npc:As you wish. Come and see me",
        })
        t.ticks(2)
        t.check("talkToMalak-agreed", select(2, t.var.server("varb373_fdvw_subquest")) == 2, "fdvw_subquest = " .. tostring(select(2, t.var.server("varb373_fdvw_subquest"))) .. " (2 after the first How)")
        -- The repeat talk at sub-stage 2 (RSProx 4629 [191], [2199]): the shorter menu, "again?" wording.
        t.exec("askAboutKillingDessous", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
        t.exec("askAboutKillingDessous-dialog", t.chat.play, {
            "npc:Why are you still here?",
            "choose:How do I kill Dessous again?",
            "player:How am I supposed to kill Dessous again?",
            "npc:Take a silver bar",
            "npc:When you have done that",
            "npc:In this way",
        })
        t.ticks(2)

        -- Out of Morytania by Lumbridge Teleport; the Draynor bank (open doorway) for the silver bar, and the
        -- rune kit banked there before the Entrana boat (no weapon or armour: monk_of_entrana.rs2).
        t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = "enterSewer.lumbridgeTeleport",
            runes = { { "airrune", 3 }, { "earthrune", 1 }, { "lawrune", 1 } }, where = "Lumbridge" })
        t.exec("goto-enterSewer.draynorBank", t.player.goto_tile, 3092, 3250, 0)
        t.exec("walk-enterSewer.intoBank", t.player.walk_route, { { 3092, 3247 }, { 3092, 3243 } })
        local entrana_worn = { "rune_scimitar", "rune_chainbody", "rune_platelegs", "rune_kiteshield" }
        -- First the food and potions (the pack is full of sharks and an unequip needs a free slot), then the kit.
        t.exec("enterSewer.bankOpen", t.bank.open, "bankbooth", 2, { at = { 3091, 3243 } })
        for _, d in ipairs({ "shark", "1doseprayerrestore", "2doseprayerrestore", "3doseprayerrestore", "4doseprayerrestore", "vial_empty" }) do
            if count(d) > 0 then t.exec("enterSewer.deposit." .. d, t.bank.deposit, d, "all") end
        end
        t.check("enterSewer.bankClose.food", t.bank.close())
        for _, w in ipairs(entrana_worn) do t.exec("enterSewer.unequip." .. w, t.player.unequip, w) end
        t.exec("enterSewer.bankOpen.kit", t.bank.open, "bankbooth", 2, { at = { 3091, 3243 } })
        for _, w in ipairs(entrana_worn) do t.exec("enterSewer.deposit." .. w, t.bank.deposit, w, "all") end
        t.exec("enterSewer.withdraw.silver_bar", t.bank.withdraw, "silver_bar", 1)
        t.check("enterSewer.bankClose", t.bank.close())
        t.exec("walk-enterSewer.outOfBank", t.player.walk_route, { { 3092, 3247 }, { 3092, 3250 } })
        t.exec("goto-enterSewer", t.player.goto_tile, 3118, 3245, 0)
        t.exec("enterSewer", t.player.click_loc, "vampire_trap1", 1)
        t.ticks(4) -- UNRUN: the open form lands two ticks after the op (RSProx 4629 [233]..[235])
        local vt_r, vt = t.world.loc_near("vampire_trap2", 3)
        t.check("enterSewer-opened", vt_r == "ok" and vt.tile_x == 3118 and vt.tile_z == 3244,
            "vampire_trap2 after op1 Open -> " .. tostring(vt_r) .. " " .. (vt_r == "ok" and (vt.tile_x .. "," .. vt.tile_z) or tostring(vt)) .. " (want 3118,3244)")
        t.exec("enterSewer-climb", t.player.cross_gate, { loc = "vampire_trap2", at = { 3118, 3244, 0 }, near = { 3118, 3245 },
            far_ok = function(tile) return tile.z > 6400 end, far_desc = "the Draynor sewer (z > 6400; ~climb_ladder_to 3118,9644)" })
        t.exec("goto-talkToRuantun", t.player.goto_tile, 3112, 9688, 0)
        t.exec("talkToRuantun", t.player.talk_to, "malak", 1)
        t.exec("talkToRuantun-dialog", t.chat.play, {
            "player:Hello.",
            "npc:You ssshould not",
            "player:Are you an assistant",
            "npc:I usssed to have",
            "player:I have a silver bar",
            "npc:Yesss, of courssse",
        })
        t.ticks(2)
        t.exec("talkToRuantun-pot", t.inv.await, "fd_silver_pot", 1, 10)
        t.check("talkToRuantun-bar", count("silver_bar") == 0, "silver bars held " .. count("silver_bar") .. " (Ruantun took it, deserttreasure.rs2:550)")
        t.exec("goto-talkToRuantun.ladder", t.player.goto_tile, 3118, 9644, 0)
        t.exec("talkToRuantun.climbOut", t.player.cross_gate, { loc = "vampire_ladder", at = { 3118, 9643, 0 }, near = { 3118, 9644 },
            far_ok = function(tile) return tile.z < 6400 end, far_desc = "back up in Draynor (z < 6400)" })
        local _, stage = t.var.server("varb358_deserttreasure")
        t.check("leg.3.end", stage == 11 and count("fd_silver_pot") == 1 and count("fd_dark_diamond") == 1,
            "tile " .. reading() .. ", deserttreasure stage read from server = " .. tostring(stage)
            .. ", fd_shadowwarrior_quest " .. tostring(select(2, t.var.server("varb383_fd_shadowwarrior_quest"))) .. "; dark diamond "
            .. count("fd_dark_diamond") .. ", smoke diamond " .. count("fd_diamond_fire") .. ", silver pot " .. count("fd_silver_pot") .. ", sharks " .. count("shark"))
            end,
        },
        {
            name = "blood_diamond",
            run = function(t)
        -- LEG 4 BEGIN: blessPot
        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function lowest(detail) return tonumber(tostring(detail):match("lowest hp (%d+)/")) end
        local function margin(name, fight, low, food_left)
            t.check(name, low ~= nil and low * 4 >= 99, -- food left is reported, not graded: the bosses now hit as recorded and the pack is full of kit
                fight .. ": lowest hp " .. tostring(low) .. "/99, sharks left " .. food_left .. " (margin: lowest hp >= a quarter of 99)")
        end
        local function free_slots()
            local n = 0
            for i = 0, 27 do
                local r, sl = t.inv.slot(i)
                if r == "ok" and type(sl) == "table" and sl.name == "" then n = n + 1 end
            end
            return n
        end
        -- Entrana: the monk's search (monk_of_entrana.rs2 ~has_entrana_restricted_items) passes with the rune kit in
        -- the Draynor bank (leg 3's enterSewer.deposit.* rows): nothing in the pack or worn carries a bonus.
        local forbidden = 0
        for _, s in ipairs({ "rune_scimitar", "rune_chainbody", "rune_platelegs", "rune_kiteshield", "dagger_wolfbane" }) do
            forbidden = forbidden + count(s)
        end
        t.check("blessPot.noWeaponOrArmour", forbidden == 0, "rune kit and dagger in the pack: " .. forbidden .. " (banked at Draynor)")
        t.exec("goto-blessPot.shipmonk", t.player.goto_tile, 3045, 3236, 0)
        t.exec("blessPot.boatToEntrana", t.player.talk_to, "shipmonk", 1)
        t.exec("blessPot.boatToEntrana-dialog", t.chat.play, {
            "npc:Do you seek passage",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes",
            "npc:Very well",
            "mesbox:The monk quickly searches you.",
            "npc:All is satisfactory. You may board the boat now.",
        })
        -- The crossing is a ship journey of its own length (~ship_journey), not a jump.
        t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and tt.level == 1 and tt.x < 2900 end, note = "the Entrana deck" }, 60)
        local dr, deck = t.world.tile()
        t.check("blessPot.onDeckAtEntrana", dr == "ok" and deck.level == 1 and math.abs(deck.x - 2834) <= 2 and math.abs(deck.z - 3331) <= 2,
            "after the crossing: " .. reading() .. " (want the deck, p_telejump(1_44_52_18_3) = 2834,3331,1)")
        t.exec("useGangPlank", t.player.climb, { loc = "ship_from_entrana_off", at = { 2834, 3333, 1 }, dest = { 2834, 3335, 0 }, slack = 3,
            landed_ok = function(tile) return tile.x < 2900 and tile.z > 3300 end, landed_desc = "ashore on Entrana" })
        t.exec("goto-blessPot", t.player.goto_tile, 2851, 3347, 0)
        t.exec("blessPot", t.player.talk_to, "high_priest_of_entrana", 1)
        t.exec("blessPot-dialog", t.chat.play, {
            "npc:Many greetings",
            "player:Hi, I was wondering",
            "npc:A somewhat strange request",
        })
        t.ticks(2)
        t.check("blessPot-blessed", count("fd_silver_pot_blessed") == 1,
            "blessed pots held: " .. count("fd_silver_pot_blessed") .. ", plain pots " .. count("fd_silver_pot"))
        -- Off Entrana the way it was reached: shipmonk2 (areas/entrana/scripts/monk_of_entrana.rs2 -> the Port Sarim deck
        -- 3048,3231,1), then ship_to_entrana_off ashore.
        t.exec("goto-talkToMalakWithPot.shipmonk2", t.player.goto_tile, 2832, 3336, 0)
        t.exec("talkToMalakWithPot.boatBack", t.player.talk_to, "shipmonk2", 1)
        t.exec("talkToMalakWithPot.boatBack-dialog", t.chat.play, {
            "npc:Do you wish to leave holy Entrana?",
            "choose:Yes, I'm ready to go.",
            "player:Yes, I'm ready to go.",
            "npc:Okay, let's board",
        })
        t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and tt.level == 1 and math.abs(tt.x - 3048) <= 2 and math.abs(tt.z - 3231) <= 2 end, note = "the Port Sarim deck" }, 60)
        local sr, sdeck = t.world.tile()
        t.check("talkToMalakWithPot.onDeckAtPortSarim", sr == "ok" and sdeck.level == 1 and math.abs(sdeck.x - 3048) <= 2 and math.abs(sdeck.z - 3231) <= 2,
            "after the crossing: " .. reading() .. " (want the deck, p_telejump(1_47_50_40_31) = 3048,3231,1)")
        t.exec("talkToMalakWithPot.gangplank", t.player.climb, { loc = "ship_to_entrana_off", at = { 3048, 3232, 1 }, dest = { 3048, 3234, 0 }, slack = 3,
            landed_ok = function(tile) return tile.x > 3000 and tile.z > 3232 end, landed_desc = "ashore on the Port Sarim jetty" })
        -- Draynor bank: the rune kit back on, and the Dessous ingredients and food (Quest Helper: garlic powder, spice).
        t.exec("goto-talkToMalakWithPot.draynorBank", t.player.goto_tile, 3092, 3250, 0)
        t.exec("walk-talkToMalakWithPot.intoBank", t.player.walk_route, { { 3092, 3247 }, { 3092, 3243 } })
        t.exec("talkToMalakWithPot.bankOpen", t.bank.open, "bankbooth", 2, { at = { 3091, 3243 } })
        local kit = { "rune_scimitar", "rune_chainbody", "rune_platelegs", "rune_kiteshield" }
        for _, w in ipairs(kit) do t.exec("talkToMalakWithPot.withdraw." .. w, t.bank.withdraw, w, 1) end
        t.exec("talkToMalakWithPot.withdraw.fd_crushed_garlic", t.bank.withdraw, "fd_crushed_garlic", 1)
        t.exec("talkToMalakWithPot.withdraw.spicespot", t.bank.withdraw, "spicespot", 1)
        t.exec("talkToMalakWithPot.withdraw.cake", t.bank.withdraw, "cake", 1)
        local room = free_slots()
        if room > 0 then t.exec("talkToMalakWithPot.withdraw.shark", t.bank.withdraw, "shark", room) end
        t.check("talkToMalakWithPot.bankClose", t.bank.close())
        for _, w in ipairs(kit) do t.exec("talkToMalakWithPot.wear-" .. w, t.player.equip, w) end
        t.exec("walk-talkToMalakWithPot.outOfBank", t.player.walk_route, { { 3092, 3247 }, { 3092, 3250 } })
        -- Back into Morytania: Varrock Teleport, the members' gate, the trapdoor, both gates and the barrier
        -- (Drezel's advice is given: priestperil = 61, the barrier passes at once, mausoleum_interactions.rs2:27).
        t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = "talkToMalakWithPot.varrockTeleport",
            runes = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Varrock" })
        t.exec("goto-talkToMalakWithPot.memberGate", t.player.goto_tile, 3317, 3468, 0)
        t.exec("talkToMalakWithPot.memberGate", t.player.pass_door, { closed = "fai_varrock_member_gatel", open = "fai_varrock_member_gatel_open",
            at = { 3319, 3468, 0 }, near = { 3319, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-talkToMalakWithPot.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        local tdc_r = t.world.loc_near("trapdoor", 2)
        if tdc_r == "ok" then
            t.exec("talkToMalakWithPot.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
            t.ticks(2)
        end
        local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3)
        t.check("talkToMalakWithPot.trapdoorOpen", tdo_r == "ok" and tdo.tile_x == 3405 and tdo.tile_z == 3507,
            "trapdoor_open -> " .. tostring(tdo_r) .. " " .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z) or tostring(tdo))
                .. " (want 3405,3507; closed copy present before: " .. tostring(tdc_r) .. ")")
        t.exec("talkToMalakWithPot.descend", t.player.cross_gate, { loc = "trapdoor_open", at = { 3405, 3507, 0 }, near = { 3405, 3506 },
            far_ok = function(tile) return tile.z > 6400 end, far_desc = "under the temple (z > 6400)" })
        t.exec("talkToMalakWithPot.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 }, near = { 3405, 9896 },
            far_ok = function(tile) return tile.z < 9895 end, far_desc = "south of pip_underground_door1 (z < 9895)" })
        t.exec("walk-talkToMalakWithPot.toGate2", t.player.walk_route, { { 3405, 9890 }, { 3410, 9891 }, { 3418, 9893 }, { 3424, 9897 }, { 3431, 9897 } })
        t.exec("talkToMalakWithPot.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 }, near = { 3431, 9897 },
            far_ok = function(tile) return tile.x > 3431 end, far_desc = "east of pip_underground_door2 (x > 3431)" })
        t.exec("talkToMalakWithPot.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal", at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.z < 6400 and tile.x >= 3420 end, far_desc = "east of the Salve, out at 3423,3485" })
        t.exec("goto-talkToMalakWithPot", t.player.goto_tile, 3496, 3477, 0)
        t.exec("talkToMalakWithPot", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
        t.exec("talkToMalakWithPot-dialog", t.chat.play, {
            "player:I found Ruantun",
            "player:Ow!",
            "npc:There you go",
            "player:Thanks for nothing",
            "npc:Come and speak",
        })
        t.ticks(2)
        t.check("talkToMalakWithPot-blood", count("fd_silver_pot_blood_blessed") == 1,
            "blood-filled blessed pots held: " .. count("fd_silver_pot_blood_blessed"))

        t.exec("addPowder", t.player.use_item_on_item, "fd_crushed_garlic", "fd_silver_pot_blood_blessed")
        t.ticks(2)
        t.check("addPowder-done", count("fd_silver_pot_blood_garlic_blessed") == 1 and count("fd_crushed_garlic") == 0,
            "garlic blood pots held: " .. count("fd_silver_pot_blood_garlic_blessed") .. ", garlic powder left " .. count("fd_crushed_garlic"))
        t.exec("addSpice", t.player.use_item_on_item, "spicespot", "fd_silver_pot_blood_garlic_blessed")
        t.ticks(2)
        t.check("addSpice-done", count("fd_silver_pot_blood_garlic_spiced_blessed") == 1 and count("spicespot") == 0,
            "seasoned blessed pots held: " .. count("fd_silver_pot_blood_garlic_spiced_blessed") .. ", spice left " .. count("spicespot"))

        -- The graveyard fence is closed on the south and west (maps/m55_53.jl2, loc 6557); its north side (z 3406, x 3568-3572) is the opening.
        t.exec("goto-usePotOnGrave", t.player.goto_tile, 3570, 3408, 0)
        t.exec("usePotOnGrave", t.player.use_on, "fd_silver_pot_blood_garlic_spiced_blessed", t.player.by_symbol("loc", "vampire_big_grave_noblood"))
        t.exec("usePotOnGrave-dessous", t.npc.await_present, "blooddiamond_vampirewarrior", 10, 20)
        t.check("usePotOnGrave-potUsed", count("fd_silver_pot_blood_garlic_spiced_blessed") == 0,
            "seasoned pots held after the tomb: " .. count("fd_silver_pot_blood_garlic_spiced_blessed"))
        -- Dessous rises on the far side of the tomb with no melee route (attack answers "I can't reach that!"), so he is fought with the water spells the guide allows.
        local sharks_at_dessous = count("shark")
        t.exec("killDessous-engage", t.player.cast, "water_blast", "blooddiamond_vampirewarrior", 8)
        local kill_result, kill_detail = "timeout", ""
        local dessous_lowest = nil
        for round = 1, 60 do
            kill_result, kill_detail = t.npc.await_dead_engaged(8, 1, { eat = { item = "shark", below = 65 } })
            local low = lowest(kill_detail)
            if low and (dessous_lowest == nil or low < dessous_lowest) then dessous_lowest = low end
            if kill_result == "ok" then break end
            t.player.cast("water_blast", "blooddiamond_vampirewarrior", 8)
        end
        t.check("killDessous", kill_result == "ok", "killed Dessous with water_blast casts: " .. tostring(kill_result) .. " " .. tostring(kill_detail))
        if dessous_lowest == nil then
            local _, hp = t.skill.read("hitpoints")
            dessous_lowest = type(hp) == "table" and hp.level or nil
        end
        margin("killDessous-margin", "Dessous (sharks at the start " .. sharks_at_dessous .. ")", dessous_lowest, count("shark"))
        t.ticks(2)
        t.check("killDessous-stage", select(2, t.var.server("varb373_fdvw_subquest")) == 3,
            "fdvw_subquest = " .. tostring(select(2, t.var.server("varb373_fdvw_subquest"))) .. " a tick after the corpse (3 = Dessous dead, 4629 [3404])")

        t.exec("goto-talkToMalakForDiamond", t.player.goto_tile, 3496, 3477, 0)
        t.exec("talkToMalakForDiamond", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
        t.exec("talkToMalakForDiamond-dialog", t.chat.play, {
            "npc:Ah, the wandering hero",
            "player:Quit playing games",
            "npc:Do not take that tone",
            "npc:Here, take your precious little bauble.",
            "npc:I will take that silver pot",
            "npc:Lord Drakan.",
            "npc:He will be extremely pleased",
        })
        t.ticks(2)
        t.check("talkToMalakForDiamond-held", count("fd_blood_diamond") == 1 and count("fd_silver_pot_blessed") == 0
            and select(2, t.var.server("varb373_fdvw_subquest")) == 4, -- UNRUN: RSProx 4629 [3472..3489]
            "blood diamonds held: " .. count("fd_blood_diamond") .. ", blessed pots " .. count("fd_silver_pot_blessed") .. ", fdvw_subquest " .. tostring(select(2, t.var.server("varb373_fdvw_subquest"))))

        -- To the troll child: Camelot Teleport, the Seers' bank for the Ice Path kit, then the whole walk up to the
        -- Trollheim summit (a pocket on foot: eadgar.lua's route, sampler b58 round 2).
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "giveCakeToTroll.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        t.exec("goto-giveCakeToTroll.seersBank", t.player.goto_tile, 2726, 3484, 0)
        t.exec("walk-giveCakeToTroll.intoBank", t.player.walk_route, { { 2726, 3489 }, { 2724, 3493 } })
        t.exec("giveCakeToTroll.bankOpen", t.bank.open, "kr_bankbooth", 2, { at = { 2724, 3494 } })
        for _, w in ipairs({ "death_climbingboots", "death_spikedboots", "abyssal_whip" }) do
            t.exec("giveCakeToTroll.withdraw." .. w, t.bank.withdraw, w, 1)
        end
        t.exec("giveCakeToTroll.withdraw.4dose2restore", t.bank.withdraw, "4dose2restore", 3)
        -- the scimitar comes off for the whip (one slot); the ice diamond needs one free at the end
        local room2 = free_slots() - 2
        if room2 > 0 then t.exec("giveCakeToTroll.withdraw.shark", t.bank.withdraw, "shark", room2) end
        t.check("giveCakeToTroll.bankClose", t.bank.close())
        t.exec("giveCakeToTroll.wear-whip", t.player.equip, "abyssal_whip")
        t.exec("giveCakeToTroll.bankOpen2", t.bank.open, "kr_bankbooth", 2, { at = { 2724, 3494 } })
        t.exec("giveCakeToTroll.deposit.rune_scimitar", t.bank.deposit, "rune_scimitar", "all")
        t.check("giveCakeToTroll.bankClose2", t.bank.close())
        t.exec("walk-giveCakeToTroll.outOfBank", t.player.walk_route, { { 2726, 3489 }, { 2726, 3484 } })

        -- The climb: Tenzing's fence gate and doors, the stile, both rock pairs, the secret door, the prison
        -- stairs and door, the north stairs and the top exit (eadgar.lua walk_up; quest_troll.rs2:114-198).
        local trip_low = nil
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if trip_low == nil or hp.level < trip_low then trip_low = hp.level end
                if hp.level < 60 and count("shark") > 0 then t.player.inv_op("shark", 1) t.ticks(1) end
            end
        end
        local function walk_check(name, x, z, level, ticks, tol)
            tol = tol or 1
            local wr, wd = t.player.walk_to(x, z, ticks)
            vitals()
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and tt.level == level and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol,
                "walk_to(" .. x .. "," .. z .. ") -> " .. tostring(wr) .. " " .. tostring(wd) .. "; tile " .. reading()
                    .. " (want within " .. tol .. " of " .. x .. "," .. z .. "," .. level .. ")")
        end
        t.exec("goto-giveCakeToTroll.tenzing", t.player.goto_tile, 2826, 3555, 0)
        t.exec("giveCakeToTroll.tenzingGate", t.player.pass_door, { closed = "death_fencegate_l", open = "death_openfencegate_l",
            at = { 2824, 3555, 0 }, near = { 2825, 3555 }, far = { 2823, 3555 } })
        t.exec("giveCakeToTroll.tenzingDoor", t.player.cross_gate, { loc = "death_sherpa_door", at = { 2822, 3555, 0 }, near = { 2823, 3555 },
            far_ok = function(tile) return tile.x >= 2819 and tile.x <= 2822 and tile.z >= 3554 and tile.z <= 3557 end,
            far_desc = "inside Tenzing's house, x 2819-2822 z 3554-3557" })
        t.exec("giveCakeToTroll.wear-climbingboots", t.player.equip, "death_climbingboots")
        t.exec("giveCakeToTroll.tenzingBackDoor", t.player.cross_gate, { loc = "death_sherpa_backdoor", at = { 2820, 3557, 0 }, near = { 2820, 3557 },
            far_ok = function(tile) return tile.z >= 3558 end, far_desc = "north of the back door, z >= 3558" })
        t.exec("giveCakeToTroll.stile", t.player.cross_gate, { loc = "death_fullstyle", at = { 2817, 3562, 0 }, near = { 2817, 3561 },
            far_ok = function(tile) return tile.z >= 3564 end, far_desc = "north of the stile, z >= 3564" })
        t.exec("goto-giveCakeToTroll.rocks", t.player.goto_tile, 2856, 3611, 0)
        t.exec("giveCakeToTroll.rocks1", t.player.cross_gate, { loc = "troll_climbingrocks", at = { 2856, 3612, 0 }, near = { 2856, 3611 },
            far_ok = function(tile) return tile.z > 3612 end, far_desc = "over the south rocks, z > 3612" })
        vitals()
        walk_check("walk-giveCakeToTroll.toRocks2", 2834, 3626, 0, 120)
        t.exec("giveCakeToTroll.rocks2", t.player.cross_gate, { loc = "troll_climbingrocks", at = { 2834, 3628, 0 }, near = { 2834, 3627 },
            far_ok = function(tile) return tile.z >= 3629 end, far_desc = "over the second rocks, z >= 3629" })
        walk_check("walk-giveCakeToTroll.toSecretDoor", 2827, 3646, 0, 60, 2)
        -- The secret door's disguised rock face never renders a hittable pixel (eadgar.lua RUN 4, b55): drive.op sends the
        -- op and the server runs [oploc1,troll_stronghold_entrance]; graded on its exact landing 2823,10050,0.
        local secret = t.player.by_symbol("loc", "troll_stronghold_entrance")
        local sop, sdet = t.drive.op(secret, 1)
        t.ticks(3)
        local sr2, st = t.world.tile()
        t.check("giveCakeToTroll.secretDoor", sr2 == "ok" and st.x == 2823 and st.z == 10050 and st.level == 0,
            "drive.op(troll_stronghold_entrance) -> " .. tostring(sop) .. " " .. tostring(sdet) .. "; tile " .. reading() .. " (want 2823,10050,0)")
        walk_check("walk-giveCakeToTroll.prisonCorridor", 2837, 10090, 0, 90)
        walk_check("walk-giveCakeToTroll.prisonStairs", 2851, 10106, 0, 140)
        t.exec("giveCakeToTroll.prisonStairsUp", t.player.climb, { loc = "troll_stronghold_stairs", at = { 2852, 10106, 0 }, dest = { 2852, 10109, 1 }, slack = 1 })
        vitals()
        t.exec("giveCakeToTroll.prisonDoor", t.player.pass_door, { closed = "troll_stronghold_prison_door_closed",
            at = { 2848, 10107, 1 }, near = { 2848, 10107 }, far = { 2845, 10107 },
            far_ok = function(tile) return tile.x <= 2847 end, far_desc = "west of the prison door, x <= 2847" })
        walk_check("walk-giveCakeToTroll.northStairs", 2841, 10108, 1, 20)
        t.exec("giveCakeToTroll.northStairsUp", t.player.climb, { loc = "troll_stronghold_stairs", at = { 2842, 10108, 1 }, dest = { 2844, 10108, 2 }, slack = 2 }) -- two landings seen on two runs: 2843,10107 and 2845,10108
        vitals()
        walk_check("walk-giveCakeToTroll.topExit", 2837, 10090, 2, 80)
        t.exec("giveCakeToTroll.topExit", t.player.climb, { loc = "troll_stronghold_top_exit_mid", at = { 2838, 10090, 2 }, dest = { 2840, 3690, 0 }, slack = 1 })
        vitals()
        t.check("giveCakeToTroll.climbMargin", trip_low ~= nil and trip_low * 4 >= 99 and count("shark") >= 1,
            "the walk up through the stronghold: lowest hp " .. tostring(trip_low) .. "/99, sharks left " .. count("shark")
                .. " (margin: lowest hp >= a quarter of 99)")
        -- The summit is one walking component (comp.py 2835,3738 -> 2840,3690): an overland hop to the child.
        t.exec("goto-giveCakeToTroll", t.player.goto_tile, 2835, 3738, 0)
        t.exec("giveCakeToTroll", t.player.use_on, "cake", t.player.by_symbol("npc", "fourdiamonds_troll_child_crying"))
        t.exec("giveCakeToTroll-dialog", t.chat.play, {
            "player:Hey there little troll",
            "npc:(sniff)",
        })
        t.ticks(2)
        t.check("giveCakeToTroll-eaten", count("cake") == 0, "cakes held " .. count("cake") .. " (the child took it)")
        t.exec("talkToChildTroll", t.player.talk_to, "fourdiamonds_troll_child_okay", 1)
        t.exec("talkToChildTroll-dialog", t.chat.play, {
            "player:Hello there",
            "npc:-sniff- H-hello",
            "player:Why so sad",
            "npc:It was the bad man",
            "player:Bad man",
            "npc:He said it was",
            "npc:My mommy",
            "npc:Then he did",
            "player:A diamond you say",
            "npc:-sniff- I don't think",
            "npc:I give you my promise",
            "npc:Do we have a deal",
            "choose:Yes",
            "player:Absolutely",
        })
        t.ticks(2)
        local _, stage = t.var.server("varb358_deserttreasure")
        local sub = select(2, t.var.server("varb382_fd_icewarrior_subquest"))
        t.check("leg.4.end", stage == 11 and sub == 2 and count("fd_blood_diamond") == 1,
            "tile " .. reading() .. ", deserttreasure stage read from server = " .. tostring(stage)
            .. " (11 = the hunts), fdvw_subquest " .. tostring(select(2, t.var.server("varb373_fdvw_subquest"))) .. ", fd_icewarrior_subquest " .. tostring(sub)
            .. " (want 2); carrying shadow, smoke and blood diamonds, sharks " .. count("shark"))
                -- LEG 4 END
            end,
        },
        {
            name = "ice_path",
            run = function(t)
        -- LEG 5 BEGIN: enterIceGate
        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function lowest(detail) return tonumber(tostring(detail):match("lowest hp (%d+)/")) end
        -- The Ice Path's cold drains Attack, Strength, Defence, Ranged and Magic a level per ten ticks past the gate
        -- (deserttreasure.rs2:1225 [softtimer,dt_ice_cold], ^dt_cold_interval = 10), and an xp drop no longer undoes it
        -- (LostCity Player.ts:1841-1851 addXp). Quest Helper's Ice diamond panel brings restore potions for it
        -- (quest-helper DesertTreasure.java:685 restorePotions = ItemCollections.RESTORE_POTIONS, which lists
        -- _4DOSESTATRESTORE and _4DOSE2RESTORE); a super restore dose heals the combat stats by 8 + 25% (prayer_potion.rs2
        -- [proc,super_restore_effect]) and gives back 8 + 25% Prayer, which Damis' aura took, for Protect from Melee at Kamil.
        local cold_stats = { "attack", "strength", "defence", "magic" }
        local restore_doses = { "1dose2restore", "2dose2restore", "3dose2restore", "4dose2restore" }
        local function stat_reading()
            local parts = {}
            for _, s in ipairs(cold_stats) do
                local _, v = t.skill.read(s)
                parts[#parts + 1] = s .. " " .. tostring(type(v) == "table" and v.level) .. "/" .. tostring(type(v) == "table" and v.base_level)
            end
            local _, p = t.skill.read("prayer")
            parts[#parts + 1] = "prayer " .. tostring(type(p) == "table" and p.level)
            return table.concat(parts, ", ")
        end
        local function cold_deficit()
            local worst = 0
            for _, s in ipairs(cold_stats) do
                local _, v = t.skill.read(s)
                if type(v) == "table" and v.base_level - v.level > worst then worst = v.base_level - v.level end
            end
            return worst
        end
        local function prayer_now()
            local _, p = t.skill.read("prayer")
            return type(p) == "table" and p.level or 0
        end
        local function drink_restores(name, want_prayer)
            local before = stat_reading()
            local drunk = {}
            for _ = 1, 4 do
                if cold_deficit() < 10 and prayer_now() >= (want_prayer or 0) then break end
                local dose = nil
                for _, d in ipairs(restore_doses) do
                    if count(d) > 0 then dose = d break end
                end
                if dose == nil then break end
                local result = t.player.inv_op(dose, 1)
                t.ticks(2)
                drunk[#drunk + 1] = dose .. " (" .. tostring(result) .. ")"
            end
            t.check(name, cold_deficit() < 10 and prayer_now() >= (want_prayer or 0), "drank " .. #drunk .. " restore dose(s) [" .. table.concat(drunk, ", ") .. "]: "
                .. before .. " -> " .. stat_reading() .. " (want every drained stat within 10, prayer >= " .. tostring(want_prayer or 0) .. ")")
        end
        local function margin(name, fight, low)
            -- food left is reported, not graded: Kamil now hits as recorded and this leg's pack is full of kit
            t.check(name, low ~= nil and low * 4 >= 99,
                fight .. ": lowest hp " .. tostring(low) .. "/99, sharks left " .. count("shark") .. " (margin: lowest hp >= a quarter of 99)")
        end
        -- The ice gate (icegate_left 2838,3739): [label,dt_ice_gate_go] squeezes the player to ^dt_ice_gate_e 2839,3739.
        t.exec("goto-enterIceGate", t.player.goto_tile, 2836, 3739, 0)
        -- The first click is the child's three pages (4628 [1012]..[1020]; fd_trollchild_intro 0 -> 1), the second squeezes through.
        -- UNRUN: the click and the pages are reasoned from dt_ice.rs2, not run.
        t.exec("enterIceGate.firstClick", t.player.click_loc, "icegate_left", 1, { at = { 2838, 3739, 0 } })
        t.exec("enterIceGate.firstClick-dialog", t.chat.play, {
            "npc:Oh, I'm sorry",
            "npc:I'm afraid I must have frozen",
            "npc:You are much smaller",
        })
        t.exec("enterIceGate", t.player.cross_gate, { loc = "icegate_left", at = { 2838, 3739, 0 }, near = { 2837, 3739 },
            far_ok = function(tile) return tile.x >= 2839 end, far_desc = "east of the ice gate (^dt_ice_gate_e 2839,3739)" })
        -- The spiked boots can only be worn on the far side of the gate (death_locs.rs2 [opheld2,death_spikedboots]).
        t.exec("wear-spikedboots", t.player.equip, "death_spikedboots")

        -- 22 aggressive ice trolls swarm the 99-hitpoint account (two deaths with 16 sharks eaten): the seven types are held passive
        -- (docs/quest_authoring/gaps-combat.md ::passive) so each one is still attacked and killed by the whip, one at a time.
        local troll_passive = true
        for index = 1, 7 do
            if t.cheat("::passive trollrescue_icetroll_melee" .. index) ~= "ok" then troll_passive = false end
        end
        t.check("killIceTrolls-passive", troll_passive, "::passive on the seven ice troll types: they no longer swarm the player but still take hits and die (test affordance, gaps-combat)")
        local sharks_at_trolls = count("shark")
        local trolls_lowest, trolls_ticks = nil, 0
        for round = 1, 12 do
            if select(2, t.var.server("varb378_fd_icewarrior_trollskilled")) >= 5 then break end
            if cold_deficit() >= 30 then drink_restores("drinkRestore-trolls" .. round) end
            local engaged = "no_row"
            for _, sym in ipairs({ "trollrescue_icetroll_melee1", "trollrescue_icetroll_melee2", "trollrescue_icetroll_melee3",
                "trollrescue_icetroll_melee4", "trollrescue_icetroll_melee5", "trollrescue_icetroll_melee6", "trollrescue_icetroll_melee7" }) do
                engaged = t.player.attack(sym, 2, 20)
                if engaged == "ok" then break end
            end
            local _, troll_detail = t.npc.await_dead_engaged(60, 4, { eat = { item = "shark", below = 75 } })
            local low = lowest(troll_detail)
            if low and (trolls_lowest == nil or low < trolls_lowest) then trolls_lowest = low end
            trolls_ticks = trolls_ticks + (tonumber(tostring(troll_detail):match("dead after (%d+) tick")) or 0)
        end
        t.ticks(2)
        t.check("killIceTrolls", select(2, t.var.server("varb378_fd_icewarrior_trollskilled")) >= 5,
            "ice trolls killed with the whip: fd_icewarrior_trollskilled = " .. tostring(select(2, t.var.server("varb378_fd_icewarrior_trollskilled"))) .. " (needs 5)")
        margin("killIceTrolls-margin", "the ice trolls (sharks at the start " .. tostring(sharks_at_trolls) .. ", " .. trolls_ticks .. " tick(s) to kills)", trolls_lowest)
        -- The cave entrance (3x2 multiloc at 2868,3718, deserttreasure.rs2:1290) -> ^dt_ice_cave_in 2874,3720.
        t.exec("goto-enterTrollCave", t.player.goto_tile, 2866, 3720, 0)
        t.exec("enterTrollCave", t.player.cross_gate, { loc = "trollrescue_troll_cave_entrance", at = { 2868, 3718, 0 }, near = { 2867, 3720 },
            far_ok = function(tile) return tile.x == 2874 and tile.z == 3720 end, far_desc = "through the cave, 2874,3720 (^dt_ice_cave_in)" })
        -- Fire Blast needs 59 Magic, Protect from Melee needs prayer points (prayer does not regenerate; Damis drained it).
        drink_restores("drinkRestore-killKamil", 30)
        -- Protect from Melee at Kamil (Quest Helper DesertTreasure.java:540 "Get into melee distance and protect from melee"):
        -- he swings slash for up to 22 (icediamond_icewarrior strength 80 + 100, deserttreasure.npc), a prayed npc melee hit
        -- is 0 (combat_stats.rs2 playerhit_n_melee_apply). UP before the walk to him, so it is on for his first swing.
        local function protect_melee(name, want)
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            local tab_result, wr = "ok", "ok"
            if now ~= want then
                tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local pw
                wr, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(2)
            end
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, pr = t.skill.read("prayer")
            t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on)
                .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
        end
        protect_melee("killKamil-protectMelee", 1)
        local sharks_at_kamil = count("shark")
        -- Out of the cave onto 2878,3757, where the recorder stopped (4628 [1510]): the zone there sends the line
        -- two ticks on and Kamil is added for the player.
        t.exec("goto-killKamil.caveMouth", t.player.goto_tile, 2878, 3757, 0)
        t.ticks(4)
        t.exec("killKamil-presence", t.msg.expect, "You can feel an evil presence nearby...")
        t.exec("goto-killKamil", t.player.goto_tile, 2863, 3753, 0)
        local _, magic_at_kamil = t.skill.read("magic")
        local magic_now = type(magic_at_kamil) == "table" and magic_at_kamil.level or nil
        t.check("killKamil-magic", (magic_now or 0) >= 59, "magic " .. tostring(magic_now) .. "/"
            .. tostring(type(magic_at_kamil) == "table" and magic_at_kamil.base_level) .. " before the first Fire Blast (needs 59)")
        -- UNRUN: Kamil is no longer a world spawn: dt_ice.rs2's zone (2872..2879 x 3752..3759) adds him for the
        -- player at 2855,3757 (dt_kamil.rs2, 4628 [1512]); the route from the cave mouth is assumed to cross it.
        -- His freeze is a two-tick delay, and a click inside it is dropped (4628 [1569]), so the cast is clicked
        -- again every 6 ticks (the recorder clicked 2..4 times a cast) rather than once a minute.
        t.exec("killKamil-engage", t.player.cast, "fire_blast", "icediamond_icewarrior", 8)
        local kamil_result, kamil_detail = "timeout", ""
        local kamil_lowest = nil
        for round = 1, 120 do
            kamil_result, kamil_detail = t.npc.await_dead_engaged(6, 2, { eat = { item = "shark", below = 75 } })
            local low = lowest(kamil_detail)
            if low and (kamil_lowest == nil or low < kamil_lowest) then kamil_lowest = low end
            if kamil_result == "ok" then break end
            t.player.cast("fire_blast", "icediamond_icewarrior", 8)
        end
        t.check("killKamil", kamil_result == "ok", "killed Kamil with fire_blast: " .. tostring(kamil_result) .. " " .. tostring(kamil_detail))
        margin("killKamil-margin", "Kamil (sharks at the start " .. tostring(sharks_at_kamil) .. ")", kamil_lowest)
        protect_melee("killKamil-prayerOff", 0)
        t.ticks(2)
        t.check("killKamil-stage", select(2, t.var.server("varb382_fd_icewarrior_subquest")) == 3,
            "fd_icewarrior_subquest = " .. tostring(select(2, t.var.server("varb382_fd_icewarrior_subquest"))) .. " a tick after the corpse (3 = Kamil dead)")

        -- Kamil's path runs on to the ice ledge (one walking component: reach.py 204 tiles, every door shut).
        t.exec("goto-climbOnToLedge", t.player.goto_tile, 2837, 3805, 0)
        t.exec("climbOnToLedge", t.player.climb, { loc = "trollrescue_blankmodel", at = { 2837, 3804, 0 }, dest = { 2837, 3803, 1 }, slack = 1 }) -- UNRUN: 4628 [2381] lands on 2837,3803, level 1 (needs the spiked boots worn)
        -- The Ice Path up to the small gate, on the ledge's own floor.
        local wr, wd = t.player.goto_tile(2853, 3811, 1)
        local pr, pt = t.world.tile()
        t.check("walk-goThroughPathGate", pr == "ok" and pt.level == 1 and math.abs(pt.x - 2853) <= 1 and math.abs(pt.z - 3811) <= 1,
            "goto 2853,3811 -> " .. tostring(wr) .. " " .. tostring(wd) .. "; tile " .. reading() .. " (want beside the path gate, level 1)")
        t.exec("goThroughPathGate", t.player.climb, { loc = "icegate_right_small", at = { 2853, 3810, 1 }, dest = { 2852, 3809, 2 }, slack = 0 }) -- UNRUN: 4628 [2535]: the squeeze is eight ticks, then 2852,3809 on level 2
        -- The long walk to the blocks drains again; drink only if the cold has taken twenty levels since Kamil.
        if cold_deficit() >= 20 then drink_restores("drinkRestore-breakIce") end
        local br, bd = t.player.walk_to(2828, 3808, 60)
        local b_r, bt = t.world.tile()
        t.check("walk-breakIce1", b_r == "ok" and bt.level == 2 and math.abs(bt.x - 2828) <= 2 and math.abs(bt.z - 3808) <= 2,
            "walk_to(2828,3808) -> " .. tostring(br) .. " " .. tostring(bd) .. "; tile " .. reading() .. " (want by the ice blocks, level 2)")
        -- A block does not die: at 0 it becomes the parent inside it (troll_block_1 -> the troll father, 4628
        -- [2685]), so the fight is read from the block's own varbit, never waited out as a death (a cast after
        -- the change would be a cast at the freed troll).
        t.player.cast("fire_blast", "troll_block_1", 8)
        for round = 1, 8 do
            t.ticks(3)
            if select(2, t.var.server("varb380_fd_icewarrior_dadfree")) == 1 then break end
            t.player.cast("fire_blast", "troll_block_1", 8)
        end
        t.ticks(2)
        t.check("breakIce1-dead", select(2, t.var.server("varb380_fd_icewarrior_dadfree")) == 1,
            "the first block emptied: dadfree " .. tostring(select(2, t.var.server("varb380_fd_icewarrior_dadfree"))))
        t.exec("breakIce2", t.player.cast, "fire_blast", "troll_block_2", 8)
        for round = 1, 6 do
            t.ticks(3)
            if select(2, t.var.server("varb381_fd_icewarrior_mumfree")) == 1 then break end
            t.player.cast("fire_blast", "troll_block_2", 8)
        end
        t.ticks(2)
        t.check("breakIce2-dead", select(2, t.var.server("varb381_fd_icewarrior_mumfree")) == 1 and select(2, t.var.server("varb380_fd_icewarrior_dadfree")) == 1,
            "both blocks shattered by fire_blast: dadfree " .. tostring(select(2, t.var.server("varb380_fd_icewarrior_dadfree"))) .. ", mumfree " .. tostring(select(2, t.var.server("varb381_fd_icewarrior_mumfree"))))
        t.ticks(2)
        t.check("breakIce-stage", select(2, t.var.server("varb382_fd_icewarrior_subquest")) == 4,
            "fd_icewarrior_subquest = " .. tostring(select(2, t.var.server("varb382_fd_icewarrior_subquest"))) .. " (4 = both parents free)")
        -- UNRUN: the reunion is one run of pages that starts by itself the tick after the second block empties
        -- (4628 [2685]), fades at the last click ([2733]), moves the player beside the child at T+3 ([2736]) and goes on
        -- with the child's pages to "Here ya go" ([2750]) and the player's last line ([2758]); dt_ice.rs2 [queue,dt_ice_reunion].
        -- The pack needs a free slot at that point (the diamond is laid on the floor when it is full).
        t.exec("reunion-dialog", t.chat.play, {
            "npc:Phew",
            "npc:Yes, I thought",
            "player:He must have",
            "npc:You mean",
            "npc:And how did",
            "player:Your son",
            "npc:Ooohhh",
            "npc:Yes, but",
            "npc:If he'd",
            "player:Wait",
            "npc:Don't you",
            "npc:Now, now",
            "npc:Let's get out",
        })
        -- The fade on that click ([2733]), the move beside the child three ticks on ([2736]), then his page ([2737]).
        t.ticks(6)
        t.exec("reunion-dialog2", t.chat.play, {
            "npc:Mommy",
            "npc:That's right son",
            "npc:It has been",
            "npc:That's right son",
            "npc:RAW MACKEREL",
            "npc:Here ya go",
            "player:Don't worry",
        })
        -- With the other three already held, the fourth diamond brings the player's own line (4629 [3490]).
        t.ticks(3)
        t.exec("reunion-allFour", t.chat.play, { "player:I should take all four diamonds to Eblis" })
        local hr, ht = t.world.tile()
        t.check("reunion-home", hr == "ok" and ht.level == 0 and ht.x == 2836 and ht.z == 3739,
            "after the reunion: " .. reading() .. " (want 2836,3739,0, ^dt_ice_reunion_tile)")
        t.ticks(2)
        local _, stage = t.var.server("varb358_deserttreasure")
        t.check("leg.5.end", count("fd_icediamond") == 1 and stage == 12 and select(2, t.var.server("varb382_fd_icewarrior_subquest")) == 5,
            "tile " .. reading() .. ", deserttreasure stage read from server = " .. tostring(stage) .. " (12 = all four diamonds held; want subquest 5)"
            .. ", fd_icewarrior_subquest " .. tostring(select(2, t.var.server("varb382_fd_icewarrior_subquest")))
            .. "; ice diamond held " .. tostring(count("fd_icediamond")) .. ", sharks " .. tostring(count("shark")))
        -- LEG 5 END
            end,
        },
        {
            name = "pyramid",
            run = function(t)
        -- LEG 6 BEGIN: placeSmoke
        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function column(name) return select(2, t.var.server(name)) end
        local function free_slots()
            local n = 0
            for i = 0, 27 do
                local r, sl = t.inv.slot(i)
                if r == "ok" and type(sl) == "table" and sl.name == "" then n = n + 1 end
            end
            return n
        end
        -- Off the summit by Lumbridge Teleport, the Al Kharid bank for the pyramid's food (Quest Helper: bring food),
        -- and the Shantay Pass.
        t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = "placeSmoke.lumbridgeTeleport",
            runes = { { "airrune", 3 }, { "earthrune", 1 }, { "lawrune", 1 } }, where = "Lumbridge" })
        t.exec("goto-placeSmoke.alKharidBank", t.player.goto_tile, 3276, 3167, 0)
        t.exec("walk-placeSmoke.intoBank", t.player.walk_route, { { 3269, 3166 } })
        t.exec("placeSmoke.bankOpen", t.bank.open, "bankbooth", 2, { at = { 3268, 3166 } })
        for _, d in ipairs({ "death_spikedboots", "4dose2restore", "3dose2restore", "2dose2restore", "1dose2restore", "vial_empty" }) do
            if count(d) > 0 then t.exec("placeSmoke.deposit." .. d, t.bank.deposit, d, "all") end
        end
        local room = free_slots() - 2
        t.exec("placeSmoke.withdraw.water_skin4", t.bank.withdraw, "water_skin4", 1)
        room = room - 1
        if room > 0 then t.exec("placeSmoke.withdraw.shark", t.bank.withdraw, "shark", math.min(room, 14)) end
        t.check("placeSmoke.bankClose", t.bank.close())
        t.exec("walk-placeSmoke.outOfBank", t.player.walk_route, { { 3276, 3167 } })
        t.exec("goto-placeSmoke.shantay", t.player.goto_tile, 3304, 3123, 0)
        local coins0, pass0 = count("coins"), count("shantay_pass")
        t.exec("placeSmoke.buyPass", t.player.talk_to, "shantay", 4)
        t.exec("placeSmoke.buyPass-dialog", t.chat.play, { "objbox:You purchase a Shantay Pass." })
        t.check("placeSmoke.buyPass-paid", count("shantay_pass") == pass0 + 1 and count("coins") == coins0 - 5,
            "shantay_pass " .. pass0 .. " -> " .. count("shantay_pass") .. ", coins " .. coins0 .. " -> " .. count("coins"))
        t.exec("walk-placeSmoke.toDoorway", t.player.walk_route, { { 3304, 3118 } })
        t.exec("placeSmoke.doorway", t.player.click_loc, "shantay_pass_henge_doorway", 1)
        local pwo = t.ui.await_open("cws_warning_10", 15)
        local pwr, pww = t.ui.widget("cws_warning_10:warn1")
        t.check("placeSmoke.doorway-dialog", pwo == "ok" and pwr == "ok" and select(1, t.ui.invoke(pww, 1)) == "ok",
            "desert warning " .. tostring(pwo) .. ", warn1 " .. tostring(pwr))
        t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and tt.z < 3116 end, note = "placeSmoke.doorway" }, 10)
        local dr, dt = t.world.tile()
        t.check("placeSmoke.doorway-landed", dr == "ok" and dt.x == 3304 and dt.z < 3116 and count("shantay_pass") == pass0,
            "after the doorway: " .. reading() .. ", shantay_pass " .. count("shantay_pass"))

        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("leg6.snapshot", snapshot_result == "ok", "magic xp before the hand-in: " .. tostring(snapshot and snapshot.magic and snapshot.magic.experience))

        -- The four obelisks: the diamond is used on the pillar (deserttreasure.rs2:1889-1898 oplocu).
        local function place(step, stand_x, stand_z, sym, varname)
            t.exec("goto-" .. step, t.player.goto_tile, stand_x, stand_z, 0)
            local pillar = t.player.by_symbol("loc", sym)
            t.check(step .. "-pillar", pillar ~= nil, sym .. " resolved: " .. tostring(pillar and pillar.id))
            local diamond = ({ varb390_fd_column_blood = "fd_blood_diamond", varb387_fd_column_fire = "fd_diamond_fire", varb389_fd_column_ice = "fd_icediamond", varb388_fd_column_shadow = "fd_dark_diamond" })[varname]
            local r, d = t.player.use_on(diamond, pillar)
            t.ticks(3)
            t.check(step, column(varname) == 1 and count(diamond) == 0, "use_on " .. diamond .. " -> " .. tostring(r) .. " " .. tostring(d) .. "; " .. varname .. " = "
                .. tostring(column(varname)) .. " read from the server, " .. diamond .. " held " .. count(diamond) .. "; messages: " .. tostring(t.msg.last(2)))
        end
        -- The recorded order: blood [4374], smoke [4407], ice [4426], shadow [4444].
        place("placeBlood", 3221, 2907, "desert_treasure_oblix_a", "varb390_fd_column_blood")
        place("placeSmoke", 3245, 2907, "desert_treasure_oblix_b", "varb387_fd_column_fire")
        place("placeIce", 3245, 2884, "desert_treasure_oblix_c", "varb389_fd_column_ice")
        place("placeShadow", 3223, 2885, "desert_treasure_oblix_d", "varb388_fd_column_shadow")
        t.check("placeShadow-stage", select(2, t.var.server("varb358_deserttreasure")) == 13, "deserttreasure = " .. tostring(select(2, t.var.server("varb358_deserttreasure"))) .. " once the fourth diamond is absorbed (13 = pyramid)")

        -- The pyramid: the exterior ladder (desert_laddertop, maplink src 3233,2896/2898 -> 2913,4954,3) and three
        -- ladders down; a trap can throw the player back outside (3233,2886, ^dt_pyramid_outside), so the way in retries.
        -- The way up is the staircase on the north face (x 3232-3233, z 2901-2910 open on the map) to the pyramid
        -- entrance four_diamonds_door_1 (3233,2899, [label,dt_pyramid_door]: sealed until all four diamonds are in,
        -- then it carries the player through), then the ladder top (maplink src 3233,2898 -> 2913,4954,3).
        -- The floors' mummies and scarabs are held passive (test affordance, docs/quest_authoring/gaps-combat.md):
        -- a pit trap restarts the descent (four falls on this roll, 1,400 ticks inside), and their fights are not
        -- what this leg reads. test/quests/_dt_pyramid.lua reads the hazards' own frames.
        local pyr_passive = true
        for _, sym in ipairs({ "deserttreasure_mummy_1", "deserttreasure_mummy_2", "deserttreasure_mummy_3", "deserttreasure_mummy_4", "deserttreasure_mummy_5", "scarab_swarm" }) do
            if t.cheat("::passive " .. sym) ~= "ok" then pyr_passive = false end
        end
        t.check("enterPyramid-passive", pyr_passive, "::passive on the pyramid's mummies and scarabs")
        t.exec("goto-enterPyramid", t.player.goto_tile, 3233, 2909, 0)
        t.exec("walk-enterPyramid.northStairs", t.player.walk_route, { { 3233, 2905 }, { 3233, 2900 } })
        t.exec("enterPyramid.entrance", t.player.cross_gate, { loc = "four_diamonds_door_1", at = { 3233, 2899, 0 }, near = { 3233, 2900 },
            far_ok = function(tile) return tile.z == 2898 end, far_desc = "through the entrance to 3233,2898 (4629 [4481])" })
        t.exec("enterPyramid", t.player.climb, { loc = "desert_laddertop", at = { 3233, 2897, 0 }, src = { 3233, 2898 }, dest = { 2913, 4954, 3 }, slack = 2 })
        -- Each floor's ladder down (maplink rows: desert_laddertop3_2 2909,4964,3 -> 2846,4964,2; desert_laddertop2_1
        -- 2846,4973,2 -> 2782,4972,1; desert_laddertop1_0 2784,4941,1 -> 3233,9293,0). The floors are mazes (the
        -- press walks 80+ ticks) with scarabs and mummies (deserttreasure.rs2 [softtimer,dt_pyramid_hazard]) that can
        -- stop a walk: a press that did not land is pressed again, graded on the level and the landing.
        -- A pit trap throws the player out to 3233,2887 (4629 [4653], [4805]): then the way in is walked again
        -- and the floors start over, as the recorder's did twice.
        local function inside()
            local r, tt = t.world.tile()
            return r == "ok" and not (tt.level == 0 and tt.z < 3000)
        end
        local function reenter()
            t.ticks(14)
            t.exec("reenter.goto", t.player.goto_tile, 3233, 2900, 0)
            t.exec("reenter.entrance", t.player.cross_gate, { loc = "four_diamonds_door_1", at = { 3233, 2899, 0 }, near = { 3233, 2900 },
                far_ok = function(tile) return tile.z == 2898 end, far_desc = "through the entrance" })
            t.exec("reenter.ladder", t.player.climb, { loc = "desert_laddertop", at = { 3233, 2897, 0 }, src = { 3233, 2898 }, dest = { 2913, 4954, 3 }, slack = 2 })
        end
        local ladders = {
            { "goDownFromFirstFloor", "desert_laddertop3_2", { 2909, 4964, 3 }, { 2846, 4964, 2 } },
            { "goDownFromSecondFloor", "desert_laddertop2_1", { 2846, 4973, 2 }, { 2782, 4972, 1 } },
            -- The third floor's ladder is pressed from 2784,4954: from the landing the press finds no route and
            -- sends nothing, and a character left standing where its walk ends is taken by a pit every time.
            { "goDownFromThirdFloor", "desert_laddertop1_0", { 2784, 4941, 1 }, { 3233, 9293, 0 }, { 2784, 4954 } },
        }
        local falls, li, guard = 0, 1, 0
        while li <= #ladders and guard < 80 do
            guard = guard + 1
            if not inside() then
                falls = falls + 1
                reenter()
                li = 1
            end
            local row = ladders[li]
            if row[5] then
                t.player.walk_to(row[5][1], row[5][2], 120)
            end
            local r, d = t.player.climb({ loc = row[2], at = row[3], dest = row[4], slack = 2, ticks = 200 })
            local lr, lt = t.world.tile()
            if r == "ok" and lr == "ok" and lt.level == row[4][3] and inside() then
                t.check(row[1], true, "landed " .. reading())
                li = li + 1
            elseif inside() then
                t.ticks(2)
            end
        end
        t.check("pyramidFloors", li > #ladders, "ladders done " .. (li - 1) .. " of 3, pit falls " .. falls .. ", now " .. reading())

        t.exec("goto-enterMiddleOfPyramid", t.player.goto_tile, 3234, 9326, 0)
        t.exec("enterMiddleOfPyramid", t.player.cross_gate, { loc = "dt_ancient_temple_door_open", at = { 3234, 9324, 0 }, near = { 3234, 9325 },
            far_ok = function(tile) return tile.z < 9324 end, far_desc = "the central room, south of the temple door" })
        t.exec("talkToAzz", t.player.talk_to, "azzanadra_real", 1)
        -- 4629 [5362]..[5464]: eleven pages, stage 14 with the eighth, the scroll on the last one's click.
        t.exec("talkToAzz-dialog", t.chat.play, {
            "npc:I knew they could not trap me here for long! Well done, friend. Tell me, how goes the war?",
            "player:War?",
            "npc:You do not know of the war for our survival?",
            "npc:Tell me, what news of great Paddewwa?",
            "player:Er... sorry, I've never heard of them...",
            "npc:No! My lord... what has become of you?",
            "npc:My thanks to you friend for your help in freeing me",
            "npc:How long have I been trapped here?",
            "npc:My friend, for your efforts in freeing me, I offer you the gift of knowledge.",
            "npc:They will replace the knowledge you previously had",
            "npc:I trust that we shall meet again, but now I must leave you.",
        })
        t.ticks(3)
        t.quest.expect_complete()
        -- The scroll's close [5474]: the line and the Ancient spellbook.
        t.exec("complete.wisdom", t.msg.expect, "A strange wisdom has filled your mind...")
        t.check("complete.spellbook", select(2, t.var.server("varb4070_spellbook")) == 1, "spellbook = " .. tostring(select(2, t.var.server("varb4070_spellbook"))) .. " (1 = Ancient)")
        -- documented 20,006.9 Magic XP (dt_magic_reward_xp = 200069 tenths). The whole-unit readings differ by 20007 or 20006
        -- with the half point the run's spells left in the total (Fire Blast 34.5, Water Blast 28.5).
        local _, magic_after = t.skill.read("magic")
        local magic_before = snapshot and snapshot.magic and snapshot.magic.experience
        local magic_gain = (type(magic_after) == "table" and magic_after.experience or 0) - (magic_before or 0)
        t.check("reward.magic_xp", magic_before ~= nil and (magic_gain == 20007 or magic_gain == 20006),
            "magic: before=" .. tostring(magic_before) .. " after=" .. tostring(type(magic_after) == "table" and magic_after.experience) .. " delta=" .. magic_gain .. " (20,006.9 documented)")
        t.finish(0)
        return
        -- LEG 6 END
            end,
        },
    },
}
