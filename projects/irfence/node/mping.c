#include <nrk.h>
#include <nrk_error.h>
#include <include.h>
#include <ulib.h>
#include <stdio.h>
#include <stdlib.h>

#include "cfg.h"
#include "ports.h"
#include "config.h"
#include "output.h"
#include "enum.h"
#include "led.h"
#include "router.h"
#include "time.h"
#include "rxtx.h"

#include "mping.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_PING

/* Application layer messages ('messages' mean routed pkt types) */

/* Ping message */
#define MSG_PING_TOKEN_OFFSET 0
#define MSG_PING_TOKEN_LEN    1

/* Pong message */
#define MSG_PONG_TOKEN_OFFSET 0
#define MSG_PONG_TOKEN_LEN    1
#define MSG_PONG_RSSI_OFFSET  1
#define MSG_PONG_RSSI_LEN     2

/* Application layer message types ('messages' mean routed packets) */
typedef enum {
    MSG_TYPE_INVALID = 0,
    MSG_TYPE_PING,
    MSG_TYPE_PONG,
} msg_type_t;

static const char msg_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [MSG_TYPE_INVALID] = "<invalid>",
    [MSG_TYPE_PING] = "ping",
    [MSG_TYPE_PONG] = "pong",
};

static const char mping_name[] PROGMEM = "mping";

static node_id_t periodic_recipient;
static uint8_t periodic_token = 0;
static nrk_time_t last_ping;
static uint8_t exp_token = 0;
static nrk_sig_t ping_signal;

static msg_t pong_msg;
static msg_t ping_msg;

static msg_t ping_listener_queue[PING_LISTENER_QUEUE_SIZE];

static listener_t ping_listener = {
    .port = PORT_PING,
    .queue = { .size = PING_LISTENER_QUEUE_SIZE },
    .queue_data = ping_listener_queue,
};

static void finish_ping()
{
    exp_token = 0;
    nrk_led_clr(led_awaiting_pong);
}

static bool ping_in_progress()
{
    return exp_token != 0;
}

static bool ping_period_elapsed(nrk_time_t *next_event)
{
    nrk_time_t now, elapsed;

    nrk_time_get(&now);
    nrk_time_sub(&elapsed, now, last_ping);
    if (time_cmp(&elapsed, &ping_period) >= 0) {
        return true;
    } else {
        nrk_time_add(next_event, last_ping, ping_period);
        return false;
    }
}

static int8_t do_ping(node_id_t recipient, uint8_t ping_token)
{
    int8_t rc;

    if (ping_in_progress()) {
        LOG("WARN: ping in progress\r\n");
        return NRK_ERROR;
    }

    OUT("sending ping with token: ");
    OUTP("%d\r\n", ping_token);

    exp_token = ping_token;
    nrk_led_set(led_awaiting_pong);

    init_message(&ping_msg);
    ping_msg.recipient = recipient;
    ping_msg.port = PORT_PING;
    ping_msg.type = MSG_TYPE_PING;
    ping_msg.payload[MSG_PING_TOKEN_OFFSET] = ping_token;
    ping_msg.len += MSG_PING_TOKEN_LEN;
    rc = send_message(&ping_msg);

    if (rc != NRK_OK) {
        LOG("WARN: ping send failed\r\n");
        finish_ping();
        return rc;
    }

    nrk_time_get(&last_ping);

    /* Kick the server so that it can schedule a timeout wakeup */
    nrk_event_signal(ping_signal);

    return NRK_OK;
}

static void handle_ping(msg_t *msg)
{
    int8_t rc;
    uint8_t token;
    uint8_t rssi = 0;
    neighbor_t *neighbor;

    nrk_led_set(led_proc_ping);
    nrk_wait(pong_delay);
    nrk_led_clr(led_proc_ping);

    neighbor = get_neighbor(msg->sender);
    if (neighbor)  /* only available if one-hop */
        rssi = neighbor->rssi;

    token = msg->payload[MSG_PONG_TOKEN_OFFSET];
    LOG("ping with token, sending pong: token ");
    LOGP("%d", token);
    LOGA(" rssi ");
    LOGP("%d\r\n", rssi);

    init_message(&pong_msg);
    pong_msg.recipient = msg->sender;
    pong_msg.type = MSG_TYPE_PONG;
    pong_msg.port = PORT_PING;
    pong_msg.payload[MSG_PONG_TOKEN_OFFSET] = token;
    pong_msg.len += MSG_PONG_TOKEN_LEN;
    pong_msg.payload[MSG_PONG_RSSI_OFFSET] = rssi;
    pong_msg.len += MSG_PONG_RSSI_LEN;
    rc = send_message(&pong_msg);
    if (rc != NRK_OK)
        LOG("WARN: failed to send pong msg\r\n");
}

