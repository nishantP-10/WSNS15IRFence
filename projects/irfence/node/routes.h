#ifndef ROUTES_H
#define ROUTES_H

#include "cfg.h"
#include "node_id.h"

typedef node_id_t route_matrix_t[MAX_NODES][MAX_NODES];

/* Routes pkt fields (bytes): shared between router and discover modules */
#define PKT_ROUTES_VER_OFFSET     0
#define PKT_ROUTES_VER_LEN        1
#define PKT_ROUTES_TABLE_OFFSET   1
#define PKT_ROUTES_TABLE_LEN      (MAX_NODES * MAX_NODES * sizeof(node_id_t))

#endif // ROUTES_H

