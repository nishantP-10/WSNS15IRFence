#include <nrk.h>
#include <include.h>
#include <nrk_error.h>
#include <stdlib.h>

#include "cfg.h"
#include "node_id.h"
#include "config.h"
#include "output.h"
#include "enum.h"
#include "led.h"
#include "time.h"
#include "packets.h"
#include "routes.h"
#include "rxtx.h"
#include "dijkstra.h"

#include "rftop.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_RFTOP

/* Request Packet fields(bytes) */
#define PKT_REQUEST_ORIGIN_OFFSET 0
#define PKT_REQUEST_ORIGIN_LEN    1
#define PKT_REQUEST_SEQ_OFFSET    1
#define PKT_REQUEST_SEQ_LEN       1
#define PKT_REQUEST_DEPTH_OFFSET  2
#define PKT_REQUEST_DEPTH_LEN     1

/* Request Packet fields(bytes) */
#define PKT_RESPONSE_ORIGIN_OFFSET 0
#define PKT_RESPONSE_ORIGIN_LEN    1
#define PKT_RESPONSE_SEQ_OFFSET    1
#define PKT_RESPONSE_SEQ_LEN       1

static const char state_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [DISCOVER_IDLE] = "idle",
    [DISCOVER_SCHEDULED] = "scheduled",
    [DISCOVER_PENDING] = "pending",
    [DISCOVER_IN_PROGRESS] = "in_progress",
    [DISCOVER_COMPLETED] = "completed",
};

static discover_state_t discover_state = DISCOVER_IDLE;
static uint8_t periods_in_state = 0;
static nrk_time_t last_activity;

nrk_sig_t discover_signal; /* part of public interface */

static nrk_task_type DISCOVER_TASK;
static NRK_STK discover_task_stack[STACKSIZE_DISCOVER];

/* TODO: rename */
/* static */ graph network;   //please check wgraph.h for definition

static uint8_t outstanding_seq = 0;
static uint8_t discovered_seq = 0;

static node_id_t route_to_origin;

static node_id_t next_hops[MAX_NODES]; /* by shortest paths */

/* Routing tables for *all* nodes in one matrix */
static route_matrix_t routes;

static pkt_t tx_pkt;

static void print_graph(graph *g)
{
    uint8_t ei;
    node_id_t v;
    COUT("digraph RF { ");
    COUTP("%d; ", this_node_id);
    for (v = 0; v < MAX_NODES; ++v) {
        for (ei = 0; ei < g->degree[v]; ++ei)
            COUTP("%d -> %d; ", v, g->edges[v][ei].v);
    }
    COUTA("}\r\n");
}

static void print_routes(route_matrix_t *routes)
{
    node_id_t src, dest;

    OUT("routes: \r\n");
    OUT("   "); /* column width of source label */
    for (dest = 0; dest < MAX_NODES; ++dest)
        OUTP("%d ", dest);
    OUT("\r\n");
    for (src = 0; src < MAX_NODES; ++src) {
        OUTP("%d: ", src);
        for (dest = 0; dest < MAX_NODES; ++dest)
            OUTP("%d ", (*routes)[src][dest]);
        OUT("\r\n");
    }
}

static void init_graph(graph *g)
{
    memset(g, 0, sizeof(graph));
    g->nvertices = MAX_NODES;
}

static bool edge_exists(edge *array, node_id_t dest_node, uint8_t degree)
{
    uint8_t cntr=0;
    if(degree==0)
        return false;

    for(cntr=0;cntr<degree;cntr++)
        if(array[cntr].v==dest_node)
            return true;

    return false;
}

static void add_path_to_graph(pkt_t *pkt)
{
    uint8_t count=0;
    uint8_t hops=pkt->hops;
    node_id_t node1,node2;

    if(hops>MAX_PATH_LEN) {
        LOG("WARN: response with truncated path\r\n");
        return;
    }

    /* Add ourselves as the last hop in the path */
    pkt->path[hops++] = this_node_id;

    for(count=hops-1;count>0;count--){

        node1=pkt->path[count];
        node2=pkt->path[count-1];

        LOG("adding rf link: ");
        LOGP("%d <-> %d\r\n", node1, node2);

        if(!edge_exists(network.edges[node1],node2,network.degree[node1])){

            network.edges[node1][network.degree[node1]].v=node2;
            network.edges[node1][network.degree[node1]].weight=1;
            network.degree[node1]++;
        }
        if(!edge_exists(network.edges[node2],node1,network.degree[node2])){

            network.edges[node2][network.degree[node2]].v=node1;
            network.edges[node2][network.degree[node2]].weight=1;
            network.degree[node2]++;
        }
    }	
}

