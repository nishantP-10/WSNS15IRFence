#include <nrk.h>
#include <include.h>
#include <nrk_error.h>
#include <nrk_eeprom.h>
#include <nrk_time.h>
#include <avr/pgmspace.h>

#include "output.h"
#include "options.h"
#include "time.h"

static const option_t *options; /* ptr to prog memory */

/* Only writes if the value has changed */
void save_to_eeprom(uint16_t addr, uint8_t value)
{
    if (nrk_eeprom_read_byte(addr) != value)
        nrk_eeprom_write_byte(addr, value);
}

void save_buf_to_eeprom(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; ++i)
        save_to_eeprom(addr + i, buf[i]);
}

const option_t *find_option(const char *name)
{
    const option_t *opt = &options[0];
    while (pgm_read_word(&opt->value)) {
       if (!strcmp_P(name, opt->name))
            return opt;
        opt++;
    }
    return NULL;
}

int8_t set_option(const option_t *opt, const char *val)
{
    uint32_t val_num;
    int32_t val_snum;
    char val_char;
    nrk_time_t val_time;

    void *value = pgm_read_word(&opt->value);
    opt_type_t type = pgm_read_byte(&opt->type);

    switch (type) {
        case OPT_TYPE_UINT8:
            sscanf(val, "%lu", &val_num);
            *(uint8_t *)value = (uint8_t)val_num;
            break;
        case OPT_TYPE_UINT16:
            sscanf(val, "%lu", &val_num);
            *(uint16_t *)value = (uint16_t)val_num;
            break;
        case OPT_TYPE_UINT32:
            sscanf(val, "%lu", &val_num);
            *(uint32_t *)value = (uint32_t)val_num;
            break;
        case OPT_TYPE_INT8:
            sscanf(val, "%ld", &val_snum);
            *(int8_t *)value = (int8_t)val_num;
            break;
        case OPT_TYPE_INT16:
            sscanf(val, "%ld", &val_snum);
            *(int16_t *)value = (int16_t)val_num;
            break;
        case OPT_TYPE_INT32:
            sscanf(val, "%ld", &val_snum);
            *(int32_t *)value = (int32_t)val_num;
            break;
        case OPT_TYPE_BOOL:
            sscanf(val, "%c", &val_char);
            *(bool *)value = val_char == 'T' || val_char == '1';
            break;
        case OPT_TYPE_TIME:
            sscanf(val, "%lu:%lu", &val_time.secs, &val_time.nano_secs);
            *(nrk_time_t *)value = val_time;
            break;
        case OPT_TYPE_BLOB:
            OUT("setting blob option type not supported\r\n");
            break;
        default:
            LOG("ERROR: set: unsupported option type\r\n");
            return NRK_ERROR;
    }
    return NRK_OK;
}

void print_option(const option_t *opt)
{
    nrk_time_t val_time;

    void *value = pgm_read_word(&opt->value);
    opt_type_t type = pgm_read_byte(&opt->type);

    OUT("\t");
    OUTF(opt->name);
    OUT(": ");
    switch (type) {
        case OPT_TYPE_UINT8:
            OUTP("%u", *(uint8_t *)value);
            break;
        case OPT_TYPE_UINT16:
            OUTP("%u", *(uint16_t *)value);
            break;
        case OPT_TYPE_UINT32:
            OUTP("%lu", *(uint32_t *)value);
            break;
        case OPT_TYPE_INT8:
            OUTP("%d", *(int8_t *)value);
            break;
        case OPT_TYPE_INT16:
            OUTP("%d", *(int16_t *)value);
            break;
        case OPT_TYPE_INT32:
            OUTP("%ld", *(int32_t *)value);
            break;
        case OPT_TYPE_BOOL:
            OUTP("%c", *(bool *)value ? 'T' : 'F');
            break;
        case OPT_TYPE_TIME:
            val_time = *(nrk_time_t *)value;
            OUTP("%lu:%lu", val_time.secs, val_time.nano_secs);
            break;
        case OPT_TYPE_BLOB:
            OUT("<blob>");
            break;
        default:
            LOG("<unsupported>\r\n");
    }
    OUT("\r\n");
}

