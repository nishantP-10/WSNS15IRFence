#include <nrk.h>
#include <nrk_error.h>
#include <include.h>
#include <stdlib.h>

#include "cfg.h"
#include "node_id.h"
#include "output.h"
#include "enum.h"
#include "config.h"
#include "router.h"
#include "beam.h"
#include "ports.h"
#include "rpc.h"
#if ENABLE_AUTOFENCE
#include "autofence.h"
#endif

#include "fence.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_FENCE

#define RPC_CREATE_SECTION_REQ_OUT_NODE_OFFSET  0
#define RPC_CREATE_SECTION_REQ_OUT_NODE_LEN     1

#define RPC_CREATE_SECTION_REPLY_STATUS_OFFSET  0
#define RPC_CREATE_SECTION_REPLY_STATUS_LEN     1

#define RPC_CREATE_SECTION_REQ_LEN RPC_CREATE_SECTION_REQ_OUT_NODE_LEN
#define RPC_CREATE_SECTION_REPLY_LEN RPC_CREATE_SECTION_REPLY_STATUS_LEN


#define RPC_DESTROY_SECTION_REQ_OUT_NODE_OFFSET  0
#define RPC_DESTROY_SECTION_REQ_OUT_NODE_LEN     1

#define RPC_DESTROY_SECTION_REPLY_STATUS_OFFSET  0
#define RPC_DESTROY_SECTION_REPLY_STATUS_LEN     1

#define RPC_DESTROY_SECTION_REQ_LEN RPC_DESTROY_SECTION_REQ_OUT_NODE_LEN
#define RPC_DESTROY_SECTION_REPLY_LEN RPC_DESTROY_SECTION_REPLY_STATUS_LEN

#define RPC_REQ_CREATE_BEAM_REQ_LEN 0
#define RPC_REQ_CREATE_BEAM_REPLY_LEN 0

#define RPC_REQ_DESTROY_BEAM_REQ_LEN 0
#define RPC_REQ_DESTROY_BEAM_REPLY_LEN 0

#define RPC_BREACH_REQ_IN_NODE_OFFSET  0
#define RPC_BREACH_REQ_IN_NODE_LEN     1
#define RPC_BREACH_REQ_OUT_NODE_OFFSET 1
#define RPC_BREACH_REQ_OUT_NODE_LEN    1

#define RPC_BREACH_REQ_LEN (\
    RPC_BREACH_REQ_IN_NODE_LEN + \
    RPC_BREACH_REQ_OUT_NODE_LEN)

#define RPC_BREACH_REPLY_LEN 0

#define RPC_RESTORE_REQ_IN_NODE_OFFSET  0
#define RPC_RESTORE_REQ_IN_NODE_LEN     1
#define RPC_RESTORE_REQ_OUT_NODE_OFFSET 1
#define RPC_RESTORE_REQ_OUT_NODE_LEN    1

#define RPC_RESTORE_REQ_LEN (\
    RPC_RESTORE_REQ_IN_NODE_LEN + \
    RPC_RESTORE_REQ_OUT_NODE_LEN)

#define RPC_RESTORE_REPLY_LEN 0


#define MAX_FENCE_RPC_REQ_LEN MAX(\
    RPC_CREATE_SECTION_REQ_LEN, \
    MAX(\
    RPC_DESTROY_SECTION_REQ_LEN, \
    MAX(\
    RPC_REQ_CREATE_BEAM_REQ_LEN, \
    MAX(\
    RPC_REQ_DESTROY_BEAM_REQ_LEN, \
    MAX(\
    RPC_BREACH_REQ_LEN, \
    RPC_RESTORE_REQ_LEN)))))

