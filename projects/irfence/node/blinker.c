#include <nrk.h>
#include <nrk_error.h>
#include <nrk_time.h>
#include <stdlib.h>

#include "cfg.h"
#include "output.h"
#include "config.h"

#include "blinker.h"

static const char blinker_name[] PROGMEM = "blinker";

static nrk_time_t blinker_period = { 1, 0 * NANOS_PER_MS };

static uint8_t blinker_led = LED_ORANGE;
static bool blinker_led_on;

static int8_t blinker_config(uint8_t argc, char **argv)
{
    if (!(argc == 0 || argc == 1)) {
        OUT("usage: blinker [<led>]\r\n");
        return NRK_ERROR;
    }
    
    if (argc == 1)
        blinker_led = atoi(argv[0]);

    return NRK_OK;
}

static void blinker_process(bool enabled, nrk_time_t *next_event,
                            nrk_sig_mask_t *wait_mask)
{
    nrk_time_t now;

    if (enabled) {
        nrk_led_toggle(blinker_led);
        blinker_led_on = !blinker_led_on;

        nrk_time_get(&now);
        nrk_time_add(next_event, now, blinker_period);
    } else {
        nrk_led_clr(blinker_led);
    }
}

periodic_func_t func_blinker = {
    .name = blinker_name,
    .proc = blinker_process,
    .config = blinker_config,
};
