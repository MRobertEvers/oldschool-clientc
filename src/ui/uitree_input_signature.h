#ifndef SRC_UI_UITREE_INPUT_SIGNATURE_H
#define SRC_UI_UITREE_INPUT_SIGNATURE_H

/**
 * Compare-before-bump signatures for the retained tree's host inputs.
 *
 * A host request copies ambient client state into a descriptor the tree would
 * otherwise retain unchanged. The emit walk records which coarse domains it
 * actually read, and this is how those domains get a version: fold every value
 * a request could expose into one word, compare it with last frame's, and bump
 * the domain only when the word moves.
 *
 * These are NOT render caches and nothing is persisted. The only property the
 * callers rely on is that a byte that changed changes the word -- each
 * xor-multiply step is a bijection in the word, so a single-word difference can
 * never cancel.
 *
 * Eight bytes per multiply, not one. The fold runs every frame over sizeof
 * (slots), the chat view, the IF_SETEVENTS table and the minimenu, and a
 * 64-bit multiply is three 32-bit ones on armv7 -- per BYTE that was about
 * 0.08 ms a frame on the Moto X.
 *
 * Start a domain from UITREE_INPUT_SIGNATURE_OFFSET (FNV-1a's 64-bit basis)
 * and thread the result through each value in turn.
 *
 * Two things this is NOT, both easy to assume from the basis and the prime:
 *
 *  - it is not FNV-1a, and does not agree with it on any input. FNV mixes one
 *    byte per multiply; this mixes eight.
 *  - a raw byte fold does not encode its own length. Three bytes and the same
 *    three followed by zeros give the same word, and so do a 4-byte and an
 *    8-byte copy of the same small number. That is harmless here because each
 *    domain threads a fixed sequence of fixed-width fields -- and it is why
 *    UITree_InputSignatureString has to fold the length itself. Anything else
 *    genuinely variable-length must do the same.
 *
 * Both are pinned in ui/test/uitree_input_signature_test.c.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UITREE_INPUT_SIGNATURE_OFFSET 1469598103934665603ull
#define UITREE_INPUT_SIGNATURE_PRIME 1099511628211ull

static inline uint64_t
UITree_InputSignatureBytes(
    uint64_t hash,
    void const* data,
    size_t size)
{
    unsigned char const* bytes = (unsigned char const*)data;
    size_t i = 0;

    while( i + sizeof(uint64_t) <= size )
    {
        uint64_t word;
        memcpy(&word, bytes + i, sizeof(word));
        hash ^= word;
        hash *= UITREE_INPUT_SIGNATURE_PRIME;
        i += sizeof(word);
    }
    if( i < size )
    {
        uint64_t word = 0;
        memcpy(&word, bytes + i, size - i);
        hash ^= word;
        hash *= UITREE_INPUT_SIGNATURE_PRIME;
    }
    return hash;
}

static inline uint64_t
UITree_InputSignatureInt(
    uint64_t hash,
    int value)
{
    return UITree_InputSignatureBytes(hash, &value, sizeof(value));
}

static inline uint64_t
UITree_InputSignatureU64(
    uint64_t hash,
    uint64_t value)
{
    return UITree_InputSignatureBytes(hash, &value, sizeof(value));
}

/**
 * A string, length first.
 *
 * The length is folded in separately so that "ab" + "c" and "a" + "bc" are
 * different signatures; without it a pair of adjacent fields could swap a
 * character between them and the domain would never be bumped. NULL is the
 * empty string, which is what an unset caption is.
 */
static inline uint64_t
UITree_InputSignatureString(
    uint64_t hash,
    char const* value)
{
    size_t length = value ? strlen(value) : 0;

    hash = UITree_InputSignatureBytes(hash, &length, sizeof(length));
    if( length )
        hash = UITree_InputSignatureBytes(hash, value, length);
    return hash;
}

#endif /* SRC_UI_UITREE_INPUT_SIGNATURE_H */
