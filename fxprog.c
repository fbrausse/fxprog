
#define _POSIX_C_SOURCE		200809L

#include <stdlib.h>
#include <stdio.h>
#include <string.h> /* strcmp() */
#include <stddef.h> /* offsetof() */
#include <unistd.h> /* getopt() */
#include <libusb.h>
#include <sys/stat.h> /* fstat() */
#include <setjmp.h> /* yeah, yeah, evil... */

#include "usb.h"

#ifdef _POSIX_MAPPED_FILES
# include <sys/mman.h> /* mmap() */
#endif

/* defaults */
#define DEFAULT_IN_FMT		"ihex"
// #define DEFAULT_DEV_TYPE	"fx2"
#define DEFAULT_DUMP_FMT	"bin"
#define DEFAULT_TIMEOUT		200 /* ms */

#define DEFAULT_I2C_CONF	0x0e /* 128 KB Microchip EEPROM @ 100kHz */
#define DEFAULT_IMG_TYPE	0xb0 /* binary */

#define USB_REQ_FIRMWARE_LOAD	0xa0
#define FX2_REG_CPUCS		0xe600

#define VID_CYPRESS		0x04b4
#define PID_FX2			0x8613 /* default boot image identification */
#define PID_FX3			0x00f3 /* default boot image identification */

enum dev_type_t { DEV_FX2, DEV_FX3 };

static const struct dev_type dev_types[] = {
	[DEV_FX2] = { "fx2", { VID_CYPRESS, PID_FX2 } },
	[DEV_FX3] = { "fx3", { VID_CYPRESS, PID_FX3 } },
};

/* firmware data types */
struct record {
	struct record *next;
	uint32_t addr;
	uint32_t size;
	uint8_t data[];
};

static struct record * record_create(uint32_t addr, uint32_t size)
{
	struct record *r = malloc(offsetof(struct record, data) + size);
	r->next = NULL;
	r->addr = addr;
	r->size = size;
	return r;
}

static struct record * record_read_ihex(FILE *f);
static struct record * record_read_cyfw(FILE *f);
static struct record * record_read_bin(FILE *f);

/* support tables */
static const struct {
	const char *name;
	struct record * (*read)(FILE *f);
} in_fmts[] = {
	{ "ihex", record_read_ihex, },
	{ "cyfw", record_read_cyfw, },
	{ "bin", record_read_bin, }
};

static const struct {
	const char *name;
} dump_fmts[] = {
	{ "bin", },
};

/* firmware input helper functions */

static struct record * record_merge_adj(struct record *head)
{
	struct record hd = { .next = head, };
	struct record *prev = &hd, *cur, *next;

	while ((cur = prev->next) != NULL && (next = cur->next) != NULL) {
		if (cur->addr + cur->size != next->addr) {
			prev = cur;
			continue;
		}
		cur = realloc(cur, offsetof(struct record, data) + cur->size + next->size);
		prev->next = cur;
		cur->next = next->next;
		memcpy(cur->data + cur->size, next->data, next->size);
		cur->size += next->size;
		free(next);
	}

	return hd.next;
}

/* simple linear insertion sort;
 * lexicograph. ordering: 1.addr, 2.size, enables size 0 entries to be merged */
static struct record * record_sort(struct record *head)
{
	struct record ret = { .next = NULL, };
	struct record *prev, *it, *next;

	for (; head; head = next) {
		next = head->next;
		for (prev = &ret; (it = prev->next) != NULL; prev = it)
			if ((it->addr >  head->addr) ||
			    (it->addr == head->addr && it->size > head->size))
				break;
		prev->next = head;
		head->next = it;
	}

	return ret.next;
}

/* read ihex */

static jmp_buf ihex_jmp_buf;

static uint8_t nibble(char c)
{
	if ('0' <= c && c <= '9') return c - '0';
	if ('A' <= c && c <= 'F') return c - 'A' + 10;
	if ('a' <= c && c <= 'f') return c - 'a' + 10;
	longjmp(ihex_jmp_buf, 0x100 | (c & 0xff));
}

