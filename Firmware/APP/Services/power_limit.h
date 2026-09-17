#ifndef POWER_LIMIT_H_
#define POWER_LIMIT_H_

#include <stdint.h>

/* Total output power budget (W): shown on the dashboard and consumed by the
 * power-allocation pass (Services/power_alloc.c, applied by the
 * thread_power_mirror pass in APP/app_tasks.c).
 *
 * Value rules:
 *   - input below PWR_LIMIT_UVP_MV -> 0 (UVP, the PD state bar shows "UVP");
 *   - PD contract present          -> 95% of the negotiated contract power;
 *   - DC input                     -> user value: powers up at PWR_LIMIT_MAX_W
 *     and the dashboard DOWN key subtracts PWR_LIMIT_STEP_W per press,
 *     wrapping back to the maximum after PWR_LIMIT_MIN_W. */
#define PWR_LIMIT_MIN_W    60u
#define PWR_LIMIT_MAX_W    270u
#define PWR_LIMIT_STEP_W   10u
#define PWR_LIMIT_UVP_MV   8000u

uint32_t PWR_Limit_GetW(void);
uint32_t PWR_Limit_GetUserW(void);
uint8_t  PWR_Limit_IsAdjustable(void);
void     PWR_Limit_StepDown(void);

#endif /* POWER_LIMIT_H_ */
