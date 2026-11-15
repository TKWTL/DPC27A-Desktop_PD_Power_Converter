#include "i2c_api.h"
#include "main.h"
#include "ch32x035.h"
#include "debug.h"
#include "time_api.h"

#define I2C_API_DMA_TX_CHANNEL        DMA1_Channel6
#define I2C_API_DMA_RX_CHANNEL        DMA1_Channel7
#define I2C_API_ERROR_MASK            (I2C_STAR1_AF | I2C_STAR1_BERR | I2C_STAR1_ARLO | I2C_STAR1_OVR)
#define I2C_API_BUS_RECOVERY_PULSE_US 5u
#define I2C_API_BUS_FREE_SPIN_US      250u
/* CH32X035 Fast I2C specifies tBUF (STOP->START) >= 1.2 us.
 * Two microseconds after BUSY clears gives a bounded compliant margin. */
#define I2C_API_BUS_FREE_MIN_US       2u
/* Repeated START must assert SB before BTF-triggered EV IRQs are enabled. */
#define I2C_API_RESTART_SPIN_US       250u
/* STOP completion is a hardware handshake, bounded by the free-running timer. */
#define I2C_API_STOP_SPIN_US          250u
/* All four I2C-owned PFIC vectors are in word 0 on CH32X035 (27,28,30,31). */
#define I2C_API_IRQ_MASK              ((1UL << DMA1_Channel6_IRQn) | (1UL << DMA1_Channel7_IRQn) | \
                                       (1UL << I2C1_EV_IRQn) | (1UL << I2C1_ER_IRQn))

/* 事件中断风暴熔断值：同一状态连续被 EV 中断打断超过该次数就强制中止。
 * （正常一条事务的状态切换都伴随 set_state()，绝不会连续同状态打断。） */
#define I2C_API_EV_STORM_LIMIT        64u

typedef struct
{
    volatile I2C_API_State state;
    volatile I2C_API_Owner owner;
    volatile I2C_API_Result result;
    uint8_t address_7bit;
    const uint8_t *tx;
    uint16_t tx_len;
    uint8_t *rx;
    uint16_t rx_len;
    volatile uint32_t last_progress_ms;
} I2C_API_Transaction;

static uint32_t s_clock_hz = I2C_API_DEFAULT_CLOCK_HZ;
static I2C_API_Transaction s_xfer;
static volatile uint8_t s_recovery_requested;

#if DPC_I2C_FIRST_FAULT_TRACE_ENABLE
/* Claim before filling; publish ready last. First-failure-only instrumentation
 * doesn't alter the successful I2C transaction path beyond a conditional. */
static volatile uint8_t s_first_fault_claimed;
static volatile uint8_t s_first_fault_ready;
static I2C_API_FirstFault s_first_fault;
#endif

#if DPC_I2C_SNAPSHOT_ACTIVE
static volatile I2C_API_Error s_last_error = I2C_API_ERR_NONE;
static volatile uint16_t s_last_star1;
static volatile uint16_t s_last_star2;
static volatile uint32_t s_recovery_count;
/* Fault-site snapshot survives DMA shutdown and I2C1 peripheral reset. */
static volatile I2C_API_RecoveryCause s_fault_cause;
static volatile I2C_API_State s_fault_state;
static volatile I2C_API_Owner s_fault_owner;
static volatile I2C_API_Result s_fault_result;
static volatile uint8_t s_fault_addr;
static volatile uint16_t s_fault_tx_len, s_fault_rx_len;
static volatile uint16_t s_fault_tx_cnt, s_fault_rx_cnt;
static volatile uint16_t s_fault_ctlr2;
static volatile uint32_t s_fault_dma_rx_cfgr, s_fault_dma_intfr;
static volatile uint32_t s_fault_age_ms;
static volatile uint8_t s_fault_scl, s_fault_sda;
#endif /* DPC_I2C_SNAPSHOT_ACTIVE */

/*
 * The watchdog measures inactivity in the current hardware state, not total
 * wall-clock transaction lifetime.  Each interrupt-driven state change
 * refreshes the timestamp, so a slow-but-progressing transfer is not killed
 * while a genuine bus stall still times out.
 */
static void set_state(I2C_API_State state)
{
    s_xfer.last_progress_ms = TIME_Millis();
    s_xfer.state = state;
}

/* The ISR producers have the same preemption level; only the foreground
 * needs this lock. Do not mask USBPD or SysTick. Preserve PFIC enable state. */
static uint32_t i2c_lock(void)
{
    uint32_t enabled = NVIC->ISR[0] & I2C_API_IRQ_MASK;
    NVIC->IRER[0] = I2C_API_IRQ_MASK;
    __asm__ volatile ("fence iorw, iorw\n\tfence.i" ::: "memory");
    return enabled;
}

static void i2c_unlock(uint32_t enabled)
{
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    NVIC->IENR[0] = enabled;
}

static void i2c_fence(void)
{
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
}

static void gpio_input_release(GPIO_TypeDef *port, uint32_t pin)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &gpio);
}

static void gpio_drive_low(GPIO_TypeDef *port, uint32_t pin)
{
    GPIO_InitTypeDef gpio = {0};
    GPIO_ResetBits(port, pin);
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &gpio);
    GPIO_ResetBits(port, pin);
}

static void i2c_gpio_af(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin = PWR_I2C_SCL_Pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PWR_I2C_SCL_GPIO_Port, &gpio);

    gpio.GPIO_Pin = PWR_I2C_SDA_Pin;
    GPIO_Init(PWR_I2C_SDA_GPIO_Port, &gpio);
}

static void i2c_hw_init(void)
{
    I2C_InitTypeDef i2c = {0};

    RCC_APB2PeriphClockCmd(PWR_I2C_SCL_GPIO_CLK | PWR_I2C_SDA_GPIO_CLK | RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(PWR_I2C_REMAP, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    i2c_gpio_af();

    I2C_DeInit(I2C1);
    i2c.I2C_ClockSpeed = s_clock_hz;
    i2c.I2C_Mode = I2C_Mode_I2C;
    i2c.I2C_DutyCycle = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1 = 0x00;
    i2c.I2C_Ack = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &i2c);
    I2C_Cmd(I2C1, ENABLE);
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Current);
    I2C_DMACmd(I2C1, DISABLE);
    I2C_DMALastTransferCmd(I2C1, DISABLE);
    I2C_ITConfig(I2C1, (uint16_t)(I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR), DISABLE);

    DMA_Cmd(I2C_API_DMA_TX_CHANNEL, DISABLE);
    DMA_Cmd(I2C_API_DMA_RX_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL6 | DMA1_FLAG_GL7);
}

