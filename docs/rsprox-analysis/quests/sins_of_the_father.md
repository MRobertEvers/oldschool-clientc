# Sins of the Father, from RSProx

Written 2026-10-23. The quest (`quest_sinsofthefather/`, 13 script files) was
built from two recordings of the whole quest:

| session | rev | world | what it shows |
|---|---|---|---|
| index 1621 = rsprox.net 1637 | 226.1 | w367, normal | the whole quest, [1023] "Yes." .. [7330] the scroll; the recorder is level 126, a male character |
| index 4 = rsprox.net 102 | 227.1 | Leagues V | the whole quest again; the same 69 stage values in the same order |

Branches no recording reaches (re-talks, refusals, lost items) are the wiki
transcript's and are marked `[wiki]` in the scripts. Rules inferred from too
few samples are marked `[inferred]`, and lines with no source are marked `[unverified]`.
The cutscenes were drafted with `tools/rsprox/rsprox_cutscene.py` and read
against the transcript.

The test is `test/quests/sinsofthefather.lua`, 10 legs. A full fresh run
passed 395/395 rows (scroll, 2 quest points, journal) on 2026-10-23, after
the client, compiler and `[ai_hunt]` fixes below. The run includes the
gallons read off the drawn scroll and the Darkmeyer guard probe.

## The stage ladder

`%varb7255_myq5` steps by two, 0 .. 138, each value written on the recorded
tick of the action that earns it. The table with the tick and the page for
every value is in `configs/sinsofthefather.constant`. Index 4 walks the same values.

## Pages

`build/sotf_private/audit/pages.py` compares every `if_settext` on the chat
interfaces (217, 219, 229, 231, 11, 193) in [1000..7340] with the scripts' text.
The recorder's name is read as `<displayname>`.

- 2215 of 2243 pages match verbatim.
- The 28 that do not are travel, not the quest: a Meiyerditch citizen, the
  vyrewatch's mine trip [4010..4162] and [5252..5278], and the mine cart's
  boxes. The two valve boxes "You set the valve to 3/2." are built by script
  code that the checker cannot expand.
- Fixed by this check:
  - Safalaan's "One day he nearly succeeded." [3664] and Vanescula's
    "...husband, King Ascertes Hallow." [3670]. These had used the wiki's wording.
  - Drezel's trailing space [1857].
  - Safalaan's hideout talk at stage 36 [2425..2430], which was missing.
  - The unscented disguise's three Inspect boxes [4661], which were missing.

Chathead expressions (`audit/faces.py`; the helpers pick the 1..4-line seq,
so the family is compared) agree on 1060 pages with two exceptions:

- [1728] was fixed (happy).
- [1669] uses seq 8542. In rev 226 that is a chathead seq. Our rev-241 cache
  reuses the id for `vfx_mad_angel_attack_smite`, so Kroy keeps `chat_neutral`.
  OPEN.

The two recorders are male. The two lines that name the player in the third
person use `~sf_pronoun` [inferred female forms].

## The wire audit (2026-10-23)

`build/sotf_private/audit/diff.py` splits both sides into windows by stage
value: the recording's varbit 7255 and our tick log's varp2712 low 9 bits
change on the same actions. It then compares named events in each window:
npc and player seqs and spotanims, sounds, projectiles, map spotanims, locs,
and myq5 npc add / retype / del.

It was run against the full run's tick log
(`build/sotf_private/audit/full5_ticklog.tsv`). Overall: 1584 events matched,
4066 recording-only, 4627 ours-only. The bulk of both one-sided columns comes
from these sources, each checked once:

- **The recorder's kit.** Osmumtens fang, voidwaker spec, blood fury
  (`sanguinesti_staff_heal` on whatever they hit), a defender parry, potions,
  equips, prayers, a blowpipe. Ours: the Ivandis or blisterwood flail, sharks
  and manta rays.
- **The recorder's trips.** A house visit mid-trek (every `poh_*` loc), fairy
  rings, slime-pit and Ectophial teleports, a Grand Exchange trip
  [6179..6490], and the vyrewatch mines [4010..4162].
- **Fight length.** Our test character fights 2-4x longer than the recorder,
  so ours-only counts of the same seqs and sounds scale with it.
- **The view.** Npcs hidden by their multinpc varbit are added and freed with
  the player's view; the tells tool does not print those adds. Locs are
  re-sent as zones re-enter view: the chase's `_noop` locs appear 18..32
  times in the recording and once in ours.
