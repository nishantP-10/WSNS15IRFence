#include <nrk.h>
#include <string.h>
#include <stdlib.h>

#include "output.h"

#include "parse.h"

/* Convert string to a number, taking the base from the prefix if any. */
uint32_t aton(const char *s)
{
    uint8_t len = strlen(s);
    uint8_t base = 0; /* special, auto-detection of hex, octal, dec */

    if (len >= 2 && s[0] == '0' && s[1] == 'b') {
        base = 2;
        s += 2; /* skip the '0b' prefix */
    }
    return strtoul(s, NULL, base);
}

node_id_t parse_node_id(const char *str)
{
    node_id_t id = atoi(str);
    if (!IS_VALID_NODE_ID(id)) {
        OUT("ERROR: node id out of range\r\n");
        return INVALID_NODE_ID;
    }
    return id;
}