#define MAX_FENCE_RPC_REPLY_LEN MAX(\
    RPC_CREATE_SECTION_REPLY_LEN, \
    MAX(\
    RPC_DESTROY_SECTION_REPLY_LEN, \
    MAX(\
    RPC_REQ_CREATE_BEAM_REPLY_LEN, \
    MAX(\
    RPC_REQ_DESTROY_BEAM_REPLY_LEN, \
    MAX(\
    RPC_BREACH_REPLY_LEN, \
    RPC_RESTORE_REPLY_LEN)))))

enum {
    RPC_INVALID = 0,
    RPC_CREATE_SECTION,
    RPC_DESTROY_SECTION,

    /* These are unfortunate, they exist because the 'incoming' node
     * has to be the slave, so that it can notify the master when the
     * beam is broken. So, at the fence level, the master has to send
     * the RPC to the incoming node, but at the beam level, the beam
     * has to be initiated by sending an RPC from outgoing node to
     * the incoming node. The efficient way to do this seems to imply
     * making beam layer aware of fence layer concept (like master),
     * which we don't want, so for now, we solve the problem with
     * this extra pair of calls. */
    RPC_REQ_CREATE_BEAM,
    RPC_REQ_DESTROY_BEAM,

    RPC_BREACH,
    RPC_RESTORE,
} rpc_t;

static const char proc_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [RPC_INVALID] = "<invalid>",
    [RPC_CREATE_SECTION] = "create_section",
    [RPC_DESTROY_SECTION] = "destroy_section",
    [RPC_REQ_CREATE_BEAM] = "req_create_beam",
    [RPC_REQ_DESTROY_BEAM] = "req_create_beam",
    [RPC_BREACH] = "breach",
    [RPC_RESTORE] = "restore",
};

static const char fence_name[] PROGMEM = "fence";

static rpc_proc_t proc_create_section;
static rpc_proc_t proc_destroy_section;
static rpc_proc_t proc_req_create_beam;
static rpc_proc_t proc_req_destroy_beam;
static rpc_proc_t proc_breach;
static rpc_proc_t proc_restore;

static rpc_proc_t *procedures[] = {
    [RPC_CREATE_SECTION] = &proc_create_section,
    [RPC_DESTROY_SECTION] = &proc_destroy_section,
    [RPC_REQ_CREATE_BEAM] = &proc_req_create_beam,
    [RPC_REQ_DESTROY_BEAM] = &proc_req_destroy_beam,
    [RPC_BREACH] = &proc_breach,
    [RPC_RESTORE] = &proc_restore,
};
static msg_t server_queue[FENCE_RPC_SERVER_QUEUE_SIZE];
static msg_t client_queue[RPC_CLIENT_QUEUE_SIZE];
static rpc_endpoint_t endpoint = {
    .server = {
        .name = fence_name,
        .listener = {
            .port = PORT_RPC_SERVER_FENCE,
            .queue = { .size = FENCE_RPC_SERVER_QUEUE_SIZE },
            .queue_data = server_queue,
        },
        .procedures = procedures,
        .proc_count = sizeof(procedures) / sizeof(procedures[0]),
        .proc_names = &proc_names,
    },
    .client = {
        .name = fence_name,
        .listener = {
            .port = PORT_RPC_CLIENT_FENCE,
            .queue = { .size = RPC_CLIENT_QUEUE_SIZE },
            .queue_data = client_queue,
        }
    }
};

static nrk_task_type FENCE_TASK;
static NRK_STK fence_task_stack[STACKSIZE_FENCE];

static uint8_t req_buf[MAX_FENCE_RPC_REQ_LEN];
static uint8_t reply_buf[MAX_FENCE_RPC_REPLY_LEN];

typedef struct {
    bool active;
    fence_t fence;
} master_state_t;

typedef struct {
    bool active;
    bool breached;
    node_id_t master;
    node_id_t in_node;
    node_id_t out_node;
} slave_state_t;

static master_state_t master_state;
static slave_state_t slave_state;

static nrk_time_t fence_section_delay = {2, 0};

