#include "cs2vm_wasm.h"

#include "cs2vm2/cs2vm2.h"
#include "engine/cs2_opcode_dialect.h"
#include "engine/cs2vm2_script_from_rscache.h"

#include <rscache.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define CS2W_EXPORT EMSCRIPTEN_KEEPALIVE

EM_JS(
    int,
    cs2w_js_host_exec,
    (uintptr_t session, uintptr_t invocation, uintptr_t thread, uintptr_t request, int kind),
    {
        const exec = Module['cs2HostExec'];
        if( typeof exec !== 'function' )
        {
            Module['cs2LastHostError'] = new Error('CS2VM wasm requires Module.cs2HostExec');
            return -1;
        }
        try
        {
            return exec(session, invocation, thread, request, kind) | 0;
        }
        catch( error )
        {
            Module['cs2LastHostError'] = error;
            if( typeof Module['onCs2HostError'] === 'function' )
                Module['onCs2HostError'](error);
            return -1;
        }
    });

/* The generic callback above is deliberately retained for every request.  A
 * browser HostRuntime may additionally opt one invocation into the compact
 * transaction ABI below.  JavaScript still owns every read and mutation: C
 * only caches immutable snapshots returned by JS and queues ordered request
 * records until the next observation barrier. */
EM_JS(
    int,
    cs2w_js_fast_host_query,
    (uintptr_t session, uintptr_t invocation, int query_kind, int key,
     uintptr_t output, int capacity),
    {
        const query = Module['cs2FastHostQuery'];
        if( typeof query !== 'function' ) return -1;
        try
        {
            return query(session, invocation, query_kind, key, output, capacity) | 0;
        }
        catch( error )
        {
            Module['cs2LastHostError'] = error;
            if( typeof Module['onCs2HostError'] === 'function' )
                Module['onCs2HostError'](error);
            return -1;
        }
    });

/* Scalar content/layout reads still belong to JavaScript, but reflecting a
 * complete request object for every immutable enum/struct lookup is needless
 * work. Result 1 is an i32, 2 is UTF-8 with its byte length written separately,
 * and 0 asks C to preserve the generic path for an unsupported shape. */
EM_JS(
    int,
    cs2w_js_fast_host_scalar_query,
    (uintptr_t session, uintptr_t invocation, int request_kind,
     int a, int b, int c, uintptr_t int_output,
     uintptr_t string_output, int string_capacity, uintptr_t string_length_output,
     uintptr_t cacheable_output),
    {
        const query = Module['cs2FastHostScalarQuery'];
        if( typeof query !== 'function' ) return -1;
        try
        {
            return query(session, invocation, request_kind, a, b, c, int_output,
                         string_output, string_capacity, string_length_output,
                         cacheable_output) | 0;
        }
        catch( error )
        {
            Module['cs2LastHostError'] = error;
            if( typeof Module['onCs2HostError'] === 'function' )
                Module['onCs2HostError'](error);
            return -1;
        }
    });

EM_JS(
    int,
    cs2w_js_fast_host_flush,
    (uintptr_t session, uintptr_t invocation, uintptr_t records, int record_count,
     uintptr_t arena, int arena_size),
    {
        const flush = Module['cs2FastHostFlush'];
        if( typeof flush !== 'function' ) return -1;
        try
        {
            return flush(session, invocation, records, record_count, arena, arena_size) | 0;
        }
        catch( error )
        {
            Module['cs2LastHostError'] = error;
            if( typeof Module['onCs2HostError'] === 'function' )
                Module['onCs2HostError'](error);
            return -1;
        }
    });
#else
#define CS2W_EXPORT
static CS2W_NativeHostExec g_native_host_exec = NULL;

void
cs2w_set_native_host_exec(CS2W_NativeHostExec exec)
{
    g_native_host_exec = exec;
}
#endif

#define CS2W_SESSION_MAGIC 0x43533257u /* CS2W */
#define CS2W_INVOCATION_MAGIC 0x43534957u /* CSIW */
#define CS2W_MAX_HOOK_ARGS 64
#define CS2W_MAX_DYNAMIC_FIELD 4096
#define CS2W_ERROR_MESSAGE_LEN 192
#define CS2W_NO_OFFSET ((size_t)-1)
#define CS2W_FAST_RECORD_WORDS 12
#define CS2W_FAST_MAX_RECORDS 65536
#define CS2W_FAST_INITIAL_RECORDS 1024
#define CS2W_FAST_INITIAL_ARENA 16384
#define CS2W_FAST_INITIAL_SNAPSHOT_ENTRIES 2048
#define CS2W_FAST_QUERY_INVENTORY 1
#define CS2W_FAST_QUERY_CHILDREN 2
#define CS2W_FAST_QUERY_VAR 3
#define CS2W_FAST_QUERY_VARBIT 4
#define CS2W_FAST_QUERY_VARC_INT 5
#define CS2W_FAST_QUERY_CLIENTCLOCK 6
#define CS2W_FAST_QUERY_MISSING (-2)

struct CS2W_FastRecord
{
    int32_t words[CS2W_FAST_RECORD_WORDS];
};

struct CS2W_FastInventoryEntry
{
    int slot;
    int object_id;
    int count;
};

struct CS2W_FastInventory
{
    int id;
    struct CS2W_FastInventoryEntry* entries;
    int count;
};

struct CS2W_FastChildEntry
{
    int sub_id;
    int component_id;
};

struct CS2W_FastChildren
{
    int parent_id;
    int parent_exists;
    struct CS2W_FastChildEntry* entries;
    int count;
};

struct CS2W_FastValue
{
    int kind;
    int key;
    int value;
};

/* Immutable cache-data reads are shared by every hook in one WASM session.
 * A large generated interface can ask for the same enum/struct tuple thousands
 * of times while rebuilding dynamic rows. Keeping those exact scalar answers
 * here avoids a C -> JavaScript round trip after the first read without moving
 * ownership of the backing cache data out of HostRuntime. */
struct CS2W_FastScalarCacheEntry
{
    int occupied;
    int request_kind;
    int a;
    int b;
    int c;
    int result_kind;
    int int_value;
    char* string_value;
    int string_length;
};

struct CS2W_FieldDescriptor
{
    const char* name;
    int kind;
    size_t offset;
    int capacity;
    int stride;
    size_t count_offset;
};

struct CS2W_RequestDescriptor
{
    int kind;
    const char* name;
    const struct CS2W_FieldDescriptor* fields;
    int field_count;
};

/* Generated from the same .def that declares CS2VM_HostRequest. */
#include "cs2vm_host_schema.gen.h"

struct CS2W_ScriptEntry
{
    int id;
    struct CS2VM2_Script script;
};

struct CS2W_Session
{
    uint32_t magic;
    int dialect;
    int revision;
    int sealed;
    int invocation_count;
    struct CS2W_ScriptEntry** scripts;
    int script_count;
    int script_capacity;
    int last_error;
    char last_error_message[CS2W_ERROR_MESSAGE_LEN];
    struct CS2W_FastScalarCacheEntry* fast_scalar_cache;
    int fast_scalar_cache_count;
    int fast_scalar_cache_capacity;
};

struct CS2W_EventValues
{
    int mouse_x;
    int mouse_y;
    int component_id;
    int component_sub_id;
    int op_index;
    int drag_target_id;
    int drag_target_sub_id;
    int key_typed;
    int key_pressed;
    int op_subindex;
    int window_mode;
    int default_window_mode;
    char* opbase;
};

struct CS2W_Invocation
{
    uint32_t magic;
    struct CS2W_Session* session;
    struct CS2W_ScriptEntry* entry;
    struct CS2VM2* vm;
    struct CS2VM2_Thread* thread;
    int active_component_id;
    int dot_component_id;
    int canvas_width;
    int canvas_height;
    int ints[CS2W_MAX_HOOK_ARGS];
    char* strings[CS2W_MAX_HOOK_ARGS];
    int int_count;
    int string_count;
    struct CS2W_EventValues event;
    int started;
    int terminal;
    int status;
    int host_call_count;
    int last_error;
    int error_opcode;
    int error_pc;
    int error_script_id;
    int fast_host_enabled;
    struct CS2W_FastRecord* fast_records;
    int fast_record_count;
    int fast_record_capacity;
    uint8_t* fast_arena;
    int fast_arena_size;
    int fast_arena_capacity;
    int fast_create_serial;
    int fast_active_token;
    int fast_dot_token;
    struct CS2W_FastInventory* fast_inventories;
    int fast_inventory_count;
    int fast_inventory_capacity;
    struct CS2W_FastChildren* fast_children;
    int fast_children_count;
    int fast_children_capacity;
    struct CS2W_FastValue* fast_values;
    int fast_value_count;
    int fast_value_capacity;
    char* fast_scalar_string;
    int fast_scalar_string_capacity;
};

static char*
cs2w_copy_string(const char* value)
{
    const char* source = value ? value : "";
    size_t length = strlen(source);
    char* copy = (char*)malloc(length + 1);
    if( !copy )
        return NULL;
    memcpy(copy, source, length + 1);
    return copy;
}

static bool
cs2w_session_valid(const struct CS2W_Session* session)
{
    return session && session->magic == CS2W_SESSION_MAGIC;
}

static bool
cs2w_invocation_valid(const struct CS2W_Invocation* invocation)
{
    return invocation && invocation->magic == CS2W_INVOCATION_MAGIC &&
           cs2w_session_valid(invocation->session);
}

static void
cs2w_session_error(
    struct CS2W_Session* session,
    int error,
    const char* message)
{
    if( !cs2w_session_valid(session) )
        return;
    session->last_error = error;
    snprintf(
        session->last_error_message,
        sizeof(session->last_error_message),
        "%s",
        message ? message : "");
}

static void
cs2w_invocation_error(
    struct CS2W_Invocation* invocation,
    int error)
{
    if( !cs2w_invocation_valid(invocation) )
        return;
    invocation->last_error = error;
    invocation->status = CS2W_RUN_ERROR;
    invocation->terminal = 1;
}

static int
cs2w_script_position(
    const struct CS2W_Session* session,
    int script_id,
    bool* found)
{
    int low = 0;
    int high = session->script_count;
    while( low < high )
    {
        int middle = low + (high - low) / 2;
        int candidate = session->scripts[middle]->id;
        if( candidate < script_id )
            low = middle + 1;
        else
            high = middle;
    }
    if( found )
        *found = low < session->script_count && session->scripts[low]->id == script_id;
    return low;
}

static struct CS2W_ScriptEntry*
cs2w_script_find(
    const struct CS2W_Session* session,
    int script_id)
{
    bool found = false;
    int position = cs2w_script_position(session, script_id, &found);
    return found ? session->scripts[position] : NULL;
}

static const struct CS2W_RequestDescriptor*
cs2w_request_descriptor(int kind)
{
    int low = 0;
    int high = (int)(sizeof(cs2w_requests) / sizeof(cs2w_requests[0]));
    while( low < high )
    {
        int middle = low + (high - low) / 2;
        if( cs2w_requests[middle].kind < kind )
            low = middle + 1;
        else
            high = middle;
    }
    if( low >= (int)(sizeof(cs2w_requests) / sizeof(cs2w_requests[0])) ||
        cs2w_requests[low].kind != kind )
        return NULL;
    return &cs2w_requests[low];
}

