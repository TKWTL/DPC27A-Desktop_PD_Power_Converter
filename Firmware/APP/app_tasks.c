#include "app_tasks.h"
#include "coroOS.h"
#include "debug.h"
#include "pd.h"
#include "Services/power_limit.h"
#include "Services/power_alloc.h"
#include "Services/fan_control.h"
#include "algorithm/port_protocol_algo.h"
#include "framework/pm_api.h"
#include "framework/pm_sleep_timer.h"
#include "board.h"
#include "vbus_sense.h"
#include "i2c_api.h"
#include "soft_i2c.h"
#include "sw3526.h"
#include "sw3538.h"
#include "GX21M15/gx21m15.h"
#include "fan_pwm.h"
#include "spi_dma.h"
#include "sh1107_display.h"
#include "buttons.h"
#include "ui_conf.h"
#include "Pages/port_pages.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "time_api.h"

static coro_scheduler_t s_scheduler;

static SoftI2C_Handle s_sw3526_bus1;
static SoftI2C_Handle s_sw3526_bus2;
static SW3526_Handle s_sw3526_1;
static SW3526_Handle s_sw3526_2;
static SW3538_Handle s_sw3538;
static GX21M15_Handle s_gx21m15;

ui_t ui;

static void APP_PrintResetCause(void)
{
    uint8_t any = 0u;
    printf("[RESET] cause:");
    if(RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET) { printf(" POR/PDR"); any = 1u; }
    if(RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET) { printf(" PIN"); any = 1u; }
    if(RCC_GetFlagStatus(RCC_FLAG_SFTRST) != RESET) { printf(" SW"); any = 1u; }
    if(RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) { printf(" IWDG"); any = 1u; }
    if(RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET) { printf(" WWDG"); any = 1u; }
    if(RCC_GetFlagStatus(RCC_FLAG_LPWRRST) != RESET) { printf(" LPWR"); any = 1u; }
    if(!any) printf(" unknown/cleared");
    printf("\r\n");
    RCC_ClearFlag();
}

THRD_DECLARE(thread_pd)
{
    THRD_BEGIN;
    while(1)
    {
        PD_Task(TIME_Millis());
        THRD_YIELD;
    }
    THRD_END;
}

THRD_DECLARE(thread_vbus)
{
    static uint16_t mv;
    static uint8_t log_div;
    static uint32_t sleep_left;
    THRD_BEGIN;
    log_div = 0u;
    while(1)
    {
        THRD_DELAY(20u);
        mv = VBUS_Sense_ReadMillivolts();
        PD_SetVbusMillivolts(mv);
        if(++log_div >= 50u)
        {
            log_div = 0u;
            printf("[VBUS] %u mV\r\n", (unsigned)mv);

            /* Sleep countdown right behind the voltage line (debug aid):
             * seconds until the framework sleeps, "off" = No Auto Sleep /
             * countdown stopped, pm_off = 1 once the state machine left RUN
             * (screen off), left=0 s means the deadline has passed. */
            sleep_left = pm_sleep_timer_left_ms();
            if(sleep_left == PM_SLEEP_INFINITE)
                printf("[PM] sleep left=off pm_off=%u\r\n", (unsigned)pm_api_is_sleeping());
            else
                printf("[PM] sleep left=%lu s pm_off=%u\r\n",
                       (unsigned long)((sleep_left + 999ul) / 1000ul),
                       (unsigned)pm_api_is_sleeping());
        }
    }
    THRD_END;
}

#define UI_FRAME_PERIOD_MS         8u
#define UI_KEY_SERVICE_MS          10u
#define PD_STARTUP_QUIET_WINDOW_MS 1000u

/* The OLED panel is reset by an external RC network only; it must not see its
 * first command until ~200 ms after power-up.  Waited cooperatively in
 * thread_ui() before diapInit() - never as a blocking wait, which starves
 * PD_Task() (see the comment at the call site). */
#define UI_OLED_SETTLE_MS          200u

/* Ignore key wake-ups for this long after entering UI_OFF: the press that
 * requested the sleep (and its release / DAS events) must not light the
 * screen up again ("manual sleep comes back on immediately"). */
#define PM_WAKE_ARM_MS             300u

static uint32_t s_app_boot_ms;

