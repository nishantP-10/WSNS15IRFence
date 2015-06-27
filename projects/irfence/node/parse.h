#ifndef PARSE_H
#define PARSE_H

#include "node_id.h"

uint32_t aton(const char *s);
node_id_t parse_node_id(const char *str);

#endif
