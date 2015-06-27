#ifndef RFTOP_H
#define RFTOP_H

#include "rxtx.h"
#include "routes.h"
#include "wgraph.h"

typedef enum {
    DISCOVER_IDLE = 0,
    DISCOVER_SCHEDULED,
    DISCOVER_PENDING,
    DISCOVER_IN_PROGRESS,
    DISCOVER_COMPLETED,
} discover_state_t;

extern nrk_sig_t discover_signal; /* fired periodically */

uint8_t init_rftop(uint8_t priority);
int8_t discover();
discover_state_t get_discover_state();
void reset_discover_state();
uint8_t get_discover_sequence();
route_matrix_t * get_discovered_routes();

void handle_discover_request(pkt_t *pkt);
void handle_discover_response(pkt_t *pkt);

int8_t cmd_rftop(uint8_t argc, char **argv);
int8_t cmd_discover(uint8_t argc, char **argv);
int8_t cmd_probe(uint8_t argc, char **argv);
int8_t cmd_calc_routes(uint8_t argc, char **argv);
int8_t cmd_bc_routes(uint8_t argc, char **argv);

/* Persistant private state: exposed only for config.c */
extern graph network;

#endif // DISCOVER_H
