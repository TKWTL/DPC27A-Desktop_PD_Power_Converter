# 软件 I2C(SoftI2C)

> 职责:用 GPIO 实现句柄式 I2C 主机,为两颗固定地址的 SW3526 提供独立总线。

`soft_i2c.c/.h` 是面向本板两颗 SW3526 的 GPIO 软件 I2C 主机。

## 运行方式(非阻塞协作)

1. 用 `SoftI2C_TryWrite()` / `SoftI2C_TryWriteRead()` 发起传输;
2. 每个 coroOS 调度周期调用一次 `SoftI2C_Service()`;
3. 等待 `SoftI2C_GetResult()` 不再为 `SOFT_I2C_RESULT_ACTIVE`;
4. 用 `SoftI2C_TakeResult()` 取走最终状态。

每个 `SoftI2C_Handle` 独立持有引脚与传输状态,因此两颗同为 `0x3c` 地址的
SW3526 可以并发工作且互不干扰。

## 电气行为

- 开漏模拟:驱动只拉低,高电平靠释放 GPIO 由外部上拉电阻产生;
- 时钟拉伸(clock stretching)以有限超时轮询;
- `SoftI2C_Recover()` 提供显式同步 9 时钟总线清理,仅用于启动/故障恢复;
- 兼容用的阻塞封装(`SoftI2C_Write()` 等)仍然存在,但新代码应优先使用 Try/Service 路径。

## 2026-11-15：UI 加速后的偶发离线修订（待实测）

用户反馈：UI 8→6 ms 后、按键加速之前，两颗固定焊接的 SW3526 偶尔整列变为 `---`；项目代码在任意失败的底层 I/O 后将 status.online 清零。未捕获故障波形或结果码，不能认为已证明唯一根因。

- 原 `stretch_started_us` 从前一低电平阶段计时，UI/ISR 调度延迟可能被误算为从机拉伸；原边沿 deadline 也取自 GPIO 操作前。改为自由运行 SysTick 低32位（48 MHz/HCLK÷8，6 MHz）：初始化缓存 half_period_ticks、stretch_timeout_ticks；放开 SCL 后启动拉伸计时，后续只轮询；GPIO 操作完成或观察到 SCL 高后再安排下一边沿。5 us 半周期、1000 us 拉伸阈值不变；按 32 位模差处理回绕，不使用 64 位微秒除法。总线检查给 SDA 上拉留时间，STOP 完成需实际 SCL/SDA 均高。保留开漏模拟和原引脚。
- 调度顺序 soft_i2c_service 移到 UI 前。OLED 初始化及普通 UI 绘制在两路事务都非 ACTIVE 的空隙执行，power_mirror 仍在 UI 后可发下一笔；保留 6 ms 绘制目标、8 ms 键盘扫描/入队、2 ms 派发、333/500 ms 轮后采样和 USBPD 优先级。忙时帧待处理，不补帧；若单笔交易拖长，实际 UI 帧间隔可能超过目标。其它前台任务和中断仍有调度影响，未改为硬实时定时器。
- 恢复只在原有 TIMEOUT/BUS_STUCK 路径：确认 SCL 实际拉高才算一个恢复时钟，最多9个；共用约1 ms SCL 等待预算（另含固定半周期延时与中断执行），已空闲不再人为 STOP；需要 STOP 时先拉低 SCL，再拉低 SDA，确认 SCL 高后释放 SDA，最终检查两线。失败释放两线并记数。
- 调试器直接观察 `s_sw3526_bus1`、`s_sw3526_bus2`（APP/app_tasks.c 静态对象）：`fault_count`、`last_fault_result`（3=NACK,4=TIMEOUT,5=BUS_STUCK,6=ERROR）、`last_fault_state`（见 soft_i2c.h）、`last_fault_tx_stage`（0=写地址,1=数据,2=读地址）、`last_fault_tx_index`、`last_fault_rx_index`、`last_fault_lines`（bit0=SCL,bit1=SDA 高电平）、`max_service_gap_ticks`（除以6为名义 µs）、`recovery_count`、`recovery_fail_count`。记录保留到复位/Init；fault_count 计故障事件（若 NACK 后 STOP 又失败会计两次）。无串口诊断。在线判定不滤波，不重发写配置；现有功率分配和协议重上线行为不变。

验证：源码检查与边沿/拉伸/调度模型测试，未目标编译及实测。若仍复现，保留 RAM 故障记录和对应 ELF/MAP，空载抓 SCL/SDA 波形；避免根据临时日志改变时序而误判修复成功。
