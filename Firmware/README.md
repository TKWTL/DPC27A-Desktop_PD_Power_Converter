# 固件(Firmware)

本目录沿用 `CH32X035_DemoBoard` 已验证的分层组织,但全部引脚/器件映射都基于本板自己的网表。

## 目录结构

```text
APP/                 入口、任务注册 + MiaoUI + 页面函数
  Services/          输出侧策略模块:总功率限制(`power_limit.c/.h`)、功率分配(`power_alloc.c/.h`)、风扇曲线(`fan_control.c/.h`)
  framework/         低功耗状态机与 UI_OFF 休眠(`pm_*.c/.h`,含 Sleep 菜单)
  MiaoUI/            精简菜单/UI 移植,单字体 + XBM 图标(仪表盘/Settings 子页/Statistics/关于/烧屏/恐龙)
  Pages/             页面函数(Dashboard 仪表盘、`port_pages.c` 端口协议页、服务页、`game_dinosaur.c` 小游戏)
BSP/                 板级启动 + 器件驱动
  SW3538/            硬件 I2C 双口快充控制器驱动
  SW3526/            句柄式双实例快充控制器驱动
  VBUS/              PA7 ADC 输入母线电压检测
  Fan/               PB9 / TIM1_CH1 187.5 kHz PWM 风扇调速
  Buttons/           K1/K2 低有效按键消抖/输入状态机
  Display/           SH1107 传输层 + DMA 完成中断整帧刷屏
Peripheral/PD/       USB-PD Sink 策略(PPS 优先 / PD3.1 EPR)与 PHY 后端
Peripheral/I2C/      PA13/PA14 I2C1 中断驱动事务引擎 + 看门狗恢复
Peripheral/SoftI2C/  句柄式协作 GPIO-I2C 引擎(供 SW3526 使用)
Peripheral/Time/     SysTick 自由运行µs计数器 + 1 ms 节拍中断
Peripheral/USART/    USART1 DMA 异步控制台
Peripheral/SPI/      重映射 SPI1 TX + DMA1 CH3 完成中断(硬件 NSS 片选)
ThirdParty/u8g2/     精简 u8g2 核心 + SH1107 TK078F288 后端
Project/Core/        WCH 器件库/SPL 与 system 文件
Project/Debug/       自研整数 printf + rand/sbrk/延时兼容层
Project/Scheduler/   coroOS
Project/Startup/     启动汇编
Project/Ld/          链接脚本(完整 20 KiB CPU RAM)
Project/obj/         构建产物(除 `.gitkeep` 外被忽略)
```

## 协作式任务总览(`APP/app_tasks.c`)

| 任务 | 周期 | 职责 |
|---|---|---|
| `thread_pd` | 每轮 | USB-PD 协议/策略推进 |
| `thread_vbus` | 20 ms | PA7 采样输入母线电压并喂给 PD 策略 |
| `thread_ui` | 绘制 6 ms / 扫描 8 ms | 启动即点亮屏幕并进入 Dashboard 仪表盘(与 PD 状态无关);按键扫描 + MiaoUI 主循环 |
| `thread_soft_i2c_service` | 每轮 | 两路软件 I2C 各推进一步 |
| `thread_i2c_watchdog` | 10 ms | I2C 事务超时检测与总线恢复 |
| `thread_power_mirror` | 333 ms（轮后延时） | SW3538 + 两个 SW3526 的状态/ADC 镜像刷新,并把总功率限制分配给各芯片功率上限 |
| `thread_gx21m15` | 500 ms（轮后延时） | GX21M15U 温度镜像刷新 |
| `thread_fan` | 500 ms | 自动风扇调速:UVP 停转 / 传感器失效全速 / 40(37)°C 迟滞 + 每 °C +2 |
| `thread_pm` | 50 ms | 低功耗状态机:`APP/framework`,空闲 1 min → UI_OFF 关屏;按键/插拔/>5 W 唤醒 |

