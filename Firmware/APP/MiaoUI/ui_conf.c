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
#include "vbus_sense.h"
#include "time_api.h"
#include <stdio.h>

static ui_page_t s_main_page;
static ui_page_t s_statistics_page;
static ui_page_t s_settings_icons_page;
static ui_page_t s_settings_page;

static ui_item_t s_dashboard_item;
static ui_item_t s_statistics_item;
static ui_item_t s_statistics_back_item;
static ui_item_t s_stats_pdo_item, s_stats_req_item, s_stats_current_item, s_stats_vbus_item;
static ui_item_t s_settings_item;
static ui_item_t s_settings_icons_back_item;
static ui_item_t s_settings_detail_item;
static ui_item_t s_settings_back_item;
static ui_item_t s_about_item;
static ui_item_t s_burnin_item;
static ui_item_t s_dino_item;

/* IH01A-style persistent, allocation-free read-only rows. Neither the
 * statistics page nor its periodic refresh performs any I2C transaction. */
static char s_stats_pdo[20] = " PDO: --";
static char s_stats_req[20] = " Req: --";
static char s_stats_current[20] = " Cur: --";
static char s_stats_vbus[20] = " VBUS: --";
static uint32_t s_statistics_poll_ms;
static uint8_t s_statistics_valid;

void Statistics_Poll(void)
{
    uint32_t now = TIME_Millis();
    uint32_t mv;
    uint32_t ma;

    if(s_statistics_valid && (uint32_t)(now - s_statistics_poll_ms) < 200u)
        return;

    s_statistics_valid = 1u;
    s_statistics_poll_ms = now;
    mv = VBUS_Sense_ReadMillivolts();
    (void)snprintf(s_stats_vbus, sizeof(s_stats_vbus),
                   " VBUS %lu.%02luV", (unsigned long)(mv / 1000u),
                   (unsigned long)((mv % 1000u) / 10u));

    /* Selection happens before PS_RDY; do not report an in-flight Request
     * as an active PD contract. Detached inputs still show the ADC VBUS. */
    if(!PD_IsConnected())
    {
        (void)snprintf(s_stats_pdo, sizeof(s_stats_pdo), " PDO: DC/--");
        (void)snprintf(s_stats_req, sizeof(s_stats_req), " Req: --");
        (void)snprintf(s_stats_current, sizeof(s_stats_current), " Cur: --");
    }
    else if(!PD_IsPowerReady())
    {
        (void)snprintf(s_stats_pdo, sizeof(s_stats_pdo), " PDO: NEG");
        (void)snprintf(s_stats_req, sizeof(s_stats_req), " Req: --");
        (void)snprintf(s_stats_current, sizeof(s_stats_current), " Cur: --");
    }
    else
    {
        mv = PD_GetContractVoltageMv();
        ma = PD_GetContractCurrentMa();
        (void)snprintf(s_stats_pdo, sizeof(s_stats_pdo), " PDO#%u %s",
                       (unsigned)PD_GetRequestedPDOIndex(),
                       PD_IsEPRContractActive() ? "EPR" :
                       (PD_IsPPSContractActive() ? "PPS" : "SPR"));
        (void)snprintf(s_stats_req, sizeof(s_stats_req),
                       " Req %lu.%02luV", (unsigned long)(mv / 1000u),
                       (unsigned long)((mv % 1000u) / 10u));
        (void)snprintf(s_stats_current, sizeof(s_stats_current),
                       " Cur %lu.%02luA", (unsigned long)(ma / 1000u),
                       (unsigned long)((ma % 1000u) / 10u));
    }
}

/* Tapping a read-only row returns to Statistics instead of opening Text_Widget. */
static void statistics_readonly(ui_t *ui)
{
    if(ui != 0 && ui->action == UI_ACTION_NONE)
        ui->action = UI_ACTION_BACK;
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

    /* Root Statistics follows Settings; PD and VBUS are read-only rows. */
    AddItem("-Statistics", UI_ITEM_PARENTS, img_statistics,
            &s_statistics_item, &s_main_page, &s_statistics_page, 0);
        AddPage("[Statistics]", &s_statistics_page, UI_PAGE_TEXT, &s_main_page);
        AddItem("[Menu]", UI_ITEM_RETURN, 0, &s_statistics_back_item,
                &s_statistics_page, &s_main_page, 0);
        Statistics_Poll();
        AddItem(s_stats_pdo, UI_ITEM_WORD, 0, &s_stats_pdo_item,
                &s_statistics_page, 0, statistics_readonly);
        AddItem(s_stats_req, UI_ITEM_WORD, 0, &s_stats_req_item,
                &s_statistics_page, 0, statistics_readonly);
        AddItem(s_stats_current, UI_ITEM_WORD, 0, &s_stats_current_item,
                &s_statistics_page, 0, statistics_readonly);
        AddItem(s_stats_vbus, UI_ITEM_WORD, 0, &s_stats_vbus_item,
                &s_statistics_page, 0, statistics_readonly);

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
