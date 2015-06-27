#include <nrk.h>
#include <stdlib.h>

#include "output.h"

#include "time.h"

int8_t time_cmp(nrk_time_t *time_a, nrk_time_t *time_b)
{
    if (time_a->secs < time_b->secs)
        return -1;
    if (time_a->secs > time_b->secs)
        return 1;
    if (time_a->nano_secs < time_b->nano_secs)
        return -1;
    if (time_a->nano_secs > time_b->nano_secs)
        return 1;
    return 0;
}

void choose_delay(nrk_time_t *delay, nrk_time_t *max_delay)
{
    /* Make roughly twice as many parts as nodes */
    const uint8_t part_denom = 100; /* doesn't matter too much */
    const uint8_t part_numerator = part_denom / (2 * MAX_NODES);

    uint16_t delay_ms;

    /* Pick a random fraction of the specified delay */
    delay_ms = TIME_TO_MS(*max_delay);
    delay_ms = (delay_ms * part_numerator / part_denom) *
               (rand() % (part_denom / part_numerator + 1));
    MS_TO_TIME(*delay, delay_ms);
}

#if ENABLE_TIME_CONV
int8_t cmd_cnvtime(uint8_t argc, char **argv)
{
    nrk_time_t time;
    uint32_t ms;
    char to_unit;
    char *sec, *nsec;

    if (argc != 3) {
        OUT("usage: cnvtime t|m <value>\r\n");
        return NRK_ERROR;
    }
    to_unit = argv[1][0];

    switch (to_unit) {
        case 't':
            ms = atol(argv[2]);
            MS_TO_TIME(time, ms);
            OUTP("%lu = ", ms);
            OUTP("%lu:%lu\r\n", time.secs, time.nano_secs);
            break;
        case 'm':
            sec = argv[2];
            nsec = strchr(sec, ':');
            if (nsec == NULL) {
                OUT("invalid time value: expecting s:ns\r\n");
                return;
            }
            *nsec = '\0';
            nsec++;
            time.secs = atol(sec);
            time.nano_secs = atol(nsec);
            ms = TIME_TO_MS(time);
            OUTP("%lu:%lu = ", time.secs, time.nano_secs);
            OUTP("%lu ms\r\n", ms);
            break;
        default:
            OUT("unknown unit\r\n");
            return NRK_ERROR;
    }
    return NRK_OK;
}
#endif
