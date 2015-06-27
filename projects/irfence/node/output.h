#ifndef OUTPUT_H
#define OUTPUT_H

#include <nrk.h>

#include "cfg.h"
#include "config.h"

extern const char new_line_str[];

/* Remember to mirror changes in log_cat_names array */
typedef enum {
    LOG_CATEGORY_INVALID =  0, /* not a bit in a bitmask */
    LOG_CATEGORY_MASTER =   1 << 0,
    LOG_CATEGORY_RXTX =     1 << 1,
    LOG_CATEGORY_ROUTER =   1 << 2,
    LOG_CATEGORY_IR =       1 << 3,
    LOG_CATEGORY_BEAM =     1 << 4,
    LOG_CATEGORY_RFTOP =    1 << 5,
    LOG_CATEGORY_PING =     1 << 6,
    LOG_CATEGORY_RPC =      1 << 7,
    LOG_CATEGORY_FENCE =    1 << 8,
    LOG_CATEGORY_IRTOP =    1 << 9,
    LOG_CATEGORY_LOCALIZATION = 1 << 10,
    LOG_CATEGORY_TWI          = 1 << 11,
    LOG_CATEGORY_COMPASS      = 1 << 12,
    LOG_CATEGORY_PERIODIC     = 1 << 13,
    LOG_CATEGORY_RXTXDATA     = 1 << 14,
    LOG_CATEGORY_LED          = 1 << 15,
} log_category_t;

/* Define a default in order to not require each file to define a category, but
 * the downside is that those that do want to define one must 'undef' first. */
#define LOG_CATEGORY LOG_CATEGORY_MASTER

void log_preamble();

#define STRINGIFY_INNER(x) #x
#define STRINGIFY(x) STRINGIFY_INNER(x)
#define CODE_LOCATION __FILE__ ":" STRINGIFY(__LINE__)

#define ABORT(msg) \
    do { \
        log_preamble(); \
        nrk_kprintf(PSTR(CODE_LOCATION ": ABORT: ")); \
        nrk_kprintf(PSTR(msg)); \
        nrk_led_set(LED_ABORTED); \
        nrk_halt(); \
    } while (0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            log_preamble(); \
            nrk_kprintf(PSTR(CODE_LOCATION ": ASSERT: ")); \
            nrk_kprintf(PSTR(#cond)); \
            nrk_led_set(LED_ABORTED); \
            nrk_halt(); \
        } \
    } while (0)

#define OUT(msg) nrk_kprintf(PSTR(msg))
#define OUTP(...) printf(__VA_ARGS__)
#define OUTF(ptr) nrk_kprintf(ptr)

#define LOG_ENABLED(cat) (logcat & LOG_CATEGORY_MASTER && logcat & cat)

#define CLOG(cat, msg) \
    do { \
        if (LOG_ENABLED(cat)) { \
            log_preamble(); \
            nrk_kprintf(PSTR(__FILE__ ": " msg)); \
        } \
    } while (0)

#define CLOGA(cat, msg) \
    do { \
        if (LOG_ENABLED(cat)) nrk_kprintf(PSTR(msg)); \
    } while (0)

#define CLOGP(cat, ...) \
    do { \
        if (LOG_ENABLED(cat)) printf(__VA_ARGS__); \
    } while (0)

#define CLOGF(cat, msg) \
    do { \
        if (LOG_ENABLED(cat)) nrk_kprintf(msg); \
    } while (0)

#define LOG(msg) CLOG(LOG_CATEGORY, msg)
#define LOGA(msg) CLOGA(LOG_CATEGORY, msg)
#define LOGP(...) CLOGP(LOG_CATEGORY, __VA_ARGS__)
#define LOGF(msg) CLOGF(LOG_CATEGORY, msg)
#define LOGNL() CLOGF(LOG_CATEGORY, new_line_str)

#define WARN(msg) \
    do { \
        log_preamble(); nrk_kprintf(PSTR(CODE_LOCATION ": WARN: " msg)); \
    } while (0)

/* Message for control center */
#define CTRL_PREFIX "CTRL: "

#define COUT(msg) nrk_kprintf(PSTR(CTRL_PREFIX msg))
#define COUTA(msg) nrk_kprintf(PSTR(msg))
#define COUTP(...) printf(__VA_ARGS__)

int8_t cmd_log(uint8_t argc, char **argv);

#endif // OUTPUT_H
