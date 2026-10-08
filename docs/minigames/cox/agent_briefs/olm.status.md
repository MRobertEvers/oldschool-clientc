# Olm room status

Branch: `cursor/cox-olm-solo-4t41-a9fc` (base `cursor/cox-raid-rooms-da39`)

## Gate

- `cox_olm_solo_4t41`: **green** — ledger pass=15 fail=0; `sm.done` head_dead=true;
  `spec.olm.phases_solo`=4; coverage FULL (8 in-scope rows)
- Proof (both trees): `cox_olm_solo_4t41_{reprove_green.log,ledger_green.tsv,idle,mid,clear}.png`
  + `cox_olm_solo_4t41_pr_body.md` under `/opt/cursor/artifacts/` and
  `/cursor/stores/parent/artifacts/`

## Strategy (owner 2026-10-07)

1. **Solo Melee 4-tick 4:1** first — done green
2. Then **duo**, then **trio** — each its own harness
3. Explicit state machine (not scythe spam)
4. Prayer flick on `%varp6766_cox_olm_style` (magic/ranged); no godmode

## Implemented

- Harness + content as prior; supplies use cache `4dose2restore` /
  `4dosepotionofsaradomin` (not `br_*`) so setup `::give` lands
- Head: size=5, carved-head LoS clear, aisle Attack path

## Next

- Duo / trio harnesses
