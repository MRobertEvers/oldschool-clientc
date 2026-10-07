/*
 * The full-key config text's two markers, as the C readers classify them.
 *
 * `content/content_value.h` is the single test every reader under src/ applies
 * where it splits `key=value`. Its failure modes are all silent ones, so each
 * case below is a spelling that a reader would otherwise have taken as data:
 * `default` read as a name, `empty` read as a list entry, `\default` read as a
 * marker when it is the escaped word, `\\default` (an escaped backslash, the
 * old text's spelling of a string that starts with one) unescaped by a reader
 * that never unescaped anything.
 *
 * Run: make -C src test-content-value
 */

#include "content/content_value.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    printf("content-value: %-60s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

/** Classify a copy of `text`; report what it became. */
static enum ContentValueMarker
classify(
    const char* text,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s", text);
    return ContentValue_Marker(out);
}

int
main(void)
{
    char value[64];
    char line[64];

    /* The two markers. */
    check(classify("default", value, sizeof(value)) == CONTENT_VALUE_IS_DEFAULT,
          "`default` is the absent marker");
    check(strcmp(value, "default") == 0, "classifying `default` leaves it alone");
    check(classify("empty", value, sizeof(value)) == CONTENT_VALUE_IS_EMPTY,
          "`empty` is the present-empty marker");
    check(ContentValue_IsMarker(CONTENT_VALUE_IS_DEFAULT) &&
              ContentValue_IsMarker(CONTENT_VALUE_IS_EMPTY),
          "both markers are markers");

    /* The escape: the word itself, as data. */
    check(classify("\\default", value, sizeof(value)) == CONTENT_VALUE_PLAIN,
          "`\\default` is not a marker");
    check(strcmp(value, "default") == 0, "`\\default` unescapes to the word `default`");
    check(classify("\\empty", value, sizeof(value)) == CONTENT_VALUE_PLAIN,
          "`\\empty` is not a marker");
    check(strcmp(value, "empty") == 0, "`\\empty` unescapes to the word `empty`");

    /* Everything else is untouched: this header undoes the marker escape only. */
    check(classify("\\\\default", value, sizeof(value)) == CONTENT_VALUE_PLAIN &&
              strcmp(value, "\\\\default") == 0,
          "`\\\\default` (escaped backslash) is plain and left as written");
    check(classify("defaults", value, sizeof(value)) == CONTENT_VALUE_PLAIN &&
              strcmp(value, "defaults") == 0,
          "`defaults` is a plain value");
    check(classify("Default", value, sizeof(value)) == CONTENT_VALUE_PLAIN,
          "the test is case-sensitive: `Default` is a plain value");
    check(classify("default ", value, sizeof(value)) == CONTENT_VALUE_PLAIN,
          "trimming is the reader's job: `default ` is a plain value");
    check(classify("foo,default", value, sizeof(value)) == CONTENT_VALUE_PLAIN,
          "a marker inside a list entry (`foo,default`) is not a marker");
    check(classify("", value, sizeof(value)) == CONTENT_VALUE_PLAIN && value[0] == '\0',
          "an empty value is a plain (empty) value");
    check(classify("\\[header]", value, sizeof(value)) == CONTENT_VALUE_PLAIN &&
              strcmp(value, "\\[header]") == 0,
          "other escapes (`\\[`) are left for the reader");
    check(classify("12", value, sizeof(value)) == CONTENT_VALUE_PLAIN &&
              strcmp(value, "12") == 0,
          "an ordinary value is plain and unchanged");

    /* The prefix form, for readers that match `key=` without splitting. */
    snprintf(line, sizeof(line), "bodypart=default");
    check(ContentValue_LineMarker(line, 9) == CONTENT_VALUE_IS_DEFAULT,
          "LineMarker: `bodypart=default` is the absent marker");
    snprintf(line, sizeof(line), "column=empty");
    check(ContentValue_LineMarker(line, 7) == CONTENT_VALUE_IS_EMPTY,
          "LineMarker: `column=empty` is the present-empty marker");
    snprintf(line, sizeof(line), "name=\\default");
    check(ContentValue_LineMarker(line, 5) == CONTENT_VALUE_PLAIN &&
              strcmp(line, "name=default") == 0,
          "LineMarker: `name=\\default` unescapes in place");
    snprintf(line, sizeof(line), "bodypart=4");
    check(ContentValue_LineMarker(line, 9) == CONTENT_VALUE_PLAIN,
          "LineMarker: `bodypart=4` is plain");

    if( g_failures )
    {
        printf("content-value: %d FAILED\n", g_failures);
        return 1;
    }
    printf("content-value: all passed\n");
    return 0;
}
