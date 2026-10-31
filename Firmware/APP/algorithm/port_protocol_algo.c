/* Protocol-mask debounce, offline requests and controller reconnect policy.
 * SW35xx driver transactions remain exclusively in thread_power_mirror. */
#include "port_protocol_algo.h"
static uint32_t s_request_ms[PORT_PROTO_ALGO_PORTS];
static uint16_t s_mask[PORT_PROTO_ALGO_PORTS];
static uint8_t s_pending[PORT_PROTO_ALGO_PORTS];
static uint8_t s_was_online[PORT_PROTO_ALGO_PORTS];
static uint8_t s_paths_pending=1u;

void PortProto_AlgoRequest(uint8_t port,uint16_t mask,uint32_t now)
{
    if(port>=PORT_PROTO_ALGO_PORTS) return;
    s_mask[port]=mask;s_request_ms[port]=now;s_pending[port]=1u;
}

uint8_t PortProto_AlgoObserve(uint8_t port,uint8_t online,uint16_t defaults,uint32_t now)
{
    uint8_t edge;
    if(port>=PORT_PROTO_ALGO_PORTS) return 0u;
    online=online?1u:0u;
    edge=(online&&!s_was_online[port])?1u:0u;
    s_was_online[port]=online;
    if(edge) {
        PortProto_AlgoRequest(port,defaults,now);
        if(port==0u) s_paths_pending=1u;
    }
    return edge;
}

uint8_t PortProto_AlgoTakeDue(uint8_t port,uint8_t online,uint32_t now,uint16_t *mask)
{
    if(port>=PORT_PROTO_ALGO_PORTS || mask==0 || !online || !s_pending[port] ||
       (uint32_t)(now-s_request_ms[port])<PORT_PROTO_ALGO_DEBOUNCE_MS)
        return 0u;
    *mask=s_mask[port];s_pending[port]=0u;return 1u;
}

uint8_t PortProto_AlgoTakePaths(uint8_t online)
{
    if(!online || !s_paths_pending) return 0u;
    s_paths_pending=0u;return 1u;
}
