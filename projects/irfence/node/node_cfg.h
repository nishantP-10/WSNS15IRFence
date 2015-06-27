#ifndef NODE_CFG_H
#define NODE_CFG_H

#include "node_id.h"

extern node_id_t this_node_id;
extern bool is_gateway;
extern uint8_t rf_chan;
extern uint8_t rf_power;
extern uint8_t rssi_thres;

extern uint8_t topology_mask;

#define IS_REACHEABLE(dest) (!(topology_mask & (1 << (dest))))

#endif // NODE_CFG_H
