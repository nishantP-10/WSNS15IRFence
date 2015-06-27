#ifndef RPC_H
#define RPC_H

#include <nrk.h>

#include "node_id.h"
#include "router.h"

typedef int8_t rpc_proc_t(node_id_t requester,
                                uint8_t *req_buf, uint8_t req_len,
                                uint8_t *reply_buf, uint8_t reply_size,
                                uint8_t *reply_len);

typedef struct {
    const char *name; /* in prog name */
    uint8_t port;
    msg_t msg;
    listener_t listener;
    uint8_t seq;
} rpc_client_t;

typedef struct {
    const char *name; /* in prog mem */
    listener_t listener;
    uint8_t seq[MAX_NODES]; /* last handled seq */
    msg_t reply_msg;
    rpc_proc_t **procedures;
    uint8_t proc_count;
    const char (*proc_names)[][MAX_ENUM_NAME_LEN];
} rpc_server_t;

typedef struct {
    rpc_server_t server;
    rpc_client_t client;
} rpc_endpoint_t;

void rpc_init_client(rpc_client_t *client);
void rpc_init_server(rpc_server_t *server);
void rpc_activate_server(rpc_server_t *server);
int8_t rpc_call(rpc_client_t *client,
                node_id_t node, uint8_t port, uint8_t id,
                nrk_time_t *timeout,
                uint8_t *req_buf, uint8_t req_len,
                uint8_t *reply_buf, uint8_t *reply_len);
void rpc_serve(rpc_server_t *server);
void rpc_server_loop(rpc_server_t *server);
nrk_sig_mask_t rpc_wait_mask(rpc_server_t *server);

/* Alternative to rpc_init_{client,server} */
void rpc_init_endpoint(rpc_endpoint_t *endpoint);
void rpc_activate_endpoint(rpc_endpoint_t *endpoint);

#endif
