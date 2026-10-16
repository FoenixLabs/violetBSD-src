/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _A2560ME_BOARD_H_
#define _A2560ME_BOARD_H_
#define A2560ME_SRAM_SIZE 0x00800000
/* Build 23 CPU-visible DDR; the first 8 MiB are hidden behind SRAM. */
#define A2560ME_DDR_BASE 0x00800000
#if defined(A2560ME_DDR_FULL) && defined(A2560ME_DDR64)
#error Select either full DDR or the 64 MiB comparison region
#endif
#ifdef A2560ME_DDR_FULL
#define A2560ME_DDR_SIZE 0x3f800000
#else
#define A2560ME_DDR_SIZE 0x04000000
#endif
/* Dedicated DDR scanout, excluded from UVM so all mappings are uncached.
 * 640x480x8 uses 307200 bytes; reserve 320 KiB, aligned for 4/8 KiB pages. */
#define A2560ME_GUI_FB_BASE A2560ME_DDR_BASE
#define A2560ME_GUI_FB_SIZE (640 * 480)
#define A2560ME_GUI_FB_RESERVE 0x00050000
#define A2560ME_FLASH_BASE 0xffc00000
#define A2560ME_FLASH_SIZE 0x00400000
#define A2560ME_RTC_BASE 0xfec00080
#define A2560ME_SYSTEM_CTRL 0xfec00000
/* GAVIN keyed whole-board cold reset, reference rev1.6 pp15,20-21. */
#define A2560ME_SOFT_RESET 0xdead8000U
#define A2560ME_CLOCK_HZ 50000000
#define A2560ME_UART1 0xfec023f8
#define A2560ME_UART2 0xfec022f8
/* Super I/O PME runtime registers, matching A2560Me MCP superio.h. */
#define A2560ME_GP33 0xfec02136
#define A2560ME_FAN1 0xfec02156
#define A2560ME_FAN_CTRL 0xfec02158
/* Verified USB console: FPGA UART4 -> host ttyUSB3. */
#define A2560ME_USB_CONSOLE_UNIT 1
#define A2560ME_USB_CONSOLE_BASE (0xfec00b00 + A2560ME_USB_CONSOLE_UNIT * 0x100)
#define A2560ME_LAN 0xfec00600
/* Board IRQ source supplied by the user; not a CPU vector number. */
#define A2560ME_LAN_IRQ 0x20
#ifndef _LOCORE
#include <sys/types.h>
#define REG8(a) (*(volatile uint8_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))
extern unsigned a2560me_clock_hz;
extern int a2560me_boot_growroot;
extern unsigned a2560me_boot_option;
/* Reserved reset-copy page; mapped uncached once the MMU is enabled. */
int a2560me_intr_establish(unsigned, int (*)(void *), void *);
void a2560me_led_init(void);
void a2560me_led_disk(unsigned, bool);
void a2560me_led_network(void);
void a2560me_cninit(void);
int a2560me_console_mapchar(int, unsigned *);
void a2560me_boot_puts(const char *);
void a2560me_clock_early(void);
void a2560me_clock_intr(struct clockframe *);
void a2560me_usb_init(void);
void a2560me_usb_putc(int);
void a2560me_usb_mirror(int);
int a2560me_usb_getc(void);
void a2560me_usb_pollc(int);

#endif
#endif
