/*
 * The CS2 decompiler, checked against the implementation it was ported from.
 *
 * RuneStar/cs2 ships both halves of a fixture: `input/` holds ~7,900 raw
 * clientscripts, and `scripts/scripts/` holds the source its own decompiler
 * produced from them. Comparing this port's output byte for byte against that
 * is the only check that means anything here — a decompiler that merely
 * *produces* plausible source proves nothing, whereas reproducing another
 * implementation's output on eight thousand real scripts does.
 *
 * Point the suite at the reference checkout with CS2_REFERENCE, or leave it as
 * a sibling of this repository. Without it the test skips, like every other
 * test here that needs data it does not carry.
 *
 * The figures are pinned rather than merely printed, so a regression fails
 * instead of quietly reporting a smaller number. Two known divergences are
 * allowed by exact count; see EXCEPTIONS.md G1.
 */

#include "cs2/cs2_compile.h"
#include "cs2/cs2_decompile.h"
#include "datatypes/clientscript.h"
#include "rscache_test.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Measured against RuneStar/cs2 at 2a8b8fc. Raise these when the port improves;
 * never lower them without a recorded reason.
 *
 * `decompiled` is deliberately higher than the reference's own 6,491: this port
 * carries opcode arities from the client's CS2 VM that the 2021 sources predate,
 * and degrades where upstream throws. `identical` is the figure that says the
 * port is faithful; `decompiled` only says it is not *less* capable.
 *
 * The recorded reason for 6,489 -> 6,485, which is a *lowering*: two opcodes
 * changed arity between this dump's era and OldSchool 239, and the table has no
 * era dimension to hold both. `mec_category` (6695) and `_6623` each return a
 * pair in 239 and a single value here; taking the 239 reading costs three
 * scripts and one script respectively on this corpus and gains ten and thirteen
 * on cache.osrs239, which is the revision the work targets. EXCEPTIONS.md G10
 * records it and names the fix — signatures scoped by era, through the
 * `RSCache_CS2_CommandOverride` seam that already exists for exactly this. */
#define CS2_EXPECT_MIN_IDENTICAL 6485
#define CS2_EXPECT_MAX_DIFFERENT 2
#define CS2_EXPECT_MIN_DECOMPILED 7467

struct cs2_entry
{
    int id;
    struct RSCache_ClientScript* script;
    bool attempted;
    unsigned char* bytes;
    int byte_count;
};

struct cs2_fixture
{
    char input_directory[1024];
    char expected_directory[1024];
    char names_directory[1024];

    struct cs2_entry* entries;
    int count;
    int capacity;

    struct RSCache_CS2_Names names;
};

static unsigned char*
cs2_read_file(const char* path, int* out_size)
{
    FILE* file = fopen(path, "rb");
    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(file);
        return NULL;
    }
    unsigned char* data = (unsigned char*)malloc((size_t)size + 1);
    if( !data )
    {
        fclose(file);
        return NULL;
    }
    size_t read = fread(data, 1, (size_t)size, file);
    fclose(file);
    data[read] = 0;
    *out_size = (int)read;
    return data;
}

static struct cs2_entry*
cs2_entry_for(struct cs2_fixture* fixture, int id)
{
    for( int i = 0; i < fixture->count; i++ )
    {
        if( fixture->entries[i].id == id )
            return &fixture->entries[i];
    }
    if( fixture->count == fixture->capacity )
    {
        int capacity = fixture->capacity ? fixture->capacity * 2 : 256;
        struct cs2_entry* entries = (struct cs2_entry*)realloc(
            fixture->entries, (size_t)capacity * sizeof(*entries));
        if( !entries )
            return NULL;
        memset(
            entries + fixture->capacity,
            0,
            (size_t)(capacity - fixture->capacity) * sizeof(*entries));
        fixture->entries = entries;
        fixture->capacity = capacity;
    }
    struct cs2_entry* entry = &fixture->entries[fixture->count++];
    memset(entry, 0, sizeof(*entry));
    entry->id = id;
    return entry;
}

