#ifndef PACKETS_H
#define PACKETS_H

#include "enum.h"

/* Router layer pkt types */
typedef enum {
    PKT_TYPE_INVALID = 0,
    PKT_TYPE_ACK,
    PKT_TYPE_PING,
    PKT_TYPE_PONG,
    PKT_TYPE_DISCOVER_REQUEST,
    PKT_TYPE_DISCOVER_RESPONSE,
    PKT_TYPE_MSG,
    PKT_TYPE_ROUTES,
    NUM_PKT_TYPES,
} pkt_type_t;

extern const char pkt_names[NUM_PKT_TYPES][MAX_ENUM_NAME_LEN];

#endif // PACKETS_H
