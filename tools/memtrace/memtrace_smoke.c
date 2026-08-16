/**
 * Minimal malloc instrumentation smoke test (MEMTRACE=1).
 * Build: cc -DTORIRS_MEMTRACE=1 -I../../.. torirs_memtrace.c memtrace_smoke.c \
 *   -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
 *   -Wl,--wrap=reallocf -Wl,--wrap=posix_memalign -Wl,--wrap=strdup -ldl -o memtrace_smoke
 * macOS: cc -DTORIRS_MEMTRACE=1 -I../../.. torirs_memtrace.c memtrace_smoke.c -ldl -o memtrace_smoke
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
    void* a = malloc(128);
    void* b = calloc(4, 32);
    char* s = strdup("hello memtrace");
    void* c = realloc(a, 256);
    (void)b;
    (void)s;
    free(c);
    free(b);
    printf("memtrace_smoke ok\n");
    return 0;
}
