/* Google-Chrome-style dino runner, ported from NUEDC_2025B Firmware_0
 * (APP/Applications/game_dinosaur.c, original by TKWTL / hjl240) to this
 * project's MiaoUI:
 *
 *  - pass based: ui_loop() calls the page every available UI frame (~6 ms target) and every pass renders
 *    one frame (no frame divider); the simulation runs on its own 16 ms tick
 *    (DINO_SIM_TICK_MS), decoupled from the drawing cost - the original
 *    blocked its task with osDelay(16) inside a while(1) loop;
 *  - no floats: the original float score/height/speed became fixed point
 *    (score/world: 8 units per pixel, height: 6 units per pixel);
 *  - random: this project's own rand()/srand() (Project/Debug/debug.c,
 *    xorshift32) instead of the RTC-seeded libc pair - it is seeded from
 *    TIME_Millis() on every game start, because the board has no RTC;
 *  - screen: the original hardcoded a 64 px tall panel; the layout now
 *    derives from UI_VER_RES (128x80 here) so the ground sits at the bottom;
 *  - ground: same 597 px strip as the original, kept as the decoded
 *    dot/segment tables below (identical pixels and period);
 *  - keys: K2 (ENTER) exits to the menu and the run is kept, so entering the
 *    page again resumes it; K1 (DOWN) jumps and restarts after a game over
 *    (the original used ENTER/PLUS to jump and BACK to exit);
 *  - edge: cacti and clouds are blitted at their (possibly negative) x, just
 *    like in the original - u8g2's XBMP path clips the off-screen part, so
 *    they scroll out gradually instead of vanishing at the left edge;
 *  - speed: world (ground, clouds and cacti) at 3/4 of the former rate,
 *    jump arc interpolated over 1.5 times the original time (576 ms),
 *    leg animation duration doubled. Score and spawn countdowns unchanged.
 *
 * Fixed point: score/world use 8 units per pixel, carrying the quarter-unit
 * world speed remainder; jump height uses 6 units per pixel. */
#include "game_dinosaur.h"

#include "time_api.h"
#include "display/dispDriver.h"
#include "i18n.h"

#include <stdlib.h>   /* rand()/srand() - provided by Project/Debug/debug.c */
#include <stdio.h>    /* snprintf - the project's integer-only printf */

#define DINO_W  16
#define DINO_H  16

/* Ground strip: same 8 rows as the original GROUND[] bitmap, anchored to the
 * bottom of the panel.  Row 3 of the original strip is the (nearly solid)
 * surface line - its 5 solid segments are listed in s_ground_solid; the
 * remaining 290 set pixels (rows 0..2 and 4..7) are s_ground_dots, packed as
 * (x << 3) | row. */
#define DINO_GROUND_W       597u
#define DINO_GROUND_H       8u
#define DINO_GROUND_Y       (UI_VER_RES - 1u - DINO_GROUND_H)          /* 71 */
#define DINO_GROUND_LINE_Y  (DINO_GROUND_Y + 3u)                       /* 74 */

#define DINO_DEFAULT_POS_X  6
#define DINO_DEFAULT_HEIGHT (DINO_GROUND_LINE_Y - (DINO_H - 1u))       /* 59 */

#define DINO_SCORE_SHIFT    3u
/* Keep simulation, spawns and score on the original independent clock. */
#define DINO_SIM_TICK_MS    16u
#define DINO_SIM_MAX_TICKS  3u      /* catch-up cap per call (48 ms) */
#define DINO_GROUND_Y6      ((int16_t)(DINO_DEFAULT_HEIGHT * 6))
#define DINO_JUMP_SPEED2    (-12)   /* original immediate takeoff, px * 2 */
#define DINO_JUMP_GRAVITY2  1       /* add once per original arc step */
#define DINO_RUN_FRAME_PX   9u      /* 6 * 2 * 3/4: twice previous frame time */

#define CACTUS_RESPAWN_MAX  256u
#define CACTUS_RESPAWN_MIN  64u
#define CACTUS_SPACE        96
#define CACTUS_POOL_DEPTH   3u

