#include <nrk.h>
#include <include.h>
#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <avr/sleep.h>
#include <hal.h>
#include <bmac.h>
#include <nrk_error.h>
#include <nrk_timer.h>
#include <nrk_eeprom.h>
#include "cfg.h"
#include "dijkstra.h"

#define IS_VALID_NODE_ID(id) (id != 0)
#define IS_GATEWAY (this_node_id == GATEWAY_NODE_ID)
#define IS_VALID_ACK_REQ(ack_req) (IS_VALID_NODE_ID((ack_req)->src))

/* For simulating a non-fully-connected topology on the desk */
#ifdef TEST_TOPOLOGY
#define IS_REACHEABLE(dest) (!(test_topology[this_node_id] & (1 << (dest))))
#else
#define IS_REACHEABLE(dest) true
#endif

#define ABORT(msg) \
    do { \
        nrk_kprintf(PSTR(msg)); \
        nrk_led_set(LED_ABORTED); \
        nrk_halt(); \
    } while (0); \

/* Router layer packets */

/* Packet header fields (in bytes) */
#define PKT_HDR_TYPE_OFFSET 0
#define PKT_HDR_TYPE_LEN    1
#define PKT_HDR_SRC_OFFSET  1
#define PKT_HDR_SRC_LEN     1
#define PKT_HDR_DEST_OFFSET 2
#define PKT_HDR_DEST_LEN    1
#define PKT_HDR_HOPS_OFFSET 3
#define PKT_HDR_HOPS_LEN    1
#define PKT_HDR_PATH_OFFSET 4
#define PKT_HDR_PATH_LEN    7

#define PKT_HDR_LEN ( \
    PKT_HDR_TYPE_LEN + \
    PKT_HDR_SRC_LEN + \
    PKT_HDR_DEST_LEN + \
    PKT_HDR_HOPS_LEN + \
    PKT_HDR_PATH_LEN )
/* Ping pkt fields (bytes) */
#define PKT_PING_TOKEN_OFFSET 0
#define PKT_PING_TOKEN_LEN    1

/* Pong pkt fields (bytes) */
#define PKT_PONG_TOKEN_OFFSET 0
#define PKT_PONG_TOKEN_LEN    1

/* Request Packet fields(bytes) */
#define PKT_REQUEST_SEQUENCE_NUM_OFFSET 0
#define PKT_REQUEST_SEQUENCE_NUM_LEN    1


#define MAX_PATH_LEN (PKT_HDR_PATH_LEN / sizeof(node_id_t))
#define MAX_PATHS 16

/* Msg pkt fields (bytes) */
#define PKT_MSG_SENDER_OFFSET    0
#define PKT_MSG_SENDER_LEN       1
#define PKT_MSG_RECIPIENT_OFFSET 1
#define PKT_MSG_RECIPIENT_LEN    1
#define PKT_MSG_SEQ_OFFSET       2
#define PKT_MSG_SEQ_LEN          1

#define PKT_MSG_HDR_LEN ( \
    PKT_MSG_SENDER_LEN + \
    PKT_MSG_RECIPIENT_LEN + \
    PKT_MSG_SEQ_LEN )

/* Msg ack pkt fields (bytes) */
#define PKT_MSG_ACK_SEQ_OFFSET   0
#define PKT_MSG_ACK_SEQ_LEN      1

/* Routes pkt fields (bytes) */
#define PKT_ROUTES_VER_OFFSET     0
#define PKT_ROUTES_VER_LEN        1
#define PKT_ROUTES_TABLE_OFFSET   1
#define PKT_ROUTES_TABLE_LEN      (MAX_NODES * MAX_NODES * sizeof(node_id_t))

#define MAX_PATH_LEN (PKT_HDR_PATH_LEN / sizeof(node_id_t))

/* Application layer messages ('messages' mean routed pkt types) */

/* In all messages */
#define MSG_TYPE_OFFSET       0
#define MSG_TYPE_LEN          1

/* Ping message */
#define MSG_PING_TOKEN_OFFSET 1
#define MSG_PING_TOKEN_LEN    1
#define MSG_PING_LEN (MSG_TYPE_LEN + MSG_PING_TOKEN_LEN)

/* Pong message */
#define MSG_PONG_TOKEN_OFFSET 1
#define MSG_PONG_TOKEN_LEN    1
#define MSG_PONG_LEN (MSG_TYPE_LEN + MSG_PONG_TOKEN_LEN)

typedef uint8_t node_id_t;
typedef node_id_t route_matrix_t[MAX_NODES][MAX_NODES];

typedef void (*rcv_msg_handler_t)(node_id_t sender,
                                  uint8_t *payload, uint8_t len);

/* Router layer pkt types */
typedef enum {
    PKT_TYPE_INVALID = 0,
    PKT_TYPE_PING,
    PKT_TYPE_PONG,
    PKT_TYPE_DISCOVERY_REQUEST,
    PKT_TYPE_DISCOVERY_RESPONSE,
    PKT_TYPE_POTATO,
    PKT_TYPE_MSG,
    PKT_TYPE_MSG_ACK,
    PKT_TYPE_ROUTES,
} pkt_type_t;

/* Application layer message types ('messages' mean routed packets) */
typedef enum {
    MSG_TYPE_INVALID = 0,
    MSG_TYPE_PING,
    MSG_TYPE_PONG,
} msg_type_t;

typedef struct {
    uint8_t buf[RF_MAX_PAYLOAD_SIZE];
    uint8_t len;

    int8_t rssi;

    /* Header */
    pkt_type_t type;
    node_id_t src;
    node_id_t dest;
    uint8_t hops;
    node_id_t path[MAX_PATH_LEN];
    uint8_t path_len;

    /* Points into buf (after header) */
    uint8_t *payload;
    uint8_t payload_len;
} pkt_t;

typedef struct {
    node_id_t id;
    nrk_time_t last_heard;
    uint8_t rssi;
} neighbor_t;

typedef struct {
    nrk_time_t time_sent;
    node_id_t src;
    node_id_t dest;
    node_id_t recipient;
    uint8_t seq;
} msg_ack_req_t;

#ifdef TEST_TOPOLOGY
/* A matrix represented as an array of bitmaps: bit set means no link */
static const uint8_t test_topology[MAX_NODES] = {
    [1] = 0b00001000,
    [3] = 0b00000010,
};
#endif

#if defined(TEST_ROUTES) || defined(MODE_ROUTES)
static const route_matrix_t test_routes = {
    [1] = {
        [1] = 0,
        [2] = 2,
        [3] = 4,
        [4] = 4,
        [6] = 6,
    },
    [2] = {
        [1] = 1,
        [2] = 0,
        [3] = 3,
        [4] = 4,
        [6] = 6,
    },
    [3] = {
        [1] = 4,
        [2] = 2,
        [3] = 0,
        [4] = 4,
        [6] = 4,
    },
    [4] = {
        [1] = 1,
        [2] = 2,
        [3] = 3,
        [4] = 0,
        [6] = 6,
    },
    [6] = {
        [1] = 1,
        [2] = 4,
        [3] = 4,
        [4] = 4,
        [6] = 0,
    },
};
#endif

