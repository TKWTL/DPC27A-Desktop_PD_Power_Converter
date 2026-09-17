/* Per-port fast-charge protocol pages.
 *
 * Three TEXT pages reachable straight from the icon menu:
 *   [TypeA+C]   SW3538 (the Type-A and Type-C receptacles, PT1/PT2)
 *   [TypeC1]    SW3526 #1
 *   [TypeC2]    SW3526 #2
 *
 * Every page is a list of plain multi-select switches, one per protocol
 * family the chip exposes; " PD" leads the list, right after the [Menu] back
 * item.  Flipping a switch recomputes the complete family mask and hands it
 * to APP_PortProtocol_Touch(); app_tasks.c debounces the
 * request and performs the write from thread_power_mirror, so the UI never
 * touches the buses directly and each I2C handle keeps a single owner thread.
 *
 * RAM-only, like the other MiaoUI settings: a power cycle resets both the UI
 * state and the chips back to their power-up defaults.  The firmware
 * re-establishes the intended baseline at every boot and again whenever a
 * controller returns from offline: app_tasks.c pushes an "every family
 * enabled" mask to all three controllers (and enables both SW3538
 * fast-charge paths), so vendor OTP differences cannot leave functions
 * disabled; these switches only track the user's deviations from that
 * all-enabled baseline, and a controller reconnection resets both the chips
 * and the switch states back to that baseline.
 */
#include "port_pages.h"

#include "app_tasks.h"
#include "core/ui.h"
#include "images/image.h"

/* ------------------------------------------------------------------ */
/* Protocol tables                                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
    const char *name;   /* menu text, leading space like the other items */
    uint16_t    bit;    /* family bit inside the chip protocol mask */
} port_proto_entry_t;

/* SW3538 exposes eleven families.  PD leads the list (the first row below the
 * page's [Menu] back item); RG108_3 v1.2 has no documented VOOC/SuperVOOC
 * enable bit, so it is deliberately not listed. */
static const port_proto_entry_t s_protos_3538[] =
{
    { " PD",    SW3538_PROTOCOL_ENABLE_PD },
    { " QC2.0", SW3538_PROTOCOL_ENABLE_QC2 },
    { " QC3.0", SW3538_PROTOCOL_ENABLE_QC3 },
    { " QC3+",  SW3538_PROTOCOL_ENABLE_QC3P },
    { " QC4+",  SW3538_PROTOCOL_ENABLE_QC4P },
    { " FCP",   SW3538_PROTOCOL_ENABLE_FCP },
    { " SCP",   SW3538_PROTOCOL_ENABLE_SCP },
    { " PE",    SW3538_PROTOCOL_ENABLE_PE },
    { " SFCP",  SW3538_PROTOCOL_ENABLE_SFCP },
    { " AFC",   SW3538_PROTOCOL_ENABLE_AFC },
    { " TFCP",  SW3538_PROTOCOL_ENABLE_TFCP },
};
#define PORT_PROTO_3538_COUNT ((uint8_t)(sizeof(s_protos_3538) / sizeof(s_protos_3538[0])))

/* SW3526 exposes eight families in the menu (QC3+/QC4+/TFCP are not present
 * in RG012_1 v1.0; VOOC is supported by the chip but deliberately not
 * offered in the UI).  PD leads the list right after the [Menu] back item,
 * as on the SW3538 page. */
static const port_proto_entry_t s_protos_3526[] =
{
    { " PD",    SW3526_PROTOCOL_ENABLE_PD },
    { " QC2.0", SW3526_PROTOCOL_ENABLE_QC2 },
    { " QC3.0", SW3526_PROTOCOL_ENABLE_QC3 },
    { " FCP",   SW3526_PROTOCOL_ENABLE_FCP },
    { " SCP",   SW3526_PROTOCOL_ENABLE_SCP },
    { " PE",    SW3526_PROTOCOL_ENABLE_PE },
    { " SFCP",  SW3526_PROTOCOL_ENABLE_SFCP },
    { " AFC",   SW3526_PROTOCOL_ENABLE_AFC },
};
#define PORT_PROTO_3526_COUNT ((uint8_t)(sizeof(s_protos_3526) / sizeof(s_protos_3526[0])))

/* ------------------------------------------------------------------ */
/* Page state                                                         */
/* ------------------------------------------------------------------ */

static ui_page_t s_page_3538;
static ui_page_t s_page_3526[2];

static ui_item_t s_enter_3538;
static ui_item_t s_enter_3526[2];
static ui_item_t s_back_3538;
static ui_item_t s_back_3526[2];

static uint8_t s_state_3538[PORT_PROTO_3538_COUNT];
static ui_data_t s_data_3538[PORT_PROTO_3538_COUNT];
static ui_element_t s_elem_3538[PORT_PROTO_3538_COUNT];
static ui_item_t s_item_3538[PORT_PROTO_3538_COUNT];

static uint8_t s_state_3526[2][PORT_PROTO_3526_COUNT];
static ui_data_t s_data_3526[2][PORT_PROTO_3526_COUNT];
static ui_element_t s_elem_3526[2][PORT_PROTO_3526_COUNT];
static ui_item_t s_item_3526[2][PORT_PROTO_3526_COUNT];

/* ------------------------------------------------------------------ */
/* Switch callbacks                                                   */
/* ------------------------------------------------------------------ */

static uint16_t proto_build_mask(const port_proto_entry_t *table,
                                 uint8_t count,
                                 const uint8_t *state)
{
    uint16_t mask = 0u;
    uint8_t k;

    for(k = 0u; k < count; ++k)
    {
        if(state[k] != 0u)
            mask |= table[k].bit;
    }
    return mask;
}

