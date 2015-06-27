#ifndef LED_H
#define LED_H

#include <nrk.h>
#include <nrk_time.h>

uint8_t init_led(uint8_t priority);
int8_t set_led(uint8_t led, nrk_time_t duration);
int8_t pulse_led(uint8_t led);

int8_t cmd_clrled(uint8_t argc, char **argv);
int8_t cmd_setled(uint8_t argc, char **argv);

#endif // LED_H
