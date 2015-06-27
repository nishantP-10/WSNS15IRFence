#include <nrk.h>
#include <nrk_error.h>
#include <nrk_time.h>
#include <include.h>
#include <math.h>
#include <stdlib.h>

#include "output.h"
#include "time.h"
#include "twi.h"
#include "rpc.h"
#include "ports.h"
#include "rxtx.h"

#include "compass.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_COMPASS

#define COMPASS_ADDR 0x1E

#define REG_CTRL_A 0x00
#define REG_CTRL_B 0x01
#define REG_MODE 0x02
#define REG_DATA_OUT_X_MSB 0x03
#define REG_DATA_OUT_X_LSB 0x04
#define REG_DATA_OUT_Y_MSB 0x05
#define REG_DATA_OUT_Y_LSB 0x06
#define REG_DATA_OUT_Z_MSB 0x07
#define REG_DATA_OUT_Z_LSB 0x08
#define REG_STATUS 0x09
#define REG_ID_A 0x0A
#define REG_ID_B 0x0B
#define REG_ID_C 0x0C

#define DEV_ID_LEN 3

#define BIT_STATUS_READY (1 << 0)

#define NUM_AXES 3
#define VALUE_SIZE 2 /* bytes in a sensor axis reading */

/* From datasheet, in order of register addrs */
#define RAW_AXIS_X 0
#define RAW_AXIS_Z 1
#define RAW_AXIS_Y 2

/* Normal order of axis */
#define AXIS_X 0
#define AXIS_Y 1
#define AXIS_Z 2

/* From datasheet Table 9 (p. 13) */
#define GAUSS_LSB 1090
#define GAUSS_TO_MICROTESLA 100 /* Adafruit sensor library */

static bool have_compass;
static nrk_time_t heading_period = { 2, 0 * NANOS_PER_MS };

static int16_t mag[NUM_AXES];
static float mag_uT[NUM_AXES];


#if ENABLE_COMPASS_RPC

#define RPC_HEADING_REPLY_HEADING_OFFSET 0
#define RPC_HEADING_REPLY_HEADING_LEN 2
#define RPC_HEADING_REPLY_MAG_OFFSET 2
#define RPC_HEADING_REPLY_MAG_LEN sizeof(mag_uT)

#define RPC_HEADING_REPLY_LEN (\
        RPC_HEADING_REPLY_HEADING_LEN + \
        RPC_HEADING_REPLY_MAG_LEN)

#define RPC_HEADING_REQ_LEN 0

static uint8_t req_buf[RPC_HEADING_REQ_LEN];
static uint8_t reply_buf[RPC_HEADING_REPLY_LEN];

#endif

static const char compass_name[] PROGMEM = "compass";

#if ENABLE_COMPASS_RPC

enum {
    RPC_INVALID = 0,
    RPC_HEADING,
} rpc_t;

static const char proc_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    [RPC_INVALID] = "<invalid>",
    [RPC_HEADING] = "heading",
};


static rpc_proc_t proc_heading;

static rpc_proc_t *procedures[] = {
    [RPC_HEADING] = &proc_heading,
};
static msg_t server_queue[COMPASS_RPC_SERVER_QUEUE_SIZE];
static msg_t client_queue[RPC_CLIENT_QUEUE_SIZE];
rpc_endpoint_t compass_endpoint = {
    .server = {
        .name = compass_name,
        .listener = {
            .port = PORT_RPC_SERVER_COMPASS,
            .queue = { .size = COMPASS_RPC_SERVER_QUEUE_SIZE },
            .queue_data = server_queue,
        },
        .procedures = procedures,
        .proc_count = sizeof(procedures) / sizeof(procedures[0]),
        .proc_names = &proc_names,
    },
    .client = {
        .name = compass_name,
        .listener = {
            .port = PORT_RPC_CLIENT_COMPASS,
            .queue = { .size = RPC_CLIENT_QUEUE_SIZE },
            .queue_data = client_queue,
        }
    }
};

#endif

