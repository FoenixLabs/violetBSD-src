/* SPDX-License-Identifier: BSD-2-Clause */
/* Included only by the explicitly destructive scratch diagnostic. */
void machine_halt(void);
static void
ata_write_dump(const char *name, const uint8_t *p)
{
	for (unsigned i = 0; i < 512; i += 16) {
		printf("ATA-WDATA %s %03x", name, i);
		for (unsigned j = 0; j < 16; j++) printf(" %02x", p[i+j]);
		printf("\n");
	}
}
static void
ata_writecheck(struct boarddisk *sc)
{
	static uint8_t expected[512] __aligned(16);
	static uint8_t staged[512] __aligned(16);
	static uint8_t readback[512] __aligned(16);
	uint8_t *ddr = NULL;
	paddr_t pa = 0;
	int e;

	printf("ATA-WRITE build=%u id=%#x memsys=%#x capacity=%llu\n",
	    REG32(0xfec0003c), REG32(0xfec0000c), REG32(0xfec00038),
	    (unsigned long long)sc->ld.sc_secperunit);
	if (!sc->scratch_identity || sc->ld.sc_secperunit != 125045424 ||
	    REG32(0xfec0003c) != 35) {
		printf("ATA-WRITE REFUSE identity/capacity/build\n");
		goto done;
	}
	/* Refuse reruns/foreign media: the independently backed-up range is zero. */
	memset(expected, 0, sizeof(expected));
	for (unsigned i = 0; i < ATA_SCRATCH_COUNT; i++) {
		e = ata_sector(sc, ATA_SCRATCH_LBA+i, readback, false);
		if (e || memcmp(expected, readback, 512)) {
			printf("ATA-WRITE REFUSE scratch not zero lba=%u error=%d\n",
			    ATA_SCRATCH_LBA+i, e);
			goto done;
		}
	}
	ddr = kmem_alloc(512, KM_SLEEP);
	if (((uintptr_t)ddr & 1) ||
	    !pmap_extract(pmap_kernel(), (vaddr_t)ddr, &pa) ||
	    pa < 0x00800000 || pa + 512 > 0x40000000 ||
	    ((uintptr_t)ddr & (PAGE_SIZE-1)) > PAGE_SIZE-512 ||
	    (uintptr_t)staged + 512 > 0x00800000) {
		printf("ATA-WRITE REFUSE buffer placement\n");
		goto done;
	}
	printf("ATA-WRITE ddr_va=%p ddr_pa=%#lx sram=%p range=%u+%u\n",
	    ddr, (unsigned long)pa, staged, ATA_SCRATCH_LBA, ATA_SCRATCH_COUNT);
	/* This opt-in build is deployed only with capture already running.
	 * UART RX rejected the arm byte on hardware; keep RX out of this test.
	 * Identity, capacity, initial-zero and per-write bounds remain mandatory.
	 */
	printf("ATA-WRITE AUTO: scratch writes start after 5-second countdown\n");
	for (unsigned i = 5; i != 0; i--) {
		printf("ATA-WRITE countdown %u\n", i);
		delay(1000000);
	}
	ata_scratch_armed = true;
	for (unsigned mode = 0; mode < 3; mode++) {
		for (unsigned pass = 0; pass < 16; pass++) {
			uint32_t lba = ATA_SCRATCH_LBA + mode*16 + pass;
			for (unsigned i = 0; i < 256; i++) {
				uint16_t w = 0x5a00 ^ ((mode+1)<<12) ^ (pass<<8) ^ i;
				expected[2*i] = w >> 8;
				expected[2*i+1] = w;
			}
			memcpy(ddr, expected, 512);
			memcpy(staged, ddr, 512);
			if (memcmp(ddr, expected, 512) || memcmp(staged, expected, 512)) {
				printf("ATA-WRITE FAIL source before write mode=%u pass=%u\n", mode, pass);
				ata_write_dump("expected", expected);
				ata_write_dump("ddr", ddr);
				ata_write_dump("staged", staged);
				goto done;
			}
			ata_word_source = mode == 1;
			memset(ata_write_status, 0, sizeof(ata_write_status));
			ata_prelast_alt = 0x100; /* sample not reached */
			ata_words_written = 0;
			e = ata_sector(sc, lba, mode == 2 ? staged : ddr, true);
			printf("ATA-WRITE mode=%u pass=%u lba=%u write=%d status=%02x/%02x/%02x\n",
			    mode, pass, lba, e, ata_write_status[0],
			    ata_write_status[1], ata_write_status[2]);
			printf("ATA-WRITE prelast_alt=%03x words_written=%u\n",
			    ata_prelast_alt, ata_words_written);
			if (e) {
				printf("ATA-WRITE FAIL write; no retry last_status=%02x\n", ata_last_status);
				goto done;
			}
			if (memcmp(ddr, expected, 512) || memcmp(staged, expected, 512)) {
				printf("ATA-WRITE FAIL source changed after write\n");
				ata_write_dump("expected", expected);
				ata_write_dump("ddr", ddr);
				goto done;
			}
			e = ata_sector(sc, lba, readback, false);
			if (e || memcmp(expected, readback, 512)) {
				printf("ATA-WRITE FAIL readback error=%d mode=%u pass=%u lba=%u\n", e, mode, pass, lba);
				ata_write_dump("expected", expected);
				ata_write_dump("source", mode == 2 ? staged : ddr);
				ata_write_dump("readback", readback);
				goto done;
			}
		}
		printf("ATA-WRITE mode=%u PASS 16 sectors\n", mode);
	}
	printf("ATA-WRITE PASS all 48 sectors; independent Linux verification required\n");
done:
	ata_scratch_armed = false;
	ata_word_source = false;
	if (ddr != NULL) kmem_free(ddr, 512);
	printf("ATA-WRITE stopped; halting before filesystem mount\n");
	machine_halt();
}
