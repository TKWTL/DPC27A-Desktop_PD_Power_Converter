/* Dashboard page for the Desktop PD Power Converter.
 *
 * This is the function page the UI boots into.  It is a pure consumer of the
 * device mirrors (333 ms power telemetry, 500 ms temperature): the UI never
 * touches I2C itself, so the page only formats cached telemetry.
 *
 * Layout (128x80 panel, 6x12 font, 21 characters per line):
 *   row 1              board input state: temperature ("25.3C") / input voltage
 *                      ("20V") / total power limit ("270Wmax"; the number is
 *                      right-aligned in three digits so its "W" sits where the
 *                      old "140Wmax" had it; DOWN cycles it in the DC path) /
 *                      PD state ("UVP" below 8 V, right-aligned)
 *   rows 2..6          three columns: SW3538 "TypeA+C", SW3526 #1 "TypeC1",
 *                      SW3526 #2 "TypeC2"; from top to bottom:
 *                      port name / output voltage / current / power / protocol
 *                      ("NC" = no sink attached, "5V" = sink without a fast
 *                      charge; protocol text is centered in its column, the
 *                      SW3538 one shifted 3 px right, and row 1 keeps
 *                      the PD state right-aligned to the panel edge)
 *
 *   row baseline      y = 12 + 13 * (row - 1)   (1 px top margin, 1 px gap)
 *   column right edge 43 px (7 chars) / 85 px (6 chars) / 127 px (6 chars)
 * Values are right aligned, so a wider number eats into the one character gap
 * instead of leaving its column.
 */
#include "dashboard.h"

#include "app_tasks.h"
#include "display/dispDriver.h"
#include "pd.h"
#include "Services/power_limit.h"
#include "vbus_sense.h"

#include <stdio.h>
#include <string.h>

#define DASH_ROW1_Y        12u
#define DASH_ROW_STEP      13u
#define DASH_X_MARGIN      1u
#define DASH_COL1_RIGHT    43u
#define DASH_COL2_RIGHT    85u
#define DASH_COL3_RIGHT    127u
#define DASH_COL2_X        (DASH_X_MARGIN + 8u * UI_FONT_WIDTH)
#define DASH_COL3_X        (DASH_X_MARGIN + 15u * UI_FONT_WIDTH)

#define DASH_NAME_SW3538    "TypeA+C"
#define DASH_NAME_SW3526_1  "TypeC1"
#define DASH_NAME_SW3526_2  "TypeC2"

#define DASH_OFFLINE_TEXT   "---"
#define DASH_PORT_NC_TEXT   "NC"   /* chip online, no sink attached */
#define DASH_PORT_5V_TEXT   "5V"   /* sink attached, no fast-charge protocol */
#define DASH_TEXT_LEN       24u
#define DASH_VALUE_LEN      12u
#define DASH_SIGNATURE_LEN  160u
#define DASH_COLUMN_COUNT   3u

typedef struct
{
    const char *name;
    char volt[DASH_VALUE_LEN];
    char amp[DASH_VALUE_LEN];
    char watt[DASH_VALUE_LEN];
    const char *protocol;
} dash_column_t;

static void dash_draw_right(uint16_t right_x, uint16_t y, const char *text)
{
    int16_t x = (int16_t)right_x - (int16_t)(strlen(text) * UI_FONT_WIDTH);

    if(x < 0)
        x = 0;

    Disp_DrawStr((uint16_t)x, y, text);
}

/* The protocol line is centered between the port name (left-aligned) and the
 * right-aligned value rows; dx is a pixel nudge (the SW3538 status reads 3 px
 * right of centre). */
static void dash_draw_center(uint16_t left_x, uint16_t right_x, uint16_t y,
                             const char *text, int16_t dx)
{
    int16_t width = (int16_t)(right_x - left_x + 1u);
    int16_t text_w = (int16_t)(strlen(text) * UI_FONT_WIDTH);
    int16_t x = (int16_t)left_x + (width - text_w) / 2 + dx;

    if(x < 0)
        x = 0;

    Disp_DrawStr((uint16_t)x, y, text);
}

