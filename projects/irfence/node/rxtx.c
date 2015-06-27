#include <nrk.h>
#include <include.h>
#include <nrk_error.h>
#include <stdlib.h>
#include <bmac.h>

#include "cfg.h"
#include "node_id.h"
#include "config.h"
#include "parse.h"
#include "output.h"
#include "enum.h"
#include "led.h"
#include "queue.h"
#include "time.h"
#include "random.h"
#include "packets.h"

#include "rxtx.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_RXTX

/* The real check is (MAX_PATH_LEN <= PKT_HDR_PATH_LEN / sizeof(node_id_t)),
 * but no sizeof in preprocessor conditions. Assume it equals 1. */
#if (MAX_PATH_LEN > PKT_HDR_PATH_LEN)
#error MAX_PATH_LEN is larger than what fits in the path field
#endif

#define IS_REACHEABLE(dest) (!(topology_mask & (1 << (dest))))

typedef enum {
    TX_STATE_NONE = 0,
    TX_STATE_SENT,
    TX_STATE_FAILED,
    TX_STATE_OK,
    TX_STATE_REAPED,
} tx_state_t;

static const char state_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [TX_STATE_NONE] = "<none>",
    [TX_STATE_SENT] = "sent",
    [TX_STATE_FAILED] = "failed",
    [TX_STATE_OK] = "ok",
    [TX_STATE_REAPED] = "reaped",
};

typedef struct {
    pkt_t pkt; /* TODO: make this a pointer */
    uint8_t flags;
    tx_state_t state;
} tx_pkt_t;

typedef pkt_t rx_pkt_t; /* so far, rx pkt does not need extra fields */

static nrk_task_type RX_TASK;
static NRK_STK rx_task_stack[STACKSIZE_RXTX_RX];

static nrk_task_type TX_TASK;
static NRK_STK tx_task_stack[STACKSIZE_RXTX_TX];

static nrk_task_type RCV_TASK;
static NRK_STK rcv_task_stack[STACKSIZE_RXTX_RCV];

static const char neighbors_name[] PROGMEM = "neighbors";
static neighbor_t neighbors_data[MAX_NEIGHBORS];
static node_id_t neighbors_ids[MAX_NEIGHBORS];
static nodelist_t neighbors = {
    .name = neighbors_name,
    .log_cat = LOG_CATEGORY,
    .nodes = neighbors_ids,
    .size = MAX_NEIGHBORS,
};

static uint8_t tx_seq;
static nrk_sem_t *tx_seq_sem;

static bool tx_pkt_acked;
static uint8_t exp_ack_seq;
static node_id_t exp_ack_src;

static nrk_sig_t tx_signal; /* a pkt was enqueued for transmission */
static nrk_sig_t rx_signal; /* a pkt was received and enqueued for handling */
static nrk_sig_t tx_reaped_signal; /* pkt transmission result has been picked up */
static nrk_sig_t ack_signal; /* pkt ack received */

/* Signals part of public interface */
nrk_sig_t pkt_rcved_signal; /* a pkt is ready to be handled by external consumer */
nrk_sig_t tx_done_signal; /* a pkt transmission result is ready for pick up */

static pkt_t ack_pkt;

/* Queue synchronization: protection is necessary when incrementing head and
 * tail when there is more than one incrementer. No protection is necessary for
 * reads of tail and head when (1) byte assignments and reads are atomic, and
 * (2) the queue is accessed only by no more than one producer and one consumer
 * at a time.
 *
 * Current design is to have the only consumer of rxtx be the router. So,
 * there are no concurrent send_pkts, and no concurrent handle_pkts.
 * */

static queue_t tx_queue = { .size = TX_QUEUE_SIZE };
static tx_pkt_t tx_queue_data[TX_QUEUE_SIZE];

static queue_t rx_queue = { .size = RX_QUEUE_SIZE };
static rx_pkt_t rx_queue_data[RX_QUEUE_SIZE];

static queue_t rcv_queue = { .size = RCV_QUEUE_SIZE };
static pkt_t rcv_queue_data[RCV_QUEUE_SIZE];

static neighbor_t *add_neighbor(node_id_t id)
{
    uint8_t idx;
    neighbor_t *neighbor;

    idx = nodelist_add(&neighbors, id);
    neighbor = &neighbors_data[idx];
    memset(neighbor, 0, sizeof(neighbor_t));
    neighbor->id = id;

    return neighbor;
}

