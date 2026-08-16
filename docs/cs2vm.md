# CS2 VM (RuneStar-aligned clientscript engine)

`src2/vm/cs2vmx.c` implements a full Jagex CS2 bytecode interpreter aligned with
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
cache/ → RSCacheDat2Disk → table 12 → dat2a_clientscript decode → CS2VMX_RunScript
```

Regenerate opcode tables after RuneStar updates:

```bash
python3 tools/cs2_gen_opcodes/gen_opcodes.py
python3 tools/cs2_gen_opcodes/validate_cache.py cache
```

## Architecture (CS2VMX)

| Module | Role |
|--------|------|
| [`cs2_opcode.h`](src2/vm/cs2_opcode.h) | Generated opcode constants |
| [`cs2_opcode_meta.c`](src2/vm/cs2_opcode_meta.c) | Operand kind + VM vs host handler |
| [`cs2_script.c`](src2/vm/cs2_script.c) | Decoded script free helpers |
| [`dat2a_clientscript.c`](src/osrs/rscache/dat2a/dat2a_clientscript.c) | RuneStar `Script.kt` decode |
| [`cs2vmx.c`](src2/vm/cs2vmx.c) | Dual-stack interpreter (`CS2VMX` state) |
| [`cs2vmx.h`](src2/vm/cs2vmx.h) | `CS2VMX`, BindHost / PushCallScript / RunScript |
| [`cs2vmx_host.h`](src2/vm/cs2vmx_host.h) | `CS2VM_HostRequest` + `CS2VMX_HostExec_Fn` |
| [`cs2vm_unity.c`](src2/vm/cs2vm_unity.c) | Unity build — compile once to link all CS2 VM `.c` files |

The old `cs2vm` / `cs2_host_ui` pair is removed. Hosts implement a single
`host_exec(vm, request)` callback and return `CS2VM_EXECNO_OK`, `YIELD`, or `ERROR`.

## Running a script

```c
CS2VMX_BindHost(&vm, user, MyHostExec);
CS2VMX_ResetRuntime(&vm);
CS2VMX_PushCallScript(&vm, script);
/* optional: CS2VMX_SetIntCurrentFrameLocal / CS2VMX_SetActiveAndDotComponentId */
for (;;) {
    int rc = CS2VMX_RunScript(&vm);
    if( rc == CS2VM_EXECNO_DONE || rc == CS2VM_EXECNO_OK ) break;
    if( rc == CS2VM_EXECNO_YIELD ) { /* fulfill pending host work, then re-enter */ }
    /* ERROR */
}
```

On `YIELD`, the opcode checkpoint rolls back stack/frames/pc so `RunScript` can be
re-entered after external host work (script/config/sprite/font/model load, etc.).

## Game hosts

| Host | File |
|------|------|
| `GameRunescape_CS2HostExec` | [`runescape_cs2_host.c`](src2/games/runescape_cs2_host.c) |
| `GameInterfaceEditor_CS2HostExec` | [`game_interface_editor_cs2_host.c`](src2/games/game_interface_editor_cs2_host.c) |

UI hooks enqueue scripts via `UITreeBehaviorHost.cs2_enqueue` (e.g.
`GameRunescape_CS2Enqueue`); a flush step turns the queue into awaitable tasks.

## Unity build

Full-VM consumers should compile [`cs2vm_unity.c`](src2/vm/cs2vm_unity.c) once instead of listing
`cs2_opcode_meta.c`, `cs2_script.c`, `cs2_trigger_args.c`, and `cs2vmx.c` separately.
Add `-I<repo>/src2` and `#include "vm/cs2vmx.h"` (and `cs2vmx_host.h` as needed) for
the public API. Partial consumers that only need decode helpers (e.g. `dump_interface`) should
continue to compile individual files.

### Inventory / dynamic component host opcodes

Hosts that handle inventory scripts implement `CS2VM_HOST_REQUEST_INVS_*`,
`CC_CREATE` / `CC_SETOBJECT` / `IF_FIND`, etc. Typical handlers create or update
**`UIELEM_CC_OBJ`** nodes (`uitree_cc_create`, `uitree_apply_object`) so IF3
equipment/backpack scripts can build item icons from container 93/94.

`GOSUB_WITH_PARAMS` yields `CS2VM_HOST_REQUEST_PUSHSCRIPT`; the host pushes the
callee with `CS2VMX_PushCallScript`. `RETURN` pops the frame and leaves return
ints on the shared stack for the caller.

### Active component (hook source widget)

Widget hook scripts (onLoad, onClick, onVarpTransmit, onInvTransmit, etc.) run with
**both** `active_component_id` and `dot_component_id` set to the handler’s source
widget via `CS2VMX_SetActiveAndDotComponentId`.

## IF3 hook dispatch

| Hook | Trigger |
|------|---------|
| `onLoad` | First frame when UI tree becomes ready |
| `onClick` | Left-click / minimenu IF-button on clickable component |
| `onVarpTransmit` | Varp write when `varp_triggers` matches |
| `onInvTransmit` | Container 93/94 update when `inventory_triggers` matches |

`uitree_behavior_dispatch_inv_transmit` / `uitree_behavior_dispatch_varp_transmit` scan baked
nodes and **enqueue** matching hooks via `host->cs2_enqueue` (they no longer call the VM
inline).

## Wiring

- `GameRunescape.cs2vm` + `GameRunescape.cs2_host`
- `UITreeBehaviorHost.cs2_enqueue` via `ui_input_adapter_init_behavior_host_ex`
- Hook run → enqueue → `Task_Dat2CS2Run` / flush

## Related

- [CS1 VM](cs1vm.md)
- [Equipment IF3 rendering](equipment_if3_rendering.md)
