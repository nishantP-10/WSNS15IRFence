#include <nrk.h>
#include <nrk_error.h>
#include <stdlib.h>

#include "cfg.h"
#include "config.h"
#include "rpc.h"
#include "command.h"
#include "output.h"
#include "ports.h"

#include "rcmd.h"

enum {
    RPC_INVALID = 0,
    RPC_RCMD,
} rpc_t;

static const char proc_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [RPC_INVALID] = "<invalid>",
    [RPC_RCMD] = "rcmd",
};


static const char rcmd_name[] PROGMEM = "rcmd";

static rpc_proc_t proc_rcmd;

static rpc_proc_t *procedures[] = {
    [RPC_RCMD] = &proc_rcmd,
};
static msg_t server_queue[RCMD_RPC_SERVER_QUEUE_SIZE];
static msg_t client_queue[RPC_CLIENT_QUEUE_SIZE];
rpc_endpoint_t rcmd_endpoint = {
    .server = {
        .name = rcmd_name,
        .listener = {
            .port = PORT_RPC_SERVER_RCMD,
            .queue = { .size = RCMD_RPC_SERVER_QUEUE_SIZE },
            .queue_data = server_queue,
        },
        .procedures = procedures,
        .proc_count = sizeof(procedures) / sizeof(procedures[0]),
        .proc_names = &proc_names,
    },
    .client = {
        .name = rcmd_name,
        .listener = {
            .port = PORT_RPC_CLIENT_RCMD,
            .queue = { .size = RPC_CLIENT_QUEUE_SIZE },
            .queue_data = client_queue,
        }
    }
};

static char cmd_buf[MAX_CMD_LEN];
static uint8_t reply_buf[1 /* dummy, since 0 causes a crash (?) */];
static char *argv[MAX_ARGS]; /* array of ptrs into 'cmd' */

static int8_t rpc_rcmd(node_id_t node, char *cmd)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    LOG("remote cmd to "); LOGP("%u: ", node);
    LOGP("%s\r\n", cmd);

    req_len += strlen(cmd) + 1; /* include null byte */

    rc = rpc_call(&rcmd_endpoint.client, node,
                  PORT_RPC_SERVER_RCMD, RPC_RCMD, &rcmd_rpc_time_out,
                  (uint8_t *)cmd_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: rcmd rpc failed\r\n");
        return rc;
    }

#if 0 /* TODO: make cmds return a code */
    if (reply_len != RPC_RCMD_REPLY_LEN) {
        LOG("WARN: rcmd reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    rc = reply_buf[RPC_RCMD_REPLY_RC_OFFSET];
    LOG("rpc rcmd rc: "); LOGP("%d\r\n", rc);
#endif

    return NRK_OK;
}

static int8_t proc_rcmd(node_id_t requester,
                        uint8_t *req_buf, uint8_t req_len,
                        uint8_t *reply_buf, uint8_t reply_size,
                        uint8_t *reply_len)
{
    cmd_handler_t handler;
    uint8_t argc;

    LOG("rcv rcmd req from "); LOGP("%u", requester); LOGA(": ");
    LOGP("%s\r\n", req_buf); /* TODO: check that it is null terminated */

    argc = parse_args((char *)req_buf, argv, MAX_ARGS);
    if (argc == 0) {
        LOG("WARN: failed to parse cmd\r\n");
        return NRK_ERROR;
    }

    handler = lookup_cmd_handler(argv[0]);
    if (!handler) {
        LOG("WARN: unknown cmd\r\n");
        return NRK_ERROR;
    }

    return handler(argc, argv);
}

int8_t cmd_rcmd(uint8_t argc, char **argv)
{
    uint8_t i;
    node_id_t node;
    uint8_t len = 0, arg_len;
    int8_t rc = NRK_OK;
    uint8_t cmd_args_idx = 0;
    bool args_valid = false;

    if (argc >= 4) {
        for (i = 2; i < argc; i++) {
            if (argv[i][0] == '-') {
                cmd_args_idx = i + 1;
                break;
            }
        }
        if (cmd_args_idx > 2 && cmd_args_idx < argc)
            args_valid = true;
    }

    if (!args_valid) {
        OUT("usage: rcmd <node>... - <cmd> [<arg>...]\r\n");
        return NRK_ERROR;
    }

    for (i = cmd_args_idx; i < argc; ++i) {
        arg_len = strlen(argv[i]);
        memcpy(cmd_buf + len, argv[i], arg_len);
        len += arg_len;
        cmd_buf[len++] = ' ';
    }
    cmd_buf[len] = '\0';

    for (i = 1; i < cmd_args_idx - 1; ++i) {
        node = atoi(argv[i]);

        rc = rpc_rcmd(node, cmd_buf);
        if (rc != NRK_OK) {
            OUT("ERROR: rcmd rpc failed to: "); OUTP("%u\r\n", node);
            /* continue */
        }
    }
    return rc;
}

uint8_t init_rcmd(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    rpc_init_endpoint(&rcmd_endpoint);

    ASSERT(num_tasks == NUM_TASKS_RCMD);
    return num_tasks;
}
