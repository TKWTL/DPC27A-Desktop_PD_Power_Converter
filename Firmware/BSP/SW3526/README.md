# SW3526 Driver

基于 **SW3526 Register List RG012_1 v1.0** 的非阻塞 coroOS / Soft-I2C 驱动。

## 本版能力

- 两颗固定地址 `0x3C` 的 SW3526 通过独立 `SoftI2C_Handle` 实例运行；
- `StatusLoad()` / `ADCLoad()` / 协议状态读取；
- `SetPowerLimitW(12..71W)`；
- **协议族使能配置**：QC2 / QC3 / FCP / SCP / PD / PE / VOOC / SFCP / AFC；
- **保护状态读取**：VIN OVP、过温预警、过温关机、输出短路；
- 保护模式回读：PD 限流 UV/OC、QC3 限流方式、QC3/PPS 恒功率模式；
- CC1/CC2 attach 状态读取。

## 协议配置

```c
SW3526_ProtocolMask mask =
    SW3526_PROTOCOL_ENABLE_QC2 |
    SW3526_PROTOCOL_ENABLE_QC3 |
    SW3526_PROTOCOL_ENABLE_PD;

THRD_SPAWN_ARGS(SW3526_SetProtocolMask, &sw3526, mask);
```

`SW3526_SetProtocolMask()` 使用寄存器原值做 read-modify-write：

- `0xA8`：DPDM 协议族，**0=使能 / 1=禁止**；
- `0xA9[0]`：PD master enable，**0=使能 / 1=禁止**；
- `0xAA[5]`：DPDM/BC 模块，**0=使能 / 1=禁止**；
- `0xAC[2]`：端口全部快充总开关，**0=使能 / 1=禁止**。

API **不会修改 `0xA9[7:2]` 的 PPS0/PPS1 与 9/12/15/20V PDO 档位配置**，因此启停 PD 协议族不会重写已有 PDO/PPS 方案。

当前使能状态可通过：

```c
THRD_SPAWN_ARGS(SW3526_ProtocolConfigLoad, &sw3526);
const SW3526_ProtocolConfig *cfg = SW3526_GetProtocolConfig(&sw3526);
```

读取。

## 保护状态

`Reg0x0B` 提供实际异常状态：

```c
THRD_SPAWN_ARGS(SW3526_ProtectionStatusLoad, &sw3526);
const SW3526_ProtectionStatus *p = SW3526_GetProtectionStatus(&sw3526);

if(p->vin_ovp) { /* VIN over-voltage */ }
if(p->over_temperature_alarm) { /* thermal warning */ }
if(p->over_temperature_shutdown) { /* thermal shutdown */ }
if(p->output_short) { /* output short */ }
```

`ProtectionStatusLoad()` 同时读取 `Reg0xAB`，返回当前 PD/QC3 限流保护方式以及 QC3/PPS 恒功率配置。

普通 `StatusLoad()` 仍会刷新 `Reg0x0B`，因此 `GetProtectionStatus()` 的实时 fault 位也会随常规状态轮询更新；只有 `Reg0xAB` 的模式字段需要调用 `ProtectionStatusLoad()` 才保证最新。

## 注意

- 所有写配置 API 都执行 `0x12 = 0x20 -> 0x40 -> 0x80` 解锁序列。
- 保留位不应修改；协议 API 对包含保留/无关位的寄存器使用 read-modify-write。
- 协议开关可能导致已连接设备重新协商，不应在高速周期任务中反复调用。
- `SetPowerLimitW()` 的临时功率编码已改为 per-handle scratch，不再使用跨协程不安全的自动/全局临时值。
