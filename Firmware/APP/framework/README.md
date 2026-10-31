# DPC27A PM sync (PB02B reference)

**PM_MAX_SLEEP_DEPTH remains PM_SLEEP_DEPTH_UI_OFF.** UI_OFF only powers off
the OLED and stops MiaoUI rendering. USBPD, I2C, telemetry, fan, coroOS and the
CH32X035 MCU must keep running.

- Nonzero sleep block mask freezes the remaining timer, instead of hiding
  an expired deadline. All blockers must clear before the countdown resumes.
- A real input/output activity during blocking refreshes the frozen remainder
  to the full timeout. Without activity, 60 s minus 20 s = 40 s after unblock.
- No Auto Sleep and manual force are independent; force survives UI_OFF until
  wake and is cleared then, keeping existing wake-key suppression.
- TIME_Millis() 32-bit comparisons are wrap-safe for timeouts under 2^31 ms.
- PM registers OLED (order 10) and CH32X035 MCU (order 250). PB02B-style
  on_deepsleep/on_standby fields are placeholders: no MCU sleep entry exists.
- APP/algorithm/port_protocol_algo implements 800 ms mask debounce,
  offline-pending writes, reconnect defaults and SW3538 fast-charge path flags.
  The power-mirror coroutine remains the only SW35xx driver writer.
- APP/Services fan and power policy modules already avoid direct driver writes
  and remain unchanged.

Host C regressions: from Firmware/ run 'sh Tests/host/run.sh'.
Hardware tests must still cover UI wake, PD timing, controller reinitialization.