static uint8_t hex(const char *data, uint8_t *crc)
{
	uint8_t r = nibble(data[0]) << 4 | nibble(data[1]);
	*crc += r;
	return r;
}

static struct record * record_read_ihex(FILE *f)
{
	struct record *r, *head = NULL, **tail = &head;
	char *data = NULL;
	size_t dsize = 0;
	ssize_t ret;
	uint8_t crc = 0, crc_ref, type;
	unsigned size, line, j; /* ignore unnecessary gcc warning about 'line' being clobbered by longjmp */
	int jmpr;

	for (line = 1; (ret = getline(&data, &dsize, f)) > 0; line++) {
		if (ret < 11) {
			fprintf(stderr, "warning: "
				"skipping invalid line %u: too short\n",
				line);
			continue;
		}
		if (data[0] != ':') {
			fprintf(stderr, "warning: skipping invalid line %u\n",
				line);
			continue;
		}
		if ((j = strspn(data + 1, "0123456789ABCDEFabcdef")) < 8) {
			fprintf(stderr,
				"ihex contains invalid data on line %u: '%c'\n",
				line, data[1+j]);
			break;
		}
		size     = hex(data + 1, &crc);
		if (size > ret - 11) {
			fprintf(stderr,
				"ihex contains invalid line %u: size (%u) > "
				"data length (%zd)\n",
				line, size, ret - 11);
			break;
		}
		r        = malloc(offsetof(struct record, data) + size);
		*tail    = r;
		tail     = &r->next;
		r->next  = NULL;
		r->size  = size;
		r->addr  = hex(data + 3, &crc) << 8;
		r->addr |= hex(data + 5, &crc);
		type     = hex(data + 7, &crc);

		if ((jmpr = setjmp(ihex_jmp_buf))) {
			fprintf(stderr,
				"ihex contains invalid data on line %u: '%c'\n",
				line, jmpr & 0xff);
			break;
		}

		for (j=0; j<size; j++)
			r->data[j] = hex(data + 9 + (j+j), &crc);

		crc_ref = hex(data + 9 + (j+j), &crc);
		if (crc) {
			fprintf(stderr,
				"CRC failure on line %u: expected 0x%02hhx, "
				"got: 0x%02x\n",
				line, crc_ref, (crc - crc_ref) & 0xff);
			break;
		}

		/* ordinary record, no offsets / segmented memory supported */
		if (!type)
			continue;
		/* EOF record */
		if (type == 1)
			goto done;
		/* unknown record */
		fprintf(stderr, "unsupported record type 0x%02hhx on line %u\n",
			type, line);
		break;
	}

	/* failure case */
	while (head) {
		r = head->next;
		free(head);
		head = r;
	}

done:
	free(data);
	return head;
}

/* read cyfw */

static uint32_t le32toh(const uint8_t *v)
{
	uint32_t r;
	r  = v[0];
	r |= v[1] <<  8;
	r |= v[2] << 16;
	r |= v[3] << 24;
	return r;
}

static struct record * record_read_cyfw(FILE *f)
{
	struct record *r, *head = NULL, **tail = &head;
	uint32_t i, size, addr, crc = 0, crc_ref;
	uint8_t v[8];
	uint8_t bImageCTL, bImageType;
	int u;

	if (getc(f) != 'C' || getc(f) != 'Y') {
		fprintf(stderr, "input does not begin with magic 'CY'\n");
		return NULL;
	}

	if ((u = getc(f)) == EOF)
		goto eof;
	// fw->i2c_conf = u;
	if ((u = getc(f)) == EOF)
		goto eof;
	// fw->img_type = u;

