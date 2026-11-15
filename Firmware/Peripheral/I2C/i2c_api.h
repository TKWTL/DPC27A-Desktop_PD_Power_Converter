#ifndef I2C_API_H_
#define I2C_API_H_

#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_API_DEFAULT_CLOCK_HZ          400000UL
#define I2C_API_WATCHDOG_TIMEOUT_MS       75UL

typedef enum
{
    I2C_API_OWNER_NONE = 0,
    I2C_API_OWNER_SW3538,
    I2C_API_OWNER_GX21M15U,
    I2C_API_OWNER_APPLICATION
} I2C_API_Owner;

typedef enum
{
    I2C_API_RESULT_IDLE = 0,
    I2C_API_RESULT_ACTIVE,
    I2C_API_RESULT_OK,
    I2C_API_RESULT_NACK,
    I2C_API_RESULT_TIMEOUT,
    I2C_API_RESULT_ERROR
} I2C_API_Result;

typedef enum
{
    I2C_API_ERR_NONE = 0,
    I2C_API_ERR_BUS_BUSY,
    I2C_API_ERR_START_TX,
    I2C_API_ERR_ADDR_TX,
    I2C_API_ERR_TX_DMA,
    I2C_API_ERR_TX_BTF,
    I2C_API_ERR_START_RX,
    I2C_API_ERR_ADDR_RX,
    I2C_API_ERR_RX_DMA,
    I2C_API_ERR_RX,
    I2C_API_ERR_NACK,
    I2C_API_ERR_BERR,
    I2C_API_ERR_ARLO,
    I2C_API_ERR_OVR,
    I2C_API_ERR_WATCHDOG,
    I2C_API_ERR_STOP,       /* STOP / BUSY / MSL did not settle */
    I2C_API_ERR_BUS_STATE   /* software phase no longer owns hardware bus */
} I2C_API_Error;

typedef enum
{
    I2C_API_STATE_IDLE = 0,
    I2C_API_STATE_WAIT_BUS_IDLE,
    I2C_API_STATE_WAIT_START_TX,
    I2C_API_STATE_WAIT_ADDR_TX,
    I2C_API_STATE_WAIT_TX_DMA,
    I2C_API_STATE_WAIT_TX_BTF,
    I2C_API_STATE_WAIT_START_RX,
    I2C_API_STATE_WAIT_ADDR_RX,
    I2C_API_STATE_WAIT_RX_DMA,
    I2C_API_STATE_WAIT_RX_SINGLE
} I2C_API_State;

/*
 * CH32X035 backend:
 *   I2C1 PA13=SCL, PA14=SDA @ 400 kHz (I2C1_RM=001)
 *   DMA1 Channel6 = I2C_TX
 *   DMA1 Channel7 = I2C_RX
 *
 * Transactions are zero-copy. tx/rx buffers must remain valid until the owner
 * consumes the terminal result with I2C_API_TakeResult(). OK is published
 * only after STOP/START/BUSY/MSL have cleared. Foreground callers are
 * cooperative (not concurrent threads/ISRs).
 */
void I2C_API_Init(uint32_t clock_hz);
uint32_t I2C_API_GetClockHz(void);

uint8_t I2C_API_TryPing(I2C_API_Owner owner, uint8_t address_7bit);
uint8_t I2C_API_TryWrite(I2C_API_Owner owner,
                         uint8_t address_7bit,
                         const uint8_t *tx,
                         uint16_t tx_len);
uint8_t I2C_API_TryWriteRead(I2C_API_Owner owner,
                             uint8_t address_7bit,
                             const uint8_t *tx,
                             uint16_t tx_len,
                             uint8_t *rx,
                             uint16_t rx_len);

/* Run from a separate low-cost coroOS watchdog thread.  This is the only
 * foreground I2C call: it owns timeout detection, bus recovery and the rare
 * retry of a transaction whose START had to wait for a busy bus. */
void I2C_API_WatchdogService(uint32_t now_ms);

uint8_t I2C_API_IsBusy(void);
I2C_API_Owner I2C_API_GetOwner(void);
I2C_API_Result I2C_API_GetResult(I2C_API_Owner owner);
I2C_API_Result I2C_API_TakeResult(I2C_API_Owner owner);
/* Transaction engine handlers; called only by APP/ch32x035_it.c wrappers. */
void I2C_API_EV_IRQHandler(void);
void I2C_API_ER_IRQHandler(void);
void I2C_API_TxDMA_IRQHandler(void);
void I2C_API_RxDMA_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_API_H_ */
