# MiaoUI 产品界面移植

> 职责:提供产品级菜单/页面框架,适配 128×80 横屏 OLED 与双按键输入。

本目录是对 `Firmware_0.zip` 中 MiaoUI 源码的精简移植(保留首版点亮所需部分)。

## 目录构成

| 目录/文件 | 内容 |
|---|---|
| `core/` | 页面框架与主循环(`MiaoUi_Setup()`、`ui_loop()`) |
| `display/` | 显示适配(`dispDriver`,绑定 SH1107 + u8g2) |
| `indev/` | 输入适配(按键事件 → UI 事件) |
| `widget/` | 文本与数值/参数控件 |
| `fonts/` | 唯一字库 `font_fusion_pixel_p12_zhcn` |
| `images/` | 仪表盘、Statistics、设置、端口、烧屏、游戏等 30×30 XBM 图标（Statistics 来自 IH01A；Settings/System 共用配置图标，Display 复用烧屏图标） |
| `ui_conf.h` | 分辨率、默认旋转 `U8G2_R1`、字体与布局常量 |

> 产品页面函数不放在本目录,而是放在 `APP/Pages/`(例如 `dashboard.c/.h`)。

## 保留的能力

- 图标页与文本页;
- 剪裁窗口;
- 矩形/边框与圆角矩形/边框图元;
- 圆、直线、像素与 XBM 图片;
- 全缓冲文本渲染;
- 横屏旋转 R1/R3(产品使用 128×80);
- 背景色、对比度、横屏翻转等设置项。

## 开机 Dashboard 页

`MiaoUi_Setup()` 把 `-Dashboard` 项直接置为运行态(`ui->menuState = UI_ITEM_RUNING`),
上电即进入函数页而不是图标菜单。页面函数放在 `APP/Pages/`(不在本目录),
保持 `ui_conf.c` 只负责菜单树:

- `Pages/dashboard.c/.h`:`Dashboard_Page()`(开机仪表盘);

