#include <nrk.h>
#include <include.h>
#include <ulib.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/sleep.h>
#include <hal.h>
#include <nrk_error.h>
#include <nrk_timer.h>

#include "cfg.h"
#include "node_id.h"
#include "config.h"
#include "parse.h"
#include "packets.h"
#include "routes.h"
#include "time.h"
#include "rxtx.h"
#include "output.h"
#if ENABLE_RFTOP
#include "rftop.h"
#endif

#include "router.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_ROUTER

#define IS_VALID_RX_MSG_PKT(pkt) (IS_VALID_NODE_ID((pkt)->src))

/* Router layer packets */

/* Ping pkt fields (bytes) */
#define PKT_PING_TOKEN_OFFSET 0
#define PKT_PING_TOKEN_LEN    1

/* Pong pkt fields (bytes) */
#define PKT_PONG_TOKEN_OFFSET 0
#define PKT_PONG_TOKEN_LEN    1

/* Msg pkt fields (bytes) */
#define PKT_MSG_SENDER_OFFSET    0
#define PKT_MSG_SENDER_LEN       1
#define PKT_MSG_RECIPIENT_OFFSET 1
#define PKT_MSG_RECIPIENT_LEN    1
#define PKT_MSG_SEQ_OFFSET       2
#define PKT_MSG_SEQ_LEN          1
#define PKT_MSG_PORT_OFFSET      3
#define PKT_MSG_PORT_LEN         1
#define PKT_MSG_TYPE_OFFSET      4
#define PKT_MSG_TYPE_LEN         1

#define PKT_MSG_HDR_LEN ( \
    PKT_MSG_SENDER_LEN + \
    PKT_MSG_RECIPIENT_LEN + \
    PKT_MSG_SEQ_LEN + \
    PKT_MSG_PORT_LEN + \
    PKT_MSG_TYPE_LEN )

/* Msg ack pkt fields (bytes) */
#define PKT_MSG_ACK_SEQ_OFFSET   0
#define PKT_MSG_ACK_SEQ_LEN      1

/* Routes pkt fields (bytes) */
/* See routes.h (shared with discover.c) */

typedef enum {
    TX_MSG_STATE_INVALID = 0,
    TX_MSG_STATE_ALLOCED,
    TX_MSG_STATE_READY,
    TX_MSG_STATE_SENT,
} tx_msg_state_t;

static const char tx_msg_state_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [TX_MSG_STATE_INVALID] = "<invalid>",
    [TX_MSG_STATE_ALLOCED] = "ALLOCED",
    [TX_MSG_STATE_READY] = "READY",
    [TX_MSG_STATE_SENT] = "SENT",
};

typedef struct {
    tx_msg_state_t state;
    pkt_t pkt;
    node_id_t sender;
    node_id_t recipient;
    uint8_t seq;
    uint8_t attempt;
    nrk_time_t next_attempt;
    uint8_t tx_handle;
} tx_msg_pkt_t;

typedef struct {
    node_id_t id;
    uint8_t seq; /* last msg seq number received from this node */
} peer_t;

typedef pkt_t rx_msg_pkt_t; /* so far, rx msg pkts don't need extra fields */

/* static */ node_id_t routes[MAX_NODES];
static uint8_t routes_ver = 0;

static const char peers_name[] PROGMEM = "peers";
static peer_t peers_data[MAX_PEERS];
static node_id_t peers_ids[MAX_PEERS];
static nodelist_t peers = {
    .name = peers_name,
    .log_cat = LOG_CATEGORY,
    .nodes = peers_ids,
    .size = MAX_PEERS,
};

static uint8_t msg_sent_seq;

static tx_msg_pkt_t tx_msg_queue[TX_MSG_QUEUE_SIZE];
static nrk_sem_t *tx_msg_queue_sem;

static nrk_task_type ROUTER_TASK;
static NRK_STK router_task_stack[STACKSIZE_ROUTER];

static nrk_sig_t tx_msg_signal;
static nrk_sig_t rx_msg_signal;

static bool tx_msg_event; /* a flag with same meaning as tx_msg_signal */

static uint8_t exp_token; /* token expected in pong for sent ping */

/* Tx pkt buffer shared by different funcs of same task: ok to share because
 * send_pkt makes a copy. */
static pkt_t tx_pkt;
static pkt_t rcv_pkt; /* recieved packet is copied into here */

