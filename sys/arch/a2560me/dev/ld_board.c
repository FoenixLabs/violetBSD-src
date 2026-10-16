/* SPDX-License-Identifier: BSD-2-Clause */
/* PIO-only board disks: independent SPI SD ports and the ATA master. */
#include "opt_a2560me.h"
#if defined(A2560ME_ATA_SLOW_READ) && !defined(A2560ME_ATA_READCHECK)
#error A2560ME_ATA_SLOW_READ requires the read-only diagnostic
#endif
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/bufq.h>
#include <sys/disk.h>
#ifdef A2560ME_ATA_READCHECK
#include <sys/disklabel.h>
#include <lib/libkern/libkern.h>
#endif
#ifdef A2560ME_ATA_WRITECHECK
#include <sys/kmem.h>
#include <uvm/uvm_extern.h>
#endif
#include <sys/workqueue.h>
#include <sys/kernel.h>
#include <dev/ldvar.h>
#include <machine/board.h>
#include "sd_crc.h"

int a2560me_fdc_init(void);
int a2560me_fdc_sector(uint32_t, uint8_t *, bool);
struct boarddisk {
	struct ld_softc ld;
	struct workqueue *wq;
	struct work work;
	struct buf *bp;
	uintptr_t base;
	bool ata, floppy, blockaddr;
#ifdef A2560ME_ATA_WRITECHECK
	bool scratch_identity;
#endif
	int error;
};
/* Use register-source byte cycles for the A2560Me peripheral bridge. */
static void sd_write8(uintptr_t address, unsigned value)
{
	__asm volatile("moveb %0,%1@" :: "d"(value), "a"(address) : "memory");
}
static int spi(struct boarddisk *sc, int v)
{
	if (sc->error)
		return 0xff;
	sd_write8(sc->base + 1, v);
	/* Match the working standalone SD2 path at66.67MHz. */
	delay(2);
	for (unsigned n = 0; n < 100000; n++) {
		if (!((REG8(sc->base) | REG8(sc->base) |
		    REG8(sc->base)) & 0x80)) {
			/* DATA is latched until the next write; reading does not pop. */
			uint8_t a = REG8(sc->base + 1);
			uint8_t b = REG8(sc->base + 1);
			uint8_t c = REG8(sc->base + 1);
			return (a & b) | (a & c) | (b & c);
		}
	}
	sc->error = ETIMEDOUT;
	return 0xff;
}
static void deselect(struct boarddisk *sc)
{
	sd_write8(sc->base, REG8(sc->base) & ~1U);
	(void)spi(sc, 0xff);
}
static int ready(struct boarddisk *sc)
{
	for (unsigned n = 0; n < 50000 && !sc->error; n++) {
		if (spi(sc, 0xff) == 0xff)
			return 0;
		delay(10);
	}
	return sc->error ? sc->error : ETIMEDOUT;
}
static int command(struct boarddisk *sc, unsigned cmd, uint32_t arg)
{
	deselect(sc);
	sd_write8(sc->base, REG8(sc->base) | 1);
	(void)spi(sc, 0xff);
	if (ready(sc))
		return 0xff;
	spi(sc, 0x40 | cmd);
	spi(sc, arg >> 24);
	spi(sc, arg >> 16);
	spi(sc, arg >> 8);
	spi(sc, arg);
	spi(sc, cmd == 0 ? 0x95 : cmd == 8 ? 0x87 : 1);
	for (unsigned i = 0; i < 10; i++) {
		int r = spi(sc, 0xff);
		if (!(r & 0x80))
			return r;
	}
	return 0xff;
}
static int receive(struct boarddisk *sc, uint8_t *p, size_t len)
{
	int token = 0xff;
	for (unsigned i = 0; i < 10000 && !sc->error; i++) {
		token = spi(sc, 0xff);
		if (token != 0xff)
			break;
		delay(10);
	}
	if (token != 0xfe)
		return sc->error ? sc->error : EIO;
	for (size_t i = 0; i < len; i++)
		p[i] = spi(sc, 0xff);
	uint16_t wire = (uint16_t)spi(sc, 0xff) << 8;
	wire |= spi(sc, 0xff);
	if (sc->error)
		return sc->error;
	return sd_crc16(p, len) == wire ? 0 : EIO;
}
static int sd_init(struct boarddisk *sc)
{
	uint8_t csd[16], r7[4];
	sd_write8(sc->base + 2, sd_slow_divider(a2560me_clock_hz));
	sd_write8(sc->base, 2);
	for (unsigned i = 0; i < 10; i++)
		spi(sc, 0xff);
	if (command(sc, 0, 0) != 1)
		return ENXIO;
	bool v2 = command(sc, 8, 0x1aa) == 1;
	if (v2) {
		for (unsigned i = 0; i < 4; i++)
			r7[i] = spi(sc, 0xff);
		if (r7[2] != 1 || r7[3] != 0xaa)
			return EIO;
	}
	int r = 1;
	for (unsigned i = 0; i < 1000 && !sc->error; i++) {
		if (command(sc, 55, 0) > 1)
			return ENOTSUP;
		r = command(sc, 41, v2 ? 0x40000000 : 0);
		if (r == 0)
			break;
		delay(1000);
	}
	if (r != 0)
		return ETIMEDOUT;
	if (command(sc, 58, 0) != 0)
		return EIO;
	for (unsigned i = 0; i < 4; i++)
		r7[i] = spi(sc, 0xff);
	sc->blockaddr = v2 && (r7[0] & 0x40);
	if (!sc->blockaddr && command(sc, 16, 512) != 0)
		return EIO;
	int csd_error = EIO;
	for (unsigned attempt = 0; attempt < 8; attempt++) {
		sc->error = 0;
		csd_error = command(sc, 9, 0) == 0 ? receive(sc, csd, 16) : EIO;
		deselect(sc);
		if (!csd_error && !sc->error)
			break;
		csd_error = sc->error ? sc->error : csd_error;
	}
	if (csd_error)
		return csd_error;
	if ((csd[0] >> 6) == 1) {
		uint32_t size = ((csd[7] & 63) << 16) | (csd[8] << 8) | csd[9];
		sc->ld.sc_secperunit = ((uint64_t)size + 1) * 1024;
	} else if ((csd[0] >> 6) == 0) {
		unsigned size =
		    ((csd[6] & 3) << 10) | (csd[7] << 2) | (csd[8] >> 6);
		unsigned mult = ((csd[9] & 3) << 1) | (csd[10] >> 7);
		unsigned shift = (csd[5] & 15) + mult + 2;
		sc->ld.sc_secperunit =
		    ((uint64_t)size + 1) * (1ULL << shift) / 512;
	} else
		return ENOTSUP;
	deselect(sc);
	sd_write8(sc->base, 0);
	return sc->error;
}
/* Match MCP sdc0_protected(): set means protected. Its header comment
 * describes the opposite polarity; retain the check, not a write bypass.
 */
