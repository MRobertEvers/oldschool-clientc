# Enter the Abyss (miniquest) from RSProx 4423

Index 4423 (rev 241.1, w369, 2026-10-29, "Enter the Abyss miniquest") records
the whole miniquest end to end in 595 ticks. Recorder: Rune Mysteries complete,
`abyssal_miniquest` (varp 492) 0 at login. Cites are `[tick]` in 4423 unless
another index is named. Supporting sessions: 821 (rev 234, Battle Runes shop
after the miniquest), 1677 (rev 226) and 3201 (rev 232), both the Varrock Mage
after the miniquest with Temple of the Eye in progress.

Content: `quests/quest_entertheabyss/` (script, `.varp`, `.inv`), the shared
essence teleport `skill_runecraft/scripts/essence_mine.rs2`, Aubury
(`areas/varrock/scripts/aubury.rs2`), Cromperty
(`areas/area_ardougne_east/scripts/wizard_cromperty.rs2`). Test:
`test/quests/entertheabyss.lua`.

The previous port was built from the wiki. Against the recording, it had: an
invented Wilderness first meeting ("This location is unsafe..."), with the
stage set on the menu answer instead of the first page; the wiki's older
Varrock intro; every mood neutral; none of the quest varbits; a completion
scroll and a completion menu that the recording does not have; "bargain" for
Jagex's "bargin"; an invented attire refusal message; a journal in a format no
recording uses; the orb line's text wrong; and a LostCity essence teleport (an
npc seq Jagex never plays, no projectile, no area sounds, the jump a tick
late). Every quest varbit sat on a base varp that was `temp, transmit=no`.

## 1. Action inventory (`rsprox_actions.py`)

| first tick | action (count) | verdict |
|---|---|---|
| [3] | welcome_screen play | engine (login) |
| [5] | `ditch_wilderness_cover` op1 (2) | generic: `wilderness_ditch.rs2`; jump [10] 3106,3520 -> 3106,3523 = ours (test row `crossWildernessDitchNorth.landed`) |
| [28] | prayerbook prayer15 (2) | recorder's kit (Protect from Melee against the skeleton) |
| [30] | `rcu_zammy_mage1` op1 (7) | **this content**, section 3 |
| [69] | `skeleton_armed5` op2 | recorder's own fight (the skeleton that cut the talks short) |
| [115] | `kittenpet1` op1 | generic: pets (`quest_fluffs/scripts/kitten_pet.rs2`) |
| [126] | questlist op2 (2), questjournal close (2) | **this content**, section 6 (journal) |
| [136] | `looting_bag` op3 (take) | generic ground item pickup |
| [138] | `poh_tablet_varrockteleport` op2 (2) | generic: `teleport_tablet.rs2`; [142] -> 3214,3425 |
| [147] | `kittenobject` op7 (drop) | generic: pets |
| [178] | `fai_varrock_poor_door_flipped` op1 | generic door: [182] `loc_del` + `_open_flipped` at 3256,3388 rot 1 + synth 62 (`doors.loc`) |
| [186] | `thrantaxaltar` op1 Pray-at, op2 Check (2 each) | generic: Pray-at via `[oploc1,_prayer_altar]` (category 897); Check is `quest_arthur/scripts/thrantax_altar.rs2` ("An altar to the evil god Zamorak." = [188]) |
| [195] | `rcu_zammy_mage1_edge` op1 (4) | **this content**, section 4 |
| [309] | wornitems call_follower (3) | generic: pets |
| [312] | raw sardine on `kittenpet1` | generic: pets ("I think it's hungry!" [299]) |
| [319] | `amulet_of_glory_4` op3 (wear) | generic equip |
| [340] | bank booth op2 (3), bank tabs and items | generic bank |
| [385] | `4dosestamina` op2 | generic drink |
| [402] | `aubury` op1 | **fixed**: menu, reply, essence teleport (section 5) |
| [425] | `necklace_of_passage_2` rub (if_subop) | generic: `necklace_of_passage.rs2`; [428] -> 3116,3178 |
| [433] | `wizard` op2 (2) | recorder's own fight |
| [436] | `wizards_tower_laddertop` op1 | generic maplink: [442] 3105,3162 -> 3104,9576 (`maplink.dbrow`) |
| [445] | `head_wizard` op3 Teleport | the essence teleport, section 5 (`sedridor.rs2` `[opnpc3,head_wizard]`) |
| [459] | `poh_tablet_ardougneteleport` op2 | generic: `teleport_tablet.rs2`; [463] -> 2660,3301 |
| [476] | `castledoubledoorr` op1 | generic door |
| [481] | `ardounge_wizard` op1 | **fixed**: greeting, menu label (section 5) |
| [501] | `poh_tablet_varrockteleport` op2 | generic: [506] -> 3212,3425 |
| [595] | logout | engine |

