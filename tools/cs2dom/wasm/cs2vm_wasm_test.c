#include "cs2vm_wasm.h"

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"

#include <rscache.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TestScript
{
    int id;
    int op_count;
    const uint16_t* opcodes;
    const int* operands;
    const char* const* strings;
    int int_locals;
    int string_locals;
    int int_args;
    int string_args;
};

static int g_seen_event = 0;
static int g_seen_push = 0;
static int g_seen_gosub = 0;
static int g_seen_nested = 0;
static int g_seen_text = 0;
static int g_seen_find = 0;
static int g_seen_getx = 0;
static int g_seen_children = 0;
static int g_nested = 0;
static int g_model_x_angle = 1900;
static int g_seen_model_get = 0;
static int g_seen_model_set = 0;
static int g_seen_db = 0;
static int g_seen_db_results = 0;

static int
field_index(
    int kind,
    const char* name)
{
    int count = cs2w_request_field_count(kind);
    for( int i = 0; i < count; i++ )
    {
        const char* candidate = cs2w_request_field_name(kind, i);
        if( candidate && strcmp(candidate, name) == 0 )
            return i;
    }
    return -1;
}

static int
field_i32(
    uintptr_t request,
    int kind,
    const char* name)
{
    int field = field_index(kind, name);
    assert(field >= 0);
    return cs2w_request_field_i32(request, field, 0);
}

static const char*
field_string(
    uintptr_t request,
    int kind,
    const char* name)
{
    int field = field_index(kind, name);
    assert(field >= 0);
    return cs2w_request_field_string(request, field, 0);
}

static int
pop_int(
    uintptr_t thread)
{
    int value = 0x13572468;
    assert(cs2w_thread_pop_int(thread, &value));
    return value;
}

static const char*
pop_string(
    uintptr_t thread)
{
    const char* value = NULL;
    assert(cs2w_thread_pop_string(thread, &value));
    return value;
}

static void
assert_thread_stacks_empty(uintptr_t thread)
{
    int integer = 0x13572468;
    const char* string = "unchanged";
    assert(!cs2w_thread_pop_int(thread, &integer));
    assert(integer == 0x13572468);
    assert(!cs2w_thread_pop_string(thread, &string));
    assert(strcmp(string, "unchanged") == 0);
    assert(!cs2w_thread_pop_int(0, &integer));
    assert(!cs2w_thread_pop_int(thread, NULL));
    assert(!cs2w_thread_pop_string(0, &string));
    assert(!cs2w_thread_pop_string(thread, NULL));
}

