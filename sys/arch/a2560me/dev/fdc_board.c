/* SPDX-License-Identifier: BSD-2-Clause */
/* Conservative 1.44 MiB, drive-0, non-DMA 765 backend. No formatting. */
#include <sys/param.h>
#include <sys/systm.h>
#include <machine/board.h>
int a2560me_fdc_init(void);
int a2560me_fdc_sector(uint32_t, uint8_t *, bool);
void a2560me_fdc_motoroff(void);
static int fifo(bool read, uint8_t *v)
{
	for (unsigned n = 0; n < 10000; n++) {
		unsigned s = REG8(0xfec023f4);
		if ((s & 0xc0) == (read ? 0xc0 : 0x80)) {
			if (read)
				*v = REG8(0xfec023f5);
			else
				REG8(0xfec023f5) = *v;
			return 0;
		}
		delay(10);
	}
	return ETIMEDOUT;
}
static int send(const uint8_t *p, unsigned n)
{
	while (n--) {
		uint8_t v = *p++;
		int e = fifo(false, &v);
		if (e)
			return e;
	}
	return 0;
}
static void sio(unsigned r, unsigned v)
{
	REG8(0xfec0202e) = r;
	REG8(0xfec0202f) = v;
}
int a2560me_fdc_init(void)
{
	REG8(0xfec0202e) = 0x55;
	sio(7, 0);
	sio(0x60, 3);
	sio(0x61, 0xf0);
	sio(0x70, 6);
	sio(0x30, 1);
	REG8(0xfec0202e) = 0xaa;
	if (REG8(0xfec023f4) == 0xff)
		return ENXIO;
	REG8(0xfec023f2) = 0;
	delay(100);
	REG8(0xfec023f2) = 0x0c;
	delay(1000);
	/* Drain reset status for four logical drives. */
	for (unsigned n = 0; n < 4; n++) {
		uint8_t cmd = 8, r;
		if (send(&cmd, 1) || fifo(true, &r))
			return EIO;
		if (r != 0x80 && fifo(true, &r))
			return EIO;
	}
	REG8(0xfec023f7) = 0; /* 500 kbit/s */
	const uint8_t specify[] = {3, 0xc0, 0x0b};
	return send(specify, sizeof(specify));
}
void a2560me_fdc_motoroff(void)
{
	REG8(0xfec023f2) = 0x0c;
}
static int seek(const uint8_t *cmd, unsigned n, unsigned cylinder)
{
	if (send(cmd, n))
		return EIO;
	for (unsigned i = 0; i < 200; i++) {
		delay(10000);
		uint8_t sense = 8, st, cyl;
		if (send(&sense, 1) || fifo(true, &st))
			return EIO;
		if (st == 0x80)
			continue;
		if (fifo(true, &cyl))
			return EIO;
		if (st & 0x20)
			return ((st & 0xc0) == 0 && cyl == cylinder) ? 0 : EIO;
	}
	return ETIMEDOUT;
}
int a2560me_fdc_sector(uint32_t lba, uint8_t *data, bool wr)
{
	if (lba >= 2880)
		return EINVAL;
	/* Reinitialize after any prior partial transaction; then recalibrate.
	 */
	int e = a2560me_fdc_init();
	if (e)
		return e;
	REG8(0xfec023f2) = 0x1c;
	delay(500000);
	uint8_t recal[] = {7, 0};
	if ((e = seek(recal, 2, 0)))
		goto out;
	unsigned c = lba / 36, h = (lba / 18) % 2, r = lba % 18 + 1;
	uint8_t scmd[] = {15, h << 2, c};
	if ((e = seek(scmd, 3, c)))
		goto out;
	delay(15000);
	uint8_t cmd[] = {wr ? 0x45 : 0x46, h << 2, c, h, r, 2, r, 0x1b, 0xff};
	if ((e = send(cmd, sizeof(cmd))))
		goto out;
	/* Data phase must have NDMA set; never mistake result bytes for data.
	 */
	for (unsigned i = 0; i < 512; i++) {
		unsigned n, s = 0;
		for (n = 0; n < 100000; n++) {
			s = REG8(0xfec023f4);
			if (s & 0x80)
				break;
		}
		if (n == 100000) {
			e = ETIMEDOUT;
			goto out;
		}
		if ((s & 0xe0) != (wr ? 0xa0 : 0xe0)) {
			e = EIO;
			goto out;
		}
		if (wr)
			REG8(0xfec023f5) = data[i];
		else
			data[i] = REG8(0xfec023f5);
	}
	uint8_t result[7];
	for (unsigned i = 0; i < 7; i++)
		if ((e = fifo(true, &result[i])))
			goto out;
	/* EOT can terminate a successful one-sector transfer with EN set. */
	if (((result[0] & 0xc0) &&
	     !((result[0] & 0xc0) == 0x40 && result[1] == 0x80)) ||
	    (result[1] & ~0x80) || result[2])
		e = EIO;
out:
	a2560me_fdc_motoroff();
	return e;
}
