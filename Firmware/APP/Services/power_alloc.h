/* Output power allocation policy for the three fast-charge controllers.
 *
 * The dashboard supplies the total input budget (PWR_Limit_GetW(), see
 * power_limit.h: UVP 0 W, PD 95% of the contract power, DC user value) and the
 * caller supplies each chip's measured output power from its ADC mirror
 * (APP/app_tasks.c thread_power_mirror; ports without a device are passed as
 * 0 W):
 *   - each chip keeps the power it currently draws and additionally sees the
 *     still-free part of the budget:
 *       share[i] = budget - sum(measured_w of the other chips),
 *     capped to the controller ceiling (SW3538 140 W, SW3526 65 W);
 *   - a detached port measures 0 W, so its share is the free headroom and a
 *     later attachment finds the best available ceiling; unplugging returns
 *     the share automatically - the plan is rebuilt on every power-mirror pass;
 *   - oversubscription (several chips may see the same free headroom) and
 *     measurement ripple are accepted by design.
 * Shares are clamped into the range each SetPowerLimitW() accepts
 * (SW3538 18..140 W, SW3526 12..71 W via PWR_Alloc_ApiW()) and the
 * ApplyNeeded/ApplyDone pair makes thread_power_mirror write only on change.
 *
 * Pure RAM work: the caller supplies the measurements and performs the actual
 * I2C writes from thread_power_mirror (APP/app_tasks.c).
 */
#ifndef POWER_ALLOC_H_
#define POWER_ALLOC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PWR_ALLOC_CHIPS       3u   /* 0 = SW3538 (A+C), 1 = SW3526 C1, 2 = SW3526 C2 */
#define PWR_CAP_SW3538_W    140u   /* controller capability ceiling (W) */
#define PWR_CAP_SW3526_W     65u
#define PWR_API_MIN_SW3538_W 18u   /* range accepted by SW3538_SetPowerLimitW() */
#define PWR_API_MAX_SW3538_W 140u
#define PWR_API_MIN_SW3526_W 12u   /* range accepted by SW3526_SetPowerLimitW() */
#define PWR_API_MAX_SW3526_W 71u

/* Rebuild the allocation plan from the per-chip measured output power (0 W for
 * ports without a device).  share[] always receives PWR_ALLOC_CHIPS entries. */
void PWR_Alloc_Plan(const uint8_t measured_w[PWR_ALLOC_CHIPS],
                    uint8_t share[PWR_ALLOC_CHIPS]);

/* Clamp a share into the range the chip's SetPowerLimitW() accepts. */
uint8_t PWR_Alloc_ApiW(uint8_t chip, uint8_t share);

/* "Needs write" tracking: ApplyNeeded() returns 1 while the chip has not been
 * programmed with this value yet (first pass always writes). */
uint8_t PWR_Alloc_ApplyNeeded(uint8_t chip, uint8_t w);
void PWR_Alloc_ApplyDone(uint8_t chip, uint8_t w);

#ifdef __cplusplus
}
#endif

#endif /* POWER_ALLOC_H_ */