uint8_t rf_chan;
node_id_t this_node_id;
graph network;				//please check wgraph.h for definition

static neighbor_t neighbors[MAX_NEIGHBORS];
static uint8_t num_neighbors = 0;

static node_id_t routes[MAX_NODES];
static uint8_t routes_ver = 0;

static node_id_t hop_to_gateway;
static uint8_t isDiscovered = 0;
#ifdef ENABLE_REQUEST_TASK
static uint8_t request_number=0;
#endif
static node_id_t next_hops[MAX_NODES]; /* by shortest paths */

static uint8_t msg_sent_seq = 0;
static msg_ack_req_t msg_ack_queue[MSG_ACK_QUEUE_SIZE];
static pkt_t tx_msg_queue[TX_MSG_QUEUE_SIZE];
static uint8_t tx_msg_head = 0;
static uint8_t tx_msg_tail = 0;

static rcv_msg_handler_t rcv_msg_handler;

static nrk_task_type RX_TASK;
static NRK_STK rx_task_stack[NRK_APP_STACKSIZE];

static nrk_task_type ROUTER_TASK;
static NRK_STK router_task_stack[NRK_APP_STACKSIZE];

#ifdef ENABLE_PING_TASK
static nrk_task_type PING_TASK;
static NRK_STK ping_task_stack[NRK_APP_STACKSIZE];
#endif

#ifdef ENABLE_REQUEST_TASK
static nrk_task_type REQUEST_TASK;
static NRK_STK request_task_stack[NRK_APP_STACKSIZE];
#endif

#ifdef DISPLAY_NETWORK
static nrk_task_type DISPLAY_TASK;
static NRK_STK display_task_stack[NRK_APP_STACKSIZE];
#endif

static pkt_t tx_pkt; /* shared (protectetd by tx_sem) */
static nrk_sem_t *tx_sem; /* only one transmission at a time */
static uint8_t rx_buf[RF_MAX_PAYLOAD_SIZE]; /* TODO: rcv directly into queue */

static pkt_t rx_queue[RX_QUEUE_SIZE];
static uint8_t rx_head = 0, rx_tail = 0;
static nrk_sig_t router_signal;

static nrk_sem_t *relay_msg_sem; /* only one routed msg send at a time */

static nrk_time_t led_on_time = {0, 500000000U};
static nrk_time_t check_period = {0, 200 * NANOS_PER_MS};
static nrk_time_t msg_ack_timeout = {2, 0 * NANOS_PER_MS};

static nrk_sig_t uart_rx_signal;

static uint8_t ping_token = 0;

static int8_t time_cmp(nrk_time_t *time_a, nrk_time_t *time_b)
{
    if (time_a->secs < time_b->secs)
        return -1;
    if (time_a->secs > time_b->secs)
        return 1;
    if (time_a->nano_secs < time_b->nano_secs)
        return -1;
    if (time_a->nano_secs > time_b->nano_secs)
        return 1;
    return 0;
}

static const char * pkt_type_to_string(pkt_type_t type)
{
    switch (type) {
        case PKT_TYPE_PING: return "ping";
        case PKT_TYPE_PONG: return "pong";
	case PKT_TYPE_DISCOVERY_REQUEST: return "Request";
        case PKT_TYPE_DISCOVERY_RESPONSE: return "Response/Forward";
        case PKT_TYPE_POTATO: return "potato";
        case PKT_TYPE_MSG: return "msg";
        case PKT_TYPE_MSG_ACK: return "msgack";
        case PKT_TYPE_ROUTES: return "routes";
        case PKT_TYPE_INVALID: return "<invalid>";
        default: return "<unknown>";
    }
}

static const char * msg_type_to_string(msg_type_t type)
{
    switch (type) {
        case MSG_TYPE_PING: return "ping";
        case MSG_TYPE_PONG: return "pong";
        case MSG_TYPE_INVALID: return "<invalid>";
        default: return "<unknown>";
    }
}

static void print_routes()
{
    node_id_t dest;

    nrk_kprintf(PSTR("Routes ver "));
    printf("%d: ", routes_ver);
    for (dest = 0; dest < MAX_NODES; ++dest)
        printf("%d <- %d; ", dest, routes[dest]);
    printf("\r\n");
}

static void print_graph(graph *g)
{
    uint8_t ei;
    node_id_t v;
    nrk_kprintf(PSTR("graph G {\r\n"));
    for (v = 0; v < MAX_NODES; ++v) {
        for (ei = 0; ei < g->degree[v]; ++ei)
            printf("%d -- %d;\r\n", v, g->edges[v][ei].v);
    }
    nrk_kprintf(PSTR("}\r\n"));
}

static void tx_enter()
{
    int8_t rc = nrk_sem_pend(tx_sem);
    if (rc == NRK_ERROR)
        ABORT("ERROR: failed to pend on tx sem\r\n");
}

static void tx_exit()
{
    int8_t rc = nrk_sem_post(tx_sem);
    if (rc == NRK_ERROR)
        ABORT("ERROR: failed to post on tx sem\r\n");
}

static int8_t send_buf(uint8_t *buf, uint8_t len)
{
    int8_t i, rc;

    printf("TX RAW [%d]: ", len);
    for (i = 0; i < len; i++)
        printf("%02x ", buf[i]);
    printf("\r\n");

    nrk_led_set(LED_PKT_SEND);
    nrk_wait(led_on_time);
    nrk_led_clr(LED_PKT_SEND);

    rc = bmac_tx_pkt(buf, len);
    if(rc != NRK_OK) {
        nrk_kprintf(PSTR("ERROR: bmac_tx_pkt rc "));
        printf("%d\r\n", rc);
    }

    return rc;
}

static void init_pkt(pkt_t *pkt)
{
    memset(pkt, 0, sizeof(pkt_t)); /* optional */
    pkt->src = this_node_id; /* note: send_pkt does not use this */
    pkt->payload = pkt->buf + PKT_HDR_LEN;
    pkt->len = PKT_HDR_LEN;
    pkt->hops = 0;
    pkt->type = 0;
}

/* To send a packet:
 *   tx_enter();
 *   init_pkt(&tx_pkt);
 *   tx_pkt.type = <type>
 *   tx_pkt.dest = <destination node id>;
 *   tx_pkt.payload[PKT_<TYPE>_<FIELD>_OFFSET] = <field value>;
 *   tx_pkt.len += PKT_<TYPE>_<FIELD>_LEN;
 *   ... repeat the above two lines for other fields
 *   send_pkt(&tx_pkt)
 *   tx_exit();
 *
 * To forward a packet:
 *   tx_enter();
 *   pkt->dest = <destination node id>;
 *   send_pkt(pkt)
 *   tx_exit();
 */
