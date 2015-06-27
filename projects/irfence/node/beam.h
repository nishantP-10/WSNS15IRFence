#ifndef BEAM_H
#define BEAM_H

extern nrk_sig_t beam_signal; /* fired when beam state changes */

uint8_t init_beam(uint8_t priority);
int8_t create_beam(node_id_t receiver);
int8_t destroy_beam();
bool get_beam_state();

int8_t cmd_beam(uint8_t argc, char **argv);

#endif
