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


## 2026-11-14：复位前故障现场锁存

串口恢复日志现在输出两行。第一行仍显示原始错误及 `STAR1/STAR2`，新增 `cause`（1=75 ms 超时、2=事件中断风暴、3=DMA TE 错误、4=其他 I²C 错误）、`owner`（1=SW3538、2=GX21M15U、3=应用）、7 位地址及冻结的事务状态 `st`。第二行：

- `pre=SCL/SDA`：故障现场 GPIO 电平；`post=SCL/SDA`：恢复后 GPIO 电平。两者**不是同一时刻**。
- `rx`：请求读取的字节数量；`left`：DMA1 CH7 故障瞬间剩余计数；`cf7`：DMA1 CH7 CFGR；`if`：DMA1 INTFR；`c2`：I²C1 CTLR2；`age`：当前状态停滞时长（ms）。
- `left=2`（`rx=2`）表示尚未搬运字节；`left=1` 已搬运一个；`left=0` 应结合 TC7/TE7、DMA EN 与中断是否执行判断。不要仅凭剩余计数断言原因。
- 看门狗/ER/DMA 错误路径和 **EV 风暴熔断路径**都在 `stop_dma()` 前记录寄存器；不会在 ISR 中打印，也不额外读 `DATAR`。总线恢复行为和 75 ms 看门狗阈值不变。

复现条件建议分别测试：无负载、仅开启 DC-DC、以及大功率输出（报告约 5 秒一次），记录持续时间与恢复计数，并在示波器上同步观察 PA13/SCL、PA14/SDA 与本地电源纹波。比较 400/100 kHz 时不应同时改变其它配置。

本次通过 GitHub 仓库内容审查，**尚未在目标编译器或硬件上编译/验证**。
