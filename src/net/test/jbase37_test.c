#include "net/jbase37.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
expect_name(uint64_t encoded, char const* expected)
{
    char decoded[13];
    base37tostr(encoded, decoded, sizeof(decoded));
    assert(strcmp(decoded, expected) == 0);
}

int
main(void)
{
    char const* names[] = {
        "testc", "a", "gf1234567890", "matthewevers", "zzzzzzzzzzzz", "999999999999"
    };
    for( size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++ )
        expect_name(strtobase37(names[i]), names[i]);

    /* Valid names above the old 2^60 cutoff and both sides of 37^12. */
    assert(strtobase37("gf1234567890") > (UINT64_C(1) << 60));
    expect_name(UINT64_C(6582952005840035280), "999999999999");
    expect_name(UINT64_C(6582952005840035281), "invalid_name");
    expect_name(UINT64_MAX, "invalid_name");
    expect_name(0, "invalid_name");
    expect_name(37, "invalid_name");

    /* A short destination remains terminated and cannot corrupt its neighbour. */
    char small[6] = "?????";
    base37tostr(strtobase37("gf1234567890"), small, 5);
    assert(strcmp(small, "inva") == 0);
    assert(small[5] == '\0');
    puts("test-base37: ok");
    return 0;
}
