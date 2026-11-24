/*
 * UITree_String (ui/uitree_string.h): a short UI-tree string that carries its
 * length. Set from bytes and a length, from a C string, cleared, compared,
 * read; truncated where strncpy(dst, src, 63) truncated; and never padded.
 *
 *   make -C src test-uitree-string
 */
#include "ui/uitree_string.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int ok,
    char const* what)
{
    g_checks++;
    if( !ok )
    {
        g_failures++;
        printf("uitree-string: %-60s FAILED\n", what);
        return;
    }
    printf("uitree-string: %-60s ok\n", what);
}

int
main(void)
{
    struct UITree_String s;
    struct UITree_String t;
    char longer[200];

    memset(&s, 0, sizeof(s));
    check(UITree_StringEmpty(&s) && UITree_StringLen(&s) == 0 && UITree_StringText(&s)[0] == '\0',
        "a zeroed string is empty");

    UITree_StringSet(&s, "Withdraw-1xyz", 10);
    check(UITree_StringLen(&s) == 10, "set from bytes: the length given");
    check(strcmp(UITree_StringText(&s), "Withdraw-1") == 0, "set from bytes: those bytes, terminated");

    /* No padding: a shorter write leaves the bytes after its NUL alone. */
    memset(&s, 'Z', sizeof(s));
    UITree_StringSet(&s, "Eat", 3);
    check(s.text[3] == '\0' && s.text[4] == 'Z', "a write copies what it was given and a NUL, nothing more");

    UITree_StringSetC(&s, "Wield");
    check(UITree_StringLen(&s) == 5 && strcmp(s.text, "Wield") == 0, "set from a C string");
    UITree_StringSetC(&s, NULL);
    check(UITree_StringEmpty(&s), "set from NULL: empty");
    UITree_StringSet(&s, NULL, 0);
    check(UITree_StringEmpty(&s), "set from no bytes: empty");

    /* Truncation: what strncpy(dst, src, UITREE_STRING_CAP) kept. */
    memset(longer, 'a', sizeof(longer) - 1);
    longer[sizeof(longer) - 1] = '\0';
    UITree_StringSetC(&s, longer);
    check(UITree_StringLen(&s) == UITREE_STRING_CAP, "a long C string keeps UITREE_STRING_CAP bytes");
    check(s.text[UITREE_STRING_CAP] == '\0', "and is terminated");
    UITree_StringSet(&t, longer, 150);
    check(UITree_StringEquals(&s, &t), "the bytes form truncates alike");
    check(UITree_StringEqualsBytes(&s, longer, 150), "bytes past the cap compare as truncated");
    check(UITree_StringEqualsC(&s, longer), "a C string past the cap compares as truncated");

    UITree_StringSetC(&s, "Deposit-All");
    UITree_StringSetC(&t, "Deposit-All");
    check(UITree_StringEquals(&s, &t), "equal strings compare equal");
    check(UITree_StringEqualsC(&s, "Deposit-All"), "equals its C string");
    check(!UITree_StringEqualsC(&s, "Deposit-Al"), "not a prefix of it");
    check(!UITree_StringEqualsC(&s, "Deposit-All-but-one"), "not an extension of it");
    UITree_StringSetC(&t, "Deposit-1");
    check(!UITree_StringEquals(&s, &t), "different strings compare different");

    /* A shorter write over a longer one: equal to the short text, whatever
     * lies past its end. */
    UITree_StringSetC(&s, "Withdraw-All-but-one");
    UITree_StringSetC(&s, "Eat");
    UITree_StringSetC(&t, "Eat");
    check(UITree_StringEquals(&s, &t), "bytes past the length are not part of the string");

    UITree_StringClear(&s);
    check(UITree_StringEmpty(&s) && s.text[0] == '\0', "clear");
    check(UITree_StringEqualsC(&s, ""), "cleared equals the empty C string");
    check(UITree_StringEqualsC(&s, NULL), "and NULL");

    /* Overlapping set (a string set from its own text). */
    UITree_StringSetC(&s, "Examine");
    UITree_StringSet(&s, s.text + 2, 3);
    check(strcmp(s.text, "ami") == 0, "set from its own bytes");

    printf("uitree-string: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
