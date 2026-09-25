/*
 * The retained tree's compare-before-bump signatures.
 *
 * The whole retain mechanism rests on one property: if a value a host request
 * can expose changed, the domain's signature changed. A signature that misses
 * a change does not crash or look wrong in a log -- it leaves a stale
 * descriptor on screen, and the client keeps running. So the property is
 * stated here rather than trusted.
 *
 * What is asserted:
 *
 *   - a single byte changed anywhere in a buffer changes the word, at every
 *     offset including inside the tail that the eight-bytes-at-a-time loop
 *     does not reach. This is the property the retain gate actually needs.
 *   - order matters: the same values threaded in a different order give a
 *     different signature.
 *   - strings fold their length first, so a character moving between two
 *     adjacent strings changes the result. Without that, "ab" + "cde" and
 *     "abc" + "de" are the same signature and a caption swap goes
 *     unpublished. NULL is the empty string, which is what an unset caption
 *     is.
 *
 * And two things it deliberately does NOT do, pinned here because both are
 * easy to assume and both are false:
 *
 *   - it is not FNV-1a. FNV xors one byte then multiplies; this xors eight
 *     then multiplies once, which is the whole point (a 64-bit multiply is
 *     three 32-bit ones on armv7, and per byte that was about 0.08 ms a
 *     frame on the Moto X). The two disagree on every input, whole words
 *     included.
 *   - a RAW byte fold does not encode length: three bytes and the same three
 *     followed by zeros give the same word, and so do a 4-byte and an 8-byte
 *     copy of the same small number. That is harmless where it is used --
 *     each domain threads a fixed sequence of fixed-width fields -- and it is
 *     exactly why the string fold has to add the length itself. A caller that
 *     folds a genuinely variable-length buffer has to do the same.
 *
 * Build and run:
 *   make -C src test-uitree-input-signature
 */

#include "ui/uitree_input_signature.h"

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

static void
test_every_byte_matters(void)
{
    /* 21 bytes: two whole words plus a five-byte tail, so the loop and the
     * tail path are both exercised at every offset. */
    unsigned char buffer[21];
    uint64_t base;

    for( size_t i = 0; i < sizeof(buffer); i++ )
        buffer[i] = (unsigned char)((i * 7) + 3);
    base = UITree_InputSignatureBytes(UITREE_INPUT_SIGNATURE_OFFSET, buffer, sizeof(buffer));

    for( size_t i = 0; i < sizeof(buffer); i++ )
    {
        unsigned char const original = buffer[i];
        uint64_t changed;

        buffer[i] = (unsigned char)(original ^ 0x01);
        changed = UITree_InputSignatureBytes(UITREE_INPUT_SIGNATURE_OFFSET, buffer, sizeof(buffer));
        CHECK(changed != base, "flipping bit 0 of byte %zu did not change the signature", i);

        /* And the high bit, which lands in a different word lane. */
        buffer[i] = (unsigned char)(original ^ 0x80);
        changed = UITree_InputSignatureBytes(UITREE_INPUT_SIGNATURE_OFFSET, buffer, sizeof(buffer));
        CHECK(changed != base, "flipping bit 7 of byte %zu did not change the signature", i);

        buffer[i] = original;
    }
}

/*
 * What the fold is NOT. Both of these are the kind of thing the next reader
 * assumes without checking, and both are false; stating them here is cheaper
 * than the afternoon spent finding out.
 */
