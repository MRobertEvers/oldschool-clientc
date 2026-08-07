/*
 * rsprot unity build anchor — compile this single file to build the library.
 * See include/rsprot.h for the public surface and README.md for scope/status.
 */

/*
 * rsprot_buf.c is NOT here, and that is the point.
 *
 * It is the one part of this library with no dependencies of its own, and it is
 * the part everything else wants: the packet codecs, the executor, and now the
 * net stack's decoders all need a buffer and none of them need crypto. While it
 * lived in this TU, linking a buffer pulled in rsprot_crypto.c, which calls
 * src/net's ISAAC and RSA, which needs tommath — so a test that decodes two
 * bytes failed to link against a bignum library. That happened three separate
 * times before the split.
 *
 * It is now its own object (`$(OBJ_DIR)/rsprot_buf.o`), linked by everything;
 * this TU is the parts that have dependencies. Keep it out of here — including
 * it in both places is a duplicate-symbol link failure, which is at least loud.
 */

// clang-format off
#include "src/rsprot_crypto.c"
#include "src/rsprot_compression.c"
// clang-format on