static int8_t send_pkt(pkt_t *pkt)
{
    if (!IS_REACHEABLE(pkt->dest)) {
        nrk_kprintf(PSTR("Dest unrecheable by topology: pkt dropped\r\n"));
        return NRK_OK;
    }

    pkt->buf[PKT_HDR_TYPE_OFFSET] = pkt->type;
    pkt->buf[PKT_HDR_SRC_OFFSET] = this_node_id;
    pkt->buf[PKT_HDR_DEST_OFFSET] = pkt->dest;

    if (pkt->hops < MAX_PATH_LEN) /* otherwise: further hops are not recorded */
        pkt->buf[PKT_HDR_PATH_OFFSET + pkt->hops] = this_node_id;

    pkt->buf[PKT_HDR_HOPS_OFFSET] = pkt->hops + 1;

    return send_buf(pkt->buf, pkt->len);
}

static node_id_t pick_alt_route(node_id_t excluded_node)
{
    uint8_t neighbor_idx;
    if (num_neighbors == 0 ||
        (num_neighbors == 1 && neighbors[0].id == excluded_node))
        return INVALID_NODE_ID;

    /* For now, just a random pick */
    do {
        neighbor_idx = rand() % num_neighbors;
    } while (neighbors[neighbor_idx].id == excluded_node);

    return neighbors[neighbor_idx].id;
}

static msg_ack_req_t *alloc_msg_ack_req()
{
    uint8_t i;
    msg_ack_req_t *ack_req;
    for (i = 0; i < MSG_ACK_QUEUE_SIZE; ++i) {
        ack_req = &msg_ack_queue[i];
        if (!IS_VALID_ACK_REQ(ack_req))
            return ack_req;
    }
    return NULL;
}

static void release_msg_ack_req(msg_ack_req_t *ack_req)
{
    ack_req->src = INVALID_NODE_ID;
    ack_req->dest = INVALID_NODE_ID;
    ack_req->seq = 0;
}

static int8_t relay_msg_pkt(pkt_t *pkt)
{
    int8_t rc = NRK_OK;
    node_id_t recipient = pkt->payload[PKT_MSG_RECIPIENT_OFFSET];
    msg_ack_req_t *ack_req;
    uint8_t retries = 0;

    nrk_sem_pend(relay_msg_sem);

    nrk_kprintf(PSTR("Relaying msg pkt from "));
    printf("%d\r\n", pkt->src);

    /* Find next hop from routing table */
    pkt->dest = routes[recipient];
    if (!IS_VALID_NODE_ID(pkt->dest)) {
        nrk_kprintf(PSTR("WARN: no route to node "));
        printf("%d\r\n", recipient);
        nrk_sem_post(relay_msg_sem);
        return NRK_ERROR;
    }

    /* Create state signifying we are waiting for an ack */

    ack_req = alloc_msg_ack_req();
    if (!ack_req) {
        nrk_kprintf(PSTR("WARN: failed to allocate msg ack req\r\n"));
        nrk_sem_post(relay_msg_sem);
        return rc;
    }

    nrk_time_get(&ack_req->time_sent);
    ack_req->src = pkt->src;
    ack_req->dest = pkt->dest;
    ack_req->recipient = recipient;
    ack_req->seq = ++msg_sent_seq;

    nrk_set_next_wakeup(msg_ack_timeout);

    pkt->payload[PKT_MSG_SEQ_OFFSET] = msg_sent_seq;

    /* Send packet to next hop */
    nrk_kprintf(PSTR("Sending msg to next hop "));
    printf("%d (try %d)\r\n", pkt->dest, retries);
    /* tx_enter/tx_exit is called by the caller */
    rc = send_pkt(pkt);
    if (rc != NRK_OK) {
        nrk_kprintf(PSTR("WARN: msg send failed, giving up\r\n"));
        nrk_sem_post(relay_msg_sem);
        return rc;
    }

    nrk_sem_post(relay_msg_sem);
    return rc;
}

static int8_t send_request_pkt(uint8_t val)
{ 
    uint8_t rc = NRK_OK;
    tx_enter();
    init_pkt(&tx_pkt);
    tx_pkt.type = PKT_TYPE_DISCOVERY_REQUEST;
    tx_pkt.dest = BROADCAST_NODE_ID;
    tx_pkt.payload[PKT_REQUEST_SEQUENCE_NUM_OFFSET] = val;
    tx_pkt.len += PKT_REQUEST_SEQUENCE_NUM_LEN;
    rc = send_pkt(&tx_pkt);
    tx_exit();

    if (rc != NRK_OK) {
        nrk_kprintf(PSTR("Discovery Request failed, giving up.\r\n"));
        return rc;
    }
    return rc;
}

/* A ping packet triggers a pong packet to be sent to the source
 * with the same token as was received in the ping token (echo). */
static void handle_ping(pkt_t *pkt)
{
    int8_t rc;
    uint8_t token;
    nrk_time_t pong_delay = {5, 0};

    if (!(pkt->dest == this_node_id || pkt->dest == BROADCAST_NODE_ID))
        return;

    if (pkt->len < PKT_PING_TOKEN_LEN) {
        nrk_kprintf(PSTR("Ping pkt too short\r\n"));
        return;
    }
    token = pkt->payload[PKT_PING_TOKEN_OFFSET];

    nrk_kprintf(PSTR("Ping with token (waiting...): "));
    printf("%02x\r\n", token);

    nrk_wait(pong_delay);

    /* Send pong reply */
    nrk_kprintf(PSTR("Sending pong reply with token: "));
    printf("%02x\r\n", token);

    tx_enter();
    init_pkt(&tx_pkt);
    tx_pkt.type = PKT_TYPE_PONG;
    tx_pkt.payload[PKT_PONG_TOKEN_OFFSET] = token;
    tx_pkt.len += PKT_PONG_TOKEN_LEN;
    tx_pkt.dest = pkt->src;
    rc = send_pkt(&tx_pkt);
    tx_exit();
    if (rc != NRK_OK) {
        nrk_kprintf(PSTR("Pong reply failed, giving up.\r\n"));
        return;
    }
}

