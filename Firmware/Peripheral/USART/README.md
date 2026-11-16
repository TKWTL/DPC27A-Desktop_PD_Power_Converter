# 异步串口(USART async API)

> 职责:提供 921600 波特率的 DMA 全双工调试串口与 `printf` 重定向。

`usart_async.c/.h` 把 USART1(PB10 TX / PB11 RX)实现为 DMA 支撑的全双工字节流,
收发各 256 字节缓冲。

- TX:DMA1 Channel4,从软件环形缓冲按普通模式分块发送;TC 中断自动推进环并与
  下一段连续数据接续;
- RX:DMA1 Channel5,256 字节循环 DMA;TC 中断累计整圈数,前台读取时用 DMA
  `CNTR` 推导生产者位置;
- `printf()` 经 `Project/Debug/debug.c` 重定向到本模块;SDI 未参与编译;
- TX 背压是有界的:即使 DMA 通道异常,也不会永久卡死 coroOS。

TX/RX 具体接口见 usart_async.h。常规发送可查询 pending/busy/dropped；接收可查询 available/overrun。正常主循环不依赖 USART 轮询完成 DMA，日志分组见 [固件总览](../../README.md)。HardFault 使用独立有界 USART 寄存器写入，不依赖 DMA/printf。