#if DPC_I2C_SNAPSHOT_ACTIVE
static void snapshot(I2C_API_Error error, I2C_API_RecoveryCause cause,
                     I2C_API_Result result, uint16_t star1, uint16_t star2)
{
    /* Do not read DATAR or log inside IRQs; retain the unmodified register
     * scene before STOP/DMA cleanup, including on event-storm aborts. */
    s_last_error = error;
    s_fault_cause = cause;
    s_fault_state = s_xfer.state;
    s_fault_owner = s_xfer.owner;
    s_fault_result = result;
    s_fault_addr = s_xfer.address_7bit;
    s_fault_tx_len = s_xfer.tx_len;
    s_fault_rx_len = s_xfer.rx_len;
    s_last_star1 = star1;
    s_last_star2 = star2;
    s_fault_ctlr2 = I2C1->CTLR2;
    s_fault_tx_cnt = (uint16_t)DMA_GetCurrDataCounter(I2C_API_DMA_TX_CHANNEL);
    s_fault_rx_cnt = (uint16_t)DMA_GetCurrDataCounter(I2C_API_DMA_RX_CHANNEL);
    s_fault_dma_rx_cfgr = I2C_API_DMA_RX_CHANNEL->CFGR;
    s_fault_dma_intfr = DMA1->INTFR;
    s_fault_scl = GPIO_ReadInputDataBit(PWR_I2C_SCL_GPIO_Port, PWR_I2C_SCL_Pin) ? 1u : 0u;
    s_fault_sda = GPIO_ReadInputDataBit(PWR_I2C_SDA_GPIO_Port, PWR_I2C_SDA_Pin) ? 1u : 0u;
    s_fault_age_ms = (s_xfer.result == I2C_API_RESULT_ACTIVE) ?
                     (uint32_t)(TIME_Millis() - s_xfer.last_progress_ms) : 0u;
}
#endif /* DPC_I2C_SNAPSHOT_ACTIVE */

#if DPC_I2C_FIRST_FAULT_TRACE_ENABLE
static void latch_first_fault(I2C_API_Error error, I2C_API_Result result,
                              uint8_t event_storm, uint16_t star1, uint16_t star2)
{
    if(s_first_fault_claimed != 0u)
        return;

    /* ISR producers are non-preempting peers; foreground finish holds the
     * I2C PFIC lock. Set claimed first, ready last. No UART in IRQ paths. */
    s_first_fault_claimed = 1u;
    s_first_fault.time_ms = TIME_Millis();
    s_first_fault.owner = (uint8_t)s_xfer.owner;
    s_first_fault.state = (uint8_t)s_xfer.state;
    s_first_fault.result = (uint8_t)result;
    s_first_fault.error = (uint8_t)error;
    s_first_fault.event_storm = event_storm;
    s_first_fault.star1 = star1;
    s_first_fault.star2 = star2;
    s_first_fault.ctlr2 = I2C1->CTLR2;
    s_first_fault.dma_intfr = DMA1->INTFR;
    s_first_fault.tx_remaining = (uint16_t)DMA_GetCurrDataCounter(I2C_API_DMA_TX_CHANNEL);
    s_first_fault.rx_remaining = (uint16_t)DMA_GetCurrDataCounter(I2C_API_DMA_RX_CHANNEL);
    /* Make all recorded fields visible before the ready flag is published. */
    __asm__ volatile ("" ::: "memory");
    s_first_fault_ready = 1u;
}
#endif

static void clear_i2c_error_flags(void)
{
    if((I2C1->STAR1 & I2C_STAR1_AF) != 0u)   I2C_ClearFlag(I2C1, I2C_FLAG_AF);
    if((I2C1->STAR1 & I2C_STAR1_BERR) != 0u) I2C_ClearFlag(I2C1, I2C_FLAG_BERR);
    if((I2C1->STAR1 & I2C_STAR1_ARLO) != 0u) I2C_ClearFlag(I2C1, I2C_FLAG_ARLO);
    if((I2C1->STAR1 & I2C_STAR1_OVR) != 0u)  I2C_ClearFlag(I2C1, I2C_FLAG_OVR);
}

static void stop_dma(void)
{
    I2C_DMACmd(I2C1, DISABLE);
    DMA_Cmd(I2C_API_DMA_TX_CHANNEL, DISABLE);
    DMA_Cmd(I2C_API_DMA_RX_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL6 | DMA1_FLAG_GL7);
    /* Keep LAST until STOP is confirmed (or the peripheral is reset). */
}

/* 只复位 I2C 外设本身（不含 GPIO 位翻转恢复）：清掉所有标志与状态机。
 *
 * SB / BTF 这两个事件标志只能靠“读/写 DR”清 —— 那会往总线发真实数据，
 * 绝不能为了清标志去做；外设复位（RCC 复位 + 重新初始化）是唯一安全且
 * 彻底的办法。 */
static void i2c_peripheral_reset(void)
{
    I2C_ITConfig(I2C1, (uint16_t)(I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR), DISABLE);
    stop_dma();
    I2C1->CTLR1 = 0u; /* reset path: don't replay pending START/STOP */
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);
    TIME_DelayUs(2u);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);
    i2c_hw_init();
}

/* Only use STAR2 for idle checks outside an active data/address phase:
 * STAR1 -> STAR2 can clear ADDR, so do not poll it during normal transfer. */
static uint8_t i2c_bus_idle(void)
{
    uint16_t ctlr1 = I2C1->CTLR1;
    return (((ctlr1 & (I2C_CTLR1_PE | I2C_CTLR1_START | I2C_CTLR1_STOP)) == I2C_CTLR1_PE) &&
            ((I2C1->STAR2 & (I2C_STAR2_BUSY | I2C_STAR2_MSL)) == 0u)) ? 1u : 0u;
}

static uint8_t wait_idle_us(uint32_t us)
{
    uint32_t start = TIME_Ticks32();
    uint32_t limit = TIME_UsToTicks(us);
    while(!i2c_bus_idle())
    {
        if((uint32_t)(TIME_Ticks32() - start) >= limit)
            return 0u;
    }
    return 1u;
}

/* Called under the foreground I2C lock, with all peripheral sources off.
 * ADDR is cleared by STAR1/STAR2; STOPF is NOT. A master-only engine must
 * never write a dummy DATAR byte to remove stale events. Reset if necessary. */
static uint8_t i2c_prepare_start(void)
{
    uint16_t star1;
    if(!i2c_bus_idle())
        return 0u;

    star1 = I2C1->STAR1;
    (void)I2C1->STAR2;
    if((star1 & (I2C_STAR1_SB | I2C_STAR1_ADDR | I2C_STAR1_BTF |
                 I2C_STAR1_STOPF | I2C_STAR1_RXNE | I2C_API_ERROR_MASK)) != 0u)
    {
        i2c_peripheral_reset();
        TIME_DelayUs(I2C_API_BUS_FREE_MIN_US);
    }
    if(!i2c_bus_idle())
        return 0u;

    /* Peripheral sources have been disabled; clear old PFIC pending bits
     * before publishing START. Never clear pending after the new START. */
    NVIC->IPRR[0] = I2C_API_IRQ_MASK;
    i2c_fence();
    return 1u;
}

