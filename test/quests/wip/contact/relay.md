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

## killGiantScarab is a real level-191 fight now: stage the character for it (seam matthew-mbp-m4-b53-seam4 contact_giant_scarab_combat_stats)

- **What changed (content, uncommitted in this pass):** `quest_contact/configs/contact.npc` (new) gives
  `contact_scarab_boss` the wiki's stats: Giant_Scarab_(Contact!) oldid 15328051, 130 hp, attack 169,
  strength 190, defence 169, ranged 190, max hit 20, stab defence 70, speed 4, aggressive, poison 9.
  `contact_scarab.rs2` gives it its own swing. It stabs a player standing next to its 3x3 body. It shoots
  a player anywhere else, and that ranged swing also poisons them: "The vast scarab clacks its
  mandibles and you are mystically poisoned" (poison 9, and it overrides an antipoison). The four
  summons are now the Contact! versions, `contact_insectoid_mage_b` (level 66) and
  `contact_locust_bow_b` / `contact_locust_lance_b` (level 68), with 20 hp each (wiki Scarab_Mage oldid
  15281960, Locust_rider oldid 15281959, cache stats). They are no longer the dungeon's level 93/98/106
  forms. A server read was added: `t.cheat("::contact_scarab_hp")` prints
  `Giant Scarab hitpoints <cur>/<base> ...`. The client bar is 30 wide, so it cannot tell 130 from 10.
- **The round-3 staging dies.** Abyssal whip, no armour, 10 lobsters, eating below 50: in
  `build/quest_gate/b53s4_contact_copy_r3` (a verbatim copy of `round3_rejected.lua`) rows 1-212 pass,
  then `player.died` lands 73 ticks into `killGiantScarab.dead` with the boss still at 25/30. Whip slash
  against slash defence 99 barely lands, and the boss plus four summons hit for about 5 a tick.
- **Staging that wins** (`build/quest_gate/b53s4_contact_copy_geared3`, SUMMARY 241 PASS 241/0): change
  only the setup and the eat item.
  - Setup:
    `"::give rune_full_helm 1", "::give rune_chainbody 1", "::give rune_platelegs 1", "::give zamorak_spear 1", "::give shark 20"`
    then
    `"::wield rune_full_helm", "::wield rune_chainbody", "::wield rune_platelegs", "::wield zamorak_spear"`.
    Drop the whip and the lobsters. `rune_platebody` refuses without Dragon Slayer.
  - The spear is the wiki's own advice: "weaker to stab than any other melee style, so a Zamorakian spear
    is recommended". Quest Helper Contact.java:133/136 lists combat gear and food as brought along.
  - The kill: `t.npc.await_dead_engaged(400, 40, { eat = { item = "shark", below = 50 } })` ->
    `dead after 213 tick(s), 11 re-engagement(s) ... ate shark 18 time(s)`. Then `quest.stage.scarab_killed`
    110, pickUpKeris, Osman, ready 120.
- **Walk back to the ladder before leaveChasm.** The fight ends far from the ladder, and `leaveChasm`
  failed `screen_position: yaw 1093 framed nothing in 5 poses`. Add
  `t.player.walk_to(6442, 70, 60)` first. That is the arrival tile minus 14,4; the copy reads it from
  `chasm2.arrived`, 6456,74, and walk_to the arrival tile itself stalls at 6442,70. After that,
  `leaveChasm` PASSes `map_flag` and `chasm.left` reads 2116,4364,2.
- **Margins.** Two runs of this staging (`geared2` and `geared3`) gave the same 213 ticks and 18 sharks,
  with a lowest hp of 25. Twenty sharks left 2 spare, so do not bring fewer.
- **Not in the port (known_gaps in docs/bosses/quest_combat_manifest.json, quest-contact):**
  - The summons all appear on entry. The wiki says the Scarab summons them one at a time, cycling
    magic, ranged, melee, but no source gives the rate.
  - The Scarab's light-extinguish (QH Contact.java:261; Quick guide oldid 15233716) is absent: no rate
    or message is sourced, and the chasm has no darkness. Do not assert it.
- **Closer re-run (seam4 closer, `build/quest_gate/b53s4c_close_geared`, 241/0):** the same staging
  took 203 ticks and ate all 20 sharks (lowest hp 19/99). Runs are not tick-reproducible
  (the embedded server's clock is not locked), so the "2 spare" above is not a margin: give
  `"::give shark 25"` (or add prayer) when rewriting `contact.lua`.
