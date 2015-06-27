#ifndef PERIODIC_H
#define PERIODIC_H

typedef void periodic_func_init_t();
typedef void periodic_func_proc_t(bool enabled,
                                  nrk_time_t *next_event,
                                  nrk_sig_mask_t *wait_mask);
typedef int8_t periodic_func_config_t(uint8_t argc, char **argv);

typedef struct {
    const char *name;
    bool enabled;
    bool last_enabled;
    periodic_func_init_t *init;
    periodic_func_proc_t *proc;
    periodic_func_config_t *config;
} periodic_func_t;

uint8_t init_periodic(uint8_t priority, periodic_func_t **funcs);

int8_t cmd_periodic(uint8_t argc, char **argv);

#endif