static void print_fence(fence_t *fence)
{
    uint8_t i;

    COUT("fence: ");
    for (i = 0; i < fence->len; ++i) {
        COUTP("%u:%d ", fence->posts[i], fence->section_state[i]);
    }
    COUTA("\r\n");
}

static int8_t rpc_create_section(node_id_t out_node, node_id_t in_node)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;
    bool section_status;

    req_buf[RPC_CREATE_SECTION_REQ_OUT_NODE_OFFSET] = out_node;
    req_len += RPC_CREATE_SECTION_REQ_OUT_NODE_LEN;

    rc = rpc_call(&endpoint.client, in_node, PORT_RPC_SERVER_FENCE, RPC_CREATE_SECTION,
                  &fence_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: create section rpc failed\r\n");
        return rc;
    }
    if (reply_len != RPC_CREATE_SECTION_REPLY_LEN) {
        LOG("WARN: beam reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    section_status = reply_buf[RPC_CREATE_SECTION_REPLY_STATUS_OFFSET];
    LOG("rpc section status: "); LOGP("%d\r\n", section_status);

    return section_status ? NRK_OK : NRK_ERROR;
}

static int8_t rpc_destroy_section(node_id_t out_node, node_id_t in_node)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;
    bool section_status;

    req_buf[RPC_DESTROY_SECTION_REQ_OUT_NODE_OFFSET] = out_node;
    req_len += RPC_DESTROY_SECTION_REQ_OUT_NODE_LEN;

    rc = rpc_call(&endpoint.client, in_node, PORT_RPC_SERVER_FENCE, RPC_DESTROY_SECTION,
                  &fence_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: destroy section rpc failed\r\n");
        return rc;
    }
    if (reply_len != RPC_DESTROY_SECTION_REPLY_LEN) {
        LOG("WARN: beam reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    section_status = reply_buf[RPC_DESTROY_SECTION_REPLY_STATUS_OFFSET];
    LOG("rpc section status: "); LOGP("%d\r\n", section_status);

    return rc;
}

static int8_t rpc_req_create_beam(node_id_t out_node)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    rc = rpc_call(&endpoint.client, out_node, PORT_RPC_SERVER_FENCE, RPC_REQ_CREATE_BEAM,
                  &fence_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: req create beam rpc failed\r\n");
        return rc;
    }
    if (reply_len != RPC_REQ_CREATE_BEAM_REPLY_LEN) {
        LOG("WARN: req create beam rpc reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    return rc;
}

static int8_t rpc_req_destroy_beam(node_id_t out_node)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    rc = rpc_call(&endpoint.client, out_node, PORT_RPC_SERVER_FENCE,
                  RPC_REQ_DESTROY_BEAM,
                  &fence_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: req destroy beam rpc failed\r\n");
        return rc;
    }
    if (reply_len != RPC_REQ_DESTROY_BEAM_REPLY_LEN) {
        LOG("WARN: req destroy beam rpc reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    return rc;
}

static int8_t rpc_breach(node_id_t master, node_id_t out_node, node_id_t in_node)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    req_buf[RPC_BREACH_REQ_OUT_NODE_OFFSET] = out_node;
    req_len += RPC_BREACH_REQ_OUT_NODE_LEN;
    req_buf[RPC_BREACH_REQ_IN_NODE_OFFSET] = in_node;
    req_len += RPC_BREACH_REQ_IN_NODE_LEN;

    rc = rpc_call(&endpoint.client, master, PORT_RPC_SERVER_FENCE, RPC_BREACH,
                  &fence_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: breach rpc failed\r\n");
        return rc;
    }
    if (reply_len != RPC_BREACH_REPLY_LEN) {
        LOG("WARN: beam reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    return rc;
}

static int8_t rpc_restore(node_id_t master, node_id_t out_node, node_id_t in_node)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    req_buf[RPC_RESTORE_REQ_OUT_NODE_OFFSET] = out_node;
    req_len += RPC_RESTORE_REQ_OUT_NODE_LEN;
    req_buf[RPC_RESTORE_REQ_IN_NODE_OFFSET] = in_node;
    req_len += RPC_RESTORE_REQ_IN_NODE_LEN;

    rc = rpc_call(&endpoint.client, master, PORT_RPC_SERVER_FENCE, RPC_RESTORE,
                  &fence_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: restore rpc failed\r\n");
        return rc;
    }
    if (reply_len != RPC_RESTORE_REPLY_LEN) {
        LOG("WARN: restore reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    return rc;
}

static int8_t proc_create_section(node_id_t requester,
                                  uint8_t *req_buf, uint8_t req_len,
                                  uint8_t *reply_buf, uint8_t reply_size,
                                  uint8_t *reply_len)
{
    int8_t rc;
    node_id_t out_node;

    LOG("create section req from: "); LOGP("%u\r\n", requester);

    if (req_len < RPC_CREATE_SECTION_REQ_LEN) {
        LOG("WARN: unexpected req len: ");
        LOGP("%u/%u\r\n", req_len, RPC_CREATE_SECTION_REQ_LEN);
        return NRK_ERROR;
    }

    if (reply_size < RPC_CREATE_SECTION_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_CREATE_SECTION_REPLY_LEN);
        return NRK_ERROR;
    }

    out_node = req_buf[RPC_CREATE_SECTION_REQ_OUT_NODE_OFFSET];
    rc = rpc_req_create_beam(out_node);

    if (rc == NRK_OK) {
        slave_state.active = true;
        slave_state.breached = false;
        slave_state.master = requester;
        slave_state.in_node = this_node_id;
        slave_state.out_node = out_node;
    }

    reply_buf[RPC_CREATE_SECTION_REPLY_STATUS_OFFSET] = rc == NRK_OK;
    *reply_len = RPC_CREATE_SECTION_REPLY_LEN;

    return NRK_OK;
}

int8_t create_fence(fence_t *fence)
{
    int8_t rc;
    uint8_t i;

    if (master_state.active) {
        WARN("ERROR: a fence is already active\r\n");
        return NRK_ERROR;
    }

    LOG("creating fence: ");
    for (i = 0; i < fence->len; ++i)
        LOGP("%d ", fence->posts[i]);
    LOGA("\r\n");

    if (fence->len < 2) {
        LOG("WARN: fence must have at least two posts\r\n");
        return NRK_ERROR;
    }

    for (i = 0; i < fence->len - 1; ++i) {
        rc = rpc_create_section(fence->posts[i], fence->posts[i + 1]);
        LOGP("create section: ");
        LOGP("%d -> %d: %c\r\n",
             fence->posts[i], fence->posts[i + 1],
             rc == NRK_OK ? 'S' : 'F');

        if (rc == NRK_OK) {
            fence->section_state[i] = SECTION_STATE_ACTIVE;
        } /* continue: don't give up */

        print_fence(fence);

        nrk_wait(fence_section_delay);
    }

    master_state.active = true;

    return NRK_OK;
}

int8_t destroy_fence(fence_t *fence)
{
    int8_t rc;
    uint8_t i;

    if (!master_state.active) {
       WARN("ERROR: no fence is active\r\n");
       return NRK_ERROR;
    }

    LOG("destroying fence: ");
    for (i = 0; i < fence->len; ++i)
        LOGP("%d ", fence->posts[i]);
    LOGA("\r\n");

    if (fence->len < 2) {
        LOG("WARN: fence must have at least two posts\r\n");
        return NRK_ERROR;
    }

    for (i = 0; i < fence->len - 1; ++i) {
        rc = rpc_destroy_section(fence->posts[i], fence->posts[i + 1]);
        LOGP("destroy section: ");
        LOGP("%d -> %d: %c\r\n",
             fence->posts[i], fence->posts[i + 1],
             rc == NRK_OK ? 'S' : 'F');

        if (rc == NRK_OK) {
            fence->section_state[i] = SECTION_STATE_NONE;
        } /* don't give up */

        print_fence(fence);
    }

    master_state.active = false;
    memset(&master_state.fence, 0, sizeof(master_state.fence));
    return NRK_OK;
}


static int8_t proc_destroy_section(node_id_t requester,
                                   uint8_t *req_buf, uint8_t req_len,
                                   uint8_t *reply_buf, uint8_t reply_size,
                                   uint8_t *reply_len)
{
    int8_t rc;
    node_id_t out_node;

    LOG("destroy section req from: "); LOGP("%u\r\n", requester);

    if (requester != slave_state.master) {
        LOG("WARN: cannot destroy section: requester not master: ");
        LOGP("%d/%d\r\n", requester, slave_state.master);
        return NRK_ERROR;
    }

    if (req_len < RPC_DESTROY_SECTION_REQ_LEN) {
        LOG("WARN: unexpected req len: ");
        LOGP("%u/%u\r\n", req_len, RPC_DESTROY_SECTION_REQ_LEN);
        return NRK_ERROR;
    }

    if (reply_size < RPC_DESTROY_SECTION_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_DESTROY_SECTION_REPLY_LEN);
        return NRK_ERROR;
    }

    out_node = req_buf[RPC_DESTROY_SECTION_REQ_OUT_NODE_OFFSET];
    rc = rpc_req_destroy_beam(out_node);

    if (rc == NRK_OK) {
        slave_state.active = false;
        slave_state.breached = false;
        slave_state.master = INVALID_NODE_ID;
        slave_state.in_node = INVALID_NODE_ID;
        slave_state.out_node = INVALID_NODE_ID;
    }

    reply_buf[RPC_DESTROY_SECTION_REPLY_STATUS_OFFSET] = rc;
    *reply_len = RPC_DESTROY_SECTION_REPLY_LEN;

    return NRK_OK;
}

static int8_t proc_req_create_beam(node_id_t requester,
                                uint8_t *req_buf, uint8_t req_len,
                                uint8_t *reply_buf, uint8_t reply_size,
                                uint8_t *reply_len)
{
    int8_t rc;

    LOG("request beam req from: "); LOGP("%u\r\n", requester);

    /* TODO: could add state for the out node (distinct from,
     * but in the same category as 'slave' -- actually, could
     * re-use slave.) */

    if (req_len != RPC_REQ_CREATE_BEAM_REQ_LEN) {
        LOG("WARN: req len unexpected: ");
        LOGP("%u/%u\r\n", req_len, RPC_REQ_CREATE_BEAM_REQ_LEN);
        return NRK_ERROR;
    }

    if (reply_size < RPC_REQ_CREATE_BEAM_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_REQ_CREATE_BEAM_REPLY_LEN);
        return NRK_ERROR;
    }

    rc = create_beam(requester);
    LOG("create beam rc: "); LOGP("%d\r\n", rc);

    *reply_len = 0;

    return rc;
}

static int8_t proc_req_destroy_beam(node_id_t requester,
                                uint8_t *req_buf, uint8_t req_len,
                                uint8_t *reply_buf, uint8_t reply_size,
                                uint8_t *reply_len)
{
    int8_t rc;

    LOG("request beam req from: "); LOGP("%u\r\n", requester);

    /* TODO: could add state for the out node (distinct from,
     * but in the same category as 'slave' -- actually, could
     * re-use slave.) */

    if (req_len != RPC_REQ_DESTROY_BEAM_REQ_LEN) {
        LOG("WARN: req len unexpected: ");
        LOGP("%u/%u\r\n", req_len, RPC_REQ_DESTROY_BEAM_REQ_LEN);
        return NRK_ERROR;
    }

    if (reply_size < RPC_REQ_DESTROY_BEAM_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_REQ_DESTROY_BEAM_REPLY_LEN);
        return NRK_ERROR;
    }

    rc = destroy_beam(requester);
    LOG("destroy beam rc: "); LOGP("%d\r\n", rc);

    *reply_len = 0;

    return rc;
}

static int8_t proc_breach(node_id_t requester,
                          uint8_t *req_buf, uint8_t req_len,
                          uint8_t *reply_buf, uint8_t reply_size,
                          uint8_t *reply_len)
{
    node_id_t in_node, out_node;
    bool found;
    uint8_t i;

    LOG("breach req from: "); LOGP("%u\r\n", requester);

    if (!master_state.active) {
        LOG("WARN: unexpected breach req: not master: ");
        LOGP("%d\r\n", requester);
        return NRK_ERROR;
    }

    if (req_len != RPC_BREACH_REQ_LEN) {
        LOG("WARN: req len unexpected: ");
        LOGP("%u/%u\r\n", req_len, RPC_BREACH_REQ_LEN);
        return NRK_ERROR;
    }

    if (reply_size < RPC_BREACH_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_BREACH_REPLY_LEN);
        return NRK_ERROR;
    }

    in_node = req_buf[RPC_BREACH_REQ_IN_NODE_OFFSET];
    out_node = req_buf[RPC_BREACH_REQ_OUT_NODE_OFFSET];

    LOG("section breached: ");
    LOGP("%d -> %d\r\n", out_node, in_node);

    found = false;
    for (i = 0; i < master_state.fence.len - 1; ++i) {
        if (master_state.fence.posts[i] == out_node &&
            master_state.fence.posts[i + 1] == in_node) {
            master_state.fence.section_state[i] =
                SECTION_STATE_BREACHED;
            found = true;
            break;
        }
    }

    if (!found) {
        LOG("WARN: section not found in fence\r\n");
        return NRK_ERROR;
    }

    print_fence(&master_state.fence);

    *reply_len = 0;

    return NRK_OK;
}

static int8_t proc_restore(node_id_t requester,
                          uint8_t *req_buf, uint8_t req_len,
                          uint8_t *reply_buf, uint8_t reply_size,
                          uint8_t *reply_len)
{
    node_id_t in_node, out_node;
    bool found;
    uint8_t i;

    LOG("restore req from: "); LOGP("%u\r\n", requester);

    if (!master_state.active) {
        LOG("WARN: unexpected restore req: not master: ");
        LOGP("%d\r\n", requester);
        return NRK_ERROR;
    }

    if (req_len != RPC_RESTORE_REQ_LEN) {
        LOG("WARN: req len unexpected: ");
        LOGP("%u/%u\r\n", req_len, RPC_RESTORE_REQ_LEN);
        return NRK_ERROR;
    }

    if (reply_size < RPC_RESTORE_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_RESTORE_REPLY_LEN);
        return NRK_ERROR;
    }

    in_node = req_buf[RPC_RESTORE_REQ_IN_NODE_OFFSET];
    out_node = req_buf[RPC_RESTORE_REQ_OUT_NODE_OFFSET];

    LOG("section restored: ");
    LOGP("%d -> %d\r\n", out_node, in_node);

    found = false;
    for (i = 0; i < master_state.fence.len - 1; ++i) {
        if (master_state.fence.posts[i] == out_node &&
            master_state.fence.posts[i + 1] == in_node) {
            master_state.fence.section_state[i] =
                SECTION_STATE_ACTIVE;
            found = true;
            break;
        }
    }

    if (!found) {
        LOG("WARN: section not found in fence\r\n");
        return NRK_ERROR;
    }

    print_fence(&master_state.fence);


    *reply_len = 0;

    return NRK_OK;
}

