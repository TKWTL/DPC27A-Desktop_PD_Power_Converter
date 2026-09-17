/* Output power allocation policy (see power_alloc.h for the rules).
 * Pure RAM computation - the caller performs the I2C writes. */
#include "power_alloc.h"
#include "power_limit.h"

static uint8_t s_pwr_alloc_order[PWR_ALLOC_CHIPS];   /* 0 = detached, else attach seq */
static uint8_t s_pwr_alloc_seq;
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

void PWR_Alloc_Plan(uint8_t attached_3538, uint8_t attached_c1, uint8_t attached_c2,
                    uint8_t share[PWR_ALLOC_CHIPS])
{
    uint8_t attached[PWR_ALLOC_CHIPS];
    uint32_t remaining = PWR_Limit_GetW();
    uint8_t first = 0xFFu;
    uint8_t i;

    attached[0] = attached_3538;
    attached[1] = attached_c1;
    attached[2] = attached_c2;

    /* Refresh the attach order and split the budget; pure computation (no I2C). */
    for(i = 0u; i < (uint8_t)PWR_ALLOC_CHIPS; i++)
    {
        if(attached[i] != 0u)
        {
            if(s_pwr_alloc_order[i] == 0u)
                s_pwr_alloc_order[i] = (uint8_t)(++s_pwr_alloc_seq);
        }
        else
        {
            s_pwr_alloc_order[i] = 0u;      /* unplugged: reservation returns */
        }
    }

    for(i = 0u; i < (uint8_t)PWR_ALLOC_CHIPS; i++)
    {
        if((s_pwr_alloc_order[i] != 0u) &&
           ((first == 0xFFu) || (s_pwr_alloc_order[i] < s_pwr_alloc_order[first])))
            first = i;
    }

    for(i = 0u; i < (uint8_t)PWR_ALLOC_CHIPS; i++)
        share[i] = 0u;

    if(first != 0xFFu)
    {
        share[first] = (pwr_alloc_cap_w(first) < remaining) ?
                       pwr_alloc_cap_w(first) : (uint8_t)remaining;
        remaining -= share[first];
    }

    for(i = 0u; i < (uint8_t)PWR_ALLOC_CHIPS; i++)
    {
        if(i != first)
        {
            share[i] = (pwr_alloc_cap_w(i) < remaining) ?
                       pwr_alloc_cap_w(i) : (uint8_t)remaining;
        }
    }
}