/* A pong packet triggers no action, just report the token */
static void handle_pong(pkt_t *pkt)
{
    int8_t rc;
    uint8_t token;
    static bool hot_potato_sent = false;

    if (pkt->dest != this_node_id)
        return;

    if (pkt->len < PKT_PONG_TOKEN_LEN) {
        nrk_kprintf(PSTR("Pong pkt too short\r\n"));
        return;
    }
    token = pkt->payload[PKT_PONG_TOKEN_OFFSET];

    nrk_kprintf(PSTR("Pong with token: "));
    printf("%d (exp %d)\r\n", token, ping_token);

    if (ping_token == token) {
        nrk_led_clr(LED_PING_SENT);
    } else {
        nrk_kprintf(PSTR("WARN: unexpected token\r\n"));
    }
    /* Inject a hot potato that will be passed around */
    if (this_node_id == GATEWAY_NODE_ID && !hot_potato_sent) {
        nrk_kprintf(PSTR("Creating a hot potato\r\n"));
        hot_potato_sent = true;

        tx_enter();
        init_pkt(&tx_pkt);
        tx_pkt.type = PKT_TYPE_POTATO;
        tx_pkt.dest = pkt->src;
        rc = send_pkt(&tx_pkt);
        tx_exit();
        if (rc != NRK_OK) {
            nrk_kprintf(PSTR("Hot potato send failed, ignored.\r\n"));
       }
    }
}

static void handle_discovery_request(pkt_t *pkt){
	
	nrk_kprintf(PSTR("In Handle Request"));
	uint8_t val=pkt->payload[PKT_REQUEST_SEQUENCE_NUM_OFFSET];
	if(this_node_id==GATEWAY_NODE_ID)
	return;
	if(isDiscovered==0 && this_node_id!=GATEWAY_NODE_ID)
	{
	isDiscovered=pkt->payload[PKT_REQUEST_SEQUENCE_NUM_OFFSET];//Mark Itself
	hop_to_gateway=pkt->src;//special path to Gateway       
	tx_enter();
	init_pkt(&tx_pkt);
	tx_pkt.type = PKT_TYPE_DISCOVERY_RESPONSE;
	tx_pkt.dest = pkt->src;
	send_pkt(&tx_pkt);	//Response To Sender  
	tx_exit();
	send_request_pkt(val);
	return;
	}
	
	if(isDiscovered==val){
	tx_enter();
	tx_pkt.type = PKT_TYPE_DISCOVERY_RESPONSE;
	tx_pkt.dest = hop_to_gateway;		
	send_pkt(&tx_pkt);
	tx_exit();
	return;       //Broadcast for all neighbours
	}
	if(isDiscovered<val){
	 send_request_pkt(val);
	 tx_enter();
	 tx_pkt.type = PKT_TYPE_DISCOVERY_RESPONSE;
         tx_pkt.dest = hop_to_gateway;
	 send_pkt(&tx_pkt);
	 tx_exit();
	}
}


static bool newNode(edge *array,node_id_t dest_node,uint8_t degree){
	uint8_t cntr=0;
	if(degree==0)
		return true;
	else
		for(cntr=0;cntr<degree;cntr++)
			if(array[cntr].v==dest_node)
				return false;
	
		return true;
}

static void extract_graph_info(pkt_t *pkt){

	uint8_t count=0;
	uint8_t hops=pkt->hops;
	node_id_t node1,node2;

	if(hops>MAX_PATH_LEN)
	hops=MAX_PATH_LEN;

	//hops=hops-1;
	nrk_kprintf(PSTR("Adding new Path"));

	for(count=hops;count>0;count--){

	node1=pkt->path[count];
	node2=pkt->path[count-1];
	
	if(newNode(network.edges[node1],node2,network.degree[node1])){
	
		network.edges[node1][network.degree[node1]].v=node2;
		network.edges[node1][network.degree[node1]].weight=1;
		network.degree[node1]++;
	}
	if(newNode(network.edges[node2],node1,network.degree[node2])){
	
		network.edges[node2][network.degree[node2]].v=node1;
		network.edges[node2][network.degree[node2]].weight=1;
		network.degree[node2]++;
		}

	}	
}

static void handle_discovery_response(pkt_t *pkt){
	nrk_kprintf(PSTR("In Handle Response\r\n"));
	uint8_t rc=0;
	if(this_node_id==GATEWAY_NODE_ID){
               	extract_graph_info(pkt);
 		nrk_kprintf(PSTR("Response Packet Reached Gateway!!\r\n"));
		return;
	}
	else
	{
 
	if(isDiscovered!=0)
        	{
				tx_enter();
				pkt->dest = hop_to_gateway;
		 		rc = send_pkt(pkt);
				tx_exit();
				if (rc == NRK_ERROR)
        			    nrk_kprintf(PSTR("WARN: hot potato fwd failed\r\n"));

        	}else{
			nrk_kprintf(PSTR("Response Transmitted to Undiscovered Node"));
	
	    	     }	
	}
}


static void handle_potato(pkt_t *pkt)
{
    
    int8_t rc;
    nrk_time_t hold_time = {2, 0};

    nrk_kprintf(PSTR("Got hot potato, forwarding back to src: "));
    printf("%d\r\n", pkt->src);

    nrk_led_set(LED_HOLDING_POTATO);
    nrk_wait(hold_time);
    nrk_led_clr(LED_HOLDING_POTATO);

    tx_enter();
    pkt->dest = pkt->src;
    rc = send_pkt(pkt);
    tx_exit();

    if (rc == NRK_ERROR)
        nrk_kprintf(PSTR("WARN: hot potato fwd failed\r\n"));
}

static void handle_msg(pkt_t *pkt)
{
    int8_t rc;
    node_id_t sender, recipient;
    uint8_t seq;

    if (pkt->len < PKT_MSG_HDR_LEN) {
        nrk_kprintf(PSTR("WARN: msg pkt too short\r\n"));
        return;
    }

    sender = pkt->payload[PKT_MSG_SENDER_OFFSET];
    recipient = pkt->payload[PKT_MSG_RECIPIENT_OFFSET];
    seq = pkt->payload[PKT_MSG_SEQ_OFFSET];

    nrk_kprintf(PSTR("Got msg pkt, sending ack: "));
    printf("sdr %d rcp %d seq %d\r\n", pkt->src, recipient, seq);

    tx_enter();
    init_pkt(&tx_pkt);
    tx_pkt.type = PKT_TYPE_MSG_ACK;
    tx_pkt.payload[PKT_MSG_ACK_SEQ_OFFSET] = seq;
    tx_pkt.len += PKT_MSG_ACK_SEQ_LEN;
    tx_pkt.dest = pkt->src;
    rc = send_pkt(&tx_pkt);
    tx_exit();
    if (rc != NRK_OK) {
        nrk_kprintf(PSTR("App pkt ack reply failed, giving up.\r\n"));
        return;
    }

    if (recipient == this_node_id) {
        nrk_kprintf(PSTR("Msg reached destination\r\n"));

        /* TODO: this should be put in a queue and processed from app task */
        if (rcv_msg_handler) {
            rcv_msg_handler(sender, pkt->payload + PKT_MSG_HDR_LEN,
                                    pkt->len - PKT_MSG_HDR_LEN);
        } else {
            nrk_kprintf(PSTR("WARN: No receive message handler installed\r\n"));
        }
    } else { /* forward */
        nrk_kprintf(PSTR("Relaying msg pkt to next hop\r\n"));
        rc = relay_msg_pkt(pkt);
        if (rc != NRK_OK) {
            nrk_kprintf(PSTR("WARN: failed to relay msg\r\n"));
            /* TODO: update routing table upon failure */
        }
    }
}

