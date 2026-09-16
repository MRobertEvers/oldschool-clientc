/*
 * The array commands that reorder, copy, insert and remove elements:
 * ARRAY_RANDOMISE (8001), ARRAY_ISNULL (8002), ARRAY_MAX (8009),
 * ARRAY_REVERSE (8012), ARRAY_SWAP (8014), ARRAY_COPY (8015),
 * ARRAY_INSERT (8025), ARRAY_DELETE (8026), ARRAY_PUSHALL (8027).
 *
 * Each case runs one opcode through the real dispatch over an array placed in
 * the thread's pool, then reads the array and the stacks back.
 *
 * The expected ARRAY_RANDOMISE orders were printed by the JDK itself --
 * `new java.util.Random((long)seed1 << 32 | seed2)` and the client's
 * top-down `nextInt(i + 1)` swap walk -- not by a second copy of the C
 * algorithm, which would only prove the code agrees with itself.
 *
 * Two layout traps these cover: an int array's cells are indexed with an int
 * stride inside a pointer-wide block, so moving elements as pointers scrambles
 * int arrays; and ARRAY_COPY / ARRAY_PUSHALL onto the same array overlap.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

static void
check_int(int got, int want, char const* label)
{
    if( got == want )
        return;
    printf("  FAIL: %s got %d want %d\n", label, got, want);
    g_fail++;
}

static void
check_str(char const* got, char const* want, char const* label)
{
    if( got && strcmp(got, want) == 0 )
        return;
    printf("  FAIL: %s got \"%s\" want \"%s\"\n", label, got ? got : "(null)", want);
    g_fail++;
}

static int
no_host_exec(struct CS2VM2_Thread* thread, struct CS2VM_HostRequest* request)
{
    (void)thread;
    (void)request;
    printf("  FAIL: an array command reached the host\n");
    g_fail++;
    return CS2VM_EXECNO_ERROR;
}

struct Fixture
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    uint16_t opcodes[2];
    int int_operands[2];
    char* string_operands[2];
};

static void
fixture_begin(struct Fixture* fixture, int opcode)
{
    memset(fixture, 0, sizeof(*fixture));
    CS2VM2_Init(&fixture->vm);
    CS2VM2_BindHost(&fixture->vm, NULL, no_host_exec);
    CS2VM2_ScriptInit(&fixture->script);
    fixture->opcodes[0] = (uint16_t)opcode;
    fixture->opcodes[1] = (uint16_t)CS2_OP_RETURN;
    fixture->script.script_id = 998027;
    fixture->script.op_count = 2;
    fixture->script.opcodes = fixture->opcodes;
    fixture->script.int_operands = fixture->int_operands;
    fixture->script.string_operands = fixture->string_operands;
    fixture->thread = CS2VM2_ThreadMain(&fixture->vm);
    CS2VM2_PushCallScript(fixture->thread, &fixture->script);
}

static char*
fixture_int_array(struct Fixture* fixture, int const* values, int count)
{
    struct CS2VM2_Thread* const thread = fixture->thread;
    struct CS2VM2_Array* const array = &thread->arrays[thread->array_alloc++];
    int const capacity = count + 8;
    array->cells.strings = calloc((size_t)capacity, sizeof(char*));
    assert(array->cells.strings);
    array->capacity = capacity;
    array->size = count;
    array->defined = 1;
    array->is_string = 0;
    for( int i = 0; i < count; i++ )
        array->cells.ints[i] = values[i];
    return (char*)array;
}

static char*
fixture_string_array(struct Fixture* fixture, char** values, int count)
{
    struct CS2VM2_Thread* const thread = fixture->thread;
    struct CS2VM2_Array* const array = &thread->arrays[thread->array_alloc++];
    int const capacity = count + 8;
    array->cells.strings = calloc((size_t)capacity, sizeof(char*));
    assert(array->cells.strings);
    array->capacity = capacity;
    array->size = count;
    array->defined = 1;
    array->is_string = 1;
    for( int i = 0; i < count; i++ )
        array->cells.strings[i] = values[i];
    return (char*)array;
}

static void
push_int(struct Fixture* fixture, int value)
{
    fixture->thread->ints_stack[fixture->thread->ints_stack_top++] = value;
}

static void
push_str(struct Fixture* fixture, char* value)
{
    fixture->thread->strs_stack[fixture->thread->strs_stack_top++] = value;
}

static int
fixture_run(struct Fixture* fixture)
{
    return CS2VM2_RunScript(fixture->thread);
}

static void
check_ints(struct CS2VM2_Array const* array, int const* want, int count, char const* label)
{
    check_int(array->size, count, label);
    for( int i = 0; i < count && i < array->size; i++ )
    {
        if( array->cells.ints[i] != want[i] )
        {
            printf("  FAIL: %s element %d got %d want %d\n", label, i, array->cells.ints[i], want[i]);
            g_fail++;
        }
    }
}

static void
test_randomise_matches_java(void)
{
    struct Case
    {
        int seed1;
        int seed2;
        int count;
        int want[16];
    } const cases[] = {
        { 1, 2, 10, { 60, 0, 90, 50, 20, 30, 80, 10, 70, 40 } },
        /* A negative low seed sign-extends over the high half, as in Java. */
        { -5, -1, 7, { 0, 10, 60, 20, 40, 50, 30 } },
        { 0, 123456789, 16, { 150, 140, 90, 80, 110, 70, 10, 50, 120, 30, 20, 60, 40, 130, 0, 100 } },
    };
    printf("ARRAY_RANDOMISE shuffles exactly as java.util.Random does:\n");
    for( size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++ )
    {
        struct Fixture fixture;
        int values[16];
        for( int i = 0; i < cases[c].count; i++ )
            values[i] = i * 10;
        fixture_begin(&fixture, CS2_OP_ARRAY_RANDOMISE);
        char* const handle = fixture_int_array(&fixture, values, cases[c].count);
        push_str(&fixture, handle);
        push_int(&fixture, cases[c].seed1);
        push_int(&fixture, cases[c].seed2);
        check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "randomise runs");
        check_ints((struct CS2VM2_Array*)handle, cases[c].want, cases[c].count, "randomised order");
        CS2VM2_Free(&fixture.vm);
    }
}

