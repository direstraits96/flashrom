/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2000 Silicon Integrated System Corporation
 * Copyright (C) 2004 Tyan Corp <yhlu@tyan.com>
 * Copyright (C) 2005-2008 coresystems GmbH 
 * Copyright (C) 2008,2009 Carl-Daniel Hailfinger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include "flash.h"

char *chip_to_probe = NULL;
int exclude_start_page, exclude_end_page;
int verbose = 0;
int programmer = PROGRAMMER_INTERNAL;

const struct programmer_entry programmer_table[] = {
	{
		.init			= internal_init,
		.shutdown		= internal_shutdown,
		.map_flash_region	= physmap,
		.unmap_flash_region	= physunmap,
		.chip_readb		= internal_chip_readb,
		.chip_readw		= internal_chip_readw,
		.chip_readl		= internal_chip_readl,
		.chip_writeb		= internal_chip_writeb,
		.chip_writew		= internal_chip_writew,
		.chip_writel		= internal_chip_writel,
	},

	{
		.init			= dummy_init,
		.shutdown		= dummy_shutdown,
		.map_flash_region	= dummy_map,
		.unmap_flash_region	= dummy_unmap,
		.chip_readb		= dummy_chip_readb,
		.chip_readw		= dummy_chip_readw,
		.chip_readl		= dummy_chip_readl,
		.chip_writeb		= dummy_chip_writeb,
		.chip_writew		= dummy_chip_writew,
		.chip_writel		= dummy_chip_writel,
	},

	{
		.init			= nic3com_init,
		.shutdown		= nic3com_shutdown,
		.map_flash_region	= nic3com_map,
		.unmap_flash_region	= nic3com_unmap,
		.chip_readb		= nic3com_chip_readb,
		.chip_readw		= fallback_chip_readw,
		.chip_readl		= fallback_chip_readl,
		.chip_writeb		= nic3com_chip_writeb,
		.chip_writew		= fallback_chip_writew,
		.chip_writel		= fallback_chip_writel,
	},

	{
		.init			= satasii_init,
		.shutdown		= satasii_shutdown,
		.map_flash_region	= satasii_map,
		.unmap_flash_region	= satasii_unmap,
		.chip_readb		= satasii_chip_readb,
		.chip_readw		= fallback_chip_readw,
		.chip_readl		= fallback_chip_readl,
		.chip_writeb		= satasii_chip_writeb,
		.chip_writew		= fallback_chip_writew,
		.chip_writel		= fallback_chip_writel,
	},

	{},
};

int programmer_init(void)
{
	return programmer_table[programmer].init();
}

int programmer_shutdown(void)
{
	return programmer_table[programmer].shutdown();
}

void *programmer_map_flash_region(const char *descr, unsigned long phys_addr,
				  size_t len)
{
	return programmer_table[programmer].map_flash_region(descr,
							     phys_addr, len);
}

void programmer_unmap_flash_region(void *virt_addr, size_t len)
{
	programmer_table[programmer].unmap_flash_region(virt_addr, len);
}

void chip_writeb(uint8_t val, chipaddr addr)
{
	programmer_table[programmer].chip_writeb(val, addr);
}

void chip_writew(uint16_t val, chipaddr addr)
{
	programmer_table[programmer].chip_writew(val, addr);
}

void chip_writel(uint32_t val, chipaddr addr)
{
	programmer_table[programmer].chip_writel(val, addr);
}

uint8_t chip_readb(const chipaddr addr)
{
	return programmer_table[programmer].chip_readb(addr);
}

uint16_t chip_readw(const chipaddr addr)
{
	return programmer_table[programmer].chip_readw(addr);
}

uint32_t chip_readl(const chipaddr addr)
{
	return programmer_table[programmer].chip_readl(addr);
}

void map_flash_registers(struct flashchip *flash)
{
	size_t size = flash->total_size * 1024;
	/* Flash registers live 4 MByte below the flash. */
	flash->virtual_registers = (chipaddr)programmer_map_flash_region("flash chip registers", (0xFFFFFFFF - 0x400000 - size + 1), size);
}

