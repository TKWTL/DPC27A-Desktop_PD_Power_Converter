# 固件(Firmware)

本目录沿用 `CH32X035_DemoBoard` 已验证的分层组织,但全部引脚/器件映射都基于本板自己的网表。

## 目录结构

```text
APP/                 入口、任务注册 + MiaoUI + 页面函数
  Services/          输出侧策略模块:总功率限制(`power_limit.c/.h`)、功率分配(`power_alloc.c/.h`)、风扇曲线(`fan_control.c/.h`)
  framework/         低功耗状态机与 UI_OFF 休眠(`pm_*.c/.h`,含 Sleep 菜单)
  MiaoUI/            精简菜单/UI 移植,单字体 + XBM 图标(仪表盘/设置/关于/烧屏/休眠/恐龙)
  Pages/             页面函数(Dashboard 仪表盘、`port_pages.c` 端口协议页、服务页、`game_dinosaur.c` 小游戏)
BSP/                 板级启动 + 器件驱动
  SW3538/            硬件 I2C 双口快充控制器驱动
  SW3526/            句柄式双实例快充控制器驱动
  VBUS/              PA7 ADC 输入母线电压检测
  Fan/               PB9 / TIM1_CH1 187.5 kHz PWM 风扇调速
  Buttons/           K1/K2 低有效按键消抖/输入状态机
  Display/           SH1107 传输层 + DMA 完成中断整帧刷屏
Peripheral/PD/       USB-PD Sink / PD3.1 EPR 策略与 PHY 后端
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
| `thread_ui` | 10 ms | 启动即点亮屏幕并进入 Dashboard 仪表盘(与 PD 状态无关);按键扫描 + MiaoUI 主循环 |
| `thread_soft_i2c_service` | 每轮 | 两路软件 I2C 各推进一步 |
| `thread_i2c_watchdog` | 10 ms | I2C 事务超时检测与总线恢复 |
| `thread_power_mirror` | 333 ms | SW3538 + 两个 SW3526 的状态/ADC 镜像刷新,并把总功率限制分配给各芯片功率上限 |
| `thread_gx21m15` | 500 ms | GX21M15U 温度镜像刷新 |
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
> 卡死后的整机复位可以从下次启动的 `[RESET] cause: … IWDG` 确认。

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
- 首屏是 **Dashboard 仪表盘函数页**(见下),图标菜单含 dashboard/设置/三个端口协议页
  (TypeA+C/TypeC1/TypeC2,`APP/Pages/port_pages.c`)/烧屏测试/小恐龙/关于,
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
- 数据全部取自器件镜像(功率 333 ms,温度 500 ms),UI 不做 I2C 访问:
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
  MiaoUI 数据项用 `UI_DATA_FLOAT`（`ptr` 指向缩放整数，`decimals` 为显示小数位），
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