static void clear_neighbors()
{
    nodelist_clear(&neighbors);
    memset(&neighbors_data, 0, sizeof(neighbors_data));
}

static void update_neighbor(neighbor_t *neighbor, uint8_t seq, uint8_t rssi)
{
    uint8_t current_rssi;
    uint16_t updated_rssi;

    neighbor->seq = seq;
    nrk_time_get(&neighbor->last_heard);

    /* Average (almost) of last 'count' values */
    current_rssi = neighbor->rssi != 0 ? neighbor->rssi : rssi;
    updated_rssi = current_rssi;
    updated_rssi = (updated_rssi * (rssi_avg_count - 1)) + rssi;
    updated_rssi /= rssi_avg_count;

    neighbor->rssi = updated_rssi;

    LOG("neighbor updated: ");
    LOGP("%u", neighbor->id);
    LOGA(" seq "); LOGP("%u", neighbor->seq);
    LOGA(" rssi ");
    LOGP("%u + %u -> %u\r\n", current_rssi, rssi, neighbor->rssi);
}

nodelist_t *get_neighbors()
{
    return &neighbors;
}

neighbor_t *get_neighbor(node_id_t node_id)
{
    int8_t idx = nodelist_find(&neighbors, node_id);
    return idx >= 0 ? &neighbors_data[idx] : NULL;
}

static int8_t send_buf(uint8_t *buf, uint8_t len)
{
    int8_t rc;

#if ENABLE_LED
    pulse_led(led_sent_pkt);
#endif

    rc = bmac_tx_pkt(buf, len);
    if(rc != NRK_OK) {
        LOG("ERROR: bmac_tx_pkt rc ");
        LOGP("%d\r\n", rc);
    }

    return rc;
}

void init_pkt(pkt_t *pkt)
{
    memset(pkt, 0, sizeof(pkt_t)); /* optional */
    pkt->src = this_node_id; /* note: send_pkt does not use this */
    pkt->payload = pkt->buf + PKT_HDR_LEN;
    pkt->len = PKT_HDR_LEN;
    pkt->hops = 0;
    pkt->type = 0;
}

/* Returns a (weak) handle for queued pkt: a seq number */
int8_t send_pkt(pkt_t *pkt, uint8_t flags, uint8_t *seq)
{
    uint8_t tx_pkt_idx;
    tx_pkt_t *tx_pkt;
    uint8_t handle;

    if (pkt->dest == pkt->src) {
        LOG("invalid pkt dest: "); LOGP("%u\r\n", pkt->dest);
        return NRK_ERROR;
    }

    if (queue_full(&tx_queue)) {
        LOG("send pkt failed: tx queue full\r\n");
        return NRK_ERROR;
    }

    nrk_sem_pend(tx_seq_sem);
    if (++tx_seq == 0) /* not a valid seq number */
        ++tx_seq;
    handle = tx_seq;
    nrk_sem_post(tx_seq_sem);

    if (seq)
        *seq = handle;

    tx_pkt_idx = queue_alloc(&tx_queue);
    tx_pkt = &tx_queue_data[tx_pkt_idx];
    memset(tx_pkt, 0, sizeof(tx_pkt_t));
    memcpy(&tx_pkt->pkt, pkt, sizeof(pkt_t));
    tx_pkt->flags = flags;
    tx_pkt->pkt.seq = handle;

    /* for the router zero means unqueued (for router) */
    ASSERT(tx_pkt->pkt.seq != 0);

    queue_enqueue(&tx_queue);

    LOG("send pkt: enqueued: seq "); LOGP("%d\r\n", handle);
    nrk_event_signal(tx_signal);
    return NRK_OK;
}

bool is_tx_done(uint8_t seq)
{
    uint8_t tx_pkt_idx;
    tx_pkt_t *tx_pkt;

    if (queue_empty(&tx_queue)) {
        LOG("tx req not found: seq "); LOGP("%d\r\n", seq);
        ABORT("tx req not in tx queue\r\n");
    }

    /* Completed tx can only be the one at the head */
    tx_pkt_idx = queue_peek(&tx_queue);
    tx_pkt = &tx_queue_data[tx_pkt_idx];
    if (seq == tx_pkt->pkt.seq)
        return tx_pkt->state == TX_STATE_OK || tx_pkt->state == TX_STATE_FAILED;
    return false;
}

