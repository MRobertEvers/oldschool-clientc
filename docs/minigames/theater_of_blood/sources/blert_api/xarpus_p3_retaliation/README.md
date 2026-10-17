# Xarpus P3: when the retaliation is judged (2026-10-17)

- `fetch.py N`: the first N completed Normal raids per scale 2..5 from blert's
  `/challenges` list, Xarpus room events only (stage 14), into `./raw/` (about
  54 MB for 200 rooms; not pinned).
- `analyse.py`, `analyse.txt`: every P3 swing on Xarpus by a player whose
  hitpoints the stream carries (the recorders), binned by ticks since the last
  TOB_XARPUS_TURN (npc attack 11), against a single-tick hitpoint drop of 40 or
  more on the swing's tick (`@+0`), the next (`@+1`) and the one after (`@+2`).

What it shows (199 rooms, 2127 swings, 14 retaliations):

- Every retaliation shows on the recorder's hitpoints ONE tick after the swing,
  never on it: the hit is answered when it LANDS on him, not when it is rolled.
- 4 of 213 swings ON a turn tick were retaliated (1.9 %). Vecs (a8895ca1) and
  winiciuz (89ce5780) each swung from the same tile at turn-5 unharmed and at
  the turn tick retaliated: the turn-tick swing is judged against the NEW
  quadrant. A rule that frees the tick of a turn cannot produce these.
- Swings at turn+7 are retaliated 5 of 253 (2.0 %) against 5 of 1861 at
  turn+1..+6 (0.27 %): a turn+7 swing lands on the next turn tick and is judged
  against a quadrant the swinger could not yet see. Turn+0 and turn+7 are the
  two blind swings; the other six are judged against a facing on screen.