- 第 1 行:温度(已使能且有效的 Temp#1/Temp#2 融合,显示为 `25.3C` 一位小数,传感器离线时 `--.-C`)、
  输入电压(`20V`,整数伏)、总功率限制(`270Wmax`;数字 3 位右对齐固定 `W` 位置,
  UVP 0 W、PD 合同 95%、DC 用
  DOWN 以 10 W 步进调节、60 W 后回绕 270 W)、PD 输入状态(`UVP`/`EPR`/`SPR`/
  `NEG`/`DC`,输入 <8 V 显示 `UVP`,PD 未连接一律 `DC`;状态文本右对齐到屏幕右缘);
- 第 2~6 行:3 列 = SW3538(A+C 汇总:共享 VOUT、两口电流之和、芯片协议)、
  SW3526#1(`C1`)、SW3526#2(`C2`);每列依次为端口名、输出电压、电流、功率、协议
  (协议行:未接设备 `NC`、接了但无快充 `5V`、否则协议名——PD 固定档 `PD-FIX`、
  PPS `PD-PPS`;文本在列内居中,
  SW3538 列再右移 3 px);
- 数值右对齐到列右边界(43/85/127 px);功率 >=10 W 两位小数、<10 W 三位小数
  (普通浮点 printf:MiaoUI 本身已用 `%.2f/%.3f`);
- 数据取自已刷新的器件镜像,芯片离线时该列显示 `---`;内容签名不变就不重复刷屏,
  但 `Disp_GetFlushCount()` 变化时(菜单/fade/设置页刷过帧)强制重发本页;
- 吞掉导航键(DC 状态下 DOWN 调节总功率限制),按 K2(ENTER) 返回图标菜单。
- 入页时 `Disp_SetMaxClipWindow()` 恢复全屏剪裁窗口(菜单页会留下标题/数据区剪裁)。

> 温度由 `APP_ReadControlTemperatureX10()` 取自已启用且有效的两路镜像，四舍五入到 0.1 °C，
> UI 不直接访问 I2C。

## 服务项与设置项

菜单树(`ui_conf.c`)直接挂载,没有 "Tools" 二级菜单:

- Display 子页 `-Burn-in Test`(`Pages/service_pages.c` 的 `Burnin_Page()`):
  先显示说明(约 3 s,任意键跳过),随后整屏涂白用于烧屏观察,任意键返回菜单;
- 主菜单 `-Dino Game`(`Pages/game_dinosaur.c`,由 NUEDC_2025B Firmware_0 移植):
  小恐龙跑酷——K1(DOWN) 跳/失败后重开、K2(ENTER) 退出且**保留本次进度**
  (再次进入继续,不重开);无帧分频,UI 每一拍(6 ms)渲染一帧(理论上限 ~167 fps,受
  绘制耗时与刷屏合并影响);模拟独立按 16 ms 定时推进(`DINO_SIM_TICK_MS`,不受
  绘制耗时影响):每 tick 一个原版 16 ms 步长 + 半个分数步(奇数进位)→ 世界/起跳
  下落与原版同速(1:1)、分数为原版一半 —— 画面仍每拍重绘,比原版 osDelay(16)
  循环更顺;仙人掌/云按原版方式直接以(可能为负的)坐标
  交给 u8g2 绘制,可正常滚出左边缘;随机数用本工程自研 `rand()/srand()`
  (以 `TIME_Millis()` 播种);
- **主菜单长按 K2(ENTER) = BACK → 立即休眠**(`core/ui.c` 的 BACK 分支调用
  `pm_api_force_sleep()`,取代旧的主菜单 `-Sleep` 图标);子菜单/弹窗内长按回到
  上一级,函数页(dashboard/小恐龙/烧屏)内长按强制退出到菜单;
- 设置页 `-Sleep`(紧随 `[Home]` 菜单项后)→ `[Sleep]` 页:9 档超时单选
  (No Auto Sleep…30min,默认 1 min);休眠由 `thread_pm` 推进,详见 `APP/framework/README.md`;
- 设置页(紧跟在显示类设置项之后,由 `Add_Service_Items()` 注册):
  - ` Fan +10C`(`UI_ITEM_DATA`/`UI_DATA_SWITCH`,K2 直接翻转,数值列显示勾选框):
    风扇触发点/停止点整体 +10 °C(40/37 → 50/47 °C),回调
    `FAN_Control_SetTemperatureDelay()`;设置只存内存(与其它设置一致),
    `thread_fan` 在下一次 500 ms 轮询生效;
  - ` Reset Now`:`Board_SoftReset()`(PFIC 软件复位,BOOT_MODE 不动);
  - ` Reboot to ISP`:`Board_RebootToISP()`(下一次复位进入 CH32 出厂 USB ISP);
  - 临时 `Fan Test` 项已于 2026-09-14 删除:风扇改由 `thread_fan` 按
    `Services/fan_control.c` 的曲线自动调速。

## 输入约定

刻意只用两键:K1 = DOWN(页面导航;在数值编辑弹窗内递增数值),
K2 = ENTER/确认 **抬起触发**(`indevScan()` 在 K2 松开时才发 ENTER,按住不会连发),
**长按(约 800 ms,键仍按住时)发一次 UI_BACK**:子菜单/弹窗回到上一级、根菜单立即休眠;
长按过的那次按下的松开不会再触发 ENTER。唤醒用的那次按下属于电源框架
(UI_OFF 期间不跑 `indevScan()`,不受抬起触发影响),其松开不会误触发页面上的 ENTER。
Dashboard 页内 K1 被忽略,按 K2(短按)返回图标菜单,长按同样返回(强制退出)。

## 注意事项

- 本版本只链接 `font_fusion_pixel_p12_zhcn` 一个中英文比例字库;增加字体需同时更新链接列表与显存预算。
- 全屏自绘页(如 `APP/Pages/dashboard.c`)若要缓存帧,必须用 `Disp_GetFlushCount()`
  判断“面板是否还是自己的内容”:计数变化(菜单/淡入淡出刷过帧)时必须重发；
  只看自己的内容签名会在“退出菜单再进入”时漏帧(屏幕停在 fade 残留)。
- UI 运行在 `thread_ui`,启动即初始化屏幕与按键,**不依赖 PD 状态**
  (DC 输入、5 V 普通电源或 EPR 协商失败时界面都正常显示);
  刷屏由中断驱动,不会长时间占用调度器。

## 2026-11-14：Settings 图标子页与 Statistics

沿用 IH01A `Firmware/APP/MiaoUI/ui_conf.c` 的静态 `ui_page_t/ui_item_t`、`AddItem` 后立即 `AddPage` 的深度优先菜单注册风格，不引入动态分配。

- 主图标页依次包括 Dashboard、**Settings**、Statistics、Burn-in Test、Dino Game、About；Settings 位于 Statistics 前。
- 主 Settings (`UI_PAGE_ICON`) 首位为 `[Menu]` 返回图标，使用已有 `img_home`；其后依次为 TypeA+C、TypeC1、TypeC2 与第二层 Settings。两级 Settings **复用同一个** `img_configuration` 常量，不复制位图。第二层进入原先的 `UI_PAGE_TEXT` 设置页（Sleep、Display、Fan/Reset/ISP）。
- 三个端口设置页面的返回指向 Settings 图标页；文本设置页返回同样指向该页。图标页使用现有 BACK 操作返回上一级；根菜单 BACK→手动关屏行为不变。
- Statistics (`UI_PAGE_TEXT`) 采用 IH01A 的 `img_statistics` 30×30 XBM 资源（GPL-3.0-or-later），并显示 PD 已选 PDO 序号及 SPR/PPS/EPR、申请电压/电流、VBUS 实测值。未连接 / 协商中显示占位符，不将未完成协商报告为有效合同。
- 当前改为数值镜像（见下方最新修订），由 `Statistics_Poll()` 最多每 200 ms 更新，读取 PD 状态、PA7 VBUS ADC 与 GX21M15/SW3538 NTC 镜像，不发起 I²C 访问。标签使用较短 ASCII 文本以适配 DPC27A 的 128×80 屏及固定 6 px 字体；不移植 IH01A 的中文字体与 UI 像素布局。
- 图标素材沿用原仓库的 GPL-3.0-or-later 声明；本次只额外引入一张 Statistics 图（120 字节原始 XBM）。实际 Flash 差额需以编译后的 `.map` 为准。

尚未在目标硬件验证菜单层级、按钮返回与 PD 实时显示。

2026-11-15：Statistics 在 VBUS 后追加 ` GX21M15: xx.xC` 与 ` SW3538: xx.xC` 两个只读文本行，未就绪/离线/NTC 无效显示 `--.-C`；使用原生文本页滚动。UI 执行间隔 8→6 ms，独立按键 10 ms 和游戏 16 ms 不改；老化测试说明使用 2400 ms 实际时间。暂不接入中文化，i18n 清单仅增加两个保留英文的型号键。

### 2026-11-15：定点数值栏与按键时序（最新）

- `UI_DATA_FIXED`（旧名 `UI_DATA_FLOAT` 为相同枚举值）用 `int *ptr` 保存缩放后的整数，`decimals=0..3` 选择小数位；`UI_DATA_INT` 同用 `UI_FmtScaled()`，无浮点或 printf 格式化。例：2800/2→28.00、-1/1→-0.1、1250/1→125.0。可选 `valid` 指向 uint8_t，有效为非零，NULL 表示始终有效；数据指针空或无效显示 `--`。右栏 86..118 px，按 6 px 字宽右对齐，最多 5 字符，超宽显示 `OVF`，不截断数字；单位放标题内。兼容旧 RW 弹窗、开关、字符串项。RO 项 ENTER 不进入弹窗，BACK 正常返回。
- Statistics 6 项均为 UI_ITEM_DATA + RO：PDO 为 INT（左标签标明 DC/NEG/SPR/PPS/EPR），Req (V)/Cur (A)/VBUS (V) 为 FIXED 2 位；GX21M15 (C)/SW3538 (C) 为 FIXED 1 位。仍是 200 ms 镜像读取，无额外 I²C 访问，NTC 随 333 ms 延时电源镜像采样，GX21M15 随 500 ms 延时温度镜像采样。
- UI 6 ms 与按键 8 ms 独立调度，2 ms 协程派发；每次实际扫描生成一次事件，4 项队列向 UI 一次交付一项。无阻塞补帧、休眠/PD quiet 清队列，唤醒手势不进入菜单。长按 800 ms、K1 DAS 96 ms、K2 DAS 参数 128 ms（K2 UI 仍只有一次 BACK）、消抖 16 ms。实际时间受调度和绘制耗时影响。
- 尚未上板验证快速点击、菜单滚动、显示裁剪与编译占用。

### 2026-11-15：开关栏对齐和 i18n 边界

- `UI_DATA_SWITCH` 的空心外框及选中填充整体以 `UI_DATA_X1 - UI_FONT_HIGHT` 为左边界，与 INT/FIXED 右栏共用右缘。128 px、12 px 字高时 x=106（原 x=86，右移20 px），外框 12×12，填充内缩2 px；不改变纵坐标、裁剪、开关行为或滚动条。
- 未来中文化不改变 Dashboard 自绘页与图标菜单共用 `Dashboard_DrawStatusLine()` 的任何文本、数字格式、ASCII 字体和坐标；仅 `-Dashboard` 菜单入口可以翻译。具体边界见 [APP/I18N.md](../I18N.md)。

### 2026-11-15：Display/System 与温度融合（最新菜单定义）

- 根图标页：Dashboard / Settings / Statistics / Dino Game / About。Settings 图标页：[Menu] / Display / TypeA+C / TypeC1 / TypeC2 / System。Display 复用 img_burn_in，System 保留 img_configuration，两者均进入 TEXT 页。
- Display：[Return] / -Sleep Options / Keep On >5W（默认开）/ Contrast / Background / Screen Flip / -Burn-in Test。烧屏功能从根菜单移入；退出回 Display。
- System：[Return] / Fan +10'C / Use Temp#1 / Use Temp#2 / Reset Now / Reboot to ISP。两路温度参与开关默认开、独立、RAM-only；Fan +10'C 原功能及默认值不变。
- Statistics 温度两行改为 Temp#1 (C)、Temp#2 (C)，仍分别显示真实有效镜像（即使未选为控制源），失效显示 --。Dashboard 与风扇共用 APP_ReadControlTemperatureX10：两个有效且启用则取平均，一路可用则取该路，无可用则主界面 --.-C，风扇下一轮 500 ms 控制全速（UVP 优先停转）。不再保留启动/缺失宽限；采样周期及 NTC 换算不变。Dashboard 保持 ASCII，i18n 仅更新规划表。

### 2026-11-15：开关间隙、恐龙手感与 UTF-8 移植规划

- 开关外框左边界改为 `UI_DATA_X1 - UI_FONT_HIGHT - 2u`，现在 x=104（原106），填充 x=106，宽度12/8px不变，右边界116与 scrollbar 左边界118间隔2px。INT/FIXED 数字仍右对齐至118。
- Dino：16ms 模拟时基、分数速度与生成倒计时不变；地面、云和仙人掌共享世界位移的75%倍率，余数累加避免截断损耗。跳跃保留原39px最高点，旧24步/384ms扩展为48步/768ms（原即时起跳位移保持），4倍像素定点插入中点。腿动画原每6px切换，现每9px切换；结合75%世界速度，初始速度下48→96ms，每个速度档时间约为旧两倍。实际时长仍受协作式绘制/有限追帧影响。
- IH01A UTF-8 和选框只做源代码对照及计划，未启用 Unicode/字体/翻译/语言开关。见 [I18N.md](../I18N.md)，尤其保留128×80、图标顶栏和定点数字右对齐。

### 2026-11-15：正式双语适配与恐龙跳跃（最新）

- 实施 [I18N.md](../I18N.md)：System 的 Language 开关（英文默认，RAM-only），原英文键，100汉字+95ASCII的3655B U8g2 Unicode字库；完整原BDF不可用，字形来源及许可证见该文档。保留 Dashboard/图标实时顶栏/右侧数据的原 ASCII 字体和128×80、图标位置、定点右对齐、开关2px间隙。TEXT 选框高13px，左右各1px，真实文字宽度+2，不作宽度上限，title clip 裁剪。中文 About 换行、参数弹窗、烧屏提示及游戏文本已接入；纯ASCII About段落保持等宽，避免截断许可证。

- 2026-11-16：删除独立 ASCII 字库；所有页面使用 `font_fusion_pixel_p12_zhcn` 比例字体，Dashboard、数值栏和参数弹窗按实际像素宽度排版，About 文本压缩至六行，游戏结束页统一测宽居中。前述等宽布局为历史版本。目标编译及板测待验证。
- Dino 16ms模拟时钟不变。把前版48步/768ms跳跃降至36步/576ms（-25%），每原弧段3时间单位、每tick推进2时间单位，六倍像素定点插值并保持39px峰值、原即时起跳位移。世界75%倍率、走路初始96ms帧间隔、分数/障碍生成时钟不变。
- 未目标编译及实机验证；以上最新修订优先于前面的规划/旧参数记录。

- 2026-11-16 后续：唯一融合字体内的ASCII已换回原 `h12w6` 6px等宽字形；保留汉字与融合字体名称，撤销上一轮为比例ASCII实施的界面坐标变更（以此条为准）。
