# CH32X035 I2C1 中断驱动异步 DMA 后端

> 职责:为多属主提供一条共享硬件 I2C1 总线,整个事务由中断推进。

## 硬件映射

```text
PA13  I2C1_SCL
PA14  I2C1_SDA
DMA1 Channel6  I2C_TX
DMA1 Channel7  I2C_RX
时钟            400 kHz
```

## 事务引擎(全部中断驱动)

| 中断 | 处理内容 |
|---|---|
| `I2C1_EV` | `SB` → 发地址;`ADDR` → 启动 TX DMA / 重复 START / 单字节读 |
| `I2C1_ER` | `AF`(NACK)、`BERR`、`ARLO`、`OVR` |
| `DMA1_Channel6` | TX 负载发完 → 等待 `BTF` → 重复 START 或 STOP |
| `DMA1_Channel7` | RX 负载收完 → STOP |

前台只剩**看门狗**(10 ms 周期),它负责:

1. 事务超过 75 ms 无进展 → 中止并请求总线恢复;
2. 恢复序列(关 I2C → GPIO 接管 → 最多 9 个 SCL → 类 STOP 释放 → 重新初始化);
3. 补启动:请求发起时若总线仍忙(`BUSY=1`),事务停在 `WAIT_BUS_IDLE`,
   由看门狗在总线空闲时补发 START(发起时已先做 250 µs 短暂等待;
   正常 STOP 之后不会进入这个状态)。

## 事件中断风暴保护(2026-09-12 修复实装)

`I2C1_EV` 是**电平触发**的:只要 `SB`/`ADDR`/`BTF`/`STOPF` 中有一个没被清掉, `IT_EVT`
一使能就会立刻重入。实测这种风暴可达 **~10^6 次/秒**,而 `I2C1_EV` 的抢占优先级
高于 SysTick → 1 ms 节拍、IWDG 喂狗全停摆 → 表现成“停在某页 → IWDG 复位”。
现在的处理分四层:

0. **开事务前先清场** `i2c_prepare_start()`(每次发 START 前必调):`STAR1→STAR2`
   读序列清掉 `ADDR`/`STOPF`;若还残留 `SB`/`BTF`(这两个只能靠读/写 DR 清,
   会往总线发真数据)且总线不忙,则先 `i2c_peripheral_reset()`(RCC 复位 +
   重新初始化)再发 START——这是唯一安全且彻底的清法。
1. **正常路径一个标志都不留**:
   - `finish()` 在 `BUSY` 或 `BTF` 置位时补一个 STOP(STOP 顺带清 `BTF`);
   - TX-DMA 等 `BTF` 期间先关 `IT_EVT`,避免“预期中的 BTF 事件”被当成异常;
   - ISR 在“事务已结束”时只清 `ADDR`/`STOPF` + 关 `IT_EVT`,**不请求恢复**
     （否则每次正常收尾的 TX-DMA 都会把 I2C 复位一次）;
   - 非预期状态的 `SB`/`ADDR` 各有明确处置(读 `STAR2` 清掉 / 熔断恢复)。
2. **风暴熔断**: ISR 记录“同一状态被连续打断”的次数,超过 `I2C_API_EV_STORM_LIMIT`
   (64) 就关掉全部 I2C 中断源 + 拉 STOP + `s_recovery_requested=1`,把现场
   (`STAR1`/`STAR2`/`state`)交给下一轮看门狗恢复;
3. **看门狗恢复**: 下一轮 10 ms 看门狗执行 `bus_recover()`(外设复位 + GPIO 位翻转
   + 重新初始化)把总线拉回干净状态。

标志位对照:`STAR1` bit0=SB、bit1=ADDR、bit2=BTF、bit4=STOPF。

## 客户端用法(与重构前一致)

```c
THRD_UNTIL(I2C_API_TryWriteRead(owner, addr, tx, tx_len, rx, rx_len));
THRD_UNTIL(I2C_API_GetResult(owner) != I2C_API_RESULT_ACTIVE);
result = I2C_API_TakeResult(owner);
```

客户端任务中不允许 `while(flag)` 轮询;缓冲区零拷贝,必须保持有效直到
`TakeResult()`。

## DMA 策略

- TX 负载:DMA1 CH6;
- RX ≥ 2 字节:DMA1 CH7 + `I2C LAST`(硬件对最后一字节回 NACK);
- RX == 1 字节:ACK=0 → 清 ADDR → STOP → 等 `RXNE` 后读字节
  (中断内有界自旋,避免 ITBUFEN 引发的 TXE 中断风暴);
- 中断里等待 `BTF`/`BSY` 都是有界自旋(最长约一个字节时间);
  USBPD 中断优先级更高,不会被它阻塞。

## 看门狗与恢复

`I2C_API_WatchdogService()` 由独立 coroOS 线程每 10 ms 调用一次。
事务超过 75 ms 未完成会被中止并请求恢复。BERR、ARLO、OVR 同样触发恢复;
普通器件 NACK 不触发。