static bool sd_write_protected(uint32_t status)
{
	return (status & 0x02000000) != 0;
}
static int sd_sector(struct boarddisk *sc, uint32_t lba, uint8_t *data, bool wr)
{
	int e = 0;
	if (!sc->blockaddr && lba > UINT32_MAX / 512)
		return EFBIG;
	if (wr && sc->base == 0xfec00300 && sd_write_protected(REG32(0xfec00518)))
		return EROFS;
	/* Reads may retry after reselecting the same sector. Never retry writes:
	 * an error after data acceptance can be an ambiguous completed write. */
	unsigned attempts = wr ? 1 : 8;
	do {
		if (!wr)
			sc->error = 0;
		e = 0;
		if (command(sc, wr ? 24 : 17, sc->blockaddr ? lba : lba * 512) != 0)
			e = EIO;
		else if (!wr)
			e = receive(sc, data, 512);
		else {
			if ((e = ready(sc)) == 0) {
				spi(sc, 0xfe);
				for (unsigned i = 0; i < 512; i++)
					spi(sc, data[i]);
				spi(sc, 0xff);
				spi(sc, 0xff);
				if ((spi(sc, 0xff) & 31) != 5)
					e = EIO;
				else
					e = ready(sc);
			}
		}
		deselect(sc);
		e = sc->error ? sc->error : e;
		if (!e || wr)
			return e;
	} while (--attempts);
	return e;
}
#ifdef A2560ME_ATA_WRITECHECK
#define ATA_SCRATCH_LBA 33558529U
#define ATA_SCRATCH_COUNT 63U
static bool ata_scratch_armed, ata_word_source;
static unsigned ata_last_status, ata_write_status[3];
static unsigned ata_prelast_alt, ata_words_written;
#endif
static int ata_wait(struct boarddisk *sc, bool drq)
{
	for (unsigned i = 0; i < 100000; i++) {
		unsigned s = REG8(sc->base + 14);
#ifdef A2560ME_ATA_WRITECHECK
		ata_last_status = s;
#endif
		if (s == 0xff || s == 0)
			return ENXIO;
		if (!(s & 0x80)) {
			if (s & 0x21)
				return EIO;
			/* Idle/completion must not accept an unfinished data phase. */
			if (((s & 8) != 0) == drq)
				return 0;
		}
		delay(10);
	}
	return ETIMEDOUT;
}
static int ata_init(struct boarddisk *sc)
{
	uint16_t id[256];
	REG8(sc->base + 12) = 0xa0;
	delay(10);
	if (REG8(sc->base + 14) == 0xff || REG8(sc->base + 14) == 0)
		return ENXIO;
	REG8(sc->base + 4) = 0;
	REG8(sc->base + 6) = 0;
	REG8(sc->base + 8) = 0;
	REG8(sc->base + 10) = 0;
	REG8(sc->base + 14) = 0xec;
	int e = ata_wait(sc, true);
	if (e)
		return e;
	for (unsigned i = 0; i < 256; i++)
		id[i] = __builtin_bswap16(REG16(sc->base));
#ifdef A2560ME_ATA_WRITECHECK
	char serial[21];
	for (unsigned i = 0; i < 10; i++) {
		serial[2*i] = id[10+i] >> 8;
		serial[2*i+1] = id[10+i];
	}
	serial[20] = 0;
	for (int i = 19; i >= 0 && serial[i] == ' '; i--) serial[i] = 0;
	sc->scratch_identity = strcmp(serial, "K052290343") == 0;
	printf("ATA-WRITE identity serial='%s' match=%u\n", serial,
	    sc->scratch_identity);
#endif
	/* GAVIN swaps ATA lanes: IDENTIFY fields are numeric little-endian. */
#ifdef A2560ME_ATA_READCHECK
	printf("ATA-FPGA id=%#x build=%u memsys=%#x\n",
	    REG32(0xfec0000c), REG32(0xfec0003c), REG32(0xfec00038));
	printf("ATA-IDENTIFY 49=%04x 51=%04x 53=%04x 64=%04x "
	    "67=%04x 68=%04x\n",
	    id[49], id[51], id[53], id[64], id[67], id[68]);
#endif
	if (!(id[49] & 0x0200))
		return ENOTSUP;
	sc->ld.sc_secperunit =
	    uimin(((uint32_t)id[61] << 16) | id[60], 0x10000000);
	return ata_wait(sc, false);
}
static int ata_sector(struct boarddisk *sc, uint32_t lba, uint8_t *data,
		      bool wr)
{
#ifdef A2560ME_ATA_WRITECHECK
	/* Fail closed even for internal diagnostic calls. */
	if (wr && (!ata_scratch_armed || !sc->scratch_identity ||
	    sc->ld.sc_secperunit != 125045424 || lba < ATA_SCRATCH_LBA ||
	    lba - ATA_SCRATCH_LBA >= ATA_SCRATCH_COUNT))
		return EROFS;
#endif
	if (lba >= 0x10000000)
		return EFBIG;
	int e = ata_wait(sc, false);
	if (e)
		return e;
	REG8(sc->base + 12) = 0xe0 | (lba >> 24);
	delay(1);
	REG8(sc->base + 4) = 1;
	REG8(sc->base + 6) = lba;
	REG8(sc->base + 8) = lba >> 8;
	REG8(sc->base + 10) = lba >> 16;
	REG8(sc->base + 14) = wr ? 0x30 : 0x20;
	/* Allow command status to settle before polling. */
	delay(1);
	if ((e = ata_wait(sc, true)))
		return e;
#ifdef A2560ME_ATA_WRITECHECK
	if (wr) ata_write_status[0] = ata_last_status;
#endif
	/* Sector bytes already arrive in CPU memory order through GAVIN. */
#ifdef A2560ME_ATA_WRITECHECK
	/* Keep the status sample out of the first 255-word transfer loop. */
	unsigned limit = wr ? 510 : 512;
#else
	unsigned limit = 512;
#endif
	for (unsigned i = 0; i < limit; i += 2) {
		if (wr) {
#ifdef A2560ME_ATA_WRITECHECK
			if (ata_word_source)
				REG16(sc->base) = *(const uint16_t *)(data + i);
			else
#endif
				REG16(sc->base) =
				    ((unsigned)data[i] << 8) | data[i + 1];
		} else {
#ifdef A2560ME_ATA_SLOW_READ
			/* Final timing comparison: 2 us before every sector-data word. */
			delay(2);
#endif
			unsigned w = REG16(sc->base);
			data[i] = w >> 8;
			data[i + 1] = w;
		}
	}
#ifdef A2560ME_ATA_WRITECHECK
	if (wr) {
		uint16_t w = ata_word_source ? *(const uint16_t *)(data + 510) :
		    ((unsigned)data[510] << 8) | data[511];
		ata_words_written = 255;
		/* Alternate status, byte-wide, no interrupt acknowledgement.
		 * Save raw BSY/DRQ/ERR/DF; do not print or delay here.
		 */
		ata_prelast_alt = REG8(sc->base + 0x1c);
		/* Never send data when status does not permit another PIO word. */
		if (ata_prelast_alt == 0xff ||
		    (ata_prelast_alt & 0xa9) != 0x08)
			return EIO;
		REG16(sc->base) = w;
		ata_words_written = 256;
	}
#endif

	delay(1);
	if ((e = ata_wait(sc, false)))
		return e;
	if (wr) {
#ifdef A2560ME_ATA_WRITECHECK
		ata_write_status[1] = ata_last_status;
#endif
		REG8(sc->base + 14) = 0xe7;
		delay(1);
		e = ata_wait(sc, false);
#ifdef A2560ME_ATA_WRITECHECK
		ata_write_status[2] = ata_last_status;
#endif
		return e;
	}
	return 0;
}
#ifdef A2560ME_ATA_WRITECHECK
#include "ata_writecheck.h"
#endif
#ifdef A2560ME_ATA_READCHECK
void machine_halt(void);
/* Explicit diagnostic kernel: capture the real disklabel read, then halt.
 * Static buffers keep the comparison copies in the kernel's SRAM data area.
 */
