#ifndef RXTX_H
#define RXTX_H

#include <bmac.h>

#include "node_id.h"
#include "pkt.h"
#include "nodelist.h"

/* Packet header fields (in bytes) */
#define PKT_HDR_TYPE_OFFSET 0
#define PKT_HDR_TYPE_LEN    1
#define PKT_HDR_SRC_OFFSET  1
#define PKT_HDR_SRC_LEN     1
#define PKT_HDR_DEST_OFFSET 2
#define PKT_HDR_DEST_LEN    1
#define PKT_HDR_SEQ_OFFSET  3
#define PKT_HDR_SEQ_LEN     1
#define PKT_HDR_HOPS_OFFSET 4
#define PKT_HDR_HOPS_LEN    1
#define PKT_HDR_PATH_OFFSET 5
#define PKT_HDR_PATH_LEN    7

#define PKT_HDR_LEN ( \
    PKT_HDR_TYPE_LEN + \
    PKT_HDR_SRC_LEN + \
    PKT_HDR_DEST_LEN + \
    PKT_HDR_SEQ_LEN + \
    PKT_HDR_HOPS_LEN + \
    PKT_HDR_PATH_LEN )

typedef enum {
    TX_FLAG_NONE = 0,
    TX_FLAG_NOTIFY = 1 << 0,
} tx_flags_t;

typedef struct {
    node_id_t id;
    nrk_time_t last_heard;
    uint8_t rssi;
    uint8_t seq; /* seq num of last received pkt */
} neighbor_t;

extern nrk_sig_t pkt_rcved_signal; /* a pkt is ready to be handled by external consumer */
extern nrk_sig_t tx_done_signal; /* a pkt transmission result is ready for pick up */

uint8_t init_rxtx(uint8_t priority);
void init_pkt(pkt_t *pkt);
int8_t send_pkt(pkt_t *pkt, uint8_t flags, uint8_t *seq);
bool is_tx_done(uint8_t seq);
bool reap_tx(uint8_t seq);
bool receive_pkt(pkt_t *pkt);

nodelist_t *get_neighbors();
neighbor_t *get_neighbor(node_id_t node_id);

int8_t cmd_top(uint8_t argc, char **argv);
int8_t cmd_hood(uint8_t argc, char **argv);

#endif // RXTX_H
