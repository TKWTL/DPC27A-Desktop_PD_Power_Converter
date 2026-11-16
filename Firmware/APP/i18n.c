/* Display-only dictionary inspired by IH01A (original English key -> zh-Hans).
 * Dashboard and numeric data still use Disp_DrawStr and UI_FONT directly.
 * No heap, locale state is RAM-only and reset defaults to English. */
#include "i18n.h"
#include "MiaoUI/ui_conf.h"
#include "MiaoUI/display/dispDriver.h"
#include <string.h>

typedef struct {
    const char *english;
    const char *zh;
} translation_t;

static uint8_t s_chinese;
static const translation_t s_dictionary[] = {
#define DPC_I18N_RUNTIME_TABLE 1
#include "i18n_table.inc"
#undef DPC_I18N_RUNTIME_TABLE
};

void DPC_I18n_SetChinese(uint8_t enable)
{
    s_chinese = enable ? 1u : 0u;
}

uint8_t DPC_I18n_IsChinese(void)
{
    return s_chinese;
}

const char *DPC_I18n_Text(const char *english)
{
    unsigned i;
    if(!english)
        return "";
    if(!s_chinese)
        return english;
    for(i = 0u; i < sizeof(s_dictionary) / sizeof(s_dictionary[0]); ++i)
    {
        /* Pointer comparison fast path; exact byte match is authoritative. */
        if(english == s_dictionary[i].english ||
           strcmp(english, s_dictionary[i].english) == 0)
            return s_dictionary[i].zh;
    }
    return english;
}

const uint8_t *DPC_I18n_Font(void)
{
    return UI_FONT;
}

uint16_t DPC_I18n_TextWidth(const char *text)
{
    Disp_SetFont(DPC_I18n_Font());
    return Disp_GetUTF8Width(text ? text : "");
}

uint16_t DPC_I18n_DrawText(uint16_t x, uint16_t y, const char *text)
{
    Disp_SetFont(DPC_I18n_Font());
    if(!text)
        text = "";
    return s_chinese ? Disp_DrawUTF8(x, y, text) : Disp_DrawStr(x, y, text);
}

uint16_t DPC_I18n_DrawLabel(uint16_t x, uint16_t y, const char *english)
{
    return DPC_I18n_DrawText(x, y, DPC_I18n_Text(english));
}

unsigned DPC_I18n_CharBytes(const char *text)
{
    unsigned char lead = (unsigned char)text[0];
    unsigned n = 1u;
    unsigned i;
    if(lead >= 0xC2u && lead <= 0xDFu)
        n = 2u;
    else if(lead >= 0xE0u && lead <= 0xEFu)
        n = 3u;
    else if(lead >= 0xF0u && lead <= 0xF4u)
        n = 4u;
    for(i = 1u; i < n; ++i)
        if(((unsigned char)text[i] & 0xC0u) != 0x80u)
            return 1u; /* terminator/invalid continuation, never overread */
    return n;
}
