#include <nrk.h>

#include "enum.h"

const char * enum_to_str(uint8_t val, const char names[][MAX_ENUM_NAME_LEN],
                         uint8_t num)
{
    if (val < num)
        return names[val];
    return PSTR("<invalid>");
}

