# CS2VM2 browser bridge

This directory is the browser boundary around the existing production C
`src/cs2vm2` interpreter. It does not contain another interpreter and it does
not own a UI tree. C executes bytecode; JavaScript implements HOST requests and
mutates the React-side tree.

## Build and test

```sh
make -C tools/cs2dom test-wasm-core
make -C tools/cs2dom wasm
```

The second command produces `tools/cs2dom/web/cs2vm_wasm.js` and
`cs2vm_wasm.wasm`. The JavaScript is an ES-module default-export factory:

```js
import createCS2VMModule from '/cs2vm-wasm/cs2vm_wasm.js';

const module = await createCS2VMModule({
  locateFile: name => `/cs2vm-wasm/${name}`,
  cs2HostExec(session, invocation, thread, request, kind) {
    // Decode request synchronously, call the JavaScript HOST, push its result.
    return 0; // CS2W_HOST_OK; -1 is ERROR and -2 is YIELD.
  },
});
```

The complete exported contract and numeric constants are in
[`cs2vm_wasm.h`](cs2vm_wasm.h). `cs2w_abi_version()` currently returns
`0x00010002`.

## Lifetime and invocation model

1. `cs2w_session_create(dialect, revision)` creates a script registry.
2. Copy each raw `.cs2b` record into wasm memory and call
   `cs2w_session_load_script(session, id, pointer, length)`. Decoding and opcode
   dialect translation both happen in C.
3. Call `cs2w_session_seal()` after loading.
4. Create an invocation, add its typed ScriptEvent arguments, set event locals,
   call `cs2w_invocation_run()`, then destroy it in `finally`.
5. Destroy the session after all invocations are gone.

Each invocation owns a distinct `CS2VM2_Acquire()` block. A synchronous HOST
request can therefore dispatch a nested hook on the same session without
clobbering the outer script. The sorted script registry is shared and immutable;
`GOSUB_WITH_PARAMS` resolves there in C and is not sent to JavaScript.

Integer and string arguments fill their separate C local banks in call order,
matching `ScriptEvent`. The bridge substitutes all `CS2VM_SCRIPT_ARG_*`
sentinels from the invocation's event fields. The literal string
`event_opbase` is substituted from `CS2W_EVENT_STRING_OPBASE`.

## HOST callback

The request pointer is borrowed and only valid during `cs2HostExec`. Decode it
with:

- `cs2w_request_kind_name(kind)`
- `cs2w_request_field_count(kind)`
- `cs2w_request_field_name(kind, index)`
- `cs2w_request_field_kind(kind, index)`
- `cs2w_request_field_offset(kind, index)`
- `cs2w_request_field_capacity(kind, index)`
- `cs2w_request_field_stride(kind, index)`
- `cs2w_request_field_count_offset(kind, index)`
- `cs2w_request_pointer_size()`
- `cs2w_request_field_length(request, index)`
- `cs2w_request_field_i32(request, index, element)`
- `cs2w_request_field_string(request, index, element)`

The generated schema covers every row in
`cs2vm2_host_request_kinds.def`. Field names remain the C snake-case names.
Array lengths are semantic: `arg_count`, `int_arg_count`, `pair_count`,
`trigger_count`, or `str_arg_count`, rather than the backing C capacity. A U64
field is returned as low and high 32-bit words at elements zero and one.

JavaScript answers through `cs2w_thread_push_int()` and
`cs2w_thread_push_string()`; the latter copies into the C VM string pool.
Component-finding handlers additionally use `cs2w_thread_set_target()`.
`cs2w_thread_current_operand()` reports whether a CC opcode selected active
(`0`) or dot (`1`). Dynamic children should use unique runtime component IDs in
the C registers; `event_com` and `event_comsubid` remain separate event fields.

Three child-iteration requests are special. For `IF_CHILDREN_FIND`,
`IF_CHILDREN_COLLECT`, and `CC_CHILDREN_FIND_COUNT`, copy the matching sub-IDs
to wasm memory and call:

```c
cs2w_thread_set_children(thread, parent_id, sub_ids, count);
```

Do not also push the count for opcodes 211/212: their existing C handlers push
`children_iter_count` after HOST returns. The C VM then owns find-next and the
children-array handle.

Typed result selection remains HOST policy. For example, JavaScript uses
`ENUM.output_type` and component-param metadata to choose push-int versus
push-string; the bridge deliberately does not duplicate that game-state logic.

Browser hosts may opt an invocation into the compact transaction ABI with
`cs2w_invocation_set_fast_host(invocation, 1)`. The module then calls the
JavaScript-owned `cs2FastHostQuery` for inventory/child snapshots and
`cs2FastHostFlush` for ordered mutation records. A request outside the proven
fast vocabulary first commits the transaction and invalidates its snapshots;
the generic `cs2HostExec` path remains the fallback. Native builds reject the
opt-in so portable bridge tests always exercise the generic ABI.

## Generated schema

`gen_host_schema.py` parses the authoritative request manifest and emits
`cs2vm_host_schema.gen.h`. Offsets are expressed with C `offsetof`, so the JS
side never assumes struct padding, pointer size, enum representation, or bool
layout. The generated file is committed and regenerated automatically by the
make target when the manifest changes.
