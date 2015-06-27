#ifndef ENUM_H
#define ENUM_H

#include "cfg.h"

#define ENUM_TO_STR(val, names) \
    enum_to_str(val, names, sizeof(names) / sizeof(names[0]))

const char * enum_to_str(uint8_t val, const char names[][MAX_ENUM_NAME_LEN],
                         uint8_t num);

#endif
