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
#include "core/ui.h"
#include "display/dispDriver.h"
#include "indev/indevDriver.h"
#include "images/image.h"
#include "Pages/dashboard.h"   /* 图标页顶部复用 dashboard 第一行状态文本 */
#include "widget/parameter.h"
#include "widget/text.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "framework/pm_api.h"   /* 根菜单 BACK → pm_api_force_sleep() */

//禁用调试输出定义函数
void disable_logging(const char *s){
    (void)s;
}

/**
 * 添加项目到页面
 *
 * 该函数用于向指定的页面添加一个新的项目。项目包括名称、类型、元素、图像等信息，
 * 并且可以设置项目的功能回调。添加的项目将按照链表的形式管理，同时维护页面上项目的数量。
 *
 * @param name 项目名称，不可为NULL。
 * @param type 项目类型，如数据、开关、文本等。
 * @param Element 项目关联的元素，根据项目类型不同，包含不同的数据。
 * @param image 项目显示的图像，如果为NULL，则使用预设图像。
 * @param item 待添加的项目结构体。
 * @param localPage 项目所在的页面结构体。
 * @param nextPage 项目跳转的下一个页面结构体，如果无跳转，则为NULL。
 * @param function 项目对应的函数指针，用于处理项目特定的功能。
 */
void AddItem(const char *name, UI_ITEM_TYPE type, const uint8_t *image, ui_item_t *item, ui_page_t *localPage, ui_page_t *nextPage, ui_item_function function)
{
    // 检查输入参数的有效性
    // 参数检验
    if (!name || !item || !localPage) {
        UI_LOG("Invalid parameter(s)\n");
        return; // 早期返回以避免进一步错误
    }
    item->x = UI_PAGE_INIT_X;
    item->y = UI_PAGE_INIT_Y;
    // 初始化项目的基本信息
    item->itemName = name; // 设置项目名称
    item->itemType = type; // 设置项目类型

    // 如果页面类型为图像且没有指定图像，则使用预设图像
    if(localPage->type == UI_PAGE_ICON && image != NULL)
        item->logo = image;

    item->page.location = localPage; // 设置项目所在页面

    if(function != NULL)
        item->itemFunction = function; // 设置项目关联的函数

    // 初始化项目链接信息
    /* 初始化项目下一个项为NULL */
    item->nextItem = NULL;

    // 设置跳转页面的父项目为当前项目
    /* 设置跳转页面的父级项目 */
    if (nextPage != NULL)
        nextPage->item.parent = item;
    else // 如果没有下一个页面，则设置为当前页面
        nextPage = localPage;
    item->page.jumpPage = nextPage; // 设置项目跳转页面

    // 如果页面为空，则初始化项目链表
    /* 链式结构创建项目 */
    if (localPage->item.head == NULL) // 如果是首个项目
    {
        // 初始化项目链表头部和尾部
        item->lastItem = item;
        localPage->item.head = item;
        localPage->item.tail = item;
        localPage->length = 0; // 初始化项目计数
    }
    else // 如果不是首个项目
    {
        // 将新项目连接到链表尾部
        // 连接新项目到链表尾部
        item->lastItem = localPage->item.tail;                // 新项目上一个项指向当前尾项目
        localPage->item.tail->nextItem = item;                // 尾项目下一个项指向新项目
        localPage->item.tail = localPage->item.tail->nextItem; // 更新尾项目为新项目
        localPage->length++; // 项目计数加一
    }
    // 设置项目ID为当前页面项目数量
    item->id = localPage->length; // 设置项目ID为当前页面项目计数
    // 关联页面的头尾项目互相指向，用于快速遍历
    // 关联页面的头尾项目互相指向，用于快速遍历
    localPage->item.tail->nextItem = localPage->item.head;
    localPage->item.head->lastItem = localPage->item.tail;
}

/**
 * @brief 向页面中添加一个新页面。
 *
 * @param name 新页面的名称。
 * @param page 指向新页面结构体的指针。
 * @param type 新页面的类型。
 * @param parent 所属的上级页面。
 */
void AddPage(const char *name, ui_page_t *page, UI_PAGE_TYPE type, ui_page_t *parent)
{
    // 初始化新页面的基本信息
    page->pageName = name;
    page->item.head = NULL;
    page->item.tail = NULL;
    page->pageState = UI_PAGE_STATIC;
    page->type = type;
    page->parentPage = parent;
}

/**
 * @brief PID实现的动画（Q10 定点整数版，无 FPU）
 *
 * @param targrt    目标坐标值（Q10 定点）。
 * @param now       当前坐标值（Q10 定点）。
 * @param obj       PID参数（kp/ki/kd 均为 Q10）。
 *
 * 旧版用 float，会把 __addsf3/__subsf3/__mulsf3/__divsf3/__floatsisf…
 * 等软浮点库全部拉进镜像（实测 ~3.7 KB）。所有量改用 Q10 整数后，
 * 只需 32 位乘加与算术右移，精度 1/1024 像素，手感不变。
 */
#define UI_ANIM_SUM_MAX   (1 << 22)   /* 积分项限幅：防 ki*sum_error 溢出 */

int32_t UI_Animation(int32_t targrt, int32_t now, ui_animation_param_t *obj)
{
    int32_t x = now;
    int32_t delta_error;
    int32_t velocity;

    // 计算误差
    obj->error = targrt - x;
    // 积分环节，累加误差
    obj->sum_error += obj->error;
    if(obj->sum_error > UI_ANIM_SUM_MAX)
        obj->sum_error = UI_ANIM_SUM_MAX;
    else if(obj->sum_error < -UI_ANIM_SUM_MAX)
        obj->sum_error = -UI_ANIM_SUM_MAX;
    // 微分环节，计算误差变化率
    delta_error = obj->error - obj->last_error;

    // 计算控制量
    velocity = (int32_t)(((obj->kp * obj->error) + (obj->ki * obj->sum_error) +
                          (obj->kd * delta_error)) >> UI_FX_SHIFT);

    // 更新状态
    x += velocity;
    obj->last_error = obj->error;

    /* 收敛死区：误差已小于半个像素时直接吸附到目标。
     * 整数 PID 的步进会被 >>10 吃掉（约 1/1024 像素），不吸附就会永远差
     * 那么一点点，而画图那边一旦向下取整就变成可见的 1 px 偏差。 */
    if(((targrt - x) < (UI_FX_ONE / 2)) && ((x - targrt) < (UI_FX_ONE / 2)))
        x = targrt;

    return x;
}

/**
 * @brief 定点动画是否已到位
 *
 * 整数 PID 在误差极小时步进会被右移吃掉（1/1024 像素级死区），
 * 不会严格等于目标值，因此用 1 像素以内作为到位判据。
 */
static uint8_t UI_IsAnimationDone(int32_t now, int32_t target)
{
    int32_t d = now - target;

    if(d < 0)
        d = -d;

    return (d <= UI_FX_ONE) ? 1u : 0u;
}

/**
 * 使用圆形函数实现的缓动效果，既适用于渐入也适用于渐出。
 *
 * @param t 当前时间，表示动画的进度。
 * @param b 初始值，动画开始前的位置。
 * @param c 变化量，动画结束时的总变化。
 * @param d 持续时间，动画的总时长。
 * @return 返回动画当前位置的整数像素。
 *
 * 旧版是 float 版圆形缓动，这里用 Q12 中间精度的 smoothstep 多项式
 * y = b + c·x²·(3-2x) 实现（与上一版无 FPU 的 float 实现数学上等价），
 * 全程整数运算。c 最大 128，中间量不超 int32。
 */
