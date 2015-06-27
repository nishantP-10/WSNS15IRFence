#include <nrk.h>
#include <include.h>
#include <nrk_error.h>
#include <nrk_eeprom.h>
#include <stdlib.h>

#include "cfg.h"
#include "output.h"
#include "options.h"

#include "router.h"
#if ENABLE_IRTOP
#include "irtop.h"
#endif
#if ENABLE_POSITION
#include "position.h"
#endif
#if ENABLE_RFTOP
#include "rftop.h"
#endif

#include "config.h"

/* The default values specified here will take effect when loading of options
 * from eeprom on bootup is disabled. */

uint16_t logcat = ~0x0;

uint8_t rf_chan;
node_id_t this_node_id;
bool is_gateway;
uint8_t rf_power = 0; /* 0 is max */
uint8_t rssi_thres = 0;
int8_t chan_clear_thres = -45;

/* Topolgy mask: links between nodes as a bitmask: bit set means no link */
uint8_t topology_mask;

bool auto_discover = false;
uint8_t discover_period_s = 30;  // a multiple of DISCOVER_TASK_PERIOD_S
nrk_time_t discover_time_out = {20, 0 * NANOS_PER_MS};
nrk_time_t discover_req_delay = {1, 0 * NANOS_PER_MS}; /* max */
uint8_t route_broadcast_attempts = 1;
uint8_t discover_send_attempts = 2;

bool heal_routes = false;

uint8_t rssi_dist_intercept = 60;
uint8_t rssi_dist_slope_inverse = 15;

uint8_t rssi_avg_count = 5;

nrk_time_t pkt_ack_timeout = {2, 0 * NANOS_PER_MS};
nrk_time_t rx_queue_slot_wait = {0, 100 * NANOS_PER_MS};
nrk_time_t tx_msg_retry_delay = {2, 0 * NANOS_PER_MS};
nrk_time_t pong_delay = {1, 0 * NANOS_PER_MS};
nrk_time_t ping_period = {2, 0 * NANOS_PER_MS};
nrk_time_t potato_hold_time = {2, 0 * NANOS_PER_MS};
nrk_time_t bmac_rx_check_rate = {0, 200 * NANOS_PER_MS};
nrk_time_t beam_rpc_time_out = {10, 0 * NANOS_PER_MS};
nrk_time_t beam_sense_time = {1, 0 * NANOS_PER_MS};
nrk_time_t fence_rpc_time_out = {10, 0 * NANOS_PER_MS};
nrk_time_t ping_time_out = {10, 0 * NANOS_PER_MS};
nrk_time_t rcmd_rpc_time_out = {10, 0 * NANOS_PER_MS};
nrk_time_t ir_probe_time = {2, 0 * NANOS_PER_MS};
nrk_time_t ir_direction_time = {5, 0 * NANOS_PER_MS};
nrk_time_t irtop_rpc_timeout = {2, 0 * NANOS_PER_MS};
nrk_time_t twi_tx_poll_interval = {0, 100 * NANOS_PER_MS};
nrk_time_t twi_tx_timeout = {0, 500 * NANOS_PER_MS};
nrk_time_t compass_measure_timeout = {0, 100 * NANOS_PER_MS};
nrk_time_t compass_poll_interval = {0, 10 * NANOS_PER_MS};
nrk_time_t compass_rpc_time_out = {4, 0 * NANOS_PER_MS};

uint8_t ir_carrier_freq_khz = 38;
uint8_t ir_pulse_duty_cycle = 75; /* % */
uint8_t ir_pulse_ticks = 4; /* GCD(100, ir_pulse_duty_cycle */
/* TODO: ir_pulse_period */

uint8_t ir_probe_broadcasts = 3;

nrk_time_t led_pulse_time = {0, 300 * NANOS_PER_MS};
nrk_time_t warn_led_on_time = {1, 0 * NANOS_PER_MS};

uint8_t led_sent_pkt = GREEN_LED;
uint8_t led_received_pkt = BLUE_LED;
uint8_t led_awaiting_pong = ORANGE_LED;
uint8_t led_proc_ping = ORANGE_LED;
uint8_t led_holding_potato = ORANGE_LED;
uint8_t led_discover = ORANGE_LED;
uint8_t led_awaiting_beam = ORANGE_LED;

/* EEPROM addresses: NRK uses 9 bytes. See firefly3/include/nrk_eeprom.h. We
 * start grabbing at 16. The addrs in the list is out of order due to legacy. */