> SPI 显示与硬件 I2C 事务完全由中断推进(DMA1 CH3 / I2C1 EV+ER / DMA1 CH6+CH7),
> 不再占用调度器线程;无栈协程不保存自动局部变量,
> 凡是在 `THRD_DELAY` / `THRD_YIELD` 之后还要用的数据,必须放在 static 存储或器件句柄里。
>
> 主循环每轮在 `APP_Tasks_RunOnce()` 之后调用 `APP_Tasks_Idle()`:没有需要全速推进的软件状态机时
> 执行 `WFI` 休眠,由 **1 ms SysTick 节拍**或任意外设中断唤醒。两种情况下保持全速轮询:
> 软件 I2C 传输进行中(边沿由调度器逐轮推进,休眠会把单字节拖慢两个数量级),
> 以及 `PD_WantsFastPoll()`(PD 已 attach:Source_Capabilities → Request 的响应延迟必须留在
> Source 的 ~24 ms 窗口内;实测 1 ms 休眠粒度会把 5 ms 拉长到 ~31 ms,Source 直接不回 GoodCRC)。
>
> 看门狗:`APP_Tasks_Init()` 末尾使能 IWDG(LSI,约 1–3 s),仅在 `APP_Tasks_Idle()` 每轮刷新;
> 诊断构建(`DPC_DIAGNOSTICS_ENABLE=1`)可从下次启动的 `[RESET] cause: … IWDG` 确认卡死复位；默认精简构建不输出该行。

## 中断与 VTF

- **抢占级 0(唯一)**:`USBPD` —— 可打断其它任何 ISR,GoodCRC 路径不被批量传输阻塞;
- **抢占级 1**:SPI TX DMA1_CH3、USART TX/RX DMA1_CH4/5、I2C1 EV+ER、I2C TX/RX
  DMA1_CH6/7、SysTick —— 同级互不抢占;**最坏嵌套 2 级**(DMA ← USBPD),
  正好为 V4C 的两级硬件压栈;全部 ISR 以 `WCH-Interrupt-fast` 声明;
- **VTF 免表入口**:VTF0=USBPD、VTF1=USART TX、VTF2=USART RX、VTF3=SPI TX;
- 单点维护:`APP/ch32x035_it.c` 的 `APP_IRQ_Init()` 持有优先级总表与 VTF 映射,
  由 `main()` 在启动早期调用;**新增外设中断必须注册进该表**(IPRIOR 复位值 0x00
  会把它误落在 USBPD 同级,拖慢 PD 响应)。

## 电源控制器驱动现状

- SW3538 通过 `Peripheral/I2C` 共享硬件 I2C1;驱动提供协议解码、端口/在线状态以及
  VIN/VOUT/双路端口电流 ADC 镜像。
- 每颗 SW3526 各自拥有独立的 `SoftI2C_Handle`,因此两个固定地址 `0x3c` 的器件
  可以同时使用,不依赖全局驱动状态。
- 器件 API 遵循 SW6306 命名惯例:`STRG` 表示状态/回读,`CTRG` 表示控制/配置;
  协程式 load 函数把解码后的状态缓存在句柄中。
- 在状态/遥测之外,驱动现提供协议使能掩码(`*_SetProtocolMask`)、功率上限
  (`*_SetPowerLimitW`)与保护状态读取;UI 端口协议页的改动经 `APP_PortProtocol_Touch()`
  防抖 800 ms,由 `thread_power_mirror` 执行写入(UI 永不直接触碰总线;掩码写会使
  在线设备重新协商)。SW3526 的 VOOC 位保留在驱动内,但不在 UI 提供开关。

`Board_RebootToISP()` 选择 CH32X035 出厂 BOOT 区并执行软复位,是应用层进入
WCH ISP 模式的接口。

