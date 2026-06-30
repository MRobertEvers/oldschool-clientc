# CS2 VM (pragmatic client-side script engine)

`src2/vm/cs2vm.c` implements a small stack machine used when
`StaticUIBehavior.script_kind == CS2VM_SCRIPT_KIND_CS2`. Existing cache-driven
button scripts remain on CS1 (`csvm` / `ToriAuxLibVM`).

This is **not** a verified reproduction of Jagex's historical CS2 opcode table.
It is sized for local interaction/minimenu condition checks in this client.

## Host state

`struct CS2VM_State` supplies varp/varbit/varc/stat accessors and optional
optimistic varp writes (`set_varp_optimistic`).

## Script format

Scripts are `int` arrays terminated by opcode `0`. Operands follow inline after
multi-byte opcodes.

## Opcode table

| Opcode | Meaning |
|--------|---------|
| `0` | End script; return top-of-stack (or 0) |
| `1` | Push immediate (`script[pc++]`) |
| `2` | Push varp (`script[pc++]`) |
| `3` | Push varbit (`script[pc++]`) |
| `4` | Push varc (`script[pc++]`) |
| `10` | Add (pop b, pop a, push a+b) |
| `11` | Subtract |
| `12` | Multiply |
| `13` | Divide (0 if divisor is 0) |
| `20` | Equal |
| `21` | Not equal |
| `22` | Less than |
| `23` | Greater than |
| `30` | Branch if false (pop cond, jump to `script[pc++]` if zero) |
| `31` | Unconditional jump to `script[pc++]` |
| `40` | Set varp optimistic (pop value, `script[pc++]` = varp id) |
| `50` | Return (pop and return value) |

## Comparators (`cs2vm_compare`)

Used with script comparator/operand fields on `StaticUIBehavior`:

| Comparator | Test |
|------------|------|
| `0` | value > operand |
| `1` | value < operand |
| `2` | value == operand |
| `3` | value != operand |

## Wiring

- `GameRunescape` owns `struct CS2VM* cs2vm` (created in `GameRunescape_New`).
- `UITreeBehaviorHost.cs2vm` + `cs2vm_state` are set via `ui_input_adapter_init_behavior_host`.
- `uitree_behavior_is_active` evaluates CS2 scripts when `script_kind` is CS2.

## Related subsystems

- [UI Click System](ui_click_system.md) — mouse routing (separate from CS2 conditions)
- [UI Minimenu System](ui_minimenu_system.md) — right-click menus
- [UI Interaction State](ui_interaction_state.md) — resolved click targets
- [UI Inventory System](ui_inventory_system.md) — inventory slot interaction