/* Master-only CTLR1 configuration: PE on, POS=0, no SMBus/PEC options.
 * Do not read-modify-write self-clearing START/STOP bits. */
static void request_stop(uint16_t ack)
{
    I2C1->CTLR1 = (uint16_t)(I2C_CTLR1_PE | (ack & I2C_CTLR1_ACK) |
                             I2C_CTLR1_STOP);
}

static I2C_API_Error error_from_star1(uint16_t star1)
{
    /* Fatal bus errors win over a coincident NACK. */
    if(star1 & I2C_STAR1_ARLO) return I2C_API_ERR_ARLO;
    if(star1 & I2C_STAR1_BERR) return I2C_API_ERR_BERR;
    if(star1 & I2C_STAR1_OVR)  return I2C_API_ERR_OVR;
    if(star1 & I2C_STAR1_AF)   return I2C_API_ERR_NACK;
    return I2C_API_ERR_NONE;
}

static void record_terminal(I2C_API_Result result, I2C_API_Error error,
                            uint8_t event_storm, uint16_t star1, uint16_t star2)
{
#if DPC_I2C_FIRST_FAULT_TRACE_ENABLE
    if(error != I2C_API_ERR_NONE)
        latch_first_fault(error, result, event_storm, star1, star2);
#endif
#if DPC_I2C_SNAPSHOT_ACTIVE
    if(error != I2C_API_ERR_NONE)
    {
        I2C_API_RecoveryCause cause = I2C_API_RECOVERY_I2C_ERROR;
        if(event_storm)
            cause = I2C_API_RECOVERY_EVENT_STORM;
        else if(result == I2C_API_RESULT_TIMEOUT)
            cause = I2C_API_RECOVERY_TIMEOUT;
        else if((error == I2C_API_ERR_TX_DMA) || (error == I2C_API_ERR_RX_DMA))
            cause = I2C_API_RECOVERY_DMA_ERROR;
        snapshot(error, cause, result, star1, star2);
    }
    else
    {
        s_last_error = I2C_API_ERR_NONE;
        s_last_star1 = star1;
        s_last_star2 = star2;
    }
#endif
    (void)result;
    (void)error;
    (void)event_storm;
    (void)star1;
    (void)star2;
}

/* One terminal hardware owner. ISR peers cannot preempt each other; the
 * watchdog calls this while holding the I2C-only PFIC lock. */
static void finish_impl(I2C_API_Result result, I2C_API_Error error,
                        uint8_t request_recovery, uint8_t event_storm)
{
    uint16_t star1;
    uint16_t star2;
    uint16_t ctlr1;
    uint8_t early_stop;

    if(s_xfer.result != I2C_API_RESULT_ACTIVE)
        return;

    /* Both diagnostic and lean builds perform the same ordered MMIO reads. */
    star1 = I2C1->STAR1;
    star2 = I2C1->STAR2;
    ctlr1 = I2C1->CTLR1;
    early_stop = (s_xfer.state == I2C_API_STATE_WAIT_RX_SINGLE) ? 1u : 0u;

    /* A successful DMA completion is not sufficient if an I2C error is
     * pending. ER and DMA IRQs have different subpriorities. */
    if(result == I2C_API_RESULT_OK && (star1 & I2C_API_ERROR_MASK) != 0u)
    {
        error = error_from_star1(star1);
        result = (error == I2C_API_ERR_NACK) ? I2C_API_RESULT_NACK : I2C_API_RESULT_ERROR;
        if(error != I2C_API_ERR_NACK)
            request_recovery = 1u;
    }

    /* A TX/RX-DMA success needs ownership of the bus before the sole STOP.
     * The one-byte path already issued STOP before reading RXNE. */
    if((result == I2C_API_RESULT_OK && !early_stop &&
        ((star2 & I2C_STAR2_MSL) == 0u || (ctlr1 & I2C_CTLR1_STOP) != 0u)) ||
       (result == I2C_API_RESULT_TIMEOUT &&
        (s_xfer.state == I2C_API_STATE_WAIT_ADDR_TX || s_xfer.state == I2C_API_STATE_WAIT_ADDR_RX) &&
        (star1 & I2C_API_ERROR_MASK) == 0u &&
        (star2 & (I2C_STAR2_BUSY | I2C_STAR2_MSL)) == 0u &&
        (ctlr1 & (I2C_CTLR1_START | I2C_CTLR1_STOP)) == 0u))
    {
        if(result == I2C_API_RESULT_OK)
            result = I2C_API_RESULT_ERROR;
        error = I2C_API_ERR_BUS_STATE;
        request_recovery = 1u;
    }

    record_terminal(result, error, event_storm, star1, star2);
    I2C_ITConfig(I2C1, (uint16_t)(I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR), DISABLE);
    stop_dma();

    /* STOP may already be in flight (one-byte RX or hardware error).
     * Do not replay it and do not modify ACK/POS while it is pending. On
     * arbitration loss we do not own the bus; never generate a foreign STOP. */
    if(!early_stop)
    {
        /* Recheck live ownership immediately before issuing the command. */
        uint16_t live1 = I2C1->STAR1;
        uint16_t live2 = I2C1->STAR2;
        ctlr1 = I2C1->CTLR1;
        if((live2 & I2C_STAR2_MSL) != 0u &&
           ((star1 | live1) & I2C_STAR1_ARLO) == 0u)
        {
            if((ctlr1 & (I2C_CTLR1_START | I2C_CTLR1_STOP)) == 0u)
                request_stop(ctlr1);
            else if((ctlr1 & I2C_CTLR1_START) != 0u)
                request_recovery = 1u;
        }
        else if(result == I2C_API_RESULT_OK)
        {
            error = error_from_star1(live1);
            if(error == I2C_API_ERR_NONE)
                error = I2C_API_ERR_BUS_STATE;
            result = (error == I2C_API_ERR_NACK) ? I2C_API_RESULT_NACK : I2C_API_RESULT_ERROR;
            request_recovery = 1u;
            record_terminal(result, error, event_storm, live1, live2);
        }
    }

    /* No normal success or NACK is published until hardware actually
     * finishes STOP. Known fatal errors can go straight to recovery. */
    if(!request_recovery)
    {
        if(!wait_idle_us(I2C_API_STOP_SPIN_US))
        {
            star1 = I2C1->STAR1;
            star2 = I2C1->STAR2;
            result = I2C_API_RESULT_TIMEOUT;
            error = I2C_API_ERR_STOP;
            request_recovery = 1u;
            /* This late failure is captured after DMA shutdown, before reset. */
            record_terminal(result, error, event_storm, star1, star2);
        }
        else
        {
            I2C_API_Error late_error;
            I2C_DMALastTransferCmd(I2C1, DISABLE);
            star1 = I2C1->STAR1;
            late_error = error_from_star1(star1);
            /* Don't clear an error that arrived while STOP was in flight
             * and then publish OK. Preserve a previously recorded NACK. */
            if(late_error != I2C_API_ERR_NONE &&
               (result == I2C_API_RESULT_OK || late_error != I2C_API_ERR_NACK))
            {
                star2 = I2C1->STAR2;
                result = (late_error == I2C_API_ERR_NACK) ? I2C_API_RESULT_NACK : I2C_API_RESULT_ERROR;
                error = late_error;
                if(error != I2C_API_ERR_NACK)
                    request_recovery = 1u;
                record_terminal(result, error, event_storm, star1, star2);
            }
        }
    }
    if(request_recovery && result == I2C_API_RESULT_OK)
    {
        result = I2C_API_RESULT_ERROR;
        error = I2C_API_ERR_BUS_STATE;
        record_terminal(result, error, event_storm, star1, star2);
    }

    clear_i2c_error_flags();
    (void)I2C1->STAR1;
    (void)I2C1->STAR2;
    /* Quiescent I2C and DMA: discard stale, not newly-created, IRQs. */
    i2c_fence();
    NVIC->IPRR[0] = I2C_API_IRQ_MASK;
    if(request_recovery)
        s_recovery_requested = 1u;
    s_xfer.state = I2C_API_STATE_IDLE;
    i2c_fence(); /* DMA writes and cleanup precede terminal result publication. */
    s_xfer.result = result;
}

