#pragma once

/* fwcfg - QEMU's firmware configuration device (v0.60.179), for switches
 * the host passes at boot (`-fw_cfg name=opt/soupos/desktop,string=1`).
 * 1 when a file of that name is there, 0 when it is not or there is no
 * fw_cfg at all (real hardware: the signature reads back as 0xFF). */
int fwcfg_has(const char *name);