The generic rows were checked for a binding in our tree, not diffed packet by
packet (Open).

## 2. Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| [10] | 3106,3520 -> 3106,3523 | ditch | generic, matched |
| [142] | Wilderness -> 3214,3425 | Varrock tablet [138] | generic |
| [416] | 3254,3401 -> 2892,4827 | Aubury, cast [412], jump C+4 | fixed (section 5) |
| [428] | mine -> 3116,3178 | necklace of passage [425] | generic |
| [442] | 3105,3162 -> 3104,9576 | ladder | generic |
| [453] | 3104,9572 -> 2900,4821 | Sedridor op3, cast [449] (arrival tick) | fixed (section 5) |
| [463] | -> 2660,3301 | Ardougne tablet | generic |
| [490] | 2682,3326 -> 2897,4845 | Cromperty, cast [486] | fixed (section 5) |
| [506] | -> 3212,3425 | Varrock tablet | generic |

The three mine landings are each within one tile of an `essence_mine_teleports`
row (15, 16, 13), as `map_findsquare(.., 1)` places them: matched.

## 3. Wilderness Mage (`rcu_zammy_mage1` -> `rcu_zammy_mage1a`)

| R | ours | verdict |
|---|---|---|
| [31] varp 492 0 -> 1 with the first page, `chatneu3` "If you want to talk, this isn't the place for it. Meet me in Varrock's Chaos Temple, by the rune shop. Unless you're here to buy something?" | the same | fixed (was set on "Alright, I'll go.", invented text) |
| [32] menu "Let's see what you're selling.\|Alright, I'll go." | the same | fixed |
| [36]/[44] player `chatcon1` "Let's see what you're selling.", page closed by the skeleton's hit | player page, then the shop | the shop after the page is the wiki's (no recording finishes this branch) |
| [51]/[102] player `chatcon1` "Alright, I'll go.", close | the same | fixed (was an invented npc reply) |
| [42]..[117] stages 1+: `chatang3` "I already told you to meet me in Varrock's Chaos Temple, by the rune shop. Unless you're here to buy something?" + the same menu (7 talks) | the same | fixed |
| Trade (index 821 [8864], after the miniquest): varbit `abyssal_miniquest_seen_shop` (13730) = 1, `shopmain` with `shop_main_init(inv 98, "Battle Runes")`, 14 stock rows | `[label,eta_wildy_trade]`, `entertheabyss.inv` | fixed (no shop existed; Trade said "I have nothing to sell you"). Rates 550/1000/2 after, 400/1300/30 before are the wiki's; the wiki's smaller stock before the miniquest is NOT modelled (Open) |
| after the miniquest: no recording talks to him | one invented line, marked | UNVERIFIED (Open) |
| op4 Teleport: not in 4423 | `~runecraft_enter_abyss` | not audited here (Open) |
| movement: walks 3106,3559 -> 3107,3559 [29] | generated `wander`, spawn 3106,3558 | matched |

The invented "refuses to deal with you while you wear Saradomin or Guthix
colours" messages are deleted: no source has them.

## 4. Varrock Mage (`rcu_zammy_mage1_edge` -> `rcu_zammy_mage1_edgeb`)

Pages are copied from `rsprox_pages.py` with each page's mood. Each varbit
write goes out before the page it sits on:

| tick | write | page it goes out with |
|---|---|---|
| [204] | `abyssal_miniquest_intro` (13731) = 1 | "Well I can't go doing it in the middle of Varrock..." |
| [277] | `abyssal_miniquest_choice` (13732) = 1 | "Alright, if you help us access the Rune Essence Mine..." |
| [291] | varp 492 1 -> 2, `abyssal_miniquest_orb` (13733) = 1, orb into the pack | objbox "The Mage of Zamorak hands you an orb." |
| [542] | varp 492 2 -> 3, orb out | objbox (full orb) "You hand the orb to the Mage of Zamorak." |
| [544] | `abyssal_miniquest_reward` (13734) = 1 | "Yes, this will do nicely..." |
| [553] | varp 492 3 -> 4, `miniquests_completed_count` +1, `rcu_abyssal_warning` (626) = 1, Runecraft 0 -> 1000 xp, book + pouch | `objectbox_double` "The Mage of Zamorak hands you a book and a pouch." |
| [584] | `tower_burning_zam_story` (13736) = 1 | "Exactly." |

No quest scroll, no completion message, no jingle. The level-up (1 -> 9) shows
when the chat closes [587..590] (engine). Ours had `~quest_complete_rewards`'s
scroll; it is removed.

