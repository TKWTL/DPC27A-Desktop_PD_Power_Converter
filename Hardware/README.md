# 硬件

Desktop PD Power Converter(桌面 PD 电源转换器)的硬件设计文件。整机共 **4 块 PCB**:
主板、背板、内面板、外面板;其中**背板与外面板为无元件面板板**,仅需 Gerber 打样,
无网表 / BOM / 贴片定位文件。

## 目录结构

```text
Hardware/
├─ source/                        设计源工程(LCEDA Pro / 嘉立创EDA专业版)
│  └─ ProPrj_Desktop_PD_Power_Converter.epro2
├─ Preview/                       预览与交换文件
│  ├─ SCH_Desktop_PD_Power_Converter_主板.pdf         原理图(主板)
│  ├─ SCH_Desktop_PD_Power_Converter_内面板.pdf       原理图(内面板)
│  ├─ Netlist_Desktop_PD_Power_Converter_主板.tel      网表(主板)
│  └─ Netlist_Desktop_PD_Power_Converter_内面板.tel    网表(内面板)
├─ Manufacturing/                 生产文件
│  ├─ Gerber/                     打样文件(4 块板都要打样)
│  │  └─ Gerber_Desktop_PD_Power_Converter_{主板,背板,内面板,外面板}.zip
│  ├─ CPL/                        贴片坐标(仅主板 / 内面板)
│  │  └─ PickAndPlace_Desktop_PD_Power_Converter_{主板,内面板}.csv
│  └─ BOM/                        交互式 BOM(浏览器打开,点选高亮定位)
│     └─ InteractiveBOM_Desktop_PD_Power_Converter_{主板,内面板}.html
└─ README.md
```

## 电源树(输入二选一 → VBUS → 辅助电源)

```text
【输入二选一】
Type-C PD 输入(USB4) ─► Q7(RUH4040M2) ◄─ 驱动 U9(MX5050T 理想二极管控制器) ┐
                                                                            ├─► 共漏 ─► F1(保险丝 0451020.MRL) ─► VBUS
XT30 输入(CN1)       ─► Q6(RUH4040M2) ◄─ 驱动 U5(MX5050T 理想二极管控制器) ┘

【VBUS 与辅助电源】
VBUS ─┬─► SW3538(U1,双口 A+C;外置半桥 Q2/Q3(BSC059N04LSG)+ L4(22 µH)为其降压级)
      ├─► SW3526 #1(U2) / #2(U8)(C1 / C2)
      └─► 降压转换器 U4(LGS5148)+ L6(10 µH) ─► VPP(9 V) ─┬─► 风扇接口 P1(低边 PWM Q1/AO3400A,L3(100 µH)+D9(BAT46WS)续流)
                                                         ├─► OLED VPP(内面板 OLED1.2)
                                                         └─► LDO U10(SPX3819M5-3.3) ─► 3V3 ─┬─► CH32X035(U7)、GX21M15U(U11)
                                                                                              └─► OLED VDD(内面板 OLED1.4)
```

- **输入二选一**:两路各由 **MX5050T 理想二极管控制器**(外置 **RUH4040M2** N-MOS)做
  OR-ing,共漏后过 **F1(保险丝,0451020.MRL)** 合并为 **VBUS**;两路同时插入时
  自动择一,防止反灌;
- **VPP(9 V)**:给风扇与 OLED VPP(经内面板)供电,并送入 3.3 V LDO;**3V3** 供
  CH32X035、GX21M15U 与 OLED VDD(经内面板),同时充当 I²C / CC 上拉电源。

## 打样 / 配单 / 贴片

1. **打样**:上传 `Manufacturing/Gerber/*.zip`(4 块板:主板 / 背板 / 内面板 / 外面板)给板厂;
2. **配单**:浏览器打开 `Manufacturing/BOM/InteractiveBOM_*.html`(主板 / 内面板),
   点击 BOM 表即可在 PCB 视图上高亮定位元件(可旋转 / 翻面),方便备料与手焊核对;
3. **贴片**:主板 / 内面板使用 `Manufacturing/CPL/*.csv`(贴片坐标 / 器件位号);
4. 完整物料表(BOM + BOM 外机械件)、分步装配流程与检查点见
   [`../Docs/ASSEMBLY_GUIDE.md`](../Docs/ASSEMBLY_GUIDE.md)。

## 备注

- 位号 / 型号 / 连接关系以 `Preview/` 下的网表与原理图 PDF 为准,本文电源树用于
  快速理解架构;
- 图片资源:[`../Images/`](../Images/);固件与目录说明:[`../Firmware/`](../Firmware/);
- 关键芯片资料(CH32X035 / SW3538 / SW3526 / SH1107 / GX21M15U)汇总在仓库根
  [`README.md`](../README.md) §13「资料与参考」。
