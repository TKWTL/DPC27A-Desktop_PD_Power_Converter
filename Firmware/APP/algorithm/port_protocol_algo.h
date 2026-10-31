#ifndef PORT_PROTOCOL_ALGO_H_
#define PORT_PROTOCOL_ALGO_H_
#include <stdint.h>
/* Driver-free integer policy: no GPIO, I2C, UI or coroutine dependencies. */
#define PORT_PROTO_ALGO_PORTS 3u
#define PORT_PROTO_ALGO_DEBOUNCE_MS 800u
void PortProto_AlgoRequest(uint8_t port,uint16_t mask,uint32_t now);
uint8_t PortProto_AlgoObserve(uint8_t port,uint8_t online,uint16_t defaults,uint32_t now);
uint8_t PortProto_AlgoTakeDue(uint8_t port,uint8_t online,uint32_t now,uint16_t *mask);
uint8_t PortProto_AlgoTakePaths(uint8_t sw3538_online);
#endif