static listener_t *listeners[MAX_LISTENERS];
static uint8_t num_listeners = 0;
static nrk_sem_t *listeners_sem;

static void print_routes()
{
    node_id_t dest;

    LOG("routes ver ");
    LOGP("%d: ", routes_ver);
    for (dest = 0; dest < MAX_NODES; ++dest)
        LOGP("%d <- %d; ", dest, routes[dest]);
    LOGP("\r\n");
}

static void clear_route(node_id_t dest)
{
    ASSERT(dest < MAX_NODES);
    routes[dest] = INVALID_NODE_ID;
}

static void set_route(node_id_t dest, node_id_t next_hop)
{
    ASSERT(dest < MAX_NODES);
    ASSERT(next_hop < MAX_NODES);

    routes[dest] = next_hop;
}

static node_id_t pick_alt_route(node_id_t excluded_node)
{
    nodelist_t *neighbors;
    uint8_t idx;

    neighbors = get_neighbors();

    if (neighbors->len == 0 ||
        (neighbors->len == 1 && neighbors->nodes[0] == excluded_node))
        return INVALID_NODE_ID;

    /* For now, just a random pick */
    do {
        idx = rand() % neighbors->len;
    } while (neighbors->nodes[idx] == excluded_node);

    return neighbors->nodes[idx];
}

static void heal_route(node_id_t recipient, node_id_t src)
{
    node_id_t next_hop;

    /* Update the routing table to another neighbor */
    next_hop = pick_alt_route(src);
    if (IS_VALID_NODE_ID(next_hop)) {
        LOG("updating routes: ");
        LOGP("%d: %d\r\n", recipient, next_hop);
        set_route(recipient, next_hop);
    } else {
        LOG("WARN: no alt routes to ");
        LOGP("%d (-%d)\r\n", recipient, src);
        /* TODO: reflect the packet back to source */
    }
}

static void set_tx_msg_state(tx_msg_pkt_t *msg_pkt, tx_msg_state_t state)
{
    LOG("msg tx state: seq "); LOGP("%d: ", msg_pkt->seq);
    LOGF(ENUM_TO_STR(msg_pkt->state, tx_msg_state_names));
    LOGA(" -> ");
    LOGF(ENUM_TO_STR(state, tx_msg_state_names));
    LOGNL();

    msg_pkt->state = state;

}

static void log_queued_tx_msg(tx_msg_pkt_t *msg_pkt)
{
    LOG("queued msg: ");
    LOGP("%u -> %u [%u] ", msg_pkt->sender, msg_pkt->recipient, msg_pkt->seq);
    LOGF(ENUM_TO_STR(msg_pkt->state, tx_msg_state_names));
    LOGA(" tx "); LOGP("%u", msg_pkt->tx_handle);
    LOGA(" attempt "); LOGP("%u", msg_pkt->attempt);
    LOGA("\r\n");
}

static void print_tx_queue() {
    uint8_t i;
    tx_msg_pkt_t *msg_pkt;

    for (i = 0; i < TX_MSG_QUEUE_SIZE; ++i) {
        msg_pkt = &tx_msg_queue[i];
        if (msg_pkt->state != TX_MSG_STATE_INVALID)
            log_queued_tx_msg(msg_pkt);
    }
}

static listener_t *lookup_listener(port_t port)
{
    listener_t *listener;
    int8_t i;

    for (i = 0; i < num_listeners; ++i) {
        listener = listeners[i];
        if (listener->active && listener->port == port)
            return listener;
    }
    return NULL;
}

static tx_msg_pkt_t *alloc_tx_msg_pkt()
{
    uint8_t i;
    tx_msg_pkt_t *msg_pkt;

    for (i = 0; i < TX_MSG_QUEUE_SIZE; ++i) {
        msg_pkt = &tx_msg_queue[i];

        nrk_sem_pend(tx_msg_queue_sem);
        if (msg_pkt->state == TX_MSG_STATE_INVALID) {
            msg_pkt->state = TX_MSG_STATE_ALLOCED;

            if (++msg_sent_seq == 0) /* not a valid seq number */
                ++msg_sent_seq;

            msg_pkt->seq = msg_sent_seq;
            nrk_sem_post(tx_msg_queue_sem);
            return msg_pkt;
        }
        nrk_sem_post(tx_msg_queue_sem);
    }
    return NULL;
}

