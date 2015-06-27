#include <nrk.h>
#include <nrk_error.h>
#include <include.h>
#include <stdlib.h>

#include "cfg.h"
#include "output.h"
#include "enum.h"
#include "time.h"
#include "config.h"
#include "router.h"
#include "rxtx.h"
#include "ir.h"
#include "rpc.h"
#include "ports.h"
#include "localization.h"
#include "routes.h"
#include "rftop.h"

#include "irtop.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_IRTOP

#define RPC_IR_PONG_REQ_LEN   0
#define RPC_IR_PONG_REPLY_LEN 0

#define RPC_IR_PROBE_REQ_LEN 0

#define IR_PROBE_REPLY_NODE_OFFSET 0
#define IR_PROBE_REPLY_NODE_LEN 1
#define IR_PROBE_REPLY_DIST_OFFSET 1
#define IR_PROBE_REPLY_DIST_LEN 2
#define IR_PROBE_REPLY_ANGLE_OFFSET 3
#define IR_PROBE_REPLY_ANGLE_LEN 2

#define IR_PROBE_RECORD_SIZE (\
        IR_PROBE_REPLY_NODE_LEN + \
        IR_PROBE_REPLY_DIST_LEN + \
        IR_PROBE_REPLY_ANGLE_LEN)

#define RPC_IR_PROBE_REPLY_LEN MAX_NODES * IR_PROBE_RECORD_SIZE

#define MAX_IRTOP_RPC_REQ_LEN RPC_IR_PONG_REQ_LEN
#define MAX_IRTOP_RPC_REPLY_LEN RPC_IR_PONG_REPLY_LEN

#define MAX_IRTOP_PROBE_RPC_REQ_LEN RPC_IR_PROBE_REQ_LEN
#define MAX_IRTOP_PROBE_RPC_REPLY_LEN RPC_IR_PROBE_REPLY_LEN

typedef enum {
    MSG_TYPE_INVALID = 0,
    MSG_TYPE_PROBE,
} msg_type_t;

static const char msg_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [MSG_TYPE_INVALID] = "<invalid>",
    [MSG_TYPE_PROBE] =  "probe",
};

enum {
    RPC_INVALID = 0,
    RPC_IR_PONG,
} rpc_t;

static const char proc_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [RPC_INVALID] = "<invalid>",
    [RPC_IR_PONG] = "ir_pong",
};

enum {
    PROBE_RPC_INVALID = 0, /* hacky name to avoid redefinition */
    RPC_IR_PROBE,
} probe_rpc_t;

static const char probe_proc_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [RPC_INVALID] = "<invalid>",
    [RPC_IR_PROBE] = "ir_probe",
};

static const char irtop_name[] PROGMEM = "irtop";
static const char irtop_probe_name[] PROGMEM = "irtop_probe";

typedef enum {
    STATE_IDLE,
    STATE_PROBING,
    STATE_WAITING_FOR_BEAM,
} state_t;

static const char state_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [STATE_IDLE] = "idle",
    [STATE_PROBING] = "probing",
    [STATE_WAITING_FOR_BEAM] = "waiting_for_beam",
};

/* static */ ir_graph_t ir_graph;

static rpc_proc_t proc_ir_pong;

static rpc_proc_t *procedures[] = {
    [RPC_IR_PONG] = &proc_ir_pong,
};

static msg_t server_queue[IRTOP_RPC_SERVER_QUEUE_SIZE];
static msg_t client_queue[RPC_CLIENT_QUEUE_SIZE];
static rpc_endpoint_t endpoint = {
    .server = {
        .name = irtop_name,
        .listener = {
            .port = PORT_RPC_SERVER_IRTOP,
            .queue = { .size = IRTOP_RPC_SERVER_QUEUE_SIZE },
            .queue_data = server_queue,
        },
        .procedures = procedures,
        .proc_count = sizeof(procedures) / sizeof(procedures[0]),
        .proc_names = &proc_names,
    },
    .client = {
        .name = irtop_name,
        .listener = {
            .port = PORT_RPC_CLIENT_IRTOP,
            .queue = { .size = RPC_CLIENT_QUEUE_SIZE },
            .queue_data = client_queue,
        }
    }
};

/* Probe RPC call is separate so that IR_PONG rpc can be handled
 * while handling IR_PROBE rpc. */
static rpc_proc_t proc_ir_probe;

