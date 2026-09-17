/* Per-port fast-charge protocol pages for the Desktop PD Power Converter.
 * Lives outside APP/MiaoUI on purpose: ui_conf.c only owns the menu tree. */
#ifndef APP_PAGES_PORT_PAGES_H_
#define APP_PAGES_PORT_PAGES_H_

#include "ui_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Icon-page entries (-TypeA+C / -TypeC1 / -TypeC2) and the three TEXT pages
 * behind them, each holding the fast-charge protocol-family switches of one
 * port (PD first, right after the page's [Menu] back item): SW3538 for the
 * Type-A + Type-C pair, SW3526 #1 for TypeC1 and SW3526 #2 for TypeC2.
 * Requests are forwarded to app_tasks.c, which debounces and writes them. */
void Add_Port_Items(ui_page_t *parent_page);

#ifdef __cplusplus
}
#endif

#endif /* APP_PAGES_PORT_PAGES_H_ */
