# Seam22 analyses (tob_normal_trio_findings, 2026-10-05)

Pinned from the pass state dir (build/seam_state/matthew-mbp-m4-raid-b1-seam22/trio/, which is
gitignored) so the CONTENT_BUGS.md "From seam22" rows and the xarpus/sotetseg/nylocas spec rows
point at files that exist in the tree.

- `fetch_blert_xarpus.py`: the Xarpus room harvest (blert stage 14; Normal trio, duo, four, solo).
  The raw JSON (about 7.7 MB) is not pinned; the script re-fetches it into `./blert_xarpus/`.
- `an_blert_xarpus.py` .. `an_blert_xarpus6.py` and their `.txt` outputs: where the chained
  splats land (raider tiles: trio 115/117, four 40/40), what blert can and cannot count.
- `an_ours_xarpus.py`, `an_ours_xarpus_{before,after}.txt`: our chain count, HEAD pack vs seam22.
- `an_blert_verzik_p1.py`, `an_blert_verzik_p1_attacks.py` and outputs: Verzik P1 in Normal
  trios (tiles at the autos, prayed drops, Dawnbringer specials). Input: the spec pass's
  `blert_verzik/<mode>_<scale>_*.json` streams (argv: dir mode scale).
- `wiki_Prayer_drain.txt`: the drain lines of `../../wiki_Prayer.wikitext` the Sotetseg prayer
  drain row quotes.
- `s22_sote_trio.lua`, `s22_xarpus_trio.lua`, `s22_prayer_drain.lua`: the party scratches the
  rows quote (each declares its own party size; run with tools/raid_gate/run.py --script <file> --name <name> --no-build).
- `mk_before_pack.sh`, `ab_room.sh`: the off-tree HEAD-pack A/B (TORIRSSERVER_SCRIPTS) used for
  every "before" measurement; nothing in the shared tree was edited to take one.
