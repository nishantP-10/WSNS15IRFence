#include <nrk.h>
#include <stdlib.h>

#include "output.h"

#include "nodelist.h"

#define NL_LOG(msg)  CLOG(list->log_cat, msg)
#define NL_LOGA(msg) CLOGA(list->log_cat, msg)
#define NL_LOGP(...) CLOGP(list->log_cat, __VA_ARGS__)
#define NL_LOGF(msg) CLOGF(list->log_cat, msg)

uint8_t nodelist_add(nodelist_t *list, node_id_t id)
{
    int8_t idx = -1;

    ASSERT(list);
    ASSERT(list->len <= list->size);

    if (list->len < list->size) {
        idx = list->len++;
    } else {
        idx = rand() % list->len; /* pick a victim to replace */
        NL_LOG("list '"); NL_LOGF(list->name);
        NL_LOGA("': evicted node: ");
        NL_LOGP("%u at %u\r\n", list->nodes[idx], idx);
    }

    list->nodes[idx] = id;

    NL_LOG("list '"); NL_LOGF(list->name);
    NL_LOGA("' added node: ");
    NL_LOGP("%u at %u\r\n", list->nodes[idx], idx);

    return idx;
}

void nodelist_clear(nodelist_t *list)
{
    ASSERT(list);
    ASSERT(list->len <= list->size);

    list->len = 0;
    memset(list->nodes, 0, list->size);
}

int8_t nodelist_find(nodelist_t *list, node_id_t id)
{
    int8_t i;

    ASSERT(list);
    ASSERT(list->len < list->size);

    for (i = 0; i < list->len; ++i)
        if (list->nodes[i] == id)
            return i;
    return -1;
}