static void
test_it_is_not_fnv1a_and_does_not_encode_length(void)
{
    unsigned char const buffer[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint64_t byte_at_a_time = UITREE_INPUT_SIGNATURE_OFFSET;

    /* FNV-1a over the same bytes with the same basis and prime. It differs,
     * because this fold xors eight bytes per multiply instead of one -- which
     * is the reason it exists. */
    for( size_t i = 0; i < sizeof(buffer); i++ )
    {
        byte_at_a_time ^= buffer[i];
        byte_at_a_time *= UITREE_INPUT_SIGNATURE_PRIME;
    }
    CHECK(
        UITree_InputSignatureBytes(UITREE_INPUT_SIGNATURE_OFFSET, buffer, sizeof(buffer)) !=
            byte_at_a_time,
        "the word fold matched FNV-1a; if that is now intended, the header's "
        "performance argument no longer holds");

    /* A raw byte fold carries no length, so trailing zeros are invisible: on
     * a little-endian host three bytes and the same three padded to eight
     * land the same word in the accumulator. Harmless where it is used (each
     * domain threads fixed-width fields) and the reason the string fold adds
     * its own length. */
    {
        unsigned char const three[8] = {1, 2, 3, 0, 0, 0, 0, 0};
        uint64_t const short_fold =
            UITree_InputSignatureBytes(UITREE_INPUT_SIGNATURE_OFFSET, three, 3);
        uint64_t const padded_fold =
            UITree_InputSignatureBytes(UITREE_INPUT_SIGNATURE_OFFSET, three, 8);
        uint64_t const one = 1;

        if( (*(unsigned char const*)&one) == 1 )
            CHECK(
                short_fold == padded_fold,
                "trailing zeros became visible; a caller folding a "
                "variable-length buffer may now be relying on that");
    }

    /* The same reason, seen through the typed helpers: width is not encoded. */
    CHECK(
        UITree_InputSignatureInt(UITREE_INPUT_SIGNATURE_OFFSET, 5) ==
            UITree_InputSignatureU64(UITREE_INPUT_SIGNATURE_OFFSET, 5),
        "int and u64 stopped colliding on a small value");
}

static void
test_strings_fold_their_length_first(void)
{
    /* The case: two adjacent captions, one character moving between them.
     * Without the length, both spellings fold the same five characters in the
     * same order and the domain is never bumped. */
    uint64_t left = UITREE_INPUT_SIGNATURE_OFFSET;
    uint64_t right = UITREE_INPUT_SIGNATURE_OFFSET;

    left = UITree_InputSignatureString(left, "ab");
    left = UITree_InputSignatureString(left, "cde");
    right = UITree_InputSignatureString(right, "abc");
    right = UITree_InputSignatureString(right, "de");
    CHECK(left != right, "\"ab\"+\"cde\" and \"abc\"+\"de\" hashed the same");

    CHECK(
        UITree_InputSignatureString(UITREE_INPUT_SIGNATURE_OFFSET, NULL) ==
            UITree_InputSignatureString(UITREE_INPUT_SIGNATURE_OFFSET, ""),
        "NULL and the empty string are not the same unset caption");
    CHECK(
        UITree_InputSignatureString(UITREE_INPUT_SIGNATURE_OFFSET, "") !=
            UITree_InputSignatureString(UITREE_INPUT_SIGNATURE_OFFSET, " "),
        "the empty string and a space hashed the same");
}

static void
test_order_and_type_matter(void)
{
    uint64_t forward = UITREE_INPUT_SIGNATURE_OFFSET;
    uint64_t backward = UITREE_INPUT_SIGNATURE_OFFSET;

    forward = UITree_InputSignatureInt(forward, 17);
    forward = UITree_InputSignatureInt(forward, 4);
    backward = UITree_InputSignatureInt(backward, 4);
    backward = UITree_InputSignatureInt(backward, 17);
    CHECK(forward != backward, "two ints folded in either order hashed the same");

    /* A domain seeded per-index must not collide with its neighbour. */
    CHECK(
        UITree_InputSignatureInt(UITREE_INPUT_SIGNATURE_OFFSET, 1) !=
            UITree_InputSignatureInt(UITREE_INPUT_SIGNATURE_OFFSET, 2),
        "two domain seeds collided");

    CHECK(
        UITree_InputSignatureU64(UITREE_INPUT_SIGNATURE_OFFSET, 0) !=
            UITree_InputSignatureU64(UITREE_INPUT_SIGNATURE_OFFSET, 1ull << 63),
        "the top bit of a u64 was lost");
}

int
main(void)
{
    test_every_byte_matters();
    test_it_is_not_fnv1a_and_does_not_encode_length();
    test_strings_fold_their_length_first();
    test_order_and_type_matter();

    if( g_failures )
    {
        printf("uitree_input_signature_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("uitree_input_signature_test: OK\n");
    return 0;
}