static const struct CS2W_FieldDescriptor*
cs2w_request_field(
    int kind,
    int field_index)
{
    const struct CS2W_RequestDescriptor* request = cs2w_request_descriptor(kind);
    if( !request || field_index < 0 || field_index >= request->field_count )
        return NULL;
    return &request->fields[field_index];
}

static int
cs2w_field_length(
    const void* request,
    const struct CS2W_FieldDescriptor* field)
{
    int length = field->capacity;
    if( field->count_offset != CS2W_NO_OFFSET )
    {
        memcpy(&length, (const uint8_t*)request + field->count_offset, sizeof(length));
        if( length < 0 )
            return 0;
        if( field->capacity > 0 && length > field->capacity )
            length = field->capacity;
        if( field->capacity == 0 && length > CS2W_MAX_DYNAMIC_FIELD )
            length = CS2W_MAX_DYNAMIC_FIELD;
    }
    return length;
}

static bool
cs2w_has_unsupported_long(
    const struct RSCache_CS2_Script* script,
    int dialect)
{
    if( script->local_long_count != 0 || script->long_argument_count != 0 )
        return true;
    for( int i = 0; i < script->op_count; i++ )
    {
        int opcode = script->opcodes[i];
        if( opcode == 61 || opcode == 62 || (opcode >= 66 && opcode <= 73) )
            return true;
        if( dialect == CS2W_DIALECT_CANONICAL && (opcode == 51 || opcode == 52) )
            return true;
    }
    return false;
}

static struct RSCache_ClientScript*
cs2w_decode_script(
    const struct CS2W_Session* session,
    int script_id,
    const uint8_t* data,
    int data_size)
{
    int preferred = session->dialect == CS2W_DIALECT_RS2_DAT2 || session->revision < 237
                        ? RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY
                        : RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN;
    struct RSCache_ClientScript* decoded = RSCache_ClientScriptNewFromDecodeFlags(
        script_id,
        data,
        data_size,
        preferred | RSCACHE_CLIENTSCRIPT_DECODE_QUIET);
    if( decoded )
        return decoded;
    return RSCache_ClientScriptNewFromDecodeFlags(
        script_id,
        data,
        data_size,
        (preferred == RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY
             ? RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN
             : RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY) |
            RSCACHE_CLIENTSCRIPT_DECODE_QUIET);
}

static int
cs2w_resolve_event_int(
    const struct CS2W_Invocation* invocation,
    int value)
{
    switch( value )
    {
    case CS2VM_SCRIPT_ARG_MOUSE_X: return invocation->event.mouse_x;
    case CS2VM_SCRIPT_ARG_MOUSE_Y: return invocation->event.mouse_y;
    case CS2VM_SCRIPT_ARG_WIDGET_ID: return invocation->event.component_id;
    case CS2VM_SCRIPT_ARG_OP_INDEX: return invocation->event.op_index;
    case CS2VM_SCRIPT_ARG_WIDGET_CHILD_INDEX: return invocation->event.component_sub_id;
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_ID: return invocation->event.drag_target_id;
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_CHILD_INDEX:
        return invocation->event.drag_target_sub_id;
    case CS2VM_SCRIPT_ARG_KEY_TYPED: return invocation->event.key_typed;
    case CS2VM_SCRIPT_ARG_KEY_PRESSED: return invocation->event.key_pressed;
    case CS2VM_SCRIPT_ARG_OP_SUBINDEX: return invocation->event.op_subindex;
    default: return value;
    }
}

static void
cs2w_fast_clear_snapshots(struct CS2W_Invocation* invocation)
{
    if( !invocation )
        return;
    for( int i = 0; i < invocation->fast_inventory_count; i++ )
        free(invocation->fast_inventories[i].entries);
    invocation->fast_inventory_count = 0;
    for( int i = 0; i < invocation->fast_children_count; i++ )
        free(invocation->fast_children[i].entries);
    invocation->fast_children_count = 0;
    invocation->fast_value_count = 0;
}

static int
cs2w_fast_reserve_records(
    struct CS2W_Invocation* invocation,
    int additional)
{
    if( additional < 0 || invocation->fast_record_count > INT_MAX - additional )
        return 0;
    int required = invocation->fast_record_count + additional;
    if( required <= invocation->fast_record_capacity )
        return 1;
    int capacity = invocation->fast_record_capacity
                       ? invocation->fast_record_capacity
                       : CS2W_FAST_INITIAL_RECORDS;
    while( capacity < required )
    {
        if( capacity > INT_MAX / 2 )
            return 0;
        capacity *= 2;
    }
    struct CS2W_FastRecord* records = (struct CS2W_FastRecord*)realloc(
        invocation->fast_records,
        (size_t)capacity * sizeof(*records));
    if( !records )
        return 0;
    invocation->fast_records = records;
    invocation->fast_record_capacity = capacity;
    return 1;
}

static int
cs2w_fast_reserve_arena(
    struct CS2W_Invocation* invocation,
    int additional,
    int* offset_out)
{
    int aligned = (invocation->fast_arena_size + 3) & ~3;
    if( additional < 0 || aligned < invocation->fast_arena_size ||
        aligned > INT_MAX - additional )
        return 0;
    int required = aligned + additional;
    if( required > invocation->fast_arena_capacity )
    {
        int capacity = invocation->fast_arena_capacity
                           ? invocation->fast_arena_capacity
                           : CS2W_FAST_INITIAL_ARENA;
        while( capacity < required )
        {
            if( capacity > INT_MAX / 2 )
                return 0;
            capacity *= 2;
        }
        uint8_t* arena = (uint8_t*)realloc(invocation->fast_arena, (size_t)capacity);
        if( !arena )
            return 0;
        invocation->fast_arena = arena;
        invocation->fast_arena_capacity = capacity;
    }
    if( aligned > invocation->fast_arena_size )
        memset(
            invocation->fast_arena + invocation->fast_arena_size,
            0,
            (size_t)(aligned - invocation->fast_arena_size));
    invocation->fast_arena_size = required;
    *offset_out = aligned;
    return 1;
}

static int cs2w_fast_flush(struct CS2W_Invocation* invocation);

static int
cs2w_fast_is_pending_target(
    const struct CS2W_Invocation* invocation,
    int component_id)
{
    return (invocation->fast_active_token != 0 &&
            component_id == invocation->fast_active_token) ||
           (invocation->fast_dot_token != 0 &&
            component_id == invocation->fast_dot_token);
}

static void
cs2w_fast_set_record_component(
    const struct CS2W_Invocation* invocation,
    struct CS2W_FastRecord* record,
    int component_id)
{
    record->words[1] = component_id;
    /* Word 11 distinguishes a batch-local create token from an ordinary
     * signed component id.  The full signed UID domain remains available to
     * real IF/CC records, including interface groups >= 32768. */
    record->words[11] = cs2w_fast_is_pending_target(invocation, component_id);
}

static void
cs2w_fast_patch_request_targets(
    struct CS2W_Invocation* invocation,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request,
    int active_token,
    int dot_token)
{
    if( (!active_token && !dot_token) || !request )
        return;
    const struct CS2W_RequestDescriptor* descriptor =
        cs2w_request_descriptor(request->kind);
    if( !descriptor )
        return;
    int active_id = CS2VM2_DotOrActiveComponentId(thread, 0);
    int dot_id = CS2VM2_DotOrActiveComponentId(thread, 1);
    for( int i = 0; i < descriptor->field_count; i++ )
    {
        const struct CS2W_FieldDescriptor* field = &descriptor->fields[i];
        if( field->kind != CS2W_FIELD_I32 ||
            (strcmp(field->name, "component_id") != 0 &&
             strcmp(field->name, "parent_id") != 0 &&
             strcmp(field->name, "uid") != 0) )
            continue;
        int value;
        memcpy(&value, (uint8_t*)request + field->offset, sizeof(value));
        if( active_token && value == active_token )
            memcpy((uint8_t*)request + field->offset, &active_id, sizeof(active_id));
        else if( dot_token && value == dot_token )
            memcpy((uint8_t*)request + field->offset, &dot_id, sizeof(dot_id));
    }
}

/* JavaScript deliberately caps one borrowed packed view at 65,536 records.
 * Commit at that exact wire boundary before appending another record.  A
 * chunk flush is still synchronous, so script/Host ordering is unchanged and
 * no C or JS state can be observed between adjacent records. */
static int
cs2w_fast_ensure_record_room(struct CS2W_Invocation* invocation)
{
    if( invocation->fast_record_count < CS2W_FAST_MAX_RECORDS )
        return 1;
    return cs2w_fast_flush(invocation) == CS2VM_EXECNO_OK;
}

static struct CS2W_FastRecord*
cs2w_fast_record(
    struct CS2W_Invocation* invocation,
    int kind)
{
    if( !cs2w_fast_ensure_record_room(invocation) ||
        !cs2w_fast_reserve_records(invocation, 1) )
        return NULL;
    struct CS2W_FastRecord* record =
        &invocation->fast_records[invocation->fast_record_count++];
    memset(record, 0, sizeof(*record));
    record->words[0] = kind;
    return record;
}

