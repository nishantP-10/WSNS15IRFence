#include <nrk.h>
#include <avr/pgmspace.h>

#include "output.h"

#include "command.h"

#define ARG_DELIM " "

static const cmd_t *commands;

uint8_t parse_args(char *cmd, char **argv, uint8_t size)
{
    char *arg;
    uint8_t argc = 0;
    argv[argc++] = strtok(cmd, ARG_DELIM);
    while (argc < size && (arg = strtok(NULL, ARG_DELIM)))
       argv[argc++] = arg;
    return argc;
}

const cmd_handler_t lookup_cmd_handler(char *name)
{
    const cmd_t *cmd_def = &commands[0];
    while (pgm_read_word(&cmd_def->handler) &&
           strcmp_P(name, cmd_def->word))
        cmd_def++;

    return (cmd_handler_t)pgm_read_word(&cmd_def->handler);
}

void show_usage()
{
    const cmd_t *cmd_def = &commands[0];
    while (pgm_read_word(&cmd_def->handler)) {
        OUTF(cmd_def->word);
        OUT(": ");
        OUTF(cmd_def->help);
        OUT("\r\n");
        cmd_def++;
    }
}

uint8_t init_command(uint8_t priority, const cmd_t *cmds)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);
    commands = cmds;
    ASSERT(num_tasks == NUM_TASKS_COMMAND);
    return num_tasks;
}
