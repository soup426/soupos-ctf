#pragma once
#include <stdint.h>

/* Realtek RTL8139 driver.
 *
 * Chosen because QEMU emulates it and it is programmed entirely through I/O
 * ports plus two DMA areas: no MMIO window to map, no descriptor rings, no
 * firmware upload. The buffers are static arrays, which works because the
 * kernel is identity-mapped, so their virtual addresses are the physical ones
 * the card's registers want. */

#define ETH_ALEN      6
#define ETH_FRAME_MAX 1514      /* 14-byte header + 1500 payload, no FCS */

/* Probe PCI, reset the card and start it. Returns 0 on success, -1 when no
 * RTL8139 is present (the usual case when QEMU is started without a NIC). */
int  rtl8139_init(void);

/* Non-zero once init has succeeded. */
int  rtl8139_present(void);

/* Our MAC address, read out of the card's EEPROM-backed registers. */
const uint8_t *rtl8139_mac(void);

/* Queue one complete Ethernet frame (header included, no FCS: the card adds
 * it). Returns 0, or -1 if every transmit descriptor is still busy. */
int  rtl8139_send(const void *frame, uint16_t len);

/* Counters, for `netinfo`. */
void rtl8139_stats(uint32_t *tx, uint32_t *rx, uint32_t *dropped);

/* Called by the driver for each frame received, set by the net layer. */
typedef void (*rtl8139_rx_fn)(const uint8_t *frame, uint16_t len);
void rtl8139_set_rx_handler(rtl8139_rx_fn fn);