static int
cs2w_fast_enqueue_hook(
    struct CS2W_Invocation* invocation,
    int kind,
    int component_id,
    int script_id,
    const char* signature,
    const int* trigger_ids,
    int trigger_count,
    const int* int_args,
    int int_arg_count,
    uint64_t str_arg_mask,
    int str_arg_count,
    const char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN])
{
    /* A positive trigger_count above CS2VM_STACK_MAX cannot be produced by
     * CS2VM2: the handler must pop every trigger id before constructing this
     * request. Keep the 4096 guard as defence for a corrupt/direct request;
     * every rejected shape uses the generic bridge rather than aborting. */
    if( trigger_count < 0 || trigger_count > CS2W_MAX_DYNAMIC_FIELD ||
        int_arg_count < 0 || int_arg_count > CS2VM_SETON_INT_ARG_MAX ||
        str_arg_count < 0 || str_arg_count > CS2VM_SETON_STR_ARG_MAX ||
        (trigger_count > 0 && !trigger_ids) )
        return 0;
    const char* safe_signature = signature ? signature : "";
    size_t raw_signature_length = strlen(safe_signature);
    if( raw_signature_length > CS2W_MAX_HOOK_ARGS + 1 )
        return 0;
    int signature_length = (int)raw_signature_length;
    int signature_block = (int)((raw_signature_length + 3u) & ~3u);
    size_t payload_size = sizeof(int32_t) + (size_t)signature_block +
                          (size_t)trigger_count * sizeof(int32_t) +
                          (size_t)int_arg_count * sizeof(int32_t) +
                          (size_t)str_arg_count * CS2VM_SETON_STR_ARG_LEN;
    if( payload_size > INT_MAX )
        return 0;
    /* Hook payload offsets belong to the same chunk as their record.  Flush
     * before reserving arena bytes; flushing from cs2w_fast_record afterwards
     * would reset the arena and leave this record pointing at stale bytes. */
    if( !cs2w_fast_ensure_record_room(invocation) )
        return 0;
    int offset;
    if( !cs2w_fast_reserve_arena(invocation, (int)payload_size, &offset) )
        return 0;
    uint8_t* cursor = invocation->fast_arena + offset;
    memcpy(cursor, &signature_length, sizeof(signature_length));
    cursor += sizeof(signature_length);
    if( signature_length )
        memcpy(cursor, safe_signature, (size_t)signature_length);
    if( signature_block > signature_length )
        memset(cursor + signature_length, 0, (size_t)(signature_block - signature_length));
    cursor += signature_block;
    if( trigger_count )
    {
        memcpy(cursor, trigger_ids, (size_t)trigger_count * sizeof(int32_t));
        cursor += (size_t)trigger_count * sizeof(int32_t);
    }
    if( int_arg_count )
    {
        memcpy(cursor, int_args, (size_t)int_arg_count * sizeof(int32_t));
        cursor += (size_t)int_arg_count * sizeof(int32_t);
    }
    if( str_arg_count )
        memcpy(
            cursor,
            str_args,
            (size_t)str_arg_count * CS2VM_SETON_STR_ARG_LEN);

    struct CS2W_FastRecord* record = cs2w_fast_record(invocation, kind);
    if( !record )
    {
        invocation->fast_arena_size = offset;
        return 0;
    }
    if( kind < CS2VM_HOST_REQUEST_IF_SETPOSITION )
        cs2w_fast_set_record_component(invocation, record, component_id);
    else
        record->words[1] = component_id;
    record->words[2] = script_id;
    record->words[3] = trigger_count;
    record->words[4] = int_arg_count;
    record->words[5] = (int32_t)(uint32_t)str_arg_mask;
    record->words[6] = (int32_t)(uint32_t)(str_arg_mask >> 32);
    record->words[7] = str_arg_count;
    record->words[8] = signature_length;
    record->words[9] = offset;
    record->words[10] = (int)payload_size;
    return 1;
}

/* Compact setters with one UTF-8 argument use the same borrowed arena as
 * hooks.  The byte length is explicit so JavaScript never scans past this
 * transaction, and the record-room check precedes the arena reservation for
 * the same chunk-boundary reason as cs2w_fast_enqueue_hook. */
static int
cs2w_fast_enqueue_string(
    struct CS2W_Invocation* invocation,
    int kind,
    int component_id,
    int value,
    const char* text)
{
    const char* source = text ? text : "";
    size_t raw_length = strlen(source);
    if( raw_length > INT_MAX || !cs2w_fast_ensure_record_room(invocation) )
        return 0;
    int offset;
    if( !cs2w_fast_reserve_arena(invocation, (int)raw_length, &offset) )
        return 0;
    if( raw_length )
        memcpy(invocation->fast_arena + offset, source, raw_length);
    struct CS2W_FastRecord* record = cs2w_fast_record(invocation, kind);
    if( !record )
    {
        invocation->fast_arena_size = offset;
        return 0;
    }
    cs2w_fast_set_record_component(invocation, record, component_id);
    record->words[2] = value;
    record->words[3] = offset;
    record->words[4] = (int)raw_length;
    return 1;
}