static const option_t options[] PROGMEM = {
    /* name, type, eeprom addr, value ptr */
    { "rf_chan",      OPT_TYPE_UINT8, EE_CHANNEL, &rf_chan},
    { "node_id",      OPT_TYPE_UINT8, EE_MAC_ADDR_0, &this_node_id },
    { "is_gtw",       OPT_TYPE_BOOL,  19, &is_gateway},
    { "rf_power",     OPT_TYPE_UINT8, 16, &rf_power},
    { "rssi_thres",   OPT_TYPE_UINT8, 17, &rssi_thres},
    { "topology",     OPT_TYPE_UINT8, 18, &topology_mask},
    { "pkt_ack_timeout",  OPT_TYPE_TIME, 20 /* +2 */, &pkt_ack_timeout},
    { "pong_delay",       OPT_TYPE_TIME, 22 /* +2 */, &pong_delay},
    { "bmac_rx_check_rate", OPT_TYPE_TIME, 24 /* +2 */, &bmac_rx_check_rate},
    { "led_pulse_time",   OPT_TYPE_TIME, 26 /* +2 */, &led_pulse_time},
    { "warn_led_on_time", OPT_TYPE_TIME, 28 /* +2 */, &warn_led_on_time},
    { "auto_discover",    OPT_TYPE_BOOL, 30, &auto_discover},
    { "chan_clear_thres",    OPT_TYPE_INT8, 31, &chan_clear_thres},

    { "led_sent_pkt",     OPT_TYPE_UINT8, 32, &led_sent_pkt},
    { "led_awaiting_pong",    OPT_TYPE_UINT8, 33, &led_awaiting_pong},
    { "led_proc_ping",    OPT_TYPE_UINT8, 34, &led_proc_ping},
    { "led_holding_potato",   OPT_TYPE_UINT8, 35, &led_holding_potato},
    { "led_discover",         OPT_TYPE_UINT8, 36, &led_discover},
    { "led_received_pkt",     OPT_TYPE_UINT8, 37, &led_received_pkt},

    { "potato_hold_time", OPT_TYPE_TIME, 38 /* +2 */, &potato_hold_time},
    { "route_broadcast_attempts", OPT_TYPE_UINT8, 40,
        &route_broadcast_attempts},
    { "discover_period_s", OPT_TYPE_UINT8, 41, &discover_period_s},
    { "discover_time_out", OPT_TYPE_TIME, 42, &discover_time_out},
    { "beam_rpc_time_out", OPT_TYPE_TIME, 44 /* +2 */, &beam_rpc_time_out},
    { "beam_sense_time", OPT_TYPE_TIME, 46 /* +2 */, &beam_sense_time},
    { "fence_rpc_time_out", OPT_TYPE_TIME, 48 /* +2 */, &fence_rpc_time_out},
    { "logcat", OPT_TYPE_UINT16, 50, &logcat},
    { "ping_time_out", OPT_TYPE_TIME, 52 /* +2 */, &ping_time_out},

    /* EMPTY SLOT: 54 */
    /* EMPTY SLOT: 55 */
    { "ir_carrier_freq_khz",  OPT_TYPE_UINT8, 56, &ir_carrier_freq_khz},
    { "ir_pulse_duty_cycle",  OPT_TYPE_UINT8, 57, &ir_pulse_duty_cycle},
    { "ir_pulse_ticks",       OPT_TYPE_UINT8, 58, &ir_pulse_ticks},
    { "rcmd_rpc_time_out",    OPT_TYPE_TIME, 60, &rcmd_rpc_time_out},
    { "ir_probe_time",    OPT_TYPE_TIME, 62, &ir_probe_time},
    { "tx_msg_retry_delay",    OPT_TYPE_TIME, 64, &tx_msg_retry_delay},
    { "ir_direction_time",    OPT_TYPE_TIME, 66, &ir_direction_time},
    { "irtop_rpc_timeout",    OPT_TYPE_TIME, 68, &irtop_rpc_timeout},
    { "twi_tx_poll_interval",    OPT_TYPE_TIME, 70, &twi_tx_poll_interval},
    { "compass_poll_interval",   OPT_TYPE_TIME, 72, &compass_poll_interval},
    { "compass_measure_timeout",   OPT_TYPE_TIME, 74, &compass_measure_timeout},
    { "twi_tx_timeout",    OPT_TYPE_TIME, 76, &twi_tx_timeout},
    /* EMPTY SLOT: 78-80 */
    { "rssi_dist_intercept", OPT_TYPE_UINT8, 80, &rssi_dist_intercept},
    { "rssi_dist_slope_inverse", OPT_TYPE_UINT8, 81, &rssi_dist_slope_inverse},
    { "compass_rpc_time_out", OPT_TYPE_TIME, 82, &compass_rpc_time_out},
    { "ping_period", OPT_TYPE_TIME, 84, &ping_period},
    { "rssi_avg_count", OPT_TYPE_UINT8, 86, &rssi_avg_count},
    { "rx_queue_slot_wait", OPT_TYPE_TIME, 88, &rx_queue_slot_wait},
    { "discover_req_delay", OPT_TYPE_TIME, 90, &discover_req_delay},
    { "discover_send_attempts", OPT_TYPE_UINT8, 92, &discover_send_attempts},
    { "heal_routes", OPT_TYPE_BOOL, 93, &heal_routes},
    { "ir_probe_broadcasts", OPT_TYPE_UINT8, 94, &ir_probe_broadcasts},


    /* EE_ROUTES (see addr below) */
#define OFFSET_0 128
#if ENABLE_RFTOP
    { "network", OPT_TYPE_BLOB, OFFSET_0, &network, sizeof(network) },
#define OFFSET_1 OFFSET_0 + sizeof(network)
#endif
#if ENABLE_ROUTER
    { "routes", OPT_TYPE_BLOB, OFFSET_1, &routes, sizeof(routes) },
#define OFFSET_2 OFFSET_1 + sizeof(routes)
#endif
#if ENABLE_IRTOP
    { "ir_graph", OPT_TYPE_BLOB, OFFSET_2, &ir_graph, sizeof(ir_graph) },
#define OFFSET_3 OFFSET_2 + sizeof(ir_graph)
    { "ir_neighbors", OPT_TYPE_BLOB, OFFSET_3, &ir_neighbors,
        sizeof(ir_neighbors) },
#endif /* ENABLE_IRTOP */
#if ENABLE_POSITION
#define OFFSET_4 OFFSET_3 + sizeof(ir_neighbors)
    { "locations", OPT_TYPE_BLOB, OFFSET_4, &locations, sizeof(locations) },
#define OFFSET_5 OFFSET_4 + sizeof(locations)
    { "map_dim", OPT_TYPE_BLOB, OFFSET_5, &map_dim, sizeof(map_dim) },
#define OFFSET_6 OFFSET_5 + sizeof(map_dim)
#endif /* ENABLE_POSITION */

    { {0}, 0, 0, NULL }
};