/* USB-PD GoodCRC reception is a sub-millisecond timing path.  During initial
 * SPR/EPR negotiation, nonessential display and telemetry traffic can create a
 * dense SPI/I2C/DMA interrupt load.  Keep the first negotiation window quiet;
 * after a valid power contract is ready, all normal UI and 333 ms telemetry
 * work resumes.  If a non-PD/DC source is used, the timeout prevents the UI
 * from being suppressed indefinitely. */
static uint8_t APP_NoncriticalIOAllowed(void)
{
    if(!PD_IsConnected())
        return 1u;

    if(PD_IsPowerReady())
        return 1u;

    if((uint32_t)(TIME_Millis() - s_app_boot_ms) >= PD_STARTUP_QUIET_WINDOW_MS)
        return 1u;

    return 0u;
}

/* Wake request while the UI is off: ui_loop() (and with it indevScan()) does
 * not run then, so the button events are read directly here.  Only a *fresh*
 * press may wake the device: the release edge, the DAS repeats of a held key
 * and the latched KeyState_Release of the key that requested the sleep must
 * not light the screen up again (that was the "manual sleep comes back on
 * immediately" bug).  Reading the edge is non-consuming - indevScan() still
 * sees the event once the UI runs again. */
static uint8_t app_pm_wake_key_pressed(void)
{
    uint8_t i;

    for(i = 0u; i < (uint8_t)KeyIndex_Max; i++)
    {
        if(Key_EdgeDetect((KeyIndex_t)i) == KeyEdge_Rising)
            return 1u;
    }

    return 0u;
}

/* Non-consuming key check for the awake path: every press/hold restarts the
 * idle countdown of the power manager.  Only ShortPress/LongPress count as
 * "down" - the driver leaves KeyState_Release latched after the first press
 * ever (until the next press), and treating that as activity used to refresh
 * the countdown on every pass (the "countdown stuck at 60 s" bug). */
static uint8_t app_pm_key_active(void)
{
    uint8_t i;
    KeyState_t st;

    for(i = 0u; i < (uint8_t)KeyIndex_Max; i++)
    {
        st = KEY_GetState((KeyIndex_t)i);
        if((st == KeyState_ShortPress) || (st == KeyState_LongPress) ||
           (Key_EdgeDetect((KeyIndex_t)i) != KeyEdge_Null))
            return 1u;
    }

    return 0u;
}

THRD_DECLARE(thread_ui)
{
    static uint32_t key_service_ms;
    static uint32_t now_ms;
    static uint32_t pm_sleep_ms;
    static uint8_t pm_sleep_seen;

    THRD_BEGIN;

    /* The panel is reset by an external RC network only, so it must not see
     * its first command until ~200 ms after power-up.  The wait happens here,
     * cooperatively - NOT inside the u8x8 init sequence: diapInit() runs in
     * this thread's first slice and a blocking 200 ms wait there (TIME_DelayMs
     * busy-wait) froze the whole cooperative loop.  PD_Task() was starved
     * right inside the Source_Capabilities response window (~30 ms), the PD
     * source then cut VBUS and the device reboot-looped every 1.2 s when
     * powered over USB-PD (reset cause: POR/PDR, GoodCRC failures in the log).
     * Sleeping here keeps every other thread (PD, telemetry, PM) running
     * during the settle time. */
    THRD_DELAY(UI_OLED_SETTLE_MS);

    diapInit();
    Key_Init();
    MiaoUi_Setup(&ui);
    key_service_ms = TIME_Millis();
    printf("[OLED] SH1107 ready: logical 128x80, native 80x128, external VPP, charge pump disabled\r\n");

    while(1)
    {
        THRD_DELAY(UI_FRAME_PERIOD_MS);
        now_ms = TIME_Millis();

        /* Key tick = a true 10 ms grid.  The UI frame is 8 ms, so a plain
         * "elapsed >= 10 ms" test only fired on every second frame (~16 ms,
         * drifting with frame load): the 1 s long press became ~1.6 s and the
         * 80 ms DAS repeat became ~128 ms, so a held DOWN key looked dead.
         * Advance the deadline by exactly one tick per service (bounded
         * catch-up; resync if the thread was starved for long). */
        if((uint32_t)(now_ms - key_service_ms) >= UI_KEY_SERVICE_MS)
        {
            uint8_t catch_up = 0u;

            do
            {
                key_service_ms += UI_KEY_SERVICE_MS;
                Key_DebounceService_10ms();
                ++catch_up;
            } while(((uint32_t)(now_ms - key_service_ms) >= UI_KEY_SERVICE_MS) && (catch_up < 4u));

            if((uint32_t)(now_ms - key_service_ms) >= UI_KEY_SERVICE_MS)
                key_service_ms = now_ms;   /* starved: resync the grid */

            Key_Scand();
        }

        if(pm_api_ui_should_block() || (pm_api_is_unstable_wake() != 0u))
        {
            /* UI_OFF: the display hook already blanked the panel and drawing
             * stays paused.  A fresh key press is a wake-up request here - the
             * action is swallowed so it cannot also trigger the menu (this
             * also covers the ~100 ms silence right after a wake).
             *
             * Arming delay: the press that requested the sleep and its
             * release/DAS events must not wake the device again, so key
             * wake-ups are ignored for PM_WAKE_ARM_MS after entering UI_OFF. */
            if(pm_sleep_seen == 0u)
            {
                pm_sleep_seen = 1u;
                pm_sleep_ms = now_ms;
            }

            if(((uint32_t)(now_ms - pm_sleep_ms) >= PM_WAKE_ARM_MS) &&
               (app_pm_wake_key_pressed() != 0u))
                pm_api_refresh_idle();

            ui.action = UI_ACTION_NONE;
        }
        else
        {
            pm_sleep_seen = 0u;

            if(APP_NoncriticalIOAllowed())
            {
                /* Any key press/hold counts as activity for the power manager
                 * (the check is non-consuming, see app_pm_key_active()). */
                if(app_pm_key_active() != 0u)
                    pm_api_refresh_idle();

                ui_loop(&ui);
            }
        }
    }
    THRD_END;
}