static void dash_draw_column(const dash_column_t *col, uint16_t name_x,
                             uint16_t right_x, int16_t protocol_dx)
{
    uint16_t y = (uint16_t)(DASH_ROW1_Y + DASH_ROW_STEP);   /* row 2: port name */

    Disp_DrawStr(name_x, y, col->name);
    dash_draw_right(right_x, (uint16_t)(y + DASH_ROW_STEP), col->volt);
    dash_draw_right(right_x, (uint16_t)(y + 2u * DASH_ROW_STEP), col->amp);
    dash_draw_right(right_x, (uint16_t)(y + 3u * DASH_ROW_STEP), col->watt);
    dash_draw_center(name_x, right_x, (uint16_t)(y + 4u * DASH_ROW_STEP),
                     col->protocol, protocol_dx);
}

/* Value formatting.  Everything is integer/fixed-point on this MCU: the
 * firmware provides its own integer-only printf (Project/Debug/debug.c), so
 * the image no longer carries soft-float printf support and "%f" must not be
 * used.  Values are printed as "whole.fraction" with the same number of
 * decimals as before (the fraction is truncated, not rounded). */
static void dash_fmt_volt(char *buf, uint16_t mv)
{
    snprintf(buf, DASH_VALUE_LEN, "%u.%02uV",
             (unsigned)(mv / 1000u), (unsigned)((mv % 1000u) / 10u));
}

static void dash_fmt_amp(char *buf, uint32_t ma_x10)
{
    snprintf(buf, DASH_VALUE_LEN, "%u.%03uA",
             (unsigned)(ma_x10 / 10000u), (unsigned)((ma_x10 % 10000u) / 10u));
}

/* Power keeps two decimals from 10 W up and three below that: the SW3538 column
 * is seven characters wide ("139.02W") while SW3526 never exceeds 65 W
 * ("65.120W"), so a one-decimal form is not needed. */
static void dash_fmt_watt(char *buf, uint32_t mw)
{
    if(mw >= 10000u)
        snprintf(buf, DASH_VALUE_LEN, "%u.%02uW",
                 (unsigned)(mw / 1000u), (unsigned)((mw % 1000u) / 10u));
    else
        snprintf(buf, DASH_VALUE_LEN, "%u.%03uW",
                 (unsigned)(mw / 1000u), (unsigned)(mw % 1000u));
}

static void dash_column_offline(dash_column_t *col)
{
    snprintf(col->volt, DASH_VALUE_LEN, "%s", DASH_OFFLINE_TEXT);
    snprintf(col->amp, DASH_VALUE_LEN, "%s", DASH_OFFLINE_TEXT);
    snprintf(col->watt, DASH_VALUE_LEN, "%s", DASH_OFFLINE_TEXT);
    col->protocol = DASH_OFFLINE_TEXT;
}

static const char *dash_protocol_sw3538(uint8_t protocol)
{
    switch(protocol)
    {
        case SW3538_PROTOCOL_QC2:       return "QC2.0";
        case SW3538_PROTOCOL_QC3:       return "QC3.0";
        case SW3538_PROTOCOL_QC3P:      return "QC3+";
        case SW3538_PROTOCOL_FCP:       return "FCP";
        case SW3538_PROTOCOL_SCP:       return "SCP";
        case SW3538_PROTOCOL_PD_FIXED:  return "PD-FIX";
        case SW3538_PROTOCOL_PD_PPS:    return "PD-PPS";
        case SW3538_PROTOCOL_PE11:      return "PE1.1";
        case SW3538_PROTOCOL_PE20:      return "PE2.0";
        case SW3538_PROTOCOL_VOOC10:    return "VOOC1";
        case SW3538_PROTOCOL_VOOC40:    return "VOOC4";
        case SW3538_PROTOCOL_SFCP:      return "SFCP";
        case SW3538_PROTOCOL_AFC:       return "AFC";
        case SW3538_PROTOCOL_TFCP:      return "TFCP";
        default:                        return DASH_OFFLINE_TEXT;
    }
}

