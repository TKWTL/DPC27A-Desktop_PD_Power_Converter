/* Total output power budget shared by the dashboard and the power-allocation
 * pass in APP/app_tasks.c.
 *
 * The UVP / PD states are evaluated on every read; only the DC value is
 * adjustable (dashboard DOWN key while the DC path is active). */
#include "power_limit.h"
#include "pd.h"
#include "vbus_sense.h"

static uint32_t s_user_limit_w = PWR_LIMIT_MAX_W;   /* DC power-on default */

uint32_t PWR_Limit_GetUserW(void)
{
    return s_user_limit_w;
}

uint8_t PWR_Limit_IsAdjustable(void)
{
    if(VBUS_Sense_ReadMillivolts() < PWR_LIMIT_UVP_MV)
        return 0u;

    return PD_IsConnected() ? 0u : 1u;   /* DC path only */
}

void PWR_Limit_StepDown(void)
{
    if(s_user_limit_w > PWR_LIMIT_MIN_W)
        s_user_limit_w -= PWR_LIMIT_STEP_W;
    else
        s_user_limit_w = PWR_LIMIT_MAX_W;   /* at 60 W: wrap back to the max */
}

uint32_t PWR_Limit_GetW(void)
{
    uint32_t mv;
    uint32_t ma;

    if(VBUS_Sense_ReadMillivolts() < PWR_LIMIT_UVP_MV)
        return 0u;                          /* UVP: output budget forced 0 */

    if(!PD_IsConnected())
        return s_user_limit_w;              /* DC: user value */

    mv = PD_GetContractVoltageMv();
    ma = PD_GetContractCurrentMa();
    if((mv == 0u) || (ma == 0u))
        return 0u;                          /* PD attached, contract not ready */

    /* Contract power x 95%, integer math (32-bit safe: 50 V * 5 A). */
    return (((mv * ma) / 1000000u) * 95u) / 100u;
}
