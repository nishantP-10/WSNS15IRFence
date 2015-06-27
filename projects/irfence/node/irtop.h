#ifndef IRTOP_H
#define IRTOP_H

#include "node_id.h"

typedef struct {
    bool valid;
    uint8_t led;
    uint8_t receivers; /* bitmask */
} ir_neighbor_t;

typedef struct {
    bool valid;
    uint16_t dist;
    uint16_t angle;
} ir_edge_t;

typedef ir_edge_t ir_graph_t[MAX_NODES][MAX_NODES];


ir_neighbor_t *get_ir_neighbor(node_id_t node);
ir_graph_t *get_ir_graph();

uint8_t init_irtop(uint8_t priority);

int8_t cmd_irhood(uint8_t argc, char **argv);
int8_t cmd_irtop(uint8_t argc, char **argv);
int8_t cmd_irprobe(uint8_t argc, char **argv);
int8_t cmd_irdiscover(uint8_t argc, char **argv);

/* Persistant private state: exposed only for config.c */
extern ir_graph_t ir_graph;
extern ir_neighbor_t ir_neighbors[MAX_NODES];

#endif
