# CS1 VM (interface component scripts)

`src2/vm/cs1vm.c` evaluates **CS1** scripts embedded in dat1 interface components
(`cs1Scripts`). These drive button active state, toggle/select behavior, and
visibility comparators. CS2 dat2 clientscripts use [`cs2vm`](cs2vm.md).

Reference implementation: `Client-TS/src/client/Client.ts` `getIfVar`.

## Execution model

Accumulator-based (not stack-based). Each value-producing opcode contributes to
an accumulator `acc`. Opcodes 15–17 set arithmetic mode for the *next* value:

| Opcode | Arithmetic for next value |
|--------|---------------------------|
| default / 0 | add (`acc += value`) |
| 15 | subtract |
| 16 | divide (`acc /= value`, skip if divisor 0) |
| 17 | multiply |

Script terminates at opcode `0` (end); `acc` is the result.

## Opcode table

Constants in `src2/vm/cs1vm_opcode.h`.

| Opcode | Name | Operands | Callback / behavior |
|--------|------|----------|---------------------|
| `0` | end | — | return accumulator |
| `1` | stat_level | skill | `get_stat_level` |
| `2` | stat_base_level | skill | `get_stat_base_level` |
| `3` | stat_xp | skill | `get_stat_xp` |
| `4` | inv_count | iface, obj | `get_inv_count` (obj operand +1) |
| `5` | pushvar | varp | `get_varp` |
| `6` | stat_xp_remaining | skill | `get_stat_xp_remaining` |
| `7` | varp_scale | varp | `(varp * 100) / 46875` |
| `8` | combat_level | — | `get_combat_level` |
| `9` | total_level | — | `get_total_level` |
| `10` | inv_contains | iface, obj | `inv_contains` (obj +1; 999999999 if present) |
| `11` | runenergy | — | `get_runenergy` |
| `12` | runweight | — | `get_runweight` |
| `13` | testbit | varp, bit | bit test on varp |
| `14` | push_varbit | varbit | `get_varbit` |
| `15` | subtract | — | set subtract mode |
| `16` | divide | — | set divide mode |
| `17` | multiply | — | set multiply mode |
| `18` | coordx | — | `get_coord_x` |
| `19` | coordz | — | `get_coord_z` |
| `20` | push_constant | value | immediate |

Any callback may be NULL → treated as 0.

## Script kind (`CS1VM_SCRIPT_KIND_*`)

Inline dat1 behavior scripts are classified as CS1 or CS2-kind for cache tagging:

| Constant | Meaning |
|----------|---------|
| `CS1VM_SCRIPT_KIND_CS1` | Evaluated by `cs1vm_eval` / `cs1vm_eval_len` |
| `CS1VM_SCRIPT_KIND_CS2` | Tagged in cache but **not evaluated** (inactive) |

## Comparators (`cs1vm_compare`)

| Comparator | Test |
|------------|------|
| `0` (default) | value == operand |
| `2` | value < operand |
| `3` | value > operand |
| `4` | value != operand |

## Host wiring

`struct CS1Host` is filled by:

- `cs1vm_host_fill_varp_varbit()` — varp/varbit via `ToriAuxLibVM` (`cs1vm_host.c`)
- `rs_ui_host_fill_cs1host()` — full callbacks in `runescape.c` (inventory via `UIInvDataService`)
- `UITreeBehaviorHost.cs1host` — set in `ui_input_adapter_init_behavior_host_ex`

`ToriAuxLibVM_IsActive` accepts optional `CS1Host*`; when NULL, uses varp/varbit only.

## API

| Function | Purpose |
|----------|---------|
| `cs1vm_new` / `cs1vm_free` | VM instance |
| `cs1vm_eval` | Opcode-0-terminated scripts |
| `cs1vm_eval_len` | Bounded scripts (`scripts_lengths` from cache) |
| `cs1vm_script_length` | Walk script to find length |
| `cs1vm_compare` | Comparator helper |

## Related

- [CS2 VM](cs2vm.md) — dat2 clientscript interpreter
- [Equipment IF3 rendering](equipment_if3_rendering.md) — CS1/CS2 scope boundary