static void finish(I2C_API_Result result, I2C_API_Error error, uint8_t request_recovery)
{
    finish_impl(result, error, request_recovery, 0u);
}

static void start_transaction(void)
{
    set_state(I2C_API_STATE_WAIT_START_TX);
    i2c_fence();
    /* Previous STOP/START cleared and bus free. Restore ACK and POS=0
     * as part of a single command write; arm EV only after requesting START. */
    I2C1->CTLR1 = (uint16_t)(I2C_CTLR1_PE | I2C_CTLR1_ACK | I2C_CTLR1_START);
    I2C_ITConfig(I2C1, (uint16_t)(I2C_IT_EVT | I2C_IT_ERR), ENABLE);
}

static uint8_t wait_bus_free(void)
{
    if(!wait_idle_us(I2C_API_BUS_FREE_SPIN_US))
        return 0u;
    TIME_DelayUs(I2C_API_BUS_FREE_MIN_US);
    return i2c_bus_idle();
}

static uint8_t begin(I2C_API_Owner owner,
                     uint8_t address_7bit,
                     const uint8_t *tx,
                     uint16_t tx_len,
                     uint8_t *rx,
                     uint16_t rx_len)
{
    uint32_t irq_enabled;

    if(owner == I2C_API_OWNER_NONE || address_7bit > 0x7Fu)
        return 0u;
    if(s_xfer.owner != I2C_API_OWNER_NONE || s_xfer.result != I2C_API_RESULT_IDLE)
        return 0u;
    if(s_recovery_requested)
        return 0u;
    if(tx_len != 0u && tx == 0)
        return 0u;
    if(rx_len != 0u && rx == 0)
        return 0u;

    irq_enabled = i2c_lock();
    s_xfer.owner = owner;
    s_xfer.address_7bit = address_7bit;
    s_xfer.tx = tx;
    s_xfer.tx_len = tx_len;
    s_xfer.rx = rx;
    s_xfer.rx_len = rx_len;
    set_state(I2C_API_STATE_WAIT_BUS_IDLE);
    i2c_fence(); /* publish descriptor and sender buffer before ACTIVE */
    s_xfer.result = I2C_API_RESULT_ACTIVE;
#if DPC_I2C_SNAPSHOT_ACTIVE
    s_last_error = I2C_API_ERR_NONE;
#endif

    if(wait_bus_free() && i2c_prepare_start())
    {
        start_transaction();
    }
    i2c_unlock(irq_enabled);
    return 1u;
}

static void start_tx_dma(void)
{
    DMA_InitTypeDef dma = {0};

    DMA_Cmd(I2C_API_DMA_TX_CHANNEL, DISABLE);
    DMA_DeInit(I2C_API_DMA_TX_CHANNEL);
    DMA_ClearFlag(DMA1_FLAG_GL6);

    dma.DMA_PeripheralBaseAddr = (uint32_t)&I2C1->DATAR;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_xfer.tx;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = s_xfer.tx_len;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(I2C_API_DMA_TX_CHANNEL, &dma);

    I2C_DMALastTransferCmd(I2C1, DISABLE);
    DMA_ITConfig(I2C_API_DMA_TX_CHANNEL, (uint32_t)(DMA_IT_TC | DMA_IT_TE), ENABLE);
    set_state(I2C_API_STATE_WAIT_TX_DMA);
    i2c_fence();
    DMA_Cmd(I2C_API_DMA_TX_CHANNEL, ENABLE);
    I2C_DMACmd(I2C1, ENABLE);
}

static void start_rx_dma(void)
{
    DMA_InitTypeDef dma = {0};

    DMA_Cmd(I2C_API_DMA_RX_CHANNEL, DISABLE);
    DMA_DeInit(I2C_API_DMA_RX_CHANNEL);
    DMA_ClearFlag(DMA1_FLAG_GL7);

    dma.DMA_PeripheralBaseAddr = (uint32_t)&I2C1->DATAR;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_xfer.rx;
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = s_xfer.rx_len;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(I2C_API_DMA_RX_CHANNEL, &dma);

    /* LAST makes the peripheral NACK the final DMA byte. */
    /* ACK already set during SB, before the receiver address. */
    I2C_DMALastTransferCmd(I2C1, ENABLE);
    DMA_ITConfig(I2C_API_DMA_RX_CHANNEL, (uint32_t)(DMA_IT_TC | DMA_IT_TE), ENABLE);
    set_state(I2C_API_STATE_WAIT_RX_DMA);
    i2c_fence();
    DMA_Cmd(I2C_API_DMA_RX_CHANNEL, ENABLE);
    I2C_DMACmd(I2C1, ENABLE);
}

/*
 * ADDR is cleared by the mandatory STAR1 -> STAR2 read sequence. Do this
 * explicitly instead of I2C_CheckEvent(): the generic event helper reads both
 * status registers internally, which hides the exact clear point from the DMA
 * receive state machine.
 */