THRD_DECLARE(thread_soft_i2c_service)
{
    THRD_BEGIN;
    while(1)
    {
        SoftI2C_Service(&s_sw3526_bus1);
        SoftI2C_Service(&s_sw3526_bus2);
        THRD_YIELD;
    }
    THRD_END;
}

THRD_DECLARE(thread_i2c_watchdog)
{
    static uint32_t last_recovery_count;
    static I2C_API_Diagnostic d;
    THRD_BEGIN;
    last_recovery_count = I2C_API_GetRecoveryCount();
    while(1)
    {
        THRD_DELAY(10u);
        I2C_API_WatchdogService(TIME_Millis());
        if(I2C_API_GetRecoveryCount() != last_recovery_count)
        {
            last_recovery_count = I2C_API_GetRecoveryCount();
            I2C_API_GetDiagnostic(&d);
            printf("[I2C] recovery #%lu: %s SCL=%u SDA=%u STAR1=0x%04x STAR2=0x%04x\r\n",
                   (unsigned long)last_recovery_count, I2C_API_GetLastErrorName(),
                   d.scl_high, d.sda_high, d.star1, d.star2);
        }
    }
    THRD_END;
}

#define POWER_MIRROR_POLL_MS     333u   /* SW3538 / SW3526 power telemetry */
#define TEMP_MIRROR_POLL_MS      500u   /* GX21M15U temperature mirror */
#define POLL_PHASE_POWER_MS       50u
#define POLL_PHASE_GX21M15_MS    300u
#define POLL_PHASE_FAN_MS        100u

/* ---- Output power allocation ------------------------------------------------
 *
 * The dashboard supplies the total budget (PWR_Limit_GetW(): UVP 0 W, PD 95%
 * of the contract power, DC user value).  The split is rebuilt on every
 * power-mirror pass from the chips' measured output power (ADC mirror, 0 W
 * while nothing is attached):
 *   - each chip is provisioned with (budget - the other chips' measured
 *     power), capped by its controller ceiling - it keeps what it draws and
 *     additionally sees the still-free part of the budget;
 *   - a detached port measures 0 W, so its share is the free headroom left
 *     for the next attachment, and unplugging returns the share at once;
 *   - oversubscription and measurement ripple are accepted by design.
 * Shares are clamped into the range each SetPowerLimitW() accepts
 * (SW3538 18..140 W, SW3526 12..71 W) and written only when they change. */
/* The allocation rules above live in Services/power_alloc.c/.h: PWR_Alloc_Plan()
 * rebuilds the budget split from the measured power computed below, and the
 * PWR_Alloc_ApplyNeeded/Done() pair deduplicates the chip writes performed by
 * thread_power_mirror below. */