static int
host_exec(
    uintptr_t session,
    uintptr_t invocation,
    uintptr_t thread,
    uintptr_t request,
    int kind)
{
    (void)invocation;
    const char* name = cs2w_request_kind_name(kind);
    assert(name);

    if( strcmp(name, "PUSH_VAR") == 0 )
    {
        assert(field_i32(request, kind, "varp_id") == 7);
        assert(cs2w_thread_push_int(thread, 77));
        g_seen_push++;
        return CS2W_HOST_OK;
    }
    if( strcmp(name, "POP_VAR") == 0 )
    {
        int id = field_i32(request, kind, "varp_id");
        int value = field_i32(request, kind, "value");
        if( id == 99 )
        {
            assert(value == 42);
            g_seen_event++;
            if( !g_nested )
            {
                g_nested = 1;
                struct CS2W_Invocation* nested = cs2w_invocation_create(
                    (struct CS2W_Session*)session,
                    300,
                    0x4444,
                    0x4444,
                    512,
                    334);
                assert(nested);
                assert(cs2w_invocation_run(nested) == CS2W_RUN_DONE);
                assert(cs2w_invocation_destroy(nested));
            }
        }
        else if( id == 98 )
        {
            assert(value == 77);
            g_seen_push++;
        }
        else if( id == 97 )
        {
            assert(value == 1);
            g_seen_find++;
        }
        else if( id == 96 )
        {
            assert(value == 12);
            g_seen_getx++;
        }
        else if( id == 95 )
        {
            assert(value == 2);
            g_seen_children++;
        }
        else if( id == 94 )
        {
            assert(value == 3);
            g_seen_children++;
        }
        else if( id == 93 )
        {
            assert(value == 88);
            g_seen_db_results++;
        }
        else if( id == 92 )
        {
            assert(value == 6);
            g_seen_db_results++;
        }
        else if( id == 91 )
        {
            assert(value == 4);
            g_seen_db_results++;
        }
        else if( id == 90 )
        {
            assert(value == 3);
            g_seen_db_results++;
        }
        else if( id == 89 )
        {
            assert(value == 99);
            g_seen_db_results++;
        }
        else if( id == 88 )
        {
            assert(value == 10);
            g_seen_db_results++;
        }
        else if( id == 87 )
        {
            assert(value == 2);
            g_seen_db_results++;
        }
        else if( id == 86 )
        {
            assert(value == 701);
            g_seen_db_results++;
        }
        else if( id == 200 )
        {
            assert(value == 5);
            g_seen_gosub++;
        }
        else if( id == 300 )
        {
            assert(value == 300);
            g_seen_nested++;
        }
        else
            assert(!"unexpected POP_VAR id");
        return CS2W_HOST_OK;
    }
    if( strcmp(name, "CC_SETTEXT") == 0 )
    {
        int component_id = field_i32(request, kind, "component_id");
        if( component_id == 0x6666 )
        {
            assert(strcmp(field_string(request, kind, "text"), "db-value") == 0);
            assert(cs2w_thread_current_operand(thread) == 0);
            g_seen_db_results++;
        }
        else
        {
            assert(component_id == 0x2222);
            assert(strcmp(field_string(request, kind, "text"), "Use") == 0);
            assert(cs2w_thread_current_operand(thread) == 1);
            g_seen_text++;
        }
        return CS2W_HOST_OK;
    }
    if( strcmp(name, "CC_FIND") == 0 )
    {
        assert(field_i32(request, kind, "parent_id") == 123);
        assert(field_i32(request, kind, "sub_id") == 4);
        assert(field_i32(request, kind, "dot_operand") == 1);
        assert(cs2w_thread_set_target(thread, 1, 0x3333));
        assert(cs2w_thread_push_int(thread, 1));
        return CS2W_HOST_OK;
    }
    if( strcmp(name, "CC_GETX") == 0 )
    {
        assert(field_i32(request, kind, "component_id") == 0x3333);
        assert(cs2w_thread_current_operand(thread) == 1);
        assert(cs2w_thread_push_int(thread, 12));
        return CS2W_HOST_OK;
    }
    if( strcmp(name, "CC_CHILDREN_FIND_COUNT") == 0 )
    {
        const int children[] = { 3, 7 };
        int parent = field_i32(request, kind, "parent_id");
        assert(parent == 0x1111);
        assert(cs2w_thread_set_children(thread, parent, children, 2));
        return CS2W_HOST_OK;
    }
    if( strcmp(name, "IF_GETMODELANGLE_X") == 0 )
    {
        assert(field_i32(request, kind, "component_id") == 0x5555);
        assert(cs2w_thread_push_int(thread, g_model_x_angle));
        g_seen_model_get++;
        return CS2W_HOST_OK;
    }
    if( strcmp(name, "IF_SETMODELANGLE") == 0 )
    {
        assert(field_i32(request, kind, "component_id") == 0x5555);
        assert(field_i32(request, kind, "offset_x") == -3);
        assert(field_i32(request, kind, "offset_y") == 4);
        assert(field_i32(request, kind, "angle_x") == g_model_x_angle + 12);
        assert(field_i32(request, kind, "angle_y") == 77);
        assert(field_i32(request, kind, "angle_z") == 88);
        assert(field_i32(request, kind, "zoom") == 420);
        g_model_x_angle = field_i32(request, kind, "angle_x");
        g_seen_model_set++;
        return CS2W_HOST_OK;
    }
    if( strncmp(name, "DB_", 3) == 0 )
    {
        if( strcmp(name, "DB_GETFIELD") == 0 )
        {
            assert(pop_int(thread) == 2);       /* index */
            assert(pop_int(thread) == 777);     /* packed column */
            assert(pop_int(thread) == 321);     /* row id */
            assert_thread_stacks_empty(thread);
            assert(cs2w_thread_push_int(thread, 88));
            assert(cs2w_thread_push_string(thread, "db-value"));
        }
        else if( strcmp(name, "DB_GETFIELDCOUNT") == 0 )
        {
            assert(pop_int(thread) == 777);     /* packed column */
            assert(pop_int(thread) == 321);     /* row id */
            assert_thread_stacks_empty(thread);
            assert(cs2w_thread_push_int(thread, 3));
        }
        else if( strcmp(name, "DB_FIND_WITH_COUNT") == 0 )
        {
            assert(pop_int(thread) == 0);       /* int type tag */
            assert(pop_int(thread) == 55);      /* value */
            assert(pop_int(thread) == 440);     /* packed column */
            assert_thread_stacks_empty(thread);
            assert(cs2w_thread_push_int(thread, 6));
        }
        else if( strcmp(name, "DB_FIND_FILTER_WITH_COUNT") == 0 )
        {
            assert(pop_int(thread) == 2);       /* string type tag */
            assert(strcmp(pop_string(thread), "needle") == 0);
            assert(pop_int(thread) == 441);     /* packed column */
            assert_thread_stacks_empty(thread);
            assert(cs2w_thread_push_int(thread, 4));
        }
        else if( strcmp(name, "DB_FIND") == 0 )
        {
            assert(pop_int(thread) == 1);       /* any non-2 tag is int */
            assert(pop_int(thread) == 56);
            assert(pop_int(thread) == 442);
            assert_thread_stacks_empty(thread);
        }
        else if( strcmp(name, "DB_FIND_FILTER") == 0 )
        {
            assert(pop_int(thread) == 2);
            assert(strcmp(pop_string(thread), "narrow") == 0);
            assert(pop_int(thread) == 443);
            assert_thread_stacks_empty(thread);
        }
        else if( strcmp(name, "DB_FINDALL_WITH_COUNT") == 0 )
        {
            assert(pop_int(thread) == 10);      /* table id */
            assert_thread_stacks_empty(thread);
            assert(cs2w_thread_push_int(thread, 2));
        }
        else if( strcmp(name, "DB_GETROWTABLE") == 0 )
        {
            assert(pop_int(thread) == 321);     /* row id */
            assert_thread_stacks_empty(thread);
            assert(cs2w_thread_push_int(thread, 10));
        }
        else if( strcmp(name, "DB_GETROW") == 0 )
        {
            assert(pop_int(thread) == 4);       /* query index */
            assert_thread_stacks_empty(thread);
            assert(cs2w_thread_push_int(thread, 99));
        }
        else if( strcmp(name, "DB_FINDALL") == 0 )
        {
            assert(pop_int(thread) == 11);      /* table id */
            assert_thread_stacks_empty(thread);
        }
        else if( strcmp(name, "DB_FINDNEXT") == 0 )
        {
            assert_thread_stacks_empty(thread);
            assert(cs2w_thread_push_int(thread, 701));
        }
        else
            assert(!"unexpected DB request");
        g_seen_db++;
        return CS2W_HOST_OK;
    }
    fprintf(stderr, "unexpected HOST request %s (%d)\n", name, kind);
    return CS2W_HOST_ERROR;
}