static const char *dash_protocol_sw3526(uint8_t protocol)
{
    switch(protocol)
    {
        case SW3526_PROTOCOL_QC2:       return "QC2.0";
        case SW3526_PROTOCOL_QC3:       return "QC3.0";
        case SW3526_PROTOCOL_FCP:       return "FCP";
        case SW3526_PROTOCOL_SCP:       return "SCP";
        case SW3526_PROTOCOL_PD_FIXED:  return "PD-FIX";
        case SW3526_PROTOCOL_PD_PPS:    return "PD-PPS";
        case SW3526_PROTOCOL_PE11:      return "PE1.1";
        case SW3526_PROTOCOL_PE20:      return "PE2.0";
        case SW3526_PROTOCOL_VOOC:      return "VOOC";
        case SW3526_PROTOCOL_SFCP:      return "SFCP";
        case SW3526_PROTOCOL_AFC:       return "AFC";
        default:                        return DASH_OFFLINE_TEXT;
    }
}

/* SW3538 drives two ports (A + C) from one buck with a single protocol register,
 * so this column shows the chip summary: shared VOUT, the sum of both port
 * currents and the resulting power. */
static void dash_fill_sw3538(dash_column_t *col)
{
    SW3538_Handle *handle = APP_GetSW3538();
    uint16_t vout_mv = SW3538_ReadVOUTmV(handle);
    uint32_t iout_ma_x10 = SW3538_ReadPort1IOUTmA_x10(handle) + SW3538_ReadPort2IOUTmA_x10(handle);
    uint32_t mw = (((uint32_t)vout_mv * (iout_ma_x10 / 10u)) / 1000u);   /* mV * mA / 1000 = mW */

    col->name = DASH_NAME_SW3538;
    col->protocol = DASH_OFFLINE_TEXT;

    if(SW3538_IsOnline(handle))
    {
        dash_fmt_volt(col->volt, vout_mv);
        dash_fmt_amp(col->amp, iout_ma_x10);
        dash_fmt_watt(col->watt, mw);

        if(!SW3538_IsPort1DeviceOnline(handle) && !SW3538_IsPort2DeviceOnline(handle))
            col->protocol = DASH_PORT_NC_TEXT;
        else if(SW3538_GetProtocol(handle) == SW3538_PROTOCOL_NONE)
            col->protocol = DASH_PORT_5V_TEXT;
        else
            col->protocol = dash_protocol_sw3538((uint8_t)SW3538_GetProtocol(handle));
    }
    else
    {
        dash_column_offline(col);
    }
}

static void dash_fill_sw3526(dash_column_t *col, SW3526_Handle *handle, const char *name)
{
    const struct SW3526_StatusTypedef *status = SW3526_GetStatus(handle);
    uint16_t vout_mv = (status != 0) ? status->vout_mv : 0u;
    uint32_t iout_ma_x10 = (status != 0) ? status->iout_ma_x10 : 0u;
    uint32_t mw = (((uint32_t)vout_mv * (iout_ma_x10 / 10u)) / 1000u);   /* mV * mA / 1000 = mW */

    col->name = name;
    col->protocol = DASH_OFFLINE_TEXT;

    if((status != 0) && SW3526_IsOnline(handle))
    {
        dash_fmt_volt(col->volt, vout_mv);
        dash_fmt_amp(col->amp, iout_ma_x10);
        dash_fmt_watt(col->watt, mw);

        /* The protocol register carries the type in the low nibble; the raw
         * byte must be masked (SW3526_GetProtocol) or the online/high-voltage
         * bits make every lookup fall through to "---". */
        /* A plain Type-C sink draws the default 5 V without engaging any fast
         * charge protocol: 0x06.7 (protocol online) stays clear while the port
         * switch is on (0x07.1, PORT_ON).  Treating "no protocol" as "no
         * device" made a working 5 V load read NC, so a sink counts as
         * attached when the port is on (or a protocol is engaged). */
        if(!SW3526_IsPortOn(handle) && !SW3526_IsProtocolOnline(handle))
            col->protocol = DASH_PORT_NC_TEXT;
        else if(SW3526_GetProtocol(handle) == SW3526_PROTOCOL_NONE)
            col->protocol = DASH_PORT_5V_TEXT;
        else
            col->protocol = dash_protocol_sw3526((uint8_t)SW3526_GetProtocol(handle));
    }
    else
    {
        dash_column_offline(col);
    }
}