	while (1) {
		if (!fread(v, 8, 1, f))
			goto eof;
		size = le32toh(v);
		addr = le32toh(v + 4);
		if (addr & 0x03)
			fprintf(stderr,
				"warning: address 0x%08x is not 32-bit "
				"aligned\n", addr);
		r = malloc(offsetof(struct record, data) + size * 4);
		*tail = r;
		tail = &r->next;
		r->next = NULL;
		r->addr = addr;
		r->size = size * 4;
		if (!size)
			break;
		if (!fread(r->data, size * 4, 1, f))
			goto eof;
		for (i=0; i<size; i++)
			crc += le32toh(r->data + 4 * i);
	}
	if (!fread(v, 4, 1, f))
		goto eof;
	crc_ref = le32toh(v); /* checksum */
	if (crc != crc_ref) {
		fprintf(stderr,
			"checksum failure: expected %08x, got %08x\n",
			crc_ref, crc);
		goto fail;
	}
	if (getc(f) != EOF)
		fprintf(stderr,
			"warning: ignoring garbage at the end of firmware data "
			"at input position %ld\n", ftell(f) - 1);
	return head;

eof:
	if (feof(f))
		fprintf(stderr, "premature EOF while reading input\n");
	else
		fprintf(stderr, "error reading input: %s\n",
			strerror(ferror(f)));
fail:
	while (head) {
		r = head->next;
		free(head);
		head = r;
	}
	return NULL;
}

static struct record * record_read_bin(FILE *f)
{
	struct stat st;
	int fd;
	struct record *r;

	fd = fileno(f);
	if (fd == -1) {
		perror("invalid input file handle");
		return NULL;
	}
	if (fstat(fd, &st) == -1) {
		perror("stat");
		return NULL;
	}

	r = malloc(offsetof(struct record, data) + st.st_size);
	r->next = NULL;
	r->addr = 0;
	r->size = st.st_size;
	if (!fread(r->data, st.st_size, 1, f)) {
		perror("reading input file");
		free(r);
		return NULL;
	}
	return r;
}

/* USB helper functions */

static int usb_query_device_fw(libusb_device_handle *hdev, unsigned timeout)
{
	int res;
	uint8_t v;
	res = libusb_control_transfer(hdev,
		0xc0, 0xa0, 0, 0, (uint8_t *)&v, sizeof(v),
		timeout);
	if (res < 0) {
		fprintf(stderr, "error checking device boot-loader type: %s\n",
			libusb_error_name(res));
	} else if ((unsigned)res == sizeof(v)) {
		return v;
	} else {
		fprintf(stderr,
			"short read while checking device boot-loader type: "
			"received: %u, requested: %zu\n", res, sizeof(v));
	}
	return -1;
}

static int usb_control_tfer(
	libusb_device_handle *hdev, int ep, int req, struct record *rec,
	uint32_t *tferd, unsigned timeout
) {
	uint8_t *data = rec->data;
	uint32_t addr = rec->addr;
	uint32_t size = rec->size;
	int res = 0;

	do {
		uint16_t sz = size > 0x1000 ? 0x1000 : size;
		fprintf(stderr,
			"submitting %02x %02x val: %04x idx: %04x len: %04x\n",
			ep, req, addr & 0xffff, addr >> 16, sz);
		res = libusb_control_transfer(hdev,
			ep, req, addr & 0xffff, addr >> 16, data, sz, timeout);
		if (res < 0) {
			fprintf(stderr, "error %s control transfer data: %s\n",
				ep & 0x80 ? "receiving" : "sending",
				libusb_error_name(res));
			res = 1;
			break;
		}
		if ((unsigned)res < sz) {
			fprintf(stderr,
				"short %s %d < %u while transferring data\n",
				ep & 0x80 ? "read" : "write", res, sz);
			res = 2;
			break;
		}
		res = 0;
		size -= sz;
		addr += sz;
		data += sz;
	} while (size);

	if (tferd)
		*tferd = rec->size - size;
	return res;
}

static int usb_upload_records(
	libusb_device_handle *hdev,
	struct record *head,
	unsigned timeout
) {
	struct record *r;
	unsigned irec;
	int res = 0;

	fprintf(stderr, "query: 0x%02x\n", usb_query_device_fw(hdev, timeout));

	for (r = head, irec = 0; r; r = r->next, irec++) {
		res = usb_control_tfer(hdev, 0x40, USB_REQ_FIRMWARE_LOAD, r, NULL, DEFAULT_TIMEOUT);
		if (res) {
			fprintf(stderr, "error uploading firmware record %u\n",
				irec);
			break;
		}
	}

	return res;
}

