-- Varrock Museum basement: all fourteen natural historian lectures, each a
-- cutscene in a copy of the basement (RSProx index 4348, 2026-10-23). Content:
-- areas/varrock/scripts/vm_natural_historian.rs2.
--
-- Every lecture page below is the recording's (its first 45 characters), in
-- order, and each lecture must put the player back on the tile 4348 does.
-- Route: Lumbridge -> the museum's basement stairs, then each wing's
-- historian in turn. The wire audit (camera, cast, poses, props, music) is
-- in docs/rsprox-analysis/quests/natural_history_quiz.md.
return {
    id = "naturalhistorylectures",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
    },

    run = function(t)
        local STAND = {
            north = { 1758, 4973 },
            east = { 1771, 4961 },
            south = { 1757, 4946 },
            west = { 1749, 4961 },
        }
        local LECTURES = {
        {
            wing = "north", choice = "Tell me about lizards.", ask = "Tell me about lizards.", exit = { 1741, 4976 },
            pages = {
                "npc:Contrary to popular belief, even though most ",
                "npc:This is due to a very thick and tough hide, m",
                "npc:In fact, the only people to successfully disc",
                "npc:Interestingly enough, these scales are made f",
                "npc:Lizards have a very well developed sense of v",
                "npc:A tiny, light-sensitive, transparent structur",
                "npc:This is vital for the cold-blooded lizards wh",
                "npc:Like many cold-blooded creatures, if they are",
                "npc:And this concludes my short lecture on lizard",
            },
        },
        {
            wing = "north", choice = "Tell me about tortoises.", ask = "Tell me about the tortoises.", exit = { 1751, 4976 },
            pages = {
                "npc:The tortoise is a very well defended beast. I",
                "npc:Tortoises can vary in size from about as long",
                "npc:Did you know you can tell how old a tortoise ",
                "npc:Most land-based tortoises eat plants, feeding",
                "npc:Tortoises generally live as long as people, a",
                "npc:Because of this, they symbolise longevity wit",
                "npc:The oldest tortoise ever recorded was Mibbiwo",
                "npc:Mibbiwocket is still in the care of the gnomi",
                "npc:And this concludes my short lecture on tortoi",
            },
        },
        {
            wing = "north", choice = "Tell me about dragons.", ask = "Tell me about dragons.", exit = { 1767, 4977 },
            pages = {
                "npc:The dragons of Gielinor are a most confusing ",
                "npc:However, if you delve a little deeper into th",
                "npc:With most species there is a line, an ancestr",
                "npc:For instance, you can trace the ancestry of t",
                "npc:However, with the dragon, no such root ancest",
                "npc:There are many forms of dragon, such as the c",
                "npc:They colonise many areas of Gielinor, though ",
                "npc:Eating habits tend to vary, with the majority",
                "npc:Throughout history, dragons have appeared in ",
                "npc:However, modern evidence does not support thi",
                "npc:The lifespan of the common dragon is as yet u",
                "npc:Although, it has been mooted that spontaneous",
                "npc:And this concludes my short lecture on dragon",
            },
        },
        {
            wing = "north", choice = "Tell me about wyverns.", ask = "Tell me about the wyverns.", exit = { 1776, 4976 },
            pages = {
                "npc:The wyverns' tale is a sad one. This now exti",
                "npc:In that they are both large flying reptiles. ",
                "npc:Much of the evidence for wyvern behaviour com",
                "npc:As most lizards cannot maintain their own bod",
                "npc:One is that Asgarnia was at one time a much m",
                "npc:If they follow the dragon paradigm, then they",
                "npc:How and why the wyverns became extinct is som",
                "npc:There are some inconsistencies in the finding",
                "npc:Also the bones we have collected remain a lit",
                "npc:They have also been shown to radiate a very w",
                "npc:I'm sure that in due time, these mysteries wi",
                "npc:And this concludes my short lecture on wyvern",
            },
        },
        {
            wing = "east", choice = "Tell me about snails.", ask = "Tell me about snails.", exit = { 1775, 4961 },
            pages = {
                "npc:Snails move like worms by squishing up and st",
                "npc:They also use slime for protection. For insta",
                "npc:When they hide in their shells, snails secret",
                "npc:The snails of Morytania are the most malignan",
                "npc:They are broken down into two distinct specie",
                "npc:Both of these varieties are voracious carnivo",
                "npc:They then simply have to wait, whilst the dig",
                "npc:How these strange creatures came to be is sti",
                "npc:The local populace has capitalised on the app",
                "npc:Other known uses of snail by-products include",
                "npc:And this concludes my short lecture on snails",
            },
        },
        {
            wing = "east", choice = "Tell me about monkeys.", ask = "Tell me about monkeys.", exit = { 1775, 4959 },
            pages = {
                "npc:A monkey is a member of either of two known g",
                "npc:Because of their similarity to monkeys, apes ",
                "npc:Though some natural historians don't consider",
                "npc:The Karamja monkeys are rumoured to be fairly",
                "npc:In appearance, they stand much shorter than a",
                "npc:They are very fond of bananas and bitternuts,",
                "npc:The harmless monkeys of Mos Le'Harmless are a",
                "npc:Interestingly, Karamaja monkeys have a deep d",
                "npc:And this concludes my short lecture on monkey",
            },
        },
        {
            wing = "east", choice = "Tell me about sea slugs.", ask = "Tell me about the sea slugs.", exit = { 1782, 4959 },
            pages = {
                "npc:The term 'sea slug' is something of a misnome",
                "npc:Very little is actually known about the sea s",
                "npc:For some reason all expeditions we have sent ",
                "npc:It is presumed that the species is native to ",
                "npc:There must be some natural resource in the ar",
                "npc:Through looking at similar species we have de",
                "npc:There are reports that these reclusive animal",
                "npc:If they do follow the same pattern as other s",
                "npc:It is typical of prey animals such as these t",
                "npc:If only we could acquire one for study. I'm s",
                "npc:And this concludes my short lecture on sea sl",
            },
        },
        {
            wing = "east", choice = "Tell me about snakes.", ask = "Tell me about snakes.", exit = { 1782, 4961 },
            pages = {
                "npc:Serpentes, or ophidia, is the suborder under ",
                "npc:Unlike lizards, snakes have no limbs whatsoev",
                "npc:Found in nice tropical forests, temperate lat",
                "npc:While others still are fast hunters who use s",
                "npc:The sense of smell in snakes has been enhance",
                "npc:However, instead of using just their nose, th",
                "npc:The tongue then returns to the mouth and the ",
                "npc:Since these pits are split apart from each ot",
                "npc:So the next time you see a snake sticking it'",
                "npc:And this concludes my short lecture on snakes",
            },
        },
        {
            wing = "south", choice = "Tell me about terrorbirds.", ask = "Tell me about the terrorbirds.", exit = { 1757, 4939 },
            pages = {
                "npc:Terrorbirds live in nomadic groups of between",
                "npc:They mainly feed on seeds and other plants. T",
                "npc:They have no teeth to chew with, so they swal",
                "npc:They can go without water for a long time, ex",
                "npc:Terrorbirds are known to eat almost anything,",
                "npc:Terrorbirds usually weigh a little less than ",
                "npc:There are claws on two of the wings' fingers ",
                "npc:This is an adaptation unique to terrorbirds t",
                "npc:The gnomes in particular, prize the terrorbir",
                "npc:There are a number of recorded incidents of p",
                "npc:A terrorbird is so fast, it can easily outrun",
                "npc:And this concludes my short lecture on terror",
            },
        },
        {
            wing = "south", choice = "Tell me about the Kalphite Queen.", ask = "Tell me about the Kalphite Queen.", exit = { 1760, 4939 },
            pages = {
                "npc:The kalphites, otherwise known as the kalphis",
                "npc:Most of the early documentation and research ",
                "npc:This, of course, was before he was driven ins",
                "npc:Kalphites are related to beetles and scorpion",
                "npc:This shell can be compressed into a ball or f",
                "npc:They exist in a caste-based society, with the",
                "npc:Voracious carnivores, a pack of adult workers",
                "npc:They typically live in large nests marked by ",
                "npc:Indeed, there is some relationship between th",
                "npc:During the early part of the fourth age, Scab",
                "npc:When the people eventually revolted against h",
                "npc:Of course, any right-minded scientist discoun",
                "npc:And this concludes my short lecture on kalphi",
            },
        },
        {
            wing = "west", choice = "Tell me about camels.", ask = "Tell me about camels.", exit = { 1736, 4961 },
            pages = {
                "npc:The term camel refers to either of the two kn",
                "npc:There is the bactrian, which has two humps an",
                "npc:Camels are ill-tempered beasts at the best of",
                "npc:This would explain why used camel salesmen ar",
                "npc:In the wild, camels have been known to lie in",
                "npc:This is quite surprising, as most domestic ca",
                "npc:Camel milk is much more nutritious than cow m",
                "npc:Another useful camel by-product is dung. Thei",
                "npc:Scientific research has also shown that chill",
                "npc:And this concludes my short lecture on camels",
            },
        },
        {
            wing = "west", choice = "Tell me about leeches.", ask = "Tell me about leeches.", exit = { 1743, 4961 },
            pages = {
                "npc:Leeches are fascinating creatures and are ver",
                "npc:They like to inhabit streams, rivers and seas",
                "npc:One of the most common misconceptions is that",
                "npc:In fact, very, very few leeches are parasitic",
                "npc:Most leeches are meat eaters, feeding on a va",
                "npc:Those that do feed on blood have developed an",
                "npc:Then they bite into the skin using a Y-shaped",
                "npc:They will then feed until they are completely",
                "npc:Most leeches are very small, measuring no mor",
                "npc:They are much more mobile than their smaller ",
                "npc:Quite how these leeches came to be so big is ",
                "npc:All we can assume is that there is some kind ",
                "npc:And this concludes my short lecture on leeche",
            },
        },
        {
            wing = "west", choice = "Tell me about moles.", ask = "Tell me about moles.", exit = { 1736, 4959 },
            pages = {
                "npc:Now, moles are small mammals of the talpidae ",
                "npc:They vary greatly in habitat and can be found",
                "npc:Male moles are known as boars with the female",
                "npc:Moles are considered to be an agricultural pe",
                "npc:This has been highlighted in Falador by Wyson",
                "npc:This fearsome beast has huge claws, wicked te",
                "npc:That said, they do benefit the soil by aerati",
                "npc:And this concludes my short lecture on moles.",
            },
        },
        {
            wing = "west", choice = "Tell me about penguins.", ask = "Tell me about penguins.", exit = { 1743, 4959 },
            pages = {
                "npc:This often-maligned aquatic bird is known to ",
                "npc:These aquatic birds seem to spend the majorit",
                "npc:Unlike most animals that prefer cold climes, ",
                "npc:For creatures with such small brains, they do",
                "npc:Their diet consists mainly of fish, squid and",
                "npc:Penguins primarily rely on their vision while",
                "npc:Some theories suggest that penguins are helpe",
                "npc:Penguins spend a long time going without food",
                "npc:Fortunately, most penguins build up a layer o",
                "npc:And this concludes my short lecture on pengui",
            },
        },
        }
        local function tile_str(tl)
            return tl and (tostring(tl.x) .. "," .. tostring(tl.z) .. "," .. tostring(tl.level)) or "nil"
        end

        t.ticklog.start()
        t.exec("goto-museum", t.player.goto_tile, 3256, 3449, 0)
        t.exec("stairs-down", t.player.click_loc, "vm_basement_wall_stairs_down", 1, { at = { 3255, 3451, 0 } })
        t.ticks(4)

        local first = true
        for i = 1, #LECTURES do
            local l = LECTURES[i]
            local step = "lecture" .. i
            t.ticklog.mark("leg." .. step)
            t.exec(step .. ".goto", t.player.goto_tile, STAND[l.wing][1], STAND[l.wing][2], 0)
            t.exec(step .. ".talk", t.player.talk_to, "vm_natural_historian_" .. l.wing, 1)
            local opening = {}
            if first then
                opening = {
                    "npc:Hello there and welcome to the Natural History exhibit",
                    "player:Hello. So what is it you do here?",
                    "npc:Well, we look after all of the research in this section.",
                    "npc:When I'm not doing that",
                    "player:Nice. So then, I've got a few questions for you.",
                    "npc:Well, fire away",
                }
                first = false
            else
                opening = {
                    "npc:Hello again, sir, how can I help you on this fine day?",
                    "player:I was hoping you could tell me about something.",
                }
            end
            opening[#opening + 1] = "choose:" .. l.choice
            opening[#opening + 1] = "player:" .. l.ask
            opening[#opening + 1] = "npc:Ahh"
            opening[#opening + 1] = "npc:If you just follow me to the display case"
            t.exec(step .. ".ask", t.chat.play, opening)
            t.expect(step .. ".first-page", t.await({
                level = function()
                    return t.chat.kind() == "npc"
                end,
                note = step .. ": the first lecture page",
            }, 12))
            local _, inside = t.world.tile()
            t.check(step .. ".in-copy", inside ~= nil and inside.x > 6400, "during the lecture at " .. tile_str(inside))
            t.exec(step .. ".pages", t.chat.play, l.pages)
            t.ticks(7)
            local _, back = t.world.tile()
            t.check(step .. ".back", back ~= nil and back.x == l.exit[1] and back.z == l.exit[2] and back.level == 0,
                "after the lecture at " .. tile_str(back) .. " (want " .. l.exit[1] .. "," .. l.exit[2] .. ",0)")
            t.expect(step .. ".unlocked", t.var.await_server("varb542_cutscene_status", 0, 5))
        end
        t.finish(0)
    end,
}
