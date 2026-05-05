#include "osrs/revconfig/uitree_textpool.h"

#include <stdlib.h>
#include <string.h>

void
uitree_textpool_reset(struct UITreeTextPool* p)
{
    if( p )
        p->used = 0;
}

void
uitree_textpool_free(struct UITreeTextPool* p)
{
    if( !p )
        return;
    free(p->buf);
    p->buf  = NULL;
    p->cap  = 0;
    p->used = 0;
}

char*
uitree_textpool_push_dup(struct UITreeTextPool* p, char const* src, size_t len)
{
    if( !p )
        return NULL;
    if( len > 0 && !src )
        return NULL;
    size_t need = len + 1;
    if( p->used + need > p->cap )
    {
        size_t nc = p->cap ? p->cap * 2 : 4096u;
        while( p->used + need > nc )
            nc *= 2;
        char* nb = (char*)realloc(p->buf, nc);
        if( !nb )
            return NULL;
        p->buf = nb;
        p->cap = nc;
    }
    char* dst = p->buf + p->used;
    if( len > 0 )
        memcpy(dst, src, len);
    dst[len] = '\0';
    p->used += need;
    return dst;
}
