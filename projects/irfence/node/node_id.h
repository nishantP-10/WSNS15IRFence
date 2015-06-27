#ifndef NODE_ID_H
#define NODE_ID_H

typedef uint8_t node_id_t;
typedef uint8_t node_set_t;

#define IS_VALID_NODE_ID(id) (id != 0 && id < MAX_NODES)

#define NODE_SET_INIT(set) do { set = 0; } while (0)
#define NODE_SET_ADD(set, id) do { set |= (1 << (id)); } while (0)
#define NODE_SET_REMOVE(set, id) do { set &= ~(1 << (id)); } while (0)
#define NODE_SET_IN(set, id) (set & (1 << (id)))

#endif // NODE_ID_H