static int
cs2w_fast_flush(struct CS2W_Invocation* invocation)
{
    if( !invocation->fast_host_enabled || invocation->fast_record_count == 0 )
        return CS2VM_EXECNO_OK;
#ifdef __EMSCRIPTEN__
    int active_token = invocation->fast_active_token;
    int dot_token = invocation->fast_dot_token;
    int result = cs2w_js_fast_host_flush(
        (uintptr_t)invocation->session,
        (uintptr_t)invocation,
        (uintptr_t)invocation->fast_records,
        invocation->fast_record_count,
        (uintptr_t)invocation->fast_arena,
        invocation->fast_arena_size);
    if( result != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( active_token || dot_token )
    {
        for( int i = 0; i < invocation->fast_record_count; i++ )
        {
            struct CS2W_FastRecord* record = &invocation->fast_records[i];
            if( record->words[0] != CS2VM_HOST_REQUEST_CC_CREATE )
                continue;
            int token = record->words[7];
            if( active_token && token == active_token )
                CS2VM2_SetTargetComponentId(invocation->thread, 0, record->words[6]);
            if( dot_token && token == dot_token )
                CS2VM2_SetTargetComponentId(invocation->thread, 1, record->words[6]);
        }
    }
    invocation->fast_active_token = 0;
    invocation->fast_dot_token = 0;
    invocation->fast_create_serial = 0;
    invocation->fast_record_count = 0;
    invocation->fast_arena_size = 0;
    return CS2VM_EXECNO_OK;
#else
    return CS2VM_EXECNO_ERROR;
#endif
}

static int
cs2w_fast_inventory_compare(const void* left, const void* right)
{
    const struct CS2W_FastInventoryEntry* a =
        (const struct CS2W_FastInventoryEntry*)left;
    const struct CS2W_FastInventoryEntry* b =
        (const struct CS2W_FastInventoryEntry*)right;
    return (a->slot > b->slot) - (a->slot < b->slot);
}

static int
cs2w_fast_child_compare(const void* left, const void* right)
{
    const struct CS2W_FastChildEntry* a = (const struct CS2W_FastChildEntry*)left;
    const struct CS2W_FastChildEntry* b = (const struct CS2W_FastChildEntry*)right;
    return (a->sub_id > b->sub_id) - (a->sub_id < b->sub_id);
}

static int
cs2w_fast_query_rows(
    struct CS2W_Invocation* invocation,
    int query_kind,
    int key,
    int row_size,
    void** rows_out,
    int* count_out,
    int* missing_out)
{
#ifdef __EMSCRIPTEN__
    int capacity = CS2W_FAST_INITIAL_SNAPSHOT_ENTRIES;
    void* rows = malloc((size_t)capacity * (size_t)row_size);
    if( !rows )
        return 0;
    int count = cs2w_js_fast_host_query(
        (uintptr_t)invocation->session,
        (uintptr_t)invocation,
        query_kind,
        key,
        (uintptr_t)rows,
        capacity);
    if( count == CS2W_FAST_QUERY_MISSING )
    {
        free(rows);
        *rows_out = NULL;
        *count_out = 0;
        *missing_out = 1;
        return 1;
    }
    if( count < 0 || count > 65536 )
    {
        free(rows);
        return 0;
    }
    if( count > capacity )
    {
        void* resized = realloc(rows, (size_t)count * (size_t)row_size);
        if( !resized )
        {
            free(rows);
            return 0;
        }
        rows = resized;
        capacity = count;
        int second = cs2w_js_fast_host_query(
            (uintptr_t)invocation->session,
            (uintptr_t)invocation,
            query_kind,
            key,
            (uintptr_t)rows,
            capacity);
        if( second != count )
        {
            free(rows);
            return 0;
        }
    }
    if( count == 0 )
    {
        free(rows);
        rows = NULL;
    }
    *rows_out = rows;
    *count_out = count;
    *missing_out = 0;
    return 1;
#else
    (void)invocation;
    (void)query_kind;
    (void)key;
    (void)row_size;
    (void)rows_out;
    (void)count_out;
    (void)missing_out;
    return 0;
#endif
}

static struct CS2W_FastInventory*
cs2w_fast_inventory(
    struct CS2W_Invocation* invocation,
    int id)
{
    for( int i = 0; i < invocation->fast_inventory_count; i++ )
        if( invocation->fast_inventories[i].id == id )
            return &invocation->fast_inventories[i];
    if( invocation->fast_inventory_count == invocation->fast_inventory_capacity )
    {
        int capacity = invocation->fast_inventory_capacity
                           ? invocation->fast_inventory_capacity * 2
                           : 4;
        struct CS2W_FastInventory* values = (struct CS2W_FastInventory*)realloc(
            invocation->fast_inventories,
            (size_t)capacity * sizeof(*values));
        if( !values )
            return NULL;
        invocation->fast_inventories = values;
        invocation->fast_inventory_capacity = capacity;
    }
    void* rows = NULL;
    int count = 0;
    int missing = 0;
    if( !cs2w_fast_query_rows(
            invocation,
            CS2W_FAST_QUERY_INVENTORY,
            id,
            (int)sizeof(struct CS2W_FastInventoryEntry),
            &rows,
            &count,
            &missing) ||
        missing )
        return NULL;
    struct CS2W_FastInventory* inventory =
        &invocation->fast_inventories[invocation->fast_inventory_count++];
    inventory->id = id;
    inventory->entries = (struct CS2W_FastInventoryEntry*)rows;
    inventory->count = count;
    if( count > 1 )
        qsort(inventory->entries, (size_t)count, sizeof(*inventory->entries),
              cs2w_fast_inventory_compare);
    return inventory;
}

static struct CS2W_FastChildren*
cs2w_fast_children(
    struct CS2W_Invocation* invocation,
    int parent_id)
{
    for( int i = 0; i < invocation->fast_children_count; i++ )
        if( invocation->fast_children[i].parent_id == parent_id )
            return &invocation->fast_children[i];
    if( invocation->fast_children_count == invocation->fast_children_capacity )
    {
        int capacity = invocation->fast_children_capacity
                           ? invocation->fast_children_capacity * 2
                           : 4;
        struct CS2W_FastChildren* values = (struct CS2W_FastChildren*)realloc(
            invocation->fast_children,
            (size_t)capacity * sizeof(*values));
        if( !values )
            return NULL;
        invocation->fast_children = values;
        invocation->fast_children_capacity = capacity;
    }
    void* rows = NULL;
    int count = 0;
    int missing = 0;
    if( !cs2w_fast_query_rows(
            invocation,
            CS2W_FAST_QUERY_CHILDREN,
            parent_id,
            (int)sizeof(struct CS2W_FastChildEntry),
            &rows,
            &count,
            &missing) )
        return NULL;
    struct CS2W_FastChildren* children =
        &invocation->fast_children[invocation->fast_children_count++];
    children->parent_id = parent_id;
    children->parent_exists = !missing;
    children->entries = (struct CS2W_FastChildEntry*)rows;
    children->count = count;
    if( count > 1 )
        qsort(children->entries, (size_t)count, sizeof(*children->entries),
              cs2w_fast_child_compare);
    return children;
}

static int
cs2w_fast_value(
    struct CS2W_Invocation* invocation,
    int query_kind,
    int key,
    int* value_out)
{
    for( int i = 0; i < invocation->fast_value_count; i++ )
    {
        struct CS2W_FastValue* value = &invocation->fast_values[i];
        if( value->kind == query_kind && value->key == key )
        {
            *value_out = value->value;
            return 1;
        }
    }
    if( invocation->fast_value_count == invocation->fast_value_capacity )
    {
        int capacity = invocation->fast_value_capacity
                           ? invocation->fast_value_capacity * 2
                           : 16;
        struct CS2W_FastValue* values = (struct CS2W_FastValue*)realloc(
            invocation->fast_values,
            (size_t)capacity * sizeof(*values));
        if( !values )
            return 0;
        invocation->fast_values = values;
        invocation->fast_value_capacity = capacity;
    }
#ifdef __EMSCRIPTEN__
    int value = 0;
    int count = cs2w_js_fast_host_query(
        (uintptr_t)invocation->session,
        (uintptr_t)invocation,
        query_kind,
        key,
        (uintptr_t)&value,
        1);
    if( count != 1 )
        return 0;
    struct CS2W_FastValue* cached =
        &invocation->fast_values[invocation->fast_value_count++];
    cached->kind = query_kind;
    cached->key = key;
    cached->value = value;
    *value_out = value;
    return 1;
#else
    return 0;
#endif
}

static uint32_t
cs2w_fast_scalar_hash(int request_kind, int a, int b, int c)
{
    uint32_t hash = 2166136261u;
    const uint32_t words[] = {
        (uint32_t)request_kind, (uint32_t)a, (uint32_t)b, (uint32_t)c
    };
    for( int i = 0; i < 4; i++ )
    {
        hash ^= words[i];
        hash *= 16777619u;
        hash ^= hash >> 16;
    }
    return hash;
}

static struct CS2W_FastScalarCacheEntry*
cs2w_fast_scalar_cache_slot(
    struct CS2W_Session* session,
    int request_kind,
    int a,
    int b,
    int c)
{
    if( !session->fast_scalar_cache_capacity ) return NULL;
    uint32_t mask = (uint32_t)session->fast_scalar_cache_capacity - 1u;
    uint32_t index = cs2w_fast_scalar_hash(request_kind, a, b, c) & mask;
    for( ;; )
    {
        struct CS2W_FastScalarCacheEntry* entry =
            &session->fast_scalar_cache[index];
        if( !entry->occupied ||
            (entry->request_kind == request_kind && entry->a == a &&
             entry->b == b && entry->c == c) )
            return entry;
        index = (index + 1u) & mask;
    }
}

static int
cs2w_fast_scalar_cache_grow(struct CS2W_Session* session)
{
    int old_capacity = session->fast_scalar_cache_capacity;
    int capacity = old_capacity ? old_capacity * 2 : 256;
    if( capacity < old_capacity || capacity > 65536 ) return 0;
    struct CS2W_FastScalarCacheEntry* entries =
        (struct CS2W_FastScalarCacheEntry*)calloc(
            (size_t)capacity, sizeof(*entries));
    if( !entries ) return 0;
    struct CS2W_FastScalarCacheEntry* old_entries = session->fast_scalar_cache;
    session->fast_scalar_cache = entries;
    session->fast_scalar_cache_capacity = capacity;
    for( int i = 0; i < old_capacity; i++ )
    {
        struct CS2W_FastScalarCacheEntry* old = &old_entries[i];
        if( !old->occupied ) continue;
        struct CS2W_FastScalarCacheEntry* next = cs2w_fast_scalar_cache_slot(
            session, old->request_kind, old->a, old->b, old->c);
        *next = *old;
    }
    free(old_entries);
    return 1;
}

static int
cs2w_fast_scalar_cache_lookup(
    struct CS2W_Session* session,
    int request_kind,
    int a,
    int b,
    int c,
    int* value_out,
    const char** string_out,
    int* string_length_out)
{
    struct CS2W_FastScalarCacheEntry* entry = cs2w_fast_scalar_cache_slot(
        session, request_kind, a, b, c);
    if( !entry || !entry->occupied ) return 0;
    *value_out = entry->int_value;
    if( entry->result_kind == 2 )
    {
        *string_out = entry->string_value;
        *string_length_out = entry->string_length;
    }
    return entry->result_kind;
}

/* Cache allocation is an optimization only. If memory is tight, keep the
 * just-produced HOST result and retry JavaScript on the next lookup rather
 * than turning a valid script into an allocation failure. */
static void
cs2w_fast_scalar_cache_store(
    struct CS2W_Session* session,
    int request_kind,
    int a,
    int b,
    int c,
    int result_kind,
    int int_value,
    const char* string_value,
    int string_length)
{
    if( result_kind != 1 && result_kind != 2 ) return;
    if( !session->fast_scalar_cache_capacity ||
        (session->fast_scalar_cache_count + 1) * 10 >=
            session->fast_scalar_cache_capacity * 7 )
    {
        if( !cs2w_fast_scalar_cache_grow(session) ) return;
    }
    struct CS2W_FastScalarCacheEntry* entry = cs2w_fast_scalar_cache_slot(
        session, request_kind, a, b, c);
    if( !entry || entry->occupied ) return;
    char* string_copy = NULL;
    if( result_kind == 2 )
    {
        string_copy = (char*)malloc((size_t)string_length + 1);
        if( !string_copy ) return;
        memcpy(string_copy, string_value, (size_t)string_length);
        string_copy[string_length] = '\0';
    }
    entry->occupied = 1;
    entry->request_kind = request_kind;
    entry->a = a;
    entry->b = b;
    entry->c = c;
    entry->result_kind = result_kind;
    entry->int_value = int_value;
    entry->string_value = string_copy;
    entry->string_length = string_length;
    session->fast_scalar_cache_count++;
}

/* Returns 1 for an exact integer result, 2 for an exact UTF-8 result, 0 for a
 * generic fallback, and -1 on a JavaScript/HOST failure. */
static int
cs2w_fast_scalar_query(
    struct CS2W_Invocation* invocation,
    int request_kind,
    int a,
    int b,
    int c,
    int* value_out,
    const char** string_out,
    int* string_length_out)
{
#ifdef __EMSCRIPTEN__
    int cached = cs2w_fast_scalar_cache_lookup(
        invocation->session, request_kind, a, b, c,
        value_out, string_out, string_length_out);
    if( cached ) return cached;
    if( !invocation->fast_scalar_string )
    {
        invocation->fast_scalar_string = (char*)malloc(4096);
        if( !invocation->fast_scalar_string ) return -1;
        invocation->fast_scalar_string_capacity = 4096;
    }
    int length = 0;
    int cacheable = 0;
    int result = cs2w_js_fast_host_scalar_query(
        (uintptr_t)invocation->session,
        (uintptr_t)invocation,
        request_kind,
        a,
        b,
        c,
        (uintptr_t)value_out,
        (uintptr_t)invocation->fast_scalar_string,
        invocation->fast_scalar_string_capacity,
        (uintptr_t)&length,
        (uintptr_t)&cacheable);
    if( result == 2 && length >= invocation->fast_scalar_string_capacity )
    {
        if( length < 0 || length >= CS2W_MAX_DYNAMIC_FIELD * CS2VM_SETON_STR_ARG_LEN )
            return 0;
        char* buffer = (char*)realloc(
            invocation->fast_scalar_string, (size_t)length + 1);
        if( !buffer ) return -1;
        invocation->fast_scalar_string = buffer;
        invocation->fast_scalar_string_capacity = length + 1;
        result = cs2w_js_fast_host_scalar_query(
            (uintptr_t)invocation->session,
            (uintptr_t)invocation,
            request_kind,
            a,
            b,
            c,
            (uintptr_t)value_out,
            (uintptr_t)invocation->fast_scalar_string,
            invocation->fast_scalar_string_capacity,
            (uintptr_t)&length,
            (uintptr_t)&cacheable);
    }
    if( result == 2 )
    {
        if( length < 0 || length >= invocation->fast_scalar_string_capacity ) return -1;
        invocation->fast_scalar_string[length] = '\0';
        *string_out = invocation->fast_scalar_string;
        *string_length_out = length;
    }
    if( cacheable && (result == 1 || result == 2) )
        cs2w_fast_scalar_cache_store(
            invocation->session, request_kind, a, b, c, result,
            *value_out, result == 2 ? invocation->fast_scalar_string : NULL,
            result == 2 ? length : 0);
    return result;
#else
    (void)invocation;
    (void)request_kind;
    (void)a;
    (void)b;
    (void)c;
    (void)value_out;
    (void)string_out;
    (void)string_length_out;
    return -1;
#endif
}

static int
cs2w_fast_push_scalar(
    struct CS2VM2_Thread* thread,
    int result_kind,
    int int_value,
    const char* string_value,
    int string_length,
    int allow_string)
{
    if( result_kind < 0 ) return CS2VM_EXECNO_ERROR;
    if( result_kind == 0 ) return CS2VM_EXECNO_YIELD;
    if( result_kind == 1 ) return CS2VM2_PushInt(thread, int_value);
    if( result_kind != 2 || !allow_string ) return CS2VM_EXECNO_YIELD;
    char* copy = CS2VM2_StrDupLen(thread, string_value, (size_t)string_length);
    return copy ? CS2VM2_PushStr(thread, copy) : CS2VM_EXECNO_ERROR;
}

static int
cs2w_fast_inventory_value(
    const struct CS2W_FastInventory* inventory,
    int slot,
    int count_value)
{
    int low = 0;
    int high = inventory->count;
    while( low < high )
    {
        int middle = low + (high - low) / 2;
        if( inventory->entries[middle].slot < slot )
            low = middle + 1;
        else
            high = middle;
    }
    if( low < inventory->count && inventory->entries[low].slot == slot )
        return count_value ? inventory->entries[low].count
                           : inventory->entries[low].object_id;
    return count_value ? 0 : -1;
}

static int
cs2w_fast_child_value(
    const struct CS2W_FastChildren* children,
    int sub_id,
    int* component_id_out)
{
    if( !children->parent_exists )
        return 0;
    int low = 0;
    int high = children->count;
    while( low < high )
    {
        int middle = low + (high - low) / 2;
        if( children->entries[middle].sub_id < sub_id )
            low = middle + 1;
        else
            high = middle;
    }
    if( low >= children->count || children->entries[low].sub_id != sub_id )
        return 0;
    *component_id_out = children->entries[low].component_id;
    return 1;
}

static int
cs2w_fast_host_exec(
    struct CS2W_Invocation* invocation,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct CS2W_FastRecord* record;
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_CC_CREATE:
    {
        /* Keep a batch-local target token in the C thread. Subsequent compact
         * CC setters carry word-11=1 and JavaScript resolves that token after
         * applying this create. Any observing/generic request flushes first
         * and patches the real signed id back into the VM/request. */
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        if( invocation->fast_create_serial >= INT_MAX - 1 )
            return CS2VM_EXECNO_ERROR;
        int previous = CS2VM2_DotOrActiveComponentId(
            thread, request->u.CC_CREATE.dot_operand);
        int previous_is_token = cs2w_fast_is_pending_target(invocation, previous);
        /* CC_CREATECHILD/CC_CREATESIBLING validate their implicit parent in
         * CS2VM2 before the HOST bridge sees the request, and reject every
         * negative component id.  Keep pending ids in a positive reserved
         * band so nested creation reaches this bridge; word 11 and the
         * invocation token slots, rather than the sign, distinguish them
         * from real component ids. */
        int token = INT_MAX - ++invocation->fast_create_serial;
        record->words[1] = request->u.CC_CREATE.parent_id;
        record->words[2] = request->u.CC_CREATE.component_type;
        record->words[3] = request->u.CC_CREATE.child_index;
        record->words[4] = request->u.CC_CREATE.is_nested;
        record->words[5] = request->u.CC_CREATE.dot_operand;
        record->words[6] = -1;
        record->words[7] = token;
        record->words[8] = previous;
        record->words[9] = previous_is_token;
        if( request->u.CC_CREATE.dot_operand ) invocation->fast_dot_token = token;
        else invocation->fast_active_token = token;
        CS2VM2_SetTargetComponentId(thread, request->u.CC_CREATE.dot_operand, token);
        return CS2VM_EXECNO_OK;
    }
    case CS2VM_HOST_REQUEST_CC_FIND:
    {
        struct CS2W_FastChildren* children = cs2w_fast_children(
            invocation, request->u.CC_FIND.parent_id);
        if( !children )
            return CS2VM_EXECNO_ERROR;
        int component_id = -1;
        int found = cs2w_fast_child_value(
            children, request->u.CC_FIND.sub_id, &component_id);
        record = cs2w_fast_record(invocation, request->kind);
        if( !record )
            return CS2VM_EXECNO_ERROR;
        record->words[1] = request->u.CC_FIND.parent_id;
        record->words[2] = request->u.CC_FIND.sub_id;
        record->words[3] = request->u.CC_FIND.dot_operand;
        record->words[4] = component_id;
        if( found )
            CS2VM2_SetTargetComponentId(
                thread, request->u.CC_FIND.dot_operand, component_id);
        return CS2VM2_PushInt(thread, found);
    }
    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
    {
        int value = 0;
        const char* string_value = NULL;
        int string_length = 0;
        int result = cs2w_fast_scalar_query(
            invocation,
            request->kind,
            request->u.STRUCT_PARAM.struct_id,
            request->u.STRUCT_PARAM.param_id,
            0,
            &value,
            &string_value,
            &string_length);
        return cs2w_fast_push_scalar(
            thread, result, value, string_value, string_length, 1);
    }
    case CS2VM_HOST_REQUEST_ENUM_STRING:
    case CS2VM_HOST_REQUEST_ENUM:
    {
        int enum_id = request->kind == CS2VM_HOST_REQUEST_ENUM
                          ? request->u.ENUM.enum_id
                          : request->u.ENUM_STRING.enum_id;
        int key = request->kind == CS2VM_HOST_REQUEST_ENUM
                      ? request->u.ENUM.key
                      : request->u.ENUM_STRING.key;
        int output_type = request->kind == CS2VM_HOST_REQUEST_ENUM
                              ? request->u.ENUM.output_type
                              : (int)'s';
        int value = 0;
        const char* string_value = NULL;
        int string_length = 0;
        int result = cs2w_fast_scalar_query(
            invocation,
            request->kind,
            enum_id,
            key,
            output_type,
            &value,
            &string_value,
            &string_length);
        return cs2w_fast_push_scalar(
            thread, result, value, string_value, string_length,
            output_type == (int)'s');
    }
    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
    {
        int value = 0;
        const char* string_value = NULL;
        int string_length = 0;
        int result = cs2w_fast_scalar_query(
            invocation,
            request->kind,
            request->u.ENUM_GETOUTPUTCOUNT.enum_id,
            0,
            0,
            &value,
            &string_value,
            &string_length);
        return cs2w_fast_push_scalar(
            thread, result, value, string_value, string_length, 0);
    }
    case CS2VM_HOST_REQUEST_CC_GETX:
    case CS2VM_HOST_REQUEST_CC_GETY:
    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
    case CS2VM_HOST_REQUEST_IF_GETX:
    case CS2VM_HOST_REQUEST_IF_GETY:
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
    {
        /* Component geometry observes every queued setter. A pending create
         * additionally needs generic target patching, so leave that rare
         * shape to the normal barrier. */
        if( invocation->fast_active_token || invocation->fast_dot_token )
            return CS2VM_EXECNO_YIELD;
        if( cs2w_fast_flush(invocation) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        cs2w_fast_clear_snapshots(invocation);
        int component_id;
        switch( request->kind )
        {
        case CS2VM_HOST_REQUEST_CC_GETX: component_id = request->u.CC_GETX.component_id; break;
        case CS2VM_HOST_REQUEST_CC_GETY: component_id = request->u.CC_GETY.component_id; break;
        case CS2VM_HOST_REQUEST_CC_GETWIDTH:
            component_id = request->u.CC_GETWIDTH.component_id; break;
        case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
            component_id = request->u.CC_GETHEIGHT.component_id; break;
        case CS2VM_HOST_REQUEST_IF_GETX: component_id = request->u.IF_GETX.component_id; break;
        case CS2VM_HOST_REQUEST_IF_GETY: component_id = request->u.IF_GETY.component_id; break;
        case CS2VM_HOST_REQUEST_IF_GETWIDTH:
            component_id = request->u.IF_GETWIDTH.component_id; break;
        default: component_id = request->u.IF_GETHEIGHT.component_id; break;
        }
        int value = 0;
        const char* string_value = NULL;
        int string_length = 0;
        int result = cs2w_fast_scalar_query(
            invocation, request->kind, component_id, 0, 0,
            &value, &string_value, &string_length);
        return cs2w_fast_push_scalar(
            thread, result, value, string_value, string_length, 0);
    }
    case CS2VM_HOST_REQUEST_PUSH_VAR:
    case CS2VM_HOST_REQUEST_PUSH_VARBIT:
    case CS2VM_HOST_REQUEST_PUSH_VARC_INT:
    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
    {
        int query_kind;
        int key;
        if( request->kind == CS2VM_HOST_REQUEST_PUSH_VAR )
        {
            query_kind = CS2W_FAST_QUERY_VAR;
            key = request->u.PUSH_VAR.varp_id;
        }
        else if( request->kind == CS2VM_HOST_REQUEST_PUSH_VARBIT )
        {
            query_kind = CS2W_FAST_QUERY_VARBIT;
            key = request->u.PUSH_VARBIT.varbit_id;
        }
        else if( request->kind == CS2VM_HOST_REQUEST_PUSH_VARC_INT )
        {
            query_kind = CS2W_FAST_QUERY_VARC_INT;
            key = request->u.PUSH_VARC_INT.varc_id;
        }
        else
        {
            query_kind = CS2W_FAST_QUERY_CLIENTCLOCK;
            key = 0;
        }
        int value;
        if( !cs2w_fast_value(invocation, query_kind, key, &value) )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushInt(thread, value);
    }
    case CS2VM_HOST_REQUEST_INV_GETOBJ:
    case CS2VM_HOST_REQUEST_INV_GETNUM:
    {
        int id = request->kind == CS2VM_HOST_REQUEST_INV_GETOBJ
                     ? request->u.INV_GETOBJ.inv_id
                     : request->u.INV_GETNUM.inv_id;
        int slot = request->kind == CS2VM_HOST_REQUEST_INV_GETOBJ
                       ? request->u.INV_GETOBJ.slot
                       : request->u.INV_GETNUM.slot;
        struct CS2W_FastInventory* inventory = cs2w_fast_inventory(invocation, id);
        if( !inventory )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushInt(
            thread,
            cs2w_fast_inventory_value(
                inventory,
                slot,
                request->kind == CS2VM_HOST_REQUEST_INV_GETNUM));
    }
    case CS2VM_HOST_REQUEST_CC_SETHIDE:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETHIDE.component_id);
        record->words[2] = request->u.CC_SETHIDE.hidden ? 1 : 0;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        record->words[1] = request->u.IF_SETHIDE.component_id;
        record->words[2] = request->u.IF_SETHIDE.hidden ? 1 : 0;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETPOSITION.component_id);
        record->words[2] = request->u.CC_SETPOSITION.x;
        record->words[3] = request->u.CC_SETPOSITION.y;
        record->words[4] = request->u.CC_SETPOSITION.xmode;
        record->words[5] = request->u.CC_SETPOSITION.ymode;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETSIZE:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETSIZE.component_id);
        record->words[2] = request->u.CC_SETSIZE.width;
        record->words[3] = request->u.CC_SETSIZE.height;
        record->words[4] = request->u.CC_SETSIZE.wmode;
        record->words[5] = request->u.CC_SETSIZE.hmode;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_IF_SETPOSITION:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        record->words[1] = request->u.IF_SETPOSITION.component_id;
        record->words[2] = request->u.IF_SETPOSITION.x;
        record->words[3] = request->u.IF_SETPOSITION.y;
        record->words[4] = request->u.IF_SETPOSITION.xmode;
        record->words[5] = request->u.IF_SETPOSITION.ymode;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_IF_SETSIZE:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        record->words[1] = request->u.IF_SETSIZE.component_id;
        record->words[2] = request->u.IF_SETSIZE.width;
        record->words[3] = request->u.IF_SETSIZE.height;
        record->words[4] = request->u.IF_SETSIZE.wmode;
        record->words[5] = request->u.IF_SETSIZE.hmode;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETTRANS:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETTRANS.component_id);
        record->words[2] = request->u.CC_SETTRANS.trans;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETCOLOUR:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETCOLOUR.component_id);
        record->words[2] = request->u.CC_SETCOLOUR.colour;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETFILL:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETFILL.component_id);
        record->words[2] = request->u.CC_SETFILL.filled;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETGRAPHIC.component_id);
        record->words[2] = request->u.CC_SETGRAPHIC.graphic_id;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
        return cs2w_fast_enqueue_string(
                   invocation, request->kind, request->u.CC_SETTEXT.component_id,
                   0, request->u.CC_SETTEXT.text)
                   ? CS2VM_EXECNO_OK
                   : CS2VM_EXECNO_YIELD;
    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETTEXTFONT.component_id);
        record->words[2] = request->u.CC_SETTEXTFONT.font_id;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETTEXTALIGN.component_id);
        record->words[2] = request->u.CC_SETTEXTALIGN.x_align;
        record->words[3] = request->u.CC_SETTEXTALIGN.y_align;
        record->words[4] = request->u.CC_SETTEXTALIGN.line_height;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETTEXTSHADOW.component_id);
        record->words[2] = request->u.CC_SETTEXTSHADOW.shadowed;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_IF_SETTRANS:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        record->words[1] = request->u.IF_SETTRANS.component_id;
        record->words[2] = request->u.IF_SETTRANS.trans;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETOBJECT.component_id);
        record->words[2] = request->u.CC_SETOBJECT.obj_id;
        record->words[3] = request->u.CC_SETOBJECT.count;
        record->words[4] = request->u.CC_SETOBJECT.num_mode;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETOBJECT_ALWAYS_NUM:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETOBJECT_ALWAYS_NUM.component_id);
        record->words[2] = request->u.CC_SETOBJECT_ALWAYS_NUM.obj_id;
        record->words[3] = request->u.CC_SETOBJECT_ALWAYS_NUM.count;
        record->words[4] = request->u.CC_SETOBJECT_ALWAYS_NUM.num_mode;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETOBJECT_NONUM:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETOBJECT_NONUM.component_id);
        record->words[2] = request->u.CC_SETOBJECT_NONUM.obj_id;
        record->words[3] = request->u.CC_SETOBJECT_NONUM.count;
        record->words[4] = request->u.CC_SETOBJECT_NONUM.num_mode;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_CLEAROPS:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_CLEAROPS.component_id);
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        record->words[1] = request->u.IF_CLEAROPS.component_id;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETOP:
        return cs2w_fast_enqueue_string(
                   invocation, request->kind, request->u.CC_SETOP.component_id,
                   request->u.CC_SETOP.index, request->u.CC_SETOP.text)
                   ? CS2VM_EXECNO_OK
                   : CS2VM_EXECNO_YIELD;
    case CS2VM_HOST_REQUEST_CC_SETOPBASE:
        return cs2w_fast_enqueue_string(
                   invocation, request->kind, request->u.CC_SETOPBASE.component_id,
                   0, request->u.CC_SETOPBASE.text)
                   ? CS2VM_EXECNO_OK
                   : CS2VM_EXECNO_YIELD;
    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETDRAGGABLEBEHAVIOR.component_id);
        record->words[2] = request->u.CC_SETDRAGGABLEBEHAVIOR.behavior;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETDRAGDEADZONE.component_id);
        record->words[2] = request->u.CC_SETDRAGDEADZONE.zone;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
        record = cs2w_fast_record(invocation, request->kind);
        if( !record ) return CS2VM_EXECNO_ERROR;
        cs2w_fast_set_record_component(
            invocation, record, request->u.CC_SETDRAGDEADTIME.component_id);
        record->words[2] = request->u.CC_SETDRAGDEADTIME.time;
        return CS2VM_EXECNO_OK;
