/*
 * The runtime knobs that carry a value.
 *
 * Each of these has a small grammar, and every way of getting one wrong is
 * silent. A spawn argument that does not parse spawns the built-in id instead
 * of the one you typed. A map-square list that half-parses meshes half a world
 * and reads as "the renderer got faster". A field of view outside the
 * projection's domain mirrors the scene rather than failing.
 *
 * So the grammars are stated here, in both directions: what each accepts, and
 * what it must refuse.
 *
 * Build and run:
 *   make -C src test-env-values
 */

#include "torirs_env_values.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define WANT_ARG(args_, name_, want_)                                                              \
    do                                                                                             \
    {                                                                                              \
        int got_ = ToriRS_EnvNamedArg((args_), (name_), -7);                                       \
        CHECK(got_ == (want_), "arg \"%s\" of \"%s\" is %d, want %d", (name_), (args_) ? (args_) : "(null)", got_, (want_)); \
    } while( 0 )

static void
test_named_arg(void)
{
    /* The shape a debug hotkey actually carries. */
    WANT_ARG("id=3106", "id", 3106);
    WANT_ARG("id=3106,height=92,delay=0", "height", 92);
    WANT_ARG("id=3106,height=92,delay=0", "delay", 0);
    WANT_ARG("id=3106,height=92,delay=0", "id", 3106);

    /* Absent, so the fallback stands. */
    WANT_ARG("id=3106", "model", -7);
    WANT_ARG(NULL, "id", -7);
    WANT_ARG("", "id", -7);

    /* A prefix is not the name. The case that makes the '=' test load-bearing
     * is a clause whose name RUNS ON INTO DIGITS: without it, "id42" reads as
     * the name "id" with the value 42 -- a number that parses cleanly and is
     * silently wrong. "idle=5" is rejected either way, by the number parse. */
    WANT_ARG("id42", "id", -7);
    WANT_ARG("idle=5", "id", -7);
    WANT_ARG("id=5", "idle", -7);

    /* The number must consume the whole clause. A trailing character means the
     * clause was not what it looked like, and guessing at its prefix is how a
     * typo becomes a plausible wrong id. */
    WANT_ARG("id=12x", "id", -7);
    WANT_ARG("id=", "id", -7);
    WANT_ARG("id=-4", "id", -7);

    /* strtol's bases are accepted, because ids get pasted from dumps. */
    WANT_ARG("id=0x40", "id", 64);

    /* A later clause with the same name wins: that is what editing the tail of
     * a string looks like from the person's side. */
    WANT_ARG("id=1,id=2", "id", 2);

    /* Whitespace is NOT trimmed. Stated because it is the one thing here that
     * might reasonably be either way, and a caller pasting "id=1, id=2" gets
     * the first clause only -- worth knowing before it is a puzzle. */
    WANT_ARG("id=1, id=2", "id", 1);
}

static void
test_chunk_list(void)
{
    int chunks[8];
    int count;

    count = ToriRS_EnvChunkList("50,50", chunks, 4);
    CHECK(count == 1, "single pair count %d, want 1", count);
    CHECK(chunks[0] == 50 && chunks[1] == 50, "single pair is %d,%d", chunks[0], chunks[1]);

    count = ToriRS_EnvChunkList("50,50;51,50;50,51;51,51", chunks, 4);
    CHECK(count == 4, "2x2 block count %d, want 4", count);
    CHECK(chunks[6] == 51 && chunks[7] == 51, "last pair is %d,%d", chunks[6], chunks[7]);

    count = ToriRS_EnvChunkList(" 50 , 50 ; 51 , 50 ", chunks, 4);
    CHECK(count == 2, "spaced list count %d, want 2", count);
    CHECK(chunks[2] == 51, "spaced second x is %d, want 51", chunks[2]);

    /* Never a partial read. Each of these is a plausible typo, and each would
     * otherwise mesh fewer squares than asked -- invisible in the frame. */
    CHECK(ToriRS_EnvChunkList("50", chunks, 4) == 0, "a lone number parsed");
    CHECK(ToriRS_EnvChunkList("50,", chunks, 4) == 0, "a dangling comma parsed");
    CHECK(ToriRS_EnvChunkList("50,50;", chunks, 4) == 0, "a trailing semicolon parsed");
    CHECK(ToriRS_EnvChunkList("50,50;junk", chunks, 4) == 0, "trailing junk parsed");
    CHECK(ToriRS_EnvChunkList("50,50 junk", chunks, 4) == 0, "trailing text parsed");
    CHECK(ToriRS_EnvChunkList("", chunks, 4) == 0, "an empty spec parsed");

    /* Longer than the cap is REFUSED, not truncated. Clipping would be the
     * same silent half-load as a typo: fewer squares meshed than asked for,
     * invisible in the frame, and it reads as the renderer getting faster. */
    CHECK(ToriRS_EnvChunkList("1,1;2,2;3,3", chunks, 2) == 0, "an over-long list was truncated");
    count = ToriRS_EnvChunkList("1,1;2,2", chunks, 2);
    CHECK(count == 2, "a list exactly at the cap gave %d, want 2", count);

    /* Negative coordinates are ordinary: a square index can be relative. */
    count = ToriRS_EnvChunkList("-1,-2", chunks, 4);
    CHECK(count == 1 && chunks[0] == -1 && chunks[1] == -2, "negative pair is %d,%d", chunks[0], chunks[1]);
}

