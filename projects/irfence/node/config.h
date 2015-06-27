#ifndef CONFIG_H
#define CONFIG_H

#include "node_id.h"

extern uint16_t logcat;

extern node_id_t this_node_id;
extern bool is_gateway;
extern uint8_t rf_chan;
extern uint8_t rf_power;
extern uint8_t rssi_thres;
extern int8_t chan_clear_thres;

extern uint8_t topology_mask;

extern bool auto_discover;
extern uint8_t discover_period_s;
extern nrk_time_t discover_time_out;
extern nrk_time_t discover_req_delay;
extern uint8_t route_broadcast_attempts;
extern uint8_t discover_send_attempts;

extern bool heal_routes;

extern uint8_t rssi_dist_intercept;
extern uint8_t rssi_dist_slope_inverse;

extern uint8_t rssi_avg_count;

extern nrk_time_t pkt_ack_timeout;
extern nrk_time_t rx_queue_slot_wait;
extern nrk_time_t tx_msg_retry_delay;
extern nrk_time_t pong_delay;
extern nrk_time_t ping_period;
extern nrk_time_t potato_hold_time;
extern nrk_time_t bmac_rx_check_rate;
extern nrk_time_t beam_rpc_time_out;
extern nrk_time_t beam_sense_time;
extern nrk_time_t fence_rpc_time_out;
extern nrk_time_t ping_time_out;
extern nrk_time_t rcmd_rpc_time_out;
extern nrk_time_t ir_probe_time;
extern nrk_time_t ir_direction_time;
extern nrk_time_t irtop_rpc_timeout;
extern nrk_time_t twi_tx_poll_interval;
extern nrk_time_t twi_tx_timeout;
extern nrk_time_t compass_measure_timeout;
extern nrk_time_t compass_poll_interval;
extern nrk_time_t compass_rpc_time_out;

extern uint8_t ir_carrier_freq_khz;
extern uint8_t ir_pulse_duty_cycle;
extern uint8_t ir_pulse_ticks;

extern uint8_t ir_probe_broadcasts;

extern nrk_time_t led_pulse_time;
extern nrk_time_t warn_led_on_time;

extern uint8_t led_sent_pkt;
extern uint8_t led_received_pkt;
extern uint8_t led_awaiting_pong;
extern uint8_t led_proc_ping;
extern uint8_t led_holding_potato;
extern uint8_t led_discover;
extern uint8_t led_awaiting_beam;

void init_config();

int8_t cmd_set(uint8_t argc, char **argv);
int8_t cmd_get(uint8_t argc, char **argv);
int8_t cmd_save(uint8_t argc, char **argv);
int8_t cmd_load(uint8_t argc, char **argv);

#endif // CONFIG_H
