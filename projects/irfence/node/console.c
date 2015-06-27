#include <nrk.h>
#include <include.h>
#include <nrk_error.h>
#include <ulib.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

#include "cfg.h"
#include "output.h"
#include "command.h"

#include "console.h"

#ifndef NRK_NO_POWER_DOWN
#error Console requires NKR_NO_POWER_DOWN in nrk_cfg.h
#endif

#define CHAR_CTRL_C 0x03
#define CHAR_BACKSPACE 0x8

/* This is what GNU screen sends. YMMV. */
#define CHAR_ESCAPE_1 0x1b
#define CHAR_ESCAPE_2 0x5b
#define CHAR_UP_ARROW 0x1b5b41
#define CHAR_DOWN_ARROW 0x1b5b42

#define PROMPT "> "

static nrk_task_type CONSOLE_TASK;
static NRK_STK console_task_stack[STACKSIZE_CONSOLE];

static char cmd[MAX_CMD_LEN];
static char prev_cmd[MAX_CMD_LEN];
static char *argv[MAX_ARGS]; /* array of ptrs into 'cmd' */

static void console_task()
{
    char ch_byte;
    uint32_t ch; /* holds multi-byte characters */
    bool multibyte_part;
    uint8_t len;
    bool cmd_ready;
    bool at_prev_cmd;
    nrk_sig_t uart_rx_signal;
    uint8_t argc;
    int8_t rc;
    cmd_handler_t handler;
    const nrk_time_t startup_delay = { 2, 0 };

    uart_rx_signal = nrk_uart_rx_signal_get();
    rc = nrk_signal_register(uart_rx_signal);
    if (rc != NRK_OK)
        ABORT("failed to register for UART rx signal\r\n");

    nrk_wait(startup_delay); /* to let the init output settle */

    while (1) {
        OUTP("%u%s", this_node_id, PROMPT);

        /* Read command */
        cmd_ready = false;
        len = 0;
        memset(&cmd, 0, sizeof(cmd));
        ch = 0;
        multibyte_part = false;
        at_prev_cmd = false;
        do {
            /* Wait for some input */
            while (!nrk_uart_data_ready(NRK_DEFAULT_UART)) {
                nrk_event_wait(SIG(uart_rx_signal));
            }

            /* Read whatever chars are available so far */
            while (nrk_uart_data_ready(NRK_DEFAULT_UART) && !cmd_ready) {
                ch_byte = getchar();
                ch = multibyte_part ? ((ch << 8) | ch_byte) : ch_byte;

                if (ch_byte == CHAR_ESCAPE_1 || ch_byte == CHAR_ESCAPE_2) {
                    multibyte_part = true;
                    break; /* get the remaining bytes of multi-byte char */
                } else {
                    multibyte_part = false;
                }

                if (ch == '\r' || ch == '\n' || ch == CHAR_CTRL_C) {
                    putchar(ch); /* echo for user */
                    cmd_ready = true;
                    if (ch == CHAR_CTRL_C) {
                        putchar('\r');
                        len = 0; /* means cmd was aborted */
                    }

                    /* tty-specific oddities */
#ifdef NO_TERM /* plain 'cat' */
                    if (ch == '\r')
                        putchar('\n');
#else
                    putchar('\n');
#endif
                } else if (ch == CHAR_UP_ARROW) {
                    if (!at_prev_cmd) {
                        strcpy(cmd, prev_cmd);
                        len = strlen(cmd);
                        OUTP("%s", prev_cmd);
                        at_prev_cmd = true;
                    }
                } else if (ch == CHAR_DOWN_ARROW) {
                    if (at_prev_cmd) {
                        memset(cmd, ' ', len);
                        OUTP("\r%u%s%s\r%u%s", this_node_id, PROMPT, cmd,
                                             this_node_id, PROMPT);
                        len = 0;
                        at_prev_cmd = false;
                    }
                } else if (ch == CHAR_BACKSPACE) {
                    if (len > 0) {
                        putchar(ch);
                        putchar(' ');
                        putchar(ch);
                        len--;
                    }
                } else {
                    putchar(ch); /* echo for user */
                    cmd[len++] = ch;
                }
            }
        } while (!cmd_ready);

        /* Process cmd */
        if (len == 0) /* aborted with ctrl-c */
            continue;

        cmd[len] = '\0';

        strcpy(prev_cmd, cmd);

        argc = parse_args(cmd, argv, MAX_ARGS);
        ASSERT(argc > 0);

        handler = lookup_cmd_handler(argv[0]);
        if (handler) {
            rc = handler(argc, argv);
            if (rc != NRK_OK)
                OUT("ERROR: cmd failed\r\n");
        } else if (!strcmp_P(argv[0], PSTR("help")) ||
                   !strcmp_P(argv[0], PSTR("?"))) {
            show_usage();
        } else {
            OUT("error: invalid cmd (try '?'): ");
            OUTP("'%s'\r\n", argv[0]);
        }
    }

    ABORT("console task exited\r\n");
}

uint8_t init_console(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    num_tasks++;
    CONSOLE_TASK.task = console_task;
    CONSOLE_TASK.Ptos = (void *) &console_task_stack[STACKSIZE_CONSOLE - 1];
    CONSOLE_TASK.Pbos = (void *) &console_task_stack[0];
    CONSOLE_TASK.prio = priority;
    CONSOLE_TASK.FirstActivation = TRUE;
    CONSOLE_TASK.Type = BASIC_TASK;
    CONSOLE_TASK.SchType = PREEMPTIVE;
    CONSOLE_TASK.period.secs = 0;
    CONSOLE_TASK.period.nano_secs = 0;
    CONSOLE_TASK.cpu_reserve.secs = 0;
    CONSOLE_TASK.cpu_reserve.nano_secs = 0;
    CONSOLE_TASK.offset.secs = 0;
    CONSOLE_TASK.offset.nano_secs = 0;
    nrk_activate_task(&CONSOLE_TASK);

    ASSERT(num_tasks == NUM_TASKS_CONSOLE);
    return num_tasks;
}
