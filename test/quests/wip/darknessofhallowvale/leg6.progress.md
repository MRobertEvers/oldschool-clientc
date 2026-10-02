# leg 6 notebook
Runs 1-8 from-leg 6 (portrait room entry, door 3631,3259 stays open, lab, Safalaan, Veliaf) green to the Veliaf dialog; run 9 adds completion rows.
Run 10 (full): tankVanstrom stage stuck at 220 after 40-tick await (leg 5); raised await to 120. Run 11 full.
Runs 11-12 full: tankVanstrom stage stays 220 even after 120-tick await and a step-round loop (3 strikes only). Over budget; gave up. Leg 6 itself 77/77 from-leg 6.
Fresh runner: run1-4 from-leg 5 diagnosing; tankVanstrom fix = fifth blow opens a mesbox holding stage 220, so wait 44 ticks then chat.drain then expect 230 (written). Now full run.
Run5 full: leg 6 failed at door clicks (doors left open by leg 5 in a continuous run); added door_click helper tolerant of 'no copy'. Run6 full.
Run6 full: leg6 failed bringMessage.ladder3631 (door 3631,3259 shut again); added door_click before it. Run7 full.
Run7 full: 393/393 PASS. gate RED only because harness's checkpoint row shares name leg.6.end (no shot); renamed mine leg.6.finish. Run8 full. helper_coverage MIXED: kickBoard (tool heuristic on 'Climb up the walls...' text vs Search op, row cannot move player) and goToMines (CONTENT_GAP doh_castle.rs2:64) in earlier legs.