int easeInOutCirc(int t, int b, int c, int d)
{
    int32_t x_q;    /* Q12: 0 ~ 4096 */
    int32_t sq_q;   /* x²，Q12 */
    int32_t s_q;    /* 3-2x，Q12 */

    if(d <= 0)
        return b + c;

    if(t <= 0)
        x_q = 0;
    else if(t >= d)
        x_q = (int32_t)(UI_FX_ONE << 2);
    else
        x_q = ((int32_t)t << 12) / d;

    sq_q = (x_q * x_q) >> 12;
    s_q = (int32_t)((UI_FX_ONE << 2) * 3) - (2 * x_q);

    return b + (int)((((int32_t)c * sq_q) >> 12) * s_q >> 12);
}

/**
 * @brief 定点数显示：把“缩放整数 value / 10^decimals”写成字符串
 *
 * 项目不再使用 %f（无 FPU，且已用自研整数 printf 替掉软浮点 printf），
 * 所有浮点显示统一走这个函数，UI 数据项用 UI_DATA_FIXED + decimals 描述。
 */
void UI_FmtScaled(char *buf, size_t size, int32_t value, uint8_t decimals)
{
    char reversed[12];
    uint32_t mag = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    uint8_t n = 0u, digits = 0u;
    size_t pos = 0u;

    if(size == 0u)
        return;
    if(decimals > 3u)
        decimals = 0u;

    /* Build least significant digit first, keeping leading fractional zeros.
     * The largest signed 32-bit representation needs 12 chars plus NUL. */
    do
    {
        reversed[n++] = (char)('0' + mag % 10u);
        mag /= 10u;
        ++digits;
        if(decimals && digits == decimals)
            reversed[n++] = '.';
    } while(mag || digits <= decimals);

    if(value < 0)
        reversed[n++] = '-';
    if(n >= size)
    {
        buf[0] = '\0'; /* never pass a misleading truncated number to UI */
        return;
    }
    while(n)
        buf[pos++] = reversed[--n];
    buf[pos] = '\0';
}


void Change_UIState(ui_t *ui, UI_STATE state)
{
    ui->menuState = state;
}

//初始化子菜单的坐标
static void Cursor_AnimationParam_Init(ui_t *ui, ui_item_t *next_item)
{
    ui->nowItem->page.location->cursorLastColumn = ui->cursor.targrtColumn;
    ui->nowItem->page.location->cursorLastRow = ui->cursor.targrtRow;
    ui->nowItem->page.location->cursorLastWide = ui->cursor.targrtWide;
    ui->cursor.targrtColumn = next_item->x;
    ui->cursor.targrtRow = next_item->y - UI_FONT_HIGHT;
}

//返回父菜单的坐标计算
//勘误：修复了非菜单第一项返回时位置错误的问题
static void Cursor_AnimationParam_Change(ui_t *ui/*, ui_item_t *nowitem*/)
{//ui->nowItem->page.location->parentPage->item.lastJumpItem->page.location, ui->nowItem->page.location->item.head
    ui->cursor.targrtColumn = ui->nowItem->page.location->parentPage->cursorLastColumn;
    ui->cursor.targrtRow = ui->nowItem->page.location->parentPage->cursorLastRow;
    ui->cursor.targrtWide = ui->nowItem->page.location->parentPage->cursorLastWide;
}

static void Dialog_AnimationParam_Init(ui_t *ui)
{
    ui->dialog.nowHigh = 0;
    ui->dialog.nowWide = 0;
    ui->dialog.times = 0;
}

static void Scrollbar_Init(ui_t *ui)
{
    ui->scrollbar.value = 0;
}

static void Optionbar_Init(ui_t *ui)
{
    ui->optionbar.nowLenght = 0;
    ui->optionbar.position = 0;
}

/**
 * 绘制对话框
 * @param x 对话框左上角x坐标
 * @param y 对话框左上角y坐标
 * @param w 对话框宽度
 * @param h 对话框高度
 * 该函数首先绘制对话框的边框，然后绘制对话框的背景盒。
 * 使用了OLED显示设备的相关函数来完成绘制操作。
 */
static void Draw_Dialog(ui_t *ui, uint16_t x,uint16_t y,uint16_t Now_w,uint16_t Now_h,int16_t targrtW,int16_t targrtH)
{
    uint8_t bgC = ui->bgColor;
    Disp_SetDrawColor(&bgC);
    Disp_DrawBox(x, y, targrtW, Now_h);
    // 设置绘制背景的颜色，并绘制背景盒
    bgC = bgC^0x01;
    Disp_SetDrawColor(&bgC);
    Disp_DrawLine(x, y, x+Now_w, y);
    Disp_DrawLine(x, y, x, y+Now_h);
    Disp_DrawLine(x+targrtW, y+targrtH, x+targrtW, y+targrtH-Now_h);
    Disp_DrawLine(x+targrtW, y+targrtH, x+targrtW-Now_w, y+targrtH);
}

/**
 * @brief 显示一个按指定尺寸缩放的对话框。
 *
 * 此函数用于在应用绘制状态时，通过线性动画效果展示一个对话框的缩放过程。函数首先检查当前是否处于应用绘制状态，
 * 如果是，则根据设定的时间和目标尺寸计算当前对话框的宽度和高度，并进行绘制。当动画时间达到预设的对话框显示时间
 * 后，将状态切换到应用运行状态，并返回true。整个过程通过OLED发送缓冲区来更新显示。
 *
 * @param x 对话框的x坐标。
 * @param y 对话框的y坐标。
 * @param targrtW 目标对话框的宽度。
 * @param targrtH 目标对话框的高度。
 * @return uint8_t 如果动画时间达到预设的对话框显示时间，则返回true；否则返回false。
 */
uint8_t Dialog_Show(ui_t *ui, int16_t x,int16_t y,int16_t targrtW,int16_t targrtH)
{
    // 当前处于应用绘制状态时，处理对话框的缩放动画
    if (ui->menuState == UI_ITEM_DRAWING)
    {
        // 根据当前时间和目标尺寸计算对话框的当前宽度
        ui->dialog.nowWide = (uint16_t)easeInOutCirc(ui->dialog.times, 0, targrtW, UI_DIALOG_SCALE_ANI_TOTAL_TIME);
        // 根据当前时间和目标尺寸计算对话框的当前高度
        ui->dialog.nowHigh = (uint16_t)easeInOutCirc(ui->dialog.times, 0, targrtH, UI_DIALOG_SCALE_ANI_TOTAL_TIME);
        // 绘制当前尺寸的对话框
        Draw_Dialog(ui, x, y, ui->dialog.nowWide, ui->dialog.nowHigh, targrtW, targrtH);
        ui->dialog.times ++;
    }

    // 当动画时间达到预设的对话框显示时间时，切换到应用运行状态
    if ((ui->dialog.nowWide == targrtW) && (ui->dialog.nowHigh == targrtH))
    {
        ui->dialog.times = 0;
        Change_UIState(ui, UI_ITEM_RUNING);
        return true;
    }

    Disp_SendBuffer();
    return false;
}

