# MiaoUI 产品界面

MiaoUI 精简移植，适配 SH1107 128×80 横屏及双按键。页面函数在 APP/Pages/，菜单树在 ui_conf.c，无堆分配。显示层支持裁剪、像素/线/矩形/圆、XBM 和双语文本。

| 模块 | 职责 |
|---|---|
| core/ | MiaoUi_Setup、ui_loop、导航、动画和选框 |
| display/ | u8g2/SH1107 适配、异步刷屏 |
| indev/ | 按键事件适配及四项队列 |
| widget/ | TEXT、数据项、参数/开关弹窗 |
| fonts/ | 唯一字库 font_fusion_pixel_p12_zhcn（6px 等宽 ASCII / 12px 汉字） |
| images/ | 30×30 XBM 菜单图标 |
| ui_conf.c/.h | 128×80、R1/R3、菜单树及布局常量 |

## 当前菜单树

```text
[Menu]
  -Dashboard  (上电默认进入)
  -Settings [图标页]
    [Menu]
    -Display [文本页]
      [Return]
      -Sleep Options [No Auto Sleep, 15s, 30s, 1/2/5/10/15/30min]
      Keep On >5W
      Contrast / Background / Screen Flip
      -Burn-in Test
    -TypeA+C (中文：TypeA+C设置) [PD, QC2.0, QC3.0, QC3+, QC4+, FCP, SCP, PE, SFCP, AFC, TFCP]
    -TypeC1  (中文：TypeC1设置) [PD, QC2.0, QC3.0, FCP, SCP, PE, SFCP, AFC]
    -TypeC2  (中文：TypeC2设置) [同 TypeC1]
    -System [文本页]
      [Return]
      Fan +10'C / Use Temp#1 / Use Temp#2 / Language
      Reset Now / Reboot to ISP
  -Statistics (系统数据)
    [Menu]
    PDO / Req (V) / Cur (A) / VBUS (V) / Temp#1 (C) / Temp#2 (C)
  -Dino Game (小恐龙)
  -About
```

System/Settings 共用配置图标，Display 复用烧屏图标；Statistics 图标来自 IH01A。默认简体中文、设置 RAM-only；重启至ISP模式进入出厂 ISP。协议族控制异步提交，800 ms 防抖，UI 不直接触碰总线，控制器重新上线重设全使能基线。SW3526 VOOC 驱动能力不等于菜单开关。

## Dashboard 与系统数据

- MiaoUi_Setup() 直接运行 Dashboard_Page。K1 仅在 DC 且非 UVP 时按 10 W 步降低预算；K2 短/长按返回根菜单。
- 首行是融合温度、整数输入伏数、Wmax、输入状态 UVP/NEG/EPR/PPS/SPR/DC（ASCII，不翻译）。
- 三列 TypeA+C、TypeC1、TypeC2；输出电压两位、电流三位、功率 ≥10 W 两位、<10 W 三位小数；6 px 等宽，右界 43/85/127，基线 12+13n。离线 ---、未连接 NC、无快充 5V；PD 固定/可调分别 PD-FIX/PD-PPS。SW3538 共享电压及两路电流和，协议居中偏移 3 px。
- 源数据为镜像；Disp_GetFlushCount() 变化或内容签名变化时重发 Dashboard，避免返回页面漏刷。进入全屏页应恢复最大剪裁窗口。
- Statistics 是 6 个只读 UI_ITEM_DATA，PDO 为 INT，Req（请求电压）/Cur/VBUS（输入电压）两位 FIXED、两路温度一位 FIXED；200 ms 目标更新，未就绪/失效 --，超宽 OVF，不自行 I²C。

## 数据控件与布局

UI_DATA_FIXED（旧名 UI_DATA_FLOAT 同枚举）指向 int 缩放整数、decimals 0..3；UI_FmtScaled() 支持符号和定点格式，不走浮点。valid 非 NULL 时非零表示有效；RO 项 ENTER 不弹窗。数值区 86..118，按 6 px 计算宽度（最多 5 字符）；左 title clip 0..84，选框高 13 px，实际译文字宽+2，随选中项滚动。SWITCH 外框 x=104、12×12，填充内缩 2 px，右边缘 116 与 scrollbar 118 间隔 2 px。参数弹窗分离中文标签与 ASCII 数值。UI_Animation 使用 Q10。

## 输入与帧调度

K1 DOWN 导航/递增；K2 松开确认，长按约 800 ms 一次 BACK，抑制后续松开确认。根图标页 BACK 手动关屏，子页返回，函数页按各自处理逻辑退出。按键 8 ms、消抖 16 ms，K1 DAS 96 ms，K2 长按不重复确认；每次真实扫描调用入队，动作一次消费，4 项队列满优先确认/返回。启动 quiet、休眠/唤醒静默清队列，避免虚假 ENTER。

thread_ui 上电 200 ms 后初始化（等待 SoftI2C 空闲）；派发 2 ms、目标帧 6 ms；软 I²C ACTIVE、PD 交易及 PPS 刷新前 50 ms 暂缓普通帧，不补帧。PD 普通帧让行仍保留输入采集，启动 quiet 不同。非硬实时；长绘制/其它协程可能延迟采样，不能保证捕获阻塞期间的完整短脉冲。

## 服务页

- Burn-in Test：Display 内，说明 2400 ms（可由按键操作跳过），随后白屏，任意键退出。
- Dino Game（小恐龙，分数前缀 HI 不翻译）：K1 跳跃/失败重开、K2 退出并保留进度；16 ms 模拟时钟、每次最多补 3 tick，世界位移为原版 75%、分数半速；跳跃 36 tick=576 ms，原 39px 高度不变，腿动画每 9 px。中文结算采用实际测宽、13 px 基线间距；英文保留原坐标。
- About：英文/中文按整码点及 120 px 测宽折行，12 px 行距，最多 6 行；当前内容含 Git 路径、支持 PD/QC/SCP 等快充描述。字符及授权见 [I18N](../I18N.md)。

更改字体/布局需 Clean Build、检查 MAP、实测双语和两种显示方向。历史试验见 [审查前文档](https://github.com/TKWTL/DPC27A-Desktop_PD_Power_Converter/blob/341b2c590010e1c9ccb11c7558f8f778ef4c64d2/Firmware/APP/MiaoUI/README.md)；本页为当前源码说明。
