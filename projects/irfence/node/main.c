#include <nrk.h>
#include <include.h>
#include <stdlib.h>
#include <nrk_error.h>
#include <nrk_watchdog.h>
#include <bmac.h>

#include "cfg.h"
#include "config.h"
#include "node_id.h"
#include "output.h"
#include "time.h"
#include "led.h"
#include "command.h"
#include "random.h"
#include "rpc.h"

#if ENABLE_CONSOLE
#include "console.h"
#endif
#if ENABLE_RCMD
#include "rcmd.h"
#endif
#if ENABLE_WATCHDOG
#include "watchdog.h"
#endif
#if ENABLE_RXTX
#include "rxtx.h"
#endif
#if ENABLE_ROUTER
#include "router.h"
#endif
#if ENABLE_RFTOP
#include "rftop.h"
#endif
#if ENABLE_MPING
#include "mping.h"
#endif
#if ENABLE_RPING
#include "rping.h"
#endif
#if ENABLE_TWI
#include "twi.h"
#endif
#if ENABLE_COMPASS
#include "compass.h"
#endif
#if ENABLE_IR
#include "ir.h"
#endif
#if ENABLE_IRTOP
#include "irtop.h"
#endif
#if ENABLE_BEAM
#include "beam.h"
#endif
#if ENABLE_FENCE
#include "fence.h"
#endif
#if ENABLE_AUTOFENCE
#include "autofence.h"
#endif
#if ENABLE_LOCALIZATION
#include "localization.h"
#endif
#if ENABLE_POSITION
#include "position.h"
#endif
#if ENABLE_PERIODIC
#include "periodic.h"
#endif
#if ENABLE_BLINKER
#include "blinker.h"
#endif

static nrk_task_type MAIN_TASK;
static NRK_STK main_task_stack[STACKSIZE_MAIN];

static void main_task()
{
    nrk_sig_mask_t wait_mask;
    nrk_time_t next_event, now, sleep_time;
    int8_t rc;

#if ENABLE_RCMD
    rpc_activate_endpoint(&rcmd_endpoint);
#endif

#if ENABLE_MPING
    mping_init_server();
#endif

#if ENABLE_RPING
    rpc_activate_endpoint(&rping_endpoint);
#endif

#if ENABLE_COMPASS_RPC
    rpc_activate_endpoint(&compass_endpoint);
#endif

    while (1) {
        wait_mask = 0;
        TIME_CLEAR(next_event);

#if ENABLE_RCMD
        rpc_serve(&rcmd_endpoint.server);
        wait_mask |= rpc_wait_mask(&rcmd_endpoint.server);
#endif

#if ENABLE_MPING
        wait_mask |= mping_serve(&next_event);
#endif

#if ENABLE_RPING
        rpc_serve(&rping_endpoint.server);
        wait_mask |= rpc_wait_mask(&rping_endpoint.server);
#endif

#if ENABLE_COMPASS_RPC
        rpc_serve(&compass_endpoint.server);
        wait_mask |= rpc_wait_mask(&compass_endpoint.server);
#endif

        if (IS_VALID_TIME(next_event)) {
            nrk_time_get(&now);
            rc = nrk_time_sub(&sleep_time, next_event, now);
            if (rc == NRK_OK) {
                LOG("task: sleep for ");
                LOGP("%lu ms\r\n", TIME_TO_MS(sleep_time));

                nrk_set_next_wakeup(sleep_time);
                wait_mask |= SIG(nrk_wakeup_signal);
            } else {
                LOG("task: next event has passed\r\n");
            }
        }

        if (!wait_mask)
            wait_mask |= SIG(nrk_wakeup_signal);

        ASSERT(wait_mask);
        nrk_event_wait(wait_mask);
    }
    ABORT("main task exited\r\n");
}


