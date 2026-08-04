#ifndef SRC_NET_MOCK_MOCK_JS5_H
#define SRC_NET_MOCK_MOCK_JS5_H

/*
 * The JS5 (cache download) service.
 *
 * A client opens ONE socket and decides on the first byte what it is for:
 * opcode 14 starts a game login, opcode 15 starts a JS5 session. So JS5 is not
 * a separate server, it is a branch of the login handshake, and it has to live
 * next to it.
 *
 * A vanilla OldSchool client will not reach the login screen without this. It
 * asks for the master index before anything else and treats a failure as
 * "unable to connect to the update server" -- which is why a private server
 * that only implements the game protocol shows a client that never starts.
 *
 * THE PART THAT IS EASY TO GET WRONG: JS5 serves the archive **exactly as it
 * sits on disk**, compression byte and all, with only the two-byte version
 * trailer removed. It is not a decode-and-re-encode. That is why this file
 * reads .idx/.dat2 sectors itself instead of going through rscache, whose whole
 * job is the decompression and decryption that must not happen here.
 *
 * Wire, per RSProt's Js5Service:
 *
 *   in    p1 opcode  (0 prefetch, 1 urgent, 2/3 priority change, 4 xor change)
 *         p1 archive
 *         p2 group
 *
 *   out   p1 archive
 *         p2 group
 *         first 509 bytes of the on-disk archive
 *         then, repeating: p1 0xFF + up to 511 more bytes
 *
 * The 0xFF every 512 bytes is a synchronisation marker the client asserts on;
 * it is not a length and it does not appear at the end.
 *
 * Archive 255 is the reference-table index: group 255 of it is the master
 * index (the list of every archive's version and CRC), and group N is
 * archive N's reference table.
 */

#include <stdint.h>

struct MockJs5;

/** Open a cache directory for JS5 serving. NULL when the directory has no
 *  main_file_cache.dat2, which is a configuration error worth reporting rather
 *  than a client that mysteriously hangs. */
struct MockJs5*
mock_js5_open(char const* cache_dir);

void
mock_js5_close(struct MockJs5* js5);

/**
 * Build the response for one (archive, group) request.
 *
 * Returns the number of bytes written to `out`, 0 when the group does not
 * exist, or -1 when `out_cap` is too small. The caller owns the buffer; a whole
 * response can be a few hundred kilobytes for a large model group, so the
 * buffer is the caller's problem rather than an allocation here.
 */
int
mock_js5_build_response(
    struct MockJs5* js5,
    int archive,
    int group,
    uint8_t* out,
    int out_cap);

/** Largest response the loaded cache can produce, so a caller can size one
 *  buffer once instead of guessing. */
int
mock_js5_max_response_bytes(struct MockJs5* js5);

#endif
