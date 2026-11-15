/*
 * Lightweight non-blocking button debounce/edge/DAS helper derived from the
 * button module in the attached Firmware_0.zip application.
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    KeyState_None = 0,
    KeyState_ShortPress,
    KeyState_LongPress,
    KeyState_Release
} KeyState_t;

typedef enum
{
    KeyEdge_Null = 0,
    KeyEdge_Rising,
    KeyEdge_Falling,
    KeyEdge_Holding,
    KeyEdge_Error
} KeyEdge_t;

typedef uint8_t (*fpGetIOStateFunc)(void);

typedef struct
{
    uint8_t LongPressTime;
    uint8_t DASInterval;
    uint8_t MultiClickTime;
    uint8_t ZeroIsPress;
    fpGetIOStateFunc GetIOFunc;
} KeyInfo_t;

typedef struct
{
    uint8_t CurrIOState;
    uint8_t LastIOState;
    uint8_t DebounceCounter; /* raw level stability only */
    uint8_t PressCounter;    /* independent long-press/DAS timer */
    uint8_t MultiClick;
    uint8_t ClickCounter;
    KeyState_t State;
    KeyState_t LastState;
    KeyEdge_t Edge;
} Key_t;

typedef enum
{
    KeyIndex_Down = 0,   /* K1 / PA5, active low */
    KeyIndex_Enter,      /* K2 / PA6, active low */
    KeyIndex_Max
} KeyIndex_t;

void Key_Init(void);
#define KEY_SERVICE_PERIOD_MS 8u
void Key_DebounceService_8ms(void);
void Key_Scand(void);
KeyEdge_t Key_EdgeDetect(KeyIndex_t key);
KeyState_t KEY_GetState(KeyIndex_t key);
uint8_t KEY_GetDASClick(KeyIndex_t key);
uint8_t KEY_GetClickTimes(KeyIndex_t key, uint8_t times);

#ifdef __cplusplus
}
#endif

#endif /* BUTTONS_H */