static void
test_isnull(void)
{
    struct Fixture fixture;
    int const values[1] = { 1 };
    static char not_a_handle[] = "x";
    printf("ARRAY_ISNULL:\n");

    fixture_begin(&fixture, CS2_OP_ARRAY_ISNULL);
    push_str(&fixture, fixture_int_array(&fixture, values, 1));
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "isnull runs");
    check_int(fixture.thread->ints_stack[0], 0, "a live array is not null");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_ISNULL);
    push_str(&fixture, not_a_handle);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "isnull runs on a non-array");
    check_int(fixture.thread->ints_stack[0], 1, "a non-array is null");
    CS2VM2_Free(&fixture.vm);
}

static void
test_max(void)
{
    struct Fixture fixture;
    int const values[4] = { 3, 9, 9, 2 };
    static char b[] = "b", c[] = "c", a[] = "a";
    char* strings[3] = { b, c, a };
    printf("ARRAY_MAX:\n");

    fixture_begin(&fixture, CS2_OP_ARRAY_MAX);
    push_str(&fixture, fixture_int_array(&fixture, values, 4));
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "max runs");
    check_int(fixture.thread->ints_stack_top, 1, "an int array answers on the int stack");
    check_int(fixture.thread->ints_stack[0], 9, "int max");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_MAX);
    push_str(&fixture, fixture_int_array(&fixture, values, 0));
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "max of empty runs");
    check_int(fixture.thread->ints_stack[0], -1, "empty int array answers -1");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_MAX);
    push_str(&fixture, fixture_string_array(&fixture, strings, 3));
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "string max runs");
    check_int(fixture.thread->ints_stack_top, 0, "a string array pushes no int");
    check_int(fixture.thread->strs_stack_top, 1, "a string array answers on the string stack");
    check_str(fixture.thread->strs_stack[0], "c", "string max");
    CS2VM2_Free(&fixture.vm);
}

static void
test_reverse_and_swap(void)
{
    struct Fixture fixture;
    int const values[5] = { 1, 2, 3, 4, 5 };
    printf("ARRAY_REVERSE and ARRAY_SWAP:\n");

    fixture_begin(&fixture, CS2_OP_ARRAY_REVERSE);
    char* handle = fixture_int_array(&fixture, values, 5);
    push_str(&fixture, handle);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "reverse runs");
    check_ints((struct CS2VM2_Array*)handle, (int const[]){ 5, 4, 3, 2, 1 }, 5, "reversed");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_SWAP);
    handle = fixture_int_array(&fixture, values, 5);
    push_str(&fixture, handle);
    push_int(&fixture, 0);
    push_int(&fixture, 4);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "swap runs");
    check_ints((struct CS2VM2_Array*)handle, (int const[]){ 5, 2, 3, 4, 1 }, 5, "swapped ends");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_SWAP);
    push_str(&fixture, fixture_int_array(&fixture, values, 5));
    push_int(&fixture, 0);
    push_int(&fixture, 5);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_ERROR, "swap past the end aborts the script");
    CS2VM2_Free(&fixture.vm);
}

