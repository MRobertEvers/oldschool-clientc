#include "retained_alloc_tracker.h"

#include <stdint.h>
#include <stdlib.h>

#define RETAINED_ALLOC_MAGIC 0x5254414Cu

union RetainedAllocHeader
{
    struct
    {
        union RetainedAllocHeader* previous;
        union RetainedAllocHeader* next;
        size_t size;
        uint32_t magic;
    } fields;
    long double alignment;
    void* pointer_alignment;
};

static union RetainedAllocHeader* retained_alloc_head;
static struct RetainedAllocSnapshot retained_alloc_totals;

static void
retained_alloc_link(union RetainedAllocHeader* header)
{
    header->fields.previous = NULL;
    header->fields.next = retained_alloc_head;
    if( retained_alloc_head )
        retained_alloc_head->fields.previous = header;
    retained_alloc_head = header;
}

static void
retained_alloc_unlink(union RetainedAllocHeader* header)
{
    if( header->fields.previous )
        header->fields.previous->fields.next = header->fields.next;
    else
        retained_alloc_head = header->fields.next;
    if( header->fields.next )
        header->fields.next->fields.previous = header->fields.previous;
}

void*
retained_alloc_malloc(size_t size)
{
    union RetainedAllocHeader* header;
    if( size > SIZE_MAX - sizeof(*header) )
        return NULL;
    header = (union RetainedAllocHeader*)malloc(sizeof(*header) + size);
    if( !header )
        return NULL;
    header->fields.size = size;
    header->fields.magic = RETAINED_ALLOC_MAGIC;
    retained_alloc_link(header);
    retained_alloc_totals.live_blocks++;
    retained_alloc_totals.live_bytes += size;
    if( retained_alloc_totals.live_blocks > retained_alloc_totals.peak_blocks )
        retained_alloc_totals.peak_blocks = retained_alloc_totals.live_blocks;
    if( retained_alloc_totals.live_bytes > retained_alloc_totals.peak_bytes )
        retained_alloc_totals.peak_bytes = retained_alloc_totals.live_bytes;
    return (void*)(header + 1);
}

void*
retained_alloc_calloc(size_t count, size_t size)
{
    size_t bytes;
    void* pointer;
    if( size != 0u && count > SIZE_MAX / size )
        return NULL;
    bytes = count * size;
    pointer = retained_alloc_malloc(bytes);
    if( pointer )
    {
        unsigned char* out = (unsigned char*)pointer;
        size_t index;
        for( index = 0u; index < bytes; index++ )
            out[index] = 0u;
    }
    return pointer;
}

void*
retained_alloc_realloc(void* pointer, size_t size)
{
    union RetainedAllocHeader* old_header;
    union RetainedAllocHeader* new_header;
    union RetainedAllocHeader* previous;
    union RetainedAllocHeader* next;
    size_t old_size;

    if( !pointer )
        return retained_alloc_malloc(size);
    if( size == 0u )
    {
        retained_alloc_free(pointer);
        return NULL;
    }
    if( size > SIZE_MAX - sizeof(*new_header) )
        return NULL;

    old_header = ((union RetainedAllocHeader*)pointer) - 1;
    if( old_header->fields.magic != RETAINED_ALLOC_MAGIC )
        abort();
    old_size = old_header->fields.size;
    previous = old_header->fields.previous;
    next = old_header->fields.next;
    new_header =
        (union RetainedAllocHeader*)realloc(old_header, sizeof(*new_header) + size);
    if( !new_header )
        return NULL;

    new_header->fields.previous = previous;
    new_header->fields.next = next;
    new_header->fields.size = size;
    new_header->fields.magic = RETAINED_ALLOC_MAGIC;
    if( previous )
        previous->fields.next = new_header;
    else
        retained_alloc_head = new_header;
    if( next )
        next->fields.previous = new_header;

    retained_alloc_totals.live_bytes -= old_size;
    retained_alloc_totals.live_bytes += size;
    if( retained_alloc_totals.live_bytes > retained_alloc_totals.peak_bytes )
        retained_alloc_totals.peak_bytes = retained_alloc_totals.live_bytes;
    return (void*)(new_header + 1);
}

void
retained_alloc_free(void* pointer)
{
    union RetainedAllocHeader* header;
    if( !pointer )
        return;
    header = ((union RetainedAllocHeader*)pointer) - 1;
    if( header->fields.magic != RETAINED_ALLOC_MAGIC )
        abort();
    retained_alloc_unlink(header);
    retained_alloc_totals.live_blocks--;
    retained_alloc_totals.live_bytes -= header->fields.size;
    header->fields.magic = 0u;
    free(header);
}

struct RetainedAllocSnapshot
retained_alloc_snapshot(void)
{
    return retained_alloc_totals;
}
