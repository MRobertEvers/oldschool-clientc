#include "mem_format.h"

#include <stdio.h>

void
mem_format_bytes(char* buf, size_t buf_size, size_t bytes)
{
    if( !buf || buf_size == 0 )
        return;

    const double gb = 1024.0 * 1024.0 * 1024.0;
    const double mb = 1024.0 * 1024.0;
    const double kb = 1024.0;

    if( bytes >= (size_t)1024 * 1024 * 1024 )
        snprintf(buf, buf_size, "%.2fGB", (double)bytes / gb);
    else if( bytes >= (size_t)1024 * 1024 )
        snprintf(buf, buf_size, "%.1fMB", (double)bytes / mb);
    else if( bytes >= 1024 )
        snprintf(buf, buf_size, "%.1fKB", (double)bytes / kb);
    else
        snprintf(buf, buf_size, "%zuB", bytes);
}

void
mem_format_heap_triple(
    char* buf,
    size_t buf_size,
    size_t heap_used,
    size_t heap_total,
    size_t heap_peak)
{
    if( !buf || buf_size == 0 )
        return;

    char u[48];
    char t[48];
    char p[48];
    mem_format_bytes(u, sizeof u, heap_used);
    mem_format_bytes(t, sizeof t, heap_total);
    mem_format_bytes(p, sizeof p, heap_peak);
    snprintf(buf, buf_size, "%s / %s / %s", u, t, p);
}
