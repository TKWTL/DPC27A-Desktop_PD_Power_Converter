#include "app_tasks.h"
#include "main.h" /* independent compile-time telemetry and diagnostics flags */
#include "coroOS.h"
#include "debug.h"
#include "pd.h"
#include "Services/power_limit.h"
#include "Services/power_alloc.h"
#include "Services/fan_control.h"
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
#include "indev/indevDriver.h"
#include "display/dispDriver.h"
#include "time_api.h"

static coro_scheduler_t s_scheduler;

static SoftI2C_Handle s_sw3526_bus1;
static SoftI2C_Handle s_sw3526_bus2;
static SW3526_Handle s_sw3526_1;
static SW3526_Handle s_sw3526_2;
static SW3538_Handle s_sw3538;
static GX21M15_Handle s_gx21m15;
static uint8_t s_temperature_sources = APP_TEMP_SOURCE_ALL;

ui_t ui;

void APP_SetTemperatureSources(uint8_t mask)
{
    s_temperature_sources = (uint8_t)(mask & APP_TEMP_SOURCE_ALL);
}

uint8_t APP_GetTemperatureSources(void)
{
    return s_temperature_sources;
}

/* Read original telemetry even if excluded from control; Statistics is a
 * diagnostic view. All temperatures share signed tenths of a degree C. */
uint8_t APP_ReadTemperatureX10(uint8_t source, int16_t *value)
{
    int32_t mc;
    if(value == 0)
        return 0u;
    if(source == APP_TEMP_SOURCE_1)
    {
        if(!GX21M15_IsOnline(&s_gx21m15))
            return 0u;
        mc = GX21M15_ReadTemperatureMilliC(&s_gx21m15);
        *value = (int16_t)((mc >= 0 ? mc + 50 : mc - 50) / 100);
        return 1u;
    }
    if(source == APP_TEMP_SOURCE_2)
        return SW3538_ReadNTCTemperatureX10(&s_sw3538, value);
    return 0u;
}

uint8_t APP_ReadControlTemperatureX10(int16_t *value)
{
    int16_t t1 = 0, t2 = 0;
    uint8_t v1, v2;
    int32_t sum;
    if(value == 0)
        return 0u;
    v1 = (s_temperature_sources & APP_TEMP_SOURCE_1) &&
         APP_ReadTemperatureX10(APP_TEMP_SOURCE_1, &t1);
    v2 = (s_temperature_sources & APP_TEMP_SOURCE_2) &&
         APP_ReadTemperatureX10(APP_TEMP_SOURCE_2, &t2);
    if(v1 && v2)
    {
        sum = (int32_t)t1 + t2;
        *value = (int16_t)((sum + (sum >= 0 ? 1 : -1)) / 2);
    }
    else if(v1)
        *value = t1;
    else if(v2)
        *value = t2;
    else
        return 0u;
    return 1u;
}

static void APP_PrintResetCause(void)
{
#if DPC_LOG_STARTUP_ENABLE
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
#endif
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
#if DPC_DIAGNOSTICS_ENABLE
    static uint8_t log_div;
    static uint32_t sleep_left;
#endif
    THRD_BEGIN;
#if DPC_DIAGNOSTICS_ENABLE
    log_div = 0u;
#endif
    while(1)
    {
        THRD_DELAY(20u);
        mv = VBUS_Sense_ReadMillivolts();
        PD_SetVbusMillivolts(mv);
#if DPC_DIAGNOSTICS_ENABLE
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
#endif
    }
    THRD_END;
}

#define UI_FRAME_PERIOD_MS         6u
#define UI_KEY_SERVICE_MS          KEY_SERVICE_PERIOD_MS
#define UI_DISPATCH_MS             2u
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
 * immediately" bug).  Reading the edge is non-consuming; the wake gesture is not queued. */
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

/* Only defer expensive foreground rendering while a soft-I2C byte stream
 * is active. PD, input scanning and the other scheduled tasks remain live. */