static void ata_readcheck(struct boarddisk *sc, const uint8_t *data)
{
	static uint8_t original[512], repeated[512];
	const struct disklabel *label = (const void *)data;
	printf("ATA-FPGA build=%u cpu_id=%#x memsys=%#x\n",
	    REG32(0xfec0003c), REG32(0xfec0000c), REG32(0xfec00038));
	printf("ATA-LABEL GAVIN_CTRL=%#x count=%u max=%u checksum=%#x\n",
	    REG32(A2560ME_SYSTEM_CTRL), label->d_npartitions, MAXPARTITIONS,
	    label->d_npartitions <= MAXPARTITIONS ? dkcksum(label) : 0xffff);
	uint32_t sector = 2049;
#ifdef A2560ME_ATA_CHECK_LBA
	sector = A2560ME_ATA_CHECK_LBA;
#endif
	if (sector == 2049) {
		memcpy(original, data, sizeof(original));
	} else {
		int error = ata_sector(sc, sector, original, false);
		if (error) {
			printf("ATA-CHECK sector=%u initial read error=%d; "
			    "halting before filesystem mount\n", sector, error);
			machine_halt();
			return;
		}
	}
	printf("ATA-CHECK sector=%u buffer=%p; writes disabled\n", sector,
	    sector == 2049 ? data : original);
	for (unsigned pass = 0; pass <= 1000; pass++) {
		const uint8_t *bytes = original;
		if (pass != 0) {
			int error = ata_sector(sc, sector, repeated, false);
			if (error) {
				printf("ATA-CHECK pass=%u read error=%d\n", pass, error);
				break;
			}
			bytes = repeated;
		}
		unsigned differences = 0;
		for (unsigned i = 0; i < 512; i++)
			differences += bytes[i] != original[i];
		/* Keep serial output out of the back-to-back read workload. */
		if (pass != 0 && pass != 1000 && differences == 0)
			continue;
		printf("ATA-CHECK pass=%u differences=%u\n", pass, differences);
		for (unsigned i = 0; i < 512; i += 16) {
			printf("ATA-DATA %u %03x", pass, i);
			for (unsigned j = 0; j < 16; j++)
				printf(" %02x", bytes[i + j]);
			printf("\n");
		}
		if (differences != 0) {
			printf("ATA-CHECK FAIL: first mismatch retained\n");
			break;
		}
		if (pass == 1000)
			printf("ATA-CHECK 1000 rereads match initial sector; "
			    "compare initial bytes with host reference\n");
	}
	printf("ATA-CHECK complete; halting before filesystem mount\n");
	machine_halt();
}
#endif
static void disk_work(struct work *wk, void *arg)
{
	struct boarddisk *sc = arg;
	struct buf *bp = sc->bp;
	size_t done = 0;
	int e = 0;
	sc->error = 0;
	a2560me_led_disk(device_unit(sc->ld.sc_dv), true);
	while (done < bp->b_bcount) {
		uint64_t lba = (uint64_t)bp->b_rawblkno + done / 512;
		if (lba >= sc->ld.sc_secperunit || lba > UINT32_MAX) {
			e = EINVAL;
			break;
		}
		uint8_t *data = (uint8_t *)bp->b_data + done;
		e = sc->floppy
			? a2560me_fdc_sector(lba, data, !(bp->b_flags & B_READ))
		    : sc->ata
			? ata_sector(sc, lba, data, !(bp->b_flags & B_READ))
			: sd_sector(sc, lba, data, !(bp->b_flags & B_READ));
		if (e) {
			if (sc->ata)
				aprint_error_dev(sc->ld.sc_dv,
				    "ATA %s failed at sector %llu: error %d, "
				    "status %#x, error register %#x\n",
				    (bp->b_flags & B_READ) ? "read" : "write",
				    (unsigned long long)lba, e,
				    REG8(sc->base + 0x1c), REG8(sc->base + 2));
			break;
		}
		done += 512;
	}
#ifdef A2560ME_ATA_WRITECHECK
	if (!e && sc->ata && (bp->b_flags & B_READ) &&
	    bp->b_rawblkno <= 2049 &&
	    (uint64_t)bp->b_rawblkno + done / 512 > 2049)
		ata_writecheck(sc);
#endif
#ifdef A2560ME_ATA_READCHECK
	/* Wait for the entire multi-sector request, not just the label sector. */
	if (!e && sc->ata && (bp->b_flags & B_READ) &&
	    bp->b_rawblkno <= 2049 &&
	    (uint64_t)bp->b_rawblkno + done / 512 > 2049)
		ata_readcheck(sc, (const uint8_t *)bp->b_data +
		    (2049 - bp->b_rawblkno) * 512);
#endif
	a2560me_led_disk(device_unit(sc->ld.sc_dv), false);
	bp->b_error = e;
	bp->b_resid = bp->b_bcount - done;
	sc->bp = NULL;
	lddone(&sc->ld, bp);
	/* bp may have been freed by lddone; do not access it here. */
}
static int disk_start(struct ld_softc *ld, struct buf *bp)
{
	struct boarddisk *sc = (void *)ld;
#if defined(A2560ME_ATA_READCHECK) || defined(A2560ME_ATA_WRITECHECK)
	/* No disk writes at all, including if an unexpected root is selected. */
	if (!(bp->b_flags & B_READ))
		return EROFS;
#endif
	if (sc->bp)
		return EAGAIN;
	if (bp->b_bcount % 512 || bp->b_rawblkno < 0)
		return EINVAL;
	sc->bp = bp;
	workqueue_enqueue(sc->wq, &sc->work, NULL);
	return 0;
}
static int match(device_t p, cfdata_t cf, void *aux)
{
	return cf->cf_unit < 5;
}
static void attach(device_t p, device_t self, void *aux)
{
	static const uintptr_t ports[] = {0xfec00300, 0xfec00340, 0xfec00380,
					  0xfec00400, 0xfec023f0};
	struct boarddisk *sc = device_private(self);
	unsigned unit = device_unit(self);
	struct ld_softc *ld = &sc->ld;
	sc->base = ports[unit];
	sc->ata = unit == 3;
	sc->floppy = unit == 4;
	ld->sc_dv = self;
	aprint_normal(": %s at %#lx (PIO)\n",
		      sc->floppy ? "765 floppy (1.44 MiB)"
		      : sc->ata	 ? "ATA master"
				 : "SPI SD",
		      (unsigned long)sc->base);
#ifdef A2560ME_ATA_SLOW_READ
	if (sc->ata)
		aprint_normal_dev(self,
		    "ATA read-only timing test: 2 us before each data-word read\n");
#endif
	if (sc->base == 0xfec00300) {
		uint32_t status = REG32(0xfec00518);
		aprint_normal_dev(self, "SD write protect: status=%#x protected=%s\n",
		    status, sd_write_protected(status) ? "yes" : "no");
	}
	int e = sc->floppy ? a2560me_fdc_init()
		: sc->ata  ? ata_init(sc)
			   : sd_init(sc);
	if (sc->floppy)
		ld->sc_secperunit = 2880;
	if (e) {
		if (!sc->ata && !sc->floppy)
			deselect(sc);
		aprint_error_dev(self, "no usable media (%d)\n", e);
		return;
	}
	e = workqueue_create(&sc->wq, device_xname(self), disk_work, sc,
			     PRI_BIO, IPL_BIO, 0);
	if (e) {
		aprint_error_dev(self, "workqueue: %d\n", e);
		return;
	}
	ld->sc_secsize = 512;
	ld->sc_maxxfer = 4096;
	ld->sc_maxqueuecnt = 1;
	ld->sc_flags = LDF_ENABLED;
	ld->sc_start = disk_start;
	ldattach(ld, "fcfs");
	/* Explain missing/corrupt labels instead of only failing root open. */
	ld->sc_dksc.sc_flags |= DKF_WARNLABEL;
}
CFATTACH_DECL_NEW(ld_board, sizeof(struct boarddisk), match, attach, NULL,
		  NULL);
