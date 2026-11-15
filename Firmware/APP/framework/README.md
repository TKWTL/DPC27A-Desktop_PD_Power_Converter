# 低功耗框架(`APP/framework`)

> 职责:提供电源状态机与 **UI_OFF** 休眠等级——无操作超时后关闭 OLED 并暂停 UI 绘制,
> 按键 / 插拔事件 / 输出功率活动将其唤醒;PD、遥测、风扇等其余线程不受影响。

## 来源与适配(2026-09-14 逐一对比后移植)

核心与两个同源工程逐文件对比过(git diff):

| 来源 | 取用内容 |
|---|---|
| `Pocket_PowerBank\firmware\Applications\framework` | **修复后的核心**:`pm_controller_resume_idle()` 的定时器刷新(否则阻塞解除瞬间就入睡)、`pm_sleep_timer` 重写(disable/force 旗标、64 位防溢出换算) |
| `NUEDC_2025B\Firmware_0\APP\framework` | **MiaoUI 接口**(`pm_ui_register.c` 的 Sleep 单选菜单)与 **UI_OFF 模式**(关屏而非深睡、任意键唤醒、唤醒后 ~100ms 按键静默) |

本工程适配:

- 无 FreeRTOS:`pm_sleep_timer` 改用 `TIME_Millis()`(32 位环绕安全比较),无临界区(单核协作式);
- `PM_MAX_SLEEP_DEPTH = PM_SLEEP_DEPTH_UI_OFF`:只启用 UI_OFF;深睡状态保留在枚举里备用;
- **force 旗标保持到 UI_OFF→RUN 的唤醒瞬间才清除**:参考工程在 DEEPSLEEP/STANDBY 分支清除,
  本工程不可达;若提前清,"No Auto Sleep + 手动休眠"会在下一轮弹回 RUN;
- 未移植深睡专用机制(唤醒数据预取门闩、睡眠总线门控、总线互斥锁判空)。

## 文件

| 文件 | 说明 |
|---|---|
| `pm_policy.c/.h` | 状态机(与两个参考工程逐字一致,仅默认深度不同) |
| `pm_controller.c/.h` | 状态机驱动 + 转换钩子(Pocket 版含 `resume_idle` 修复) |
| `pm_device.c/.h` | 设备钩子注册表(prepare/suspend/resume,按 `order` 排序) |
| `pm_sleep_timer.c/.h` | 空闲倒计时(No Auto Sleep = `disable()`;手动休眠 = `force_expire()`) |
| `pm_api.c/.h` | 应用接口:`pm_api_poll()/refresh_idle()/force_sleep()/set_sleep_timeout()/ui_should_block()` |
| `pm_device_builtin.c` | OLED 钩子:UI_OFF 关屏(`Disp_SetPowerSave(1)`)、唤醒亮屏 + 重绘 |
| `pm_ui_register.c/.h` | MiaoUI:设置页 `-Sleep`(紧随菜单项)→ `[Sleep]` 页(9 档超时单选);主菜单立即休眠改为**根菜单长按 K2=UI_BACK → `pm_api_force_sleep()`**(`core/ui.c`;旧 `-Sleep` 图标已删) |

## 行为

- 上电默认 **1 分钟**无操作进入 UI_OFF(菜单可改;掉电不保存,同两个参考工程);
- 唤醒源:任意键 / PD·VBUS·端口插拔边沿 / 输出总功率 **>5 W**(均在 `thread_pm` 扫描);
- 进入 UI_OFF 只是关 OLED + 暂停 `ui_loop()`(见 `thread_ui` 分支):PD、500ms 遥测、风扇照常;
- 唤醒瞬间 ~100 ms 忽略按键(唤醒键本身也被吞,不会连带触发菜单);
- `thread_pm` 以 50 ms 周期调用 `pm_api_poll()` 推进状态机(初始相位 200 ms)。

## 接口速查

见 `pm_api.h`。新增受管设备:实现 prepare/suspend/resume 钩子后在
`pm_device_register_builtin()` 注册(或覆盖弱符号 `pm_device_register_linker_set()`)。

## 注意事项

- **深睡未启用**:继续往下移植需实现 CH32X035 的睡眠入口/唤醒源,并把 `PM_MAX_SLEEP_DEPTH`
  提升到 DEEPSLEEP,同时补回唤醒预取与总线门控(参考 Pocket 工程);
- 睡眠设置(超时档位)不持久化;
- `PM_BLOCK_OUTPUT_POWER` 激活时会冻结倒计时；块解除时 `pm_controller_resume_idle()` 从冻结的剩余时间继续，避免"解除即入睡"，而不是把时间重新置满。


## 2026-11-14：同步 IH01A 的冻结倒计时与手动休眠修复

- 移植 [IH01A](https://github.com/TKWTL/IH01A-Compact_Induction_Heater) 的 `pm_sleep_timer.c/.h`、`pm_controller.c` 和 `pm_api.c` 修复：`PM_BLOCK_OUTPUT_POWER` 在测量输出功率 **>5 W** 时冻结倒计时，解除时从冻结的剩余时间继续，而不在归零后才阻止休眠。
- 根菜单长按 BACK 仍可执行 `pm_api_force_sleep()`，即使启用了 No Auto Sleep 或当前输出功率超过 5 W，也可进入 UI_OFF；普通持续功率采样不解除手动关屏。强制休眠立即运行一次 `pm_api_poll()`，不等待下一个 50 ms tick。
- **与 IH01A 不同：保留功率事件唤醒。** 按键新按下（沿用 UI_OFF 后 300 ms 防反弹）、PD/VBUS/输出端口插拔变化、输出功率跨越 5 W 门槛或相对上一次显著变化点累计变化至少 2 W，均通过 `pm_api_wake_from_activity()` 唤醒。高于 5 W 的**稳定输出**本身只冻结倒计时，不会每 50 ms 将手动关屏重新唤醒。为了降低遥测抖动对亮屏的影响，2 W 是本轮诊断固件的工程阈值，可依实测再调整。
- 屏幕只是 UI_OFF，PD 任务、SW3538/SW3526 轮询、风扇和 I²C 故障恢复不停止；未启用深度睡眠。
- **后续中文化参照 IH01A：** `Firmware/APP/MiaoUI/fonts/font_fusion_pixel_12_zh.c`（Fusion Pixel 12px，OFL）、`fonts/LICENSE-OFL`、UTF-8 文字宽度测量及菜单高亮布局；但 DPC27A 是 128×80 屏且 Flash 紧张，本次不导入字库或翻译。
- 本提交暂未用 MounRiver Studio II 编译、烧录或实测。

### 2026-11-15：Display 的功率阻止休眠开关

`-Settings → -Display → Keep On >5W` (UI_DATA_SWITCH，RAM-only，默认开启) 控制 `PM_BLOCK_OUTPUT_POWER`。开启沿用实测输出 >5 W 冻结自动倒计时；关闭立即清除该 block 位，下一次 50 ms PM poll 恢复倒计时。其它 block 位不受影响。按键/插拔/功率跨越 5 W 或相对基准累计变化 ≥2 W 唤醒仍保留；手动关屏不受阻止休眠开关影响。