static void clear_addr_flag(void)
{
    volatile uint16_t star1;
    volatile uint16_t star2;

    star1 = I2C1->STAR1;
    star2 = I2C1->STAR2;
    (void)star1;
    (void)star2;
}

static uint8_t handle_error_flags(void)
{
    I2C_API_Error error = error_from_star1(I2C1->STAR1);
    if(error == I2C_API_ERR_NONE)
        return 0u;

    if(error == I2C_API_ERR_NACK)
        finish(I2C_API_RESULT_NACK, error, 0u);
    else
        finish(I2C_API_RESULT_ERROR, error, 1u);
    return 1u;
}

static I2C_API_Error timeout_error_for_state(I2C_API_State state)
{
    switch(state)
    {
        case I2C_API_STATE_WAIT_BUS_IDLE: return I2C_API_ERR_BUS_BUSY;
        case I2C_API_STATE_WAIT_START_TX: return I2C_API_ERR_START_TX;
        case I2C_API_STATE_WAIT_ADDR_TX:  return I2C_API_ERR_ADDR_TX;
        case I2C_API_STATE_WAIT_TX_DMA:   return I2C_API_ERR_TX_DMA;
        case I2C_API_STATE_WAIT_TX_BTF:   return I2C_API_ERR_TX_BTF;
        case I2C_API_STATE_WAIT_START_RX: return I2C_API_ERR_START_RX;
        case I2C_API_STATE_WAIT_ADDR_RX:  return I2C_API_ERR_ADDR_RX;
        case I2C_API_STATE_WAIT_RX_DMA:   return I2C_API_ERR_RX_DMA;
        case I2C_API_STATE_WAIT_RX_SINGLE:return I2C_API_ERR_RX;
        default:                           return I2C_API_ERR_WATCHDOG;
    }
}

static void bus_recover(void)
{
    uint8_t i;

    I2C_ITConfig(I2C1, (uint16_t)(I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR), DISABLE);
    stop_dma();
    I2C1->CTLR1 = 0u; /* reset path: don't replay pending START/STOP */
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);
    TIME_DelayUs(2u);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);

    /* Emulate open-drain recovery: output-low means asserted, floating input
     * means released to the board pull-up. Never actively drive SDA/SCL high. */
    gpio_input_release(PWR_I2C_SCL_GPIO_Port, PWR_I2C_SCL_Pin);
    gpio_input_release(PWR_I2C_SDA_GPIO_Port, PWR_I2C_SDA_Pin);
    TIME_DelayUs(I2C_API_BUS_RECOVERY_PULSE_US);

    if(GPIO_ReadInputDataBit(PWR_I2C_SDA_GPIO_Port, PWR_I2C_SDA_Pin) == Bit_RESET)
    {
        for(i = 0u; i < 9u; ++i)
        {
            gpio_drive_low(PWR_I2C_SCL_GPIO_Port, PWR_I2C_SCL_Pin);
            TIME_DelayUs(I2C_API_BUS_RECOVERY_PULSE_US);
            gpio_input_release(PWR_I2C_SCL_GPIO_Port, PWR_I2C_SCL_Pin);
            TIME_DelayUs(I2C_API_BUS_RECOVERY_PULSE_US);
            if(GPIO_ReadInputDataBit(PWR_I2C_SDA_GPIO_Port, PWR_I2C_SDA_Pin) != Bit_RESET)
                break;
        }
    }

    /* STOP-like release: SDA low while SCL released, then release SDA. */
    gpio_drive_low(PWR_I2C_SDA_GPIO_Port, PWR_I2C_SDA_Pin);
    TIME_DelayUs(I2C_API_BUS_RECOVERY_PULSE_US);
    gpio_input_release(PWR_I2C_SCL_GPIO_Port, PWR_I2C_SCL_Pin);
    TIME_DelayUs(I2C_API_BUS_RECOVERY_PULSE_US);
    gpio_input_release(PWR_I2C_SDA_GPIO_Port, PWR_I2C_SDA_Pin);
    TIME_DelayUs(I2C_API_BUS_RECOVERY_PULSE_US);

    i2c_hw_init();
    NVIC->IPRR[0] = I2C_API_IRQ_MASK;
    i2c_fence();
#if DPC_I2C_SNAPSHOT_ACTIVE
    s_recovery_count++;
#endif
    s_recovery_requested = 0u;
}

void I2C_API_Init(uint32_t clock_hz)
{
    NVIC_InitTypeDef nvic = {0};

    if(clock_hz == 0u)
        clock_hz = I2C_API_DEFAULT_CLOCK_HZ;
    s_clock_hz = clock_hz;

    s_xfer.state = I2C_API_STATE_IDLE;
    s_xfer.owner = I2C_API_OWNER_NONE;
    s_xfer.result = I2C_API_RESULT_IDLE;
    s_recovery_requested = 0u;
#if DPC_I2C_FIRST_FAULT_TRACE_ENABLE
    s_first_fault_claimed = 0u;
    s_first_fault_ready = 0u;
#endif
#if DPC_I2C_SNAPSHOT_ACTIVE
    s_recovery_count = 0u;
    s_last_error = I2C_API_ERR_NONE;
    s_fault_cause = I2C_API_RECOVERY_NONE;
#endif
    i2c_hw_init();

    /* Event/error and both DMA channels carry the transaction; only the
     * watchdog still runs from a coroutine. */
    nvic.NVIC_IRQChannel = I2C1_EV_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 0u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    nvic.NVIC_IRQChannel = I2C1_ER_IRQn;
    NVIC_Init(&nvic);

    nvic.NVIC_IRQChannel = DMA1_Channel6_IRQn;
    nvic.NVIC_IRQChannelSubPriority = 1u;
    NVIC_Init(&nvic);

    nvic.NVIC_IRQChannel = DMA1_Channel7_IRQn;
    NVIC_Init(&nvic);

#if DPC_I2C_SNAPSHOT_ACTIVE
    s_last_star1 = I2C1->STAR1;
    s_last_star2 = I2C1->STAR2;
#endif
}

uint32_t I2C_API_GetClockHz(void)
{
    return s_clock_hz;
}

uint8_t I2C_API_TryPing(I2C_API_Owner owner, uint8_t address_7bit)
{
    return begin(owner, address_7bit, 0, 0u, 0, 0u);
}

uint8_t I2C_API_TryWrite(I2C_API_Owner owner,
                         uint8_t address_7bit,
                         const uint8_t *tx,
                         uint16_t tx_len)
{
    if(tx_len == 0u)
        return 0u;
    return begin(owner, address_7bit, tx, tx_len, 0, 0u);
}

uint8_t I2C_API_TryWriteRead(I2C_API_Owner owner,
                             uint8_t address_7bit,
                             const uint8_t *tx,
                             uint16_t tx_len,
                             uint8_t *rx,
                             uint16_t rx_len)
{
    if(tx_len == 0u || rx_len == 0u)
        return 0u;
    return begin(owner, address_7bit, tx, tx_len, rx, rx_len);
}