/* Board temperature from the 500 ms GX21M15U device mirror (BSP/GX21M15,
 * polled by thread_gx21m15 in APP/app_tasks.c).  The UI never touches I2C: it
 * only reads the cached milli-Celsius value and rounds it to 0.1 C.  Returning
 * 0 lets the caller fall back to the "--.-C" placeholder, exactly like an
 * offline chip column renders "---". */
static uint8_t dash_read_temperature(int16_t *tenths_c)
{
    GX21M15_Handle *handle = APP_GetGX21M15();
    int32_t milli_c;

    if((tenths_c == 0) || !GX21M15_IsOnline(handle))
        return 0u;

    milli_c = GX21M15_ReadTemperatureMilliC(handle);
    *tenths_c = (int16_t)((milli_c >= 0) ? ((milli_c + 50) / 100)
                                         : ((milli_c - 50) / 100));
    return 1u;
}

static const char *dash_pd_state_text(void)
{
    /* Input undervoltage overrides every other state (budget forced to 0 W). */
    if(VBUS_Sense_ReadMillivolts() < PWR_LIMIT_UVP_MV)
        return "UVP";

    if(PD_IsConnected())
    {
        if(!PD_IsPowerReady())
            return "NEG";

        return PD_IsEPRContractActive() ? "EPR" : "SPR";
    }

    /* No PD contract: the board input is the DC path. */
    return "DC";
}

static void dash_signature_append(char *signature, size_t size, const char *text)
{
    size_t used = strlen(signature);
    size_t len = strlen(text);

    if((used + len + 2u) >= size)
        return;

    signature[used] = '|';
    signature[used + 1u] = '\0';
    strcat(signature, text);
}

/* Compose the row-1 head (temperature / input voltage / total power limit).
 * Shared by the full dashboard and by the status line the main icon menu
 * paints at the top of the screen. */
static void dash_fmt_row1_head(char *head, size_t size)
{
    char temperature[8];
    char input_volts[8];
    char input_limit[8];
    uint32_t input_v;
    uint32_t limit_w;
    int16_t tenths_c;

    if(dash_read_temperature(&tenths_c))
    {
        const char *sign = "";

        if(tenths_c < 0)
        {
            sign = "-";
            tenths_c = (int16_t)-tenths_c;
        }

        snprintf(temperature, sizeof temperature, "%s%d.%dC", sign,
                 (int)(tenths_c / 10), (int)(tenths_c % 10));
    }
    else
    {
        snprintf(temperature, sizeof temperature, "%s", "--.-C");
    }

    input_v = ((uint32_t)VBUS_Sense_ReadMillivolts() + 500u) / 1000u;
    snprintf(input_volts, sizeof input_volts, "%luV", (unsigned long)input_v);

    /* Total output power limit: UVP -> 0 W, PD -> 95% of the contract power,
     * DC -> user value (the DOWN key cycles it while in the DC path).  The
     * merged mirror thread consumes this value for the per-chip allocation.
     * The number is right-aligned in three digits ("  0Wmax" / " 60Wmax" /
     * "270Wmax") so the "W" stays where the old "140Wmax" had it. */
    limit_w = PWR_Limit_GetW();
    snprintf(input_limit, sizeof input_limit, "%3luWmax", (unsigned long)limit_w);

    snprintf(head, size, "%s %s %s", temperature, input_volts, input_limit);
}

/* Row 1 with its coordinates unchanged; the main icon menu calls this from
 * ui.c so the menu shows the same live status line (128x80 layout only). */