| conversation | R | verdict |
|---|---|---|
| first meeting | [196..208]: "Ah, you again. The Wilderness is hardly..." / "Err... I didn't really want anything." / "So why did you approach me?" / "I was just wondering what you were doing." / "I was conducting business." / "What business?" / "I sell runes." / "In the Wilderness? Why?" / intro varbit / `chatang4` "Well I can't..." / menu "Where do you get your runes from?\|Interesting. Thanks for the information." | fixed (was the wiki's older intro) |
| "Interesting..." | [208] player page, no reply | matched |
| later meetings | [212], [248] `chatneu1` "Ah, you again. Do you need something?" / "Where do you get your runes from?\|Just looking around." | fixed (missing). "Just looking around." UNVERIFIED |
| runes chain + 4-way menu | [215..223] | fixed (moods) |
| loyal branch | [229..236] | fixed (moods) |
| rat walk | [244..246] | fixed (moods) |
| mercenary branch | [263..277] | fixed (moods, choice varbit) |
| Saradomin / not interested / "Okay, fine" / steal / No deal / think about it / reconsider greeting | not recorded | the wiki's text, neutral moods, marked UNVERIFIED |
| deal + orb | [280..291] | fixed (moods, stage and varbit on the click, objbox) |
| stage 2, charged orb carried | [540..554], with "bargin" | fixed |
| stage 2, orb banked / uncharged / lost | not recorded | the wiki's (they were invented), UNVERIFIED |
| topics after the reward | [554] "Can you tell me more about the Abyss?\|Can you tell me more about your group?\|I'd better be off."; after a topic `chatquiz1` "Now, did you want something else?" + "<the other topic>\|No thanks." ([573], [585]) | fixed (the menu looped the same three options) |
| after the miniquest | index 1677 [3897] / 3201 [1914] `chatneu1` "Ah, you again. What do you want?" + (TOE option)\|Abyss\|group\|"Nothing. I'm just looking around." | the greeting is recorded; the menu without the TOE option is inferred |
| Abyss topic after Temple of the Eye | index 3201 [2613] an extra `chatang4` page | added |
| attire refusal | index 1677 [3858] `chatang3` "How dare you wear such disrespectful attire in this holy place? Remove those immediately if you wish to speak to me." | fixed (was a `mes`); which items trigger it is UNVERIFIED |
| movement | wanders 3257..3263 x 3382..3385 [177..188], [539..540] | generated `wander` + `moverestrict=indoors`, spawn 3260,3383: matched |

## 5. The essence teleport (generic; recorded three times, all alike)

| | Aubury [412] | Sedridor [449] | Cromperty [486] | ours |
|---|---|---|---|---|
| trigger | click on Aubury's "Of course..." page | op3, on the arrival tick | click on the player's page | on the trigger |
| npc | `curse_casting` 92, say "Senventior Disthine Molenko!", no seq | same | same | was `human_castcurse` + "Senventior disthine molenko!": fixed |
| sounds | `sound_area` 127 `curse_cast_and_fire` range 10 at the npc; 126 `curse_hit` delay 100 range 10 at the player | same | same | was `synth curse_all`: fixed |
| projectile | `map_projanim_v2 curse_travel` npc -> player, 61..100, angle 16, progress 128, heights 124 (v2, so 31 here) | same | same | was none: fixed |
| player | `curse_impact` 124 delay 100 | same | same | was delay 70: fixed |
| C+4 | spot varbit, (3rd spot) "Your scrying orb has absorbed enough teleportation information.", `essencemine_portal` (2313) = 5, teleport | = 1 | = 7 | the jump was C+5, the text "teleport information", no portal roll: fixed (below) |

`essencemine_portal` is a roll per teleport, not per teleporter: Aubury sent 2,
5, 0, 1 (index 1612 [48..157]), 4 (208 [859]), 3 (2264 [93]) and 5 here;
Sedridor 4 (235 [4453]) and 1 here; Cromperty 7. Its multilocs and multinpc read
1-based in the config (entry N is value N-1, the ninth the default). Values 0, 1, 3,
4 and 7 show the four loc portals (`blankrunestone_exit_portal` /
`_2`); values 2, 5 and 6 blank them all, and the way out is the npc
`blankrunestone_exit_portal_1` (3087, drawn as `essencemine_portal_1`). It is added on the
jump's tick on the loc portal tile nearest the landing, 3 of 3: 2897,4846 ->
2885,4850 and 2925,4817 -> 2933,4815 (1612 [48], [90]), 2892,4827 -> 2889,4813
(4423 [416]). On roll 0 the recorder used the loc portal (1612 [141]). Ours: `random(8)`
on every jump, and on 2, 5 or 6 `npc_add` of 3087 at the nearest corner
(`essence_mine.npc`: still, no wander). Both exits (1612 [147..148] loc, [76..77]
npc) send the line, `curse_impact` delay 30 and `curse_travel` from the portal
(0..30, heights 15/31, as the v1 packet prints them) on the op, then the jump and
`teleport_all` one tick later. Ours had no projectile, the synth on the op and the
jump a tick late: fixed. Jagex's mine is a per-player copy (raw x ~9957); ours
is shared (Open). The first two spots send only the spot varbit.

Aubury (Rune Mysteries complete), recorded [407..411]: `chathap1` "Do you want to
buy some runes?", menu "Yes please!\|Can you tell me about your cape?\|Can you
teleport me to the Rune Essence Mine?\|No thank you.", the player `chatquiz1`,
Aubury `chathap3` "Of course. By the way, if you end up making any runes from the
essence you mine, I'll happily buy them from you." Fixed (it had three options
and no reply); the cape branch and "No thank you." are the wiki's (UNVERIFIED).

