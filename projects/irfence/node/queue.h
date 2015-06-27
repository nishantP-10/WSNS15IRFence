#ifndef QUEUE_H
#define QUEUE_H

#include <nrk.h>

typedef struct {
    uint8_t head;
    uint8_t tail;
    const uint8_t size;
} queue_t;

uint8_t queue_peek(queue_t *q);
uint8_t queue_alloc(queue_t *q);
void queue_enqueue(queue_t *q);
void queue_dequeue(queue_t *q);
bool queue_empty(queue_t *q);
bool queue_full(queue_t *q);

#endif
