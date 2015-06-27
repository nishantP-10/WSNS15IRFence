#ifndef ROUTER_H
#define ROUTER_H

#include "cfg.h"
#include "node_id.h"
#include "queue.h"

typedef uint8_t port_t;

typedef struct {
    node_id_t sender;
    node_id_t recipient;
    uint8_t port;
    uint8_t type;
    uint8_t payload[MAX_MSG_SIZE];
    uint8_t len;
} msg_t;

typedef struct {
    port_t port;
    queue_t queue;
    msg_t *queue_data;
    nrk_sig_t signal;
    bool active;
} listener_t;

uint8_t init_router(uint8_t priority);
void init_message(msg_t *msg);
int8_t send_message(msg_t *msg);
void register_listener(listener_t *listener);
void activate_listener(listener_t *listener);
void deactivate_listener(listener_t *listener);

int8_t cmd_route(uint8_t argc, char **argv);
int8_t cmd_set_routes(uint8_t argc, char **argv);
int8_t cmd_ping(uint8_t argc, char **argv);

/* Persistant private state: exposed only for config.c */
extern node_id_t routes[MAX_NODES];

#endif // ROUTER_H