- **World noise.**
  - `synth 3424` recurs through the whole session ([1165], [2016], [2378],
    [4514] ...).
  - `synth 3120` / `4195` are clock ticks.
  - Ours: the crop-circle event at 2970,3216 (`ii_magic_wheat_*`).
  - Darkmeyer's and Burgh's town life: `sitting_eating`, vampyres feeding,
    sentinels fighting others.
  - [1245] the recorder turned the music back on (varp 168 0 -> 5) and the
    server re-sent the song.

Per section, what remains after those, with the verdicts:

| section | R / ours | finding | verdict |
|---|---|---|---|
| start, Hameln, Carl, barrel [1023..1159] | match | - | - |
| Carl's chase [1159..1510] | 19 matched | population of the copy matches npc for npc. Carl's 9 `emote_think`s are the recorder's failed first chase plus the retry's 5 | matches |
| Kroy's cutscene [1510..1673] | 44 matched | - | - |
| Kroy [1673..1712] | 9 attacks / 51 | rule: claw when adjacent and the player is not praying melee, else a blood blast; the recorder prayed melee ([1676]), our test does not. Vial 1/15 recorded (both sessions), ours ~1/8 | rule matches; vial rate [inferred] |
| Paterdomus, woods [1775..1980] | match | - | - |
| the meeting [1980..2080] | 31 matched | the stun and entangle projectiles were each sent twice (a drafting slip) | FIXED (once). Map projectiles are not in our tick log |
| the trek [2209..3009] | 275 matched | juvinates and nail beasts match. Bridge crossing is a 2-tile forced step [2867] | FIXED (bridge). The escape's mist: added on the juvinate's tile, `mist_disappear` 8 ticks on (recorded 9), gone the next |
| Burgh, boat, graveyard, Vanescula, lock, coffin, convincing [3009..3902] | match | lock presses are `if_button1 665:32 sub=tile` as recorded | - |
| lab, bloodvelds [3902..4414] | 77 matched | rockslide `barrows_rockslide1` [4270] at (8,102) | matches (once; the recording re-sends it at [4275] as the zone re-enters view) |
| disguise [4414..4769] | match | - | - |
| Damien's cutscene [4885..4942] | 23 matched | the player's walk in [4893..4898] was missing (it left them behind the doorway fire); two `invisible_type8_nonblocking` at (7,29) (6,30) L1 [4892] | FIXED |
| Damien [4942..5064] | 203 matched | dart / smoke / fire mix matches (12/9/3 of 24 vs 41/32/10 of 83). Veliaf's blows never happened: his timer read a player varp with no player bound | FIXED (Veliaf). Also fixed: Damien faces the fire's tile on the cast and lights it 2 ticks later; 1 attack in 7 at Veliaf, dart or smoke (3/21); faces back 3 ticks later |
| Darkmeyer [5182..5850] | match | a disguised player is never attacked (15 sentinels, 10 vyrewatch passed) | FIXED: `[ai_hunt]` (below) |
| Maria [5570..5572] | - | talked to through the cell's bars after opening the prison door; an ap op within 5 | FIXED |
| Arboretum doors [5768..5772] | - | one forced step from the doors' tile at L+1 | FIXED |
| note, valves, tree [5778..5849] | match | the tree's swing loop (`p_oploc(3)`) had no op3 binding | FIXED |
| flail [6020..6179] | 6 matched | Lvl-3 Enchant via the jewellery group (218:12, 218:37) | - |
| ambush cutscene [6588..6840] | 116 matched | - | - |
| Vanstrom [6840..7201] | 662 matched | 750 hp (752 recorded, last blow overkill); 200 on the enrage. Protect from Magic blocks his spells until he heals (the recorder prayed it: prayer_allactive bit 12) | FIXED (block was a half) |
| escape, hideout [7201..7330] | 58 matched | - | - |

**Music.** `midi_song_v2` envelopes were compared per call. Fixed to their
recorded envelopes with `midi_song_envelope`:

- [2497..3016] the trek's silences and room songs.
- [3577]/[3589] the boat and graveyard landing.
- [6587]/[6593]/[6733] the ambush.

The default (0,60,60,0) matches everywhere else.

**Damage.**

- Kroy: 240 hp; the recorded splats total 240.
- Damien: darts are ranged and get through Protect from Magic, smoke is magic
  and does not. The recorder took a 10 and a 2 while praying magic, and the
  splat is the dart's.
