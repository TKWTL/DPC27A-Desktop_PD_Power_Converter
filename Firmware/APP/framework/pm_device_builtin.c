/* Device hooks of the power framework for this board.
 *
 * UI_OFF is the only enabled level, so the display is the only consumer:
 *   prepare (RUN -> UI_OFF): blank the SH1107 immediately - the power-save
 *           command is queued and applied at the next safe point of the SPI
 *           page chain (SH1107_Display_RequestPowerSave), so calling it from
 *           the PM thread cannot race the display DMA;
 *   suspend (-> DEEPSLEEP): nothing extra for now;
 *   resume  (-> RUN): display on again and repaint the u8g2 buffer (the panel
 *           keeps its GDDRAM while off, and the extra flush also forces the
 *           dashboard page to redraw its content signature). */
#include "pm_device.h"

#include "display/dispDriver.h"

static void pm_oled_prepare(void *ctx)
{
    (void)ctx;
    Disp_SetPowerSave(1u);
}

static void pm_oled_suspend(void *ctx)
{
    (void)ctx;
    /* UI_OFF already blanked the panel; a future DEEPSLEEP port can add the
     * full display-sleep command here. */
}

static void pm_oled_resume(void *ctx)
{
    (void)ctx;
    Disp_SetPowerSave(0u);
    Disp_SendBuffer();
}

static const pm_device_node_t s_pm_oled = {
    "oled",
    pm_oled_prepare,
    pm_oled_suspend,
    pm_oled_resume,
    0,
    10,
    0,
    0
};

/* MCU owns the last-suspend / first-resume slot. Neither DeepSleep nor
 * Standby is implemented. USBPD and all coroutines run through UI_OFF. */
static const pm_device_node_t s_pm_mcu = {
    "ch32x035", 0, 0, 0, 0, 250, 0, 0
};

void pm_device_register_builtin(void)
{
    pm_device_register(&s_pm_oled);
    pm_device_register(&s_pm_mcu);
}
