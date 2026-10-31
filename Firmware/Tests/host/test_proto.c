#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "port_protocol_algo.h"
int main(void)
{
    uint16_t m=0u;
    PortProto_AlgoRequest(1u,0x123u,0u);
    assert(!PortProto_AlgoTakeDue(1u,1u,799u,&m));
    assert(!PortProto_AlgoTakeDue(1u,0u,800u,&m));
    assert(PortProto_AlgoTakeDue(1u,1u,800u,&m));assert(m==0x123u);
    PortProto_AlgoRequest(2u,0x201u,0xfffffffeu);
    assert(!PortProto_AlgoTakeDue(2u,1u,0x10u,&m));
    assert(PortProto_AlgoTakeDue(2u,1u,0x31eu,&m));assert(m==0x201u);
    assert(PortProto_AlgoObserve(0u,1u,0x7ffu,1000u));
    assert(!PortProto_AlgoObserve(0u,1u,0x7ffu,1001u));
    assert(PortProto_AlgoTakePaths(1u));assert(!PortProto_AlgoTakePaths(1u));
    assert(PortProto_AlgoTakeDue(0u,1u,1800u,&m));assert(m==0x7ffu);
    assert(!PortProto_AlgoObserve(0u,0u,0x7ffu,1900u));
    assert(PortProto_AlgoObserve(0u,1u,0x7ffu,2000u));
    assert(PortProto_AlgoTakePaths(1u));
    puts("Port protocol PASS");return 0;
}
