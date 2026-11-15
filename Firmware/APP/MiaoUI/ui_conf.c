/* Product menu built on the MiaoUI core from Firmware_0.zip.  Page functions
 * live in APP/Pages/ (see dashboard.c), so this file only owns the menu tree. */
#include "ui_conf.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "images/image.h"

#include "Pages/dashboard.h"
#include "Pages/service_pages.h"
#include "Pages/port_pages.h"
#include "Pages/game_dinosaur.h"
#include "framework/pm_ui_register.h"
#include "pd.h"
#include "app_tasks.h"
#include "vbus_sense.h"
#include "time_api.h"

static ui_page_t s_main_page;
static ui_page_t s_statistics_page;
static ui_page_t s_settings_icons_page;
static ui_page_t s_settings_page;

static ui_item_t s_dashboard_item;
static ui_item_t s_statistics_item;
static ui_item_t s_statistics_back_item;
enum { STAT_PDO, STAT_REQ, STAT_CUR, STAT_VBUS, STAT_GX, STAT_NTC, STAT_COUNT };
static ui_item_t s_stats_items[STAT_COUNT];
static ui_item_t s_settings_item;
static ui_item_t s_settings_icons_back_item;
static ui_item_t s_settings_detail_item;
static ui_item_t s_settings_back_item;
static ui_item_t s_about_item;
static ui_item_t s_burnin_item;
static ui_item_t s_dino_item;

/* Numeric data mirrors. No per-row display strings and no UI bus access. */
static int s_stats_value[STAT_COUNT];
static uint8_t s_stats_has[STAT_COUNT];
static ui_data_t s_stats_data[STAT_COUNT];
static ui_element_t s_stats_element[STAT_COUNT];
static uint32_t s_statistics_poll_ms;
static uint8_t s_statistics_valid;

void Statistics_Poll(void)
{
    uint32_t now = TIME_Millis();
    int32_t milli_c;
    int16_t ntc_x10 = 0;
    uint8_t ready;
    const GX21M15_Handle *gx = APP_GetGX21M15();

    if(s_statistics_valid && (uint32_t)(now - s_statistics_poll_ms) < 200u)
        return;

    s_statistics_valid = 1u;
    s_statistics_poll_ms = now;
    milli_c = GX21M15_ReadTemperatureMilliC(gx);
    s_stats_has[STAT_GX] = GX21M15_IsOnline(gx);
    s_stats_value[STAT_GX] = (int)((milli_c >= 0 ? milli_c + 50 : milli_c - 50) / 100);
    s_stats_has[STAT_NTC] = SW3538_ReadNTCTemperatureX10(APP_GetSW3538(), &ntc_x10);
    s_stats_value[STAT_NTC] = ntc_x10;
    s_stats_has[STAT_VBUS] = 1u;
    s_stats_value[STAT_VBUS] = (int)(VBUS_Sense_ReadMillivolts() / 10u);

    ready = (uint8_t)(PD_IsConnected() && PD_IsPowerReady());
    s_stats_has[STAT_PDO] = ready;
    s_stats_has[STAT_REQ] = ready;
    s_stats_has[STAT_CUR] = ready;
    if(!PD_IsConnected())
        s_stats_items[STAT_PDO].itemName = " PDO (DC)";
    else if(!ready)
        s_stats_items[STAT_PDO].itemName = " PDO (NEG)";
    else
    {
        s_stats_items[STAT_PDO].itemName = PD_IsEPRContractActive() ? " PDO (EPR)" :
                                  PD_IsPPSContractActive() ? " PDO (PPS)" : " PDO (SPR)";
        s_stats_value[STAT_PDO] = PD_GetRequestedPDOIndex();
        s_stats_value[STAT_REQ] = (int)(PD_GetContractVoltageMv() / 10u);
        s_stats_value[STAT_CUR] = (int)(PD_GetContractCurrentMa() / 10u);
    }
}

static void statistics_add_rows(void)
{
    static const char *const names[STAT_COUNT] = {
        " PDO (DC)", " Req (V)", " Cur (A)", " VBUS (V)", " GX21M15 (C)", " SW3538 (C)"
    };
    uint8_t n;
    for(n = 0u; n < STAT_COUNT; ++n)
    {
        ui_data_t *data = &s_stats_data[n];
        data->ptr = &s_stats_value[n];
        data->valid = &s_stats_has[n];
        data->actionType = UI_DATA_ACTION_RO;
        data->dataType = (n == STAT_PDO) ? UI_DATA_INT : UI_DATA_FIXED;
        data->decimals = (n == STAT_PDO) ? 0u : (n >= STAT_GX ? 1u : 2u);
        s_stats_element[n].data = data;
        AddItem(names[n], UI_ITEM_DATA, 0, &s_stats_items[n], &s_statistics_page, 0, 0);
        Create_element(&s_stats_items[n], &s_stats_element[n]);
    }
    Statistics_Poll();
}