/* SW3526 attach detect: a plain Type-C 5 V sink only powers the port switch
 * (0x07.1 PORT_ON); the protocol-online bit (0x06.7) stays clear until a fast
 * charge protocol is actually engaged, so both sources are accepted. */
static uint8_t app_sw3526_attached(const SW3526_Handle *handle)
{
    return (uint8_t)((SW3526_IsOnline(handle) &&
                      (SW3526_IsPortOn(handle) ||
                       SW3526_IsProtocolOnline(handle))) ? 1u : 0u);
}

static uint8_t app_power_attached(uint8_t chip)
{
    if(chip == 0u)
    {
        return (uint8_t)((SW3538_IsOnline(&s_sw3538) &&
                (SW3538_IsPort1DeviceOnline(&s_sw3538) ||
                 SW3538_IsPort2DeviceOnline(&s_sw3538))) ? 1u : 0u);
    }

    if(chip == 1u)
        return app_sw3526_attached(&s_sw3526_1);

    return app_sw3526_attached(&s_sw3526_2);
}

/* Output power in whole watts from the ADC mirror.  The current values are in
 * 0.1 mA units, so W = mV * I(0.1 mA) / 10^7. */
static uint8_t app_power_w(uint16_t vout_mv, uint32_t iout_ma_x10)
{
    uint32_t w = ((uint32_t)vout_mv * iout_ma_x10) / 10000000u;

    return (uint8_t)((w > 255u) ? 255u : w);
}

/* The algorithm owns mask requests; only this task writes SW35xx devices. */
void APP_PortProtocol_Touch(uint8_t port,uint16_t mask)
{
    PortProto_AlgoRequest(port,mask,TIME_Millis());
}
static void app_proto_reinit_on_edge(void)
{
    uint32_t now=TIME_Millis();
    uint8_t reset=0u;
    reset |= PortProto_AlgoObserve(APP_PORT_PROTO_SW3538,SW3538_IsOnline(&s_sw3538),
                                   (uint16_t)SW3538_PROTOCOL_ENABLE_ALL,now);
    reset |= PortProto_AlgoObserve(APP_PORT_PROTO_SW3526_1,SW3526_IsOnline(&s_sw3526_1),
                                   (uint16_t)SW3526_PROTOCOL_ENABLE_ALL,now);
    reset |= PortProto_AlgoObserve(APP_PORT_PROTO_SW3526_2,SW3526_IsOnline(&s_sw3526_2),
                                   (uint16_t)SW3526_PROTOCOL_ENABLE_ALL,now);
    if(reset) PortPages_EnableAllProtocols();
}

/* One merged mirror thread: SW3538 (hardware I2C1) and both SW3526s (soft
 * I2C) are refreshed back-to-back, then the power budget is re-allocated. */
