#include "rtc.h"

/* CMOS/RTC is accessed via two I/O ports:
   0x70 - index register (bit 7 = NMI disable while reading)
   0x71 - data register */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd2bin(uint8_t bcd) {
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

void rtc_read(rtc_time_t *t) {
    /* Spin until the Update-In-Progress bit (bit 7 of register 0x0A) clears.
       An update takes ~2ms; polling is fine during a shell command. */
    while (cmos_read(0x0A) & 0x80) {}

    /* Register B flag bits (MC146818):
     *   bit 1 (0x02) = 24/12 : 1 = 24-hour, 0 = 12-hour
     *   bit 2 (0x04) = DM    : 1 = binary,  0 = BCD
     * These two were swapped here, which is why every field came back as its
     * raw BCD byte read as decimal: QEMU sets 24-hour (0x02), the code took
     * that to mean "binary" and skipped the conversion, so 0x25 printed as
     * 37 and the clock showed 2038-08-37 33:39:55. */
    uint8_t sb       = cmos_read(0x0B);
    int     binary   = sb & 0x04;   /* bit 2: 1=binary, 0=BCD   */
    int     h24      = sb & 0x02;   /* bit 1: 1=24-hour, 0=12hr */

    uint8_t sec      = cmos_read(0x00);
    uint8_t min      = cmos_read(0x02);
    uint8_t raw_hour = cmos_read(0x04);   /* bit 7 = PM flag in 12-hr mode */
    uint8_t day      = cmos_read(0x07);
    uint8_t month    = cmos_read(0x08);
    uint8_t yr       = cmos_read(0x09);

    if (!binary) {
        sec   = bcd2bin(sec);
        min   = bcd2bin(min);
        day   = bcd2bin(day);
        month = bcd2bin(month);
        yr    = bcd2bin(yr);
        /* Preserve PM bit across BCD conversion */
        raw_hour = (uint8_t)(bcd2bin(raw_hour & 0x7F) | (raw_hour & 0x80));
    }

    uint8_t hour = raw_hour & 0x7F;
    if (!h24 && (raw_hour & 0x80))   /* 12-hour PM: add 12, handle 12pm edge */
        hour = (uint8_t)((hour % 12) + 12);

    t->second = sec;
    t->minute = min;
    t->hour   = hour;
    t->day    = day;
    t->month  = month;
    t->year   = (yr < 70) ? (uint16_t)(2000 + yr) : (uint16_t)(1900 + yr);
}