bool reap_tx(uint8_t seq)
{
    uint8_t tx_pkt_idx;
    tx_pkt_t *tx_pkt;
    bool succeeded;

    if (queue_empty(&tx_queue))
        ABORT("failed to reap: tx queue empty\r\n");

    tx_pkt_idx = queue_peek(&tx_queue);
    tx_pkt = &tx_queue_data[tx_pkt_idx];
    if (seq != tx_pkt->pkt.seq) {
        LOG("ERROR: seq mismatch: ");
        LOGP("%d != %d\r\n", seq, tx_pkt->pkt.seq);
        ABORT("failed to reap: seq mismatch\r\n");
    }

    if (!(tx_pkt->state == TX_STATE_OK || tx_pkt->state == TX_STATE_FAILED)) {
        LOG("ERROR: tx not done: state ");
        LOGF(ENUM_TO_STR(tx_pkt->state, state_names));
        LOGA("\r\n");
        ABORT("failed to reap: tx not done\r\n");
    }

    succeeded = tx_pkt->state == TX_STATE_OK;
    tx_pkt->state = TX_STATE_REAPED;

    LOG("reaped tx: seq ");
    LOGP("%d: %c\r\n", seq, succeeded ? 'S' : 'F');

    nrk_event_signal(tx_reaped_signal);
    return succeeded;
}

static int8_t tx_packet(pkt_t *pkt)
{
    uint8_t i;

    if (!IS_REACHEABLE(pkt->dest)) {
        LOG("dropped pkt: dest unreachable by top: ");
        LOGP("%d\r\n", pkt->dest);
        return NRK_OK; /* it's not an error, we're simulating a topology */
    }

    pkt->buf[PKT_HDR_TYPE_OFFSET] = pkt->type;
    pkt->buf[PKT_HDR_SRC_OFFSET] = this_node_id;
    pkt->buf[PKT_HDR_DEST_OFFSET] = pkt->dest;
    pkt->buf[PKT_HDR_SEQ_OFFSET] = pkt->seq;

    if (pkt->hops < MAX_PATH_LEN) /* otherwise: further hops are not recorded */
        pkt->buf[PKT_HDR_PATH_OFFSET + pkt->hops] = this_node_id;

    pkt->buf[PKT_HDR_HOPS_OFFSET] = pkt->hops + 1;

    LOG("PKT TX [");
    LOGF(ENUM_TO_STR(pkt->type, pkt_names));
    LOGA(", ");
    LOGP("%d]: ", pkt->seq);
    LOGP("%d -> %d", this_node_id, pkt->dest);
    CLOGA(LOG_CATEGORY_RXTXDATA, ": ");
    for (i = 0; i < pkt->len; i++)
        CLOGP(LOG_CATEGORY_RXTXDATA, "%02x ", pkt->buf[i]);
    LOGA("\r\n");

    return send_buf(pkt->buf, pkt->len);
}

// Returns whether a received packet was available and was copied
bool receive_pkt(pkt_t *pkt)
{
    uint8_t rcv_pkt_idx;
    pkt_t *rcv_pkt;

    if (queue_empty(&rcv_queue))
        return false;

    rcv_pkt_idx = queue_peek(&rcv_queue);
    rcv_pkt = &rcv_queue_data[rcv_pkt_idx];
    memcpy(pkt, rcv_pkt, sizeof(pkt_t));
    queue_dequeue(&rcv_queue);

    return true;
}