/* main */

static void print_help(const char *prog_name, const struct usb_common *uc);
static void print_help_cfg_byte(void);

enum mode {
	FW_UPLOAD,
	RAM_UPLOAD,
	RAM_DOWNLOAD,
	QUERY,
	/* todo: send data, recv data, benchmark IN/OUT ep(s) */
};

static int str_find(
	const char *key, const void *base, size_t nmemb, size_t size,
	size_t off, unsigned *pos
) {
	unsigned i;
	const char *b = base;
	for (i=0; i<nmemb; i++, b += size)
		if (!strcmp(key, *(const char **)(b + off))) {
			*pos = i;
			return 1;
		}
	return 0;
}

#define sfind(key, arr, field, pos)                                            \
	str_find((key), (arr), ARRAY_SIZE(arr), sizeof(*(arr)),                \
		offsetof(__typeof__(*(arr)), field), (pos))

int main(int argc, char **argv)
{
	int opt, r;
	unsigned i;

	/* device selection */
	const char *dev_addr = NULL;

	/* formats / device type */
	const char *sin_fmt   = DEFAULT_IN_FMT;
	const char *sdev_type = NULL;
	const char *sdump_fmt = DEFAULT_DUMP_FMT;

	/* mode of operation */
	const char *in   = NULL;
	const char *dump = NULL;
	const char *load = NULL;

	int cpu_reset = 1;
	int query = 0;
	int sort = 0;
	int merge = 1;

	uint8_t i2c_conf = DEFAULT_I2C_CONF;
	uint8_t img_type = DEFAULT_IMG_TYPE;

	struct usb_common uc = USB_COMMON_INIT(dev_types,ARRAY_SIZE(dev_types),-2,-2);

	r = usb_common_parse_opts(&uc, argc, argv);
	if (r)
		return 1;

	while ((opt = getopt(argc, argv, ":qf:F:d:i:rmsl:I:T:hH")) != -1) {
		switch (opt) {
		case 'q': query     = 1; break;
		case 'f': sin_fmt   = optarg; break;
		case 'F': sdump_fmt = optarg; break;
		case 'd': dump      = optarg; break;
		case 'i': in        = optarg; break;
		case 'r': cpu_reset = 0; break;
		case 'm': merge     = 0; break;
		case 's': sort      = 1; break;
		case 'l': load      = optarg; break;
		case 'I': i2c_conf  = strtoul(optarg, NULL, 0) & 0xffU; break;
		case 'T': img_type  = strtoul(optarg, NULL, 0) & 0xffU; break;
		case 'h':
			print_help(argv[0], &uc);
			return 0;
		case 'H':
			print_help_cfg_byte();
			return 0;
		case ':': FATAL(1,"option -%c needs a parameter\n",optopt);
		case '?': FATAL(1,"invalid option: -%c\n",optopt);
		}
	}

	/* check consistency and validity of supplied options */
	if (dump && load) {
		fprintf(stderr,
			"ambigious mode of operation: both load and dump RAM "
			"have been specified\n");
		exit(1);
	}
	if (dump && in) {
		fprintf(stderr, "cannot input data in dump RAM mode\n");
		exit(1);
	}

	unsigned in_fmt;
	unsigned dump_fmt;

#define find_or_die(res, key, arr, what) do {                                  \
		if (!sfind((key), (arr), name, &(res)))                        \
			fprintf(stderr, "unknown %s: %s\n", what, key),        \
			exit(1);                                               \
	} while (0)

	find_or_die(in_fmt, sin_fmt, in_fmts, "input format (-f)");
	find_or_die(dump_fmt, sdump_fmt, dump_fmts, "dump format (-F)");

