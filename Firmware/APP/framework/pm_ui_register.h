#ifndef PM_UI_REGISTER_H
#define PM_UI_REGISTER_H

#include "core/ui.h"

/* MiaoUI registration of the power framework (ported from the NUEDC_2025B
 * pm_ui_register.c and adapted to this project's menu builder):
 *
 *   PM_UI_AddSleepSettingsItems()  settings page: "-Sleep" entry opening the
 *                                  "[Sleep]" page with the No Auto Sleep ...
 *                                  30min radio group (place it right after
 *                                  the menu item).
 *
 * The main-page "-Sleep" icon is gone: the root menu answers BACK (long press
 * of ENTER) with pm_api_force_sleep(), see core/ui.c Process_UI_Run(). */
void PM_UI_AddSleepSettingsItems(ui_page_t *settings_page);
void PM_UI_AddPowerSleepBlockItem(ui_page_t *parent_page);

#endif /* PM_UI_REGISTER_H */