void Dashboard_DrawStatusLine(const ui_t *ui)
{
    char head[DASH_TEXT_LEN];
    uint8_t color;

    dash_fmt_row1_head(head, sizeof head);

    color = (uint8_t)(ui->bgColor ^ 1u);
    Disp_SetFont(UI_FONT);
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&color);
    Disp_DrawStr(DASH_X_MARGIN, DASH_ROW1_Y, head);
    dash_draw_right(DASH_COL3_RIGHT, DASH_ROW1_Y, dash_pd_state_text());
}

void Dashboard_Page(ui_t *ui)
{
    static char s_signature[DASH_SIGNATURE_LEN];
    static uint8_t s_frame_valid = 0u;
    static uint32_t s_sent_mark = 0u;   /* flush count after our last frame */

    dash_column_t columns[DASH_COLUMN_COUNT];
    char signature[DASH_SIGNATURE_LEN];
    char row1[DASH_TEXT_LEN];
    char row1_head[DASH_TEXT_LEN];
    const char *pd_state;
    uint8_t color;
    uint8_t i;

    /* Navigation is swallowed here (only ENTER/BACK leave for the icon menu);
     * in the DC path DOWN also cycles the total power limit. */
    if((ui->action == UI_ACTION_UP) || (ui->action == UI_ACTION_DOWN))
    {
        if((ui->action == UI_ACTION_DOWN) && (PWR_Limit_IsAdjustable() != 0u))
            PWR_Limit_StepDown();

        ui->action = UI_ACTION_NONE;
    }

    dash_fill_sw3538(&columns[0]);
    dash_fill_sw3526(&columns[1], APP_GetSW3526_1(), DASH_NAME_SW3526_1);
    dash_fill_sw3526(&columns[2], APP_GetSW3526_2(), DASH_NAME_SW3526_2);

    pd_state = dash_pd_state_text();
    dash_fmt_row1_head(row1_head, sizeof row1_head);
    snprintf(row1, sizeof row1, "%s %s", row1_head, pd_state);

    /* Content signature: identical strings mean identical pixels, so the frame
     * is pushed to the panel only when something really changed. */
    memset(signature, 0, sizeof signature);
    dash_signature_append(signature, sizeof signature, row1);
    for(i = 0u; i < DASH_COLUMN_COUNT; i++)
    {
        dash_signature_append(signature, sizeof signature, columns[i].name);
        dash_signature_append(signature, sizeof signature, columns[i].volt);
        dash_signature_append(signature, sizeof signature, columns[i].amp);
        dash_signature_append(signature, sizeof signature, columns[i].watt);
        dash_signature_append(signature, sizeof signature, columns[i].protocol);
    }

    color = ui->bgColor;
    Disp_SetFont(UI_FONT);
    /* The menu leaves a restricted clip window behind (title/data areas), so the
     * full-screen page restores the maximum window before drawing. */
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&color);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);

    color = (uint8_t)(ui->bgColor ^ 1u);
    Disp_SetDrawColor(&color);
    /* Row 1: fields flow from the left, PD input state is pinned right - the
     * main icon menu shows the exact same line, so it lives in the shared
     * helper (same coordinates there as here). */
    Dashboard_DrawStatusLine(ui);
    /* SW3538 status reads 3 px right of the column centre. */
    dash_draw_column(&columns[0], DASH_X_MARGIN, DASH_COL1_RIGHT, 3);
    dash_draw_column(&columns[1], DASH_COL2_X, DASH_COL2_RIGHT, 0);
    dash_draw_column(&columns[2], DASH_COL3_X, DASH_COL3_RIGHT, 0);

    /* Another page owns the same u8g2 buffer and every fade step flushes its own
     * frame, so the transport count is the reliable "is the panel still mine?"
     * check: it covers menu re-entry (same menu state, same content) as well as
     * real content changes. */
    if((s_frame_valid == 0u) || (Disp_GetFlushCount() != s_sent_mark) ||
       (memcmp(s_signature, signature, sizeof signature) != 0))
    {
        memcpy(s_signature, signature, sizeof signature);
        s_frame_valid = 1u;
        Disp_SendBuffer();
        s_sent_mark = Disp_GetFlushCount();   /* our own request bumped the count */
    }
}
