#ifndef FENCE_H
#define FENCE_H

typedef enum {
    SECTION_STATE_NONE = 0,
    SECTION_STATE_ACTIVE,
    SECTION_STATE_BREACHED,
} section_state_t;

typedef struct {
    node_id_t posts[MAX_NODES];
    uint8_t len;
    section_state_t section_state[MAX_NODES];
} fence_t;

uint8_t init_fence(uint8_t priority);

int8_t create_fence(fence_t *fence);
int8_t destroy_fence(fence_t *fence);

int8_t cmd_fence(uint8_t argc, char **argv);

#endif
