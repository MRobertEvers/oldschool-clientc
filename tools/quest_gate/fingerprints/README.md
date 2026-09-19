# Login / Character-Creator fingerprints

Two small `.json` files, each the top-left 64x64 pixel block (RGB, 8-bit) of
a screenshot from a REAL, deliberately-broken run -- captured once, not
guessed at:

- `character_creator.json` -- a login with no fixture copied into `saves/`
  at all. A fresh account boots into the Character Creator modal instead of
  the world (`run.py`'s own module docstring already names this exact trap:
  "a fixture that is not physically copied in means a fresh character stuck
  on the Character Creator modal, which reads exactly like a broken verb").
  Captured through the ordinary driver `t.shot` path, so it is a real PNG
  in the same format `gate.py` reads from every quest's `shots/*.png`.

- `pre_login.json` -- a VALID fixture (login would eventually succeed), but
  the process is SIGKILLed at a short wall-clock deadline, well before the
  server round trip completes. The frame itself comes from
  `TORIRS_PRESENT_BMP_FRAME=2` (`platform_sdl2.c`), which dumps the
  SDL-presented frame after 2 real render presents -- independent of login
  or plugin state entirely, so there is no `t.shot` to ask for (the killed
  process never reached `run(t)`). It lands on the client's own "Checking
  for updates - 0%" loading screen.

Both were confirmed visually before being saved (2026-09-19): the Character
Creator capture IS the Character Creator panel; the pre-login capture IS the
loading screen, solid black at the corner this block reads.

## Format

```json
{
  "name": "character_creator",
  "source": "<where the source frame came from, and how>",
  "width": <full frame width>, "height": <full frame height>,
  "channels": 3,
  "block_w": 64, "block_h": 64,
  "rows_hex": ["<192 hex chars = 64 RGB pixels>", ...]
}
```

`gate.py` decodes a real quest shot's own top-left 64x64 block with its own
PNG reader (stdlib + `zlib`, no Pillow) and compares it against both
fingerprints row by row -- mean absolute difference per byte over the
overlapping region. A quest shot that matches either one within
`gate.py`'s `FINGERPRINT_MATCH_THRESHOLD` means the run never actually got
past boot/login, however green its ledger otherwise looks, and `gate.py`
reports it as a finding.

Measured separation, so the threshold has real headroom on both sides: an
ordinary in-world shot's own top-left 64x64 corner is uniformly the engine's
clear colour, `(32, 36, 40)` (`0xFF202428` -- see the project memory note
"gray screen is an empty COMMITTED frame"), against `pre_login`'s solid
`(0, 0, 0)` and `character_creator`'s textured, high-contrast panel corner
(measured average around `(67, 60, 51)` with pixel values spanning the full
0-255 range from the "Walk here" tooltip text) -- tens of levels apart in
every case, nowhere close to the noise a headless, software-rendered,
non-interactive frame actually produces run to run.

## Regenerating

Only needed if the boot screen or the Character Creator's own layout
changes -- both fingerprints are frame content, not a fixed constant, and go
stale exactly when that content does.

```sh
QUEST_BINARY=src/torirs_questtest python3 tools/quest_gate/fingerprints/capture_fingerprints.py
```

Requires a built `src/torirs_questtest` (or `QUEST_BINARY=` pointing at any
built quest-test binary, same convention as `run.py`). Writes both files in
place; review the diff before committing, the same as any other captured
artefact.
