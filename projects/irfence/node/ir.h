#ifndef IR_H
#define IR_H

#define IR_ALL_RECEIVERS (~0)

extern nrk_sig_t ir_rcv_signal;

uint8_t init_ir(uint8_t priority);

void ir_led_on(uint8_t led);
void ir_led_off();
void ir_arm(uint8_t rcvers);
void ir_disarm(uint8_t rcvers);
uint8_t ir_rcv_state(uint8_t rcvers);

int8_t cmd_irled(uint8_t argc, char **argv);
int8_t cmd_irrcv(uint8_t argc, char **argv);
int8_t cmd_irfreq(uint8_t argc, char **argv);
#endif
