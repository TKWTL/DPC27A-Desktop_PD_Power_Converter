# 固件（Firmware）

DPC27A Desktop PD Power Converter 的 CH32X035C8T6 固件。采用无 RTOS 的 coroOS 协作式调度、USB-PD Sink 策略、独立快充控制器与 128×80 双语 OLED 界面。本文以当前源码为准；开发期间的实验、旧配置和实测记录见模块文档及 Git 历史。

## 构建与烧录

- 使用 MounRiver Studio II 打开本目录的 `Desktop_PD_Power_Converter.wvproj`（工程配置 `.project/.cproject`）。当前按体积优化（-Os，`-flto=auto`），不建议随意关闭 LTO。
- [链接脚本](Project/Ld/Link.ld) 配置 62 KiB Flash（63,488 B）、20 KiB RAM，栈预留 2 KiB。系统主频 48 MHz；构建产物位于 `Project/obj/`。
- 更换字体、编译宏或工具链后 Clean Build；检查 MAP/ELF 的 Flash/RAM、栈和库符号。System → Reboot to ISP（重启至ISP模式）进入 WCH 出厂 ISP；Reset Now 仅软件复位。
- 本次审查没有新的目标 MAP/ELF 或完整台架验收数据，不以估算替代实测占用。

## 目录结构

| 路径 | 职责 |
|---|---|
| APP/ | main、任务、IRQ、i18n；MiaoUI 菜单、Pages 页面 |
| APP/Services/ | 总功率限制、分配算法、风扇曲线 |
| APP/framework/ | UI_OFF 休眠状态机、计时器、设备钩子 |
| BSP/ | 板级初始化、SW3538、双 SW3526、GX21M15U、VBUS ADC、按键、风扇、SH1107 |
| Peripheral/ | PD、硬件/软件 I²C、SysTick、SPI DMA、USART DMA |
| ThirdParty/u8g2/ | 精简图形库及 SH1107 后端 |
| Project/ | WCH SPL、启动/链接、整数 printf、coroOS、构建输出 |

## 硬件映射

| 模块 | 资源 |
|---|---|
| USB-PD Sink | USBPD、CC1/CC2 |
| VBUS | PA7 ADC |
| I2C1 | PA13 SCL / PA14 SDA，400 kHz；SW3538 0x3C、GX21M15U 0x4F |
| 软件 I²C | SW3526 #1 PA4 SCL / PA3 SDA；#2 PA1 SCL / PA2 SDA，各 0x3C |
| OLED SPI1 | PA11 SCK、PA10 MOSI、PA12 NSS、PA9 D/C；12 MHz，DMA1 CH3 |
| USART1 | PB10 TX / PB11 RX，921600 baud，DMA1 CH4/5，收发各 256 B |
| I2C1 DMA | DMA1 CH6 TX / CH7 RX |
| K1 / K2 | PA5 / PA6，内部上拉、低有效 |
| 风扇 | PB9 / TIM1_CH1，187.5 kHz，8 bit duty 0–255 |

## 协作式任务（APP/app_tasks.c）

按注册顺序：

| 任务 | 目标间隔 | 职责 |
|---|---|---|
| thread_pd | 每轮 | PD 策略及协议 |
| thread_vbus | 20 ms | 采样 VBUS，提交 PD 掉线判断 |
| thread_soft_i2c_service | 每轮 | 两路软件 I²C 边沿 |
| thread_ui | 派发 2 ms；按键 8 ms；帧 6 ms | OLED 初始化、按键、Statistics 与 UI |
| thread_i2c_watchdog | 10 ms | 硬件 I²C 看门狗/恢复 |
| thread_power_mirror | 轮后 333 ms | 三颗快充芯片状态/ADC、协议配置、功率分配 |
| thread_gx21m15 | 轮后 500 ms | 数字温度镜像 |
| thread_fan | 500 ms | 自动风扇 |
| thread_pm | 50 ms | UI_OFF、功率和插拔唤醒 |

以上为协作式目标间隔，不是硬实时保证；轮后延时另含 I/O 时间。无栈协程跨 THRD_DELAY/YIELD/SPAWN 存活的变量必须为 static 或句柄成员。