uint8_t Notifications(ui_t *ui, int16_t x,int16_t y,int16_t targrtW,int16_t targrtH)
{
    // 当前处于应用绘制状态时，处理对话框的缩放动画
    if (ui->menuState == UI_ITEM_DRAWING)
    {
        // 根据当前时间和目标尺寸计算对话框的当前宽度
        ui->dialog.nowWide = (uint16_t)easeInOutCirc(ui->dialog.times, 0, targrtW, UI_DIALOG_SCALE_ANI_TOTAL_TIME);
        // 根据当前时间和目标尺寸计算对话框的当前高度
        ui->dialog.nowHigh = (uint16_t)easeInOutCirc(ui->dialog.times, 0, targrtH, UI_DIALOG_SCALE_ANI_TOTAL_TIME);
        // 绘制当前尺寸的对话框
        Draw_Dialog(ui, x, y, ui->dialog.nowWide, ui->dialog.nowHigh, targrtW, targrtH);
        ui->dialog.times ++;
    }

    // 当动画时间达到预设的对话框显示时间时，切换到应用运行状态
    if ((ui->dialog.nowWide == targrtW) && (ui->dialog.nowHigh == targrtH))
    {
        ui->dialog.times = 0;
        char value[20] = {0};
        snprintf(value, sizeof(value), "%s OK!", ui->nowItem->itemName);
        Disp_DrawStr(x + 5, y + targrtH/2, value);
        Change_UIState(ui, UI_ITEM_RUNING);
        return true;
    }
    Disp_SendBuffer();
    return false;
}

/**
 * 绘制一个滚动条
 * 该函数根据给定的参数，在OLED屏幕上绘制一个滚动条。滚动条的可见部分的长度
 * 会根据当前值在最小值和最大值之间动态调整。
 * 同时也会改动参数的值
 * @param x 滚动条左上角的x坐标
 * @param y 滚动条左上角的y坐标
 * @param w 滚动条的宽度
 * @param h 滚动条的高度
 * @param r 滚动条圆角的半径
 * @param data->min 滚动条代表的最小值
 * @param data->max 滚动条代表的最大值
 * @param data->step 滚动条的步进值
 * @param NowValue 滚动条当前的值
 */
void Draw_Scrollbar(ui_t *ui, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, int step)
{
    uint8_t color = ui->bgColor;
    int32_t temp_q = 0;   /* Q10 定点的滚动条长度 */
    ui_data_t *data = (ui->nowItem != NULL && ui->nowItem->element != NULL) ? ui->nowItem->element->data : NULL;

    if(data == NULL)
    {
        return;
    }
    #if ( UI_USE_FREERTOS == 1 )
    if(data->dataRootMutex != NULL)xSemaphoreTake(*data->dataRootMutex, portMAX_DELAY);
    #endif

    switch (data->dataType)
    {
    case UI_DATA_INT:
    case UI_DATA_FIXED:   /* 定点整数：与 INT 同构 */
    {
        int *pv = (int *)data->ptr;
        int span = data->max - data->min;
        int bar_w = (w > 6u) ? ((int)w - 6) : 0;

        if((pv != NULL) && (*pv <= data->max) && (*pv >= data->min))
        {
            /* 根据当前值计算滚动条可见部分的长度 */
            *pv += step;
            if (*pv > data->max)
            {
                *pv = data->min;   //实现回环设置
            }
            if (*pv < data->min)
            {
                *pv = data->max;   //实现回环设置
            }

            if(span != 0)
            {
                int delta = (*pv - data->min);
                if(delta < 0)
                    delta = -delta;
                temp_q = UI_FX((delta * bar_w) / span);
            }
            temp_q += UI_FX(6);
        }
        break;
    }
    default:
        break;
    }
    // 绘制滚动条的填充部分
    Disp_SetDrawColor(&color);
    Disp_DrawRBox(x, y, w, h, r);
    color = color ? 0 : 1;
    Disp_SetDrawColor(&color);
    ui->scrollbar.value = UI_Animation(temp_q, ui->scrollbar.value, &ui->animation.scrollbar_ani);

    /* 防积分饱和与越界：整数 PID 贴近目标时会推过头一两帧，数据条一旦画得比
     * 轨道还长，超出轨道右端的那截白尖落在“擦除盒”之外，之后每一帧都擦不掉——
     * 表现为轨道右端残留一块白条（其左缘是擦除端帽留下的凹三角），数据条拉满时
     * 又正好与它重合。到位即吸附并清零积分项，绘制宽度再钳在 [0, w] 之内。 */
    if(((temp_q - ui->scrollbar.value) < (UI_FX_ONE / 2)) &&
       ((ui->scrollbar.value - temp_q) < (UI_FX_ONE / 2)))
    {
        ui->scrollbar.value = temp_q;
        ui->animation.scrollbar_ani.sum_error = 0;
    }
    if(ui->scrollbar.value > UI_FX((int32_t)w))
        ui->scrollbar.value = UI_FX((int32_t)w);
    else if(ui->scrollbar.value < 0)
        ui->scrollbar.value = 0;

    /* u8g2_DrawRBox() 在 w < 2r 时内部 ww = w - 2r 会无符号下溢，把中段当成
     * 超大宽度画出一条横贯全屏的白条（动画从 0 生长的头几帧、或数值为 0 时
     * 都会命中）。这条越界白条落在擦除轨道(0..w)之外的两端，之后每帧都擦不
     * 掉：进弹窗一次就在左右两端各留一块残迹，右侧那块最大，且其左缘正是擦
     * 除轨道端帽的凹弧（数据条拉满时又和它重合）。宽度不足 2r 时直接不画，
     * 动画长到 6px（=2r，恰好也是本控件的最小可见长度）再开始绘制。 */
    if(UI_FXI(ui->scrollbar.value) >= (int32_t)(2u * r))
        Disp_DrawRBox(x, y, (uint16_t)UI_FXI(ui->scrollbar.value), h, r);
    #if ( UI_USE_FREERTOS == 1 )
    if(data->dataRootMutex != NULL)xSemaphoreGive(*data->dataRootMutex);
    #endif
}

/**
 * @brief 使屏幕上的内容逐渐消失的函数
 *
 * @param disapper 一个用于控制消失程度的参数，初始值建议为0，每次调用本函数会自动增加
 * @return uint8_t 返回调整后的disapper值，用于下次调用时继续消失过程
 */
static uint8_t UI_Disapper(ui_t *ui, uint8_t disapper)
{
    short disapper_temp = 0;
    // 计算屏幕缓冲区的总长度
    int length = 8 * Disp_GetBufferTileHeight() * Disp_GetBufferTileWidth();
    uint8_t *p = Disp_GetBufferPtr(); // 获取屏幕缓冲区的指针

    // 如果背景色为黑色，那么执行与操作，让像素点逐渐变暗从而消失
    if(ui->bgColor==0)
    {
        for( int i = 0; i < length; i++)
        {
            p[i] = p[i] & (rand()%0xff) >> disapper; // 通过与操作使像素点变暗
        }
    }
    else // 如果背景色不是黑色，执行或操作，让像素点逐渐变亮从而消失
    {
        for( int i = 0; i < length; i++)
        {
            p[i] = p[i] | (rand()%0xff) >> disapper; // 通过或操作使像素点变亮
        }
    }

    disapper += 2; // 每次调用使消失程度增加，以便逐渐完成消失过程
    if(disapper >= 8) // 当消失程度达到最大值时重置为0，准备下一次调用
    {
        disapper = 0;
    }

    Disp_SendBuffer(); // 将更新后的缓冲区内容发送到OLED显示设备
    disapper_temp = disapper;
    return disapper_temp; // 返回调整后的disapper值，供外部调用者使用
}

/**
 * @brief 绘制选项位置
 *
 * 该函数用于在OLED屏幕上动态绘制选项的位置，实现类似选项滑动选择的效果。
 * 根据当前项和下一个项的信息，计算并绘制出选项的滑动过程。
 *
 * @param nowItem 当前选项项的指针
 * @param next_item 下一个选项项的指针
 * @return uint8_t 如果绘制过程完成（达到设定的时间），返回true；否则返回false。
 */
