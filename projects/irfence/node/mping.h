#ifndef MPING_H
#define MPING_H

#include <nrk.h>

#include "periodic.h"

extern periodic_func_t func_mping;

uint8_t init_mping(uint8_t prio);

void mping_init_server();
nrk_sig_mask_t mping_serve();

int8_t cmd_mping(uint8_t argc, char **argv);
int8_t cmd_pmping(uint8_t argc, char **argv);

#endif
