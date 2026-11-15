#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32x035.h"

/* Compile-time logging policy. The three operational logs below remain
 * enabled even in the lean (DPC_DIAGNOSTICS_ENABLE=0) configuration.
 * The detailed option owns optional VBUS/PM, OLED-ready, CFG and PD trace
 * messages (PDO dumps, PHY and negotiation detail). Concise PD events remain. Temporary I2C fault reporting was retired. All changes require
 * a Clean Build. */
#define DPC_DIAGNOSTICS_ENABLE       0  /* Detailed diagnostics (default off) */
#define DPC_LOG_STARTUP_ENABLE       1  /* Startup identity, reset, buses, PD */
#define DPC_LOG_POWER_ALLOC_ENABLE   1  /* Only when applied power limits change */
#define DPC_LOG_FAN_CHANGE_ENABLE    1  /* Only when fan PWM duty changes */

#if (DPC_DIAGNOSTICS_ENABLE != 0) && (DPC_DIAGNOSTICS_ENABLE != 1)
#error "DPC_DIAGNOSTICS_ENABLE must be 0 or 1"
#endif
#if (DPC_LOG_STARTUP_ENABLE != 0) && (DPC_LOG_STARTUP_ENABLE != 1)
#error "DPC_LOG_STARTUP_ENABLE must be 0 or 1"
#endif
#if (DPC_LOG_POWER_ALLOC_ENABLE != 0) && (DPC_LOG_POWER_ALLOC_ENABLE != 1)
#error "DPC_LOG_POWER_ALLOC_ENABLE must be 0 or 1"
#endif
#if (DPC_LOG_FAN_CHANGE_ENABLE != 0) && (DPC_LOG_FAN_CHANGE_ENABLE != 1)
#error "DPC_LOG_FAN_CHANGE_ENABLE must be 0 or 1"
#endif
/*
 * Desktop PD Power Converter board net aliases.
 *
 * These names describe MCU direction/function, not legacy DemoBoard labels.
 * The mapping is derived from Netlist_Schematic1_2026-09-09.tel.
 */

/* USART1 debug console: PB10=TX, PB11=RX. */
#define UART_DBG_TX_Pin              GPIO_Pin_10
#define UART_DBG_TX_GPIO_Port        GPIOB
#define UART_DBG_TX_GPIO_CLK         RCC_APB2Periph_GPIOB
#define UART_DBG_RX_Pin              GPIO_Pin_11
#define UART_DBG_RX_GPIO_Port        GPIOB
#define UART_DBG_RX_GPIO_CLK         RCC_APB2Periph_GPIOB

/* Shared hardware I2C1: SW3538 + GX21M15U. AFIO I2C1_RM=001. */
#define PWR_I2C_SCL_Pin              GPIO_Pin_13
#define PWR_I2C_SCL_GPIO_Port        GPIOA
#define PWR_I2C_SCL_GPIO_CLK         RCC_APB2Periph_GPIOA
#define PWR_I2C_SDA_Pin              GPIO_Pin_14
#define PWR_I2C_SDA_GPIO_Port        GPIOA
#define PWR_I2C_SDA_GPIO_CLK         RCC_APB2Periph_GPIOA
#define PWR_I2C_REMAP                GPIO_PartialRemap1_I2C1

/* SW3526 #1 software I2C (fixed-address device). */
#define SW3526_1_SCL_Pin             GPIO_Pin_4
#define SW3526_1_SCL_GPIO_Port       GPIOA
#define SW3526_1_SDA_Pin             GPIO_Pin_3
#define SW3526_1_SDA_GPIO_Port       GPIOA

/* SW3526 #2 software I2C (separate bus because the address conflicts). */
#define SW3526_2_SCL_Pin             GPIO_Pin_1
#define SW3526_2_SCL_GPIO_Port       GPIOA
#define SW3526_2_SDA_Pin             GPIO_Pin_2
#define SW3526_2_SDA_GPIO_Port       GPIOA

/* 0.78-inch 128x80 OLED, 4-wire SPI. SPI1_RM=10; PA9 is D/C, not MISO. */
#define OLED_MOSI_Pin                GPIO_Pin_10
#define OLED_MOSI_GPIO_Port          GPIOA
#define OLED_SCK_Pin                 GPIO_Pin_11
#define OLED_SCK_GPIO_Port           GPIOA
#define OLED_CS_Pin                  GPIO_Pin_12
#define OLED_CS_GPIO_Port            GPIOA
#define OLED_DC_Pin                  GPIO_Pin_9
#define OLED_DC_GPIO_Port            GPIOA
#define OLED_SPI_REMAP               GPIO_PartialRemap2_SPI1

/* User inputs / board control. */
#define KEY1_Pin                     GPIO_Pin_5
#define KEY1_GPIO_Port               GPIOA
#define KEY2_Pin                     GPIO_Pin_6
#define KEY2_GPIO_Port               GPIOA
#define POWER_INT_Pin                GPIO_Pin_8
#define POWER_INT_GPIO_Port          GPIOA
#define FAN_Pin                      GPIO_Pin_9
#define FAN_GPIO_Port                GPIOB

/* VBUS divider: 75 kOhm high side, 6.8 kOhm low side -> PA7 / ADC A7. */
#define VBUS_SENSE_Pin               GPIO_Pin_7
#define VBUS_SENSE_GPIO_Port         GPIOA
#define VBUS_SENSE_ADC_CHANNEL       ADC_Channel_7
#define VBUS_DIVIDER_TOP_OHM         75000UL
#define VBUS_DIVIDER_BOTTOM_OHM      6800UL
#define VBUS_ADC_VREF_MV             3300UL
#define VBUS_ADC_FULL_SCALE          4095UL

/* Native CH32X035 USB-PD pins. */
#define PD_CC1_Pin                   GPIO_Pin_14
#define PD_CC1_GPIO_Port             GPIOC
#define PD_CC1_GPIO_CLK              RCC_APB2Periph_GPIOC
#define PD_CC2_Pin                   GPIO_Pin_15
#define PD_CC2_GPIO_Port             GPIOC
#define PD_CC2_GPIO_CLK              RCC_APB2Periph_GPIOC

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