static uint8_t Draw_TextPage_Optionbar(ui_t *ui, ui_item_t *now_Item, ui_item_t *next_item)
{
    (void)now_Item;
    // 根据下一个项的id和位置长度，计算其理论绘制长度（整数比例，不开 float）
    {
        int total = (next_item->page.location->length > 0) ? next_item->page.location->length : 1;
        ui->optionbar.targetLenght = UI_FX((int32_t)((UI_VER_RES * next_item->id) / total));
    }
    // 使用定点 PID 计算当前的绘制长度
    ui->optionbar.nowLenght = UI_Animation(ui->optionbar.targetLenght, ui->optionbar.nowLenght, &ui->animation.optionbar_ani);
    ui->optionbar.position = UI_Animation(UI_FX(UI_VER_RES), ui->optionbar.position, &ui->animation.optionbarPos_ani);//实现进入页面时的细线动效
    // 绘制选项移动的指示线
    Disp_DrawLine(UI_HOR_RES - 7, 0, UI_HOR_RES - 7, (uint16_t)UI_FXI(ui->optionbar.position));
    // 根据计算出的长度，绘制当前选项的高亮框
    if(UI_FXI(ui->optionbar.nowLenght) > 3)   // 边的长度必须大于圆角宽度+1，不然会显示异常
    Disp_DrawRBox(UI_HOR_RES - 10, 0, 6, (uint16_t)UI_FXI(ui->optionbar.nowLenght), 2);

    if(UI_IsAnimationDone(ui->optionbar.nowLenght, ui->optionbar.targetLenght) &&
       (ui->optionbar.position == UI_FX(UI_VER_RES)))
    {
        return true;
    }
    return false;
}

static uint8_t Draw_ImagePage_Optionbar(ui_t *ui, ui_item_t *now_Item, ui_item_t *next_item)
{
    (void)now_Item;
    // 根据下一个项的id和位置长度，计算其理论绘制长度（整数比例，不开 float）
    {
        int total = (next_item->page.location->length > 0) ? next_item->page.location->length : 1;
        ui->optionbar.targetLenght = UI_FX((int32_t)((UI_HOR_RES * next_item->id) / total));
    }
    // 使用定点 PID 计算当前的绘制长度
    ui->optionbar.nowLenght = UI_Animation(ui->optionbar.targetLenght, ui->optionbar.nowLenght, &ui->animation.optionbar_ani);
    ui->optionbar.position = UI_Animation(UI_FX(UI_HOR_RES), ui->optionbar.position, &ui->animation.optionbarPos_ani);//实现进入页面时的细线动效
    // 绘制选项移动的指示线(整体下移 16px，让出顶部状态行；仅适配 128x80)
    Disp_DrawLine(0, UI_IMAGE_BAR_Y + 2, (uint16_t)UI_FXI(ui->optionbar.position), UI_IMAGE_BAR_Y + 2);
    // 根据计算出的长度，绘制当前选项的高亮框
    if(UI_FXI(ui->optionbar.nowLenght) > 3) Disp_DrawRBox(0, UI_IMAGE_BAR_Y, (uint16_t)UI_FXI(ui->optionbar.nowLenght), 5, 2);//边的长度必须大于圆角宽度+1，不然会显示异常

    if(UI_IsAnimationDone(ui->optionbar.nowLenght, ui->optionbar.targetLenght) &&
       (ui->optionbar.position == UI_FX(UI_HOR_RES)))
    {
        return true;
    }
    return false;
}

static void Calculate_Page_xy(ui_t *ui, ui_page_t *Page, ui_item_t *now_Item, ui_item_t *next_item)
{
    switch (Page->type)
    {
    case UI_PAGE_TEXT:
        // 计算页面滚动的距离
        if (Page->pageState == UI_PAGE_MOVING)
        {
            if ((next_item->id - now_Item->id) > 0)ui->headY -= ((next_item->id - now_Item->id) > (Page->length - UI_VER_RES/ui->textSpace)) ? ((Page->length + 1 - UI_VER_RES/ui->textSpace) *ui->textSpace) : ui->textSpace;
            else ui->headY += ((now_Item->id - next_item->id) > (Page->length - UI_VER_RES/ui->textSpace)) ? ((Page->length + 1 - UI_VER_RES/ui->textSpace) *ui->textSpace) : ui->textSpace;
            Page->pageState = UI_PAGE_STATIC; // 更新页面状态为静态显示
        }

        // 页面切换时重置第一行位置
        if(next_item->page.location != now_Item->page.location)
        {
            if(now_Item->itemType == UI_ITEM_PARENTS)
            {
                now_Item->page.location->jumpX = ui->headX;
                now_Item->page.location->jumpY = ui->headY;
                ui->headY = UI_PAGE_INIT_Y;
                ui->headX = UI_PAGE_INIT_X;
            }
            else if (now_Item->itemType == UI_ITEM_RETURN)
            {
                ui->headY = next_item->page.location->jumpY;
                ui->headX = next_item->page.location->jumpX;
            }
        }
        break;
    case UI_PAGE_ICON:
        // 计算页面滚动的距离
        if (Page->pageState == UI_PAGE_MOVING)
        {
            if ((next_item->id - now_Item->id) > 0)ui->headX -= ((next_item->id - now_Item->id) > (Page->length - UI_HOR_RES/ui->imageSpace)) ? ((Page->length + 1 - UI_HOR_RES/ui->imageSpace) *ui->imageSpace) : ui->imageSpace;
            else ui->headX += ((now_Item->id - next_item->id) > (Page->length - UI_HOR_RES/ui->imageSpace)) ? ((Page->length + 1 - UI_HOR_RES/ui->imageSpace) *ui->imageSpace) : ui->imageSpace;
            Page->pageState = UI_PAGE_STATIC; // 更新页面状态为静态显示
        }

        // 页面切换时重置第一行位置
        if(next_item->page.location != now_Item->page.location)
        {
            if(now_Item->itemType == UI_ITEM_PARENTS)
            {
                now_Item->page.location->jumpX = ui->headX;
                now_Item->page.location->jumpY = ui->headY;
                ui->headX = UI_PAGE_INIT_X;
                ui->headY = UI_PAGE_INIT_Y;
            }
            else if (now_Item->itemType == UI_ITEM_RETURN)
            {
                ui->headX = next_item->page.location->jumpX;
                ui->headY = next_item->page.location->jumpY;
            }
        }
        break;
    default:
        break;
    }
}

static void Calculate_Item_xy(ui_t *ui, ui_page_t *Page)
{
    ui_item_t * Item = Page->item.head; // 从页面的头部开始遍历
    for (uint16_t i = 0; i <= Page->length; i++)
    {
        switch (Page->type)
        {
        case UI_PAGE_TEXT:
            Item->y = ui->headY + i *ui->textSpace;
            break;
        case UI_PAGE_ICON:
            Item->x = ui->headX + i *ui->imageSpace;
            break;
        default:
            break;
        }
        Item = Item->nextItem;
    }
}

static void Calculate_Coordinate(ui_t *ui, ui_page_t *Page, ui_item_t *now_Item, ui_item_t *next_item)
{
    // 初始化或计算页面滚动时的起始绘制位置
    if(ui->menuState == UI_PAGE_RUNING)
    {
        Calculate_Page_xy(ui, Page, now_Item, next_item);
        Calculate_Item_xy(ui, Page);
    }
}