static void release_tx_msg_pkt(tx_msg_pkt_t *msg_pkt)
{
    LOG("releasing msg tx: seq "); LOGP("%d\r\n", msg_pkt->seq);

    memset(msg_pkt, 0, sizeof(tx_msg_pkt_t));
    msg_pkt->state = TX_MSG_STATE_INVALID;
}

static int8_t relay_msg_pkt(tx_msg_pkt_t *msg_pkt)
{
    pkt_t *pkt = &msg_pkt->pkt;

    msg_pkt->tx_handle = 0; /* reset */

    /* Find next hop from routing table */
    if (msg_pkt->recipient != BROADCAST_NODE_ID) {
        pkt->dest = routes[msg_pkt->recipient];
        if (!IS_VALID_NODE_ID(pkt->dest))
        {
            LOG("WARN: no route: ");
            LOGP("%u --> %u [%u]\r\n", msg_pkt->sender, msg_pkt->recipient,
                 msg_pkt->seq);

            return NRK_ERROR;
        }
    } else {
        pkt->dest = BROADCAST_NODE_ID;
    }

    LOG("relaying msg pkt: ");
    LOGP("%u -> %u --> %u\r\n", msg_pkt->sender, pkt->dest, msg_pkt->recipient);

    return send_pkt(pkt, TX_FLAG_NOTIFY, &msg_pkt->tx_handle);
}

/* A ping packet triggers a pong packet to be sent to the source
 * with the same token as was received in the ping token (echo). */
static void handle_ping(pkt_t *pkt)
{
    uint8_t token;
    int8_t rc;

    if (pkt->len < PKT_PING_TOKEN_LEN) {
        LOG("ping pkt too short: ");
        LOGP("%u < %u\r\n", pkt->len, PKT_PING_TOKEN_LEN);
        return;
    }
    token = pkt->payload[PKT_PING_TOKEN_OFFSET];

    LOG("got ping: token "); LOGP("%u\r\n", token);

    nrk_led_set(led_proc_ping);
    nrk_wait(pong_delay);
    nrk_led_clr(led_proc_ping);

    /* Send pong reply */
    LOG("sending pong: token "); LOGP("%u\r\n", token);

    init_pkt(&tx_pkt);
    tx_pkt.type = PKT_TYPE_PONG;
    tx_pkt.payload[PKT_PONG_TOKEN_OFFSET] = token;
    tx_pkt.len += PKT_PONG_TOKEN_LEN;
    tx_pkt.dest = pkt->src;
    rc = send_pkt(&tx_pkt, TX_FLAG_NONE, NULL);
    if (rc != NRK_OK)
        LOG("WARN: failed to send pong\r\n");
}

/* A pong packet triggers no action, just report the token */
static void handle_pong(pkt_t *pkt)
{
    uint8_t token;

    if (pkt->len < PKT_PONG_TOKEN_LEN) {
        LOG("pong pkt too short: ");
        LOGP("%u < %u\r\n", pkt->len, PKT_PONG_TOKEN_LEN);
        return;
    }
    token = pkt->payload[PKT_PONG_TOKEN_OFFSET];

    if (token != exp_token) {
        LOG("WARN: pong with unexpected token: ");
        LOGP("%u/%u\r\n", token, exp_token);
        return;
    }

    LOG("pong with token: "); LOGP("%u\r\n", token);
    nrk_led_clr(led_awaiting_pong);
}

