-- Dorgesh-Kaan's citizens and the Varrock Museum gate guard, as RSProx index
-- 4524 talked to them (areas/dorgeshkaan/scripts/citizens.rs2,
-- areas/varrock/scripts/vm_museum_guard.rs2): Barlak [220..281] and Ur-meg
-- [419..449] down their recorded menu paths, a cave goblin and the cave goblin
-- guard (one of five / three lines at random, so only drained), and the museum
-- guard's gate [1367..1378] in, then out again (the way out is unverified).
local function go(t, name, x, z, level)
    t.cheat(string.format("::goto %d %d %d", x, z, level))
    t.ticks(3)
    local _, tile = t.world.tile()
    t.check(name .. ".at", tile ~= nil and tile.x == x and tile.z == z,
            tile and (tile.x .. "," .. tile.z) or "no tile")
end

return {
    id = "dorgeshkaan_citizens",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::complete quest_losttribe",
        "::complete quest_deathtothedorgeshuun",
        "::complete quest_digsite",
    },
    run = function(t)
        go(t, "barlak", 2695, 5316, 0)
        t.exec("barlak.talk", t.player.talk_to, "dorgesh_male_bone_seller", 1)
        t.exec("barlak.dialog", t.chat.play, {
            "npc:Bones!",
            "player:What?",
            "npc:I don't have any bones!",
            "options",
            "choose:Then how do you stand up?",
            "player:Then how do you stand up?",
            "npc:What?",
            "player:How do you stand up if you have no bones?",
            "npc:Ha, ha, ha, ha, ha!",
            "options",
            "choose:What kind of bones do you need?",
            "player:What kind of bones do you need?",
            "npc:Enormous bones, as big as you can get.",
            "npc:Sometimes you might find a particularly long",
            "npc:Occasionally, you might find a long curved bone",
            "npc:I'll also teach you a bit",
            "options",
            "choose:Will you buy anything besides bones?",
            "player:Will you buy anything besides bones?",
            "npc:Well, I've had a few people bring me",
            "npc:Of course, like the bones",
            "options",
            "choose:Goodbye.",
            "player:Goodbye.",
            "npc:Goodbye.",
        })

        go(t, "urmeg", 2692, 5301, 0)
        t.exec("urmeg.talk", t.player.talk_to, "dorgesh_female_council_2", 1)
        t.exec("urmeg.dialog", t.chat.play, {
            "npc:Oh dear...",
            "player:What's the matter?",
            "npc:I'm worried. I think the Council",
            "player:What issue?",
            "npc:I'm very happy for you humans",
            "player:What about who?",
            "npc:Them!",
            "player:The gods?",
            "npc:Don't say it! What if they hear?",
            "options",
            "choose:Don't you want to worship the god of the goblins?",
            "player:Don't you want to worship",
            "npc:No! How could you even suggest that?",
            "npc:It may seem to you that we're cowardly",
            "options",
            "choose:Why are you scared of the gods?",
            "player:Why are you scared of the gods?",
            "npc:Because they're so powerful, of course!",
            "npc:In the time of the great war",
            "npc:We've hidden down here for this long",
            "options",
            "choose:Goodbye.",
            "player:Goodbye.",
            "npc:Goodbye.",
        })

        go(t, "cave_goblin", 2726, 5370, 1)
        t.exec("cave_goblin.talk", t.player.talk_to, "dorgesh_male_patrol2", 1)
        t.exec("cave_goblin.dialog", t.chat.drain, { max_pages = 8 })

        go(t, "guard", 3317, 9604, 0)
        t.exec("guard.talk", t.player.talk_to, "cave_goblin_guard1", 1)
        t.exec("guard.dialog", t.chat.drain, { max_pages = 8 })

        go(t, "museum", 3258, 3447, 0)
        t.exec("museum.talk", t.player.talk_to, "vm_museum_guard", 1)
        t.exec("museum.dialog", t.chat.play, {
            "npc:Welcome! Would you like to go into the Dig Site",
            "options",
            "choose:Yes, I'll go in!",
        })
        t.ticks(5)
        local _, inside = t.world.tile()
        t.check("museum.inside", inside ~= nil and inside.x == 3261 and inside.z == 3446,
                inside and (inside.x .. "," .. inside.z) or "no tile")
        t.exec("museum.out", t.player.click_loc, "vm_barrier_guard_gate", 1)
        t.ticks(5)
        local _, outside = t.world.tile()
        t.check("museum.outside", outside ~= nil and outside.x == 3261 and outside.z == 3447,
                outside and (outside.x .. "," .. outside.z) or "no tile")
    end,
}