恢复序列:

1. 关闭 I2C DMA 与 I2C1;
2. 复位 I2C1 外设;
3. 释放 SCL/SDA 为浮空输入;
4. 若 SDA 仍为低,以“输出低/释放输入”方式最多发 9 个 SCL 脉冲;
5. 在 SCL 释放状态下做类 STOP 的 SDA 释放;
6. 恢复 PA13/PA14 为 I2C 复用功能,以 400 kHz 重新初始化 I2C1。

GPIO 恢复过程从不主动拉高 SCL/SDA,依靠板上的 I2C 上拉电阻完成拉高。


## 2026-11-14：故障现场锁存字段说明

仅在诊断构建中输出；开关配置和编译行为统一见 [`Firmware/README.md`](../../README.md) 的「诊断输出预编译开关」。诊断构建的恢复日志分两行：第一行显示原始错误及 `STAR1/STAR2`，并记录 `cause`（1=75 ms 超时、2=事件中断风暴、3=DMA TE 错误、4=其他 I²C 错误）、`owner`（1=SW3538、2=GX21M15U、3=应用）、7 位地址及冻结的事务状态 `st`。第二行：

- `pre=SCL/SDA`：故障现场 GPIO 电平；`post=SCL/SDA`：恢复后 GPIO 电平。两者**不是同一时刻**。
- `rx`：请求读取的字节数量；`left`：DMA1 CH7 故障瞬间剩余计数；`cf7`：DMA1 CH7 CFGR；`if`：DMA1 INTFR；`c2`：I²C1 CTLR2；`age`：当前状态停滞时长（ms）。
- `left=2`（`rx=2`）表示尚未搬运字节；`left=1` 已搬运一个；`left=0` 应结合 TC7/TE7、DMA EN 与中断是否执行判断。不要仅凭剩余计数断言原因。
- 看门狗/ER/DMA 错误路径和 **EV 风暴熔断路径**都在 `stop_dma()` 前记录寄存器；不会在 ISR 中打印，也不额外读 `DATAR`。总线恢复行为和 75 ms 看门狗阈值不变。

复现条件建议分别测试：无负载、仅开启 DC-DC、以及大功率输出（报告约 5 秒一次），记录持续时间与恢复计数，并在示波器上同步观察 PA13/SCL、PA14/SDA 与本地电源纹波。比较 400/100 kHz 时不应同时改变其它配置。

本次通过 GitHub 仓库内容审查，**尚未在目标编译器或硬件上编译/验证**。

## 2026-11-14：根据无负载实测收敛的 RX DMA/EV 中断竞争修复

本次实测（11 次）：
```text
[I2C] rec#1 RX DMA cause=2 owner=2 addr=4f st=8 S1=0044 S2=0003
[I2C] pre=0/0 post=1/1 rx=2 left=0 cf7=0000208b if=07005700 c2=1b30 age=0
```

- `cause=2` 明确表示 EV 事件中断风暴，非 75 ms 超时；`owner=1/2` 均曾出现，涉及 SW3538/GX21M15U 共享的 I2C1。
- `rx=2 left=0` 和 `DMA1.INTFR` 的 TC7=1、TE7=0 说明 DMA 两字节传输已经完成，未见 DMA 错误，正常的 DMA7 完成收尾却未及时发生。
- `I2C1.CTLR2=0x1B30`：LAST（0x1000）、DMAEN（0x0800）、ITEVTEN（0x0200）、ITERREN（0x0100）均为 1；`STAR1=0x44` 的 BTF/RXNE 会引发仍然开启的 EV IRQ。由于固件将 I2C EV 设为子优先级 0、DMA7 为子优先级 1，持续待处理的 EV IRQ 可能抢先反复运行直到第 64 次熔断，造成 TC7 处理饥饿。
- **最小修复**：接收地址 `ADDR` 处理分支进入 `rx_len >= 2` 时，先 `I2C_ITConfig(I2C1, I2C_IT_EVT, DISABLE)`，再按原顺序配置 DMA/LAST、清除 ADDR。数据阶段由 DMA7 TC/TE ISR 接管，不再需要 EV IRQ。I2C ER IRQ 不关闭；下一条事务 `start_transaction()` 会重新使能 EV。单字节读取与 TX DMA 路径不变。
- **判据**：修复后在无负载、仅开启 DC-DC、大功率输出三种工况下重复观察。预期 EV 风暴恢复显著减少或消失；若仍出现 `RX DMA cause=2`，重点核对 `c2` 的 `0x0200` 位是否仍使能，并继续检查 DMA IRQ 响应/多字节 LAST。此修复是基于软件现场的因果推断，**尚未编译或实机验证**。


## 2026-11-14：5 分钟大功率实测通过与可复用教训

**测试结果**：启用 RX DMA 数据阶段屏蔽 I²C EV 中断的修复版后，用户进行了 **5 分钟连续大功率输出测试，未观察到 I²C 报错/恢复日志**。这是实机证据，说明此前频繁发生的中断风暴在这一测试条件下已消失；它不代表已验证所有功率、温度、负载瞬态及长期运行工况。当前仍维持 400 kHz 时钟和原有 75 ms 看门狗。