void Create_Parameter(ui_t *ui)
{
    Create_Disp_Parameters(ui);
}

void Create_Text(ui_t *ui)
{
    static ui_text_t about_text;
    static ui_element_t about_element;

    (void)ui;

    /* About page body.  Text_Widget() renders 20 chars per line (UI_HOR_RES /
     * UI_FONT_WIDTH - 1) with a 12 px line step, so keep every line <= 20
     * characters and the whole block within ~6 visible lines.  A full line is
     * 20 x 6 px + 4 px inset = 124 px, inside the 128 px panel width. */
    about_text.ptr = "  Desktop PD Power\nSource: https://github.com/TKWTL/Desktop_PD_Power_Converter\n MiaoUI u8g2 coroOS\nMIT BSD-2 BSD-3 AGPL";
    about_text.font = UI_FONT;
    about_text.fontHight = UI_FONT_HIGHT;
    about_text.fontWidth = UI_FONT_WIDTH;
    about_element.text = &about_text;
    Create_element(&s_about_item, &about_element);
}

void Create_MenuTree(ui_t *ui)
{
    (void)ui;

    AddPage("[Menu]", &s_main_page, UI_PAGE_ICON, 0);
    AddItem("-Dashboard", UI_ITEM_WORD, img_dashboard,
            &s_dashboard_item, &s_main_page, 0, Dashboard_Page);

    /* Settings icon submenu: Return/Home, TypeA+C, TypeC1, TypeC2, Settings.
     * Both Settings tiers share one constant img_configuration bitmap. */
    AddItem("-Settings", UI_ITEM_PARENTS, img_configuration,
            &s_settings_item, &s_main_page, &s_settings_icons_page, 0);
        AddPage("[Settings]", &s_settings_icons_page, UI_PAGE_ICON, &s_main_page);
        AddItem("[Menu]", UI_ITEM_RETURN, img_home,
                &s_settings_icons_back_item, &s_settings_icons_page, &s_main_page, 0);
        Add_Port_Items(&s_settings_icons_page);
        AddItem("-Settings", UI_ITEM_PARENTS, img_configuration,
                &s_settings_detail_item, &s_settings_icons_page, &s_settings_page, 0);
            AddPage("[Settings]", &s_settings_page, UI_PAGE_TEXT, &s_settings_icons_page);
            AddItem("[Return]", UI_ITEM_RETURN, 0,
                    &s_settings_back_item, &s_settings_page, &s_settings_icons_page, 0);
            PM_UI_AddSleepSettingsItems(&s_settings_page);
            Add_Disp_Items(&s_settings_page);
            Add_Service_Items(&s_settings_page);

    /* Root Statistics follows Settings; PD, VBUS and temperatures are read-only rows. */
    AddItem("-Statistics", UI_ITEM_PARENTS, img_statistics,
            &s_statistics_item, &s_main_page, &s_statistics_page, 0);
        AddPage("[Statistics]", &s_statistics_page, UI_PAGE_TEXT, &s_main_page);
        AddItem("[Menu]", UI_ITEM_RETURN, 0, &s_statistics_back_item,
                &s_statistics_page, &s_main_page, 0);
        statistics_add_rows();

    AddItem("-Burn-in Test", UI_ITEM_WORD, img_burn_in,
            &s_burnin_item, &s_main_page, 0, Burnin_Page);

    AddItem("-Dino Game", UI_ITEM_WORD, img_dino,
            &s_dino_item, &s_main_page, 0, Game_DinoSaur);

    AddItem("-About", UI_ITEM_WORD, img_user_account,
            &s_about_item, &s_main_page, 0, 0);
}

void MiaoUi_Setup(ui_t *ui)
{
    Create_UI(ui, &s_dashboard_item);

    /* Boot straight into the dashboard function page instead of painting the
     * icon menu first: ui_loop() only draws the menu from its UI_PAGE_INIT
     * branch, so starting in the item-running state keeps the menu as the page
     * the dashboard returns to. */
    ui->menuState = UI_ITEM_RUNING;
}
