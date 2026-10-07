# Networking (M5)

Written 2026-10-06. First network stack for soupOS.

## Target

`ping 10.0.2.2` replies, from a kernel that currently has no concept of a
network at all. That is the whole first milestone: a real NIC driver, enough
Ethernet/ARP/IPv4/ICMP to be pinged and to ping, and the shell commands to
drive it.

## The card: RTL8139

Chosen because QEMU emulates it, it is the classic hobby-OS NIC, and it is
programmed entirely through I/O ports plus two DMA buffers. No MMIO mapping,
no descriptor rings to speak of, no firmware.

`pci.c` can enumerate devices but cannot read a BAR, read the interrupt line,
or write config space, so it gains:

```c
int      pci_find(uint16_t vendor, uint16_t device, pci_dev_t *out);
uint32_t pci_cfg_read32 (const pci_dev_t *d, uint8_t off);
void     pci_cfg_write32(const pci_dev_t *d, uint8_t off, uint32_t val);
uint32_t pci_bar(const pci_dev_t *d, int n);
uint8_t  pci_irq_line(const pci_dev_t *d);
void     pci_enable_bus_master(const pci_dev_t *d);
```

Bus mastering matters: the card DMAs into our buffers, and QEMU will silently
drop transfers if the bus-master bit is clear.

**DMA buffers are static arrays in `.bss`.** The kernel is identity-mapped, so
a static buffer's virtual address is its physical address, which is exactly
what the card's registers want. The RX ring is 8 KB + 16 + 1500 (the wrap
allowance the datasheet requires), and there are four 1792-byte TX buffers,
used round-robin because the card has four descriptors.

## The stack, bottom up

- **Ethernet**: 14-byte header, three types we care about (ARP 0x0806, IPv4
  0x0800). Anything else is counted and dropped.
- **ARP**: a 8-entry cache, replies to requests for our address, and resolves
  on demand before an IPv4 send. A send that has to resolve is queued only in
  the sense that `ping` retries: there is no packet queue, which keeps the
  whole thing synchronous and small.
- **IPv4**: no fragmentation, no options, header checksum computed and
  verified. Anything not addressed to us is dropped.
- **ICMP**: echo request and echo reply. Replying makes us pingable; sending
  makes `ping` work.

Not in this milestone: DHCP, UDP, TCP, DNS, routing beyond a single gateway.
The addresses are static and match QEMU's user-mode network, which is what the
run targets use.

## Addressing

QEMU's SLIRP network is fixed and well known, so configuration is a compiled-in
default rather than DHCP:

| what     | address     |
|----------|-------------|
| soupOS   | 10.0.2.15   |
| gateway  | 10.0.2.2    |
| netmask  | 255.255.255.0 |

`ifconfig` (shell command) prints them and lets the IP be changed at runtime.

## Interrupts vs polling

The card raises an IRQ for RX and TX completion, and the handler must
acknowledge the ISR register or the line stays asserted. The RX path runs in
the IRQ handler: it copies each frame out of the ring into a static scratch
buffer and processes it there. That is not what a serious kernel does (a real
one defers to a task), but soupOS's IRQ handlers already do real work, the
protocol handling is tens of microseconds, and introducing a softirq mechanism
for one driver is the wrong order.

## Shell surface

- `netinfo` — card, MAC, link state, counters.
- `ifconfig [ip]` — show or set addressing.
- `arp` — the cache.
- `ping <ip> [count]` — the point of the exercise.

## How this gets verified

QEMU can write every frame the guest sends or receives to a pcap:

```
-object filter-dump,id=d0,netdev=n0,file=net.pcap
```

That is ground truth independent of anything soupOS prints, and it is how each
layer gets checked as it lands: the first TX appears as a real Ethernet frame,
the ARP exchange appears as request and reply, and the ping appears as echo
request and echo reply.

For the gate, `ping 10.0.2.2 3` must report replies in the serial log. The
smoke test's QEMU invocation gains the netdev and the card.
