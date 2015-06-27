#ifndef CFG_H
#define CFG_H

// #define VERBOSE

/* Mode of operation */
// #define MODE_WRITE_CONFIG
// #define MODE_PING_PKT
//#define MODE_PING_MSG
#define DISPLAY_NETWORK
#define ENABLE_REQUEST_TASK
//#define ENABLE_PING_TASK

/* Enable to use hardcoded routes and simulated topology */
// #define TEST_ROUTES
// #define TEST_TOPOLOGY

#define PING_SENDER        3
#define PING_RECIPIENT     1

#define BROADCAST_NODE_ID 0xff
#define GATEWAY_NODE_ID 1
#define INVALID_NODE_ID 0

#define MAX_NODES 7
#define MAX_NEIGHBORS MAX_NODES


#define CHANNEL_CLEAR_THRESHOLD_DB -45

#define LED_ABORTED RED_LED
#define LED_PING_SENT BLUE_LED
#define LED_PKT_SEND GREEN_LED
#define LED_HOLDING_POTATO ORANGE_LED
#define LED_PROCESSING_MSG ORANGE_LED

#define RX_QUEUE_SIZE 4
#define TX_MSG_QUEUE_SIZE 4
#define MSG_ACK_QUEUE_SIZE 4

#define MAX_ROUTE_TRIES 1

#endif