static int8_t calc_routes(graph *net_graph, route_matrix_t *route_matrix)
{
    node_id_t source, dest;

    LOG("calc routes\r\n");
    for (source = 0; source < MAX_NODES; ++source) {
        dijkstra(net_graph, source, (int8_t *)next_hops); /* evil cast node_id_t */
        for (dest = 0; dest < MAX_NODES; ++dest)
            (*route_matrix)[dest][source] = next_hops[dest];
    }

    return NRK_OK;
}

static int8_t broadcast_request(node_id_t origin, uint8_t seq,
                                uint8_t depth, uint8_t attempt)
{ 
    LOG("broadcast req: seq "); LOGP("%u", seq);
    LOGA(" attempt "); LOGP("%u\r\n", attempt);

    init_pkt(&tx_pkt);
    tx_pkt.type = PKT_TYPE_DISCOVER_REQUEST;
    tx_pkt.dest = BROADCAST_NODE_ID;
    tx_pkt.payload[PKT_REQUEST_ORIGIN_OFFSET] = origin;
    tx_pkt.len += PKT_REQUEST_ORIGIN_LEN;
    tx_pkt.payload[PKT_REQUEST_SEQ_OFFSET] = seq;
    tx_pkt.len += PKT_REQUEST_SEQ_LEN;
    tx_pkt.payload[PKT_REQUEST_DEPTH_OFFSET] = depth;
    tx_pkt.len += PKT_REQUEST_DEPTH_LEN;
    return send_pkt(&tx_pkt, TX_FLAG_NONE, NULL);
}

static void send_response(node_id_t origin, node_id_t src,
                          uint8_t seq, uint8_t attempt)
{
    int8_t rc;

    LOG("response to origin: seq "); LOGP("%u", seq);
    LOGA(" via "); LOGP("%u ", src);
    LOGA(" attempt "); LOGP("%u\r\n", attempt);

    init_pkt(&tx_pkt);
    tx_pkt.type = PKT_TYPE_DISCOVER_RESPONSE;
    tx_pkt.dest = src;
    tx_pkt.payload[PKT_RESPONSE_ORIGIN_OFFSET] = origin;
    tx_pkt.len += PKT_RESPONSE_ORIGIN_LEN;
    tx_pkt.payload[PKT_RESPONSE_SEQ_OFFSET] = seq;
    tx_pkt.len += PKT_RESPONSE_SEQ_LEN;
    rc = send_pkt(&tx_pkt, TX_FLAG_NONE, NULL);
    if (rc != NRK_OK)
        LOG("WARN: failed to send discover req\r\n");
}

static void forward_response(pkt_t *pkt, uint8_t attempt)
{
    int8_t rc;

    LOGA("fwd to next hop ");
    LOGP("%d ", route_to_origin);
    LOGA(" attempt "); LOGP("%u\r\n", attempt);

    pkt->dest = route_to_origin;
    rc = send_pkt(pkt, TX_FLAG_NONE, NULL);
    if (rc != NRK_OK)
        LOG("WARN: failed to send discover req\r\n");
}

void handle_discover_request(pkt_t *pkt)
{
    node_id_t origin;
    uint8_t seq, depth;
    nrk_time_t delay;
    uint8_t attempt;
    int8_t rc;

    origin = pkt->payload[PKT_REQUEST_ORIGIN_OFFSET];
    seq = pkt->payload[PKT_REQUEST_SEQ_OFFSET];
    depth = pkt->payload[PKT_REQUEST_DEPTH_OFFSET];

    if (!IS_VALID_NODE_ID(origin)) {
        LOG("WARN: invalid origin node id in response: ");
        LOGP("%d\r\n", origin);
        return;
    }

    LOG("request:");
    LOGA(" src "); LOGP("%u", pkt->src);
    LOGA(" orig "); LOGP("%u", origin);
    LOGA(" seq "); LOGP("%d (%d)", seq, discovered_seq);
    LOGA(" depth "); LOGP("%d\r\n", depth);

    nrk_time_get(&last_activity);

    choose_delay(&delay, &discover_req_delay);
    nrk_wait(delay);

    /* Discover this node: mark and broadcast to neighbors */
    if (discovered_seq != seq) {
        discovered_seq = seq;
        route_to_origin = pkt->src;

        LOG("discovered: orig "); LOGP("%d", origin);
        LOGA(" seq "); LOGP("%d", discovered_seq);
        LOGA(" hop "); LOGP("%d\r\n", route_to_origin);

        if (depth == 0 || pkt->hops < depth) {
            attempt = 0;
            do {
                rc = broadcast_request(origin, seq, depth, attempt);
                if (rc != NRK_OK)
                    LOG("WARN: failed to broadcast req\r\n");
                nrk_wait(delay);
            } while (++attempt < discover_send_attempts);
        } else {
            LOG("max depth reached: "); LOGP("%d\r\n", depth);
        }
    } else {
        LOG("already discovered: seq "); LOGP("%u\r\n", discovered_seq);
    }

    attempt = 0;
    do {
        send_response(origin, pkt->src, seq, attempt);
        nrk_wait(delay);
    } while (++attempt < discover_send_attempts);
}

