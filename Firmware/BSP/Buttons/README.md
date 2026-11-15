# 按键输入(Buttons)

> 职责:把 K1/K2 的电气电平转换成“短按/长按/释放/连击”事件,供 UI 层消费。

## 硬件映射

| 按键 | 引脚 | 索引 | 电气特性 |
|---|---|---|---|
| K1 | PA5 | `KeyIndex_Down` | 低电平有效,`Board_Init()` 打开内部上拉 |
| K2 | PA6 | `KeyIndex_Enter` | 低电平有效,同上 |

`Board_Key1Pressed()` / `Board_Key2Pressed()` 把低有效电平规范化为 `1 = 按下`。

## 主要接口

| 接口 | 说明 |
|---|---|
| `Key_Init()` | 清空按键状态 |
| `Key_DebounceService_8ms()` | 8 ms 节拍:递减消抖/长按/连击计时 |
| `Key_Scand()` | 扫描 IO 并更新状态机(建议每个 8 ms 节拍调用一次) |
| `Key_EdgeDetect(KeyIndex_t)` | 取边沿:`Rising`(按下)/`Falling`(释放)/`Holding`(长按) |
| `KEY_GetState(KeyIndex_t)` | 当前状态:`None` / `ShortPress` / `LongPress` / `Release` |
| `KEY_GetDASClick()` / `KEY_GetClickTimes()` | 连击(DAS)计数查询 |

## 参数与实现要点

- 消抖 16 ms(`DEBOUNCE_TIME_8MS = 2` 个 8 ms 节拍);
- 长按 100 个节拍(约 800 ms);进入长按后重复触发(DAS):K1(DOWN)间隔 12 个节拍
  (96 ms;菜单滚动/数值调节/Dashboard 功率调节的步进速率),K2(ENTER)间隔 16 个节拍(128 ms);
- 连击超时 50 个节拍(400 ms),超时后连击计数清零;
- 状态机开销恒定、不阻塞,由 `thread_ui` 每 8 ms 驱动一次。

## 注意事项

- 按键读取回调经 `board.c` 间接访问 GPIO,本模块不直接操作寄存器;
- MiaoUI 的输入适配位于 `APP/MiaoUI/indev/`,映射关系:K1 = DOWN,K2 = ENTER。

- 消抖计数和长按/DAS 使用独立计数器；按下期间的短暂释放抖动不得把长按计数缩短为消抖时间，待释放确认时不产生长按。
- `thread_ui` 以 2 ms 为派发等待，按 8 ms 时间网格递减计数、采样一次 GPIO；UI 自己使用 6 ms 帧网格。
- MiaoUI `indevPoll()` 每次实际扫描调用一次，将动作排入 4 项有界队列；`indevScan()` 出队，队满优先保留确认/返回，休眠/协商屏蔽时 `indevReset()`。不能保证捕获阻塞期间发生并结束的完整脉冲。