#undef find_or_die

	long dump_from = 0, dump_num = 0;

	if (dump) {
		char *endptr, c;
		dump_from = strtol(dump, &endptr, 0);
		c = *endptr++;
		if (dump_from < 0 || (c != ':' && c != '+') ||
		    (dump_num = strtol(endptr, &endptr, 0)) < 0 || *endptr)
			FATAL(1,"invalid dump syntax (-d): %s\n",dump);
		if (c == ':') {
			if (dump_num < dump_from)
				FATAL(1,"invalid dump value <to> (%lu): "
					"must be >= <addr> (%lu)\n",
					dump_num, dump_from);
			dump_num -= dump_from;
		}
	}

	r = usb_common_setup(&uc);
	if (r)
		return r;

	if (query) {
		int q = usb_query_device_fw(uc.hdev, DEFAULT_TIMEOUT);
		/* FX3 -> 0x1b (hard reset - power off)
		 *        0x28 (soft reset - reset switch after having been programmed at least once)
		 * FX2 -> 0x00 */
		if (q >= 0)
			printf("0x%02x\n", q);
	} else if (dump) {
		/* dump RAM [dump_from,dump_from+dump_num) */
		struct record *rec = record_create(dump_from, dump_num);
		usb_control_tfer(uc.hdev, 0xc0, USB_REQ_FIRMWARE_LOAD, rec, &rec->size, DEFAULT_TIMEOUT);
		fwrite(rec->data, rec->size, 1, stdout);
		free(rec);
	} else if (in) {
		/* load RAM or FW */
		struct record *recs, *cpu_reset;
		FILE *fw_fd = fopen(in, "r");
		if (!fw_fd) {
			perror(in);
			goto out2;
		}
		recs = in_fmts[in_fmt].read(fw_fd);
		fclose(fw_fd);
		if (!recs)
			goto out2;
		if (sort)
			recs = record_sort(recs);
		if (merge)
			recs = record_merge_adj(recs);

#if 0
		cpu_reset = NULL;
		if (uc.spec.dev_type && dev_types - uc.spec.dev_type == DEV_FX2) {
			cpu_reset = record_create(FX2_REG_CPUCS, 1);
			cpu_reset->data[0] = 0x01;
			fprintf(stderr, "resetting CPU...\n");
			usb_upload_records(hdev, cpu_reset, DEFAULT_TIMEOUT);
			
		}

		usb_upload_records(hdev, recs, DEFAULT_TIMEOUT);

		if (uc.spec.dev_type && dev_types - uc.spec.dev_type == DEV_FX2) {
			cpu_reset->data[0] = 0x00;
			fprintf(stderr, "resuming CPU...\n");
			usb_upload_records(hdev, cpu_reset, DEFAULT_TIMEOUT);
			free(cpu_reset);
		}
#else
		if (uc.spec.dev_type && dev_types - uc.spec.dev_type == DEV_FX2) {
			fprintf(stderr, "resetting CPU...\n");
			libusb_control_transfer(uc.hdev, 0x40, USB_REQ_FIRMWARE_LOAD,
				FX2_REG_CPUCS, 0x0000, (uint8_t[]){0x01}, 1,
				DEFAULT_TIMEOUT);
		}

		usb_upload_records(uc.hdev, recs, DEFAULT_TIMEOUT);

		if (uc.spec.dev_type && dev_types - uc.spec.dev_type == DEV_FX2) {
			fprintf(stderr, "resuming CPU...\n");
			libusb_control_transfer(uc.hdev, 0x40, USB_REQ_FIRMWARE_LOAD,
				FX2_REG_CPUCS, 0x0000, (uint8_t[]){0x00}, 1,
				DEFAULT_TIMEOUT);
		}
#endif

		while (recs) {
			struct record *next = recs->next;
			free(recs);
			recs = next;
		}
	}

out2:
	usb_common_teardown(&uc);
	return r;
}

