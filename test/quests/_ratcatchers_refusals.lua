-- Before Ratcatchers, the ratcatchers, their cats and the four Rat Pits entrances
-- turn the player away. RSProx index 4489 (Ardougne, Varrock, the sewer, Port Sarim)
-- and 4492 (Keldagrim), recorded at varbit 1404 = 0 with a catspeak amulet on.
--
--     python3 tools/quest_gate/run.py --script test/quests/_ratcatchers_refusals.lua --name ratcatchers_refusals
return {
    id = "ratcatchers_refusals",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::clearinv",
        "::give ics_little_amulet_of_catspeak",
    },

    run = function(t)
        t.ticks(2)
        t.check("catspeak.worn", t.cheat("::wield ics_little_amulet_of_catspeak") == "ok", "wield the amulet")
        t.ticks(2)

        local function here(name, tele)
            t.check(name .. ".tele", t.cheat("::tele " .. tele) == "ok", tele)
            t.ticks(3)
        end

        -- click_loc returns on arrival; the recorded refusals open their page up to two ticks later.
        local function await_page()
            for _ = 1, 8 do
                if t.chat.kind() ~= "none" then return end
                t.ticks(1)
            end
        end

        -- Trotters and Hooknosed Jack wander: retry until the talk lands.
        local function talk(name, npc, pages)
            local r, d
            for try = 1, 6 do
                r, d = t.player.talk_to(npc, 1)
                if r == "ok" or t.chat.kind() ~= "none" then break end
                t.note(name .. "-try" .. try .. ": " .. tostring(r) .. " " .. tostring(d))
                t.ticks(3)
            end
            t.check(name, r == "ok" or t.chat.kind() ~= "none", "talk_to " .. npc .. " -> " .. tostring(r))
            await_page()
            t.exec(name .. "-dialog", t.chat.play, pages)
            t.ticks(2)
        end

        local function refused(name, loc, x, z, level, pages)
            local _, before = t.world.tile()
            t.exec(name, t.player.click_loc, loc, 1, { at = { x, z, level } })
            await_page()
            t.exec(name .. "-dialog", t.chat.play, pages)
            t.ticks(2)
            local _, after = t.world.tile()
            t.check(name .. ".stayed", before and after and after.level == before.level
                and math.abs(after.z - before.z) < 8,
                "still on the surface beside the entrance")
        end

        -- The Varrock sewer.
        here("sewer.trotters", "0_50_154_37_10")
        talk("trotters", "vc_trotters", {
            "player:Hello there.",
            "npc:Oh, what do you want? Can't you see I'm trying to nap?",
            "player:Shouldn't you be catching rats?",
            "npc:Oh my, no. I have humans for that.",
        })
        here("sewer.sisters", "0_50_154_44_12")
        talk("sisters", "vc_grimesquit", {
            "npc:'Ello 'Ello!",
            "player:Who's this then?",
            "npc:I don't know any more than you do!",
            "player:Well ask them!",
            "npc:You ask them!",
            "player:No! I'm not talking to some stranger in a sewer.",
            "npc:Go away, weirdo! We're busy!",
        })

        -- Ardougne: Bones, Jimmy, the Flying Horse Inn's ladder.
        here("ardougne", "0_40_51_3_57")
        talk("bones", "vc_bones", {
            "player:Who's a cute little kitty?",
            "npc:Jimmy doesn't like me talking to strangers.",
        })
        talk("jimmy", "vc_jimmy_dazzler", {
            "npc:Who are you, and what do you want?",
            "player:My name is",
            "npc:No.",
            "player:What?",
            "npc:I don't know you, therefore I will not answer any questions.",
            "player:As if those clothes you're wearing wouldn't do that already.",
        })
        refused("ardougne.ladder", "vc_ladder_down", 2561, 3320, 0, {
            "player:I can't see the bottom of the ladder. I think I'll leave it be, for now.",
        })

        -- Varrock: Pox, Hooknosed Jack, his manhole.
        here("varrock", "0_51_53_5_9")
        talk("pox", "vc_pox", {
            "player:Hi cat!",
            "npc:Hello.",
            "player:What are you doing?",
            "npc:I think I'll go hunting soon",
            "player:Happy hunting.",
        })
        talk("jack", "vc_hooknosed_jack", {
            "player:Hello.",
            "npc:What do you want?",
            "npc:Actually, I've got no time for this. I don't want to talk to you.",
        })
        refused("varrock.manhole", "vc_manhole_open", 3267, 3400, 0, {
            "npc:Step away from that manhole, you've no business down there.",
        })

        -- Port Sarim: Bellemorde, The Face, her manhole.
        here("port_sarim", "0_47_50_11_33")
        talk("bellemorde", "vc_bellemorde", {
            "player:Hello Bellemorde.",
            "npc:Hello human.",
            "player:Would you like a fish?",
            "npc:I don't want your fish.",
        })
        talk("face", "vc_face", {
            "player:Hello.",
            "npc:...",
            "player:Never mind!",
        })
        refused("port_sarim.manhole", "vc_manhole_open", 3018, 3232, 0, {
            "npc:Step away from that manhole, you've no business down there.",
        })

        -- Keldagrim: Smokin' Joe, The Beast, the Laughing Miner's stairs.
        here("keldagrim.joe", "0_45_159_49_36")
        talk("joe", "vc_smokin_joe", {
            "player:Hello!",
            "player:Hello?",
            "player:Hey!",
            "mesbox:Smokin' Joe ignores you.",
        })
        here("keldagrim.beast", "0_45_159_35_13")
        talk("beast", "vc_the_beast", {
            "player:Hello, The Beast! Why do they call you that?",
            "npc:...",
            "player:Oh, I see.",
        })
        refused("keldagrim.stairs", "vc_keldagrim_stairs_upper", 2914, 10188, 0, {
            "mesbox:The large cat near the stairs glowers menacingly",
            "player:I think I had better stay away from the stairs for now.",
        })
    end,
}
