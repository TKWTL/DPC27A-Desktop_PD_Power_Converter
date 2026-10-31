/* PB02B-style frozen countdown with cooperative 32-bit TIME_Millis().
 * Timeout must remain below 2^31 ms for modular signed comparisons. */
#include "pm_sleep_timer.h"
#include "time_api.h"

static uint32_t s_timeout_ms, s_deadline, s_frozen_ms;
static uint8_t s_disabled, s_paused, s_forced;

static uint32_t timer_left(uint32_t now)
{
    int32_t diff = (int32_t)(s_deadline - now);
    return (diff > 0) ? (uint32_t)diff : 0u;
}

void pm_sleep_timer_init(uint32_t ms)
{
    s_timeout_ms = ms;
    s_deadline = TIME_Millis() + ms;
    s_frozen_ms = 0u;
    s_disabled = 0u;
    s_paused = 0u;
    s_forced = 0u;
}

void pm_sleep_timer_set(uint32_t ms)
{
    s_timeout_ms = ms;
    s_disabled = 0u;
    if(s_paused) s_frozen_ms = ms;
    else s_deadline = TIME_Millis() + ms;
}

void pm_sleep_timer_disable(void) { s_disabled = 1u; }

void pm_sleep_timer_refresh(void)
{
    if(s_paused) s_frozen_ms = s_timeout_ms;
    else s_deadline = TIME_Millis() + s_timeout_ms;
}

void pm_sleep_timer_pause(void)
{
    if(!s_paused) {
        s_frozen_ms = timer_left(TIME_Millis());
        s_paused = 1u;
    }
}

void pm_sleep_timer_resume(void)
{
    if(s_paused) {
        s_deadline = TIME_Millis() + s_frozen_ms;
        s_paused = 0u;
    }
}

void pm_sleep_timer_force_expire(void) { s_forced = 1u; }
void pm_sleep_timer_clear_force(void) { s_forced = 0u; }

uint32_t pm_sleep_timer_left_ms(void)
{
    if(s_forced) return 0u;
    if(s_disabled) return PM_SLEEP_INFINITE;
    if(s_paused) return s_frozen_ms;
    return timer_left(TIME_Millis());
}

uint8_t pm_sleep_timer_expired(void)
{
    if(s_forced) return 1u;
    if(s_disabled || s_paused) return 0u;
    return ((int32_t)(TIME_Millis() - s_deadline) >= 0) ? 1u : 0u;
}

uint8_t pm_sleep_timer_is_disabled(void) { return s_disabled ? 1u : 0u; }