static const struct RSCache_CS2_Script*
cs2_load_script(void* user, int script_id)
{
    struct cs2_fixture* fixture = (struct cs2_fixture*)user;
    struct cs2_entry* entry = cs2_entry_for(fixture, script_id);
    if( !entry )
        return NULL;
    if( entry->attempted )
        return entry->script ? &entry->script->script : NULL;
    entry->attempted = true;

    char path[2048];
    snprintf(path, sizeof(path), "%s/%d", fixture->input_directory, script_id);
    entry->bytes = cs2_read_file(path, &entry->byte_count);
    if( !entry->bytes )
        return NULL;
    entry->script = RSCache_ClientScriptNewFromDecodeFlags(
        script_id,
        entry->bytes,
        entry->byte_count,
        RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY);
    return entry->script ? &entry->script->script : NULL;
}

static enum RSCache_CS2_Type
cs2_load_param_type(void* user, int param_id)
{
    struct cs2_fixture* fixture = (struct cs2_fixture*)user;
    return RSCache_CS2_NamesParamType(&fixture->names, param_id);
}

static int
cs2_compare_ints(const void* a, const void* b)
{
    int left = *(const int*)a;
    int right = *(const int*)b;
    return left < right ? -1 : (left > right ? 1 : 0);
}

/** The reference sanitises a script name into a file name the same way. */
static void
cs2_name_to_file(const char* name, char* out, int capacity)
{
    int write = 0;
    for( int i = 0; name[i] && write + 1 < capacity; i++ )
        out[write++] = (name[i] == '/' || name[i] == '\\') ? '_' : name[i];
    out[write] = '\0';
}

static bool
cs2_directory_exists(const char* path)
{
    DIR* dir = opendir(path);
    if( !dir )
        return false;
    closedir(dir);
    return true;
}

/**
 * Locate the reference checkout.
 *
 * CS2_REFERENCE wins; otherwise the usual sibling layout is tried, since the
 * two repositories are normally cloned next to each other.
 */
static bool
cs2_find_fixture(struct cs2_fixture* fixture, const char* root)
{
    const char* candidates[3];
    const char* from_environment = getenv("CS2_REFERENCE");
    int candidate_count = 0;
    char sibling[1024];
    char sibling_up[1024];

    if( from_environment )
        candidates[candidate_count++] = from_environment;
    snprintf(sibling, sizeof(sibling), "%s/../cs2", root ? root : ".");
    candidates[candidate_count++] = sibling;
    snprintf(sibling_up, sizeof(sibling_up), "%s/../../cs2", root ? root : ".");
    candidates[candidate_count++] = sibling_up;

    for( int i = 0; i < candidate_count; i++ )
    {
        snprintf(fixture->input_directory, sizeof(fixture->input_directory), "%s/input",
                 candidates[i]);
        snprintf(
            fixture->expected_directory,
            sizeof(fixture->expected_directory),
            "%s/scripts/scripts",
            candidates[i]);
        snprintf(
            fixture->names_directory,
            sizeof(fixture->names_directory),
            "%s/src/main/resources/org/runestar/cs2",
            candidates[i]);
        if( cs2_directory_exists(fixture->input_directory) &&
            cs2_directory_exists(fixture->expected_directory) )
            return true;
    }
    return false;
}