void handle_discover_response(pkt_t *pkt)
{
    node_id_t origin = pkt->payload[PKT_RESPONSE_ORIGIN_OFFSET];
    uint8_t seq = pkt->payload[PKT_RESPONSE_SEQ_OFFSET];
    nrk_time_t delay;
    uint8_t attempt;

    if (!IS_VALID_NODE_ID(origin)) {
        LOG("WARN: invalid origin in response: ");
        LOGP("%d\r\n", origin);
        return;
    }

    LOG("response: orig "); LOGP("%u", origin);
    LOGA(" seq "); LOGP("%u", seq);
    LOGA(" src "); LOGP("%u", pkt->src);
    LOGA(": ");

    nrk_time_get(&last_activity);

    if (origin == this_node_id) {
        LOGA("reached origin\r\n");

        add_path_to_graph(pkt);
        print_graph(&network);
    } else { /* we're not the destination: forward to gateway */
        attempt = 0;
        do {
            forward_response(pkt, attempt);
            choose_delay(&delay, &discover_req_delay);
            nrk_wait(delay);
        } while (++attempt < discover_send_attempts);
    }
}

static int8_t broadcast_routes(route_matrix_t *route_matrix, uint8_t ver)
{
    uint8_t attempt = 0;
    int8_t rc;

    do  {
        LOG("broadcasting routes ver: ");
        LOGP("%u\r\n", ver);

        init_pkt(&tx_pkt);
        tx_pkt.type = PKT_TYPE_ROUTES;
        tx_pkt.dest = BROADCAST_NODE_ID;
        tx_pkt.payload[PKT_ROUTES_VER_OFFSET] = ver;
        tx_pkt.len += PKT_ROUTES_VER_LEN;
        memcpy(tx_pkt.payload + PKT_ROUTES_TABLE_OFFSET, route_matrix,
               sizeof(route_matrix_t));
        tx_pkt.len += sizeof(route_matrix_t);
        rc = send_pkt(&tx_pkt, TX_FLAG_NONE, NULL);
        if (rc != NRK_OK)
            LOG("WARN: failed to bcast routes\r\n");
    } while(++attempt < route_broadcast_attempts);

    return rc;
}

static void set_state(discover_state_t new_state)
{
    LOG("state: ");
    LOGF(ENUM_TO_STR(discover_state, state_names));
    LOGA(" -> ");
    LOGF(ENUM_TO_STR(new_state, state_names));
    LOGA("\r\n");

    discover_state = new_state;
    periods_in_state = 0;
}

int8_t probe(uint8_t depth)
{
    outstanding_seq++; 
    nrk_time_get(&last_activity);

    LOG("starting: orig "); LOGP("%d", this_node_id);
    LOGA(" seq "); LOGP("%d", outstanding_seq);
    LOGA(" depth "); LOGP("%d\r\n", depth);

    init_graph(&network);
    print_graph(&network);
    discovered_seq = outstanding_seq; // origin starts discovered
    return broadcast_request(this_node_id, outstanding_seq, depth, 0 /* attempt */);
}

int8_t discover()
{
    set_state(DISCOVER_IN_PROGRESS);

    return probe(0 /* unrestricted depth */);
}

discover_state_t get_discover_state()
{
    return discover_state;
}

uint8_t get_discover_sequence()
{
    return outstanding_seq;
}

void reset_discover_state()
{
    discovered_seq = 0;
    set_state(DISCOVER_IDLE);
}

route_matrix_t * get_discovered_routes()
{
    return &routes;
}

