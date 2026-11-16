# 低功耗框架（APP/framework）

仅使用 UI_OFF，不实施 MCU 深睡/Standby。PD、I²C、快充芯片轮询、风扇仍运行；与主循环空闲 WFI 是不同机制。

核心源自 Pocket_PowerBank 与 NUEDC_2025B Firmware_0，冻结计时及手动关屏修复同步 IH01A。无 FreeRTOS，时基为 TIME_Millis()，单核协作式运行。

| 文件 | 职责 |
|---|---|
| pm_policy.c/.h | 状态转换，最大深度 UI_OFF |
| pm_controller.c/.h | 控制器、计时暂停/恢复 |
| pm_device.c/.h | 注册设备 prepare/suspend/resume 钩子 |
| pm_sleep_timer.c/.h | 32 位回绕安全空闲计时、disable/force |
| pm_api.c/.h | 应用接口、休眠阻止、唤醒与静默 |
| pm_device_builtin.c | OLED 关屏/亮屏与重绘 |
| pm_ui_register.c/.h | Display 的 Sleep Options 单选及 Keep On >5W |

## 当前行为

- 默认 60 s，Display → Sleep Options 9 档：No Auto Sleep、15 s、30 s、1/2/5/10/15/30 min；RAM-only。Keep On >5W 默认开。
- thread_pm 初始相位 200 ms、50 ms 周期，从镜像计算输出功率与插拔签名。稳定 >5 W 且开关开启时冻结自动倒计时；解除阻止恢复剩余时间，不直接归零或重置满周期。关闭开关清除该 block，不改变其它阻止位。
- 根菜单长按 K2 调用 pm_api_force_sleep() 并立即 poll；即使禁用自动休眠或稳定大功率，也允许手动休眠，force 保持到唤醒才清除。屏灭时 thread_ui 仍扫描按键，普通绘制停止；其它任务不停。
- 唤醒源：关屏 300 ms 防反弹后新按下；PD/VBUS/各输出口 attach/detach 变化；输出功率跨越 5 W 门槛或与基准累计变化 ≥2 W。稳定高功率不是反复唤醒事件。按键消耗整次唤醒手势，唤醒后名义约 100 ms 静默（受 50 ms poll 和状态推进影响）。
- pm_api_wake_from_activity() 用于活动唤醒，pm_api_refresh_idle() 用于正常按键重置计时，pm_api_ui_should_block() / unstable wake 供 UI 让行。

新增受管设备需注册钩子并避免长阻塞；深睡枚举仅保留，若实际启用需补充总线门控、唤醒源及板级验证。历史移植差异见 [此前文档](https://github.com/TKWTL/DPC27A-Desktop_PD_Power_Converter/blob/341b2c590010e1c9ccb11c7558f8f778ef4c64d2/Firmware/APP/framework/README.md)。本轮只更新说明，未执行新的板测。
