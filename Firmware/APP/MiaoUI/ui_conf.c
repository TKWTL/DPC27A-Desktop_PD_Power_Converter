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

static ui_page_t s_main_page;
static ui_page_t s_settings_page;

static ui_item_t s_dashboard_item;
static ui_item_t s_settings_item;
static ui_item_t s_settings_back_item;
static ui_item_t s_about_item;
static ui_item_t s_burnin_item;
static ui_item_t s_dino_item;

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

    /* Per-port fast-charge protocol pages: three more icon-page entries that
     * open [TypeA+C], [TypeC1] and [TypeC2]. */
    Add_Port_Items(&s_main_page);

    AddItem("-Settings", UI_ITEM_PARENTS, img_configuration,
            &s_settings_item, &s_main_page, &s_settings_page, 0);
        AddPage("[Settings]", &s_settings_page, UI_PAGE_TEXT, &s_main_page);
        AddItem("[Menu]", UI_ITEM_RETURN, 0,
                &s_settings_back_item, &s_settings_page, &s_main_page, 0);
        PM_UI_AddSleepSettingsItems(&s_settings_page);
        Add_Disp_Items(&s_settings_page);
        Add_Service_Items(&s_settings_page);

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