THRD_DECLARE(thread_power_mirror)
{
    static uint8_t share[PWR_ALLOC_CHIPS];
    static uint8_t meas_w[PWR_ALLOC_CHIPS];  /* measured output power: 3538 / C1 / C2 */
    static uint8_t w0;
    static uint8_t w1;
    static uint8_t w2;
    static uint8_t changed;
    static uint16_t proto_mask[PORT_PROTO_ALGO_PORTS];

    THRD_BEGIN;
    THRD_DELAY(POLL_PHASE_POWER_MS);
    while(1)
    {
        if(APP_NoncriticalIOAllowed())
        {
            THRD_SPAWN_ARGS(SW3538_StatusLoad, &s_sw3538);
            if(SW3538_IsOnline(&s_sw3538))
                THRD_SPAWN_ARGS(SW3538_ADCLoad, &s_sw3538);

            THRD_SPAWN_ARGS(SW3526_StatusLoad, &s_sw3526_1);
            if(SW3526_IsOnline(&s_sw3526_1))
                THRD_SPAWN_ARGS(SW3526_ADCLoad, &s_sw3526_1);

            THRD_SPAWN_ARGS(SW3526_StatusLoad, &s_sw3526_2);
            if(SW3526_IsOnline(&s_sw3526_2))
                THRD_SPAWN_ARGS(SW3526_ADCLoad, &s_sw3526_2);

            /* Controllers that just came back from offline get the protocol
             * defaults re-applied (see app_proto_reinit_on_edge()). */
            app_proto_reinit_on_edge();

            /* Measured output power per chip feeds the allocation; a port
             * without a device contributes 0 W. */
            meas_w[0] = app_power_attached(0u) ?
                        app_power_w(SW3538_GetStatus(&s_sw3538)->vout_mv,
                                    SW3538_GetStatus(&s_sw3538)->pt1_iout_ma_x10 +
                                    SW3538_GetStatus(&s_sw3538)->pt2_iout_ma_x10) : 0u;
            meas_w[1] = app_power_attached(1u) ?
                        app_power_w(SW3526_GetStatus(&s_sw3526_1)->vout_mv,
                                    SW3526_GetStatus(&s_sw3526_1)->iout_ma_x10) : 0u;
            meas_w[2] = app_power_attached(2u) ?
                        app_power_w(SW3526_GetStatus(&s_sw3526_2)->vout_mv,
                                    SW3526_GetStatus(&s_sw3526_2)->iout_ma_x10) : 0u;

            /* Measured power -> per-chip power limits (Services/power_alloc.c). */
            PWR_Alloc_Plan(meas_w, share);
            w0 = PWR_Alloc_ApiW(0u, share[0]);
            w1 = PWR_Alloc_ApiW(1u, share[1]);
            w2 = PWR_Alloc_ApiW(2u, share[2]);
            changed = 0u;

            if((PWR_Alloc_ApplyNeeded(0u, w0) != 0u) && (SW3538_IsOnline(&s_sw3538) != 0u))
            {
                THRD_SPAWN_ARGS(SW3538_SetPowerLimitW, &s_sw3538, w0);
                PWR_Alloc_ApplyDone(0u, w0);
                changed = 1u;
            }

            if((PWR_Alloc_ApplyNeeded(1u, w1) != 0u) && (SW3526_IsOnline(&s_sw3526_1) != 0u))
            {
                THRD_SPAWN_ARGS(SW3526_SetPowerLimitW, &s_sw3526_1, w1);
                PWR_Alloc_ApplyDone(1u, w1);
                changed = 1u;
            }

            if((PWR_Alloc_ApplyNeeded(2u, w2) != 0u) && (SW3526_IsOnline(&s_sw3526_2) != 0u))
            {
                THRD_SPAWN_ARGS(SW3526_SetPowerLimitW, &s_sw3526_2, w2);
                PWR_Alloc_ApplyDone(2u, w2);
                changed = 1u;
            }

            if(changed != 0u)
            {
                printf("[PWR] budget %lu W -> 3538 %u W / C1 %u W / C2 %u W\r\n",
                       (unsigned long)PWR_Limit_GetW(),
                       (unsigned)w0, (unsigned)w1, (unsigned)w2);
            }

            /* Only the coroutine issues I2C writes. These static masks
             * survive every THRD_SPAWN_ARGS yield. */
            if(PortProto_AlgoTakeDue(APP_PORT_PROTO_SW3538,SW3538_IsOnline(&s_sw3538),
                                      TIME_Millis(),&proto_mask[0]))
            {
                THRD_SPAWN_ARGS(SW3538_SetProtocolMask,&s_sw3538,
                                (SW3538_ProtocolMask)proto_mask[0]);
                printf("[CFG] SW3538 protocol mask 0x%03X\r\n",(unsigned)proto_mask[0]);
            }
            if(PortProto_AlgoTakeDue(APP_PORT_PROTO_SW3526_1,SW3526_IsOnline(&s_sw3526_1),
                                      TIME_Millis(),&proto_mask[1]))
            {
                THRD_SPAWN_ARGS(SW3526_SetProtocolMask,&s_sw3526_1,
                                (SW3526_ProtocolMask)proto_mask[1]);
                printf("[CFG] SW3526 C1 protocol mask 0x%03X\r\n",(unsigned)proto_mask[1]);
            }
            if(PortProto_AlgoTakeDue(APP_PORT_PROTO_SW3526_2,SW3526_IsOnline(&s_sw3526_2),
                                      TIME_Millis(),&proto_mask[2]))
            {
                THRD_SPAWN_ARGS(SW3526_SetProtocolMask,&s_sw3526_2,
                                (SW3526_ProtocolMask)proto_mask[2]);
                printf("[CFG] SW3526 C2 protocol mask 0x%03X\r\n",(unsigned)proto_mask[2]);
            }
            if(PortProto_AlgoTakePaths(SW3538_IsOnline(&s_sw3538)))
            {
                THRD_SPAWN_ARGS(SW3538_SetPortFastChargeEnabled,&s_sw3538,1u,1u);
                THRD_SPAWN_ARGS(SW3538_SetPortFastChargeEnabled,&s_sw3538,2u,1u);
                printf("[CFG] SW3538 fast-charge paths: PT1+PT2 enabled\r\n");
            }
        }
        THRD_DELAY(POWER_MIRROR_POLL_MS);
    }
    THRD_END;
}

