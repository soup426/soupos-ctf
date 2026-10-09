#!/usr/bin/env bash
# The whole network path for real: PCI probe, DMA ring, IRQ 11 through the
# slave PIC, DHCP from SLIRP, ARP, ICMP, a DNS query on the wire, and TCP
# against listeners on the host.
source "$(dirname "$0")/lib.sh"
gate_init net net
gate_boot
gate_drive \
    "sip 10.0.2.2 3" "WAIT:8" \
    "sniff example.com" "WAIT:6" \
    "reserve 10.0.2.2 19000" "WAIT:6" \
    "holler 10.0.2.2 19001 souptcp" "WAIT:8" \
    "takeout 10.0.2.2:19002 /soup" "WAIT:9"
check "the NIC was found and configured" '\[net\] rtl8139 at io'
check "an address was taken by DHCP" '\[dhcp\] lease 10\.0\.2\.15 mask 255\.255\.255\.0'
check "ARP resolved and all pings replied" '\[net\] ping done 3/3'
# The ANSWER is deliberately not asserted: it needs the host to have working
# DNS, and a gate that fails when you are offline is a bad gate.
check "a DNS query was built and sent" '\[net\] dns query id='
check "a TCP connection was established" '\[tcp\] established with 10\.0\.2\.2:19000'
check "TCP carried data and got a reply" '\[tcp\] sent 7 bytes'
check "TCP received the echo"            '\[tcp\] rx 7 bytes'
check "an HTTP page was fetched over TCP" '\[tcp\] takeout 3[0-9] bytes'
check "the connection closed gracefully"  '\[tcp\] closed cleanly'
# Bytes carried each way, counted per connection: holler sends "souptcp" and
# the listener echoes it upper-cased, so exactly 7 each way.
check "a connection counts the bytes it carried" 'closed cleanly \(sent 7, received 7\)'
gate_finish