被移除的参考模块不是占位符:INA226、SSD1306、WS2812 与 PIOC 仍然不存在。
u8g2 只是作为刻意精简的 SH1107 图形依赖回归,不再包含旧的 SSD1306/I2C 传输层。

## 风扇 PWM 与 SH1107 显示

- PB9 使用原生 TIM1_CH1 输出 187.5 kHz PWM。PSC=0、ARR=255 使 CCR1 与 8 位占空比
  一一对应(0..255 共 256 级);`FAN_PWM_SetDuty8()` 即该量程的唯一接口。
- 转速由 `thread_fan` 按 `Services/fan_control.c` 的曲线自动控制:UVP 停转、GX21M15U
  失效全速,平时 40 °C 启动 / 37 °C 停止,之后每升高 1 °C 占空比 +2(启动点 80,静音直跳)。
- SH1107 使用 SPI1 重映射 10:PA11 SCK、PA10 MOSI、PA12(硬件 NSS 片选)、PA9 D/C。
  SPI 时钟 12 MHz,SPI TX 使用 DMA1 CH3,整帧分页由 DMA 完成中断推进。
- 0.78 英寸面板由 TK078F288 80×128 原生 u8g2 后端驱动。`U8G2_R1` 得到产品需要的
  128×80 横屏画布;`U8G2_R3` 是反向横屏,`Disp_SetRotationMode()` 保留 R0..R3
  供台面方向测试。
- VPP 由外部提供。屏幕初始化发送 `0xAD,0x8A`,保持 SH1107 内部 DC-DC/电荷泵关闭。
- u8g2 占用一块 1280 字节全帧缓冲,中断驱动传输层另有一块 1280 字节 DMA 快照;
  帧按 page 分批发运,像素数据传输期间不会长时间占住前台调度器。
- MiaoUI 保留剪裁窗口、圆角矩形/框、XBM 图片、圆/线图元和文本渲染;
  当前只链接 `font_menu_main_h12w6` 一个字体。
- 首屏是 **Dashboard 仪表盘函数页**(见下)。主图标菜单顺序为
  Dashboard / Settings / Statistics / Burn-in Test / Dino Game / About；
  Settings 图标子菜单首项是 `img_home` 返回上级，后面依次为 TypeA+C、TypeC1、TypeC2
  (`APP/Pages/port_pages.c`)与第二层 Settings（进入原文字设置页），两层 Settings 共用
  `img_configuration`；Statistics 提供 PD 申请档位、请求电压/电流、VBUS 实测值，以及 GX21M15U 和 SW3538 NTC 温度。
  仪表盘与其它全屏页一样都只是被菜单调用的绘制函数;仪表盘上导航键
  被忽略,按 K2(ENTER,松开触发)返回图标菜单,长按 K2=BACK 同样返回。
  K1=DOWN,K2=ENTER(长按=BACK:子页回上级、根菜单立即休眠),两路 GPIO 使用内部
  上拉、电气低有效,沿用原有消抖/长按状态机。

## 开机仪表盘(Dashboard)

`MiaoUi_Setup()` 把首项 `-Dashboard` 直接置为运行态,所以上电后不再先画图标菜单,
而是进入 128×80 全屏遥测页(`Dashboard_Page()`,位于 `APP/Pages/dashboard.c`):

```text
--.-C 20V 270Wmax DC         <- 温度(GX21M15U,离线占位;在线如 25.3C) / 输入电压(整数伏) / 总功率限制 / PD 状态(右对齐)
      A+C       C1      C2 <- 第二行:各块端口名
   20.00V   20.00V   20.00V <- 第三行:输出电压(两位小数)
   1.250A   0.500A   0.000A <- 第四行:输出电流(三位小数)
   25.00W   10.00W    0.00W <- 第五行:输出功率(>=10 W 两位小数,<10 W 三位小数)
      PPS        PD       NC <- 第六行:协商协议(NC 无设备 / 5V 无快充;居中)
```

