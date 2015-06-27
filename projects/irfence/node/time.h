#ifndef TIME_H
#define TIME_H

#include <nrk.h>

#define MS_IN_S  1000
#define NS_IN_MS 1000000

#define TIME_TO_MS(t) ((t).secs * MS_IN_S + (t).nano_secs / NS_IN_MS)
#define MS_TO_TIME(t, ms) \
    do { \
        (t).secs = ms / MS_IN_S; \
        (t).nano_secs = (ms % MS_IN_S) * NS_IN_MS; \
    } while (0)

#define IS_VALID_TIME(t) ((t).secs != 0 || (t).nano_secs != 0)
#define TIME_CLEAR(t) do { (t).secs = (t).nano_secs = 0; } while (0)

int8_t time_cmp(nrk_time_t *time_a, nrk_time_t *time_b);
void choose_delay(nrk_time_t *delay, nrk_time_t *max_delay);

#ifdef ENABLE_TIME_CONV
int8_t cmd_cnvtime(uint8_t argc, char **argv);
#endif

#endif // TIME_H