static void handle_msg_ack(pkt_t *pkt)
{
    uint8_t i;
    uint8_t seq;
    msg_ack_req_t *ack_req;
    bool req_found = false;

    if (pkt->len < PKT_MSG_ACK_SEQ_LEN) {
        nrk_kprintf(PSTR("WARN: msg ack pkt too short\r\n"));
        return;
    }

    seq = pkt->payload[PKT_MSG_ACK_SEQ_OFFSET];

    nrk_kprintf(PSTR("Received msg ack for seq "));
    printf("%d\r\n", seq);

    for (i = 0; i < MSG_ACK_QUEUE_SIZE; ++i) {
        ack_req = &msg_ack_queue[i];
        if (IS_VALID_ACK_REQ(ack_req)) {
            if (ack_req->dest == pkt->src && ack_req->seq == seq) {
                /* Ack has been received, clear the request slot */
                nrk_kprintf(PSTR("Msg ack request fulfilled: idx "));
                printf("%d\r\n", i);
                req_found = true;
                release_msg_ack_req(ack_req);
            }
        }
    }

    if (!req_found)
        nrk_kprintf(PSTR("WARN: received unexpected msg ack\r\n"));
}

int8_t send_message(node_id_t recipient, uint8_t *payload, uint8_t len)
{
    int8_t rc = NRK_OK;
    pkt_t *msg_pkt;

    nrk_kprintf(PSTR("Enqueue msg for rcp "));
    printf("%d \r\n", recipient);

    msg_pkt = &tx_msg_queue[tx_msg_tail];

    init_pkt(msg_pkt);
    msg_pkt->type = PKT_TYPE_MSG;
    msg_pkt->payload[PKT_MSG_SENDER_OFFSET] = this_node_id;
    msg_pkt->len += PKT_MSG_SENDER_LEN;
    msg_pkt->payload[PKT_MSG_RECIPIENT_OFFSET] = recipient;
    msg_pkt->len += PKT_MSG_RECIPIENT_LEN;
    msg_pkt->payload[PKT_MSG_SEQ_OFFSET] = msg_sent_seq;
    msg_pkt->len += PKT_MSG_SEQ_LEN;
    memcpy(msg_pkt->payload + PKT_MSG_HDR_LEN, payload, len);
    msg_pkt->len += len;

    tx_msg_tail = (tx_msg_tail + 1) % TX_MSG_QUEUE_SIZE;
    if (tx_msg_tail == tx_msg_head)
        ABORT("ERROR: tx msg queue overflow\r\n");

    nrk_event_signal(router_signal);

    return rc;
}


static void set_rcv_msg_handler(rcv_msg_handler_t handler)
{
    rcv_msg_handler = handler;
}

/* TODO: move this out to app.c (msg layer has acces only to routed msgs) */
static void receive_message(node_id_t sender, uint8_t *payload, uint8_t len)
{
    int8_t rc;
    nrk_time_t ping_processing_time = {1, 0};
    msg_type_t type;
    uint8_t msg[MSG_PING_LEN];
    uint8_t msg_len = 0;
    uint8_t token;

    type = payload[MSG_TYPE_OFFSET];

    nrk_kprintf(PSTR("Received msg: "));
    printf("sdr %d type %s\r\n", sender, msg_type_to_string(type));

    nrk_led_set(LED_PROCESSING_MSG);

    switch (type) {
        case MSG_TYPE_PING:
            nrk_wait(ping_processing_time);

            token = payload[MSG_PONG_TOKEN_OFFSET];
            nrk_kprintf(PSTR("Ping with token, sending pong: "));
            printf("%d\r\n", token);

            msg[MSG_TYPE_OFFSET] = MSG_TYPE_PONG;
            msg_len += MSG_TYPE_LEN;
            msg[MSG_PONG_TOKEN_OFFSET] = token;
            msg_len += MSG_PONG_TOKEN_LEN;
            rc = send_message(sender, msg, msg_len);
            if (rc != NRK_OK)
                nrk_kprintf(PSTR("WARN: failed to send pong msg\r\n"));
            break;

        case MSG_TYPE_PONG:
            token = payload[MSG_PONG_TOKEN_OFFSET];
            nrk_kprintf(PSTR("Pong with token: "));
            printf("%d\r\n", token);
            nrk_led_clr(LED_PING_SENT);
            break;

        default:
            nrk_kprintf(PSTR("WARN: unknown msg pkt type\r\n"));
    }

    nrk_led_clr(LED_PROCESSING_MSG);
}


static void update_neighbor(node_id_t node, uint8_t rssi)
{
    int8_t i, idx = -1;

    for (i = 0; i < num_neighbors; ++i) {
        if (neighbors[i].id == node) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (num_neighbors < MAX_NEIGHBORS) {
            idx = num_neighbors++;
            neighbors[idx].id = node;

            nrk_kprintf(PSTR("NEIGHBOR: added "));
            printf("%d\r\n", node);
        } else {
            /* TODO: find a victim to replace based on last heard and/or rssi */
            idx = 0;
            nrk_kprintf(PSTR("NEIGHBOR: removed "));
            printf("%d\r\n", neighbors[idx].id);
            neighbors[idx].id = node;
        }
    }
    nrk_time_get(&neighbors[idx].last_heard);
    neighbors[idx].rssi = rssi;

    nrk_kprintf(PSTR("NEIGHBOR: updated "));
    printf("%d\r\n", node);
}

static void handle_routes(pkt_t *pkt)
{
    int8_t rc;
    route_matrix_t *route_matrix;
    uint8_t ver;

    ver = pkt->payload[PKT_ROUTES_VER_OFFSET];

    nrk_kprintf(PSTR("Got routing table: ver "));
    printf("%d -> %d\r\n", routes_ver, ver);

    if (ver == routes_ver) {
        nrk_kprintf(PSTR("Ignored routing table pkt: already up-to-date\r\n"));
        return;
    }

    route_matrix = (route_matrix_t *)&pkt->payload[PKT_ROUTES_TABLE_OFFSET];

    memcpy(routes, &(*route_matrix)[this_node_id],
           sizeof(node_id_t) * MAX_NODES);
    routes_ver = ver;

    print_routes();

    tx_enter();
    pkt->dest = BROADCAST_NODE_ID;
    rc = send_pkt(pkt);
    tx_exit();

    if (rc != NRK_OK)
        nrk_kprintf(PSTR("WARN: failed to re-broadcast routes\r\n"));
}

