/* rtl8139.c - Realtek RTL8139 Ethernet driver. See rtl8139.h for why this card.
 *
 * Receive is a single circular buffer the card DMAs into, with a 16-byte
 * header before each frame (status word + length) and a hardware read pointer
 * we advance as we consume. Transmit is four fixed buffers with four status
 * registers, used round-robin.
 *
 * Both DMA areas are static arrays. The kernel identity-maps all of RAM, so
 * their addresses are already physical, which is what the card wants and what
 * makes this driver short.
 */
#include "rtl8139.h"
#include "pci.h"
#include "isr.h"
#include "klog.h"
#include "str.h"

/* ── Register offsets from the I/O base ─────────────────────────────────── */
#define REG_MAC        0x00     /* 6 bytes                                  */
#define REG_TSD0       0x10     /* transmit status, 4 of them, 4 bytes apart */
#define REG_TSAD0      0x20     /* transmit start address, likewise          */
#define REG_RBSTART    0x30     /* receive buffer physical address           */
#define REG_CMD        0x37     /* command                                   */
#define REG_CAPR       0x38     /* current address of packet read            */
#define REG_IMR        0x3C     /* interrupt mask                            */
#define REG_ISR        0x3E     /* interrupt status                          */
#define REG_TCR        0x40     /* transmit configuration                    */
#define REG_RCR        0x44     /* receive configuration                     */
#define REG_CONFIG1    0x52

#define CMD_RESET      0x10
#define CMD_RX_ENABLE  0x08
#define CMD_TX_ENABLE  0x04
#define CMD_RX_EMPTY   0x01

#define ISR_ROK        0x0001   /* receive ok      */
#define ISR_RER        0x0002
#define ISR_TOK        0x0004   /* transmit ok     */
#define ISR_TER        0x0008

/* Transmit status bits. Completion is tested with these rather than with the
 * OWN bit, whose polarity is documented inconsistently and is easy to invert:
 * a descriptor we never used is free, and one we did use is free again once
 * the card reports it finished, successfully or not. */
#define TSD_TOK        0x00008000   /* transmit ok      */
#define TSD_TUN        0x00004000   /* FIFO underrun    */
#define TSD_TABT       0x40000000   /* aborted          */
#define TSD_DONE       (TSD_TOK | TSD_TUN | TSD_TABT)

/* RX config: accept broadcast + multicast + physical match, plus "wrap", which
 * lets the card run past the end of the ring instead of splitting a frame.
 * That is why the buffer is oversized by 1500 bytes below. */
#define RCR_FLAGS      0x0000000F
#define RCR_WRAP       0x00000080
#define RCR_MXDMA_1024 0x00000600
#define RCR_RXFTH_NONE 0x0000E000

#define RX_BUF_LEN     8192
#define RX_BUF_PAD     16
#define RX_BUF_WRAP    1500
#define TX_BUFS        4
#define TX_BUF_LEN     1792

