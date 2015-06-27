#include <nrk.h>
#include <include.h>
#include <stdlib.h>
#include <nrk_error.h>
#include <nrk_time.h>

#include "cfg.h"
#include "config.h"
#include "node_id.h"
#include "output.h"
#include "time.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_LED

static nrk_task_type LED_TASK;
static NRK_STK led_task_stack[STACKSIZE_LED];

static nrk_sig_t led_signal;

static nrk_time_t led_off_time[NUM_LEDS];
static nrk_sem_t *led_off_time_sem;

static void clear_leds()
{
    uint8_t i;
    for (i = 0; i < NUM_LEDS; ++i)
        nrk_led_clr(i);
}

/* Turn on a LED for a given duration asynchronously */
int8_t set_led(uint8_t led, nrk_time_t duration)
{
    nrk_time_t now, off_time;

    nrk_time_get(&now);
    nrk_time_add(&off_time, now, duration);

    nrk_sem_pend(led_off_time_sem);
    if (!IS_VALID_TIME(led_off_time[led]) ||
        time_cmp(&off_time, &led_off_time[led]) > 0)
        led_off_time[led] = off_time;
    nrk_led_set(led);
    nrk_sem_post(led_off_time_sem);

    LOG("set led: "); LOGP("%u [%lu ms]\r\n", led, TIME_TO_MS(duration));

    /* Wakeup the task so that it re-calculates the time until next event */
    nrk_event_signal(led_signal);

    return NRK_OK;
}

int8_t pulse_led(uint8_t led)
{
    return set_led(led, led_pulse_time);
}

static void led_task()
{
    int8_t rc;
    uint8_t led;
    nrk_sig_mask_t wait_signal_mask;
    nrk_time_t now, time_until_event;
    nrk_time_t event, next_event;
    bool cleared;

    LOG("starting task\r\n");

    rc = nrk_signal_register(led_signal);
    if (rc == NRK_ERROR)
        ABORT("nrk_signal_register led signal\r\n");

    while (1) {
        
        nrk_time_get(&now);

        next_event.secs = next_event.nano_secs = 0;

        /* Clear any LEDs that are due to be cleared */
        for (led = 0; led < NUM_LEDS; ++led) {
            cleared = false;
            nrk_sem_pend(led_off_time_sem);
            event = led_off_time[led];
            if (IS_VALID_TIME(event)) {
                if (time_cmp(&event, &now) < 0) {
                    nrk_led_clr(led);
                    led_off_time[led].secs = led_off_time[led].nano_secs = 0;
                    cleared = true;
                }
            }
            nrk_sem_post(led_off_time_sem);

            if (cleared) {
                LOG("cleared led: "); LOGP("%u\r\n", led);
                event.secs = event.nano_secs = 0; /* don't affect next_event */
            }

            /* Next event can be outside of critical section because it can be
             * based on out of date data: in this case, the sleep will be until
             * led_signal instead of until the next event. */
            if (IS_VALID_TIME(event) && (!IS_VALID_TIME(next_event) ||
                time_cmp(&event, &next_event) < 0)) {
                next_event = event;
            }
        }

        LOG("next event: ");
        LOGP("%lu.%lu\r\n", next_event.secs, next_event.nano_secs);

        if (IS_VALID_TIME(next_event)) {
            nrk_time_sub(&time_until_event, next_event, now);
            LOG("sleep for: ");
            LOGP("%lu ms\r\n", TIME_TO_MS(time_until_event));
            nrk_set_next_wakeup(time_until_event);
        }

        /* Wait for either a new led set request or the next clear event */
        wait_signal_mask = SIG(led_signal);
        if (IS_VALID_TIME(next_event))
            wait_signal_mask |= SIG(nrk_wakeup_signal);
        nrk_event_wait(wait_signal_mask);

        LOG("awake\r\n");
    }

    ABORT("led task exited\r\n");
}

int8_t cmd_clrled(uint8_t argc, char **argv)
{
    clear_leds();
    return NRK_OK;
}

int8_t cmd_setled(uint8_t argc, char **argv)
{
    uint8_t led;
    nrk_time_t duration;

    if (argc != 3) {
        OUT("usage: setled <led> <duration_ms>\r\n");
        return NRK_ERROR;
    }

    led = atoi(argv[1]);
    MS_TO_TIME(duration, atol(argv[2]));

    return set_led(led, duration);
}

uint8_t init_led(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    led_signal = nrk_signal_create();
    if (led_signal == NRK_ERROR)
        ABORT("nrk_signal_create led event\r\n");

    led_off_time_sem = nrk_sem_create(1, NRK_MAX_TASKS);
    if (led_off_time_sem == NULL)
        ABORT("failed to create led time off sem\r\n");

    num_tasks++;
    LED_TASK.task = led_task;
    LED_TASK.Ptos = (void *) &led_task_stack[STACKSIZE_LED - 1];
    LED_TASK.Pbos = (void *) &led_task_stack[0];
    LED_TASK.prio = priority;
    LED_TASK.FirstActivation = TRUE;
    LED_TASK.Type = BASIC_TASK;
    LED_TASK.SchType = PREEMPTIVE;
    LED_TASK.period.secs = 0;
    LED_TASK.period.nano_secs = 0;
    LED_TASK.cpu_reserve.secs = 0;
    LED_TASK.cpu_reserve.nano_secs = 0;
    LED_TASK.offset.secs = 0;
    LED_TASK.offset.nano_secs = 0;
    nrk_activate_task (&LED_TASK);

    ASSERT(num_tasks == NUM_TASKS_LED);
    return num_tasks;
}