static void handle_packet(pkt_t *pkt)
{
    int i;

    printf ("RX PKT [%d]: ", pkt->len);
    for (i = 0; i < pkt->len; i++)
        printf ("%02x ", pkt->buf[i]);
    printf ("\r\n");

    if (pkt->len < PKT_HDR_LEN) {
        nrk_kprintf(PSTR("Pkt too short\r\n"));
        return;
    }


    pkt->type = pkt->buf[PKT_HDR_TYPE_OFFSET];
    pkt->hops = pkt->buf[PKT_HDR_HOPS_OFFSET];
    pkt->path_len = pkt->hops <= MAX_PATH_LEN ? pkt->hops : MAX_PATH_LEN;
		  
    for (i = 0; i < pkt->path_len; ++i)
        pkt->path[i] = pkt->buf[PKT_HDR_PATH_OFFSET + i];

    pkt->payload = pkt->buf + PKT_HDR_LEN;
    pkt->payload_len = pkt->len - PKT_HDR_LEN;

    printf("RX [%s] %d -> %d\r\n", pkt_type_to_string(pkt->type),
           pkt->src, pkt->dest);

    switch (pkt->type) {
        case PKT_TYPE_PING:
            handle_ping(pkt);
            break;
        case PKT_TYPE_PONG:
            handle_pong(pkt);
            break;
        case PKT_TYPE_POTATO:
            handle_potato(pkt);
            break;
        case PKT_TYPE_MSG:
            handle_msg(pkt);
            break;
        case PKT_TYPE_MSG_ACK:
            handle_msg_ack(pkt);
            break;
	case PKT_TYPE_DISCOVERY_REQUEST:
            handle_discovery_request(pkt);
            break;
	case PKT_TYPE_DISCOVERY_RESPONSE:
            handle_discovery_response(pkt);
            break;
        case PKT_TYPE_ROUTES:
            handle_routes(pkt);
            break;
        default:
            nrk_kprintf(PSTR("Unknown pkt type"));
    }
}

static void handle_queued_pkts()
{
#ifdef VERBOSE
    nrk_kprintf(PSTR("Handling queued pkts: "));
    printf("head %d tail %d\r\n", rx_head, rx_tail);
#endif

    while (rx_head != rx_tail) {
        printf("HDL <- h %d (t %d)\r\n", rx_head, rx_tail);
        handle_packet(&rx_queue[rx_head]);
        rx_head = (rx_head + 1) % RX_QUEUE_SIZE;
    }
}

static uint8_t timeout_msg_ack_reqs()
{
    uint8_t i;
    msg_ack_req_t *ack_req;
    nrk_time_t now, elapsed;
    node_id_t next_hop;
    uint8_t num_reqs = 0;

#ifdef VERBOSE
    nrk_kprintf(PSTR("Timing out msg ack requests\r\n"));
#endif

    nrk_time_get(&now);
    for (i = 0; i < MSG_ACK_QUEUE_SIZE; ++i) {
        ack_req = &msg_ack_queue[i];
        if (IS_VALID_ACK_REQ(ack_req)) {
            nrk_time_sub(&elapsed, now, ack_req->time_sent);
            if (time_cmp(&elapsed, &msg_ack_timeout) >= 0) {
                nrk_kprintf(PSTR("Msg ack req timed out: idx "));
                printf("%d\r\n", i);

                /* Update the routing table to another neighbor */
                next_hop = pick_alt_route(ack_req->src);
                if (IS_VALID_NODE_ID(next_hop)) {
                    routes[ack_req->recipient] = next_hop;
                } else {
                    nrk_kprintf(PSTR("WARN: no alternative routes found\r\n"));
                    /* TODO: reflect the packet back to source */
                }

                release_msg_ack_req(ack_req);
            }
            num_reqs++;
        }
    }

    return num_reqs;
}

static void send_queued_msgs()
{
    int8_t rc;

#ifdef VERBOSE
    nrk_kprintf(PSTR("Sending queued msgs: "));
    printf("head %d tail %d\r\n", tx_msg_head, tx_msg_tail);
#endif

    while (tx_msg_head != tx_msg_tail) {
        nrk_kprintf(PSTR("Send queued msg: head "));
        printf("%d\r\n", tx_msg_head);

        tx_enter();
        rc = relay_msg_pkt(&tx_msg_queue[tx_msg_head]);
        if (rc != NRK_OK)
            nrk_kprintf(PSTR("WARN: failed to relay queued msg pkt\r\n"));
        tx_exit();
        tx_msg_head = (tx_msg_head + 1) % TX_MSG_QUEUE_SIZE;
    }
}

static void build_routes(graph *net_graph, route_matrix_t *route_matrix)
{
    node_id_t source, dest;
    nrk_kprintf(PSTR("Building routes from network graph... "));
    for (source = 0; source < MAX_NODES; ++source) {
        dijkstra(net_graph, source, (int *)next_hops); /* evil cast node_id_t */
        for (dest = 0; dest < MAX_NODES; ++dest)
            (*route_matrix)[dest][source] = next_hops[dest];
    }
    nrk_kprintf(PSTR("done\r\n"));
}

#ifdef MODE_WRITE_CONFIG
static uint32_t read_number()
{
#define NUM_DIGITS 2

    uint32_t n = 0;
    char s[NUM_DIGITS + 1] = {0};
    int i = 0;
    do {
        if(nrk_uart_data_ready(NRK_DEFAULT_UART)) {
            s[i++] = getchar();
            uart_router_signal = 0;
        } else {
            nrk_event_wait(SIG(uart_router_signal));
        }
    } while(i < NUM_DIGITS);

    sscanf(s, "%lu", &n);
    printf(s, "\r\n");
    return n;
}

static void write_config()
{
    uint32_t chan;
    uint32_t node_id;

    nrk_kprintf(PSTR("Channel: "));
    chan = read_number();
    nrk_kprintf(PSTR("\r\n"));

    nrk_kprintf(PSTR("Node ID: "));
    node_id = read_number();
    nrk_kprintf(PSTR("\r\n"));

    nrk_eeprom_write_byte(EE_MAC_ADDR_0, node_id);
    nrk_eeprom_write_byte(EE_CHANNEL, chan);

    printf("Wrote: chan %lu, node %lu\r\n", chan, node_id);
}
#endif

