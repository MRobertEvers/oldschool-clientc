/*
 * decode -> encode -> memcmp, over every script in LostCity's compiled corpus.
 *
 * This is the reader's strongest proof and it is worth being clear about why.
 * A decoder can pass every structural check and still be wrong: skip a field it
 * does not model, mis-size one it does, or normalise a value on the way in. The
 * only evidence that none of that happened is reproducing the original bytes
 * exactly, which requires having understood every one of them.
 *
 * It also pins the parts of the format nothing else exercises — the absolute
 * build-machine paths in the header, the param-type bytes above 127, the line
 * table, and the trailer's "+ 1" length convention.
 *
 * A synthetic case runs first so a broken encoder is diagnosed on four
 * instructions rather than in the middle of five megabytes.
 *
 *   SS_CORPUS=/path/to/LostCity_Server make -C src test-ss-roundtrip
 */

#include "ss_opcode.h"
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
/* Synthetic: build a script by hand, encode it, decode it back        */
/* ------------------------------------------------------------------ */

static void
test_synthetic(void)
{
    struct SSVM_Script source;
    struct SSVM_Script decoded;
    struct SSVM_Error err;
    struct SSVM_SwitchTable table;
    struct SSVM_SwitchCase cases[2];
    uint16_t opcodes[4];
    int32_t int_operands[4];
    char* string_operands[4];
    int32_t line_pcs[1];
    int32_t line_numbers[1];
    uint8_t buffer[512];
    size_t length = 0;

    printf("synthetic round trip\n");

    SSVM_ErrorClear(&err);
    memset(&source, 0, sizeof(source));

    /* Deliberately covers each operand width: a large (4-byte) operand, a
     * string operand, a small (1-byte) operand, and the trailing RETURN. */
    opcodes[0] = SS_OP_PUSH_CONSTANT_INT;
    int_operands[0] = -12345;
    string_operands[0] = NULL;

    opcodes[1] = SS_OP_PUSH_CONSTANT_STRING;
    int_operands[1] = 0;
    string_operands[1] = (char*)"hello, <br>world";

    opcodes[2] = SS_OP_MES;
    int_operands[2] = 1; /* the dot flag: commands carry a one-byte operand */
    string_operands[2] = NULL;

    opcodes[3] = SS_OP_RETURN;
    int_operands[3] = 0;
    string_operands[3] = NULL;

    cases[0].key = 7;
    cases[0].delta = 3;
    cases[1].key = -2;
    cases[1].delta = 11;
    table.cases = cases;
    table.case_count = 2;

    line_pcs[0] = 0;
    line_numbers[0] = 42;

    source.id = 3;
    source.name = (char*)"[opnpc1,test_subject]";
    source.source_path = (char*)"/nowhere/test.rs2";
    source.lookup_key = 10 | (2 << 8) | (3105 << 10);
    /* 254 is NPC_STAT: a signed char turns it negative, and every later type
     * comparison then misses. Round-tripping it proves the reader keeps it. */
    source.param_types[0] = 254;
    source.param_types[1] = 255;
    source.param_type_count = 2;
    source.int_local_count = 5;
    source.string_local_count = 2;
    source.int_arg_count = 1;
    source.string_arg_count = 1;
    source.op_count = 4;
    source.opcodes = opcodes;
    source.int_operands = int_operands;
    source.string_operands = string_operands;
    source.switch_tables = &table;
    source.switch_table_count = 1;
    source.line_pcs = line_pcs;
    source.line_numbers = line_numbers;
    source.line_count = 1;

    if( !SSVM_ScriptEncode(&source, buffer, sizeof(buffer), &length, &err) )
    {
        printf("  FAIL encode: %s\n", err.message);
        g_fail++;
        return;
    }
    CHECK(length > 0, "encode produced bytes");

    if( !SSVM_ScriptDecode(3, buffer, length, &decoded, &err) )
    {
        printf("  FAIL decode: %s\n", err.message);
        g_fail++;
        return;
    }

    CHECK(strcmp(decoded.name, source.name) == 0, "name survives");
    CHECK(strcmp(decoded.source_path, source.source_path) == 0, "source path survives");
    CHECK(decoded.lookup_key == source.lookup_key, "lookup key survives");
    CHECK(decoded.param_type_count == 2, "param type count survives");
    CHECK(decoded.param_types[0] == 254 && decoded.param_types[1] == 255,
          "param types above 127 survive unsigned");
    CHECK(decoded.int_local_count == 5 && decoded.string_local_count == 2,
          "local counts survive");
    CHECK(decoded.int_arg_count == 1 && decoded.string_arg_count == 1,
          "arg counts survive");
    CHECK(decoded.op_count == 4, "instruction count survives");
    CHECK(decoded.int_operands[0] == -12345, "negative large operand survives");
    CHECK(decoded.string_operands[1] && strcmp(decoded.string_operands[1], string_operands[1]) == 0,
          "string operand survives");
    CHECK(decoded.int_operands[2] == 1, "small operand (dot flag) survives");
    CHECK(decoded.switch_table_count == 1 && decoded.switch_tables[0].case_count == 2,
          "switch table survives");
    CHECK(decoded.switch_tables[0].cases[1].key == -2 &&
              decoded.switch_tables[0].cases[1].delta == 11,
          "negative switch key survives");
    CHECK(decoded.line_count == 1 && decoded.line_numbers[0] == 42, "line table survives");

    SSVM_ScriptFree(&decoded);
}

