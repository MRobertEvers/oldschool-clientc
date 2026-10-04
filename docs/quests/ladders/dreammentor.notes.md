# Dream Mentor -- what the ladder cannot know

Cyrisus is NOT at the guide's tile in every form. The cache's shared Fallen Man lies at
2348,10362,2 (not 2346,10360). From stage 6 an owner-private copy stands on that tile and is
the one that changes shape; clicking either lands on the same talk (it reads the stage).
After stage 20 the owner's copy is gone (the shared Fallen Man stays lying there).

Between steps:
- Surface ladder 2142,3944 and mine ladder 2330,10353 are explicit (the generic ladder only
  changed plane): down lands 2330,10352,2; up lands 2143,3944,0.
- Cave: wall A 2335,10346 crawls in (lands 2341,10356), wall B 2341,10355 crawls out (lands
  2335,10345). Both work on every visit; the tiles between are blocked on purpose.
- Jack ('Bird's-Eye') only exists from stage 16 (cache multinpc on %dream_prog).
- Sink 2091,3922, brazier 2073,3912 have no cache op: use the vial on the sink, the
  tinderbox on the brazier. Cyrisus at the Oneiromancer's door (stage 20) and at the brazier
  (stage 24) is the cache's multinpc on %dream_cyris_multi.

Dialogue that gates progress:
- Start: "Yes." then close the status screen (escape or its X).
- Food: 5% a piece, 20 pieces; the same food twice in a row is refused. Goals 20 / 40 / 70.
- Talk rounds: the positive pair per round is in Quest Helper's talkToCyrisus3 list; 13 right
  answers fill Spirit (8 each). Stage 8 needs Health 40 and Spirit 32; stage 12 needs 70 and 72.
- Jack: "Cyrisus in the mine" (chest + bank screen); Cyrisus: "Talk about the Armament".
- Oneiromancer: "Cyrisus." (stage 20 gives the vial; stage 26 finishes; needs 2 free slots).
- Cyrisus at the brazier: "Yes, let's go!" with the potion; free re-entry afterwards.

Fights (all in the owner's own instance of the arena, entered at 6431,109; no Prayer, no
teleport): Inadequacy (180 hp, crush + ranged, summons up to 5 Doubts, they die with it), then
Everlasting 230 hp, Untouchable 90 hp (def 434, magic is the answer; not aggressive), Illusive
144 hp (burrows after two hits, Cyrisus stomps it at 22 hp). Bring melee armour and ~20 sharks;
an unarmoured level-99 died in 96 ticks to the Inadequacy, and a rune-armoured one
died to the Everlasting toe-to-toe (two 24s inside one 16-tick cast): eat at 70+. Lectern 'Our lives': Leave frees the
instance, the next entry starts from the Inadequacy again.

Different from the guide: the equipment choice is the real bank screen (interface 260), the
set is graded by Cyrisus (dreammentor_armour.rs2); the reward lamp is the shared inert one.