static uint8_t init_main(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    num_tasks++;
    MAIN_TASK.task = main_task;
    MAIN_TASK.Ptos = (void *) &main_task_stack[STACKSIZE_MAIN - 1];
    MAIN_TASK.Pbos = (void *) &main_task_stack[0];
    MAIN_TASK.prio = priority;
    MAIN_TASK.FirstActivation = TRUE;
    MAIN_TASK.Type = BASIC_TASK;
    MAIN_TASK.SchType = PREEMPTIVE;
    MAIN_TASK.period.secs = 10; /* dummy period, to not run
                                   continuouasly when no subtasks
                                   modules are linked in */
    MAIN_TASK.period.nano_secs = 0;
    MAIN_TASK.cpu_reserve.secs = 0;
    MAIN_TASK.cpu_reserve.nano_secs = 0;
    MAIN_TASK.offset.secs = 0;
    MAIN_TASK.offset.nano_secs = 0;
    nrk_activate_task (&MAIN_TASK);

    return num_tasks;
}

static int8_t cmd_echo(uint8_t argc, char **argv)
{
    uint8_t i;
    OUT("echo args: ");
    for (i = 0; i < argc; ++i)
        OUTP("'%s' ", argv[i]);
    OUT("\r\n");
    return NRK_OK;
}

static int8_t cmd_reset(uint8_t argc, char **argv)
{
    nrk_watchdog_enable();
    nrk_halt();
    return NRK_OK;
}

#if ENABLE_PERIODIC
static periodic_func_t *periodic_funcs[] = {
#if ENABLE_BLINKER
    &func_blinker,
#endif
#if ENABLE_MPING
    &func_mping,
#endif
#if ENABLE_COMPASS
    &func_heading,
#endif
    NULL
};
#endif

static const cmd_t cmds[] PROGMEM = {
    { "echo", "print the given args", &cmd_echo},
    { "log", "choose log categories", &cmd_log},
    { "reset", "halt OS and reset", &cmd_reset},

#if ENABLE_LED
    { "clrled", "turn off all LEDs", &cmd_clrled},
    { "setled", "turn on an LED for a duration", &cmd_setled},
#endif

#if ENABLE_TIME_CONV
    { "cnvtime", "convert time to/from ms", &cmd_cnvtime},
#endif

#if ENABLE_PERIODIC
    { "periodic", "toggle periodic functions", &cmd_periodic},
#endif

#if ENABLE_RCMD
    { "rcmd", "execute a cmd on a remote node", &cmd_rcmd},
#endif

    /* Config cmds */
    { "set", "set option value", &cmd_set},
    { "get", "get option value", &cmd_get},
    { "save", "save options to eeprom", &cmd_save},
    { "load", "load options from eeprom", &cmd_load},

    /* RX/TX cmds */
#if ENABLE_RXTX
    { "top", "manipulate RF topology mask", &cmd_top},
#endif

#if ENABLE_WATCHDOG
    /* Watchdog cmds */
    { "watchdog", "enable/disable watchdog task", &cmd_watchdog},
#endif

    /* Router cmds */
#if ENABLE_ROUTER
    { "hood", "list rf neighborhood", &cmd_hood},
    { "route", "show/change routing table", &cmd_route},
    { "ping", "send a ping", &cmd_ping},
#endif
#if ENABLE_RFTOP
    { "discover", "initiate route discovery", &cmd_discover},
    { "probe", "discover network topology graph", &cmd_probe},
    { "rftop", "print or edit RF network topology graph", &cmd_rftop},
    { "calc-routes", "calculate routing tables", &cmd_calc_routes},
    { "bc-routes", "broadcast routes", &cmd_bc_routes},
    { "set-routes", "apply discovered routes", &cmd_set_routes},
#endif

    /* Ping app cmds */
#if ENABLE_MPING
    { "mping", "send a ping msg", &cmd_mping},
#endif
#if ENABLE_RPING
    { "rping", "call ping rpc", &cmd_rping},
#endif

    /* TWI/I2C cmds */ 
#if ENABLE_TWI
    { "twi", "transact over the TWI/I2C bus", &cmd_twi},
#endif

    /* Compass cmds */
#if ENABLE_COMPASS
    { "head", "show compass heading", &cmd_head},
#endif

    /* Localizatoin cmds */
#if ENABLE_LOCALIZATION
    { "irledangle", "get absolute angle of led", &cmd_irledangle},
    { "dist", "get distance to node", &cmd_dist},
#endif
#if ENABLE_POSITION
    { "localize", "find (x, y) locations for nodes", &cmd_localize},
    { "loc", "show or set (x, y) node locations", &cmd_loc},
    { "mapdim", "show or set map dimensions", &cmd_mapdim},
#endif

    /* IR cmds */
#if ENABLE_IR
    { "irled", "turn an IR led on/off", &cmd_irled},
    { "irrcv", "get the signal from an IR rcver", &cmd_irrcv},
    { "irfreq", "change freq of IR PWM", &cmd_irfreq},
#endif
#if ENABLE_IRTOP
    { "irhood", "show IR neighborhood", &cmd_irhood},
    { "irtop", "show IR topology", &cmd_irtop},
    { "irdiscover", "discover IR topology", &cmd_irdiscover},
    { "irprobe", "probe IR neighbors", &cmd_irprobe},
#endif

    /* Beam cmds */
#if ENABLE_BEAM
    { "beam", "form an IR link with a neighbor", &cmd_beam},
#endif

    /* Fence cmds */
#if ENABLE_FENCE
    { "fence", "form fence out of IR beams", &cmd_fence},
#endif
#if ENABLE_AUTOFENCE
    { "mfence", "calculate max area fence", &cmd_mfence},
#endif

    { {0} , {0} , NULL }
};