static void print_help(const char *prog_name, const struct usb_common *uc)
{
	unsigned i;
	char *uc_help = usb_common_help(uc);
	char *uc_usage = usb_common_usage(uc);

	printf("usage: %s %s <OPERATION>\n", prog_name, uc_usage);
	printf("\n");
	printf("<OPERATION> is one of:\n");
/*
	printf("  prog    [-f <fmt>] [-i <fw.dat>] [-r]    -- load RAM w/ firmware\n");
	printf("  ram_get <addr>{:<to>|+<size>}            -- dump RAM data\n");
	printf("  ram_put <addr> [-i <in.bin>]             -- load RAM w/ data\n");
	printf("  ep_get  <ep> [<size>]                    -- dump data read from an endpoint\n");
	printf("  ep_put  <ep> [-i <in.bin>]               -- push data to an endpoint\n");
*/
	printf("load RAM w/ firmware      : [-f <fmt>] [-i <fw.dat>] [-r]\n");
	printf("load RAM w/ arbitrary data: -l <addr> [-i <in.bin>]\n");
	printf("dump RAM                  : [-F <fmt>] -d <addr>{:<to>|+<size>}\n");
	printf("\n");
	printf("%s", uc_help);
	printf("  -q              query device for boot-loader fw type\n");
	printf("  -f <format>     format of input data, default: " DEFAULT_IN_FMT "\n");
	printf("                  supported:");
	for (i=0; i<ARRAY_SIZE(in_fmts); i++)
		printf(" %s%c", in_fmts[i].name,
			i < ARRAY_SIZE(in_fmts) - 1
			? ',' : '\n');
	printf("  -i <fw.dat>     data to write to the USB device\n");
	printf("  -r              don't reset CPU while loading the FW\n");
	printf("  -m              don't merge adjacent to-be-transferred entries\n");
	printf("  -s              do sort entries prior to merging / transmission\n");
	printf("  -I <i2c-conf>   i2c configuration byte (unchecked), default: 0x%02x\n", DEFAULT_I2C_CONF);
	printf("  -T <img-type>   image type configuration byte (unchecked), default: 0x%02x\n", DEFAULT_IMG_TYPE);
	printf("  -F <format>     format to dump RAM contents, default: " DEFAULT_DUMP_FMT "\n");
	printf("                  supported:");
	for (i=0; i<ARRAY_SIZE(dump_fmts); i++)
		printf(" %s%c", dump_fmts[i].name,
			i < ARRAY_SIZE(dump_fmts) - 1
			? ',' : '\n');
	printf("  -d <addr>{:<to>|+<size>}\n") ;
	printf("                  dump RAM contents from <addr> to (excl.) either <to> or\n");
	printf("                  <addr>+<size> as binary data to stdout\n");
	printf("  -h              print this help message\n");
	printf("  -H              print details about the i2c and image type configuration bytes\n");

	free(uc_usage);
	free(uc_help);
}

static void print_help_cfg_byte(void)
{
	printf("i2c configuration byte, default: 0x%02x\n", DEFAULT_I2C_CONF);
	printf(
		"  Bit [0]: 0: execution binary file / 1: data file type\n"
		"    [3:1]: i2c size (unused for SPI EEPROM):\n"
		"              2: 4KB, 3: 8KB, 4: 16KB, 5: 32KB, 6: 64KB (128KB Atmel),\n"
		"              7: 128KB (Microchip)\n"
		"    [5:4]: i2c speed for i2c-booting:\n"
		"              0: 100kHz, 1: 400kHz, 2: 1MHz, 3: 3.4MHz (reserved)\n"
		"           SPI speed for SPI-booting:\n"
		"              0: 10 MHz, 1: 20 MHz, 2: 30 MHz, 3: 40 MHz (reserved)\n"
		"    [7:6]: reserved, 0\n");
	printf("\n");
	printf("image type byte, default: 0x%02x\n", DEFAULT_IMG_TYPE);
	printf(
		"  0xb0: normal FW binary image w/ checksum\n"
		"  0xb1: reserved for security image type\n"
		"  0xb2: boot w/ new VID and PID\n");
}
