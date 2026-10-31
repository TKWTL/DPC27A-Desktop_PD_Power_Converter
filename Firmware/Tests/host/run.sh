#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
"$CC" -std=c99 -Wall -Wextra -Werror -I Tests/host/stubs -I APP/framework \
 APP/framework/pm_sleep_timer.c Tests/host/test_pm.c -o /tmp/dpc27a_pm
/tmp/dpc27a_pm
"$CC" -std=c99 -Wall -Wextra -Werror -I APP/algorithm \
 APP/algorithm/port_protocol_algo.c Tests/host/test_proto.c -o /tmp/dpc27a_protocol
/tmp/dpc27a_protocol