static rpc_proc_t *probe_procedures[] = {
    [RPC_IR_PROBE] = &proc_ir_probe,
};
static msg_t probe_server_queue[IRTOP_RPC_SERVER_QUEUE_SIZE];
static msg_t probe_client_queue[RPC_CLIENT_QUEUE_SIZE];
static rpc_endpoint_t probe_endpoint = {
    .server = {
        .name = irtop_probe_name,
        .listener = {
            .port = PORT_RPC_SERVER_IRTOP_PROBE,
            .queue = { .size = IRTOP_RPC_SERVER_QUEUE_SIZE },
            .queue_data = probe_server_queue,
        },
        .procedures = probe_procedures,
        .proc_count = sizeof(probe_procedures) / sizeof(probe_procedures[0]),
        .proc_names = &probe_proc_names,
    },
    .client = {
        .name = irtop_probe_name,
        .listener = {
            .port = PORT_RPC_CLIENT_IRTOP_PROBE,
            .queue = { .size = RPC_CLIENT_QUEUE_SIZE },
            .queue_data = probe_client_queue,
        }
    }
};

static msg_t irtop_queue[IRTOP_QUEUE_SIZE];
static listener_t irtop_listener = {
    .port = PORT_IRTOP,
    .queue = { .size = IRTOP_QUEUE_SIZE },
    .queue_data = irtop_queue,
};

static nrk_task_type IRTOP_TASK;
static NRK_STK irtop_task_stack[STACKSIZE_IRTOP];

static nrk_task_type IRTOP_PROBE_TASK;
static NRK_STK irtop_probe_task_stack[STACKSIZE_IRTOP_PROBE];

static uint8_t req_buf[MAX_IRTOP_RPC_REQ_LEN];
static uint8_t reply_buf[MAX_IRTOP_RPC_REPLY_LEN];
static uint8_t probe_req_buf[MAX_IRTOP_PROBE_RPC_REQ_LEN];
static uint8_t probe_reply_buf[MAX_IRTOP_PROBE_RPC_REPLY_LEN];

static msg_t msg;

static state_t state = STATE_IDLE;
static nrk_time_t state_entered;
static int8_t current_direction_led;
static int8_t prober = INVALID_NODE_ID;

/* node id -> neighbor info */
/* static */ ir_neighbor_t ir_neighbors[MAX_NODES];

static void set_state(state_t new_state)
{
    LOG("state: ");
    LOGF(ENUM_TO_STR(state, state_names));
    LOGA(" -> ");
    LOGF(ENUM_TO_STR(new_state, state_names));
    LOGA("\r\n");

    state = new_state; 
    nrk_time_get(&state_entered);
}

static void reset_state()
{
    ir_disarm(~0);
    prober = INVALID_NODE_ID;
    nrk_led_clr(led_awaiting_beam);
    set_state(STATE_IDLE);
}

/* Since it's a calculated value, changing option value is not enough */
static nrk_time_t calc_probe_timeout()
{
    nrk_time_t probe_timeout;
    uint16_t probe_timeout_ms;

    probe_timeout_ms = TIME_TO_MS(ir_direction_time) * (NUM_IR_LEDS + 1);
    MS_TO_TIME(probe_timeout, probe_timeout_ms);
    return probe_timeout;
}

static void print_ir_graph(ir_graph_t *graph)
{
    node_id_t out_node, in_node;
    ir_edge_t *edge;
    COUT("digraph IR { ");
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

ir_neighbor_t *get_ir_neighbor(node_id_t node)
{
    ir_neighbor_t *neighbor = &ir_neighbors[node];
    return neighbor->valid ? neighbor : NULL;
}

ir_graph_t *get_ir_graph()
{
    return &ir_graph;
}

static int8_t rpc_ir_pong(node_id_t prober)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    rc = rpc_call(&endpoint.client, prober, PORT_RPC_SERVER_IRTOP, RPC_IR_PONG,
                  &irtop_rpc_timeout, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: ir pong rpc failed\r\n");
        return rc;
    }

    return rc;
}

static int8_t proc_ir_pong(node_id_t requester,
                                uint8_t *req_buf, uint8_t req_len,
                                uint8_t *reply_buf, uint8_t reply_size,
                                uint8_t *reply_len)
{
    ir_neighbor_t *ir_neighbor;

    if (state != STATE_PROBING) {
        LOG("WARN: unexpectd ir pong rpc\r\n");
        return NRK_ERROR;
    }
    ir_neighbor = &ir_neighbors[requester];
    ir_neighbor->valid = true;
    ir_neighbor->led = current_direction_led;

    LOG("added ir neighbor: ");
    LOGP("[%d] -> %d\r\n", ir_neighbor->led, requester);

    return NRK_OK;
}