/*
 * Interrupt-driven transaction engine.
 *
 *   I2C1_EV  : SB -> address phase; ADDR -> DMA start / repeated START /
 *              single-byte read; RXNE is not enabled (see WAIT_ADDR_RX).
 *   I2C1_ER  : AF / BERR / ARLO / OVR.
 *   DMA1 CH6 : TX payload done -> bounded BTF wait -> repeated START or STOP.
 *   DMA1 CH7 : RX payload done -> STOP.
 *
 * The former foreground I2C_API_Service() polling loop is gone; only the
 * 10 ms watchdog still runs in a coroutine.
 */
/* 事件标志清不掉时的熔断：关掉全部 I2C 中断源、拉 STOP、请求总线恢复。
 *
 * WCH/STM32 系 I2C 的事件中断是电平触发：只要 SB/ADDR/BTF/STOPF 里有一个
 * 没被清掉，IT_EVT 一开就会无限重入——实测可达 ~10^6 次/秒。I2C1_EV 的
 * 抢占优先级高于 SysTick，所以一旦风暴，1 ms 节拍、IWDG 喂狗全部停摆，
 * 最后表现为“停在某页 → IWDG 复位”。 */
static void i2c_abort_event_storm(I2C_API_Error error)
{
    finish_impl(I2C_API_RESULT_TIMEOUT, error, 1u, 1u);
}

void I2C_API_EV_IRQHandler(void)
{
    static I2C_API_State storm_state = I2C_API_STATE_IDLE;
    static uint32_t storm_entries;
    I2C_API_State state_now = s_xfer.state;
    uint16_t star1;

    /* A stale PFIC event must never abort a completed/deferred transaction. */
    if(s_xfer.result != I2C_API_RESULT_ACTIVE ||
       state_now == I2C_API_STATE_WAIT_BUS_IDLE)
    {
        storm_state = I2C_API_STATE_IDLE;
        storm_entries = 0u;
        I2C_ITConfig(I2C1, (uint16_t)I2C_IT_EVT, DISABLE);
        return;
    }

    /* ---- 风暴熔断：同一状态被事件中断反复打断 => 有标志清不掉 ---- */
    if(state_now == storm_state)
    {
        storm_entries++;
    }
    else
    {
        storm_state = state_now;
        storm_entries = 0u;
    }

    if(storm_entries > I2C_API_EV_STORM_LIMIT)
    {
        storm_entries = 0u;
        i2c_abort_event_storm(timeout_error_for_state(state_now));
        return;
    }

    star1 = I2C1->STAR1;
    if((star1 & I2C_API_ERROR_MASK) != 0u)
    {
        (void)handle_error_flags();
        return;
    }

    if((star1 & I2C_STAR1_SB) != 0u)
    {
        /* SB precedes any address bytes, so STAR2 is safe to read here.
         * Never issue an address into an already-lost master transaction. */
        if((I2C1->STAR2 & I2C_STAR2_MSL) == 0u ||
           (I2C1->CTLR1 & (I2C_CTLR1_START | I2C_CTLR1_STOP)) != 0u)
        {
            finish(I2C_API_RESULT_ERROR, I2C_API_ERR_BUS_STATE, 1u);
            return;
        }

        if(s_xfer.state == I2C_API_STATE_WAIT_START_TX)
        {
            set_state(I2C_API_STATE_WAIT_ADDR_TX);
            I2C_Send7bitAddress(I2C1,
                                (uint8_t)(s_xfer.address_7bit << 1),
                                I2C_Direction_Transmitter);
        }
        else if(s_xfer.state == I2C_API_STATE_WAIT_START_RX)
        {
            /* For a one-byte receive ACK must already be low when ADDR is
             * cleared; DMA receives keep ACK enabled and use LAST. */
            I2C1->CTLR1 = (uint16_t)(I2C_CTLR1_PE |
                                      ((s_xfer.rx_len == 1u) ? 0u : I2C_CTLR1_ACK));
            set_state(I2C_API_STATE_WAIT_ADDR_RX);
            I2C_Send7bitAddress(I2C1,
                                (uint8_t)(s_xfer.address_7bit << 1),
                                I2C_Direction_Receiver);
        }
        else
        {
            /* SB 落在非预期状态：不能用写地址来清（会往总线发错误地址），
             * 直接熔断并让10 ms 看门狗做总线/外设恢复。 */
            i2c_abort_event_storm(I2C_API_ERR_START_TX);
        }
        return;
    }

    if((star1 & I2C_STAR1_ADDR) != 0u)
    {
        if(s_xfer.state == I2C_API_STATE_WAIT_ADDR_TX)
        {
            /* BTF can assert I2C1_EV even with ITBUFEN=0. The TX byte phase
             * belongs to DMA1 CH6, not EV, exactly like RX belongs to CH7.
             * Mask EVT before releasing ADDR, while retaining ERR interrupts.
             * CH6 will re-enable EVT only after the repeated START has SB. */
            I2C_ITConfig(I2C1, (uint16_t)I2C_IT_EVT, DISABLE);
            if(s_xfer.tx_len != 0u)
            {
                /* Arm DMA before releasing ADDR. The receiver path already
                 * used this handshake. */
                start_tx_dma();
                clear_addr_flag();
            }
            else
            {
                /* Public API only allows zero TX length for a ping. */
                clear_addr_flag();
                finish(I2C_API_RESULT_OK, I2C_API_ERR_NONE, 0u);
            }
        }
        else if(s_xfer.state == I2C_API_STATE_WAIT_ADDR_RX)
        {
            if(s_xfer.rx_len == 1u)
            {
                /* Single-byte sequence: ACK=0 -> clear ADDR -> STOP, then
                 * read the byte.  Bounded spin (one byte time at 400 kHz)
                 * instead of ITBUFEN: enabling the buffer interrupt here would
                 * also raise the TXE event, which cannot be cleared without
                 * writing the data register. */
                uint32_t guard = 200000u;

                I2C_ITConfig(I2C1, (uint16_t)I2C_IT_EVT, DISABLE);
                set_state(I2C_API_STATE_WAIT_RX_SINGLE);
                clear_addr_flag();
                request_stop(0u); /* sole STOP for a one-byte RX; keep ACK=0 */

                while(((I2C1->STAR1 & (I2C_STAR1_RXNE | I2C_API_ERROR_MASK)) == 0u) && guard)
                    --guard;

                if(handle_error_flags())
                    return;

                if((I2C1->STAR1 & I2C_STAR1_RXNE) != 0u)
                {
                    s_xfer.rx[0] = I2C_ReceiveData(I2C1);
                    finish(I2C_API_RESULT_OK, I2C_API_ERR_NONE, 0u);
                }
                else
                {
                    finish(I2C_API_RESULT_ERROR, I2C_API_ERR_RX, 1u);
                }
            }
            else
            {
                /* Once ADDR is cleared, the RX data phase is owned entirely
                 * by DMA1 CH7. RXNE/BTF can keep I2C1_EV asserted even after
                 * DMA TC7 is pending. Since EV has higher subpriority than
                 * CH7, an EV interrupt storm can starve the DMA-completion
                 * handler and abort an otherwise complete two-byte read.
                 * Disable only event IRQs BEFORE releasing ADDR; retain ERR
                 * IRQs, DMA TC/TE IRQs and the existing watchdog.
                 * start_transaction() re-enables EV for the next START. */
                I2C_ITConfig(I2C1, (uint16_t)I2C_IT_EVT, DISABLE);
                start_rx_dma();
                clear_addr_flag();
            }
        }
        else
        {
            /* Unexpected ADDR is a state-machine fault, not normal progress. */
            finish(I2C_API_RESULT_ERROR, I2C_API_ERR_BUS_STATE, 1u);
        }
        return;
    }

    /* STOPF is a slave-mode event; STAR2 does not clear it. In this
     * master-only engine recover instead of writing CTLR1 during a command. */
    if((star1 & I2C_STAR1_STOPF) != 0u)
        finish(I2C_API_RESULT_ERROR, I2C_API_ERR_BUS_STATE, 1u);
}

