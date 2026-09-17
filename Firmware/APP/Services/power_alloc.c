/* Output power allocation policy (see power_alloc.h for the rules).
 * Pure RAM computation - the caller performs the I2C writes. */
#include "power_alloc.h"
#include "power_limit.h"

static uint8_t s_pwr_alloc_written[PWR_ALLOC_CHIPS];
static uint8_t s_pwr_alloc_valid[PWR_ALLOC_CHIPS];

static uint8_t pwr_alloc_cap_w(uint8_t chip)
{
    return (chip == 0u) ? (uint8_t)PWR_CAP_SW3538_W : (uint8_t)PWR_CAP_SW3526_W;
}

uint8_t PWR_Alloc_ApiW(uint8_t chip, uint8_t share)
{
    uint8_t w = share;

    if(chip == 0u)
    {
        if(w > PWR_API_MAX_SW3538_W) w = PWR_API_MAX_SW3538_W;
        if(w < PWR_API_MIN_SW3538_W) w = PWR_API_MIN_SW3538_W;
    }
    else
    {
        if(w > PWR_API_MAX_SW3526_W) w = PWR_API_MAX_SW3526_W;
        if(w < PWR_API_MIN_SW3526_W) w = PWR_API_MIN_SW3526_W;
    }

    return w;
}

uint8_t PWR_Alloc_ApplyNeeded(uint8_t chip, uint8_t w)
{
    if((s_pwr_alloc_valid[chip] != 0u) && (s_pwr_alloc_written[chip] == w))
        return 0u;

    return 1u;
}

void PWR_Alloc_ApplyDone(uint8_t chip, uint8_t w)
{
    s_pwr_alloc_written[chip] = w;
    s_pwr_alloc_valid[chip] = 1u;
}

void PWR_Alloc_Plan(const uint8_t measured_w[PWR_ALLOC_CHIPS],
                    uint8_t share[PWR_ALLOC_CHIPS])
{
    uint32_t budget = PWR_Limit_GetW();
    uint32_t used = (uint32_t)measured_w[0] + (uint32_t)measured_w[1] +
                    (uint32_t)measured_w[2];
    uint32_t others;
    uint32_t w;
    uint8_t i;

    for(i = 0u; i < (uint8_t)PWR_ALLOC_CHIPS; i++)
    {
        /* This chip keeps what it draws; the free part of the budget (what no
         * chip is using) is the extra ceiling it sees.  A detached chip
         * measures 0 W, so it is provisioned with the free headroom only. */
        others = used - (uint32_t)measured_w[i];
        w = (budget > others) ? (budget - others) : 0u;
        if(w > (uint32_t)pwr_alloc_cap_w(i))
            w = (uint32_t)pwr_alloc_cap_w(i);

        share[i] = (uint8_t)w;
    }
}