static void process_tx_queue()
{
    uint8_t tx_pkt_idx;
    tx_pkt_t *tx_pkt;
    pkt_t *pkt;
    uint8_t attempt;
    int8_t rc;

    while (!queue_empty(&tx_queue)) {
        tx_pkt_idx = queue_peek(&tx_queue);
        tx_pkt = &tx_queue_data[tx_pkt_idx];
        pkt = &tx_pkt->pkt;
        attempt = 0;

        do {
            LOG("sending pkt:");
            LOGA(" dest "); LOGP("%d", pkt->dest);
            LOGA(" attempt "); LOGP("%d", attempt);
            LOGA("\r\n");

            /* Tell the rcv task what ack to expect */
            tx_pkt_acked = false;
            exp_ack_seq = tx_pkt->pkt.seq;
            exp_ack_src = pkt->dest;

            rc = tx_packet(pkt);
            if (rc != NRK_OK) {
                LOG("tx packet failed\r\n");
                continue;
            }

            if (pkt->dest != BROADCAST_NODE_ID) {
                LOG("waiting for ack: ");
                LOGP("src %d seq %d\r\n", exp_ack_src, exp_ack_seq);

                nrk_set_next_wakeup(pkt_ack_timeout);
                nrk_event_wait(SIG(ack_signal) | SIG(nrk_wakeup_signal));
            } else {
                LOG("bcast packet: marking acked\r\n");
                tx_pkt_acked = true;
            }

        } while (!tx_pkt_acked && ++attempt < MAX_PKT_SEND_ATTEMPTS);

        tx_pkt->state = tx_pkt_acked ? TX_STATE_OK : TX_STATE_FAILED;

        LOG("tx done:");
        LOGA(" seq "); LOGP("%d ", tx_pkt->pkt.seq);
        LOGA(" state "); LOGF(ENUM_TO_STR(tx_pkt->state, state_names));
        LOGA("\r\n");

        if (tx_pkt->flags & TX_FLAG_NOTIFY) {

            LOG("waiting for reap: seq ");
            LOGP("%d\r\n", tx_pkt->pkt.seq);

            nrk_event_signal(tx_done_signal);
            nrk_event_wait(SIG(tx_reaped_signal));

            ASSERT(tx_pkt->state == TX_STATE_REAPED);
            LOG("tx reaped: seq ");
            LOGP("%d\r\n", tx_pkt->pkt.seq);
        }

        LOG("send pkt: dequeued: seq "); LOGP("%d\r\n", tx_pkt->pkt.seq);
        queue_dequeue(&tx_queue);
    }
}

static void process_rx_queue()
{
    int8_t rc;
    uint8_t i;
    uint8_t rx_pkt_idx;
    rx_pkt_t *rx_pkt;
    uint8_t rcv_pkt_idx;
    pkt_t *rcv_pkt;
    uint8_t last_seq;
    neighbor_t *neighbor;
    int8_t neighbor_idx;

    while (!queue_empty(&rx_queue)) {
        rx_pkt_idx = queue_peek(&rx_queue);
        rx_pkt = &rx_queue_data[rx_pkt_idx];

        rx_pkt->type = rx_pkt->buf[PKT_HDR_TYPE_OFFSET];
        rx_pkt->seq = rx_pkt->buf[PKT_HDR_SEQ_OFFSET];

        LOG("PKT RX [");
        LOGF(ENUM_TO_STR(rx_pkt->type, pkt_names));
        LOGA(", ");
        LOGP("%d] ", rx_pkt->seq);
        LOGP("%d -> %d", rx_pkt->src, rx_pkt->dest);
        CLOGA(LOG_CATEGORY_RXTXDATA, ": ");
        for (i = 0; i < rx_pkt->len; i++)
            CLOGP (LOG_CATEGORY_RXTXDATA, "%02x ", rx_pkt->buf[i]);
        LOGA("\r\n");

        if (rx_pkt->type == PKT_TYPE_ACK) {
            if (!tx_pkt_acked &&
                rx_pkt->src == exp_ack_src && rx_pkt->seq == exp_ack_seq) {

                LOG("pkt acked: ");
                LOGA(" src "); LOGP("%u", rx_pkt->src);
                LOGA(" seq "); LOGP("%u", rx_pkt->seq);
                LOGNL();

                tx_pkt_acked = true;
                nrk_event_signal(ack_signal);
            } else {
                LOG("WARN: unexpected ack: ");
                if (tx_pkt_acked) {
                    LOGA(" src "); LOGP("%u/%u ", rx_pkt->src, exp_ack_src);
                    LOGA(" seq "); LOGP("%u/%u ", rx_pkt->seq, exp_ack_seq);
                } else {
                    LOGA("no pending tx");
                }
                LOGA("\r\n");
            }
        } else {
            if (rx_pkt->dest != BROADCAST_NODE_ID) {

                LOG("sending ack: ");
                LOGA(" src "); LOGP("%u", rx_pkt->src);
                LOGA(" seq "); LOGP("%u", rx_pkt->seq);
                LOGNL();

                init_pkt(&ack_pkt);
                ack_pkt.type = PKT_TYPE_ACK;
                ack_pkt.dest = rx_pkt->src;
                ack_pkt.seq = rx_pkt->seq;
                rc = tx_packet(&ack_pkt);
                if (rc != NRK_OK)
                    LOG("WARN: failed to send ack\r\n");
            } else {
                LOG("received bcast pkt: not acking\r\n");
            }

            neighbor_idx = nodelist_find(&neighbors, rx_pkt->src);
            if (neighbor_idx >= 0)
                neighbor = &neighbors_data[neighbor_idx];
            else
                neighbor = add_neighbor(rx_pkt->src);

            last_seq = neighbor->seq;
            update_neighbor(neighbor, rx_pkt->seq, rx_pkt->rssi);

            if (rx_pkt->seq == last_seq) {
                LOG("packet dropped: duplicate seq ");
                LOGP("%u\r\n", rx_pkt->seq);
                queue_dequeue(&rx_queue);
                continue;
            }

            /* TODO: copy and space can be avoided by having rx task alloc buf */
            if (queue_full(&rcv_queue)) {
                LOG("packet dropped: rcv queue full\r\n");
                queue_dequeue(&rx_queue);
                continue;
            }
            rcv_pkt_idx = queue_alloc(&rcv_queue);
            rcv_pkt = &rcv_queue_data[rcv_pkt_idx];

            memcpy(rcv_pkt, rx_pkt, sizeof(pkt_t));
            queue_enqueue(&rcv_queue);
            nrk_event_signal(pkt_rcved_signal);
        }
        queue_dequeue(&rx_queue);
    }
}

