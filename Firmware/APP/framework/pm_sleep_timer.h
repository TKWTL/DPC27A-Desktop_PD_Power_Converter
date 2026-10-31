#ifndef PM_SLEEP_TIMER_H
#define PM_SLEEP_TIMER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PM_SLEEP_INFINITE 0xFFFFFFFFUL
/* TIME_Millis() is 32-bit; configured timeouts must be < 2^31 ms.
 * disable = No Auto Sleep; pause = freeze elapsed countdown; refresh while
 * paused restores full frozen timeout; force overrides both modes. */
void pm_sleep_timer_init(uint32_t timeout_ms);
void pm_sleep_timer_set(uint32_t timeout_ms);
void pm_sleep_timer_disable(void);
void pm_sleep_timer_refresh(void);
void pm_sleep_timer_pause(void);
void pm_sleep_timer_resume(void);
void pm_sleep_timer_force_expire(void);
void pm_sleep_timer_clear_force(void);
uint32_t pm_sleep_timer_left_ms(void);
uint8_t pm_sleep_timer_expired(void);
uint8_t pm_sleep_timer_is_disabled(void);
#ifdef __cplusplus
}
#endif
#endif