Cromperty, recorded [482..485]: `chatneu2` "Hello there. My name is Cromperty. I am
a wizard, and an inventor." (one page; ours forced three rows and "Wizard"),
`chatneu3` "You must be <displayname>. My good friend Sedridor has told me about
you...", menu label "...Rune Essence Mine?", no reply. Fixed.

## 6. Journal

Recorded twice:

- [126], stage 1: `<col=000080>I spoke to the <col=800000>Mage of Zamorak<col=000080> in the <col=800000>Wilderness<col=000080>. He asked` / `<col=000080>me to meet him in the <col=800000>Varrock Chaos Temple<col=000080>, near the <col=800000>Rune` / `<col=800000>Shop<col=000080>.`
- [492], stage 2 with the orb charged: that paragraph struck (3 `<str>` rows), then
  five current rows ending "I should now take the <col=800000>Orb<col=000080>
  back to him.", with no blank row between.

Ours writes both from the recorded strings with `~journal_struck`. Not started,
stage 2 uncharged, stage 3 and complete are UNVERIFIED (no recording opens
them, and the wiki has no journal transcript); they reuse the recorded wording.

## 7. Vars that reach the client

Every quest varbit (13730-13737) and Rune Mysteries' (13720-13729) sit on
`varp3404_runemysteries_secondary`. The generated block had it `scope=temp,
transmit=no`: never sent, and lost at logout. It is now `perm, transmit=yes` in
`entertheabyss.varp`, with those lines deleted from the generated block (the
TGV varp279 pattern). varp 492 and 491 were already perm/transmit.
`essencemine_portal` is on `varp638_swansong_temp` (temp, transmit=yes):
matched.

## Wire audit (our tick log, private packs)

| event | R | ours | verdict |
|---|---|---|---|
| essence cast (Aubury) | [412] npc spotanim 108/92, say, sound 127 r10 at npc, proj 109 61..100, player spotanim 110/124/100, sound 126 d100 r10 at player | tick 130, the same seven rows | matched |
| essence cast (Sedridor op3) | [449], on the arrival tick | tick 173, one tick after arrival | engine op timing (Open) |
| the jump | C+4 | 130 -> 134, 173 -> 177 | matched |
| npc portal | added on the jump tick, nearest corner | `npc_spawn` 3087 on 134 | matched |
| portal exit | spotanim 110/124/30 + proj 109 0..30 on the op, synth 200 + jump next tick | 142/143, 183/184 | matched |

## Test

`test/quests/entertheabyss.lua` follows the recorded route: the Wilderness talk,
the first Varrock meeting through the mercenary branch to the orb, Aubury by
talk, Sedridor by his Teleport op, Cromperty by talk, then the hand-over. Each
mine exit reads the rolled `essencemine_portal` and uses the npc portal on 2, 5
and 6, the loc portal otherwise. The end is graded on the stage, the miniquest
count, the quest varbits, the xp, the book and the pouch (no scroll rows).
Last run (private packs, 2026-10-29): 77 of 79. The two FAILs are the driver's
spell-cast rows (Camelot, Varrock): the teleport lands 3 ticks after the cast
input, as before, and the `.landed` rows pass, but the cast row's 4-tick window
now closes first. The content is untouched; the cause is outside this port (Open).

## Open

- The Abyss itself (op4 Teleport, the skull, the prayer drain, the outer ring) is
  not in this recording.
- The Wilderness Mage after the miniquest: no recorded talk.
- Battle Runes before the miniquest: the wiki's smaller stock is not modelled.
- The essence mine is shared, not Jagex's per-player copy: every player in it sees npc portals other players' rolls added. The npc portal's lifetime (500 ticks here) is unverified.
- The test's two spell-cast rows time out by a tick (above).
- Sedridor's op3 cast comes a tick after arrival; the recording casts on the arrival tick (engine op timing).
- Journal states other than stage 1 and stage 2 charged.
- The attire refusal's item list.
- Sedridor's talk menu label ("...Rune Essence?") is not recorded here (op3 was used).
- The generic rows of section 1 were checked for a binding, not diffed packet by packet.
- Dialogue pages and varbit ticks were not diffed from a TRACE_OUT run (the tick log carries no interface rows); the test reads every page's text.
