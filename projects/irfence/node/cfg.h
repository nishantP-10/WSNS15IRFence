#ifndef CFG_H
#define CFG_H

// #define VERBOSE
// #define NO_TERM /* plain 'cat' */
#define LOG_TIMESTAMP

#define ENABLE_LED          1
#define ENABLE_COMMAND      1
#define ENABLE_CONSOLE      1
#define ENABLE_PERIODIC     1
#define ENABLE_BLINKER      1
#define ENABLE_RXTX         1
#define ENABLE_ROUTER       1
#define ENABLE_RFTOP        1
#define ENABLE_RPC          1
#define ENABLE_MPING        1
#define ENABLE_RPING        1
#define ENABLE_RCMD         1
#define ENABLE_TWI          1
#define ENABLE_COMPASS      1
#define ENABLE_COMPASS_RPC  1
#define ENABLE_IR           1
#define ENABLE_IRTOP        1
#define ENABLE_BEAM         1
#define ENABLE_FENCE        1
#define ENABLE_AUTOFENCE    1
#define ENABLE_LOCALIZATION 1
#define ENABLE_POSITION     1
#define ENABLE_TIME_CONV    0

/* Task counts for modules for compile-time info */
#define NUM_TASKS_LED 1
#define NUM_TASKS_COMMAND 0
#define NUM_TASKS_CONSOLE 1
#define NUM_TASKS_PERIODIC 1
#define NUM_TASKS_RXTX 3
#define NUM_TASKS_ROUTER 1
#define NUM_TASKS_RFTOP 1
#define NUM_TASKS_MPING 0
#define NUM_TASKS_RPING 0
#define NUM_TASKS_RPC 0
#define NUM_TASKS_RCMD 0
#define NUM_TASKS_TWI 0
#define NUM_TASKS_COMPASS 0
#define NUM_TASKS_IR 1
#define NUM_TASKS_IRTOP 2
#define NUM_TASKS_BEAM 1
#define NUM_TASKS_FENCE 1


/* NUM_TASKS = NUM_TASKS & (ENABLED_module ? 0x0 : 0xF) */
/* Care only about 4 bits: max 16 tasks per module */
#define NUM_TASKS(module) (\
    (NUM_TASKS_ ## module & (ENABLE_ ## module << 0) ) | \
    (NUM_TASKS_ ## module & (ENABLE_ ## module << 1) ) | \
    (NUM_TASKS_ ## module & (ENABLE_ ## module << 2) ) | \
    (NUM_TASKS_ ## module & (ENABLE_ ## module << 3) ))

#define TASK_COUNT (0 \
    + NUM_TASKS(LED) \
    + NUM_TASKS(COMMAND) \
    + NUM_TASKS(CONSOLE) \
    + NUM_TASKS(PERIODIC) \
    + NUM_TASKS(RXTX) \
    + NUM_TASKS(ROUTER) \
    + NUM_TASKS(RFTOP) \
    + NUM_TASKS(MPING) \
    + NUM_TASKS(RPING) \
    + NUM_TASKS(RPC) \
    + NUM_TASKS(RCMD) \
    + NUM_TASKS(TWI) \
    + NUM_TASKS(COMPASS) \
    + NUM_TASKS(IR) \
    + NUM_TASKS(IRTOP) \
    + NUM_TASKS(BEAM) \
    + NUM_TASKS(FENCE) \
)

#define BROADCAST_NODE_ID 0xff
#define INVALID_NODE_ID 0

#define NUM_LEDS 4
#define NUM_IR_LEDS 8
#define NUM_IR_RECEIVERS 4

#define NUM_ADC_CHANS 7
#define RANDOM_SEED_ADC_READS 32

#define MAX_NODES 6
#define MAX_NEIGHBORS MAX_NODES
#define MAX_PEERS MAX_NODES
#define MAX_PATH_LEN 6

#define MAX_ENUM_NAME_LEN 24

/* LEDs which are too low-level to be configurable as options */
#define LED_ABORTED RED_LED
#define LED_WARN    RED_LED
#define LED_DEFAULT_OPTIONS ORANGE_LED

#define STACKSIZE_DEFAULT 256

#define STACKSIZE_MAIN STACKSIZE_DEFAULT
#define STACKSIZE_LED STACKSIZE_DEFAULT
#define STACKSIZE_CONSOLE STACKSIZE_DEFAULT
#define STACKSIZE_PERIODIC STACKSIZE_DEFAULT
#define STACKSIZE_RXTX_RX STACKSIZE_DEFAULT
#define STACKSIZE_RXTX_TX STACKSIZE_DEFAULT
#define STACKSIZE_RXTX_RCV STACKSIZE_DEFAULT
#define STACKSIZE_ROUTER STACKSIZE_DEFAULT
#define STACKSIZE_DISCOVER STACKSIZE_DEFAULT
#define STACKSIZE_IR STACKSIZE_DEFAULT
#define STACKSIZE_BEAM STACKSIZE_DEFAULT
#define STACKSIZE_FENCE STACKSIZE_DEFAULT
#define STACKSIZE_IRTOP STACKSIZE_DEFAULT
#define STACKSIZE_IRTOP_PROBE STACKSIZE_DEFAULT

#define TWI_MSG_BUF_SIZE 8

#define TX_QUEUE_SIZE 6
#define RX_QUEUE_SIZE 6
#define RCV_QUEUE_SIZE 4
#define TX_MSG_QUEUE_SIZE 4
#define RX_MSG_QUEUE_SIZE 4

#define MAX_MSG_SIZE 64
#define MAX_LISTENERS 18 /* TODO: count listeners */

#define PKT_POOL_SIZE 8

#define MAX_MSG_SEND_ATTEMPTS 3
#define MAX_PKT_SEND_ATTEMPTS 3

#define MAX_CMD_LEN 64
#define MAX_ARGS 16

#define PING_LISTENER_QUEUE_SIZE 2
#define BEAM_LISTENER_QUEUE_SIZE 2
#define FENCE_LISTENER_QUEUE_SIZE 2
#define RCMD_LISTENER_QUEUE_SIZE 2

/* common accross all rpc clients */
#define RPC_CLIENT_QUEUE_SIZE 2

#define PING_RPC_SERVER_QUEUE_SIZE 2
#define BEAM_RPC_SERVER_QUEUE_SIZE 2
#define FENCE_RPC_SERVER_QUEUE_SIZE 2
#define RCMD_RPC_SERVER_QUEUE_SIZE 2
#define IRTOP_RPC_SERVER_QUEUE_SIZE 2
#define IRTOP_QUEUE_SIZE 2
#define COMPASS_RPC_SERVER_QUEUE_SIZE 2

/* Period of discover state machine transitions. This is not
 * the period at which the discovery sequence is triggered: see
 * 'discover_period_s' option. */
#define DISCOVER_TASK_PERIOD_S 5

#endif
