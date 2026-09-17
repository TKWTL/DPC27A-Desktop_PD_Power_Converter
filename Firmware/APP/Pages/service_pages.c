/* Service items/pages for the Desktop PD Power Converter.
 *
 * The menu tree (APP/MiaoUI/ui_conf.c) registers them directly; there is no
 * "Tools" submenu:
 *   Fan +10C        switch item: shifts the fan thresholds from 40/37 C to
 *                   50/47 C through FAN_Control_SetTemperatureDelay();
 *   Reset Now       Board_SoftReset() - plain PFIC software reset;
 *   Reboot to ISP   Board_RebootToISP() - factory bootloader on next reset;
 *   Burn-in Test    full white screen after a short explanation; any key
 *                   during the white phase leaves.
 *
 * Input map is the product's two-key adapter (K1 = DOWN, K2 = ENTER).  Page
 * functions are pass-based: ui_loop() calls them every ~8 ms, actions arrive
 * in ui->action, and the page either swallows an action (ui->action =
 * UI_ACTION_NONE) or leaves it set so ui.c returns to the menu - the same
 * convention Dashboard_Page() uses.
 */
#include "service_pages.h"

#include "board.h"
#include "debug.h"
#include "Services/fan_control.h"
#include "core/ui.h"
#include "display/dispDriver.h"

/* Intro dwell before the white screen; the UI task ticks every 8 ms (the 300
 * ticks below are therefore about 2.4 s). */
#define BURNIN_INTRO_TICKS   300u

#define BURNIN_LINE_X        4u
#define BURNIN_LINE1_Y       12u
#define BURNIN_LINE_STEP     13u

static ui_item_t s_soft_reset_item;
static ui_item_t s_isp_reboot_item;
static ui_item_t s_fan_delay_item;
static uint8_t   s_fan_delay_state;   /* 0 = 40/37 C, 1 = +10 C (50/47 C) */

/* ------------------------------------------------------------------ */
/* Menu registration                                                  */
/* ------------------------------------------------------------------ */

/* Fan trigger delay.  The menu item is a plain switch, so the stored flag is
 * 0/1; the fan curve itself works in degrees and shifts *both* thresholds
 * (FAN_Control_SetTemperatureDelay: 40/37 C -> 50/47 C).  RAM-only like every
 * other setting; thread_fan picks the change up on its next 500 ms poll. */
static void fan_delay_clicked(ui_t *ui)
{
    (void)ui;
    FAN_Control_SetTemperatureDelay((s_fan_delay_state != 0u) ? 10u : 0u);
}

void Add_Service_Items(ui_page_t *parent_page)
{
    static ui_data_t fan_delay_data;
    static ui_element_t fan_delay_element;

    /* Switch item: K2 toggles it in place and the value column shows a
     * checkbox, exactly like "Background"/"Screen Flip" on the display page.
     * Registered first so it sits directly above the reset entries. */
    s_fan_delay_state = (FAN_Control_GetTemperatureDelay() != 0u) ? 1u : 0u;
    fan_delay_data.name = "Fan +10C";
    fan_delay_data.ptr = &s_fan_delay_state;
    fan_delay_data.function = fan_delay_clicked;
    fan_delay_data.functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
    fan_delay_data.dataType = UI_DATA_SWITCH;
    fan_delay_data.actionType = UI_DATA_ACTION_RW;
    fan_delay_data.min = 0;      /* 0/0: plain switch, not a radio group */
    fan_delay_data.max = 0;
    fan_delay_data.step = 0;
    fan_delay_data.decimals = 0;
    fan_delay_element.data = &fan_delay_data;
    Create_element(&s_fan_delay_item, &fan_delay_element);

    AddItem(" Fan +10'C", UI_ITEM_DATA, 0, &s_fan_delay_item,
            parent_page, 0, 0);
    AddItem(" Reset Now", UI_ITEM_WORD, 0, &s_soft_reset_item,
            parent_page, 0, SoftReset_Action);
    AddItem(" Reboot to ISP", UI_ITEM_WORD, 0, &s_isp_reboot_item,
            parent_page, 0, IspReboot_Action);
}

/* ------------------------------------------------------------------ */
/* Reset actions                                                      */
/* ------------------------------------------------------------------ */

void SoftReset_Action(ui_t *ui)
{
    (void)ui;

    printf("[SYS] software reset requested\r\n");
    Delay_Ms(3);              /* let the async UART shift the line out */
    Board_SoftReset();        /* never returns */
}

void IspReboot_Action(ui_t *ui)
{
    (void)ui;

    printf("[SYS] reboot to ISP requested (BOOT_MODE)\r\n");
    Delay_Ms(3);
    Board_RebootToISP();      /* never returns */
}

/* ------------------------------------------------------------------ */
/* Burn-in test                                                       */
/* ------------------------------------------------------------------ */

static void burnin_paint_intro(const ui_t *ui)
{
    uint8_t bg = ui->bgColor;
    uint8_t fg = (uint8_t)(bg ? 0u : 1u);

    Disp_SetDrawColor(&bg);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetDrawColor(&fg);
    Disp_DrawStr(BURNIN_LINE_X, BURNIN_LINE1_Y, "Burn-in Test");
    Disp_DrawStr(BURNIN_LINE_X, BURNIN_LINE1_Y + BURNIN_LINE_STEP, "Checks the OLED for");
    Disp_DrawStr(BURNIN_LINE_X, BURNIN_LINE1_Y + 2u * BURNIN_LINE_STEP, "decay after many");
    Disp_DrawStr(BURNIN_LINE_X, BURNIN_LINE1_Y + 3u * BURNIN_LINE_STEP, "hours of light-up.");
    Disp_DrawStr(BURNIN_LINE_X, BURNIN_LINE1_Y + 4u * BURNIN_LINE_STEP, "White screen - press");
    Disp_DrawStr(BURNIN_LINE_X, BURNIN_LINE1_Y + 5u * BURNIN_LINE_STEP, "any key to leave.");
    Disp_SendBuffer();
}

static void burnin_paint_white(void)
{
    uint8_t color = 1u;       /* full white, independent of the menu theme */

    Disp_SetDrawColor(&color);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SendBuffer();
}

void Burnin_Page(ui_t *ui)
{
    static uint8_t  s_phase;   /* 0 = explanation, 1 = white screen */
    static uint16_t s_ticks;

    switch(s_phase)
    {
        case 0u:
            if(ui->action != UI_ACTION_NONE)
            {
                /* A key during the explanation skips straight to the white
                 * screen (same behaviour as the Firmware_0 reference); the
                 * action is consumed so the page stays open. */
                ui->action = UI_ACTION_NONE;
                burnin_paint_white();
                s_phase = 1u;
                break;
            }

            if(s_ticks == 0u)
                burnin_paint_intro(ui);

            s_ticks++;
            if(s_ticks >= BURNIN_INTRO_TICKS)
            {
                burnin_paint_white();
                s_phase = 1u;
            }
            break;

        case 1u:
            if(ui->action != UI_ACTION_NONE)
            {
                /* Any key ends the white screen.  Re-arm the state before
                 * returning; ui.c switches back to the menu because the
                 * action is left set (same exit convention as the dashboard). */
                s_phase = 0u;
                s_ticks = 0u;
            }
            break;

        default:
            s_phase = 0u;
            s_ticks = 0u;
            break;
    }
}