static void discover_task ()
{
    uint8_t periods_in_idle = discover_period_s / DISCOVER_TASK_PERIOD_S;
    nrk_time_t now, elapsed;
    int8_t rc;

    while (1) {
        switch (discover_state) {
            case DISCOVER_IDLE:
                if (auto_discover) {
                    if (periods_in_idle < periods_in_idle)
                        break;

                    set_state(DISCOVER_SCHEDULED);
                }
                break;

            case DISCOVER_SCHEDULED:
#if ENABLE_LED
                pulse_led(led_discover);
#endif
                set_state(DISCOVER_PENDING);
                nrk_event_signal(discover_signal);
                break;

            case DISCOVER_IN_PROGRESS:
                nrk_time_get(&now);
                nrk_time_sub(&elapsed, now, last_activity);
                if (time_cmp(&elapsed, &discover_time_out) < 0) {
                    LOG("silent for ");
                    LOGP("%lu ms\r\n", TIME_TO_MS(elapsed));
                    break;
                }

                LOG("finished, distrib routes: seq ");
                LOGP("%d\r\n", outstanding_seq);

                print_graph(&network);

                rc = calc_routes(&network, &routes);
                if (rc == NRK_OK) {
                    print_routes(&routes);
                    rc = broadcast_routes(&routes, outstanding_seq);
                    if (rc != NRK_OK)
                        LOG("WARN: failed to bcast routes\r\n");
                } else {
                    LOG("WARN: failed to calc routes\r\n");
                }
                set_state(DISCOVER_COMPLETED);
                nrk_event_signal(discover_signal);
                break;

            case DISCOVER_PENDING:
            case DISCOVER_COMPLETED:
                /* the router failed to do its part in one task period */
                set_state(DISCOVER_IDLE);
                break;

            default:
                ABORT("unexpected state\r\n");
        }
        periods_in_state++;
        nrk_wait_until_next_period();
    }
    ABORT("discover task exited\r\n");
}

int8_t cmd_probe(uint8_t argc, char **argv)
{
    uint8_t depth = 0;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: probe [<depth>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2)
        depth = atoi(argv[1]);

    return probe(depth);
}

int8_t cmd_discover(uint8_t argc, char **argv)
{
    if (auto_discover) {
        OUT("ERROR: disable auto discovery to run manually\r\n");
        return NRK_ERROR;
    }

    if (discover_state != DISCOVER_IDLE) {
        OUT("ERROR: not in idle state\r\n");
        return NRK_ERROR;
    }

    set_state(DISCOVER_SCHEDULED);
    return NRK_OK;
}

int8_t cmd_rftop(uint8_t argc, char **argv)
{
    node_id_t out_node, in_node;
    bool exists = false;
    uint8_t i, j;

    if (!(argc == 1 || argc == 3)) {
        OUT("usage: rftop [<out_node> <in_node>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 3) { /* create or destroy an edge */
        out_node = atoi(argv[1]);
        in_node = atoi(argv[2]);

        for (i = 0; i < network.degree[out_node]; ++i) {
            if (network.edges[out_node][i].v == in_node) {
                exists = true;
                break;
            }
        }

        if (exists) { /* then, remove it */
            for (j = i; j < network.degree[out_node] - 1; ++j)
                network.edges[out_node][j].v = network.edges[out_node][j + 1].v;
            network.degree[out_node]--;
        } else {
            network.edges[out_node][network.degree[out_node]++].v = in_node;
        }
    } else {
        print_graph(&network);
    }

    return NRK_OK;
}

int8_t cmd_calc_routes(uint8_t argc, char **argv)
{
    int8_t rc;
    
    rc = calc_routes(&network, &routes);
    if (rc == NRK_OK)
        print_routes(&routes);
    return rc;
}

int8_t cmd_bc_routes(uint8_t argc, char **argv)
{
    uint8_t ver;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: bc-routes [<ver>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2)
        ver = atoi(argv[1]);
    else
        ver = outstanding_seq;

    return broadcast_routes(&routes, ver);
}

uint8_t init_rftop(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    discover_signal = nrk_signal_create();
    if (discover_signal == NRK_ERROR)
        ABORT("create sig: discover\r\n");

    num_tasks++;
    DISCOVER_TASK.task = discover_task;
    DISCOVER_TASK.Ptos = (void *) &discover_task_stack[STACKSIZE_DISCOVER - 1];
    DISCOVER_TASK.Pbos = (void *) &discover_task_stack[0];
    DISCOVER_TASK.prio = priority;
    DISCOVER_TASK.FirstActivation = TRUE;
    DISCOVER_TASK.Type = BASIC_TASK;
    DISCOVER_TASK.SchType = PREEMPTIVE;
    DISCOVER_TASK.period.secs = DISCOVER_TASK_PERIOD_S;
    DISCOVER_TASK.period.nano_secs = 0;
    DISCOVER_TASK.cpu_reserve.secs = 0;
    DISCOVER_TASK.cpu_reserve.nano_secs = 0;
    DISCOVER_TASK.offset.secs = 10;
    DISCOVER_TASK.offset.nano_secs = 0;
    nrk_activate_task (&DISCOVER_TASK);

    ASSERT(num_tasks == NUM_TASKS_RFTOP);
    return num_tasks;
}