/* Switch_Widget() flips the state byte first and calls the item callback
 * afterwards, so the state array is already current when these run.  The UI
 * only forwards the request; the actual I2C write happens later in
 * thread_power_mirror (see APP_PortProtocol_Touch). */
static void port_proto_3538_changed(ui_t *ui)
{
    (void)ui;
    APP_PortProtocol_Touch(APP_PORT_PROTO_SW3538,
                           proto_build_mask(s_protos_3538,
                                            PORT_PROTO_3538_COUNT,
                                            s_state_3538));
}

/* TypeC1 and TypeC2 share this callback; the owning page tells them apart. */
static void port_proto_3526_changed(ui_t *ui)
{
    uint8_t idx = (ui->nowItem->page.location == &s_page_3526[1]) ? 1u : 0u;

    APP_PortProtocol_Touch((idx == 0u) ? APP_PORT_PROTO_SW3526_1
                                       : APP_PORT_PROTO_SW3526_2,
                           proto_build_mask(s_protos_3526,
                                            PORT_PROTO_3526_COUNT,
                                            s_state_3526[idx]));
}

/* Re-enable every switch on all three pages.  app_tasks.c pairs this with the
 * "every family enabled" masks pushed to the controllers - at boot and again
 * whenever a controller reconnects - so page state and chip state always
 * share the same baseline. */
void PortPages_EnableAllProtocols(void)
{
    uint8_t k;
    uint8_t idx;

    for(k = 0u; k < PORT_PROTO_3538_COUNT; ++k)
        s_state_3538[k] = 1u;

    for(idx = 0u; idx < 2u; ++idx)
    {
        for(k = 0u; k < PORT_PROTO_3526_COUNT; ++k)
            s_state_3526[idx][k] = 1u;
    }
}

/* ------------------------------------------------------------------ */
/* Menu registration                                                  */
/* ------------------------------------------------------------------ */

void Add_Port_Items(ui_page_t *parent_page)
{
    uint8_t k;
    uint8_t idx;

    /* --- SW3538: TypeA+C -------------------------------------------- */
    AddItem("-TypeA+C", UI_ITEM_PARENTS, img_type_ac, &s_enter_3538,
            parent_page, &s_page_3538, 0);
    AddPage("[TypeA+C]", &s_page_3538, UI_PAGE_TEXT, parent_page);
    AddItem("[Menu]", UI_ITEM_RETURN, 0, &s_back_3538,
            &s_page_3538, parent_page, 0);

    for(k = 0u; k < PORT_PROTO_3538_COUNT; ++k)
    {
        s_state_3538[k] = 1u;      /* default: every family enabled */
        s_data_3538[k].name = s_protos_3538[k].name;
        s_data_3538[k].ptr = &s_state_3538[k];
        s_data_3538[k].function = port_proto_3538_changed;
        s_data_3538[k].functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
        s_data_3538[k].dataType = UI_DATA_SWITCH;
        s_data_3538[k].actionType = UI_DATA_ACTION_RW;
        s_data_3538[k].min = 0;    /* 0/0: plain switch, not a radio group */
        s_data_3538[k].max = 0;
        s_data_3538[k].step = 0;
        s_data_3538[k].decimals = 0;
        s_elem_3538[k].data = &s_data_3538[k];
        Create_element(&s_item_3538[k], &s_elem_3538[k]);
        AddItem(s_protos_3538[k].name, UI_ITEM_DATA, 0, &s_item_3538[k],
                &s_page_3538, 0, 0);
    }

    /* --- SW3526 #1 / #2: TypeC1 / TypeC2 ---------------------------- */
    for(idx = 0u; idx < 2u; ++idx)
    {
        AddItem((idx == 0u) ? "-TypeC1" : "-TypeC2", UI_ITEM_PARENTS,
                (idx == 0u) ? img_type_c1 : img_type_c2,
                &s_enter_3526[idx], parent_page, &s_page_3526[idx], 0);
        AddPage((idx == 0u) ? "[TypeC1]" : "[TypeC2]", &s_page_3526[idx],
                UI_PAGE_TEXT, parent_page);
        AddItem("[Menu]", UI_ITEM_RETURN, 0, &s_back_3526[idx],
                &s_page_3526[idx], parent_page, 0);

        for(k = 0u; k < PORT_PROTO_3526_COUNT; ++k)
        {
            s_state_3526[idx][k] = 1u;
            s_data_3526[idx][k].name = s_protos_3526[k].name;
            s_data_3526[idx][k].ptr = &s_state_3526[idx][k];
            s_data_3526[idx][k].function = port_proto_3526_changed;
            s_data_3526[idx][k].functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
            s_data_3526[idx][k].dataType = UI_DATA_SWITCH;
            s_data_3526[idx][k].actionType = UI_DATA_ACTION_RW;
            s_data_3526[idx][k].min = 0;
            s_data_3526[idx][k].max = 0;
            s_data_3526[idx][k].step = 0;
            s_data_3526[idx][k].decimals = 0;
            s_elem_3526[idx][k].data = &s_data_3526[idx][k];
            Create_element(&s_item_3526[idx][k], &s_elem_3526[idx][k]);
            AddItem(s_protos_3526[k].name, UI_ITEM_DATA, 0,
                    &s_item_3526[idx][k], &s_page_3526[idx], 0, 0);
        }
    }
}
