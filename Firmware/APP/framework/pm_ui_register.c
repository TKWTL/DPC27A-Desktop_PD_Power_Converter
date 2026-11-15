/* Sleep menu of the power framework (see pm_ui_register.h).
 *
 * "Sleep now" is not an icon any more: the root menu answers BACK (the long
 * press of ENTER) with pm_api_force_sleep(), see core/ui.c's Process_UI_Run.
 *
 * The radio group uses the MiaoUI convention min == 1 (radio) + max == group
 * id; Switch_Widget() zeroes every entry of the group and sets the clicked
 * one before calling the shared callback, so the callback only has to find
 * the entry that is set.  Default = 1min, matching PM_DEFAULT_TIMEOUT_MS in
 * pm_api.c; the choice is RAM-only (same as both reference projects). */
#include "pm_ui_register.h"

#include "framework/pm_api.h"

#define PM_UI_DEFAULT_OPTION  3u   /* " 1min" */
#define PM_UI_OPTION_COUNT    9u

static const char *const s_opt_names[PM_UI_OPTION_COUNT] = {
    " No Auto Sleep", " 15s", " 30s", " 1min", " 2min",
    " 5min", " 10min", " 15min", " 30min"
};

/* 0 = No Auto Sleep, otherwise the idle timeout in seconds. */
static const int s_opt_seconds[PM_UI_OPTION_COUNT] = {
    0, 15, 30, 60, 120, 300, 600, 900, 1800
};

static ui_page_t s_sleep_page;
static ui_item_t s_sleep_menu_item;   /* "-Sleep" on the settings text page */
static ui_item_t s_sleep_back_item;   /* "[Home]" return to the settings page */
static ui_item_t s_opt_item[PM_UI_OPTION_COUNT];
static ui_data_t s_opt_data[PM_UI_OPTION_COUNT];
static ui_element_t s_opt_elem[PM_UI_OPTION_COUNT];
static uint8_t s_opt_state[PM_UI_OPTION_COUNT];
static ui_item_t s_power_block_item;
static uint8_t s_power_block_state;

static void pm_ui_option_clicked(ui_t *ui)
{
    uint8_t i;

    (void)ui;

    for (i = 0u; i < (uint8_t)PM_UI_OPTION_COUNT; i++)
    {
        if (s_opt_state[i] != 0u)
        {
            pm_api_set_sleep_timeout(s_opt_seconds[i]);
            return;
        }
    }
}

/* Settings page: sleep settings entry (placed right after the menu item) with
 * its own "[Sleep]" text page holding the timeout radio group. */
void PM_UI_AddSleepSettingsItems(ui_page_t *settings_page)
{
    uint8_t i;

    if (settings_page == 0) {
        return;
    }

    s_opt_state[PM_UI_DEFAULT_OPTION] = 1u;

    AddItem("-Sleep Options", UI_ITEM_PARENTS, 0, &s_sleep_menu_item,
            settings_page, &s_sleep_page, 0);
    AddPage("[Sleep]", &s_sleep_page, UI_PAGE_TEXT, settings_page);

    AddItem("[Home]", UI_ITEM_RETURN, 0, &s_sleep_back_item,
            &s_sleep_page, settings_page, 0);

    for (i = 0u; i < (uint8_t)PM_UI_OPTION_COUNT; i++)
    {
        s_opt_data[i].name = s_opt_names[i];
        s_opt_data[i].ptr = &s_opt_state[i];
        s_opt_data[i].function = pm_ui_option_clicked;
        s_opt_data[i].functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
        s_opt_data[i].dataType = UI_DATA_SWITCH;
        s_opt_data[i].actionType = UI_DATA_ACTION_RW;
        s_opt_data[i].min = 1;    /* min == 1 -> radio ... */
        s_opt_data[i].max = 1;    /* ... in group 1: only one entry set */
        s_opt_data[i].step = 0;
        s_opt_data[i].decimals = 0;
        s_opt_elem[i].data = &s_opt_data[i];
        Create_element(&s_opt_item[i], &s_opt_elem[i]);

        AddItem(s_opt_names[i], UI_ITEM_DATA, 0, &s_opt_item[i],
                &s_sleep_page, 0, 0);
    }
}

/* Independent switch, not part of the timeout radio group. */
static void pm_ui_power_block_clicked(ui_t *ui)
{
    (void)ui;
    pm_api_set_output_sleep_block_enabled(s_power_block_state);
}

void PM_UI_AddPowerSleepBlockItem(ui_page_t *parent_page)
{
    static ui_data_t data;
    static ui_element_t element;

    s_power_block_state = pm_api_get_output_sleep_block_enabled();
    data.name = " Keep On >5W";
    data.ptr = &s_power_block_state;
    data.function = pm_ui_power_block_clicked;
    data.functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
    data.dataType = UI_DATA_SWITCH;
    data.actionType = UI_DATA_ACTION_RW;
    element.data = &data;
    Create_element(&s_power_block_item, &element);
    AddItem(data.name, UI_ITEM_DATA, 0, &s_power_block_item, parent_page, 0, 0);
}