static uint8_t app_soft_i2c_busy(void)
{
    return (uint8_t)(SoftI2C_IsBusy(&s_sw3526_bus1) || SoftI2C_IsBusy(&s_sw3526_bus2));
}

THRD_DECLARE(thread_ui)
{
    static uint32_t key_service_ms;
    static uint32_t frame_ms;
    static uint32_t dispatch_ms;
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
    THRD_UNTIL(!app_soft_i2c_busy());

    diapInit();
    Key_Init();
    MiaoUi_Setup(&ui);
    key_service_ms = TIME_Millis();
    frame_ms = key_service_ms;
    dispatch_ms = key_service_ms;
    indevReset();
#if DPC_DIAGNOSTICS_ENABLE
    printf("[OLED] SH1107 ready: logical 128x80, native 80x128, external VPP, charge pump disabled\r\n");
#endif

    while(1)
    {
        /* Normally dispatch every 2 ms. Also wake at a transaction boundary
         * for an overdue frame: otherwise a new transfer can start in the
         * same pass before the next 2 ms dispatch becomes due. */
        THRD_UNTIL(((uint32_t)(TIME_Millis() - dispatch_ms) >= UI_DISPATCH_MS) ||
                   (!app_soft_i2c_busy() &&
                    (uint32_t)(TIME_Millis() - frame_ms) >= UI_FRAME_PERIOD_MS &&
                    !pm_api_ui_should_block() && !pm_api_is_unstable_wake() &&
                    APP_NoncriticalIOAllowed()));
        now_ms = TIME_Millis();
        dispatch_ms = now_ms;

        /* Independent 8 ms key grid and 6 ms draw grid. A delayed dispatch
         * advances counters (bounded) but never invents GPIO observations. */
        uint8_t scanned = 0u;
        if((uint32_t)(now_ms - key_service_ms) >= UI_KEY_SERVICE_MS)
        {
            uint8_t catch_up = 0u;
            do
            {
                key_service_ms += UI_KEY_SERVICE_MS;
                Key_DebounceService_8ms();
                ++catch_up;
            } while(((uint32_t)(now_ms - key_service_ms) >= UI_KEY_SERVICE_MS) && (catch_up < 4u));

            if((uint32_t)(now_ms - key_service_ms) >= UI_KEY_SERVICE_MS)
                key_service_ms = now_ms;

            Key_Scand();
            scanned = 1u;
        }

        if(pm_api_ui_should_block() || (pm_api_is_unstable_wake() != 0u))
        {
            if(pm_sleep_seen == 0u)
            {
                pm_sleep_seen = 1u;
                pm_sleep_ms = now_ms;
            }

            if(scanned && ((uint32_t)(now_ms - pm_sleep_ms) >= PM_WAKE_ARM_MS) &&
               (app_pm_wake_key_pressed() != 0u))
                pm_api_wake_from_activity();

            /* Swallow the entire wake gesture, including its later release. */
            indevReset();
            ui.action = UI_ACTION_NONE;
        }
        else
        {
            pm_sleep_seen = 0u;

            if(APP_NoncriticalIOAllowed())
            {
                if(scanned)
                {
                    if(app_pm_key_active() != 0u)
                        pm_api_refresh_idle();
                    indevPoll();
                }

                /* The service runs before UI; a completed transaction
                 * leaves a render opportunity before power_mirror can start
                 * the next one. Keep due frames pending, not catch-up bursts. */
                if((uint32_t)(now_ms - frame_ms) >= UI_FRAME_PERIOD_MS &&
                   !app_soft_i2c_busy())
                {
                    frame_ms = now_ms; /* no catch-up rendering burst */
                    Statistics_Poll();
                    ui_loop(&ui);
                }
            }
            else
            {
                indevReset();
                ui.action = UI_ACTION_NONE;
            }
        }
        /* A slow draw may make the 2 ms dispatch deadline already due.
         * Never execute two UI passes back-to-back inside one coroutine call. */
        THRD_YIELD;
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
    THRD_BEGIN;
    while(1)
    {
        THRD_DELAY(10u);
        I2C_API_WatchdogService(TIME_Millis());
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

/* ---- Port fast-charge protocol masks (UI requests + boot defaults) --------
 * The MiaoUI protocol pages own the desired mask; this layer only forwards
 * the latest request to the chip once the switching activity settles, since
 * a mask write makes an attached sink renegotiate.  APP_Tasks_Init() pushes
 * an "every family enabled" mask through the same path at boot, and the
 * power-mirror pass repeats it whenever a controller returns from offline
 * (see app_proto_reinit_on_edge()), so vendor OTP differences cannot leave
 * protocol functions disabled.  The write runs inside thread_power_mirror so
 * every transfer on a handle stays in one thread. */
#define PORT_PROTO_DEBOUNCE_MS   800u

static uint32_t s_proto_req_ms[3];
static uint16_t s_proto_req_mask[3];
static uint8_t  s_proto_req_pending[3];
static uint8_t  s_sw3538_paths_pending = 1u;   /* push PT1/PT2 fast charge once per online edge */
static uint8_t  s_chip_online_prev[3];         /* last pass online state, for the reconnect re-init */

void APP_PortProtocol_Touch(uint8_t port, uint16_t mask)
{
    if(port >= 3u)
        return;

    s_proto_req_mask[port] = mask;
    s_proto_req_ms[port] = TIME_Millis();
    s_proto_req_pending[port] = 1u;
}

/* Re-apply the boot protocol defaults on every controller offline -> online
 * edge.  A controller that reset falls back to its vendor OTP state, so its
 * all-enabled mask (and, for the SW3538, both fast-charge paths) is pushed
 * again - once per reconnection, through the same debounced writer.  The
 * protocol pages return to the all-enabled baseline as well (single-byte
 * writes, picked up by the next UI frame).  Called once per power-mirror
 * pass, after the status refresh. */
static void app_proto_reinit_on_edge(void)
{
    uint8_t online;

    online = (SW3538_IsOnline(&s_sw3538) != 0u) ? 1u : 0u;
    if((online != 0u) && (s_chip_online_prev[0] == 0u))
    {
        s_sw3538_paths_pending = 1u;
        APP_PortProtocol_Touch(APP_PORT_PROTO_SW3538,
                               (uint16_t)SW3538_PROTOCOL_ENABLE_ALL);
        PortPages_EnableAllProtocols();
    }
    s_chip_online_prev[0] = online;

    online = (SW3526_IsOnline(&s_sw3526_1) != 0u) ? 1u : 0u;
    if((online != 0u) && (s_chip_online_prev[1] == 0u))
    {
        APP_PortProtocol_Touch(APP_PORT_PROTO_SW3526_1,
                               (uint16_t)SW3526_PROTOCOL_ENABLE_ALL);
        PortPages_EnableAllProtocols();
    }
    s_chip_online_prev[1] = online;

    online = (SW3526_IsOnline(&s_sw3526_2) != 0u) ? 1u : 0u;
    if((online != 0u) && (s_chip_online_prev[2] == 0u))
    {
        APP_PortProtocol_Touch(APP_PORT_PROTO_SW3526_2,
                               (uint16_t)SW3526_PROTOCOL_ENABLE_ALL);
        PortPages_EnableAllProtocols();
    }
    s_chip_online_prev[2] = online;
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
#if DPC_LOG_POWER_ALLOC_ENABLE
    static uint8_t changed;
#endif

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
#if DPC_LOG_POWER_ALLOC_ENABLE
            changed = 0u;
#endif

            if((PWR_Alloc_ApplyNeeded(0u, w0) != 0u) && (SW3538_IsOnline(&s_sw3538) != 0u))
            {
                THRD_SPAWN_ARGS(SW3538_SetPowerLimitW, &s_sw3538, w0);
                PWR_Alloc_ApplyDone(0u, w0);
#if DPC_LOG_POWER_ALLOC_ENABLE
                changed = 1u;
#endif
            }

            if((PWR_Alloc_ApplyNeeded(1u, w1) != 0u) && (SW3526_IsOnline(&s_sw3526_1) != 0u))
            {
                THRD_SPAWN_ARGS(SW3526_SetPowerLimitW, &s_sw3526_1, w1);
                PWR_Alloc_ApplyDone(1u, w1);
#if DPC_LOG_POWER_ALLOC_ENABLE
                changed = 1u;
#endif
            }

            if((PWR_Alloc_ApplyNeeded(2u, w2) != 0u) && (SW3526_IsOnline(&s_sw3526_2) != 0u))
            {
                THRD_SPAWN_ARGS(SW3526_SetPowerLimitW, &s_sw3526_2, w2);
                PWR_Alloc_ApplyDone(2u, w2);
#if DPC_LOG_POWER_ALLOC_ENABLE
                changed = 1u;
#endif
            }

#if DPC_LOG_POWER_ALLOC_ENABLE
            if(changed != 0u)
            {
                printf("[PWR] budget %lu W -> 3538 %u W / C1 %u W / C2 %u W\r\n",
                       (unsigned long)PWR_Limit_GetW(),
                       (unsigned)w0, (unsigned)w1, (unsigned)w2);
            }
#endif

            /* Debounced port protocol-mask writes requested by the UI. */
            if((s_proto_req_pending[APP_PORT_PROTO_SW3538] != 0u) &&
               ((TIME_Millis() - s_proto_req_ms[APP_PORT_PROTO_SW3538]) >=
                PORT_PROTO_DEBOUNCE_MS) &&
               (SW3538_IsOnline(&s_sw3538) != 0u))
            {
                s_proto_req_pending[APP_PORT_PROTO_SW3538] = 0u;
                THRD_SPAWN_ARGS(SW3538_SetProtocolMask, &s_sw3538,
                                (SW3538_ProtocolMask)s_proto_req_mask[APP_PORT_PROTO_SW3538]);
#if DPC_DIAGNOSTICS_ENABLE
                printf("[CFG] SW3538 protocol mask 0x%03X\r\n",
                       (unsigned)s_proto_req_mask[APP_PORT_PROTO_SW3538]);
#endif
            }

            if((s_proto_req_pending[APP_PORT_PROTO_SW3526_1] != 0u) &&
               ((TIME_Millis() - s_proto_req_ms[APP_PORT_PROTO_SW3526_1]) >=
                PORT_PROTO_DEBOUNCE_MS) &&
               (SW3526_IsOnline(&s_sw3526_1) != 0u))
            {
                s_proto_req_pending[APP_PORT_PROTO_SW3526_1] = 0u;
                THRD_SPAWN_ARGS(SW3526_SetProtocolMask, &s_sw3526_1,
                                (SW3526_ProtocolMask)s_proto_req_mask[APP_PORT_PROTO_SW3526_1]);
#if DPC_DIAGNOSTICS_ENABLE
                printf("[CFG] SW3526 C1 protocol mask 0x%03X\r\n",
                       (unsigned)s_proto_req_mask[APP_PORT_PROTO_SW3526_1]);
#endif
            }

            if((s_proto_req_pending[APP_PORT_PROTO_SW3526_2] != 0u) &&
               ((TIME_Millis() - s_proto_req_ms[APP_PORT_PROTO_SW3526_2]) >=
                PORT_PROTO_DEBOUNCE_MS) &&
               (SW3526_IsOnline(&s_sw3526_2) != 0u))
            {
                s_proto_req_pending[APP_PORT_PROTO_SW3526_2] = 0u;
                THRD_SPAWN_ARGS(SW3526_SetProtocolMask, &s_sw3526_2,
                                (SW3526_ProtocolMask)s_proto_req_mask[APP_PORT_PROTO_SW3526_2]);
#if DPC_DIAGNOSTICS_ENABLE
                printf("[CFG] SW3526 C2 protocol mask 0x%03X\r\n",
                       (unsigned)s_proto_req_mask[APP_PORT_PROTO_SW3526_2]);
#endif
            }

            /* Boot default, once: enable both SW3538 fast-charge paths (the
             * UI has no switch for these bits and vendor OTP may ship them
             * disabled). */
            if((s_sw3538_paths_pending != 0u) && (SW3538_IsOnline(&s_sw3538) != 0u))
            {
                s_sw3538_paths_pending = 0u;
                THRD_SPAWN_ARGS(SW3538_SetPortFastChargeEnabled, &s_sw3538, 1u, 1u);
                THRD_SPAWN_ARGS(SW3538_SetPortFastChargeEnabled, &s_sw3538, 2u, 1u);
#if DPC_DIAGNOSTICS_ENABLE
                printf("[CFG] SW3538 fast-charge paths: PT1+PT2 enabled\r\n");
#endif
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

/* Automatic fan speed: shared enabled/valid sensor fusion. No bus access.
 * Missing feedback goes straight to the fail-safe on the next fan poll;
 * existing input UVP remains the highest-priority override. */
#define FAN_POLL_MS          500u

THRD_DECLARE(thread_fan)
{
    static uint8_t applied;
    static uint8_t applied_valid;

    THRD_BEGIN;
    THRD_DELAY(POLL_PHASE_FAN_MS);
    while(1)
    {
        int16_t temp_x10 = 0;
        uint8_t online = APP_ReadControlTemperatureX10(&temp_x10);
        int16_t temp_c = (int16_t)((temp_x10 >= 0 ? temp_x10 + 5 : temp_x10 - 5) / 10);
        uint8_t uvp = (uint8_t)(VBUS_Sense_ReadMillivolts() < PWR_LIMIT_UVP_MV);
        uint8_t pwm = FAN_Control_NextPwm(temp_c, online, uvp);

        if((applied_valid == 0u) || (pwm != applied))
        {
            applied = pwm;
            applied_valid = 1u;
            FAN_PWM_SetDuty8(pwm);
#if DPC_LOG_FAN_CHANGE_ENABLE
            printf("[FAN] %dC online=%u uvp=%u -> pwm %u\r\n",
                   (int)temp_c, (unsigned)online, (unsigned)uvp, (unsigned)pwm);
#endif
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
/* 2 W accumulated step filters measurement noise; 5 W crossings always wake.
 * A steady >5 W output blocks automatic sleep but must not wake UI_OFF. */
#define PM_ACTIVITY_WAKE_DELTA_MW 2000u

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
    static uint8_t power_valid;
    static uint8_t above_threshold;
    static uint32_t power_mw;
    static uint32_t power_reference_mw;
    static uint32_t power_diff_mw;

    THRD_BEGIN;
    THRD_DELAY(POLL_PHASE_PM_MS);
    while(1)
    {
        now_sig = app_power_activity_signature();
        if(sig_valid == 0u)
        {
            sig = now_sig;
            sig_valid = 1u;
            pm_api_refresh_idle(); /* baseline, not an edge */
        }
        else if(now_sig != sig)
        {
            sig = now_sig;
            pm_api_wake_from_activity(); /* PD/VBUS/port attach or detach */
        }

        power_mw = app_output_power_mw();
        above_threshold = (power_mw > PM_ACTIVITY_POWER_MW) ? 1u : 0u;
        if(power_valid == 0u)
        {
            power_reference_mw = power_mw;
            power_valid = 1u;
        }
        else
        {
            power_diff_mw = (power_mw >= power_reference_mw) ?
                            (power_mw - power_reference_mw) :
                            (power_reference_mw - power_mw);
            if(((power_mw > PM_ACTIVITY_POWER_MW) !=
                (power_reference_mw > PM_ACTIVITY_POWER_MW)) ||
               (power_diff_mw >= PM_ACTIVITY_WAKE_DELTA_MW))
            {
                power_reference_mw = power_mw;
                pm_api_wake_from_activity(); /* actual load step */
            }
        }

        pm_api_set_sleep_block(PM_BLOCK_OUTPUT_POWER, above_threshold);
        pm_api_poll();
        THRD_DELAY(PM_POLL_MS);
    }
    THRD_END;
}

static const coro_thread_fn_t s_threads[] =
{
    thread_pd,
    thread_vbus,
    thread_soft_i2c_service,
    thread_ui,
    thread_i2c_watchdog,
    thread_power_mirror,
    thread_gx21m15,
    thread_fan,
    thread_pm
};
static coro_pt_t s_thread_states[sizeof(s_threads) / sizeof(s_threads[0])];

void APP_Tasks_Init(void)
{
#if DPC_LOG_STARTUP_ENABLE
    printf("\r\n=== Desktop PD Power Converter / CH32X035C8T6 ===\r\n");
#endif
    APP_PrintResetCause();
#if DPC_LOG_STARTUP_ENABLE
    printf("SystemClk:%lu Hz ChipID:%08lx\r\n",
           (unsigned long)SystemCoreClock, (unsigned long)DBGMCU_GetCHIPID());
    printf("UART1: PB10 TX / PB11 RX @ 921600, DMA async\r\n");
    printf("Fan PWM: PB9 TIM1_CH1 @ %lu Hz, %u levels\r\n",
           (unsigned long)FAN_PWM_GetFrequencyHz(), (unsigned)(FAN_PWM_MAX_LEVEL + 1U));
    printf("Display SPI1: PA11 SCK / PA10 MOSI @ %lu Hz, DMA1 CH3 TX + IRQ, NSS=PA12\r\n",
           (unsigned long)SPI_DMA_GetClockHz());
#endif

    Board_Init();
    FAN_PWM_Init();
    SPI_DMA_Init();
    VBUS_Sense_Init();

    I2C_API_Init(I2C_API_DEFAULT_CLOCK_HZ);
    SW3538_HandleInit(&s_sw3538, I2C_API_OWNER_SW3538);
    GX21M15_HandleInit(&s_gx21m15, I2C_API_OWNER_GX21M15U, GX21M15_I2C_ADDR_DEFAULT);
#if DPC_LOG_STARTUP_ENABLE
    printf("I2C1: PA13 SCL / PA14 SDA @ %lu Hz, EV/ER + TX/RX DMA IRQ; SW3538 + GX21M15 ready\r\n",
           (unsigned long)I2C_API_GetClockHz());
#endif

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
#if DPC_LOG_STARTUP_ENABLE
    printf("Soft-I2C: SW3526#1 PA4/PA3, SW3526#2 PA1/PA2; independent handles\r\n");
#endif

    /* Startup protocol defaults: enable every protocol family on all three
     * controllers so vendor OTP differences cannot leave functions disabled,
     * and put the protocol pages on the same all-enabled baseline.  The same
     * defaults are re-applied whenever a controller returns from offline
     * (app_proto_reinit_on_edge() in thread_power_mirror): a reset chip falls
     * back to its OTP state, so every reconnection gets one fresh push
     * through the debounced writer.  That thread also enables both SW3538
     * fast-charge paths on those edges (see s_sw3538_paths_pending). */
    PortPages_EnableAllProtocols();
    APP_PortProtocol_Touch(APP_PORT_PROTO_SW3538, (uint16_t)SW3538_PROTOCOL_ENABLE_ALL);
    APP_PortProtocol_Touch(APP_PORT_PROTO_SW3526_1, (uint16_t)SW3526_PROTOCOL_ENABLE_ALL);
    APP_PortProtocol_Touch(APP_PORT_PROTO_SW3526_2, (uint16_t)SW3526_PROTOCOL_ENABLE_ALL);

#if DPC_LOG_STARTUP_ENABLE
    printf("PD Sink: SPR <= %u mV; EPR nominal %u mV / %u mA; policy <= %u mA\r\n",
           (unsigned)PD_SPR_MAX_FIXED_MV,
           (unsigned)PD_EPR_NOMINAL_MV,
           (unsigned)PD_NOMINAL_REQUEST_MA,
           (unsigned)PD_POLICY_REQUEST_MAX_MA);
#endif

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

    if(app_soft_i2c_busy())
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