THRD_DECLARE(thread_gx21m15)
{
    THRD_BEGIN;
    THRD_DELAY(POLL_PHASE_GX21M15_MS);
    while(1)
    {
        if(APP_NoncriticalIOAllowed())
            THRD_SPAWN_ARGS(GX21M15_TemperatureLoad, &s_gx21m15);
        THRD_DELAY(TEMP_MIRROR_POLL_MS);
    }
    THRD_END;
}

/* Automatic fan speed.  Pure RAM work (it only reads the 500 ms GX21M15
 * mirror), so unlike the device mirrors it also runs during the PD quiet
 * window - thermal protection must not wait for a PD contract.  Two guards
 * keep the fail-safe full-speed blast away from startup and bus glitches: a
 * missing sensor counts only after FAN_SENSOR_STRIKES consecutive failed
 * samples, and never during the first FAN_SENSOR_GRACE_MS (the first
 * temperature sample needs the 300 ms I2C phase plus up to the 1 s quiet
 * window, so every power-up would otherwise jump straight to 255). */
#define FAN_POLL_MS          500u
#define FAN_SENSOR_STRIKES   3u
#define FAN_SENSOR_GRACE_MS 3000u

THRD_DECLARE(thread_fan)
{
    static int32_t temp_mc;
    static int16_t temp_c;
    static uint8_t online;
    static uint8_t sensor_ok;
    static uint8_t missing;
    static uint8_t uvp;
    static uint8_t pwm;
    static uint8_t applied;
    static uint8_t applied_valid;

    THRD_BEGIN;
    THRD_DELAY(POLL_PHASE_FAN_MS);
    while(1)
    {
        uvp = (uint8_t)((VBUS_Sense_ReadMillivolts() < PWR_LIMIT_UVP_MV) ? 1u : 0u);
        online = GX21M15_IsOnline(&s_gx21m15);
        temp_mc = GX21M15_ReadTemperatureMilliC(&s_gx21m15);
        temp_c = (int16_t)((temp_mc >= 0) ? ((temp_mc + 500) / 1000)
                                          : ((temp_mc - 500) / 1000));

        if(online != 0u)
        {
            missing = 0u;
        }
        else if(missing < 255u)
        {
            missing++;
        }

        sensor_ok = 1u;
        if((missing >= (uint8_t)FAN_SENSOR_STRIKES) &&
           ((uint32_t)(TIME_Millis() - s_app_boot_ms) >= FAN_SENSOR_GRACE_MS))
        {
            sensor_ok = 0u;      /* truly missing -> the policy returns 255 */
        }

        pwm = FAN_Control_NextPwm(temp_c, sensor_ok, uvp);

        if((applied_valid == 0u) || (pwm != applied))
        {
            applied = pwm;
            applied_valid = 1u;
            FAN_PWM_SetDuty8(pwm);
            printf("[FAN] %dC online=%u uvp=%u -> pwm %u\r\n",
                   (int)temp_c, (unsigned)online, (unsigned)uvp, (unsigned)pwm);
        }

        THRD_DELAY(FAN_POLL_MS);
    }
    THRD_END;
}

/* ---- Power management (UI_OFF level) ---------------------------------------
 *
 * Activity sources that reset the idle timer (see APP/framework/pm_api.h):
 *   - any key press (thread_ui);
 *   - PD/VBUS or output-port plug/unplug edges;
 *   - more than PM_ACTIVITY_POWER_MW (5 W) leaving the outputs.
 * thread_pm only drives the framework state machine; the visible effect
 * (screen off/on) is the OLED hook in APP/framework/pm_device_builtin.c. */
#define PM_POLL_MS            50u
#define POLL_PHASE_PM_MS     200u
#define PM_ACTIVITY_POWER_MW 5000u