static uint8_t*
encode_script(
    const struct TestScript* fixture,
    int* size_out)
{
    struct RSCache_ClientScript script;
    memset(&script, 0, sizeof(script));
    RSCache_CS2_ScriptInit(&script.script);
    script.script.script_id = fixture->id;
    script.script.signature = strdup("");
    script.script.op_count = fixture->op_count;
    script.script.local_int_count = fixture->int_locals;
    script.script.local_string_count = fixture->string_locals;
    script.script.int_argument_count = fixture->int_args;
    script.script.string_argument_count = fixture->string_args;
    script.script.opcodes = (uint16_t*)calloc((size_t)fixture->op_count, sizeof(uint16_t));
    script.script.int_operands = (int*)calloc((size_t)fixture->op_count, sizeof(int));
    script.script.long_operands = (int64_t*)calloc((size_t)fixture->op_count, sizeof(int64_t));
    script.script.string_operands = (char**)calloc((size_t)fixture->op_count, sizeof(char*));
    assert(script.script.signature && script.script.opcodes && script.script.int_operands &&
           script.script.long_operands && script.script.string_operands);
    for( int i = 0; i < fixture->op_count; i++ )
    {
        script.script.opcodes[i] = fixture->opcodes[i];
        script.script.int_operands[i] = fixture->operands ? fixture->operands[i] : 0;
        if( fixture->strings && fixture->strings[i] )
            script.script.string_operands[i] = strdup(fixture->strings[i]);
    }
    uint32_t capacity = RSCache_ClientScriptEncodeBound(&script);
    uint8_t* bytes = (uint8_t*)malloc(capacity);
    assert(bytes);
    uint32_t size = RSCache_ClientScriptEncodeFlags(
        &script,
        RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN,
        bytes,
        capacity);
    assert(size > 0 && size <= capacity);
    RSCache_ClientScriptFreeInplace(&script);
    *size_out = (int)size;
    return bytes;
}

