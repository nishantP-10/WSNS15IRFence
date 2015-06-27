#include <nrk.h>
#include <nrk_error.h>
#include <include.h>
#include <stdlib.h>

#include "cfg.h"
#include "output.h"
#include "config.h"
#include "ir.h"
#include "irtop.h"
#include "router.h"
#include "ports.h"
#include "rpc.h"

#include "beam.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_BEAM

#define RPC_RECEIVE_BEAM_REPLY_STATUS_OFFSET 0
#define RPC_RECEIVE_BEAM_REPLY_STATUS_LEN    1

#define RPC_RECEIVE_BEAM_REQ_LEN         0
#define RPC_RECEIVE_BEAM_REPLY_LEN       RPC_RECEIVE_BEAM_REPLY_STATUS_LEN

#define RPC_DROP_BEAM_REQ_LEN         0
#define RPC_DROP_BEAM_REPLY_LEN       0

#define MAX_BEAM_RPC_REQ_LEN MAX(\
    RPC_RECEIVE_BEAM_REQ_LEN, \
    RPC_DROP_BEAM_REQ_LEN)

#define MAX_BEAM_RPC_REPLY_LEN MAX(\
    RPC_RECEIVE_BEAM_REPLY_LEN, \
    RPC_DROP_BEAM_REPLY_LEN)

typedef struct {
    node_id_t node;
    uint8_t device; /* led idx OR receivers bitmask */
    bool active;
    bool broken; /* for incoming beam only */
} beam_t;

enum {
    RPC_INVALID = 0,
    RPC_RECEIVE_BEAM,
    RPC_DROP_BEAM,
} rpc_t;

static const char proc_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [RPC_INVALID] = "<invalid>",
    [RPC_RECEIVE_BEAM] = "receive_beam",
    [RPC_DROP_BEAM] = "drop_beam",
};

static const char beam_name[] PROGMEM = "beam";

static rpc_proc_t proc_receive_beam;
static rpc_proc_t proc_drop_beam;

static rpc_proc_t *procedures[] = {
    [RPC_RECEIVE_BEAM] = &proc_receive_beam,
    [RPC_DROP_BEAM] = &proc_drop_beam,
};
static msg_t server_queue[BEAM_RPC_SERVER_QUEUE_SIZE];
static msg_t client_queue[RPC_CLIENT_QUEUE_SIZE];
static rpc_endpoint_t endpoint = {
    .server = {
        .name = beam_name,
        .listener = {
            .port = PORT_RPC_SERVER_BEAM,
            .queue = { .size = BEAM_RPC_SERVER_QUEUE_SIZE },
            .queue_data = server_queue,
        },
        .procedures = procedures,
        .proc_count = sizeof(procedures) / sizeof(procedures[0]),
        .proc_names = &proc_names,
    },
    .client = {
        .name = beam_name,
        .listener = {
            .port = PORT_RPC_CLIENT_BEAM,
            .queue = { .size = RPC_CLIENT_QUEUE_SIZE },
            .queue_data = client_queue,
        }
    }
};

static nrk_task_type BEAM_TASK;
static NRK_STK beam_task_stack[STACKSIZE_BEAM];

static uint8_t req_buf[MAX_BEAM_RPC_REQ_LEN];
static uint8_t reply_buf[MAX_BEAM_RPC_REPLY_LEN];

nrk_sig_t beam_signal; /* fired when beam state changes */

static beam_t in_beam, out_beam;

bool get_beam_state()
{
    return !in_beam.broken;
}

static int8_t rpc_receive_beam(node_id_t node)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;
    bool beam_status;

    rc = rpc_call(&endpoint.client, node, PORT_RPC_SERVER_BEAM, RPC_RECEIVE_BEAM,
                  &beam_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: receive beam rpc failed\r\n");
        return rc;
    }
    if (reply_len != RPC_RECEIVE_BEAM_REPLY_LEN) {
        LOG("WARN: beam reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    beam_status = reply_buf[RPC_RECEIVE_BEAM_REPLY_STATUS_OFFSET];
    LOG("rpc receive beam status: "); LOGP("%d\r\n", beam_status);

    return beam_status ? NRK_OK : NRK_ERROR;
}

static int8_t proc_receive_beam(node_id_t requester,
                                uint8_t *req_buf, uint8_t req_len,
                                uint8_t *reply_buf, uint8_t reply_size,
                                uint8_t *reply_len)
{
    uint8_t beam_status;
#if 0
    ir_neighbor_t *ir_neighbor;
#endif

    LOG("rcv beam req from: "); LOGP("%u\r\n", requester);

    if (reply_size < RPC_RECEIVE_BEAM_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_RECEIVE_BEAM_REPLY_LEN);
        return NRK_ERROR;
    }

#if 0
    ir_neighbor = get_ir_neighbor(requester);
    if (!ir_neighbor) {
        LOG("WARN: node not an IR neighbor: ");
        LOGP("%d\r\n", requester);
        return NRK_ERROR;
    }
#endif

#if 0
    ir_arm(ir_neighbor->receivers);
#endif
    nrk_wait(beam_sense_time); /* wait for interrupt to update state */
#if 0
    beam_status = ir_rcv_state(ir_neighbor->receivers);
#else
    beam_status = ir_rcv_state(0xf);
#endif

    if (beam_status) {
        memset(&in_beam, 0, sizeof(in_beam));
        in_beam.node = requester;
#if 0
        in_beam.device = ir_neighbor->receivers;
#else
        in_beam.device = 0xf;
#endif
        in_beam.active = true;
    } else {
#if 0
        ir_disarm(ir_neighbor->receivers);
#endif
    }

    reply_buf[RPC_RECEIVE_BEAM_REPLY_STATUS_OFFSET] = beam_status;
    *reply_len = RPC_RECEIVE_BEAM_REPLY_LEN;

    return NRK_OK;
}