int8_t get_heading(int16_t *heading)
{
    uint8_t status = 0;
    nrk_time_t start, elapsed, now;
    int8_t rc;
    uint8_t i;
    uint8_t lsb, msb;
    float heading_rad;

    if (!have_compass) {
        LOG("ERROR: no compass\r\n");
        return NRK_ERROR;
    }
 
    /* trigger a measurement */
    rc = twi_write(COMPASS_ADDR, REG_MODE, 0x01);
    if (rc != NRK_OK) {
        LOG("ERROR: failed to set mode\r\n");
        return rc;
    }

    /* wait for data ready */
    nrk_time_get(&start);
    do {
        nrk_wait(compass_poll_interval);

        status = 0;
        rc = twi_read(COMPASS_ADDR, REG_STATUS, &status);
        if (rc != NRK_OK) {
            LOG("ERROR: failed to read status\r\n");
            return rc;
        }
        nrk_time_get(&now);
        nrk_time_sub(&elapsed, now, start);
    } while (!(status & BIT_STATUS_READY) &&
            time_cmp(&elapsed, &compass_measure_timeout) < 0);

    if (!(status & BIT_STATUS_READY)) {
        LOG("ERROR: compass measure timed out\r\n");
        return NRK_ERROR;
    }

    for (i = 0; i < NUM_AXES; ++i) {
        rc = twi_read(COMPASS_ADDR, REG_DATA_OUT_X_MSB + 2 * i, &msb);
        if (rc != NRK_OK) break;
        rc = twi_read(COMPASS_ADDR, REG_DATA_OUT_X_LSB + 2 * i, &lsb);
        if (rc != NRK_OK) break;
        mag[i] = (msb << 8) | lsb;
    }

    if (rc != NRK_OK) {
        LOG("ERROR: compass read failed\r\n");
        return rc;
    }

    /* Re-order to (X, Y, Z) and cast to float */
    mag_uT[AXIS_X] = mag[RAW_AXIS_X];
    mag_uT[AXIS_Y] = mag[RAW_AXIS_Y];
    mag_uT[AXIS_Z] = mag[RAW_AXIS_Z];

    /* Convert to uT units */
    for (i = 0; i < NUM_AXES; ++i)
        mag_uT[i] = mag_uT[i] / GAUSS_LSB * GAUSS_TO_MICROTESLA;

    heading_rad = atan2(mag_uT[AXIS_Y], mag_uT[AXIS_X]);
    if (heading_rad < 0)
        heading_rad += 2.0 * M_PI;
    heading_rad *= 180.0 / M_PI;

    *heading  = heading_rad;

    LOG("raw: ");
    for (i = 0; i < NUM_AXES; ++i)
        LOGP("%x:%x ", (mag[i] >> 8) & 0xFF, mag[i] & 0xFF);
    LOGA("dec: ");
    for (i = 0; i < NUM_AXES; ++i)
        LOGP("%d ", mag[i]);
    LOGA("\r\n");

    LOG("uT: ");
    for (i = 0; i < NUM_AXES; ++i)
        LOGP("%d ", (int16_t)mag_uT[i]);
    LOGP("=> %d deg\r\n", *heading);

    return NRK_OK;
}

#if ENABLE_COMPASS_RPC
static int8_t rpc_heading(node_id_t node, int16_t *heading)
{
    int8_t rc = NRK_OK;
    uint8_t reply_len = sizeof(reply_buf);
    uint8_t req_len = 0;

    rc = rpc_call(&compass_endpoint.client, node, PORT_RPC_SERVER_COMPASS, RPC_HEADING,
                  &compass_rpc_time_out, req_buf, req_len, reply_buf, &reply_len);
    if (rc != NRK_OK) {
        LOG("WARN: heading rpc failed\r\n");
        return rc;
    }

    if (reply_len != RPC_HEADING_REPLY_LEN) {
        LOG("WARN: heading reply of unexpected length\r\n");
        return NRK_ERROR;
    }

    *heading = reply_buf[RPC_HEADING_REPLY_HEADING_OFFSET + 1];
    *heading <<= 8;
    *heading |= reply_buf[RPC_HEADING_REPLY_HEADING_OFFSET];

    memcpy(mag_uT, reply_buf + RPC_HEADING_REPLY_MAG_OFFSET, sizeof(mag_uT));

    return NRK_OK;
}

static int8_t proc_heading(node_id_t requester,
                        uint8_t *req_buf, uint8_t req_len,
                        uint8_t *reply_buf, uint8_t reply_size,
                        uint8_t *reply_len)
{
    int16_t heading;
    int8_t rc;

    LOG("heading req from "); LOGP("%u\r\n", requester);

    if (reply_size < RPC_HEADING_REPLY_LEN) {
        LOG("WARN: reply buf too small: ");
        LOGP("%u/%u\r\n", reply_size, RPC_HEADING_REPLY_LEN);
        return NRK_ERROR;
    }

    if (!have_compass) {
        LOG("WARN: rpc failed: no compass\r\n");
        return NRK_ERROR;
    }

    rc = get_heading(&heading);
    if (rc != NRK_OK) {
        LOG("WARN: failed to get heading\r\n");
        return rc;
    }

    reply_buf[RPC_HEADING_REPLY_HEADING_OFFSET] = heading & 0xFF;
    reply_buf[RPC_HEADING_REPLY_HEADING_OFFSET + 1] = heading >> 8;
    *reply_len = RPC_HEADING_REPLY_HEADING_LEN;
    memcpy(reply_buf + RPC_HEADING_REPLY_MAG_OFFSET,  mag_uT, sizeof(mag_uT));
    *reply_len += sizeof(mag_uT);

    return NRK_OK;
}
#endif

