#ifndef OPTIONS_H
#define OPTIONS_H

#define MAX_OPT_NAME_LEN 32

typedef enum {
    OPT_TYPE_UINT8,
    OPT_TYPE_UINT16,
    OPT_TYPE_UINT32,
    OPT_TYPE_INT8,
    OPT_TYPE_INT16,
    OPT_TYPE_INT32,
    OPT_TYPE_BOOL,
    OPT_TYPE_TIME,
    OPT_TYPE_BLOB,
} opt_type_t;

typedef struct {
    const char name[MAX_OPT_NAME_LEN];
    opt_type_t type;
    uint16_t addr; /* eeprom addr */
    void *value; /* ptr to value */
    uint8_t size; /* for OPT_TYPE_BLOB only */
} option_t;


void init_options(const option_t *opts);
const option_t *find_option(const char *name);
int8_t set_option(const option_t *opt, const char *value);
int8_t save_option(const option_t *opt);
int8_t load_option(const option_t *opt);
void print_option(const option_t *opt);

int8_t save_options();
int8_t load_options();
void print_options();

void save_to_eeprom(uint16_t addr, uint8_t value);
void save_buf_to_eeprom(uint16_t addr, const uint8_t *buf, uint8_t len);

#endif // OPTIONS_H