#define CS2W_FAST_HOOK_CASE(name)                                            \
    case CS2VM_HOST_REQUEST_##name:                                          \
        return cs2w_fast_enqueue_hook(                                       \
                   invocation, request->kind, request->u.name.component_id,  \
                   request->u.name.script_id, request->u.name.signature,     \
                   request->u.name.trigger_ids, request->u.name.trigger_count, \
                   request->u.name.int_args, request->u.name.int_arg_count,  \
                   request->u.name.str_arg_mask, request->u.name.str_arg_count, \
                   request->u.name.str_args)                                 \
                   ? CS2VM_EXECNO_OK                                         \
                   : CS2VM_EXECNO_YIELD
        CS2W_FAST_HOOK_CASE(CC_SETONMOUSEOVER);
        CS2W_FAST_HOOK_CASE(CC_SETONMOUSELEAVE);
        CS2W_FAST_HOOK_CASE(CC_SETONDRAG);
        CS2W_FAST_HOOK_CASE(CC_SETONOP);
        CS2W_FAST_HOOK_CASE(CC_SETONDRAGCOMPLETE);
        CS2W_FAST_HOOK_CASE(CC_SETONMOUSEREPEAT);
        CS2W_FAST_HOOK_CASE(IF_SETONMOUSEOVER);
        CS2W_FAST_HOOK_CASE(IF_SETONMOUSELEAVE);
        CS2W_FAST_HOOK_CASE(IF_SETONOP);