- 行高 12 px + 1 px 间隔:行基线 `y = 12 + 13*(行-1)`;列右边界 43/85/127 px,
  数值右对齐 —— 偏宽的数值占用 1 字符间隙,SW3538 列 7 字符可容下 `139.02W`。
- **总功率限制**(`Services/power_limit.c`):UVP(输入 < 8 V)固定 `0 W`;PD 固定为合同
  功率的 95%;DC 下按 DOWN 调节,上电默认 `270 W`(最大值),每次 -10 W,到
  `60 W` 后回绕到 `270 W`。该值由 `thread_power_mirror` 用于芯片功率分配;
  第一行显示为 `270Wmax`(数字 3 位右对齐,`W` 与旧 `140Wmax` 同一位置)。
- PD 状态栏:输入 < 8 V 显示 `UVP`;否则 `EPR/SPR/NEG/DC`(PD 未连接 = `DC`)。
- 第一行大约 21 字符:温度丢 `'`、电压取整就是为了在 `UVP`/`EPR`(3 字符)时
  也不溢出(22 字符会被屏幕右边缘截掉最后一字)。
- 数据全部取自器件镜像(功率 333 ms 延时,GX21M15 温度 500 ms 延时),UI 不做 I2C 访问:
  SW3538 列 = 共享 VOUT + A/C 两路电流之和 + 芯片协议;
  两个 SW3526 列 = 各自的 VOUT/IOUT/协议。芯片离线时该列显示 `---`;
  协议行:未接设备 `NC`、接了但无快充 `5V`,其余显示协议名,文本在列内居中
  (SW3538 列右移 3 px);第 1 行的 PD 状态右对齐到屏幕右缘。
- 数值用**定点整数**格式化：真实值 = 缩放整数 / 10^decimals（`%u.%02uV` 这种写法），
  **禁止 `%f`** —— 见下方「Flash 约束与 printf 规则」；
  内容签名不变就不重复刷屏,但 `Disp_GetFlushCount()` 一旦变化(菜单/fade 刷过帧)
  就强制重发本页——只看内容签名会在“退出菜单再进入”时漏帧。

## Flash 约束与 printf 规则(2026-09-12 定下)

- **不要用 `%f` / `float` / `double`**。本板无 FPU，软浮点库全套约 8.7 KiB；
  浮点显示统一用定点整数：`snprintf(buf, n, "%u.%02uV", mv/1000u, (mv%1000u)/10u)`；
  MiaoUI 数据项用 `UI_DATA_FIXED`（兼容 `UI_DATA_FLOAT`）（`ptr` 指向缩放整数，`decimals` 为显示小数位），
  渲染走 `UI_FmtScaled()`。
- **printf 是本工程自带的**（`Project/Debug/debug.c`）：纯整数格式化，支持
  `%% %d %i %u %x %X %c %s` + `'0'`/`'-'`/宽度；`l`/`ll` 修饰被接受但按 32 位处理
  （不要用它打印 64 位值）；单条输出上限 127 字符，超长会被截断。
- **不要调用 newlib 的 `rand()`/`assert()`/`malloc()`/`fprintf()`**：
  `rand()` 会拖进 assert→fprintf→reent stdio→malloc 整条链（实测 ~4.5 KiB）；
  随机数请用自带的 xorshift（`rand()` 已在本工程重实现，直接用即可）。
- MiaoUI 动画/PID 是 **Q10 定点**（`UI_FX()/UI_FXI()`，`ui_conf.h`）：
  调用 `UI_Animation()` 传值/取值都要带 `UI_FX()`，真正画图时再 `UI_FXI()` 转回像素；
  `kp/ki/kd` 也按 Q10 填（0.25 → 256）。
- 改完请用 map 复核：库占用（`_`/`__` 开头符号）应只剩 `_write`/`_sbrk` 与少量
  libgcc 移位辅助。


## 串口输出与故障诊断预编译开关（2026-11-14）