static void handle_pong(msg_t *msg)
{
    uint8_t token;
    uint8_t ping_rssi, pong_rssi = 0;
    neighbor_t *neighbor;

    token = msg->payload[MSG_PONG_TOKEN_OFFSET];

    if (token != exp_token) {
        LOG("WARN: pong with unexpected token: ");
        LOGP("%u/%u\r\n", token, exp_token);
        return;
    }

    ping_rssi = msg->payload[MSG_PONG_RSSI_OFFSET];

    neighbor = get_neighbor(msg->sender);
    if (neighbor)  /* only available if one-hop */
        pong_rssi = neighbor->rssi;

    OUT("rcved pong from ");
    OUTP("%d", msg->sender);
    OUT(" token ");
    OUTP("%d", token);
    OUT(" rssi ping/pong ");
    OUTP("%d/%d\r\n", ping_rssi, pong_rssi);

    finish_ping();
}

static void handle_msg(msg_t *msg)
{
    LOG("received msg: from ");
    LOGP("%d", msg->sender);
    LOGA(" type ");
    LOGF(ENUM_TO_STR(msg->type, msg_names));
    LOGA("\r\n");

    switch (msg->type) {
        case MSG_TYPE_PING:
            handle_ping(msg);
            break;
        case MSG_TYPE_PONG:
            handle_pong(msg);
            break;
        default:
            LOG("WARN: unexpected msg\r\n");
    }
}

void mping_init_server()
{
    int8_t rc;

    register_listener(&ping_listener);
    activate_listener(&ping_listener);

    rc = nrk_signal_register(ping_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: listener\r\n");
}

nrk_sig_mask_t mping_serve(nrk_time_t *next_event)
{
    uint8_t msg_idx;
    queue_t *ping_queue = &ping_listener.queue;

    while (!queue_empty(ping_queue)) {
        msg_idx = queue_peek(ping_queue);
        handle_msg(&ping_listener.queue_data[msg_idx]);
        queue_dequeue(ping_queue);
    }

    /* This is the only place where timed out pings are cleaned up */

    if (ping_in_progress()) {
       if (ping_period_elapsed(next_event)) {
           LOG("WARN: ping timed out\r\n");
           finish_ping();
       }
    }

    return SIG(ping_listener.signal) | SIG(ping_signal);
}

static void periodic_mping_init()
{
}

static int8_t periodic_mping_config(uint8_t argc, char **argv)
{
    if (argc != 1) {
        OUT("usage: mping <rcp>\r\n");
        return NRK_ERROR;
    }

    periodic_recipient = atoi(argv[0]);

    return NRK_OK;
}


static void periodic_mping_process(bool enabled,
        nrk_time_t *next_event, nrk_sig_mask_t *wait_mask)
{
    int8_t rc;
    nrk_time_t now;

    /* We are not necessarily invoked periodically */
    if (ping_period_elapsed(next_event)) {

        /* Only generate a new ping if none is already in progress,
         * the ones that timeout will be cleaned up by the server
         * mping_serve task, not this periodic function task. */

        if (!ping_in_progress() && enabled) {

            if (++periodic_token == 0) /* zero is not a valid */
                periodic_token++;

            rc = do_ping(periodic_recipient, periodic_token);
            if (rc != NRK_OK)
                LOG("WARN: ping failed\r\n");

            /* When period has elapsed, next event was not set by
             * ping_period_elapsed, and it cannot because last_ping
             * was not yet set, (set in do_ping). */
            nrk_time_add(next_event, last_ping, ping_period);
            LOG("set next event after do ping\r\n");

        } else { /* if there is a ping, we ask for a wakeup below */

         /* If there is a ping in progress, we want to keep waking
          * up until it times out and is cleaned up by the server. */
            nrk_time_get(&now);
            nrk_time_add(next_event, now, ping_period);
            LOG("set next event while ping in prog\r\n");
        }
    }

    LOG("periodic: next event: ");
    LOGP("%lu.%lu\r\n", next_event->secs, next_event->nano_secs);
}

int8_t cmd_mping(uint8_t argc, char **argv)
{
    node_id_t recipient;
    uint8_t token;
    int8_t rc;

    if (argc != 1 && argc != 2 && argc != 3) {
        OUT("usage: mping [<recipient> [<token>]]\r\n");
        return NRK_ERROR;
    }

    if (argc == 1) { /* give up on ping in progress */
        finish_ping();
        rc = NRK_OK;
    } else {
        recipient = atoi(argv[1]);
        token = argc == 3 ? atoi(argv[2]) : 1; /* zero is not a valid token */

        rc = do_ping(recipient, token);
    }
    return rc;
}

uint8_t init_mping(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    ping_listener.signal = nrk_signal_create();
    if (ping_listener.signal == NRK_ERROR)
        ABORT("create sig: listener\r\n");

    ping_signal = nrk_signal_create();
    if (ping_signal == NRK_ERROR)
        ABORT("create sig: ping\r\n");

    ASSERT(num_tasks == NUM_TASKS_MPING);
    return num_tasks;
}

periodic_func_t func_mping = {
    .name = mping_name,
    .init = periodic_mping_init,
    .proc = periodic_mping_process,
    .config = periodic_mping_config,
};