#undef CS2W_FAST_HOOK_CASE
    default: return CS2VM_EXECNO_YIELD;
    }
}

static int
cs2w_fast_host_is_pure_read(int kind)
{
    /* These reads use immutable content or host stores independent of the
     * queued UITree fields. Keep the compact transaction open across them;
     * component reads and every state write still force an ordered flush. */
    return kind == CS2VM_HOST_REQUEST_ENUM ||
           kind == CS2VM_HOST_REQUEST_ENUM_STRING ||
           kind == CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT ||
           kind == CS2VM_HOST_REQUEST_CHAT_GETHISTORYLENGTH ||
           kind == CS2VM_HOST_REQUEST_STRUCT_PARAM ||
           kind == CS2VM_HOST_REQUEST_PUSH_VAR ||
           kind == CS2VM_HOST_REQUEST_PUSH_VARBIT ||
           kind == CS2VM_HOST_REQUEST_PUSH_VARC_INT ||
           kind == CS2VM_HOST_REQUEST_PUSH_VARC_STRING ||
           kind == CS2VM_HOST_REQUEST_CLIENTCLOCK;
}

static int
cs2w_bridge_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct CS2W_Invocation* invocation = (struct CS2W_Invocation*)CS2VM_USER(thread);
    if( !cs2w_invocation_valid(invocation) || !request )
        return CS2VM_EXECNO_ERROR;
    invocation->host_call_count++;

    if( request->kind == CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS )
    {
        struct CS2W_ScriptEntry* callee = cs2w_script_find(
            invocation->session,
            request->u.GOSUB_WITH_PARAMS.script_id);
        if( !callee )
        {
            invocation->last_error = CS2W_ERROR_MISSING_SCRIPT;
            return CS2VM_EXECNO_ERROR;
        }
        return CS2VM2_PushCallScript(thread, &callee->script);
    }

    int pure_fast_read = 0;
    if( invocation->fast_host_enabled )
    {
        /* A find must observe earlier creates, and a full chunk must commit
         * before cs2w_fast_record can reuse its batch-local target tokens.
         * Patch any request field already built from the placeholder after
         * the flush publishes the real id into the C thread. */
        if( (request->kind == CS2VM_HOST_REQUEST_CC_FIND &&
             (invocation->fast_active_token || invocation->fast_dot_token)) ||
            invocation->fast_record_count >= CS2W_FAST_MAX_RECORDS )
        {
            int active_token = invocation->fast_active_token;
            int dot_token = invocation->fast_dot_token;
            if( cs2w_fast_flush(invocation) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
            cs2w_fast_patch_request_targets(
                invocation, thread, request, active_token, dot_token);
            cs2w_fast_clear_snapshots(invocation);
        }
        int fast_result = cs2w_fast_host_exec(invocation, thread, request);
        if( fast_result != CS2VM_EXECNO_YIELD )
            return fast_result;
        /* Any request outside the proven transaction vocabulary may observe
         * component state or invoke user code. Commit all earlier effects in
         * wire order first, then discard snapshots which that request could
         * invalidate. */
        pure_fast_read = cs2w_fast_host_is_pure_read(request->kind);
        if( !pure_fast_read )
        {
            int active_token = invocation->fast_active_token;
            int dot_token = invocation->fast_dot_token;
            if( cs2w_fast_flush(invocation) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
            cs2w_fast_patch_request_targets(
                invocation, thread, request, active_token, dot_token);
            cs2w_fast_clear_snapshots(invocation);
        }
    }

#ifdef __EMSCRIPTEN__
    int result = cs2w_js_host_exec(
        (uintptr_t)invocation->session,
        (uintptr_t)invocation,
        (uintptr_t)thread,
        (uintptr_t)request,
        (int)request->kind);
#else
    int result = g_native_host_exec
                     ? g_native_host_exec(
                           (uintptr_t)invocation->session,
                           (uintptr_t)invocation,
                           (uintptr_t)thread,
                           (uintptr_t)request,
                           (int)request->kind)
                     : CS2VM_EXECNO_ERROR;
#endif
    if( invocation->fast_host_enabled && !pure_fast_read )
        cs2w_fast_clear_snapshots(invocation);
    if( result != CS2VM_EXECNO_OK && result != CS2VM_EXECNO_ERROR &&
        result != CS2VM_EXECNO_YIELD )
        return CS2VM_EXECNO_ERROR;
    return result;
}

CS2W_EXPORT uint32_t
cs2w_abi_version(void)
{
    return CS2W_ABI_VERSION;
}

CS2W_EXPORT struct CS2W_Session*
cs2w_session_create(
    int dialect,
    int revision)
{
    if( dialect != CS2W_DIALECT_CANONICAL && dialect != CS2W_DIALECT_RS2_DAT2 )
        return NULL;
    struct CS2W_Session* session = (struct CS2W_Session*)calloc(1, sizeof(*session));
    if( !session )
        return NULL;
    session->magic = CS2W_SESSION_MAGIC;
    session->dialect = dialect;
    session->revision = revision;
    return session;
}

CS2W_EXPORT int
cs2w_session_destroy(struct CS2W_Session* session)
{
    if( !cs2w_session_valid(session) )
        return 0;
    if( session->invocation_count != 0 )
    {
        cs2w_session_error(session, CS2W_ERROR_BAD_STATE, "session has live invocations");
        return 0;
    }
    for( int i = 0; i < session->script_count; i++ )
    {
        CS2VM2_ScriptFree(&session->scripts[i]->script);
        free(session->scripts[i]);
    }
    free(session->scripts);
    for( int i = 0; i < session->fast_scalar_cache_capacity; i++ )
        free(session->fast_scalar_cache[i].string_value);
    free(session->fast_scalar_cache);
    session->magic = 0;
    free(session);
    return 1;
}

CS2W_EXPORT int
cs2w_session_load_script(
    struct CS2W_Session* session,
    int script_id,
    const uint8_t* data,
    int data_size)
{
    bool found = false;
    int position;
    struct RSCache_ClientScript* decoded;
    struct CS2W_ScriptEntry* entry;

    if( !cs2w_session_valid(session) || script_id < 0 || !data || data_size <= 0 )
        return 0;
    session->last_error = CS2W_ERROR_NONE;
    session->last_error_message[0] = '\0';
    if( session->sealed || session->invocation_count != 0 )
    {
        cs2w_session_error(session, CS2W_ERROR_BAD_STATE, "script registry is immutable");
        return 0;
    }
    position = cs2w_script_position(session, script_id, &found);
    if( found )
    {
        cs2w_session_error(session, CS2W_ERROR_DUPLICATE_SCRIPT, "duplicate script id");
        return 0;
    }
    decoded = cs2w_decode_script(session, script_id, data, data_size);
    if( !decoded )
    {
        cs2w_session_error(session, CS2W_ERROR_DECODE, "clientscript decode failed");
        return 0;
    }
    if( cs2w_has_unsupported_long(&decoded->script, session->dialect) )
    {
        RSCache_ClientScriptFree(decoded);
        cs2w_session_error(
            session,
            CS2W_ERROR_UNSUPPORTED_LONG,
            "CS2VM2 does not implement long-stack bytecode");
        return 0;
    }
    entry = (struct CS2W_ScriptEntry*)calloc(1, sizeof(*entry));
    if( !entry )
    {
        RSCache_ClientScriptFree(decoded);
        cs2w_session_error(session, CS2W_ERROR_OUT_OF_MEMORY, "script allocation failed");
        return 0;
    }
    entry->id = script_id;
    /* CS2VM2 has no long stack and the validation above proved no long value
     * is reachable.  The rscache decoder nevertheless allocates a zero-filled
     * long operand bank for every script; ScriptFromRSCache predates that bank
     * and cannot transfer it, so release it before the ownership move. */
    free(decoded->script.long_operands);
    decoded->script.long_operands = NULL;
    if( !CS2VM2_ScriptFromRSCache(
            &decoded->script,
            &entry->script,
            session->dialect == CS2W_DIALECT_RS2_DAT2 ? CS2_OPCODE_DIALECT_RS2_DAT2
                                                       : CS2_OPCODE_DIALECT_CANONICAL) )
    {
        RSCache_ClientScriptFree(decoded);
        free(entry);
        cs2w_session_error(session, CS2W_ERROR_DECODE, "clientscript conversion failed");
        return 0;
    }
    entry->script.rs2_dialect = session->dialect == CS2W_DIALECT_RS2_DAT2;
    RSCache_ClientScriptFree(decoded);

    if( session->script_count == session->script_capacity )
    {
        int capacity = session->script_capacity ? session->script_capacity * 2 : 64;
        struct CS2W_ScriptEntry** scripts = (struct CS2W_ScriptEntry**)realloc(
            session->scripts,
            (size_t)capacity * sizeof(*scripts));
        if( !scripts )
        {
            CS2VM2_ScriptFree(&entry->script);
            free(entry);
            cs2w_session_error(session, CS2W_ERROR_OUT_OF_MEMORY, "script registry allocation failed");
            return 0;
        }
        session->scripts = scripts;
        session->script_capacity = capacity;
    }
    if( position < session->script_count )
        memmove(
            &session->scripts[position + 1],
            &session->scripts[position],
            (size_t)(session->script_count - position) * sizeof(*session->scripts));
    session->scripts[position] = entry;
    session->script_count++;
    return 1;
}

CS2W_EXPORT int
cs2w_session_seal(struct CS2W_Session* session)
{
    if( !cs2w_session_valid(session) || session->invocation_count != 0 )
        return 0;
    session->sealed = 1;
    return 1;
}

CS2W_EXPORT int
cs2w_session_script_count(const struct CS2W_Session* session)
{
    return cs2w_session_valid(session) ? session->script_count : 0;
}

CS2W_EXPORT int
cs2w_session_last_error(const struct CS2W_Session* session)
{
    return cs2w_session_valid(session) ? session->last_error : CS2W_ERROR_ARGUMENT;
}

CS2W_EXPORT const char*
cs2w_session_last_error_message(const struct CS2W_Session* session)
{
    return cs2w_session_valid(session) ? session->last_error_message : "invalid session";
}

CS2W_EXPORT struct CS2W_Invocation*
cs2w_invocation_create(
    struct CS2W_Session* session,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int canvas_width,
    int canvas_height)
{
    if( !cs2w_session_valid(session) || canvas_width <= 0 || canvas_height <= 0 )
        return NULL;
    struct CS2W_ScriptEntry* entry = cs2w_script_find(session, script_id);
    if( !entry )
    {
        cs2w_session_error(session, CS2W_ERROR_MISSING_SCRIPT, "entry script is not loaded");
        return NULL;
    }
    struct CS2W_Invocation* invocation = (struct CS2W_Invocation*)calloc(1, sizeof(*invocation));
    if( !invocation )
    {
        cs2w_session_error(session, CS2W_ERROR_OUT_OF_MEMORY, "invocation allocation failed");
        return NULL;
    }
    invocation->event.opbase = cs2w_copy_string("");
    if( !invocation->event.opbase )
    {
        free(invocation);
        cs2w_session_error(session, CS2W_ERROR_OUT_OF_MEMORY, "event allocation failed");
        return NULL;
    }
    invocation->magic = CS2W_INVOCATION_MAGIC;
    invocation->session = session;
    invocation->entry = entry;
    invocation->active_component_id = active_component_id;
    invocation->dot_component_id = dot_component_id;
    invocation->canvas_width = canvas_width;
    invocation->canvas_height = canvas_height;
    invocation->status = CS2W_RUN_ERROR;
    invocation->error_opcode = -1;
    invocation->error_pc = -1;
    invocation->error_script_id = script_id;
    invocation->event.component_id = active_component_id;
    invocation->event.component_sub_id = -1;
    invocation->event.drag_target_id = -1;
    invocation->event.drag_target_sub_id = -1;
    invocation->event.key_typed = -1;
    invocation->event.key_pressed = -1;
    invocation->event.window_mode = CS2VM_WINDOW_MODE_FIXED;
    invocation->event.default_window_mode = CS2VM_WINDOW_MODE_FIXED;
    session->invocation_count++;
    return invocation;
}

CS2W_EXPORT int
cs2w_invocation_destroy(struct CS2W_Invocation* invocation)
{
    if( !cs2w_invocation_valid(invocation) )
        return 0;
    struct CS2W_Session* session = invocation->session;
    if( invocation->vm )
        CS2VM2_Release(invocation->vm);
    for( int i = 0; i < invocation->string_count; i++ )
        free(invocation->strings[i]);
    free(invocation->event.opbase);
    cs2w_fast_clear_snapshots(invocation);
    free(invocation->fast_inventories);
    free(invocation->fast_children);
    free(invocation->fast_values);
    free(invocation->fast_scalar_string);
    free(invocation->fast_records);
    free(invocation->fast_arena);
    invocation->magic = 0;
    if( session->invocation_count > 0 )
        session->invocation_count--;
    free(invocation);
    return 1;
}

CS2W_EXPORT int
cs2w_invocation_set_fast_host(
    struct CS2W_Invocation* invocation,
    int enabled)
{
    if( !cs2w_invocation_valid(invocation) || invocation->started )
        return 0;
#ifdef __EMSCRIPTEN__
    invocation->fast_host_enabled = enabled ? 1 : 0;
    return 1;
#else
    invocation->fast_host_enabled = 0;
    return enabled ? 0 : 1;
#endif
}

CS2W_EXPORT int
cs2w_invocation_add_int_arg(
    struct CS2W_Invocation* invocation,
    int value)
{
    if( !cs2w_invocation_valid(invocation) || invocation->started ||
        invocation->int_count >= CS2W_MAX_HOOK_ARGS )
        return 0;
    invocation->ints[invocation->int_count++] = value;
    return 1;
}

CS2W_EXPORT int
cs2w_invocation_add_string_arg(
    struct CS2W_Invocation* invocation,
    const char* value)
{
    if( !cs2w_invocation_valid(invocation) || invocation->started ||
        invocation->string_count >= CS2W_MAX_HOOK_ARGS )
        return 0;
    char* copy = cs2w_copy_string(value);
    if( !copy )
        return 0;
    invocation->strings[invocation->string_count++] = copy;
    return 1;
}

CS2W_EXPORT int
cs2w_invocation_set_event_i32(
    struct CS2W_Invocation* invocation,
    int field,
    int value)
{
    if( !cs2w_invocation_valid(invocation) || invocation->started )
        return 0;
    switch( field )
    {
    case CS2W_EVENT_MOUSE_X: invocation->event.mouse_x = value; break;
    case CS2W_EVENT_MOUSE_Y: invocation->event.mouse_y = value; break;
    case CS2W_EVENT_COMPONENT_ID: invocation->event.component_id = value; break;
    case CS2W_EVENT_COMPONENT_SUB_ID: invocation->event.component_sub_id = value; break;
    case CS2W_EVENT_OP_INDEX: invocation->event.op_index = value; break;
    case CS2W_EVENT_DRAG_TARGET_ID: invocation->event.drag_target_id = value; break;
    case CS2W_EVENT_DRAG_TARGET_SUB_ID: invocation->event.drag_target_sub_id = value; break;
    case CS2W_EVENT_KEY_TYPED: invocation->event.key_typed = value; break;
    case CS2W_EVENT_KEY_PRESSED: invocation->event.key_pressed = value; break;
    case CS2W_EVENT_OP_SUBINDEX: invocation->event.op_subindex = value; break;
    case CS2W_EVENT_WINDOW_MODE: invocation->event.window_mode = value; break;
    case CS2W_EVENT_DEFAULT_WINDOW_MODE: invocation->event.default_window_mode = value; break;
    default: return 0;
    }
    return 1;
}

CS2W_EXPORT int
cs2w_invocation_set_event_string(
    struct CS2W_Invocation* invocation,
    int field,
    const char* value)
{
    if( !cs2w_invocation_valid(invocation) || invocation->started ||
        field != CS2W_EVENT_STRING_OPBASE )
        return 0;
    char* copy = cs2w_copy_string(value);
    if( !copy )
        return 0;
    free(invocation->event.opbase);
    invocation->event.opbase = copy;
    return 1;
}

static int
cs2w_invocation_start(struct CS2W_Invocation* invocation)
{
    invocation->vm = CS2VM2_Acquire();
    if( !invocation->vm )
    {
        cs2w_invocation_error(invocation, CS2W_ERROR_OUT_OF_MEMORY);
        return 0;
    }
    CS2VM2_BindHost(invocation->vm, invocation, cs2w_bridge_host_exec);
    invocation->thread = CS2VM2_ThreadMain(invocation->vm);
    CS2VM2_ThreadSetCanvas(
        invocation->thread,
        invocation->canvas_width,
        invocation->canvas_height);
    CS2VM2_ThreadSetWindowMode(
        invocation->thread,
        invocation->event.window_mode,
        invocation->event.default_window_mode);
    if( CS2VM2_ThreadStart(invocation->thread, &invocation->entry->script) != CS2VM_EXECNO_OK )
    {
        cs2w_invocation_error(invocation, CS2W_ERROR_VM);
        return 0;
    }
    for( int i = 0; i < invocation->int_count; i++ )
    {
        if( CS2VM2_SetIntCurrentFrameLocal(
                invocation->thread,
                i,
                cs2w_resolve_event_int(invocation, invocation->ints[i])) != CS2VM_EXECNO_OK )
        {
            cs2w_invocation_error(invocation, CS2W_ERROR_ARGUMENT);
            return 0;
        }
    }
    for( int i = 0; i < invocation->string_count; i++ )
    {
        const char* value = strcmp(invocation->strings[i], "event_opbase") == 0
                                ? invocation->event.opbase
                                : invocation->strings[i];
        if( CS2VM2_SetStringCurrentFrameLocal(invocation->thread, i, value) != CS2VM_EXECNO_OK )
        {
            cs2w_invocation_error(invocation, CS2W_ERROR_ARGUMENT);
            return 0;
        }
    }
    CS2VM2_SetActiveAndDotComponentId(invocation->thread, invocation->active_component_id);
    CS2VM2_SetTargetComponentId(invocation->thread, 1, invocation->dot_component_id);
    invocation->started = 1;
    return 1;
}

CS2W_EXPORT int
cs2w_invocation_run(struct CS2W_Invocation* invocation)
{
    struct CS2VM2_ThreadError error = { -1, -1, -1 };
    enum CS2VM2_ThreadStatus status;
    if( !cs2w_invocation_valid(invocation) || invocation->terminal )
        return CS2W_RUN_ERROR;
    if( !invocation->started && !cs2w_invocation_start(invocation) )
        return CS2W_RUN_ERROR;
    if( invocation->status == CS2W_RUN_YIELDED )
        CS2VM2_ClearYieldHalt(invocation->thread);
    status = CS2VM2_ThreadRun(invocation->thread, &error);
    if( invocation->fast_host_enabled && cs2w_fast_flush(invocation) != CS2VM_EXECNO_OK )
    {
        invocation->status = CS2W_RUN_ERROR;
        invocation->terminal = 1;
        invocation->last_error = CS2W_ERROR_VM;
        invocation->error_opcode = error.opcode;
        invocation->error_pc = error.pc;
        invocation->error_script_id = error.script_id;
        return invocation->status;
    }
    if( status == CS2VM2_THREAD_DONE )
    {
        invocation->status = CS2W_RUN_DONE;
        invocation->terminal = 1;
        return invocation->status;
    }
    if( status == CS2VM2_THREAD_YIELDED )
    {
        invocation->status = CS2W_RUN_YIELDED;
        return invocation->status;
    }
    invocation->status = CS2W_RUN_ERROR;
    invocation->terminal = 1;
    if( invocation->last_error == CS2W_ERROR_NONE )
        invocation->last_error = CS2W_ERROR_VM;
    invocation->error_opcode = error.opcode;
    invocation->error_pc = error.pc;
    invocation->error_script_id = error.script_id;
    return invocation->status;
}


CS2W_EXPORT int
cs2w_invocation_last_error(const struct CS2W_Invocation* invocation)
{
    return cs2w_invocation_valid(invocation) ? invocation->last_error : CS2W_ERROR_ARGUMENT;
}

CS2W_EXPORT int
cs2w_invocation_error_opcode(const struct CS2W_Invocation* invocation)
{
    return cs2w_invocation_valid(invocation) ? invocation->error_opcode : -1;
}

CS2W_EXPORT int
cs2w_invocation_error_pc(const struct CS2W_Invocation* invocation)
{
    return cs2w_invocation_valid(invocation) ? invocation->error_pc : -1;
}

CS2W_EXPORT int
cs2w_invocation_error_script_id(const struct CS2W_Invocation* invocation)
{
    return cs2w_invocation_valid(invocation) ? invocation->error_script_id : -1;
}

CS2W_EXPORT int
cs2w_invocation_host_call_count(const struct CS2W_Invocation* invocation)
{
    return cs2w_invocation_valid(invocation) ? invocation->host_call_count : 0;
}

CS2W_EXPORT const char*
cs2w_request_kind_name(int kind)
{
    const struct CS2W_RequestDescriptor* request = cs2w_request_descriptor(kind);
    return request ? request->name : NULL;
}

CS2W_EXPORT int
cs2w_request_field_count(int kind)
{
    const struct CS2W_RequestDescriptor* request = cs2w_request_descriptor(kind);
    return request ? request->field_count : 0;
}

CS2W_EXPORT const char*
cs2w_request_field_name(
    int kind,
    int field_index)
{
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(kind, field_index);
    return field ? field->name : NULL;
}

CS2W_EXPORT int
cs2w_request_field_kind(
    int kind,
    int field_index)
{
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(kind, field_index);
    return field ? field->kind : CS2W_FIELD_INVALID;
}

static int
cs2w_public_offset(size_t offset)
{
    return offset == CS2W_NO_OFFSET || offset > (size_t)INT_MAX ? -1 : (int)offset;
}

CS2W_EXPORT int
cs2w_request_field_offset(
    int kind,
    int field_index)
{
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(kind, field_index);
    return field ? cs2w_public_offset(field->offset) : -1;
}

CS2W_EXPORT int
cs2w_request_field_capacity(
    int kind,
    int field_index)
{
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(kind, field_index);
    return field ? field->capacity : -1;
}

CS2W_EXPORT int
cs2w_request_field_stride(
    int kind,
    int field_index)
{
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(kind, field_index);
    return field ? field->stride : -1;
}

CS2W_EXPORT int
cs2w_request_field_count_offset(
    int kind,
    int field_index)
{
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(kind, field_index);
    return field ? cs2w_public_offset(field->count_offset) : -1;
}

CS2W_EXPORT int
cs2w_request_pointer_size(void)
{
    return (int)sizeof(void*);
}

CS2W_EXPORT int
cs2w_request_field_length(
    uintptr_t request_address,
    int field_index)
{
    const struct CS2VM_HostRequest* request =
        (const struct CS2VM_HostRequest*)request_address;
    if( !request )
        return 0;
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(request->kind, field_index);
    return field ? cs2w_field_length(request, field) : 0;
}

CS2W_EXPORT int
cs2w_request_field_i32(
    uintptr_t request_address,
    int field_index,
    int element_index)
{
    const struct CS2VM_HostRequest* request =
        (const struct CS2VM_HostRequest*)request_address;
    int value = 0;
    if( !request )
        return 0;
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(request->kind, field_index);
    if( !field || element_index < 0 || element_index >= cs2w_field_length(request, field) )
        return 0;
    const uint8_t* address = (const uint8_t*)request + field->offset;
    switch( field->kind )
    {
    case CS2W_FIELD_I32:
        memcpy(&value, address, sizeof(value));
        return value;
    case CS2W_FIELD_BOOL:
    {
        bool boolean = false;
        memcpy(&boolean, address, sizeof(boolean));
        return boolean ? 1 : 0;
    }
    case CS2W_FIELD_U8: return (int)*address;
    case CS2W_FIELD_I32_ARRAY:
        memcpy(&value, address + (size_t)element_index * sizeof(value), sizeof(value));
        return value;
    case CS2W_FIELD_I32_POINTER:
    {
        const int* pointer = NULL;
        memcpy(&pointer, address, sizeof(pointer));
        if( pointer )
            memcpy(&value, pointer + element_index, sizeof(value));
        return value;
    }
    case CS2W_FIELD_U64:
    {
        uint64_t wide = 0;
        memcpy(&wide, address, sizeof(wide));
        return (int)(uint32_t)(wide >> (element_index * 32));
    }
    default: return 0;
    }
}

CS2W_EXPORT const char*
cs2w_request_field_string(
    uintptr_t request_address,
    int field_index,
    int element_index)
{
    const struct CS2VM_HostRequest* request =
        (const struct CS2VM_HostRequest*)request_address;
    const char* value = NULL;
    if( !request )
        return NULL;
    const struct CS2W_FieldDescriptor* field = cs2w_request_field(request->kind, field_index);
    if( !field || element_index < 0 || element_index >= cs2w_field_length(request, field) )
        return NULL;
    const uint8_t* address = (const uint8_t*)request + field->offset;
    if( field->kind == CS2W_FIELD_STRING )
    {
        memcpy(&value, address, sizeof(value));
        return value;
    }
    if( field->kind == CS2W_FIELD_STRING_ARRAY )
        return (const char*)(address + (size_t)element_index * (size_t)field->stride);
    return NULL;
}

CS2W_EXPORT int
cs2w_thread_pop_int(
    uintptr_t thread_address,
    int* value_out)
{
    struct CS2VM2_Thread* thread = (struct CS2VM2_Thread*)thread_address;
    int value;
    if( !thread || !value_out || CS2VM2_PopInt(thread, &value) != CS2VM_EXECNO_OK )
        return 0;
    *value_out = value;
    return 1;
}

CS2W_EXPORT int
cs2w_thread_pop_string(
    uintptr_t thread_address,
    const char** value_out)
{
    struct CS2VM2_Thread* thread = (struct CS2VM2_Thread*)thread_address;
    char* value;
    if( !thread || !value_out || CS2VM2_PopStr(thread, &value) != CS2VM_EXECNO_OK )
        return 0;
    *value_out = value;
    return 1;
}

CS2W_EXPORT int
cs2w_thread_push_int(
    uintptr_t thread_address,
    int value)
{
    struct CS2VM2_Thread* thread = (struct CS2VM2_Thread*)thread_address;
    return thread && CS2VM2_PushInt(thread, value) == CS2VM_EXECNO_OK;
}

CS2W_EXPORT int
cs2w_thread_push_string(
    uintptr_t thread_address,
    const char* value)
{
    struct CS2VM2_Thread* thread = (struct CS2VM2_Thread*)thread_address;
    if( !thread )
        return 0;
    char* copy = CS2VM2_StrDup(thread, value ? value : "");
    return copy && CS2VM2_PushStr(thread, copy) == CS2VM_EXECNO_OK;
}

CS2W_EXPORT int
cs2w_thread_set_target(
    uintptr_t thread_address,
    int dot_operand,
    int component_id)
{
    struct CS2VM2_Thread* thread = (struct CS2VM2_Thread*)thread_address;
    if( !thread )
        return 0;
    CS2VM2_SetTargetComponentId(thread, dot_operand ? 1 : 0, component_id);
    return 1;
}

CS2W_EXPORT int
cs2w_thread_set_active_dot(
    uintptr_t thread_address,
    int active_component_id,
    int dot_component_id)
{
    struct CS2VM2_Thread* thread = (struct CS2VM2_Thread*)thread_address;
    if( !thread )
        return 0;
    CS2VM2_SetActiveAndDotComponentId(thread, active_component_id);
    CS2VM2_SetTargetComponentId(thread, 1, dot_component_id);
    return 1;
}

CS2W_EXPORT int
cs2w_thread_set_children(
    uintptr_t thread_address,
    int parent_component_id,
    const int* child_sub_ids,
    int child_count)
{
    struct CS2VM2_Thread* thread = (struct CS2VM2_Thread*)thread_address;
    if( !thread || child_count < 0 || child_count > CS2VM2_CHILDREN_ITER_MAX ||
        (child_count > 0 && !child_sub_ids) )
        return 0;
    if( child_count > 0 )
        memcpy(
            thread->children_iter_indices,
            child_sub_ids,
            (size_t)child_count * sizeof(thread->children_iter_indices[0]));
    thread->children_iter_count = child_count;
    thread->children_iter_index = 0;
    thread->children_iter_parent = parent_component_id;
    return 1;
}

CS2W_EXPORT int
cs2w_thread_active_component(uintptr_t thread_address)
{
    const struct CS2VM2_Thread* thread = (const struct CS2VM2_Thread*)thread_address;
    return thread ? thread->active_component_id : -1;
}

CS2W_EXPORT int
cs2w_thread_dot_component(uintptr_t thread_address)
{
    const struct CS2VM2_Thread* thread = (const struct CS2VM2_Thread*)thread_address;
    return thread ? thread->dot_component_id : -1;
}

CS2W_EXPORT int
cs2w_thread_current_operand(uintptr_t thread_address)
{
    const struct CS2VM2_Thread* thread = (const struct CS2VM2_Thread*)thread_address;
    if( !thread || thread->frame_sp <= 0 )
        return 0;
    const struct CS2VM2_Frame* frame = thread->frames[thread->frame_sp - 1];
    if( !frame || !frame->script || !frame->script->int_operands || frame->pc <= 0 ||
        frame->pc > frame->script->op_count )
        return 0;
    return frame->script->int_operands[frame->pc - 1];
}