static void fence_task()
{
    int8_t rc;

    rc = nrk_signal_register(beam_signal);
    if (rc == NRK_ERROR)
        ABORT("failed to register for beam signal\r\n");

    rpc_activate_endpoint(&endpoint);

    while (1) {
        
        if (slave_state.active) {
            if (!get_beam_state()) {
               if (!slave_state.breached) {
                   LOG("section broken\r\n");
                   slave_state.breached = true;
                   rc = rpc_breach(slave_state.master,
                                   slave_state.out_node, slave_state.in_node);
                   LOG("rpc breach: rc "); LOGP("%d\r\n", rc);
               }
            } else {
                if (slave_state.breached) {
                    LOG("section restored\r\n");
                    slave_state.breached = false;
                   rc = rpc_restore(slave_state.master,
                                    slave_state.out_node, slave_state.in_node);
                   LOG("rpc restore: rc "); LOGP("%d\r\n", rc);
                }
            }
        }

        rpc_serve(&endpoint.server);

        nrk_event_wait( SIG(beam_signal) |
                        SIG(endpoint.server.listener.signal) );
    }

    ABORT("fence task exited\r\n");
}


int8_t cmd_fence(uint8_t argc, char **argv)
{
    node_id_t node;
    uint8_t i;
    int8_t rc = NRK_OK;

    if (!(argc == 1 || argc == 2 || argc >= 3)) {
        OUT("usage: fence [+|- [max | <node> <node>...]]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2 || argc >= 3) {

        if (argc == 2 && argv[1][0] == '-') { /* destroy */
            if (!master_state.active) {
                OUT("ERROR: no fence is active\r\n");
                return NRK_ERROR;
            }
            rc = destroy_fence(&master_state.fence);
        } else { /* create */

            if (master_state.active) {
                OUT("ERROR: a fence is already active\r\n");
                return NRK_ERROR;
            }

            memset(&master_state.fence, 0, sizeof(fence_t));

            if (argc == 3 && !strcmp("max", argv[2])) {
#if ENABLE_AUTOFENCE
                rc = calc_max_fence(&master_state.fence);
                if (rc != NRK_OK) {
                    OUT("ERROR: max fence calculation failed\r\n");
                    return NRK_ERROR;
                }
#else
                OUT("ERROR: auto fence module not linked in\r\n");
                return NRK_ERROR;
#endif /* ENABLE_AUTOFENCE */
            } else {
                master_state.fence.len = 0;
                for (i = 2; i < argc; ++i) {
                    node = atoi(argv[i]);
                    if (!IS_VALID_NODE_ID(node)) {
                        OUT("ERROR: invalid node id\r\n");
                        return NRK_ERROR;
                    }
                    master_state.fence.posts[i - 2] = node;
                    master_state.fence.len++;
                }
            }

            rc = create_fence(&master_state.fence);
        }
    } else { /* no args */
        print_fence(&master_state.fence);
    }
    return rc;
}

uint8_t init_fence(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    rpc_init_endpoint(&endpoint);

    beam_signal = nrk_signal_create();
    if (beam_signal == NRK_ERROR)
        ABORT("failed to create beam signal\r\n");

    num_tasks++;
    FENCE_TASK.task = fence_task;
    FENCE_TASK.Ptos = (void *) &fence_task_stack[STACKSIZE_FENCE - 1];
    FENCE_TASK.Pbos = (void *) &fence_task_stack[0];
    FENCE_TASK.prio = priority;
    FENCE_TASK.FirstActivation = TRUE;
    FENCE_TASK.Type = BASIC_TASK;
    FENCE_TASK.SchType = PREEMPTIVE;
    FENCE_TASK.period.secs = 0;
    FENCE_TASK.period.nano_secs = 0;
    FENCE_TASK.cpu_reserve.secs = 0;
    FENCE_TASK.cpu_reserve.nano_secs = 0 * NANOS_PER_MS;
    FENCE_TASK.offset.secs = 0;
    FENCE_TASK.offset.nano_secs = 0;
    nrk_activate_task (&FENCE_TASK);

    ASSERT(num_tasks == NUM_TASKS_FENCE);
    return num_tasks;
}