int main ()
{
    uint8_t prio = NRK_MAX_TASKS;

    /* On watchdog reboot, the watchdog is forced to be enabled */
    nrk_watchdog_disable();

    nrk_setup_ports ();
    nrk_setup_uart (UART_BAUDRATE_115K2);

    nrk_gpio_clr(NRK_BUTTON);
    nrk_gpio_direction(NRK_BUTTON, NRK_PIN_INPUT);
    PORTD |= 1 << 1; /* enable pull-up */

    nrk_init ();

    nrk_time_set (0, 0);

    init_config ();
    init_random();

    bmac_task_config ();

    LOG("init modules... max tasks ");
    LOGP("%u\r\n", prio);

    /* Tasks in order of priority: from highest to lowest priority */

#if ENABLE_WATCHDOG
    prio -= init_watchdog(prio);
#endif
#if ENABLE_COMMAND
    prio -= init_command(prio, cmds);
#endif

#if ENABLE_CONSOLE
    prio -= init_console(prio);
#endif

#if ENABLE_RXTX
    prio -= init_rxtx(prio);
#endif
#if ENABLE_ROUTER
    prio -= init_router(prio);
#endif
#if ENABLE_RFTOP
    prio -= init_rftop(prio);
#endif

#if ENABLE_RCMD
    prio -= init_rcmd(prio);
#endif

#if ENABLE_TWI
    prio -= init_twi(prio);
#endif
#if ENABLE_COMPASS
    prio -= init_compass(prio);
#endif

#if ENABLE_IR
    prio -= init_ir(prio);
#endif
#if ENABLE_IRTOP
    prio -= init_irtop(prio);
#endif /* ENABLE_IRTOP */
#if ENABLE_BEAM
    prio -= init_beam(prio);
#endif /* ENABLE_BEAM */
#if ENABLE_FENCE
    prio -= init_fence(prio);
#endif /* ENABLE_FENCE */
#if ENABLE_MPING
    prio -= init_mping(prio);
#endif
#if ENABLE_RPING
    prio -= init_rping(prio);
#endif

#if ENABLE_LED
    prio -= init_led(prio);
#endif

#if ENABLE_PERIODIC
    prio -= init_periodic(prio, periodic_funcs);
#endif

    prio -= init_main(prio);

    LOG("created tasks: "); LOGP("%u\r\n", NRK_MAX_TASKS - prio);

    nrk_start ();

    return 0;
}