static void handle_msg(pkt_t *pkt)
{
    node_id_t sender, recipient;
    uint8_t seq, port, type;
    tx_msg_pkt_t *tx_msg_pkt;
    uint8_t rx_msg_idx;
    msg_t *rx_msg;
    listener_t *listener;
    int8_t peer_idx;
    peer_t *peer;

    if (pkt->len < PKT_MSG_HDR_LEN) {
        LOG("WARN: msg pkt too short: ");
        LOGP("%u/%u\r\n", pkt->len, PKT_MSG_HDR_LEN);
        return;
    }

    sender = pkt->payload[PKT_MSG_SENDER_OFFSET];
    recipient = pkt->payload[PKT_MSG_RECIPIENT_OFFSET];
    seq = pkt->payload[PKT_MSG_SEQ_OFFSET];

    LOG("got msg pkt: ");
    LOGP("%d --> %d [%d]\r\n", sender, recipient, seq);

    if (recipient == this_node_id || recipient == BROADCAST_NODE_ID) {
        port = pkt->payload[PKT_MSG_PORT_OFFSET];
        type = pkt->payload[PKT_MSG_TYPE_OFFSET];

        LOG("msg at dest: ");
        LOGP("%d --> %d", sender, recipient);
        LOGA(" seq "); LOGP("%u", seq);
        LOGA(": port "); LOGP("%u", port);
        LOGA(" len ");
        LOGP("%d/%d", pkt->payload_len - PKT_MSG_HDR_LEN, pkt->payload_len);
        LOGA("\r\n");

        peer_idx = nodelist_find(&peers, sender);
        if (peer_idx < 0) {
            peer_idx = nodelist_add(&peers, sender);
            peers_data[peer_idx].id = sender;
        }
        peer = &peers_data[peer_idx];

        if (seq == peer->seq) {
            LOG("msg dropped: duplicate seq: "); LOGP("%u\r\n", seq);
            return;
        }
        peer->seq = seq;

        listener = lookup_listener(port);
        if (!listener) {
            LOG("WARN: no listener on port ");
            LOGP("%d\r\n", port);
            return;
        }

        if (queue_full(&listener->queue)) {
            LOG("dropped msg: listener queue full: port ");
            LOGP("%d\r\n", port);
            return;
        }

        rx_msg_idx = queue_alloc(&listener->queue);
        rx_msg = &listener->queue_data[rx_msg_idx];
        rx_msg->sender = sender;
        rx_msg->recipient = recipient;
        rx_msg->type = type;
        rx_msg->len = pkt->payload_len - PKT_MSG_HDR_LEN;
        ASSERT(rx_msg->len <= sizeof(rx_msg->payload));
        memcpy(rx_msg->payload, pkt->payload + PKT_MSG_HDR_LEN, rx_msg->len);

        queue_enqueue(&listener->queue);
        nrk_event_signal(listener->signal);

    } else if (sender == this_node_id) {
        LOG("dropped msg: returned to sender: ");
        LOGP("%d --> %d", sender, recipient);
        LOGA(" seq "); LOGP("%u\r\n", seq);

    } else if (pkt->hops >= MAX_PATH_LEN) {
        LOG("dropped msg: max hops reached: ");
        LOGP("%d --> %d", sender, recipient);
        LOGA(" seq "); LOGP("%u\r\n", seq);

    } else { /* forward */
        LOG("relaying msg to next hop: ");
        LOGP("%d --> %d", sender, recipient);
        LOGA(" seq "); LOGP("%u\r\n", seq);

        tx_msg_pkt = alloc_tx_msg_pkt();
        if (!tx_msg_pkt) {
            LOG("WARN: fwd msg dropped: tx msg queue full\r\n");
            print_tx_queue();
            return;
        }

        tx_msg_pkt->sender = sender;
        tx_msg_pkt->recipient = recipient;
        tx_msg_pkt->seq = pkt->payload[PKT_MSG_SEQ_OFFSET];
        memcpy(&tx_msg_pkt->pkt, pkt, sizeof(pkt_t));

        LOG("enqueued msg for fwd:");
        LOGA(" snd "); LOGP("%u", sender);
        LOGA(" rcp "); LOGP("%u", recipient);
        LOGA(" seq "); LOGP("%u", tx_msg_pkt->seq);
        LOGA("\r\n");

        set_tx_msg_state(tx_msg_pkt, TX_MSG_STATE_READY);
        nrk_event_signal(tx_msg_signal);
        tx_msg_event = true; /* sending signal to self does not wakeup */
    }
}

void init_message(msg_t *msg)
{
    memset(msg, 0, sizeof(msg_t));
}

