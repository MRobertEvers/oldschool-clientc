# Native multiplayer evidence — visual review

Produced by `tools/sailing_multiplayer_acceptance.py --session
/tmp/sailing-final-multiplayer --start --headless`, recorded in `results.json`.

> **Provenance corrected 2026-09-07.** The run described in this section used
> the **2026-09-06 21:38 pair** `c7e62cce` / `a53899eb`. The multiplayer
> acceptance has been re-run twice since — on `4854e303` at 08:11, and on the
> **final pair `5e34b81f` / `6a0e7f04`** at 09:36 (session
> `/tmp/sailing-fin3-multiplayer`, `ok: true`,
> `/tmp/sailing-fin3/accept/multiplayer.log`). `results.json` carries the final
> pair's hashes and is the current record. The five image descriptions below
> were written against the 21:38 captures; the frames were re-checked on
> `4854e303` and still show what is described.

**Re-run 2026-09-06 by worker `client`.** Hashes verified on disk before that
run:

| Artifact | SHA256 | Which pair |
|---|---|---|
| `src/torirs` | `c7e62cce180aaeb6bc4cd044818b0209ca99edb478f5280c3ebd10d6b79b1d8d` | 2026-09-06 21:38 |
| `src/build_opt/torirsserver` | `a53899eb8ce5d086e36df3653aaccfd2ee93c05f8e767ece05ecc40a94b9c3db` | 2026-09-06 21:38 |
| `script.dat` (30,076 scripts) | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` | unchanged throughout |

**PASS in 494.708 ms**, session `/tmp/sailing-final-multiplayer` PID 95405,
soft3d, headless, 807x503, `allow_stale_scripts: false`. The session was stopped.
Elapsed time here is **not** performance evidence: several workers ran clients
concurrently in this phase.

Scope: **two actual server players, production v5 packets, one native rendering
client**. There is no second external socket transport.

All five PNGs were opened and reviewed individually. The descriptions are what
is visible in each frame, not a restatement of the JSON.

| Image | What is actually visible | Matches the JSON claim? |
|---|---|---|
| `multiplayer-primary.png` | Baseline before the peer exists. The skiff alone on open ocean, sail furled upright with the red masthead pennant, one green-clad player standing at the helm, the cyan pick-highlight square on the deck tile ahead of him. `Adamant Bane 80/80`, Facilities showing `Steering` and `Repairs: No kits`. Minimap: open water, no land. | Yes — `before.sailing.vessel` hp 80/80, `sails_set 0`, one player. |
| `multiplayer-aboard.png` | Identical framing, and a **second humanoid stands on the aft deck**, clearly separate from the captain and drawn with its own body and limbs. Nothing else changed; the hull is still stopped with the sail furled. | Yes — `aboard.client.present`, `view 1`, `home_view 1`, fine 448,704, server `role 3` (passenger). |
| `multiplayer-running.png` | The sail is now **set** and bellied, the pennant streams, the Facilities slot icon has changed to the under-way icon, the chat reads `Vessel 1 sailing heading 0 at tier 2` and land has entered the minimap. **Both** figures are visible together — the peer has moved from the stern up beside the captain while the hull travels. | Yes — the "two deck RUN tiles while the hull moves" frame: `running.client.route_run 1`, `route_length 1`, hull `fine_z` one tile lower, `client.element` unchanged (the model was not destroyed and rebuilt). |
| `multiplayer-removed.png` | Sail still set, same `heading 0 at tier 2` line, so the hull state is unchanged — but **only the captain remains**. No afterimage, stray shadow or orphaned highlight where the peer stood. | Yes — `peer remove` retired the actor from the draw commands, not merely from server state. |
| `multiplayer-restored.png` | The checkpoint frame: furled sail, red pennant, moored Facilities icon, one player at the helm, cyan square in its original place — the same composition as `multiplayer-primary.png`. | Yes — `restore multiplayer_ocean` returned the scene to the saved baseline. |

## Diff: primary vs restored, outside the overlays

Channel-exact comparison over the world region `x 0-517, y 0-339` (excluding the
top-left FPS/memory readout), 162,320 pixels compared: **101 differ**, all inside
a single 20x28 box at x371-390, y217-244. `multiplayer-restore-diff-x8.png` is
the 8x crop (primary / restored / magenta difference mask) and shows the whole
delta is the **captain's own model**, one idle-animation frame apart — head,
shoulder and arm outline moved by a pixel. The hull, deck planking, helm
highlight and water are identical, and **nothing differs anywhere near the stern
tile the peer occupied** in `multiplayer-aboard.png`. No residue.

## Second-boarder deck-tile fix — native confirmation

Checked directly on the same warm session after the acceptance restored its
baseline, because the acceptance tool itself does not cover it (it calls
`peer place` before reading a position):

- captain (`peerproof`): server **6404, 67**, level 1.
- immediately after `peer create Deckmate2` (then `step 30`): peer server
  **6403, 66**, level 1, `role 3`, client `view 1`, deck-local fine 448,320.

Different deck tile on **both** axes; no stacking on the captain's square.
`multiplayer-second-boarder-tile.png` was opened: two distinct figures stand on
the deck, the captain at the helm behind the mast and the new guest one plank
forward and to port — visibly separate bodies on separate deck squares. The peer
was removed and the checkpoint restored afterwards.

## Flags and caveats

1. **There is no flat-mode image in this set, and `results.json` does not claim
   one.** The tool takes exactly five captures and never touches the world-order
   mode — every capture records `"world_order_mode": 0`. The
   peer-disappears-in-flat-mode proof is in `docs/sailing_validation/client/`
   (`client-peer-flat.png`), not here.
2. `multiplayer-removed.png` is **not** a negative control. It is a success
   frame: the assertion is that the actor is absent after removal. The genuine
   negative controls for this work live elsewhere
   (`lifecycle-login-stale-menu-before.png`, `/tmp/sailing-privacy-native-*.png`).
3. `permissions` on the guest read `navigate: false, cargo: false` throughout —
   a passenger, not a navigator. `screen.picked` is false in every reading; the
   projected point is reported but this run never aimed a click at it, so this
   set proves rendering and movement, not guest picking.
4. All five frames plus the second-boarder frame were rendered by `soft3d`. The
   GPU path for this fixture is covered separately in
   `docs/sailing_validation/gpu/`, which found two `gl3-zbuffer`-only visual
   defects (see that README) — one of which, deck-actor occlusion at the close
   deck camera, would affect a GPU rendering of these same peer-on-deck frames.
