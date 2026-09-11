# Native cargo-hold privacy dropdown

All Settings row **470**, struct **6372**, enum **244**, varbit **19614**
(`settings_cargo_hold_privacy`, varp 4849 bits 22–23):
0 Navigators, 1 All players, 2 No players.

## The defect

`OSRS-Content/osrs239-content/scripts/setting_dropdown_entry_op.cs2` — clientscript
**3852** — is what a click on a dropdown choice actually runs:

```
if (cc_find($component3, $int1) = ^true) {
	cc_settext($text0);
	if ($int12 = 0) { ~settings_set_dropdown(...) / ~settings_set_keybind(...) }
}
```

Struct 6372 carries `param1085=1` ("the server applies this row"), so `$int12` is
non-zero, the apply hub **3967** is never entered, **8830** never runs, and the
mirrors that hang off those two scripts stay silent. The label moved; client and
server both stayed at 0. `sailing-privacy-native-bug.png` is that state.

No server in this revision arms the row, so the client has to finish it.

## The fix

`src/game/rs_cs2_host.c`. On a `CC_SETTEXT` whose `UITree_ApplyText` returned true —
the point at which the chosen label has demonstrably landed on a real component —
`rs_cs2_settings_apply_cargo_privacy_dropdown` identifies script 3852's cargo row
from the frame's integer locals (`[0]==2` dropdown kind, `[10]==470` setting,
`[13]==6372` struct, `[12]!=0` server-applied; at least 14 integer arguments), takes
`[2]` as the choice, and when it is 0..2 writes varbit 19614 and queues the
**existing** settings mirror — `CLIENT_CHEAT "setting 19614 <value>"`, flushed by
`app_cs2_flush_settings_mirrors` at the settled CS2 boundary, validated 0..2 by the
server. The constants and the reason each local is checked are in
`src/game/sailing_settings.h`.

This is not a generic varp/varbit bridge: it applies one named varbit for one named
row. The 8830 mirror, the 3967 hub path and the 9657 announcement path are unchanged.

## Evidence

| File | What it shows |
|---|---|
| `sailing-privacy-native-before.png` | **Negative control.** Pre-fix baseline: row reads "Navigators", client 0 / server 0. |
| `sailing-privacy-native-bug.png` | **Negative control.** Pre-fix defect: label reads "No players" while client and server were both still 0. |
| `privacy-search-cargo.png` | All Settings open, search `cargo`, row reads "Navigators". |
| `privacy-dropdown-open.png` | The row's dropdown open: Navigators / All players / No players. |
| `privacy-choice0-navigators.png` | After clicking Navigators: label "Navigators", client 0 / server 0. |
| `privacy-choice1-all-players.png` | After clicking All players: label "All players", client 1 / server 1. |
| `privacy-choice2-no-players.png` | After clicking No players: label "No players", client 2 / server 2. |
| `privacy-persist-after-relogin.png` | After `raw logout` and a fresh `start --resume-save`: label "All players", client 1 / server 1. |
| `results.json` | Measured values, widget rectangles, hashes, unit-test provenance and known observations. |

Each choice in the recorded run was driven from a different starting value, so a
stuck varbit could not be mistaken for a correct one.

## Reproducing

```sh
make -C src PLATFORM_OBJ_BASE=build_james test-cs2-sailing-settings

python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy \
  start --headless --binary src/torirs_james_opt --scripts /tmp/sailing-james-pack
python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy \
  button settings_side:settings_open -1 1
python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy step 30
python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy click 250 56
python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy raw text cargo
python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy click 433 105
python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy click 425 148
python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy step 40
python3 tools/sailing_harness.py --session /tmp/sailing-james-privacy \
  varbit settings_cargo_hold_privacy
```

Choice rows are `settings:dropdown_buttons` subids 2/5/8 for values 0/1/2, measured
at x 388–462, y 118–138 / 138–158 / 158–178. Re-derive them with the harness
`widget` command if the panel moves.

## Re-proof on the SHARED binary — 2026-09-06 21:52 CDT (worker `social`)

Everything above (the defect, the 3852 analysis and the fix) is unchanged. What
changed is the provenance: the whole proof was re-taken on the shared build and
the shared pack, and `results.json` was regenerated from it.

| Artifact | sha256 |
|---|---|
| `src/torirs` (**2026-09-06 21:38 pair**, two rebuilds before the final `5e34b81f`) | `c7e62cce180aaeb6bc4cd044818b0209ca99edb478f5280c3ebd10d6b79b1d8d` |
| `src/build_opt/torirsserver` (**2026-09-06 21:38 pair**) | `a53899eb8ce5d086e36df3653aaccfd2ee93c05f8e767ece05ecc40a94b9c3db` |
| `OSRS-Content/.../build/script.dat` | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` |

- Fresh session `/tmp/sailing-final-privacy`, user `finalpriv`, ocean fixture
  3072,3160, headless soft3d.
- The three choices were driven **0 -> 2 -> 1 -> 0**, so each one starts from a
  different value. All three: client == server == the chosen value.
- The choice row's rectangle is re-derived every time with
  `widget settings:dropdown_buttons <2|5|8>` rather than clicking the
  coordinates this file recorded last time; the measured rects are in
  `results.json`.
- Persistence: set to **1 (All players)**, `raw logout`, saved
  `4849 = 4194304 = 1 << 22` (bits 22–23 = 1), client stopped, the save copied
  into the **new** directory `/tmp/sailing-final-privacy-resume`, then
  `start --resume-save` there — which suppresses both the environment and the
  manifest bootstrap cheats, so nothing re-applies the value. After the resume:
  label **All players**, client 1 / server 1.
- Every PNG in this directory was opened and described; the descriptions are in
  `results.json` under `images[].shows`.
- The two negative controls, `sailing-privacy-native-before.png` and
  `sailing-privacy-native-bug.png`, were **not** regenerated. Their hashes still
  match the previous `results.json` byte for byte.
- `test-cs2-sailing-settings` was **not** re-run: this worker was not permitted
  to build. That row in `results.json` is carried-forward provenance.
- The `Connection lost` overlay noted before did **not** reproduce.

> **Provenance note, 2026-09-07.** This run used the 2026-09-06 21:38 pair
> `c7e62cce` / `a53899eb`. It was **not** repeated on `4854e303` or on the final
> pair `5e34b81f` / `6a0e7f04`. Nothing in either rebuild touches
> `src/game/rs_cs2_host.c`, script 3852 or varbit 19614, and the unit gate
> `test-cs2-sailing-settings` — which covers the same bridge — passes on both
> later pairs (`/tmp/sailing-fin3/test-cs2-sailing-settings.log`).