int8_t cmd_set(uint8_t argc, char **argv)
{
    const char *name;
    const char *value;
    const option_t *opt;
    bool save;

    if (!(argc == 3 || argc == 4)) {
        OUT("usage: set <option> <value> [t]\r\n");
        return NRK_ERROR;
    }

    name = argv[1];
    value = argv[2];
    save = argc == 4 ? argv[3][0] != 't' : true;

    opt = find_option(name);
    if (!opt) {
        OUT("ERROR: option not found\r\n");
        return NRK_ERROR;
    }
    set_option(opt, value);
    if (save)
        save_option(opt);
    print_option(opt);
    return NRK_OK;
}

int8_t cmd_get(uint8_t argc, char **argv)
{
    const char *name;
    const option_t *opt;

    if (argc != 1 && argc != 2) {
        OUT("usage: get [<option>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 1) { /* all options */
        print_options();

    } else { /* the given option */
        name = argv[1];

        opt = find_option(name);
        if (!opt) {
            OUT("ERROR: option not found\r\n");
            return NRK_ERROR;
        }
        print_option(opt);
    }
    return NRK_OK;
}

int8_t cmd_save(uint8_t argc, char **argv)
{
    int8_t rc;
    const char *name;
    const option_t *opt;

    if (argc != 1 && argc != 2) {
        OUT("usage: save [<option>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 1) { /* all options */
        rc = save_options();

    } else { /* the given option */
        name = argv[1];

        opt = find_option(name);
        if (!opt) {
            OUT("ERROR: option not found\r\n");
            return NRK_ERROR;
        }
        rc = save_option(opt);
    }
    return rc;
}

int8_t cmd_load(uint8_t argc, char **argv)
{
    int8_t rc;
    const char *name;
    const option_t *opt;

    if (argc != 1 && argc != 2) {
        OUT("usage: load [<option>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 1) { /* all options */
        rc = load_options();

    } else { /* the given option */
        name = argv[1];

        opt = find_option(name);
        if (!opt) {
            OUT("ERROR: option not found\r\n");
            return NRK_ERROR;
        }
        rc = load_option(opt);
    }
    return rc;
}

void init_config()
{
    init_options(options);

    /* Don't load options from eeprom if button is pressed during boot. This is
     * useful for saving newly created options to eeprom for the first time. */
    if (nrk_gpio_get(NRK_BUTTON)) {
        nrk_gpio_clr(NRK_BUTTON); /* clear, otherwise persists across reset */
        LOG("loading options from eeprom\r\n");
        load_options();
    } else {
        nrk_led_set(LED_DEFAULT_OPTIONS);
        LOG("leaving options at default values\r\n");
    }

    print_options();
}
