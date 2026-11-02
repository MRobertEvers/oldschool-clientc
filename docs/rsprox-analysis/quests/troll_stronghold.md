# Troll Stronghold — RSProx index 4455

Recorded 2026-11-02, rev 241.1, world 495, one session end to end (2,270 ticks), labelled
"Troll Stronghold and Freeing Edgar": a Varrock tablet and the bank [106..181], the games
necklace to Burthorpe [188], Denulth's start [213..291], Dunstan [331..352], Ghommal
[385], Saba [407..429], Tenzing's climbing boots [460..498], the stile and the climb north
[501..676], the arena and Dad [698..980] (`troll_stronghold_dad.md`), the troll pass
[1004..1024], the stronghold [1102..2187] (the kitchen, the haybales, the generals and the
prison key [1343..1832], the prison, both guards, Eadgar and Godric [1909..2152]), the
secret exit [2187..2198], Denulth [2221] and Dunstan's reward [2255..2264]. The journal was
opened nine times at six states.

Before this port the quest was a LostCity (2004) port: the 2004 start menu ("I'm sorry to hear
that." / "Is there anything...", then "I'll get Godric back!" as a choice), the completion
queued, a three-line reward scroll, the arena gates swinging open and staying open, the prison
door a door swing, the cells' 15-tick doors and the prisoners walking to the far end of the
prison, the 2004 journal, no diary task, the snore as an area sound with no Zzz, the generals
standing still and silent, the quest's varbits never sent.

**Ours:** `quests/quest_troll/` (quest_troll.rs2, troll_journal.rs2, troll_stronghold_camp_guard.rs2,
quest_troll.npc, quest_troll.varp new), `quests/quest_death/scripts/` (death_denulth.rs2,
death_dunstan.rs2, death_tenzing.rs2, death_saba_eohric.rs2), `skill_agility/scripts/stiles.rs2`,
`doors/scripts/door_procs.rs2`, `general_use/scripts/haybales.rs2`, `general_use/scripts/chests.rs2`,
`quests/quest_eadgar/scripts/eadgar_troll_chief_cook.rs2` (the drawers, the rack),
`quests/quest_monkeymadnessii/scripts/monkeymadnessii.rs2` (Kob),
`quests/quest_myarmsbigadventure/scripts/maba_travel.rs2` (the roof ladder),
`quests/quest_whileguthixsleeps/scripts/wgs_warguild.rs2` (Ghommal's shell),
`drop_tables/scripts/troll_commander.rs2`, `drop_tables/scripts/mountain_troll.rs2`,
`interface_diaries/configs/diary_claim.constant`, the generated blocks of varp 318 and the three
generals (`server_fields.generated.varp/.npc`, `npc_movement.generated.npc`,
`npc_movement/t/troll_general*.move`). Dad and the spectators: `troll_stronghold_dad.md`.

**Tests** (scriptrun, private packs from a symlink mirror with HEAD's quest_losttribe while
another session had it mid-edit, `src/build_e9_opt/torirsserver`, `TORIRSSERVER_DEV=1`):

- `test/quests/troll.lua`: 77 of 78 rows pass; `quest.journal` is the one no scriptrun can pass
  (no quest list). It now follows the recorded start (the confirm), the boots objbox, the
  walk-through arena exit, and takes the first general from where the recorder stood.
- `test/quests/_journals.lua`: the seven recorded states (rows 1..11 of each). Headless through
  `::trolljournal` + `t.ui.journal_read`, all seven match row for row, and on the scriptrun
  wirelog all seven match tag for tag.

## 1. Every action in the recording

| tick | action | verdict |
|---|---|---|
| 45 | welcome screen | engine |
| 59.., 297.. | quest list x19, overview, journal x9 | **this content** (§9) |
| 106 | Varrock teleport tablet | generic: audited 2026-10-29 |
| 117..118 | worn-tab removes | generic, bound |
| 129..181 | bank booth, deposits, withdraws, tabs | generic, bound; not diffed |
| 188, 2204 | games necklace Rub > Burthorpe (subop 0) | generic: audited 2026-10-29 |
| 196 | death_archer1 Attack-?(op2) | the recorder's click in Burthorpe; generic combat |
| 213, 2219 | Denulth | **this content** (§2, §10) |
| 325, 2250 | Dunstan's door (poordoor) | generic door: matched in the Death Plateau port |
| 331, 2255 | Dunstan | **this content** (§2, §8) |
| 359.., 597.. | staminas, prayer restores, lobsters | generic consumption, bound |
| 385 | Ghommal | generic: **fixed** — the click names the multinpc shell `warguild_ghommal` (13646); only the rung was bound |
| 407..429 | Saba's cave in/out, Saba | Saba **this content** (§2); the cave: Death Plateau port |
| 460..498 | Tenzing's door, Tenzing, his back door | Tenzing **this content** (§2); doors: Death Plateau port |
| 501 | stile (death_fullstyle) | generic: **fixed** (§3) |
| 510 | climbing boots Wear (x3) | generic equip; `equip_feet` (2237) recorded |
| 526 | mountain goat Examine | client/engine |
| 546..661 | troll_climbingrocks x10 | **this content** (§3) |
| 585 | troll_stronghold_entrance x3 (before the exit is known) | **this content**: "You don't know how to open the secret door." with the face, matched |
| 619..676 | climbing rocks top / bottom | **this content** (§3) |
| 699..980 | arena doors, Dad, spectators | **this content**: doors §4, the fight `troll_stronghold_dad.md` |
| 710, 1366 | staff of earth / rune scimitar Wield | the recorder's kit |
| 712.., 1047.. | Protect from Melee / Missiles | generic prayer |
| 968 | cert_blankrune_high pickup | another player's kill: a clue wizard's drop at 2909,3617 turned public (`ownership=other`), not Dad's; Dad never dies in this recording (`troll_stronghold_dad.md`) |
| 1004..1024 | troll pass entrance / exit | **this content**: teleports matched (2907,10019 / 2908,3654), no arrivedelay |
| 1092 | camp guard Examine | client/engine |
| 1100 | diary_troll_climbingrocks | superseded by the door click at [1102]; the loc is `climb_unqualified` |
| 1102 | troll_stronghold_door | **this content** (§5) |
| 1111 | death_troll_melee4 Attack | generic combat (the stronghold's mountain trolls) |
| 1116..1920 | stairs x10 | **this content**: face L+1, teleport L+2 (`p_arrivedelay`), all six landings matched |
| 1124..1913 | prison door x3 | **this content** (§6) |
| 1146..1195 | interior doors x5 | generic door swing: `door_open` (62), stays open (still open in the [1301] re-send), matched |
| 1154 | Ug x3 | Troll Romance not started: mesbox "He doesn't look like he feels like talking.", matched |
| 1173 | Arrg | the two pages (chatang1, chatscared1), matched |
| 1198 | Burntmeat | Eadgar's Ruse not started: the five pages and moods matched |
| 1212..1229 | kitchen drawers open / search x2 | generic: **fixed** (§7) |
| 1231 | troll_celldoor (kitchen) | generic door: **fixed** sound — `iron_door_open` (71), not `door_open` |
| 1236..1254 | haybail x5 | generic: **fixed** (§7) |
| 1274..1282 | storeroom door | "You need to find the right key to open this door." on L+1 with the face, matched |
| 1325 | Kob | **fixed**: the page "What you do here, human? Get out of my sight!" (chatang1); ours said "A defeated chieftain's camp." |
| 1343..1832 | troll generals, their drops, the prison key | **this content** (§7b) |
| 1352..1354 | quest list x3 in combat: "You can't do that right now." | generic UI rule: OPEN |
| 1418 | myarm_ladder | **fixed**: the player's refusal page before My Arm's Big Adventure (chatbored2); ours climbed |
| 1559..1571 | chestclosed open / search | generic: **fixed** (§7) |
| 1844, 2124 | bury big bones | generic, bound |
| 1873 | run toggle | engine |
| 1929 | eadgar_rack | **fixed**: mesbox "You look under the rack but find nothing." on L+1 |
| 1945..2152 | Eadgar, Godric, guards, cell doors | **this content** (§6) |
| 2087..2090 | the guard's drop pickups | generic pickups (`pick2`) |
| 2187..2198 | troll_stronghold_exit / entrance | **this content** (§5) |
| 2264 | quest scroll close | generic |
| 2269 | logout | engine |

### Teleports

| tick | cause | landing R | ours |
|---|---|---|---|
| 110 | Varrock tablet | 3214,3423 | generic, audited |
| 191, 2207 | games necklace > Burthorpe | 2897,3551 / 2898,3551 | generic, audited |
| 410, 429 | Saba's cave | 2269,4752 / 2858,3577 | Death Plateau port |
| 504 | stile | exact move to 2817,3563 on L+1, the step off on O+3 | **fixed** (§3) |
| 556..661 | climbing rocks | 2-tile exact move on O | **fixed** (§3) |
| 623, 632, 676 | rocks top/bottom | 3-tile exact move O+1 / walks + fall | top **fixed**; bottom matched |
| 726..952 | Dad's knockbacks | `troll_stronghold_dad.md` | |
| 1005, 1024 | troll pass | 2907,10019 / 2908,3654 | matched |
| 1105 | stronghold door | 2837,10090,2 | matched |
| 1122..1920 | stairs | six landings | matched |
| 2193..2198 | secret exit / entrance | 2827,3646 / 2823,10050 | matched |

## 2. Dialogue (every page)

Every page of `rsprox_pages.py` is in our scripts with its mood, but these changed:

| R | before | now |
|---|---|---|
| Denulth [258..291]: ambush pages, then the combat-level mesbox, `Start the Troll Stronghold quest?` Yes/No, "Is there anything I can do to help?", the warning page; stage 10 and "You've started a new quest" with "I'll get Godric back!" [287] | two 2004 menus, forced `|` breaks, the stage before the last page | the recorded order; "No." keeps the 2004 "I'm sorry to hear that." [unverified] |
| Denulth White Knights, last page chatang4 [243] | chat_sad | chat_angry |
| Saba during the quest [415..421] | the Death Plateau-complete branch | its own six pages |
| Tenzing "Nice place" during the quest [494] | "Now that the Imperial Guard know..." | "The Guard know the secret way... upper hand soon!" and "Let us hope so traveller!" chathap1 |
| Dunstan's reward [2255..2261] | the completion queued | the stage, the talisman and the scroll on the last page's click (§8) |

## 3. The climb: stile and rocks

| interaction | R | ours before | now |
|---|---|---|---|
| stile [501..507] | walk onto 2817,3562; L+1: exact move to 3563 (`human_walk_style` delay 30, delay1 30, delay2 94, no locmerge); O+3 a step to 3564 | a teleport onto the near tile and `p_delay(0)` first (a tick late), a loc merge | the teleport only when off the near tile; no merge |
| rocks, success (9 of 10) | on O (L+1 / C): both lines, a 2-tile exact move across (seq 839 delay 30, 30/94, facing along it), busy to O+3 | Underground Pass's proc: `p_arrivedelay`, exact move 0/60 | the recorded shape |
| rocks, slip [650..651] | O: "You climb onto the rock..."; O+1: "...but you slip back down.", 3 damage, `human_stumble_back` | the same | kept (rate unverified: 1 in 10 at Agility 30) |
| rocks without boots [548], [658] | "You'll need some climbing boots to go that way." + face | matched | |
| rocks top [622..626], [670..674] | face O; O+1 the 3-tile exact move and the xp; seq cleared O+4 | xp and the clear a tick or three late | the xp on the move tick |
| rocks bottom [629..639] | walks x2, fall (`human_climbing_fall`, `stumble_loop` x10), O+5 "You fall and hurt yourself.", "Ouch", 2 damage | matched (5% of hp + 1) | |

## 4. The arena doors

Walk-throughs, not swings (§1b), each leaf alone: on L+1 the leaf's slot an `inviswall` and its
inactive copy on the tile across the wall for two ticks, `iron_door_open` (71), the player one
step onto the door tile. entrance right 2897,3619 -> inactive 2896,3619 rot 1 [704]; entrance
left -> 2896,3618 rot 3 ([790], another player); exit left 2916,3629 -> exit_inactive
2916,3628 rot 0 [980]. Dad's page "No human pass through arena without defeating Dad!"
(chatang1) and `troll_told` go out on the crossing tick [704]. Ours on the scriptrun: [189]
the same three locs and the sound, back at [191]. Exit right: unrecorded, mirrored.

## 5. The stronghold's doors

| R | ours |
|---|---|
| front door [1105]: `troll_entered_stronghold`, the Fremennik easy task "Enter the Troll Stronghold" (task 9) and its line, the teleport, on L+1 | the diary task was never awarded: **fixed** (`~diary_task_award`). Jagex's own varps for it (1184 bit 9, `started_fremennik_diary`) are not what our diary system writes: OPEN (diary-wide) |
| secret exit [2193]: `troll_opened_back_exit`, "You open the door.", teleport 2827,3646 | matched |
| secret entrance from outside [2196]: "You open the secret door.", 2823,10050 | matched |

## 6. The prison

| R | ours before | now |
|---|---|---|
| prison door, no key [1128], [1475]: "You need a key." + `locked` (2402) + face | no sound | `locked` |
| with the key [1911]: L+1 the stage 30, "You unlock the prison door.", the key gone, `lockeddoor` (1631) + `door_open` (62), the door crossed (`..._prison_door_open` 2847,10107 rot 1, two ticks) | a door swing that stayed | the walk-through |
| cell door, no key [1949]: "You need a key to unlock this door." + `locked` + face | "...unlock the door." | the recorded line |
| cell unlock [2093], [2144]: the line, the varbit / stage 40, the key gone, `lockeddoor`, the prisoner's page (chathap1), the player facing him | `locked`, the 15-tick door | `lockeddoor` |
| the walk out: from O+2 a glide a tile a tick (npc-info `teleport`), the door crossed on the tick he steps onto it (`troll_celldoor_*_open` x-1 rot 1, two ticks, no sound), east to x 2835, south, removed the tick after the last step: Eadgar at 2835,10068 [2117], Godric at 2835,10063 [2169] | the door 15 ticks, the walk on to 2824,10050, a tick late | the recorded path and ends; the first step O+2 |
| sleepers (camp guard [1087..1295], prison guards [1948..2182]): every 9..16 ticks `troll_zeds` (height 0) and the east-facing angle re-sent; a tick later `troll_snore` as a synth to a player within 3 tiles (13 heard at 2..3, none at 4+) | an area snore, no Zzz; the camp guard had no timer at all | the recorded rule; `timer=1` starts the camp guard |
| pickpocket fail [1971..1973]: line + face on O; O+1 "You fail to pick the guard's pocket.", say "What you think you doing?", transform to awake; it swings on O+2 | "What do you think you're doing?" | the recorded say |
| pickpocket success [2136..2137]: O+1 `human_pickpocket`, `pick`, the key, "You pick the guard's pocket and find a key." | matched | |
| the awake guard's drop [2081..2083]: Cell key 2 on the death tick (despawn 200, owner only, never public, no line), bones and helm two ticks on | the key public after 100 ticks | `obj_add_private(.., 0)` |
| Godric's talk [1957..1963] | matched (moods too) | |

## 7. The stronghold's other locs

| R | ours before | now |
|---|---|---|
| kitchen drawers open [1216..1218]: face, then `human_openchest` + `cupboard_open` (58), open a tick on; still open 80 ticks later | no sound | `cupboard_open` |
| drawers search [1221..1222]: "You search the drawers..." then "You don't find anything." | one line "...but find nothing of interest." | the two lines |
| haybail x5 [1238..1254]: spam "You search the hay bale..." + face + `human_pickupfloor` on O; O+3 spam "You find nothing of interest." or the needle page "Wow! A needle!<br>Now what are the chances of finding that?" (chathap2) with the needle | game messages; the page wrapped by us | `mes_spam`; Jagex's break kept |
| chest [1567..1571]: `human_openchest` + `chest_open` (52) on O, open O+1; search "You search the chest but find nothing." | no sound; "You find nothing." | both (4433's Camelot chest agrees) |

### 7b. The generals and the prison key

- The prison key [1802]: on the death tick, `despawn=200, ownership=self, neverturnpublic`, with
  "<col=ef1020>Untradeable drop: Prison key</col>"; bones and the rolled drop two ticks on.
  Ours: the key public after 100 ticks, no line: **fixed**.
- Sounds: troll_general swings with `2h_crush` (2502) every attack [1349..1413] (ours: null —
  **fixed** in quest_troll.npc); troll_general3 `mace_crush` (2508), matched; defend
  `stone_armour3` (33) to the dealer and death `troll_death` (866), matched.
- Movement: 81 single walk steps over 22 lives, within 3 tiles of each spawn; ours stood
  still (generated `wanderrange=0` + `defaultmode=none`): **fixed** (wander 3, authored
  `.move` rows).

## 8. Stages and vars

| var | value | R | ours |
|---|---|---|---|
| troll_quest 317 | 10 | [287] with Denulth's warning-page click | matched |
| troll_told (318 bit 4) | 1 | [704] the arena crossing | matched |
| troll_accepted_challenge (bit 3) | 1 | [718] Dad's page | `troll_stronghold_dad.md` |
| troll_quest / troll_to_the_death | 20 / 1 | [925] the surrender | `troll_stronghold_dad.md` |
| troll_entered_stronghold (bit 2) | 1 | [1105] the front door | matched |
| troll_quest | 30 | [1911] the prison door | matched |
| troll_freed_eadgar (bit 0) | 1 | [2093] the cell | matched |
| troll_quest | 40 | [2144] Godric's cell | matched |
| troll_opened_back_exit (bit 1) | 1 | [2193] | matched |
| troll_quest | 50 | [2261] Dunstan's last page | matched |

**Varp 318 (the base of all six varbits) was `scope=temp, transmit=no`**: none of the quest's
varbits ever reached the client, and they were lost at logout. Now `quest_troll.varp`:
perm, transmitted (`varp_transmit_audit.py`).

The completion [2261]: the stage, qp, the count, the line, disease -50/0, the talisman, the
chat closed, `midi_song_v2` 147, jingle 152, the scroll ("1 Quest Point", "A Law Talisman").
Ours: the shared `~quest_complete_rewards` on the click; the jingle was Quest Complete 2 (153,
by difficulty), now 1 (152) from the recording (`questpoints.rs2` recorded rows).

## 9. The journal

Six states recorded (TS_4455_* in `_journals.lua`); all match row for row and tag for tag.
Changed: "Godric <col=000080>from" (Jagex's space), periods on the struck rows, stage 30
folds both earlier steps into "I found my way into the prison.", Eadgar folds over Godric's
step. UNVERIFIED: stage 10 with Dad's warning heard or his challenge accepted, the completed
journal.

## 10. Wire audit (scriptrun, `build_e9`)

Diffed `ticklog_tells.py` of the run against the recording per section: the stile and rocks
seqs, the arena door locs and sound, the prison door, both cells (locs, ticks, the prisoners'
tiles and despawns), the snores and Zzz, the pickpocket say/transform, the general's key and
drops, the completion jingle. Mismatches found were fixed above; `npc_movement_audit.py
paths` over the recording and ours found the generals (fixed); every other type the zone
spawns either matches or differs only by how long it was in view.

## Open

- "You can't do that right now." on opening the quest list in combat [1352..1354]: a generic
  UI rule we do not have. Which state decides it (combat timer or a delay) is not settled.
- The Fremennik diary: Jagex sends varp 1184 (bit 9) and `started_fremennik_diary`; ours keeps
  its own mask varps. Diary-wide.
- The player's defend sound: Jagex sends `blade4_1..4` (2751..2754, delay 20) where ours sends
  `human_block_*`/`human_hit_*`. Engine-wide, not this quest.
- A world npc-vs-npc fight aborts in `[proc,pest_on_damage]` (varp 6674 with no player) on
  every run of this test; not this quest's npcs.
- Dad is added per player (owner-private) at 2912,3613, not where 4455 first shows him (2901,3618
  facing west [698]): added there, he blocks and never swings on the scriptrun (troll_dad.lua's
  swing rows fail), cause not found. Whether Jagex's Dad is a
  shared world npc is unverified (he is not in the m45_56 static spawn dump). An owned npc faces its
  owner whenever idle (engine), so ours faces the player before the challenge and never sends the
  recorded face resets [926]/[981] (`troll_stronghold_dad.md`).
- Unrecorded: the arena's right exit leaf, the prison door after stage 30, a sleeping guard
  attacked, the climbing-rock slip rate, the stage-10 "told"/"accepted" journals, the
  completed journal, Denulth's "No.".
