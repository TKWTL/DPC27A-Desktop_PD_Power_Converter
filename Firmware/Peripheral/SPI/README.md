# SPI1 + DMA 显示传输

> 职责:提供不依赖 u8g2 的 4 线 SPI 显示传输层(SH1107 使用),页链由 DMA 完成中断推进。

## 硬件映射

| 信号 | 引脚 | 说明 |
|---|---|---|
| SCK | PA11 | SPI1 重映射 `GPIO_PartialRemap2_SPI1`,AF 推挽 |
| MOSI | PA10 | AF 推挽 |
| NSS / CS | PA12 | **SPI 硬件 NSS 输出**(SSOE=1),不再是软件 GPIO |
| D/C | PA9 | 普通 GPIO 输出 |

- 模式:主机、Mode 0、8 位、MSB first、单线发送(`SPI_Direction_1Line_Tx`);
- 时钟:**12 MHz**(48 MHz / 4,`SPI_DMA_CLOCK_HZ_DEFAULT`);
- TX DMA:DMA1 Channel3,传输完成产生中断;RX 不使用。

## CS 语义(硬件 NSS)

`SPI_SSOutputCmd(SPI1, ENABLE)` 后 NSS 引脚由 SPI 外设驱动:**SPI 使能期间 NSS 拉低,
SPI 关闭后释放为高**。因此 `SPI_DMA_DisplaySelect()` 实际就是:

| 参数 | 动作 | 效果 |
|---|---|---|
| 1 | `SPI_Cmd(SPI1, ENABLE)` | NSS 拉低(选中面板) |
| 0 | 等 `BSY=0` 后 `SPI_Cmd(SPI1, DISABLE)` | NSS 释放(高) |

## 传输模型

- `SPI_DMA_TryTransmit()` 非阻塞启动一次 DMA 发送,忙时返回 0;
- 完成路径:**DMA1_Channel3 中断** → 清标志、关通道 → 等 `SPI BSY=0`
  (12 MHz 下不到 1 µs)→ 置空闲并调用已注册的完成回调;
- `SPI_DMA_SetDoneCallback()` 注册回调,显示层用它把整帧分页推进;
- `SPI_DMA_WriteBlocking()` 仅供 SH1107 初始化/控制使用的短命令路径;
- **没有** `SPI_DMA_Service()` 之类的轮询服务,调度器不再参与 SPI 推进。

## 注意事项

- 关闭 SPI(释放 CS)前必须等 `BSY=0`,否则会截断最后一个字节;
- **本文件所有标志等待都有界**(`SPI_DMA_FLAG_GUARD = 100000` 次):本工程是协作式调度,
  任何无界 `while(硬件 flag)` 都会连带 PD 策略与 UI 一起冻死。`SPI_DMA_WriteBlocking()`
  超时返回 0(初始化路径会拿去当错误上报);`SPI_DMA_DisplaySelect(0)` 超时仍会关 SPI
  (最坏多一个花屏帧,但不能让主循环卡住);
- 完成回调运行在中断上下文，应保持简短；显示层常规页只发送 3 字节命令并启动下一页 DMA，帧边界还可能复制一帧或执行挂起控制命令。
