#pragma once

/* power - putting the machine away (v0.60.183). */
void power_off(void);      /* ACPI S5 where there is one (QEMU's PIIX4 at 0x604, Bochs and old
                            * QEMU at 0xB004), else interrupts off and halt, as freeze is */
void power_reboot(void);   /* the keyboard controller's reset line, else a triple fault */