int
main(int argc, char** argv)
{
    const char* root = argc > 1 ? argv[1] : "../..";

    struct cs2_fixture fixture;
    memset(&fixture, 0, sizeof(fixture));
    printf("cs2 decompiler tests\n");

    if( !cs2_find_fixture(&fixture, root) )
    {
        printf("-- reference corpus\n");
        printf("   SKIP (set CS2_REFERENCE to a RuneStar/cs2 checkout)\n");
        printf("test_cs2: skipped\n");
        return 0;
    }

    RSCache_CS2_NamesInit(&fixture.names);
    RSCache_CS2_NamesLoadDirectory(&fixture.names, fixture.names_directory);

    struct RSCache_CS2_DecompileOptions options;
    memset(&options, 0, sizeof(options));
    options.scripts.user = &fixture;
    options.scripts.load = cs2_load_script;
    options.param_types.user = &fixture;
    options.param_types.load = cs2_load_param_type;
    options.names = &fixture.names;

    /* Every id in the dump, in order, so a failure names a stable script. */
    int* ids = NULL;
    int id_count = 0;
    int id_capacity = 0;
    DIR* dir = opendir(fixture.input_directory);
    struct dirent* entry;
    while( dir && (entry = readdir(dir)) )
    {
        char* end = NULL;
        long id = strtol(entry->d_name, &end, 10);
        if( end == entry->d_name || *end != '\0' )
            continue;
        if( id_count == id_capacity )
        {
            id_capacity = id_capacity ? id_capacity * 2 : 1024;
            ids = (int*)realloc(ids, (size_t)id_capacity * sizeof(int));
            if( !ids )
                return 1;
        }
        ids[id_count++] = (int)id;
    }
    if( dir )
        closedir(dir);
    qsort(ids, (size_t)id_count, sizeof(int), cs2_compare_ints);

    RSCACHE_TEST_GROUP("decompile vs RuneStar/cs2");

    int decompiled = 0;
    int compared = 0;
    int identical = 0;
    int different = 0;
    int absent = 0;
    const char* first_difference = NULL;

    for( int i = 0; i < id_count; i++ )
    {
        char error[512] = { 0 };
        char* name = NULL;
        char* source = RSCache_CS2_Decompile(ids[i], &options, &name, error, (int)sizeof(error));
        if( !source )
        {
            free(name);
            continue;
        }
        decompiled++;

        char file_name[600];
        char path[2048];
        cs2_name_to_file(name ? name : "", file_name, (int)sizeof(file_name));
        snprintf(path, sizeof(path), "%s/%s.cs2", fixture.expected_directory, file_name);

        int expected_size = 0;
        unsigned char* expected = cs2_read_file(path, &expected_size);
        if( !expected )
        {
            /* The reference failed on this script where this port did not; that
             * is an improvement, not a mismatch. */
            absent++;
        }
        else
        {
            compared++;
            if( (int)strlen(source) == expected_size &&
                memcmp(source, expected, (size_t)expected_size) == 0 )
            {
                identical++;
            }
            else
            {
                different++;
                if( !first_difference )
                    first_difference = name ? name : "?";
            }
            free(expected);
        }
        free(source);
        if( name != first_difference )
            free(name);
    }

    printf(
        "   scripts=%d decompiled=%d compared=%d identical=%d different=%d "
        "reference-failed=%d\n",
        id_count,
        decompiled,
        compared,
        identical,
        different,
        absent);
    if( first_difference )
        printf("   first difference: %s\n", first_difference);

    RSCACHE_CHECK(decompiled >= CS2_EXPECT_MIN_DECOMPILED);
    RSCACHE_CHECK(identical >= CS2_EXPECT_MIN_IDENTICAL);
    /* Allowed by exact count, not blanket-skipped: a third divergence fails. */
    RSCACHE_CHECK(different <= CS2_EXPECT_MAX_DIFFERENT);
    if( different > 0 && different <= CS2_EXPECT_MAX_DIFFERENT )
        printf("   ALLOWED %d divergence(s) — see EXCEPTIONS.md G1\n", different);

    free(ids);
    for( int i = 0; i < fixture.count; i++ )
    {
        RSCache_ClientScriptFree(fixture.entries[i].script);
        free(fixture.entries[i].bytes);
    }
    free(fixture.entries);
    RSCache_CS2_NamesFree(&fixture.names);

    printf(
        "test_cs2: %d checks, %d failures\n", rscache_test_checks, rscache_test_failures);
    return rscache_test_failures == 0 ? 0 : 1;
}
