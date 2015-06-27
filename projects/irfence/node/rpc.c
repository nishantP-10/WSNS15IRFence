#include <nrk.h>
#include <nrk_error.h>

#include "cfg.h"
#include "output.h"
#include "enum.h"
#include "time.h"
#include "router.h"
#include "ports.h"

#include "rpc.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_RPC

typedef enum {
    MSG_TYPE_INVALID = 0,
    MSG_TYPE_RPC_REQ,
    MSG_TYPE_RPC_REPLY,
    MSG_TYPE_RPC_ERROR,
} msg_type_t;

static const char msg_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [MSG_TYPE_INVALID] = "<invalid>",
    [MSG_TYPE_RPC_REQ] =  "req",
    [MSG_TYPE_RPC_REPLY] = "reply",
    [MSG_TYPE_RPC_ERROR] = "error",
};

#define MSG_RPC_ID_OFFSET      0
#define MSG_RPC_ID_LEN         1
#define MSG_RPC_SEQ_OFFSET     1
#define MSG_RPC_SEQ_LEN        1
#define MSG_RPC_PORT_OFFSET    2
#define MSG_RPC_PORT_LEN       1

#define MSG_RPC_HEADER_LEN (\
    MSG_RPC_ID_LEN + \
    MSG_RPC_SEQ_LEN + \
    MSG_RPC_PORT_LEN)

#define PROC_NAME(id, svr) \
    enum_to_str(id, *svr->proc_names, svr->proc_count)

#define LOG_SVR(server) \
    do { \
        LOG("server "); LOGF(server->name); \
        LOGP(":%d: ", server->listener.port); \
    } while (0);

#define LOG_CLT(client, svr_node, port, id) \
    do { \
        LOG("client "); LOGF(client->name); \
        LOGP(":%d", client->listener.port); \
        LOGA(": req "); LOGP("%d:%d:%d: ", svr_node, port, id); \
    } while (0);

static nrk_sig_t create_signal()
{
    nrk_sig_t signal;

    signal = nrk_signal_create();
    if (signal == NRK_ERROR)
        ABORT("create sig: listener\r\n");
    return signal;
}

static void init_rpc_msg(msg_t *msg, uint8_t reply_port,
                         node_id_t recipient, uint8_t port,
                         uint8_t id, uint8_t seq)
{
    init_message(msg);

    msg->recipient = recipient;
    msg->port = port;
    msg->payload[MSG_RPC_ID_OFFSET] = id;
    msg->len += MSG_RPC_ID_LEN;
    msg->payload[MSG_RPC_SEQ_OFFSET] = seq;
    msg->len += MSG_RPC_SEQ_LEN;
    msg->payload[MSG_RPC_PORT_OFFSET] = reply_port;
    msg->len += MSG_RPC_PORT_LEN;
}

static void init_req_msg(msg_t *msg, uint8_t reply_port,
                         node_id_t recipient, uint8_t port,
                         uint8_t id, uint8_t seq)
{
    init_rpc_msg(msg, reply_port, recipient, port, id, seq);
    msg->type = MSG_TYPE_RPC_REQ;
}

static void init_reply_msg(msg_t *msg, node_id_t recipient, uint8_t port,
                         uint8_t id, uint8_t seq)
{
    init_rpc_msg(msg, 0 /* no reply port */, recipient, port, id, seq);
    /* type set later to either REPLY or ERROR */
}

