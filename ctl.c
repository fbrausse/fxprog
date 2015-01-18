
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
	if (argc < 5 || argc > 6)
		return 1;

	uint8_t  bmRequestType = strtol(argv[0], NULL, 0);
	uint8_t  bRequest      = strtol(argv[1], NULL, 0);
	uint16_t wValue        = strtol(argv[2], NULL, 0);
	uint16_t wIndex        = strtol(argv[3], NULL, 0);
	uint16_t wLength       = strtol(argv[4], NULL, 0);
	unsigned timeout = argc > 5 ? strtol(argv[5], NULL, 0) : 500;

	uint8_t *buf = calloc(1, wLength);

	if (~bmRequestType & 0x80) {
		/* host to device transfer */
		if (!fread(buf, wLength, 1, stdin)) {
			fprintf(stderr, "error reading %hu bytes from stdin: %s\n",
				wLength, strerror(errno));
			return 2;
		}
	}

	int r = libusb_control_transfer(hdev, bmRequestType, bRequest, wValue, wIndex, buf, wLength, timeout);
	if (r < 0) {
		fprintf(stderr, "error during control transfer: %s\n",
			libusb_error_name(r));
		return 3;
	}

	if (bmRequestType & 0x80) {
		/* device to host transfer */
		r = fwrite(buf, r, 1, stdout);
	}

	return 0;
}

#define USAGE(ret,progname)	FATAL(ret,"\
usage: %s %s <bmRequestType> <bRequest> <wValue> <wIndex> <wLength> [<timeout_ms>]\n\
\n\
%s\
",progname,usb_common_usage,usb_common_help)

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

	if (argc - optind < 5 || argc - optind > 6)
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