int read_memmapped(struct flashchip *flash, uint8_t *buf)
{
	int i;

	/* We could do a memcpy as optimization if the flash is onboard */
	//memcpy(buf, (const char *)flash->virtual_memory, flash->total_size * 1024);
	for (i = 0; i < flash->total_size * 1024; i++)
		buf[i] = chip_readb(flash->virtual_memory + i);
		
	return 0;
}

struct flashchip *probe_flash(struct flashchip *first_flash, int force)
{
	struct flashchip *flash;
	unsigned long base = 0, size;

	for (flash = first_flash; flash && flash->name; flash++) {
		if (chip_to_probe && strcmp(flash->name, chip_to_probe) != 0)
			continue;
		printf_debug("Probing for %s %s, %d KB: ",
			     flash->vendor, flash->name, flash->total_size);
		if (!flash->probe && !force) {
			printf_debug("failed! flashrom has no probe function for this flash chip.\n");
			continue;
		}

		size = flash->total_size * 1024;

		base = flashbase ? flashbase : (0xffffffff - size + 1);
		flash->virtual_memory = (chipaddr)programmer_map_flash_region("flash chip", base, size);

		if (force)
			break;

		if (flash->probe(flash) != 1)
			goto notfound;

		if (first_flash == flashchips
		    || flash->model_id != GENERIC_DEVICE_ID)
			break;

notfound:
		programmer_unmap_flash_region((void *)flash->virtual_memory, size);
	}

	if (!flash || !flash->name)
		return NULL;

	printf("Found chip \"%s %s\" (%d KB) at physical address 0x%lx.\n",
	       flash->vendor, flash->name, flash->total_size, base);
	return flash;
}

int verify_flash(struct flashchip *flash, uint8_t *buf)
{
	int idx;
	int total_size = flash->total_size * 1024;
	uint8_t *buf2 = (uint8_t *) calloc(total_size, sizeof(char));
	if (!flash->read) {
		printf("FAILED!\n");
		fprintf(stderr, "ERROR: flashrom has no read function for this flash chip.\n");
		return 1;
	} else
		flash->read(flash, buf2);

	printf("Verifying flash... ");

	if (verbose)
		printf("address: 0x00000000\b\b\b\b\b\b\b\b\b\b");

	for (idx = 0; idx < total_size; idx++) {
		if (verbose && ((idx & 0xfff) == 0xfff))
			printf("0x%08x", idx);

		if (*(buf2 + idx) != *(buf + idx)) {
			if (verbose)
				printf("0x%08x FAILED!", idx);
			else
				printf("FAILED at 0x%08x!", idx);
			printf("  Expected=0x%02x, Read=0x%02x\n",
			       *(buf + idx), *(buf2 + idx));
			return 1;
		}

		if (verbose && ((idx & 0xfff) == 0xfff))
			printf("\b\b\b\b\b\b\b\b\b\b");
	}
	if (verbose)
		printf("\b\b\b\b\b\b\b\b\b\b ");

	printf("VERIFIED.          \n");

	return 0;
}

int read_flash(struct flashchip *flash, char *filename, unsigned int exclude_start_position, unsigned int exclude_end_position)
{
	unsigned long numbytes;
	FILE *image;
	unsigned long size = flash->total_size * 1024;
	unsigned char *buf = calloc(size, sizeof(char));
	if ((image = fopen(filename, "w")) == NULL) {
		perror(filename);
		exit(1);
	}
	printf("Reading flash... ");
	if (!flash->read) {
		printf("FAILED!\n");
		fprintf(stderr, "ERROR: flashrom has no read function for this flash chip.\n");
		return 1;
	} else
		flash->read(flash, buf);

	if (exclude_end_position - exclude_start_position > 0)
		memset(buf + exclude_start_position, 0,
		       exclude_end_position - exclude_start_position);

	numbytes = fwrite(buf, 1, size, image);
	fclose(image);
	printf("%s.\n", numbytes == size ? "done" : "FAILED");
	if (numbytes != size)
		return 1;
	return 0;
}