int8_t send_message(msg_t *msg)
{
    int8_t rc = NRK_OK;
    tx_msg_pkt_t *msg_pkt;
    pkt_t *pkt;

    if (!((IS_VALID_NODE_ID(msg->recipient) ||
        msg->recipient == BROADCAST_NODE_ID) &&
        msg->recipient != this_node_id)) {
        LOG("ERROR: invalid recipient\r\n");
        return NRK_ERROR;
    }

    msg_pkt = alloc_tx_msg_pkt();
    if (!msg_pkt) {
        LOG("WARN: msg dropped: tx msg queue full\r\n");
        return NRK_ERROR;
    }

    msg_pkt->sender = this_node_id;
    msg_pkt->recipient = msg->recipient;
    pkt = &msg_pkt->pkt;

    LOG("enqueue msg:");
    LOGA(" rcp "); LOGP("%u", msg_pkt->recipient);
    LOGA(" seq "); LOGP("%u", msg_pkt->seq);
    LOGNL();

    init_pkt(pkt);
    pkt->type = PKT_TYPE_MSG;
    pkt->payload[PKT_MSG_SENDER_OFFSET] = msg_pkt->sender;
    pkt->payload[PKT_MSG_RECIPIENT_OFFSET] = msg_pkt->recipient;
    pkt->payload[PKT_MSG_SEQ_OFFSET] = msg_pkt->seq;
    pkt->payload[PKT_MSG_PORT_OFFSET] = msg->port;
    pkt->payload[PKT_MSG_TYPE_OFFSET] = msg->type;
    pkt->len += PKT_MSG_HDR_LEN;
    ASSERT(msg->len < sizeof(pkt->buf)-(pkt->payload-pkt->buf)-PKT_MSG_HDR_LEN);
    memcpy(pkt->payload + PKT_MSG_HDR_LEN, msg->payload, msg->len);
    pkt->len += msg->len;

    set_tx_msg_state(msg_pkt, TX_MSG_STATE_READY);
    nrk_event_signal(tx_msg_signal);

    return rc;
}

void register_listener(listener_t *listener)
{
    uint8_t i;

    LOG("init listener: port "); LOGP("%d\r\n", listener->port);

    ASSERT(listener->signal > 0); /* caller must init */

    /* Protect listeners array to not alloc same slot twice */
    nrk_sem_pend(listeners_sem);

    for (i = 0; i < num_listeners; ++i)
        ASSERT(listeners[i]->port != listener->port);
    ASSERT(num_listeners < MAX_LISTENERS - 1);

    listeners[num_listeners++] = listener;

    nrk_sem_post(listeners_sem);
}

