#ifndef PKT_H
#define PKT_H

#include "cfg.h"
#include "packets.h"

typedef struct {
    uint8_t buf[RF_MAX_PAYLOAD_SIZE];
    uint8_t len;

    int8_t rssi;

    /* Header */
    pkt_type_t type;
    node_id_t src;
    node_id_t dest;
    uint8_t seq;
    uint8_t hops;
    node_id_t path[MAX_PATH_LEN];
    uint8_t path_len;

    /* Points into buf (after header) */
    uint8_t *payload;
    uint8_t payload_len;
} pkt_t;

#endif // PKT_H
