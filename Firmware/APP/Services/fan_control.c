/* Automatic fan curve.  See fan_control.h for the rules.
 *
 * Pure policy module: it never touches a peripheral, so thread_fan can call
 * it even while the PD quiet window suppresses the I2C/SPI device mirrors. */
#include "fan_control.h"

/* Curve constants.  Duty is the 0..255 FAN_PWM scale; 80 is the measured
 * silent start level (0..48 does not turn the fan, 48..72 buzzes), so the
 * running curve never drops below it. */
#define FAN_START_TEMP_C  40
#define FAN_STOP_TEMP_C   37
#define FAN_START_PWM     80u
#define FAN_STEP_PWM       2u

/* Cap on the delay offset so the curve stays sane (30 C keeps the start
 * point at 70 C). */
#define FAN_DELAY_MAX_C   30u

static uint8_t s_delay_c;
static uint8_t s_running;

void FAN_Control_SetTemperatureDelay(uint8_t delay_c)
{
    s_delay_c = (delay_c > FAN_DELAY_MAX_C) ? (uint8_t)FAN_DELAY_MAX_C : delay_c;
}

uint8_t FAN_Control_GetTemperatureDelay(void)
{
    return s_delay_c;
}

uint8_t FAN_Control_NextPwm(int16_t temperature_c, uint8_t sensor_online, uint8_t uvp)
{
    int16_t start;
    int16_t stop;
    uint32_t pwm;

    if(uvp != 0u)
    {
        s_running = 0u;
        return 0u;
    }

    if(sensor_online == 0u)
        return 255u;                 /* fail safe: no temperature feedback */

    start = (int16_t)(FAN_START_TEMP_C + (int)s_delay_c);
    stop  = (int16_t)(FAN_STOP_TEMP_C + (int)s_delay_c);

    if(s_running == 0u)
    {
        if(temperature_c >= start)
            s_running = 1u;          /* start point (40 C at delay 0) */
    }
    else if(temperature_c <= stop)
    {
        s_running = 0u;              /* stop point (37 C at delay 0) */
    }

    if(s_running == 0u)
        return 0u;

    /* Between stop and start the fan holds the silent floor; above the start
     * point every degree adds FAN_STEP_PWM (40 C -> 80, 41 C -> 82, ...). */
    pwm = FAN_START_PWM;
    if(temperature_c > start)
        pwm += (uint32_t)(temperature_c - start) * FAN_STEP_PWM;

    return (pwm > 255u) ? 255u : (uint8_t)pwm;
}
