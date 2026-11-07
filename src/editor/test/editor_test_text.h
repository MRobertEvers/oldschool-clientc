#ifndef EDITOR_TEST_TEXT_H
#define EDITOR_TEST_TEXT_H

/*
 * A content file as the editor reads it (editor_host_local.c, "Line endings"):
 * a CRLF file -- Git for Windows checks the content tree out CRLF by default,
 * and Python writes text files CRLF there -- is read as LF. The tests that read
 * the tree or a tool's output directly, then compare bytes with what the editor
 * emits, read through this, so they hold the editor to the same text the host
 * hands it on every platform. Every "\r\n" becomes "\n"; a lone '\r' is data.
 */

#include <stddef.h>

static inline void
editor_test_text_lf(
    char* data,
    size_t* size)
{
    size_t out = 0;

    for( size_t at = 0; at < *size; at++ )
    {
        if( data[at] == '\r' && at + 1 < *size && data[at + 1] == '\n' )
            continue;
        data[out++] = data[at];
    }
    data[out] = '\0';
    *size = out;
}

#endif