static void handle_probe(msg_t *msg)
{
    LOG("ir probe received\r\n");

    prober = msg->sender;
    set_state(STATE_WAITING_FOR_BEAM);
    nrk_led_set(led_awaiting_beam);
    ir_arm(~0);
}

static void handle_msg(msg_t *msg)
{
    LOG("received msg: from ");
    LOGP("%d", msg->sender);
    LOGA(" type ");
    LOGF(ENUM_TO_STR(msg->type, msg_names));
    LOGA("\r\n");

    switch (msg->type) {
        case MSG_TYPE_PROBE:
            handle_probe(msg);
            break;
        default:
            LOG("WARN: unexpected msg\r\n");
    }
}

static int8_t probe()
{
    uint8_t led;
    int8_t rc;
    uint8_t num_neighbors = 0;
    node_id_t node;
    uint8_t broadcast = 0;
    nrk_time_t delay;

    if (state != STATE_IDLE) {
        LOG("WARN: cannot probe: not IDLE\r\n");
        return NRK_ERROR;
    }

    LOG("probe IR hood\r\n");
    set_state(STATE_PROBING);

    memset(ir_neighbors, 0, sizeof(ir_neighbors));
    
    do {
        init_message(&msg);
        msg.recipient = BROADCAST_NODE_ID;
        msg.port = PORT_IRTOP;
        msg.type = MSG_TYPE_PROBE;
        rc = send_message(&msg);
        if (rc != NRK_OK) {
            LOG("WARN: failed to send probe msg\r\n");
            continue;
        }

        choose_delay(&delay, &ir_probe_time);
        nrk_wait(delay);

    } while (broadcast++ < ir_probe_broadcasts);

    for (led = 0; led < NUM_IR_LEDS; ++led) {
        LOG("probe direction: led "); LOGP("%d\r\n", led);

        current_direction_led = led;

        ir_led_on(led);
        nrk_wait(ir_direction_time); /* wait for ir_pong rpc */
        ir_led_off();
    }

    set_state(STATE_IDLE);

    for (node = 0; node < MAX_NODES; ++node)
        if (ir_neighbors[node].valid)
            num_neighbors++;

    LOG("probe found ir neighbors: ");
    LOGP("%d\r\n", num_neighbors);

    return NRK_OK;
}

static int8_t rpc_probe(node_id_t node, ir_graph_t *ir_graph_out)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(probe_reply_buf);
    uint8_t req_len = 0;
    uint8_t num_neighbors;
    node_id_t neighbor;
    ir_edge_t *edge;
    uint8_t i;
    uint8_t offset;
    nrk_time_t probe_rpc_timeout;
    nrk_time_t probe_timeout;

    LOG("ir probe on node ");
    LOGP("%d\r\n", node);

    probe_timeout = calc_probe_timeout();
    nrk_time_add(&probe_rpc_timeout, probe_timeout, irtop_rpc_timeout);

    rc = rpc_call(&probe_endpoint.client, node, PORT_RPC_SERVER_IRTOP_PROBE,
                  RPC_IR_PROBE, &probe_rpc_timeout,
                  probe_req_buf, req_len, probe_reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: ir probe rpc failed\r\n");
        return rc;
    }

    if (reply_len % IR_PROBE_RECORD_SIZE != 0) {
        LOG("WARN: invalid ir probe reply size: ");
        LOGP("%d\r\n", reply_len);
        return NRK_ERROR;
    }

    num_neighbors = reply_len / IR_PROBE_RECORD_SIZE;
    LOG("ir probe rpc reply: neighbors: ");
    LOGP("%d\r\n", num_neighbors);

    for (i = 0; i < num_neighbors; ++i) {
        offset = i * IR_PROBE_RECORD_SIZE;

        neighbor = probe_reply_buf[offset + IR_PROBE_REPLY_NODE_OFFSET];

        if (!IS_VALID_NODE_ID(neighbor)) {
            LOG("WARN: invalid node id in probe rpc reply: ");
            LOGP("%u\r\n", neighbor);
            continue;
        }

        edge = &ir_graph[node][neighbor];
        edge->valid = true;

        edge->dist = probe_reply_buf[offset + IR_PROBE_REPLY_DIST_OFFSET + 1];
        edge->dist <<= 8;
        edge->dist |= probe_reply_buf[offset + IR_PROBE_REPLY_DIST_OFFSET];

        edge->angle = probe_reply_buf[offset + IR_PROBE_REPLY_ANGLE_OFFSET + 1];
        edge->angle <<= 8;
        edge->angle |= probe_reply_buf[offset + IR_PROBE_REPLY_ANGLE_OFFSET];


        LOG("ir probe rpc reply: ");
        LOGP("%u->%u: %u %u\r\n", node, neighbor, edge->dist, edge->angle);
    }

    return rc;
}

