#ifndef NODELIST_H
#define NODELIST_H

#include "node_id.h"
#include "output.h"

typedef struct {
    const char *name;
    log_category_t log_cat;
    node_id_t *nodes;
    uint8_t size;
    uint8_t len;
} nodelist_t;

uint8_t nodelist_add(nodelist_t *list, node_id_t id);
int8_t nodelist_find(nodelist_t *list, node_id_t id);
void nodelist_clear(nodelist_t *list);

#endif
