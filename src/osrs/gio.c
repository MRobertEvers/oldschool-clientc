#include "gio.h"

#include "datastruct/list_macros.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct GIORequest
{
    uint32_t message_id;

    enum GIOStatus status;

    enum GIORequestKind kind;
    uint32_t command;
    uint64_t param_b;
    uint64_t param_a;

    void* data;
    int data_size;

    struct GIORequest* next;
};

struct GIOQueue
{
    uint64_t queue_counter;

    struct GIORequest* requests_list;
};

struct GIOQueue*
gioq_new()
{
    struct GIOQueue* q = malloc(sizeof(struct GIOQueue));
    memset(q, 0, sizeof(struct GIOQueue));
    q->queue_counter = 1;

    return q;
}

void
gioq_free(struct GIOQueue* q)
{
    struct GIORequest* iter = q->requests_list;
    struct GIORequest* next = NULL;

    while( iter )
    {
        next = iter->next;
        free(iter->data);
        free(iter);
        iter = next;
    }

    free(q);
}

bool
gioq_is_empty(struct GIOQueue* q)
{
    return q->requests_list == NULL;
}

uint32_t
gioq_submit(
    struct GIOQueue* q,
    enum GIORequestKind kind,
    uint32_t command,
    uint64_t param_a,
    uint64_t param_b)
{
    struct GIORequest* request_buffer = NULL;
    struct GIORequest* last = NULL;

    request_buffer = (struct GIORequest*)malloc(sizeof(struct GIORequest));
    memset(request_buffer, 0, sizeof(struct GIORequest));

    request_buffer->message_id = q->queue_counter++;
    request_buffer->kind = kind;
    request_buffer->command = command;
    request_buffer->param_a = param_a;
    request_buffer->param_b = param_b;

    last = NULL;
    ll_last(q->requests_list, last);
    if( last == NULL )
        q->requests_list = request_buffer;
    else
        last->next = request_buffer;

    return request_buffer->message_id;
}

void
gioq_release(
    struct GIOQueue* q,
    struct GIOMessage* message)
{
    // Remove from list.
    struct GIORequest* iter = NULL;
    ll_foreach(q->requests_list, iter)
    {
        if( iter->message_id == message->message_id )
        {
            iter->status = GIO_STATUS_FINALIZED;
            break;
        }
    }
}

void*
gioq_adopt(
    struct GIOQueue* q,
    struct GIOMessage* message,
    int* out_data_size)
{
    void* data = NULL;
    int data_size = 0;

    struct GIORequest* iter = NULL;
    ll_foreach(q->requests_list, iter)
    {
        if( iter->message_id == message->message_id )
        {
            iter->status = GIO_STATUS_FINALIZED;
            data = iter->data;
            data_size = iter->data_size;

            iter->data = NULL;
            iter->data_size = 0;
            break;
        }
    }

    if( out_data_size )
        *out_data_size = data_size;

    return data;
}

bool
gioq_poll(
    struct GIOQueue* q,
    struct GIOMessage* out)
{
    struct GIORequest* iter = NULL;
    memset(out, 0, sizeof(struct GIOMessage));
    out->status = GIO_STATUS_PENDING;

    ll_foreach(q->requests_list, iter)
    {
        assert(iter->status != GIO_STATUS_STALE);
        if( iter->status == GIO_STATUS_DONE )
        {
            iter->status = GIO_STATUS_STALE;
            out->message_id = iter->message_id;
            out->status = iter->status;
            out->kind = iter->kind;
            out->command = iter->command;
            out->param_b = iter->param_b;
            out->param_a = iter->param_a;
            out->data = iter->data;
            out->data_size = iter->data_size;
            return true;
        }
    }

    return false;
}

bool
gioq_poll_for(
    struct GIOQueue* q,
    uint32_t req_id)
{
    struct GIORequest* iter = NULL;

    ll_foreach(q->requests_list, iter)
    {
        assert(iter->status != GIO_STATUS_STALE);
        if( iter->status == GIO_STATUS_DONE && iter->message_id == req_id )
        {
            return true;
        }
    }

    return false;
}

