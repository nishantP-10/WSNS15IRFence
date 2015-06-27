#include <nrk.h>

#include "packets.h"

const char pkt_names[NUM_PKT_TYPES][MAX_ENUM_NAME_LEN] PROGMEM = {
    [PKT_TYPE_INVALID] = "<invalid>",
    [PKT_TYPE_ACK] = "ack",
    [PKT_TYPE_PING] = "ping",
    [PKT_TYPE_PONG] = "pong",
    [PKT_TYPE_DISCOVER_REQUEST] = "discover-req",
    [PKT_TYPE_DISCOVER_RESPONSE] = "discover-resp",
    [PKT_TYPE_MSG] = "msg",
    [PKT_TYPE_ROUTES] = "routes",
};