static void
load_script(
    struct CS2W_Session* session,
    const struct TestScript* fixture)
{
    int size = 0;
    uint8_t* bytes = encode_script(fixture, &size);
    if( !cs2w_session_load_script(session, fixture->id, bytes, size) )
        fprintf(stderr, "load failed: %s\n", cs2w_session_last_error_message(session));
    assert(cs2w_session_last_error(session) == CS2W_ERROR_NONE);
    free(bytes);
}

static void
test_schema(void)
{
    struct CS2VM_HostRequest request;
    int triggers[] = { 11, 22, 33 };
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT;
    request.u.IF_SETONVARTRANSMIT.component_id = 0x12345678;
    request.u.IF_SETONVARTRANSMIT.script_id = 404;
    request.u.IF_SETONVARTRANSMIT.signature = "isY";
    request.u.IF_SETONVARTRANSMIT.trigger_ids = triggers;
    request.u.IF_SETONVARTRANSMIT.trigger_count = 3;
    request.u.IF_SETONVARTRANSMIT.int_args[0] = 9;
    request.u.IF_SETONVARTRANSMIT.int_arg_count = 1;
    request.u.IF_SETONVARTRANSMIT.str_arg_mask = UINT64_C(1) << 40;
    request.u.IF_SETONVARTRANSMIT.str_arg_count = 1;
    strcpy(request.u.IF_SETONVARTRANSMIT.str_args[0], "hello");

    int kind = request.kind;
    assert(strcmp(cs2w_request_kind_name(kind), "IF_SETONVARTRANSMIT") == 0);
    assert(field_i32((uintptr_t)&request, kind, "component_id") == 0x12345678);
    int trigger_field = field_index(kind, "trigger_ids");
    assert(cs2w_request_field_kind(kind, trigger_field) == CS2W_FIELD_I32_POINTER);
    assert(cs2w_request_field_length((uintptr_t)&request, trigger_field) == 3);
    assert(cs2w_request_field_i32((uintptr_t)&request, trigger_field, 2) == 33);
    int mask_field = field_index(kind, "str_arg_mask");
    assert(cs2w_request_field_i32((uintptr_t)&request, mask_field, 0) == 0);
    assert(cs2w_request_field_i32((uintptr_t)&request, mask_field, 1) == 256);
    int strings_field = field_index(kind, "str_args");
    assert(cs2w_request_field_length((uintptr_t)&request, strings_field) == 1);
    assert(strcmp(
               cs2w_request_field_string((uintptr_t)&request, strings_field, 0),
               "hello") == 0);

    assert(strcmp(cs2w_request_kind_name(CS2_OP_CC_GETMODELZOOM), "CC_GETMODELZOOM") == 0);
    assert(strcmp(
               cs2w_request_kind_name(CS2_OP_CC_GETMODELTRANSPARENT),
               "CC_GETMODELTRANSPARENT") == 0);
    assert(strcmp(
               cs2w_request_kind_name(CS2_OP_IF_GETMODELANGLE_X),
               "IF_GETMODELANGLE_X") == 0);
    assert(strcmp(
               cs2w_request_kind_name(CS2_OP_IF_GETMODELTRANSPARENT),
               "IF_GETMODELTRANSPARENT") == 0);

    static const struct
    {
        int kind;
        const char* name;
    } widget_getters[] = {
        { CS2_OP_CC_GETLAYER, "CC_GETLAYER" },
        { CS2_OP_CC_GETSCROLLX, "CC_GETSCROLLX" },
        { CS2_OP_CC_GETSCROLLY, "CC_GETSCROLLY" },
        { CS2_OP_CC_GETSCROLLWIDTH, "CC_GETSCROLLWIDTH" },
        { CS2_OP_CC_GETSCROLLHEIGHT, "CC_GETSCROLLHEIGHT" },
        { CS2_OP_CC_GETBLENDTRANS, "CC_GETBLENDTRANS" },
        { CS2_OP_CC_GETARCSTART, "CC_GETARCSTART" },
        { CS2_OP_CC_GETARCEND, "CC_GETARCEND" },
        { CS2_OP_CC_GETOPBASE, "CC_GETOPBASE" },
        { CS2_OP_IF_GETTRANS, "IF_GETTRANS" },
        { CS2_OP_IF_GETOPBASE, "IF_GETOPBASE" },
    };
    for( size_t i = 0; i < sizeof(widget_getters) / sizeof(widget_getters[0]); i++ )
    {
        int getter_kind = widget_getters[i].kind;
        assert(strcmp(cs2w_request_kind_name(getter_kind), widget_getters[i].name) == 0);
        assert(cs2w_request_field_count(getter_kind) == 1);
        int component_field = field_index(getter_kind, "component_id");
        assert(component_field == 0);
        assert(cs2w_request_field_kind(getter_kind, component_field) == CS2W_FIELD_I32);
    }
}