void I2C_API_ER_IRQHandler(void)
{
    if(s_xfer.result != I2C_API_RESULT_ACTIVE ||
       s_xfer.state == I2C_API_STATE_WAIT_BUS_IDLE)
    {
        /* Stray error with no started transaction: just clear it. */
        clear_i2c_error_flags();
        I2C_ClearITPendingBit(I2C1, I2C_IT_PECERR);
        return;
    }

    if(!handle_error_flags())
    {
        /* PECERR is SMBus-only and unused here; clear it so the error
         * interrupt cannot stay asserted. */
        I2C_ClearITPendingBit(I2C1, I2C_IT_PECERR);
    }
}

void I2C_API_TxDMA_IRQHandler(void)
{
    uint32_t guard = 200000u;

    if((s_xfer.result != I2C_API_RESULT_ACTIVE) ||
       (s_xfer.state != I2C_API_STATE_WAIT_TX_DMA))
    {
        /* No transaction owns this channel any more (for example after a
         * watchdog abort): just silence the channel. */
        DMA_ClearITPendingBit(DMA1_IT_TC6);
        DMA_ClearITPendingBit(DMA1_IT_TE6);
        DMA_Cmd(I2C_API_DMA_TX_CHANNEL, DISABLE);
        return;
    }

    if(DMA_GetITStatus(DMA1_IT_TE6) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_TE6);
        finish(I2C_API_RESULT_ERROR, I2C_API_ERR_TX_DMA, 1u);
        return;
    }

    if(DMA_GetITStatus(DMA1_IT_TC6) == RESET)
        return;

    DMA_ClearITPendingBit(DMA1_IT_TC6);
    DMA_Cmd(I2C_API_DMA_TX_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL6);
    I2C_DMACmd(I2C1, DISABLE);

    /* DMA TC means the last byte reached DATAR.  Wait for BTF (bounded, at
     * most one byte time at 400 kHz) so the following repeated START or STOP
     * is positioned after the final ACK bit.
     *
     * BTF 本身也会产生一次事件中断：等待期间先把 EV 源关掉，否则中断会在
     * 这段窗口里冲进来，把 BTF 留成“事务已结束却还挂着的事件标志”
     * （旧代码下一次开 IT_EVT 时就是被它打成风暴的）。 */
    I2C_ITConfig(I2C1, (uint16_t)I2C_IT_EVT, DISABLE);
    while(((I2C1->STAR1 & (I2C_STAR1_BTF | I2C_API_ERROR_MASK)) == 0u) && guard)
        --guard;

    if(handle_error_flags())
        return;

    /* DMA TC only means DATAR was loaded, not that the last bit/ACK left
     * the wire. Never start another transfer or report success without BTF. */
    if((I2C1->STAR1 & I2C_STAR1_BTF) == 0u)
    {
        finish(I2C_API_RESULT_TIMEOUT, I2C_API_ERR_TX_BTF, 1u);
        return;
    }

    if(s_xfer.rx_len != 0u)
    {
        uint32_t start_ticks;
        uint32_t max_ticks = TIME_UsToTicks(I2C_API_RESTART_SPIN_US);

        /* With TX BTF still set, enabling EVT before START/SB recreates a
         * stale-BTF interrupt storm. Arm START with EVT masked; wait for the
         * hardware SB flag before granting the address phase to EV. */
        if((I2C1->STAR2 & I2C_STAR2_MSL) == 0u ||
           (I2C1->CTLR1 & (I2C_CTLR1_START | I2C_CTLR1_STOP)) != 0u)
        {
            finish(I2C_API_RESULT_ERROR, I2C_API_ERR_BUS_STATE, 1u);
            return;
        }
        set_state(I2C_API_STATE_WAIT_START_RX);
        start_ticks = TIME_Ticks32();
        I2C1->CTLR1 = (uint16_t)(I2C_CTLR1_PE | I2C_CTLR1_ACK | I2C_CTLR1_START);
        while(((I2C1->STAR1 & (I2C_STAR1_SB | I2C_API_ERROR_MASK)) == 0u) &&
              ((uint32_t)(TIME_Ticks32() - start_ticks) < max_ticks))
        {
        }
        if(handle_error_flags())
            return;
        if((I2C1->STAR1 & I2C_STAR1_SB) == 0u)
        {
            finish(I2C_API_RESULT_TIMEOUT, I2C_API_ERR_START_RX, 1u);
            return;
        }
        I2C_ITConfig(I2C1, (uint16_t)I2C_IT_EVT, ENABLE);
    }
    else
    {
        finish(I2C_API_RESULT_OK, I2C_API_ERR_NONE, 0u);
    }
}

void I2C_API_RxDMA_IRQHandler(void)
{
    if((s_xfer.result != I2C_API_RESULT_ACTIVE) ||
       (s_xfer.state != I2C_API_STATE_WAIT_RX_DMA))
    {
        DMA_ClearITPendingBit(DMA1_IT_TC7);
        DMA_ClearITPendingBit(DMA1_IT_TE7);
        DMA_Cmd(I2C_API_DMA_RX_CHANNEL, DISABLE);
        return;
    }

    if(DMA_GetITStatus(DMA1_IT_TE7) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_TE7);
        finish(I2C_API_RESULT_ERROR, I2C_API_ERR_RX_DMA, 1u);
        return;
    }

    if(DMA_GetITStatus(DMA1_IT_TC7) == RESET)
        return;

    DMA_ClearITPendingBit(DMA1_IT_TC7);
    DMA_Cmd(I2C_API_DMA_RX_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL7);
    /* Keep LAST/ACK until the common terminal path completes STOP. */
    finish(I2C_API_RESULT_OK, I2C_API_ERR_NONE, 0u);
}

