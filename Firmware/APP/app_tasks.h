#ifndef APP_TASKS_H_
#define APP_TASKS_H_

#include "sw3526.h"
#include "sw3538.h"
#include "GX21M15/gx21m15.h"

void APP_Tasks_Init(void);
void APP_Tasks_RunOnce(void);
void APP_Tasks_Idle(void);

SW3538_Handle *APP_GetSW3538(void);
SW3526_Handle *APP_GetSW3526_1(void);
SW3526_Handle *APP_GetSW3526_2(void);
GX21M15_Handle *APP_GetGX21M15(void);

/* Port fast-charge protocol mask requests (the three MiaoUI protocol pages). */
#define APP_PORT_PROTO_SW3538    0u   /* SW3538: Type-A + Type-C pair */
#define APP_PORT_PROTO_SW3526_1  1u   /* SW3526 #1: TypeC1 */
#define APP_PORT_PROTO_SW3526_2  2u   /* SW3526 #2: TypeC2 */

/* Forward a port protocol-family mask from the UI.  thread_power_mirror
 * debounces the request and performs the write (a mask write makes an
 * attached sink renegotiate, so bursts of switch flips collapse into one
 * transfer).  The mask is stored as-is; a request made while the chip is
 * offline stays pending until it is back. */
void APP_PortProtocol_Touch(uint8_t port, uint16_t mask);

#endif /* APP_TASKS_H_ */