- OLED 上电约 200 ms 后等待软件 I²C 空闲再初始化；不能用阻塞延时饿死 PD。
- 上电后最初 1000 ms 内若 PD 已连接但未 ready，普通 UI/遥测暂缓；未连接或合同就绪时解除该启动限制。
- 普通 UI 帧额外在软件 I²C ACTIVE、PD_ShouldDeferUI()（PD 交易、PHY 事件、EPR 维护、PPS 刷新前 50 ms）及 UI_OFF/唤醒静默时让行。普通 PD 帧让行不停止按键采集或遥测；启动 quiet 和休眠则清理输入事件。无补帧突发。
- 主循环在没有软件 I²C 活动且 PD_WantsFastPoll() 为假时 WFI，否则全速轮询；SysTick 1 ms 节拍/外设中断唤醒。IWDG 在初始化末尾开启，idle 钩子每轮喂狗。

### IRQ 优先级

USBPD 唯一抢占级 0，SPI/USART/I²C DMA、I²C EV/ER、SysTick 为抢占级 1，正常外设最坏两级嵌套。VTF0=USBPD、VTF1=USART TX、VTF2=USART RX、VTF3=SPI TX。统一在 APP/ch32x035_it.c 的 APP_IRQ_Init() 管理，保持 PD 最高优先级。

## 输入与输出功率策略

- PD Sink：优先选择 >20 V 的 PPS（目标上限 21 V、6.35 A 编码上限），否则最高不超过 20 V 的 SPR Fixed；EPR 标称 28 V/5 A，内部策略允许广告范围内的非标 Fixed ≤32 V / ≤7 A。非标申请并非产品额定功率保证。PPS 每 8 s 刷新，EPR KeepAlive 375 ms；详见 [PD 策略](Peripheral/PD/README.md)。
- 总功率预算：VBUS <8 V 为 0 W；PD 输入按合同整瓦功率的 95% 向下取整，合同电压/电流无效时为 0；DC 默认 270 W，Dashboard K1 每次减 10 W，60 W 后回到 270 W。
- 分配：设预算 B、各芯片测得整瓦功率 P_i、其它芯片总功率 others，则本芯片目标 min(cap_i,max(0,B-others))；cap 分别 140/65/65 W，未连接负载按 0 W。API 下发范围分别 18–140、12–71 W（SW3526 产品目标上限仍为 65 W）。这是各自可用上限，不是三个互斥预留份额，空载上限之和可能超过 B；亦非瞬时硬件总功率保护。UVP 预算为零仍受 API 最小值限制。
- SW3538 管理 TypeA+C 共享 VOUT；SW3526 各管理一个 Type-C。协议页提交掩码，800 ms 防抖后由电源镜像协程写入；启动及控制器离线恢复重新建立全使能基线，SW3538 两路快充通路同时使能。UI 不直接读写 I²C。

## 显示、菜单与温控

- SH1107 原生 80×128、逻辑 128×80，默认 R1，反向 R3，外部 VPP，内部电荷泵关闭。u8g2 1280 B 全帧缓冲加 1280 B DMA 快照；16 页 DMA 链，忙时合并中间帧。
- 启动进入 Dashboard；菜单根页 Dashboard / Settings / Statistics（系统数据）/ Dino Game / About；Settings 内含返回、Display、TypeA+C、TypeC1、TypeC2、System。默认简体中文，System 的 Language 开关可切英文；设置 RAM-only。
- Dashboard 始终 ASCII：首行温度、整数 VBUS、Wmax、UVP/NEG/EPR/PPS/SPR/DC；其下三列 TypeA+C / TypeC1 / TypeC2，输出 V（2 位）、A（3 位）、W（≥10 W 两位，否则三位）、协议。离线为 ---，无负载 NC，无快充 5V。数值 6 px 等宽、右对齐，内容或面板归属变化才提交帧。
- Statistics 每 200 ms 最多更新一次，6 项只读镜像：PDO（DC/NEG/SPR/PPS/EPR）、Req(V)、Cur(A)、VBUS(V)、Temp#1(C)、Temp#2(C)；合同未就绪/温度失效用 --，超宽数值用 OVF。
- 温度 #1 GX21M15U，#2 SW3538 外接 NTC；System 独立开关默认均启用。控制温度为有效且启用源的均值或唯一有效源；均不可用时 Dashboard 显示 --.-C，风扇全速（UVP 优先停转）。Statistics 独立显示两个有效原始镜像。
- 风扇 40 °C 启动、37 °C 停止，运行最小 duty 80；每升高 1 °C +2，最大 255。Fan +10'C 将两个温度点整体上移 10 °C。
- 自动休眠默认 1 min，Display → Sleep Options 可调；Keep On >5W 默认开，仅冻结自动倒计时。根菜单长按 K2 手动关屏，不停止 PD/遥测/风扇。新按键、PD/VBUS/端口插拔、功率跨 5 W 或相对基准累计变化 ≥2 W 唤醒；稳定大功率不反复唤醒手动关屏。
- K1=DOWN（导航/递增/跳跃；DC Dashboard 减功率）；K2 短按松开确认，约 800 ms 长按 BACK，一次触发。详见 [MiaoUI](APP/MiaoUI/README.md) 与 [低功耗框架](APP/framework/README.md)。