开关集中位于 [`APP/main.h`](APP/main.h)。普通构建默认**保留三类实用日志**，但关闭详细运行日志：

```c
#define DPC_DIAGNOSTICS_ENABLE       0
#define DPC_LOG_STARTUP_ENABLE       1
#define DPC_LOG_POWER_ALLOC_ENABLE   1
#define DPC_LOG_FAN_CHANGE_ENABLE    1
```

各宏均相互独立，且只接受 `0` / `1`。修改后必须执行 **Clean Build**，尤其是 `APP/app_tasks.c` 和 `Peripheral/I2C/i2c_api.c` 及包含 `main.h` 的文件，避免旧目标文件混用。

| 宏 | 默认 | 控制范围 |
|---|---|---|
| `DPC_DIAGNOSTICS_ENABLE` | **0** | 额外的每秒 VBUS/PM、OLED 就绪、端口协议配置 `[CFG]`、PD 报文/PDO/PHY 详细诊断 |
| `DPC_LOG_STARTUP_ENABLE` | **1** | 启动标题、`[RESET]` 复位原因、主频/ChipID、UART/SPI/风扇 PWM/I²C/Soft-I²C 信息及 PD 输入策略参数 |
| `DPC_LOG_POWER_ALLOC_ENABLE` | **1** | `[PWR]` 功率分配变更：仅当对至少一颗在线芯片实际下发新的功率限制后打印一次；不按轮询周期重复打印未变化的结果 |
| `DPC_LOG_FAN_CHANGE_ENABLE` | **1** | `[FAN]` 风扇 PWM 档位变更：只有初始应用或 PWM 值变化时打印温度、在线状态、UVP 和新档位；不按 500 ms 周期重复打印相同档位 |

例如默认构建会输出 `[RESET]`、`[PWR]` 和 `[FAN]`，但不会输出周期性 `[VBUS]` / `[PM]`；设置 `DPC_DIAGNOSTICS_ENABLE=1` 只会额外打开详细诊断，**不会改变上述三个独立宏的取值**。关闭启动、功率或风扇日志时只需单独把对应宏设为 `0`。

这些宏**不是全固件的串口静音总开关**：精简 PD 连接、合同、重置、回退和故障事件仍默认输出，详细 PDO/PHY/协商过程由主开关控制。功率分配/风扇的实际运算和执行不依赖日志开关。各配置的 Flash/RAM 消耗以相同工具链生成的 `.map` 为准。

### 硬件 I²C 修订后的实测与日志清理（2026-11-15）

用户反馈：在提交 `9611c8c`、旧 master/snapshot/periodic/first-fault 四项均为 0 的构建中，SW3538 与 GX21M15U 数据正常，无异常输出或无法读取现象。该反馈支持驱动修订有效，但未提供长时间压力测试结果，亦不能证明唯一根因。历史诊断记录保留在 [I²C 文档](Peripheral/I2C/README.md)。

本轮退役临时诊断：删除三个实验宏、I²C 首故障锁存、扩展快照及 `[I2C]`/`[I2C0]` 打印。常用启动/功率/风扇日志与原详细日志主开关保留。看门狗、STOP 握手、错误处理、PFIC 中断保护、DMA 与内存屏障保持实测基线的精简路径；保留必要的 MMIO 读取。旧实验宏不再有效。清理后的源码仍需 Clean Build 与板端复测。

### I²C 修复笔记（2026-11-15）

