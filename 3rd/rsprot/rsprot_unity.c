/*
 * rsprot unity build anchor — compile this single file to build the library.
 * See include/rsprot.h for the public surface and README.md for scope/status.
 */

/*
 * rsprot_buf.c is its own object, linked by everything that reads or writes a
 * buffer; the executor (src/rsprot_exec.c), its storage (src/rsprot_storage.c)
 * and the codecs (rsprot_packets_unity.c) are too. This TU is crypto and
 * compression. Including rsprot_buf.c here as well is a duplicate-symbol link
 * failure, which is at least loud.
 */

// clang-format off
#include "src/rsprot_crypto.c"
#include "src/rsprot_compression.c"
// clang-format on
