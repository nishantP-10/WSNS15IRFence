#include <nrk.h>
#include <nrk_error.h>
#include <include.h>
#include <stdlib.h>

#include "output.h"
#include "compass.h"
#include "router.h"
#include "rxtx.h"

#include "localization.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_LOCALIZATION

uint16_t get_led_angle(uint8_t led)
{
    int8_t rc;
    uint16_t angle;
    int16_t heading;

    rc = get_heading(&heading);
    if (rc != NRK_OK) {
        LOG("WARN: can't calc led angle: no heading\r\n");
        return 0;
    }

    /* +90 and invert is to make angle relative to East (i.e. the x-axis) */
    angle = heading + (uint16_t)led * 360 / NUM_IR_LEDS + 90;
    while (angle >= 360)
        angle -= 360;
    angle = 360 - angle;

    LOG("led angle: ");
    LOGP("[%d] -> %d deg\r\n", led, angle);
    return angle;
}

uint16_t get_distance(node_id_t node)
{
    int16_t dist = 0;
    neighbor_t *neighbor;

    neighbor = get_neighbor(node);
    if (neighbor) {
        dist = (rssi_dist_intercept - neighbor->rssi);
        dist /= rssi_dist_slope_inverse;
        if (dist < 0)
            dist = 0;
    }
    LOG("dist: ");
    LOGP("node %d ", node);
    LOGP(" -> %d\r\n", dist);
    return dist;
}

int8_t cmd_irledangle(uint8_t argc, char **argv)
{
    uint8_t led;
    uint16_t angle;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: irledangle [<led>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2) {
        led = atoi(argv[1]);
        angle = get_led_angle(led);
    } else {
        for (led = 0; led < NUM_IR_LEDS; ++led) {
            angle = get_led_angle(led);
            OUTP("%d: %d\r\n", led, angle);
        }
    }
    return NRK_OK;
}

int8_t cmd_dist(uint8_t argc, char **argv)
{
    node_id_t node;
    uint16_t dist;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: dist [<node>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2) {
        node = atoi(argv[1]);
        dist = get_distance(node);
        OUTP("%d: %d\r\n", node, dist);
    } else {
        for (node = 0; node < MAX_NODES; ++node) {
            dist = get_distance(node);
            OUTP("%d: %d\r\n", node, dist);
        }
    }
    return NRK_OK;
}
