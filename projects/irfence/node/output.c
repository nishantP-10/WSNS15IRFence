#include <nrk.h>
#include <nrk_error.h>
#include <stdlib.h>

#include "config.h"

#include "output.h"
#include "enum.h"

const char new_line_str[] PROGMEM = "\r\n";

static const char log_cat_names[][MAX_ENUM_NAME_LEN] PROGMEM = {
    "master",
    "rxtx",
    "router",
    "ir",
    "beam",
    "rftop",
    "ping",
    "rpc",
    "fence",
    "irtop",
    "localization",
    "twi",
    "compass",
    "periodic",
    "rxtxdata",
    "led",
};

#define NUM_LOG_CATEGORIES (sizeof(log_cat_names) / sizeof(log_cat_names[0]))

static log_category_t log_category_from_string(const char *str)
{
    uint8_t i;
    for (i = 0; i < NUM_LOG_CATEGORIES; ++i)
        if (!strcmp_P(str, log_cat_names[i]))
            return 1 << i;
    return LOG_CATEGORY_INVALID;
}

void log_preamble()
{
#ifdef LOG_TIMESTAMP
    nrk_time_t timestamp;

    nrk_time_get(&timestamp);
    printf("%u| %lu.%lu: ", this_node_id,
           timestamp.secs, timestamp.nano_secs / NANOS_PER_MS);
#else
    printf("%u| ", this_node_id);
#endif
}

int8_t cmd_log(uint8_t argc, char **argv)
{
    log_category_t cat;
    bool state;
    const char *category_name;
    uint8_t i;

    if (!(argc == 1 || argc >= 2)) {
        OUT("usage: log [[+|-[<category>|*]...]\r\n");
        return NRK_ERROR;
    }

    if (argc == 1) {
        for (i = 0; i < NUM_LOG_CATEGORIES; ++i) {
            cat = 1 << i;
            if (logcat & cat) {
                OUTF(ENUM_TO_STR(i, log_cat_names));
                OUT(" ");
            }
        }
        OUT("\r\n");
    } else {
        for (i = 1; i < argc; ++i) {
            state = (argv[i][0] == '+');

            /* blank category name means 'master' */
            if (argv[i][0] != '\0' && argv[i][1] == '\0') {
                cat = LOG_CATEGORY_MASTER;
            } else {
                category_name = &argv[i][1];
                if (category_name[0] == '*') {
                    cat = ~0x0;
                } else {
                    cat = log_category_from_string(category_name);
                    if (cat == LOG_CATEGORY_INVALID) {
                        OUT("ERROR: unknown log category\r\n");
                        return NRK_ERROR;
                    }
                }
            }
            logcat &= ~cat;
            if (state)
                logcat |= cat;
        }
    }

    return NRK_OK;
}
