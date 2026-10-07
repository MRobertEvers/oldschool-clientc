# blert-io/blert -- Tombs of Amascut material

Shallow clone `https://github.com/blert-io/blert` at commit
`7c7750cf01b23e5d223623d7c34c5ef10ca327fd` (2026-10-01 07:41 -0700), cloned 2026-10-02.

Result of the search (`grep -ril "akkha|kephri|zebak|amascut|ba-ba|tombs"` over the whole tree):
**there is no ToA guide page, no ToA npc-id table, no ToA tick constant and no ToA
phase rule in the repository.** `web/app/guides/` holds only `blert/` and `tob/`.

What exists, and is copied here:

- `web_app_raids_toa_page.tsx` -- the raid landing page: "Coming Soon! We are adding
  raid recording support for the Tombs of Amascut soon!" (a first-party statement that
  recording does not exist).
- `common_challenge_ts_toa_excerpt.txt` -- `ChallengeType.TOA` and the nine `Stage.TOA_*`
  enum values and their room order (APMEKEN, BABA, CRONDIS, ZEBAK, HET, AKKHA, SCABARAS,
  KEPHRI, WARDENS), `grep -n -i toa` with 2 lines of context.
- `challenge-harder/src/lifecycle/core/decide.rs` carries `TODO(frolv): handle toa` (lines
  129, 143), not copied.

- `recorder_probe_2026-10-02.txt` -- `GET /api/v1/challenges?type=N&limit=1` at 2026-10-03T01:39Z (2026-10-02 local): type=3 (ToA) `[]`, type=2 (CoX) `[]`, type=1 (ToB) and type=4 (Colosseum) return live rows. Blert still does not record ToA or CoX.
