/*
 * content/content_name_index.h against the front-to-back scan it replaces.
 *
 * Every table it indexes is filled in storage order, so the index must answer
 * what a scan for the FIRST matching name answered: a hit for every name
 * added, the first value for a name added twice, -1 for anything else. Checked
 * across several growths, with names that differ only in their last byte or
 * only by a prefix, and on an index that has never been added to.
 */

#include "content/content_name_index.h"

#include <stdio.h>

static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

#define NAMES 5000

static char g_names[NAMES][32];
static int g_name_count;

/* The scan: the position of the first stored name equal to `name`, or -1. */
static int
scan_first(const char* name)
{
    for( int i = 0; i < g_name_count; i++ )
    {
        if( strcmp(g_names[i], name) == 0 )
            return i;
    }
    return -1;
}

int
main(void)
{
    struct ContentNameIndex index = { 0 };
    char probe[32];
    char what[96];
    unsigned seed = 7u;

    check(ContentNameIndex_Find(&index, "anything") == -1, "an empty index answers absent");
    ContentNameIndex_Free(&index);

    /*
     * Storage order with repeats: every 7th name repeats an earlier one, the
     * case a scan answers with the earlier position. The rest are neighbours
     * (`enum_12`, `enum_120`, `enum_1200`) so a prefix or a last-byte match
     * cannot pass for equality.
     */
    for( int i = 0; i < NAMES; i++ )
    {
        int value;

        seed = seed * 1103515245u + 12345u;
        if( i > 0 && i % 7 == 0 )
            snprintf(g_names[i], sizeof(g_names[i]), "%s", g_names[(seed >> 8) % (unsigned)i]);
        else
            snprintf(g_names[i], sizeof(g_names[i]), "enum_%d", i);
        g_name_count = i + 1;
        value = ContentNameIndex_Add(&index, g_names[i], i);
        snprintf(what, sizeof(what), "add %d `%s` answers the first binding", i, g_names[i]);
        check(value == scan_first(g_names[i]), what);
    }

    for( int i = 0; i < g_name_count; i++ )
    {
        snprintf(what, sizeof(what), "find `%s` is the first position", g_names[i]);
        check(ContentNameIndex_Find(&index, g_names[i]) == scan_first(g_names[i]), what);
    }
    for( int i = 0; i < 2000; i++ )
    {
        snprintf(probe, sizeof(probe), "enum_%d", NAMES + i);
        check(ContentNameIndex_Find(&index, probe) == -1, "a name never added is absent");
        snprintf(probe, sizeof(probe), "num_%d", i);
        check(ContentNameIndex_Find(&index, probe) == -1, "a suffix of a name is absent");
    }
    check(ContentNameIndex_Find(&index, "") == -1, "the empty name is absent");
    check(ContentNameIndex_Find(&index, "enum_") == -1, "a prefix of a name is absent");
    check(index.count * 2 <= (int)index.mask + 1, "the index is at most half full");

    ContentNameIndex_Free(&index);
    check(ContentNameIndex_Find(&index, g_names[0]) == -1, "a freed index answers absent");
    check(ContentNameIndex_Add(&index, g_names[0], 3) == 3, "a freed index can be refilled");
    ContentNameIndex_Free(&index);

    if( g_failures )
    {
        fprintf(stderr, "content_name_index_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("content_name_index_test: ok\n");
    return 0;
}
