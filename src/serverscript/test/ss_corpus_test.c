/*
 * The reader against LostCity's real compiled corpus.
 *
 * ~9,300 scripts and 5 MB of bytecode is the strongest evidence available that
 * the decoder is right, and it costs nothing to run. Everything asserted here
 * is self-consistency rather than a measured constant: the corpus is rebuilt
 * whenever the reference server's content changes, so a hardcoded byte count
 * or script id would start failing for reasons that have nothing to do with
 * this code. Aggregates are printed instead, so drift is visible without being
 * a failure.
 *
 * The load is SKIPped when the reference server is not checked out beside this
 * repo, matching src/game/test/db_cache_test.c.
 *
 *   SS_CORPUS=/path/to/LostCity_Server make -C src test-ss-corpus
 */

#include "ss_meta.h"
#include <assert.h>
#include "ssvm_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do                                                                         \
    {                                                                          \
        if( cond )                                                             \
        {                                                                      \
            printf("  ok   %s\n", (msg));                                      \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__);          \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

#define DEFAULT_CORPUS "../LostCity_Server"

/* ------------------------------------------------------------------ */

/**
 * script.pack is the compiler's own id -> name register, one `id=name` per
 * line. Comparing it against every decoded script's embedded name checks the
 * header decode and the container's entry ordering at the same time, against a
 * source this reader never touches.
 */
static char**
load_script_pack(const char* path, int* out_count)
{
    FILE* file;
    char line[512];
    char** names = NULL;
    int capacity = 0;
    int highest = -1;

    *out_count = 0;

    file = fopen(path, "rb");
    if( !file )
        return NULL;

    while( fgets(line, sizeof(line), file) )
    {
        char* eq;
        int id;
        size_t len;

        eq = strchr(line, '=');
        if( !eq )
            continue;
        *eq = '\0';
        id = atoi(line);
        if( id < 0 )
            continue;

        len = strlen(eq + 1);
        while( len && (eq[len] == '\n' || eq[len] == '\r') )
            eq[len--] = '\0';

        if( id >= capacity )
        {
            int grown = id + 1024;
            char** bigger = (char**)realloc(names, (size_t)grown * sizeof(char*));

            assert(bigger);
            memset(bigger + capacity, 0, (size_t)(grown - capacity) * sizeof(char*));
            names = bigger;
            capacity = grown;
        }
        names[id] = strdup(eq + 1);
        if( id > highest )
            highest = id;
    }
    fclose(file);

    *out_count = highest + 1;
    return names;
}

/* ------------------------------------------------------------------ */

static void
report_aggregates(const struct SSVM_Provider* provider)
{
    long total_ops = 0;
    long total_strings = 0;
    int max_ops = 0;
    int max_switch_tables = 0;
    int max_cases = 0;
    int max_locals = 0;
    int max_args = 0;
    int max_line_rows = 0;
    size_t max_string = 0;
    int distinct = 0;
    int named_only = 0;
    int32_t i;
    static unsigned char seen[SS_OPCODE_MAX];

    memset(seen, 0, sizeof(seen));

    for( i = 0; i < provider->count; i++ )
    {
        const struct SSVM_Script* script = &provider->scripts[i];
        int op;
        int table;

        if( script->op_count == 0 )
            continue;

        total_ops += script->op_count;
        if( script->op_count > max_ops )
            max_ops = script->op_count;
        if( script->lookup_key < 0 )
            named_only++;
        if( script->switch_table_count > max_switch_tables )
            max_switch_tables = script->switch_table_count;
        if( script->line_count > max_line_rows )
            max_line_rows = script->line_count;
        if( script->int_local_count + script->string_local_count > max_locals )
            max_locals = script->int_local_count + script->string_local_count;
        if( script->int_arg_count + script->string_arg_count > max_args )
            max_args = script->int_arg_count + script->string_arg_count;

        for( table = 0; table < script->switch_table_count; table++ )
        {
            if( script->switch_tables[table].case_count > max_cases )
                max_cases = script->switch_tables[table].case_count;
        }

        for( op = 0; op < script->op_count; op++ )
        {
            int opcode = script->opcodes[op];

            if( opcode < SS_OPCODE_MAX && !seen[opcode] )
            {
                seen[opcode] = 1;
                distinct++;
            }
            if( script->string_operands[op] )
            {
                size_t len = strlen(script->string_operands[op]);

                total_strings++;
                if( len > max_string )
                    max_string = len;
            }
        }
    }

    /* Printed, never asserted: the corpus is regenerated whenever the reference
     * server's content changes, so these move for reasons unrelated to the
     * reader. They are here so a surprising change is visible. */
    printf("  corpus  %d slots, %d scripts, %d name-addressed\n", provider->count,
           provider->loaded, named_only);
    printf("  ops     %ld total, %d distinct opcodes, max %d per script\n", total_ops,
           distinct, max_ops);
    printf("  strings %ld operands, longest %zu bytes\n", total_strings, max_string);
    printf("  maxima  %d switch tables, %d cases, %d locals, %d args, %d line rows\n",
           max_switch_tables, max_cases, max_locals, max_args, max_line_rows);
}

static void
test_structure(const struct SSVM_Provider* provider)
{
    int32_t i;
    int bad_trailing_return = 0;
    int bad_switch_delta = 0;
    int bad_branch_target = 0;
    int unknown_opcodes = 0;

    printf("script structure\n");

    for( i = 0; i < provider->count; i++ )
    {
        const struct SSVM_Script* script = &provider->scripts[i];
        int op;
        int table;

        if( script->op_count == 0 )
            continue;

        /* The reference's interpreter errors rather than finishing when pc runs
         * past the last op, so every compiled script has to end in RETURN or it
         * could never complete. */
        if( script->opcodes[script->op_count - 1] != SS_OP_RETURN )
            bad_trailing_return++;

        for( op = 0; op < script->op_count; op++ )
        {
            int opcode = script->opcodes[op];

            if( !SSVM_OpcodeMetaKnown(opcode) )
                unknown_opcodes++;

            /* Branch operands are instruction-index deltas, and the VM applies
             * pc += delta then ++pc, so the landing site is op + delta + 1.
             * Reading them as byte offsets works on a hand-built five-op test
             * and fails on every real script — this is where that shows up. */
            switch( opcode )
            {
            case SS_OP_BRANCH:
            case SS_OP_BRANCH_NOT:
            case SS_OP_BRANCH_EQUALS:
            case SS_OP_BRANCH_LESS_THAN:
            case SS_OP_BRANCH_GREATER_THAN:
            case SS_OP_BRANCH_LESS_THAN_OR_EQUALS:
            case SS_OP_BRANCH_GREATER_THAN_OR_EQUALS:
            {
                long target = (long)op + script->int_operands[op] + 1;

                if( target < 0 || target >= script->op_count )
                    bad_branch_target++;
                break;
            }
            default:
                break;
            }
        }

        for( table = 0; table < script->switch_table_count; table++ )
        {
            const struct SSVM_SwitchTable* t = &script->switch_tables[table];
            int c;

            for( c = 0; c < t->case_count; c++ )
            {
                /* The reference treats delta 0 as "no such case" (`if (result)
                 * state.pc += result`), so a real table must never contain one
                 * or that case would silently fall through. */
                if( t->cases[c].delta == 0 )
                    bad_switch_delta++;
            }
        }
    }

    CHECK(bad_trailing_return == 0, "every script ends in RETURN");
    CHECK(bad_branch_target == 0, "every branch delta lands inside its script");
    CHECK(bad_switch_delta == 0, "no switch case carries the sentinel delta 0");

    /* LC_OP / OC_IOP / OC_OP have ids but no handler in the reference; if the
     * corpus never emits them, nothing is lost by refusing to execute them. */
    if( unknown_opcodes )
        printf("  note   %d ops in the corpus have no known signature\n", unknown_opcodes);
    CHECK(unknown_opcodes == 0, "every opcode the corpus uses has a known signature");
}

static void
test_names(const struct SSVM_Provider* provider, const char* corpus)
{
    char path[1024];
    char** pack;
    int pack_count = 0;
    int compared = 0;
    int mismatched = 0;
    int32_t i;

    printf("names against script.pack\n");

    snprintf(path, sizeof(path), "%s/content/pack/script.pack", corpus);
    pack = load_script_pack(path, &pack_count);
    if( !pack )
    {
        printf("  SKIP  %s not readable\n", path);
        return;
    }

    for( i = 0; i < provider->count && i < pack_count; i++ )
    {
        const struct SSVM_Script* script = &provider->scripts[i];

        if( script->op_count == 0 || !script->name || !pack[i] )
            continue;
        compared++;
        if( strcmp(script->name, pack[i]) != 0 )
        {
            if( mismatched < 5 )
                printf("  id %d: decoded \"%s\", pack says \"%s\"\n", (int)i, script->name,
                       pack[i]);
            mismatched++;
        }
    }

    printf("  compared %d names\n", compared);
    CHECK(compared > 0, "script.pack supplied names to compare");
    CHECK(mismatched == 0, "every decoded name matches script.pack");

    for( i = 0; i < pack_count; i++ )
        free(pack[i]);
    free(pack);
}

static void
test_indices(const struct SSVM_Provider* provider)
{
    const struct SSVM_Script* script;
    int32_t i;
    int name_roundtrip_failures = 0;

    printf("indices\n");

    /* Every script must be findable by the exact name it carries. */
    for( i = 0; i < provider->count; i++ )
    {
        const struct SSVM_Script* original = &provider->scripts[i];

        if( original->op_count == 0 || !original->name )
            continue;
        if( SSVM_ProviderGetByName(provider, original->name) == NULL )
            name_roundtrip_failures++;
    }
    CHECK(name_roundtrip_failures == 0, "every script resolves by its own name");

    CHECK(SSVM_ProviderGetByName(provider, "[proc,definitely_not_a_script]") == NULL,
          "an unknown name resolves to nothing");

    /* Name-addressed scripts must be absent from the trigger index. The
     * reference guards with `lookupKey !== 0xffffffff` against a value it read
     * signed, which is always true, so all of them land under key -1 and
     * clobber each other — after which any trigger miss can return whichever
     * one happened to be inserted last. */
    CHECK(provider->by_key_count + 0 < provider->loaded,
          "the trigger index is smaller than the script count");
    for( i = 0; i < provider->by_key_count; i++ )
    {
        script = &provider->scripts[provider->by_key[i].id];
        if( script->lookup_key < 0 )
        {
            CHECK(0, "a name-addressed script leaked into the trigger index");
            break;
        }
    }
    if( i == provider->by_key_count )
        CHECK(1, "no name-addressed script is in the trigger index");

    printf("  index   %d names, %d trigger keys, %d duplicate keys\n",
           provider->by_name_count, provider->by_key_count, provider->duplicate_keys);

    /* A duplicate key means two scripts declared the same trigger+subject, so
     * only one of them is reachable. Naming the triggers turns a bare count
     * into something actionable, and says whether a trigger the mock depends on
     * is affected.
     *
     * Expect the coord-subject triggers (mapzone / mapzoneexit / zone) and
     * nothing else. Their subject is a coord literal like `0_29_75`, which the
     * reference's compiler resolves with parseInt — that stops at the first
     * underscore and returns 0, so every one of them collapses onto the same
     * key. It is a compiler bug in the reference, not a key-width problem.
     *
     * It is inert *in the reference*, whose engine reaches all four by name
     * (`ScriptProvider.getByName`) and never by key. This tree dispatches them
     * the same way, and its own compiler now writes -1 for a coord subject — so
     * a pack built here contributes no coord keys at all. This corpus is the
     * reference's already-compiled bytes, so the count below describes the
     * reference's compiler and does not move when ours changes.
     * Anything OUTSIDE that family appearing here would be a real finding. */
    if( provider->duplicate_keys )
    {
        static int per_trigger[SS_TRIGGER_MAX];
        int trigger;

        memset(per_trigger, 0, sizeof(per_trigger));
        for( i = 1; i < provider->by_key_count; i++ )
        {
            if( provider->by_key[i].key != provider->by_key[i - 1].key )
                continue;
            trigger = (int)(provider->by_key[i].key & 0xff);
            if( trigger >= 0 && trigger < SS_TRIGGER_MAX )
                per_trigger[trigger]++;
        }
        for( trigger = 0; trigger < SS_TRIGGER_MAX; trigger++ )
        {
            if( per_trigger[trigger] )
                printf("  dup     %s x%d (only the lowest id is reachable)\n",
                       SSVM_TriggerName(trigger), per_trigger[trigger]);
        }
    }

    /* Resolve a real script through the trigger path, whichever one the corpus
     * happens to hold, so this does not depend on specific content. */
    script = NULL;
    for( i = 0; i < provider->count; i++ )
    {
        const struct SSVM_Script* candidate = &provider->scripts[i];
        int trigger;

        if( candidate->op_count == 0 || candidate->lookup_key < 0 )
            continue;
        /* Only type-addressed scripts have a subject we can reconstruct. */
        if( ((candidate->lookup_key >> 8) & 3) != SS_LOOKUP_TYPE )
            continue;

        trigger = candidate->lookup_key & 0xff;
        script = SSVM_ProviderGetByTriggerSpecific(provider, trigger,
                                                   candidate->lookup_key >> 10, -1);
        if( script )
        {
            printf("  sample  %s resolves by trigger %s\n", script->name,
                   SSVM_TriggerName(trigger));
            break;
        }
    }
    CHECK(script != NULL, "a type-addressed script resolves through GetByTrigger");
}

/* ------------------------------------------------------------------ */

int
main(int argc, char** argv)
{
    const char* corpus;
    char dir[1024];
    struct SSVM_Provider provider;
    struct SSVM_Error err;
    struct stat st;

    corpus = argc > 1 ? argv[1] : getenv("SS_CORPUS");
    if( !corpus )
        corpus = DEFAULT_CORPUS;

    snprintf(dir, sizeof(dir), "%s/engine/data/pack/server", corpus);
    if( stat(dir, &st) != 0 )
    {
        printf("SKIP: no compiled corpus at %s\n", dir);
        printf("      set SS_CORPUS to a LostCity_Server checkout to run this\n");
        return 0;
    }

    SSVM_ErrorClear(&err);
    if( !SSVM_ProviderLoadDir(&provider, dir, &err) )
    {
        printf("FAIL: %s\n", err.message);
        if( err.script_id >= 0 )
            printf("      script %d at offset %ld\n", err.script_id, err.offset);
        return 1;
    }

    printf("loaded %s\n", dir);
    CHECK(provider.compiler_version == SSVM_COMPILER_VERSION, "compiler version");
    CHECK(provider.loaded > 0, "at least one script decoded");
    /* Absent slots are normal: the compiler leaves a hole where a script id was
     * retired. What matters is that every slot the idx claims has a size for
     * decoded, which the loader's end-of-file check already proved. */
    CHECK(provider.loaded <= provider.count, "loaded count fits the slot count");

    report_aggregates(&provider);
    test_structure(&provider);
    test_names(&provider, corpus);
    test_indices(&provider);

    SSVM_ProviderFree(&provider);

    if( g_fail )
    {
        printf("ss corpus test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("ss corpus test: all passed\n");
    return 0;
}
