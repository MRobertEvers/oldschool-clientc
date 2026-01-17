#ifndef GIO_H
#define GIO_H

#include <stdbool.h>
#include <stdint.h>

struct GIOQueue;

enum GIORequestKind
{
    GIO_REQ_INIT,
    GIO_REQ_ASSET,
};

enum GIOStatus
{
    GIO_STATUS_PENDING,
    GIO_STATUS_DONE,
    GIO_STATUS_INFLIGHT,
    GIO_STATUS_STALE,
    GIO_STATUS_FINALIZED,
    GIO_STATUS_ERROR,
};

struct GIOMessage
{
    uint32_t message_id;
    enum GIOStatus status;
    enum GIORequestKind kind;

    uint32_t command;
    uint64_t param_b;
    uint64_t param_a;

    void* data;
    int data_size;
};

struct GIOQueue*
gioq_new(void);
void
gioq_free(struct GIOQueue* q);

/* Submit multiple requests */
uint32_t
gioq_submit(
    struct GIOQueue* q,
    enum GIORequestKind kind,
    uint32_t command,
    uint64_t param_b,
    uint64_t param_a);

void
gioq_release(
    struct GIOQueue* q,
    struct GIOMessage* message);

/* Poll a single request */
bool
gioq_poll(
    struct GIOQueue* q,
    struct GIOMessage* out);

bool
gioqb_read_next(
    struct GIOQueue* q,
    struct GIOMessage* out);
bool
gioqb_mark_inflight(
    struct GIOQueue* q,
    uint32_t message_id);
bool
gioqb_mark_done(
    struct GIOQueue* q,
    uint32_t message_id,
    uint32_t command,
    uint64_t param_b,
    uint64_t param_a,
    void* data,
    int data_size);

void
gioqb_remove_finalized(struct GIOQueue* q);

#endif