## talkToMaisa is a talk ACROSS the chasm (seam matthew-mbp-m4-b53-seam3 contact_chasm_walk_to_maisa)

- **The map is right; nothing walks to Maisa.** She stands "at the other end of a gaping chasm" (wiki
  Contact! oldid 15292391, walkthrough), and she says Kaleef "wouldn't have been able to get across this
  chasm" (Transcript:Contact! oldid 15263370, "Talking to Maisa"). Quest Helper Contact.java:244 puts her
  at 2258,4317, "on the west side of the chasm". The chasm is `maps/m35_67.jm2`'s flag-1 strip at
  x2259..2263, unbroken from z4298 to z4338, with no bridge flag on level 1. It renders as a dark ravine
  (shot 049 below). No ladder, landing or map edit was needed.
- **Content fix, committed as OSRS-Content fd1bf2f29f**, in
  `server/scripts/quests/quest_contact/scripts/contact_maisa.rs2`: `[apnpc1,contact_maisa_multi]` and
  `[apnpc1,contact_maisa]` are stacked above the existing `[opnpc1,...]` headers. It is the same approach
  trigger Hudon uses across the river (`quest_waterfall/scripts/hudon.rs2:8`, LostCity
  `hudon.rs2`). The default ap range is 10, and the line of sight runs over the open chasm.
- **Rows.** These replace the committed probe and `t.blocked`. All of them were measured in
  `build/quest_gate/b53s3_contact_copy`: SUMMARY 131, pass=130, fail=0, plus one end marker. The script is
  `build/seam_state/matthew-mbp-m4-b53-seam3/scratch/contact_copy.lua`.
  1. After `quest.stage.read`, `walk_to` 2264,4314, then `walk_to` 2264,4317. You are on the east lip,
     5 tiles from her.
  2. `t.exec("talkToMaisa", t.player.talk_to, "contact_maisa_multi", 1)` -> `page none->npc`.
  3. `chat.play`: `npc:you're not Kaleef`, `player:Kaleef is dead`, `npc:Where was Prince Ali imprisoned`,
     `choose:Draynor Village.`, `player:Draynor Village`, `npc:Who helped free him`.
  4. The port ends the first talk after question 1, so Talk-to her again (`talkToMaisa.again`). The page is
     the options page.
  5. `chat.play`: `choose:Leela.`, `player:Leela`, `npc:I believe you`, `npc:Find him in Al Kharid`. Mind
     the capital F.
  6. Then `t.ticks(2)` and `quest.stage.met_maisa` (60).
  7. `goto-osman` 3288,3180 (plain travel; the next step is in Al Kharid). Then
     `talkToOsman` `contact_osman_multi` + `chat.play`: `player:I've just come from Maisa`,
     `npc:Menaphos and Al Kharid have been rivals`,
     `choose:It could drive a wedge between the Menaphite cities.`, `player:It could drive a wedge`,
     `npc:Now that is interesting`. Then `quest.stage.told_osman` (70).
- **Next unproved:** `talkToOsmanOutsideSoph` (`contact_osman_desert_multi`, QH 3285,2812), then the second
  descent into the private chasm.
- **Port dialogue is not the transcript.** The transcript asks both questions in one conversation and
  offers an "I don't know." row. That is a separate parity leg; this seam did not change it.
