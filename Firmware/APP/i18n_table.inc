/* DPC27A UI translation inventory (UTF-8). Same { English, zh-Hans }
 * initializer convention as IH01A. Included by APP/i18n.c (display only).
 * Exact English keys; preserve prefix punctuation and format specifiers.
 * Do not translate internal identifiers, CLI, USB-PD protocol names or telemetry.
 * Dashboard_Page and its shared Dashboard_DrawStatusLine remain ASCII with
 * the original font/layout in ALL languages. Only the -Dashboard menu entry
 * is translated; no dashboard screen content belongs in this table.
 * Runtime format templates below require translation before formatting.
 */
/* Root/menu */
    { "[Menu]", "[主菜单]" },
    { "-Dashboard", "-仪表盘" },
    { "-Settings", "-设置" },
    { "[Settings]", "[设置]" },
    { "[Return]", "[返回]" },
    { "-Statistics", "-系统数据" },
    { "[Statistics]", "[系统数据]" },
    { "-Dino Game", "-恐龙游戏" },
    { "-About", "-关于" },

/* Ports (technical identifiers deliberately unchanged). Fallback already
 * returns the original, so omit these identity rows from the runtime table. */
#ifndef DPC_I18N_RUNTIME_TABLE
    { "-TypeA+C", "-TypeA+C" },
    { "[TypeA+C]", "[TypeA+C]" },
    { "-TypeC1", "-TypeC1" },
    { "[TypeC1]", "[TypeC1]" },
    { "-TypeC2", "-TypeC2" },
    { "[TypeC2]", "[TypeC2]" },
    { " PD", " PD" },
    { " QC2.0", " QC2.0" },
    { " QC3.0", " QC3.0" },
    { " QC3+", " QC3+" },
    { " QC4+", " QC4+" },
    { " FCP", " FCP" },
    { " SCP", " SCP" },
    { " PE", " PE" },
    { " SFCP", " SFCP" },
    { " AFC", " AFC" },
    { " TFCP", " TFCP" },
#endif

/* Settings / sleep / display / system */
    { "-Display", "-显示" },
    { "[Display]", "[显示]" },
    { "-System", "-系统" },
    { "[System]", "[系统]" },
    { "-Sleep Options", "-休眠选项" },
    { "[Sleep]", "[休眠]" },
    { "[Home]", "[返回]" },
    { " No Auto Sleep", " 禁用自动休眠" },
    { " 15s", " 15秒" },
    { " 30s", " 30秒" },
    { " 1min", " 1分钟" },
    { " 2min", " 2分钟" },
    { " 5min", " 5分钟" },
    { " 10min", " 10分钟" },
    { " 15min", " 15分钟" },
    { " 30min", " 30分钟" },
    { " Keep On >5W", " >5W不休眠" },
    { " Contrast", " 对比度" },
    { "Contrast", "对比度" },
    { " Background", " 反色显示" },
    { "Background", "反色显示" },
    { " Screen Flip", " 屏幕翻转" },
    { "Screen Flip", "屏幕翻转" },
    { "-Burn-in Test", "-老化测试" },
    { " Fan +10'C", " 风扇 +10'C" },
    { "Fan +10C", "风扇 +10C" },
    { " Use Temp#1", " 使用温度#1" },
    { " Use Temp#2", " 使用温度#2" },
    { " Reset Now", " 系统重启" },
    { " Reboot to ISP", " 重启至ISP" },
    { " Language", " 中文界面" },

/* About / burn-in / game / widget */
    { "  Desktop PD Power\nSource: https://github.com/TKWTL/Desktop_PD_Power_Converter\n MiaoUI u8g2 coroOS\nMIT BSD-2 BSD-3 AGPL", "  桌面PD电源\n源码: https://github.com/TKWTL/Desktop_PD_Power_Converter\n MiaoUI u8g2 coroOS\nMIT BSD-2 BSD-3 AGPL" },
    { "Burn-in Test", "老化测试" },
    { "Checks the OLED for", "检查OLED" },
    { "decay after many", "长时间点亮后" },
    { "hours of light-up.", "是否衰减." },
    { "White screen - press", "白屏 - 按" },
    { "any key to leave.", "任意键退出." },
    { "Game Over!!", "游戏结束!!" },
    { "Dino Game V0.1", "恐龙游戏 V0.1" },
    { "from hjl240", "来自 hjl240" },
    { "Jump to Restart", "跳跃重开" },
    { "Null!!", "空!!" },

/* Statistics numeric item labels (not formatted values) */
    { " PDO (DC)", " 档位 (DC)" },
    { " PDO (NEG)", " 档位 (协商)" },
    { " PDO (SPR)", " 档位 (SPR)" },
    { " PDO (PPS)", " 档位 (PPS)" },
    { " PDO (EPR)", " 档位 (EPR)" },
    { " Req (V)", " 请求 (V)" },
    { " Cur (A)", " 电流 (A)" },
    { " VBUS (V)", " 母线 (V)" },
    { " Temp#1 (C)", " 温度#1 (C)" },
    { " Temp#2 (C)", " 温度#2 (C)" },

/* Remaining dynamic templates */
    { "HI %05d %05d", "最高 %05d %05d" },
    { "%s OK!", "%s 完成!" },