int
main(void)
{
    static const uint16_t entry_ops[] = {
        CS2_OP_PUSH_INT_LOCAL,
        CS2_OP_POP_VAR,
        CS2_OP_PUSH_VAR,
        CS2_OP_POP_VAR,
        CS2_OP_PUSH_STRING_LOCAL,
        CS2_OP_CC_SETTEXT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_CC_FIND,
        CS2_OP_POP_VAR,
        CS2_OP_CC_GETX,
        CS2_OP_POP_VAR,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_GOSUB_WITH_PARAMS,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_CC_CHILDREN_FIND_COUNT,
        CS2_OP_POP_VAR,
        CS2_OP_CC_CHILDREN_FINDNEXTID,
        CS2_OP_POP_VAR,
        CS2_OP_RETURN,
    };
    static const int entry_operands[] = {
        0, 99, 7, 98, 0, 1, 123, 4, 1, 97, 1, 96, 5, 200,
        0, 0, 95, 0, 94, 0,
    };
    static const uint16_t gosub_ops[] = {
        CS2_OP_PUSH_INT_LOCAL, CS2_OP_POP_VAR, CS2_OP_RETURN,
    };
    static const int gosub_operands[] = { 0, 200, 0 };
    static const uint16_t nested_ops[] = {
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_POP_VAR, CS2_OP_RETURN,
    };
    static const int nested_operands[] = { 300, 300, 0 };
    static const uint16_t model_tick_ops[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_IF_GETMODELANGLE_X,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_ADD,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_IF_SETMODELANGLE,
        CS2_OP_RETURN,
    };
    static const int model_tick_operands[] = {
        -3, 4, 0x5555, 0, 12, 0, 77, 88, 420, 0x5555, 0, 0,
    };
    static const uint16_t db_ops[] = {
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DB_GETFIELD, CS2_OP_POP_VAR, CS2_OP_CC_SETTEXT,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DB_GETFIELDCOUNT, CS2_OP_POP_VAR,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DB_FIND_WITH_COUNT, CS2_OP_POP_VAR,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_STRING, CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DB_FIND_FILTER_WITH_COUNT, CS2_OP_POP_VAR,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DB_FIND,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_STRING, CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DB_FIND_FILTER,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_DB_FINDALL_WITH_COUNT, CS2_OP_POP_VAR,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_DB_GETROWTABLE, CS2_OP_POP_VAR,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_DB_GETROW, CS2_OP_POP_VAR,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_DB_FINDALL,
        CS2_OP_DB_FINDNEXT, CS2_OP_POP_VAR, CS2_OP_RETURN,
    };
    static const int db_operands[] = {
        321, 777, 2, 0, 93, 0,
        321, 777, 0, 90,
        440, 55, 0, 0, 92,
        441, 0, 2, 0, 91,
        442, 56, 1, 0,
        443, 0, 2, 0,
        10, 0, 87,
        321, 0, 88,
        4, 0, 89,
        11, 0,
        0, 86, 0,
    };
    static const char* const db_strings[] = {
        NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL,
        NULL, "needle", NULL, NULL, NULL,
        NULL, NULL, NULL, NULL,
        NULL, "narrow", NULL, NULL,
        NULL, NULL, NULL,
        NULL, NULL, NULL,
        NULL, NULL, NULL,
        NULL, NULL,
        NULL, NULL, NULL,
    };
    const struct TestScript scripts[] = {
        { 100, (int)(sizeof(entry_ops) / sizeof(entry_ops[0])), entry_ops, entry_operands,
          NULL, 1, 1, 1, 1 },
        { 200, (int)(sizeof(gosub_ops) / sizeof(gosub_ops[0])), gosub_ops, gosub_operands,
          NULL, 1, 0, 1, 0 },
        { 300, (int)(sizeof(nested_ops) / sizeof(nested_ops[0])), nested_ops, nested_operands,
          NULL, 0, 0, 0, 0 },
        { 400, (int)(sizeof(model_tick_ops) / sizeof(model_tick_ops[0])),
          model_tick_ops, model_tick_operands, NULL, 0, 0, 0, 0 },
        { 500, (int)(sizeof(db_ops) / sizeof(db_ops[0])),
          db_ops, db_operands, db_strings, 0, 0, 0, 0 },
    };

    test_schema();
    cs2w_set_native_host_exec(host_exec);
    struct CS2W_Session* session = cs2w_session_create(CS2W_DIALECT_CANONICAL, 239);
    assert(session);
    for( size_t i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i++ )
        load_script(session, &scripts[i]);
    assert(cs2w_session_script_count(session) == 5);
    assert(cs2w_session_seal(session));

    struct CS2W_Invocation* invocation =
        cs2w_invocation_create(session, 100, 0x1111, 0x2222, 512, 334);
    assert(invocation);
    assert(cs2w_invocation_add_int_arg(invocation, CS2VM_SCRIPT_ARG_MOUSE_X));
    assert(cs2w_invocation_add_string_arg(invocation, "event_opbase"));
    assert(cs2w_invocation_set_event_i32(invocation, CS2W_EVENT_MOUSE_X, 42));
    assert(cs2w_invocation_set_event_string(invocation, CS2W_EVENT_STRING_OPBASE, "Use"));
    assert(cs2w_invocation_run(invocation) == CS2W_RUN_DONE);
    /* Twelve JavaScript HOST calls plus one registry-resolved GOSUB request. */
    assert(cs2w_invocation_host_call_count(invocation) == 13);
    assert(cs2w_invocation_last_error(invocation) == CS2W_ERROR_NONE);
    assert(cs2w_invocation_destroy(invocation));

    assert(g_seen_event == 1);
    assert(g_seen_push == 2);
    assert(g_seen_gosub == 1);
    assert(g_seen_nested == 1);
    assert(g_seen_text == 1);
    assert(g_seen_find == 1);
    assert(g_seen_getx == 1);
    assert(g_seen_children == 2);

    for( int tick = 0; tick < 2; tick++ )
    {
        invocation = cs2w_invocation_create(session, 400, 0x5555, 0x5555, 512, 334);
        assert(invocation);
        assert(cs2w_invocation_run(invocation) == CS2W_RUN_DONE);
        assert(cs2w_invocation_host_call_count(invocation) == 2);
        assert(cs2w_invocation_destroy(invocation));
    }
    assert(g_seen_model_get == 2);
    assert(g_seen_model_set == 2);
    assert(g_model_x_angle == 1924);

    invocation = cs2w_invocation_create(session, 500, 0x6666, 0x6666, 512, 334);
    assert(invocation);
    assert(cs2w_invocation_run(invocation) == CS2W_RUN_DONE);
    assert(cs2w_invocation_host_call_count(invocation) == 20);
    assert(cs2w_invocation_destroy(invocation));
    assert(g_seen_db == 11);
    assert(g_seen_db_results == 9);

    assert(cs2w_session_destroy(session));
    CS2VM2_PoolDrain();
    puts("cs2vm wasm bridge: ok");
    return 0;
}
