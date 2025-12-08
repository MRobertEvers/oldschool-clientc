#include "gio.h"

#include "datastruct/list_macros.h"

#include <stdlib.h>
#include <string.h>

enum GIOStatus
{
    GIO_STATUS_PENDING,
    GIO_STATUS_DONE,
    GIO_STATUS_INFLIGHT,
    GIO_STATUS_FINALIZED,
    GIO_STATUS_ERROR,
};

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

uint32_t
gioq_submit(
    struct GIOQueue* q,
    enum GIORequestKind kind,
    uint32_t command,
    uint64_t param_b,
    uint64_t param_a)
{
    struct GIORequest* request_buffer = NULL;
    struct GIORequest* last = NULL;

    request_buffer = (struct GIORequest*)malloc(sizeof(struct GIORequest));
    memset(request_buffer, 0, sizeof(struct GIORequest));

    request_buffer->message_id = q->queue_counter++;
    request_buffer->kind = kind;
    request_buffer->command = command;
    request_buffer->param_b = param_b;
    request_buffer->param_a = param_a;

    last = NULL;
    ll_last(q->requests_list, last);
    if( last == NULL )
        q->requests_list = request_buffer;
    else
        last->next = request_buffer;

    return request_buffer->message_id;
}

void
gioq_release(struct GIOQueue* q, struct GIOMessage* message)
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

bool
gioq_poll(struct GIOQueue* q, struct GIOMessage* out)
{
    struct GIORequest* iter = NULL;

    ll_foreach(q->requests_list, iter)
    {
        if( iter->status == GIO_STATUS_DONE )
        {
            out->message_id = iter->message_id;
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
gioqb_read_next(struct GIOQueue* q, struct GIOMessage* out)
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

found:
    out->message_id = iter->message_id;
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
gioqb_mark_inflight(struct GIOQueue* q, uint32_t message_id)
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
    uint64_t param_b,
    uint64_t param_a,
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
            iter->param_b = param_b;
            iter->param_a = param_a;
            iter->data = data;
            iter->data_size = data_size;
            return true;
        }
    }

    return false;
}