/* Two-key input adapter for MiaoUI: K1=DOWN, K2=ENTER/BACK.
 *
 * K1 keeps the early-MiaoUI scheme: DAS auto-repeat, DOWN everywhere.
 *
 * K2 is *release* triggered, with long press = BACK:
 *   - press and let go before the long-press threshold -> UI_ACTION_ENTER;
 *   - hold past the threshold -> one UI_ACTION_BACK (fired while the key is
 *     still held) and the release of that press is swallowed, so a long press
 *     can never confirm something as well;
 *   - only a press this adapter has seen (KeyEdge_Rising) can produce an
 *     action: the K2 press that wakes a sleeping panel is handled by the power
 *     manager (indevPoll() is not called while the UI is off), so its
 *     release must not fire ENTER on the page that just lit up; presses that
 *     start while the UI is blocked (PD quiet window) are dropped too. */
#include "indev/indevDriver.h"
#include "buttons.h"
#include "core/ui.h"

/* Poll once per actual GPIO scan; draw may run more or less often.
 * Queue is bounded; keep ENTER/BACK ahead of DOWN repeats on overflow. */
#define INDEV_QUEUE_SIZE 4u
static UI_ACTION s_queue[INDEV_QUEUE_SIZE];
static uint8_t s_count;
static uint8_t s_enter_down;
static uint8_t s_enter_back;
static uint8_t s_down_armed;

static void enqueue(UI_ACTION action)
{
    uint8_t n;
    if(s_count == INDEV_QUEUE_SIZE)
    {
        if(action == UI_ACTION_DOWN)
            return;
        for(n = 0u; n < s_count; ++n)
            if(s_queue[n] == UI_ACTION_DOWN)
                break;
        if(n == s_count)
            return;
        for(; n + 1u < s_count; ++n)
            s_queue[n] = s_queue[n + 1u];
        --s_count;
    }
    s_queue[s_count++] = action;
}

void indevReset(void)
{
    s_count = 0u;
    s_enter_down = 0u;
    s_enter_back = 0u;
    s_down_armed = 0u;
}

void indevPoll(void)
{
    KeyEdge_t edge = Key_EdgeDetect(KeyIndex_Enter);

    if(edge == KeyEdge_Rising)
    {
        s_enter_down = 1u;
        s_enter_back = 0u;
    }
    else if(edge == KeyEdge_Holding && s_enter_down && !s_enter_back)
    {
        s_enter_back = 1u;
        enqueue(UI_ACTION_BACK);
    }
    else if(edge == KeyEdge_Falling)
    {
        if(s_enter_down && !s_enter_back)
            enqueue(UI_ACTION_ENTER);
        s_enter_down = 0u;
        s_enter_back = 0u;
    }

    edge = Key_EdgeDetect(KeyIndex_Down);
    if(edge == KeyEdge_Rising)
        s_down_armed = 1u;
    else if(edge == KeyEdge_Falling)
        s_down_armed = 0u;
    if(s_down_armed && KEY_GetDASClick(KeyIndex_Down))
        enqueue(UI_ACTION_DOWN);
}

UI_ACTION indevScan(void)
{
    UI_ACTION action;
    uint8_t n;
    if(!s_count)
        return UI_ACTION_NONE;
    action = s_queue[0];
    --s_count;
    for(n = 0u; n < s_count; ++n)
        s_queue[n] = s_queue[n + 1u];
    return action;
}
