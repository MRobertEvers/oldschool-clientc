# Shadow of the Storm, rebuilt from RSProx

Sources: RSProx index **4475** (rev 242, 2026-11-06; the start to stage 20,
inside a session that also does Mort'ton and the Rat Pits) and index **4477**
(rev 242, 2026-11-07; stage 20 to the completion). Both are the owner's own
recordings, our cache's revision. Cites are `[tick]` in 4477 unless written
`4475 [tick]`. Unrecorded branches come from the OSRS wiki's
Transcript:Shadow of the Storm and are marked `[wiki]` in the scripts; the
journal states no recording opens come from the RS3 wiki's
Transcript:Shadow of the Storm/Journal and are marked unverified.

Content: `OSRS-Content/osrs-content/server/scripts/quests/quest_shadowstorm/`
(`shadowstorm.rs2` Reen, Badden, the golem's lines, the journal;
`shadowstorm_dye.rs2` Uzer, the passage, Evil Dave, the kilns, the book, the
storm, the incantation; `shadowstorm_room.rs2` the throne room copy, its
wizards, the escort, both rituals, the finale; `shadowstorm_fight.rs2`
Agrith-Naar) plus the hand-overs in `quest_golem/scripts/golem.rs2` and
`golem_portal.rs2`, and `skill_crafting/scripts/jewellery/jewellery_if.rs2`.
Test: `test/quests/shadowstorm.lua`.

What the previous port was: built from the wiki and Quest Helper, with
invented lines (Denath "So, another apprentice...", Jennifer "Here. Take a
silver bar to a furnace.", the golem "Denath came in the night...", "You dye
Silverlight black."), the throne room in the shared world, a fixed
incantation, a one-sigil gate, a lamp paying Hitpoints, and none of the
scenes. Every page is now the recording's.

## 1. Action inventory (rsprox_actions.py, 4477)

| action | verdict |
|---|---|
| opnpc1 agrith_dave_at_portal x12 | this content (§4) |
| wornitems slot3 / inventory op=3 (wield, unwield the sword) | generic equip; recorder's kit |
| use golem_mushroom on silverlight x2 | this content: the dye (§5) |
| wornitems slot12 op3 x4 (ring of dueling: Castle Wars) | generic jewellery (§2b), recorder's travel |
| oploc castlewars_bankchest, bankbooth, fai_varrock_bankbooth*, exchange_bank_wall*, banker op3 | generic banking |
| bank withdraw/deposit rows | generic |
| inventory subop necklace_of_passage (Eagles' Eyrie) | generic jewellery, recorder's travel |
| oploc1 golem_insidestairs_top x8 | generic maplink (ladders.loc; the arch 3491,3090 -> 2721,4886,0) |
| oploc1 golem_insidestairs_base x4 | this content at 90..110: the falling rocks (§7); otherwise the maplink |
| oploc1 golem_portal x14 | this content: Dave's refusal before 30, the room after (§6) |
| wornitems slot2 op5 (glory Al Kharid) | generic jewellery |
| inventory op=7 drops (sword, book, sigil, gems, tinderbox, bones...) | generic drop |
| opnpc1 agrith_reen_alkharid / _uzer / agrith_reen | this content (§3) |
| opnpc1 agrith_badden_uzer / agrith_badden | this content (§3) |
| 3dosestamina / lobster / prayer restores op=2 | generic consumables; recorder's kit |
| oploc1 golem_black_mushrooms; opobj3 golem_mushroom | this content / generic pickup |
| opnpc1 agrith_matthew, _tanya, _jennifer, _patrick, _eric, _denath | this content (§6) |
| oploc1 golem_demon_portal x6 | this content: out of the copy |
| opnpc1 golem_golem x8; opnpct golem_golem with golem_golemkey | this content via golem.rs2's hand-over |
| questlist op2 / questjournal close x9 | the journal (§9) |
| oploc crate3 / crate2 (Uzer) | generic search; not this quest |
| varrock / falador / kourend teleport tablets | generic tablets (§2b) |
| opnpc3 generalassistant2, shop buy chisel | generic shop |
| agrith_kiln_1/3/4 x9 | this content (§8) |
| opnpc1 macro_geni, genie lamp | generic random event |
| agrith_book op2 / book close / drop / pickup | this content: the book (§8) |
| oploct golem_demon_throne with chisel, hammer | the throne gems (§6); golem_portal.rs2 |
| staff_of_earth wield, xeric_* wield | recorder's kit |
| fai_falador_furnace op2, silver_crafting:agrith_sigil x2 | generic silver crafting; the sigil cell |
| agrith_sigil op2 x14 (Chant) | this content: the chant (§10) |
| agrith_sigil_mould drop, golem_golemkey drop/pickup, golem_ink drop | generic |
| oploc6 golem_throne_nogems, sez_agrith_rubble_wiz; opnpc6 agrith_sandy | examines, generic (cache text) |
| oploc1 agrith_portal_closing x2 | this content: out at 90..110 (§7) |
| opobj3 bones / bury | generic |
| opnpc1 agrith_dave_in_passage | this content (§7) |
| GE offer for silver bars | generic Grand Exchange |
| opnpc1 agrith_golem_throneroom, agrith_dave_sigil, agrith_reen_sigil | this content (§6) |
| prayerbook, orbs, retaliate | recorder's kit |
| opnpc2 agrith_naar x114 | the fight: shadowstorm_fight.rs2, its own audit |
| questscroll close, quest_rating close | completion (§11) |

4475 adds: the quest-start chat (§3), the Shantay Pass and the carpet (generic
travel; the doorway took the pass and left the player standing: now the two
one-tile steps a tick apart through it, south [503..504], [1491..1492] and
north [1394..1397], as 4452 [866..867]), Badden at stage 10 and 20, Evil Dave's refusals with 0, 1 and 2
pieces, the golem at 10 and 20, Reen's "Did you go to Uzer" at 10 and 20.

## 2. Teleports (4477)

Every move is the recorder's travel (dueling ring, necklace of passage,
tablets, glory: generic, §2b of the skill) or one of this quest's:

| tick | move | ours |
|---|---|---|
| [632], [878], ... | ruin stairs down 3491,3090 -> 2721,4886,0 | maplink row (unchanged) |
| [934] | the escort into the copy (32,19) | `@sots_escort` |
| [1553], [4107] | golem_demon_portal -> 2721,4911,0 | `~sots_portal_leave` |
| [2253], [2834], ... | golem_portal -> a fresh copy (32,20) | `~sots_portal_enter` |
| [3074] | agrith_portal_closing -> 2721,4911,0, then Tanya | `[oploc1,agrith_portal_closing]` |
| [3211], [1568] | stairs up 2721,4884 -> 3491,3090 | `[oploc1,golem_insidestairs_base]` (rocks or ~climb) |
| [4096], [4123], [4260], ... | Telekinetic Grab | the fight |

## 3. Stages and vars

Jagex's stage values, all recorded: 0 -> 10 (4475 [175], Reen's last page),
20 (4475 [731], Badden's "I don't know..."), 30 ([933] the escort box), 40
([1024] Denath's incantation page, words rolled), 50 ([1118] Matthew's last
story page), 60 ([1932] the golem's "He was carrying a large book" page,
agrith_kiln rolled), 70 ([2324] "Don't worry. I'm here to kill the demon!"),
80 ([2846] Denath's "It is time for the ritual to commence!"), 90 ([2940] the
backwards chant's last word, convinced_dave 1), 100 ([3082] Tanya killed), 110
([3830] "*gulp*"), 120 ([4059] the forward chant's last word), 124 ([5170] the
finishing blow; golem_clay 6 -> 4), 125 ([5190] the skill picked).

Carriers: agrith_badden_uzer 1 at the start; agrith_reen_uzer 1 at [933];
the recruits 1 -> 2 on the agreeing page's click (sigil handed over, a
`*_walking` npc slides a tile a tick to the stairs or portal) -> 3 when the
player is next free ([3177], [3324], [3379]); convinced_golem 0 -> 1 with the
refusal's menu [3355], 2 with the scroll [3388], 3 with the sigil [3763]
(golem_clay 4 -> 5 -> 6 [3783]). golem_throne_gems 1 with the gems [3051].
The incantation: agrith_incantation_1..5 drawn one at a time until unused,
every draw sent ([1024]: _4 drawn 4,2,0,2,4,2,3). Ours draws the same way.

## 4. Evil Dave's clothing check

| tick | worn | answer |
|---|---|---|
| 4475 [852] | xeric hat/top/robe | refused |
| 4475 [883] | black med helm + xeric top/robe | refused (1) |
| 4475 [1093] | priest gown + robe + xeric hat | refused (2) |
| 4475 [1124], [1927] | black med helm + priest gown + robe | "totally evil" (3) |
| 4475 [2543] | dyed desert shirt + robe | refused (2) |
| [251] | dyed shirt + robe + khazard helmet + dyed sword wielded | refused: the sword is not clothing |
| [662] | dyed shirt + robe + black med helm, dyed sword worn | "totally evil" + "And I love that evil black sword!" |
| [907] | same, plain sword carried | "But what's that sword? It's far too shiny..." |
| [931] | same, dyed sword carried | the sword line, "Okay, I'll take you through" |

The refusal is identical for 0, 1 and 2 pieces. The pieces beyond the five
recorded are the wiki's list (unverified).

## 5. Pages

Every recorded page (rsprox_pages.py over both sessions) is in the scripts
verbatim with its mood; menus keep the recorded option order and the
"leave out the last answer" menus of Badden, Reen, Dave, the golem and Denath
are modelled as recorded. The player's name is `<displayname>` throughout.

## 6. The throne room

A private copy of 42_76 level 2 at instance level 0, made on every entry;
furniture added each time (eight torches, five beds, the fire, two red
standards with torches); the wizards the stage calls for (30..80: Denath,
Matthew, Jennifer, Patrick, Eric, Tanya; 90..120: Matthew, Jennifer, Patrick
and the recruits who agreed; 120 adds Agrith-Naar at the circle). The escort
([933..1025]) is copied tick by tick: the fade, the lock, the jingle "denath
cutscene", the walls, Dave's walk, the player's walk, every camera move,
Denath's pages. The first ritual ([2846..3008]): Denath's call, the wizards'
places (recorded tiles), the player's place 30,38 and Denath's overhead
call, the chant (anim summon_summon_start + overhead per word, the circle's
echo a tick later, sigil forms from the first word, the camera backing off),
the success scene (floor loc, Denath's summon_tele + denath_leave, his sigil
on the floor, the pages and cameras, BOOM, agrith_portal_closing, Dave, Eric
and Tanya leaving). The second ([3830..4084]): the gather, the place 32,39
("You should take your place in the circle first."), the forward chant,
Agrith-Naar's arrival (summon_tele + qip_ds_delrith_summoned), the fire
blast killing Matthew (bones), the player's pages and the release into the
fight. Tanya's "Oh?" ([1151..1176]): three laughs on pages with no continue,
4 ticks apart, each also said overhead, the camera backing off her tile and
climbing (1000, 1500, 2000), the reset, then Matthew's page with the faces
turned. The throne's gems (the hammer and chisel, six gems, the ruby's box)
are golem_portal.rs2's, now from [2392], [2401], [3051].

## 7. The passage and the storm

Out at 90: Tanya and three ghosts added, her block/death seqs, ghost synths,
the "incantation" track, the line, her sigil and bones ([3074..3083]). The
stairs at 90..110 can drop rocks (line, roof-collapse spotanim + synth, a
camera shake of 10, the 1 hit at op+1, the reset at op+2, no climb): one
recorded climb, ours rolls 1 in 2 (unverified). The storm: the whole Uzer
square at 90..110, a tick after entering ("You must fight your way against a
sandstorm!", "incantation", a camera shake of 3, the sandstorm overlay, synth
agrith_sandstorm), closed a tick after leaving (the overlay, cam_reset,
"desert heat" out into the desert, nothing extra down the stairs); 1-damage
hits, 5 in 239 storm ticks: ours rolls 1 in 48 a tick.

Music by stage ([2253..4113]): the room plays Throne of the Demon before 80,
Incantation from the ritual's start ([2846]) to 110 and on re-entry
([3786]), Grip of the Talon from Agrith-Naar's arrival ([4062], [4113],
[4625]); the ruin and Uzer play Incantation at 90..110 ([3212], [3424]) where
they otherwise play Forgotten and The Golem. The engine's square table cannot
key on a stage, so the scripts play the stage's track, and both stairs hold
the square's own pick over the climb so only the stage's track goes out.