static void
test_copy(void)
{
    struct Fixture fixture;
    int const values[5] = { 0, 1, 2, 3, 4 };
    static char s[] = "s";
    char* strings[1] = { s };
    printf("ARRAY_COPY:\n");

    fixture_begin(&fixture, CS2_OP_ARRAY_COPY);
    char* handle = fixture_int_array(&fixture, values, 5);
    push_str(&fixture, handle);
    push_str(&fixture, handle);
    push_int(&fixture, 0);
    push_int(&fixture, 1);
    push_int(&fixture, 3);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "overlapping copy runs");
    check_ints((struct CS2VM2_Array*)handle, (int const[]){ 0, 0, 1, 2, 4 }, 5, "overlap copied as arraycopy");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_COPY);
    char* src = fixture_int_array(&fixture, values, 5);
    char* dst = fixture_int_array(&fixture, (int const[]){ 9, 9, 9, 9 }, 4);
    push_str(&fixture, src);
    push_str(&fixture, dst);
    push_int(&fixture, 2);
    push_int(&fixture, 1);
    push_int(&fixture, -1);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "length -1 copy runs");
    check_ints((struct CS2VM2_Array*)dst, (int const[]){ 9, 2, 3, 4 }, 4, "length -1 takes the rest of src");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_COPY);
    push_str(&fixture, fixture_int_array(&fixture, values, 5));
    push_str(&fixture, fixture_int_array(&fixture, values, 2));
    push_int(&fixture, 0);
    push_int(&fixture, 0);
    push_int(&fixture, 3);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_ERROR, "copy past dst's end aborts");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_COPY);
    push_str(&fixture, fixture_int_array(&fixture, values, 5));
    push_str(&fixture, fixture_string_array(&fixture, strings, 1));
    push_int(&fixture, 0);
    push_int(&fixture, 0);
    push_int(&fixture, 1);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_ERROR, "copy between types aborts");
    CS2VM2_Free(&fixture.vm);
}

static void
test_insert_delete_pushall(void)
{
    struct Fixture fixture;
    int const values[3] = { 1, 2, 3 };
    static char a[] = "a", b[] = "b";
    char* strings[2] = { a, b };
    static char inserted[] = "z";
    printf("ARRAY_INSERT, ARRAY_DELETE and ARRAY_PUSHALL:\n");

    fixture_begin(&fixture, CS2_OP_ARRAY_INSERT);
    char* handle = fixture_int_array(&fixture, values, 3);
    push_str(&fixture, handle);
    push_int(&fixture, 7);
    push_int(&fixture, 0);
    push_int(&fixture, 0);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "int insert runs");
    check_ints((struct CS2VM2_Array*)handle, (int const[]){ 7, 1, 2, 3 }, 4, "inserted at the front");
    check_int(fixture.thread->ints_stack_top, 0, "insert pushes nothing");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_INSERT);
    handle = fixture_string_array(&fixture, strings, 2);
    push_str(&fixture, handle);
    push_str(&fixture, inserted);
    push_int(&fixture, 2);
    push_int(&fixture, 2);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "string insert at size runs");
    check_int(((struct CS2VM2_Array*)handle)->size, 3, "string insert grew the array");
    check_str(((struct CS2VM2_Array*)handle)->cells.strings[2], "z", "string inserted at the end");
    check_int(fixture.thread->strs_stack_top, 0, "string insert consumed its value");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_INSERT);
    push_str(&fixture, fixture_int_array(&fixture, values, 3));
    push_int(&fixture, 7);
    push_int(&fixture, 4);
    push_int(&fixture, 0);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_ERROR, "insert past size aborts");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_DELETE);
    handle = fixture_int_array(&fixture, values, 3);
    push_str(&fixture, handle);
    push_int(&fixture, 1);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "delete runs");
    check_int(fixture.thread->ints_stack_top, 1, "delete pushes the removed int");
    check_int(fixture.thread->ints_stack[0], 2, "removed value");
    check_ints((struct CS2VM2_Array*)handle, (int const[]){ 1, 3 }, 2, "tail shifted down");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_DELETE);
    push_str(&fixture, fixture_string_array(&fixture, strings, 2));
    push_int(&fixture, 0);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "string delete runs");
    check_int(fixture.thread->ints_stack_top, 0, "string delete pushes no int");
    check_str(fixture.thread->strs_stack[0], "a", "removed string");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_DELETE);
    push_str(&fixture, fixture_int_array(&fixture, values, 3));
    push_int(&fixture, 3);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_ERROR, "delete past the end aborts");
    CS2VM2_Free(&fixture.vm);

    fixture_begin(&fixture, CS2_OP_ARRAY_PUSHALL);
    handle = fixture_int_array(&fixture, values, 3);
    push_str(&fixture, handle);
    push_str(&fixture, handle);
    check_int(fixture_run(&fixture), CS2VM_EXECNO_DONE, "pushall onto itself runs");
    check_ints((struct CS2VM2_Array*)handle, (int const[]){ 1, 2, 3, 1, 2, 3 }, 6, "appended its own elements");
    CS2VM2_Free(&fixture.vm);
}

int
main(void)
{
    test_randomise_matches_java();
    test_isnull();
    test_max();
    test_reverse_and_swap();
    test_copy();
    test_insert_delete_pushall();

    if( g_fail )
    {
        printf("array_mutate_ops_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("array_mutate_ops_test: all checks passed\n");
    return 0;
}
