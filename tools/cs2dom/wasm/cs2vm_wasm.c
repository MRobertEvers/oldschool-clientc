#include "cs2vm_wasm.h"

#include "cs2vm2/cs2vm2.h"
#include "engine/cs2_opcode_dialect.h"
#include "engine/cs2vm2_script_from_rscache.h"

#include <rscache.h>

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
    invocation->magic = 0;
    if( session->invocation_count > 0 )
        session->invocation_count--;
    free(invocation);
    return 1;
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
