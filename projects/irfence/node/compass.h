#ifndef _COMPASS_H_
#define _COMPASS_H_

#include "periodic.h"
#include "rpc.h"

uint8_t init_compass(uint8_t prio);
int8_t get_heading(int16_t *heading);

int8_t cmd_head(uint8_t argc, char **argv);

extern rpc_endpoint_t compass_endpoint;
extern periodic_func_t func_heading;

#endif