static void rx_task ()
{
    int8_t rc;
    uint8_t *local_rx_buf;
    pkt_t *pkt;

    bmac_init(rf_chan);

    rc = bmac_set_rx_check_rate(check_period);
    if(rc == NRK_ERROR)
        ABORT("ERROR: bmac_set_rx_check_rate\r\n");

    bmac_set_cca_thresh(CHANNEL_CLEAR_THRESHOLD_DB); 

    bmac_rx_pkt_set_buffer (rx_buf, RF_MAX_PAYLOAD_SIZE);

    while (1) {
        nrk_kprintf(PSTR("rx_task: waiting for pkt\r\n"));
        rc = bmac_wait_until_rx_pkt ();
        pkt = &rx_queue[rx_tail];
        local_rx_buf = bmac_rx_pkt_get (&pkt->len, &pkt->rssi);

        pkt->src = local_rx_buf[PKT_HDR_SRC_OFFSET];
        pkt->dest = local_rx_buf[PKT_HDR_DEST_OFFSET];

        if (!IS_REACHEABLE(pkt->src)) {
            nrk_kprintf(PSTR("Dropped pkt: src unrecheable by topology: src "));
            printf("%d\r\n", pkt->src);
            bmac_rx_pkt_release ();
            continue;
        }

        update_neighbor(pkt->src, pkt->rssi);

        if (pkt->dest != this_node_id && pkt->dest != BROADCAST_NODE_ID){
            nrk_kprintf(PSTR("Dropped pkt: not for me: dest "));
            printf("%d\r\n", pkt->dest);
            bmac_rx_pkt_release ();
            continue;
        }

        memcpy(pkt->buf, local_rx_buf, pkt->len);
        bmac_rx_pkt_release ();

        printf("RX [%d] -> t %d (h %d)\r\n", pkt->len, rx_tail, rx_head);

        rx_tail = (rx_tail + 1) % RX_QUEUE_SIZE;
        if (rx_tail == rx_head)
            ABORT("ERROR: rx queue overflow\r\n");
        nrk_event_signal(router_signal);
    }
    ABORT("ERROR: rx task exited\r\n");
}

static void router_task ()
{
    int8_t rc;
    nrk_sig_mask_t wait_signal_mask;
    uint8_t num_msg_ack_reqs;

#ifdef MODE_WRITE_CONFIG
    write_config();
    ABORT("Wrote config, halting.\r\n");
#endif

    rc = nrk_signal_register(router_signal);
    if (rc == NRK_ERROR)
        ABORT("ERROR: nrk_signal_register rx signal\r\n");

    while (1) {
        handle_queued_pkts();
        send_queued_msgs();
        num_msg_ack_reqs = timeout_msg_ack_reqs();

        wait_signal_mask = SIG(router_signal);
        if (num_msg_ack_reqs > 0)
            wait_signal_mask |= SIG(nrk_wakeup_signal);

        nrk_kprintf(PSTR("Router waiting: num msg ack reqs "));
        printf("%d\r\n", num_msg_ack_reqs);
        nrk_event_wait( wait_signal_mask );
        nrk_kprintf(PSTR("Router awake\r\n"));
    }

    ABORT("ERROR: router task exited\r\n");
}

#ifdef ENABLE_PING_TASK
static void ping_task ()
{
    int8_t rc;
#ifdef MODE_PING_MSG
    uint8_t msg[MSG_PING_LEN];
    uint8_t msg_len;
#endif

    while (!bmac_started())
        nrk_wait_until_next_period();

    ping_token = 10 * this_node_id;

    while (1) {

        if (this_node_id == PING_SENDER) {

#if defined(MODE_PING_PKT) || defined(MODE_PING_MSG)
            ping_token++;

            nrk_kprintf(PSTR("Sending ping with token: "));
            printf("%d\r\n", ping_token);

            nrk_led_set(LED_PING_SENT);

#if defined(MODE_PING_PKT)
            tx_enter();
            init_pkt(&tx_pkt);
            tx_pkt.type = PKT_TYPE_PING;
            tx_pkt.dest = PING_RECIPIENT;
            tx_pkt.payload[PKT_PING_TOKEN_OFFSET] = ping_token;
            tx_pkt.len += PKT_PING_TOKEN_LEN;
            rc = send_pkt(&tx_pkt);
            tx_exit();

#elif defined(MODE_PING_MSG)
            msg_len = 0;
            msg[MSG_TYPE_OFFSET] = MSG_TYPE_PING;
            msg_len += MSG_TYPE_LEN;
            msg[MSG_PING_TOKEN_OFFSET] = ping_token;
            msg_len += MSG_PING_TOKEN_LEN;
            rc = send_message(PING_RECIPIENT, msg, msg_len);
#endif

            if (rc != NRK_OK) {
                nrk_kprintf(PSTR("Ping send failed, ignored.\r\n"));
                nrk_led_clr(LED_PING_SENT);
            } else {
                nrk_kprintf(PSTR("Ping sent successfully.\r\n"));
            }

#elif defined(MODE_ROUTES)

            nrk_kprintf(PSTR("Sending routes\r\n"));

            tx_enter();
            init_pkt(&tx_pkt);
            tx_pkt.type = PKT_TYPE_ROUTES;
            tx_pkt.dest = BROADCAST_NODE_ID;
            tx_pkt.payload[PKT_ROUTES_VER_OFFSET] = ++routes_ver;
            tx_pkt.len += PKT_ROUTES_VER_LEN;
            memcpy(tx_pkt.payload + PKT_ROUTES_TABLE_OFFSET, test_routes,
                   sizeof(route_matrix_t));
            tx_pkt.len += sizeof(route_matrix_t);
            rc = send_pkt(&tx_pkt);
            tx_exit();

            if (rc != NRK_OK)
                nrk_kprintf(PSTR("WARN: failed to send routes\r\n"));

#endif // MODE_PING_PKT || MODE_PING_MSG
        }

        nrk_wait_until_next_period();
    }

    ABORT("ERROR: ping task exited\r\n");
}
#endif


#ifdef ENABLE_REQUEST_TASK
static void request_task ()
{
    int8_t rc;

    while (!bmac_started())
        nrk_wait_until_next_period();

    while (1) {
        if(this_node_id==GATEWAY_NODE_ID){
            memset(&network, 0, sizeof(network));
            nrk_kprintf(PSTR("Sending discovery request: "));
            request_number++;
            nrk_led_set(LED_PING_SENT);
            rc = send_request_pkt(request_number);
            if (rc != NRK_OK) {
                nrk_kprintf(PSTR("Ping send failed, ignored.\r\n"));
                nrk_led_clr(LED_PING_SENT);
            }
        }
        nrk_wait_until_next_period();
    }
    ABORT("ERROR: ping task exited\r\n");
}
#endif


#ifdef DISPLAY_NETWORK
static void send_network_data(node_id_t node)
{
   uint8_t count=0;
	for(count=0;count<network.degree[node];count++){
		nrk_kprintf("Edges");
		if(network.edges[node][count].v!=0)
		printf("%d",network.edges[node][count].v);
		else
		nrk_kprintf(PSTR("zero"));
	 return;
	}
	return;
}	