#define CLOUD_RESPAWN_MAX   512u
#define CLOUD_RESPAWN_MIN   32u
#define CLOUD_YPOS_MAX      56u     /* was 40: range extended 16 px down */
#define CLOUD_YPOS_MIN      4u
#define CLOUD_SPEED_DIVID_MAX 4u
#define CLOUD_SPEED_DIVID_MIN 2u
#define CLOUD_POOL_DEPTH    4u
#define CLOUD_W 24
#define CLOUD_H 8

/* ---- sprites (verbatim from the original) --------------------------------- */
static const uint8_t s_dino[2][32] = {
    {0x00,0x7e,0x00,0xfb,0x00,0xff,0x00,0xff,0x00,0x0f,0x81,0x7f,0xc1,0x07,0xe3,0x1f,
     0xf7,0x17,0xff,0x07,0xfe,0x07,0xfc,0x03,0xf8,0x01,0xb0,0x01,0x60,0x01,0x00,0x03},
    {0x00,0x7e,0x00,0xfb,0x00,0xff,0x00,0xff,0x00,0x0f,0x81,0x7f,0xc1,0x07,0xe3,0x1f,
     0xf7,0x17,0xff,0x07,0xfe,0x07,0xfc,0x03,0xf8,0x01,0x30,0x03,0x10,0x00,0x30,0x00}
};

static const uint8_t s_dino_jump[32] = {
    0x00,0x7e,0x00,0xfb,0x00,0xff,0x00,0xff,0x00,0x0f,0x81,0x7f,0xc1,0x07,0xe3,0x1f,
    0xf7,0x17,0xff,0x07,0xfe,0x07,0xfc,0x03,0xf8,0x01,0xb0,0x01,0x10,0x01,0x30,0x03
};

static const uint8_t s_dino_end[32] = {
    0x00,0x7e,0x00,0xf1,0x00,0xf5,0x00,0xf1,0x00,0xff,0x81,0x7f,0xc1,0x07,0xe3,0x1f,
    0xf7,0x17,0xff,0x07,0xfe,0x07,0xfc,0x03,0xf8,0x01,0xb0,0x01,0x10,0x01,0x30,0x03
};

static const uint8_t s_cactus_0[16] = {
    0x18,0x18,0x18,0x18,0xdb,0xdb,0xdb,0xdb,0xdb,0x7f,0x3e,0x18,0x18,0x18,0x18,0x1c
};

static const uint8_t s_cactus_1[32] = {
    0x08,0x10,0x18,0x18,0x58,0x9a,0x58,0x9a,0x59,0x9a,0x5b,0x9a,0x5b,0x9e,0x7b,0xdc,
    0x3b,0xf8,0x1f,0x78,0x1e,0x18,0x18,0x18,0xd8,0x1b,0x18,0x18,0x18,0x18,0x98,0x1b
};

static const uint8_t s_cactus_2[48] = {
    0x08,0x18,0x10,0x18,0x1a,0x3a,0x58,0x1b,0xba,0x58,0x5b,0xfa,0x59,0xdb,0xfa,0x5b,
    0xdb,0xfa,0x5b,0xdb,0xfa,0x7b,0xdb,0xbe,0x3b,0xdb,0xfc,0x1f,0xdb,0x78,0x1e,0xde,
    0x18,0x18,0x78,0x18,0xdb,0x9b,0xdb,0x18,0x1a,0x18,0x18,0x18,0x18,0x18,0x18,0x18
};

static const uint8_t s_cactus_3[48] = {
    0x08,0x04,0x10,0x18,0x0c,0x18,0x18,0x2c,0x1a,0x18,0x2c,0x9a,0x59,0x2c,0xda,0x5b,
    0x3c,0xda,0x5b,0x1d,0xde,0x5b,0x4d,0xdc,0x5b,0x4d,0xd9,0x7f,0x4d,0x79,0x3e,0x4f,
    0x39,0x18,0xec,0x19,0x18,0x4c,0x18,0xf8,0x4d,0xd9,0x1b,0x4c,0x18,0xdc,0xcc,0x18
};

static const uint8_t s_cloud_image[24] = {
    0x00,0xfc,0x00,0x00,0x9e,0x01,0x00,0x02,0x03,0x80,0x03,0x1f,
    0xf8,0x80,0x70,0x7c,0x00,0x60,0x0e,0x00,0xc0,0xf7,0xff,0xff
};

