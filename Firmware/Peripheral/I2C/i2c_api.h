#ifndef I2C_API_H_
#define I2C_API_H_

#include <stdint.h>
#include "main.h"  /* DPC_I2C_SNAPSHOT_ACTIVE / DPC_I2C_FIRST_FAULT_TRACE_ENABLE */

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

#if DPC_I2C_SNAPSHOT_ACTIVE
/* Event source recorded before cleanup, never inferred from post-recovery pins. */
typedef enum
{
    I2C_API_RECOVERY_NONE = 0,
    I2C_API_RECOVERY_TIMEOUT = 1,
    I2C_API_RECOVERY_EVENT_STORM = 2,
    I2C_API_RECOVERY_DMA_ERROR = 3,
    I2C_API_RECOVERY_I2C_ERROR = 4
} I2C_API_RecoveryCause;

typedef struct
{
    I2C_API_Error error;
    I2C_API_State state;
    I2C_API_Owner owner;
    I2C_API_Result result;
    uint16_t star1;
    uint16_t star2;
    uint16_t dma_tx_remaining;
    uint16_t dma_rx_remaining;
    uint8_t scl_high;
    uint8_t sda_high;
    uint32_t transaction_age_ms;
    uint32_t recovery_count;
    /* Normally pre-cleanup; STOP timeout is captured after DMA shutdown. */
    I2C_API_RecoveryCause cause;
    uint8_t address_7bit;
    uint16_t tx_len;
    uint16_t rx_len;
    uint16_t i2c_ctlr2;
    uint32_t dma_rx_cfgr;
    uint32_t dma_intfr;
    uint8_t fault_scl_high;
    uint8_t fault_sda_high;
} I2C_API_Diagnostic;
#endif /* DPC_I2C_SNAPSHOT_ACTIVE */

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
/* Diagnostic-only status API: completely omitted in production builds. */
#if DPC_I2C_SNAPSHOT_ACTIVE
uint32_t I2C_API_GetRecoveryCount(void);
I2C_API_Error I2C_API_GetLastError(void);
const char *I2C_API_GetLastErrorName(void);
void I2C_API_GetDiagnostic(I2C_API_Diagnostic *diag);
#endif

/* One-shot, debug-build-only latch, written at the FIRST unsuccessful
 * transaction before cleanup; never read/print peripherals from the UI.
 * Snapshot is immutable after valid=1, so readers need no IRQ masking.
 * The last failure and full diagnostic snapshot remain separate features. */
#if DPC_I2C_FIRST_FAULT_TRACE_ENABLE
typedef struct
{
    uint32_t time_ms;
    uint32_t dma_intfr;
    uint16_t star1;
    uint16_t star2;
    uint16_t ctlr2;
    uint16_t tx_remaining;
    uint16_t rx_remaining;
    uint8_t owner;
    uint8_t state;
    uint8_t result;
    uint8_t error;
    uint8_t event_storm;
} I2C_API_FirstFault;

uint8_t I2C_API_ReadFirstFault(I2C_API_FirstFault *out);
#endif

/* Transaction engine handlers; called only by APP/ch32x035_it.c wrappers. */
void I2C_API_EV_IRQHandler(void);
void I2C_API_ER_IRQHandler(void);
void I2C_API_TxDMA_IRQHandler(void);
void I2C_API_RxDMA_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_API_H_ */