## 8. Kilns and the book

The kiln's mesbox or the book's box on the op tick; "You have already found
the book." while carried; a dropped book is found again ([2105]). The book is
"The Confession of Ellemar", four spreads with the recording's rows; page 6
carries the player's own incantation in the book's order.

## 9. The journal

Recorded: 10 (4475 [608]), 20 (4475 [775]), 50 (1603), 70 with the mould
(2547) and with a sigil (2741), 90 (3018), 100 with one, two and three
recruits (3231, 3297, 3343). Wiki (unverified): 30/40, 60, the golem's "will
only be able to help" line, 100 with four, 110..125.

## 10. The chant

The sigil's Chant: away from a ritual, five word menus and "Nothing
interesting happens." ([2655..2670]); at 80 in the copy, the first ritual
(off the place: Denath's overhead call [wiki]); at 110, the second.

## 11. The finale

[5170]: the stage 124, Darklight in the hand on the death tick, the golem home
(golem_clay 4), objbox "As you strike the final blow...", [5177] "You try to
wipe Silverlight clean...", [5180] "You feel the power flowing...", the two
"Select a skill to advance" menus, then [5190] the stage, the scroll
(agrith_sigil; "1 Quest Point", "10,000 <Skill> XP", "Darklight") and the xp.

## 12. The fight (shadowstorm_fight.rs2)

One recording of it (4477 [4084..5175]); the recorder prayed Protect from
Magic throughout. `d` = distance from his 3x3 footprint.

| tell | recorded | ours |
|---|---|---|
| attack gap | 4 ticks (163/169 blasts, 22/22 melee, 33/34 grabs) | 4 |
| choice | d=1 melee 22/22; d>=2 Telekinetic Grab 34/203, else Fire Blast; he casts into Protect from Magic | same |
| reach | casts from d=15 on re-entry ([4114], [4626]) | attackrange 15 |
| Fire Blast | demon_casting, fireblast_travel from his SW tile 51 -> 46+10d, heights 172/124 (43/31), angle 16, progress 64, area sound fireblast_cast_and_fire r10; prayed: failedspell_impact + spellfail, no splat (160+); unprayed: firebolt_impact + fireblast_hit, splat at cast+2+d/2 | same |
| Telekinetic Grab | telegrab_travel, telegrab_cast_and_fire, telegrab_hit at the landing; at cast+2+d/2 abyssal_demon_teleport on the player + golem_teleport; of 28 landings that moved, 22 on the five tiles beside his SW tile outside his 3x3 ((-1,-1) 1, (0,-1) 4, (1,-1) 7, (-1,0) 6, (-1,1) 4), 2 on (0,0), 4 further south after the recorder stepped away; none north or east; not moved at d=1 (6/34) | a walkable one of the five at random; not moved at d=1 |
| hitpoints and regen | 62 headbar readings over three lives ([4062..5170], healthbar_standard_30, floored): a pool of 96 and 1 back every 18 ticks miss 1 of 62 (tick-by-tick, regen capped at full); the cache's 95 misses 8 at best under floor, ceil or round, at any interval | hitpoints 96, regenrate 18 |
| melee | demon_update_attack_greater + black_demon_attack, splat on the swing; max 10 x3, 5 zeros of 22 | same |
| hit on him | demon_update_defend + black_demon_hit 70/70 | same |
| death | blow at D, D+1 demon_update_death + black_demon_death with the stage 124, Darklight and the first box, D+6 vile_ashes (despawn 200) and gone | same |
| re-entry | added at (31,35) facing south, full hp, first attack the next tick | same |

The last hitpoint needs the dyed Silverlight (or Darklight) in melee range
[wiki]: no recording kills him otherwise; ours leaves him on 1.

## Wire audit so far

- Escort: furniture, the corridor walls, the cast's tiles, the jingle, Dave's
  walk (first step lock+5) and the player's walk at a walk, a tile a tick
  (lock+7..+34), matched against [934..968].
- First ritual: per wizard 6 anims, 1 spotanim, 5 echoes, Denath's call, the
  13 form changes, Denath's summon_tele + denath_leave, his sigil: matched for
  a first-time success.
- Tanya: added the tick after the move ([3075]), her seqs, the ghosts, the
  synths, the line, sigil and bones.
- Journal: all nine recorded states exact on our wire (test/quests/
  _journals.lua, journal.sots.*).
- Camera, every scene, against a TORIRSSERVER_TRACE_OUT=2 run (CAM_MOVETO_V3,
  CAM_LOOKAT_V3, CAM_SHAKE, CAM_RESET, CAM_UNLOCK decoded): the escort (one
  for one, the last shot on the "Master!" page's click as at [974]), the
  first chant's steps (were a tile south: (39-p, 38+p) now), the collapse
  with BOOM's shakes (15, then 1 with the closing portal, were missing),
  Tanya's laugh (was missing), the passage shot (the lookat was a tile
  west), the second chant's steps ((32-p, 29+p), word 5 at (27,35); were
  wrong) and its per-word shakes 1..5 (were missing), the release frames'
  reset order per tick ([979], [3008], [4084]).
