/*
 * rsprot unity build anchor — compile this single file to build the library.
 * See include/rsprot.h for the public surface and README.md for scope/status.
 */

// clang-format off
#include "src/rsprot_buf.c"
#include "src/rsprot_crypto.c"
#include "src/rsprot_compression.c"
// clang-format on