int8_t save_option(const option_t *opt)
{
    nrk_time_t val_time;
    uint32_t val_num;
    uint8_t size;

    uint16_t addr = pgm_read_word(&opt->addr);
    void *value = pgm_read_word(&opt->value);
    opt_type_t type = pgm_read_byte(&opt->type);

    switch (type) {
        case OPT_TYPE_UINT8:
            save_to_eeprom(addr, *(uint8_t *)value);
            break;
        case OPT_TYPE_UINT16:
            val_num = *(uint16_t *)value;
            save_to_eeprom(addr + 0, (val_num >> 8 * 0) & 0xff);
            save_to_eeprom(addr + 1, (val_num >> 8 * 1) & 0xff);
            break;
        case OPT_TYPE_INT8:
            save_to_eeprom(addr, *(int8_t *)value);
            break;
        case OPT_TYPE_BOOL:
            save_to_eeprom(addr, *(bool *)value ? 1 : 0);
            break;
        case OPT_TYPE_TIME: /* store ms as two bytes */
            val_time = *(nrk_time_t *)value;
            val_num = TIME_TO_MS(val_time);
            save_to_eeprom(addr + 0, (val_num >> 8 * 0) & 0xff);
            save_to_eeprom(addr + 1, (val_num >> 8 * 1) & 0xff);
            break;
        case OPT_TYPE_BLOB:
            size = pgm_read_byte(&opt->size);
            save_buf_to_eeprom(addr, (uint8_t *)value, size);
            break;
        default:
            LOG("ERROR: save: unsupported option type\r\n");
            return NRK_ERROR;
    }
    return NRK_OK;
}

int8_t load_option(const option_t *opt)
{
    uint32_t val_num = 0;
    uint8_t i;
    uint8_t size;

    uint16_t addr = pgm_read_word(&opt->addr);
    void *value = pgm_read_word(&opt->value);
    opt_type_t type = pgm_read_byte(&opt->type);

    switch (type) {
        case OPT_TYPE_UINT8:
            *(uint8_t *)value = nrk_eeprom_read_byte(addr);
            break;
        case OPT_TYPE_UINT16:
            val_num |= nrk_eeprom_read_byte(addr + 0) << (8 * 0);
            val_num |= nrk_eeprom_read_byte(addr + 1) << (8 * 1);
            *(uint16_t *)value = (uint16_t)val_num;
            break;
        case OPT_TYPE_INT8:
            *(int8_t *)value = nrk_eeprom_read_byte(addr);
            break;
        case OPT_TYPE_BOOL:
            *(bool *)value = nrk_eeprom_read_byte(addr) != 0;
            break;
        case OPT_TYPE_TIME:
            val_num = nrk_eeprom_read_byte(addr + 1);
            val_num <<= (8 * 1);
            val_num |= nrk_eeprom_read_byte(addr + 0);
            MS_TO_TIME(*(nrk_time_t *)value, val_num);
            break;
        case OPT_TYPE_BLOB:
            size = pgm_read_byte(&opt->size);
            for (i = 0; i < size; ++i)
                *((uint8_t *)value + i) = nrk_eeprom_read_byte(addr + i);
            break;
        default:
            LOG("ERROR: load: unsupported type\r\n");
            return NRK_ERROR;
    }
    return NRK_OK;
}

void print_options()
{
    const option_t *opt = &options[0];
    while (pgm_read_word(&opt->value))
        print_option(opt++);
}

int8_t load_options()
{
    const option_t *opt = &options[0];
    while (pgm_read_word(&opt->value))
        load_option(opt++);
    return NRK_OK;
}

int8_t save_options()
{
    const option_t *opt = &options[0];
    while (pgm_read_word(&opt->value))
        save_option(opt++);
    return NRK_OK;
}

/* Ptr to options description array in prog memory */
void init_options(const option_t *opts)
{
    options = opts;
}