static int8_t rpc_drop_beam(node_id_t node)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    rc = rpc_call(&endpoint.client, node, PORT_RPC_SERVER_BEAM, RPC_DROP_BEAM,
                  &beam_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: drop beam rpc failed\r\n");
        return rc;
    }

    return rc;
}

static int8_t proc_drop_beam(node_id_t requester,
                             uint8_t *req_buf, uint8_t req_len,
                             uint8_t *reply_buf, uint8_t reply_size,
                             uint8_t *reply_len)
{
    LOG("drop beam req from: "); LOGP("%u\r\n", requester);

    ir_disarm(in_beam.device);
    in_beam.active = false;

    return NRK_OK;
}

int8_t create_beam(node_id_t receiving_node)
{
    int8_t rc = NRK_OK;
    ir_neighbor_t *ir_neighbor;

    if (out_beam.active) {
        LOG("WARN: an out beam already exists\r\n");
        return NRK_ERROR;
    }

    ir_neighbor = get_ir_neighbor(receiving_node);
    if (!ir_neighbor) {
        LOG("WARN: node not an IR neighbor\r\n");
        return NRK_ERROR;
    }

    LOG("making an ir link: -> ");
    LOGP("%d[%d] -> %d\r\n", this_node_id, ir_neighbor->led, receiving_node);

    ir_led_on(ir_neighbor->led);

    rc = rpc_receive_beam(receiving_node);
    OUT("beam status: "); OUTP("%d\r\n", rc);

    if (rc != NRK_OK) {
        LOG("WARN: beam create failed\r\n");
        ir_led_off(ir_neighbor->led);
        return NRK_ERROR;
    }

    LOG("beam created\r\n");

    memset(&out_beam, 0, sizeof(out_beam));
    out_beam.node = receiving_node;
    out_beam.device = ir_neighbor->led;
    out_beam.active = true;

    return rc;
}

int8_t destroy_beam()
{
    int8_t rc = NRK_OK;

    if (!out_beam.active) {
        LOG("WARN: destroy failed: no out beam\r\n");
        return NRK_ERROR;
    }

    LOG("droping beam from ");
    LOGP("%d\r\n", out_beam.node);

    rc = rpc_drop_beam(out_beam.node);
    if (rc != NRK_OK) {
        LOG("WARN: drop beam rpc failed\r\n");
        return rc;
    }

    LOG("beam destroyed\r\n");

    ir_led_off(out_beam.device);
    out_beam.active = false;

    return rc;
}

static void beam_task()
{
    int8_t rc;

    rc = nrk_signal_register(ir_rcv_signal);
    if (rc == NRK_ERROR)
        ABORT("failed to register for ir rcv signal\r\n");

    rpc_activate_endpoint(&endpoint);

    while (1) {
        
        if (in_beam.active) {
            if (!(ir_rcv_state(in_beam.device))) {
                if (!in_beam.broken) {
                    in_beam.broken = true;
                    nrk_event_signal(beam_signal);
                    LOG("beam broken\r\n");
                }
            } else {
                if (in_beam.broken) {
                    in_beam.broken = false;
                    nrk_event_signal(beam_signal);
                    LOG("beam restored\r\n");
                }
            }
        }

        rpc_serve(&endpoint.server);

        nrk_event_wait( SIG(ir_rcv_signal) |
                        SIG(endpoint.server.listener.signal) );
    }

    ABORT("beam task exited\r\n");
}

int8_t cmd_beam(uint8_t argc, char **argv)
{
    int8_t rc;
    node_id_t node;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: beam [<node>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2) {
        node = atoi(argv[1]);
        rc = create_beam(node);
    } else {
        rc = destroy_beam();
    }
    return rc;
}

uint8_t init_beam(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    rpc_init_endpoint(&endpoint);

    beam_signal = nrk_signal_create();
    if (beam_signal == NRK_ERROR)
        ABORT("failed to create beam signal\r\n");

    num_tasks++;
    BEAM_TASK.task = beam_task;
    BEAM_TASK.Ptos = (void *) &beam_task_stack[STACKSIZE_BEAM - 1];
    BEAM_TASK.Pbos = (void *) &beam_task_stack[0];
    BEAM_TASK.prio = priority;
    BEAM_TASK.FirstActivation = TRUE;
    BEAM_TASK.Type = BASIC_TASK;
    BEAM_TASK.SchType = PREEMPTIVE;
    BEAM_TASK.period.secs = 0;
    BEAM_TASK.period.nano_secs = 0;
    BEAM_TASK.cpu_reserve.secs = 0;
    BEAM_TASK.cpu_reserve.nano_secs = 0 * NANOS_PER_MS;
    BEAM_TASK.offset.secs = 0;
    BEAM_TASK.offset.nano_secs = 0;
    nrk_activate_task (&BEAM_TASK);

    ASSERT(num_tasks == NUM_TASKS_BEAM);
    return num_tasks;
}
