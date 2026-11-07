-- Quest journals against what Jagex sends (2026-10-27): each state is set
-- with ::setvar, the journal opened through the quest list, and its rows read.
--   The Ides of Milk, complete: the 33 rows of RSProx index 4381 [870], exactly.
--   Pirate's Treasure, stage 1: the 6 rows of RSProx index 4378 [74], exactly.
--   Observatory Quest, stages 2, 3, 4 and 6: RSProx index 4412, exactly.
--   Children of the Sun, stages 2, 6 and 12: RSProx index 4408, exactly.
--   Tale of the Righteous, stages 5, 6, 14 and 16: RSProx index 4425, exactly.
--   Twilight's Promise, 14 openings from stage 4 to 38: RSProx index 4444, exactly.
--   Scorpion Catcher, stage 2 with the full cage: RSProx index 4421 [1730], exactly.
--   Merlin's Crystal, stages 1..7 (stage 4 twice, by what is in hand): RSProx index 4433, exactly.
--   Holy Grail, stages 2, 3, 4, 8 (and with the feather) and 9 (and with the
--   Grail): RSProx index 4435, exactly.
--   Death Plateau, stages 20, 30, 40, 50, 55 and four states of 70: RSProx index
--   4431, exactly.
--   Troll Stronghold, stages 10, 20 (outside, inside, with the prison key), 30
--   (and with Eadgar freed) and 40: RSProx index 4455, exactly.
--   The Grand Tree, stages 20, 60, 80, 90, 100, 110, 120 and 150 (the rock held):
--   RSProx index 4429, exactly (stage 90's trailing blank row dropped: a read
--   ends at the last text row).
--   The Dig Site, nine states of stages 1..6: RSProx index 4446, exactly.
--   The Golem, nine states of stages 1..7: RSProx index 4473, exactly.
--   Misthalin Mystery and Vampyre Slayer (the wiki's journal transcripts): every
--   state opens with its current step.
-- The driver reads a row's text without its tags, so rows are compared as plain
-- text (the wrap points); the <str> and colour tags are checked on a
-- TORIRSSERVER_TRACE_OUT=2 run.
local function plain(s)
    return (string.gsub(s, "<[^>]*>", ""))
end
-- The driver reads a row trimmed; Jagex's rows can open or close with a space
-- (Dwarf Cannon's "<col=000080> in the dwarves...", "...the Captain. "), which
-- our wire keeps (a TORIRSSERVER_TRACE_OUT=2 run), so the compare trims too.
local function plain_read(s)
    return (string.gsub(plain(s), "^%s*(.-)%s*$", "%1"))
end
local IOM_4381 = {
    "<str>I have met an enigmatic man by the name of Cassius lurking",
    "<str>around Farmer Fred's farm in Lumbridge.",
    "<str>Cassius has offered me the chance to share the profits of",
    "<str>his upcoming milk business, in exchange for a bit of help",
    "<str>setting it up.",
    "<str>Having spoken to Seth and Gillie Groats, I found that they",
    "<str>raise their animals following a set of principles that Seth",
    "<str>keeps in a book. I took that book to Cassius as it may have",
    "<str>been useful to his budding milk business.",
    "<str>Having returned Seth's book to Cassius, he then offered me",
    "<str>a sample of the milk we will be selling. It wasn't the most",
    "<str>pleasant thing to drink, but I managed to finish it.",
    "<str>The next step in getting our business set up was to get a",
    "<str>business permit. To that end, Cassius sent me to the Duke of",
    "<str>Lumbridge with another milk sample, aiming to convince him to",
    "<str>let us begin trading.",
    "<str>I spoke to the Duke of Lumbridge about getting a business",
    "<str>permit for Cassius' future milk empire, but unfortunately the",
    "<str>Duke couldn't grant us one without us speaking to the Groats",
    "<str>family first.",
    "<str>I spoke to Gillie at the Duke of Lumbridge's request, and she",
    "<str>asked me to drink the milk sample so that she could get a",
    "<str>sense of the quality of the milk. It wasn't pleasant, but",
    "<str>seemed to bring her around to my cause.",
    "<str>Gillie sent me to deal with the aggressive bull that she had",
    "<str>fenced in her field. It was a tough fight but I brought the",
    "<str>beast down. Unfortunately it turned out the the bull was",
    "<str>Brutus, who I believed to be Cassius business partner.",
    "<str>I have apologised to Cassius for killing Brutus, but",
    "<str>unfortunately this may be a deathblow to his potential milk",
    "<str>empire. It's unlikely he's going to want to see my face again.",
    "",
    "<col=ff0000>QUEST COMPLETE!",
}
local HUNT_4378 = {
    "",
    "<col=000080>I have spoken to <col=800000>Redbeard Frank<col=000080>. He has agreed to tell me the",
    "<col=000080>location of some <col=800000>treasure<col=000080> for some <col=800000>Karamja Rum<col=000080>.",
    "",
    "<col=000080>I need to go to <col=800000>Karamja<col=000080> and buy some <col=800000>rum<col=000080>. I hope it is not too",
    "<col=000080>expensive.",
}

-- Fishing Contest, RSProx index 4404: stage 2 [677], stage 4 [1252..1261].
local FC_4404_2 = {
    "<str>The Dwarves will let me use the tunnel through White Wolf",
    "<str><str>Mountain if I can win the Hemenster Fishing Competition",
    "<col=000080>My <col=800000>fishing spot<col=000080> in the contest is <col=800000>next to the willow tree",
    "<col=000080>I need to catch the <col=800000>biggest fish<col=000080> to win!",
}
local FC_4404_4 = {
    "<str>The Dwarves will let me use the tunnel through White Wolf",
    "<str><str>Mountain if I can win the Hemenster Fishing Competition.",
    "<str>I easily won the contest by catching some Giant Carp.",
    "<col=000080>I should take the <col=800000>Trophy<col=000080> back to the <col=800000>Dwarf<col=000080> at the side of <col=800000>White",
    "<col=800000>Wolf Mountain<col=000080> and claim my <col=800000>reward",
}

-- Recruitment Drive, stage 1: index 4413 [132] (before Sir Tiffy took the
-- player in) and index 4414 [134] (rd_spoke_to_tiffy).
local RD_STRUCK = {
    "<str>Sir Amik Varze told me that he had put my name forward as a",
    "<str>potential member of some mysterious organisation.",
}
local RD_4413 = {
    RD_STRUCK[1], RD_STRUCK[2],
    "<col=000080>I should head to <col=800000>Falador Park<col=000080> to meet my <col=800000>Contact<col=000080>, so that I",
    "<col=000080>can begin my <col=800000>testing for the job<col=000080>.",
}
local RD_4414 = {
    RD_STRUCK[1], RD_STRUCK[2],
    "<col=000080>A man named <col=800000>Tiffy<col=000080> brought me <col=800000>here, to the secret training",
    "<col=800000>grounds<col=000080> so that I could be tested for the job.",
    "",
    "<col=000080>I should <col=800000>work out<col=000080> what I am supposed to do to complete these",
    "<col=000080>rooms...",
}

-- Dwarf Cannon, RSProx index 4409: stage 1 [219] and with the six railings
-- fixed [561], 2 [573], 4 [640], 6 [943], 7 [1083], 8 [1140], 10 [1476].
local MC_START = {
    "<str>I have spoken to Captain Lawgof, he recruited me into the",
    "<str>Black Guard and asked me to help the dwarves.",
    "",
}
local function mc_rows(...)
    local rows = {}
    for _, row in ipairs(MC_START) do rows[#rows + 1] = row end
    for _, row in ipairs({ ... }) do rows[#rows + 1] = row end
    return rows
end
local MC_4409 = {
    { 1, false, mc_rows("<col=000080>My first task is to <col=800000>fix the broken railings",
        "<col=000080> in the dwarves defensive perimeter.") },
    { 1, true, mc_rows("<str>I have repaired all the broken railings,",
        "<col=000080>I should report back to Captain Lawgof.") },
    { 2, true, mc_rows("<str>I have repaired all the broken railings,",
        "<col=000080> Captain Lawgof has asked me to check up on his guards at",
        "<col=000080><col=800000>the watchtower <col=000080> to the South of his camp.") },
    { 4, true, mc_rows("<str>I gave the remains to Captain Lawgof.",
        "<col=000080>He sent me to find the <col=800000>Goblin base<col=000080>, South-east of the camp.") },
    { 6, true, mc_rows("<str>I have rescued Lollk and sent him back to the Captain. ",
        "<col=000080>I need to <col=800000>speak to Captain Lawgof<col=000080> again.") },
    { 7, true, mc_rows("<col=000080>Captain Lawgof has asked me to <col=800000>fix the multicannon<col=000080>.") },
    { 8, true, mc_rows("<str>I've fixed the broken multicannon,",
        "<col=000080>I need to <col=800000>speak to Captain Lawgof<col=000080> again.") },
    { 10, true, mc_rows("<str>I've spoken to Nulodion,", "<str>He gave me an ammo mould and notes,",
        "<col=000080>I need to <col=800000>speak to Captain Lawgof<col=000080> again.") },
}

-- Observatory Quest, RSProx index 4412: stage 2 [607] (the bronze bar not
-- carried), stage 3 [634] (the glass carried), stage 4 [674] (no mould),
-- stage 6 [1180].
local OBS_START = {
    "<str>I can start this quest by talking to the professor, in the",
    "<str>Observatory reception, south-west of Ardougne.",
}
local OBS_LIST = {
    "<col=000080>Seems the Observatory telescope needs repairing, due to the",
    "<col=000080>nearby goblins. The <col=800000>professor<col=000080> wants me to help by getting the",
    "<col=000080>following, with the help of his <col=800000>assistant<col=000080>:",
    "",
}
local function obs_rows(...)
    local rows = {}
    for _, part in ipairs({ ... }) do
        for _, row in ipairs(part) do rows[#rows + 1] = row end
    end
    return rows
end
local OBS_4412_2 = obs_rows(OBS_START, OBS_LIST, { "<str>3 plain wooden planks", "<col=800000>1 bronze bar" })
local OBS_4412_3 = obs_rows(OBS_START, OBS_LIST, { "<str>3 plain wooden planks", "<str>1 bronze bar", "<str>1 molten glass" })
local OBS_4412_4 = obs_rows(OBS_START, {
    "<col=000080>Seems the observatory telescope needs repairing due to the",
    "<col=000080>nearby goblins. The <col=800000>professor<col=000080> wants me to help by getting the",
    "<col=000080>following with the help of his <col=800000>assistant<col=000080>:",
    "",
}, { "<str>3 plain wooden planks", "<str>1 bronze bar", "<str>1 molten glass", "<col=800000>1 lens mould" })
local OBS_4412_6 = obs_rows(OBS_START, {
    "<str>Seems the Observatory telescope needs repairing, due to the",
    "<str>nearby goblins. The professor wants me to help by getting the",
    "<str>following, with the help of his assistant:",
    "",
    "<str>3 plain wooden planks",
    "<str>1 bronze bar",
    "<str>1 molten glass",
    "<str>1 lens mould",
    "<str>The professor was pleased to have all the pieces needed to",
    "<str>fix the telescope. Apparently, the professor's last attempt",
    "<str>at Crafting ended in disaster. So, he wants me to create the",
    "<str>lens by using the molten glass with the mould. Fine by me!",
    "<col=000080>The <col=800000>professor<col=000080> has gone ahead to the <col=800000>Observatory<col=000080>. He wants",
    "<col=000080>me to meet him there by travelling through the <col=800000>dungeon<col=000080> below",
    "<col=000080>it.",
    -- and a blank row [1180] qj18, which the reader cannot tell from the end
})

-- Children of the Sun, RSProx index 4408: stage 2 [209], 6 [365], 12 [824]
-- (and the same 14 rows at [889], [899]).
local COTS_STRUCK_1 = {
    "<str>I spoke to Alina and Noah in Varrock Square. They explained",
    "<str>that a Delegation from the Kingdom of Varlamore would soon",
    "<str>be arriving to finalise the opening up of the kingdom to",
    "<str>outsiders.",
}
local COTS_4408_2 = {
    "<col=000080>I spoke to <col=800000>Alina<col=000080> and <col=800000>Noah<col=000080> in <col=800000>Varrock Square<col=000080>. They seemed",
    "<col=000080>quite excited about something. I should talk to them again and",
    "<col=000080>see what it is.",
}
local COTS_4408_6 = {
    COTS_STRUCK_1[1], COTS_STRUCK_1[2], COTS_STRUCK_1[3], COTS_STRUCK_1[4],
    "<col=000080>The <col=800000>Delegation<col=000080> from <col=800000>Varlamore<col=000080> arrived and entered the",
    "<col=000080><col=800000>Palace<col=000080>. However, as they did, I spotted a <col=800000>Guard<col=000080> acting",
    "<col=000080>suspicious. I should follow this <col=800000>Guard<col=000080> and see what they're up",
    "<col=000080>to.",
}
local COTS_4408_12 = {
    COTS_STRUCK_1[1], COTS_STRUCK_1[2], COTS_STRUCK_1[3], COTS_STRUCK_1[4],
    "<str>The Delegation from Varlamore arrived and entered the",
    "<str>Palace. However, as they did, I spotted a Guard acting",
    "<str>suspicious. I followed this Guard to a meeting with some",
    "<str>Bandits. The Guard gave the Bandits some disguises for them",
    "<str>to use to attack the Delegation.",
    "<col=000080>I reported what I'd discovered to <col=800000>Sergeant Tobyn<col=000080> in <col=800000>Varrock",
    "<col=800000>Square<col=000080>. He asked me to point out the <col=800000>Bandits<col=000080> disguised as",
    "<col=000080><col=800000>Guards<col=000080>. There are four for me to find, with all four either",
    "<col=000080>being in <col=800000>Varrock Square<col=000080> or on the road between the <col=800000>Square<col=000080>",
    "<col=000080>and the <col=800000>Eastern Bank<col=000080>.",
}

-- Twilight's Promise, RSProx index 4444: 14 openings, stage 4 [283] .. 38 [2105]
-- (stage, bazaar, cothon, pub, colosseum knight varbits, rows).
local TP_4444 = {
    { tick = 283, stage = 4, knights = { 0, 0, 0, 0 }, rows = {
        "<col=000080>I spoke to <col=800000>Ennius and Furia Tullus<col=000080> outside the <col=800000>Sunrise Palace<col=000080>",
        "<col=000080>in <col=800000>Civitas illa Fortis<col=000080>. They directed me to speak with <col=800000>Prince",
        "<col=800000>Itzla Arkan<col=000080> in the <col=800000>Temple<col=000080> in the south of the city.",
    } },
    { tick = 406, stage = 8, knights = { 0, 0, 0, 0 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<col=000080>I arrived at the <col=800000>Temple<col=000080> in southern <col=800000>Civitas illa Fortis<col=000080>, but",
        "<col=000080><col=800000>Prince Itzla<col=000080> wasn't there. Instead, I was met by <col=800000>Metzli<col=000080>, the",
        "<col=000080><col=800000>Teokan of Ranul<col=000080>. She directed me to the <col=800000>Temple Crypt<col=000080> and said",
        "<col=000080>I'd find <col=800000>Prince Itzla<col=000080> meeting with <col=800000>Servius<col=000080>, the <col=800000>Teokan of Ralos<col=000080>,",
        "<col=000080>down there.",
    } },
    { tick = 535, stage = 12, knights = { 0, 0, 0, 0 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<col=000080>I met with <col=800000>Prince Itzla<col=000080> and <col=800000>Servius<col=000080> in the <col=800000>Temple Crypt<col=000080>. They",
        "<col=000080>asked me to help find out who tried to have <col=800000>Servius<col=000080> killed",
        "<col=000080>during their visit to <col=800000>Varrock<col=000080>. <col=800000>Prince Itzla<col=000080> suspected that one",
        "<col=000080>the <col=800000>Knights of Varlamore<col=000080> who travelled with them might have",
        "<col=000080>been involved. He suggested I speak with <col=800000>Ennius and Furia<col=000080> at",
        "<col=000080>the <col=800000>Palace<col=000080> about finding the <col=800000>Knights<col=000080>.",
    } },
    { tick = 681, stage = 14, knights = { 0, 0, 0, 0 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<col=000080>I returned to <col=800000>Ennius and Furia<col=000080> at the <col=800000>Palace<col=000080>. They directed me",
        "<col=000080>to the <col=800000>Knights<col=000080> and told me to send them to the <col=800000>Palace<col=000080>. They",
        "<col=000080>gave me a <col=800000>Crest<col=000080> to mark me as working for the <col=800000>Varlamorian",
        "<col=800000>Royal Family<col=000080>.",
        "",
        "<col=000080>I need to convince the two <col=800000>Knights<col=000080> at the <col=800000>Bazaar<col=000080> to return to",
        "<col=000080>the <col=800000>Palace<col=000080>.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Cothon<col=000080> to return to the",
        "<col=000080><col=800000>Palace<col=000080>.",
        "",
        "<col=000080>I need to convince the two <col=800000>Knights<col=000080> in the <col=800000>Outer Fortis Pub<col=000080> to",
        "<col=000080>return to the <col=800000>Palace<col=000080>.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Colosseum<col=000080> to return to",
        "<col=000080>the <col=800000>Palace<col=000080>.",
    } },
    { tick = 750, stage = 14, knights = { 1, 0, 0, 0 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<col=000080>I returned to <col=800000>Ennius and Furia<col=000080> at the <col=800000>Palace<col=000080>. They directed me",
        "<col=000080>to the <col=800000>Knights<col=000080> and told me to send them to the <col=800000>Palace<col=000080>. They",
        "<col=000080>gave me a <col=800000>Crest<col=000080> to mark me as working for the <col=800000>Varlamorian",
        "<col=800000>Royal Family<col=000080>.",
        "",
        "<col=000080>I need to convince the two <col=800000>Knights<col=000080> at the <col=800000>Bazaar<col=000080> to return to",
        "<col=000080>the <col=800000>Palace<col=000080>. To do so, I'll need to find a <col=800000>Stolen Amulet<col=000080> by",
        "<col=000080>pickpocketing nearby <col=800000>Citizens<col=000080>.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Cothon<col=000080> to return to the",
        "<col=000080><col=800000>Palace<col=000080>.",
        "",
        "<col=000080>I need to convince the two <col=800000>Knights<col=000080> in the <col=800000>Outer Fortis Pub<col=000080> to",
        "<col=000080>return to the <col=800000>Palace<col=000080>.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Colosseum<col=000080> to return to",
        "<col=000080>the <col=800000>Palace<col=000080>.",
    } },
    { tick = 825, stage = 16, knights = { 3, 0, 0, 0 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<col=000080>I returned to <col=800000>Ennius and Furia<col=000080> at the <col=800000>Palace<col=000080>. They directed me",
        "<col=000080>to the <col=800000>Knights<col=000080> and told me to send them to the <col=800000>Palace<col=000080>. They",
        "<col=000080>gave me a <col=800000>Crest<col=000080> to mark me as working for the <col=800000>Varlamorian",
        "<col=800000>Royal Family<col=000080>.",
        "",
        "<str>I convinced the Knights at the Bazaar to return to the Palace.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Cothon<col=000080> to return to the",
        "<col=000080><col=800000>Palace<col=000080>.",
        "",
        "<col=000080>I need to convince the two <col=800000>Knights<col=000080> in the <col=800000>Outer Fortis Pub<col=000080> to",
        "<col=000080>return to the <col=800000>Palace<col=000080>.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Colosseum<col=000080> to return to",
        "<col=000080>the <col=800000>Palace<col=000080>.",
    } },
    { tick = 1170, stage = 18, knights = { 3, 3, 1, 0 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<col=000080>I returned to <col=800000>Ennius and Furia<col=000080> at the <col=800000>Palace<col=000080>. They directed me",
        "<col=000080>to the <col=800000>Knights<col=000080> and told me to send them to the <col=800000>Palace<col=000080>. They",
        "<col=000080>gave me a <col=800000>Crest<col=000080> to mark me as working for the <col=800000>Varlamorian",
        "<col=800000>Royal Family<col=000080>.",
        "",
        "<str>I convinced the Knights at the Bazaar to return to the Palace.",
        "",
        "<str>I convinced the Knight at the Cothon to return to the Palace.",
        "",
        "<col=000080>I need to convince the two <col=800000>Knights<col=000080> in the <col=800000>Outer Fortis Pub<col=000080> to",
        "<col=000080>return to the <col=800000>Palace<col=000080>. To do so, I'll need to help one of them",
        "<col=000080>sober up by escorting her to the <col=800000>Fountain<col=000080> east of the <col=800000>Pub<col=000080>.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Colosseum<col=000080> to return to",
        "<col=000080>the <col=800000>Palace<col=000080>.",
    } },
    { tick = 1287, stage = 20, knights = { 3, 3, 3, 0 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<col=000080>I returned to <col=800000>Ennius and Furia<col=000080> at the <col=800000>Palace<col=000080>. They directed me",
        "<col=000080>to the <col=800000>Knights<col=000080> and told me to send them to the <col=800000>Palace<col=000080>. They",
        "<col=000080>gave me a <col=800000>Crest<col=000080> to mark me as working for the <col=800000>Varlamorian",
        "<col=800000>Royal Family<col=000080>.",
        "",
        "<str>I convinced the Knights at the Bazaar to return to the Palace.",
        "",
        "<str>I convinced the Knight at the Cothon to return to the Palace.",
        "",
        "<str>I convinced the Knights in Outer Fortis to return to the Palace.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Colosseum<col=000080> to return to",
        "<col=000080>the <col=800000>Palace<col=000080>.",
    } },
    { tick = 1387, stage = 20, knights = { 3, 3, 3, 1 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<col=000080>I returned to <col=800000>Ennius and Furia<col=000080> at the <col=800000>Palace<col=000080>. They directed me",
        "<col=000080>to the <col=800000>Knights<col=000080> and told me to send them to the <col=800000>Palace<col=000080>. They",
        "<col=000080>gave me a <col=800000>Crest<col=000080> to mark me as working for the <col=800000>Varlamorian",
        "<col=800000>Royal Family<col=000080>.",
        "",
        "<str>I convinced the Knights at the Bazaar to return to the Palace.",
        "",
        "<str>I convinced the Knight at the Cothon to return to the Palace.",
        "",
        "<str>I convinced the Knights in Outer Fortis to return to the Palace.",
        "",
        "<col=000080>I need to convince the <col=800000>Knight<col=000080> at the <col=800000>Colosseum<col=000080> to return to",
        "<col=000080>the <col=800000>Palace<col=000080>. To do so, I'll first need to train with him in the",
        "<col=000080><col=800000>Colosseum<col=000080>. He asked me to bring two different <col=800000>Attack Styles<col=000080>",
        "<col=000080>for this.",
    } },
    { tick = 1651, stage = 22, knights = { 3, 3, 3, 3 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<col=000080>I returned to <col=800000>Ennius and Furia<col=000080> at the <col=800000>Palace<col=000080>. Following their",
        "<col=000080>directions, I found the six <col=800000>Knights<col=000080> and sent them to the",
        "<col=000080><col=800000>Palace<col=000080>. With this done, I should speak with <col=800000>Ennius and Furia<col=000080>",
        "<col=000080>again.",
    } },
    { tick = 1770, stage = 24, knights = { 3, 3, 3, 3 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<str>I returned to Ennius and Furia at the Palace. Following their",
        "<str>directions, I found the six Knights and sent them to the",
        "<str>Palace.",
        "<col=000080>Having sent the six <col=800000>Knights<col=000080> to the <col=800000>Palace<col=000080>, <col=800000>Ennius and Furia<col=000080>",
        "<col=000080>suggested I search their quarters within the <col=800000>Kualti",
        "<col=800000>Headquarters<col=000080> for evidence.",
    } },
    { tick = 1877, stage = 28, knights = { 3, 3, 3, 3 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<str>I returned to Ennius and Furia at the Palace. Following their",
        "<str>directions, I found the six Knights and sent them to the",
        "<str>Palace.",
        "<col=000080>Having sent the six <col=800000>Knights<col=000080> to the <col=800000>Palace<col=000080>, <col=800000>Ennius and Furia<col=000080>",
        "<col=000080>suggested I search their quarters within the <col=800000>Kualti",
        "<col=800000>Headquarters<col=000080> for evidence. There, I found a <col=800000>Letter<col=000080> to one of",
        "<col=000080>the <col=800000>Knights<col=000080>, implicating him in the attempted attack. The",
        "<col=000080><col=800000>Letter<col=000080> also mentioned something called <col=800000>Twilight's Promise<col=000080> and",
        "<col=000080>something else called the <col=800000>Final Dawn<col=000080>. I should let <col=800000>Ennius and",
        "<col=800000>Furia<col=000080> know about this.",
    } },
    { tick = 1981, stage = 34, knights = { 3, 3, 3, 3 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<str>I returned to Ennius and Furia at the Palace. Following their",
        "<str>directions, I found the six Knights and sent them to the",
        "<str>Palace.",
        "<str>Having sent the six Knights to the Palace, Ennius and Furia",
        "<str>suggested I search their quarters within the Kualti",
        "<str>Headquarters for evidence. There, I found a Letter to one of",
        "<str>the Knights, implicating him in the attempted attack. The",
        "<str>Letter also mentioned something called Twilight's Promise and",
        "<str>something else called the Final Dawn.",
        "<str>Along with Ennius and Furia, I confronted the Knight implicated",
        "<str>in the Letter. However, before I could question him, Ennius",
        "<str>executed him.",
        "<col=000080>With the <col=800000>Knight<col=000080> dealt with, <col=800000>Ennius and Furia<col=000080> suggested I report",
        "<col=000080>to <col=800000>Prince Itzla<col=000080> and <col=800000>Servius<col=000080> at the <col=800000>Teomat<col=000080> on <col=800000>Ralos' Rise<col=000080>. To get",
        "<col=000080>there, they told me to speak with <col=800000>Regulus Cento<col=000080> outside the",
        "<col=000080><col=800000>Palace<col=000080>.",
    } },
    { tick = 2105, stage = 38, knights = { 3, 3, 3, 3 }, rows = {
        "<str>I spoke to Ennius and Furia Tullus outside the Sunrise Palace",
        "<str>in Civitas illa Fortis. They directed me to speak with Prince",
        "<str>Itzla Arkan in the Temple in the south of the city.",
        "<str>I arrived at the Temple, but Prince Itzla wasn't there. Instead, I",
        "<str>was met by Metzli, the Teokan of Ranul. She directed me to the",
        "<str>Temple Crypt and said I'd find Prince Itzla meeting with",
        "<str>Servius, the Teokan of Ralos, down there.",
        "<str>I met with Prince Itzla and Servius in the Temple Crypt. They",
        "<str>asked me to help find out who tried to have Servius killed",
        "<str>during their visit to Varrock. Prince Itzla suspected that one",
        "<str>the Knights of Varlamore who travelled with them might have",
        "<str>been involved. He suggested I speak with Ennius and Furia at",
        "<str>the Palace about finding the Knights.",
        "<str>I returned to Ennius and Furia at the Palace. Following their",
        "<str>directions, I found the six Knights and sent them to the",
        "<str>Palace.",
        "<str>Having sent the six Knights to the Palace, Ennius and Furia",
        "<str>suggested I search their quarters within the Kualti",
        "<str>Headquarters for evidence. There, I found a Letter to one of",
        "<str>the Knights, implicating him in the attempted attack. The",
        "<str>Letter also mentioned something called Twilight's Promise and",
        "<str>something else called the Final Dawn.",
        "<str>Along with Ennius and Furia, I confronted the Knight implicated",
        "<str>in the Letter. However, before I could question him, Ennius",
        "<str>executed him.",
        "<col=000080>With the <col=800000>Knight<col=000080> dealt with, <col=800000>Ennius and Furia<col=000080> suggested I report",
        "<col=000080>to <col=800000>Prince Itzla<col=000080> and <col=800000>Servius<col=000080> at the <col=800000>Teomat<col=000080> on <col=800000>Ralos' Rise<col=000080>. To get",
        "<col=000080>there, I spoke to <col=800000>Regulus Cento<col=000080> who introduced me to my own",
        "<col=000080>quetzal, <col=800000>Renu<col=000080>.",
    } },
}

-- Tale of the Righteous, RSProx index 4425: stage 5 [894], 6 [914], 14 [1975], 16 [2074].
local TOR_4425_5 = {
    "<str>I spoke to Phileas Rimor in Shayzien. He recently received a",
    "<str>parcel containing evidence to suggest that his ancestor,",
    "<str>Magnus Rimor, was murdered during an expedition to Mount",
    "<str>Quidamortem many years ago. He asked me to visit the",
    "<str>Library Historical Archive to learn more about this expedition.",
    "<str>I spoke to Pagida in the Library Historical Archive. He didn't",
    "<str>have any information on the expedition but did agree to let",
    "<str>me access the Tower of Magic Prison which was built around",
    "<str>the time the expedition took place.",
    "<col=000080>I entered the <col=800000>Tower of Magic Prison<col=000080> and managed to unlock",
    "<col=000080>one of the cells. Inside, I found a skeleton and a strange",
    "<col=000080>message about <col=800000>Lizards<col=000080> carved into the floor. I should let",
    "<col=000080><col=800000>Phileas<col=000080> know about this.",
}
local TOR_4425_6 = {
    "<str>I spoke to Phileas Rimor in Shayzien. He recently received a",
    "<str>parcel containing evidence to suggest that his ancestor,",
    "<str>Magnus Rimor, was murdered during an expedition to Mount",
    "<str>Quidamortem many years ago. He asked me to visit the",
    "<str>Library Historical Archive to learn more about this expedition.",
    "<str>I spoke to Pagida in the Library Historical Archive. He didn't",
    "<str>have any information on the expedition but did agree to let",
    "<str>me access the Tower of Magic Prison which was built around",
    "<str>the time the expedition took place.",
    "<str>I entered the Tower of Magic Prison and managed to unlock",
    "<str>one of the cells. Inside, I found a skeleton and a strange",
    "<str>message about Lizards carved into the floor.",
    "<col=000080>I told <col=800000>Phileas<col=000080> about the message and he's now convinced that",
    "<col=000080>someone survived the expedition. He'd like me to conduct an",
    "<col=000080>expedition of my own to try and find out what the original",
    "<col=000080>team discovered. I'll need to get permission from <col=800000>Lord Shiro",
    "<col=800000>Shayzien<col=000080> to conduct research on <col=800000>Mount Quidamortem<col=000080>. I can",
    "<col=000080>find him in the <col=800000>War Tent<col=000080> in the <col=800000>Shayzien Encampment<col=000080>.",
}
local TOR_4425_14 = {
    "<str>I spoke to Phileas Rimor in Shayzien. He recently received a",
    "<str>parcel containing evidence to suggest that his ancestor,",
    "<str>Magnus Rimor, was murdered during an expedition to Mount",
    "<str>Quidamortem many years ago. He asked me to visit the",
    "<str>Library Historical Archive to learn more about this expedition.",
    "<str>I spoke to Pagida in the Library Historical Archive. He didn't",
    "<str>have any information on the expedition but did agree to let",
    "<str>me access the Tower of Magic Prison which was built around",
    "<str>the time the expedition took place.",
    "<str>I entered the Tower of Magic Prison and managed to unlock",
    "<str>one of the cells. Inside, I found a skeleton and a strange",
    "<str>message about Lizards carved into the floor.",
    "<str>I told Phileas about the message which convinced him that",
    "<str>someone survived the expedition. He asked me to conduct an",
    "<str>expedition of my own to try and find out what the original",
    "<str>team discovered.",
    "<col=000080>Following the advice of <col=800000>Historian Duffy<col=000080>, I entered a cave on",
    "<col=000080>the western face of <col=800000>Mount Quidamortem<col=000080>. Inside, I made some",
    "<col=000080>interesting discoveries. I found out that the cave was the",
    "<col=000080>origin point of the <col=800000>Lizardman<col=000080> species and that they were",
    "<col=000080>created during the time of <col=800000>King Shayzien VII<col=000080>. As well as this, I",
    "<col=000080>discovered a <col=800000>Strange Altar<col=000080> which seemed to be acting as an",
    "<col=000080>amplifier for the <col=800000>Dark Altar<col=000080>. I should return to <col=800000>Lord Shayzien<col=000080>",
    "<col=000080>and let him know what I've discovered.",
}
local TOR_4425_16 = {
    "<str>I spoke to Phileas Rimor in Shayzien. He recently received a",
    "<str>parcel containing evidence to suggest that his ancestor,",
    "<str>Magnus Rimor, was murdered during an expedition to Mount",
    "<str>Quidamortem many years ago. He asked me to visit the",
    "<str>Library Historical Archive to learn more about this expedition.",
    "<str>I spoke to Pagida in the Library Historical Archive. He didn't",
    "<str>have any information on the expedition but did agree to let",
    "<str>me access the Tower of Magic Prison which was built around",
    "<str>the time the expedition took place.",
    "<str>I entered the Tower of Magic Prison and managed to unlock",
    "<str>one of the cells. Inside, I found a skeleton and a strange",
    "<str>message about Lizards carved into the floor.",
    "<str>I told Phileas about the message which convinced him that",
    "<str>someone survived the expedition. He asked me to conduct an",
    "<str>expedition of my own to try and find out what the original",
    "<str>team discovered.",
    "<str>Following the advice of Historian Duffy, I entered a cave on",
    "<str>the western face of Mount Quidamortem. Inside, I made some",
    "<str>interesting discoveries. I found out that the cave was the",
    "<str>origin point of the Lizardman species and that they were",
    "<str>created during the time of King Shayzien VII. As well as this, I",
    "<str>discovered an altar which seemed to be acting as an amplifier",
    "<str>for the Dark Altar.",
    "<str>I told Lord Shayzien about my discoveries on Mount",
    "<str>Quidamortem. He wanted to learn more about the parcel that",
    "<str>Phileas Rimor received and asked me to go and retrieve it.",
    "<col=000080>I went to the home of <col=800000>Phileas Rimor<col=000080> but I found the place",
    "<col=000080>trashed and <col=800000>Phileas<col=000080> gone. I should tell <col=800000>Lord Shayzien<col=000080> about",
    "<col=000080>this.",
}

local GT_4429_20 = {
    "<str>King Narnode has told me that the Grand Tree is dying and",
    "<str>has asked me to help. I have taken a bark sample from the",
    "<str>Grand Tree from King Narnode to Hazelmere.",
    "<col=800000>Hazelmere<col=000080> took the bark sample and gave me a <col=800000>message <col=000080>that",
    "<col=000080>I need to <col=800000>translate<col=000080> using the <col=800000>translation book<col=000080>. I then need to",
    "<col=000080>give the message to <col=800000>King Narnode.",
}
local GT_4429_60 = {
    "<str>King Narnode has told me that the Grand Tree is dying and",
    "<str>has asked me to help. I have taken a bark sample from the",
    "<str>Grand Tree from King Narnode to Hazelmere.",
    "<str>Hazelmere took the bark sample and gave me a message that",
    "<str>I translated and passed on to King Narnode. It seems that",
    "<str>Daconia rocks are killing the Grand Tree!",
    "",
    "<str>On instructions from the King, I found Glough to tell him about",
    "<str>the Daconia rocks.",
    "<str>I told Glough about the Daconia rocks and he blames it on",
    "<str>humans.",
    "<str>I found Charlie, the prisoner, and talked to him.",
    "<col=000080>I spoke to <col=800000>Charlie<col=000080> and agreed to <col=800000>search Glough's house<col=000080>, which",
    "<col=000080>is just in front of the <col=800000>Grand Tree.",
}
local GT_4429_80 = {
    "<str>King Narnode has told me that the Grand Tree is dying and",
    "<str>has asked me to help. I have taken a bark sample from the",
    "<str>Grand Tree from King Narnode to Hazelmere.",
    "<str>Hazelmere took the bark sample and gave me a message that",
    "<str>I translated and passed on to King Narnode. It seems that",
    "<str>Daconia rocks are killing the Grand Tree!",
    "",
    "<str>On instructions from the King, I found Glough to tell him about",
    "<str>the Daconia rocks.",
    "<str>I told Glough about the Daconia rocks and he blames it on",
    "<str>humans.",
    "<str>I found Charlie, the prisoner, and talked to him.",
    "<str>I spoke to Charlie and agreed to search Glough's house, which",
    "<str>is just in front of the Grand Tree.",
    "<str>Glough threw me in prison but the King freed me!",
    "",
    "<col=800000>King Narnode<col=000080> has told me to use the <col=800000>glider<col=000080>. <col=800000>Charlie <col=000080>told me to",
    "<col=000080>go to the <col=800000>shipyard<col=000080> in the <col=800000>Karamja jungle<col=000080> and speak to the",
    "<col=000080><col=800000>foreman<col=000080>. The password is <col=800000>Ka-Lu-Min",
}
local GT_4429_90 = {
    "<str>King Narnode has told me that the Grand Tree is dying and",
    "<str>has asked me to help. I have taken a bark sample from the",
    "<str>Grand Tree from King Narnode to Hazelmere.",
    "<str>Hazelmere took the bark sample and gave me a message that",
    "<str>I translated and passed on to King Narnode. It seems that",
    "<str>Daconia rocks are killing the Grand Tree!",
    "",
    "<str>On instructions from the King, I found Glough to tell him about",
    "<str>the Daconia rocks.",
    "<str>I told Glough about the Daconia rocks and he blames it on",
    "<str>humans.",
    "<str>I found Charlie, the prisoner, and talked to him.",
    "<str>I spoke to Charlie and agreed to search Glough's house, which",
    "<str>is just in front of the Grand Tree.",
    "<str>Glough threw me in prison but the King freed me!",
    "",
    "<str>I crash landed in Karamja jungle and made my way to the",
    "<str>shipyard.",
    "<col=000080>I have an <col=800000>invoice<col=000080> that the foreman had.",
}
local GT_4429_100 = {
    "<str>King Narnode has told me that the Grand Tree is dying and",
    "<str>has asked me to help. I have taken a bark sample from the",
    "<str>Grand Tree from King Narnode to Hazelmere.",
    "<str>Hazelmere took the bark sample and gave me a message that",
    "<str>I translated and passed on to King Narnode. It seems that",
    "<str>Daconia rocks are killing the Grand Tree!",
    "",
    "<str>On instructions from the King, I found Glough to tell him about",
    "<str>the Daconia rocks.",
    "<str>I told Glough about the Daconia rocks and he blames it on",
    "<str>humans.",
    "<str>I found Charlie, the prisoner, and talked to him.",
    "<str>I spoke to Charlie and agreed to search Glough's house, which",
    "<str>is just in front of the Grand Tree.",
    "<str>Glough threw me in prison but the King freed me!",
    "",
    "<str>I crash landed in Karamja jungle and made my way to the",
    "<str>shipyard.",
    "<str>I have an invoice that the foreman had. I should take it back",
    "<str>to the Grand Tree.",
    "",
    "<col=800000>Charlie<col=000080> has suggested I ask <col=800000>Glough's girlfriend<col=000080> for the <col=800000>key<col=000080> to",
    "<col=000080><col=800000>Glough's chest. <col=000080>She lives west of the <col=800000>toad swamp.",
}
local GT_4429_110 = {
    "<str>King Narnode has told me that the Grand Tree is dying and",
    "<str>has asked me to help. I have taken a bark sample from the",
    "<str>Grand Tree from King Narnode to Hazelmere.",
    "<str>Hazelmere took the bark sample and gave me a message that",
    "<str>I translated and passed on to King Narnode. It seems that",
    "<str>Daconia rocks are killing the Grand Tree!",
    "",
    "<str>On instructions from the King, I found Glough to tell him about",
    "<str>the Daconia rocks.",
    "<str>I told Glough about the Daconia rocks and he blames it on",
    "<str>humans.",
    "<str>I found Charlie, the prisoner, and talked to him.",
    "<str>I spoke to Charlie and agreed to search Glough's house, which",
    "<str>is just in front of the Grand Tree.",
    "<str>Glough threw me in prison but the King freed me!",
    "",
    "<str>I crash landed in Karamja jungle and made my way to the",
    "<str>shipyard.",
    "<str>I have an invoice that the foreman had. I should take it back",
    "<str>to the Grand Tree.",
    "",
    "<str>I obtained the key to Glough's chest from Anita, his girlfriend.",
    "<str>I found invasion plans in Glough's chest!",
    "<col=000080>I should talk to the King and see what he says...",
}
local GT_4429_120 = {
    "<str>King Narnode has told me that the Grand Tree is dying and",
    "<str>has asked me to help. I have taken a bark sample from the",
    "<str>Grand Tree from King Narnode to Hazelmere.",
    "<str>Hazelmere took the bark sample and gave me a message that",
    "<str>I translated and passed on to King Narnode. It seems that",
    "<str>Daconia rocks are killing the Grand Tree!",
    "",
    "<str>On instructions from the King, I found Glough to tell him about",
    "<str>the Daconia rocks.",
    "<str>I told Glough about the Daconia rocks and he blames it on",
    "<str>humans.",
    "<str>I found Charlie, the prisoner, and talked to him.",
    "<str>I spoke to Charlie and agreed to search Glough's house, which",
    "<str>is just in front of the Grand Tree.",
    "<str>Glough threw me in prison but the King freed me!",
    "",
    "<str>I crash landed in Karamja jungle and made my way to the",
    "<str>shipyard.",
    "<str>I have an invoice that the foreman had. I should take it back",
    "<str>to the Grand Tree.",
    "",
    "<str>I obtained the key to Glough's chest from Anita, his girlfriend.",
    "<str>I found invasion plans in Glough's chest!",
    "<col=800000>King Narnode <col=000080>isn't convinced but gave me some <col=800000>twigs<col=000080> found",
    "<col=000080>at Glough's house. I wonder what they are for?",
}
local GT_4429_150 = {
    "<str>King Narnode has told me that the Grand Tree is dying and",
    "<str>has asked me to help. I have taken a bark sample from the",
    "<str>Grand Tree from King Narnode to Hazelmere.",
    "<str>Hazelmere took the bark sample and gave me a message that",
    "<str>I translated and passed on to King Narnode. It seems that",
    "<str>Daconia rocks are killing the Grand Tree!",
    "",
    "<str>On instructions from the King, I found Glough to tell him about",
    "<str>the Daconia rocks.",
    "<str>I told Glough about the Daconia rocks and he blames it on",
    "<str>humans.",
    "<str>I found Charlie, the prisoner, and talked to him.",
    "<str>I spoke to Charlie and agreed to search Glough's house, which",
    "<str>is just in front of the Grand Tree.",
    "<str>Glough threw me in prison but the King freed me!",
    "",
    "<str>I crash landed in Karamja jungle and made my way to the",
    "<str>shipyard.",
    "<str>I have an invoice that the foreman had. I should take it back",
    "<str>to the Grand Tree.",
    "",
    "<str>I obtained the key to Glough's chest from Anita, his girlfriend.",
    "<str>I found invasion plans in Glough's chest!",
    "<str>I used the twigs to open a trap door in Glough's house.",
    "",
    "<str>I defeated the Black Demon that Glough set on me! Glough",
    "<str>fled like the coward he really is.",
    "",
    "<str>I should talk to the King and tell him what happened.",
    "",
    "<col=800000>King Narnode<col=000080> has seen the light. I have found a <col=800000>Daconia rock<col=000080>",
    "<col=000080>that was hidden amongst the <col=800000>Grand Tree's roots. I need to",
    "<col=800000>give it to the King.",
}

-- The Forsaken Tower, stage 6 (RSProx index 4424 [1002]): power and furnace
-- done, the refinery tried (2) with the notes found, the altar untouched.
local FT_4424_6 = {
    "<str>I spoke to Lady Vulcana Lovakengj and Councillor Unkar in the",
    "<str>Lovakengj Assembly. They told me that Undor, a master",
    "<str>smith, had found an issue with the Doors of Dinh. However, he",
    "<str>needed some help resolving it.",
    "<str>I spoke to Undor outside the Doors of Dinh. He informed me",
    "<str>that the doors had been damaged by sabotage. However, he",
    "<str>believed that he could repair them. Undor asked me to",
    "<str>recover Dinh's Hammer, the only tool capable of fixing the",
    "<str>doors. Undor believed that the hammer would be found within",
    "<str>The Forsaken Tower, a tower that was once owned by Dinh.",
    "<col=000080>I travelled to <col=800000>The Forsaken Tower<col=000080> to the west of <col=800000>Lovakengj<col=000080>.",
    "<col=000080>Inside, I found <col=800000>Dinh's Hammer<col=000080> sealed in a display case. I'll",
    "<col=000080>need to find a way to open the display case and recover the",
    "<col=000080>hammer.",
    "",
    "<str>I found an old Steam Generator in the basement of the tower",
    "<str>which I managed to activate. The power from the generator",
    "<str>was flowing into a nearby Power Grid which I realigned,",
    "<str>providing power to the whole tower.",
    "",
    "<str>I found an ancient Furnace in the tower which was inactive. I",
    "<str>refilled the Coolant Container supplying the Furnace. I then lit",
    "<str>the Furnace.",
    "",
    "<col=000080>I found an old <col=800000>Refinery<col=000080> in the tower. It's still in working order",
    "<col=000080>but it will need cleansing before it can be activated. I found",
    "<col=000080>some <col=800000>Old Notes<col=000080> on some shelves which might be useful.",
}

-- RSProx index 4431 [665].
local DP_4431_20 = {
    "<col=000080>I have offered to help <col=800000>Denulth<col=000080> by finding <col=800000>another way<col=000080> up",
    "<col=000080><col=800000>Death Plateau<col=000080>. I also need to find the <col=800000>combination<col=000080> to the",
    "<col=000080><col=800000>equipment room<col=000080> and <col=800000>unlock<col=000080> the door.",
    "",
    "<col=800000>Eohric<col=000080> said that the <col=800000>equipment room guard<col=000080> is staying at the",
    "<col=000080>local inn, the <col=800000>Toad and Chicken",
}
-- RSProx index 4431 [726].
local DP_4431_30 = {
    "<col=000080>I have offered to help <col=800000>Denulth<col=000080> by finding <col=800000>another way<col=000080> up",
    "<col=000080><col=800000>Death Plateau<col=000080>. I also need to find the <col=800000>combination<col=000080> to the",
    "<col=000080><col=800000>equipment room<col=000080> and <col=800000>unlock<col=000080> the door.",
    "",
    "<str>The equipment room guard is staying at the local inn, the Toad",
    "<str>and Chicken. The guard wouldn't talk to me!",
}
-- RSProx index 4431 [762].
local DP_4431_40 = {
    "<col=000080>I have offered to help <col=800000>Denulth<col=000080> by finding <col=800000>another way<col=000080> up",
    "<col=000080><col=800000>Death Plateau<col=000080>. I also need to find the <col=800000>combination<col=000080> to the",
    "<col=000080><col=800000>equipment room<col=000080> and <col=800000>unlock<col=000080> the door.",
    "",
    "<str>The equipment room guard is staying at the local inn, the Toad",
    "<str>and Chicken. The guard wouldn't talk to me!",
    "<col=800000>Eohric<col=000080> says to buy the <col=800000>guard<col=000080> a <col=800000>drink",
}
-- RSProx index 4431 [972].
local DP_4431_50 = {
    "<col=000080>I have offered to help <col=800000>Denulth<col=000080> by finding <col=800000>another way<col=000080> up",
    "<col=000080><col=800000>Death Plateau<col=000080>. I also need to find the <col=800000>combination<col=000080> to the",
    "<col=000080><col=800000>equipment room<col=000080> and <col=800000>unlock<col=000080> the door.",
    "",
    "<str>The equipment room guard is staying at the local inn, the Toad",
    "<str>and Chicken. The guard wouldn't talk to me! I bought the guard",
    "<str>a drink and he seemed more helpful.",
}
-- RSProx index 4431 [1025].
local DP_4431_55 = {
    "<col=000080>I have offered to help <col=800000>Denulth<col=000080> by finding <col=800000>another way<col=000080> up",
    "<col=000080><col=800000>Death Plateau<col=000080>. I also need to find the <col=800000>combination<col=000080> to the",
    "<col=000080><col=800000>equipment room<col=000080> and <col=800000>unlock<col=000080> the door.",
    "",
    "<str>The equipment room guard is staying at the local inn, the Toad",
    "<str>and Chicken. The guard wouldn't talk to me! I bought the guard",
    "<str>a drink and he seemed more helpful. I gambled with the guard",
    "<str>until he ran out of money. He wrote me an IOU.",
}
-- RSProx index 4431 [1122].
local DP_4431_70_DOOR = {
    "<col=000080>I have offered to help <col=800000>Denulth<col=000080> by finding <col=800000>another way<col=000080> up",
    "<col=000080><col=800000>Death Plateau<col=000080>.",
    "",
    "<str>I have found the combination to the equipment room and",
    "<str>unlocked the door.",
    "",
    "<str>The equipment room guard is staying at the local inn, the Toad",
    "<str>and Chicken. The guard wouldn't talk to me! I bought the guard",
    "<str>a drink and he seemed more helpful. I gambled with the guard",
    "<str>until he ran out of money. He wrote me an IOU.",
    "",
    "<str>It turned out the IOU was written on the back of the",
    "<str>combination! I put the stone balls in the right places on the",
    "<str>stone mechanism and unlocked the door.",
}
-- RSProx index 4431 [1437].
local DP_4431_70_DUNSTAN = {
    "<col=000080>I have offered to help <col=800000>Denulth<col=000080> by finding <col=800000>another way<col=000080> up",
    "<col=000080><col=800000>Death Plateau<col=000080>.",
    "",
    "<str>I have found the combination to the equipment room and",
    "<str>unlocked the door.",
    "",
    "<str>The equipment room guard is staying at the local inn, the Toad",
    "<str>and Chicken. The guard wouldn't talk to me! I bought the guard",
    "<str>a drink and he seemed more helpful. I gambled with the guard",
    "<str>until he ran out of money. He wrote me an IOU.",
    "",
    "<str>It turned out the IOU was written on the back of the",
    "<str>combination! I put the stone balls in the right places on the",
    "<str>stone mechanism and unlocked the door.",
    "",
    "<str>I found the sherpa's house.",
    "<col=800000>Tenzing<col=000080> will show me a <col=800000>secret way<col=000080> up Death Plateau if I get him",
    "<col=000080>some items:",
    "<col=800000>Ten loaves of bread.",
    "<str>Ten cooked trout.",
    "<col=800000>Spiked boots.",
    "",
    "<col=800000>Tenzing<col=000080> gave me his <col=800000>climbing boots<col=000080>, I need to take them to",
    "<col=000080><col=800000>Dunstan<col=000080> in <col=800000>Burthorpe<col=000080> for the <col=800000>spikes",
    "",
    "<col=800000>Dunstan<col=000080> will help me if I get his <col=800000>son signed up<col=000080> for the <col=800000>Imperial",
    "<col=800000>Guard<col=000080>. I will need an <col=800000>Iron bar<col=000080> for the boots",
}
-- RSProx index 4431 [1500].
local DP_4431_70_CERT = {
    "<col=000080>I have offered to help <col=800000>Denulth<col=000080> by finding <col=800000>another way<col=000080> up",
    "<col=000080><col=800000>Death Plateau<col=000080>.",
    "",
    "<str>I have found the combination to the equipment room and",
    "<str>unlocked the door.",
    "",
    "<str>The equipment room guard is staying at the local inn, the Toad",
    "<str>and Chicken. The guard wouldn't talk to me! I bought the guard",
    "<str>a drink and he seemed more helpful. I gambled with the guard",
    "<str>until he ran out of money. He wrote me an IOU.",
    "",
    "<str>It turned out the IOU was written on the back of the",
    "<str>combination! I put the stone balls in the right places on the",
    "<str>stone mechanism and unlocked the door.",
    "",
    "<str>I found the sherpa's house.",
    "<col=800000>Tenzing<col=000080> will show me a <col=800000>secret way<col=000080> up Death Plateau if I get him",
    "<col=000080>some items:",
    "<col=800000>Ten loaves of bread.",
    "<str>Ten cooked trout.",
    "<col=800000>Spiked boots.",
    "",
    "<col=800000>Tenzing<col=000080> gave me his <col=800000>climbing boots<col=000080>, I need to take them to",
    "<col=000080><col=800000>Dunstan<col=000080> in <col=800000>Burthorpe<col=000080> for the <col=800000>spikes",
    "",
    "<col=800000>Dunstan<col=000080> will help me if I get his <col=800000>son signed up<col=000080> for the <col=800000>Imperial",
    "<col=800000>Guard<col=000080>. I will need an <col=800000>Iron bar<col=000080> for the boots",
    "",
    "<col=800000>Denulth<col=000080> gave me a <col=800000>certificate<col=000080> to prove that Dunstan's son",
    "<col=000080>has been signed up for the Imperial Guard.",
}
-- RSProx index 4455 [294].
local TS_4455_10 = {
    "<col=000080>I promised <col=800000>Denulth<col=000080> that I would rescue <col=800000>Godric <col=000080>from the <col=800000>Troll",
    "<col=800000>Stronghold",
}
-- RSProx index 4455 [999], [1075].
local TS_4455_20 = {
    "<col=000080>I promised <col=800000>Denulth<col=000080> that I would rescue <col=800000>Godric <col=000080>from the <col=800000>Troll",
    "<col=800000>Stronghold",
    "",
    "<str>I got some climbing boots from Tenzing.",
    "",
    "<str>I have defeated the Troll Champion.",
    "",
    "<col=000080>I have to find a way to get into the <col=800000>Troll Stronghold",
    "",
    "<col=000080>I have to find a way to get into the <col=800000>prison",
}
-- RSProx index 4455 [1539].
local TS_4455_20_IN = {
    "<col=000080>I promised <col=800000>Denulth<col=000080> that I would rescue <col=800000>Godric <col=000080>from the <col=800000>Troll",
    "<col=800000>Stronghold",
    "",
    "<str>I got some climbing boots from Tenzing.",
    "",
    "<str>I have defeated the Troll Champion.",
    "",
    "<str>I have found my way into the Troll Stronghold.",
    "",
    "<col=000080>I have to find a way to get into the <col=800000>prison",
}
-- RSProx index 4455 [1837].
local TS_4455_20_KEY = {
    "<col=000080>I promised <col=800000>Denulth<col=000080> that I would rescue <col=800000>Godric <col=000080>from the <col=800000>Troll",
    "<col=800000>Stronghold",
    "",
    "<str>I got some climbing boots from Tenzing.",
    "",
    "<str>I have defeated the Troll Champion.",
    "",
    "<str>I have found my way into the Troll Stronghold.",
    "",
    "<str>I have the prison key.",
    "<col=000080>I have to get into the <col=800000>prison",
}
-- RSProx index 4455 [1916].
local TS_4455_30 = {
    "<col=000080>I promised <col=800000>Denulth<col=000080> that I would rescue <col=800000>Godric <col=000080>from the <col=800000>Troll",
    "<col=800000>Stronghold",
    "",
    "<str>I got some climbing boots from Tenzing.",
    "",
    "<str>I have defeated the Troll Champion.",
    "",
    "<str>I found my way into the prison.",
    "",
    "<col=000080>I have to rescue <col=800000>Godric",
}
-- RSProx index 4455 [2126].
local TS_4455_30_EADGAR = {
    "<col=000080>I promised <col=800000>Denulth<col=000080> that I would rescue <col=800000>Godric <col=000080>from the <col=800000>Troll",
    "<col=800000>Stronghold",
    "",
    "<str>I got some climbing boots from Tenzing.",
    "",
    "<str>I have defeated the Troll Champion.",
    "",
    "<str>I found my way into the prison.",
    "",
    "<str>I've rescued Mad Eadgar.",
    "<col=000080>I have to rescue <col=800000>Godric",
}
-- RSProx index 4455 [2175], [2214].
local TS_4455_40 = {
    "<col=000080>I promised <col=800000>Denulth<col=000080> that I would rescue <col=800000>Godric <col=000080>from the <col=800000>Troll",
    "<col=800000>Stronghold",
    "",
    "<str>I got some climbing boots from Tenzing.",
    "",
    "<str>I have defeated the Troll Champion.",
    "",
    "<str>I found my way into the prison.",
    "",
    "<str>I've rescued Godric and Mad Eadgar.",
    "<col=000080>I should return and tell <col=800000>Dunstan <col=000080>his son is safe.",
}
-- RSProx index 4431 [1762].
local DP_4431_70_SCOUTED = {
    "<str>I have found another way up Death Plateau for Denulth.",
    "",
    "<str>I have found the combination to the equipment room and",
    "<str>unlocked the door.",
    "",
    "<str>The equipment room guard is staying at the local inn, the Toad",
    "<str>and Chicken. The guard wouldn't talk to me! I bought the guard",
    "<str>a drink and he seemed more helpful. I gambled with the guard",
    "<str>until he ran out of money. He wrote me an IOU.",
    "",
    "<str>It turned out the IOU was written on the back of the",
    "<str>combination! I put the stone balls in the right places on the",
    "<str>stone mechanism and unlocked the door.",
    "",
    "<str>I found the sherpa's house. I gave Tenzing the ten loaves of",
    "<str>bread, ten cooked trout and the Spiked boots. Tenzing has",
    "<str>given me a map of the secret way. I checked the secret way",
    "<str>and it is safe for the Imperial Guard to use.",
    "",
    "<str>I have given Dunstan the certificate to prove that his son has",
    "<str>been signed up for the Imperial Guard. Dunstan made me the",
    "<str>Spiked boots.",
    "",
    "<col=000080>I should go and tell <col=800000>Denulth<col=000080> I've completed my mission.",
}

-- Scorpion Catcher, RSProx index 4421 [1730]: stage 2 with the full cage held.
-- Holy Grail, RSProx index 4435: every state the recorder opened ([125] 2,
-- [420] 3, [1588] 4, [2888] 8, [2989] 8 with the feather, [3146] 9, [3447] 9
-- with the Grail). Jagex's stray mid-row <str> tags are part of the rows.
local HG_4435_2 = {
    "<col=800000>King Arthur<col=000080> has sent me questing for the <col=800000>Holy Grail<col=000080> of <col=000080>legend.",
    "<col=000080>I should start my quest by speaking to <col=800000>Merlin<col=000080> in <col=800000>his study",
    "<col=800000>next to the Camelot library<col=000080> for directions to it.",
}
local HG_4435_3 = {
    "<str>I started my Quest for the Holy Grail in Camelot Castle.",
    "<str>King Arthur sent me to Merlin for advice on locating it.",
    "<col=800000>Merlin<col=000080> suggested two things to help find the <col=800000>Grail<col=000080>:",
    "<col=000080>Speak to <col=800000>Galahad<col=000080> who lives <col=800000>West<col=000080> of <col=800000>McGrubor's Wood<col=000080>.",
    "<col=000080>Talk to someone on a '<col=800000>Holy Island<col=000080>' he can't remember<col=000080>.",
}
local HG_4435_4 = {
    "<str>I started my Quest for the Holy Grail in Camelot Castle.",
    "<str>King Arthur sent me to Merlin for advice on locating it.",
    "<col=000080>According to a Crone on <col=800000>Entrana<col=000080> I need to go to where the <col=000080>'<col=800000>Six",
    "<col=800000>Heads' face<col=000080> and blow a <col=800000>magic whistle<col=000080> there.",
    "<col=000080>To get the <col=800000>Magic Whistle<col=000080> I need to carry something from <col=000080>the",
    "<col=000080><col=800000>Realm of the Fisher King<col=000080> to a <col=800000>Haunted House<col=000080>...",
}
local HG_4435_8 = {
    "<str>I started my Quest for the Holy Grail in Camelot Castle.",
    "<str>King Arthur sent me to Merlin for advice on locating it.",
    "<str>I spoke to Galahad in his shack West of McGrubor's Wood.",
    "<str>Galahad gave me a napkin from the Realm of the Fisher King. <str>I",
    "<str>used the napkin to find a holy whistle that could <str>teleport me",
    "<str>to the Realm of the Fisher King.",
    "<str>I blew the Whistle at the correct location and was <str>teleported",
    "<str>to the Realm of the Fisher King. The path to <str>the Fisher King's",
    "<str>castle was blocked by a mighty warrior <str>called the Black Knight",
    "<str>Titan who seemed invincible!",
    "<str>I defeated the Black Knight Titan with Excalibur's power.<str>Once",
    "<str>past the Titan I entered the Grail Castle.",
    "<str>The Fisher King couldn't give me the Grail, but legends <str>say",
    "<str>that the person who restores the land could claim the Grail.",
    "<col=000080>The <col=800000>Fisher King<col=000080> is very sick. He has asked me to find <col=000080>his son",
    "<col=000080><col=800000>Sir Percival<col=000080>, a <col=800000>Knight of the Round Table<col=000080>.",
}
local HG_4435_8F = {
    "<str>I started my Quest for the Holy Grail in Camelot Castle.",
    "<str>King Arthur sent me to Merlin for advice on locating it.",
    "<str>I spoke to Galahad in his shack West of McGrubor's Wood.",
    "<str>Galahad gave me a napkin from the Realm of the Fisher King. <str>I",
    "<str>used the napkin to find a holy whistle that could <str>teleport me",
    "<str>to the Realm of the Fisher King.",
    "<str>I blew the Whistle at the correct location and was <str>teleported",
    "<str>to the Realm of the Fisher King. The path to <str>the Fisher King's",
    "<str>castle was blocked by a mighty warrior <str>called the Black Knight",
    "<str>Titan who seemed invincible!",
    "<str>I defeated the Black Knight Titan with Excalibur's power.<str>Once",
    "<str>past the Titan I entered the Grail Castle.",
    "<str>The Fisher King couldn't give me the Grail, but legends <str>say",
    "<str>that the person who restores the land could claim the Grail.",
    "<col=000080>The <col=800000>Fisher King<col=000080> is very sick. He has asked me to find <col=000080>his son",
    "<col=000080><col=800000>Sir Percival<col=000080>, a <col=800000>Knight of the Round Table<col=000080>.",
    "<col=800000>King Arthur<col=000080> gave me a <col=800000>magic golden feather<col=000080> to help locate <col=800000>Sir",
    "<col=800000>Percival<col=000080> - I should use it to find him!",
}
local HG_4435_9 = {
    "<str>I started my Quest for the Holy Grail in Camelot Castle.",
    "<str>King Arthur sent me to Merlin for advice on locating it.",
    "<str>I spoke to Galahad in his shack West of McGrubor's Wood.",
    "<str>Galahad gave me a napkin from the Realm of the Fisher King. <str>I",
    "<str>used the napkin to find a holy whistle that could <str>teleport me",
    "<str>to the Realm of the Fisher King.",
    "<str>I blew the Whistle at the correct location and was <str>teleported",
    "<str>to the Realm of the Fisher King. The path to <str>the Fisher King's",
    "<str>castle was blocked by a mighty warrior <str>called the Black Knight",
    "<str>Titan who seemed invincible!",
    "<str>I defeated the Black Knight Titan with Excalibur's power.<str>Once",
    "<str>past the Titan I entered the Grail Castle.",
    "<str>The Fisher King couldn't give me the Grail, but legends <str>say",
    "<str>that the person who restores the land could claim the Grail.",
    "<str>I honoured the Fisher King's request to find his son, and <str>used",
    "<str>a Magic Golden Feather to track him down. When he <str>heard of",
    "<str>his father's illness he rushed back to the Grail <str>Castle using a",
    "<str>Magic Whistle that I gave him.",
    "<col=000080>I should follow him to the <col=800000>Castle<col=000080> to get the <col=800000>Holy Grail<col=000080>.",
}
local HG_4435_9G = {
    "<str>I started my Quest for the Holy Grail in Camelot Castle.",
    "<str>King Arthur sent me to Merlin for advice on locating it.",
    "<str>I spoke to Galahad in his shack West of McGrubor's Wood.",
    "<str>Galahad gave me a napkin from the Realm of the Fisher King. <str>I",
    "<str>used the napkin to find a holy whistle that could <str>teleport me",
    "<str>to the Realm of the Fisher King.",
    "<str>I blew the Whistle at the correct location and was <str>teleported",
    "<str>to the Realm of the Fisher King. The path to <str>the Fisher King's",
    "<str>castle was blocked by a mighty warrior <str>called the Black Knight",
    "<str>Titan who seemed invincible!",
    "<str>I defeated the Black Knight Titan with Excalibur's power.<str>Once",
    "<str>past the Titan I entered the Grail Castle.",
    "<str>The Fisher King couldn't give me the Grail, but legends <str>say",
    "<str>that the person who restores the land could claim the Grail.",
    "<str>I honoured the Fisher King's request to find his son, and <str>used",
    "<str>a Magic Golden Feather to track him down. When he <str>heard of",
    "<str>his father's illness he rushed back to the Grail <str>Castle using a",
    "<str>Magic Whistle that I gave him.",
    "<col=000080>Now I have the <col=800000>Grail<col=000080> with me. I should take it to <col=800000>Arthur<col=000080>.",
}
local SC_4421_2 = {
    "<str>I've spoken to Thormac, he's lost his pet Kharid Scorpions and",
    "<str>needs my help to find them.",
    "",
    "<str>I've spoken to the Seer and been given the location of one of",
    "<str>the Kharid Scorpions.",
    "",
    "<str>The first Kharid Scorpion is in a secret room near some Nasty",
    "<str>Spiders with two Coffins nearby.",
    "",
    "<col=000080>I need to talk to the <col=800000>Seer<col=000080> again now I've caught the first",
    "<col=000080><col=800000>Kharid Scorpion.",
}
-- Merlin's Crystal, every stage: RSProx index 4433 ([517] 1, [720] 2, [824] 3,
-- [1989] and [3517] 4 by what is in hand, [4603] 5, [4781] 6, [4814] 7).
local MC_INTRO = {
    "<str>I spoke to King Arthur and he said I would be worthy of",
    "<str><str>becoming a Knight of the Round Table if I could free <str>Merlin",
    "<str>from a giant crystal that he has been trapped in.",
}
local MC_GL = {
    "<str>Gawain told me it was the work of Morgan Le Faye.",
    "<str>I told Lancelot of Gawain's suspicions, and he told me <str>that",
    "<str>Mordred's Fortress is not completely impenetratable.",
}
local MC_KEEP = {
    "<str>I stowed away in a shipment of candles and reached the",
    "<str><str>Stronghold of Sir Mordred, and bested him in combat.",
    "<str>In return for sparing her son, Morgan Le Faye told me how I",
    "<str>could break the spell that had imprisoned Merlin.",
}
local MC_SUMMONED = {
    "<str>I summoned the Spirit Thrantax and forced him to aid me <str>in",
    "<str>breaking Morgan Le Fayes curse upon Merlin. He used <str>his",
    "<str>magic to weaken the crystal so that a blow from <str>Excalibur",
    "<str>would be able to shatter it for good.",
}
local function mc_rows(...)
    local out = {}
    for _, part in ipairs({ ... }) do
        for _, row in ipairs(part) do out[#out + 1] = row end
    end
    return out
end
local MC_STAGE4_HEAD = {
    "<col=000080>I need to summon the spirit <col=800000>Thrantax<col=000080> at a <col=800000>magic symbol <col=000080>as",
    "<col=000080>close to the Crystal as I can possibly find.",
    "<col=000080>To bind <col=800000>Thrantax<col=000080> to my will I will need the following:",
}
local MC_4433 = {
    [1] = mc_rows(MC_INTRO, { "",
        "<col=000080>I should ask the <col=800000>other Knights<col=000080> if they have any <col=800000>advice <col=000080>for me",
        "<col=000080>on how I should go about doing this." }),
    [2] = mc_rows(MC_INTRO, {
        "<col=800000>Gawain<col=000080> thinks this was the work of <col=800000>Morgan Le Faye<col=000080> who <col=000080>lives in",
        "<col=000080>an <col=800000>impenetrable fortress<col=000080> south of here full of <col=800000>renegade",
        "<col=800000>knights<col=000080> led by the evil <col=800000>Sir Mordred" }),
    [3] = mc_rows(MC_INTRO, MC_GL, {
        "<col=000080>There might be a way to enter with a <col=800000>delivery by sea<col=000080>..." }),
    ["4a"] = mc_rows(MC_INTRO, MC_GL, MC_KEEP, MC_STAGE4_HEAD, {
        "<col=000080>Some <col=800000>magic words<col=000080> from a <col=800000>Chaos Altar",
        "<col=000080>A <col=800000>lit Black Candle",
        "<col=000080>The <col=800000>Holy Sword Excalibur<col=000080> from the <col=800000>Lady of the Lake",
        "<str>I have some bat bones with me" }),
    ["4b"] = mc_rows(MC_INTRO, MC_GL, MC_KEEP, MC_STAGE4_HEAD, {
        "<str>I have the magic words from the Chaos Altar memorised",
        "<str>I have a lit black candle with me",
        "<str>I have the Holy Sword Excalibur with me",
        "<str>I have some bat bones with me" }),
    [5] = mc_rows(MC_INTRO, MC_GL, MC_KEEP, {
        "<str>I need to summon the spirit <str>Thrantax<str> at a <str>magic symbol <str>as",
        "<str>close to the Crystal as I can possibly find." }, MC_SUMMONED, {
        "<col=000080>I should free <col=800000>Merlin<col=000080> by using <col=800000>Excalibur<col=000080> on the <col=800000>crystal" }),
    [6] = mc_rows(MC_INTRO, MC_GL, MC_KEEP, {
        "<str>I need to summon the spirit <str>Thrantax<col=000080> at a <str>magic symbol <str>as",
        "<str><col=000080>close to the Crystal as I can possibly find." }, MC_SUMMONED, {
        "<str>I freed Merlin by shattering the Crystal that trapped him",
        "<col=000080>Now that <col=800000>Merlin<col=000080> is freed I should speak to <col=800000>King Arthur<col=000080> to <col=000080>claim",
        "<col=000080>my <col=800000>reward<col=000080> and become a <col=800000>Knight of the Round Table" }),
    [7] = mc_rows(MC_INTRO, MC_GL, MC_KEEP, {
        "<str>I need to summon the spirit <str>Thrantax<str> at a <str>magic symbol <str>as",
        "<str>close to the Crystal as I can possibly find." }, MC_SUMMONED, {
        "<str>I freed Merlin by shattering the Crystal that trapped him <str>and",
        "<str>when I told King Arthur how I had singlehandedly <str>freed Merlin",
        "<str>from his prison when his other Knights had <str>failed he",
        "<str>immediately made me a Knight of the Round Table",
        "",
        "<col=ff0000>QUEST COMPLETE!" }),
}
-- The Dig Site, RSProx index 4446: every distinct journal the recorder opened
-- (stages 1..6; see itexam_journal.rs2 for what each key holds).
local DS_4446 = {
    ["1"] = {  -- [287]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<col=000080>I need to return the <col=800000>letter of recommendation<col=000080> from the",
        "<col=000080><col=800000>Curator of Varrock Museum<col=000080> to the <col=800000>Examiner<col=000080> at the Exam",
        "<col=000080>Centre for inspection.",
    },
    ["2a"] = {  -- [1133]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<str>I need to return the letter of recommendation from the",
        "<str>Curator of Varrock Museum, to the Examiner at the Exam",
        "<str>Centre for inspection.",
        "<col=000080>I need to study for my first exam. Perhaps the <col=800000>students<col=000080> on",
        "<col=000080>the digsite can help?",
    },
    ["2b"] = {  -- [1223]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<str>I need to return the letter of recommendation from the",
        "<str>Curator of Varrock Museum, to the Examiner at the Exam",
        "<str>Centre for inspection.",
        "<str>I need to study for my first exam. Perhaps the students on",
        "<str>the digsite can help?",
        "<str>I have agreed to help the student in the green top.",
        "<str>He has lost his Animal Skull and thinks he may have dropped it",
        "<str>around the digsite. I need to find it and return it to him;",
        "<str>maybe someone has picked it up?",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the purple skirt.",
        "<str>She has lost her Lucky Mascot and thinks she may have",
        "<str>dropped it by one of the large urns at the edge of the digsite.",
        "<str>I need to find it and return it to her.",
        "<str>I should talk to her to see if she can help with my exams.",
        "<str>She gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the orange top.",
        "<str>He has lost his Special Cup and thinks he may have dropped it",
        "<str>around the tents near the panning site. I need to find it and",
        "<str>return it to him.",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "<col=000080>I should talk to the Examiner to take my first exam. If I have",
        "<col=000080>forgotten anything I can always ask the students again.",
    },
    ["3"] = {  -- [1553]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<str>I need to return the letter of recommendation from the",
        "<str>Curator of Varrock Museum, to the Examiner at the Exam",
        "<str>Centre for inspection.",
        "<str>I need to study for my first exam. Perhaps the students on",
        "<str>the digsite can help?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I have agreed to help the student in the green top.",
        "<str>He has lost his Animal Skull and thinks he may have dropped it",
        "<str>around the digsite. I need to find it and return it to him;",
        "<str>maybe someone has picked it up?",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the purple skirt.",
        "<str>She has lost her Lucky Mascot and thinks she may have",
        "<str>dropped it around the large urns on the digsite. I need to find",
        "<str>it and return it to her.",
        "<str>I should talk to her to see if she can help with my exams.",
        "<str>She gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the orange top.",
        "<str>He has lost his Special Cup and thinks he may have dropped it",
        "<str>around the tents near the panning site. I need to find it and",
        "<str>return it.",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "<str>I should talk to the Examiner to take my first exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my first Earth Science exam.",
        "<str>I need to study for my second exam. Perhaps the three",
        "<str>students on the digsite can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<col=000080>I should talk to the Examiner to take my second exam. If I have",
        "<col=000080>forgotten anything I can always ask the students again.",
    },
    ["4a"] = {  -- [1727]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<str>I need to return the letter of recommendation from the",
        "<str>Curator of Varrock Museum, to the Examiner at the Exam",
        "<str>Centre for inspection.",
        "<str>I need to study for my first exam. Perhaps the students on",
        "<str>the digsite can help?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I have agreed to help the student in the green top.",
        "<str>He has lost his Animal Skull and thinks he may have dropped it",
        "<str>around the digsite. I need to find it and return it to him;",
        "<str>maybe someone has picked it up?",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the purple skirt.",
        "<str>She has lost her Lucky Mascot and thinks she may have",
        "<str>dropped it around the large urns on the digsite. I need to find",
        "<str>it and return it to her.",
        "<str>I should talk to her to see if she can help with my exams.",
        "<str>She gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the orange top.",
        "<str>He has lost his Special Cup and thinks he may have dropped it",
        "<str>around the tents near the panning site. I need to find it and",
        "<str>return it.",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "<str>I should talk to the Examiner to take my first exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my first Earth Science exam.",
        "<str>I need to study for my second exam. Perhaps the three",
        "<str>students on the digsite can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I should talk to the Examiner to take my second exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my second Earth Science exam.",
        "<str>I should research for my second exam. Perhaps the students",
        "<str>can help me again?",
        "<col=000080>I need to speak to the student in the green top about the",
        "<col=000080>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to bring her an opal.",
        "<col=000080>I need to speak to the student in the orange top about the",
        "<col=000080>exams.",
    },
    ["4b"] = {  -- [1765]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<str>I need to return the letter of recommendation from the",
        "<str>Curator of Varrock Museum, to the Examiner at the Exam",
        "<str>Centre for inspection.",
        "<str>I need to study for my first exam. Perhaps the students on",
        "<str>the digsite can help?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I have agreed to help the student in the green top.",
        "<str>He has lost his Animal Skull and thinks he may have dropped it",
        "<str>around the digsite. I need to find it and return it to him;",
        "<str>maybe someone has picked it up?",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the purple skirt.",
        "<str>She has lost her Lucky Mascot and thinks she may have",
        "<str>dropped it around the large urns on the digsite. I need to find",
        "<str>it and return it to her.",
        "<str>I should talk to her to see if she can help with my exams.",
        "<str>She gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the orange top.",
        "<str>He has lost his Special Cup and thinks he may have dropped it",
        "<str>around the tents near the panning site. I need to find it and",
        "<str>return it.",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "<str>I should talk to the Examiner to take my first exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my first Earth Science exam.",
        "<str>I need to study for my second exam. Perhaps the three",
        "<str>students on the digsite can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I should talk to the Examiner to take my second exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my second Earth Science exam.",
        "<str>I should research for my second exam. Perhaps the students",
        "<str>can help me again?",
        "<col=000080>I need to speak to the student in the green top about the",
        "<col=000080>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to bring her an opal.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
    },
    ["5a"] = {  -- [1902]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<str>I need to return the letter of recommendation from the",
        "<str>Curator of Varrock Museum, to the Examiner at the Exam",
        "<str>Centre for inspection.",
        "<str>I need to study for my first exam. Perhaps the students on",
        "<str>the digsite can help?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I have agreed to help the student in the green top.",
        "<str>He has lost his Animal Skull and thinks he may have dropped it",
        "<str>around the digsite. I need to find it and return it to him;",
        "<str>maybe someone has picked it up?",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the purple skirt.",
        "<str>She has lost her Lucky Mascot and thinks she may have",
        "<str>dropped it around the large urns on the digsite. I need to find",
        "<str>it and return it to her.",
        "<str>I should talk to her to see if she can help with my exams.",
        "<str>She gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the orange top.",
        "<str>He has lost his Special Cup and thinks he may have dropped it",
        "<str>around the tents near the panning site. I need to find it and",
        "<str>return it.",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "<str>I should talk to the Examiner to take my first exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my first Earth Science exam.",
        "<str>I need to study for my second exam. Perhaps the three",
        "<str>students on the digsite can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I should talk to the Examiner to take my second exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my second Earth Science exam.",
        "<str>I should research for my third exam. Perhaps the students",
        "<str>can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to bring her an opal.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I should talk to the Examiner to take my third exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my third and final Earth Science exam.",
        "<col=000080>I need a find from the digsite to <col=800000>impress the Expert.",
    },
    ["5b"] = {  -- [3155]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<str>I need to return the letter of recommendation from the",
        "<str>Curator of Varrock Museum, to the Examiner at the Exam",
        "<str>Centre for inspection.",
        "<str>I need to study for my first exam. Perhaps the students on",
        "<str>the digsite can help?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I have agreed to help the student in the green top.",
        "<str>He has lost his Animal Skull and thinks he may have dropped it",
        "<str>around the digsite. I need to find it and return it to him;",
        "<str>maybe someone has picked it up?",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the purple skirt.",
        "<str>She has lost her Lucky Mascot and thinks she may have",
        "<str>dropped it around the large urns on the digsite. I need to find",
        "<str>it and return it to her.",
        "<str>I should talk to her to see if she can help with my exams.",
        "<str>She gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the orange top.",
        "<str>He has lost his Special Cup and thinks he may have dropped it",
        "<str>around the tents near the panning site. I need to find it and",
        "<str>return it.",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "<str>I should talk to the Examiner to take my first exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my first Earth Science exam.",
        "<str>I need to study for my second exam. Perhaps the three",
        "<str>students on the digsite can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I should talk to the Examiner to take my second exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my second Earth Science exam.",
        "<str>I should research for my third exam. Perhaps the students",
        "<str>can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to bring her an opal.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I should talk to the Examiner to take my third exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my third and final Earth Science exam.",
        "<str>I need a find from the digsite to impress the Expert.",
        "<col=000080>I need to take the <col=800000>letter<col=000080> to a workman near a <col=800000>winch<col=000080>.",
    },
    ["6"] = {  -- [3285]
        "<str>I should speak to the Examiner about taking Earth Science",
        "<str>Exams.",
        "<str>I should take the letter the Examiner has given me to the",
        "<str>Curator of Varrock Museum for his approval.",
        "<str>I need to return the letter of recommendation from the",
        "<str>Curator of Varrock Museum, to the Examiner at the Exam",
        "<str>Centre for inspection.",
        "<str>I need to study for my first exam. Perhaps the students on",
        "<str>the digsite can help?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I have agreed to help the student in the green top.",
        "<str>He has lost his Animal Skull and thinks he may have dropped it",
        "<str>around the digsite. I need to find it and return it to him;",
        "<str>maybe someone has picked it up?",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the purple skirt.",
        "<str>She has lost her Lucky Mascot and thinks she may have",
        "<str>dropped it around the large urns on the digsite. I need to find",
        "<str>it and return it to her.",
        "<str>I should talk to her to see if she can help with my exams.",
        "<str>She gave me an answer to one of the questions on the first",
        "<str>exam.",
        "",
        "<str>I agreed to help the student in the orange top.",
        "<str>He has lost his Special Cup and thinks he may have dropped it",
        "<str>around the tents near the panning site. I need to find it and",
        "<str>return it.",
        "<str>I should talk to him to see if he can help with my exams.",
        "<str>He gave me an answer to one of the questions on the first",
        "<str>exam.",
        "<str>I should talk to the Examiner to take my first exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my first Earth Science exam.",
        "<str>I need to study for my second exam. Perhaps the three",
        "<str>students on the digsite can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I should talk to the Examiner to take my second exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my second Earth Science exam.",
        "<str>I should research for my third exam. Perhaps the students",
        "<str>can help me again?",
        "<str>I need to speak to the student in the green top about the",
        "<str>exams.",
        "<str>I need to speak to the student in the purple skirt about the",
        "<str>exams.",
        "<str>I need to bring her an opal.",
        "<str>I need to speak to the student in the orange top about the",
        "<str>exams.",
        "<str>I should talk to the Examiner to take my third exam. If I have",
        "<str>forgotten anything I can always ask the students again.",
        "<str>I have passed my third and final Earth Science exam.",
        "<str>I need a find from the digsite to impress the Expert.",
        "<str>I need to take the letter to a workman near a winch.",
        "<col=000080>I need to <col=800000>investigate the dig shafts.",
        "<col=000080>I need to find a way to move the rocks blocking the way in the",
        "<col=000080>shaft. Perhaps someone can help me.",
    },
}
-- The Golem, RSProx index 4473: every distinct journal the recorder opened
-- ([493] stage 1 with the letter read, [622] 2 with it, [824] 2 after Elissa,
-- [997] 2 with the notes, [1159] 3, [1313] 3 with the statuette, [1392] 5,
-- [1500] 6, [1754] 7).
local G_SPOKE = "<str>I have spoken to the golem."
local G_REPAIRED = "<str>I have repaired the golem."
local G_PORTAL = { "<col=000080>The golem asked me to open the <col=800000>portal<col=000080> so it can defeat a", "<col=000080><col=800000>demon<col=000080>." }
local G_PORTAL_DONE = { "<str>The golem asked me to open the portal so it could defeat a", "<str>demon." }
local G_LETTER = { "<col=000080>Maybe I should speak to <col=800000>Elissa<col=000080> at the <col=800000>Digsite<col=000080> about the letter I", "<col=000080>found in Uzer." }
local G_ELISSA = "<str>I talked to Elissa about the letter from the desert."
local G_NOTES = "<str>I took Varmen's notes from the Digsite Exam Centre."
local function rows(...)
    local out = {}
    for _, part in ipairs({ ... }) do
        if type(part) == "table" then
            for _, r in ipairs(part) do out[#out + 1] = r end
        else
            out[#out + 1] = part
        end
    end
    return out
end
local GOLEM_4473 = {
    ["1"] = rows(G_SPOKE, "<col=000080>The <col=800000>golem<col=000080> asked me to repair him.", G_LETTER),
    ["2a"] = rows(G_SPOKE, G_REPAIRED, G_PORTAL, "<col=000080>To open the portal I will need to find a missing <col=800000>statuette<col=000080>.", G_LETTER),
    ["2b"] = rows(G_SPOKE, G_REPAIRED, G_PORTAL, "<col=000080>To open the portal I will need to find a missing <col=800000>statuette<col=000080>.", G_ELISSA,
        "<col=000080>Elissa told me that <col=800000>Varmen's expedition notes<col=000080> are in the", "<col=000080>library in the <col=800000>Exam Centre<col=000080>."),
    ["2c"] = rows(G_SPOKE, G_REPAIRED, G_PORTAL, "<col=000080>To open the portal I will need to find a missing <col=800000>statuette<col=000080>.", G_ELISSA, G_NOTES,
        "<col=000080>According to <col=800000>Varmen's notes<col=000080>, he removed the <col=800000>statuette<col=000080>", "<col=000080>during his expedition."),
    ["3a"] = rows(G_SPOKE, G_REPAIRED, G_PORTAL, G_ELISSA, G_NOTES,
        "<col=000080>To open the portal I will need the missing statuette which is in", "<col=000080>the <col=800000>Varrock Museum<col=000080>."),
    ["3b"] = rows(G_SPOKE, G_REPAIRED, G_PORTAL, G_ELISSA, G_NOTES, "<str>I have retrieved the missing statuette from the Varrock", "<str>Museum.",
        "<col=000080>I should take the <col=800000>statuette<col=000080> to Uzer and put it back in the", "<col=000080>temple."),
    ["5"] = rows(G_SPOKE, G_REPAIRED, G_PORTAL_DONE, G_ELISSA, G_NOTES, "<str>I have retrieved the missing statuette from the Varrock",
        "<str>Museum and repaired the puzzle in the temple.", "<str>I have opened the portal.", "<col=000080>I should find out what happened to the <col=800000>demon<col=000080>."),
}
GOLEM_4473["6"] = rows(G_SPOKE, G_REPAIRED, G_PORTAL_DONE, G_ELISSA, G_NOTES, "<str>I have retrieved the missing statuette from the Varrock",
    "<str>Museum and repaired the puzzle in the temple.", "<str>I have opened the portal.", "<str>I have visited the demon's lair and seen its skeleton.",
    "<col=000080>I should inform the <col=800000>golem<col=000080> that the <col=800000>demon<col=000080> is dead.")
GOLEM_4473["7"] = rows(G_SPOKE, G_REPAIRED, G_PORTAL_DONE, G_ELISSA, G_NOTES, "<str>I have retrieved the missing statuette from the Varrock",
    "<str>Museum and repaired the puzzle in the temple.", "<str>I have opened the portal.", "<str>I have visited the demon's lair and seen its skeleton.",
    "<str>I told the golem that the demon was dead, but it did not", "<str>believe me!",
    "<col=000080>I should find some way to convince the <col=800000>golem<col=000080> that its task is", "<col=000080>done.")
return {
    id = "journals",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv" },
    run = function(t)
        t.ticks(3)
        local function read(display)
            local r, j = t.ui.journal_open(display)
            if r == "ok" then
                t.ui.journal_close()
            end
            return r, j
        end
        local function exact(name, display, want)
            local r, j = read(display)
            local lines = (r == "ok" and j and j.lines) or {}
            local diff = nil
            for i = 1, math.max(#want, #lines) do
                if lines[i] ~= (want[i] and plain_read(want[i])) then
                    diff = "row " .. i .. ": want '" .. tostring(want[i]) .. "' read '" .. tostring(lines[i]) .. "'"
                    break
                end
            end
            t.check(name, r == "ok" and diff == nil, tostring(r) .. " " .. #lines .. " rows; " .. (diff or "every row matches"))
        end
        local function shape(name, display, struck_min, current)
            local r, j = read(display)
            local lines = (r == "ok" and j and j.lines) or {}
            local found = false
            for _, l in ipairs(lines) do
                if current and string.find(l, current, 1, true) then found = true end
            end
            t.check(name, r == "ok" and #lines > struck_min and (current == nil or found),
                tostring(r) .. " " .. #lines .. " rows; current '" .. tostring(current) .. "' " .. (found and "found" or "MISSING"))
        end
        local function set(cmd)
            t.cheat(cmd, false)
            t.ticks(2)
        end

        set("::setvar varb20106_cowquest 22")
        exact("journal.idesofmilk.complete", "The Ides of Milk", IOM_4381)
        set("::setvar varb20106_cowquest 12")
        shape("journal.idesofmilk.duke", "The Ides of Milk", 10, "I spoke to the Duke of Lumbridge")

        set("::setvar varp71_hunt 1")
        exact("journal.hunt.rum", "Pirate's Treasure", HUNT_4378)

        set("::setvar varp11_fishingcompo 2")
        exact("journal.fishingcontest.willow", "Fishing Contest", FC_4404_2)
        set("::setvar varp11_fishingcompo 4")
        exact("journal.fishingcontest.won", "Fishing Contest", FC_4404_4)

        set("::setvar varb657_rd_main 1")
        set("::setvar varb692_rd_spoke_to_tiffy 0")
        exact("journal.recruitmentdrive.park", "Recruitment Drive", RD_4413)
        set("::setvar varb692_rd_spoke_to_tiffy 1")
        exact("journal.recruitmentdrive.grounds", "Recruitment Drive", RD_4414)

        for _, st in ipairs(MC_4409) do
            local fixed = st[2] and 1 or 0
            for b = 2240, 2245 do
                set("::setvar varb" .. b .. "_mcannon_railing" .. (b - 2239) .. "_fixed " .. fixed)
            end
            set("::setvar varp0_mcannon " .. st[1])
            exact("journal.dwarfcannon." .. st[1] .. (st[2] and "" or ".railings"), "Dwarf Cannon", st[3])
        end

        set("::setvar varp112_itgronigen 2")
        exact("journal.observatory.bronze", "Observatory Quest", OBS_4412_2)
        set("::give molten_glass 1")
        set("::setvar varp112_itgronigen 3")
        exact("journal.observatory.glass", "Observatory Quest", OBS_4412_3)
        set("::clearinv")
        set("::setvar varp112_itgronigen 4")
        exact("journal.observatory.mould", "Observatory Quest", OBS_4412_4)
        set("::setvar varp112_itgronigen 6")
        exact("journal.observatory.gone_ahead", "Observatory Quest", OBS_4412_6)

        set("::setvar varb9632_vmq1 2")
        exact("journal.childrenofthesun.2", "Children of the Sun", COTS_4408_2)
        set("::setvar varb9632_vmq1 6")
        exact("journal.childrenofthesun.6", "Children of the Sun", COTS_4408_6)
        set("::setvar varb9632_vmq1 12")
        exact("journal.childrenofthesun.12", "Children of the Sun", COTS_4408_12)

        for _, j in ipairs(TP_4444) do
            set("::setvar varb9829_vmq2_bazaar_knights " .. j.knights[1])
            set("::setvar varb9830_vmq2_cothon_knight " .. j.knights[2])
            set("::setvar varb9831_vmq2_pub_knights " .. j.knights[3])
            set("::setvar varb9832_vmq2_colosseum_knight " .. j.knights[4])
            set("::setvar varb9649_vmq2 " .. j.stage)
            exact("journal.twilightspromise." .. j.tick, "Twilight's Promise", j.rows)
        end

        for _, st in ipairs({ { 5, TOR_4425_5 }, { 6, TOR_4425_6 }, { 14, TOR_4425_14 }, { 16, TOR_4425_16 } }) do
            set("::setvar varb6358_shayzienquest " .. st[1])
            exact("journal.taleoftherighteous." .. st[1], "Tale of the Righteous", st[2])
        end

        for _, st in ipairs({ { 20, GT_4429_20 }, { 60, GT_4429_60 }, { 80, GT_4429_80 }, { 90, GT_4429_90 },
            { 100, GT_4429_100 }, { 110, GT_4429_110 }, { 120, GT_4429_120 } }) do
            set("::setvar varp150_grandtree " .. st[1])
            exact("journal.grandtree." .. st[1], "The Grand Tree", st[2])
        end
        set("::give grandtree_daconiarock 1")
        set("::setvar varp150_grandtree 150")
        exact("journal.grandtree.150", "The Grand Tree", GT_4429_150)
        set("::clearinv")

        set("::setvar varb7796_lovaquest 6")
        set("::setvar varb7797_lovaquest_electricity 4")
        set("::setvar varb7798_lovaquest_furnace 4")
        set("::setvar varb7799_lovaquest_refinery 2")
        set("::setvar varb7804_lovaquest_foundhammer 1")
        set("::setvar varb7808_lovaquest_foundnotes 1")
        exact("journal.forsakentower.6", "The Forsaken Tower", FT_4424_6)

        set("::give scorpioncagefull 1")
        set("::setvar varp76_scorpcatcher 2")
        exact("journal.scorpioncatcher.2", "Scorpion Catcher", SC_4421_2)
        set("::clearinv")
        for _, st in ipairs({ 1, 2, 3, 5, 6, 7 }) do
            set("::setvar varp14_arthur " .. st)
            exact("journal.merlinscrystal." .. st, "Merlin's Crystal", MC_4433[st])
        end
        set("::setvar varp14_arthur 4")
        set("::setvar varp6436_excalibur_components_progress 0")
        set("::give bat_bones 1")
        exact("journal.merlinscrystal.4a", "Merlin's Crystal", MC_4433["4a"])
        set("::setvar varp6436_excalibur_components_progress 1")
        set("::give lit_black_candle 1")
        set("::give excalibur 1")
        exact("journal.merlinscrystal.4b", "Merlin's Crystal", MC_4433["4b"])
        set("::clearinv")
        set("::setvar varp6436_excalibur_components_progress 0")
        set("::setvar varp14_arthur 0")

        -- Death Plateau, RSProx index 4431: every state the recorder opened.
        -- Stage 70's paragraphs follow the path's progress (varp6442_death_map
        -- bits 0..3) and Tenzing's items what the backpack holds ([1437]: ten
        -- trout, the bread short, no spiked boots).
        set("::setvar varp6442_death_map 0")
        for _, s2 in ipairs({ { 20, DP_4431_20 }, { 30, DP_4431_30 }, { 40, DP_4431_40 },
                { 50, DP_4431_50 }, { 55, DP_4431_55 }, { 70, DP_4431_70_DOOR } }) do
            set("::setvar varp314_death_equiproom " .. s2[1])
            exact("journal.deathplateau." .. s2[1] .. (s2[1] == 70 and ".door" or ""), "Death Plateau", s2[2])
        end
        set("::clearinv")
        set("::give trout 10")
        set("::setvar varp6442_death_map 3")
        exact("journal.deathplateau.70.dunstan", "Death Plateau", DP_4431_70_DUNSTAN)
        set("::setvar varp6442_death_map 4")
        exact("journal.deathplateau.70.cert", "Death Plateau", DP_4431_70_CERT)
        set("::setvar varp6442_death_map 8")
        exact("journal.deathplateau.70.scouted", "Death Plateau", DP_4431_70_SCOUTED)
        set("::setvar varp6442_death_map 0")
        set("::setvar varp314_death_equiproom 0")
        set("::clearinv")

        -- Troll Stronghold, RSProx index 4455: every state the recorder opened.
        -- The boots row stands on Dad's warning having been heard (varbit
        -- troll_told) or boots held; the key row on the key in the backpack.
        set("::clearinv")
        set("::setvar varp317_troll_quest 10")
        exact("journal.trollstronghold.10", "Troll Stronghold", TS_4455_10)
        set("::setvar varb4_troll_told 1")
        set("::setvar varp317_troll_quest 20")
        exact("journal.trollstronghold.20", "Troll Stronghold", TS_4455_20)
        set("::setvar varb2_troll_entered_stronghold 1")
        exact("journal.trollstronghold.20.in", "Troll Stronghold", TS_4455_20_IN)
        set("::give troll_key_prison 1")
        exact("journal.trollstronghold.20.key", "Troll Stronghold", TS_4455_20_KEY)
        set("::clearinv")
        set("::setvar varp317_troll_quest 30")
        exact("journal.trollstronghold.30", "Troll Stronghold", TS_4455_30)
        set("::setvar varb0_troll_freed_eadgar 1")
        exact("journal.trollstronghold.30.eadgar", "Troll Stronghold", TS_4455_30_EADGAR)
        set("::setvar varp317_troll_quest 40")
        exact("journal.trollstronghold.40", "Troll Stronghold", TS_4455_40)
        for _, v in ipairs({ "varb0_troll_freed_eadgar", "varb2_troll_entered_stronghold", "varb4_troll_told", "varp317_troll_quest" }) do
            set("::setvar " .. v .. " 0")
        end

        -- Holy Grail, RSProx index 4435: every state the recorder opened; stage
        -- 8 with and without the feather, 9 with and without the Grail.
        for _, st in ipairs({ { 2, HG_4435_2 }, { 3, HG_4435_3 }, { 4, HG_4435_4 }, { 8, HG_4435_8 },
                { 9, HG_4435_9 } }) do
            set("::setvar varp5_grail " .. st[1])
            exact("journal.holygrail." .. st[1], "Holy Grail", st[2])
        end
        set("::give magic_golden_feather 1")
        set("::setvar varp5_grail 8")
        exact("journal.holygrail.8.feather", "Holy Grail", HG_4435_8F)
        set("::clearinv")
        set("::give holy_grail 1")
        set("::setvar varp5_grail 9")
        exact("journal.holygrail.9.grail", "Holy Grail", HG_4435_9G)
        set("::clearinv")
        set("::setvar varp5_grail 0")
        set("::clearinv")
        local MM = {
            { 5, 1, "I agreed to sail to the island" },
            { 20, 3, "I need to stop the killer" },
            { 90, 6, "I must investigate the manor" },
            { 105, 3, "I must confront the killer." },
            { 120, 4, "They turned on one another" },
            { 130, 5, "Mandy left to tend to her wounds" },
            { 135, 5, "QUEST COMPLETE!" },
        }
        for _, s in ipairs(MM) do
            set("::setvar varb3468_mistmyst_progress " .. s[1])
            shape("journal.misthalin." .. s[1], "Misthalin Mystery", s[2], s[3])
        end

        set("::setvar varp178_vampire 1")
        shape("journal.vampire.1", "Vampyre Slayer", 0, "suggested I begin by seeking out")
        set("::setvar varp178_vampire 2")
        shape("journal.vampire.2", "Vampyre Slayer", 3, "seem willing to talk to me")
        set("::setvar varb15609_harlow_given_beer 1")
        shape("journal.vampire.beer", "Vampyre Slayer", 3, "I should see what he can tell me")
        set("::setvar varb15610_harlow_given_stake 1")
        shape("journal.vampire.stake", "Vampyre Slayer", 3, "If I lose it")
        set("::setvar varp178_vampire 3")
        shape("journal.vampire.complete", "Vampyre Slayer", 6, "QUEST COMPLETE!")

        -- The Dig Site, RSProx index 4446 [287..3285]: stage, errands (two bits a
        -- student: student1 0-1, student3 2-3, student2 4-5) and the flags.
        local function ds(stage, errands, curator, expert, hint)
            set("::setvar varp131_itexamlevel " .. stage)
            set("::setvar varp6605_itexam_errands " .. errands)
            set("::setvar varb2552_itcuratorletter " .. curator)
            set("::setvar varb2550_itexpertletter " .. expert)
            set("::setvar varb2549_itdigsitehint " .. hint)
        end
        ds(1, 0, 1, 0, 0) exact("journal.digsite.1", "The Dig Site", DS_4446["1"])
        ds(2, 0, 1, 0, 0) exact("journal.digsite.2a", "The Dig Site", DS_4446["2a"])
        ds(2, 42, 1, 0, 0) exact("journal.digsite.2b", "The Dig Site", DS_4446["2b"])
        ds(3, 42, 1, 0, 0) exact("journal.digsite.3", "The Dig Site", DS_4446["3"])
        ds(4, 32, 1, 0, 0) exact("journal.digsite.4a", "The Dig Site", DS_4446["4a"])
        ds(4, 40, 1, 0, 0) exact("journal.digsite.4b", "The Dig Site", DS_4446["4b"])
        ds(5, 0, 1, 0, 0) exact("journal.digsite.5a", "The Dig Site", DS_4446["5a"])
        ds(5, 0, 1, 1, 0) exact("journal.digsite.5b", "The Dig Site", DS_4446["5b"])
        ds(6, 0, 1, 1, 1) exact("journal.digsite.6", "The Dig Site", DS_4446["6"])
        ds(0, 0, 0, 0, 0)

        local function golem(a, b, statuette)
            set("::setvar varb346_golem_a " .. a)
            set("::setvar varb347_golem_b " .. b)
            set("::setvar varb355_golem_retrieved_statuette " .. statuette)
        end
        golem(1, 1, 0) exact("journal.golem.1", "The Golem", GOLEM_4473["1"])
        golem(2, 1, 0) exact("journal.golem.2a", "The Golem", GOLEM_4473["2a"])
        golem(2, 2, 0) exact("journal.golem.2b", "The Golem", GOLEM_4473["2b"])
        golem(2, 3, 0) exact("journal.golem.2c", "The Golem", GOLEM_4473["2c"])
        golem(3, 3, 0) exact("journal.golem.3a", "The Golem", GOLEM_4473["3a"])
        golem(3, 3, 1) exact("journal.golem.3b", "The Golem", GOLEM_4473["3b"])
        golem(5, 3, 1) exact("journal.golem.5", "The Golem", GOLEM_4473["5"])
        golem(6, 3, 1) exact("journal.golem.6", "The Golem", GOLEM_4473["6"])
        golem(7, 3, 1) exact("journal.golem.7", "The Golem", GOLEM_4473["7"])
        golem(0, 0, 0)

        -- The strip icons carry no server events (cache clickmask=2; Jagex arms
        -- none, RSProx index 4289 [0], [509]) and still switch the body.
        t.exec("strip.tab", t.ui.tab, "quests")
        t.ticks(4)
        local _, summary_icon = t.ui.widget("side_journal:summary_list")
        t.expect("strip.summary", t.ui.invoke(summary_icon, 1))
        t.expect("strip.summary.open", t.ui.await_open("account_summary_sidepanel", 5))
        local _, quest_icon = t.ui.widget("side_journal:quest_list")
        t.expect("strip.quests", t.ui.invoke(quest_icon, 1))
        t.expect("strip.quests.open", t.ui.await_open("questlist", 5))
        t.ticks(2)

        -- Row ops on Pandemonium (quest id 206): Show on map opens the world
        -- map (index 3430 [376]); the quick guide prints Jagex's line (3055 [22]).
        local _, row = t.ui.widget("questlist:list", 206)
        t.expect("row.quickguide", t.ui.invoke(row, 5))
        t.ticks(2)
        t.expect("row.quickguide.line", t.msg.expect("A quick guide for Pandemonium has been opened in the Old School RuneScape Wiki."))
        t.expect("row.showonmap", t.ui.invoke(row, 3))
        t.expect("row.showonmap.open", t.ui.await_open("worldmap", 5))
        t.finish(0)
    end,
}
