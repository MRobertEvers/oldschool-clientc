# Historical GPU captures — client `54a09b3f`, 2026-09-06 13:00–13:23

Moved here 2026-09-07 by the `audit` worker. These nine files sat loose in
`docs/sailing_validation/gpu/` alongside the 2026-09-06 21:45 `gl3-*` /
`soft3d-*` comparison set and were repeatedly mistaken for it. They are **not**
final evidence and no ledger row rests on them.

## What they are

| file | what it is |
|---|---|
| `deck-results.json` | the run's own record: pid 38961, `src/torirs` `54a09b3f120cc4b5890cc4472dcf9bbfd9fc95e24708f7b587c83b4fef90cdc0`, `--opengl3-zbuffer`, user `sailgpu`, `allow_stale_scripts: true` |
| `deck-baseline.png`, `deck-restored.png`, `deck-net-installed.png`, `deck-net-restored.png`, `deck-sailing.png`, `deck-sailing-next.png`, `deck-sailing-looped.png` | that run's deck/animation captures |
| `ocean-skiff.png` | that run's 13:00 ocean frame |

The binary that produced them (`54a09b3f`) is three shared rebuilds behind the
final pair (`c7e62cce` → `4854e303` → `5e34b81f`), and the pack was loaded with
the stale override on. The **current** deck evidence with the same names lives
one directory up from `gpu/`, in `docs/sailing_validation/deck-*.png` /
`deck-results.json`, re-captured on `4854e303` at 08:11 and again on the final
pair `5e34b81f` at 09:36 on 2026-09-07.

## The one thing they are still cited for

`gpu/README.md` defect 2 (the `gl3-zbuffer` deck occlusion) says *"The same
occlusion is present in `historical/deck-baseline.png` from the earlier
gl3 run against client `54a09b3f…`"*. That sentence now means
**`gpu/historical/deck-baseline.png`** — this file. It is kept for exactly that
purpose: it is what makes defect 2 pre-existing rather than a regression of this
phase. The magnified crop of it, `gpu/gl3-deck-figure-occluded-prior-x5.png`,
stays in `gpu/` with the rest of the comparison set.

Opened and read 2026-09-07 to confirm it still shows what the defect claims:
the skiff under sail on open ocean at the deck camera, tan planking, grey sail
set, red masthead pennant, the cyan deck-highlight square amidships — **and no
player figure anywhere on the deck**, though the session is aboard and holding
the helm (`sailgpu`, chat "You take the helm of vessel 1…", inventory tab open
with 15000 coins). That absence is the occlusion.

**Done 2026-09-07:** `gpu/README.md` now cites
`historical/deck-baseline.png` in both places it referred to this file — the
note about the retired captures and the GPU-2 prior-art paragraph.
