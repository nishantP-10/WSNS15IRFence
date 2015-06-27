#include <nrk.h>
#include <nrk_error.h>
#include <include.h>
#include <math.h>
#include <stdlib.h>

#include "output.h"
#include "queue.h"
#include "irtop.h"

#include "position.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_LOCALIZATION

/* static */ location_t locations[MAX_NODES];
/* static */ point_t map_dim;

static node_id_t loc_queue_data[MAX_NODES];
static queue_t loc_queue = { .size = MAX_NODES };

static void print_loc_graph(ir_graph_t *graph, location_t *locs)
{
    node_id_t out_node, in_node;
    ir_edge_t *edge;
    bool node_valid;
    location_t *loc;

    COUT("digraph LOC { ");

    COUTA("dim_x="); COUTP("%u; ", map_dim.x);
    COUTA("dim_y="); COUTP("%u; ", map_dim.y);

    /* Lame way to get list of nodes */
    for (in_node = 0; in_node < MAX_NODES; ++in_node) {
        node_valid = false;
        for (out_node = 0; out_node < MAX_NODES; ++out_node) {
            edge = &ir_graph[out_node][in_node];
            if (edge->valid) {
                node_valid = true;
                break;
            }
        }
        if (node_valid) {
            loc = &locs[in_node];
            COUTP("%u ", in_node);
            COUTA(" [");
            COUTP("x=%d,", loc->pt.x);
            COUTP("y=%d", loc->pt.y);
            COUTA("]; ");
        }
    }

    for (in_node = 0; in_node < MAX_NODES; ++in_node) {
        for (out_node = 0; out_node < MAX_NODES; ++out_node) {
            edge = &ir_graph[out_node][in_node];
            if (edge->valid) {
                COUTP("%d -> %d", out_node, in_node);
                COUTA(" [");
                COUTP("d=%u,", edge->dist);
                COUTP("a=%u", edge->angle);
                COUTA("]; ");
            }
        }
    }
    COUTA("}\r\n");
}

static int8_t localize(ir_graph_t *ir_graph, node_id_t ref_node)
{
    uint8_t localized = 0; /* bitmask */
    uint8_t i, k;
    node_id_t node_i, node_j;
    ir_edge_t *edge;
    location_t *loc_i, *loc_j;
    float angle_rad;

    memset(&locations, 0, sizeof(locations));
    memset(&loc_queue, 0, sizeof(loc_queue));
    memset(&loc_queue_data, 0, sizeof(loc_queue_data));

    LOG("localize: ref node ");
    LOGP("%u\r\n", ref_node);

    locations[ref_node].valid = true;
    locations[ref_node].pt.x = 0;
    locations[ref_node].pt.y = 0;
    localized |= 1 << ref_node;

    i = queue_alloc(&loc_queue);
    loc_queue_data[i] = ref_node;
    queue_enqueue(&loc_queue);

    while (!queue_empty(&loc_queue)) {
        i = queue_peek(&loc_queue);
        node_i = loc_queue_data[i];
        queue_dequeue(&loc_queue);
        loc_i = &locations[node_i];

        LOG("localizing neighbors of node ");
        LOGP("%u (%d,%d)\r\n", node_i, loc_i->pt.x, loc_i->pt.y);

        for (node_j = 0; node_j < MAX_NODES; ++node_j) {
            edge = &((*ir_graph)[node_i][node_j]);
            if (edge->valid && !(localized & (1 << node_j))) {
                loc_j = &locations[node_j];
                angle_rad = (float)edge->angle / 180 * M_PI;
                loc_j->valid = true;
                loc_j->pt.x = loc_i->pt.x + edge->dist * cosf(angle_rad);
                loc_j->pt.y = loc_i->pt.y + edge->dist * sinf(angle_rad);

                LOG("localized node: ");
                LOGP("%u [%u,%u] ", node_j, edge->angle, edge->dist);
                LOG(" -> ");
                LOGP("(%d,%d)\r\n", loc_j->pt.x, loc_j->pt.y);

                localized |= 1 << node_j;

                k = queue_alloc(&loc_queue);
                loc_queue_data[k] = node_j;
                queue_enqueue(&loc_queue);
            }
        }
    }

    print_loc_graph(ir_graph, locations);

    return NRK_OK;
}

static void print_locations()
{
    node_id_t node;
    location_t *loc;

    OUT("locations:\r\n");
    for (node = 0; node < MAX_NODES; ++node) {
        loc = &locations[node];
        if (loc->valid)
            OUTP("%u: (%d,%d)\r\n", node, loc->pt.x, loc->pt.y);
    }

    print_loc_graph(&ir_graph, locations);
}

location_t *get_locations() {
    return locations;
}

int8_t cmd_localize(uint8_t argc, char **argv)
{
    int8_t rc;
    ir_graph_t *ir_graph;
    node_id_t ref_node = this_node_id;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: loc [<ref_node>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2)
        ref_node = atoi(argv[1]);

    ir_graph = get_ir_graph();

    rc = localize(ir_graph, ref_node);
    if (rc == NRK_OK)
        print_locations();
    return rc;
}

int8_t cmd_loc(uint8_t argc, char **argv)
{
    node_id_t node;
    location_t *loc;

    if (!(argc == 1 || argc == 2 || argc == 4)) {
        OUT("usage: loc [<node> [<x> <y>]]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2 || argc == 4) { /* set or delete */
        node = atoi(argv[1]);
        if (node >= MAX_NODES) {
            OUT("ERROR: invalid node id\r\n");
            return NRK_ERROR;
        }
        loc = &locations[node];
        if (argc == 4) { /* set */
            loc->valid = true;
            loc->pt.x = atoi(argv[2]);
            loc->pt.y = atoi(argv[3]);
        } else {
            loc->valid = false;
            loc->pt.x = 0;
            loc->pt.y = 0;
        }
    } else {
        print_locations();
    }
    return NRK_OK;
}

int8_t cmd_mapdim(uint8_t argc, char **argv)
{
    if (!(argc == 1 || argc == 3)) {
        OUT("usage: mapdim [<x> <y>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 3) { /* set */
        map_dim.x = atoi(argv[1]);
        map_dim.y = atoi(argv[2]);
    } else {
        OUTP("%d %d\r\n", map_dim.x, map_dim.y);
    }
    return NRK_OK;
}
