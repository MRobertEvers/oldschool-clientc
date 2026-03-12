/*
 * filestore.c
 *
 * Compiled with Emscripten to a browser WASM module.
 * read_file() is exported and callable from JavaScript via ccall().
 *
 * Since this runs inside a browser there is no real filesystem, so the
 * function returns a hardcoded string.  On a real C platform you would
 * replace the body with fopen/fread/fclose calls.
 */

#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE
const char *read_file(const char *path)
{
    (void)path; /* path accepted for API compatibility; ignored in browser */
    return "{\"source\":\"C/WASM\","
           "\"data\":\"Hello from Emscripten! (hardcoded in C)\"}";
}
