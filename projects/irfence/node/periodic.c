#include <nrk.h>
#include <nrk_error.h>
#include <avr/pgmspace.h>
#include <stdlib.h>

#include "cfg.h"
#include "output.h"
#include "config.h"
#include "time.h"

#include "periodic.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_PERIODIC

static nrk_task_type PERIODIC_TASK;
static NRK_STK periodic_task_stack[STACKSIZE_PERIODIC];

static periodic_func_t **functions;

static nrk_sig_t func_signal;

static void periodic_task()
{
    nrk_sig_mask_t wait_mask, func_wait_mask;
    nrk_time_t next_event, func_next_event;
    periodic_func_t **funcp;
    periodic_func_t *func;
    nrk_time_t now, sleep_time;
    int8_t rc;

    funcp = &functions[0];
    while (*funcp) {
        func = *funcp;
        LOG("init: "); LOGF(func->name); LOGNL();
        if (func->init)
            func->init();
        funcp++;
    }

    rc = nrk_signal_register(func_signal);
    if (rc == NRK_ERROR)
        ABORT("reg sig: func\r\n");

    while (1) {

        LOG("awake\r\n");

        TIME_CLEAR(next_event);

        wait_mask = SIG(func_signal);

        funcp = &functions[0];
        while (*funcp) {
            func = *funcp;

            TIME_CLEAR(func_next_event);
            func_wait_mask = 0;

            if (func->enabled ||
                func->enabled != func->last_enabled) {

                LOG("proc: "); LOGF(func->name); LOGNL();
                ASSERT(func->proc);

                func->proc(func->enabled,
                           &func_next_event, &func_wait_mask);
            }
            func->last_enabled = func->enabled;

            wait_mask |= func_wait_mask;
            if (IS_VALID_TIME(func_next_event) &&
                (!IS_VALID_TIME(next_event) ||
                 time_cmp(&func_next_event, &next_event) < 0)) {
                next_event = func_next_event;
            }

            funcp++;
        }

        if (IS_VALID_TIME(next_event)) {
            nrk_time_get(&now);
            rc = nrk_time_sub(&sleep_time, next_event, now);
            if (rc != NRK_OK) {
                LOG("next event in the past\r\n");
                continue;
            }
            LOG("sleeping for: ");
            LOGP("%lu ms\r\n", TIME_TO_MS(sleep_time));
            nrk_set_next_wakeup(sleep_time);
            wait_mask |= SIG(nrk_wakeup_signal);
        }
        LOG("waiting\r\n");
        nrk_event_wait( wait_mask );
    }

    ABORT("periodic task exited\r\n");
}

int8_t cmd_periodic(uint8_t argc, char **argv)
{
    periodic_func_t **funcp;
    periodic_func_t *func = NULL;
    bool enable;
    int8_t rc;

    if (argc != 1 && argc < 3) {
        OUT("usage: periodic [<func> +|- [<args>...]]\r\n");
        return NRK_ERROR;
    }
    
    funcp = &functions[0];
    while (*funcp) {
        if (argc == 1) {
            OUTF((*funcp)->name); OUT(" ");
        } else {
            if (!strcmp_P(argv[1], (*funcp)->name))
                break;
        }
        funcp++;
    }

    if (argc == 1) {
        OUT("\r\n");
        return NRK_OK;
    }

    if (!*funcp) {
        OUT("ERROR: func not found\r\n");
        return NRK_ERROR;
    }
    func = *funcp;

    enable = argv[2][0] == '+';

    if (enable && func->config) {
        rc = func->config(argc - 3, argc >= 3 ? &argv[3] : NULL);
        if (rc != NRK_OK) {
            LOG("config failed: func "); LOGF(func->name); LOGNL();
            return NRK_ERROR;
        }
    }
    func->enabled = enable;
    nrk_event_signal(func_signal);

    LOG("func enabled: "); LOGF(func->name);
    LOGP(" %d", func->enabled);
    LOGNL();

    return NRK_OK;
}

uint8_t init_periodic(uint8_t priority, periodic_func_t **funcs)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    func_signal = nrk_signal_create();
    if (func_signal == NRK_ERROR)
        ABORT("create sig: func\r\n");

    functions = funcs;

    num_tasks++;
    PERIODIC_TASK.task = periodic_task;
    PERIODIC_TASK.Ptos = (void *) &periodic_task_stack[STACKSIZE_PERIODIC - 1];
    PERIODIC_TASK.Pbos = (void *) &periodic_task_stack[0];
    PERIODIC_TASK.prio = priority;
    PERIODIC_TASK.FirstActivation = TRUE;
    PERIODIC_TASK.Type = BASIC_TASK;
    PERIODIC_TASK.SchType = PREEMPTIVE;
    PERIODIC_TASK.period.secs = 0;
    PERIODIC_TASK.period.nano_secs = 0;
    PERIODIC_TASK.cpu_reserve.secs = 0;
    PERIODIC_TASK.cpu_reserve.nano_secs = 0;
    PERIODIC_TASK.offset.secs = 0;
    PERIODIC_TASK.offset.nano_secs = 0;
    nrk_activate_task (&PERIODIC_TASK);

    return num_tasks;
}


