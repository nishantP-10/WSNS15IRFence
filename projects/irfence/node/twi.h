#ifndef _TWI_H_
#define _TWI_H_

uint8_t init_twi(uint8_t priority);
int8_t twi_write(uint8_t addr, uint8_t reg, uint8_t val);
int8_t twi_read(uint8_t addr, uint8_t reg, uint8_t *val);


int8_t cmd_twi(uint8_t argc, char **argv);

#endif