static void rx_task ()
{
    int8_t rc;
    uint8_t *local_rx_buf;
    uint8_t pkt_idx;
    pkt_t *pkt;
    uint8_t seq;

    rc = bmac_init(rf_chan);
    if (rc == NRK_ERROR)
        ABORT("bmac_init\r\n");

    rc = bmac_set_rx_check_rate(bmac_rx_check_rate);
    if (rc == NRK_ERROR)
        ABORT("bmac_set_rx_check_rate\r\n");

    rc = bmac_set_rf_power(rf_power);
    if (rc == NRK_ERROR)
        ABORT("bmac_set_rf_power\r\n");

    rc = bmac_set_cca_thresh(chan_clear_thres);
    if (rc == NRK_ERROR)
        ABORT("bmac_set_cca_thres\r\n");

    /* For waiting for free slots in the queue */
    rc = nrk_signal_register(pkt_rcved_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: pkt rcved\r\n");

    while (1) {
        while (queue_full(&rx_queue)) {
            LOG("rx queue full: waiting for slot\r\n");
            nrk_set_next_wakeup(rx_queue_slot_wait);
            nrk_event_wait(SIG(pkt_rcved_signal) | SIG(nrk_wakeup_signal));
        }

        pkt_idx = queue_alloc(&rx_queue);
        pkt = &rx_queue_data[pkt_idx];

        do {
            /* Need to set buffer in this inner loop to 'release' buffer */
            rc = bmac_rx_pkt_set_buffer (pkt->buf, RF_MAX_PAYLOAD_SIZE);
            if (rc == NRK_ERROR)
                ABORT("bmac_rx_pkt_set_buffer\r\n");

            LOG("rx_task: waiting for pkt\r\n");
            rc = bmac_wait_until_rx_pkt ();
            local_rx_buf = bmac_rx_pkt_get (&pkt->len, &pkt->rssi);
            if (!local_rx_buf) {
                LOG("bmac_rx_pkt_get: no packet\r\n");
                continue;
            }

            if (pkt->rssi < rssi_thres) {
                LOG("dropped pkt: below RSSI thres: ");
                LOGP("%d < %d\r\n", pkt->rssi, rssi_thres);
                continue;
            }

            if (pkt->len < PKT_HDR_LEN) {
                LOG("dropped pkt: too short: ");
                LOGP("%d < %d\r\n", pkt->len, PKT_HDR_LEN);
                continue;
            }

            pkt->src = pkt->buf[PKT_HDR_SRC_OFFSET];
            pkt->dest = pkt->buf[PKT_HDR_DEST_OFFSET];
            pkt->type = pkt->buf[PKT_HDR_TYPE_OFFSET];

            if (!IS_REACHEABLE(pkt->src)) {
                LOG("dropped pkt: src unreachable by top: ");
                LOGP("%d\r\n", pkt->src);
                continue;
            }

            if (pkt->dest != this_node_id && pkt->dest != BROADCAST_NODE_ID){
                LOG("dropped pkt: not for me: ");
                LOGP("%d\r\n", pkt->dest);
                continue;
            }

            break;
        } while (true); /* loop until a valid pkt is received */

#if ENABLE_LED
        pulse_led(led_received_pkt);
#endif

        /* For debugging log purposes only */
        seq = pkt->buf[PKT_HDR_SEQ_OFFSET];

        LOG("RX [");
        LOGF(ENUM_TO_STR(pkt->type, pkt_names));
        LOGA(", ");
        LOGP("%d] ", seq);
        LOGP("%d -> %d", pkt->src, pkt->dest);
        LOGA("\r\n");

        queue_enqueue(&rx_queue);
        nrk_event_signal(rx_signal);
    }
    ABORT("rx task exited\r\n");
}

static void rcv_task()
{
    int8_t rc;

    rc = nrk_signal_register(rx_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: rx\r\n");

    while (1) {
        process_rx_queue();

        LOG("rcv task waiting\r\n");
        nrk_event_wait( SIG(rx_signal) );
        LOG("rcv task awake\r\n");
    }
    ABORT("rcv task exited\r\n");
}

static void tx_task()
{
    int8_t rc;

    seed_rand(); /* needs to be in a task, and this task needs it */
    tx_seq = rand(); /* so that pkts after restart don't look the same */
    LOG("tx seq: "); LOGP("%u\r\n", tx_seq);

    rc = nrk_signal_register(tx_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: tx\r\n");

    rc = nrk_signal_register(tx_reaped_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: tx reaped\r\n");

    rc = nrk_signal_register(ack_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: pkt acked\r\n");

    while (1) {
        process_tx_queue();

        LOG("tx task waiting\r\n");
        nrk_event_wait( SIG(tx_signal) );
        LOG("tx task awake\r\n");
    }
    ABORT("tx task exited\r\n");
}

int8_t cmd_top(uint8_t argc, char **argv)
{
    uint8_t i;
    char op;

    if (!(argc == 1 || argc >= 2)) {
        OUT("usage: top [+|- [<dest>...]]\r\n");
        return NRK_ERROR;
    }

    if (argc == 1) { /* no arg means print */
        for (i = 0; i < MAX_NODES; ++i)
            if (IS_REACHEABLE(i))
                OUTP("%u ", i);
        OUT("\r\n");
    } else if (argc >= 2) { /* add/remove */
        op = argv[1][0];
        switch (op) {
            case '+':
                if (argc == 2) {
                    topology_mask = 0x0;
                } else {
                    for (i = 2; i < argc; ++i)
                        topology_mask &= ~(1 << atoi(argv[i]));
                }
                break;
            case '-':
                if (argc == 2) {
                    topology_mask = ~0x0;
                } else {
                    for (i = 2; i < argc; ++i)
                        topology_mask |= 1 << atoi(argv[i]);
                }
            default:
                return NRK_ERROR;
        }
    }
    return NRK_OK;
}

int8_t cmd_hood(uint8_t argc, char **argv)
{
    uint8_t i;
    neighbor_t *neighbor;
    uint32_t since_ms;
    nrk_time_t since;
    nrk_time_t now;
    node_id_t node = INVALID_NODE_ID;

    if (!(argc == 1 || argc == 2 || argc == 3)) {
        OUT("usage: hood [+|- [<node>]]\r\n");
        return NRK_ERROR;
    }

    if (argc == 3 || argc == 2) { /* add or remove neighbor */
        if (argc == 3)
            if ((node = parse_node_id(argv[2])) == INVALID_NODE_ID)
                return NRK_ERROR;
        switch (argv[1][0]) {
            case '+':
                if (node != INVALID_NODE_ID) {
                    add_neighbor(node);
                } else {
                    OUT("ERROR: no node id\r\n");
                    return NRK_ERROR;
                }
                break;
            case '-':
                if (node != INVALID_NODE_ID) {
                    OUT("ERROR: not supported\r\n");
                    return NRK_ERROR;
                } else {
                    clear_neighbors();
                }
                break;
        }
    } else { /* show neighbors */
        nrk_time_get(&now);

        for (i = 0; i < neighbors.len; ++i) {
            neighbor = &neighbors_data[i];
            if (time_cmp(&now, &neighbor->last_heard) > 0)
                nrk_time_sub(&since, now, neighbor->last_heard);
            else /* time wrapped around, give up on reporting */
                since.secs = since.nano_secs = 0;
            since_ms = since.secs * MS_IN_S + since.nano_secs / NS_IN_MS;
            OUTP("\t%u: r\t%u\t", neighbor->id, neighbor->rssi);
            OUTP("l\t%lu:%lu (%lu ms)\r\n",
                 neighbor->last_heard.secs, neighbor->last_heard.nano_secs,
                 since_ms);
        }
    }
    return NRK_OK;
}

uint8_t init_rxtx(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    rx_signal = nrk_signal_create();
    if (rx_signal == NRK_ERROR)
        ABORT("create sig: rx\r\n");

    tx_signal = nrk_signal_create();
    if (tx_signal == NRK_ERROR)
        ABORT("create sig: tx\r\n");

    pkt_rcved_signal = nrk_signal_create();
    if (pkt_rcved_signal == NRK_ERROR)
        ABORT("create sig: pkt rcved\r\n");

    tx_done_signal = nrk_signal_create();
    if (tx_done_signal == NRK_ERROR)
        ABORT("create sig: tx done\r\n");

    tx_reaped_signal = nrk_signal_create();
    if (tx_reaped_signal == NRK_ERROR)
        ABORT("create sig: tx reaped\r\n");

    ack_signal = nrk_signal_create();
    if (ack_signal == NRK_ERROR)
        ABORT("create sig: pkt acked\r\n");

    tx_seq_sem = nrk_sem_create(1, NRK_MAX_TASKS);
    if (tx_seq_sem == NULL)
        ABORT("create sem: tx seq\r\n");

    num_tasks++;
    RCV_TASK.task = rcv_task;
    RCV_TASK.Ptos = (void *) &rcv_task_stack[STACKSIZE_RXTX_RCV - 1];
    RCV_TASK.Pbos = (void *) &rcv_task_stack[0];
    RCV_TASK.prio = priority++;
    RCV_TASK.FirstActivation = TRUE;
    RCV_TASK.Type = BASIC_TASK;
    RCV_TASK.SchType = PREEMPTIVE;
    RCV_TASK.period.secs = 1;
    RCV_TASK.period.nano_secs = 0;
    RCV_TASK.cpu_reserve.secs = 1;
    RCV_TASK.cpu_reserve.nano_secs = 500 * NANOS_PER_MS;
    RCV_TASK.offset.secs = 0;
    RCV_TASK.offset.nano_secs = 0;
    nrk_activate_task (&RCV_TASK);

    num_tasks++;
    TX_TASK.task = tx_task;
    TX_TASK.Ptos = (void *) &tx_task_stack[STACKSIZE_RXTX_TX - 1];
    TX_TASK.Pbos = (void *) &tx_task_stack[0];
    TX_TASK.prio = priority++;
    TX_TASK.FirstActivation = TRUE;
    TX_TASK.Type = BASIC_TASK;
    TX_TASK.SchType = PREEMPTIVE;
    TX_TASK.period.secs = 0;
    TX_TASK.period.nano_secs = 500 * NANOS_PER_MS;
    TX_TASK.cpu_reserve.secs = 1;
    TX_TASK.cpu_reserve.nano_secs = 500 * NANOS_PER_MS;
    TX_TASK.offset.secs = 0;
    TX_TASK.offset.nano_secs = 0;
    nrk_activate_task (&TX_TASK);

    num_tasks++;
    RX_TASK.task = rx_task;
    RX_TASK.Ptos = (void *) &rx_task_stack[STACKSIZE_RXTX_RX - 1];
    RX_TASK.Pbos = (void *) &rx_task_stack[0];
    RX_TASK.prio = priority++;
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

    ASSERT(num_tasks == NUM_TASKS_RXTX);
    return num_tasks;
}
