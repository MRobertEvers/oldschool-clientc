#ifndef CS2DOM_CS2VM_WASM_H
#define CS2DOM_CS2VM_WASM_H

/*
 * Narrow browser ABI over the production C CS2VM2.
 *
 * Pointers are opaque 32-bit handles in an emscripten build.  Request pointers
 * are borrowed and valid only for the synchronous cs2HostExec callback that
 * received them.  Every string returned by an accessor has the same lifetime.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum CS2W_Dialect
{
    CS2W_DIALECT_CANONICAL = 0,
    CS2W_DIALECT_RS2_DAT2 = 1,
};

enum CS2W_Error
{
    CS2W_ERROR_NONE = 0,
    CS2W_ERROR_ARGUMENT = 1,
    CS2W_ERROR_BAD_STATE = 2,
    CS2W_ERROR_MISSING_SCRIPT = 3,
    CS2W_ERROR_DUPLICATE_SCRIPT = 4,
    CS2W_ERROR_DECODE = 5,
    CS2W_ERROR_UNSUPPORTED_LONG = 6,
    CS2W_ERROR_OUT_OF_MEMORY = 7,
    CS2W_ERROR_VM = 8,
};

enum CS2W_RunStatus
{
    CS2W_RUN_DONE = 0,
    CS2W_RUN_YIELDED = 1,
    CS2W_RUN_ERROR = 2,
};

enum CS2W_EventField
{
    CS2W_EVENT_MOUSE_X = 0,
    CS2W_EVENT_MOUSE_Y = 1,
    CS2W_EVENT_COMPONENT_ID = 2,
    CS2W_EVENT_COMPONENT_SUB_ID = 3,
    CS2W_EVENT_OP_INDEX = 4,
    CS2W_EVENT_DRAG_TARGET_ID = 5,
    CS2W_EVENT_DRAG_TARGET_SUB_ID = 6,
    CS2W_EVENT_KEY_TYPED = 7,
    CS2W_EVENT_KEY_PRESSED = 8,
    CS2W_EVENT_OP_SUBINDEX = 9,
    CS2W_EVENT_WINDOW_MODE = 10,
    CS2W_EVENT_DEFAULT_WINDOW_MODE = 11,
};

enum CS2W_EventStringField
{
    CS2W_EVENT_STRING_OPBASE = 0,
};

enum CS2W_FieldKind
{
    CS2W_FIELD_INVALID = 0,
    CS2W_FIELD_I32 = 1,
    CS2W_FIELD_BOOL = 2,
    CS2W_FIELD_U8 = 3,
    CS2W_FIELD_STRING = 4,
    CS2W_FIELD_I32_ARRAY = 5,
    CS2W_FIELD_I32_POINTER = 6,
    /* Read element 0 for the low word and element 1 for the high word. */
    CS2W_FIELD_U64 = 7,
    CS2W_FIELD_STRING_ARRAY = 8,
};

struct CS2W_Session;
struct CS2W_Invocation;

#define CS2W_ABI_VERSION 0x00010002u

uint32_t
cs2w_abi_version(void);

/* A session owns decoded immutable scripts, not a VM thread. */
struct CS2W_Session*
cs2w_session_create(int dialect, int revision);

/* Returns 1. Returns 0 and leaves the session alive while invocations exist. */
int
cs2w_session_destroy(struct CS2W_Session* session);

/* Decode and copy one raw cache clientscript archive (`.cs2b`). */
int
cs2w_session_load_script(
    struct CS2W_Session* session,
    int script_id,
    const uint8_t* data,
    int data_size);

/* Prevent later script mutation. Nested invocation is then unconditionally safe. */
int
cs2w_session_seal(struct CS2W_Session* session);

/* Bind/preload one immutable HostData namespace before sealing the session.
 * The compact preload records are copied synchronously by the module. */
int
cs2w_session_set_fast_scalar_namespace(
    struct CS2W_Session* session,
    int namespace_id);

int
cs2w_session_preload_fast_scalars(
    struct CS2W_Session* session,
    const int32_t* records,
    int record_count,
    const uint8_t* arena,
    int arena_size);

int
cs2w_session_script_count(const struct CS2W_Session* session);

int
cs2w_session_last_error(const struct CS2W_Session* session);

const char*
cs2w_session_last_error_message(const struct CS2W_Session* session);

/*
 * One invocation owns one CS2VM2_Acquire block.  Consequently a synchronous
 * JavaScript HOST request may start another invocation on this same session.
 */
struct CS2W_Invocation*
cs2w_invocation_create(
    struct CS2W_Session* session,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int canvas_width,
    int canvas_height);

int
cs2w_invocation_destroy(struct CS2W_Invocation* invocation);

/* Enable the browser-only compact HOST transaction path before first run.
 * Unsupported/native builds reject enabled=1 and retain generic callbacks. */
int
cs2w_invocation_set_fast_host(struct CS2W_Invocation* invocation, int enabled);

/* Bounded invocation-local mirror of the React DB query iterator. A failed
 * import leaves it invalid and DB_FINDNEXT retains the generic HOST path. */
int
cs2w_invocation_set_fast_db_iterator(
    struct CS2W_Invocation* invocation,
    const int32_t* rows,
    int count,
    int cursor,
    int revision);

int
cs2w_invocation_clear_fast_db_iterator(struct CS2W_Invocation* invocation);

