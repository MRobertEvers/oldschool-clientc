# The rig pass: which animations can each monster play

Owner, 2026-10-03: "The corpus is unlikely to supply animations. You will have to use the
tooling that finds animations that match the rigging of the walk and idle animations of
the npc." A cache npc record names only its ready and walk animations. Every other
animation the monster can play is built on the same skeleton (framemap) as those two, so
the rig closes the candidate set. `docs/DEATH_ATK_DEF_ANIMS.md` explains the method and
`tools/gen_npc_combat.py` implements it.

One Sonnet worker per unit, ONE AT A TIME (owner: never many at once), documents only: no
client or server is built or run, no generated file is rewritten.

## Method, per unit

1. **The npcs.** Resolve each npc symbol of the unit to its cache record in
   `docs/minigames/<game>/sources/cache_npc.txt` (id, name, size, readyanim, walkanim and
   the other animation fields, with the dump line).
2. **The rig.** The framemap of the ready and walk animations. Import
   `tools/gen_npc_combat.py` as a module from a scratch script under
   `build/rig_state/<pass>/` and use its own loaders (`load_catalog`, `seq_features`,
   `seq_names_by_rig`; read `main()` for how it builds the rig index and which catalog
   it needs). NEVER run it with `--write` or `--fix-authored`: it rewrites thousands of
   generated ledgers. If ready and walk sit on different framemaps, list both rigs.
3. **Every sequence on the rig**, from `configs/all.seq`: id, name, frame count, length in
   client ticks and game ticks (30 client ticks per game tick), every frame sound with its
   name, and the fields that hint at use (forced priority, loops, the hand items).
   A rig shared by many creatures (the human rig, framemap 0, carries about 3,900) is NOT
   closed by the join: narrow it by the unit's own name words and say so; a row found
   that way is tier `name`, never better.
4. **Classify each candidate**: role is one of `ready`, `walk`, `attack` (number the
   variants), `special`, `spawn`, `death`, `defend`, `transition`, `other`, `unknown`.
   Tier says what the role rests on, and nothing may be claimed above its evidence:
   `bound` (the npc record names it); `rig+name` (on the rig and its Jagex name states the
   role); `rig+sound` (on the rig and a frame sound's Jagex name states the role);
   `rig` (on the rig, role not stated: role `unknown`); `name` (shared rig or off-rig,
   matched by name words only). Never classify by resemblance to another monster.
5. **Graphics and projectiles.** A spotanim has its own model and rig, so the join does not
   reach it. List the spotanims in `cache_spotanim.txt` whose Jagex name shares the
   monster's name words, tier `name`, with their `anim=` sequence and its length.
6. **The generated ledger.** Compare with `OSRS-Content/osrs239-content/npc_combat/<letter>/<npc>.combat`
   and write down every row where the ledger names an animation that is NOT on the npc's
   rig or belongs to another monster by name (the Colosseum inventory found Sol Heredit
   given the javelin colossus's attack and death). Do not edit the ledger: it is generated.

## What a worker writes

- `docs/minigames/<game>/sources/rig/<unit>.tsv`: columns `npc`, `npc_id`, `framemap`,
  `kind` (seq or spotanim), `id`, `cache_name`, `frames`, `game_ticks`, `frame_sounds`,
  `role`, `tier`, `note`. One row per candidate per npc.
- `docs/minigames/<game>/sources/rig/<unit>.md`: under 80 lines: the rig or rigs, the
  count of candidates, the attack, special, spawn and death candidates with their tiers,
  what stays `unknown`, the ledger disagreements, and what only a recording, a plugin
  constant or a picture could settle.
- Its report JSON in the pass state directory. Nothing is committed by a worker.

A role at tier `rig` or `name` is a candidate, not a fact. The corpus (Blert's attack
tables and plugin constants carry animation ids) and, later, a picture taken by the test
driver are what promote it.
