# SW3538 Driver

基于 **SW3538 I2C Register List RG108_3 v1.2** 的非阻塞 coroOS / I2C1 驱动。

## 本版能力

- 状态、端口在线、协议、ADC、限流值读取；
- `SetPowerLimitW(18..140W)`；
- **协议族使能配置**：QC2 / QC3 / QC3+ / QC4+ / FCP / SCP / PE / SFCP / AFC / TFCP / PD；
- **通路快充开关**：Path1 / Path2；
- **保护配置/运行状态读取**：BUCK/通路状态、IRQ 事件、NTC/VIN/DIE/VOUT/DPDM/CC 保护使能与门限。

## 协议配置

SW3538 的协议开关位位于 `0x1xx` 扩展寄存器区。驱动自动执行：

```text
0x10 = 0x20 -> 0x40 -> 0x80 -> 0x81
```

进入扩展 bank，并在结束或错误清理时写 `Reg0x180 = 0` 返回普通 bank。

```c
SW3538_ProtocolMask mask =
    SW3538_PROTOCOL_ENABLE_QC2 |
    SW3538_PROTOCOL_ENABLE_QC3 |
    SW3538_PROTOCOL_ENABLE_FCP |
    SW3538_PROTOCOL_ENABLE_SCP |
    SW3538_PROTOCOL_ENABLE_PD;

THRD_SPAWN_ARGS(SW3538_SetProtocolMask, &sw3538, mask);
```

涉及：

- `Reg0x116[7]`：BC/DPDM 模块；
- `Reg0x119[2:0]`：FCP、低压/高压 SCP；
- `Reg0x11A[7:0]`：PE / SFCP / AFC / QC2 / QC3 / QC3+ / QC4+ / TFCP；
- `Reg0x122[0]`：PD master enable。

**PD 被作为一个协议族处理。** API 只修改 `Reg0x122[0]`，不会改写 `Reg0x124` 中的固定 PDO / PPS 档位，也不会改写 PDO 电流或 PPS 电压电流参数。

RG108_3 v1.2 没有公开独立的 VOOC/SuperVOOC enable bit，因此本 API 不伪造这类开关；协议状态枚举仍保留芯片实际回报的 VOOC 类型。

通路总开关：

```c
THRD_SPAWN_ARGS(SW3538_SetPortFastChargeEnabled, &sw3538, 1u, 1u);
THRD_SPAWN_ARGS(SW3538_SetPortFastChargeEnabled, &sw3538, 2u, 0u);
```

对应 `Reg0x11B[7:6]`。

## 保护状态

```c
THRD_SPAWN_ARGS(SW3538_ProtectionStatusLoad, &sw3538);
const SW3538_ProtectionStatus *p = SW3538_GetProtectionStatus(&sw3538);
```

读取内容包括：

- `Reg0x0A/0x0D`：BUCK / Path1 / Path2 与设备在线状态；
- `Reg0x20`：场景切换、BUCK ON、BUCK OFF 事件；
- `Reg0x13`：NTC 过温、VIN OVP、DIE 过温保护使能；
- `Reg0x10C`：单点失效、VOUT UVP；
- `Reg0x10D`：NTC 保护门限；
- `Reg0x10E`：VIN UVLO/OVP 门限、DPDM OVP、DPDM 弱短路、CC OVP。

### 重要限制

RG108_3 v1.2 **没有定义类似 SW3526 Reg0x0B 的可读“故障原因锁存寄存器”**。因此：

- `fault_cause_available == 0`；
- `buck_off_event` 只能说明出现过 BUCK OFF 事件，不能单独判定是 VIN OVP、过温、UVP 还是其他原因；
- 需要危险事件弹窗时，建议把芯片的保护配置/运行状态与系统自己的 VIN/VOUT/温度/端口遥测共同判断。

## 扩展 bank 可靠性

本版把新增扩展寄存器操作以及 `SetPowerLimitW()` 改为“错误也必须退出扩展 bank”的流程。即使中途 I2C 失败，只要已经成功选择 bank，函数仍会尝试 `Reg0x180=0`，避免后续普通寄存器访问被错误解释成 `0x1xx` 地址。

## 外接 NTC 温度遥测（2026-11-15）

主板网表 R46 = MF52A104J3950，U1.24 NTC → 100 kΩ B3950 → GND。RG108_3 v1.2 §2.23–2.26：Reg0x40=7 锁存 NTC ADC，0x41/42 12 bit、1.2 mV/LSB，0x44[7] 0=20 µA、1=40 µA。ADCLoad 在已有强制 ADC Reg0x18 中置 bit1，其他位、保护门限/使能均不改；读电流 → 锁存 → 读 ADC → 再读电流，前后档位不同则本次 NTC 无效。若实际 ADC 饱和或恒流源不在合规范围，温度不能可靠估计。档位前后相同也不能排除两次切换或 ADC 内部采样滞后；尚待实测。

R(Ω)=D×60 或 D×30。使用 R25=100000 Ω、B=3950 K、T25=298.15 K 的 Beta 模型离线生成 0~125°C 每 5°C 26 点（uint16_t，单位 10 Ω，共 52 B），整数线性插值到 0.1°C。超出表范围/ADC 0 或 4095 均无效；允许端点 ±30 Ω 的量化余量。受 4.915 V ADC 量程和恒流源合规限制，低温有效范围小于模型范围（名义 20 µA 下 R>245.7 kΩ 无法测量，40 µA 更窄）；没有把饱和解释成正常低温。返回接口 `SW3538_ReadNTCTemperatureX10(handle,&value)`，false 表示无有效温度或设备离线。I²C 失败清除有效标志，电流档位不一致不影响电源其他遥测的在线状态，下一轮再采。Statistics 使用镜像。最新温控按 System 的 Use Temp#2（默认开启）将 NTC 作为第二路控制温度参与融合；不修改 SW3538 自身硬件保护。

数值验证：两电流档所有有效 ADC 码单调；以 Beta 模型 0~125°C 0.1°C 步进模拟四舍五入 ADC（剔除不可测点，共 2236 个有效测点），最大插值+ADC量化误差约 0.40°C，不含 NTC 阻值/B 容差、ADC/恒流误差、自热和安装误差。25°C 应约 ADC1667(20 µA) / 3333(40 µA)。型号和板端温度需用同点参考温度计核对。来源：[SW3538 DS108_3 v1.2](https://www.ismartware.com/upload/goods/20220811/202208111603413353.pdf)，RG108_3 v1.2 §2.23–2.26 公开镜像（https://www.scribd.com/document/1074159217/sw3538-rejestry）。
