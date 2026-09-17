# Firmware architecture

```text
APP
  main -> app_tasks -> coroOS
       |-> PD task --------------------> Peripheral/PD
       |-> VBUS ADC task --------------> BSP/VBUS
       |-> UI task (MiaoUI) -----------> APP/MiaoUI -> BSP/Display -> Peripheral/SPI
       |-> I2C watchdog task ----------> Peripheral/I2C
       |-> Soft-I2C service -----------> Peripheral/SoftI2C (x2 handles)
       |-> SW3538 mirror task ---------> BSP/SW3538 -> hardware I2C
       |-> SW3526 #1 mirror task ------> BSP/SW3526 -> SoftI2C #1
       |-> SW3526 #2 mirror task ------> BSP/SW3526 -> SoftI2C #2

BSP
  board       pin-safe startup + factory-ISP reboot API
  VBUS        PA7 ADC measurement
  SW3538      protocol/port/ADC driver, handle based
  SW3526      protocol/port/ADC driver, multi-handle safe
  Display     SH1107 transport, IRQ-driven full-frame flush
  Fan         PB9 / TIM1_CH1 187.5 kHz PWM
  Buttons     K1/K2 debounce + edge/DAS state machine

Peripheral
  PD          SPR/EPR policy + CH32X035 USBPD port
  I2C         shared PA13/PA14 interrupt-driven DMA master + watchdog recovery
  SoftI2C     independent non-blocking GPIO I2C state machines
  SPI         remapped SPI1 TX + DMA1 CH3, hardware NSS chip select
  Time        free-running µs counter + 1 ms SysTick tick IRQ
  USART       PB10/PB11 asynchronous DMA console

Project
  Core        WCH device/SPL + system/core files
  Debug       self-contained printf/rand/sbrk runtime glue (no newlib stdio/float)
  Scheduler   stackless coroOS
  Startup     vector/startup assembly
  Ld          62 KiB Flash / 20 KiB RAM linker layout
```

## Scheduling

1. PD runs every scheduler pass.
2. VBUS is sampled every 20 ms and fed into the PD policy engine.
3. The SH1107 page chain and every hardware-I2C transaction are interrupt
   driven: the SPI TX-DMA completion interrupt advances the display pages, and
   the I2C1 event/error interrupts plus the TX/RX DMA interrupts carry a
   transaction from START to STOP. No scheduler service polls them.
4. A single 10 ms coroutine still owns the I2C watchdog: transaction timeout
   detection, bus recovery and the rare deferred START when the bus was busy.
5. Both software-I2C handles advance one timing edge/state per scheduler pass.
   GPIO-I2C therefore never busy-waits during normal SW3526 transactions.
6. SW3538 and both SW3526 mirrors are refreshed cooperatively at a 333 ms
   cadence (the GX21M15U temperature mirror stays at 500 ms).
7. Every pass ends in `APP_Tasks_Idle()`: the core executes `WFI` unless a
   software-I2C transfer is in flight (its edges advance once per pass, so a
   1 ms sleep would stretch a byte by two orders of magnitude). The 1 ms
   SysTick tick and every peripheral interrupt wake it, so the timed
   coroutines, the PD poll and the UI keep their cadence while the core is
   idle. The millisecond API itself is maintained by that tick interrupt
   (`TIME_TickHandler()`), while the microsecond API keeps reading the
   free-running counter.

All data required after a coroutine yield is stored either in the device handle
or in static task storage. Device drivers do not use a single global status
object, which is essential for the two SW3526 instances.

## Interrupts and VTF

The QingKe V4C PFIC runs with the two priority levels the two-level hardware
stack supports:

- preemption 0 - `USBPD` only, so the GoodCRC path may preempt any other ISR
  and never waits behind a bulk transfer;
- preemption 1 - SPI TX DMA1 CH3, USART TX/RX DMA1 CH4/5, I2C1 EV/ER, I2C
  TX/RX DMA1 CH6/7 and the SysTick tick; level-1 handlers never preempt each
  other, so the deepest normal nesting is two levels (DMA <- USBPD).

All ISRs are declared `interrupt("WCH-Interrupt-fast")` (hardware stack).
Four vector-table-free (VTF) entries skip the vector fetch on entry:
VTF0 = USBPD, VTF1 = USART TX, VTF2 = USART RX, VTF3 = SPI TX.
`APP_IRQ_Init()` (`APP/ch32x035_it.c`, called from `main()`) owns the priority
table and the VTF map; every new interrupt must be registered there too,
because IPRIOR resets to 0x00 and would alias it onto the USBPD level.

## Driver register naming

The SW3538/SW3526 headers intentionally mirror the existing SW6306 style:

- `*_STRG_*` — status/readback register;
- `*_CTRG_*` — control/configuration register;
- `*_ADC_SET_*` — ADC channel selector values;
- `*_StatusTypedef` — decoded/cached register mirror attached to a handle.

## Factory ISP reset

`Board_RebootToISP()` uses the CH32 factory boot-mode sequence: unlock
`FLASH->BOOT_MODEKEYR`, set `FLASH_STATR_BOOT_MODE`, clear reset-cause flags and
issue the keyed PFIC system reset. This reset-based handoff is preferred over a
direct jump into ROM while PD/I2C/DMA/application state is still active.

## Implemented baseline (moved from the root README)

- USB-PD Sink policy: SPR negotiation plus USB-PD 3.1 EPR entry; the request
  path is adaptive (Get_Source_Cap_Extended -> Source-PDP-based EPR Enter ->
  highest-voltage EPR Fixed PDO, non-standard voltages up to 32 V / currents up
  to the 7 A policy ceiling; nominal contract 28 V / 5 A);
- nine stackless coroutines (`APP/app_tasks.c`); the SPI display and every
  hardware-I2C transaction are interrupt driven (DMA1 CH3 page chain, I2C1
  EV/ER + DMA, GX21M15U shares the I2C1 bus with the SW3538);
- two independent GPIO soft-I2C buses drive the two fixed-address (0x3c)
  SW3526 controllers concurrently, each with its own handle/state;
- USART1 console at 921600 baud (DMA), VBUS on PA7 (75k/6.8k divider, feeds the
  PD disconnect check), full 20 KiB CPU SRAM (no PIOC reservation);
- `Board_RebootToISP()` jumps to the CH32 factory ISP on the next reset;
- MiaoUI (logical 128x80, R1/R3) with dashboard/menu/service pages and the
  UI_OFF low-power framework in `APP/framework`;
- removed modules are not placeholders: INA226, SSD1306, WS2812 and PIOC are
  absent from this hardware - see `Docs/PORTING_NOTES.md`.