static int8_t proc_ir_probe(node_id_t requester,
                            uint8_t *req_buf, uint8_t req_len,
                            uint8_t *reply_buf, uint8_t reply_size,
                            uint8_t *reply_len)
{
    node_id_t node;
    uint16_t angle, dist;
    int8_t rc;
    uint8_t i = 0;
    uint8_t offset;

    LOG("probe rpc\r\n");

    rc = probe();
    if (rc != NRK_OK) {
        LOG("ir probe failed\r\n");
        return rc;
    }

    LOG("probe reply: node: dist angle\r\n");

    *reply_len = 0;
    for (node = 0; node < MAX_NODES; ++node) {
        if (ir_neighbors[node].valid) {
            angle = get_led_angle(ir_neighbors[node].led);
            dist = get_distance(node);

            offset = i * IR_PROBE_RECORD_SIZE;

            reply_buf[offset + IR_PROBE_REPLY_NODE_OFFSET] = node;
            *reply_len += IR_PROBE_REPLY_NODE_LEN;
            reply_buf[offset + IR_PROBE_REPLY_DIST_OFFSET] = dist;
            reply_buf[offset + IR_PROBE_REPLY_DIST_OFFSET + 1] = dist >> 8;
            *reply_len += IR_PROBE_REPLY_DIST_LEN;
            reply_buf[offset + IR_PROBE_REPLY_ANGLE_OFFSET] = angle;
            reply_buf[offset + IR_PROBE_REPLY_ANGLE_OFFSET + 1] = angle >> 8;
            *reply_len += IR_PROBE_REPLY_ANGLE_LEN;

            LOG("probe reply: ");
            LOGP("%d: %u %u\r\n", node, dist, angle);

            i++;
        }
    }

    LOG("probe reply neighbors: ");
    LOGP("%u\r\n", i);

    return NRK_OK;
}

static int8_t ir_discover(node_set_t nodes, bool incremental)
{
    int8_t rc;
    node_id_t node;
    ir_neighbor_t *ir_neighbor;
    ir_edge_t *edge;

    if (state != STATE_IDLE) {
        LOG("WARN: cannot discover: not IDLE\r\n");
        return NRK_ERROR;
    }

    LOG("discovering ir topology: nodes "); LOGP("0x%x\r\n", nodes);

    if (!incremental)
        memset(&ir_graph, 0, sizeof(ir_graph));

    /* First, probe on self */
    if (NODE_SET_IN(nodes, this_node_id)) {
        rc = probe();
        if (rc != NRK_OK) {
            LOG("WARN: probe on self failed\r\n");
            return rc;
        }

        /* Transfer the discovered IR neighbors to the graph */
        for (node = 0; node < MAX_NODES; ++node) {
            ir_neighbor = &ir_neighbors[node];
            if (ir_neighbor->valid) {
                edge = &ir_graph[this_node_id][node];
                edge->valid = true;
                edge->dist = get_distance(node);
                edge->angle = get_led_angle(ir_neighbor->led);
            }
        }

        print_ir_graph(&ir_graph);
    }

    for (node = 0; node < MAX_NODES; ++node) {
        if (NODE_SET_IN(nodes, node) && node != this_node_id) {
            LOG("sending probe request to: "); LOGP("%d\r\n", node);

            rc = rpc_probe(node, &ir_graph);
            if (rc != NRK_OK) {
                LOG("WARN: probe request failed\r\n");
                /* ignore and move on */
            }

            print_ir_graph(&ir_graph);
        }
    }

    LOG("discover completed\r\n");

    return NRK_OK;
}