## Flash 约束与调试日志

- 唯一字库 font_fusion_pixel_p12_zhcn：209 字形、4,218 B（95 个 h12w6 等宽 ASCII、114 个 Fusion Pixel / Ark Pixel 汉字）。为数组源码大小，不是总 Flash 实测。扩充方式见 [I18N](APP/I18N.md)。
- 纯整数 printf（Project/Debug/debug.c），支持 %% %d %i %u %x %X %c %s 与 0/-/宽度；l/ll 按 32 位处理，单条上限 127 字符。不用 %f/软浮点、newlib malloc/assert/fprintf；随机数使用工程自带 rand/srand。UI 定点显示 UI_DATA_FIXED/UI_FmtScaled，动画 Q10。
- TIME_Micros() 保持硬件 64 位自由运行计数，用 32 位分段除法计算低 32 位微秒；不从低位 ticks 直接除。约 71.58 min 微秒回绕，须以 MAP 核对库辅助是否被剔除。

日志宏在 [APP/main.h](APP/main.h)，相互独立，修改后 Clean Build：

| 宏 | 默认 | 功能 |
|---|---:|---|
| DPC_DIAGNOSTICS_ENABLE | 0 | 详细 PD PDO/PHY、周期性 VBUS/PM、OLED/CFG |
| DPC_LOG_STARTUP_ENABLE | 1 | 启动、复位原因、时钟及总线 |
| DPC_LOG_POWER_ALLOC_ENABLE | 1 | 功率上限下发后变化日志 |
| DPC_LOG_FAN_CHANGE_ENABLE | 1 | 风扇初始/档位变化 |

精简 PD 合同、连接、超时与恢复事件仍输出，不是全局静音。I²C 临时首故障/快照宏和串口报告已删除；需对照历史诊断提交，而非启用无效宏。

## 验证与历史记录

- 历史硬件 I²C RX DMA/EV 修复曾通过 5 min 大功率测试；后续诊断全关版本 9611c8c 用户确认 SW3538/GX21M15U 通信正常。软件 I²C 9709592 用户确认偶发离线消失。仅适用于当时测试条件。
- 非标 PPS 曾重复超时/重连；先 RX-first，再 UI 让行。用户后续反馈 PD 目前还算稳定，未提供完整压力测试报告，不据此判定所有源/负载已验证。
- 本轮 UI 文案、字库静态审查完成；目标 Clean Build、MAP/ELF 与板端复测未执行。建议覆盖 DC/UVP、SPR/PPS/EPR 与维持、端口插拔/负载变化、双温度失效、休眠唤醒、双语滚动与方向、连续 >12 min（最好跨 71.58 min 微秒回绕）。
- 旧 README 的逐轮实验与历史 Flash 数据可从 [审查前版本](https://github.com/TKWTL/DPC27A-Desktop_PD_Power_Converter/blob/341b2c590010e1c9ccb11c7558f8f778ef4c64d2/Firmware/README.md) 追溯；详细故障复盘仍见 I²C、SoftI²C、PD 子文档。

## 模块文档

- [MiaoUI](APP/MiaoUI/README.md) · [I18N](APP/I18N.md) · [低功耗框架](APP/framework/README.md)
- [按键](BSP/Buttons/README.md) · [显示](BSP/Display/README.md) · [风扇](BSP/Fan/README.md) · [SW3538](BSP/SW3538/README.md) · [SW3526](BSP/SW3526/README.md)
- [PD](Peripheral/PD/README.md) · [I²C](Peripheral/I2C/README.md) · [SoftI²C](Peripheral/SoftI2C/README.md) · [SPI](Peripheral/SPI/README.md) · [Time](Peripheral/Time/README.md) · [USART](Peripheral/USART/README.md)
- [u8g2](ThirdParty/u8g2/README.md) · [第三方声明](../THIRD_PARTY_NOTICES.md)
