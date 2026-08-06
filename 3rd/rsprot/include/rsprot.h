#ifndef RSPROT_H
#define RSPROT_H

/*
 * rsprot — C port of https://github.com/2004Scape/RSProt (buffer / crypto /
 * compression / protocol tables + codecs). Include this single header;
 * compile rsprot_unity.c once to build the library.
 *
 * See 3rd/rsprot/README.md for scope, status, and how to regenerate the
 * per-revision tables and codecs from the vendored Kotlin source.
 */

// clang-format off
#include "../src/rsprot_buf.h"
#include "../src/rsprot_crypto.h"
#include "../src/rsprot_compression.h"
#include "../gen/rsprot_tables.h"
// clang-format on

#endif /* RSPROT_H */
