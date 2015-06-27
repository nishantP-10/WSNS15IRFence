#include <nrk.h>
#include <nrk_error.h>
#include <nrk_driver_list.h>
#include <nrk_driver.h>
#include <adc_driver.h>

#include <stdlib.h>

#include "cfg.h"
#include "output.h"

#include "random.h"

void seed_rand()
{
    int8_t fd;
    int8_t rc;
    uint16_t val = 0;
    uint8_t chan;
    uint16_t seed = 0;
    uint8_t count = 0;

    fd = nrk_open(ADC_DEV_MANAGER, READ);
    if (fd == NRK_ERROR)
        ABORT("failed to open ADC\r\n");

    do {
        for (chan = 0; chan < NUM_ADC_CHANS; ++chan) {
            rc = nrk_set_status(fd, ADC_CHAN, chan);
            if (rc == NRK_ERROR)
                ABORT("failed to set ADC chan\r\n");

            rc = nrk_read(fd, (uint8_t*)&val, 2);
            if (rc == NRK_ERROR)
                ABORT("failed to read ADC\r\n");

            seed ^= val;
        }
    } while (++count < RANDOM_SEED_ADC_READS);

    fd = nrk_close(fd);
    if (fd == NRK_ERROR)
        ABORT("failed to close ADC\r\n");

    LOG("rand seed: "); LOGP("%u", seed); LOGNL();
    srand(seed);
}

void init_random()
{
    int8_t rc;

    rc = nrk_register_driver(&dev_manager_adc, ADC_DEV_MANAGER);
    if(rc == NRK_ERROR)
        ABORT("failed to reg ADC driver\r\n");
}