void I2C_API_WatchdogService(uint32_t now_ms)
{
    uint32_t irq_enabled;
    uint32_t progress;

    (void)now_ms; /* caller's sample can be older than an ISR's progress */
    if(s_xfer.result != I2C_API_RESULT_ACTIVE && !s_recovery_requested)
        return;

    irq_enabled = i2c_lock();
    if(s_xfer.result == I2C_API_RESULT_ACTIVE)
    {
        /* Re-sample under the same lock that owns finish. Signed modular
         * difference rejects a newer progress timestamp and handles wrap
         * (all valid transaction ages are far below 2^31 ms). */
        progress = s_xfer.last_progress_ms;
        if((int32_t)(TIME_Millis() - progress) > (int32_t)I2C_API_WATCHDOG_TIMEOUT_MS)
        {
            finish(I2C_API_RESULT_TIMEOUT, timeout_error_for_state(s_xfer.state), 1u);
        }
        else if(s_xfer.state == I2C_API_STATE_WAIT_BUS_IDLE && i2c_bus_idle())
        {
            if(wait_bus_free() && i2c_prepare_start())
                start_transaction();
        }
    }

    if(s_recovery_requested && s_xfer.result != I2C_API_RESULT_ACTIVE)
        bus_recover();
    i2c_unlock(irq_enabled);
}

uint8_t I2C_API_IsBusy(void)
{
    return (s_xfer.owner != I2C_API_OWNER_NONE) ? 1u : 0u;
}

I2C_API_Owner I2C_API_GetOwner(void)
{
    return s_xfer.owner;
}

I2C_API_Result I2C_API_GetResult(I2C_API_Owner owner)
{
    if(owner == I2C_API_OWNER_NONE || s_xfer.owner != owner)
        return I2C_API_RESULT_IDLE;
    I2C_API_Result result = s_xfer.result;
    if(result != I2C_API_RESULT_ACTIVE)
        i2c_fence();
    return result;
}

I2C_API_Result I2C_API_TakeResult(I2C_API_Owner owner)
{
    I2C_API_Result result;

    if(owner == I2C_API_OWNER_NONE || s_xfer.owner != owner)
        return I2C_API_RESULT_IDLE;
    if(s_xfer.result == I2C_API_RESULT_ACTIVE)
        return I2C_API_RESULT_ACTIVE;

    result = s_xfer.result;
    i2c_fence(); /* acquire completed single-byte or DMA RX data */
    s_xfer.state = I2C_API_STATE_IDLE;
    s_xfer.tx = 0;
    s_xfer.rx = 0;
    s_xfer.tx_len = 0u;
    s_xfer.rx_len = 0u;
    s_xfer.result = I2C_API_RESULT_IDLE;
    i2c_fence();
    s_xfer.owner = I2C_API_OWNER_NONE;
    return result;
}

#if DPC_I2C_FIRST_FAULT_TRACE_ENABLE
uint8_t I2C_API_ReadFirstFault(I2C_API_FirstFault *out)
{
    if((out == 0) || (s_first_fault_ready == 0u))
        return 0u;

    /* First fault is immutable once ready; acquire before copying fields. */
    __asm__ volatile ("" ::: "memory");
    *out = s_first_fault;
    return 1u;
}
#endif

#if DPC_I2C_SNAPSHOT_ACTIVE
uint32_t I2C_API_GetRecoveryCount(void)
{
    return s_recovery_count;
}

I2C_API_Error I2C_API_GetLastError(void)
{
    return s_last_error;
}

const char *I2C_API_GetLastErrorName(void)
{
    switch(s_last_error)
    {
        case I2C_API_ERR_NONE:     return "none";
        case I2C_API_ERR_BUS_BUSY: return "bus busy";
        case I2C_API_ERR_START_TX: return "start TX";
        case I2C_API_ERR_ADDR_TX:  return "address TX";
        case I2C_API_ERR_TX_DMA:   return "TX DMA";
        case I2C_API_ERR_TX_BTF:   return "TX BTF";
        case I2C_API_ERR_START_RX: return "start RX";
        case I2C_API_ERR_ADDR_RX:  return "address RX";
        case I2C_API_ERR_RX_DMA:   return "RX DMA";
        case I2C_API_ERR_RX:       return "RX";
        case I2C_API_ERR_NACK:     return "NACK";
        case I2C_API_ERR_BERR:     return "bus error";
        case I2C_API_ERR_ARLO:     return "arbitration lost";
        case I2C_API_ERR_OVR:      return "overrun";
        case I2C_API_ERR_WATCHDOG: return "watchdog timeout";
        case I2C_API_ERR_STOP:     return "STOP timeout";
        case I2C_API_ERR_BUS_STATE:return "bus state lost";
        default:                   return "unknown";
    }
}

void I2C_API_GetDiagnostic(I2C_API_Diagnostic *diag)
{
    if(diag == 0)
        return;

    diag->error = s_last_error;
    diag->state = s_fault_state;
    diag->owner = s_fault_owner;
    diag->result = s_fault_result;
    diag->star1 = s_last_star1;
    diag->star2 = s_last_star2;
    diag->dma_tx_remaining = s_fault_tx_cnt;
    diag->dma_rx_remaining = s_fault_rx_cnt;
    /* These two pins are read after recovery, for comparison only. */
    diag->scl_high = GPIO_ReadInputDataBit(PWR_I2C_SCL_GPIO_Port, PWR_I2C_SCL_Pin) ? 1u : 0u;
    diag->sda_high = GPIO_ReadInputDataBit(PWR_I2C_SDA_GPIO_Port, PWR_I2C_SDA_Pin) ? 1u : 0u;
    diag->transaction_age_ms = s_fault_age_ms;
    diag->recovery_count = s_recovery_count;
    diag->cause = s_fault_cause;
    diag->address_7bit = s_fault_addr;
    diag->tx_len = s_fault_tx_len;
    diag->rx_len = s_fault_rx_len;
    diag->i2c_ctlr2 = s_fault_ctlr2;
    diag->dma_rx_cfgr = s_fault_dma_rx_cfgr;
    diag->dma_intfr = s_fault_dma_intfr;
    diag->fault_scl_high = s_fault_scl;
    diag->fault_sda_high = s_fault_sda;
}
#endif /* DPC_I2C_SNAPSHOT_ACTIVE */
