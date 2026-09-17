/*
 * Trimmed SH1107 TK078F288 0.78-inch 80x128-native display driver.
 * Derived from upstream u8g2 csrc/u8x8_d_sh1107.c (BSD-2-Clause).
 *
 * Board note: this product supplies OLED VPP externally.  The init sequence
 * deliberately sends 0xAD,0x8A, which keeps the SH1107 internal DC-DC/charge
 * pump disabled.  Do not change 0x8A to the enable form.
 *
 * Board note: the panel is reset by an external RC network only.  The ~200 ms
 * power-up settle wait before the first command is deliberately NOT part of
 * this driver: the UI thread performs it cooperatively (THRD_DELAY) right
 * before calling diapInit() - see thread_ui() in app_tasks.c.  A blocking
 * wait in this init path once starved PD_Task() for 200 ms and caused a 1.2 s
 * reboot loop when the board was powered over USB-PD.
 */
#include "u8x8.h"

static const uint8_t s_power_save_off[] =
{
    U8X8_START_TRANSFER(),
    U8X8_C(0xAF),
    U8X8_END_TRANSFER(),
    U8X8_END()
};

static const uint8_t s_power_save_on[] =
{
    U8X8_START_TRANSFER(),
    U8X8_C(0xAE),
    U8X8_END_TRANSFER(),
    U8X8_END()
};

static const uint8_t s_init_seq[] =
{
    U8X8_START_TRANSFER(),
    U8X8_C(0xAE),                 /* display off */
    U8X8_C(0x00),                 /* lower column */
    U8X8_C(0x10),                 /* higher column */
    U8X8_C(0x20),                 /* page addressing */
    U8X8_CA(0x81, 0x6F),          /* contrast */
    U8X8_C(0xA0),                 /* segment remap */
    U8X8_C(0xC0),                 /* COM scan direction */
    U8X8_C(0xA4),                 /* RAM content -> display */
    U8X8_C(0xA6),                 /* normal display */
    U8X8_CA(0xD5, 0x91),          /* oscillator / divide */
    U8X8_CA(0xD9, 0x22),          /* pre-charge */
    U8X8_CA(0xDB, 0x3F),          /* VCOMH */
    U8X8_CA(0xA8, 0x4F),          /* 1/80 multiplex */
    U8X8_CA(0xD3, 0x68),          /* panel offset */
    U8X8_CA(0xDC, 0x00),          /* display start line */
    U8X8_CA(0xAD, 0x8A),          /* DC-DC DISABLE: VPP is supplied externally */
    U8X8_C(0xAF),                 /* display on */
    U8X8_END_TRANSFER(),
    U8X8_END()
};

static const u8x8_display_info_t s_display_info =
{
    0,          /* chip_enable_level */
    1,          /* chip_disable_level */
    20,         /* post_chip_enable_wait_ns */
    10,         /* pre_chip_disable_wait_ns */
    0,          /* reset_pulse_width_ms: reset is external RC, not MCU-driven */
    0,          /* post_reset_wait_ms: panel has already settled before UI init */
    100,        /* sda_setup_time_ns */
    100,        /* sck_pulse_width_ns */
    3000000UL,  /* board first-light SPI clock */
    0,          /* SPI mode 0 */
    4,          /* unused I2C timing */
    40,
    150,
    10,         /* native tile width: 80 px */
    16,         /* native tile height: 128 px */
    0,
    0,
    80,         /* native pixel width */
    128         /* native pixel height */
};

static uint8_t SH1107_Generic(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    uint8_t x;
    uint8_t c;
    uint8_t *ptr;

    switch(msg)
    {
        case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
            u8x8_cad_SendSequence(u8x8, arg_int ? s_power_save_on : s_power_save_off);
            return 1;

#ifdef U8X8_WITH_SET_CONTRAST
        case U8X8_MSG_DISPLAY_SET_CONTRAST:
            u8x8_cad_StartTransfer(u8x8);
            u8x8_cad_SendCmd(u8x8, 0x81);
            u8x8_cad_SendArg(u8x8, arg_int);
            u8x8_cad_EndTransfer(u8x8);
            return 1;
#endif

        case U8X8_MSG_DISPLAY_DRAW_TILE:
            u8x8_cad_StartTransfer(u8x8);
            x = (uint8_t)(((u8x8_tile_t *)arg_ptr)->x_pos * 8U + u8x8->x_offset);
            u8x8_cad_SendCmd(u8x8, (uint8_t)(0x10U | (x >> 4)));
            u8x8_cad_SendCmd(u8x8, (uint8_t)(x & 0x0FU));
            u8x8_cad_SendCmd(u8x8, (uint8_t)(0xB0U | ((u8x8_tile_t *)arg_ptr)->y_pos));
            do
            {
                c = ((u8x8_tile_t *)arg_ptr)->cnt;
                ptr = ((u8x8_tile_t *)arg_ptr)->tile_ptr;
                u8x8_cad_SendData(u8x8, (uint8_t)(c * 8U), ptr);
                --arg_int;
            } while(arg_int > 0U);
            u8x8_cad_EndTransfer(u8x8);
            return 1;

        default:
            return 0;
    }
}

uint8_t u8x8_d_sh1107_tk078f288_80x128(u8x8_t *u8x8,
                                        uint8_t msg,
                                        uint8_t arg_int,
                                        void *arg_ptr)
{
    if(SH1107_Generic(u8x8, msg, arg_int, arg_ptr))
        return 1;

    switch(msg)
    {
        case U8X8_MSG_DISPLAY_INIT:
            u8x8_d_helper_display_init(u8x8);
            u8x8_cad_SendSequence(u8x8, s_init_seq);
            return 1;

        case U8X8_MSG_DISPLAY_SETUP_MEMORY:
            u8x8_d_helper_display_setup_memory(u8x8, &s_display_info);
            return 1;

        default:
            return 0;
    }
}
