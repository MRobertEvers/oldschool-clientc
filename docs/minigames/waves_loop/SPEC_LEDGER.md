# Waves loop spec ledger

One heading per spec pass; one line per unit: rows, grade counts, presentation rows, open rows, content bugs.
Tables are `docs/minigames/<game>/encounters/<unit>.tsv`; open rows are `docs/minigames/<game>/OPEN.md`;
content bugs are `docs/minigames/waves_loop/CONTENT_BUGS.md`.

## matthew-mbp-m4-waves-b1-spec-inferno-a (the Inferno, batch A: the six system units, 2026-10-03)

Closed by the Opus closer. Parent commit b7b61ab98, content 1ea3ebb602 (comments and tags only; no constant value changed).
Total: 268 rows, A 92 / B 101 / C 12 / D 36 / E 27.

- **entry_and_cape**: 27 rows, A 23 / B 2 / C 0 / D 1 / E 1; presentation 11; open M3; bugs ENTRY-1..4 (new), ENG-6, ENG-7, INF-AV-001, INF-AV-008 (cited).
- **wave_table**: 105 rows, A 1 / B 99 / C 0 / D 3 / E 2; presentation 1; open M1, M2; bugs WT-1..5 (new), ENG-20, ENG-37, ENG-38 (cited).
- **pause_and_logout**: 19 rows, A 3 / B 0 / C 7 / D 5 / E 4; presentation 2 (both E); open M4, M5, M6; bugs PL-1..5 (new), ENG-8, ENG-42, ENTRY-1 (cited).
- **death_and_failure_reward**: 22 rows, A 8 / B 0 / C 0 / D 9 / E 5; presentation 0; open M7-M10; bugs DEATH-1..4 (new).
- **completion_reward_and_pet**: 77 rows, A 52 / B 0 / C 4 / D 16 / E 5; presentation 6; open M11-M14; bugs REWARD-1..5 (new), INF-AV-008, INF-AV-009, DEATH-3, DEATH-4 (cited).
- **practice_mode**: 18 rows, A 5 / B 0 / C 1 / D 2 / E 10; presentation 2; open M15-M17; bugs PRACTICE-1..3 (new), ENG-6 (cited).

What the closer changed and what it sends back:

- Re-derived three or more rows per table from the source files. Two corrections: `reward.pet_roll_denominator_on_task` C to D
  (the guide, STATED.md:14, says "on task it's actually one out of 50"; only the wiki states 75, though the guide's
  combined 1 in 43 is consistent with 75); `practice.osrs_mode_exists` pointed at wiki_Inferno_Strategies.wikitext:68, which
  is `{{Recommended equipment`; re-pointed to wiki_Inferno.wikitext:398 (C kept: that line plus the 2021 newspost).
- `wave_table.wave_delay` stays B, but its count is overstated: 5 of the 20 runs (1b92d513, 6a865311, 7d6a3542,
  b1e47c9b, b338bcb1) have reconstructed starts, so 340 of their gaps are Blert's asserted 6. Observed: 901 of 909 gaps
  are 6 (the 8 others are 22 x7, the pause before wave 69, and 14 x1).
- `inferno.constant`: four tag comments were reverted to HEAD because the source they named contradicts the value
  (the wave delay, `^inferno_arena_lz`, the combat spawn tiles header, the Jad tiles header). The disagreements are
  rows ENG-38, ENTRY-4, WT-1 and WT-4.
- Sent back (measured_by): the completion rows were taken under `::god` and `::zukhp 0` although a real solo run can
  reach a Zuk kill; the pet and exchange rates are static reads of the constant. The death rows used `::die` (the driver
  aborts a fought death at hp 0) and exit pay was read with varps set by a cheat. Several of our-side readings are a
  single instance: the landing tile, the teleport, the paused prayer tick, the resume-vs-continuous tiles, our 8-tick
  wave delay. Waves above 2 were read at entry, not fought.
- The death table has no presentation rows (no death sequence, message or respawn row). The pause rows are both
  E text rows. The pet model ids (33005, 34586) are not an inventory kind; every sequence and sound named is in
  AV_INVENTORY.tsv.
- Held patches (eat delay, prayer regeneration and drain): PL-1's paused prayer tick is measured without the prayer
  patch and must be measured again once it lands.