static void
test_id_list(void)
{
    CHECK(ToriRS_EnvIdListHas("1,2,3", 2), "2 not found in \"1,2,3\"");
    CHECK(ToriRS_EnvIdListHas("1,2,3", 1), "the first id was not found");
    CHECK(ToriRS_EnvIdListHas("1,2,3", 3), "the last id was not found");
    CHECK(!ToriRS_EnvIdListHas("1,2,3", 4), "4 was found in \"1,2,3\"");

    /* A prefix of a listed id is not a match -- 1 must not match 12. */
    CHECK(!ToriRS_EnvIdListHas("12,34", 1), "1 matched the \"12\" entry");
    CHECK(ToriRS_EnvIdListHas("12,34", 34), "34 was not found");

    /* Empty is not "unset": it is the list containing nothing, which is how
     * the knob is used to switch the feature off for every id. The caller
     * decides what unset means, because it has a config default to fall back
     * to and this does not. */
    CHECK(!ToriRS_EnvIdListHas("", 0), "the empty list contained something");
    CHECK(!ToriRS_EnvIdListHas("", 7), "the empty list contained 7");

    /* The walk eats one comma per number, so a space-separated list reads the
     * same (the number parse skips leading whitespace)... */
    CHECK(ToriRS_EnvIdListHas("1 2 3", 2), "space-separated 2 was not found");

    /* ...but a DOUBLED separator stops it, and everything after is invisible.
     * Pinned rather than fixed: the list is read once at boot and a stalled
     * walk shows up straight away as "my id did nothing". Worth knowing it is
     * the separator and not the id. */
    CHECK(ToriRS_EnvIdListHas("1,,2", 1), "the id before a doubled comma was lost");
    CHECK(!ToriRS_EnvIdListHas("1,,2", 2), "the walk now reads past a doubled comma");

    CHECK(!ToriRS_EnvIdListHas("junk", 0), "a non-numeric list matched 0");
}

static void
test_scale_mode(void)
{
    CHECK(ToriRS_EnvScaleMode(NULL) == TORIRS_ENV_SCALE_AUTO, "unset is not auto");
    CHECK(ToriRS_EnvScaleMode("") == TORIRS_ENV_SCALE_AUTO, "empty is not auto");
    CHECK(ToriRS_EnvScaleMode("auto") == TORIRS_ENV_SCALE_AUTO, "\"auto\" is not auto");
    CHECK(ToriRS_EnvScaleMode("1") == TORIRS_ENV_SCALE_AUTO, "\"1\" is not auto");
    CHECK(ToriRS_EnvScaleMode("off") == TORIRS_ENV_SCALE_OFF, "\"off\" is not off");
    CHECK(ToriRS_EnvScaleMode("0") == TORIRS_ENV_SCALE_OFF, "\"0\" is not off");
    CHECK(ToriRS_EnvScaleMode("512") == 512, "an explicit scale was not taken");
    CHECK(ToriRS_EnvScaleMode("8") == 8, "the lowest explicit scale was not taken");

    /* Below 8 the projection collapses, so those read as auto rather than
     * being obeyed. "2" is the interesting one: it looks like a mode. */
    CHECK(ToriRS_EnvScaleMode("7") == TORIRS_ENV_SCALE_AUTO, "7 was taken as a scale");
    CHECK(ToriRS_EnvScaleMode("2") == TORIRS_ENV_SCALE_AUTO, "2 was taken as a scale");
    CHECK(ToriRS_EnvScaleMode("-4") == TORIRS_ENV_SCALE_AUTO, "a negative was taken as a scale");
    CHECK(ToriRS_EnvScaleMode("yes") == TORIRS_ENV_SCALE_AUTO, "unknown text is not auto");
}

static void
test_fov_override(void)
{
    int const lo = 64;
    int const hi = 512;

    CHECK(ToriRS_EnvFovOverride(NULL, lo, hi) == -1, "unset did not decline");
    CHECK(ToriRS_EnvFovOverride("", lo, hi) == -1, "empty did not decline");
    CHECK(ToriRS_EnvFovOverride("junk", lo, hi) == -1, "non-numeric did not decline");
    CHECK(ToriRS_EnvFovOverride("0", lo, hi) == -1, "zero did not decline");
    CHECK(ToriRS_EnvFovOverride("-90", lo, hi) == -1, "a negative did not decline");

    CHECK(ToriRS_EnvFovOverride("128", lo, hi) == 128, "an in-range value was not taken");
    CHECK(ToriRS_EnvFovOverride("64", lo, hi) == lo, "the low bound was not taken");
    CHECK(ToriRS_EnvFovOverride("512", lo, hi) == hi, "the high bound was not taken");

    /* Clamped, never rejected: out of domain the projection mirrors the world,
     * and a mirrored scene is much harder to recognise than a narrow one. */
    CHECK(ToriRS_EnvFovOverride("1", lo, hi) == lo, "a too-small value was not clamped up");
    CHECK(ToriRS_EnvFovOverride("99999", lo, hi) == hi, "a too-large value was not clamped down");

    /* Trailing text after a number is taken, because sscanf stops there and
     * "90deg" is a person being descriptive rather than wrong. */
    CHECK(ToriRS_EnvFovOverride("90deg", lo, hi) == 90, "\"90deg\" was not read as 90");
}

int
main(void)
{
    test_named_arg();
    test_chunk_list();
    test_id_list();
    test_scale_mode();
    test_fov_override();

    if( g_failures )
    {
        printf("env_values_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("env_values_test: OK\n");
    return 0;
}
