# Preserved Phase-5 roster experiment

This directory preserves the last broad generated roster manifests before the
Phase-5a candidate was narrowed. They are byte-for-byte copies of these paths
at root commit `d81bce6d53e5086184fb24df9c3d8caa8e6468a2` (`2026-08-10T07:43:50-05:00`, `audio from rt4`):

- `roster_assets_530.csv` — SHA-256
  `6a64fcc542d4cad7f67d77799d408b3eff888b8d4e041d1048088770717c9491`
- `roster_assets_530.ini` — SHA-256
  `94773790c8a1f7d90cfafaeccbafea2a96997eebc69ba2a99ce46760c78c6e5a`

They are review/provenance evidence only. Do not pass these files to
`cachepack import`, `stage_summoning_overlay.py`, or any feature-on bake.
The active `../roster_assets_530.csv` and `../roster_assets_530.ini` are the
separate 78-pair Phase-5a candidate and must remain distinct.

The broad `summoning_roster_530` content and ledger evidence are also
preserved in the marked content lane, held out of staging by
`roster_boundary_530.json`. This archive records the prior manifests without
accepting, rewriting, or deleting that experimental work.