int erase_flash(struct flashchip *flash)
{
	uint32_t erasedbytes;
	unsigned long size = flash->total_size * 1024;
	unsigned char *buf = calloc(size, sizeof(char));
	printf("Erasing flash chip... ");
	if (NULL == flash->erase) {
		printf("FAILED!\n");
		fprintf(stderr, "ERROR: flashrom has no erase function for this flash chip.\n");
		return 1;
	}
	flash->erase(flash);

	if (!flash->read) {
		printf("FAILED!\n");
		fprintf(stderr, "ERROR: flashrom has no read function for this flash chip.\n");
		return 1;
	} else
		flash->read(flash, buf);

	for (erasedbytes = 0; erasedbytes < size; erasedbytes++)
		if (0xff != buf[erasedbytes]) {
			printf("FAILED!\n");
			fprintf(stderr, "ERROR at 0x%08x: Expected=0xff, Read=0x%02x\n",
				erasedbytes, buf[erasedbytes]);
			return 1;
		}
	printf("SUCCESS.\n");
	return 0;
}

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define POS_PRINT(x) do { pos += strlen(x); printf(x); } while (0)

void print_supported_chips(void)
{
	int okcol = 0, pos = 0;
	struct flashchip *f;

	for (f = flashchips; f->name != NULL; f++) {
		if (GENERIC_DEVICE_ID == f->model_id)
			continue;
		okcol = MAX(okcol, strlen(f->vendor) + 1 + strlen(f->name));
	}
	okcol = (okcol + 7) & ~7;

	POS_PRINT("Supported flash chips:");
	while (pos < okcol) {
		printf("\t");
		pos += 8 - (pos % 8);
	}
	printf("Tested OK operations:\tKnown BAD operations:\n\n");

	for (f = flashchips; f->name != NULL; f++) {
		printf("%s %s", f->vendor, f->name);
		pos = strlen(f->vendor) + 1 + strlen(f->name);
		while (pos < okcol) {
			printf("\t");
			pos += 8 - (pos % 8);
		}
		if ((f->tested & TEST_OK_MASK)) {
			if ((f->tested & TEST_OK_PROBE))
				POS_PRINT("PROBE ");
			if ((f->tested & TEST_OK_READ))
				POS_PRINT("READ ");
			if ((f->tested & TEST_OK_ERASE))
				POS_PRINT("ERASE ");
			if ((f->tested & TEST_OK_WRITE))
				POS_PRINT("WRITE");
		}
		while (pos < okcol + 24) {
			printf("\t");
			pos += 8 - (pos % 8);
		}
		if ((f->tested & TEST_BAD_MASK)) {
			if ((f->tested & TEST_BAD_PROBE))
				printf("PROBE ");
			if ((f->tested & TEST_BAD_READ))
				printf("READ ");
			if ((f->tested & TEST_BAD_ERASE))
				printf("ERASE ");
			if ((f->tested & TEST_BAD_WRITE))
				printf("WRITE");
		}
		printf("\n");
	}
}

void usage(const char *name)
{
	printf("usage: %s [-rwvEVfLhR] [-c chipname] [-s exclude_start]\n",
	       name);
	printf("       [-e exclude_end] [-m [vendor:]part] [-l file.layout] [-i imagename] [file]\n");
	printf("Please  note that the command line interface for flashrom will "
		"change before flashrom 1.0. Do not use flashrom in scripts or "
		"other automated tools without checking that your flashrom "
		"version won't interpret them in a totally different way.\n\n");
	printf
	    ("   -r | --read:                      read flash and save into file\n"
	     "   -w | --write:                     write file into flash\n"
	     "   -v | --verify:                    verify flash against file\n"
	     "   -E | --erase:                     erase flash device\n"
	     "   -V | --verbose:                   more verbose output\n"
	     "   -c | --chip <chipname>:           probe only for specified flash chip\n"
	     "   -s | --estart <addr>:             exclude start position\n"
	     "   -e | --eend <addr>:               exclude end postion\n"
	     "   -m | --mainboard <[vendor:]part>: override mainboard settings\n"
	     "   -f | --force:                     force write without checking image\n"
	     "   -l | --layout <file.layout>:      read rom layout from file\n"
	     "   -i | --image <name>:              only flash image name from flash layout\n"
	     "   -L | --list-supported:            print supported devices\n"
	     "   -p | --programmer <name>:         specify the programmer device\n"
	     "   -h | --help:                      print this help text\n"
	     "   -R | --version:                   print the version (release)\n"
	     "\n" " If no file is specified, then all that happens"
	     " is that flash info is dumped.\n\n");
	exit(1);
}

