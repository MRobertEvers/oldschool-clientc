# Live per-room orchestrators (this run)

**BRANCH FREEZE (2026-10-07):** Do not change branches. Stay on your own
`cursor/cox-<room>-*-da39` for the entire lane. No `git checkout` / `switch`
to parent, `v3`, or a sibling room. Parent will not hop checkouts either.

Do not resume unless the parent asks. Parent commits green-lane markers.

**STOP:** Do not `git checkout -f`, reset, stash-drop, or overwrite sibling
`test/raids/cox_*.lua` / `OSRS-Content` trees. Edit only your ownership row
in `ROOM_AGENT.md`. See `cox-room-agent-no-clobber`.

| Room | Agent ID | Branch / notes |
|---|---|---|
| tekton | bc-6e8e01ee-2dfa-5089-8328-6b72a1d1b15a | own `cursor/cox-tekton-*-da39` only |
| guardians | bc-36e57d6c-c0a4-54ad-9976-d2699a5311c3 | own branch only |
| vespula | bc-52439575-cf77-5bc3-b753-7c5151722e31 | own branch only |
| icedemon | bc-28036115-f582-5175-aaf5-64487f2d13ab | **green** `cursor/cox-icedemon-sm-da39` PR #138 — hold |
| tightrope | bc-0e60f709-4b14-5912-b2ee-beb76784676d | **green** `cursor/cox-tightrope-traversal-da39` PR #137 — hold |
| crabs | bc-51ffcdb3-8125-50a6-9573-1c025b79eb35 | own branch only |
| thieving | bc-311e5e87-f634-5584-8481-aa63d95c7978 | **green** `cursor/cox-thieving-status-7978` PR #140 — hold |
| resource | bc-cb753ee5-02cd-5e19-bdeb-2ffb116453db | own branch only |
| shamans | bc-64efd667-9a32-5a76-a197-0f98c1b1c645 | own branch only |
| mystics | bc-2922cae1-cc03-537b-b38f-2397016f48d2 | own branch only |
| vasa | bc-47112672-81a0-5c55-8de1-ed8cc4958456 | own branch only |
| vanguards | bc-6731c199-2956-52b7-9de9-49829dbad74f | own branch only |
| muttadiles | bc-68d32325-2635-566e-aeea-2eb464a6772b | own branch only |
| scavenger_small | bc-96c722d5-2e31-5f0a-87cc-8fc0811baac7 | **green** `cursor/cox-scavenger-small-da39` PR #139 — hold |
| olm | bc-4ea82b1b-8fc5-506b-974b-d4894e7aa9fc | own branch only; do not edit parent olm harness if parent owns it |