static bool listen_for_reply(rpc_client_t *client, node_id_t node,
                             uint8_t port, uint8_t id, int8_t *rc,
                             uint8_t *reply_buf, uint8_t *reply_len)
{
    listener_t *listener = &client->listener;
    bool reply_received = false;
    msg_t *reply_msg;
    uint8_t msg_idx;
    uint8_t reply_id, reply_seq;
    uint8_t reply_payload_len;

    *rc = NRK_ERROR;

    while (!reply_received && !queue_empty(&listener->queue)) {
        msg_idx = queue_peek(&listener->queue);
        reply_msg = &listener->queue_data[msg_idx];
        if (reply_msg->type == MSG_TYPE_RPC_REPLY) {
            reply_id = reply_msg->payload[MSG_RPC_ID_OFFSET];
            reply_seq = reply_msg->payload[MSG_RPC_SEQ_OFFSET];
            if (reply_id == id && reply_seq == client->seq) {
                reply_received = true;
                reply_payload_len = reply_msg->len - MSG_RPC_HEADER_LEN;
                if (*reply_len >= reply_payload_len) {
                    LOG_CLT(client, node, port, id);
                    LOGA("reply: len "); LOGP("%u", reply_payload_len);

                    memcpy(reply_buf,
                           reply_msg->payload + MSG_RPC_HEADER_LEN,
                           reply_payload_len);
                    *reply_len = reply_payload_len;
                    *rc = NRK_OK;
                } else {
                    LOG_CLT(client, node, port, id);
                    LOGA("WARN: reply buf too small: ");
                    LOGP("%d/%d\r\n", *reply_len, reply_payload_len);
                }
            } else {
                LOG_CLT(client, node, port, id);
                LOGA("WARN: unexpected:");
                LOGA(" id "); LOGP("%d/%d", reply_id, id);
                LOGA(" seq "); LOGP("%d/%d", reply_seq, client->seq);
                LOGA("\r\n");
            }
        } else if (reply_msg->type == MSG_TYPE_RPC_ERROR) {
            LOG_CLT(client, node, port, id); LOGA("error reply\r\n");
            reply_received = true ;
        } else {
            LOG_CLT(client, node, port, id);
            LOGA("WARN: unexpected msg type\r\n");
        }
        queue_dequeue(&listener->queue);
    }

    return reply_received;
}

static bool wait_for_reply(rpc_client_t *client,
                           node_id_t node, uint8_t port, uint8_t id,
                           nrk_time_t *timeout, nrk_time_t *req_time,
                           nrk_time_t *elapsed)
{
    nrk_time_t now, remaining_time;

    nrk_time_sub(&remaining_time, *timeout, *elapsed);

    LOG_CLT(client, node, port, id);
    LOGA("waiting for reply: ");
    LOGP("%lu ms\r\n", TIME_TO_MS(remaining_time));

    nrk_set_next_wakeup(remaining_time);
    nrk_event_wait( SIG(client->listener.signal) | SIG(nrk_wakeup_signal) );

    nrk_time_get(&now);
    nrk_time_sub(elapsed, now, *req_time);

    LOG_CLT(client, node, port, id);
    LOGA("awake, elapsed: ");
    LOGP("%lu ms\r\n", TIME_TO_MS(*elapsed));

    return time_cmp(elapsed, timeout) < 0;
}

int8_t rpc_call(rpc_client_t *client,
                node_id_t node, uint8_t port, uint8_t id,
                nrk_time_t *timeout,
                uint8_t *req_buf, uint8_t req_len,
                uint8_t *reply_buf, uint8_t *reply_len)
{
    int8_t rc;
    msg_t *req_msg = &client->msg;
    listener_t *listener = &client->listener;
    nrk_time_t req_time, elapsed = {0};
    bool reply_received = false;

    LOG_CLT(client, node, port, id); LOGA("\r\n");

    if (req_len > MAX_MSG_SIZE - MSG_RPC_HEADER_LEN) {
        LOG_CLT(client, node, port, id); LOGA("WARN: req too big: ");
        LOGP("%d/%d\r\n", req_len, MAX_MSG_SIZE - MSG_RPC_HEADER_LEN);
        return NRK_ERROR;
    }

    init_req_msg(req_msg, listener->port, node, port, id, ++(client->seq));

    ASSERT(req_len <= sizeof(req_msg->payload) - MSG_RPC_HEADER_LEN);
    memcpy(req_msg->payload + MSG_RPC_HEADER_LEN, req_buf, req_len);
    req_msg->len += req_len;

    rc = send_message(req_msg);
    if (rc != NRK_OK) {
        LOG("WARN: failed to send rpc req msg\r\n");
        return rc;
    }

    nrk_time_get(&req_time);

    activate_listener(listener);

    do {
        LOG_CLT(client, node, port, id); LOGA("checking for reply\r\n");

        reply_received = listen_for_reply(client, node, port, id,
                                          &rc, reply_buf, reply_len);

    } while (!reply_received && wait_for_reply(client, node, port, id,
                                               timeout, &req_time, &elapsed));

    deactivate_listener(listener);

    if (!reply_received) {
        LOG_CLT(client, node, port, id);
        LOGA("WARN: timed out\r\n");
        rc = NRK_ERROR;
    }

    return rc;
}