static inline void outb_(uint16_t p, uint8_t v)  { __asm__ volatile ("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outw_(uint16_t p, uint16_t v) { __asm__ volatile ("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outl_(uint16_t p, uint32_t v) { __asm__ volatile ("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t  inb_(uint16_t p) { uint8_t  r; __asm__ volatile ("inb %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static inline uint16_t inw_(uint16_t p) { uint16_t r; __asm__ volatile ("inw %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static inline uint32_t inl_(uint16_t p) { uint32_t r; __asm__ volatile ("inl %1,%0" : "=a"(r) : "Nd"(p)); return r; }

/* DMA buffers. Aligned because the card wants the RX ring 4-byte aligned and
 * it costs nothing to be generous. */
static uint8_t  rx_buf[RX_BUF_LEN + RX_BUF_PAD + RX_BUF_WRAP] __attribute__((aligned(16)));
static uint8_t  tx_buf[TX_BUFS][TX_BUF_LEN] __attribute__((aligned(16)));

static uint16_t io_base;
static uint8_t  mac[ETH_ALEN];
static uint16_t rx_pos;              /* our read offset into the ring   */
static int      tx_next;             /* next descriptor to use          */
static int      tx_used[TX_BUFS];    /* has this descriptor been used?  */
static int      present;
static uint32_t stat_tx, stat_rx, stat_drop;
static rtl8139_rx_fn rx_handler;

int rtl8139_present(void) { return present; }
const uint8_t *rtl8139_mac(void) { return mac; }
void rtl8139_set_rx_handler(rtl8139_rx_fn fn) { rx_handler = fn; }

void rtl8139_stats(uint32_t *tx, uint32_t *rx, uint32_t *dropped) {
    if (tx)      *tx      = stat_tx;
    if (rx)      *rx      = stat_rx;
    if (dropped) *dropped = stat_drop;
}

/* ── Receive ───────────────────────────────────────────────────────────────
 * Each frame in the ring is preceded by a 4-byte header: a status word and a
 * length (which includes the 4-byte CRC the card keeps). Frames are padded to
 * a 4-byte boundary. CAPR, confusingly, is the read pointer minus 16. */
static void rx_drain(void) {
    while (!(inb_(io_base + REG_CMD) & CMD_RX_EMPTY)) {
        uint8_t *p      = rx_buf + rx_pos;
        uint16_t status = (uint16_t)(p[0] | (p[1] << 8));
        uint16_t len    = (uint16_t)(p[2] | (p[3] << 8));

        if (len < 4 || len > ETH_FRAME_MAX + 4 || !(status & 0x0001)) {
            /* A bad frame means the ring position is no longer trustworthy;
             * resetting the receiver is the documented way out. */
            stat_drop++;
            outb_(io_base + REG_CMD, CMD_TX_ENABLE);
            outb_(io_base + REG_CMD, CMD_TX_ENABLE | CMD_RX_ENABLE);
            rx_pos = 0;
            outw_(io_base + REG_CAPR, (uint16_t)(0 - 16));
            return;
        }

        uint16_t frame_len = (uint16_t)(len - 4);     /* drop the CRC */
        if (rx_handler) rx_handler(p + 4, frame_len);
        stat_rx++;

        rx_pos = (uint16_t)((rx_pos + len + 4 + 3) & ~3u);
        if (rx_pos >= RX_BUF_LEN) rx_pos %= RX_BUF_LEN;
        outw_(io_base + REG_CAPR, (uint16_t)(rx_pos - 16));
    }
}

static void rtl8139_irq(registers_t *regs) {
    (void)regs;
    uint16_t isr = inw_(io_base + REG_ISR);
    if (!isr) return;
    outw_(io_base + REG_ISR, isr);        /* ack first: the card re-raises otherwise */

    if (isr & ISR_ROK) rx_drain();
    if (isr & (ISR_RER | ISR_TER)) stat_drop++;
}

/* ── Transmit ───────────────────────────────────────────────────────────── */
int rtl8139_send(const void *frame, uint16_t len) {
    if (!present || !frame || len == 0) return -1;
    if (len > TX_BUF_LEN) return -1;

    /* Four descriptors, round-robin. One we have never used is free; one we
     * have is free again once the card reports the transfer finished. */
    for (int tries = 0; tries < TX_BUFS; tries++) {
        int d = tx_next;

        if (tx_used[d]) {
            uint32_t tsd = inl_(io_base + REG_TSD0 + 4 * d);
            if (!(tsd & TSD_DONE)) {        /* still in flight, try the next */
                tx_next = (tx_next + 1) % TX_BUFS;
                continue;
            }
        }

        memcpy(tx_buf[d], frame, len);
        /* Ethernet's 60-byte minimum before FCS; the card does not pad. */
        uint16_t n = len;
        if (n < 60) { memset(tx_buf[d] + n, 0, (uint32_t)(60 - n)); n = 60; }

        outl_(io_base + REG_TSAD0 + 4 * d, (uint32_t)tx_buf[d]);
        outl_(io_base + REG_TSD0  + 4 * d, n);   /* writing the size starts it */
        tx_used[d] = 1;
        tx_next = (tx_next + 1) % TX_BUFS;
        stat_tx++;
        return 0;
    }
    stat_drop++;
    return -1;
}

/* ── Bring-up ──────────────────────────────────────────────────────────── */
int rtl8139_init(void) {
    pci_dev_t d;
    if (pci_find(0x10EC, 0x8139, &d) < 0) return -1;

    pci_enable_bus_master(&d);
    io_base = (uint16_t)pci_bar(&d, 0);
    if (!io_base) return -1;

    /* Power on, then reset and wait for the card to clear the bit. */
    outb_(io_base + REG_CONFIG1, 0x00);
    outb_(io_base + REG_CMD, CMD_RESET);
    for (int i = 0; i < 100000 && (inb_(io_base + REG_CMD) & CMD_RESET); i++) { }
    if (inb_(io_base + REG_CMD) & CMD_RESET) {
        klog("[net] rtl8139 reset timed out\n");
        return -1;
    }

    for (int i = 0; i < ETH_ALEN; i++) mac[i] = inb_(io_base + REG_MAC + i);

    rx_pos  = 0;
    tx_next = 0;
    outl_(io_base + REG_RBSTART, (uint32_t)rx_buf);
    outw_(io_base + REG_CAPR, (uint16_t)(0 - 16));
    outl_(io_base + REG_RCR, RCR_FLAGS | RCR_WRAP | RCR_MXDMA_1024 | RCR_RXFTH_NONE);
    outl_(io_base + REG_TCR, 0x03000700);      /* default IFG, 1024-byte DMA burst */
    outb_(io_base + REG_CMD, CMD_RX_ENABLE | CMD_TX_ENABLE);
    outw_(io_base + REG_IMR, ISR_ROK | ISR_RER | ISR_TOK | ISR_TER);
    outw_(io_base + REG_ISR, 0xFFFF);          /* clear anything pending */

    uint8_t irq = pci_irq_line(&d);
    irq_register(irq, rtl8139_irq);
    irq_unmask(irq);

    present = 1;
    klog("[net] rtl8139 at io 0x%x irq %u mac %x:%x:%x:%x:%x:%x\n",
         io_base, irq, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}