typedef struct
{
    const uint8_t *pdata;
    uint8_t width;
    uint8_t height;
} dino_cactus_image_t;

static const dino_cactus_image_t s_cactus_image[4] = {
    { s_cactus_0, 8u, 16u },
    { s_cactus_1, 16u, 16u },
    { s_cactus_2, 24u, 16u },
    { s_cactus_3, 24u, 16u }
};

/* ---- ground tables (decoded from the original GROUND[] bitmap) ------------ */
static const uint16_t s_ground_dots[] = {
    0x0558,0x05b8,0x0620,0x0680,0x06e0,0x0748,0x0b71,0x0b79,0x0b81,0x0b89,0x0b91,0x1079,0x1081,0x1089,
    0x1091,0x10d9,0x10e1,0x10e9,0x10f1,0x0b6a,0x0b72,0x0b92,0x0b9a,0x106a,0x1072,0x107a,0x1092,0x109a,
    0x10a2,0x10ca,0x10d2,0x10da,0x10f2,0x10fa,0x1102,0x00e4,0x00ec,0x0264,0x026c,0x038c,0x0394,0x058c,
    0x0594,0x0904,0x090c,0x0a44,0x0a4c,0x0bc4,0x0bcc,0x0bdc,0x0be4,0x0bec,0x0bf4,0x0bfc,0x0c04,0x0c0c,
    0x0c14,0x0c1c,0x0cec,0x0cf4,0x0eec,0x0ef4,0x1264,0x126c,0x001d,0x0025,0x002d,0x00e5,0x00ed,0x015d,
    0x019d,0x01a5,0x0265,0x026d,0x02dd,0x02e5,0x0365,0x038d,0x0395,0x03a5,0x03dd,0x04b5,0x050d,0x058d,
    0x0595,0x05ad,0x05fd,0x0605,0x066d,0x0675,0x06dd,0x06e5,0x07ad,0x07b5,0x07cd,0x07d5,0x0815,0x081d,
    0x0885,0x088d,0x089d,0x08cd,0x08d5,0x08dd,0x0905,0x090d,0x097d,0x0985,0x098d,0x0a45,0x0a4d,0x0ab5,
    0x0abd,0x0afd,0x0b05,0x0bc5,0x0bcd,0x0c3d,0x0c45,0x0cbd,0x0cc5,0x0ced,0x0cf5,0x0cfd,0x0d05,0x0d3d,
    0x0e0d,0x0e15,0x0e65,0x0e6d,0x0ee5,0x0eed,0x0ef5,0x0f0d,0x0f55,0x0f5d,0x0f65,0x0fcd,0x0fd5,0x103d,
    0x1045,0x110d,0x1115,0x1125,0x112d,0x1135,0x116d,0x1175,0x117d,0x11e5,0x11ed,0x11f5,0x11fd,0x122d,
    0x1235,0x123d,0x125d,0x1265,0x126d,0x0006,0x000e,0x0016,0x003e,0x0046,0x00ae,0x00c6,0x00ce,0x00d6,
    0x011e,0x0126,0x016e,0x0176,0x017e,0x01ee,0x022e,0x02ce,0x02f6,0x02fe,0x03a6,0x041e,0x042e,0x0436,
    0x046e,0x0476,0x04b6,0x0546,0x054e,0x066e,0x0676,0x0686,0x068e,0x0696,0x06b6,0x0746,0x074e,0x07ae,
    0x07b6,0x0816,0x081e,0x0836,0x083e,0x08f6,0x0936,0x0966,0x096e,0x0976,0x099e,0x09a6,0x0a06,0x0a0e,
    0x0a26,0x0a2e,0x0a36,0x0a7e,0x0a86,0x0ace,0x0ad6,0x0ade,0x0b4e,0x0b86,0x0b8e,0x0c2e,0x0c56,0x0c5e,
    0x0cfe,0x0d06,0x0d7e,0x0d8e,0x0d96,0x0dce,0x0dd6,0x0e0e,0x0e16,0x0e9e,0x0ea6,0x0eae,0x0fce,0x0fd6,
    0x0fe6,0x0fee,0x0ff6,0x1016,0x10a6,0x10ae,0x110e,0x1116,0x116e,0x1176,0x117e,0x1196,0x119e,0x1256,
    0x1296,0x0007,0x000f,0x0017,0x003f,0x0047,0x00c7,0x00cf,0x00d7,0x01ef,0x0547,0x054f,0x0687,0x068f,
    0x0697,0x06b7,0x0837,0x083f,0x0937,0x0967,0x096f,0x0977,0x099f,0x09a7,0x0a27,0x0a2f,0x0a37,0x0b4f,
    0x0e9f,0x0ea7,0x0eaf,0x0fe7,0x0fef,0x0ff7,0x1017,0x1197,0x119f,0x1297,
};