/* Total output power from the 333 ms power mirrors (same integer math as the
 * dashboard: SW3538 column = shared VOUT x both port currents). */
static uint32_t app_output_power_mw(void)
{
    const struct SW3526_StatusTypedef *st;
    uint32_t mw = 0u;

    mw += ((uint32_t)SW3538_ReadVOUTmV(&s_sw3538) *
           ((SW3538_ReadPort1IOUTmA_x10(&s_sw3538) +
             SW3538_ReadPort2IOUTmA_x10(&s_sw3538)) / 10u)) / 1000u;

    st = SW3526_GetStatus(&s_sw3526_1);
    if(st != 0)
        mw += ((uint32_t)st->vout_mv * (st->iout_ma_x10 / 10u)) / 1000u;

    st = SW3526_GetStatus(&s_sw3526_2);
    if(st != 0)
        mw += ((uint32_t)st->vout_mv * (st->iout_ma_x10 / 10u)) / 1000u;

    return mw;
}

/* Attach state of the input and of every output port: any edge wakes the UI. */
static uint8_t app_power_activity_signature(void)
{
    uint8_t sig = 0u;

    if(PD_IsConnected())
        sig |= 0x01u;
    if(VBUS_Sense_ReadMillivolts() >= PWR_LIMIT_UVP_MV)
        sig |= 0x02u;
    if(SW3538_IsPort1DeviceOnline(&s_sw3538))
        sig |= 0x04u;
    if(SW3538_IsPort2DeviceOnline(&s_sw3538))
        sig |= 0x08u;
    if(app_sw3526_attached(&s_sw3526_1))
        sig |= 0x10u;
    if(app_sw3526_attached(&s_sw3526_2))
        sig |= 0x20u;

    return sig;
}

THRD_DECLARE(thread_pm)
{
    static uint8_t sig;
    static uint8_t sig_valid;
    static uint8_t now_sig;

    THRD_BEGIN;
    THRD_DELAY(POLL_PHASE_PM_MS);
    while(1)
    {
        now_sig = app_power_activity_signature();
        if((sig_valid == 0u) || (now_sig != sig))
        {
            sig = now_sig;
            sig_valid = 1u;
            pm_api_refresh_idle();
        }

        if(app_output_power_mw() > PM_ACTIVITY_POWER_MW)
            pm_api_refresh_idle();

        pm_api_poll();
        THRD_DELAY(PM_POLL_MS);
    }
    THRD_END;
}

static const coro_thread_fn_t s_threads[] =
{
    thread_pd,
    thread_vbus,
    thread_ui,
    thread_soft_i2c_service,
    thread_i2c_watchdog,
    thread_power_mirror,
    thread_gx21m15,
    thread_fan,
    thread_pm
};
static coro_pt_t s_thread_states[sizeof(s_threads) / sizeof(s_threads[0])];