static void show_heading(int16_t *heading)
{
    if (heading) {
        OUTP("%d ", *heading);
        OUTP("[%d %d %d]",
             (int16_t)mag_uT[0], (int16_t)mag_uT[1], (int16_t)mag_uT[2]);
    } else {
        OUT("error");
    }
    OUT("\r\n");
}

int8_t cmd_head(uint8_t argc, char **argv)
{
    int16_t heading;
    int8_t rc = NRK_OK, rc_node = NRK_OK;
#if ENABLE_COMPASS_RPC
    node_id_t node;
    uint8_t i;
#endif

    if (!(argc == 1 || argc >= 2)) {
        OUT("usage: head [<node>...]\r\n");
        return NRK_ERROR;
    }

    if (have_compass) {
        rc_node = get_heading(&heading);
        if (rc_node != NRK_OK)
            rc = rc_node;
    }

    OUT("node: heading [x y z] (uT)\r\n");

    if (have_compass) {
        OUTP("%d: ", this_node_id);
        show_heading(rc == NRK_OK ? &heading : NULL);
    }

    if (argc >= 2) {
#if ENABLE_COMPASS_RPC
        for (i = 1; i < argc; ++i) {
            node = atoi(argv[i]);

            rc_node = rpc_heading(node, &heading);
            if (rc_node != NRK_OK)
                rc = rc_node;

            OUTP("%d: ", node);
            show_heading(rc == NRK_OK ? &heading : NULL);
        }
#else
        OUT("ERROR: COMPASS_RPC not linked in\r\n");
        return NRK_ERROR;
#endif
    }
    return rc;
}

static void print_device_info()
{
    unsigned char id[4] = {0};
    uint8_t ctrl_a, ctrl_b;
    uint8_t i;
    int8_t rc;

    for (i = 0; i < DEV_ID_LEN; ++i) {
        rc = twi_read(COMPASS_ADDR, REG_ID_A + i, &id[i]);
        if (rc != NRK_OK)
            ABORT("failed to read ID\r\n");
    }

    rc = twi_read(COMPASS_ADDR, REG_CTRL_A, &ctrl_a);
    if (rc != NRK_OK)
        ABORT("failed to read ctrl a reg\r\n");

    rc = twi_read(COMPASS_ADDR, REG_CTRL_B, &ctrl_b);
    if (rc != NRK_OK)
        ABORT("failed to read ctrl b reg\r\n");

    LOG("device info: ");
    LOGA(" id: "); LOGP("%s ", id);
    LOGA(" crl a: "); LOGP("0x%x ", ctrl_a);
    LOGA(" ctrl b: "); LOGP("0x%x ", ctrl_b);
    LOGA("\r\n");
}

static void periodic_heading_process(bool enabled,
    nrk_time_t *next_event, nrk_sig_mask_t *wait_mask)
{
    int16_t heading;
    int8_t rc;
    nrk_time_t now;

    if (!enabled)
        return;

    rc = get_heading(&heading);
    OUT("heading: ");
    show_heading(rc == NRK_OK ? &heading : NULL);

    nrk_time_get(&now);
    nrk_time_add(next_event, now, heading_period);
}

uint8_t init_compass(uint8_t priority)
{
    uint8_t num_tasks = 0;
    int8_t rc;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    // 1 sample per reading and 15Hz frequency
    rc = twi_write(COMPASS_ADDR, REG_CTRL_A, 0x10);
    if (rc == NRK_OK) {
        have_compass = true;
        print_device_info();
    } else {
        have_compass = false;
        WARN("device init failed\r\n");
        /* continue (heading RPC needs to be supported) */
    }


#if ENABLE_COMPASS_RPC
    rpc_init_endpoint(&compass_endpoint);
#endif

    ASSERT(num_tasks == NUM_TASKS(COMPASS));
    return num_tasks;
}

periodic_func_t func_heading = {
    .name = compass_name,
    .proc = periodic_heading_process,
};