/* Row 3 of the original strip: solid runs as (start, length) in the 597 px
 * pattern.  The 4 gaps between them are the small dashes of the surface. */
static const uint16_t s_ground_solid[5][2] = {
    {   0u, 366u },
    { 371u,  10u },
    { 386u, 140u },
    { 531u,   7u },
    { 543u,  54u }
};

/* ---- state ---------------------------------------------------------------- */
typedef struct
{
    uint8_t available;
    uint32_t start_pos;   /* space position while available, spawn countdown otherwise */
    uint8_t type;
} dino_cactus_t;

typedef struct
{
    uint8_t available;
    uint32_t start_pos;
    uint8_t y_pos;
    uint8_t speed_div;
} dino_cloud_t;

typedef enum
{
    DINO_PLAYING = 0,
    DINO_FAILED
} dino_state_t;

static dino_cactus_t s_cactus[CACTUS_POOL_DEPTH];
static dino_cloud_t s_cloud[CLOUD_POOL_DEPTH];

static uint32_t s_score_x8;      /* score pixels * 8 (original score rate) */
static uint32_t s_dist_x8;       /* world pixels * 8 */
static uint8_t s_world_rem;      /* quarter of a world fixed-point unit */
static uint8_t s_life;
static dino_state_t s_state;
static int16_t s_height6;        /* dino top, pixels * 6 */
static uint8_t s_jump_phase;      /* 0..2, two time units per 16 ms tick */
static int16_t s_speed2;         /* vertical speed, pixels * 2 per frame */
static uint8_t s_last_coll;
static uint16_t s_high_score;    /* displayed units (score_px / 2) */
static uint32_t s_score_rem;     /* odd score unit carried to the next tick */
static uint32_t s_sim_last;      /* TIME_Millis() of the last call (0 = re-arm) */
static uint32_t s_sim_acc;       /* ms accumulated since the last simulation tick */
static uint8_t s_inited;         /* 0 -> first entry starts a run (exiting keeps it) */

static uint32_t dino_score_px(void)
{
    return s_score_x8 >> DINO_SCORE_SHIFT;
}

static uint32_t dino_dist_px(void)
{
    return s_dist_x8 >> DINO_SCORE_SHIFT;
}

static uint32_t dino_rand(uint32_t range)
{
    return (uint32_t)rand() % range;
}

static void dino_start_game(void)
{
    uint8_t i;

    srand((unsigned int)TIME_Millis());   /* this board has no RTC */

    s_score_x8 = 0u;
    s_dist_x8 = 0u;
    s_world_rem = 0u;
    s_life = 3u;
    s_state = DINO_PLAYING;
    s_height6 = DINO_GROUND_Y6;
    s_jump_phase = 0u;
    s_speed2 = 0;
    s_last_coll = 0u;
    s_score_rem = 0u;
    s_sim_last = 0u;
    s_sim_acc = 0u;

    for(i = 0u; i < CACTUS_POOL_DEPTH; i++)
    {
        s_cactus[i].available = 0u;
        s_cactus[i].type = 0u;
        s_cactus[i].start_pos = CACTUS_RESPAWN_MIN + dino_rand(CACTUS_RESPAWN_MAX - CACTUS_RESPAWN_MIN);
    }

    for(i = 0u; i < CLOUD_POOL_DEPTH; i++)
    {
        s_cloud[i].available = 0u;
        s_cloud[i].y_pos = 0u;
        s_cloud[i].speed_div = CLOUD_SPEED_DIVID_MIN;
        s_cloud[i].start_pos = CLOUD_RESPAWN_MIN + dino_rand(CLOUD_RESPAWN_MAX - CLOUD_RESPAWN_MIN);
    }
}

