# The Ides of Milk from RSProx

Rebuilt 2026-10-23 from two recordings that each play the whole quest:

| Session | Rev | Account | Covers |
|---|---|---|---|
| index 4325 (local, 2026-10-22, w403) | 241 | group ironman, female | Cassius [193] to the quest scroll [917] |
| index 4129 (rsprox.net 4153, 2026-05-21) | 238 | normal, male | the whole quest, then Gillie's rewards, the lamp and the cowbell [780..832] |
| index 3162 (rsprox.net 3179) | 232 | — | Gillie's three ordinary branches [12612..13050], Seth's ordinary line [14989] |

Both quest sessions agree page for page except the one fork (Seth's taxes line, below).
Lines no recording shows come from the wiki's `Transcript:The_Ides_of_Milk` (read
2026-10-22) and are marked `[wiki]` in the scripts; their moods are unrecorded.

Content: `OSRS-Content/osrs-content/server/scripts/quests/quest_idesofmilk/`
(`idesofmilk.rs2` talks, lamp, journal; `idesofmilk_locs.rs2` shelf, book, samples,
pen gate, arena, Brutus). Test: `test/quests/idesofmilk.lua` (106/106 live;
scriptrun 105/106, the journal row needs the clientscript VM scriptrun lacks).

Tools: `tools/rsprox/rsprox_pages.py` (every page), `rsprox_interact.py`,
`rsprox_npcinfo.py`, `npc_block_inventory.py`, and a sequence diff of each section
(transcript vs the scriptrun wire log `TORIRS_SCRIPTRUN_WIRELOG`) aligned on the
stage writes, because dialogue pacing is the player's clicks.

## Stages (`%varb20106_cowquest`), every write recorded

| Value | Written with | 4325 tick |
|---|---|---|
| 1 | the Yes, with "You've started a new quest: @q_mes@The Ides of Milk</col>" | [217] |
| 2 | Cassius's "Of milk." page | [228] |
| 3 | his "my associate Brutus" page | [244] |
| 4 | Seth's "grab me book" page, with `cowquest_seth_information` | [409] |
| 5 | "Cassius snatches the book from your hands." (book removed) | [513] |
| 6 | "Cassius hands you a milk sample to try." | [522] |
| 8 | the first drink, on the op tick | [528] |
| 10 | "Cassius hands you a milk sample." | [550] |
| 12 | the Duke's "Why not speak to Gillie Groats?" page | [594] |
| 14 | Gillie's "It definitely won't hurt your chances." | [718] |
| 16 | the second drink, on the op tick | [726] |
| 18 | Gillie's "Perhaps you'd be interested in dealing with him?" | [742] |
| 20 | Brutus despawns, with the tag line | [804] |
| 21 | Gillie's "Why don't you go and talk to him about it!" | [835] |
| 22 | Cassius's last page answered; the scroll | [917] |

`cowquest_gillie_information` goes to 1 with Gillie's "That would be Seth" page
[315]; `cowquest_reward` with the amulet's box (4129 [787]); `cowboss_reward_lamp`
when the lamp is used (4129 [807]).

## Rules the recordings fix

- **Seth's taxes line** is the only fork: the ironman recorder says "I don't think
  I've ever paid any taxes." / "You're ahead of the game then!", the other "You've
  found a way to get around the taxes?" / "A way or two." (no menu either side; the
  wiki keys it on ironman mode).
- **Gillie's menu** shows "Can you share what makes your cows so productive?" only
  while the investigation is on; "Who are you?" is answered with no echo of the
  choice, "Can you tell me how to milk a cow?" is reworded to "So how do you get
  milk from a cow then?" (3162).