- Vanstrom: barrage max 24, enraged max 10. The recorder's 41 zeros and 17
  small splats fit that. Specials: 34..49.
- The flails' bonus against vampyres, +25% (blisterwood) and +20% (Ivandis)
  [wiki, quoting Mod Ed], is applied to the quest's vampyres in `sf_hits.rs2`.
  The +5% accuracy is not applied.

## Engine and client findings

- The engine holds 64 npc vars, and the quest's were past it; they abort
  silently. The fight state moved to the copy's `map_instance_var` registers
  20..24.
- An npc's `[ai_timer]` has no player. A `%varp` read there aborts the script.
  Veliaf's and Ivan's timers did, every time.

## Fixed in the engine and client (2026-10-23)

- **Clientscript strings.** The client kept 511 bytes of each RUNCLIENTSCRIPT
  string argument; the Arboretum note (839, [5780]) lost the sentence naming
  the gallons, and Jagex sends up to ~2,900. `PktRunClientScript` now holds
  its strings in one 8 KB arena per payload (`pkt_runclientscript_str` /
  `_set_str`), so any one argument may use all of it. The struct is smaller
  than the 28 x 512 matrix it replaced, and the cs2 task's cap rises with it.
  `test-runclientscript` asserts a 2,900-character argument whole. The quest
  test reads the gallons off the scroll.
- **ServerScript string literals.** The compiler had the same cut one step
  earlier. The lexer's token held 511 characters and dropped the rest without
  a diagnostic, so the note reached the wire at 511 even after the client fix.
  `SSC_TOKEN_TEXT_MAX` is now 8192, and a literal past it fails the compile
  ("string literal longer than ..."). The whole content tree compiles under it.
  `test-ssc` (`long string literals`) runs a 2,900-character literal with an
  interpolation through `mes` whole, and checks that a longer one is refused.
- **`[ai_hunt,<npc>]`** (trigger 183). It runs when an aggressive npc is about
  to take a player, with both active; content declines by clearing the
  target. Darkmeyer's sentinels, vyrewatch and juvinates hunt again as their
  stats say and decline a player in the full noble disguise
  (`sf_darkmeyer.rs2`). Leg 8 checks both: no guard takes the disguised
  player, and one takes them with the top off.

## Fight and chase checks (2026-10-23)

- **Carl** notices a player 2 tiles away or nearer, walking or stopped
  [inferred: across both recorded chases the recorder was never nearer than
  3, on 26 ticks]. The recorder walked 3..6 behind him.
- **Damien's fires** need a straight walkable line from him, so they land in
  his room, never behind a wall.
- **Vanstrom.**
  - He casts from 1..3 tiles (65 barrages: 54 at 1, 4 at 2, 7 at 3) and walks
    in from further ([6879], [6973]..[6975]). `attackrange` 3, was 10.
  - The orb is 5 tiles from him on the player's far side (one recorded,
    [6968]), and goes off on a player within 1 ([6973], 34).
  - Prayed, about 1 blood spell in 5 still lands for 1..8 (9 of ~50).
  - The form change follows [7141] / [7144] / [7149] / [7152] / [7154]
    to the tick.
  - The acidic bloodveld walks through npcs (`moverestrict=passthru`). It sets
    off 4 ticks after it appears ([7073] -> [7077]), faces into the arena, and
    is gone the tick after its burst ([7083]). Its acid pool goes at +30 with
    `mory_vanstrom_acid_remove` ([6914]).
  - Matching already:
    - specials every 9..11 attacks, never the same twice (6 recorded);
    - the pets' posts, (52,37) and (52,23), on the player's far side;
    - darkness 48..49 and stepped off at T+2;
    - the enraged blow every 4 ticks;
    - lightning: 19 tiles every 4 ticks over dx 45..54, dz 22..38, struck 3
      ticks on.

## Open

1. **Jingle end.** Jagex re-sends the current song when a jingle ends ([3602]
   = 3577 + 15.4 s; [2516] = 2507 + 5.67 s). Our engine does not. Engine-wide.
2. **Chathead seq 8542** (above).
3. **`rsprox_npcinfo.py` and `rsprox_hit_causes.py`** match nothing on these
   transcripts (they expect `id=<symbol> (<n>)`). The per-npc tallies here were
   made with `audit/npcblocks.py`.
