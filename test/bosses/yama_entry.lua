-- yama_entry: the walk-in route, from the recording's own pages (RSProx index 4498
-- [297..404], [7143..7155]; 3268 [3995..4019]; 3329 [1173..1189]): the Voice, the
-- first contract, the throne's three menus, a contract turned away, up to Yama
-- arriving. The fight is test/bosses/yama.lua and yama_solver.lua.
local function open(t) return select(2, t.ui.is_modal()) == true end

return {
    id = "yama_entry",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::tele 0_22_157_32_30", "::item 30810",
    },
    run = function(t)
        t.ticklog.start()
        t.ticks(3)
        t.exec("voice.talk", t.player.talk_to, "voice_of_yama", 1)
        t.exec("voice.first", t.chat.play, {
            "npc:The arrogant human finally arrives...",
            "player:Upset that I disrupted your plans?",
            "npc:Disgusting. My master, the great demon Yama, wishes",
            "player:Yama wants to speak with me?",
            "npc:My master wishes to have the satisfaction",
            "player:I planted the device!",
            "npc:Silence, human! You will make no demands of me.",
            "choose:Enter Yama's domain.",
        })
        t.ticks(8)
        local _, tile = t.world.tile()
        t.check("voice.entered", tile.x > 6400, "in a domain copy, at " .. tile.x .. "," .. tile.z)
        -- The throne is 39 tiles north of the landing (index 4498 [316..341]).
        local bx, bz = tile.x - tile.x % 64, tile.z - tile.z % 64
        t.exec("walk.throne", t.player.goto_tile, bx + 28, bz + 37, 0)
        t.ticks(2)

        t.exec("throne.talk", t.player.talk_to, "yama_throne", 1)
        t.exec("throne.first", t.chat.play, {
            "npc:I would offer you some water",
            "player:Thanks...",
            "npc:I take it you have questions.",
            "choose:You know why I'm here. Stop messing around.",
            "player:You know why I'm here.",
            "npc:I do, but it is important",
            "player:How are you still communicating",
            "npc:Why, through my voice, of course.",
            "player:You know what I mean.",
            "npc:The same way I always have.",
            "player:But the dark nullifier",
            "npc:Hmm... Yes, if anything could have done so",
            "npc:However, when your opponents",
            "player:You knew we would try to use it?",
            "npc:Yes. When I heard you would be hand delivering",
            "player:But... I remember planting it!",
            "npc:I do not doubt that you remember it that way.",
            "player:What did you do?!",
            "npc:A simple transfiguration of the mind.",
            "player:You modified my memory?!",
            "npc:Yes. And I have now answered five",
            "player:You call that gloating generosity?",
            "npc:Hmm... Perhaps you are more astute",
            "player:Well you won't be getting it from me!",
            "npc:I am Yama.",
            "npc:These things could be yours.",
            "npc:Face me in combat.",
            "player:And if I lose?",
            "npc:I take your soul.",
            "player:No thanks.",
            "npc:I see. Perhaps we could lower the stakes then.",
            "npc:I will face you in an inhibited form.",
            "npc:If you win, I will give you a chance",
            "player:And if I lose?",
            "npc:When you take your final rest",
            "player:How can I know you'll keep your word?",
            "npc:The same way I will know you shall keep yours.",
            "player:...Okay.",
            "npc:Sign right here.",
            "choose:Yes.",
            "choose:Yes.",
            "npc:Excellent... Challenge me when you are ready",
        })
        t.ticks(2)

        t.exec("throne.again", t.player.talk_to, "yama_throne", 1)
        t.exec("throne.welcome", t.chat.play, {
            "npc:Have you come to challenge me?",
            "choose:No, I'd rather not.",
            "player:No, I'd rather not.",
            "npc:Perhaps you are just here to waste my time",
        })
        t.ticks(2)

        t.exec("contract.use", t.player.use_on, "yama_special_contract", t.player.by_symbol("npc", "yama_throne"))
        t.exec("contract.refused", t.chat.play, {
            "npc:I'm afraid my scribes are a tad overwhelmed at the moment.",
        })
        t.ticks(2)

        t.player.talk_to("yama_throne", 3)
        t.ticks(4)
        local r, spawns = t.ticklog.rows({ kind = "npc_spawn", type = 14176 })
        t.check("yama.arrives", r == "ok" and #spawns >= 1, "Yama was summoned by Challenge (npc_spawn 14176)")
    end,
}