- **The Duke** skips his menu while the permit is in play (4325 [587]: greeting, then
  the player's line).
- **The book** (4325 [435..440]): `human_readbook` on open; rows as Jagex broke them,
  every model row hidden, clientscript 2524 before each mount; a page turn sets
  `pagepos` and plays `readbook_spotanim_turnright` (92) + `human_readbook_turnright`;
  the close clears the seq. New shared procs `~book_read_open` / `~book_read_spread`
  (`interface_book/scripts/book.rs2`).
- **First sample** (both sessions): O stage 8, `human_eat`, synth `liquid`, a player
  page with no continue; O+3 1 damage on the poison splat, `wgs_player_coughs`, the
  recorder's hit synth (510 `female_hit_2` female, 519 `human_hit_2` male); O+5 the
  next page, whose continue hands straight to Cassius (no op).
- **Second sample**: stage, eat, `liquid`, a page with a continue; no damage; Gillie
  takes it up.
- **The pen bull** (`cowboss_mapped`, multinpc on the stage) wanders 3260..3264 x
  3295..3297 with `enabled_ops 0`.
- **The release** (R = the Yes; 4325 [770..780], 4129 [539..545]): R mesbox with no
  continue + fade out; R+1 minimap off; R+2 an instanced copy of the field, the
  player on the gate tile facing south, the gates swapped for `_noop`/`_exit`
  forms, Brutus added (`cow_boss_spawn`, spawn angle north, instant face on the
  player), Boss Moosic (859) unlocked and played, synth 11212; R+4 minimap on,
  `cam_forceangle(280, 1024)`, fade in, the same box with a continue. Brutus: T+1
  faces the player, T+4 `hpbar_hud_npc`, T+5 "Moo", T+6 comes.
- **Brutus** swings every 5 ticks (`cow_boss_attack`, no sound of his own; 8/8
  recorded swings rolled 0 on armoured recorders). After 1..6 swings a special:
  - **stomp**: seq `cow_boss_stomp` delay 30, "*snorts*", synth 11213, a face along
    the 8-way line to the player; S+4 `vfx_cowboss_stomp_impact02` on him and five
    `vfx_cowboss_stomp_impact01` in an L round the corner (4/4), the player hit if
    on one at the start of the tick (hit 3/3 standing, missed 2/2 stepped off);
    a series of up to 3, 8 ticks apart (4129 [586], [594], [602]);
  - **charge**: seq `cow_boss_charge` delay 60, "*growls*", synth 11210, a face on
    the 4-way line; S+4 a 4-tile slide (`exact_move` start, 0, end, 60, yaw), the
    face reset, the player in the strip hit;
  - both: S+6 ops re-sent (`0b00010`, 4/4) and he moves again; next swing S+8.
  - Special damage 14, 18, 18 (the wiki's 15-19 floor is under a recorded 14).
- **The death**: "m-moo..." and ops off; death seq; despawn 4 ticks later with the
  tag game line and stage 20; the tag box a tick later; its continue: boss bar off,
  fade out, E+1 minimap off, E+2 back on the same tile of the real field, E+4
  minimap on + fade in, E+6 overlay off. No kill count during the quest.
- **Completion** (shared, `quests/scripts/questpoints.rs2`, `questscroll.rs2`): the
  "Congratulations, you've completed a quest: @q_mes@..." line, song 147 with a zero
  envelope then the jingle (8/8 recorded completions), the model at zoom 400 placed
  at 78,128, all seven rows blanked then written, 2524, the mount; `quest_points`
  (153:6) is never sent (0/20 scrolls).
- **Rewards** (4129 [784..807]): the amulet given is `cowbell_amulet_empty` though the
  box shows `cowbell_amulet`; the lamp's picker is two chat menus ("Attack.|Strength.|
  Defence.|Ranged.|More options..." / "Magic.|Hitpoints.|Prayer|Previous options...")
  and a Yes/No; on Yes the bit, `found_gem`, the box.

## Wire audit (ours = scriptrun wire log + live tick log, 2026-10-23)

Sequence diff, per section: matched events, then every row only one side has.

| Section | R window | Matched | Unmatched rows and verdict |
|---|---|---|---|
| Cassius start | 4325 [198..247] | 186 | ours: the test's `::` teleports (test kit) |
| Gillie | [296..320] | 69 | none |
| Seth | [373..413] | 88 | taxes pages: ironman vs not (4129 matches ours) |
| Shelf and book | [427..441] | 72 | `if_setobject` not in our wire log (checked in code: zoom 400) |
| Cassius and the book | [505..523] | 80 | as above |
| First sample | [527..552] | 84 | hit synth 510 vs our 519: recorder female, bot male (rule above) |
| Duke | [586..605] | 97 | none |
| Gillie and second sample | [705..746] | 154 | none |
| Release | [765..781] | 22 | ours mounts `hpbar_hud` + script 2376 (engine; the recorder's was already up); synths 2720/2833 vs ours are each side's weapon and block sounds (kit) |
| Tag and leave | [798..816] | 15 | the field's music after: 76 (4325) / 327 (4129) / 2 (ours): region music, engine |
| Gillie after | [822..837] | 55 | none |
| Finish and scroll | [895..923] | 87 | ours: region music from the test's walk |
| Rewards and lamp | 4129 [783..809] | 60 | same-tick order only |

Brutus's npc info (`npc_block_inventory.py`, both sessions vs the live tick log):
sequence 17/17, say 9/9 (kinds and counts by the fight's length), enabled_ops,
exact_move, facing, headbar present on both sides; spotanim
`slayer_tots_impact` is 4129's recorder's spell.

Fixed during the audit: Brutus's state moved off npc vars (the engine holds 64
and the allocator is past it); the special no longer drops him from combat
(`npc_bind`, not `setmode none`: specials had chained every tick); a charge stops at
a fence; region music held in the arena (`midi_hold`); `minimap_toggle(0)` at R+4;
the kill count skipped during the quest; the tag box's break and tick; objbox pages
no longer send script 600 (468 recorded objbox pages, none does); the music-unlock
line's colour, no full stop, and silence when `music_unlock_text_toggle` is 1.

## Open

- **`name_change "Bull"`** on Brutus's add (3/3) is not sent: the server has no
  command for NPC_INFO NAME_CHANGE (the client decodes it).
- **Facing during a special**: ours re-faces the player at S+1 (the engine's combat
  tick faces the target every tick); recorded holds the special's coord face to
  S+6, and a charge's reset at S+4. Needs an engine face-hold.
- **The arena's other npcs**: Jagex's copy re-adds the field's farmers, cows,
  chickens and ducks with no ops; ours copies the map only.
- **Female hit sounds** in the shared `~combat_player_hit_sound` are male-only; the
  only female splats on the index (4325, two) used `female_hit_2`.
- **The engine's region-unlock line** (`torirs_server_world.c`) is still
  `<col=ff0000>You have unlocked...`; the content proc is fixed.
- **Cassius walks on through the final talk** (4325 [897..923]); ours holds him
  for every talk (`~chatnpc` playerfaceclose).
- Unrecorded and from the wiki: the exit gate's Leave / Quick-escape, a cardinal
  stomp's three tiles, the special's damage spread, the shelf's random lines'
  odds, post-quest releases.

## The journal (2026-10-27)

RSProx index 4381 [870] (2026-10-25, after this doc was written) opens it complete:
nine paragraphs, every row struck, a blank row, "QUEST COMPLETE!" in red. Copied
verbatim (`idesofmilk.rs2` `~idesofmilk_journal`, Jagex's typos included); ours
matches all 33 rows (`test/quests/_journals.lua`). The stage that first adds each
paragraph (1 at 1, 2 at 2, 3 at 5, 4 at 8, 5 at 10, 6 at 12, 7 at 16, 8 at 20, 9 at
22) and the current step's look (the newest paragraph unstruck in blue) are fitted,
not recorded: no session opens it mid-quest.

