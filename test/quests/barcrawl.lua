-- Alfred Grimhand's Barcrawl (miniquest), played the way RSProx index 4420
-- plays it: the guard's money answer, then "a barbarian", the card read, the
-- journal, the ten bars in the recorded order (Blue Moon, Jolly Boar, Rising
-- Sun, Rusty Anchor, Karamja, Dead Man's Chest, Flying Horse, Forester's Arms,
-- Blurberry, Dragon Inn), the too-drunk read, the hand-in with vial smashing
-- turned on, the second talk, and the gate (index 4421 [6..11]). Every page
-- asserted is a recorded page (docs/rsprox-analysis/quests/barcrawl.md).
--
-- The player has the recorder's levels where the drinks lower them, so the
-- drains can be compared with the recording's numbers: each stat falls by
-- 5 + 5% of its CURRENT level (Fire Brandy's attack by 6 + 5%). A natural
-- restore can land inside a drink, so a stat may come back one higher.
-- The miniquest ends with no scroll ([1350]), so the end is graded on the
-- status, the card and the varbits.
local JOURNAL_315 = {
    "The guard at the Barbarian Outpost said I could enter the",
    "outpost if I proved myself to be a barbarian by completing",
    "the Alfred Grimhand Barcrawl, which was first completed by",
    "Alfred Grimhand.",
    "He gave me a barcrawl card to track my progress.",
    "I must visit the Blue Moon Inn.",
    "I must visit Blurberry's Bar.",
    "I must visit the Dead Man's Chest.",
    "I must visit the Dragon Inn.",
    "I must visit the Flying Horse Inn.",
    "I must visit the Forester's Arms.",
    "I must visit the Jolly Boar Inn.",
    "I must visit the Karamja Spirits Bar.",
    "I must visit the Rising Sun Inn.",
    "I must visit the Rusty Anchor Inn.",
}
local JOURNAL_1313 = {
    "The guard at the Barbarian Outpost said I could enter the",
    "outpost if I proved myself to be a barbarian by completing",
    "the Alfred Grimhand Barcrawl, which was first completed by",
    "Alfred Grimhand.",
    "He gave me a barcrawl card to track my progress.",
    "I drank Uncle Humphrey's Gutrot at the Blue Moon Inn.",
    "I drank a Fire Toad Blast at Blurberry's Bar.",
    "I drank the old Supergrog at the Dead Man's Chest.",
    "I drank the Fire Brandy at the Dragon Inn.",
    "I drank the Heart Stopper at the Flying Horse Inn.",
    "I drank the Liverbane Ale at the Forester's Arms.",
    "I drank the Olde Suspiciouse at the Jolly Boar Inn.",
    "I drank the Ape Bite Liqueur at the Karamja Spirits Bar.",
    "I drank the Hand of Death cocktail at the Rising Sun Inn.",
    "I drank the Black Skull Ale at the Rusty Anchor Inn.",
    "I got quite drunk.",
}
-- The card at [307] and [1199], rows 4..13.
local CARD_307 = {
    "Blue Moon Inn (Varrock)", "Blurberry's Bar (Gnome Stronghold)", "Dead Man's Chest (Brimhaven)",
    "Dragon Inn (Yanille)", "Flying Horse Inn (Ardougne)", "Forester's Arms (Seers' Village)",
    "Jolly Boar Inn (north-east of Varrock)", "Karamja Spirits Bar (Musa Point)",
    "Rising Sun Inn (Falador)", "Rusty Anchor Inn (Port Sarim)",
}
local CARD_1199 = {
    "Blue Moon Inn - Completed!", "Blurberry's Bar - Completed!", "Dead Man's Chest - Completed!",
    "Dragon Inn (Yanille)", "Flying Horse Inn - Completed!", "Forester's Arms - Completed!",
    "Jolly Boar Inn - Completed!", "Karamja Spirits Bar - Completed!",
    "Rising Sun Inn - Completed!", "Rusty Anchor Inn - Completed!",
}

return {
    id = "barcrawl",
    fixture = "fresh_lumbridge.ini",
    max_frames = 160000,
    setup = {
        "::clearinv",
        "::give coins 300",
        "::setlevel attack 41",
        "::setlevel strength 30",
        "::setlevel hitpoints 26",
        "::setlevel prayer 8",
        "::setlevel magic 19",
        "::setlevel cooking 18",
        "::setlevel woodcutting 14",
        "::setlevel fishing 27",
        "::setlevel firemaking 30",
        "::setlevel crafting 20",
        "::setlevel smithing 29",
        "::setlevel mining 18",
        "::setlevel herblore 10",
        "::tele 0_39_55_48_48",
    },

    run = function(t)
        t.ticklog.start()
        t.ticks(3)

        local function plain(s)
            return (string.gsub(string.gsub(s or "", "<[^>]*>", ""), "^%s*(.-)%s*$", "%1"))
        end
        local function journal(name, want)
            local r, j = t.ui.journal_open("Alfred Grimhand's Barcrawl")
            local lines = (r == "ok" and j and j.lines) or {}
            if r == "ok" then
                t.ui.journal_close()
            end
            local diff = nil
            for i = 1, math.max(#want, #lines) do
                if lines[i] ~= want[i] then
                    diff = "row " .. i .. ": want '" .. tostring(want[i]) .. "' read '" .. tostring(lines[i]) .. "'"
                    break
                end
            end
            t.check(name, r == "ok" and diff == nil, tostring(r) .. " " .. #lines .. " rows; " .. (diff or "every row matches"))
        end
        local function card(name, want)
            t.exec(name .. ".read", t.player.inv_op, "barcrawl_card", 1)
            t.expect(name .. ".open", t.ui.await_open("messagescroll", 5))
            local diff = nil
            local _, title = t.ui.text("messagescroll:messcroll2")
            if plain(title) ~= "The Official Alfred Grimhand Barcrawl!" then
                diff = "title '" .. tostring(title) .. "'"
            end
            for i, row in ipairs(want) do
                local _, text = t.ui.text("messagescroll:messcroll" .. (i + 3))
                if diff == nil and plain(text) ~= row then
                    diff = "row " .. (i + 3) .. ": want '" .. row .. "' read '" .. tostring(text) .. "'"
                end
            end
            t.check(name .. ".rows", diff == nil, diff or "every row matches")
            t.ui.close_modal()
            t.ticks(1)
        end
        local function level(stat)
            local r, reading = t.skill.read(stat)
            return r == "ok" and reading.level or -1
        end
        -- Each named stat fell by c + current * 5 / 100 (or one less: a restore).
        local function drained(name, before, drains)
            for stat, c in pairs(drains) do
                local want = before[stat] - math.min(before[stat], c + math.floor(before[stat] * 5 / 100))
                local got = level(stat)
                t.check(name .. ".drain." .. stat, got == want or got == want + 1,
                    stat .. " " .. before[stat] .. " -> " .. got .. ", want " .. want)
            end
        end
        local function levels(stats)
            local out = {}
            for stat, _ in pairs(stats) do
                out[stat] = level(stat)
            end
            return out
        end
        local function signed(name, bit)
            local r = t.await({
                level = function()
                    local vr, v = t.var.server("varp77_barcrawl")
                    return vr == "ok" and math.floor(v / (2 ^ bit)) % 2 == 1
                end,
                note = name .. ".signed",
            }, 20)
            t.expect(name .. ".signed", r)
        end
        local function bar(name, tile, npc, pages, bit, drains)
            t.cheat("::tele " .. tile)
            t.ticks(3)
            local before = levels(drains or {})
            t.exec(name .. ".talk", t.player.talk_to, npc, 1)
            t.exec(name .. ".pages", t.chat.play, pages)
            signed(name, bit)
            if drains then
                drained(name, before, drains)
            end
            t.ticks(4)
        end

        -- A multinpc stands in its SHELL's stance (multinpc_stance_audit.py on 4420:
        -- barbguard1 states barrow_dharok_ready, 2065, over rungs that state none).
        local function stance(name)
            local r, d, pose = t.npc.pose("barbguard1")
            t.check(name, r == "ok" and pose ~= nil and pose.readyanim == 2065, tostring(d))
        end

        -- ---- The guard (index 4420 [278..304]) ----
        stance("guard.stance.precrawl")
        t.exec("guard.talk", t.player.talk_to, "barbguard1", 1)
        t.exec("guard.money", t.chat.play, {
            "npc:Oi, whaddya want?",
            "choose:I want some money.",
            "player:I want some money.",
            "npc:Do I look like a bank to you?",
            "end",
        })
        t.exec("guard.talk2", t.player.talk_to, "barbguard1", 1)
        t.exec("guard.start", t.chat.play, {
            "npc:Oi, whaddya want?",
            "choose:I want to come through this gate.",
            "player:I want to come through this gate.",
            "npc:Barbarians only. Are you a barbarian? You don't look",
            "choose:Looks can be deceiving, I am in fact a barbarian.",
            "player:Looks can be deceiving, I am in fact a barbarian.",
            "npc:If you're a barbarian you need to be able to drink like",
            "npc:I have the perfect challenge for you... The Alfred",
            "objbox:The guard hands you a Barcrawl card.",
            "npc:Take that card to each of the bars named on it.",
            "npc:They'll give you their strongest drink and sign your",
            "end",
        })
        t.expect("start.varp", t.var.expect("varp77_barcrawl", 1))
        t.expect("start.progress", t.var.expect("varb13714_barcrawl_progress", 1))
        t.expect("start.card", t.inv.expect_has("barcrawl_card", 1))

        card("card.307", CARD_307)
        journal("journal.315", JOURNAL_315)

        -- ---- The bars ----
        bar("bluemoon", "0_50_53_26_4", "bluemoon_bartender", {
            "npc:What can I do yer for?",
            "choose:I'm doing Alfred Grimhand's Barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Oh no not another of you guys.",
            "npc:You're going to have to pay 50 gold for the Uncle",
            "end",
        }, 3, { attack = 5, defence = 5, strength = 5, smithing = 5 })

        bar("jollyboar", "0_51_54_16_32", "jollyboar_bartender", {
            "npc:Can I help you?",
            "choose:I'm doing Alfred Grimhands Barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Ah, there seems to be a fair few doing that one these",
            "end",
        }, 9, { attack = 5, defence = 5, magic = 5, crafting = 5, mining = 5 })
        t.exec("jollyboar.thanks", t.chat.play, { "player:Thanksh very mush...", "end" })

        bar("risingsun", "0_46_52_12_46", "risingsun_barmaid2", {
            "npc:Heya! What can I get you?",
            "choose:I'm doing Alfred Grimhand's barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Heehee, this'll be fun!",
            "npc:You'll be after our Hand of Death cocktail, then. Lots",
            "end",
        }, 11, { attack = 5, defence = 5, ranged = 5, fishing = 5 })

        bar("rustyanchor", "0_47_50_39_55", "rustyanchor_bartender", {
            "choose:I'm doing Alfred Grimhand's Barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Are you sure?  You look a bit skinny for that.",
            "player:Just give me whatever I need to drink here.",
            "npc:Ok one Black Skull Ale coming up, 8 coins please.",
            "end",
        }, 12, nil)

        bar("zembo", "0_45_49_46_7", "zembo", {
            "npc:Hey, are you wanting to try some of my fine wines",
            "choose:I'm doing Alfred Grimhand's barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Ah, you'll be wanting some Ape Bite Liqueur then.",
            "end",
        }, 10, nil)
        t.exec("zembo.thanks", t.chat.play, { "player:Mmmmm, dat was luverly...", "end" })

        bar("deadman", "0_43_49_44_25", "deadmans_bartender", {
            "npc:Yohoho me hearty what would you like to drink?",
            "choose:I'm doing Alfred Grimhand's Barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Haha time to be breaking out the old Supergrog.",
            "end",
        }, 5, { attack = 5, defence = 5, prayer = 5, cooking = 5, herblore = 5 })

        local hp_before = level("hitpoints")
        bar("flyinghorse", "0_40_51_14_56", "flyinghorse_bartender", {
            "npc:Would you like to buy a drink?",
            "player:What do you serve?",
            "npc:Beer!",
            "choose:I'm doing Alfred Grimhand's Barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Fancy a bit of Heart Stopper then do you?",
            "end",
        }, 7, nil)
        t.check("flyinghorse.hit", level("hitpoints") <= hp_before - math.floor(hp_before * 25 / 100) + 1,
            "hitpoints " .. hp_before .. " -> " .. level("hitpoints") .. " (25% of current)")

        bar("foresters", "0_42_54_3_39", "foresters_bartender", {
            "npc:Good morning, what would you like?",
            "choose:I'm doing Alfred Grimhand's Barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Oh you're a barbarian then.",
            "end",
        }, 8, { attack = 5, defence = 5, woodcutting = 5, fletching = 5, firemaking = 5 })

        bar("blurberry", "1_38_54_48_32", "blurberry", {
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:Ah, you've come to the best stop on your list!",
            "end",
        }, 4, nil)
        t.check("blurberry.thanks_closed", t.chat.kind() ~= "player", "the thanks page closes on its own tick")

        card("card.1199", CARD_1199)

        bar("dragoninn", "0_39_48_60_8", "dragon_bartender", {
            "npc:What can I get you?",
            "player:What's on the menu?",
            "npc:Dragon Bitter and Greenman's Ale, oh and some cheap",
            "choose:I'm doing Alfred Grimhand's Barcrawl.",
            "player:I'm doing Alfred Grimhand's Barcrawl.",
            "npc:I suppose you'll be wanting some Fire Brandy.",
            "end",
        }, 6, nil)

        -- ---- Too drunk to read it (index 4420 [1309]) ----
        t.exec("card.drunk", t.player.inv_op, "barcrawl_card", 1)
        t.expect("card.drunk.line", t.msg.expect("You are too drunk to be able to read the barcrawl card."))
        t.expect("card.drunk.varp", t.var.expect("varp77_barcrawl", 8189))
        journal("journal.1313", JOURNAL_1313)

        -- ---- The hand-in (index 4420 [1348..1369]) ----
        t.cheat("::tele 0_39_55_47_54")
        t.ticks(3)
        t.exec("handin.talk", t.player.talk_to, "barbguard1", 1)
        t.exec("handin.pages", t.chat.play, {
            "npc:So, how's the Barcrawl coming along?",
            "player:I tink I jusht 'bout done dem all... but I losht count...",
            "objbox:You give the card to the barbarian.",
            "npc:Yep that seems fine, you can come in now.",
            "npc:Since you drink like a barbarian, I can show you how",
            "choose:Yes please, I want to smash my vials.",
            "player:Yes please, I want to smash my vials.",
            "npc:It's all part of drinking like a barbarian!",
            "end",
        })
        t.expect("complete.varp", t.var.expect("varp77_barcrawl", 2))
        t.expect("complete.progress", t.var.expect("varb13714_barcrawl_progress", 2))
        t.expect("complete.smash", t.var.expect("varb5668_auto_smash_vials", 1))
        t.expect("complete.card_gone", t.inv.expect_absent("barcrawl_card"))

        stance("guard.stance.postcrawl")
        t.exec("after.talk", t.player.talk_to, "barbguard1", 1)
        t.exec("after.pages", t.chat.play, {
            "npc:'Ello friend. I see you're drinking like a barbarian - do",
            "choose:No thank you, I like smashing them.",
            "player:No thank you, I like smashing them.",
            "npc:That's a proper barbarian spirit, that is.",
            "end",
        })

        -- ---- The gate (index 4421 [6..11]) ----
        t.exec("gate.walk", t.player.goto_tile, 2545, 3571, 0)
        t.exec("gate.cross", t.player.cross_gate, { loc = "barbariangatel", at = { 2545, 3570, 0 },
            near = { 2545, 3570 }, far_ok = function(tile) return tile.x == 2546 and (tile.z == 3569 or tile.z == 3570) end,
            far_desc = "east of the outpost gate, on the row it was crossed" })
        t.exec("gate.back", t.player.cross_gate, { loc = "barbariangatel", at = { 2545, 3570, 0 },
            near = { 2546, 3569 }, far_ok = function(tile) return tile.x == 2545 and (tile.z == 3569 or tile.z == 3570) end,
            far_desc = "west of the outpost gate, on the row it was crossed" })
    end,
}
