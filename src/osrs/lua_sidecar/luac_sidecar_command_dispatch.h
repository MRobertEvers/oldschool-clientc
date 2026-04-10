#ifndef LUAC_SIDECAR_COMMAND_DISPATCH_H
#define LUAC_SIDECAR_COMMAND_DISPATCH_H

#include <stddef.h>

/** name_utf8: caller-sanitized blob key. Returns 0 on success, -1 on failure. */
int
LuaCSidecar_BlobStore(
    const char* name_utf8,
    const void* data,
    size_t size);

/**
 * On success returns 0; *data_out is malloc'd (or NULL if size is 0) and *size_out is length.
 * On failure returns -1; *data_out and *size_out are cleared.
 */
int
LuaCSidecar_BlobRead(
    const char* name_utf8,
    void** data_out,
    int* size_out);

#endif
