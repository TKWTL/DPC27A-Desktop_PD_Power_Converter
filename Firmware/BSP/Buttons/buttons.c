#include "buttons.h"
#include "board.h"

#ifndef DEBOUNCE_TIME_8MS
#define DEBOUNCE_TIME_8MS 2U       /* 16 ms */
#endif

#define DEFAULT_LONGPRESS_TIME_8MS 150U

static uint8_t KEY_Down_GetIO(void)
{
    /* Board_Init() enables the CH32X035 internal pull-up.  Board_Key1Pressed()
     * normalizes the active-low electrical input to 1 = pressed. */
    return Board_Key1Pressed();
}

static uint8_t KEY_Enter_GetIO(void)
{
    return Board_Key2Pressed();
}

static const KeyInfo_t s_key_info[KeyIndex_Max] =
{
    /* LongPress, DAS interval, multi-click timeout, ZeroIsPress, getter.
     * K1 (DOWN) auto-repeats faster than K2: it drives menu scrolling and
     * the numeric dialogs, where 12 ticks (96 ms) per step keeps a held key
     * controllable; ENTER stays at 16 ticks (128 ms). */
    {100U, 12U, 50U, 0U, KEY_Down_GetIO},
    {100U, 16U, 50U, 0U, KEY_Enter_GetIO}
};

static Key_t s_keys[KeyIndex_Max];

void Key_Scand(void)
{
    uint8_t i;

    for(i = 0U; i < (uint8_t)KeyIndex_Max; ++i)
    {
        uint8_t io_state = s_key_info[i].GetIOFunc();
        io_state = s_key_info[i].ZeroIsPress ? (uint8_t)!io_state : io_state;

        if(s_keys[i].CurrIOState != io_state)
        {
            s_keys[i].CurrIOState = io_state;
            s_keys[i].DebounceCounter = DEBOUNCE_TIME_8MS;
        }

        if(s_keys[i].LastIOState != s_keys[i].CurrIOState)
        {
            if(s_keys[i].DebounceCounter == 0U)
            {
                if(s_keys[i].CurrIOState && !s_keys[i].LastIOState)
                {
                    s_keys[i].State = KeyState_ShortPress;
                    s_keys[i].PressCounter = s_key_info[i].LongPressTime ?
                                                s_key_info[i].LongPressTime :
                                                DEFAULT_LONGPRESS_TIME_8MS;
                }
                else if(!s_keys[i].CurrIOState && s_keys[i].LastIOState)
                {
                    s_keys[i].State = KeyState_Release;
                    s_keys[i].PressCounter = 0u;
                }
                else
                {
                    s_keys[i].State = KeyState_None;
                }
                s_keys[i].LastIOState = s_keys[i].CurrIOState;
            }
        }

        /* A short release bounce must not overwrite the hold timer. Do not
         * generate BACK during a candidate release before it is debounced. */
        if(s_keys[i].State == KeyState_ShortPress && s_keys[i].CurrIOState &&
           s_keys[i].DebounceCounter == 0U && s_keys[i].PressCounter == 0U)
        {
            s_keys[i].State = KeyState_LongPress;
            s_keys[i].PressCounter = s_key_info[i].DASInterval;
        }

        if(s_keys[i].State == s_keys[i].LastState)
            s_keys[i].Edge = KeyEdge_Null;
        else if(s_keys[i].State == KeyState_ShortPress &&
                (s_keys[i].LastState == KeyState_None || s_keys[i].LastState == KeyState_Release))
            s_keys[i].Edge = KeyEdge_Rising;
        else if(s_keys[i].State == KeyState_Release &&
                (s_keys[i].LastState == KeyState_ShortPress || s_keys[i].LastState == KeyState_LongPress))
            s_keys[i].Edge = KeyEdge_Falling;
        else if(s_keys[i].State == KeyState_LongPress && s_keys[i].LastState == KeyState_ShortPress)
            s_keys[i].Edge = KeyEdge_Holding;
        else
            s_keys[i].Edge = KeyEdge_Error;

        s_keys[i].LastState = s_keys[i].State;

        if(s_keys[i].Edge == KeyEdge_Rising)
            s_keys[i].ClickCounter = s_key_info[i].MultiClickTime;

        if(s_keys[i].ClickCounter && s_keys[i].Edge == KeyEdge_Falling)
        {
            ++s_keys[i].MultiClick;
            s_keys[i].ClickCounter = s_key_info[i].MultiClickTime;
        }
    }
}

void Key_DebounceService_8ms(void)
{
    uint8_t i;
    for(i = 0U; i < (uint8_t)KeyIndex_Max; ++i)
    {
        if(s_keys[i].DebounceCounter)
            --s_keys[i].DebounceCounter;
        if(s_keys[i].PressCounter)
            --s_keys[i].PressCounter;
        if(s_keys[i].ClickCounter)
            --s_keys[i].ClickCounter;
        else
            s_keys[i].MultiClick = 0U;
    }
}

void Key_Init(void)
{
    uint8_t i;
    for(i = 0U; i < (uint8_t)KeyIndex_Max; ++i)
    {
        s_keys[i].CurrIOState = 0U;
        s_keys[i].LastIOState = 0U;
        s_keys[i].DebounceCounter = 0U;
        s_keys[i].PressCounter = 0U;
        s_keys[i].MultiClick = 0U;
        s_keys[i].ClickCounter = 0U;
        s_keys[i].State = KeyState_None;
        s_keys[i].LastState = KeyState_None;
        s_keys[i].Edge = KeyEdge_Null;
    }
}

KeyEdge_t Key_EdgeDetect(KeyIndex_t key)
{
    return (key < KeyIndex_Max) ? s_keys[key].Edge : KeyEdge_Error;
}

KeyState_t KEY_GetState(KeyIndex_t key)
{
    return (key < KeyIndex_Max) ? s_keys[key].State : KeyState_None;
}

uint8_t KEY_GetDASClick(KeyIndex_t key)
{
    KeyEdge_t edge;

    if(key >= KeyIndex_Max)
        return 0U;

    edge = Key_EdgeDetect(key);
    if(edge == KeyEdge_Rising)
        return 1U;
    if(edge == KeyEdge_Holding && s_key_info[key].DASInterval)
        return 1U;
    if(s_key_info[key].DASInterval && KEY_GetState(key) == KeyState_LongPress &&
       s_keys[key].CurrIOState && s_keys[key].DebounceCounter == 0U &&
       s_keys[key].PressCounter == 0U)
    {
        s_keys[key].PressCounter = s_key_info[key].DASInterval;
        return 1U;
    }
    return 0U;
}

uint8_t KEY_GetClickTimes(KeyIndex_t key, uint8_t times)
{
    if(key < KeyIndex_Max && s_keys[key].MultiClick == times)
    {
        s_keys[key].MultiClick = 0U;
        return 1U;
    }
    return 0U;
}
