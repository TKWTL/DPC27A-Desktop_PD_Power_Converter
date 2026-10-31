#ifndef PM_DEVICE_H
#define PM_DEVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_hook_t)(void *ctx);

/* A device node groups the three power hooks; nodes are sorted by `order`
 * (ascending on prepare/suspend, descending on resume). */
typedef struct {
    const char *name;
    pm_hook_t on_prepare;   /* RUN -> UI_OFF (and UI_OFF -> SLEEP_PREPARE) */
    pm_hook_t on_suspend;   /* SLEEP_PREPARE -> DEEPSLEEP */
    pm_hook_t on_resume;    /* any path back to RUN */
    void *ctx;
    uint8_t order;
    pm_hook_t on_deepsleep;
    pm_hook_t on_standby;
} pm_device_node_t;

int pm_device_register(const pm_device_node_t *node);
void pm_device_prepare_all(void);
void pm_device_suspend_all(void);
void pm_device_resume_all(void);
void pm_device_enter_deepsleep(void);
void pm_device_enter_standby(void);
void pm_device_register_builtin(void);
void pm_device_register_linker_set(void);
void pm_device_register_all(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_DEVICE_H */
