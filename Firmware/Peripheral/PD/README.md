# USB-PD 策略与 PHY 层

> 职责:作为 USB-PD Sink 完成 SPR → EPR 协商;对上层只暴露策略状态与 PDO 快照。

## 分层

| 文件 | 层次 | 内容 |
|---|---|---|
| `pd.c` | 协议/策略 | Type-C attach 后的 Sink 状态推进;SPR Fixed PDO 选择(≤ 20 V);`Get_Source_Cap_Extended` → 按源端 EPR PDP 自适应 EPR Mode Enter(140 W fallback、上限 224 W);EPR Source Capabilities 分块接收;最高电压 Fixed PDO 选择(> 20 V 且 ≤ 32 V,兼容非标值);Request / Accept / PS_RDY;EPR KeepAlive;VBUS 掉线、Soft Reset 与超时恢复 |
| `pd_port.c` | PHY/CC | `USBPD`/CC/GPIO/RCC/NVIC 寄存器;原子 SOP 发送 `PD_Port_TransactSOP()`(屏蔽 IRQ → SOP TX → 等 TX_END → 立即转 RX → 轮询匹配 GoodCRC);ISR 内自动 GoodCRC(直写策略缓冲,无 mailbox;回显伙伴消息头 SpecRevision,保留值 11b 钳位为 Rev3 编码);Hard Reset 物理发送 |
| `pd.h` | 策略参数 | 对外承诺上限(SPR ≤ 20 V / 5 A;EPR 目标 28 V / 5 A;Sink PDP 140 W)与对外查询 API;内部申请策略见 `pd.c` 顶部 `PD_POLICY_*`(兼容非标源:电流 ≤ 7 A、EPR Fixed ≤ 32 V) |

分层约束:`pd.c` 不得直接访问 `USBPD->...`、GPIO、RCC、NVIC;所有寄存器操作都封装在 `pd_port.c`。

## 请求策略(2026-09-17 升级)

- **SPR**:≤ 20 V 的 Fixed PDO 中取**最高电压**(同压取最高电流),电流钳到策略上限 7 A;
- **EPR 入口自适应**:进入 EPR 前先发 `Get_Source_Cap_Extended`(超时 120 ms),按源端
  EPR Source PDP 选择 Enter PDP(缺失/无回应 → 140 W fallback;超出策略上限 → 224 W
  = 32 V × 7 A);Enter 被拒且原因为"Source unable to enter EPR now"时,120 ms 后
  **以 140 W 重试一次**;
- **EPR 请求**:就绪的 EPR Fixed PDO(> 20 V、≤ 32 V,扫描全部 15 个槽位)中取
  **最高电压**(同压取最高电流),电流钳 7 A——支持非标电压(如 24/30/32 V)与非标电流;
- 实际申请始终以源端广告值为准;产品**标称**契约仍为 EPR 28 V / 5 A。

## 对外接口

| 接口 | 说明 |
|---|---|
| `PD_Init()` / `PD_Task(now_ms)` | 初始化与每轮调度 |
| `PD_SetVbusMillivolts(mv)` | 由 VBUS 任务喂入实测输入电压,参与掉线判断 |
| `PD_IsConnected()` / `PD_IsPowerReady()` | 连接状态与供电就绪状态 |
| `PD_IsEPRContractActive()` | EPR 合同是否生效 |
| `PD_GetContractVoltageMv()` / `PD_GetContractCurrentMa()` | 当前合同电压/电流 |
| `PD_GetDisplayPDOs()` | 供 UI 展示的 PDO 快照(按 AVS > EPR Fixed > PPS > SPR 排序) |

## 时序硬规则(必须保留)

1. 普通 SOP 发送必须走 `PD_Port_TransactSOP()` 原子事务(屏蔽 USBPD IRQ → SOP TX →
   等 `IF_TX_END` → 立即转 RX → 轮询匹配的 GoodCRC);不要拆成“Send + WaitGoodCRC”分裂接口;
2. 帧正在发送时(自动 GoodCRC 或前台 TX)不得执行 `PD_ALL_CLR` 或重新启动 RX
   ——`PD_Port_RxStart()` 已按 PHY 状态自我门控;
3. 所有 PHY 忙等必须有界(TX_END 2 ms),超时把 PHY 清回 RX 并计入 `PhyDiag.tx_end_timeouts`;
4. 本板由 VBUS 供电,因此本地协议恢复默认**不主动发送 Hard Reset**(避免 Source 关闭 VBUS 导致 MCU 掉电)。

## 抓包参考(`captures/`)

逻辑分析仪在 **CC 线**上导出的 USB-PD BMC **Symbols** 级原始抓包(未解码成报文),
供比对 EPR 协商与维持时序时参考:

| 文件 | 内容 |
|---|---|
| `captures/EPR_negotiation_capture.txt` | EPR 协商阶段:SPR 合同 → EPR 入口握手 → EPR caps / EPR_Request 前后的 CC 活动 |
| `captures/EPR_keepalive_capture.txt` | EPR 合同生效后的 KeepAlive 周期活动(375 ms 周期的 CC 突发) |

采集条件:**MuseLab nanoDLA** + **PulseView**,经自制 CC 电平转换器探测 CC 线,
采样率 **12 MHz**(时间列 = 采样点序号,1 点 ≈ 83.3 ns)。

格式:每行一段 CC 符号,`<起始时间>-<结束时间> USB PD: Symbols: <符号>`;数据行对应
**4b5b 编码后的 1 byte**(10 个 600 kHz 编码位 ≈ 16.7 µs = 200 采样点 @12 MHz),
符号取值包括 `SYNC-1/2/3`、`RST-1/2`、十六进制 `0x0..0xF`、`EOP`。素材按原样归档、
不做二次加工;固件侧解码后的日志可在运行时通过 921600 串口查看(报文语义见上文
"请求策略"节)。

## 相关文档

- `Docs/FIRMWARE_ARCHITECTURE.md`:PD 在整体调度中的位置;
- 时序与策略基线:本文件"请求策略"节与 `pd.c` 顶部宏。