### 故障链与关键判断

1. 修复前不带负载也偶发，开启 DC-DC 后概率增大，大功率约每 5 秒触发一次；SW3538（`owner=1 addr=0x3C`）与 GX21M15U（`owner=2 addr=0x4F`）均受影响，故障点应优先定位到**共享 I²C1/DMA 接收后端**，而不是单一从设备。
2. 实测 `cause=2` 为 **I²C EV 中断风暴**；状态 `WAIT_RX_DMA`，`rx=2 left=0`，`DMA1.INTFR=0x07005000/0x07005700` 中 **TC7=1、TE7=0**，表明 DMA 已完成两字节搬运，没有 DMA7 传输错误。
3. 故障时 `STAR1=0x0044`（BTF/RXNE），`STAR2=0x0003`（MSL/BUSY），`CTLR2=0x1B30`（包括 LAST、DMAEN、ITEVTEN、ITERREN）。DMA 传输已经完成，**EV 仍然使能**；它对 BTF 等未处理标志持续响应，且 I2C EV 子优先级高于 DMA7，因此可能反复抢先进入 EV ISR，达到 64 次熔断阈值，导致 DMA7 TC ISR 无法及时 STOP 收尾。
4. **有效修复**：在 `WAIT_ADDR_RX` 的 `rx_len >= 2` 分支，先关闭 **I2C_IT_EVT**，然后 `start_rx_dma()`，最后 `clear_addr_flag()`。接收数据阶段仅由 DMA7 TC/TE 中断处理；**保留 I2C_IT_ERR** 和看门狗，下一条事务产生 START 时重新开启 EV。单字节 RX、TX DMA、ACK/LAST、通信速率保持原设计。
5. **工程教训**：使用 DMA 的外设，不是启用 DMA 后就可以无限保留该阶段不再需要的事件中断。必须按事务状态明确中断所有权，避免“事件标志保持置位 → 高频中断重入 → 优先级较低的 DMA 完成 ISR 饥饿”。即便 DMA 计数归零，也不能推断 CPU 已处理 DMA 完成收尾。若仅见恢复后 `SCL=SDA=1`，也不能倒推故障时总线空闲；故障状态必须在复位前采样。
6. **固件体积约束**：诊断快照只保留为可编译选项，不应长期运行、永久占用 FLASH/SRAM；调试后应关闭，并比较链接 `.map` 中 `.text/.rodata/.data` 和 `.bss` 的占用。此次没有提供关闭前后的编译数据，不能声称具体节省了多少字节。

### 诊断开关

构建选项、预编译范围及 Clean Build 步骤集中记录在 [`Firmware/README.md`](../../README.md)；这里保留 I²C 特有的故障现场字段和修复经过。

后续若复发，先在相同负载条件下用诊断版观察 `DMA7 TC7`、`DMA7 TE7` 和 `CTLR2 ITEVTEN`；对已经确认的中断竞争不要先尝试降低 I²C 时钟来掩盖。

## 2026-11-14：精简构建与诊断构建的行为分歧（待板端复测）

**用户实测**：`DPC_DIAGNOSTICS_ENABLE=0` 时，仪表盘上的 GX21M15U 温度与 SW3538 数据会间歇显示离线占位；同样设置为 `1` 后现象消失。两种构建的固件大小、执行时间、状态寄存器读取及串口行为都可能不同，单凭现象不能断言只有哪一种原因。

源码审计发现可确定的构建差异：

- 旧 `finish()`：诊断版的正常收尾路径在 `stop_dma()` 前连续读取 `I2C1->STAR1` / `I2C1->STAR2` 并保存；精简版编译时该两次寄存器读取被删。**这不是纯日志行为**：`STAR1→STAR2` 对经典 I²C 状态机有清 `ADDR` 等副作用。
- 旧 `i2c_abort_event_storm()`：诊断版的 `snapshot()` 会读 `STAR1→STAR2`，精简版只读 `STAR2`，使异常收尾路径也不一致。
- **修订**：两个入口都**无条件按 `STAR1→STAR2` 读取**，且发生在 DMA/STOP 清理之前；诊断版额外锁存寄存器、DMA 和引脚数据，精简版不保留扩展快照。保留 EV 事件中断交接修复、原有 TX/RX/ACK/LAST、看门狗和总线恢复。对 `STAR1/STAR2` 这类带副作用的外设寄存器，不允许用 `#if DPC_DIAGNOSTICS_ENABLE` 直接包围其必需访问。
- **验证状态**：仅源代码审查，尚无用户对修订版的硬件验证。应对相同提交分别 Clean Build `0`、`1`，在无负载及 5 分钟连续大功率条件下比较离线次数；若 `0` 仍有间歇离线，需要进一步捕获 DMA/事件标志与中断延迟，检查是否仍有时序敏感性。

