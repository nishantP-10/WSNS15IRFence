#ifndef RPING_H
#define RPING_H

#include "rpc.h"

uint8_t init_rping(uint8_t priority);

int8_t cmd_rping(uint8_t argc, char **argv);

extern rpc_endpoint_t rping_endpoint;

#endif
