#ifndef FAN_CONTROL_H_
#define FAN_CONTROL_H_

#include <stdint.h>

/* Automatic fan curve.  Pure policy (no hardware access): thread_fan in
 * APP/app_tasks.c reads the GX21M15 mirror, calls FAN_Control_NextPwm() and
 * applies the result through FAN_PWM_SetDuty8().
 *
 * Priority, first match wins:
 *   1. input UVP            -> 0   (forced off; the same 8 V threshold the
 *                                    power budget uses, PWR_LIMIT_UVP_MV);
 *   2. sensor not detected  -> 255 (no temperature feedback: full speed);
 *   3. temperature curve    -> start at 40 C, stop at 37 C (3 C hysteresis),
 *                              then +2 duty per degree above the start
 *                              point, capped at 255.  80 is the measured
 *                              silent start level (BSP/Fan/README.md:
 *                              0..48 does not turn, 48..72 buzzes, 80..104
 *                              is silent), so a cold start jumps 0 -> 80.
 *
 * FAN_Control_SetTemperatureDelay() shifts *all* thresholds: with 10 the fan
 * starts at 50 C and stops at 47 C (planned UI setting "delayed trigger
 * point"); the default 0 is the normal 40/37 C behaviour. */
uint8_t FAN_Control_NextPwm(int16_t temperature_c, uint8_t sensor_online, uint8_t uvp);
void    FAN_Control_SetTemperatureDelay(uint8_t delay_c);
uint8_t FAN_Control_GetTemperatureDelay(void);

#endif /* FAN_CONTROL_H_ */
