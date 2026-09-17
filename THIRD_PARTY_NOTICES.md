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

The MiaoUI core, widgets, display abstraction and bundled `font_menu_main_h12w6`
used by this firmware were ported from the user-supplied `Firmware_0.zip`. MiaoUI
source files retain their MIT notices. Only the one menu font required by the
initial product UI is linked.

The three 30x30 XBM menu image arrays retained from that package carry their
original GPL-3.0-or-later notice in `Firmware/APP/MiaoUI/images/image.c`. GPLv3
code is compatible with redistribution of this project under AGPL-3.0-only when
the corresponding source and notices are preserved.

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
