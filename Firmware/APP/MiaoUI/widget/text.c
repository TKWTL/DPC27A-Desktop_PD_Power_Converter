/*
 * This file is part of the MiaoUI Library.
 *
 * Copyright (c) 2025, JFeng-Z, <2834294740@qq.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Created on: 2025-02-08
 */
#include "widget/text.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "string.h"

#include "i18n.h"

/* Both text paths use the shared proportional font; the locale still
 * chooses UTF-8 versus ASCII decoding. Wrap by measured pixel width. */
static uint8_t text_paragraph_is_utf8(const char *p)
{
    if(!DPC_I18n_IsChinese())
        return 0u;
    while(*p && *p != '\n')
    {
        if((unsigned char)*p++ >= 0x80u)
            return 1u;
    }
    return 0u;
}

static uint16_t text_line_width(const char *line, uint8_t utf8)
{
    if(utf8)
        return DPC_I18n_TextWidth(line);
    Disp_SetFont(UI_FONT);
    return Disp_GetUTF8Width(line);
}

static void text_draw_line(uint16_t x, uint16_t y, const char *line, uint8_t utf8)
{
    if(utf8)
        DPC_I18n_DrawText(x, y, line);
    else
    {
        Disp_SetFont(UI_FONT);
        Disp_DrawStr(x, y, line);
    }
}

/* Width-based wrapping of complete UTF-8 code points. No heap or split
 * multibyte glyphs. The original 128x80 dialog and explicit line breaks stay. */
void Text_Widget(ui_t *ui)
{
    char line[128];
    unsigned len = 0u;
    uint16_t y;
    uint8_t utf8;
    const uint16_t left = 4u;
    const uint16_t right = UI_HOR_RES - 4u; /* 120px of measured text */
    if(!ui || !ui->nowItem || !ui->nowItem->element || !ui->nowItem->element->text)
        return;
    const char *ptr = DPC_I18n_Text(ui->nowItem->element->text->ptr);
    const uint16_t lh = ui->nowItem->element->text->fontHight;
    if(!lh || !Dialog_Show(ui, 1, 1, UI_HOR_RES - 2, UI_VER_RES - 2))
        return;
    y = lh;
    utf8 = text_paragraph_is_utf8(ptr);
    while(*ptr && y < UI_VER_RES - 2u)
    {
        unsigned n = 1u;
        const char *part = ptr;
        unsigned consume;
        if(*ptr == '\n')
        {
            line[len] = '\0';
            text_draw_line(left, y, line, utf8);
            len = 0u;
            y += lh;
            ++ptr;
            utf8 = text_paragraph_is_utf8(ptr);
            continue;
        }
        if(*ptr == '\t')
        {
            part = "   ";
            n = 3u;
            consume = 1u;
        }
        else
        {
            n = DPC_I18n_CharBytes(ptr);
            consume = n;
        }
        if(len + n >= sizeof(line))
        {
            line[len] = '\0';
            text_draw_line(left, y, line, utf8);
            len = 0;
            y += lh;
            if(y >= UI_VER_RES - 2u)
                break;
        }
        memcpy(line + len, part, n);
        line[len + n] = '\0';
        if(len && text_line_width(line, utf8) > right - left)
        {
            line[len] = '\0';
            text_draw_line(left, y, line, utf8);
            len = 0u;
            y += lh;
            if(y >= UI_VER_RES - 2u)
                break;
            memcpy(line, part, n);
            line[n] = '\0';
        }
        len += n;
        ptr += consume;
    }
    if(len && y < UI_VER_RES - 2u)
    {
        line[len] = '\0';
        text_draw_line(left, y, line, utf8);
    }
    Disp_SendBuffer();
}