static void
test_sizing_pass(void)
{
    struct SSVM_Script source;
    struct SSVM_Error err;
    uint16_t opcodes[1];
    int32_t int_operands[1];
    char* string_operands[1];
    uint8_t small[4];
    size_t sized = 0;
    size_t written = 0;
    uint8_t* exact;

    printf("sizing pass\n");

    SSVM_ErrorClear(&err);
    memset(&source, 0, sizeof(source));
    opcodes[0] = SS_OP_RETURN;
    int_operands[0] = 0;
    string_operands[0] = NULL;
    source.name = (char*)"[proc,tiny]";
    source.source_path = (char*)"t.rs2";
    source.lookup_key = -1;
    source.op_count = 1;
    source.opcodes = opcodes;
    source.int_operands = int_operands;
    source.string_operands = string_operands;

    CHECK(SSVM_ScriptEncode(&source, NULL, 0, &sized, &err), "sizing pass succeeds");
    CHECK(sized > 0, "sizing pass reports a length");

    /* A buffer that cannot hold the script must be refused, not overrun. */
    CHECK(!SSVM_ScriptEncode(&source, small, sizeof(small), &written, &err),
          "a too-small buffer is rejected");

    exact = (uint8_t*)malloc(sized);
    CHECK(SSVM_ScriptEncode(&source, exact, sized, &written, &err),
          "an exactly-sized buffer succeeds");
    CHECK(written == sized, "sizing pass matched the write");
    free(exact);
}

/* ------------------------------------------------------------------ */
/* The corpus                                                          */
/* ------------------------------------------------------------------ */