static void Calculate_PageState(ui_t *ui, ui_page_t *Page, ui_item_t *now_Item,ui_item_t *next_item)
{
    if(next_item->page.location != now_Item->page.location)
    {
        Page->pageState = UI_PAGE_STATIC;
        return ;
    }
    switch (Page->type)
    {
    case UI_PAGE_TEXT:
        if (next_item->y - UI_FONT_HIGHT > (ui->textSpace*(UI_VER_RES/ui->textSpace) - ui->textSpace) || next_item->y - UI_FONT_HIGHT < 0)  // 防止光标溢出可视范围
        {
            Page->pageState = UI_PAGE_MOVING;
        }
        break;
    case UI_PAGE_ICON:
        if (next_item->x > (ui->imageSpace*(UI_HOR_RES/ui->imageSpace) - ui->imageSpace) || next_item->x < 0)  // 防止光标溢出可视范围
        {
            Page->pageState = UI_PAGE_MOVING;
        }
        break;
    default:
        break;
    }
}

//勘误：修复了非菜单第一项返回时位置错误的问题
static void Calculate_Cursor(ui_t *ui, ui_page_t *Page, ui_item_t *now_Item,ui_item_t *next_item){
    if(next_item->page.location != now_Item->page.location){//换页面了
        if(now_Item->itemType == UI_ITEM_PARENTS) Cursor_AnimationParam_Init(ui, next_item);//进入子菜单，初始化子菜单各项目坐标
        else /*if (now_Item->itemType == UI_ITEM_RETURN)*/ Cursor_AnimationParam_Change(ui);//返回父菜单
        return;
    }
    switch (Page->type){
    case UI_PAGE_TEXT:
        // 计算当前项名称的宽度（nowWide 为 Q10 定点）
        ui->cursor.nowWide  = UI_FX(strlen(now_Item->itemName)*UI_FONT_WIDTH + 4);
        ui->cursor.targrtRow = next_item->y - UI_FONT_HIGHT;
        if (ui->cursor.targrtRow > (ui->textSpace*(UI_VER_RES/ui->textSpace) - ui->textSpace) || ui->cursor.targrtRow < 0)  // 防止光标溢出可视范围
        {
            if(abs(now_Item->id - next_item->id) == 1)ui->cursor.targrtRow = now_Item->y - UI_FONT_HIGHT;
        }
        break;
    case UI_PAGE_ICON:
        // 根据选项的id差值计算目标行位置，并限制在可视范围内
        ui->cursor.targrtColumn = next_item->x;
        if (ui->cursor.targrtColumn > (ui->imageSpace*(UI_HOR_RES/ui->imageSpace) - ui->imageSpace) || ui->cursor.targrtColumn < 0)  // 防止光标溢出可视范围
        {
            if(abs(now_Item->id - next_item->id) == 1)ui->cursor.targrtColumn = now_Item->x;
        }
        break;
    default:
        break;
    }
}
static void Draw_TextPage(ui_t *ui, ui_page_t *Page, ui_item_t *now_Item, ui_item_t *next_item)
{
    (void)now_Item;
    ui_item_t *temp_item = Page->item.head;
    int32_t selected_anim_y = UI_FX(UI_PAGE_INIT_Y);
    int16_t cursor_target_row = 0;
    uint8_t color = 2;

    /* 切页时，重新初始化文本动画位置 */
    if (ui->nowItem->page.location != next_item->page.location)
    {
        for (uint16_t i = 0; i <= Page->length; i++)
        {
            temp_item->animationY = 0;
            temp_item = temp_item->nextItem;
        }
        temp_item = Page->item.head;
    }

    /* 逐项绘制 */
    for (uint16_t i = 0; i <= Page->length; i++)
    {
        temp_item->animationY = UI_Animation(UI_FX(temp_item->y), temp_item->animationY, &ui->animation.textPage_ani);

        /* 记录当前选中项“实际绘制后的”基线Y */
        if (temp_item == next_item)
        {
            selected_anim_y = temp_item->animationY;
        }

        if (temp_item->animationY >= -UI_FX(UI_FONT_HIGHT) && temp_item->animationY <= UI_FX(UI_VER_RES + UI_FONT_HIGHT))
        {
            if (temp_item->itemType == UI_ITEM_DATA && temp_item->element != NULL &&
                temp_item->element->data != NULL)
            {
                const ui_data_t *data = temp_item->element->data;
                char Data[16] = {0};
                uint16_t y = (uint16_t)UI_FXI(temp_item->animationY);
                Disp_SetClipWindow(UI_DATA_X0, UI_DATA_Y0, UI_DATA_X1, UI_DATA_Y1);

                switch (data->dataType)
                {
                case UI_DATA_INT:
                case UI_DATA_FIXED:
                {
                    uint8_t decimals = (data->dataType == UI_DATA_FIXED) ? data->decimals : 0u;
                    unsigned width;
                    const char *text = Data;

                    if(data->ptr == NULL || (data->valid && !*data->valid))
                        text = "--";
                    else
                        UI_FmtScaled(Data, sizeof(Data), *(const int *)data->ptr, decimals);

                    width = (unsigned)strlen(text) * UI_FONT_WIDTH;
                    /* Refuse to show a clipped/misleading numeric value. */
                    if(width == 0u || width > (UI_DATA_X1 - UI_DATA_X0))
                    {
                        text = "OVF";
                        width = 3u * UI_FONT_WIDTH;
                    }
                    Disp_DrawStr((uint16_t)(UI_DATA_X1 - width), y, text);
                    break;
                }

                case UI_DATA_SWITCH:
                    Disp_DrawRFrame(UI_DATA_X0, (uint16_t)(UI_FXI(temp_item->animationY) - UI_FONT_HIGHT + 3), UI_FONT_HIGHT, UI_FONT_HIGHT, 2);
                    if (data->ptr && *(uint8_t *)data->ptr == true)
                        Disp_DrawRBox(UI_DATA_X0 + 2, (uint16_t)(UI_FXI(temp_item->animationY) - UI_FONT_HIGHT + 5), UI_FONT_HIGHT - 4, UI_FONT_HIGHT - 4, 2);
                    break;

                case UI_DATA_STRING:
                    if(data->ptr)
                    {
                        strncpy(Data, (const char *)data->ptr, sizeof(Data) - 1);
                        Data[sizeof(Data) - 1] = '\0';
                        Disp_DrawStr(UI_DATA_X0, y, Data);
                    }
                    break;

                default:
                    break;
                }
            }
            Disp_SetClipWindow(UI_TITLE_X0, UI_TITLE_Y0, UI_TITLE_X1, UI_TITLE_Y1);

#if ( UI_TITLE_ROLL == 1 )
            switch (temp_item->rollState)
            {
            case UI_ITEM_ROLL_STOP:
                if (strlen(temp_item->itemName) * UI_FONT_WIDTH > (UI_TITLE_X1 - UI_TITLE_X0))
                    temp_item->rollState = UI_ITEM_ROLL_FORWARD;
                break;

            case UI_ITEM_ROLL_FORWARD:
                temp_item->x--;
                if ((strlen(temp_item->itemName) * UI_FONT_WIDTH + temp_item->x) < (UI_TITLE_X1 - UI_TITLE_X0))
                    temp_item->rollState = UI_ITEM_ROLL_BACKWARD;
                break;

            case UI_ITEM_ROLL_BACKWARD:
                temp_item->x++;
                if (temp_item->x > 0)
                    temp_item->rollState = UI_ITEM_ROLL_STOP;
                break;

            default:
                break;
            }
#endif
            Disp_DrawStr(temp_item->x, (uint16_t)UI_FXI(temp_item->animationY), temp_item->itemName);
        }

        temp_item = temp_item->nextItem;
    }

    /* 高光框：保留动画，但动画目标改成“选中项实际动画基线Y + 固定补偿” */
    cursor_target_row = (int16_t)(UI_FXI(selected_anim_y) - UI_FONT_HIGHT + UI_TEXT_CURSOR_Y_OFFSET);

    Disp_SetClipWindow(UI_TITLE_X0, UI_TITLE_Y0, UI_TITLE_X1, UI_TITLE_Y1);
    color = 2;
    Disp_SetDrawColor(&color);
    ui->cursor.nowRow = UI_Animation(UI_FX(cursor_target_row), ui->cursor.nowRow, &ui->animation.cursor_ani);
    ui->cursor.nowWide = UI_Animation(UI_FX(ui->cursor.targrtWide), ui->cursor.nowWide, &ui->animation.cursor_ani);
    Disp_DrawRBox(ui->headX, (uint16_t)UI_FXI(ui->cursor.nowRow), (uint16_t)UI_FXI(ui->cursor.nowWide), UI_FONT_HIGHT - 1, 0);
    Disp_SetMaxClipWindow();
}


