# Raid fixtures

Server save files a raid room test starts from (`fixture = "<name>.ini"` in the
test table). `tools/raid_gate/run.py` reads them from HERE, never from
`test/quests/fixtures/`: the quest loop owns that directory and may change a
fixture under its own rules.

- `fresh_lumbridge.ini` -- a byte-for-byte copy of
  `test/quests/fixtures/fresh_lumbridge.ini` as of f7025ad03 (raid branch
  `matthew-mbp-m4-raid-b1`, seam1 `raid_tests_directory`): a tutorial-graduate
  account beside Hans in Lumbridge, everything else at the fresh-character
  default. It is the start for the driver-verb proofs on an ordinary npc
  (goblin, man, cow) and for a room test that walks or teleports to its raid.
  Re-copy it deliberately if the quest fixture changes; do not edit it here
  unless the raid suite needs a different state, and then give that state its
  own file.
- `tob_normal_done.ini` -- `fresh_lumbridge.ini` plus one Theatre of Blood
  completion (`6826 = 1`, `varp6826_tob_completions`, scope=perm), so every
  raider of a Hard party test passes the door's "You must complete the Theatre
  of Blood once before attempting Hard Mode." check (raid seam19).

The rules are `test/quests/fixtures/README.md`'s: `[varps]` carries only
`scope=perm` vars, and a fixture is named for the state it holds
(`tob_bank_maxed.ini`), never for a room or step number.