void print_version(void)
{
	printf("flashrom v%s\n", FLASHROM_VERSION);
}

int main(int argc, char *argv[])
{
	uint8_t *buf;
	unsigned long size, numbytes;
	FILE *image;
	/* Probe for up to three flash chips. */
	struct flashchip *flash, *flashes[3];
	int opt;
	int option_index = 0;
	int force = 0;
	int read_it = 0, write_it = 0, erase_it = 0, verify_it = 0;
	int list_supported = 0;
	int ret = 0, i;

	static struct option long_options[] = {
		{"read", 0, 0, 'r'},
		{"write", 0, 0, 'w'},
		{"erase", 0, 0, 'E'},
		{"verify", 0, 0, 'v'},
		{"chip", 1, 0, 'c'},
		{"estart", 1, 0, 's'},
		{"eend", 1, 0, 'e'},
		{"mainboard", 1, 0, 'm'},
		{"verbose", 0, 0, 'V'},
		{"force", 0, 0, 'f'},
		{"layout", 1, 0, 'l'},
		{"image", 1, 0, 'i'},
		{"list-supported", 0, 0, 'L'},
		{"programmer", 1, 0, 'p'},
		{"help", 0, 0, 'h'},
		{"version", 0, 0, 'R'},
		{0, 0, 0, 0}
	};

	char *filename = NULL;

	unsigned int exclude_start_position = 0, exclude_end_position = 0;	// [x,y)
	char *tempstr = NULL, *tempstr2 = NULL;

	print_version();

	if (argc > 1) {
		/* Yes, print them. */
		int i;
		printf_debug("The arguments are:\n");
		for (i = 1; i < argc; ++i)
			printf_debug("%s\n", argv[i]);
	}

	setbuf(stdout, NULL);
	while ((opt = getopt_long(argc, argv, "rRwvVEfc:s:e:m:l:i:p:Lh",
				  long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'r':
			read_it = 1;
			break;
		case 'w':
			write_it = 1;
			break;
		case 'v':
			verify_it = 1;
			break;
		case 'c':
			chip_to_probe = strdup(optarg);
			break;
		case 'V':
			verbose = 1;
			break;
		case 'E':
			erase_it = 1;
			break;
		case 's':
			tempstr = strdup(optarg);
			sscanf(tempstr, "%x", &exclude_start_position);
			break;
		case 'e':
			tempstr = strdup(optarg);
			sscanf(tempstr, "%x", &exclude_end_position);
			break;
		case 'm':
			tempstr = strdup(optarg);
			strtok(tempstr, ":");
			tempstr2 = strtok(NULL, ":");
			if (tempstr2) {
				lb_vendor = tempstr;
				lb_part = tempstr2;
			} else {
				lb_vendor = NULL;
				lb_part = tempstr;
			}
			break;
		case 'f':
			force = 1;
			break;
		case 'l':
			tempstr = strdup(optarg);
			if (read_romlayout(tempstr))
				exit(1);
			break;
		case 'i':
			tempstr = strdup(optarg);
			find_romentry(tempstr);
			break;
		case 'L':
			list_supported = 1;
			break;
		case 'p':
			if (strncmp(optarg, "internal", 8) == 0) {
				programmer = PROGRAMMER_INTERNAL;
			} else if (strncmp(optarg, "dummy", 5) == 0) {
				programmer = PROGRAMMER_DUMMY;
			} else if (strncmp(optarg, "nic3com", 7) == 0) {
				programmer = PROGRAMMER_NIC3COM;
				if (optarg[7] == '=')
					pcidev_bdf = strdup(optarg + 8);
			} else if (strncmp(optarg, "satasii", 7) == 0) {
				programmer = PROGRAMMER_SATASII;
				if (optarg[7] == '=')
					pcidev_bdf = strdup(optarg + 8);
			} else {
				printf("Error: Unknown programmer.\n");
				exit(1);
			}
			break;
		case 'R':
			/* print_version() is always called during startup. */
			exit(0);
			break;
		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	if (list_supported) {
		print_supported_chips();
		print_supported_chipsets();
		print_supported_boards();
		printf("\nSupported PCI devices flashrom can use "
		       "as programmer:\n\n");
		print_supported_pcidevs(nics_3com);
		print_supported_pcidevs(satas_sii);
		exit(0);
	}

	if (read_it && write_it) {
		printf("Error: -r and -w are mutually exclusive.\n");
		usage(argv[0]);
	}

	if (optind < argc)
		filename = argv[optind++];

	ret = programmer_init();

	myusec_calibrate_delay();

	for (i = 0; i < ARRAY_SIZE(flashes); i++) {
		flashes[i] =
		    probe_flash(i ? flashes[i - 1] + 1 : flashchips, 0);
		if (!flashes[i])
			for (i++; i < ARRAY_SIZE(flashes); i++)
				flashes[i] = NULL;
	}

	if (flashes[1]) {
		printf("Multiple flash chips were detected:");
		for (i = 0; i < ARRAY_SIZE(flashes) && flashes[i]; i++)
			printf(" %s", flashes[i]->name);
		printf("\nPlease specify which chip to use with the -c <chipname> option.\n");
		exit(1);
	} else if (!flashes[0]) {
		printf("No EEPROM/flash device found.\n");
		if (!force || !chip_to_probe) {
			printf("If you know which flash chip you have, and if this version of flashrom\n");
			printf("supports a similar flash chip, you can try to force read your chip. Run:\n");
			printf("flashrom -f -r -c similar_supported_flash_chip filename\n");
			printf("\n");
			printf("Note: flashrom can never write when the flash chip isn't found automatically.\n");
		}
		if (force && read_it && chip_to_probe) {
			printf("Force read (-f -r -c) requested, forcing chip probe success:\n");
			flashes[0] = probe_flash(flashchips, 1);
			if (!flashes[0]) {
				printf("flashrom does not support a flash chip named '%s'.\n", chip_to_probe);
				printf("Run flashrom -L to view the hardware supported in this flashrom version.\n");
				exit(1);
			}
			if (!filename) {
				printf("Error: No filename specified.\n");
				exit(1);
			}
			size = flashes[0]->total_size * 1024;
			buf = (uint8_t *) calloc(size, sizeof(char));

			if ((image = fopen(filename, "w")) == NULL) {
				perror(filename);
				exit(1);
			}
			printf("Force reading flash... ");
			if (!flashes[0]->read) {
				printf("FAILED!\n");
				fprintf(stderr, "ERROR: flashrom has no read function for this flash chip.\n");
				return 1;
			} else
				flashes[0]->read(flashes[0], buf);

			if (exclude_end_position - exclude_start_position > 0)
				memset(buf + exclude_start_position, 0,
				       exclude_end_position -
				       exclude_start_position);

			numbytes = fwrite(buf, 1, size, image);
			fclose(image);
			printf("%s.\n", numbytes == size ? "done" : "FAILED");
			free(buf);
			return numbytes != size;
		}
		// FIXME: flash writes stay enabled!
		exit(1);
	}

	flash = flashes[0];

	if (TEST_OK_MASK != (flash->tested & TEST_OK_MASK)) {
		printf("===\n");
		if (flash->tested & TEST_BAD_MASK) {
			printf("This flash part has status NOT WORKING for operations:");
			if (flash->tested & TEST_BAD_PROBE)
				printf(" PROBE");
			if (flash->tested & TEST_BAD_READ)
				printf(" READ");
			if (flash->tested & TEST_BAD_ERASE)
				printf(" ERASE");
			if (flash->tested & TEST_BAD_WRITE)
				printf(" WRITE");
			printf("\n");
		}
		if ((!(flash->tested & TEST_BAD_PROBE) && !(flash->tested & TEST_OK_PROBE)) ||
		    (!(flash->tested & TEST_BAD_READ) && !(flash->tested & TEST_OK_READ)) ||
		    (!(flash->tested & TEST_BAD_ERASE) && !(flash->tested & TEST_OK_ERASE)) ||
		    (!(flash->tested & TEST_BAD_WRITE) && !(flash->tested & TEST_OK_WRITE))) {
			printf("This flash part has status UNTESTED for operations:");
			if (!(flash->tested & TEST_BAD_PROBE) && !(flash->tested & TEST_OK_PROBE))
				printf(" PROBE");
			if (!(flash->tested & TEST_BAD_READ) && !(flash->tested & TEST_OK_READ))
				printf(" READ");
			if (!(flash->tested & TEST_BAD_ERASE) && !(flash->tested & TEST_OK_ERASE))
				printf(" ERASE");
			if (!(flash->tested & TEST_BAD_WRITE) && !(flash->tested & TEST_OK_WRITE))
				printf(" WRITE");
			printf("\n");
		}
		printf("Please email a report to flashrom@coreboot.org if any of the above operations\n");
		printf("work correctly for you with this flash part. Please include the full output\n");
		printf("from the program, including chipset found. Thank you for your help!\n");
		printf("===\n");
	}

	if (!(read_it | write_it | verify_it | erase_it)) {
		printf("No operations were specified.\n");
		// FIXME: flash writes stay enabled!
		exit(1);
	}

	if (!filename && !erase_it) {
		printf("Error: No filename specified.\n");
		// FIXME: flash writes stay enabled!
		exit(1);
	}

	size = flash->total_size * 1024;
	buf = (uint8_t *) calloc(size, sizeof(char));

	if (erase_it) {
		if (erase_flash(flash))
			return 1;
	} else if (read_it) {
		if (read_flash(flash, filename, exclude_start_position, exclude_end_position))
			return 1;
	} else {
		struct stat image_stat;

		if ((image = fopen(filename, "r")) == NULL) {
			perror(filename);
			exit(1);
		}
		if (fstat(fileno(image), &image_stat) != 0) {
			perror(filename);
			exit(1);
		}
		if (image_stat.st_size != flash->total_size * 1024) {
			fprintf(stderr, "Error: Image size doesn't match\n");
			exit(1);
		}

		numbytes = fread(buf, 1, size, image);
		show_id(buf, size, force);
		fclose(image);
		if (numbytes != size) {
			fprintf(stderr, "Error: Failed to read file. Got %ld bytes, wanted %ld!\n", numbytes, size);
			return 1;
		}
	}

	/* exclude range stuff. Nice idea, but at the moment it is only
	 * supported in hardware by the pm49fl004 chips. 
	 * Instead of implementing this for all chips I suggest advancing
	 * it to the rom layout feature below and drop exclude range
	 * completely once all flash chips can do rom layouts. stepan
	 */

	// ////////////////////////////////////////////////////////////
	/* FIXME: This memcpy will not work for SPI nor external flashers.
	 * Convert to chip_readb.
	 */
	if (exclude_end_position - exclude_start_position > 0)
		memcpy(buf + exclude_start_position,
		       (const char *)flash->virtual_memory +
		       exclude_start_position,
		       exclude_end_position - exclude_start_position);

	exclude_start_page = exclude_start_position / flash->page_size;
	if ((exclude_start_position % flash->page_size) != 0) {
		exclude_start_page++;
	}
	exclude_end_page = exclude_end_position / flash->page_size;
	// ////////////////////////////////////////////////////////////

	// This should be moved into each flash part's code to do it 
	// cleanly. This does the job.
	handle_romentries(buf, (uint8_t *) flash->virtual_memory);

	// ////////////////////////////////////////////////////////////

	if (write_it) {
		if (!flash->write) {
			fprintf(stderr, "Error: flashrom has no write function for this flash chip.\n");
			return 1;
		}
		ret |= flash->write(flash, buf);
	}

	if (verify_it)
		ret |= verify_flash(flash, buf);

	programmer_shutdown();

	return ret;
}