static void Draw_ImagePage(ui_t *ui, ui_page_t *Page, ui_item_t *nowItem, ui_item_t *next_item)
{
    static int16_t tital_x = 0;
    ui_item_t * temp_item = Page->item.head; //从页面的头部开始遍历
    uint8_t color = 2;
    Draw_ImagePage_Optionbar(ui, nowItem, next_item);

    /* 顶部状态行:与 dashboard 第一行完全一致(坐标不动)。图标与进度条已
     * 整体下移 16px 为它让位——仅适配 128x80 逻辑画布。 */
    Dashboard_DrawStatusLine(ui);
    if(ui->nowItem->page.location != next_item->page.location)
    {
        for (uint16_t i = 0; i <= Page->length; i++)
        {
            temp_item->animationX = 0;
            temp_item = temp_item->nextItem;
        }
    }
    for (uint16_t i = 0; i <= Page->length; i++)
    {
        temp_item->animationX = UI_Animation(UI_FX(temp_item->x), temp_item->animationX, &ui->animation.imagePage_ani);
        if(temp_item->animationX >= -UI_FX(UI_IMG_WIDTH) && temp_item->animationX <= UI_FX(UI_HOR_RES + UI_IMG_WIDTH)) //超出屏幕范围则不绘制
        {
            //绘制图标
            color = ui->bgColor^0x01;
            Disp_SetDrawColor(&color);
            Disp_DrawXBMP((uint16_t)UI_FXI(temp_item->animationX), UI_IMAGE_Y, UI_IMG_WIDTH, UI_IMG_HEIGHT, temp_item->logo);
            //加框并反色图片
            color = 2;
            Disp_SetDrawColor(&color);
            Disp_DrawRBox((uint16_t)UI_FXI(temp_item->animationX), UI_IMAGE_Y, UI_IMG_WIDTH, UI_IMG_HEIGHT, 4);
        }

        temp_item = temp_item->nextItem;
    }

    #if ( UI_TITLE_ROLL == 1 )
    // 根据temp_item的滚动状态选择不同的操作
    switch (next_item->rollState)
    {
    case UI_ITEM_ROLL_STOP: // 如果项目处于静止状态
        // 检查项目名称是否需要开始滚动
        if(strlen(next_item->itemName) * UI_FONT_WIDTH > UI_HOR_RES)
            next_item->rollState = UI_ITEM_ROLL_FORWARD; // 如果名称宽度大于项目区域宽度，改变状态为向前滚动
        else tital_x = 0;
        break;
    case UI_ITEM_ROLL_FORWARD: // 如果项目处于向前滚动状态
        tital_x --; // 向左移动项目
        // 检查是否需要改变滚动方向
        if((strlen(next_item->itemName) * UI_FONT_WIDTH + tital_x) < UI_HOR_RES)
            next_item->rollState = UI_ITEM_ROLL_BACKWARD; // 如果项目移出左边界，改变状态为向后滚动
        break;
    case UI_ITEM_ROLL_BACKWARD: // 如果项目处于向后滚动状态
        tital_x ++; // 向右移动项目
        // 检查项目是否完全回到视图内
        if(tital_x > 0)
            next_item->rollState = UI_ITEM_ROLL_STOP; // 如果项目完全回到视图内，改变状态为向前滚动
        break;
    default: // 默认情况下不做任何操作
        break;
    }
    #endif

    color = ui->bgColor^0x01;;
    Disp_SetDrawColor(&color); // 设置特定的颜色，通常用于高亮显示
    Disp_DrawStr(tital_x, UI_VER_RES - 2, next_item->itemName);
    // 根据目标位置和当前位置，以及PID算法计算并更新当前选项的位置和宽度
    ui->cursor.nowColumn = UI_Animation(UI_FX(ui->cursor.targrtColumn), ui->cursor.nowColumn, &ui->animation.cursor_ani);
    // 绘制选中项的下划线
    Disp_DrawBox((uint16_t)UI_FXI(ui->cursor.nowColumn), UI_VER_RES - 17, UI_IMG_WIDTH, 2);
}

/**
 * 绘制菜单界面
 * 该函数负责在 OLED 屏幕上绘制菜单界面，包括计算各项的位置、绘制背景、选项及其高亮显示。
 * 根据不同的菜单状态（如页面移动、选项切换），动态调整选项的显示位置，实现平滑的动画效果。
 * @param pos 当前页面位置
 * @param Page 当前页面结构体
 * @param LineSpacing 行间距
 * @param nowItem 当前选中项
 * @param next_item 下一个将要选中的项
 */
static void Draw_UI(ui_t *ui, ui_page_t *Page, ui_item_t *nowItem, ui_item_t *next_item)
{
    uint8_t color = ui->bgColor;

    Disp_SetFont(UI_FONT);

    // 计算页面状态
    if(ui->menuState == UI_PAGE_RUNING) Calculate_PageState(ui, Page, nowItem, next_item);

    // 计算下一个将要选中项的名称宽度
    if(strlen(next_item->itemName)* UI_FONT_WIDTH > (UI_TITLE_X1 - UI_TITLE_X0))
    ui->cursor.targrtWide = UI_TITLE_X1 - UI_TITLE_X0;
    else ui->cursor.targrtWide = strlen(next_item->itemName)* UI_FONT_WIDTH;
    // 开始绘制菜单界面
    //清屏
    Disp_SetDrawColor(&color); // 设置背景颜色
    Disp_DrawBox(0, 0, UI_HOR_RES, UI_VER_RES); // 绘制屏幕背景框
    color = color^0x01;
    Disp_SetDrawColor(&color); // 设置绘制颜色为高亮或低亮

    Calculate_Coordinate(ui, Page, nowItem, next_item); // 绘制前计算最终坐标
    // 初始化或更新动画参数及页面状态
    if(ui->menuState == UI_PAGE_RUNING)
    {
        Calculate_Cursor(ui, Page, nowItem, next_item);
    }

    if(Page->type == UI_PAGE_TEXT)
    {
        Draw_TextPage_Optionbar(ui, nowItem, next_item); // 绘制选项及其位置
        Draw_TextPage(ui, Page, nowItem, next_item);
    }
    else if(Page->type == UI_PAGE_ICON)
    {
        Draw_ImagePage_Optionbar(ui, nowItem, next_item); // 绘制选项及其位置
        Draw_ImagePage(ui, Page, nowItem, next_item);
    }

    Disp_SendBuffer(); // 将缓冲区的内容发送到OLED屏幕显示

    // 更新菜单状态为绘制中
    Change_UIState(ui, UI_PAGE_DRAWING);
}

