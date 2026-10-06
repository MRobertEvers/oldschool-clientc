# Nylocas spec pass, 2026-10-03 (raw evidence for encounters/nylocas.tsv)

Blert streams (stage 12) fetched 2026-10-03 00:32-00:45Z, UA `3draster-tob-research/1.0 (mrobertevers@gmail.com)`, one request per 3 s:
14 Regular (scales 3,4,5), 4 Hard (scale 4), 1 Entry (b093b327-de5b-462f-b155-3dc73d80e293, scale 4, boss only; the other Entry raid 664c1f8b has no stage-12 stream, HTTP 404). Raw JSON is not committed (build/spec_state/.../blert_nylo_raw/).
`blert_analysis_output.txt` = output of the an_blert*.py scripts. `ours_analysis_output.txt` = our server's tick log
(runs spec_nylocas_2 normal, _entry, _hard; `spec_nylocas_3.lua` kills bigs with ::tobnylokillbig; `_6` breaks supports) via an1/an2/an3/an_cap.py.
Run: `python3 tools/quest_gate/run.py --script <lua> --name <n> --no-build --no-publish`, then the an*.py on build/quest_gate/<n>/ticklog.tsv.
