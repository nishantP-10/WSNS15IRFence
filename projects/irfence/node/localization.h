#ifndef _LOCALIZATION_H_
#define _LOCALIZATION_H_

#include "node_id.h"

uint16_t get_led_angle(uint8_t led);
uint16_t get_distance(node_id_t node);

int8_t cmd_irledangle(uint8_t argc, char **argv);
int8_t cmd_dist(uint8_t argc, char **argv);

#endif