int
cs2w_invocation_fast_db_iterator_dirty(const struct CS2W_Invocation* invocation);

int
cs2w_invocation_fast_db_iterator_cursor(const struct CS2W_Invocation* invocation);

int
cs2w_invocation_fast_db_iterator_revision(const struct CS2W_Invocation* invocation);

int
cs2w_invocation_mark_fast_db_iterator_clean(struct CS2W_Invocation* invocation);

/* Mixed ScriptEvent arguments fill their respective local banks in call order. */
int
cs2w_invocation_add_int_arg(struct CS2W_Invocation* invocation, int value);

/* The bridge copies value immediately. NULL is the empty string. */
int
cs2w_invocation_add_string_arg(struct CS2W_Invocation* invocation, const char* value);

int
cs2w_invocation_set_event_i32(
    struct CS2W_Invocation* invocation,
    int field,
    int value);

int
cs2w_invocation_set_event_string(
    struct CS2W_Invocation* invocation,
    int field,
    const char* value);

/* First call starts the script; a call after YIELDED resumes it. */
int
cs2w_invocation_run(struct CS2W_Invocation* invocation);

int
cs2w_invocation_last_error(const struct CS2W_Invocation* invocation);

int
cs2w_invocation_error_opcode(const struct CS2W_Invocation* invocation);

int
cs2w_invocation_error_pc(const struct CS2W_Invocation* invocation);

int
cs2w_invocation_error_script_id(const struct CS2W_Invocation* invocation);

int
cs2w_invocation_host_call_count(const struct CS2W_Invocation* invocation);

/* Diagnostic counts for the bounded session-local immutable scalar L1. */
int
cs2w_invocation_fast_scalar_l1_hits(const struct CS2W_Invocation* invocation);

int
cs2w_invocation_fast_scalar_l1_misses(const struct CS2W_Invocation* invocation);

/* ---- Borrowed HOST request reflection ---------------------------------- */

const char*
cs2w_request_kind_name(int kind);

int
cs2w_request_field_count(int kind);

const char*
cs2w_request_field_name(int kind, int field_index);

int
cs2w_request_field_kind(int kind, int field_index);

/* Immutable C layout metadata. Browser hosts may cache these values and read
 * the borrowed request directly from the current Emscripten heap. Offsets are
 * relative to CS2VM_HostRequest; -1 means an invalid field/no count member.
 * The legacy value accessors below remain the portable fallback. */
int
cs2w_request_field_offset(int kind, int field_index);

int
cs2w_request_field_capacity(int kind, int field_index);

int
cs2w_request_field_stride(int kind, int field_index);

int
cs2w_request_field_count_offset(int kind, int field_index);

/* Direct heap reflection is valid only for the wasm32 ABI. */
int
cs2w_request_pointer_size(void);

/* Semantic length: arg_count/pair_count/etc., not just fixed C capacity. */
int
cs2w_request_field_length(uintptr_t request, int field_index);

int
cs2w_request_field_i32(uintptr_t request, int field_index, int element_index);

const char*
cs2w_request_field_string(uintptr_t request, int field_index, int element_index);

/* ---- Results written synchronously by the JavaScript HOST ------------- */

/* Pop one value while servicing a synchronous HOST request. Output pointers
 * are required and are written only on success. A popped string remains owned
 * by the thread pool and is borrowed for the duration of the callback. */
int
cs2w_thread_pop_int(uintptr_t thread, int* value_out);

int
cs2w_thread_pop_string(uintptr_t thread, const char** value_out);

int
cs2w_thread_push_int(uintptr_t thread, int value);

/* Copies UTF-8 into the VM thread's string pool before returning. */
int
cs2w_thread_push_string(uintptr_t thread, const char* value);

int
cs2w_thread_set_target(uintptr_t thread, int dot_operand, int component_id);

int
cs2w_thread_set_active_dot(uintptr_t thread, int active_component_id, int dot_component_id);

/* Populate the VM-owned iterator consumed by CHILDREN_FINDNEXT/ARRAY.  The
 * HOST handlers for 211/212 push their own count after the callback, so JS
 * must call this rather than push a count result. */
int
cs2w_thread_set_children(
    uintptr_t thread,
    int parent_component_id,
    const int* child_sub_ids,
    int child_count);

int
cs2w_thread_active_component(uintptr_t thread);

int
cs2w_thread_dot_component(uintptr_t thread);

/* Raw operand of the opcode currently inside the HOST callback.  For CC
 * opcodes, 1 selects dot and 0 selects active.  This is intentionally separate
 * from component_id: distinct dynamic children may share a packed parent id. */
int
cs2w_thread_current_operand(uintptr_t thread);

/* JavaScript callback return values are the existing CS2VM_EXECNO_* values. */
#define CS2W_HOST_OK 0
#define CS2W_HOST_ERROR -1
#define CS2W_HOST_YIELD -2

#ifndef __EMSCRIPTEN__
typedef int (*CS2W_NativeHostExec)(
    uintptr_t session,
    uintptr_t invocation,
    uintptr_t thread,
    uintptr_t request,
    int kind);

/* Test/native harness seam; browser builds import Module.cs2HostExec instead. */
void
cs2w_set_native_host_exec(CS2W_NativeHostExec exec);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CS2DOM_CS2VM_WASM_H */
