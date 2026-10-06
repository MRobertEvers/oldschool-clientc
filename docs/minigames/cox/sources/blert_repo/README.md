# Blert repo — Chambers of Xeric material

Source: https://github.com/blert-io/blert, shallow clone, commit
`7c7750cf01b23e5d223623d7c34c5ef10ca327fd` (committed 2026-10-01), cloned 2026-10-02.

Finding: Blert has **no Chambers of Xeric guide and no CoX recorder yet**.
`web/app/guides/` holds only `blert/` and `tob/` (bloat, how-to-hmt, nylocas, plugins).
`web/app/(challenges)/raids/cox/page.tsx` is a "Coming Soon!" placeholder
(`web_raids_cox_page.tsx`). The only CoX material in the repo is the stage enum
(`common_challenge_stage_enum_excerpt.ts`, lines 335-372 of `common/challenge.ts`):
COX_TEKTON, COX_CRABS, COX_ICE_DEMON, COX_SHAMANS, COX_VANGUARDS, COX_THIEVING,
COX_VESPULA, COX_TIGHTROPE, COX_GUARDIANS, COX_VASA, COX_MYSTICS, COX_MUTTADILE,
COX_OLM. No tick constants, npc ids or phase rules for CoX exist in the repo.