static void print_ir_neighbors(ir_neighbor_t neighbors[])
{
    node_id_t node;
    ir_neighbor_t *ir_neighbor;

    OUT("IR neighborhood\r\n");
    OUT("node:\tled\treceivers\r\n");
    for (node = 0; node < MAX_NODES; ++node) {
        ir_neighbor = &neighbors[node];
        if (ir_neighbor->valid)
            OUTP("%d:\t%d\t0x%x\r\n", node,
                 ir_neighbor->led, ir_neighbor->receivers);
    }
}


int8_t cmd_irhood(uint8_t argc, char **argv)
{
    node_id_t node;
    ir_neighbor_t *ir_neighbor;

    if (!(argc == 1 || argc == 2 || argc == 4)) {
        OUT("usage: irhood [<node> [<led> <receivers>]]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2 || argc == 4) { /* delete or add neighbor */
        node = atoi(argv[1]);

        ir_neighbor = &ir_neighbors[node];

        if (argc == 2) { /* delete */
            ir_neighbor->valid = false;
            ir_neighbor->led = 0;
            ir_neighbor->receivers = 0;
        } else { /* add */
            ir_neighbor->valid = true;
            ir_neighbor->led = atoi(argv[2]);
            ir_neighbor->receivers = atoi(argv[3]);
        }
    } else {
        print_ir_neighbors(ir_neighbors);
    }

    return NRK_OK;
}

int8_t cmd_irtop(uint8_t argc, char **argv)
{
    node_id_t out_node, in_node;
    ir_edge_t *edge;

    if (!(argc == 1 || argc == 2 || argc == 3 || argc == 5)) {
        OUT("usage: irtop [* | <out_node> <in_node> [<dist> <angle>]]\r\n");
        return NRK_ERROR;
    }

    if (argc == 5 || argc == 3 || argc == 2) { /* create or destroy edge(s) */
        if (argv[1][0] == '*') {
            memset(&ir_graph, 0, sizeof(ir_graph));
        } else {
            out_node = atoi(argv[1]);
            in_node = atoi(argv[2]);

            edge = &ir_graph[out_node][in_node];
            if (argc == 5) { /* create an edge */
                edge->valid = true;
                edge->dist = atoi(argv[3]);
                edge->angle = atoi(argv[4]);
            } else { /* destroy an edge */
                edge->valid = false;
                edge->dist = 0;
                edge->angle = 0;
            }
        }
    } else {
        print_ir_graph(&ir_graph);
    }
    return NRK_OK;
}

int8_t cmd_irprobe(uint8_t argc, char **argv)
{
    int8_t rc;
    
    rc = probe();
    if (rc == NRK_OK)
        print_ir_neighbors(ir_neighbors);

    return rc;
}

int8_t cmd_irdiscover(uint8_t argc, char **argv)
{
    node_set_t nodes;
    node_id_t src, dest;
    bool all_but_self;
    route_matrix_t *route_matrix;
    uint8_t i;
    int8_t rc;
    bool incremental;

    if (!(argc == 2 || argc >= 3)) {
        OUT("usage: irdiscover n|i [-|<node>...]\r\n");
        return NRK_ERROR;
    }

    all_but_self = argc == 3 && argv[2][0] == '-';

    NODE_SET_INIT(nodes);

    /* probe on all other nodes in the whole RF graph (or excluding self) */
    if (argc == 2 || all_but_self) {

        route_matrix = get_discovered_routes();

        for (src = 0; src < MAX_NODES; ++src) {
            for (dest = 0; dest < MAX_NODES; ++dest) {
                if (IS_VALID_NODE_ID((*route_matrix)[src][dest]) &&
                    (!all_but_self || src != this_node_id)) {
                    NODE_SET_ADD(nodes, src);
                    break; /* at least one valid route means src in graph */
                }
            }
        }

    }  else {
        for (i = 2; i < argc; ++i)
            NODE_SET_ADD(nodes, atoi(argv[i]));
    }

    incremental = argv[1][0] == 'i';
    
    rc = ir_discover(nodes, incremental);
    if (rc == NRK_OK)
        print_ir_graph(&ir_graph);

    return rc;
}

void irtop_task()
{
    uint8_t msg_idx;
    queue_t *irtop_queue = &irtop_listener.queue;
    uint8_t rcver_state; /* bitmask */
    nrk_time_t now, elapsed, remaining;
    int8_t rc;
    ir_neighbor_t *probing_neighbor;
    nrk_sig_mask_t wait_signal_mask;
    nrk_time_t probe_timeout;

    rpc_activate_endpoint(&endpoint);

    irtop_listener.signal = nrk_signal_create();
    if (irtop_listener.signal == NRK_ERROR)
        ABORT("failed to create listener signal\r\n");

    register_listener(&irtop_listener);
    activate_listener(&irtop_listener);

    while (1) {

        while (!queue_empty(irtop_queue)) {
            msg_idx = queue_peek(irtop_queue);
            handle_msg(&irtop_listener.queue_data[msg_idx]);
            queue_dequeue(irtop_queue);
        }

        rpc_serve(&endpoint.server);

        wait_signal_mask = 0;

        if (state == STATE_WAITING_FOR_BEAM) {
            ASSERT(IS_VALID_NODE_ID(prober));

            rcver_state = ir_rcv_state(IR_ALL_RECEIVERS);
            if (rcver_state) {
                LOG("received beam: replying with ir pong\r\n");
                rc = rpc_ir_pong(prober);
                if (rc != NRK_OK) {
                    LOG("WARN: ir pong rpc failed\r\n");
                    /* fall-through: still update receivers and reset state */
                }

                probing_neighbor = &ir_neighbors[prober];
                probing_neighbor->valid = true;
                probing_neighbor->receivers = rcver_state;

                reset_state();
            }

            probe_timeout = calc_probe_timeout();
            nrk_time_get(&now);
            nrk_time_sub(&elapsed, now, state_entered);
            if (time_cmp(&elapsed, &probe_timeout) > 0) {
                LOG("no beam: probe timed out\r\n");
                reset_state();
            }

            nrk_time_sub(&remaining, probe_timeout, elapsed);
            nrk_set_next_wakeup(remaining);
            wait_signal_mask |= SIG(nrk_wakeup_signal);
        }

        wait_signal_mask |= SIG(endpoint.server.listener.signal) |
                            SIG(irtop_listener.signal) |
                            SIG(ir_rcv_signal);
        nrk_event_wait(wait_signal_mask);
    }

    ABORT("irtop task exited\r\n");
}

static void irtop_probe_task()
{
    rpc_server_loop(&probe_endpoint.server);
}

uint8_t init_irtop(uint8_t priority)
{
    uint8_t num_tasks = 0;

    rpc_init_endpoint(&endpoint);
    rpc_init_endpoint(&probe_endpoint);

    num_tasks++;
    IRTOP_TASK.task = irtop_task;
    IRTOP_TASK.Ptos = (void *) &irtop_task_stack[STACKSIZE_IRTOP - 1];
    IRTOP_TASK.Pbos = (void *) &irtop_task_stack[0];
    IRTOP_TASK.prio = priority;
    IRTOP_TASK.FirstActivation = TRUE;
    IRTOP_TASK.Type = BASIC_TASK;
    IRTOP_TASK.SchType = PREEMPTIVE;
    IRTOP_TASK.period.secs = 0;
    IRTOP_TASK.period.nano_secs = 0;
    IRTOP_TASK.cpu_reserve.secs = 0;
    IRTOP_TASK.cpu_reserve.nano_secs = 0 * NANOS_PER_MS;
    IRTOP_TASK.offset.secs = 0;
    IRTOP_TASK.offset.nano_secs = 0;
    nrk_activate_task (&IRTOP_TASK);

    num_tasks++;
    IRTOP_PROBE_TASK.task = irtop_probe_task;
    IRTOP_PROBE_TASK.Ptos = (void *) &irtop_probe_task_stack[STACKSIZE_IRTOP_PROBE - 1];
    IRTOP_PROBE_TASK.Pbos = (void *) &irtop_probe_task_stack[0];
    IRTOP_PROBE_TASK.prio = priority;
    IRTOP_PROBE_TASK.FirstActivation = TRUE;
    IRTOP_PROBE_TASK.Type = BASIC_TASK;
    IRTOP_PROBE_TASK.SchType = PREEMPTIVE;
    IRTOP_PROBE_TASK.period.secs = 0;
    IRTOP_PROBE_TASK.period.nano_secs = 0;
    IRTOP_PROBE_TASK.cpu_reserve.secs = 0;
    IRTOP_PROBE_TASK.cpu_reserve.nano_secs = 0 * NANOS_PER_MS;
    IRTOP_PROBE_TASK.offset.secs = 0;
    IRTOP_PROBE_TASK.offset.nano_secs = 0;
    nrk_activate_task (&IRTOP_PROBE_TASK);

    ASSERT(num_tasks == NUM_TASKS_IRTOP);
    return num_tasks;
}