void rpc_init_client(rpc_client_t *client)
{
    client->listener.signal = create_signal();
    register_listener(&client->listener);
}

void rpc_init_server(rpc_server_t *server)
{
    LOG_SVR(server); LOGA("server: procs ");
    LOGP("%d\r\n", server->proc_count);

    server->listener.signal = create_signal();
    register_listener(&server->listener);
}

void rpc_activate_server(rpc_server_t *server)
{
    activate_listener(&server->listener);
}

void rpc_init_endpoint(rpc_endpoint_t *endpoint)
{
    nrk_sig_t signal = create_signal();

    /* Client and server share a signal, solely for economy purposes */
    endpoint->server.listener.signal = signal;
    endpoint->client.listener.signal = signal;

    register_listener(&endpoint->server.listener);
    register_listener(&endpoint->client.listener);
}

void rpc_activate_endpoint(rpc_endpoint_t *endpoint)
{
    activate_listener(&endpoint->server.listener);
}

void rpc_server_loop(rpc_server_t *server)
{
    rpc_activate_server(server);

    while (1) {
        rpc_serve(server);
        nrk_event_wait( SIG(server->listener.signal) );
    }
    ABORT("rpc server exited\r\n");
}

void rpc_serve(rpc_server_t *server)
{
    int8_t rc;
    uint8_t msg_idx;
    uint8_t id, seq;
    queue_t *queue = &server->listener.queue;
    listener_t *listener = &server->listener;
    msg_t *req_msg;
    msg_t *reply_msg = &server->reply_msg;
    uint8_t reply_len;
    uint8_t reply_port;

    while (!queue_empty(queue)) {
        msg_idx = queue_peek(queue);
        req_msg = &listener->queue_data[msg_idx];

        id = req_msg->payload[MSG_RPC_ID_OFFSET];
        seq = req_msg->payload[MSG_RPC_SEQ_OFFSET];
        reply_port = req_msg->payload[MSG_RPC_PORT_OFFSET];

        LOG_SVR(server); LOGA("req: ");
        LOGA(" sender "); LOGP("%u", req_msg->sender);
        LOGA(" proc "); LOGP("%u/", id); LOGF(PROC_NAME(id, server));
        LOGA(" seq "); LOGP("%u", seq);
        LOGA(" len "); LOGP("%u", req_msg->len);
        LOGA("\r\n");

        init_reply_msg(reply_msg, req_msg->sender, reply_port, id, seq);

        if (id < server->proc_count) {
            LOG_SVR(server); LOGA("calling proc "); LOGP("%d/", id);
            LOGF(PROC_NAME(id, server)); LOGA("\r\n");

            reply_len = 0;
            rc = server->procedures[id](
                    req_msg->sender,
                    req_msg->payload + MSG_RPC_HEADER_LEN,
                    req_msg->len - MSG_RPC_HEADER_LEN,
                    reply_msg->payload + MSG_RPC_HEADER_LEN,
                    MAX_MSG_SIZE - reply_msg->len,
                    &reply_len);
            if (rc == NRK_OK) {
                reply_msg->len += reply_len;
                reply_msg->type = MSG_TYPE_RPC_REPLY;
            } else {
                reply_msg->type = MSG_TYPE_RPC_ERROR;
            }
            LOG_SVR(server); LOGA("proc rc "); LOGP("%d\r\n", rc);

        } else {
            LOG_SVR(server); LOGA("WARN: invalid proc\r\n");
            reply_msg->type = MSG_TYPE_RPC_ERROR;
        }

        LOG_SVR(server); LOGA("send reply '");
        LOGF(ENUM_TO_STR(reply_msg->type, msg_names)); LOGA("' to ");
        LOGP("%d:%d\r\n", reply_msg->recipient, reply_msg->port);

        rc = send_message(reply_msg);
        if (rc != NRK_OK) {
            LOG_SVR(server); LOGA("WARN: failed to send reply\r\n");
        }

        queue_dequeue(queue);
    }
}

nrk_sig_mask_t rpc_wait_mask(rpc_server_t *server)
{
    return SIG(server->listener.signal);
}