- 现象：关闭详细诊断和首故障锁存时 SW3538 与 GX21M15U 频繁离线，开启后变稳。两份 ELF/MAP 确认实际参与 -Os/-flto；全局去 LTO 链接曾溢出 668 B，不能将 LTO 直接认定为根因。
- 失败 ELF 硬件断点 0x5982 捕获看门狗时间差 131 ms（4933→5064），状态 WAIT_ADDR_TX、owner=SW3538、地址 0x3C、TX=40 05，CTLR1=0401、CTLR2=0330、STAR1=STAR2=0，DMA6/7 尚未进入本次传输。该次为真实状态停滞而非时间差下溢，软件 active 与硬件空闲失步；调试器读取状态寄存器可能有副作用。
- 修订 `9611c8c`：一次正常事务只请求一次 STOP，避免对自清 START/STOP 位读改写；确认 STOP/START/BUSY/MSL 均清零再发布成功，保持 tBUF=2 us；DMA 接管前关 EV、先配置通道再释放 ADDR，检查异常/丢失主机身份；前台看门狗用 I²C 专属 IRQ 保护、重新采样时间与状态，使用有符号模差并添加内存屏障。保留 400 kHz、USBPD 高优先级、DMA 通道与恢复机制。
- 用户反馈：全部诊断关闭后两设备均正常，随后 `3c90fbb` 清理临时诊断。实测支持组合修复有效，未证明 STOP 为唯一根因；不再依赖诊断代码改变时序。详细历史见 [I²C 驱动笔记](Peripheral/I2C/README.md)。

### 历史 Flash 容量基线

关闭诊断锁存的历史 MAP（`Desktop_PD_Power_Converter(1).map`，对应无编号 ELF，修复前，-Os/-flto=auto）显示：Flash 62 KiB，.text 58,424 B（机器指令约 47,170 B、只读资源 11,249 B、对齐 5 B），.init/.vector 256 B、.data 156 B，合计 58,836 B，剩余 4,652 B；.bss 11,340 B、.noinit 4 B、栈 2,048 B，计入对齐后 RAM 13,552 B。LTO 合并字符串池 6,448 B，其中明显带 [PD] 前缀的字符串至少 2,668 B；字体 font_menu_main_h12w6 1,200 B，10 个菜单图标 1,200 B，恐龙游戏独立图像资源 928 B。以上不是新固件的测量；未通过本轮源码清理承诺节省量。

### Flash 优化与中文化预备

- PD 详细日志改为主诊断宏控制，默认保留简短事件，协议状态机及 PHY 不改。源码直接 printf 格式串（去重含 NUL，未计共享/合并）由 3235 B 降至 529 B（保守统计跨条件编译的分支，非实际链接结果）。最终以同配置 Clean Build 的 MAP 为准。
- [APP/I18N.md](APP/I18N.md)：英文原文为键的翻译清单与字库估算，当前 73 项、94 个去重汉字，尚未接入运行时。

### 2026-11-15：遥测与 UI 刷新、双温度

- 电源镜像从原每轮 I/O 后延时 333 ms 改为 500 ms 起始目标周期（I/O耗时计入；过载/PD quiet 时不保证每 500 ms 都能拿到新样本），GX21M15U 同样按起始周期 500 ms；功率分配与协议配置沿用此任务。UI 帧延时 8→6 ms，键盘保持独立 10 ms 时基，游戏 16 ms 模拟周期不变，老化测试说明改为真实时间 2400 ms。
- SW3538 ADCLoad 新增 NTC 通道与前后电流档位校验，26 项整数表 52 B；Statistics 每 200 ms 只读镜像，追加 GX21M15、SW3538 两行（超出屏幕可滚动），不新增 UI I²C 事务；风扇反馈仍只使用 GX21M15U。详见 [SW3538 驱动](BSP/SW3538/README.md)。
- 新功能仅源码及曲线数值验证，尚未目标编译和板端复测。

### 2026-11-15：恢复采样间隔、独立输入时基和数值栏

