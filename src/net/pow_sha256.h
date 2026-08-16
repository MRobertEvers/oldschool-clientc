#ifndef SRC_NET_POW_SHA256_H
#define SRC_NET_POW_SHA256_H

/*
 * Revision-239 login proof of work.
 *
 * The server may answer GAMELOGIN with PROOF_OF_WORK (opcode 69) instead of a
 * verdict:
 *
 *   p1  challenge type   (0 = SHA-256; the only one that exists)
 *   p1  version          (only 1 is supported)
 *   p1  difficulty       (leading zero BITS required; live OldSchool uses 18)
 *   pjstr salt
 *
 * The client must find a 64-bit `result` such that
 *
 *   sha256( hex(version) + hex(difficulty) + salt + hex(result) )
 *
 * has at least `difficulty` leading zero bits, and reply with POW_REPLY
 * (opcode 19, p8 result). Difficulty is in bits, so each step doubles the
 * expected work: 18 is about 260k hashes, 24 would be about 17 million.
 *
 * `hex` here is Java's Integer.toHexString / Long.toHexString: lowercase, no
 * leading zeros, and — the part that is easy to get wrong — no "0x" and no
 * sign extension, so a negative long prints as its unsigned 64-bit hex.
 * Searching from 0 upward keeps every candidate positive and side-steps that.
 */

#include <stddef.h>
#include <stdint.h>

/** Build the challenge's base string: hex(version) + hex(difficulty) + salt.
 *  Returns the length written, or -1 if it would not fit. */
int
pow_sha256_base_string(
    int version,
    int difficulty,
    char const* salt,
    char* out,
    size_t out_size);

/** True when `result` solves the challenge. */
int
pow_sha256_verify(
    int version,
    int difficulty,
    char const* salt,
    uint64_t result);

/**
 * Search for a solution, trying candidates 0, 1, 2, ... in order.
 *
 * `max_attempts` bounds the search so a difficulty nobody expected cannot hang
 * the login thread forever; 0 means "no bound". Returns 1 and writes
 * `*out_result` on success, 0 when the bound was hit first.
 */
int
pow_sha256_solve(
    int version,
    int difficulty,
    char const* salt,
    uint64_t max_attempts,
    uint64_t* out_result);

#endif
