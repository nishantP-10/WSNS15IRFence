#include "output.h"

#include "queue.h"

uint8_t queue_peek(queue_t *q)
{
    ASSERT(!queue_empty(q));
    return q->head;
}

uint8_t queue_alloc(queue_t *q)
{
    ASSERT(!queue_full(q));
    return q->tail;
}

void queue_enqueue(queue_t *q)
{
    ASSERT(!queue_full(q));
    q->tail = (q->tail + 1) % q->size;
}

void queue_dequeue(queue_t *q)
{
    ASSERT(!queue_empty(q));
    q->head = (q->head + 1) % q->size;
}

bool queue_empty(queue_t *q)
{
    return q->tail == q->head;
}

bool queue_full(queue_t *q)
{
    return (q->tail + 1) % q->size == q->head;
}
