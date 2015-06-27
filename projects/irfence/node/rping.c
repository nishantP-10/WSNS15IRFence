#include <nrk.h>
#include <nrk_error.h>
#include <stdlib.h>

#include "cfg.h"
#include "output.h"
#include "led.h"
#include "ports.h"
#include "config.h"
#include "rpc.h"

#include "rping.h"

enum {
    RPC_INVALID = 0,
    RPC_PING,
} rpc_t;

static const char proc_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [RPC_INVALID] = "<invalid>",
    [RPC_PING] = "ping",
};

#define RPC_PING_REQ_TOKEN_OFFSET 0
#define RPC_PING_REQ_TOKEN_LEN    1

#define RPC_PING_REPLY_TOKEN_OFFSET 0
#define RPC_PING_REPLY_TOKEN_LEN    1

#define RPC_PING_REQ_LEN         RPC_PING_REQ_TOKEN_LEN
#define RPC_PING_REPLY_LEN       RPC_PING_REPLY_TOKEN_LEN

static const char ping_name[] PROGMEM = "ping";

static rpc_proc_t proc_ping;

static rpc_proc_t *procedures[] = {
    [RPC_PING] = &proc_ping,
};
static msg_t server_queue[PING_RPC_SERVER_QUEUE_SIZE];
static msg_t client_queue[RPC_CLIENT_QUEUE_SIZE];
rpc_endpoint_t rping_endpoint = {
    .server = {
        .name = ping_name,
        .listener = {
            .port = PORT_RPC_SERVER_PING,
            .queue = { .size = PING_RPC_SERVER_QUEUE_SIZE },
            .queue_data = server_queue,
        },
        .procedures = procedures,
        .proc_count = sizeof(procedures) / sizeof(procedures[0]),
        .proc_names = &proc_names,
    },
    .client = {
        .name = ping_name,
        .listener = {
            .port = PORT_RPC_CLIENT_PING,
            .queue = { .size = RPC_CLIENT_QUEUE_SIZE },
            .queue_data = client_queue,
        }
    }
};

static uint8_t req_buf[RPC_PING_REQ_LEN];
static uint8_t reply_buf[RPC_PING_REPLY_LEN];

static int8_t rpc_ping(node_id_t node, uint8_t token)
{
    int8_t rc = NRK_OK;
    uint8_t reply_token;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    LOG("call ping rpc\r\n");

    req_buf[RPC_PING_REQ_TOKEN_OFFSET] = token;
    req_len += RPC_PING_REQ_TOKEN_LEN;

    rc = rpc_call(&rping_endpoint.client, node,
                  PORT_RPC_SERVER_PING, RPC_PING, &ping_time_out,
                  req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: ping rpc failed\r\n");
        return rc;
    }
    if (reply_len != RPC_PING_REPLY_LEN) {
        LOG("WARN: ping reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    reply_token = reply_buf[RPC_PING_REPLY_TOKEN_OFFSET];
    if (reply_token != token) {
        LOG("WARN: unexpected token in ping reply\r\n");
        return NRK_ERROR;
    }

    return rc;
}

static int8_t proc_ping(node_id_t requester,
                        uint8_t *req_buf, uint8_t req_len,
                        uint8_t *reply_buf, uint8_t reply_size,
                        uint8_t *reply_len)
{
    uint8_t token;

    if (req_len != RPC_PING_REQ_LEN) {
        LOG("WARN: req of unexpected length: ");
        LOGP("%u/%u\r\n", req_len, RPC_PING_REQ_LEN);
        return NRK_ERROR;
    }

    if (reply_size < RPC_PING_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_PING_REPLY_LEN);
        return NRK_ERROR;
    }

    nrk_led_set(led_proc_ping);
    nrk_wait(pong_delay);
    nrk_led_clr(led_proc_ping);

    token = req_buf[RPC_PING_REQ_TOKEN_OFFSET];

    reply_buf[RPC_PING_REPLY_TOKEN_OFFSET] = token;
    *reply_len = RPC_PING_REPLY_LEN;

    return NRK_OK;
}

int8_t cmd_rping(uint8_t argc, char **argv)
{
    node_id_t recipient;
    uint8_t token;
    int8_t rc;

    if (argc != 2 && argc != 3) {
        OUT("usage: rping <recipient> [<token>]\r\n");
        return NRK_ERROR;
    }

    recipient = atoi(argv[1]);
    token = argc == 3 ? atoi(argv[2]) : 0;

    nrk_led_set(led_awaiting_pong);
    rc = rpc_ping(recipient, token);
    nrk_led_clr(led_awaiting_pong);

    return rc;
}

uint8_t init_rping(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    rpc_init_endpoint(&rping_endpoint);

    ASSERT(num_tasks == NUM_TASKS_RPING);
    return num_tasks;
}
