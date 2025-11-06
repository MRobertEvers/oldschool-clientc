#ifndef DASH3D_INTQUEUE_U_C
#define DASH3D_INTQUEUE_U_C

#include <assert.h>
#include <stdlib.h>

struct IntQueue
{
    int* data;
    int length;
    int capacity;

    int head;
    int tail;
};

static inline void
int_queue_init(struct IntQueue* queue, int capacity)
{
    queue->data = (int*)malloc(capacity * sizeof(int));
    queue->length = 0;
    queue->capacity = capacity;
}

static inline void
int_queue_push_wrap(struct IntQueue* queue, int value)
{
    int next_tail = (queue->tail + 1) % queue->capacity;
    assert(next_tail != queue->head);

    queue->data[queue->tail] = value << 8;
    queue->tail = next_tail;
    queue->length++;
}

static inline void
int_queue_push_wrap_prio(struct IntQueue* queue, int value, int prio)
{
    int next_tail = (queue->tail + 1) % queue->capacity;
    assert(next_tail != queue->head);

    queue->data[queue->tail] = (value << 8) | prio;
    queue->tail = next_tail;
    queue->length++;
}

static inline int
int_queue_pop(struct IntQueue* queue)
{
    assert((queue->head) != queue->tail);

    int value = queue->data[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->length--;

    return value;
}

static inline void
int_queue_free(struct IntQueue* queue)
{
    free(queue->data);
}

#endif