static void display_network()
{

	node_id_t node= 1;	
        nrk_kprintf(PSTR("printing network/graph\r\n"));
    
	while (1) {
        print_graph(&network);
        nrk_wait_until_next_period();
    }

    nrk_kprintf(PSTR("ERROR: print task exited\r\n"));
    nrk_halt();
}

#endif



static void nrk_create_taskset ()
{
    RX_TASK.task = rx_task;
    RX_TASK.Ptos = (void *) &rx_task_stack[NRK_APP_STACKSIZE - 1];
    RX_TASK.Pbos = (void *) &rx_task_stack[0];
    RX_TASK.prio = 4;
    RX_TASK.FirstActivation = TRUE;
    RX_TASK.Type = BASIC_TASK;
    RX_TASK.SchType = PREEMPTIVE;
    RX_TASK.period.secs = 1;
    RX_TASK.period.nano_secs = 0;
    RX_TASK.cpu_reserve.secs = 1;
    RX_TASK.cpu_reserve.nano_secs = 500 * NANOS_PER_MS;
    RX_TASK.offset.secs = 0;
    RX_TASK.offset.nano_secs = 0;
    nrk_activate_task (&RX_TASK);

    ROUTER_TASK.task = router_task;
    ROUTER_TASK.Ptos = (void *) &router_task_stack[NRK_APP_STACKSIZE - 1];
    ROUTER_TASK.Pbos = (void *) &router_task_stack[0];
    ROUTER_TASK.prio = 2;
    ROUTER_TASK.FirstActivation = TRUE;
    ROUTER_TASK.Type = BASIC_TASK;
    ROUTER_TASK.SchType = PREEMPTIVE;
    ROUTER_TASK.period.secs = 1;
    ROUTER_TASK.period.nano_secs = 0;
    #ifdef MODE_WRITE_CONFIG
    ROUTER_TASK.cpu_reserve.secs = 10;
    ROUTER_TASK.cpu_reserve.nano_secs = 0;
    #else
    ROUTER_TASK.cpu_reserve.secs = 1;
    ROUTER_TASK.cpu_reserve.nano_secs = 500 * NANOS_PER_MS;
    #endif
    ROUTER_TASK.offset.secs = 0;
    ROUTER_TASK.offset.nano_secs = 0;
    nrk_activate_task (&ROUTER_TASK);

#ifdef ENABLE_PING_TASK
    PING_TASK.task = ping_task;
    PING_TASK.Ptos = (void *) &ping_task_stack[NRK_APP_STACKSIZE - 1];
    PING_TASK.Pbos = (void *) &ping_task_stack[0];
    PING_TASK.prio = 1;
    PING_TASK.FirstActivation = TRUE;
    PING_TASK.Type = BASIC_TASK;
    PING_TASK.SchType = PREEMPTIVE;
    PING_TASK.period.secs = 10;
    PING_TASK.period.nano_secs = 0;
    PING_TASK.cpu_reserve.secs = 1;
    PING_TASK.cpu_reserve.nano_secs = 500 * NANOS_PER_MS;
    PING_TASK.offset.secs = 0;
    PING_TASK.offset.nano_secs = 0;
    nrk_activate_task (&PING_TASK);
#endif

#ifdef ENABLE_REQUEST_TASK
    REQUEST_TASK.task = request_task;
    REQUEST_TASK.Ptos = (void *) &request_task_stack[NRK_APP_STACKSIZE - 1];
    REQUEST_TASK.Pbos = (void *) &request_task_stack[0];
    REQUEST_TASK.prio = 4;
    REQUEST_TASK.FirstActivation = TRUE;
    REQUEST_TASK.Type = BASIC_TASK;
    REQUEST_TASK.SchType = PREEMPTIVE;
    REQUEST_TASK.period.secs = 10;
    REQUEST_TASK.period.nano_secs = 0;
    REQUEST_TASK.cpu_reserve.secs = 1;
    REQUEST_TASK.cpu_reserve.nano_secs = 500 * NANOS_PER_MS;
    REQUEST_TASK.offset.secs = 0;
    REQUEST_TASK.offset.nano_secs = 0;
    nrk_activate_task (&REQUEST_TASK);
#endif

#ifdef DISPLAY_NETWORK
    DISPLAY_TASK.task = display_network;
    DISPLAY_TASK.Ptos = (void *) &display_task_stack[NRK_APP_STACKSIZE - 1];
    DISPLAY_TASK.Pbos = (void *) &display_task_stack[0];
    DISPLAY_TASK.prio = 1;
    DISPLAY_TASK.FirstActivation = TRUE;
    DISPLAY_TASK.Type = BASIC_TASK;
    DISPLAY_TASK.SchType = PREEMPTIVE;
    DISPLAY_TASK.period.secs = 11;
    DISPLAY_TASK.period.nano_secs = 0;
    DISPLAY_TASK.cpu_reserve.secs = 1;
    DISPLAY_TASK.cpu_reserve.nano_secs = 500 * NANOS_PER_MS;
    DISPLAY_TASK.offset.secs = 0;
    DISPLAY_TASK.offset.nano_secs = 0;
    nrk_activate_task(&DISPLAY_TASK);
#endif


    nrk_kprintf(PSTR("Create done\r\n"));
}

static void read_config()
{
    this_node_id = nrk_eeprom_read_byte(EE_MAC_ADDR_0);
    printf("NODE ID: %u\r\n", this_node_id);

#if 0
    rf_chan = nrk_eeprom_read_byte(EE_CHANNEL);
#else
    rf_chan = 15;
#endif
    printf("RF CHAN: %u\r\n", rf_chan);
}

int main ()
{
    nrk_setup_ports ();
    nrk_setup_uart (UART_BAUDRATE_115K2);

    uart_rx_signal = nrk_uart_rx_signal_get();
    nrk_signal_register(uart_rx_signal); 

    nrk_init ();

    nrk_led_clr (0);
    nrk_led_clr (1);
    nrk_led_clr (2);
    nrk_led_clr (3);

    nrk_time_set (0, 0);

    read_config();

#ifdef TEST_ROUTES
    memcpy(routes, test_routes[this_node_id], MAX_NODES);
    print_routes();
#endif

    bmac_task_config ();

    nrk_create_taskset ();

    tx_sem = nrk_sem_create(1, 2);
    if (!tx_sem)
        ABORT("ERROR: nrk_sem_create tx semaphore\r\n");

    relay_msg_sem = nrk_sem_create(1, 1);
    if (!relay_msg_sem)
        ABORT("ERROR: nrk_sem_create msg pkt send semaphore\r\n");

    router_signal = nrk_signal_create();
    if (router_signal == NRK_ERROR)
        ABORT("ERROR: nrk_signal_create rx event\r\n");

    /* TODO: move to app_init */
    set_rcv_msg_handler(receive_message);

    nrk_start ();

    return 0;

}