bool
gioq_read(
    struct GIOQueue* q,
    uint32_t req_id,
    struct GIOMessage* out)
{
    struct GIORequest* iter = NULL;
    memset(out, 0, sizeof(struct GIOMessage));
    out->status = GIO_STATUS_PENDING;

    ll_foreach(q->requests_list, iter)
    {
        assert(iter->status != GIO_STATUS_STALE);
        if( iter->status == GIO_STATUS_DONE && iter->message_id == req_id )
        {
            iter->status = GIO_STATUS_STALE;
            out->message_id = iter->message_id;
            out->status = iter->status;
            out->kind = iter->kind;
            out->command = iter->command;
            out->param_b = iter->param_b;
            out->param_a = iter->param_a;
            out->data = iter->data;
            out->data_size = iter->data_size;
            return true;
        }
    }

    return false;
}

bool
gioqb_read_next(
    struct GIOQueue* q,
    struct GIOMessage* out)
{
    struct GIORequest* iter = q->requests_list;
    if( out->message_id == 0 )
    {
        if( iter == NULL )
            goto not_found;

        goto found;
    }

    ll_foreach(q->requests_list, iter)
    {
        if( out->message_id == iter->message_id )
        {
            iter = iter->next;
            if( iter == NULL )
                goto not_found;

            goto found;
        }
    }

    if( iter == NULL )
        goto not_found;

found:
    out->message_id = iter->message_id;
    out->status = iter->status;
    out->kind = iter->kind;
    out->command = iter->command;
    out->param_b = iter->param_b;
    out->param_a = iter->param_a;
    out->data = iter->data;
    out->data_size = iter->data_size;

    return true;

not_found:;
    return false;
}

bool
gioqb_mark_inflight(
    struct GIOQueue* q,
    uint32_t message_id)
{
    struct GIORequest* iter = q->requests_list;

    ll_foreach(q->requests_list, iter)
    {
        if( iter->message_id == message_id )
        {
            iter->status = GIO_STATUS_INFLIGHT;
            return true;
        }
    }

    return false;
}

bool
gioqb_mark_done(
    struct GIOQueue* q,
    uint32_t message_id,
    uint32_t command,
    uint64_t param_a,
    uint64_t param_b,
    void* data,
    int data_size)
{
    struct GIORequest* iter = q->requests_list;

    ll_foreach(q->requests_list, iter)
    {
        if( iter->message_id == message_id )
        {
            iter->status = GIO_STATUS_DONE;
            iter->command = command;
            iter->param_a = param_a;
            iter->param_b = param_b;
            iter->data = data;
            iter->data_size = data_size;
            return true;
        }
    }

    return false;
}

void
gioqb_remove_finalized(struct GIOQueue* q)
{
    struct GIORequest* iter = q->requests_list;
    struct GIORequest* previous = NULL;

    while( iter )
    {
        if( iter->status == GIO_STATUS_FINALIZED )
        {
            if( previous )
                previous->next = iter->next;
            else
                q->requests_list = iter->next;
            free(iter->data);
            free(iter);
            iter = previous ? previous->next : q->requests_list;
            continue;
        }
        previous = iter;
        iter = iter->next;
    }
}

// void
// gioqb_remove(struct GIOQueue* q, struct GIOMessage* message)
// {
//     struct GIORequest* iter = q->requests_list;
//     struct GIORequest* previous = NULL;

//     struct GIORequest* item = NULL;

//     ll_foreach(q->requests_list, iter)
//     {
//         if( iter->message_id == message->message_id )
//         {
//             item = iter;
//             if( previous )
//                 previous->next = iter->next;
//             else
//                 q->requests_list = iter->next;
//             break;
//         }
//         previous = iter;
//         iter = iter->next;
//     }

//     if( item )
//     {
//         assert(item->status == GIO_STATUS_FINALIZED);

//         free(item->data);
//         free(item);
//     }
// }