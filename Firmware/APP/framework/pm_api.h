#ifndef PM_API_H
#define PM_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Application-facing PM API. Normal key activity refreshes the timer; fresh
 * key presses and real PD/VBUS/port/output-power changes can wake UI_OFF.
 * Steady output over 5 W freezes AUTO sleep without undoing manual UI_OFF.
 * UI_OFF only darkens OLED; PD, monitoring, fans and power output continue. */
void pm_api_init(void);
void pm_api_poll(void);                          /* thread_pm: advance the state machine */
void pm_api_refresh_idle(void);                  /* generic activity; ignored in manual UI_OFF */
void pm_api_wake_from_activity(void);            /* key or genuine power edge wakes UI_OFF */
void pm_api_force_sleep(void);                   /* manual UI_OFF; ignores automatic blocks */
void pm_api_set_sleep_timeout(int timeout_sec);  /* 0 = No Auto Sleep, >0 = seconds */
/* Output above 5 W freezes AUTO sleep; manual UI_OFF remains available. */
#define PM_BLOCK_OUTPUT_POWER (1u<<0)
/* RAM-only option, default enabled; affects auto sleep blocking only. */
void pm_api_set_output_sleep_block_enabled(uint8_t enable);
uint8_t pm_api_get_output_sleep_block_enabled(void);
void pm_api_set_sleep_block(uint8_t mask, uint8_t enable);
uint8_t pm_api_get_block_mask(void);
uint8_t pm_api_ui_should_block(void);            /* 1 = thread_ui must stop drawing */
uint8_t pm_api_is_sleeping(void);
void pm_api_set_unstable_wake(uint8_t unstable);
uint8_t pm_api_is_unstable_wake(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_API_H */
