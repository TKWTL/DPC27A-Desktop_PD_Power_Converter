# Third-party notices

Project-authored source code is licensed under **GNU AGPL-3.0-only**; see `LICENSE`.

The CH32X035 device headers, startup/system files and WCH Standard Peripheral
Library files under `Firmware/Project/` originate from Nanjing Qinheng
Microelectronics (WCH). Those files keep their original copyright and usage
notices. The repository-level AGPL declaration does not remove or replace those
upstream notices or grant rights that their original terms do not grant.

The USB-PD policy/port implementation is derived from WCH CH32X035 examples and
from the completed `TKWTL/CH32X035_DemoBoard` project. Keep the notices present
in individual source files when redistributing modified versions.

## MiaoUI

The MiaoUI core, widgets and display abstraction used by this firmware were
ported from the user-supplied `Firmware_0.zip`. MiaoUI source files retain their
MIT notices. The original separately linked ASCII font has been removed; its original MIT-licensed glyph bitmaps are now embedded inside the shared Unicode font.

The three 30x30 XBM menu image arrays retained from that package carry their
original GPL-3.0-or-later notice in `Firmware/APP/MiaoUI/images/image.c`. GPLv3
code is compatible with redistribution of this project under AGPL-3.0-only when
the corresponding source and notices are preserved.

## Chinese fonts (Fusion Pixel Font / Ark Pixel Font)

The firmware embeds one project-local U8g2 RLE Unicode subset,
`Firmware/APP/MiaoUI/fonts/font_fusion_pixel_p12_zhcn.c`. Its 95 fixed-width
ASCII glyph bitmaps are derived from the original MiaoUI h12w6 font
(Copyright (c) 2025 JFeng-Z; [MIT license](Firmware/APP/MiaoUI/LICENSE)).
Its 114 Han glyphs are derived from two font projects, both under the
**SIL Open Font License, Version 1.1 (OFL-1.1)**:

- [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font) — Copyright (c) 2022, TakWolf (https://takwolf.com). Selected 12px glyphs reused from the IH01A RLE and original LVGL subsets. [Full license](Firmware/APP/MiaoUI/fonts/LICENSE-OFL-FUSION).
- [Ark Pixel Font](https://github.com/TakWolf/ark-pixel-font) — Copyright (c) 2021, TakWolf (https://takwolf.com). Selected 12px common PNGs at revision `461d57118e1ccb1e987c1fa96313d4672e68e73a`, converted to U8g2 RLE. [Full license](Firmware/APP/MiaoUI/fonts/LICENSE-OFL-ARK).

The merged subset is not an unmodified upstream font. Its firmware symbol
is retained for compatibility, not to claim exclusive authorship or an
official upstream distribution. The original copyright/license notices
apply to their corresponding glyph data. The project-level AGPL does not
replace those licenses; the font data remain subject to the applicable MIT
and OFL redistribution conditions, including any applicable Reserved Font
Name restrictions. See [provenance and charset maintenance](Firmware/APP/I18N.md).

## u8g2

The trimmed u8g2 graphics core under `Firmware/ThirdParty/u8g2/` is derived from
U8g2 (Oliver Kraus) and is distributed under the two-clause BSD license. The
project keeps only the graphics functions needed by MiaoUI and a project-local
SH1107 TK078F288 80x128-native display backend. See
`Firmware/ThirdParty/u8g2/LICENSE`. No upstream u8g2 font collection is linked.

## Protothreads (coroOS scheduler)

The cooperative coroutine scheduler in `Firmware/Project/Scheduler/coroOS.[ch]`
is derived from the Protothreads library by Adam Dunkels (Swedish Institute of
Computer Science, 2004-2005). It keeps the Protothreads local-continuation model
(a `switch` over a stored line number) and extends it into a small scheduler
using project naming (`CORO_*` / `THRD_*`).

Protothreads are distributed under an open source BSD-style (3-clause BSD)
license: commercial and non-commercial use, with or without modification, is
permitted provided that credit is given - the copyright notice and conditions
must be retained in source redistributions and reproduced in the documentation
of binary distributions, and the Institute's name may not be used to endorse
derived products. The full license text is available at
https://dunkels.com/adam/pt/license.html