- Second ritual, per click (the bot's chant is 2 ticks a word against the
  recorder's 5+): the echo a tick after each word in both rituals (was the
  same tick), the five blank game messages as each circle closes ([2943],
  [4059], were missing), Agrith-Naar's hunt off in the scene (he cast at the
  player a tick after appearing), the blast at Matthew as [4071]: a map
  projectile from his tile and a cast synth, [4072] the impact, death,
  scream overhead and on a page with no continue, fireblast_hit and
  black_demon_death, [4075] gone with his bones, [4079] the next page.
- Music, every change: the room's track by stage (was Throne of the Demon
  always), the escort holding the square's pick until the release plays
  Throne of the Demon cut ([979]), the ruin and Uzer at 90..110, and the
  engine bug behind the room's wrong track: an instance's music area was
  matched with the instance's level, not the template's
  (ToriRSServer_RegionTileFor now returns the source zone's plane).

## Open

- Fire Blast accuracy and the finishing blow without Silverlight: 4477 is the
  only recording of the fight. Its 3 unprayed casts all hit (1, 7, 7); the
  standard magic roll (magic 100, no bonus, against the recorder's defence)
  predicts about 80%, which 3 of 3 cannot confirm or reject. No recording
  takes his last hitpoint without Silverlight in melee range, so ours
  leaving him on 1 is the wiki's rule. A recording that fights unprayed, or
  kills him with a spell, settles both.
- The 4 grab landings 2-3 tiles south (the recorder had stepped away
  between the cast and the landing): ours lands beside him every time.
- His facing re-send cadence (145 recorded blocks), the Protect from Melee
  branch (no recording prays Melee).
- Engine: the jingle's end brings back the track the jingle started over,
  where [994] brings back the one played during it (Throne of the Demon,
  cut); ToriRSServer_MusicPlay's Colosseum rule cancels the comeback. The
  client resumes the track either way. Needs the Colosseum recording
  re-checked before the rule changes.
- Engine: walking into Uzer from the desert at 90..110 sends The Golem a
  tick before the storm's Incantation (no stage-keyed square tracks); the
  stairs and portals hold it off.
- At 702 of our run the ruin's track is sent again after Tanya's scene (the
  mapzone trigger queued while the player was held); same track.
- The falling-rocks rate and the storm's hit rate are one-session fits.
- Re-entering the room mid-ritual (80) recasts the room as at 70 [unverified].
- After the quest the portal goes back to The Golem's own lair [unverified].
