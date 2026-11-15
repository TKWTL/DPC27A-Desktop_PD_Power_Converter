/* Service items/pages for the Desktop PD Power Converter: fan trigger delay,
 * the reset/ISP actions and the OLED burn-in test.
 * Lives outside APP/MiaoUI on purpose: ui_conf.c only owns the menu tree. */
#ifndef APP_PAGES_SERVICE_PAGES_H_
#define APP_PAGES_SERVICE_PAGES_H_

#include "ui_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* System-page entries: fan offset, two temperature-source switches, resets. */
void Add_Service_Items(ui_page_t *parent_page);

/* Full-screen OLED burn-in test: short explanation, then a white screen; any
 * key during the white phase returns to the menu. */
void Burnin_Page(ui_t *ui);

/* Reset actions used as function items on the System page.  Neither ever
 * returns: Reset Now restarts the application, Reboot to ISP hands over to
 * the CH32 factory bootloader on the next reset. */
void SoftReset_Action(ui_t *ui);
void IspReboot_Action(ui_t *ui);

#ifdef __cplusplus
}
#endif

#endif /* APP_PAGES_SERVICE_PAGES_H_ */
