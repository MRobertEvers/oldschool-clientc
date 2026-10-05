# Wave minigame fixtures

Server save files a wave test starts from (`fixture = "<name>.ini"` in the test
table). `tools/waves_gate/run.py` reads them from HERE, never from
`test/quests/fixtures/`: the quest loop owns that directory and may change a
fixture under its own rules. Written from the raid loop's
`test/raids/fixtures/README.md` at `94f55b306`.

- `fresh_lumbridge.ini` -- a byte-for-byte copy of
  `test/quests/fixtures/fresh_lumbridge.ini` as of `d9c86ca89` (waves branch
  `matthew-mbp-m4-waves-b1`, seam pass 1 `driver_port`; it is also byte-for-byte
  the raid branch's `test/raids/fixtures/fresh_lumbridge.ini` at `94f55b306`): a
  tutorial-graduate account beside Hans in Lumbridge, everything else at the
  fresh-character default. It is the start for the driver-verb proofs on an
  ordinary npc (goblin, man, cow) and for a wave test that enters its arena by
  `t.wave.enter`. Re-copy it deliberately if the quest fixture changes; do not
  edit it here unless the waves suite needs a different state, and then give
  that state its own file.

The rules are `test/quests/fixtures/README.md`'s: `[varps]` carries only
`scope=perm` vars, and a fixture is named for the state it holds
(`inferno_ready_maxed.ini`), never for a wave or step number.