/* ---- ground --------------------------------------------------------------- */
static void dino_ground_draw(uint32_t off)
{
    uint16_t i;
    uint8_t k;
    int16_t sx;
    int32_t a;
    int32_t b;

    /* Surface line: every solid run, plus the copy one pattern length further
     * right so the 597 px strip wraps seamlessly.  Both are clipped to the
     * panel here because u8g2 would drop negative coordinates. */
    for(i = 0u; i < (uint16_t)(sizeof s_ground_solid / sizeof s_ground_solid[0]); i++)
    {
        for(k = 0u; k < 2u; k++)
        {
            a = (int32_t)s_ground_solid[i][0] - (int32_t)off + (int32_t)k * (int32_t)DINO_GROUND_W;
            b = a + (int32_t)s_ground_solid[i][1];

            if(a < 0)
                a = 0;
            if(b > (int32_t)UI_HOR_RES)
                b = (int32_t)UI_HOR_RES;

            if(b > a)
                Disp_DrawBox((uint16_t)a, DINO_GROUND_LINE_Y, (uint16_t)(b - a), 1u);
        }
    }

    /* Gravel dots (all other rows of the strip) */
    for(i = 0u; i < (uint16_t)(sizeof s_ground_dots / sizeof s_ground_dots[0]); i++)
    {
        sx = (int16_t)((int16_t)(s_ground_dots[i] >> 3) - (int16_t)off);
        if(sx < 0)
            sx = (int16_t)(sx + (int16_t)DINO_GROUND_W);
        if(sx < (int16_t)UI_HOR_RES)
            Disp_DrawPixel((uint16_t)sx,
                           (uint16_t)(DINO_GROUND_Y + (uint16_t)(s_ground_dots[i] & 7u)));
    }
}

/* ---- clouds --------------------------------------------------------------- */
static int32_t dino_cloud_pos(const dino_cloud_t *cloud, uint32_t dist_px)
{
    return (int32_t)UI_HOR_RES -
           ((int32_t)dist_px - (int32_t)cloud->start_pos) / (int32_t)cloud->speed_div;
}

static void dino_cloud_draw(uint32_t dist_px)
{
    uint8_t i;
    int32_t pos;

    for(i = 0u; i < CLOUD_POOL_DEPTH; i++)
    {
        if(s_cloud[i].available != 0u)
        {
            pos = dino_cloud_pos(&s_cloud[i], dist_px);
            /* a negative x is fine: u8g2 clips the visible part */
            Disp_DrawXBMP((uint16_t)pos, (uint16_t)s_cloud[i].y_pos, CLOUD_W, CLOUD_H, s_cloud_image);
        }
    }
}

static void dino_cloud_process(uint32_t dist_px)
{
    uint8_t i;
    int32_t pos;

    for(i = 0u; i < CLOUD_POOL_DEPTH; i++)
    {
        if(s_cloud[i].available != 0u)
        {
            pos = dino_cloud_pos(&s_cloud[i], dist_px);
            if(pos < -(int32_t)CLOUD_W)     /* fully off the left edge */
            {
                s_cloud[i].available = 0u;
                s_cloud[i].start_pos = CLOUD_RESPAWN_MIN + dino_rand(CLOUD_RESPAWN_MAX - CLOUD_RESPAWN_MIN);
            }
        }
        else
        {
            if(s_cloud[i].start_pos > 0u)
                s_cloud[i].start_pos--;     /* spawn countdown */

            if(s_cloud[i].start_pos == 0u)
            {
                s_cloud[i].available = 1u;
                s_cloud[i].start_pos = dist_px;
                s_cloud[i].y_pos = (uint8_t)(CLOUD_YPOS_MIN + dino_rand(CLOUD_YPOS_MAX - CLOUD_YPOS_MIN));
                s_cloud[i].speed_div = (uint8_t)(CLOUD_SPEED_DIVID_MIN +
                                                 dino_rand(CLOUD_SPEED_DIVID_MAX - CLOUD_SPEED_DIVID_MIN));
            }
        }
    }
}

