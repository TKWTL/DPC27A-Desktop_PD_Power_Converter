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
#include "widget/parameter.h"
#include "stdio.h"
#include <string.h>
#include "core/ui.h"
#include "display/dispDriver.h"
#include "i18n.h"

void ParameterSetting_Widget(ui_t *ui)
{
    uint8_t color;
    char num[16] = {0};
    int x = 4, y = 12, w = UI_HOR_RES - 8, h = UI_VER_RES - 32;
    int Step = ui->nowItem->element->data->step;

    #if ( UI_USE_FREERTOS == 1 )
    if(ui->nowItem->element->data->functionType == UI_DATA_FUNCTION_EXIT_EXECUTE)
    {
        if(ui->nowItem->element->data->dataRootMutex != NULL)xSemaphoreTake(*ui->nowItem->element->data->dataRootMutex, portMAX_DELAY);
        if(ui->nowItem->element->data->dataRootTask != NULL)vTaskSuspend(*ui->nowItem->element->data->dataRootTask);
        if(ui->nowItem->element->data->dataRootMutex != NULL)xSemaphoreGive(*ui->nowItem->element->data->dataRootMutex);
    }
    #endif

    if(Dialog_Show(ui, x, y, w, h))
    {
        switch (ui->action)
        {
        case UI_ACTION_DOWN:
            /* K1 is the only non-ENTER key on this hardware: it steps the
             * value up, and Draw_Scrollbar wraps at min/max so the whole
             * range stays reachable with the one key. */
            Draw_Scrollbar(ui, x + 4, y + 18, w - 12, 6, 3, Step);
            if(ui->nowItem->element->data->function != NULL && ui->nowItem->element->data->functionType == UI_DATA_FUNCTION_STEP_EXECUTE)ui->nowItem->element->data->function(ui);
            break;
        default:
            Draw_Scrollbar(ui, x + 4, y + 18, w - 12, 6, 3, 0);
            break;
        }

        if(ui->nowItem->element->data->name == NULL) ui->nowItem->element->data->name = "Null!!";

        const ui_data_t *data = ui->nowItem->element->data;
        const char *label = DPC_I18n_Text(data->name);
        if(data->ptr && (data->dataType == UI_DATA_INT || data->dataType == UI_DATA_FIXED))
            UI_FmtScaled(num, sizeof(num), *(const int *)data->ptr,
                         data->dataType == UI_DATA_FIXED ? data->decimals : 0u);
        else
            strcpy(num, "--");

        /* Measure numbers in the shared proportional font. Clip the translated label
         * before the right-aligned value; never split a UTF-8 string. */
        Disp_SetDrawColor(&ui->bgColor);
        Disp_DrawBox(x + 3, y + 1, w - 6, 14);
        color = ui->bgColor ? 0 : 1;
        Disp_SetDrawColor(&color);
        Disp_SetFont(UI_FONT);
        uint16_t num_x = (uint16_t)(x + w - 4 - Disp_GetUTF8Width(num));
        Disp_SetClipWindow(x + 3, y + 1, num_x > x + 5 ? num_x - 2u : x + 3, y + 15);
        DPC_I18n_DrawText(x + 4, y + 13, label);
        Disp_SetMaxClipWindow();
        Disp_SetFont(UI_FONT);
        Disp_DrawStr(num_x, y + 13, num);
        Disp_SendBuffer();
    }
}

void Switch_Widget(ui_t *ui)
{
    ui_data_t *data;
    ui_page_t *page;
    ui_item_t *it;

    if (ui == NULL || ui->nowItem == NULL || ui->nowItem->element == NULL || ui->nowItem->element->data == NULL)
        return;

    data = ui->nowItem->element->data;
    if (data->dataType != UI_DATA_SWITCH || data->ptr == NULL)
        return;

    /* ===== 多选一（radio）判定：min==1 且 max>0 ===== */
    if (data->min == 1 && data->max > 0)
    {
        int group_id = data->max;

        /* 先全 0 */
        page = ui->nowItem->page.location;
        it = page->item.head;
        for (uint16_t i = 0; i <= page->length; i++)
        {
            if (it->itemType == UI_ITEM_DATA &&
                it->element && it->element->data &&
                it->element->data->dataType == UI_DATA_SWITCH &&
                it->element->data->min == 1 &&              /* 也是 radio */
                it->element->data->max == group_id &&       /* 同组 */
                it->element->data->ptr != NULL)
            {
                *(uint8_t *)it->element->data->ptr = 0;
            }
            it = it->nextItem;
        }

        /* 再把当前项写 1（保证同组始终只有一个为 1） */
        *(uint8_t *)data->ptr = 1;
    }
    else
    {
        /* ===== 普通 switch（多选）保持原行为：翻转 ===== */
        *(uint8_t *)data->ptr = !*(uint8_t *)data->ptr;
    }

    /* ===== 保留用户自定义回调，不占用 function ===== */
    if (data->function != NULL)
        data->function(ui);

    /* ===== 原有 RTOS task suspend/resume 逻辑保持不变 ===== */
    #if ( UI_USE_FREERTOS == 1 )
    if (data->dataRootTask != NULL)
    {
        switch (*(uint8_t *)data->ptr)
        {
        case false:
            vTaskResume(*data->dataRootTask);
            break;
        case true:
            vTaskSuspend(*data->dataRootTask);
            break;
        default:
            break;
        }
    }
    #endif
}
