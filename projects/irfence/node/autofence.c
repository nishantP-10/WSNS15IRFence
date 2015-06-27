#include <nrk.h>
#include <nrk_error.h>
#include <include.h>
#include <ulib.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "cfg.h"
#include "output.h"
#include "position.h"
#include "fence.h"
#include "irtop.h"

#include "autofence.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_FENCE

/* TODO: this is a bit superfluous, used by cmd for testing only */
static fence_t max_fence;

int8_t calc_max_fence(fence_t *fence)
{
    uint8_t i;
    LOG("calculating max area fence\r\n");

    ir_graph_t *ir_graph = get_ir_graph();
    location_t *locations = get_locations();
    node_id_t node, next_node;
    uint8_t used_nodes = 0x0;
    uint16_t max_angle;
    uint8_t min_y = 0;
    ir_edge_t *edge;
    location_t *loc;

    memset(&max_fence, 0, sizeof(max_fence));

    next_node = INVALID_NODE_ID;
    for (i = 0; i < MAX_NODES; ++i) {
        loc = &locations[i];
        if (loc->valid) {
            if (next_node == INVALID_NODE_ID || min_y > loc->pt.y) {
                min_y = loc->pt.y;
                next_node = i;
            }
        }
    }

    if (next_node == INVALID_NODE_ID) {
        WARN("no nodes in IR graph\r\n");
        return NRK_ERROR;
    }

    do {
        node = next_node;
        used_nodes |= 1 << node;
        fence->posts[fence->len++] = node;
        LOG("added fence pole: "); LOGP("%u\r\n", node);

        max_angle = 0;
        next_node = INVALID_NODE_ID;
        for (i = 0; i < MAX_NODES; ++i) { /* find "right-most" neighbor */
            edge = &(*ir_graph)[node][i];

            if (edge->valid) {
                LOG("checking neighbor: "); LOGP("%u", i);
                LOGA(" angle "); LOGP("%u", edge->angle);
                LOGA(" max "); LOGP("%u", max_angle);
                LOGA(" used "); LOGP("%x", used_nodes);
                LOGA("\r\n");

                if ((!(used_nodes & (1 << i))) && edge->angle > max_angle) {
                    max_angle = edge->angle;
                    next_node = i;
                }
            }
        }
    } while (next_node != INVALID_NODE_ID);

    return NRK_OK;
}

int8_t cmd_mfence(uint8_t argc, char **argv)
{
    int8_t rc;
    uint8_t i;

    rc = calc_max_fence(&max_fence);
    if (rc == NRK_OK) {
        for(i = 0; i < max_fence.len; i++)
            OUTP("%d ", max_fence.posts[i]);
        OUT("\r\n");
    }
    return rc;
}
