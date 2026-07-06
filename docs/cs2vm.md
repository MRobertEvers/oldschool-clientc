# CS2 VM (RuneStar-aligned clientscript engine)

`src2/vm/cs2vm.c` implements a full Jagex CS2 bytecode interpreter aligned with
[RuneStar/cs2](https://github.com/RuneStar/cs2). Opcode IDs and decode format match
`Opcodes.kt` and `Script.kt` from that project (766 opcodes generated into
`src2/vm/cs2_opcode.h`).

CS1 interface scripts (`cs1Scripts`) remain on [`cs1vm`](cs1vm.md).

## Cache requirement

Clientscripts load from the repo-root **`cache/`** folder:

| File | Role |
|------|------|
| `cache/main_file_cache.dat2` | Primary dat2 archive |
| `cache/main_file_cache.idx12` | Clientscript table (id 12) |

Resolved via `cache_path_resolve_osrs_repo()` in
[`src2/platforms/platform_x/cache_path_resolve.c`](src2/platforms/platform_x/cache_path_resolve.c).

```
cache/ → RSCacheDat2Disk → table 12 → dat2a_clientscript decode → cs2vm_run
```

Regenerate opcode tables after RuneStar updates:

```bash
python3 tools/cs2_gen_opcodes/gen_opcodes.py
python3 tools/cs2_gen_opcodes/validate_cache.py cache
```

## Architecture

| Module | Role |
|--------|------|
| [`cs2_opcode.h`](src2/vm/cs2_opcode.h) | Generated opcode constants |
| [`cs2_opcode_meta.c`](src2/vm/cs2_opcode_meta.c) | Operand kind + VM vs host handler |
| [`cs2_script.c`](src2/vm/cs2_script.c) | Decoded script free helpers |
| [`dat2a_clientscript.c`](src/osrs/rscache/dat2a/dat2a_clientscript.c) | RuneStar `Script.kt` decode |
| [`cs2vm.c`](src2/vm/cs2vm.c) | Dual-stack interpreter (`CS2VM` state) |
| [`cs2vm.h`](src2/vm/cs2vm.h) | `CS2VM`, `CS2Host`, invoke helpers |
| [`cs2vm_unity.c`](src2/vm/cs2vm_unity.c) | Unity build — compile once to link all CS2 VM `.c` files |
| [`cs2_host_ui.c`](src2/vm/cs2_host_ui.c) | Concrete `CS2Host` wiring + `CC_*` / `IF_*` invoke handlers (mutate `UITree`) |

## Host callbacks (`CS2Host`)

All external data and UI effects go through `CS2Host`:

- `get_varp`, `get_varbit`, `get_varc_*`, `set_varp`, `set_varbit`, `set_varc_*`
- `resolve_script` — loads table-12 scripts from core/cache (`ToriAuxLibCache_ClientScriptResolve`)
- `invoke` — dispatches opcodes classified as `CS2_HANDLER_HOST` (interface/config/world ops)

`cs2_host_ui_init` takes a `CS2HostUIInitArgs` with `core`, `cache`, `vm`, `tree`, and an
optional `on_varp_change` callback used to queue `onVarpTransmit` dispatch.

VM-native opcodes (branches, locals, arithmetic, arrays, gosub, return) run inside `cs2vm.c`.

## Unity build

Full-VM consumers should compile [`cs2vm_unity.c`](src2/vm/cs2vm_unity.c) once instead of listing
`cs2_opcode_meta.c`, `cs2_script.c`, `cs2_trigger_args.c`, `cs2vm.c`, and `cs2_host_ui.c`
separately. Add `-I<repo>/src2` and `#include "vm/cs2vm.h"` (and `cs2_host_ui.h` as needed) for
the public API. Partial consumers that only need decode helpers (e.g. `dump_interface`) should
continue to compile individual files.

Host invoke helpers (`cs2vm_host_pop_int`, `cs2vm_host_push_int`, etc.) manipulate the
active `CS2VM` stack from within `invoke` handlers.

### Inventory / dynamic component host opcodes (`cs2_host_ui.c`)

`CS2HostUIInitArgs` supplies game callbacks used by inventory scripts:

| Callback | CS2 opcodes |
|----------|-------------|
| `inv_get_obj` / `inv_get_num` / `inv_size` | `INV_GETOBJ`, `INV_GETNUM`, `INV_SIZE` |
| `resolve_obj_icon` | `CC_SETOBJECT*`, `IF_SETOBJECT*` |
| `tree` + `active_node_index` | `CC_CREATE`, `CC_DELETEALL`, `CC_FIND`, `IF_FIND` |

Implemented handlers create or update **`UIELEM_CC_OBJ`** nodes (`uitree_cc_create`,
`uitree_apply_object`) so IF3 equipment/backpack scripts can build item icons from container
93/94 without baking `UIELEM_INV_SLOT` grids in C.

`CC_SETONINVTRANSMIT` / `IF_SETONINVTRANSMIT` pop their string operand (stack balance only);
refresh is driven by the static cache `on_inv_transmit` hook on the parent component.

`GOSUB_WITH_PARAMS` saves `return_pc` on the **caller** frame; `RETURN` must resume the
caller at `caller->return_pc` (not the callee's zeroed frame) and push the return int back
onto the caller stack. Unresolved gosub targets log a warning and are skipped (no assert).

`cs2vm_run` resets `active_component` to `-1` at start; scripts must use `IF_FIND` /
`CC_FIND` or receive the component via hook argv before `CC_*` ops on a specific widget.

## IF3 hook dispatch

| Hook | Trigger |
|------|---------|
| `onLoad` | First frame when UI tree becomes ready |
| `onClick` | Left-click / minimenu IF-button on clickable component |
| `onVarpTransmit` | Varp write (button toggle, CS2 `POP_VAR`, etc.) when `varp_triggers` matches |
| `onInvTransmit` | Container 93/94 update (`inv=` seed + simulated transmit after tree load, live `GameRunescape_DispatchInvTransmit`) when `inventory_triggers` matches |

After revconfig bake, `instance_revconfig_inv_setup_after_build` seeds pool containers,
runs `GameRunescape_RunOnLoadHooks`, then **`GameRunescape_DispatchInvTransmit` per `inv=` source**
so CS2 scripts populate `UIELEM_CC_OBJ` icons before the first frame draws.

`uitree_behavior_dispatch_inv_transmit` / `uitree_behavior_dispatch_varp_transmit` scan baked
nodes and run matching hooks via `cs2vm_run`. `onTimer` is not converted from cache yet.

## Wiring

- `GameRunescape.cs2vm` + `GameRunescape.cs2host`
- `UITreeBehaviorHost.cs2vm` / `cs2host` via `ui_input_adapter_init_behavior_host_ex`
- `uitree_behavior_run_hook` → `cs2vm_run` on decoded `ToriAuxLibCore_ClientScript.script`

## Related

- [CS1 VM](cs1vm.md)
- [Equipment IF3 rendering](equipment_if3_rendering.md)