static void
test_corpus(const char* corpus)
{
    char dir[1024];
    char path[1024];
    struct SSVM_Provider provider;
    struct SSVM_Error err;
    struct stat st;
    FILE* file;
    uint8_t* dat;
    long dat_length;
    size_t offset;
    uint8_t* scratch = NULL;
    size_t scratch_capacity = 0;
    int32_t i;
    int compared = 0;
    int mismatched = 0;
    int32_t* sizes;

    printf("corpus round trip\n");

    snprintf(dir, sizeof(dir), "%s/engine/data/pack/server", corpus);
    if( stat(dir, &st) != 0 )
    {
        printf("  SKIP  no compiled corpus at %s\n", dir);
        printf("        set SS_CORPUS to a LostCity_Server checkout to run this\n");
        return;
    }

    SSVM_ErrorClear(&err);
    if( !SSVM_ProviderLoadDir(&provider, dir, &err) )
    {
        printf("  FAIL load: %s\n", err.message);
        g_fail++;
        return;
    }

    /* Re-read the raw files so the comparison is against the bytes on disk,
     * not against anything the provider derived from them. */
    snprintf(path, sizeof(path), "%s/script.dat", dir);
    file = fopen(path, "rb");
    if( !file )
    {
        printf("  FAIL cannot re-read %s\n", path);
        g_fail++;
        SSVM_ProviderFree(&provider);
        return;
    }
    fseek(file, 0, SEEK_END);
    dat_length = ftell(file);
    rewind(file);
    dat = (uint8_t*)malloc((size_t)dat_length);
    assert(dat);
    if( fread(dat, 1, (size_t)dat_length, file) != (size_t)dat_length )
    {
        printf("  FAIL cannot read script.dat body\n");
        g_fail++;
        free(dat);
        fclose(file);
        SSVM_ProviderFree(&provider);
        return;
    }
    fclose(file);

    snprintf(path, sizeof(path), "%s/script.idx", dir);
    file = fopen(path, "rb");
    sizes = (int32_t*)calloc((size_t)provider.count, sizeof(int32_t));
    if( file && sizes )
    {
        uint8_t header[4];
        size_t got = fread(header, 1, 4, file);

        (void)got;
        for( i = 0; i < provider.count; i++ )
        {
            uint8_t raw[4];

            if( fread(raw, 1, 4, file) != 4 )
                break;
            sizes[i] = (int32_t)(((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
                                 ((uint32_t)raw[2] << 8) | (uint32_t)raw[3]);
        }
    }
    if( file )
        fclose(file);

    offset = 8; /* entry count + compiler version */
    for( i = 0; i < provider.count; i++ )
    {
        const struct SSVM_Script* script = &provider.scripts[i];
        size_t needed = 0;

        if( sizes[i] == 0 )
            continue;

        if( !SSVM_ScriptEncode(script, NULL, 0, &needed, &err) )
        {
            if( mismatched < 5 )
                printf("  id %d: sizing failed: %s\n", (int)i, err.message);
            mismatched++;
            offset += (size_t)sizes[i];
            continue;
        }

        if( needed > scratch_capacity )
        {
            free(scratch);
            scratch_capacity = needed * 2;
            scratch = (uint8_t*)malloc(scratch_capacity);
        }

        if( !SSVM_ScriptEncode(script, scratch, scratch_capacity, &needed, &err) )
        {
            if( mismatched < 5 )
                printf("  id %d: encode failed: %s\n", (int)i, err.message);
            mismatched++;
            offset += (size_t)sizes[i];
            continue;
        }

        compared++;
        if( needed != (size_t)sizes[i] || memcmp(scratch, dat + offset, needed) != 0 )
        {
            if( mismatched < 5 )
            {
                size_t byte;

                printf("  id %d (%s): re-encoded %zu bytes, original %d\n", (int)i,
                       script->name ? script->name : "?", needed, sizes[i]);
                for( byte = 0; byte < needed && byte < (size_t)sizes[i]; byte++ )
                {
                    if( scratch[byte] != dat[offset + byte] )
                    {
                        printf("        first difference at byte %zu: %02x vs %02x\n", byte,
                               scratch[byte], dat[offset + byte]);
                        break;
                    }
                }
            }
            mismatched++;
        }
        offset += (size_t)sizes[i];
    }

    printf("  compared %d scripts, %zu bytes\n", compared, offset);
    CHECK(compared > 0, "the corpus supplied scripts to compare");
    CHECK(mismatched == 0, "every script re-encodes byte-identically");
    CHECK(offset == (size_t)dat_length, "the walk consumed script.dat exactly");

    free(scratch);
    free(sizes);
    free(dat);
    SSVM_ProviderFree(&provider);
}

int
main(int argc, char** argv)
{
    const char* corpus;

    corpus = argc > 1 ? argv[1] : getenv("SS_CORPUS");
    if( !corpus )
        corpus = DEFAULT_CORPUS;

    test_synthetic();
    test_sizing_pass();
    test_corpus(corpus);

    if( g_fail )
    {
        printf("ss roundtrip test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("ss roundtrip test: all passed\n");
    return 0;
}
