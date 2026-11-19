# Fairytale I - Growing Pains from RSProx

One session, rev 242.1, the owner's account: **index 4605** "Fairy Tale Part 1"
(2026-11-18, w442), the quest from Martin's start (stage 0) to the scroll (90)
in 4,613 ticks, the journal opened 22 times. Index 4603 (the same day) is
farming and banking before it; 4604 is empty.

Our tree had no Fairytale I at all: wiki stubs for Martin (Talk-to went
straight to the skillcape) and Zandar, Heskel's and Fayeth's Talk wired
straight to their Pay, no gardener menu anywhere, varp 671 `temp, transmit=no`
(so the Godfather, his henchmen and the gatekeeper could never appear on the
client), and nothing for the Godfather, Nuff, Malignius's part, the grave, the
ceremony, the grove or the Tanglefoot. Built new in
`quests/quest_fairytale1/` (OSRS-Content):

| file | what |
|---|---|
| `configs/fairytale1.constant` | stages (Jagex's 0, 10..90), tiles |
| `configs/fairytale1.varp` | varp 671 perm + transmit; server varp 6999 (gardeners, Malignius's errand) |
| `configs/fairytale1.npc` | the Tanglefoot's and the babies' sounds |
| `configs/fairytale1.varn` | Malignius's cast tile |
| `scripts/ft1_martin.rs2` | Martin, the gardeners' Talk menu |
| `scripts/ft1_falador.rs2` | Zandar, Malignius, the ingredient roll, the grave |
| `scripts/ft1_zanaris.rs2` | arrival, Godfather, Slim Louie, Nuff, gatekeeper, the grove copy, the Tanglefoot, the secateurs, the ceremony, the completion |
| `scripts/ft1_malignius.rs2` | Malignius's necromancy (ambience) |
| `scripts/ft1_zanaris_folk.rs2` | Nuff's certificate, the Zanaris fairies, the Co-ordinator |
| `scripts/ft1_journal.rs2` | the journal |

Hooks in shared files: the shed door (`leprechaun_tree.rs2`, the arrival), the
spade (`spade.rs2`), the damage funnel (`player_hit_npc_prepare.rs2`),
Filliman's Nature Spirit talk (`filliman.rs2`), Malignius's Swan Song talk
(`swansong_army.rs2`), The Garden of Tranquility's five gardeners (their
"no time to chat" fallbacks), the journal dispatch, the overview's start text,
the quest cheat and the jingle table; varp 671 deleted from the generated
varp block, the three tanglefoot types' null sounds from the generated npc
block.

Test: `test/quests/_fairytale1_recorded.lua` (reaches each place by teleport,
every conversation the recorded one; each page's raw text, `<br>`s included,
compared exactly with the recording's: 305/305 PASS).

## 1. Actions (every row a verdict)

| tick | action | verdict |
|---|---|---|
| 22.. (x27) | quest list / journal / overview | **this content**: §9 |
| 37, 596 | banker, bank booths | generic (bank); not diffed |
| 49, 2105, 523, 3900 | glory Rub > Draynor / worn Rub | generic (jewellery, §2b of the skill); not diffed this pass |
| 70 | farming_fencegate_r | generic gate (`doors.loc gate_outer_closed`) |
| 76 (x5) | Martin | **this content**: §5, §6 |
| 171..509 | Frizzy, Elstan, Heskel, Fayeth, Treznor Talk | **this content**: the gardener menu (§6) |
| 236 | farming_style (Falador stile) | generic (category 211); not diffed |
| 246, 255 | cabbage pick, cabbage on the tool leprechaun | generic; the leprechaun's line is `farming_tools.rs2` |
| 285 | Elstan Pay | generic farming ("That patch is already fully grown!") |
| 305..3597 | Falador / Lumbridge / Varrock Teleport | generic (spells) |
| 389 | Fayeth Trade | generic (shop) |
| 518 | tree (woodcut) | generic |
| 667, 3893.. | wield staff / secateurs / glory | generic equip |
| 702 | Wizard (Lumbridge camp) "Hahaha you're such an amateur!" | Lost City's camp (`zanaris_camp.rs2`) |
| 744 | the shed door | Lost City (`leprechaun_tree.rs2`) + **this content**'s arrival hook |
| 782, 981 | Zanaris fairy (Freddie) | **was unbound**: added (`ft1_zanaris_folk.rs2`) |
| 793, 797 | Chaeldar Talk / Trade | generic (slayer) |
| 806..858, 4580 | the Godfather | **this content** |
| 876, 4571 | Slim Louie | **this content** |
| 930..1369, 4107 | Fairy Nuff | **this content** (and her healer talk) |
| 939, 1373 | Nuff's certificate | **was unbound**: added |
| 955 | sleeping queen examine | engine |
| 980 | fairy crate Search | no answer in the recording (walked off); ours unbound, OPEN |
| 995.. | stamina, potions | generic drinking |
| 1013 | the Co-ordinator | **was unbound**: added |
| 1561 | Falador castle crumbling wall | generic shortcut (`agility_shortcuts_osrs.rs2`) |
| 1584..1646 | Dark Wizards' Tower stairs | generic (`ladders.loc`) |
| 1597, 1624 | Zandar | **this content** (stage 40) and his standard talk |
| 1662 | Falador members' gate | generic (`gates.rs2`) |
| 1749 (x5), 2236.. | Malignius | **this content** |
| 2008..2040 | Draynor Manor doors | Ernest the Chicken's / generic doors |
| 2066 (x3) | spade at the grave | **this content**: §2 |
| 2086, 4469 | destroy skull / secateurs | generic destroy (the obj's text) |
| 2427..3582 | Grand Exchange | generic; not diffed |
| 2880..3133, 3221..3871 | Varrock gate, Paterdomus, Mort Myre, stones, grotto | Nature Spirit / Priest in Peril ports (index 4602, 4459) |
| 3006 | Mysterious Old Man | random event (`macro`); not diffed |
| 3145, 3489, 3874 | Nature Spirit | **this content**: §8 |
| 3165 | Altar of Nature | Nature Spirit port |
| 4039, 4045 | egg, egg on the chicken shrine | Recipe for Disaster's shrine (`recipefordisaster_amikvarze.rs2`); not this quest |
| 4137 | world map | UI |
| 4165 | Zanaris jutting wall | generic shortcut (`maplink_agility`) |
| 4203, 4218 | the thorny vines | **this content** |
| 4207, 4211 | the Gatekeeper | **this content** |
| 4228, 4257 (x14) | baby, the Tanglefoot | **this content**: §11 |
| 4249.. | prayers | generic |
| 4436 | take the Queen's secateurs | **this content** |
| 4474 | the vines out | **this content** |
| 4577 | secateurs on the Godfather | default "Nothing interesting happens." (matches) |
| 4600 | quest rating 5 | the rating widget (983) is not built in our tree: engine-generic, OPEN |

### Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| [52], [526], [2108], [3904] | glory | Draynor | generic |
| [240] | stile | 3063,3282 -> 3063,3283 | generic |
| [308]..[3597] | spells | | generic |
| [750] | 3202,3169 -> 2452,4473 | the shed (Dramen staff) | Lost City; the arrival flags §4 |
| [1562] | crumbling wall | 2936 -> 2934 | generic shortcut |
| [1585]..[1646] | tower stairs | | generic |
| [2947]..[3871] | Paterdomus, Salve, stones, grotto | | Nature Spirit / Priest in Peril |
| [4035] | the shed again | | Lost City |
| [4225] | 2400,4379 -> copy 12252,15451 | the vines | **this content**: a new copy of m37_68 per entry |
| [4477] | copy -> 2400,4379 | the vines out | **this content** |

## 2. The moves and shapes

**The grave** (4605 [2066..2095]): C+1 "You start digging..." (spam),
`human_dig_long`, the player's overhead "I can't believe I'm about to do
this!", `digspade` then `digspade` x3 at 25; C+5 the seq stops and "You dig
up a skull." with the skull, or "You don't find anything." with one carried
([2082]). Ours: `~ft1_try_dig` before the shared dig (which plays `human_dig`),
stage 50 only, inside 3105..3106 x 3382..3385 (the two recorded tiles and the
wiki's rectangle; the z bounds are the wiki's). A skull in the bank also
answers "You don't find anything." where the wiki has a player line no
recording shows.

**The thorny vines** ([4203..4225]): the op, the Gate Keeper's page
(`fairy_mindslayer`'s head, name "Gate Keeper"), the box "You squeeze past
the thorny vines into a small tunnel."; on its continue C `human_into_sidestep`
with the sidestep stance (`--bas`: ready/turn `human_ready_sidestep`, walk*
`human_walk_sidestep`, run -1) and `ft_squeeze_past`; C+1 the player in a NEW
copy of m37_68 (raw 12252,15451 for 2396,4379), the stance back. Out
([4474..4478]): the box; on its continue the player outside on 2400,4379 with
the seq, stance and sound on one tick, the stance back a tick later. Ours:
matched on the tick log (sidestep [485], the copy and the Tanglefoot [486];
out [880]).

## 3. Ambience

| what | recorded | ours |
|---|---|---|
| Malignius's necromancy | every 101 ticks ([1739], [1840], [2244], [2345]): face a tile 2..4 off, `human_casting` + `firestrike_casting` (92) + "Da mihi potentiam!", `firestrike_travel` to it (v2 140/5 = 35/1, 50..86), `zamorak_flame` at him; T+3 `zamorak_flame` on the tile, `elemental_summon_zombie`/`_ghost`, `unattackable_zombie` ("DUUUHHH" on its add) / `unattackable_ghost` facing him; zombie +21 `zombie_update_despawn`, +24 gone, +25 `smokepuff_large` + `smokepuff` | **was missing**: built (`ft1_malignius.rs2`). raise at T+3 and gone 24 ticks later on our tick log (matched). Skeleton and skeleton mage by name, UNVERIFIED; the puff a tick early (on the del); the undead stand facing him where Jagex's walk after him (30 steps over 3 lives): OPEN |
| the Zanaris stormcloud | `fairy_stormcloud_non_attacking` added beside Nuff at [1319], `ft_raincloud` (ours `ft_rainbutt`, 1105) area r5, follows 3 steps, gone | not built: not this quest's (Zanaris weather), OPEN |
| the henchmen | Slim Louie / Fat Rocco re-face fixed tiles on their adds (2454,4428 / 2440,4428) | not built (idle turns), OPEN |
| Zanaris music | `dream` at the shed, `crystal cave`, `faerie`, `impetuous_clue` by area | engine music areas; not diffed |

## 4. Stages and vars

Jagex's values are ours: varbit 1803 `fairy_farmers_quest` 0 -> 10 .. 90.

| stage | 4605 | goes out with | ours (test tick log) |
|---|---|---|---|
| 10 | [127] | the last start page ("If I were you..."), with the started line | matched (with the page) |
| 20 | [546] | Martin's first page at five gardeners | matched |
| 30 | [859] | the last Godfather click, with "The Godfather is no longer paying attention to you." | matched |
| 40 | [1272] | the click after "Here, I've written down a list...", the list in the pack | matched |
| 50 | [1617] | Zandar's "Yes, but if I were you...", the list gone | matched |
| 60 | [2244] | Malignius's "Right then, to business...", the skull gone, items 1..3 rolled | matched |
| 70 | [3887] | C+6 of the ceremony, the secateurs blessed in their slot, the three items gone | matched (C [463], stage [469]) |
| 80 | [4437] | the take of the Queen's secateurs | matched |
| 90 | [4596] | the last Godfather click: items 0, qp, the line, the scroll | matched |

| var | fields before | now |
|---|---|---|
| varp 671 `fairytale_multi` (1803 stage, 1804..1806 + 2543 items, 1807 queen, 1808 godfather, 1809 nuff) | temp, transmit=no (generated) | perm, transmit (`fairytale1.varp`); generated block deleted |
| varp 6999 (server) | none | perm, unsent: the gardener count (bits 0..2), one bit per gardener (3..26), Malignius's errand (28); no var reaches the client at any of them in 4605 |

- **1808/1807 on arrival** ([751], the tick after the landing, stage >= 20):
  the Godfather and both henchmen appear (multinpc on 1808), the Queen goes
  (1807). Ours from the shed door; arrival by any other route (fairy ring,
  later quests) does not set them: OPEN.
- **1809** on the last click of Nuff's healer story ([1370]): she greets by name
  after it ([4107]).
- **The roll** is three consecutive rows of the wiki's 31-row list, the first
  in 0..28 (recorded 26/27/28 = uncut ruby, Jogre bones, king worm). How
  Malignius and the journal name rows other than 26..28 is UNVERIFIED (an
  article and the item's name).

## 5. The start and the end

**Start** ([76..128]): every Talk is "Ask about the Skillcape of Farming." |
"Ask about the quest."; the skillcape answer at Farming 57 is "Can you tell me
about your skillcape?" (the stub had the wiki's "What is that cape you're
wearing?"). The quest: "Hello." / "I can't chat now..." / the three-option menu
/ the banner / "Of course I'll help." / the problems / the farming question
(the recorded menu "Now that I think about it, you're right!" | "No, can't say
that I have.", absent from the wiki) / `~p_choice2_header("Yes.", 1, "No.", 2,
"Start the Fairytale I - Growing Pains quest?")` / three pages, the stage and
"You've started a new quest: @q_mes@Fairytale I - Growing Pains</col>" with the
last. The recorder was above combat 50 and Farming 30: the warning boxes are
the wiki's wording, UNVERIFIED. UNVERIFIED too: the "No." answer, "Anything I
can help with?", "Sorry, I have better things to do.", "No, can't say that I
have." (no reply in any source).

**End** ([4577..4600]): secateurs used on the Godfather: "Nothing interesting
happens." (ours: the default). Talk: five pages; on the last click the stage,
the item rows cleared (1804 then 1805 then 1806), qp +2, the completion line,
disease -50/0, the Queen's secateurs gone, `quest complete 3` (154), the scroll:
title "You have completed Fairytale I - Growing Pains!", model
`fairy_queen_secateurs` at 78,128, rows "2 Quest Points / 3,500 Farming XP /
2,000 Attack XP / 1,000 Magic XP / Some Magic Secateurs". Ours: all of it
(`~quest_complete_rewards`, the jingle row). The quest-rating box (983) after
the scroll: not in our tree (engine-generic).

## 6. Pages

Every page of the session through a checker (text with `<br>` -> " ", the
recorder's name -> `<displayname>`, the speaker, the mood from `if_setanim`)
against our `~chat*` calls: 241 match text and mood. Not ours: the farming
Pay, the camp wizard, the slayer master, the tool leprechaun, the random
event. Interpolated (checked in the test instead): Nuff's price, Malignius's
item rows. Then the test compares each page's raw text, `<br>`s included,
with the recording: every wrap is Jagex's (the two hyphen breaks, "out-<br>of-wack"
and "she-<br>a goes", included).

**The gardeners.** The theory is the ORDER spoken in, not the gardener (Frizzy
first: rain; Elstan second: insects; Heskel third: adventurers; Fayeth fourth:
seasons; Treznor fifth: fairies), with the wiki's "Gardener #1..#5"; spoken-to
gardeners answer "I've already told you everything I suspect." ([209]..[509]).
The menu has the quest's option only at stage 10 (4504 [885] and 4523 [4864]
show the four-option menu outside it). The quest counts the 2006 gardeners
(the wiki: none added since OSRS's launch); Treznor's two records share a bit.
UNVERIFIED: Martin's per-gardener reports (the wiki's), the "Invalid
Gardener" and "after five" answers, the menu's options 3..5 (no source
answers them; "Would you look after my crops for me?" goes to the gardener's
Pay where one is built).

**Malignius's menus**: "What would you like to say?" with the errand's option
reworded once asked ([1803] -> [2236]); the "Who are you" sub-menu leaves out
the answer just given ([1765..1800]).

Menus whose order or text were recorded all match. UNVERIFIED (wiki):
Godfather's "That's all, thanks.", Nuff's lost-list and "I did it!", Zandar's
no-list answer, Malignius's "No, not yet.", "Actually I have it right here.",
"Thanks, I'll be off now." and the clothes branch (the wiki's mushroom
transformation is not built), the Nature Spirit's lost-secateurs talk, the
Gate Keeper's re-entry line and the Queen's-secateurs objbox, Nuff's paid
heal (no line in any source: the gem goes and she heals), the ghost line
without the amulet.

## 7. Ops

| op | cache | ours before | now |
|---|---|---|---|
| Martin Talk-to | op1 | stub | the menu |
| gardeners Talk-to (19 + 5 GoT) | op1 | Heskel, Fayeth: their Pay; the rest none / "no time" | the menu |
| Godfather / Slim Louie / Nuff / Gatekeeper Talk-to (multinpc shells) | op1 | none | bound on the shells |
| fairy_walls_multi, fairy_wall_tangle_door_open1 | op1 | none | the vines |
| fairy_cert_large_multi | op1 | none | the certificate (interface 123) |
| fairy_queen_secateurs take | opobj3 | generic | the stage, then the generic take |
| Tanglefoot Attack | op2 | generic | only the magic secateurs hurt it |

## 8. The Nature Spirit's ceremony (the one camera scene)

No `cutscene_status`, fade or `minimap_toggle` in the session; every
`cam_reset` is a teleport's. One scene: the ceremony.

| tick | recorded | ours |
|---|---|---|
| entry | the Nature Spirit added on 3441,9738,1 facing south with the player ([3137], [3486], [3871]) | Nature Spirit's grotto door (`quest_druidspirit.rs2`) |
| pages | six pages; "No, I still need to collect some things." ends it and he goes on that click ([3158]) | matched |
| C ([3881]) | the click on "Then let us begin...": his `fairy_flower_enchant` (100) and `human_fairy_take_and_hold_up`, the player's `human_fairy_kneel_and_offer`, `cam_moveto` 3447,9734,1 h375 and `cam_lookat` 3441,9738,1 h375 at rate 100/100, `ft_enchant_flower`, the chat shut | matched ([463]) |
| C+6 ([3887]) | stage 70, the secateurs -> magic secateurs in their slot, the three items gone, `cam_reset`, "It is done!" | matched ([469]) |
| last click ([3891]) | "Farewell, <name> and good luck..." clicked: he goes | matched ([471]) |

`docs/quests/CUTSCENES.tsv` row `fairytale1_recorded`.

## 9. The journal

Opened 22 times. Recorded row for row (the test compares each as plain text,
every row passes): stage 10 with 0, 1, 2 and 5 gardeners ([140], [221],
[297], [513]), 20, 30, 40, 50 before and after Malignius's errand ([1635],
[1902]), 60 with 26..28 rolled ([2324]), 70, 80. A current-style journal:
every finished row `<str>`, the current step `<col=000080>` with names
`<col=800000>`, Jagex's oddities kept ("Master Gardner", "Queens'", a row
opening with a space, "Malignus"). Stage 60's current step is wrapped by the
engine's split at the journal's width, which reproduces [2324]'s rows and
colour re-opens. UNVERIFIED: 10 with 3 or 4 gardeners (the "couple" rows),
60's wraps for other rolls, and 90 (80's step struck, a blank row, QUEST
COMPLETE!). The overview (not started, [22]): "talking to
<col=800000>Martin<col=000080> in <col=800000>Draynor Village
Market<col=000080>."

## 10. Rewards

2 QP, 3,500 Farming, 2,000 Attack, 1,000 Magic (the dbrow's tenths); the magic
secateurs are kept. Malignius's post-quest sale of a new pair (the wiki, 40,000
gp) and the Nature Spirit's re-enchant for a lost pair at stage 70: the second
is built (`~ft1_nature_spirit_talk`), the first is OPEN.

## 11. The Tanglefoot (boss: one Opus agent, owner rule)

One session of the fight (4605 [4225..4441]): every count is one roll. The
recorder: Attack 60 (super attack: 74, then 73), Strength 49 (super strength:
61), Farming 57; glory, magic secateurs, sapphire ring (+17 stab, +7 str), no
armour; accurate stab; Protect from Melee, Ultimate Strength, Incredible
Reflexes. Its levels are the cache's (97/91/97/102; babies 40/40/38/40).

| block kind | recorded | ours | verdict |
|---|---|---|---|
| sequence | 68 (attack 41 every 4 ticks, block 26 on the player's swing ticks unless its attack has the slot, death 1 on lethal+1) | 56 (34/21/1) | matched |
| hit / headbar | 34 (16 x26, 12 x7, 43 x1) / 34, id 0 | 28 / 28 (one max-hit splat on a 7) | fixed |
| facing | the player at the first hit, reset with the death seq | same | matched |
| enabled_ops | 0b00000 at [4424] | same | matched |
| add, move | [4230] 2376,4384 north-east, wanders | same | matched |
| babies in the copy | 7 (5 sml1, 2 sml2) | 7 | fixed (was 8: a sml1 no baby used; the south one is sml1 on 2395,4358) |
| sml1 / sml2 facing, seqs, a death | 21 / 4 facings, 3 attacks, one death | 0 | the post-death event: OPEN |

| attack | recorded n / zero% / max | ours before | ours after | verdict |
|---|---|---|---|---|
| its melee vs Protect from Melee | 41 / 100% / 0 | 86 / 100% / 0 | 34 / 100% / 0 | matched |
| its melee unprayed | no sample | max 11 from its levels (wiki 11) | same | UNVERIFIED |
| the secateurs on it | 32 / 15.6% / 7 (max splat [4288]) | 70 / 64% / 8 | 28 / 10.7% / 7 | **fixed** |
| any other weapon | 0 and "I can't deal any damage..." | same | same | matched by reading |
| a baby after the death, unprayed | 3 / 33% / 5 | none | none | OPEN |

The rule (fitted): our ordinary roll against Defence 91 (defence roll 6,400)
predicts 15.7 zeros in 32 swings where 5 were recorded (p ~ 0.0002); the
recorded zero rate caps the roll near 3,500. The wiki: damage from "a
combination of Farming and Strength", a Jagex reply: "Attack (for likelihood
to hit)". Ours now (`~ft1_tanglefoot_prepare_hit`): the swing's attack roll
against 640 (Defence 1, no bonus), and the max is the melee formula with
Farming in place of Strength, the worn strength bonus, no prayer (Farming 57
gives 7, Strength would give 8; under Ultimate Strength no hit passed 6 in 23
swings). "Never misses" fits as well; how much Strength adds cannot be
separated in one session. Kill time 32 swings / 156 ticks recorded, 28 / 136
ours (was 70 / 346). The 103 dealt against 102 hitpoints means it regenerated
at least 1, silently; ours too.

Residual causes (`rsprox_hit_causes.py --by-prayer`): its 41 prayed swings vs
our 34, matched; the babies' 3 unprayed hits REC-ONLY (OPEN); no heal, content
hit or `none` either side.

Other edits: `param=damagetype,1` (Slash, the wiki infobox, UNVERIFIED: every
swing was prayed; the generated block's 2 deleted); the drop's line
"<col=ef1020>Untradeable drop: Queen's secateurs</col>" ([4426]).

OPEN: the post-death baby event at [4426] (one sml2 dies on the recorder's
tile, two others walk in and one hits 1, 0, 5; single-way, where our instances
are multiway), the babies walking through the player, the loot-tracker
clientscript 7192, the babies' defend sound. Engine-generic: defend and death
sounds as area sounds (r12) where Jagex sends synths; the babies' facing
latch rows.

## Wire audit (sections not covered above)

Npc block inventory (`npc_block_inventory.py`, 4605 vs the test's tick log):

| type | kind | recorded | ours | verdict |
|---|---|---|---|---|
| elemental_wizard_boss | say / sequence / spotanim | 4 / 4 / 4 | 0 / 0 / 0 | **fixed**: the necromancy (§3) |
| fairy_henchman1/2_multi | facing | 13 / 9 | 0 | idle re-faces, OPEN (§3) |
| fairy_mindslayer_guard_multi | facing | 4 | 0 | exposure: the test does not talk to him at the vines |
| filliman_tarlock_ns | sequence, spotanim 568 h100 | 1, 1 | 1, 1 | matched |
| fairy_tanglefoot | enabled_ops 0 at death | 1 | 1 | matched |
| gardeners, Martin, Zandar, Nuff, Godfather | facing | | | one page each side: dialogue counts |

## Open

- The walked route (a gate test without teleports): the test teleports
  between conversations.
- Arrival flags only from the shed (fairy rings and other routes do not set
  1807/1808).
- Malignius: skeleton / skeleton-mage raises, the undead's walk after him, the
  puff a tick early, the post-quest secateurs sale.
- The gardener menu's options 3..5 and Pay for the non-tree gardeners.
- The fairy crate, the stormcloud, the henchmen's idle turns, the quest-rating
  widget.
- Journal stage 90; stage 10 with 3 or 4 gardeners; other rolls' item words.

## 12. Follow-ups 2026-11-18 (wiki-sourced; no recording takes these)

- **Gardener protection, every crop the quest's gardeners look after.** Pay
  ops for the allotment (Elstan, Dantaera, Kragen, Lyra: two patches each, by
  the cache's op names), hops (Selena, Francis, Vasquen, Rhonen) and bush
  (Dreven, Taria, Rhazien, Torrell) gardeners, and Talk-to's "Would you look
  after my crops for me?" deal (wiki Transcript:Kragen). Payments: Kragen's
  transcript for allotments, Hops patch/Seeds and Bush patch/Seeds for the rest
  (`skill_farming/scripts/farming_gardener.rs2`). Protection now means
  something: crop disease is built (`skill_farming/scripts/farming_disease.rs2`),
  and every gardener refuses a dead or diseased patch.
- **Malignius's mushroom** (`ft1_falador.rs2`, `[label,ft1_malignius_clothes]`):
  `p_transmogrify` to an invisible npc under a `mushroom_inactive` loc for 12
  ticks; pinned by the test's `malignius.mushroom.*` rows and a crop of shot 325.
- **Single-way grove:** `maps/instance_singleway.csv` lists 37_68.
- **"Can you sell me something?"** opens Farming Supplies (wiki
  Transcript:Torrell); the test's `elstan.shop` row.
- Test `test/quests/_fairytale1_recorded.lua`: 344 PASS, 0 FAIL on 2026-11-18;
  `test/farming/disease.lua`: 34 PASS, 0 FAIL.
