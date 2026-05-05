#ifndef MEM_FORMAT_H
#define MEM_FORMAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Writes a compact human-readable size into `buf` (e.g. "20.4MB", "512B"). */
void mem_format_bytes(char* buf, size_t buf_size, size_t bytes);

/** Writes "used / total / peak" using mem_format_bytes for each value. */
void mem_format_heap_triple(
    char* buf,
    size_t buf_size,
    size_t heap_used,
    size_t heap_total,
    size_t heap_peak);

#ifdef __cplusplus
}
#endif

#endif
