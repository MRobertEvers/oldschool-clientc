# blert-io/blert — constants and rules files (shallow clone, 2026-10-02)

Repo https://github.com/blert-io/blert, commit `7c7750cf01b23e5d223623d7c34c5ef10ca327fd` (2026-10-01), cloned depth 1 on 2026-10-02.
Files are flattened (`/` becomes `__`). Pre-existing copies elsewhere in `sources/` (blert_plugin, blert_guides/tob_nylocas_mechanics_page.tsx, blert_guides/tob_how-to-hmt_page.tsx, blert_nylocas-waves.json) were NOT re-fetched.

| File here | Origin | Why |
|---|---|---|
| common__npcs__npc-id.ts, common__npcs__npc-definitions.ts | common/npcs | every ToB npc id, kinds, hitpoint tables |
| common__data__nylocas-waves.ts | common/data | typing of the wave json |
| common__split.ts, common__event.ts | common | split names, event enum (stage ids) |
| challenge-server__event-processing__theatre.ts | challenge-server | server-side analysis of rooms |
| challenge-harder__src__processing__theatre.rs | challenge-harder | Rust analysis: `VERZIK_P1_TRANSITION_TICKS = 13`, `VERZIK_P2_TRANSITION_TICKS = 6`, Bloat room origin, Sotetseg maze pivots |
| web__app__components__attack-timeline__attack-metadata.tsx | web | the attack catalogue the site names |
| web__app__utils__boss-room-state.ts | web | per-room state machine |
| bcf__docs__registry__categories__*.json | bcf | npc attack type / phase type registry |

Guide pages added to `../blert_guides/` (prefix `tob_`): Bloat humid, Nylocas trio and 4s (ranger, mage, melee, melee-freeze), Blert plugins page, guide list. Only bloat/humid, nylocas/* and how-to-hmt exist upstream for ToB; there are no Maiden, Sotetseg, Xarpus or Verzik guide pages in the repo at this commit.