void Create_element(ui_item_t *item, ui_element_t *element)
{
    if(item == NULL || element == NULL) return;

    item->element = element;
}

/**
 * @brief 根据菜单方向运行处理菜单逻辑
 *
 * @param Dir 菜单操作方向，包括上、下、进入等操作
 */
static void Process_UI_Run(ui_t *ui, UI_ACTION Action)
{
    uint8_t disapper = 0;
    uint8_t i;
    switch (ui->menuState) // 根据当前菜单状态进行不同的操作
    {
        case UI_PAGE_RUNING:
            switch (Action) // 根据操作方向更新菜单项
            {
                case UI_ACTION_UP:
                    // 向上移动菜单项，确保当前项和上一项非空
                    if (ui->nowItem != NULL && ui->nowItem->lastItem != NULL)
                    {
                        Draw_UI(ui, ui->nowItem->page.location, ui->nowItem, ui->nowItem->lastItem);
                        ui->oldItem = ui->nowItem;
                        ui->nowItem = ui->nowItem->lastItem;
                    }
                    break;
                case UI_ACTION_DOWN:
                    // 向下移动菜单项，确保当前项和下一项非空
                    if (ui->nowItem != NULL && ui->nowItem->nextItem != NULL)
                    {
                        Draw_UI(ui, ui->nowItem->page.location, ui->nowItem, ui->nowItem->nextItem);
                        ui->oldItem = ui->nowItem;
                        ui->nowItem = ui->nowItem->nextItem;
                    }
                    break;
                case UI_ACTION_ENTER:
                    /* Read-only data lives in the list; ENTER is a no-op. */
                    if(ui->nowItem && ui->nowItem->itemType == UI_ITEM_DATA &&
                       ui->nowItem->element && ui->nowItem->element->data &&
                       ui->nowItem->element->data->actionType == UI_DATA_ACTION_RO)
                    {
                        Draw_UI(ui, ui->nowItem->page.location, ui->nowItem, ui->nowItem);
                        break;
                    }
                    if(ui->nowItem != NULL && ui->nowItem->itemType == UI_ITEM_PARENTS)//当前项非空且为父项时，进入下一级菜单
                    {
                        // 避免重复状态改变
                        if(ui->action != UI_ACTION_ENTER) Change_UIState(ui, UI_PAGE_RUNING);

                        for (i = 0; i < 8; i++) disapper = UI_Disapper(ui ,disapper);// 执行UI淡出操作

                        // 如果存在跳转页面且不为空，则绘制跳转页面并更新当前项
                        if(ui->nowItem->page.jumpPage != NULL && ui->nowItem->page.jumpPage->item.head != NULL)
                        {
                            Optionbar_Init(ui);
                            Draw_UI(ui, ui->nowItem->page.jumpPage, ui->nowItem, ui->nowItem->page.jumpPage->item.head);
                            ui->nowItem->page.location->item.lastJumpItem = ui->nowItem;
                            ui->oldItem = ui->nowItem;
                            ui->nowItem = ui->nowItem->page.jumpPage->item.head;
                        }
                    }
                    else if(ui->nowItem != NULL && ui->nowItem->itemType == UI_ITEM_RETURN)//返回上一级菜单
                    {
                        // 避免重复状态改变
                        if(ui->action != UI_ACTION_ENTER) Change_UIState(ui, UI_PAGE_RUNING);

                        for (i = 0; i < 8; i++) disapper = UI_Disapper(ui ,disapper);// 执行UI淡出操作

                        // 如果存在跳转页面，则绘制跳转页面并更新当前项
                        if(ui->nowItem->page.jumpPage != NULL)
                        {
                            Draw_UI(ui, ui->nowItem->page.jumpPage->item.lastJumpItem->page.location, ui->nowItem, ui->nowItem->page.jumpPage->item.lastJumpItem);
                            ui->oldItem = ui->nowItem;
                            ui->nowItem = ui->nowItem->page.jumpPage->item.lastJumpItem;
                        }
                    }
                    else // 如果当前项为空或非父项，则进入应用绘制状态
                    {
                        UI_Disapper(ui, 1);
                        Change_UIState(ui, UI_ITEM_DRAWING);
                    }
                    break;
                case UI_ACTION_BACK:
                    if(ui->nowItem != NULL){
                        // 避免重复状态改变
                        if(ui->action != UI_ACTION_BACK) Change_UIState(ui, UI_PAGE_RUNING);

                        for (i = 0; i < 8; i++) disapper = UI_Disapper(ui ,disapper);// 执行UI淡出操作

                        // 如果不是最低级菜单
                        if(ui->nowItem->page.location->parentPage != NULL){
                            Draw_UI(ui, ui->nowItem->page.location->parentPage->item.lastJumpItem->page.location, ui->nowItem->page.location->item.head, ui->nowItem->page.location->parentPage->item.lastJumpItem);
                            ui->oldItem = ui->nowItem->page.location->item.head;
                            ui->nowItem = ui->nowItem->page.location->parentPage->item.lastJumpItem;
                        }
                        else{
                            /* 根菜单按 BACK(K2 长按) → 立即休眠/关屏:独立的
                             * "-Sleep" 图标已删,这里是菜单里关屏的唯一入口。
                             * 电源框架在下一次 poll(≤50 ms)进入 UI_OFF,随后
                             * thread_ui 停画(coroOS 下无需挂起任务)。 */
                            pm_api_force_sleep();
                        }
                    }
                    else{ // 否则进入应用绘制状态
                        UI_Disapper(ui, 1);
                        Change_UIState(ui, UI_ITEM_DRAWING);
                    }
                    break;
                default:
                    Draw_UI(ui, ui->nowItem->page.location, ui->nowItem, ui->nowItem);
                    break;
            }
            break;
        case UI_ITEM_EXIT:
            // 退出应用时的初始化操作，并返回菜单运行状态
            Dialog_AnimationParam_Init(ui);
            Scrollbar_Init(ui);
            Change_UIState(ui, UI_PAGE_RUNING);
            for (size_t i = 0; i < 8; i++)
            {
                disapper = UI_Disapper(ui, disapper); // 执行UI淡出操作
            }
            if(ui->nowItem != NULL) // 确保now_item非空
            {
                Draw_UI(ui, ui->nowItem->page.location, ui->nowItem, ui->nowItem);
            }
            break;
        case UI_PAGE_DRAWING:
            // 如果当前正在绘制菜单，则根据方向进行状态更新和菜单重绘
            if(ui->nowItem != NULL && ui->oldItem != NULL)
            {
                Draw_UI(ui, ui->nowItem->page.location, ui->oldItem, ui->nowItem);
                if (Action != UI_ACTION_NONE)
                {
                    Change_UIState(ui, UI_PAGE_RUNING);
                    Process_UI_Run(ui, Action);
                }
            }
            break;
        default:
            break;
    }
}

/**
 * @brief 处理应用程序的运行状态。
 * 对给定的项目(item)根据其类型(UI_ITEM_TYPE)和当前的菜单状态(Direction)执行相应的操作。
 * @param item 指向要处理的项目的指针。
 * @param Direction 当前菜单的方向或状态。
 */