/* ---- cacti ---------------------------------------------------------------- */
static int32_t dino_cactus_pos(const dino_cactus_t *cactus, uint32_t dist_px)
{
    return (int32_t)UI_HOR_RES - ((int32_t)dist_px - (int32_t)cactus->start_pos);
}

static void dino_cactus_draw(uint32_t dist_px)
{
    uint8_t i;
    int32_t pos;

    for(i = 0u; i < CACTUS_POOL_DEPTH; i++)
    {
        if(s_cactus[i].available != 0u)
        {
            pos = dino_cactus_pos(&s_cactus[i], dist_px);
            /* a negative x is fine: u8g2 clips the visible part */
            Disp_DrawXBMP((uint16_t)pos, (uint16_t)DINO_DEFAULT_HEIGHT,
                          s_cactus_image[s_cactus[i].type].width,
                          s_cactus_image[s_cactus[i].type].height,
                          s_cactus_image[s_cactus[i].type].pdata);
        }
    }
}

static void dino_cactus_process(uint32_t dist_px, uint32_t score_px)
{
    uint8_t i;
    uint8_t j;
    int32_t pos;

    for(i = 0u; i < CACTUS_POOL_DEPTH; i++)
    {
        if(s_cactus[i].available != 0u)
        {
            pos = dino_cactus_pos(&s_cactus[i], dist_px);
            if(pos < -(int32_t)s_cactus_image[s_cactus[i].type].width)
            {
                s_cactus[i].available = 0u;
                s_cactus[i].start_pos = CACTUS_RESPAWN_MIN + dino_rand(CACTUS_RESPAWN_MAX - CACTUS_RESPAWN_MIN);
            }
        }
        else
        {
            if(s_cactus[i].start_pos > 0u)
                s_cactus[i].start_pos--;    /* spawn countdown */

            if(s_cactus[i].start_pos == 0u)
            {
                s_cactus[i].available = 1u;

                /* keep the minimum spacing to every other live cactus */
                pos = (int32_t)dist_px;
                for(j = 0u; j < CACTUS_POOL_DEPTH; j++)
                {
                    if((j != i) && (s_cactus[j].available != 0u))
                    {
                        if((int32_t)s_cactus[j].start_pos + CACTUS_SPACE > pos)
                            pos = (int32_t)s_cactus[j].start_pos + CACTUS_SPACE;
                    }
                }
                s_cactus[i].start_pos = (uint32_t)pos;

                /* more shapes as the score grows (as in the original) */
                if(score_px < 500u)
                    s_cactus[i].type = 0u;
                else if(score_px < 2500u)
                    s_cactus[i].type = (uint8_t)dino_rand(2u);
                else
                    s_cactus[i].type = (uint8_t)dino_rand(4u);
            }
        }
    }
}

static uint8_t dino_cactus_collision(int16_t dino_top_px, uint32_t dist_px)
{
    uint8_t i;
    int32_t pos;
    int32_t dino_x1 = DINO_DEFAULT_POS_X + 1;
    int32_t dino_x2 = DINO_DEFAULT_POS_X + DINO_W - 1;

    for(i = 0u; i < CACTUS_POOL_DEPTH; i++)
    {
        if(s_cactus[i].available != 0u)
        {
            pos = dino_cactus_pos(&s_cactus[i], dist_px);
            if((dino_x2 > pos) && (dino_x1 < pos + (int32_t)s_cactus_image[s_cactus[i].type].width))
            {
                if(dino_top_px > (int16_t)(DINO_DEFAULT_HEIGHT - (int16_t)DINO_H + 4))
                    return 1u;
            }
        }
    }

    return 0u;
}

/* ---- page ----------------------------------------------------------------- */
static void dino_center_label(uint16_t y, const char *english)
{
    const char *label = DPC_I18n_Text(english);
    uint16_t w = DPC_I18n_TextWidth(label);
    DPC_I18n_DrawText(w < UI_HOR_RES ? (UI_HOR_RES - w) / 2u : 0u, y, label);
}

