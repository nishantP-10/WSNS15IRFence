#ifndef _COMMAND_H
#define _COMMAND_H

#define MAX_CMD_WORD_LEN  16
#define MAX_CMD_HELP_LEN 128

typedef int8_t (*cmd_handler_t)(uint8_t argc, char **argv);

typedef struct {
    const char word[MAX_CMD_WORD_LEN];
    const char help[MAX_CMD_HELP_LEN];
    cmd_handler_t handler;
} cmd_t;

uint8_t parse_args(char *cmd, char **argv, uint8_t size);
const cmd_handler_t lookup_cmd_handler(char *name);
void show_usage();
uint8_t init_command(uint8_t priority, const cmd_t *cmds);

#endif
