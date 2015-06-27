#ifndef _RCMD_H_
#define _RCMD_H_

uint8_t init_rcmd(uint8_t priority);

int8_t cmd_rcmd(uint8_t argc, char **argv);

extern rpc_endpoint_t rcmd_endpoint;

#endif
