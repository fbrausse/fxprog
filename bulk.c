
#define _POSIX_C_SOURCE	201501L	/* getopt(), optind */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libusb.h>
#include <stdio.h>
#include <unistd.h>		/* getopt(), optind */

#include "usb.h"

static int run_usb(libusb_context *ctx, libusb_device_handle *hdev, int argc, char **argv)
{
	int r;
	if (argc < 2 || argc > 3)
		return 1;

	uint8_t ep = strtol(argv[0], NULL, 0);
	unsigned len = strtol(argv[1], NULL, 0);
	unsigned timeout = argc > 2 ? strtol(argv[2], NULL, 0) : 500;

	uint8_t *buf = calloc(1, len);
	if (~ep & 0x80) {
		/* host to device transfer */
		if (!fread(buf, len, 1, stdin)) {
			fprintf(stderr, "error reading %u bytes from stdin: %s\n",
				len, strerror(errno));
			return 2;
		}
	}

	int tferd = 0;
	r = libusb_bulk_transfer(hdev, ep, buf, len, &tferd, timeout);
	if (r) {
		fprintf(stderr, "error during bulk transfer: %s\n",
			libusb_error_name(r));
		return 5;
	}

	if (ep & 0x80) {
		/* device to host transfer */
		r = fwrite(buf, tferd, 1, stdout);
	}

	return 0;
}

#define USAGE(ret,progname)	FATAL(ret,"\
usage: %s %s <ep> <wLength> [<timeout_ms>]\n\
\n\
%s%s%s\
\n\
Transfer direction: (<ep> & 0x80) ? device to host : host to device\n\
",progname,usb_common_usage, \
usb_common_help,usb_common_iface_alt_help,usb_common_iface_help)

int main(int argc, char **argv)
{
	struct usb_common uc = USB_COMMON_INIT(NULL,0,0,-1);
	int r;
	int opt;

	r = usb_common_parse_opts(&uc, argc, argv);
	if (r)
		return 1;

	while ((opt = getopt(argc, argv, ":h")) != -1)
		switch (opt) {
		case 'h': USAGE(0,argv[0]);
		case '?': FATAL(1,"illegal option: '-%c'\n", optopt);
		}

	if (argc - optind < 2 || argc - optind > 3)
		USAGE(1,argv[0]);

	r = usb_common_setup(&uc);
	if (r)
		return 2;

	r = run_usb(uc.ctx, uc.hdev, argc - optind, argv + optind);
	if (r)
		fprintf(stderr, "run_usb failed with code %d\n", r);

	usb_common_teardown(&uc);
	return r;
}