void Game_DinoSaur(ui_t *ui)
{
    char buf[32];
    uint8_t bg;
    uint8_t fg;
    uint32_t score_px;
    uint32_t dist_px;
    uint32_t step_x8;
    uint32_t now_ms;
    uint32_t dt;
    uint8_t ticks;
    uint8_t dino_frame;
    uint8_t coll;
    int16_t dino_top;

    /* K2 leaves the page (the action stays set so ui.c's WORD handling exits).
     * The run is not reset, so coming back to the page resumes it. */
    if(ui->action == UI_ACTION_ENTER)
    {
        s_sim_last = 0u;   /* re-entering later must not fast-forward the run */
        return;
    }

    /* K1 jumps (or restarts after a game over) and is consumed so the menu
     * never sees it. */
    if(ui->action == UI_ACTION_DOWN)
    {
        ui->action = UI_ACTION_NONE;

        if(s_inited != 0u)
        {
            if(s_state == DINO_FAILED)
                dino_start_game();
            else if(s_height6 >= DINO_GROUND_Y6)
            {
                /* Preserve the immediate -6px displacement; the first
                 * post-launch old-step velocity already includes gravity. */
                s_speed2 = DINO_JUMP_SPEED2 + DINO_JUMP_GRAVITY2;
                s_jump_phase = 0u;
                s_height6 = (int16_t)(s_height6 + 3 * DINO_JUMP_SPEED2);
            }
        }
    }

    if(s_inited == 0u)
    {
        dino_start_game();
        s_inited = 1u;
    }

    /* Independent 16 ms simulation clock. Rendering may be deferred by
     * soft-I2C activity; the bounded catch-up policy remains unchanged. */
    now_ms = TIME_Millis();
    if(s_sim_last == 0u)
    {
        s_sim_last = now_ms;              /* first pass after (re)entering */
        s_sim_acc = 0u;
    }

    dt = now_ms - s_sim_last;
    s_sim_last = now_ms;
    if(dt > (uint32_t)(DINO_SIM_TICK_MS * DINO_SIM_MAX_TICKS))
        dt = (uint32_t)(DINO_SIM_TICK_MS * DINO_SIM_MAX_TICKS);  /* paused: no fast-forward */
    s_sim_acc += dt;

    score_px = dino_score_px();
    dist_px = dino_dist_px();

    coll = s_last_coll;   /* keeps the "hit" sprite after a game over */

    ticks = 0u;
    while((s_sim_acc >= DINO_SIM_TICK_MS) && (ticks < DINO_SIM_MAX_TICKS))
    {
        s_sim_acc -= DINO_SIM_TICK_MS;
        ticks++;

        if(s_state != DINO_PLAYING)
            continue;                     /* frozen after a game over */

        /* speed tiers of the original (px per 16 ms frame): 2, then
         * +score/2000 + 1.75, then +score/20000 + 2.875, then 4 */
        if(score_px < 500u)
            step_x8 = 16u;
        else if(score_px < 2500u)
            step_x8 = (s_score_x8 / 2000u) + 14u;
        else if(score_px < 22500u)
            step_x8 = (s_score_x8 / 20000u) + 23u;
        else
            step_x8 = 32u;

        /* Score: half an original step per tick, the odd unit carried over -
         * one full original step per 32 ms (half the original score rate). */
        s_score_rem += step_x8;
        s_score_x8 += s_score_rem >> 1;
        s_score_rem &= 1u;
        score_px = dino_score_px();

        /* World speed = 75%, including ground, clouds and cacti. Carry
         * quarters rather than truncating each scaled step. */
        s_world_rem = (uint8_t)(s_world_rem + 3u * step_x8);
        s_dist_x8 += s_world_rem >> 2;
        s_world_rem &= 3u;
        dist_px = dino_dist_px();

        dino_cloud_process(dist_px);
        dino_cactus_process(dist_px, score_px);

        /* Original arc segment = 3 time units; each 16 ms tick = 2.
         * Integrate its piecewise-linear velocity across a segment boundary
         * using sixth-pixel height. 24 old segments -> 36 ticks = 576 ms.
         * Neither peak height nor the world/score clock changes. */
        if(s_height6 < DINO_GROUND_Y6)
        {
            if(s_jump_phase == 2u)
            {
                s_height6 = (int16_t)(s_height6 + s_speed2);
                s_speed2 += DINO_JUMP_GRAVITY2;
                s_height6 = (int16_t)(s_height6 + s_speed2);
                s_jump_phase = 1u;
            }
            else
            {
                s_height6 = (int16_t)(s_height6 + 2 * s_speed2);
                s_jump_phase += 2u;
                if(s_jump_phase == 3u)
                {
                    s_jump_phase = 0u;
                    s_speed2 += DINO_JUMP_GRAVITY2;
                }
            }
            if(s_height6 >= DINO_GROUND_Y6)
            {
                s_height6 = DINO_GROUND_Y6;
                s_jump_phase = 0u;
            }
        }

        coll = dino_cactus_collision((int16_t)(s_height6 / 6), dist_px);
        if((coll != 0u) && (s_last_coll == 0u))   /* edge triggered, as the original */
        {
            s_life--;
            if(s_life == 0u)
            {
                s_state = DINO_FAILED;
                s_high_score = (uint16_t)(score_px >> 1);
            }
        }
        s_last_coll = coll;
    }

    dino_top = (int16_t)(s_height6 / 6);

    /* ---- rendering (later draws cover earlier ones) ---- */
    bg = ui->bgColor;
    Disp_SetFont(UI_FONT);
    Disp_SetDrawColor(&bg);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);

    fg = (uint8_t)(ui->bgColor ^ 1u);
    Disp_SetDrawColor(&fg);

    Disp_SetBitmapMode(1u);     /* transparent foreground, as the original */

    dino_ground_draw(dist_px % DINO_GROUND_W);
    dino_cloud_draw(dist_px);
    dino_cactus_draw(dist_px);

    /* Life icons and numeric scores preserve their ASCII content. The zh
     * score label needs a slightly lower baseline and measured alignment. */
    uint16_t score_y = 11u;
    if(s_life >= 3u)
        Disp_DrawStr(1u, score_y, "***");
    else if(s_life == 2u)
        Disp_DrawStr(1u, score_y, "**");
    else if(s_life == 1u)
        Disp_DrawStr(1u, score_y, "*");

    snprintf(buf, sizeof buf, DPC_I18n_Text("HI %05d %05d"),
             (int)s_high_score, (int)(score_px >> 1));
    if(DPC_I18n_IsChinese())
    {
        uint16_t w = DPC_I18n_TextWidth(buf);
        if(w <= UI_HOR_RES - 22u)
            DPC_I18n_DrawText(UI_HOR_RES - 2u - w, score_y, buf);
        else
        {
            /* Prefer readable counters to overlap with the life icons. */
            snprintf(buf, sizeof buf, "HI %05d %05d", (int)s_high_score, (int)(score_px >> 1));
            Disp_SetFont(UI_FONT);
            Disp_DrawStr(UI_HOR_RES - 2u - Disp_GetUTF8Width(buf), score_y, buf);
        }
    }
    else
        Disp_DrawStr(UI_HOR_RES - 2u - Disp_GetUTF8Width(buf), score_y, buf);

    dino_frame = (uint8_t)((dist_px / DINO_RUN_FRAME_PX) % 2u);
    if(coll != 0u)
        Disp_DrawXBMP(DINO_DEFAULT_POS_X, (uint16_t)dino_top, DINO_W, DINO_H, s_dino_end);
    else if(dino_top != (int16_t)DINO_DEFAULT_HEIGHT)
        Disp_DrawXBMP(DINO_DEFAULT_POS_X, (uint16_t)dino_top, DINO_W, DINO_H, s_dino_jump);
    else
        Disp_DrawXBMP(DINO_DEFAULT_POS_X, (uint16_t)dino_top, DINO_W, DINO_H, s_dino[dino_frame]);

    Disp_SetBitmapMode(0u);

    if(s_state == DINO_FAILED)
    {
        dino_center_label(24u, "Game Over!!");
        dino_center_label(37u, "Dino Game V0.1");
        dino_center_label(50u, "from hjl240");
        dino_center_label(63u, "Jump to Restart");
    }

    Disp_SendBuffer();
}
