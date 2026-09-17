#include "spi_dma.h"
#include "main.h"
#include "ch32x035.h"

#define SPI_DMA_TX_CHANNEL      DMA1_Channel3
#define SPI_DMA_TX_TC_IT        DMA1_IT_TC3
#define SPI_DMA_TX_GL_FLAG      DMA1_FLAG_GL3

/* Every SPI flag wait in this file is bounded: this project runs a cooperative
 * scheduler, so an unbounded "wait for hardware" spins would take the PD policy
 * and the UI down with it.  100000 iterations is far longer than any legitimate
 * byte/BSY wait at 12 MHz (us scale) but still finite. */
#define SPI_DMA_FLAG_GUARD      100000u

static volatile uint8_t s_busy;
static volatile uint32_t s_transfer_count;
static SPI_DMA_DoneCallback s_done_callback;

void SPI_DMA_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef spi = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO |
                           RCC_APB2Periph_SPI1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    GPIO_PinRemapConfig(OLED_SPI_REMAP, ENABLE);

    /* SCK + MOSI + hardware NSS.  With SSOE=1 the NSS pin (PA12) is an SPI
     * output: it is asserted while the SPI is enabled and released when the
     * SPI is disabled, so chip-select is no longer a software GPIO. */
    gpio.GPIO_Pin = OLED_SCK_Pin | OLED_MOSI_Pin | OLED_CS_Pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* D/C stays an ordinary GPIO: SH1107 command/data framing is a transport
     * concern, not an SPI hardware function. */
    GPIO_ResetBits(OLED_DC_GPIO_Port, OLED_DC_Pin);
    gpio.GPIO_Pin = OLED_DC_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    GPIO_ResetBits(OLED_DC_GPIO_Port, OLED_DC_Pin);

    SPI_I2S_DeInit(SPI1);
    SPI_StructInit(&spi);
    spi.SPI_Direction = SPI_Direction_1Line_Tx;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Hard;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4; /* 48 MHz / 4 = 12 MHz */
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7U;
    SPI_Init(SPI1, &spi);
    SPI_SSOutputCmd(SPI1, ENABLE);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_Cmd(SPI1, DISABLE);              /* CS released until a page starts */

    DMA_DeInit(SPI_DMA_TX_CHANNEL);
    DMA_ClearFlag(SPI_DMA_TX_GL_FLAG);

    /* USBPD keeps preemption 0 (see APP_IRQ_Init); display DMA is deliberately
     * lower and must match the priority table maintained there. */
    nvic.NVIC_IRQChannel = DMA1_Channel3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 1u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    s_busy = 0U;
    s_transfer_count = 0U;
    s_done_callback = 0;
}

void SPI_DMA_SetDoneCallback(SPI_DMA_DoneCallback callback)
{
    s_done_callback = callback;
}

uint8_t SPI_DMA_TryTransmit(const uint8_t *data, uint16_t length)
{
    DMA_InitTypeDef dma = {0};

    if((data == 0) || (length == 0U) || (s_busy != 0U))
        return 0U;

    DMA_Cmd(SPI_DMA_TX_CHANNEL, DISABLE);
    DMA_DeInit(SPI_DMA_TX_CHANNEL);

    dma.DMA_PeripheralBaseAddr = (uint32_t)(uintptr_t)&SPI1->DATAR;
    dma.DMA_MemoryBaseAddr = (uint32_t)(uintptr_t)data;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = length;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(SPI_DMA_TX_CHANNEL, &dma);

    DMA_ClearFlag(SPI_DMA_TX_GL_FLAG);
    DMA_ITConfig(SPI_DMA_TX_CHANNEL, DMA_IT_TC, ENABLE);

    s_busy = 1U;
    DMA_Cmd(SPI_DMA_TX_CHANNEL, ENABLE);
    return 1U;
}

uint8_t SPI_DMA_WriteBlocking(const uint8_t *data, uint16_t length)
{
    uint32_t guard;

    if((data == 0) || (length == 0U) || (s_busy != 0U))
        return 0U;

    while(length--)
    {
        guard = SPI_DMA_FLAG_GUARD;
        while((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) && guard)
        {
            --guard;
        }

        if(guard == 0U)
            return 0U;      /* SPI cell not draining: report instead of freezing */

        SPI_I2S_SendData(SPI1, *data++);
    }

    guard = SPI_DMA_FLAG_GUARD;
    while((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) != RESET) && guard)
    {
        --guard;
    }

    return (guard != 0U) ? 1U : 0U;
}

/* DMA1 Channel3 completion.  DMA TC only means the final byte reached DATAR;
 * the transfer ends when SPI BSY falls, which is a sub-microsecond wait at
 * 12 MHz, so the short bounded spin is an acceptable IRQ cost and removes the
 * need for any scheduler-side service function. */
void SPI_DMA_TxDMA_IRQHandler(void)
{
    uint32_t guard = SPI_DMA_FLAG_GUARD;

    if(DMA_GetITStatus(SPI_DMA_TX_TC_IT) == RESET)
        return;

    DMA_ClearITPendingBit(SPI_DMA_TX_TC_IT);
    DMA_Cmd(SPI_DMA_TX_CHANNEL, DISABLE);
    DMA_ClearFlag(SPI_DMA_TX_GL_FLAG);

    while((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) != RESET) && guard)
    {
        --guard;
    }

    s_transfer_count++;
    s_busy = 0U;

    if(s_done_callback != 0)
        s_done_callback();
}

uint8_t SPI_DMA_IsBusy(void)
{
    return s_busy;
}

uint32_t SPI_DMA_GetTransferCount(void)
{
    return s_transfer_count;
}

uint32_t SPI_DMA_GetClockHz(void)
{
    return SPI_DMA_CLOCK_HZ_DEFAULT;
}

/* Panel chip select through the SPI hardware NSS pin: the NSS output is
 * asserted while the SPI cell is enabled, so this toggles SPI_Cmd(). */
void SPI_DMA_DisplaySelect(uint8_t selected)
{
    if(selected)
    {
        SPI_Cmd(SPI1, ENABLE);
    }
    else
    {
        /* Never disable the SPI while a byte is still shifting.  The wait is
         * bounded like the others: a wedged cell may cost one glitched frame,
         * but it must never freeze the cooperative main loop. */
        uint32_t guard = SPI_DMA_FLAG_GUARD;

        while((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) != RESET) && guard)
        {
            --guard;
        }
        SPI_Cmd(SPI1, DISABLE);
    }
}

void SPI_DMA_DisplaySetDataMode(uint8_t data_mode)
{
    if(data_mode)
        GPIO_SetBits(OLED_DC_GPIO_Port, OLED_DC_Pin);
    else
        GPIO_ResetBits(OLED_DC_GPIO_Port, OLED_DC_Pin);
}