static void Process_App_Run(ui_t *ui, ui_item_t *item, UI_ACTION Action)
{
    if (item == NULL) return; // 检查空指针，避免未定义行为

    ui->action = Action; // 设置项目的状态为当前菜单状态

    switch (item->itemType) // 根据项目类型执行不同的操作
    {
    case UI_ITEM_DATA:
        if(item->element != NULL && item->element->data != NULL)
        {
            if(item->element->data->actionType == UI_DATA_ACTION_RO)
            {
                Change_UIState(ui, UI_ITEM_EXIT); // 如果项目状态为进入菜单，则改变菜单状态为函数退出
                break;
            }
            else
            {
                switch (item->element->data->dataType)
                {
                case UI_DATA_INT:
                case UI_DATA_FIXED:
                    if (item->itemFunction == NULL) ParameterSetting_Widget(ui);
                    else (item->itemFunction)(ui); //执行项目的函数
                    if(ui->action == UI_ACTION_ENTER || ui->action == UI_ACTION_BACK)//退出项目
                    {
                        if(ui->nowItem->element->data->functionType == UI_DATA_FUNCTION_EXIT_EXECUTE)
                        {
                            if(ui->nowItem->element->data->function != NULL) ui->nowItem->element->data->function(ui);
                            #if ( UI_USE_FREERTOS == 1 )
                            if(ui->nowItem->element->data->dataRootTask != NULL) vTaskResume(*ui->nowItem->element->data->dataRootTask);
                            #endif
                        }
                        Change_UIState(ui, UI_ITEM_EXIT); // 如果项目状态为进入菜单，则改变菜单状态为函数退出
                    }
                    break;
                case UI_DATA_SWITCH:
                    Switch_Widget(ui);
                    Change_UIState(ui, UI_ITEM_EXIT); // 改变菜单状态为函数退出
                    break;
                default:
                    UI_LOG("Unknow data type\n");
                    break;
                }
            }
            break;
        }
        Change_UIState(ui, UI_ITEM_EXIT);
        break;
    case UI_ITEM_WORD://显示字符串
        if (item->itemFunction == NULL) Text_Widget(ui);
        else (item->itemFunction)(ui);
        /* BACK(K2 长按)强制离开函数页:dashboard/小恐龙/烧屏页都只是被菜单
         * 调用的绘制函数,页面本身若吞掉了该 action(如烧屏说明页),这里重新
         * 置回,保证"长按=回到上一级"。 */
        if(Action == UI_ACTION_BACK) ui->action = UI_ACTION_BACK;
        if(ui->action != UI_ACTION_NONE) Change_UIState(ui, UI_ITEM_EXIT); // 如果项目状态为进入菜单，则改变菜单状态为函数退出
        break;
    case UI_ITEM_ONCE_FUNCTION:
        if (item->itemFunction != NULL && item->functionState == false)
        {
            (item->itemFunction)(ui); // 如果项目有函数，则执行该函数
            item->functionState = true;
        }
        item->functionState = false;
        #if (UI_ONCEFUNCTION_TIP == 0)
        Change_UIState(ui, UI_ITEM_EXIT); // 改变菜单状态为函数退出
        #endif
        #if (UI_ONCEFUNCTION_TIP == 1)
        // 执行完成后，弹窗提醒
        if (Notifications(ui, 4, 12, UI_HOR_RES - 8, UI_VER_RES - 32))
        {
            Disp_SendBuffer();
        }
        if(ui->action != UI_ACTION_NONE)
        {
            item->functionState = false;
            Change_UIState(ui, UI_ITEM_EXIT); // 改变菜单状态为函数退出
        }
        #endif
        break;
    default:
        break; // 对未知类型不执行任何操作
    }
}

/**
 * 菜单任务处理函数
 * 该函数负责根据当前按钮扫描结果更新菜单状态，并进行相应的绘制或处理操作。
 * 无参数
 * 无返回值
 */
void ui_loop(ui_t *ui)
{
    /* These two states ignore actions: leave queued input for the next frame. */
    UI_ACTION Action = (ui->menuState == UI_PAGE_INIT || ui->menuState == UI_ITEM_EXIT)
                         ? UI_ACTION_NONE : indevScan();
    if (ui->menuState == UI_PAGE_INIT)
    {
        ui->menuState = UI_PAGE_RUNING;
        // 初始化状态下，非空检查now_item并绘制当前菜单项
        if (ui->nowItem != NULL)
        {
            Draw_UI(ui, ui->nowItem->page.location, ui->nowItem, ui->nowItem);
        }
        else
        {
            UI_LOG("ui->nowItem is NULL\n"); //打印错误信息，now_item为空
        }
    }
    else
    {
        switch (ui->menuState)
        {
            case UI_ITEM_RUNING:
            case UI_ITEM_DRAWING:
                // 在这两个状态下，处理应用程序运行逻辑
                Process_App_Run(ui, ui->nowItem, Action);
                if (ui->menuState != UI_ITEM_DRAWING)
                    Process_UI_Run(ui, Action);
                break;

            case UI_PAGE_RUNING:
            case UI_ITEM_EXIT:
            case UI_PAGE_DRAWING:
                // 处理菜单运行、应用退出和菜单绘制状态
                Process_UI_Run(ui, Action);
                break;

            default:
                break;
        }
    }
}

/* TKWTL的注释
 * 该函数为动画的动效参数赋初值
 * kp越大，动画变动越快，但会冲过头
 * ki为0时会产生静差
 */
static void AnimationParam_Init(ui_animation_t *Ani)
{
    /* PID 参数均为 Q10 定点：0.25 -> 256，0.03 -> 31，0.01 -> 10 … */
    Ani->optionbar_ani.kp = 256;
    Ani->optionbar_ani.ki = 0;
    Ani->optionbar_ani.kd = 31;

    //光标动效参数(2026-09-14 由 +100% 增速回撤 20%: 0.5/0.061/0.039 → 0.4/0.049/0.031)
    Ani->cursor_ani.kp = 410;
    Ani->cursor_ani.ki = 50;
    Ani->cursor_ani.kd = 32;

    //图片页动效参数(同上回撤 20%)
    Ani->imagePage_ani.kp = 410;
    Ani->imagePage_ani.ki = 0;//必须是0，否则图像到处乱飞
    Ani->imagePage_ani.kd = 16;

    //文本页动效参数(同上回撤 20%)
    Ani->textPage_ani.kp = 410;
    Ani->textPage_ani.ki = 0;//必须是0，否则选项到处乱飞
    Ani->textPage_ani.kd = 82;

    //滚动条本体动效参数
    Ani->scrollbar_ani.kp = 256;
    Ani->scrollbar_ani.ki = 0;//必须是0，否则调参时指示条溢出
    Ani->scrollbar_ani.kd = 31;

    //滚动条细线动效参数
    Ani->optionbarPos_ani.kp = 256;
    Ani->optionbarPos_ani.ki = 31;
    Ani->optionbarPos_ani.kd = 10;
}

void Create_UI(ui_t *ui, ui_item_t *item)
{
    Create_MenuTree(ui);
    Create_Parameter(ui);
    Create_Text(ui);
    ui->textSpace = UI_TEXT_SPACE;
    ui->imageSpace = UI_IMAGE_SPACE;
    ui->headX = UI_PAGE_INIT_X;
    ui->headY = UI_PAGE_INIT_Y;
    ui->nowItem = item;
    ui->oldItem = item;
    ui->menuState = UI_PAGE_INIT;
    ui->action = UI_ACTION_NONE;
    ui->bgColor = 0;
    ui->rotation = 0;
    AnimationParam_Init(&ui->animation);
}
