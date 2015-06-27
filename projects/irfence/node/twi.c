#include <nrk.h>
#include <nrk_error.h>
#include <nrk_timer.h>
#include <include.h>
#include <stdlib.h>

#include "TWI_Master.h"

#include "cfg.h"
#include "config.h"
#include "output.h"
#include "time.h"

#include "twi.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_TWI

static uint8_t msg_buf[TWI_MSG_BUF_SIZE];

uint8_t handle_error(uint8_t rc)
{
    /* A failure has occurred, use TWIerrorMsg to determine the
     * nature of the failure and take appropriate actions.  Se
     * header file for a list of possible failures messages.
     *
     * Here is a simple sample, where if received a NACK on the
     * slave address, then a retransmission will be initiated. */

    if (rc == TWI_MTX_ADR_NACK || rc == TWI_MRX_ADR_NACK)
        TWI_Start_Transceiver();

    return rc;
}

static int8_t twi_tx(uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t waits = 0;
    const uint8_t max_waits =
        TIME_TO_MS(twi_tx_timeout) / TIME_TO_MS(twi_tx_poll_interval);

    LOG("TWI write: ");
    for (i = 0; i < len; ++i)
        LOGP("0x%x ", msg_buf[i]);
    LOGA("\r\n");

    TWI_Start_Transceiver_With_Data(msg_buf, len);

    /* NOTE: can't use nrk_time_get because before nrk_start() */
    while (TWI_Transceiver_Busy() && waits++ < max_waits)
        nrk_spin_wait_us(twi_tx_poll_interval.nano_secs / 1000);

    if (waits >= max_waits) {
        LOG("WARN: TWI write timed out\r\n");
        return NRK_ERROR;
    }

    if (!TWI_statusReg.lastTransOK)
    {
        LOG("WARN: TWI write failed\r\n");
        handle_error(TWI_Get_State_Info());
        return NRK_ERROR;
    }

    return NRK_OK;
}


int8_t twi_write(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t len = 0;

    msg_buf[len++] = addr << TWI_ADR_BITS;
    msg_buf[len++] = reg;
    msg_buf[len++] = val;

    return twi_tx(msg_buf, len);
}

static int8_t twi_point(uint8_t addr, uint8_t reg)
{
    uint8_t len = 0;
    msg_buf[len++] = addr << TWI_ADR_BITS;
    msg_buf[len++] = reg;
    return twi_tx(msg_buf, len);
}

/* NOTE: multi-byte reads (using hw auto-incrementing of
 * register address) is not supported by this function
 * deliberately: it does not work -- only one byte is
 * returned. An additional issue is that an extra '0x3d'
 * (read command) byte is returned ('echoed') before
 * the actual reply byte -- we work around this here. */
int8_t twi_read(uint8_t addr, uint8_t reg, uint8_t *val)
{
    int8_t rc;
    uint8_t len = 0;
    uint8_t val_buf[2] = {0}; /* workaround for garbage byte */

    ASSERT(val);

    rc = twi_point(addr, reg);
    if (rc != NRK_OK) {
        LOG("ERROR: failed to set read ptr\r\n");
        return rc;
    }

    msg_buf[len++] = (addr << TWI_ADR_BITS) | (1 << TWI_READ_BIT);

    /* The example in the datasheet (and in sparkfun) sends the
     * count after the read req, but this seems non-standard. It
     * does not seem to make any difference. The auto-incremented
     * reads do not work in either case. */
    /* msg_buf[len++] = count; */

    rc = twi_tx(msg_buf, len);
    if (rc != NRK_OK) {
        LOG("ERROR: read req failed\r\n");
        return rc;
    }

    /* We want only one byte, but an extra byte is returned, hence len = 2 */
    rc = TWI_Get_Data_From_Transceiver( val_buf , 2 );
    if (!rc) {
        LOG("ERROR: failed to read data\r\n");
        return NRK_ERROR;
    }

    *val = val_buf[1];
    LOG("read: "); LOGP("0x%x \r\n", *val);

    return NRK_OK;
}

int8_t cmd_twi(uint8_t argc, char **argv)
{
    int8_t rc;
    char op;
    uint8_t addr, reg, val;

    if (!(argc == 4 || argc == 5)) {
        OUT("usage: twi r|w <addr> <reg> [<val>]\r\n");
        return NRK_ERROR;
    }

    op = argv[1][0];
    addr = strtol(argv[2], NULL, 0);
    reg = strtol(argv[3], NULL, 0);

    switch (op) {
        case 'r': /* read */
            rc = twi_read(addr, reg, &val);
            if (rc == NRK_OK) {
                OUTP("0x%x ", val);
                OUT("\r\n");
            }
            break;
        case 'w':
            if (argc != 5) {
                OUT("invalid cmd args\r\n");
                return NRK_ERROR;
            }
            val = strtol(argv[4], NULL, 0);
            rc = twi_write(addr, reg, val);
            break;
        default:
            OUT("ERROR: invalid op\r\n");
            return NRK_ERROR;
    }
    return rc;
}

uint8_t init_twi(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    TWI_Master_Initialise();

    ASSERT(num_tasks == NUM_TASKS_TWI);
    return num_tasks;
}
