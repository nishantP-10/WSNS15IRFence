#ifndef POSITION_H
#define POSITION_H

typedef struct {
    int8_t x, y;
} point_t;

typedef struct {
    bool valid;
    point_t pt;
} location_t;

location_t *get_locations();

int8_t cmd_localize(uint8_t argc, char **argv);
int8_t cmd_loc(uint8_t argc, char **argv);
int8_t cmd_mapdim(uint8_t argc, char **argv);

/* Persistant private state: exposed only for config.c */
extern location_t locations[MAX_NODES];
extern point_t map_dim;

#endif