void APP_Tasks_Init(void)
{
    printf("\r\n=== Desktop PD Power Converter / CH32X035C8T6 ===\r\n");
    APP_PrintResetCause();
    printf("SystemClk:%lu Hz ChipID:%08lx\r\n",
           (unsigned long)SystemCoreClock, (unsigned long)DBGMCU_GetCHIPID());
    printf("UART1: PB10 TX / PB11 RX @ 921600, DMA async\r\n");
    printf("Fan PWM: PB9 TIM1_CH1 @ %lu Hz, %u levels\r\n",
           (unsigned long)FAN_PWM_GetFrequencyHz(), (unsigned)(FAN_PWM_MAX_LEVEL + 1U));
    printf("Display SPI1: PA11 SCK / PA10 MOSI @ %lu Hz, DMA1 CH3 TX + IRQ, NSS=PA12\r\n",
           (unsigned long)SPI_DMA_GetClockHz());

    Board_Init();
    FAN_PWM_Init();
    SPI_DMA_Init();
    VBUS_Sense_Init();

    I2C_API_Init(I2C_API_DEFAULT_CLOCK_HZ);
    SW3538_HandleInit(&s_sw3538, I2C_API_OWNER_SW3538);
    GX21M15_HandleInit(&s_gx21m15, I2C_API_OWNER_GX21M15U, GX21M15_I2C_ADDR_DEFAULT);
    printf("I2C1: PA13 SCL / PA14 SDA @ %lu Hz, EV/ER + TX/RX DMA IRQ; SW3538 + GX21M15 ready\r\n",
           (unsigned long)I2C_API_GetClockHz());

    s_sw3526_bus1.scl_port = SW3526_1_SCL_GPIO_Port;
    s_sw3526_bus1.scl_pin = SW3526_1_SCL_Pin;
    s_sw3526_bus1.sda_port = SW3526_1_SDA_GPIO_Port;
    s_sw3526_bus1.sda_pin = SW3526_1_SDA_Pin;
    s_sw3526_bus1.half_period_us = SOFT_I2C_DEFAULT_HALF_PERIOD_US;
    s_sw3526_bus1.stretch_timeout_us = SOFT_I2C_DEFAULT_STRETCH_TIMEOUT_US;
    SoftI2C_Init(&s_sw3526_bus1);
    SW3526_HandleInit(&s_sw3526_1, &s_sw3526_bus1);

    s_sw3526_bus2.scl_port = SW3526_2_SCL_GPIO_Port;
    s_sw3526_bus2.scl_pin = SW3526_2_SCL_Pin;
    s_sw3526_bus2.sda_port = SW3526_2_SDA_GPIO_Port;
    s_sw3526_bus2.sda_pin = SW3526_2_SDA_Pin;
    s_sw3526_bus2.half_period_us = SOFT_I2C_DEFAULT_HALF_PERIOD_US;
    s_sw3526_bus2.stretch_timeout_us = SOFT_I2C_DEFAULT_STRETCH_TIMEOUT_US;
    SoftI2C_Init(&s_sw3526_bus2);
    SW3526_HandleInit(&s_sw3526_2, &s_sw3526_bus2);
    printf("Soft-I2C: SW3526#1 PA4/PA3, SW3526#2 PA1/PA2; independent handles\r\n");

    /* Startup protocol defaults: enable every protocol family on all three
     * controllers so vendor OTP differences cannot leave functions disabled,
     * and put the protocol pages on the same all-enabled baseline.  The same
     * defaults are re-applied whenever a controller returns from offline
     * (app_proto_reinit_on_edge() in thread_power_mirror): a reset chip falls
     * back to its OTP state, so every reconnection gets one fresh push
     * through the debounced writer. Fast-charge paths are also restored on
     * reconnect through the integer-only port protocol algorithm. */
    PortPages_EnableAllProtocols();
    APP_PortProtocol_Touch(APP_PORT_PROTO_SW3538, (uint16_t)SW3538_PROTOCOL_ENABLE_ALL);
    APP_PortProtocol_Touch(APP_PORT_PROTO_SW3526_1, (uint16_t)SW3526_PROTOCOL_ENABLE_ALL);
    APP_PortProtocol_Touch(APP_PORT_PROTO_SW3526_2, (uint16_t)SW3526_PROTOCOL_ENABLE_ALL);

    printf("PD Sink: SPR <= %u mV; EPR nominal %u mV / %u mA; policy <= %u mA\r\n",
           (unsigned)PD_SPR_MAX_FIXED_MV,
           (unsigned)PD_EPR_NOMINAL_MV,
           (unsigned)PD_NOMINAL_REQUEST_MA,
           (unsigned)PD_POLICY_REQUEST_MAX_MA);

    s_app_boot_ms = TIME_Millis();
    pm_api_init();

    CoroOS_Init(&s_scheduler, s_threads, s_thread_states,
                (uint8_t)(sizeof(s_threads) / sizeof(s_threads[0])));

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_32);
    IWDG_SetReload(4000u);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void APP_Tasks_RunOnce(void)
{
    CoroOS_RunOnce(&s_scheduler);
}

void APP_Tasks_Idle(void)
{
    IWDG_ReloadCounter();

    if(SoftI2C_IsBusy(&s_sw3526_bus1) || SoftI2C_IsBusy(&s_sw3526_bus2))
        return;

    if(PD_WantsFastPoll())
        return;

    __WFI();
}

SW3538_Handle *APP_GetSW3538(void)
{
    return &s_sw3538;
}

SW3526_Handle *APP_GetSW3526_1(void)
{
    return &s_sw3526_1;
}

SW3526_Handle *APP_GetSW3526_2(void)
{
    return &s_sw3526_2;
}

GX21M15_Handle *APP_GetGX21M15(void)
{
    return &s_gx21m15;
}
