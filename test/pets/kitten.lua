-- kitten: the pet kitten's care, as the owner's sessions with it out recorded it
-- (quest_fluffs/scripts/kitten_pet.rs2's header, RSProx index 4401..4429):
--   - Interact: the menu "Interact with kitten", Stroke (the line, then the page
--     three ticks on), Guess age (63 cycles: "1 hour 34 minutes" / "1 hour 25
--     minutes", 4426 [1323]);
--   - a fish: "The kitten gobbles up the fish." and the fish gone; raw bird meat:
--     "Nothing interesting happens."; the ball of wool's page, the wool kept;
--   - the hunger and attention warnings on the cycle the counters reach them;
--   - growing up: the 120th cycle makes it a cat, picked up as a pet cat.
--
--   ./src/build_opt/torirsserver --scriptrun test/pets/kitten.lua --bots 1 \
--       --name kitten0 --ticks 2000 --session /tmp/kitten --fixture test/quests/fixtures/fresh_lumbridge.ini

local function count(t, obj) local _, n = t.inv.count(obj) return tonumber(n) or 0 end
local function kitten(t) return t.player.by_symbol("npc", "kittenpet1") end

return {
    id = "kitten",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", "::give kittenobject 1", "::give raw_sardine 2", "::give ball_of_wool 1",
        "::give spit_raw_bird_meat 1",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("goto", t.player.goto_tile, 3222, 3218, 0)
        t.cheat("::kittenset 63 0 23")
        t.ticks(2)

        t.ticklog.mark("drop")
        t.player.inv_op("kittenobject", 5)
        t.exec("present", t.npc.await_present, "kittenpet1", 3, 5)
        t.check("drop.gone", count(t, "kittenobject") == 0, "kittens in the pack " .. count(t, "kittenobject"))

        t.ticklog.mark("guess")
        t.exec("guess.op", t.player.talk_to, "kittenpet1", 5)
        t.exec("guess.chat", t.chat.play, {
            "options", "choose:Guess age",
            "player:I wonder how old you are...",
            "mesbox:After taking a good look at your kitten you guess that its age is: 1 hour 34 minutes. And approximate time until fully adult: 1 hour 25 minutes, assuming you look after it.",
            "end",
        })

        t.ticklog.mark("stroke")
        t.exec("stroke.op", t.player.talk_to, "kittenpet1", 5)
        t.exec("stroke.chat", t.chat.play, {
            "options", "choose:Stroke", "player:That cat sure loves to be stroked.", "end",
        })

        t.ticklog.mark("feed")
        t.cheat("::kittenset 63 10 28")
        t.exec("feed", t.player.use_on, "raw_sardine", kitten(t))
        t.ticks(3)
        t.check("feed.eaten", count(t, "raw_sardine") == 1, "sardines " .. count(t, "raw_sardine"))

        t.ticklog.mark("meat")
        local _, meat = t.player.use_on("spit_raw_bird_meat", kitten(t))
        t.check("meat.line", tostring(meat):find("Nothing interesting happens", 1, true) ~= nil, tostring(meat))
        t.ticks(3)
        t.check("meat.kept", count(t, "spit_raw_bird_meat") == 1, "bird meat " .. count(t, "spit_raw_bird_meat"))

        t.ticklog.mark("wool")
        t.exec("wool", t.player.use_on, "ball_of_wool", kitten(t))
        t.exec("wool.chat", t.chat.play, {
            "player:That kitten loves to play with that ball of wool. I think it is its favourite.", "end",
        })
        t.check("wool.kept", count(t, "ball_of_wool") == 1, "wool " .. count(t, "ball_of_wool"))

        -- The next cycle reaches 16: "Miaow!" and the warning.
        t.ticklog.mark("hungry")
        t.cheat("::kittenset 63 15 6")
        local warned = false
        for _ = 1, 160 do
            if t.chat.kind() == "player" then warned = true break end
            t.ticks(1)
        end
        t.check("hungry.page", warned, "the hunger page within a cycle")
        t.exec("hungry.chat", t.chat.play, { "player:I think it's hungry!", "end" })

        t.ticklog.mark("lonely")
        t.cheat("::kittenset 63 0 39")
        warned = false
        for _ = 1, 160 do
            if t.chat.kind() == "player" then warned = true break end
            t.ticks(1)
        end
        t.check("lonely.page", warned, "the attention page within a cycle")
        t.exec("lonely.chat", t.chat.play, { "player:I think it wants some attention.", "end" })

        -- The 120th cycle: a cat.
        t.ticklog.mark("grow")
        t.cheat("::kittenset 119 0 23")
        local grown = false
        for _ = 1, 160 do
            if t.chat.kind() == "mesbox" then grown = true break end
            t.ticks(1)
        end
        t.check("grow.box", grown, "the grown-up box within a cycle")
        t.exec("grow.chat", t.chat.play, { "mesbox:Your kitten has grown into a healthy cat", "end" })
        t.exec("grow.cat", t.npc.await_present, "growncat", 3, 5)
        t.player.talk_to("growncat", 1)
        t.ticks(2)
        t.check("pickup.item", count(t, "growncatobject") == 1, "pet cats in the pack " .. count(t, "growncatobject"))
        t.finish(0)
    end,
}
