# IH01A 中文化参考与 DPC27A 接口约束

2026-11-14。当前任务不实施中文化，仅记录下次移植时的真实参考位置。

- 参考仓库：[TKWTL/IH01A-Compact_Induction_Heater](https://github.com/TKWTL/IH01A-Compact_Induction_Heater)，尤其是 `Firmware/APP/MiaoUI/fonts/font_fusion_pixel_12_zh.c`、`fonts/LICENSE-OFL`、`fonts/fonts.h`、`ui_conf.h` 及 `core/ui.c` 中 UTF-8 字符串测宽和菜单/高亮排版实现。
- IH01A 的逻辑画布是 **128×64**，DPC27A 是 **128×80**；不可整文件覆盖 `ui_conf.h` 或显示驱动。
- DPC27A 当前为单个 ASCII 6 px 字宽字体，三列仪表盘依赖固定字符宽度；不能直接引入 IH01A 比例字宽而忽略数据栏的像素对齐。
- DPC27A 此前 MAP 估计 Flash 仅余约 5 KiB；采用少量精选中文字形，编译前统计独立 Unicode 字符、翻译字符串、UTF-8 代码，以及替换原字体后的**净增** Flash。每一轮须重新对比 `.map`。
- 引入 Fusion Pixel 需遵守其 OFL 许可；不修改 PD 核心、功率控制和 I²C 事务时序以换取语言支持。
- 本文记录未来实现方向，不表示 DPC27A 已启用汉字字体。