- 恢复 `d1c10a5` 前的采样调度：SW3538/双 SW3526 每轮完成后 `THRD_DELAY(333)`，GX21M15 每轮完成后 `THRD_DELAY(500)`；SW3538 NTC 新功能保留，随电源镜像采样。不是固定起始周期；实际采样间隔另含 I/O 耗时。
- UI 仍以 6 ms 为目标绘制间隔，输入以 8 ms 为目标扫描间隔；协程 2 ms 派发，按时间分别调度，迟到仅有限补计数而不补造 GPIO 采样。消抖/长按/K1 DAS/K2 DAS 为 16/800/96/128 ms，连击窗口 400 ms（名义值）；拆分消抖与长按/DAS 计数器，避免释放抖动重置长按时间。
- 输入适配仅在有效扫描后入队，每个动作消费一次，4 项队列满优先保留 ENTER/BACK；INIT/EXIT 不抢消费，休眠、唤醒静默期和 PD quiet 清队列/手势，未看到按下的释放不确认。
- Statistics 用 6 个只读 INT/FIXED 数值及有效标志，标签和单位在左，右侧右对齐。不使用整行运行时字符串；通用整数格式器处理 0~3 位小数、负零附近值、INT32_MIN、无效值 `--`、超宽 `OVF`。RW 弹窗共享格式器，保留原 `UI_DATA_FLOAT` 枚举别名和 STRING/SWITCH 行为；只读项 ENTER 不进入弹窗。
- 此轮未目标构建/板测，不改变 I²C、PD、风扇和 NTC 换算实现；按键在协作式任务被阻塞期间仍可能错过完整物理脉冲。实际 Flash 和 GUI 帧率以实测为准。

### 2026-11-15：Flash 实测反馈与 UI 对齐

用户反馈中文化前 text=54,996 B、data=76 B（合计55,072 B），62 KiB Flash 名义剩余8,416 B；未提供新 MAP，不能精确确认其他段/对齐。UI_DATA_SWITCH 边框及填充统一右对齐数值栏，Dashboard 自绘页面及共用顶栏固定 ASCII/原字体，不纳入未来 i18n；菜单入口仍可翻译。此次仅布局与文档修改，未启用中文化；目标编译和板端验证待完成。

### 2026-11-15：软件 I²C 与 UI 加速偶发离线

用户报告 UI 加速后两路 SW3526 偶尔整列 `---`，发生早于按键时基调整。修正 SoftI2C 实际 SCL 释放后的拉伸计时、边沿时间戳、STOP/恢复检查，普通 UI 绘制避开在途软件 I²C 事务；不降低扫描速度，不改遥测周期、硬件 I²C、PD 或功率策略。增加每路 RAM 故障记录，无打印、无离线滤波。详见 [SoftI2C 修订笔记](Peripheral/SoftI2C/README.md)。本轮待目标构建、板端验证。

## 模块文档

- [`APP/framework/README.md`](APP/framework/README.md) — 低功耗框架(UI_OFF 休眠)
- [`APP/MiaoUI/README.md`](APP/MiaoUI/README.md) — UI 框架与输入适配
- [`BSP/Display/README.md`](BSP/Display/README.md) — SH1107 显示 BSP
- [`BSP/Fan/README.md`](BSP/Fan/README.md) — 风扇 PWM
- [`BSP/Buttons/README.md`](BSP/Buttons/README.md) — 按键状态机
- [`BSP/SW3538/README.md`](BSP/SW3538/README.md) — SW3538 驱动
- [`BSP/SW3526/README.md`](BSP/SW3526/README.md) — SW3526 驱动
- [`Peripheral/PD/README.md`](Peripheral/PD/README.md) — USB-PD 策略/PHY 分层
- [`Peripheral/I2C/README.md`](Peripheral/I2C/README.md) — 硬件 I2C1 中断驱动事务引擎
- [`Peripheral/SoftI2C/README.md`](Peripheral/SoftI2C/README.md) — 软件 I2C 引擎
- [`Peripheral/SPI/README.md`](Peripheral/SPI/README.md) — 显示 SPI + DMA 完成中断 + 硬件 NSS
- [`Peripheral/USART/README.md`](Peripheral/USART/README.md) — 异步串口
- [`ThirdParty/u8g2/README.md`](ThirdParty/u8g2/README.md) — 精简 u8g2
