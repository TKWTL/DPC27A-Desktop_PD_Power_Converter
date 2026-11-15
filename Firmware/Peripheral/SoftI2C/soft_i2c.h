#ifndef SOFT_I2C_H_
#define SOFT_I2C_H_

#include <stdint.h>
#include "ch32x035.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOFT_I2C_DEFAULT_HALF_PERIOD_US      5u
#define SOFT_I2C_DEFAULT_STRETCH_TIMEOUT_US  1000u

typedef enum
{
    SOFT_I2C_RESULT_IDLE = 0,
    SOFT_I2C_RESULT_ACTIVE,
    SOFT_I2C_RESULT_OK,
    SOFT_I2C_RESULT_NACK,
    SOFT_I2C_RESULT_TIMEOUT,
    SOFT_I2C_RESULT_BUS_STUCK,
    SOFT_I2C_RESULT_ERROR
} SoftI2C_Result;

typedef enum
{
    SOFT_I2C_STATE_IDLE = 0,
    SOFT_I2C_STATE_BUS_CHECK,
    SOFT_I2C_STATE_START_SDA_LOW,
    SOFT_I2C_STATE_START_SCL_LOW,
    SOFT_I2C_STATE_TX_BIT_SETUP,
    SOFT_I2C_STATE_TX_SCL_HIGH_WAIT,
    SOFT_I2C_STATE_TX_SCL_LOW,
    SOFT_I2C_STATE_TX_ACK_SCL_HIGH_WAIT,
    SOFT_I2C_STATE_TX_ACK_SAMPLE,
    SOFT_I2C_STATE_RESTART_RELEASE_SDA,
    SOFT_I2C_STATE_RESTART_SCL_HIGH_WAIT,
    SOFT_I2C_STATE_RESTART_SDA_LOW,
    SOFT_I2C_STATE_RESTART_SCL_LOW,
    SOFT_I2C_STATE_RX_SCL_HIGH_WAIT,
    SOFT_I2C_STATE_RX_SAMPLE,
    SOFT_I2C_STATE_RX_ACK_SCL_HIGH_WAIT,
    SOFT_I2C_STATE_RX_ACK_SCL_LOW,
    SOFT_I2C_STATE_STOP_SDA_LOW,
    SOFT_I2C_STATE_STOP_SCL_HIGH_WAIT,
    SOFT_I2C_STATE_STOP_SDA_RELEASE,
    SOFT_I2C_STATE_STOP_DONE
} SoftI2C_State;

typedef enum
{
    SOFT_I2C_TX_STAGE_ADDRESS_WRITE = 0,
    SOFT_I2C_TX_STAGE_DATA,
    SOFT_I2C_TX_STAGE_ADDRESS_READ
} SoftI2C_TxStage;

/*
 * One handle owns one physical software-I2C bus and one in-flight transfer.
 * This is intentionally handle based: fixed-address devices can coexist by
 * using separate handles on separate GPIO pairs.
 *
 * Coroutine users call SoftI2C_Try*() once, then let SoftI2C_Service() run on
 * every scheduler pass and wait until SoftI2C_GetResult() is terminal.  No
 * microsecond busy-wait is used in the asynchronous path.
 */
typedef struct
{
    GPIO_TypeDef *scl_port;
    uint32_t scl_pin;
    GPIO_TypeDef *sda_port;
    uint32_t sda_pin;
    uint16_t half_period_us;
    uint16_t stretch_timeout_us;

    volatile SoftI2C_State state;
    volatile SoftI2C_Result result;
    SoftI2C_Result pending_result;

    uint8_t address_7bit;
    const uint8_t *tx;
    uint16_t tx_len;
    uint16_t tx_index;
    uint8_t *rx;
    uint16_t rx_len;
    uint16_t rx_index;

    SoftI2C_TxStage tx_stage;
    uint8_t tx_byte;
    uint8_t tx_mask;
    uint8_t rx_byte;
    uint8_t rx_bits_left;

    uint32_t next_edge_ticks;
    uint32_t stretch_started_ticks;
    uint32_t half_period_ticks;
    uint32_t stretch_timeout_ticks;
    uint8_t scl_released_for_wait;

    uint32_t recovery_count; /* original bus-clear count */
} SoftI2C_Handle;

/* Backward-compatible type name used by the first project-port patch. */
typedef SoftI2C_Handle SoftI2C_Bus;

void SoftI2C_Init(SoftI2C_Handle *bus);

uint8_t SoftI2C_TryPing(SoftI2C_Handle *bus, uint8_t address_7bit);
uint8_t SoftI2C_TryWrite(SoftI2C_Handle *bus, uint8_t address_7bit,
                         const uint8_t *data, uint16_t length);
uint8_t SoftI2C_TryWriteRead(SoftI2C_Handle *bus, uint8_t address_7bit,
                             const uint8_t *tx, uint16_t tx_len,
                             uint8_t *rx, uint16_t rx_len);

/* Advance at most one timing/state-machine step, using free-running ticks.
 * Never intentionally blocks; foreground callers must service frequently. */
void SoftI2C_Service(SoftI2C_Handle *bus);

SoftI2C_Result SoftI2C_GetResult(const SoftI2C_Handle *bus);
SoftI2C_Result SoftI2C_TakeResult(SoftI2C_Handle *bus);
uint8_t SoftI2C_IsBusy(const SoftI2C_Handle *bus);
uint32_t SoftI2C_GetRecoveryCount(const SoftI2C_Handle *bus);

/* Explicit bus-clear utility. This path is synchronous and intended only for
 * startup/fault recovery; normal transfers should use the Try-and-Service API. */
uint8_t SoftI2C_Recover(SoftI2C_Handle *bus);

/* Compatibility blocking wrappers. New coroOS code should prefer Try-and-Service. */
uint8_t SoftI2C_Write(SoftI2C_Handle *bus, uint8_t address_7bit,
                      const uint8_t *data, uint16_t length);
uint8_t SoftI2C_WriteRead(SoftI2C_Handle *bus, uint8_t address_7bit,
                          const uint8_t *tx, uint16_t tx_len,
                          uint8_t *rx, uint16_t rx_len);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_I2C_H_ */