/* Task that wants to ever call receive_messages, must call this once first. */
void activate_listener(listener_t *listener)
{
    int8_t rc;

    LOG("activate listener: port "); LOGP("%d\r\n", listener->port);

    rc = nrk_signal_register(listener->signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: listener\r\n");

    listener->active = true;
}

void deactivate_listener(listener_t *listener)
{
    int8_t rc;

    LOG("deactivate listener: port "); LOGP("%d\r\n", listener->port);

    rc = nrk_signal_unregister(listener->signal);
    if (rc == NRK_ERROR)
        ABORT("unreg sig: listener\r\n");

    listener->active = false;
}

static void set_routes(route_matrix_t *route_matrix, uint8_t ver)
{
    LOG("setting routes to ver "); LOGP("%d\r\n", ver);

    memcpy(routes, &(*route_matrix)[this_node_id],
           sizeof(node_id_t) * MAX_NODES);
    routes_ver = ver;

    print_routes();
}

static int8_t do_ping(node_id_t dest, uint8_t token)
{
    LOG("sending ping to ");
    LOGP("%u", dest);
    LOGA(" token ");
    LOGP("%u\r\n", token);

    exp_token = token;
    nrk_led_set(led_awaiting_pong);

    init_pkt(&tx_pkt);
    tx_pkt.type = PKT_TYPE_PING;
    tx_pkt.dest = dest;
    tx_pkt.payload[PKT_PING_TOKEN_OFFSET] = token;
    tx_pkt.len += PKT_PING_TOKEN_LEN;
    return send_pkt(&tx_pkt, TX_FLAG_NONE, NULL);
}

static void handle_routes(pkt_t *pkt)
{
    route_matrix_t *route_matrix;
    uint8_t ver;
    int8_t rc;

    ver = pkt->payload[PKT_ROUTES_VER_OFFSET];

    LOG("got routes: ver ");
    LOGP("%d -> %d\r\n", routes_ver, ver);

    if (ver == routes_ver) {
        LOG("ignored routes pkt: up-to-date\r\n");
        return;
    }

    /* Route distribution completes the discovery procedure. Reset the seq
     * counter so that when origin reboots, pkts with the same seq number are
     * not regarded as pkts from the old procedure. */
    reset_discover_state();

    route_matrix = (route_matrix_t *)&pkt->payload[PKT_ROUTES_TABLE_OFFSET];
    set_routes(route_matrix, ver);

    pkt->dest = BROADCAST_NODE_ID;
    rc = send_pkt(pkt, TX_FLAG_NONE, NULL);
    if (rc != NRK_OK)
        LOG("WARN: failed to broadcast routes\r\n");
}

static void handle_packet(pkt_t *pkt)
{
    int i;

    LOG("HANDLE [");
    LOGF(ENUM_TO_STR(pkt->type, pkt_names));
    LOGA(", ");
    LOGP("%d] ", pkt->seq);
    LOGP("%d -> %d", pkt->src, pkt->dest);
    LOGA("\r\n");

    pkt->hops = pkt->buf[PKT_HDR_HOPS_OFFSET];
    pkt->path_len = pkt->hops <= MAX_PATH_LEN ? pkt->hops : MAX_PATH_LEN;
		  
    for (i = 0; i < pkt->path_len; ++i)
        pkt->path[i] = pkt->buf[PKT_HDR_PATH_OFFSET + i];

    pkt->payload = pkt->buf + PKT_HDR_LEN;
    pkt->payload_len = pkt->len - PKT_HDR_LEN;

    switch (pkt->type) {
        case PKT_TYPE_PING:
            handle_ping(pkt);
            break;
        case PKT_TYPE_PONG:
            handle_pong(pkt);
            break;
        case PKT_TYPE_MSG:
            handle_msg(pkt);
            break;
#if ENABLE_RFTOP
        case PKT_TYPE_DISCOVER_REQUEST:
            handle_discover_request(pkt);
            break;
        case PKT_TYPE_DISCOVER_RESPONSE:
            handle_discover_response(pkt);
            break;
#endif /* ENABLE_RFTOP */
        case PKT_TYPE_ROUTES:
            handle_routes(pkt);
            break;
        default:
            LOG("unknown pkt type");
    }
}

static void reap_tx_queue(nrk_time_t *next_event_time)
{
    uint8_t i;
    tx_msg_pkt_t *msg_pkt;
    nrk_time_t now;

    LOG("reaping tx queue\r\n");

    for (i = 0; i < TX_MSG_QUEUE_SIZE; ++i) {
        msg_pkt = &tx_msg_queue[i];
        if (msg_pkt->state == TX_MSG_STATE_SENT) {

            if (msg_pkt->tx_handle && is_tx_done(msg_pkt->tx_handle)) {
                if (reap_tx(msg_pkt->tx_handle)) {
                    LOG("msg tx succeeded: seq "); LOGP("%d\r\n", msg_pkt->seq);
                    release_tx_msg_pkt(msg_pkt);
                } else {
                    LOG("msg tx failed: seq "); LOGP("%d\r\n", msg_pkt->seq);

                    if (msg_pkt->attempt < MAX_MSG_SEND_ATTEMPTS)
                        set_tx_msg_state(msg_pkt, TX_MSG_STATE_READY);
                    else
                        release_tx_msg_pkt(msg_pkt);

                    if (heal_routes)
                        heal_route(msg_pkt->recipient, msg_pkt->pkt.src);
                }
            } else if (!msg_pkt->tx_handle) { /* rxtx send never enqueued */
                nrk_time_get(&now);

                if (time_cmp(&msg_pkt->next_attempt, &now) < 0) {
                    LOG("msg tx expired: ");
                    LOGA(" seq "); LOGP("%d", msg_pkt->seq); LOGNL();

                    if (msg_pkt->attempt < MAX_MSG_SEND_ATTEMPTS)
                        set_tx_msg_state(msg_pkt, TX_MSG_STATE_READY);
                    else
                        release_tx_msg_pkt(msg_pkt);

                } else { /* not yet due for retry */

                    if (!IS_VALID_TIME(*next_event_time) ||
                        time_cmp(&msg_pkt->next_attempt,
                                 next_event_time) < 0) {
                        *next_event_time = msg_pkt->next_attempt;

                        LOG("updated next event time: ");
                        LOGP("%lu.%lu", next_event_time->secs,
                             next_event_time->nano_secs / NANOS_PER_MS);
                        LOGNL();
                    }
                }
            }
        }
    }
}

static void process_tx_queue(nrk_time_t *next_event_time)
{
    int8_t rc;
    uint8_t i;
    tx_msg_pkt_t *msg_pkt;
    nrk_time_t now;

    LOG("processing tx queue\r\n");

    for (i = 0; i < TX_MSG_QUEUE_SIZE; ++i) {
        msg_pkt = &tx_msg_queue[i];
        if (msg_pkt->state == TX_MSG_STATE_READY) {

            nrk_time_get(&now);

            msg_pkt->attempt++;
            nrk_time_add(&msg_pkt->next_attempt, now, tx_msg_retry_delay);

            LOG("sending msg: ");
            LOGA(" seq "); LOGP("%d", msg_pkt->seq);
            LOGA(" attempt "); LOGP("%d", msg_pkt->attempt);
            LOGA(" next ");
            LOGP("%lu.%lu", msg_pkt->next_attempt.secs,
                 msg_pkt->next_attempt.nano_secs / NANOS_PER_MS);
            LOGNL();

            rc = relay_msg_pkt(msg_pkt);
            if (rc != NRK_OK) {
                LOG("WARN: failed to relay msg pkt: seq ");
                LOGP("%d\r\n", msg_pkt->seq);
            }

            /* reap loop will pick it up and promote to ready for retry */
            set_tx_msg_state(msg_pkt, TX_MSG_STATE_SENT);

            if (!IS_VALID_TIME(*next_event_time) ||
                time_cmp(&msg_pkt->next_attempt,
                         next_event_time) < 0) {
                *next_event_time = msg_pkt->next_attempt;

                LOG("updated next event time: ");
                LOGP("%lu.%lu", next_event_time->secs,
                     next_event_time->nano_secs / NANOS_PER_MS);
                LOGNL();
            }
        }
    }
}

#if ENABLE_RFTOP
static void process_route_discovery()
{
    discover_state_t discover_state;
    route_matrix_t *discovered_routes;
    uint8_t discovered_routes_ver;

    discover_state = get_discover_state();

    switch (discover_state) {
        case DISCOVER_PENDING:
            LOG("discovery pending: starting\r\n");
            discover();
            break;
        case DISCOVER_COMPLETED:
            LOG("discovery completed: setting routes\r\n");
            discovered_routes = get_discovered_routes();
            discovered_routes_ver = get_discover_sequence();
            set_routes(discovered_routes, discovered_routes_ver);
            reset_discover_state();
            break;
        default:
            /* nothing to do */
            break;
    }
}
#endif


static void router_task ()
{
    int8_t rc;
    nrk_sig_mask_t wait_signal_mask;
    nrk_time_t now, next_tx_event_time, sleep_time;

    rc = nrk_signal_register(tx_msg_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: rx\r\n");

    rc = nrk_signal_register(pkt_rcved_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: pkt rcved\r\n");

    rc = nrk_signal_register(tx_done_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: tx done\r\n");

#if ENABLE_RFTOP
    rc = nrk_signal_register(discover_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: discover\r\n");
#endif

    msg_sent_seq = rand(); /* to not confuse pkts after reboot with old ones */
    LOG("msg seq: "); LOGP("%u\r\n", msg_sent_seq);

    wait_signal_mask = SIG(pkt_rcved_signal)
                       | SIG(tx_msg_signal)
                       | SIG(tx_done_signal)
#if ENABLE_RFTOP
                       | SIG(discover_signal)
#endif
                       ;

    while (1) {
        print_tx_queue();

        next_tx_event_time.secs = next_tx_event_time.nano_secs = 0;

        reap_tx_queue(&next_tx_event_time);
        process_tx_queue(&next_tx_event_time);

        tx_msg_event = false;
        while (receive_pkt(&rcv_pkt))
            handle_packet(&rcv_pkt);

#if ENABLE_RFTOP
        process_route_discovery();
#endif

        if (tx_msg_event)
            continue;

        if (IS_VALID_TIME(next_tx_event_time)) {
            nrk_time_get(&now);
            if (nrk_time_sub(&sleep_time, next_tx_event_time, now) == NRK_OK) {

                LOG("sleeping for: ");
                LOGP("%lu ms\r\n", TIME_TO_MS(sleep_time));

                nrk_set_next_wakeup(sleep_time);
                nrk_event_wait( wait_signal_mask | SIG(nrk_wakeup_signal));
            } else {
                LOG("next event in the past: ");
                LOGP("%lu.%lu\r\n", next_tx_event_time.secs,
                     next_tx_event_time.nano_secs / NANOS_PER_MS);
                LOGNL();
            }
        } else {
            LOG("waiting for events\r\n");
            nrk_event_wait( wait_signal_mask );
        }

        LOG("awake\r\n");
    }

    ABORT("router task exited\r\n");
}

int8_t cmd_route(uint8_t argc, char **argv)
{
    node_id_t dest, next_hop;

    if (!(argc == 1 || argc == 2 || argc == 3)) {
        OUT("usage: route [<dest>|* [<next_hop>]]\r\n");
        return NRK_ERROR;
    }

    if (argc == 3 || argc == 2) { /* add or remove routes */
        if (argv[1][0] == '*') {
            for (dest = 1; dest < MAX_NODES; ++dest)
                if (argc == 3) { /* add a route */
                    if ((next_hop = parse_node_id(argv[2])) == INVALID_NODE_ID)
                        return NRK_ERROR;
                    set_route(dest, next_hop);
                } else { /* remove a route */
                    clear_route(dest);
                }
        } else {
            if ((dest = parse_node_id(argv[1])) == INVALID_NODE_ID)
                return NRK_ERROR;
            if (argc == 3) { /* add a route */
                if ((next_hop = parse_node_id(argv[2])) == INVALID_NODE_ID)
                    return NRK_ERROR;
                set_route(dest, next_hop);
            } else { /* remove a route */
                clear_route(dest);
            }
        }
    } else { /* print routes */
        for (dest = 1; dest < MAX_NODES; ++dest) {
            OUTP("\t%u: ", dest);
            if (IS_VALID_NODE_ID(routes[dest]))
                OUTP("%u", routes[dest]);
            OUT("\r\n");
        }
    }
    return NRK_OK;
}

#if ENABLE_RFTOP
int8_t cmd_set_routes(uint8_t argc, char **argv)
{
    route_matrix_t *discovered_routes;
    uint8_t ver;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: set-routes [<ver>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2)
        ver = atoi(argv[1]);
    else
        ver = get_discover_sequence();

    discovered_routes = get_discovered_routes();
    set_routes(discovered_routes, ver);
    return NRK_OK;
}
#endif

int8_t cmd_ping(uint8_t argc, char **argv)
{
    node_id_t dest;
    uint8_t token;

    if (argc != 2 && argc != 3) {
        OUT("usage: ping <dest> [<token>]\r\n");
        return NRK_ERROR;
    }

    dest = atoi(argv[1]);
    token = argc == 3 ? atoi(argv[2]) : 1;

    return do_ping(dest, token);
}

uint8_t init_router(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    /* Listeners reged only by tasks strictly lower prio than the router */
    listeners_sem = nrk_sem_create(1, priority + 1);
    if (listeners_sem == NULL)
        ABORT("create sem: listener\r\n");

    tx_msg_signal = nrk_signal_create();
    if (tx_msg_signal == NRK_ERROR)
        ABORT("create sig: tx msg\r\n");

    rx_msg_signal = nrk_signal_create();
    if (rx_msg_signal == NRK_ERROR)
        ABORT("create sig: rx msg\r\n");

    tx_msg_queue_sem = nrk_sem_create(1, NRK_MAX_TASKS);
    if (tx_msg_queue_sem == NULL)
        ABORT("create sem: tx msg queue\r\n");

    num_tasks++;
    ROUTER_TASK.task = router_task;
    ROUTER_TASK.Ptos = (void *) &router_task_stack[STACKSIZE_ROUTER - 1];
    ROUTER_TASK.Pbos = (void *) &router_task_stack[0];
    ROUTER_TASK.prio = priority;
    ROUTER_TASK.FirstActivation = TRUE;
    ROUTER_TASK.Type = BASIC_TASK;
    ROUTER_TASK.SchType = PREEMPTIVE;
    ROUTER_TASK.period.secs = 1;
    ROUTER_TASK.period.nano_secs = 0;
    ROUTER_TASK.cpu_reserve.secs = 1;
    ROUTER_TASK.cpu_reserve.nano_secs = 500 * NANOS_PER_MS;
    ROUTER_TASK.offset.secs = 0;
    ROUTER_TASK.offset.nano_secs = 0;
    nrk_activate_task (&ROUTER_TASK);

    ASSERT(num_tasks == NUM_TASKS_ROUTER);
    return num_tasks;
}
